// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the sema rules of CJMP feature.
 */

#include "MPTypeCheckerImpl.h"

#include "../Sema/InheritanceChecker/StructInheritanceChecker.h"
#include "Collector.h"
#include "Diags.h"
#include "TypeCheckUtil.h"
#include "cangjie/AST/AttributePack.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Option/Option.h"
#include "cangjie/Sema/CommonTypeAlias.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/ProfileRecorder.h"
#include <algorithm>

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;

MPTypeCheckerImpl::MPTypeCheckerImpl(const CompilerInstance& ci)
    : typeManager(*ci.typeManager), diag(ci.diag),
      compileCommon(ci.invocation.globalOptions.outputMode == GlobalOptions::OutputMode::CHIR),
      compilePlatform(ci.invocation.globalOptions.commonPartCjos.size() > 0),
      severalParents(ci.invocation.globalOptions.commonPartCjos.size() > 1)
{
}

namespace {
std::string GetTypeNameFromTy(ModalTy ty, bool forCJMPMatch, const std::unordered_map<ModalTy, unsigned>& genericIdx)
{
    CJC_ASSERT(Ty::IsTyCorrect(ty.get()));
    if (ty->IsPrimitive()) {
        return ty->String();
    } else if (ty->IsGeneric()) {
        if (forCJMPMatch) {
            auto it = genericIdx.find(ty);
            CJC_ASSERT(it != genericIdx.end());
            return "T" + std::to_string(it->second);
        } else {
            return ty->name;
        }
    } else {
        std::string str;
        for (auto it = ty->typeArgs.begin(); it != ty->typeArgs.end(); it++) {
            if (it == ty->typeArgs.begin()) {
                str += GetTypeNameFromTy(*it, forCJMPMatch, genericIdx);
            } else {
                str += "," + GetTypeNameFromTy(*it, forCJMPMatch, genericIdx);
            }
        }
        if (!ty->typeArgs.empty()) {
            str = "<" + str + ">";
        }
        if (forCJMPMatch) {
            switch (ty->kind) {
                case AST::TypeKind::TYPE_TUPLE:
                case AST::TypeKind::TYPE_FUNC:
                case AST::TypeKind::TYPE_VARRAY:
                case AST::TypeKind::TYPE_POINTER:
                case AST::TypeKind::TYPE_CSTRING: {
                    return ty->name + str;
                }
                default: {
                    auto decl = Ty::GetDeclOfTy(ty);
                    CJC_NULLPTR_CHECK(decl);
                    return decl->fullPackageName + "." + ty->name + str;
                }
            }
        } else {
            return ty->name + str;
        }
    }
}

std::string CalculatedGenericConstraintsStr(const std::vector<OwnedPtr<GenericConstraint>>& genericConstraints,
    const std::unordered_map<ModalTy, unsigned>& genericIdx)
{
    std::string ret;

    auto&& constraintCount = std::count_if(
        genericConstraints.begin(),
        genericConstraints.end(),
        [](auto&& gc) { return !gc->isImplicitlyIntroduced; }
    );

    if (constraintCount == 0) {
        return ret;
    }

    std::set<std::string> gcStrs;
    for (auto& genericConstraint : genericConstraints) {
        if (genericConstraint->isImplicitlyIntroduced) { 
            continue; 
        }
        std::set<std::string> ubStrs;
        for (auto& upperBound : genericConstraint->upperBounds) {
            ubStrs.emplace(GetTypeNameFromTy(upperBound->GetTy(), true, genericIdx));
        }
        CJC_ASSERT(!ubStrs.empty());

        auto gcStr = "[" + GetTypeNameFromTy(genericConstraint->type->GetTy(), true, genericIdx) + "<:";
        std::for_each(ubStrs.begin(), ubStrs.end(), [&gcStr](const std::string& s) { gcStr += s + ","; });
        gcStr += "]";
        gcStrs.emplace(gcStr);
    }
    CJC_ASSERT(!gcStrs.empty());

    std::for_each(gcStrs.begin(), gcStrs.end(), [&ret](const std::string& s) { ret += s; });
    return ret;
}

// Diag report
void DiagNotMatchedDecl(DiagnosticEngine &diag, const AST::Decl& decl, const std::string& p0, const std::string& p2)
{
    std::string info;
    if (decl.astKind == ASTKind::FUNC_DECL && decl.TestAttr(Attribute::CONSTRUCTOR)) {
        info = "constructor";
    } else if (decl.TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
        if (decl.outerDecl) {
            info = "enum '" + decl.outerDecl->identifier.GetRawText() + "' constructor" +
                " '" + decl.identifier.GetRawText() + "'";
        }
    } else {
        if (decl.astKind == ASTKind::VAR_WITH_PATTERN_DECL) {
            info = "variable with pattern";
        } else if (decl.astKind == ASTKind::EXTEND_DECL) {
            auto& ed = StaticCast<const ExtendDecl&>(decl);
            info = DeclKindToString(decl) + " '" +
                (ed.GetTy().IsCorrect() ? GetTypeNameFromTy(ed.extendedType->GetTy(), false, {})
                    : decl.identifier.GetRawText()) +
                "'";
        } else {
            info = DeclKindToString(decl) + " '" + decl.identifier.GetRawText() + "'";
        }
    }
    diag.DiagnoseRefactor(DiagKindRefactor::sema_not_matched, decl, p0, info, p2);
}

inline void DiagNotMatchedCommonDecl(DiagnosticEngine &diag, const AST::Decl& decl)
{
    DiagNotMatchedDecl(diag, decl, "specific", "common");
}

inline void DiagNotMatchedSpecificDecl(DiagnosticEngine &diag, const AST::Decl& decl)
{
    DiagNotMatchedDecl(diag, decl, "common", "specific");
}

inline void DiagNotMatchedSuperType(DiagnosticEngine &diag, const AST::Decl& decl)
{
    diag.DiagnoseRefactor(DiagKindRefactor::sema_specific_has_different_super_type, decl, DeclKindToString(decl));
}

// Match nominative decl.
bool MatchNominativeDecl(DiagnosticEngine &diag, Decl &commonDecl, Decl& specificDecl)
{
    if (commonDecl.astKind != specificDecl.astKind) {
        diag.DiagnoseRefactor(DiagKindRefactor::specific_has_different_kind,
            specificDecl, DeclKindToString(specificDecl), DeclKindToString(commonDecl));
        return false;
    }

    if (auto commonEnumDecl = DynamicCast<EnumDecl *>(&commonDecl)) {
        auto specificEnumDecl = DynamicCast<EnumDecl *>(&specificDecl);
        CJC_NULLPTR_CHECK(specificEnumDecl);
        if (commonEnumDecl->hasEllipsis) {
            specificDecl.EnableAttr(Attribute::COMMON_NON_EXHAUSTIVE);
        } else if (specificEnumDecl->hasEllipsis) {
            diag.DiagnoseRefactor(DiagKindRefactor::common_non_exaustive_platfrom_exaustive_mismatch,
                specificDecl, DeclKindToString(commonDecl), DeclKindToString(specificDecl));
        }
    }
    return true;
}

// Update the dependencies: common -> specific one.
void UpdateVarDependencies(const Decl& decl)
{
    for (auto& dep : decl.dependencies) {
        if (dep->specificImplementation) {
            dep = dep->specificImplementation;
        }
    }
}

// Check common instance member without initializer not match with specific one.
void Check4CommonInstanceVar(DiagnosticEngine& diag, const Decl& specificDecl)
{
    auto specificDecls = specificDecl.GetMemberDeclPtrs();
    for (auto decl : specificDecls) {
        if (decl->astKind == ASTKind::VAR_DECL && !decl->IsStaticOrGlobal() && decl->TestAttr(Attribute::COMMON)) {
            if (!decl->TestAttr(Attribute::COMMON_WITH_DEFAULT) && decl->specificImplementation == nullptr) {
                DiagNotMatchedSpecificDecl(diag, *decl);
            }
        }
    }
}

// Merge common nominative decl into specific one, do some match for fields.
void MergeCommonIntoSpecific(DiagnosticEngine& diag, Decl& commonDecl, Decl& specificDecl)
{
    CJC_ASSERT(commonDecl.TestAttr(AST::Attribute::COMMON));
    CJC_ASSERT(specificDecl.TestAttr(AST::Attribute::SPECIFIC));
    if (!MatchNominativeDecl(diag, commonDecl, specificDecl)) {
        return;
    }
    auto& commonDecls = commonDecl.GetMemberDecls();
    auto& specificDecls = specificDecl.GetMemberDecls();
    std::vector<OwnedPtr<Decl>> mergedDecls;
    mergedDecls.reserve(commonDecls.size() + specificDecls.size());
    // Common instance member vars (including common member params)
    std::unordered_map<std::string, std::size_t> commonVariablesIds;
    // General member instance member vars from member params
    std::unordered_map<std::string, std::size_t> memberParamIds;
    // Collect candidates to be matched in common decl
    for (auto& commonDeclT : commonDecls) {
        auto newDecl = std::move(commonDeclT);
        newDecl->outerDecl = &specificDecl;
        newDecl->doNotExport = false;

        auto id = mergedDecls.size();
        if (newDecl->astKind == ASTKind::VAR_DECL && !newDecl->IsStaticOrGlobal()) {
            auto varDecl = StaticCast<AST::VarDecl>(newDecl.get());
            if (varDecl->TestAttr(Attribute::COMMON)) {
                commonVariablesIds.emplace(varDecl->identifier, id);
            } else if (varDecl->isMemberParam) {
                memberParamIds.emplace(varDecl->identifier, id);
            }
        }
        mergedDecls.emplace_back(std::move(newDecl));
    }
    // Match instance member and merge into specific decl.
    for (auto& specificDeclT : specificDecls) {
        if (specificDeclT->astKind == ASTKind::VAR_DECL && !specificDeclT->IsStaticOrGlobal()) {
            auto varDecl = StaticCast<AST::VarDecl>(specificDeclT.get());
            auto id = varDecl->identifier;
            if (specificDeclT->TestAttr(Attribute::SPECIFIC)) {
                auto commonDeclIt = commonVariablesIds.find(id);
                if (commonDeclIt != commonVariablesIds.end()) {
                    // match
                    auto& commonDeclT = mergedDecls[commonDeclIt->second];
                    commonDeclT->specificImplementation = specificDeclT;
                    std::swap(specificDeclT, commonDeclT);
                    continue;
                } else {
                    DiagNotMatchedCommonDecl(diag, *varDecl);
                }
            } else if (varDecl->isMemberParam) {
                if (memberParamIds.find(id) != memberParamIds.end()) {
                    // General specific member params will merge into common if exist.
                    continue;
                }
            }
        }
        // Merge specific members.
        mergedDecls.emplace_back(std::move(specificDeclT));
    }
    std::swap(specificDecls, mergedDecls);
    // all the rest declarations need to be saved, because of, at least initializers of common
    // variables need to be analyzed.
    commonDecls.clear();
    for (auto& decl : mergedDecls) {
        if (decl) {
            commonDecls.emplace_back(std::move(decl));
        }
    }

    for (auto& decl : specificDecls) {
        UpdateVarDependencies(*decl);
    }
    // Check common member without initializer not match with specific one.
    Check4CommonInstanceVar(diag, specificDecl);

    commonDecl.doNotExport = true;
    commonDecl.specificImplementation = &specificDecl;
}
}

