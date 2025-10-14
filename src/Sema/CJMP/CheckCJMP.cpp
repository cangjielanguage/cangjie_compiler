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

#include "TypeCheckUtil.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/AST/Clone.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;

MPTypeCheckerImpl::MPTypeCheckerImpl(const CompilerInstance& ci)
    : typeManager(*ci.typeManager), diag(ci.diag),
      compileCommon(ci.invocation.globalOptions.outputMode == GlobalOptions::OutputMode::CHIR),
      compilePlatform(ci.invocation.globalOptions.commonPartCjo != std::nullopt)
{
}

namespace {
std::string GetExtendedTypeName(const ExtendDecl& ed)
{
    auto& extendedType = ed.extendedType;
    if (Ty::IsTyCorrect(extendedType->ty.get())) {
        return extendedType->ty->IsPrimitive() ? extendedType->ty->String() : extendedType->ty->name;
    } else {
        return extendedType->ToString();
    }
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
            info = "extend '" + GetExtendedTypeName(StaticCast<const ExtendDecl&>(decl)) + "'";
        } else {
            info = DeclKindToString(decl) + " '" + decl.identifier.GetRawText() + "'";
        }
    }
    diag.DiagnoseRefactor(DiagKindRefactor::sema_not_matched, decl, p0, info, p2);
}

inline void DiagNotMatchedCommonDecl(DiagnosticEngine &diag, const AST::Decl& decl)
{
    DiagNotMatchedDecl(diag, decl, "platform", "common");
}

inline void DiagNotMatchedPlatformDecl(DiagnosticEngine &diag, const AST::Decl& decl)
{
    DiagNotMatchedDecl(diag, decl, "common", "platform");
}

inline void DiagNotMatchedSuperType(DiagnosticEngine &diag, const AST::Decl& decl)
{
    diag.DiagnoseRefactor(DiagKindRefactor::sema_platform_has_different_super_type, decl, DeclKindToString(decl));
}

// Match nominative decl.
bool MatchNominativeDecl(DiagnosticEngine &diag, Decl &commonDecl, Decl& platformDecl)
{
    if (commonDecl.astKind != platformDecl.astKind) {
        diag.DiagnoseRefactor(DiagKindRefactor::platform_has_different_kind,
            platformDecl, DeclKindToString(platformDecl), DeclKindToString(commonDecl));
        return false;
    }

    if (auto commonEnumDecl = DynamicCast<EnumDecl *>(&commonDecl)) {
        auto platformEnumDecl = DynamicCast<EnumDecl *>(&platformDecl);
        CJC_NULLPTR_CHECK(platformEnumDecl);
        if (commonEnumDecl->hasEllipsis) {
            platformDecl.EnableAttr(Attribute::COMMON_NON_EXHAUSTIVE);
        } else if (platformEnumDecl->hasEllipsis) {
            diag.DiagnoseRefactor(DiagKindRefactor::common_non_exaustive_platfrom_exaustive_mismatch,
                platformDecl, DeclKindToString(commonDecl), DeclKindToString(platformDecl));
        }
    }
    return true;
}

// Update the dependencies: common -> platform one.
void UpdateVarDependencies(const Decl& decl)
{
    for (auto& dep : decl.dependencies) {
        if (dep->platformImplementation) {
            dep = dep->platformImplementation;
        }
    }
}

// Check common instance member without initializer not match with platform one.
void Check4CommonInstanceVar(DiagnosticEngine& diag, const Decl& platformDecl)
{
    auto platformDecls = platformDecl.GetMemberDeclPtrs();
    for (auto decl : platformDecls) {
        if (decl->astKind == ASTKind::VAR_DECL && !decl->IsStaticOrGlobal() && decl->TestAttr(Attribute::COMMON)) {
            if (!decl->TestAttr(Attribute::COMMON_WITH_DEFAULT) && decl->platformImplementation == nullptr) {
                DiagNotMatchedPlatformDecl(diag, *decl);
            }
        }
    }
}