// PrepareTypeCheck for CJMP
void MPTypeCheckerImpl::PrepareTypeCheck4CJMP(Package& pkg)
{
    Utils::ProfileRecorder recorder("PrepareTypeCheck", "PrepareTypeCheck4CJMP");
    if (!compilePlatform) {
        return;
    }
    // specific package part
    MergeCJMPNominalsExceptExtension(pkg);
}

void MPTypeCheckerImpl::MergeCJMPNominalsExceptExtension(Package& pkg)
{
    std::unordered_map<std::string, Ptr<Decl>> matchedDecls;
    Walker walkerPackage(&pkg, [this, &matchedDecls](const Ptr<Node>& node) -> VisitAction {
        if (!node->IsDecl()) {
            return VisitAction::WALK_CHILDREN;
        }
        auto decl = StaticCast<Decl>(node);
        if (decl->IsNominalDecl() && decl->astKind != ASTKind::EXTEND_DECL) {
            if (auto it = matchedDecls.find(decl->identifier); it != matchedDecls.end()) {
                auto matchedDecl = it->second;
                bool hasGenericMismatch =
                    (decl->generic && !matchedDecl->generic) || (!decl->generic && matchedDecl->generic);
                if (hasGenericMismatch) {
                } else if (decl->TestAttr(Attribute::SPECIFIC) && matchedDecl->TestAttr(Attribute::COMMON)) {
                    MergeCommonIntoSpecific(diag, *matchedDecl, *decl);
                } else if (decl->TestAttr(Attribute::COMMON) && matchedDecl->TestAttr(Attribute::SPECIFIC)) {
                    MergeCommonIntoSpecific(diag, *decl, *matchedDecl);
                }
            } else if (decl->TestAnyAttr(Attribute::COMMON, Attribute::SPECIFIC)) {
                matchedDecls.emplace(decl->identifier, decl);
            }
        }

        return VisitAction::SKIP_CHILDREN;
    });
    walkerPackage.Walk();
}

void MPTypeCheckerImpl::PrepareTypeCheck4CJMPExtension(CompilerInstance& ci, ScopeManager& scopeManager,
    ASTContext& ctx, const std::unordered_set<Ptr<AST::ExtendDecl>>& extends)
{
    if (!compilePlatform) {
        return;
    }
    MergeCJMPExtensions(ci, scopeManager, ctx, extends);
}

namespace {
void UpdateDeclMap(DiagnosticEngine& diag, ASTContext& ctx, Ptr<ExtendDecl>& ed)
{
    std::vector<Symbol*> syms;
    std::function<VisitAction(Ptr<Node>)> collector = [&syms](auto node) {
        static std::vector<ASTKind> ignoredKinds = {ASTKind::PRIMARY_CTOR_DECL, ASTKind::VAR_WITH_PATTERN_DECL};
        CJC_ASSERT(!Utils::In(node->astKind, ignoredKinds));
        if (auto decl = DynamicCast<Decl*>(node);
            decl && !decl->TestAttr(Attribute::IS_BROKEN) && decl->symbol && decl->identifier != WILDCARD_CHAR) {
            syms.emplace_back(decl->symbol);
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(ed, collector).Walk();

    for (auto sym : syms) {
        CJC_NULLPTR_CHECK(sym);
        // macro expanded decls are not added into declMap.
        // macro invoke func can NOT be seen by developer so should not be added.
        if (sym->node->astKind == ASTKind::MACRO_EXPAND_DECL || sym->node->TestAttr(Attribute::MACRO_INVOKE_FUNC)) {
            continue;
        }

        std::string scopeName = ScopeManagerApi::GetScopeNameWithoutTail(sym->scopeName);
        auto names = std::make_pair(sym->name, scopeName);
        if (sym->astKind == ASTKind::PROP_DECL) { // Function redefinition will not be checked in this phase.
            if (auto found = ctx.GetDeclsByName(names); !found.empty()) {
                bool multiPlat =
                    (sym->node->TestAttr(Attribute::COMMON) && found.front()->TestAttr(Attribute::SPECIFIC)) ||
                    (sym->node->TestAttr(Attribute::SPECIFIC) && found.front()->TestAttr(Attribute::COMMON));
                if (!multiPlat) {
                    Sema::DiagRedefinitionWithFoundNode(diag, StaticCast<Decl>(*sym->node), *found.front());
                }
            }
        }
        ctx.AddDeclName(names, StaticCast<Decl>(*sym->node));
    }
}
} // namespace

void MPTypeCheckerImpl::MergeCJMPExtensions(CompilerInstance& ci, ScopeManager& scopeManager, ASTContext& ctx,
    const std::unordered_set<Ptr<ExtendDecl>>& extends)
{
    std::unordered_map<std::string, Ptr<ExtendDecl>> specificExtendDecls;
    std::unordered_map<std::string, std::set<Ptr<ExtendDecl>>> commonExtendDecls;
    for (auto ed : extends) {
        if (!ed->GetTy().IsCorrect() || !ed->TestAnyAttr(Attribute::COMMON, Attribute::SPECIFIC)) {
            continue;
        }

        std::unordered_map<ModalTy, unsigned> genericIdx;
        if (ed->generic) {
            for (unsigned idx = 0; idx < ed->generic->typeParameters.size(); ++idx) {
                genericIdx.emplace(ed->generic->typeParameters[idx]->GetTy(), idx);
            }
        }

        std::string key;
        // serialize the extended type.
        key += GetTypeNameFromTy(ed->extendedType->GetTy(), true, genericIdx);
        // serialize the inherited types.
        std::set<std::string> inheritedTysStr;
        for (auto& inheritedType : ed->inheritedTypes) {
            inheritedTysStr.emplace(GetTypeNameFromTy(inheritedType->GetTy(), true, genericIdx));
        }
        if (!inheritedTysStr.empty()) {
            key += "<:";
        }
        std::for_each(inheritedTysStr.begin(), inheritedTysStr.end(), [&key](const std::string& str) { key += str; });
        // serialize the generic constraints.
        if (ed->generic) {
            key += CalculatedGenericConstraintsStr(ed->generic->genericConstraints, genericIdx);
        }

        // After merging `common extend` into `specific extend`, the symbol table (scopeName) needs to be updated,
        // otherwise redefined functions cannot be detected.
        // After updating the symbol table, the `declMap` must also be updated,
        // otherwise symbols within function bodies won't find their definitions.
        if (ed->TestAttr(Attribute::COMMON)) {
            if (auto it = specificExtendDecls.find(key); it != specificExtendDecls.end()) {
                auto specificExtendDecl = it->second;
                MergeCommonIntoSpecific(diag, *ed, *specificExtendDecl);
                Collector collector(scopeManager, ci.invocation.globalOptions.enableMacroInLSP);
                collector.BuildSymbolTable(ctx, specificExtendDecl, ci.IsBuildTrie());
                UpdateDeclMap(diag, ctx, specificExtendDecl);
            }
            commonExtendDecls[key].emplace(ed);
        } else {
            if (specificExtendDecls.find(key) != specificExtendDecls.end()) {
                // A common declaration may have one or more matching specific declarations
                // in descending source sets (at most one specific per source set)
                diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_specific_has_duplicate_extensions, *ed, ed->extendedType->ToString());
                continue;
            } else if (auto it = commonExtendDecls.find(key); it != commonExtendDecls.end()) {
                for (auto ced : it->second) {
                    MergeCommonIntoSpecific(diag, *ced, *ed);
                }
                Collector collector(scopeManager, ci.invocation.globalOptions.enableMacroInLSP);
                collector.BuildSymbolTable(ctx, ed, ci.IsBuildTrie());
                UpdateDeclMap(diag, ctx, ed);
            }
            specificExtendDecls.emplace(key, ed);
        }
    }
}

namespace {
// Check whether cls has general sub class.
bool HasGeneralSubClass(const AST::ClassDecl& cls)
{
    const AST::ClassDecl* cur = &cls;
    while (!cur->subDecls.empty()) {
        cur = StaticCast<AST::ClassDecl>(*(cur->subDecls.begin()));
        if (!cur->TestAttr(Attribute::COMMON)) {
            return true;
        }
    }
    return false;
}
}

// Precheck for CJMP
void MPTypeCheckerImpl::PreCheck4CJMP(const Package& pkg)
{
    if (!compileCommon) {
        return;
    }
    // common package part
    IterateToplevelDecls(pkg, [this](auto& decl) {
        if (decl->astKind == ASTKind::CLASS_DECL) {
            // Precheck for class
            PreCheckCJMPClass(*StaticCast<ClassDecl>(decl.get()));
        }
    });
}

// Precheck for class
void MPTypeCheckerImpl::PreCheckCJMPClass(const ClassDecl& cls)
{
    // Report error for common open | abstract class without init inherited by general class in common part.
    if (cls.TestAttr(Attribute::COMMON) && cls.TestAnyAttr(Attribute::OPEN, Attribute::ABSTRACT)) {
        const auto& decls = cls.GetMemberDeclPtrs();
        bool hasInit = std::any_of(decls.cbegin(), decls.cend(), [](const Ptr<Decl>& decl) {
            return decl->TestAttr(Attribute::CONSTRUCTOR);
        });
        if (!hasInit && HasGeneralSubClass(cls)) {
            // report error: please implement the constructor explicitly for common open class 'xxx'
            diag.DiagnoseRefactor(DiagKindRefactor::sema_common_open_class_no_init, cls, cls.identifier.Val());
        }
    }
}

void MPTypeCheckerImpl::FilterOutCommonCandidatesIfSpecificExist(
    std::map<Names, std::vector<Ptr<FuncDecl>>>& candidates)
{
    for (auto& [names, funcs] : candidates) {
        bool hasSpecificCandidates = false;

        for (auto& func : funcs) {
            if (func->TestAttr(Attribute::SPECIFIC)) {
                hasSpecificCandidates = true;
                break;
            }
        }

        if (hasSpecificCandidates) {
            funcs.erase(
                std::remove_if(funcs.begin(), funcs.end(),
                    [](const Ptr<FuncDecl> decl) {
                        return decl->TestAttr(Attribute::COMMON) && decl->TestAttr(Attribute::FROM_COMMON_PART);
                    }),
                funcs.end());
        }
    }
}

// TypeCheck for CJMP
void MPTypeCheckerImpl::RemoveCommonCandidatesIfHasSpecific(std::vector<Ptr<FuncDecl>>& candidates)
{
    Utils::EraseIf(candidates, [candidates](const Ptr<FuncDecl> candidate) {
        if (candidate->TestAttr(Attribute::COMMON) && candidate->specificImplementation) {
            // erase if the specific decl also contains in the candidates vector
            return std::find(candidates.begin(), candidates.end(), candidate->specificImplementation) !=
                candidates.end();
        }
        return false;
    });
}

static ModalTy CheckFuncReturnType(ModalTy ty)
{
    CJC_ASSERT_WITH_MSG(!!ty, "Function should be already resolved");
    CJC_ASSERT_WITH_MSG(ty->IsFunc(), "Function's type should be a FuncTy");
    auto retTy = StaticCast<FuncTy>(ty.Ty())->retTy;
    CJC_ASSERT_WITH_MSG(
        retTy && !Ty::IsInitialTy(retTy.Ty()), "Function's return type should be already resolved");
    return retTy;
}

void MPTypeCheckerImpl::CheckMatchedFunctionReturnTypes(FuncDecl& specificFunc, FuncDecl& commonFunc)
{
    auto commonType = commonFunc.funcBody->GetTy();
    auto specificType = specificFunc.funcBody->GetTy();

    auto commonRetTy = CheckFuncReturnType(commonType);
    auto specificRetTy = CheckFuncReturnType(specificType);

    if (commonRetTy == specificRetTy) return;
    if (typeManager.IsSubtype(specificRetTy, commonRetTy)) return;

    // the subtype check will handle most of the cases, however in the case of generics, types must 
    // be substituted properly before checking for subtype that is properly done in MatchCJMPFunction 
    // that we use as a fallback

    // we need to erase the pointer temporarily to avoid the multiple implementations error
    commonFunc.specificImplementation = nullptr;
    if (!MatchCJMPFunction(specificFunc, commonFunc)) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_return_type_incompatible,
            specificFunc,
            specificFunc.identifier);
    }
    commonFunc.specificImplementation = &specificFunc;
}