// Merge common nominative decl into platform one, do some match for fields.
void MergeCommonIntoPlatform(DiagnosticEngine& diag, Decl& commonDecl, Decl& platformDecl)
{
    CJC_ASSERT(commonDecl.TestAttr(AST::Attribute::COMMON));
    CJC_ASSERT(platformDecl.TestAttr(AST::Attribute::PLATFORM));
    if (!MatchNominativeDecl(diag, commonDecl, platformDecl)) {
        return;
    }
    auto& commonDecls = commonDecl.GetMemberDecls();
    auto& platformDecls = platformDecl.GetMemberDecls();
    std::vector<OwnedPtr<Decl>> mergedDecls;
    mergedDecls.reserve(commonDecls.size() + platformDecls.size());

    // Common instance member vars (including common member params)
    std::unordered_map<std::string, std::size_t> commonVariablesIds;
    // General member instance member vars from member params
    std::unordered_map<std::string, std::size_t> memberParamIds;
    // Collect candidates to be matched in common decl
    for (auto& commonDeclT : commonDecls) {
        auto newDecl = std::move(commonDeclT);
        newDecl->outerDecl = &platformDecl;
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
    // Match instance member and merge into platform decl.
    for (auto& platformDeclT : platformDecls) {
        if (platformDeclT->astKind == ASTKind::VAR_DECL && !platformDeclT->IsStaticOrGlobal()) {
            auto varDecl = StaticCast<AST::VarDecl>(platformDeclT.get());
            auto id = varDecl->identifier;
            if (platformDeclT->TestAttr(Attribute::PLATFORM)) {
                auto commonDeclIt = commonVariablesIds.find(id);
                if (commonDeclIt != commonVariablesIds.end()) {
                    // match
                    auto& commonDeclT = mergedDecls[commonDeclIt->second];
                    commonDeclT->platformImplementation = platformDeclT;
                    std::swap(platformDeclT, commonDeclT);
                    continue;
                } else {
                    DiagNotMatchedCommonDecl(diag, *varDecl);
                }
            } else if (varDecl->isMemberParam) {
                if (memberParamIds.find(id) != memberParamIds.end()) {
                    // General platform member params will merge into common if exist.
                    continue;
                }
            }
        }
        // Merge platform members.
        mergedDecls.emplace_back(std::move(platformDeclT));
    }
    std::swap(platformDecls, mergedDecls);
    // all the rest declarations need to be saved, because of, at least initializers of common
    // variables need to be analyzed.
    commonDecls.clear();
    for (auto& decl : mergedDecls) {
        if (decl) {
            commonDecls.emplace_back(std::move(decl));
        }
    }

    for (auto& decl : platformDecls) {
        UpdateVarDependencies(*decl);
    }
    // Check common member without initializer not match with platform one.
    Check4CommonInstanceVar(diag, platformDecl);

    commonDecl.doNotExport = true;
    commonDecl.platformImplementation = &platformDecl;
}
}

// PrepareTypeCheck for CJMP
void MPTypeCheckerImpl::PrepareTypeCheck4CJMP(Package& pkg)
{
    if (!compilePlatform) {
        return;
    }
    // platform package part
    MergeCJMPNominals(pkg);
}

void MPTypeCheckerImpl::MergeCJMPNominals(Package& pkg)
{
    std::unordered_map<std::string, Ptr<Decl>> matchedDecls;
    Walker walkerPackage(&pkg, [this, &matchedDecls](const Ptr<Node>& node) -> VisitAction {
        if (!node->IsDecl()) {
            return VisitAction::WALK_CHILDREN;
        }
        auto decl = StaticCast<Decl>(node);
        if (decl->IsNominalDecl()) {
            auto key = DeclKindToString(*decl);
            if (decl->astKind == ASTKind::EXTEND_DECL) {
                key += GetExtendedTypeName(*StaticCast<ExtendDecl>(decl));
                std::set<std::string> inheritedTypesName;
                for (auto& inheritedType : StaticCast<ExtendDecl>(decl)->inheritedTypes) {
                    inheritedTypesName.insert(inheritedType->ToString());
                }
                std::for_each(inheritedTypesName.begin(), inheritedTypesName.end(),
                    [&key](const std::string& name) { key += name; });
            } else {
                key += decl->identifier;
            }
            if (auto it = matchedDecls.find(key); it != matchedDecls.end()) {
                auto matchedDecl = it->second;
                if (decl->TestAttr(Attribute::PLATFORM) && matchedDecl->TestAttr(Attribute::COMMON)) {
                    MergeCommonIntoPlatform(diag, *matchedDecl, *decl);
                } else if (decl->TestAttr(Attribute::COMMON) && matchedDecl->TestAttr(Attribute::PLATFORM)) {
                    MergeCommonIntoPlatform(diag, *decl, *matchedDecl);
                }
            } else if (decl->TestAnyAttr(Attribute::COMMON, Attribute::PLATFORM)) {
                matchedDecls.emplace(key, decl);
            }
        }

        return VisitAction::SKIP_CHILDREN;
    });
    walkerPackage.Walk();
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

void MPTypeCheckerImpl::FilterOutCommonCandidatesIfPlatformExist(
    std::map<Names, std::vector<Ptr<FuncDecl>>>& candidates)
{
    for (auto& [names, funcs] : candidates) {
        bool hasPlatformCandidates = false;

        for (auto& func : funcs) {
            if (func->TestAttr(Attribute::PLATFORM)) {
                hasPlatformCandidates = true;
                break;
            }
        }

        if (hasPlatformCandidates) {
            funcs.erase(
                std::remove_if(funcs.begin(), funcs.end(),
                    [](const Ptr<FuncDecl> decl) { return decl->TestAttr(Attribute::COMMON); }),
                funcs.end());
        }
    }
}

// TypeCheck for CJMP
void MPTypeCheckerImpl::RemoveCommonCandidatesIfHasPlatform(std::vector<Ptr<FuncDecl>>& candidates) const
{
    bool hasPlatformCandidate = std::find_if(
        candidates.begin(), candidates.end(),
        [](const Ptr<FuncDecl> decl) { return decl->TestAttr(Attribute::PLATFORM); }
    ) != candidates.end();
    if (hasPlatformCandidate) {
        Utils::EraseIf(candidates, [](const Ptr<FuncDecl> decl) { return decl->TestAttr(Attribute::COMMON); });
    }
}

namespace {
// Collect common or platform decl.
void CollectDecl(
    Ptr<Decl> decl, std::vector<Ptr<Decl>>& commonDecls, std::vector<Ptr<Decl>>& platformDecls)
{
    if (decl->TestAttr(Attribute::COMMON)) {
        commonDecls.emplace_back(decl);
    } else if (decl->TestAttr(Attribute::PLATFORM)) {
        platformDecls.emplace_back(decl);
    }
}

// Collect common and platform decls.
void CollectCJMPDecls(Package& pkg, std::vector<Ptr<Decl>>& commonDecls, std::vector<Ptr<Decl>>& platformDecls)
{
    std::function<VisitAction(Ptr<Node>)> visitor = [&commonDecls, &platformDecls](const Ptr<Node> &node) {
        if (node->IsDecl() && node->astKind != ASTKind::PRIMARY_CTOR_DECL) {
            CollectDecl(StaticAs<ASTKind::DECL>(node), commonDecls, platformDecls);
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
bool MustMatchWithPlatform(const Decl& decl)
{
    CJC_ASSERT(decl.TestAttr(Attribute::COMMON));
    if (decl.platformImplementation) {
        return false;
    }
    // var/func with default implementation
    if (decl.TestAttr(Attribute::COMMON_WITH_DEFAULT)) {
        return false;
    }
    // common member in interface allow no platform member, maybe use abstract attr.
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
}

bool NeedToReportMissingBody(const Decl& common, const Decl& platform)
{
    if (common.outerDecl && common.TestAttr(Attribute::COMMON_WITH_DEFAULT) && !common.TestAttr(Attribute::ABSTRACT) &&
        platform.TestAttr(Attribute::ABSTRACT)) {
        return true;
    }
    return false;
}

// PostTypeCheck for CJMP
bool MPTypeCheckerImpl::MatchCJMPDeclAttrs(
    const std::vector<Attribute>& attrs, const Decl& common, const Decl& platform) const
{
    for (auto attr : attrs) {
        if (common.TestAttr(attr) != platform.TestAttr(attr)) {
            if ((attr == Attribute::ABSTRACT || attr == Attribute::OPEN)) {
                // Error `sema_platform_has_different_modifier` will be reported if common have body, but platform not.
                // Diagnostic abort wrong modifiers is confusing.
                if (NeedToReportMissingBody(common, platform)) {
                    continue;
                }
                if (platform.TestAttr(Attribute::ABSTRACT) && common.TestAttr(Attribute::OPEN)) {
                    auto kindStr = common.astKind == ASTKind::FUNC_DECL ? "function" : "property";
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_open_abstract_platform_can_not_replace_open_common,
                        platform, kindStr, kindStr);
                }
                // ABSTRACT member can be replaced with OPEN
                if (common.TestAttr(Attribute::ABSTRACT) && platform.TestAttr(Attribute::OPEN)) {
                    continue;
                }
                // Same as previous check, however static functions has no OPEN modifier
                if (common.TestAttr(Attribute::ABSTRACT) &&
                    common.TestAttr(Attribute::STATIC) && platform.TestAttr(Attribute::STATIC)) {
                    continue;
                }
            }
            if (common.astKind == ASTKind::PROP_DECL && attr == Attribute::MUT) {
                if (common.TestAttr(attr)) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_property_have_same_declaration_in_inherit_mut,
                        platform, platform.identifier.Val());
                } else {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_property_have_same_declaration_in_inherit_immut,
                        common, common.identifier.Val());
                }
            } else if (common.astKind != ASTKind::FUNC_DECL) {
                // Keep silent due to overloaded common funcs.
                diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_platform_has_different_modifier, platform, DeclKindToString(platform));
            }
            return false;
        }
    }
    return true;
}