void MPTypeCheckerImpl::CheckMatchedVariableTypes(AST::VarDecl& specificVar, AST::VarDecl& commonVar)
{
    CJC_ASSERT_WITH_MSG(commonVar.GetTy() && !Ty::IsInitialTy(commonVar.DataTy()),
        "Common variable type must be already resolved");

    if (!specificVar.GetTy() || Ty::IsInitialTy(specificVar.DataTy())) {
        // this should already be reported as parse_expected_one_of_type_or_initializer
        // here we skip it
        return;
    }

    if (commonVar.GetTy() == specificVar.GetTy())
        return;
    if (typeManager.IsTyEqual(specificVar.GetTy(), commonVar.GetTy()))
        return;

    // the type equality check will handle most of the cases except for the generics case when types 
    // need to be substituted in order to compare them that is handled in MatchCJMPVar
    //  that is fallback here

    // member variables are paired during the merge, global and statics during the Match function
    // unlike for functions, we should NOT erase the specific pointer
    MatchCJMPVar(specificVar, commonVar); // the diagnostic is reported by the function
}

static bool IsCommon(const AST::Node& decl)
{
    return decl.TestAttr(Attribute::COMMON);
}

static bool IsEnumConstructor(const AST::Node& decl)
{
    return decl.TestAttr(Attribute::ENUM_CONSTRUCTOR);
}