bool MPTypeCheckerImpl::MatchCJMPDeclAnnotations(
    const std::vector<AST::AnnotationKind>& annotations, const AST::Decl& common, const AST::Decl& platform) const
{
    for (auto anno : annotations) {
        if (common.HasAnno(anno) != platform.HasAnno(anno)) {
            // Keep silent due to overloaded common funcs.
            if (common.astKind != ASTKind::FUNC_DECL) {
                diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_platform_has_different_annotation, platform, DeclKindToString(platform));
            }
            return false;
        }
    }
    return true;
}

// Match common nominal decl with platform for details.
bool MPTypeCheckerImpl::MatchCommonNominalDeclWithPlatform(const InheritableDecl& commonDecl)
{
    auto platformDecl = commonDecl.platformImplementation;
    if (platformDecl == nullptr) {
        DiagNotMatchedPlatformDecl(diag, commonDecl);
        return false;
    }
    // Match attributes (modifiers).
    std::vector<Attribute> matchedAttr = {
        Attribute::ABSTRACT, Attribute::PUBLIC, Attribute::OPEN, Attribute::PROTECTED, Attribute::C, Attribute::SEALED};
    if (!MatchCJMPDeclAttrs(matchedAttr, commonDecl, *platformDecl)) {
        return false;
    }
    // Match annotations (built-in).
    if (!MatchCJMPDeclAnnotations({AnnotationKind::DEPRECATED}, commonDecl, *platformDecl)) {
        return false;
    }
    // Match super types.
    auto comSupInters = commonDecl.GetSuperInterfaceTys();
    auto platSupInters = StaticCast<InheritableDecl>(platformDecl)->GetSuperInterfaceTys();
    if (comSupInters.size() != platSupInters.size()) {
        DiagNotMatchedSuperType(diag, *platformDecl);
        return false;
    }
    bool match = false;
    for (auto& comSupInter : comSupInters) {
        for (auto& platSupInter : platSupInters) {
            if (typeManager.IsTyEqual(comSupInter, platSupInter)) {
                match = true;
                break;
            }
        }
        if (!match) {
            DiagNotMatchedSuperType(diag, *platformDecl);
            return false;
        }
    }
    // Match super class if need.
    if (commonDecl.astKind == ASTKind::CLASS_DECL) {
        auto comSupClass = StaticCast<ClassDecl>(&commonDecl)->GetSuperClassDecl();
        auto platSupIClass = StaticCast<ClassDecl>(platformDecl)->GetSuperClassDecl();
        if (!typeManager.IsTyEqual(comSupClass->ty, platSupIClass->ty)) {
            DiagNotMatchedSuperType(diag, *platformDecl);
            return false;
        }
    }
    return true;
}

bool MPTypeCheckerImpl::IsCJMPDeclMatchable(const Decl& lhsDecl, const Decl& rhsDecl) const
{
    bool isLeftCommon = lhsDecl.TestAttr(Attribute::COMMON);
    const Decl& commonDecl = isLeftCommon ? lhsDecl : rhsDecl;
    const Decl& platformDecl = isLeftCommon ? rhsDecl : lhsDecl;
    if (commonDecl.identifier.GetRawText() != platformDecl.identifier.GetRawText()) {
        return false;
    }
    if (platformDecl.IsMemberDecl() != commonDecl.IsMemberDecl()) {
        return false;
    }
    if (platformDecl.IsMemberDecl()) {
        CJC_NULLPTR_CHECK(platformDecl.outerDecl);
        CJC_NULLPTR_CHECK(commonDecl.outerDecl);
        if (platformDecl.outerDecl->rawMangleName != commonDecl.outerDecl->rawMangleName) {
            return false;
        }
    }
    // need check Attribute::ABSTRACT for abstract class?
    std::vector<Attribute> matchedAttrs = { Attribute::STATIC, Attribute::MUT, Attribute::PRIVATE, Attribute::PUBLIC,
        Attribute::PROTECTED, Attribute::FOREIGN, Attribute::UNSAFE, Attribute::C, Attribute::OPEN,
        Attribute::ABSTRACT};
    return MatchCJMPDeclAttrs(matchedAttrs, commonDecl, platformDecl)
        && MatchCJMPDeclAnnotations({AnnotationKind::DEPRECATED, AnnotationKind::FROZEN}, commonDecl, platformDecl);
}

bool MPTypeCheckerImpl::TrySetPlatformImpl(Decl& platformDecl, Decl& commonDecl, const std::string& kind)
{
    if (commonDecl.platformImplementation) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_multiple_common_implementations, commonDecl, kind);
        return false;
    }
    // common with default but platform without default
    if (commonDecl.outerDecl && commonDecl.TestAttr(Attribute::COMMON_WITH_DEFAULT) &&
        platformDecl.TestAttr(Attribute::ABSTRACT)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_platform_member_must_have_implementation,
            platformDecl, platformDecl.identifier.Val(), commonDecl.outerDecl->identifier.Val());
        return false;
    }
    commonDecl.platformImplementation = &platformDecl;
    commonDecl.doNotExport = true;
    return true;
}