void MPTypeCheckerImpl::CheckReturnAndVariableTypes(AST::Package& pkg)
{
    std::function<VisitAction(Ptr<Node>)> visitor = [this](const Ptr<Node>& node) {
        if (node->astKind == ASTKind::FUNC_DECL) {
            if (!IsCommon(*node) || IsEnumConstructor(*node)) {
                return VisitAction::SKIP_CHILDREN;
            }
            auto common = StaticAs<ASTKind::FUNC_DECL>(node);
            auto specific = common->specificImplementation;
            if (specific) {
                CJC_ASSERT_WITH_MSG(specific->astKind == AST::ASTKind::FUNC_DECL,
                    "Common function can be only matched to a function but got another kind");
                auto& commonFunc = *common;
                auto& specificFunc = *StaticCast<FuncDecl>(specific);

                CheckMatchedFunctionReturnTypes(specificFunc, commonFunc);
            }
            return VisitAction::SKIP_CHILDREN;
        } else if (node->astKind == ASTKind::VAR_DECL) {
            if (!IsCommon(*node) || IsEnumConstructor(*node)) {
                return VisitAction::SKIP_CHILDREN;
            }
            auto common = StaticAs<ASTKind::VAR_DECL>(node);
            auto specific = common->specificImplementation;
            if (specific) {
                CJC_ASSERT_WITH_MSG(specific->astKind == AST::ASTKind::VAR_DECL,
                    "Common variable can be only matched to a variable but got another kind");
                auto& commonVar = *common;
                auto& specificVar = *StaticCast<VarDecl>(specific);

                CheckMatchedVariableTypes(specificVar, commonVar);
            } else if (!common->TestAttr(Attribute::INITIALIZED) && common->TestAttr(Attribute::FROM_COMMON_PART)) {
                // this has been postponed, we need this to handle the case when a common var has no matched specified
                // but it does't fail because there is an initializer for it
                DiagNotMatchedSpecificDecl(diag, *common);
            }
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker(&pkg, visitor);
    walker.Walk();
}

void MPTypeCheckerImpl::ValidateMatchedAnnotationsAndModifiers(AST::Package& pkg)
{
    std::function<VisitAction(Ptr<Node>)> visitor = [this](const Ptr<Node> &node) {
        if (node->IsDecl()) {
            if (!IsCommon(*node)) { return VisitAction::WALK_CHILDREN; }
            auto commonDecl = StaticCast<Decl>(node);
            if (!commonDecl->specificImplementation) {
                return VisitAction::WALK_CHILDREN;
            }

            MatchCJMPDeclAnnotations(*commonDecl, *commonDecl->specificImplementation);
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker(&pkg, visitor);
    walker.Walk();
}

namespace {
// Collect common or specific decl.
void CollectDecl(
    Ptr<Decl> decl, std::vector<Ptr<Decl>>& commonDecls, std::vector<Ptr<Decl>>& specificDecls)
{
    if (decl->TestAttr(Attribute::COMMON)) {
        commonDecls.emplace_back(decl);
    } else if (decl->TestAttr(Attribute::SPECIFIC)) {
        specificDecls.emplace_back(decl);
    }
}

// Collect common and specific decls.
void CollectCJMPDecls(Package& pkg, std::vector<Ptr<Decl>>& commonDecls, std::vector<Ptr<Decl>>& specificDecls)
{
    std::function<VisitAction(Ptr<Node>)> visitor = [&commonDecls, &specificDecls](const Ptr<Node> &node) {
        if (node->IsDecl() && node->astKind != ASTKind::PRIMARY_CTOR_DECL) {
            CollectDecl(StaticAs<ASTKind::DECL>(node), commonDecls, specificDecls);
        }
        if (node->astKind == ASTKind::PACKAGE || node->astKind == ASTKind::FILE ||
            node->IsNominalDecl() || node->IsNominalDeclBody()) {
            return VisitAction::WALK_CHILDREN;
        }
        return VisitAction::SKIP_CHILDREN;
    };
    Walker walker(&pkg, visitor);
    walker.Walk();
}

// Check whether the common decl must be matched with paltform decl.
bool MustMatchWithPlatform(const Decl& decl, const bool compilingCommon)
{
    CJC_ASSERT(decl.TestAttr(Attribute::COMMON));
    
    if (compilingCommon) {
        // not compiling to binary, only produce CHIR, so it can be matched later.
        return false;
    }
    if (decl.specificImplementation) {
        return false;
    }
    // var/func with default implementation
    if (decl.TestAttr(Attribute::COMMON_WITH_DEFAULT)) {
        return false;
    }
    // No match is required for enum constructors when the outer enum is marked as
    // COMMON_WITH_DEFAULT and has no specific implementation
    if (decl.TestAttr(Attribute::ENUM_CONSTRUCTOR) && decl.outerDecl &&
        decl.outerDecl->TestAttr(AST::Attribute::COMMON_WITH_DEFAULT) && !decl.outerDecl->specificImplementation) {
        return false;
    }

    // common member in interface allow no specific member, maybe use abstract attr.
    if (decl.outerDecl && decl.outerDecl->astKind == ASTKind::INTERFACE_DECL) {
        return false;
    }
    // var already initialized
    if (decl.astKind == ASTKind::VAR_DECL && decl.TestAttr(Attribute::INITIALIZED)) {
        return false;
    }
    // local var member
    if (decl.astKind == ASTKind::VAR_DECL && !decl.IsStaticOrGlobal() &&
        decl.outerDecl->astKind != ASTKind::ENUM_DECL) {
        return false;
    }
    return true;
}

/**
 * True when `common` provides a default body (COMMON_WITH_DEFAULT, not
 * ABSTRACT) but the matching `specific` member is declared ABSTRACT (no body).
 * In that case `sema_specific_member_must_have_implementation`
 * ("the member %s must have body in 'specific' %s") is reported instead of
 * the misleading modifier-mismatch diagnostic.
 */
bool NeedToReportMissingBody(const Decl& common, const Decl& specific)
{
    if (common.outerDecl && common.TestAttr(Attribute::COMMON_WITH_DEFAULT) && !common.TestAttr(Attribute::ABSTRACT) &&
        specific.TestAttr(Attribute::ABSTRACT)) {
        return true;
    }

    return false;
}
} // namespace

// PostTypeCheck for CJMP
bool MPTypeCheckerImpl::MatchCJMPDeclAttrs(
    const std::vector<Attribute>& attrs, Decl& common, Decl& specific) const
{
    for (auto attr : attrs) {
        if (common.TestAttr(attr) != specific.TestAttr(attr)) {
            if ((attr == Attribute::ABSTRACT || attr == Attribute::OPEN)) {
                // Error `sema_specific_member_must_have_implementation` should be reported
                // If common have body, but specific not. Diagnostic about wrong modifiers is confusing.
                if (NeedToReportMissingBody(common, specific)) {
                    continue;
                }
                if (specific.TestAttr(Attribute::ABSTRACT) && common.TestAttr(Attribute::OPEN) &&
                    common.IsFuncOrProp()) {
                    auto kindStr = common.astKind == ASTKind::FUNC_DECL ? "function" : "property";
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_open_abstract_specific_can_not_replace_open_common,
                        specific, kindStr, kindStr);
                }
                // ABSTRACT member can be replaced with OPEN
                bool funcOrProp = common.astKind == ASTKind::FUNC_DECL || common.astKind == ASTKind::PROP_DECL;
                if (funcOrProp && common.TestAttr(Attribute::ABSTRACT) && specific.TestAttr(Attribute::OPEN)) {
                    continue;
                }
                // Same as previous check, however static functions has no OPEN modifier
                if (common.TestAttr(Attribute::ABSTRACT) &&
                    common.TestAttr(Attribute::STATIC) && specific.TestAttr(Attribute::STATIC)) {
                    continue;
                }
            }
            if (common.astKind == ASTKind::PROP_DECL && attr == Attribute::MUT) {
                if (common.TestAttr(attr)) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_property_have_same_declaration_in_inherit_mut,
                        specific, specific.identifier.Val());
                    specific.EnableAttr(Attribute::HAS_BROKEN);
                } else {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_property_have_same_declaration_in_inherit_immut,
                        common, common.identifier.Val());
                    common.EnableAttr(Attribute::HAS_BROKEN);
                }
            } else if (common.astKind == ASTKind::CLASS_DECL && (attr == Attribute::SEALED || attr == Attribute::OPEN) &&
                // Keep silent due to overloaded common funcs.
                // Allow specific sealed abstract when common is abstract
                specific.TestAttr(Attribute::SEALED) && common.TestAttr(Attribute::ABSTRACT)) {
                continue;
            } else {
                diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_specific_has_different_modifier, specific, DeclKindToString(specific));
            }
            return false;
        }
    }
    return true;
}



void MPTypeCheckerImpl::CheckCommonSpecificGenericMatch(const AST::Decl& specificDecl, const AST::Decl& commonDecl)
{
    // check generic constraints
    auto parentBounds = GetAllGenericUpperBounds(typeManager, commonDecl);
    auto childBounds = GetAllGenericUpperBounds(typeManager, specificDecl);

    CheckGenericTypeBoundsMapped(commonDecl, specificDecl, parentBounds, childBounds, diag, typeManager);
}

bool MPTypeCheckerImpl::MatchCommonNominalDeclWithSpecific(InheritableDecl& commonDecl)
{
    auto specificDecl = commonDecl.specificImplementation;
    if (!specificDecl) {
        // It's okay if common nominative declaration have no platform,
        // either all member are implemented in common one
        // or it's not yet compiling into binary, so it can be matched at later phases.
        return true;
    }
    // Match attributes (modifiers).
    std::vector<Attribute> matchedAttr = {
        Attribute::ABSTRACT, Attribute::PUBLIC, Attribute::OPEN, Attribute::PROTECTED, Attribute::C, Attribute::SEALED};
    if (!MatchCJMPDeclAttrs(matchedAttr, commonDecl, *specificDecl)) {
        return false;
    }
    // Match super types.
    std::set<Ptr<InterfaceTy>> comSupInters;
    auto platSupInters = StaticCast<InheritableDecl>(specificDecl)->GetSuperInterfaceTys();
    TypeSubst genericTyMap;
    MapCJMPGenericTypeArgs(genericTyMap, commonDecl, *specificDecl);
    if (!genericTyMap.empty()) {
        for (auto superInterface : commonDecl.GetSuperInterfaceTys()) {
            auto updatedInterfaceType = typeManager.GetInstantiatedTy(superInterface, genericTyMap);
            comSupInters.emplace(StaticCast<InterfaceTy*>(updatedInterfaceType));
        }
    } else {
        comSupInters = commonDecl.GetSuperInterfaceTys();
    }
    if (comSupInters.size() != platSupInters.size()) {
        DiagNotMatchedSuperType(diag, *specificDecl);
        return false;
    }
    bool match = false;
    for (auto& comSupInter : comSupInters) {
        for (auto& platSupInter : platSupInters) {
            if (typeManager.IsTyEqual(DataTy{comSupInter}, DataTy{platSupInter})) {
                match = true;
                break;
            }
        }
        if (!match) {
            DiagNotMatchedSuperType(diag, *specificDecl);
            return false;
        }
    }
    // Match super class if need.
    if (commonDecl.astKind == ASTKind::CLASS_DECL) {
        auto comSupClass = StaticCast<ClassDecl>(&commonDecl)->GetSuperClassDecl();
        auto platSupIClass = StaticCast<ClassDecl>(specificDecl)->GetSuperClassDecl();
        if (!typeManager.IsTyEqual(comSupClass->GetTy(), platSupIClass->GetTy())) {
            DiagNotMatchedSuperType(diag, *specificDecl);
            return false;
        }
    }

    CheckCommonSpecificGenericMatch(*specificDecl, commonDecl);

    return true;
}