bool MPTypeCheckerImpl::MatchCJMPFunction(FuncDecl& platformFunc, FuncDecl& commonFunc)
{
    if (!IsCJMPDeclMatchable(platformFunc, commonFunc)) {
        return false;
    }
    if (!typeManager.IsFuncDeclSubType(platformFunc, commonFunc)) {
        return false;
    }
    auto& commonParams = commonFunc.funcBody->paramLists[0]->params;
    auto& platformParams = platformFunc.funcBody->paramLists[0]->params;
    for (size_t i = 0; i < commonFunc.funcBody->paramLists[0]->params.size(); i++) {
        if (commonParams[i]->isNamedParam != platformParams[i]->isNamedParam) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_platform_has_different_parameter, *platformParams[i]);
            return false;
        }
        if (commonParams[i]->isNamedParam && platformParams[i]->isNamedParam) {
            if (commonParams[i]->identifier.GetRawText() != platformParams[i]->identifier.GetRawText()) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_platform_has_different_parameter, *platformParams[i]);
                return false;
            }
        }
        // desugar platform default value, desugarDecl export all the time, assignment only export const value
        if (commonParams[i]->desugarDecl && !platformParams[i]->desugarDecl) {
            platformParams[i]->assignment = ASTCloner::Clone(commonParams[i]->assignment.get());
            platformParams[i]->desugarDecl = ASTCloner::Clone(commonParams[i]->desugarDecl.get());
            platformParams[i]->desugarDecl->outerDecl = platformFunc.outerDecl;
            platformParams[i]->EnableAttr(Attribute::HAS_INITIAL);
        }
    }

    // For init or primary constructor
    if (platformFunc.TestAttr(AST::Attribute::CONSTRUCTOR) || commonFunc.TestAttr(AST::Attribute::CONSTRUCTOR)) {
        if (!platformFunc.TestAttr(AST::Attribute::PRIMARY_CONSTRUCTOR) &&
            commonFunc.TestAttr(AST::Attribute::PRIMARY_CONSTRUCTOR)) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_platform_init_common_primary_constructor, commonFunc);
            return false;
        }
        for (size_t i = 0; i < platformParams.size(); ++i) {
            if (commonParams[i]->isMemberParam && !platformParams[i]->isMemberParam) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_platform_primary_unmatched_var_decl, *platformParams[i]);
                return false;
            }
        }
    }
    return TrySetPlatformImpl(platformFunc, commonFunc, "function");
}

bool MPTypeCheckerImpl::MatchCJMPProp(PropDecl& platformProp, PropDecl& commonProp)
{
    if (!IsCJMPDeclMatchable(platformProp, commonProp)) {
        return false;
    }
    if (!typeManager.IsTyEqual(platformProp.ty, commonProp.ty)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_platform_has_different_type, platformProp, "property");
    }
    bool ret = TrySetPlatformImpl(platformProp, commonProp, "property " + platformProp.identifier);
    if (!platformProp.getters.empty() && !commonProp.getters.empty()) {
        ret &= TrySetPlatformImpl(*platformProp.getters[0], *commonProp.getters[0],
            "property getter " + platformProp.identifier);
    }
    if (!platformProp.setters.empty() && !commonProp.setters.empty()) {
        ret &= TrySetPlatformImpl(*platformProp.setters[0], *commonProp.setters[0],
            "property setter " + platformProp.identifier);
    }

    return ret;
}

bool MPTypeCheckerImpl::MatchCJMPEnumConstructor(Decl& platformDecl, Decl& commonDecl)
{
    if (!IsCJMPDeclMatchable(platformDecl, commonDecl)) {
        return false;
    }
    if (platformDecl.astKind == ASTKind::FUNC_DECL) {
        auto platformFunc = StaticAs<ASTKind::FUNC_DECL>(&platformDecl);
        auto commonFunc = StaticAs<ASTKind::FUNC_DECL>(&commonDecl);
        if (!typeManager.IsFuncDeclEqualType(*platformFunc, *commonFunc)) {
            return false;
        }
    }
    auto enumName = platformDecl.outerDecl->identifier.GetRawText();
    return TrySetPlatformImpl(platformDecl, commonDecl, "enum '" + enumName + "' constructor");
}

bool MPTypeCheckerImpl::MatchCJMPVar(VarDecl& platformVar, VarDecl& commonVar)
{
    if (!IsCJMPDeclMatchable(platformVar, commonVar)) {
        return false;
    }
    auto cType = commonVar.ty;
    auto pType = platformVar.ty;
    if (!typeManager.IsTyEqual(cType, pType)) {
        auto platformKind = platformVar.isVar ? "var" : "let";
        diag.DiagnoseRefactor(DiagKindRefactor::sema_platform_has_different_type, platformVar, platformKind);
    }
    if (platformVar.isVar != commonVar.isVar) {
        auto platformKind = platformVar.isVar ? "var" : "let";
        auto commonKind = commonVar.isVar ? "var" : "let";
        diag.DiagnoseRefactor(DiagKindRefactor::sema_platform_var_not_match_let, platformVar, platformKind, commonKind);
    }
    if (platformVar.IsStaticOrGlobal()) {
        commonVar.platformImplementation = &platformVar;
        commonVar.doNotExport = true;
    }
    // Instance variables must already be matched
    CJC_ASSERT(commonVar.platformImplementation == &platformVar);
    return true;
}

bool MPTypeCheckerImpl::TryMatchVarWithPatternWithVarDecls(
    AST::VarWithPatternDecl& platformDecl, const std::vector<Ptr<AST::Decl>>& commonDecls)
{
    if (platformDecl.irrefutablePattern->astKind != ASTKind::TUPLE_PATTERN) {
        return false;
    }
    auto platformTuplePattern = StaticCast<TuplePattern>(platformDecl.irrefutablePattern.get());

    bool matchedAll = true;
    for (auto& pattern : platformTuplePattern->patterns) {
        if (pattern->astKind != ASTKind::VAR_PATTERN) {
            matchedAll = false;
            break;
        }

        auto patternDecl = StaticCast<VarPattern>(pattern.get());
        if (!MatchPlatformDeclWithCommonDecls(*patternDecl->varDecl, commonDecls)) {
            matchedAll = false;
        }
    }

    return matchedAll;
}

bool MPTypeCheckerImpl::MatchPlatformDeclWithCommonDecls(
    AST::Decl& platformDecl, const std::vector<Ptr<AST::Decl>>& commonDecls)
{
    bool matched = false;
    bool isEnumConstructor = platformDecl.TestAttr(Attribute::ENUM_CONSTRUCTOR);
    auto kind = platformDecl.astKind;
    for (auto& commonDecl : commonDecls) {
        if (matched) {
            break;
        }
        if (commonDecl->astKind != kind) {
            continue;
        }
        if (isEnumConstructor && commonDecl->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
            matched = MatchCJMPEnumConstructor(platformDecl, *commonDecl) || matched;
        } else if (kind == ASTKind::FUNC_DECL) {
            matched = MatchCJMPFunction(*StaticCast<FuncDecl>(&platformDecl), *StaticCast<FuncDecl>(commonDecl)) ||
                matched;
        } else if (kind == ASTKind::PROP_DECL) {
            matched = MatchCJMPProp(*StaticCast<PropDecl>(&platformDecl), *StaticCast<PropDecl>(commonDecl)) ||
                matched;
        } else if (kind == ASTKind::VAR_DECL) {
            matched = MatchCJMPVar(*StaticCast<VarDecl>(&platformDecl), *StaticCast<VarDecl>(commonDecl)) ||
                matched;
        }
    }

    // VarWithPattern can match several decls from common part
    if (kind == ASTKind::VAR_WITH_PATTERN_DECL) {
        matched = TryMatchVarWithPatternWithVarDecls(*StaticCast<VarWithPatternDecl>(&platformDecl), commonDecls);
    }

    // For enum constructor
    if (!matched) {
        if (platformDecl.outerDecl && platformDecl.outerDecl->TestAttr(Attribute::COMMON_NON_EXHAUSTIVE)) {
            return false;
        }
        DiagNotMatchedCommonDecl(diag, platformDecl);
    }

    return matched;
}