namespace {
/**
 * Whether the function node is a constructor declaration
 */
bool IsConstructor(const FuncDecl& func)
{
    return func.TestAttr(Attribute::CONSTRUCTOR);
}

/**
 * Whether the function node is a primary constructor declaration
 */
bool IsPrimaryConstructor(const FuncDecl& func)
{
    return func.TestAttr(Attribute::PRIMARY_CONSTRUCTOR);
}
}

bool MPTypeCheckerImpl::IsCJMPDeclMatchable(Decl& lhsDecl, Decl& rhsDecl) const
{
    bool isLeftCommon = lhsDecl.TestAttr(Attribute::COMMON);
    Decl& commonDecl = isLeftCommon ? lhsDecl : rhsDecl;
    Decl& specificDecl = isLeftCommon ? rhsDecl : lhsDecl;
    if (commonDecl.identifier.GetRawText() != specificDecl.identifier.GetRawText()) {
        return false;
    }
    if (specificDecl.IsMemberDecl() != commonDecl.IsMemberDecl()) {
        return false;
    }
    if (specificDecl.IsMemberDecl()) {
        CJC_NULLPTR_CHECK(specificDecl.outerDecl);
        CJC_NULLPTR_CHECK(commonDecl.outerDecl);
        if (specificDecl.outerDecl->rawMangleName != commonDecl.outerDecl->rawMangleName) {
            return false;
        }
    }

    if (lhsDecl.GetGenericsCount() != rhsDecl.GetGenericsCount()) {
        return false;
    }

    return true;
}

bool MPTypeCheckerImpl::TrySetSpecificImpl(Decl& specificDecl, Decl& commonDecl, const std::string& kind)
{
    if (commonDecl.specificImplementation) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_multiple_common_implementations, commonDecl, kind);
        return false;
    }
    // common with default but specific without default
    if (NeedToReportMissingBody(commonDecl, specificDecl)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_specific_member_must_have_implementation,
            specificDecl, specificDecl.identifier.Val(), commonDecl.outerDecl->identifier.Val());
        return false;
    }
    commonDecl.specificImplementation = &specificDecl;
    commonDecl.doNotExport = true;
    return true;
}

bool MPTypeCheckerImpl::MatchCJMPFunction(FuncDecl& specificFunc, FuncDecl& commonFunc)
{
    if (!IsCJMPDeclMatchable(specificFunc, commonFunc)) {
        return false;
    }

    if (!MatchCJMPFunctionParameters(specificFunc, commonFunc)) {
        return false;
    }

    // For init or primary constructor
    if (IsConstructor(specificFunc) || IsConstructor(commonFunc)) {
        if (!IsPrimaryConstructor(specificFunc) && IsPrimaryConstructor(commonFunc)) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_specific_init_common_primary_constructor, commonFunc);
            return false;
        }
    }

    // NOTE: function return types, annotations, parameter kinds are not part of the matching logic
    // and handled at post type check validation

    return TrySetSpecificImpl(specificFunc, commonFunc, "function");
}

bool MPTypeCheckerImpl::MatchCJMPProp(PropDecl& specificProp, PropDecl& commonProp)
{
    if (!IsCJMPDeclMatchable(specificProp, commonProp)) {
        return false;
    }
    if (!typeManager.IsTyEqual(specificProp.GetTy(), commonProp.GetTy())) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_specific_has_different_type, specificProp, "property");
    }
    bool ret = TrySetSpecificImpl(specificProp, commonProp, "property " + specificProp.identifier);
    if (ret && !specificProp.getters.empty() && !commonProp.getters.empty()) {
        ret &= TrySetSpecificImpl(*specificProp.getters[0], *commonProp.getters[0],
            "property getter " + specificProp.identifier);
    }
    if (ret && !specificProp.setters.empty() && !commonProp.setters.empty()) {
        ret &= TrySetSpecificImpl(*specificProp.setters[0], *commonProp.setters[0],
            "property setter " + specificProp.identifier);
    }

    return ret;
}

bool MPTypeCheckerImpl::MatchEnumFuncTypes(const FuncDecl& specific, const FuncDecl& common)
{
    if (typeManager.IsFuncDeclEqualType(specific, common)) {
        // if types are equal
        return true;
    }

    TypeSubst genericTyMap;
    MapCJMPGenericTypeArgs(genericTyMap, *common.outerDecl, *specific.outerDecl);
    if (genericTyMap.empty()) {
        return false;
    }

    auto mappedCommonType = typeManager.GetInstantiatedTy(common.GetTy(), genericTyMap);
    return typeManager.IsTyEqual(mappedCommonType, specific.GetTy());
}

bool MPTypeCheckerImpl::MatchCJMPEnumConstructor(Decl& specificDecl, Decl& commonDecl)
{
    if (!IsCJMPDeclMatchable(specificDecl, commonDecl)) {
        return false;
    }

    // identifiers are already checked
    if (specificDecl.astKind == ASTKind::FUNC_DECL) { // enum constructrs with values
        auto& specificFunc = StaticCast<FuncDecl>(specificDecl);
        auto& commonFunc = StaticCast<FuncDecl>(commonDecl);
        if (!MatchEnumFuncTypes(specificFunc, commonFunc)) {
            return false;
        }
    }

    auto enumName = specificDecl.outerDecl->identifier.GetRawText();
    return TrySetSpecificImpl(specificDecl, commonDecl, "enum '" + enumName + "' constructor");
}

bool MPTypeCheckerImpl::MatchCJMPVar(VarDecl& specificVar, VarDecl& commonVar)
{
    if (!IsCJMPDeclMatchable(specificVar, commonVar)) {
        return false;
    }
    auto cType = commonVar.GetTy();
    if (specificVar.IsMemberDecl()) {
        TypeSubst genericTyMapForNominals;
        MapCJMPGenericTypeArgs(genericTyMapForNominals, *commonVar.outerDecl, *specificVar.outerDecl);
        if (!genericTyMapForNominals.empty()) {
            cType = typeManager.GetInstantiatedTy(cType, genericTyMapForNominals);
        }
    }
    auto pType = specificVar.GetTy();
    if (!typeManager.IsTyEqual(cType, pType) && !Ty::IsInitialTy(Ptr<const Ty>(pType.Ty()))) {
        // if the specific type is initial then the check will be restarted after the type get resolved
        // so we can suppress the check for now
        auto specificKind = specificVar.isVar ? "var" : "let";
        diag.DiagnoseRefactor(DiagKindRefactor::sema_specific_has_different_type, specificVar, specificKind);
    }
    if (specificVar.isVar != commonVar.isVar) {
        auto specificKind = specificVar.isVar ? "var" : "let";
        auto commonKind = commonVar.isVar ? "var" : "let";
        diag.DiagnoseRefactor(DiagKindRefactor::sema_specific_var_not_match_let, specificVar, specificKind, commonKind);
    }
    if (specificVar.IsStaticOrGlobal()) {
        commonVar.specificImplementation = &specificVar;
        commonVar.doNotExport = true;
    } else if (commonVar.TestAttr(Attribute::STATIC) == specificVar.TestAttr(Attribute::STATIC)) {
        // Instance variables must already be matched unless we have modifiers mismatch
        // that will be reported later
        CJC_ASSERT(commonVar.specificImplementation == &specificVar);
    } else {
        // we assign it even despite the mismatch
        // and let the generic validation report it later
        commonVar.specificImplementation = &specificVar;
    }
    return true;
}