// When there are several common extend without interfaces declared,
// if there are same-named private functions declared, a clash is reported
void MPTypeCheckerImpl::CheckCommonExtensions(std::vector<Ptr<Decl>>& commonDecls)
{
    std::map<Ptr<Ty>, std::set<std::string>> privateFunctionsOfExtensions;
    for (auto decl : commonDecls) {
        if (decl->astKind != ASTKind::EXTEND_DECL) {
            continue;
        }

        auto extendDecl = StaticCast<ExtendDecl>(decl);
        if (!extendDecl->GetSuperInterfaceTys().empty()) {
            continue;
        }

        auto& privateFunctions = privateFunctionsOfExtensions[extendDecl->extendedType->ty];
        for (auto& memberDecl : extendDecl->GetMemberDecls()) {
            if (!memberDecl->IsFuncOrProp() || !memberDecl->TestAttr(Attribute::PRIVATE)) {
                continue;
            }
            if (privateFunctions.find(memberDecl->rawMangleName) != privateFunctions.end()) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_common_direct_extension_has_duplicate_private_members,
                    *memberDecl, extendDecl->extendedType->ToString(), memberDecl->IsFunc() ? "function" : "property",
                    memberDecl->identifier.GetRawText());
            } else {
                privateFunctions.emplace(memberDecl->rawMangleName);
            }
        }
    }
}

// A common declaration may have one or more matching specific declarations
// in descending source sets (at most one specific per source set)
void MPTypeCheckerImpl::CheckSpecificExtensions(std::vector<Ptr<Decl>>& platformDecls)
{
    std::map<Ptr<Ty>, std::set<Ptr<ExtendDecl>>> superInterfaceTysOfExtensions;
    for (auto decl : platformDecls) {
        if (decl->astKind != ASTKind::EXTEND_DECL) {
            continue;
        }

        auto extendDecl = StaticCast<ExtendDecl>(decl);
        auto& extendDeclsCache = superInterfaceTysOfExtensions[extendDecl->extendedType->ty];
        for (auto item : extendDeclsCache) {
            if (extendDecl->GetSuperInterfaceTys() == item->GetSuperInterfaceTys()) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_platform_has_duplicate_extensions,
                    *extendDecl, extendDecl->extendedType->ToString());
            }
        }
        extendDeclsCache.emplace(extendDecl);
    }
}

void MPTypeCheckerImpl::MatchCJMPDecls(std::vector<Ptr<Decl>>& commonDecls, std::vector<Ptr<Decl>>& platformDecls)
{
    for (auto& platformDecl : platformDecls) {
        CJC_ASSERT(platformDecl->TestAttr(Attribute::PLATFORM) && !platformDecl->TestAttr(Attribute::COMMON));
        if (platformDecl->TestAttr(Attribute::IS_BROKEN) || platformDecl->IsNominalDecl()) {
            continue;
        }
        MatchPlatformDeclWithCommonDecls(*platformDecl, commonDecls);
    }
    std::unordered_set<std::string> matchedIds;
    // Report error for common decl having no matched platform decl.
    for (auto& decl : commonDecls) {
        if (decl->IsNominalDecl() && MatchCommonNominalDeclWithPlatform(*StaticCast<InheritableDecl>(decl))) {
            matchedIds.insert(decl->platformImplementation->identifier.Val());
        }
        if (!MustMatchWithPlatform(*decl)) {
            continue;
        }
        DiagNotMatchedPlatformDecl(diag, *decl);
    }
    // Report error for platform nominal decl having no matched common decl.
    for (auto &decl : platformDecls) {
        if (decl->IsNominalDecl() && matchedIds.find(decl->identifier.Val()) == matchedIds.end()) {
            DiagNotMatchedCommonDecl(diag, *decl);
        }
    }
}

void MPTypeCheckerImpl::MatchPlatformWithCommon(Package& pkg)
{
    std::vector<Ptr<Decl>> commonDecls;
    std::vector<Ptr<Decl>> platformDecls;
    CollectCJMPDecls(pkg, commonDecls, platformDecls);
    if (compileCommon) { // check common extensions
        CheckCommonExtensions(commonDecls);
    } else if (compilePlatform) { // match common decls and platform decls
        CheckSpecificExtensions(platformDecls);
        MatchCJMPDecls(commonDecls, platformDecls);
    }
}