bool MPTypeCheckerImpl::TryMatchVarWithPatternWithVarDecls(
    AST::VarWithPatternDecl& specificDecl, const std::vector<Ptr<AST::Decl>>& commonDecls)
{
    if (specificDecl.irrefutablePattern->astKind != ASTKind::TUPLE_PATTERN) {
        return false;
    }
    auto specificTuplePattern = StaticCast<TuplePattern>(specificDecl.irrefutablePattern.get());

    bool matchedAll = true;
    for (auto& pattern : specificTuplePattern->patterns) {
        if (pattern->astKind != ASTKind::VAR_PATTERN) {
            matchedAll = false;
            break;
        }

        auto patternDecl = StaticCast<VarPattern>(pattern.get());
        if (!MatchSpecificDeclWithCommonDecls(*patternDecl->varDecl, commonDecls)) {
            matchedAll = false;
        }
    }

    return matchedAll;
}

// this is never invoked for nominal decls
bool MPTypeCheckerImpl::MatchSpecificDeclWithCommonDecls(
    AST::Decl& specificDecl, const std::vector<Ptr<AST::Decl>>& commonDecls)
{
    bool matched = false;
    bool isEnumConstructor = specificDecl.TestAttr(Attribute::ENUM_CONSTRUCTOR);
    auto kind = specificDecl.astKind;
    for (auto& commonDecl : commonDecls) {
        // In case of several parent source sets there can be more then one matched common decl
        if (matched && !severalParents) {
            break;
        }
        if (commonDecl->astKind != kind) {
            continue;
        }
        if (isEnumConstructor && commonDecl->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
            matched = MatchCJMPEnumConstructor(specificDecl, *commonDecl) || matched;
        } else if (kind == ASTKind::FUNC_DECL) {
            matched = MatchCJMPFunction(*StaticCast<FuncDecl>(&specificDecl), *StaticCast<FuncDecl>(commonDecl)) ||
                matched;
        } else if (kind == ASTKind::PROP_DECL) {
            matched = MatchCJMPProp(*StaticCast<PropDecl>(&specificDecl), *StaticCast<PropDecl>(commonDecl)) ||
                matched;
        } else if (kind == ASTKind::VAR_DECL) {
            matched = MatchCJMPVar(*StaticCast<VarDecl>(&specificDecl), *StaticCast<VarDecl>(commonDecl)) ||
                matched;
        }
    }

    // VarWithPattern can match several decls from common part
    if (kind == ASTKind::VAR_WITH_PATTERN_DECL) {
        matched = TryMatchVarWithPatternWithVarDecls(*StaticCast<VarWithPatternDecl>(&specificDecl), commonDecls);
    }

    // For enum constructor
    if (!matched) {
        if (specificDecl.outerDecl && specificDecl.outerDecl->TestAttr(Attribute::COMMON_NON_EXHAUSTIVE)) {
            return false;
        }
        DiagNotMatchedCommonDecl(diag, specificDecl);
    }

    return matched;
}

// When there are several common extend without interfaces declared,
// if there are same-named private functions declared, a clash is reported
void MPTypeCheckerImpl::CheckCommonExtensions(std::vector<Ptr<Decl>>& commonDecls)
{
    std::map<ModalTy, std::set<std::string>> privateFunctionsOfExtensions;
    for (auto decl : commonDecls) {
        if (decl->astKind != ASTKind::EXTEND_DECL) {
            continue;
        }

        auto extendDecl = StaticCast<ExtendDecl>(decl);
        if (!extendDecl->GetSuperInterfaceTys().empty()) {
            continue;
        }

        auto& privateFunctions = privateFunctionsOfExtensions[extendDecl->extendedType->GetTy()];
        for (auto& memberDecl : extendDecl->GetMemberDecls()) {
            if (!memberDecl->IsFuncOrProp() || !memberDecl->TestAttr(Attribute::PRIVATE)) {
                continue;
            }
            if (privateFunctions.find(memberDecl->rawMangleName) != privateFunctions.end()) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_common_direct_extension_has_duplicate_private_members,
                    *memberDecl, extendDecl->extendedType->ToString(), memberDecl->IsFunc() ? "function" : "property",
                    memberDecl->identifier.GetRawText());
            } else if (memberDecl->TestAttr(Attribute::COMMON)) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_common_direct_extension_has_common_private_members,
                    *memberDecl, memberDecl->IsFunc() ? "function" : "property",
                    memberDecl->identifier.GetRawText());
            } else {
                privateFunctions.emplace(memberDecl->rawMangleName);
            }
        }
    }
}

void MPTypeCheckerImpl::MatchCJMPDecls(std::vector<Ptr<Decl>>& commonDecls, std::vector<Ptr<Decl>>& specificDecls)
{
    for (auto& specificDecl : specificDecls) {
        CJC_ASSERT(specificDecl->TestAttr(Attribute::SPECIFIC) && !specificDecl->TestAttr(Attribute::COMMON));
        if (specificDecl->TestAttr(Attribute::IS_BROKEN) || specificDecl->IsNominalDecl()) {
            continue;
        }
        MatchSpecificDeclWithCommonDecls(*specificDecl, commonDecls);
    }
    std::unordered_set<Decl*> matchedIds;
    // Report error for common decl having no matched specific decl.
    for (auto& decl : commonDecls) {
        if (decl->IsNominalDecl() && MatchCommonNominalDeclWithSpecific(*StaticCast<InheritableDecl>(decl))) {
            if (decl->specificImplementation) {
                matchedIds.insert(decl->specificImplementation.get());
            }
        }
        if (!MustMatchWithPlatform(*decl, compileCommon)) {
            continue;
        }
        if (decl->astKind == ASTKind::VAR_DECL && !decl->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
            continue; // postpone the check
        }
        DiagNotMatchedSpecificDecl(diag, *decl);
    }
    // Report error for specific nominal decl having no matched common decl.
    for (auto& decl : specificDecls) {
        if (decl->IsNominalDecl() && matchedIds.find(decl.get()) == matchedIds.end()) {
            DiagNotMatchedCommonDecl(diag, *decl);
        }
        if (decl->IsNominalDecl()) {
            CheckAbstractClassMembers(*StaticCast<InheritableDecl>(decl));
        }
    }
    for (auto& commonDecl : commonDecls) {
        if (commonDecl->IsNominalDecl()) {
            continue; // this is already handled in the loop above
        }
        auto specificDecl = commonDecl->specificImplementation;
        if (specificDecl == nullptr) {
            continue;
        }
            // need check Attribute::ABSTRACT for abstract class?
        std::vector<Attribute> matchedAttrs = { Attribute::STATIC, Attribute::MUT, Attribute::PRIVATE, Attribute::PUBLIC,
            Attribute::PROTECTED, Attribute::FOREIGN, Attribute::UNSAFE, Attribute::OPEN,
            Attribute::ABSTRACT};
        MatchCJMPDeclAttrs(matchedAttrs, *commonDecl, *specificDecl);
    }
}

void MPTypeCheckerImpl::CheckAbstractClassMembers(const InheritableDecl& specificDecl)
{
    if (!specificDecl.TestAttr(Attribute::SPECIFIC) || !specificDecl.TestAttr(Attribute::ABSTRACT)) {
        return;
    }

    if (specificDecl.astKind != ASTKind::CLASS_DECL) {
        return;
    }

    const auto& classDecl = StaticCast<const ClassDecl&>(specificDecl);

    for (const auto& memberDecl : classDecl.GetMemberDeclPtrs()) {
        // Check if member is a function or property with abstract modifier
        // and is NOT a specific-specific member or from common part
        if (memberDecl->TestAttr(Attribute::ABSTRACT) && !memberDecl->TestAttr(Attribute::SPECIFIC) &&
            !memberDecl->TestAttr(Attribute::FROM_COMMON_PART) &&
            (memberDecl->astKind == ASTKind::FUNC_DECL || memberDecl->astKind == ASTKind::PROP_DECL)) {

            // Report error: cannot add abstract members to specific abstract class
            auto memberKind = memberDecl->astKind == ASTKind::FUNC_DECL ? "function" : "property";
            diag.DiagnoseRefactor(DiagKindRefactor::sema_cjmp_non_specific_abstract_member_in_specific_class,
                *memberDecl, specificDecl.identifier.Val(), memberKind);
        }
    }
}

void MPTypeCheckerImpl::MatchSpecificWithCommon(Package& pkg)
{
    std::vector<Ptr<Decl>> commonDecls;
    std::vector<Ptr<Decl>> specificDecls;
    CollectCJMPDecls(pkg, commonDecls, specificDecls);
    if (compileCommon) { // check common extensions
        CheckCommonExtensions(commonDecls);
    }
    // This does not contradict to previous check.
    // A source code can be relatively platform and common at the same time.
    if (compilePlatform) { // match common decls and platform decls
        MatchCJMPDecls(commonDecls, specificDecls);
    }

    // both propagations are performed after the matching
    // and this is important, otherwise propagation errors would lead to
    // sema_not_matched errors that is usually confusing
    // also we almost never match declarations based on annotations
    // or default arguments so reporting sema_not_matching is misleading
    for (auto common : commonDecls) {
        auto specific = common->specificImplementation;
        if (specific) {
            PropagateCJMPDeclAnnotations(*common, *specific);
            if (auto commonFunc = DynamicCast<FuncDecl>(common); commonFunc) {
                auto specificFunc = StaticCast<FuncDecl>(specific);
                PropagateDefaultArguments(*commonFunc, *specificFunc);
            }
        }
    }
}

// Maps type parameters between common and specific declarations.
void MPTypeCheckerImpl::MapCJMPGenericTypeArgs(
    TypeSubst& genericTyMap, const AST::Decl& commonDecl, const AST::Decl& specificDecl)
{
    if (!(commonDecl.TestAttr(Attribute::GENERIC) && specificDecl.TestAttr(Attribute::GENERIC))) {
        return;
    }
    // 1. Handle nominalDecl type parameters
    if (commonDecl.IsNominalDecl() && commonDecl.GetTy() && !commonDecl.GetTy()->typeArgs.empty()) {
        for (size_t i = 0; i < commonDecl.GetTy()->typeArgs.size(); i++) {
            auto commonType = commonDecl.GetTy()->typeArgs[i];
            if (commonType->IsGeneric()) {
                Ptr<TyVar> commonGeneric = RawStaticCast<TyVar*>(commonType.Ty());
                genericTyMap[commonGeneric] = specificDecl.GetTy()->typeArgs[i].Ty();
            }
        }
    }

    CheckCommonSpecificGenericMatch(specificDecl, commonDecl);
    if (commonDecl.GetGenericsCount() != specificDecl.GetGenericsCount()) {
        return;
    }
    auto mapping = typeManager.GenerateGenericMappingFromGeneric(commonDecl, specificDecl);
    genericTyMap.insert(mapping.begin(), mapping.end());
}

void MPTypeCheckerImpl::UpdateGenericTyInMemberFromCommon(TypeSubst& genericTyMap, Ptr<AST::Decl>& member)
{
    Walker walker(member, [this, &genericTyMap](Ptr<Node> node) -> VisitAction {
        if (node->GetTy()) {
            node->SetTy(typeManager.GetInstantiatedTy(node->GetTy(), genericTyMap));
        }
        if (auto ref = DynamicCast<NameReferenceExpr*>(node); ref) {
            for (auto& instTy : ref->instTys) {
                instTy = typeManager.GetInstantiatedTy(instTy, genericTyMap);
            }
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
}

void MPTypeCheckerImpl::UpdateSpecificMemberGenericTy(
    ASTContext& ctx, const std::function<std::vector<AST::Symbol*>(ASTContext&, ASTKind)>& getSymsFunc)
{
    // Collect all relevant symbol types
    auto syms = getSymsFunc(ctx, ASTKind::CLASS_DECL);
    auto interfaceSyms = getSymsFunc(ctx, ASTKind::INTERFACE_DECL);
    syms.insert(syms.end(), interfaceSyms.begin(), interfaceSyms.end());
    auto structSyms = getSymsFunc(ctx, ASTKind::STRUCT_DECL);
    syms.insert(syms.end(), structSyms.begin(), structSyms.end());
    auto enumSyms = getSymsFunc(ctx, ASTKind::ENUM_DECL);
    syms.insert(syms.end(), enumSyms.begin(), enumSyms.end());
    auto extendSyms = getSymsFunc(ctx, ASTKind::EXTEND_DECL);
    syms.insert(syms.end(), extendSyms.begin(), extendSyms.end());

    // Process common declarations and update generic types in specific members
    for (auto sym : syms) {
        CJC_ASSERT(sym && sym->node);
        auto decl = StaticCast<Decl*>(sym->node);
        if (decl->TestAttr(Attribute::COMMON) && decl->TestAttr(Attribute::GENERIC)) {
            auto specificDecl = decl->specificImplementation;
            if (specificDecl) {
                TypeSubst genericTyMap;
                MapCJMPGenericTypeArgs(genericTyMap, *decl, *specificDecl);
                for (auto& member : specificDecl->GetMemberDecls()) {
                    if (member->TestAttr(Attribute::FROM_COMMON_PART)) {
                        auto ptr = member.get();
                        UpdateGenericTyInMemberFromCommon(genericTyMap, ptr);
                    }
                }
            }
        }
    }
}


void MPTypeCheckerImpl::GetInheritedTypesWithSpecificImpl(
    std::vector<OwnedPtr<AST::Type>>& inheritedTypes, bool hasSpecificImpl, bool compilePlatform)
{
    if (!compilePlatform || hasSpecificImpl) {
        return;
    }

    for (auto& inhType : inheritedTypes) {
        auto decl = Ty::GetDeclOfTy(inhType->GetTy());
        if (decl && decl->specificImplementation) {
            inhType->SetTy(decl->specificImplementation->GetTy());
        }
    }
}
