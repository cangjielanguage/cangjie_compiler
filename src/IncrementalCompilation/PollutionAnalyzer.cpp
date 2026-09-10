// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "PollutionAnalyzer.h"
#include "PollutionMapGen.h"
#include "cangjie/IncrementalCompilation/Utils.h"
#include "cangjie/Mangle/ASTMangler.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Sema/IncrementalUtils.h"

using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Cangjie::IncrementalCompilation;

namespace {

} // namespace

void TypeMap::CollectImportedDeclExtraRelation(const AST::Decl& decl)
{
    if (!decl.IsNominalDecl()) {
        return;
    }
    auto& type{StaticCast<InheritableDecl>(decl)};
    for (auto& parentType: type.inheritedTypes) {
        auto parentDecl = Ty::GetDeclOfTy(parentType->GetTy());
        CJC_NULLPTR_CHECK(parentDecl); // all builtin types are struct or enum and cannot have child types
        AddParent(*parentDecl, decl);
    }
    if (auto extend = DynamicCast<ExtendDecl*>(&type)) {
        AddExtend(Sema::GetTypeRawMangleName(extend->GetTy()), decl.rawMangleName);
    }
}

static std::list<Ptr<const Decl>> GetAllMembers(const Decl& decl)
{
    std::list<Ptr<const Decl>> res{};
    for (auto member : decl.GetMemberDeclPtrs()) {
        res.splice(res.cend(), GetAllMembers(*member));
        res.push_back(member);
    }
    if (auto p = DynamicCast<PropDecl*>(&decl)) {
        for (auto& getter : p->getters) {
            res.push_back(getter.get());
        }
        for (auto& setter : p->setters) {
            res.push_back(setter.get());
        }
    }
    return res;
}

PollutionResult PollutionAnalyzer::Get(PollutionAnalyseArgs&& args)
{
    if (!args.rawModified.aliases.empty()) {
        if (auto& logger = IncrementalCompilationLogger::GetInstance(); logger.IsEnable()) {
            std::stringstream r;
            for (auto decl : std::as_const(args.rawModified.aliases)) {
                r << "changed typealias: ";
                PrintDecl(r, *decl);
                r << '\n';
            }
            logger.LogLn(r.str());
        }
        return {IncreKind::ROLLBACK};
    }

    // Get population graph and type relation.
    auto [pollutedMap, typeMap] = PollutionMapGen::Get(
        args.pkg, args.mangled2Decl, args.sourcePopulations, args.semaInfo, args.chirOptInfo, args.man);

    typeMap.Merge(std::move(args.importedRelations));
    PollutionAnalyzer analyzer{
        std::move(pollutedMap), std::move(typeMap), args.mangled2Decl, args.chirOptInfo, std::move(args.extends)};
    for (auto decl : std::as_const(args.rawModified.added)) {
        CJC_NULLPTR_CHECK(decl);
        analyzer.PollutionForAddedDecl(*decl);
    }
    if (analyzer.FallBack()) {
        analyzer.PrintFallbackInfo();
        return {IncreKind::ROLLBACK};
    }

    // Start pollution for deleted decls
    for (auto& decl : std::as_const(args.rawModified.deletes)) {
        analyzer.PollutionForDeletedDecl(decl);
    }
    if (analyzer.FallBack()) {
        analyzer.PrintFallbackInfo();
        return {IncreKind::ROLLBACK};
    }
    for (auto& decl : std::as_const(args.rawModified.deletedTypeAlias)) {
        analyzer.removedNotSupported.push_back(decl);
    }
    if (analyzer.FallBack()) {
            analyzer.PrintFallbackInfo();
        return {IncreKind::ROLLBACK};
    }

    // Start pollution for changed non-type decls
    for (auto& change : std::as_const(args.rawModified.commons)) {
        analyzer.PollutionForChangedNonTypeDecl(change.second);
    }
    if (analyzer.FallBack()) {
            analyzer.PrintFallbackInfo();
        return {IncreKind::ROLLBACK};
    }

    // Start pollution for changed type decls
    for (auto& change : std::as_const(args.rawModified.types)) {
        analyzer.PollutionForChangedTypeDecl(*change.first, change.second);
    }
    if (analyzer.FallBack()) {
        analyzer.PrintFallbackInfo();
        return {IncreKind::ROLLBACK};
    }

    for (auto t: std::as_const(args.rawModified.orderChanges)) {
        CJC_NULLPTR_CHECK(t);
        analyzer.PollutionForOrderChangeDecl(*t);
    }

    PollutionResult res{
        IncreKind::INCR, analyzer.GetPollutionResult(), analyzer.GetDeletedResult(), analyzer.GetReBoxedTypes()};
    if (res.declsToRecompile.empty() && res.deleted.empty()) {
        res.kind = IncreKind::NO_CHANGE;
    }

    return res;
}

void PollutionAnalyzer::AddToPollutedDecls(const Decl& decl)
{
    pollutedDecls.emplace(&decl);
    if (auto func = DynamicCast<FuncDecl*>(&decl); func && IsImported(*func) && func->funcBody) {
        // insert default parameter functions into the recompile list if the body hash of the function changes;
        // the reason of doing this here rather than in ASTDiff is that it is possible for imported decls to be
        // propagated to, e.g. from a change in some class into its subclass, then to the default value parameter
        // whose declaration type is the subclass.
        for (auto& param: func->funcBody->paramLists[0]->params) {
            // collect it whether the desugared func has body or not, otherwise the change of inlinability of a default
            // parameter function cannot be detected
            if (param->desugarDecl) {
                param->desugarDecl->rawMangleName = param->desugarDecl->mangledName;
                AddToPollutedDecls(*param->desugarDecl);
            }
        }
    } else if (auto varWithPattern = DynamicCast<VarWithPatternDecl*>(&decl)) {
        auto allPatterns = FlattenVarWithPatternDecl(*varWithPattern);
        for (auto pattern : allPatterns) {
            if (pattern->astKind != ASTKind::VAR_PATTERN) {
                continue;
            }
            auto varPattern = StaticCast<VarPattern*>(pattern);
            pollutedDecls.emplace(varPattern->varDecl);
        }
    }
}

// Propagate pollution for added decl, which can be either:
// 1) a top-level VarDecl, VarWithPatternDecl, FuncDecl, type related decl
// 2) a member VarDecl, FuncDecl, PropDecl. In this case, we assume the parent decl has been handled
void PollutionAnalyzer::PollutionForAddedDecl(const Decl& decl)
{
    // Special case handling for type alias decl
    if (decl.astKind == ASTKind::TYPE_ALIAS_DECL) {
        typeAliases.push_back(&decl);
        return;
    }

    if (Is<const InheritableDecl*>(&decl)) {
        PollutionForAddedTypeDecl(decl);
    } else {
        PollutionForAddedNonTypeDecl(decl);
    }
}

void PollutionAnalyzer::PollutionForAddedTypeDecl(const Decl& decl)
{
    if (decl.TestAttr(Attribute::IMPORTED)) {
        // All imported stuff has been sema-d in upstream package. We only need to:
        // 1) analysis the pollution for the added decl
        // 2) recompile those generics
        bool genericImported = IsInDeclWithAttribute(decl, Attribute::GENERIC);
        if (genericImported) {
            AddToPollutedDecls(decl);
            for (const auto member : decl.GetMemberDeclPtrs()) {
                PollutionForAddedNonTypeDecl(*member);
            }
        } else {
            for (const auto member : decl.GetMemberDeclPtrs()) {
                PollutionForAddedNonTypeDecl(*member);
            }
        }
    } else {
        AddToPollutedDecls(decl);
        const auto &allMembers = GetAllMembers(decl);
        for (const auto member : std::as_const(allMembers)) {
            PollutionForAddedNonTypeDecl(*member);
        }
    }

    PollutedUnqualifiedUses(decl);
    PollutedGlobalChangeToPackageQualifiedUses(decl);
    PolluteGlobalChangeToQualifiedUses(decl);

    if (auto extend = DynamicCast<const ExtendDecl*>(&decl)) {
        AdditionPollutionForAddedExtendDecl(*extend);
    }
}

void PollutionAnalyzer::PollutionForAddedNonTypeDecl(const Decl& decl)
{
    if (decl.TestAttr(Attribute::IMPORTED)) {
        // All imported stuff has been sema-d in upstream package. We only need to:
        // 1) analysis the pollution for the added decl
        // 2) recompile those source imported functions, global variables and generics
        bool srcImported = false;
        if (auto varDecl = DynamicCast<const VarDecl*>(&decl)) {
            if (varDecl->TestAttr(Attribute::GLOBAL) && varDecl->initializer != nullptr) {
                srcImported = true;
            }
        } else if (auto funcDecl = DynamicCast<const FuncDecl*>(&decl)) {
            if (funcDecl->funcBody != nullptr && funcDecl->funcBody->body != nullptr) {
                srcImported = true;
            }
        }
        bool genericImported = IsInDeclWithAttribute(decl, Attribute::GENERIC);
        if (srcImported || genericImported) {
            AddToPollutedDecls(decl);
        }
    } else {
        AddToPollutedDecls(decl);
    }

    PollutionForAddedNonTypeDeclImpl(decl);
}

void PollutionAnalyzer::PollutionForAddedNonTypeDeclImpl(const Decl& decl)
{
    if (auto propDecl = DynamicCast<const PropDecl*>(&decl)) {
        for (auto& getter : propDecl->getters) {
            AddToPollutedDecls(*getter);
            PollutionForAddedNonTypeDeclImpl(*getter);
        }
        for (auto& setter : propDecl->setters) {
            AddToPollutedDecls(*setter);
            PollutionForAddedNonTypeDeclImpl(*setter);
        }
    }

    if (auto varWithPatternDecl = DynamicCast<const VarWithPatternDecl*>(&decl)) {
        auto allPatterns = FlattenVarWithPatternDecl(*varWithPatternDecl);
        for (auto pattern : allPatterns) {
            if (pattern->astKind != ASTKind::VAR_PATTERN) {
                continue;
            }
            auto varPattern = StaticCast<VarPattern*>(pattern);
            AddToPollutedDecls(*varPattern->varDecl);
            PollutionForAddedNonTypeDeclImpl(*varPattern->varDecl);
        }
    }

    // For newly added member VarDecl, FuncDecl or PropDecl, can we pollute a smaller scope here?
    // For example, we just need to pollute the qualified usage and unqualified usage inside the
    // parent struct/class. Besides, the EnumDecl is another story where we should pollute both
    // qualified usage and unqualified usage in whole program
    PollutedUnqualifiedUses(decl);
    PollutedGlobalChangeToPackageQualifiedUses(decl);
    PolluteGlobalChangeToQualifiedUses(decl);
}

void PollutionAnalyzer::AdditionPollutionForBodyChangedExtendDecl(const ExtendDecl& decl)
{
    if (decl.inheritedTypes.empty()) {
        if (auto extendsIt = directExtends.find(decl.rawMangleName); extendsIt != directExtends.cend()) {
            for (auto& extend : std::as_const(extendsIt->second)) {
                if (pollutedDecls.count(extend) == 0) {
                    AddToPollutedDecls(*extend);
                    PollutionForAddedTypeDecl(*extend);
                }
            }
        }
    }
}

void PollutionAnalyzer::AdditionPollutionAPIOfExtendedDecl(const AST::ExtendDecl& decl)
{
    if (auto extendedTypeRawMangleName = GetExtendedTypeRawMangleName(decl)) {
        // Serveral cases here:
        // 1) the extended type is a user-defined type which have proper raw mangle name, then
        //    we can find the type decl in the `mangled2Decl` map and pollute it
        // 2) the extended type is a primitive type (Int64, Boolean, etc) or built-in type (CPointer, CString),
        //    which don't have a type decl. In this case, we manually find and pollute all other ExtendDecls that
        //    extends this type
        if (auto it = mangled2Decl.find(*extendedTypeRawMangleName); it != mangled2Decl.cend()) {
            PolluteAPIOfDecl(*it->second);
        } else {
            for (auto extend : t.GetAllExtendsOfType(*extendedTypeRawMangleName)) {
                if (auto itt = mangled2Decl.find(extend); itt != mangled2Decl.cend()) {
                    auto extendOfBuiltinDecl = StaticCast<ExtendDecl*>(itt->second);
                    AddToPollutedDecls(*extendOfBuiltinDecl);
                    PollutedInstantiationChangeFromDecl(*extendOfBuiltinDecl);
                }
            }
        }
    }
}

void PollutionAnalyzer::AdditionPollutionAPIOfExtendDecl(const AST::ExtendDecl& decl)
{
    // Pollute the extended type
    AdditionPollutionAPIOfExtendedDecl(decl);
    // Specially, for direct extends, since we merge them into one when calculating the raw mangle name and
    // hash, so we have to manually pollute all other same direct extends
    AdditionPollutionAPIOfDirectExtendDecls(decl.rawMangleName);
}

void PollutionAnalyzer::AdditionPollutionForAddedExtendDecl(const ExtendDecl& decl)
{
    if (decl.inheritedTypes.empty() && decl.GetMemberDeclPtrs().empty()) {
        return;
    }
    AdditionPollutionAPIOfExtendedDecl(decl);
    // Specially, for direct extends, since we merge them into one when calculating the raw mangle name and
    // hash, so we have to manually pollute all other same direct extends
    AdditionPollutionForBodyChangedExtendDecl(decl);
}

static size_t TruncatePrecedingNumber(const std::string& mangle, size_t begin, size_t end)
{
    CJC_ASSERT(end <= mangle.size());
    for (size_t i{begin}; i < end; ++i) {
        // skip the number if there is one preceding the identifier
        if (!isdigit(mangle[i])) {
            return i;
        }
    }
    CJC_ASSERT(false);
    return std::string::npos;
}

// search for last appearance of '.' and return the position after it from \p begin to \p end exclusive.
// returns begin if '.' is not found.
static size_t TruncatePackage(const std::string& mangle, size_t end)
{
    CJC_ASSERT(end != 0 && end <= mangle.size());
    for (auto i{end - 1}; ; --i) {
        if (mangle[i] == '.') {
            return i + 1;
        }
        if (i == 0) {
            break;
        }
    }
    return 0;
}

static size_t TruncateGeneric(const std::string& mangle)
{
    auto end = mangle.find("<");
    if (end == std::string::npos) {
        return mangle.size();
    }
    return end;
}

static std::string GetIdentifierFromTruncatedName(const std::string& mangle)
{
    CJC_ASSERT(!mangle.empty());
    auto end = TruncateGeneric(mangle);
    auto begin = TruncatePackage(mangle, end);
    auto begin2 = TruncatePrecedingNumber(mangle, begin, end);
    CJC_ASSERT(end > begin2);
    return mangle.substr(begin2, end - begin2);
}

// Propagate pollution for deleted decl, which can be either:
// 1) a top-level VarDecl, VarWithPatternDecl, FuncDecl, type related decl
// 2) a member VarDecl, FuncDecl, PropDecl. In this case, we assume the parent decl has been handled
void PollutionAnalyzer::PollutionForDeletedDecl(const RawMangledName& mangle)
{
    deletedDecls.emplace_back(mangle);
    // For deleted decl, we need to pollute the sema-based precise usage
    PollutePreciseUsages(mangle);

    // For deleted extend decl, we also need to pollute the extended type decl
    if (auto extendedTypeName = t.FindExtendedTypeByExtendDeclMangleName(mangle)) {
        if (auto extendedType = mangled2Decl.find(*extendedTypeName); extendedType != mangled2Decl.cend()) {
            PolluteAPIOfDecl(*extendedType->second);
        } else {
            for (auto extend : t.GetAllExtendsOfType(*extendedTypeName)) {
                if (auto it = mangled2Decl.find(extend); it != mangled2Decl.cend()) {
                    auto extendOfBuiltinDecl = StaticCast<ExtendDecl*>(it->second);
                    AddToPollutedDecls(*extendOfBuiltinDecl);
                    PollutedInstantiationChangeFromDecl(*extendOfBuiltinDecl);
                }
            }
        }
    } else {
        // deleted decl not found in cache, it is a deleted imported decl
        // if that is an ExtendDecl, find the extended type
        if (auto truncatedExtendName = ASTMangler::TruncateExtendMangledName(mangle)) {
            auto identifier = GetIdentifierFromTruncatedName(*truncatedExtendName);
            std::list<const Decl*> candidateSet{};
            for (auto& pair : mangled2Decl) {
                auto decl = pair.second;
                if (decl->identifier == identifier && decl->IsNominalDecl() && decl->astKind != ASTKind::EXTEND_DECL) {
                    candidateSet.push_back(decl);
                }
            }
            for (auto t1: std::as_const(candidateSet)) {
                CJC_NULLPTR_CHECK(t1);
                AddToPollutedDecls(*t1);
                PolluteBoxUsesFromDecl(*t1);
                PollutedInstantiationChangeFromDecl(*t1);
            }

            // extended type not found, this is an imported decl of builtin type
            // propagate to its extends
            if (candidateSet.empty()) {
                PollutedToBoxUses(identifier);
            }
        }
    }

    // For deleted type decl, we also need to pollute all the ExtendDecls which extends it
    for (auto element : FindAllExtendDeclsOfType(mangle)) {
        AddToPollutedDecls(*element);
        PollutedInstantiationChangeFromDecl(*element);
    }
}

void PollutionAnalyzer::PollutionForConstDecl(const Decl& decl)
{
    AddToPollutedDecls(decl);
    if (decl.TestAttr(Attribute::GENERIC)) {
        PollutedInstantiationChangeFromDecl(decl);
    }
}

void PollutionAnalyzer::PollutionForOrderChangeDecl(const AST::Decl& decl)
{
    // the minimal propagation for a changed decl is instantiation change, which is the implementation of const decl
    // change.
    PollutionForConstDecl(decl);
}

void PollutionAnalyzer::PollutionForChangedNonTypeDecl(const CommonChange& c)
{
    AddToPollutedDecls(*c.decl);
    if (c.sig) {
        PollutionForSigChangedDecl(*c.decl);
    } else if (c.srcUse) {
        PollutionForSrcUseChangedDecl(*c.decl);
    } else if (c.body) {
        PollutionForBodyChangedDecl(*c.decl);
    }

    // Specially, if we are changing a decl inside a generic decl, we need to
    // pollute the generic decl due to the need of re-instantiation
    if (c.decl->outerDecl && IsInDeclWithAttribute(*c.decl->outerDecl, Attribute::GENERIC)) {
        PollutedInstantiationChangeFromDecl(*c.decl->outerDecl);
    }
}

void PollutionAnalyzer::PollutionForBodyChangedDecl(const Decl& decl)
{
    if (auto type = DynamicCast<InheritableDecl*>(&decl)) {
        if (typeChanges[type].body) {
            return;
        }
        typeChanges[type].body = true;

        // propagate the extended type if the type changed is an ExtendDecl
        if (auto extendDecl = DynamicCast<const ExtendDecl*>(&decl)) {
            if (auto extendedTypeRawMangleName = GetExtendedTypeRawMangleName(*extendDecl)) {
                // Serveral cases here:
                // 1) the extended type is a user-defined type which have proper raw mangle name, then
                //    we can find the type decl in the `mangled2Decl` map and pollute it
                // 2) the extended type is a primitive type (Int64, Boolean, etc) or built-in type (CPointer, CString),
                //    which don't have a type decl. In this case, we manually find and pollute all other ExtendDecls
                //    that extends this type
                if (auto it = mangled2Decl.find(*extendedTypeRawMangleName); it != mangled2Decl.cend()) {
                    if (auto inheritableDecl = DynamicCast<const InheritableDecl*>(it->second)) {
                        PollutionForBodyChangedDecl(*inheritableDecl);
                    }
                } else {
                    for (auto extend : t.GetAllExtendsOfType(*extendedTypeRawMangleName)) {
                        if (auto itt = mangled2Decl.find(extend); itt != mangled2Decl.cend()) {
                            auto extendOfBuiltinDecl = StaticCast<ExtendDecl*>(itt->second);
                            AddToPollutedDecls(*extendOfBuiltinDecl);
                        }
                    }
                }
            }

            // Specially, for direct extends, since we merge them into one when calculating the raw mangle name and
            // hash, so we have to manually pollute all other same direct extends
            AdditionPollutionForBodyChangedExtendDecl(*extendDecl);
        }

        if (decl.astKind == ASTKind::CLASS_DECL) {
            // default implementation of interface functions may copy to the type when public visiable functions
            // change, add this type as well as child types to recompile to trigger this copy behaviour.
            // Also propagate to all interface extends of this type, as the interface implementation can be defined
            // in the class body of any of its interface extends. Note also that keyword `override` is optional, so
            // public/protected member func changes in open class may have an impact on the vtable of its subclasses.
            PolluteDownStreamTypes(decl);
        }

        // All public APIs need a wrapper in Box, thus any change here should trigger re-Box
        PolluteBoxUsesFromDecl(decl);
        AddToPollutedDecls(decl);
        PollutedInstantiationChangeFromDecl(decl);
    } else {
        if (changes[&decl].body) {
            return;
        }
        changes[&decl].body = true;
        AddToPollutedDecls(decl);
        // when there is change of override/redef on PropDecl,
        // the PropDecl should be recompiled at sema (check whether the override/redef is legal)
        // in addition, AST2CHIR::SetParentInfo will traverse member of PropDecl，so here also add
        // member of PropDecl
        if (decl.astKind == ASTKind::PROP_DECL) {
            for (auto member : GetMembers(decl)) {
                AddToPollutedDecls(*member);
            }
        }

        // For decl with explicit type, there is no pollution. Yet if the decl has no explicit type,
        // we should assume the body change will affect the type thus change the signature
        if (IsUntyped(decl)) {
            PollutionForSigChangedDecl(decl);
        }

        PollutedInstantiationChangeFromDecl(decl);
        if (decl.IsConst()) {
            PollutionForSrcUseChangedDecl(decl);
        }
        PolluteCHIROptAffectDecl(decl);

        // propagate to constructors for member variables
        if (decl.outerDecl && !decl.TestAttr(Attribute::STATIC)) {
            if (auto enclosingType = DynamicCast<InheritableDecl*>(decl.outerDecl)) {
                PolluteToConstructors(*enclosingType);
            }
        }
    }
}

void PollutionAnalyzer::PollutionForSigChangedDecl(const Decl& decl)
{
    switch (decl.astKind) {
        case ASTKind::FUNC_DECL:
        case ASTKind::PRIMARY_CTOR_DECL:
            PollutionForSigChangedFuncDecl(decl);
            break;
        case ASTKind::MAIN_DECL:
        case ASTKind::MACRO_DECL:
            CJC_ABORT();
            break;
        case ASTKind::VAR_DECL:
        case ASTKind::FUNC_PARAM:
            CJC_ASSERT(decl.astKind == ASTKind::VAR_DECL ||
                (decl.astKind == ASTKind::FUNC_PARAM && StaticCast<FuncParam>(decl).isMemberParam));
            PollutionForSigChangedVarDecl(static_cast<const VarDecl&>(decl));
            break;
        case ASTKind::VAR_WITH_PATTERN_DECL:
            PollutionForSigChangedVarWithPatternDecl(static_cast<const VarWithPatternDecl&>(decl));
            break;
        case ASTKind::PROP_DECL:
            PollutionForSigChangedPropDecl(static_cast<const PropDecl&>(decl));
            break;
        default:
            PollutionForSigChangedInheritableDecl(static_cast<const InheritableDecl&>(decl));
            break;
    }
}

void PollutionAnalyzer::PollutionForSigChangedFuncDecl(const Decl& decl)
{
    if (changes[&decl].sig) {
        return;
    }
    changes[&decl].sig = true;

    PollutedUnqualifiedUses(decl);
    PollutedGlobalChangeToPackageQualifiedUses(decl);
    PolluteGlobalChangeToQualifiedUses(decl);
    PollutedInstantiationChangeFromDecl(decl);
    if (decl.outerDecl && decl.outerDecl->astKind == ASTKind::EXTEND_DECL) {
        AdditionPollutionAPIOfExtendDecl(StaticCast<ExtendDecl&>(*decl.outerDecl));
    }
}

void PollutionAnalyzer::PollutionForSigChangedVarDecl(const VarDecl& decl)
{
    if (changes[&decl].sig) {
        return;
    }
    changes[&decl].sig = true;

    if (!decl.outerDecl) {
        PollutedUnqualifiedUses(decl);
        PollutedToExprUsages(
            p.GetPackageQualifiedUses(ChangePollutedMap::Idx::BODY, decl.identifier, decl.fullPackageName));
    } else {
        // If this is a member VarDecl of struct or class, then it will NOT overload with
        // other same-identifier global variables, we just need to pollute the precise place
        PollutePreciseUsages(decl);
    }
}

void PollutionAnalyzer::PollutionForSigChangedVarWithPatternDecl(const VarWithPatternDecl& decl)
{
    if (changes[&decl].sig) {
        return;
    }
    changes[&decl].sig = true;

    auto allPatterns = FlattenVarWithPatternDecl(decl);
    for (auto pattern : allPatterns) {
        if (pattern->astKind != ASTKind::VAR_PATTERN) {
            continue;
        }
        auto varPattern = StaticCast<VarPattern*>(pattern);
        PollutionForSigChangedDecl(*varPattern->varDecl);
    }
}

void PollutionAnalyzer::PollutionForSigChangedPropDecl(const PropDecl& decl)
{
    if (changes[&decl].sig) {
        return;
    }
    changes[&decl].sig = true;

    for (auto& getter : decl.getters) {
        AddToPollutedDecls(*getter);
        PollutionForSigChangedDecl(*getter);
    }
    for (auto& setter : decl.setters) {
        AddToPollutedDecls(*setter);
        PollutionForSigChangedDecl(*setter);
    }
}

void PollutionAnalyzer::PollutionForSigChangedInheritableDecl(const InheritableDecl& decl)
{
    if (typeChanges[&decl].sig) {
        return;
    }
    typeChanges[&decl].sig = true;

    PollutePreciseUsages(decl);

    // we might want to have a separate hash value for annotations
    // The sig change includes the annotations change, we need to propagate to
    // all inner functions
    // not only the annotation impact the inner functions, the inheritance
    // change should also impact all the init functions cause they implict call super,
    // and it will also change the layout!!!
    for (const auto& member : decl.GetMemberDeclPtrs()) {
        // Filter out the construtor in EnumDecl
        if (decl.astKind == ASTKind::ENUM_DECL) {
            if (member->astKind == ASTKind::FUNC_DECL && member->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
                continue;
            }
        }

        // For imported decl, we might have compiler-added member which has no raw mangle name
        if (member->rawMangleName.empty()) {
            continue;
        }

        if (member->astKind == ASTKind::FUNC_DECL || member->astKind == ASTKind::PROP_DECL ||
            member->astKind == ASTKind::PRIMARY_CTOR_DECL) {
            PolluteAPIOfDecl(*member);
        }
    }
    for (auto extend : FindAllExtendDeclsOfType(decl.rawMangleName)) {
        PolluteAPIOfDecl(*extend);
    }
    PolluteBoxUsesFromDecl(decl);
}

void PollutionAnalyzer::PollutionForSrcUseChangedDecl(const Decl& decl)
{
    if (IsEnumConstructor(decl)) {
        // enum constructor does not have src-use change
        return;
    }

    if (auto type = DynamicCast<InheritableDecl*>(&decl)) {
        if (typeChanges[type].srcUse) {
            return;
        }
        typeChanges[type].srcUse = true;
    } else {
        if (changes[&decl].srcUse) {
            return;
        }
        changes[&decl].srcUse = true;
        // special rule: change of property shall propagate to its getters and setters
        // only property may have members in this branch
        auto allMembers = GetAllMembers(decl);
        for (auto member : std::as_const(allMembers)) {
            AddToPollutedDecls(*member);
            PollutionForSrcUseChangedDecl(*member);
        }
    }
    PollutePreciseUsages(decl);
    PolluteBoxUsesFromDecl(decl);
}

void PollutionAnalyzer::PollutionForChangedTypeDecl(const InheritableDecl& decl, const TypeChange& c)
{
    CJC_ASSERT(decl.astKind == ASTKind::ENUM_DECL || decl.astKind == ASTKind::STRUCT_DECL ||
        decl.astKind == ASTKind::CLASS_DECL || decl.astKind == ASTKind::INTERFACE_DECL ||
        decl.astKind == ASTKind::EXTEND_DECL);

    // For imported non-public type decl, we just add it into the recompilation list so the
    // backend will update its metadata info
    if (decl.TestAttr(Attribute::IMPORTED) && decl.TestAttr(Attribute::PRIVATE)) {
        AddToPollutedDecls(decl);
        return;
    }

    if (c.instVar) {
        AddToPollutedDecls(decl);
        PollutionForLayoutChangedDecl(decl);
    }
    if (c.virtFun) {
        AddToPollutedDecls(decl);
        PollutionForVTableChangedDecl(decl);
    }
    if (c.sig) {
        AddToPollutedDecls(decl);
        PollutionForSigChangedDecl(decl);
    }
    if (c.srcUse) {
        AddToPollutedDecls(decl);
        PollutionForSrcUseChangedDecl(decl);
    }
    if (c.body) {
        AddToPollutedDecls(decl);
        PollutionForBodyChangedDecl(decl);
    }

    if (!c.added.empty() || !c.del.empty() || !c.changed.empty()) {
        // Specially, if we are changing inside a generic decl, we need to
        // pollute the generic decl due to the need of re-instantiation
        PollutedInstantiationChangeFromDecl(decl);
    }

    for (auto member : c.added) {
        PollutionForAddedDecl(*member);
    }

    for (auto& member : c.del) {
        PollutionForDeletedDecl(member);
    }

    for (auto& member : c.changed) {
        PollutionForChangedNonTypeDecl(member);
    }
}

void PollutionAnalyzer::PolluteBoxUsesFromDecl(const Decl& decl)
{
    if (auto extend = DynamicCast<ExtendDecl*>(&decl)) {
        if (auto extendedTypeRawMangleName = GetExtendedTypeRawMangleName(*extend)) {
            PollutedToBoxUses(*extendedTypeRawMangleName);
        }
    } else if (auto type = DynamicCast<InheritableDecl*>(&decl)) {
        PollutedToBoxUses(type->rawMangleName);
    }
}

void PollutionAnalyzer::PolluteToConstructors(const AST::Decl& decl)
{
    const AST::PrimaryCtorDecl* pc{nullptr};
    bool hasCtor = false;
    for (auto member : decl.GetMemberDeclPtrs()) {
        if (member->rawMangleName.empty()) {
            continue;
        }
        if (IsClassOrEnumConstructor(*member)) {
            hasCtor = true;
            PolluteAPIOfDecl(*member);
        }
        if (member->astKind == ASTKind::PRIMARY_CTOR_DECL) {
            hasCtor = true;
            pc = StaticCast<AST::PrimaryCtorDecl*>(member);
            PolluteAPIOfDecl(*member);
        }
    }

    // If no explicit ctor is provided in source code, we will have to manually pollute
    // the call site of the implicitly generated ctor
    if (!hasCtor) {
        PollutePreciseUsages(decl.rawMangleName + "<init>");
    }

    // Remember to count in the member variable defined in the primal constructor
    if (pc) {
        for (auto& pl : pc->funcBody->paramLists) {
            for (auto& param : pl->params) {
                if (param->isMemberParam) {
                    PolluteAPIOfDecl(*param);
                }
            }
        }
    }
}

void PollutionAnalyzer::PollutionForLayoutChangedDecl(const InheritableDecl& decl)
{
    if (typeChanges[&decl].instVar) {
        return;
    }
    typeChanges[&decl].instVar = true;

    switch (decl.astKind) {
        case ASTKind::ENUM_DECL:
            for (auto& member : static_cast<const EnumDecl&>(decl).constructors) {
                // populate to all usages of enum constructor identifier since the enum index is changed
                PolluteAPIOfDecl(*member);
            }
            // the enum constructors change need to trigger more pollution because some enum will be generated
            // in special type in CodeGen (the ref-enum for recursive case), but there is opportunity to optimize
            PolluteAPIOfDecl(decl);
            break;
        case ASTKind::STRUCT_DECL:
            // also struct decls
            PolluteAPIOfDecl(decl);
            [[fallthrough]];
        case ASTKind::CLASS_DECL: {
            // populate to instance variables only.
            // need not populate to subtype or instance variables of this type of other declarations,
            // because the target of incremental compilation is LLVM IR. In generated LLVM IR, memory
            // alignment of nested super class or instance member is represented as one single block, so the
            // change inside the single injected block does not affect the outer memory alignment.
            for (auto member : decl.GetMemberDeclPtrs()) {
                if (member->astKind == ASTKind::VAR_DECL && !member->TestAttr(AST::Attribute::STATIC)) {
                    PolluteAPIOfDecl(*member);
                }
            }
            PolluteToConstructors(decl);
            break;
        }
        case ASTKind::EXTEND_DECL:
        case ASTKind::INTERFACE_DECL:
            CJC_ABORT();
            break;
        default:
            break;
    }
}

void PollutionAnalyzer::PollutionForVTableChangedDecl(const InheritableDecl& decl)
{
    if (typeChanges[&decl].virt) {
        return;
    }
    typeChanges[&decl].virt = true;

    // 1) Pollute the direct usage of the type decl so its children types can also update VTable
    PolluteDownStreamTypes(decl);
    PolluteBoxUsesFromDecl(decl);

    // 2) Pollute the virtual funcs, so all their usage will be recalculated
    for (auto member : GetMembers(decl)) {
        if (IsVirtual(*member)) {
            PolluteAPIOfDecl(*member);
        }
    }
}

void PollutionAnalyzer::PolluteBodyOfDecl(const Decl& decl)
{
    if (visitedBodyPollutedDecls.find(&decl) != visitedBodyPollutedDecls.end()) {
        return;
    }
    visitedBodyPollutedDecls.emplace(&decl);

    AddToPollutedDecls(decl);
    if (IsUntyped(decl)) {
        PolluteAPIOfDecl(decl);
    }
    PollutedInstantiationChangeFromDecl(decl);
    PolluteCHIROptAffectDecl(decl);
}

std::set<Ptr<const AST::Decl>> PollutionAnalyzer::FindAllExtendDeclsOfType(const RawMangledName& name)
{
    std::set<Ptr<const AST::Decl>> ret{};
    for (auto mangle : t.GetAllExtendsOfType(name)) {
        if (auto it = mangled2Decl.find(mangle); it != mangled2Decl.cend()) {
            (void)ret.insert(it->second);
        }
    }
    return ret;
}

// CPointer<T> and CString are neither builtin types nor keyword types (user can define types with the same
// name), we make special treatments for these two types
static std::optional<std::string> LookupSpecialBuiltinType(const RefType& type)
{
    // pair of typename and type parameter number
    // use vector instead of map because the number is quite small
    // change this to map when the number grow over 8
    static const std::vector<std::pair<std::string_view, size_t>> BUILTIN_NON_PRIMITIVE_TYPES{
        {CPOINTER_NAME, 1}, {CSTRING_NAME, 0}, {CFUNC_NAME, 1}};
    for (auto& pair : BUILTIN_NON_PRIMITIVE_TYPES) {
        if (pair.first == type.ref.identifier.Val() && pair.second == type.typeArguments.size()) {
            return std::string{pair.first};
        }
    }
    return {};
}

std::optional<std::string> PollutionAnalyzer::GetExtendedTypeRawMangleNameImpl(const Type& extendedType)
{
    // If this `type` is from imported AST, then we can get what we want directly from sema ty info
    auto ty = extendedType.GetTy();
    if (Ty::IsTyCorrect(ty)) {
        return Sema::GetTypeRawMangleName(ty);
    }

    const std::string* typeId{nullptr};
    switch (extendedType.astKind) {
        case ASTKind::PRIMITIVE_TYPE: {
            auto primitiveType = StaticCast<const PrimitiveType*>(&extendedType);
            return ASTMangler::ManglePrimitiveType(*primitiveType) +
                MangleUtils::MangleTypeMode(primitiveType->modal.ToModalInfo());
        }
        case ASTKind::REF_TYPE: {
            auto refType = StaticCast<const RefType*>(&extendedType);
            // use special lookup rule for builtin non-primitive types
            if (auto specialName = LookupSpecialBuiltinType(*refType)) {
                return *specialName + MangleUtils::MangleTypeMode(refType->modal.ToModalInfo());
            }
            typeId = &refType->ref.identifier.Val();
            break;
        }
        case ASTKind::QUALIFIED_TYPE: {
            auto qualifiedType = StaticCast<const QualifiedType*>(&extendedType);
            typeId = &qualifiedType->field.Val();
            break;
        }
        // Following type can't be extended
        case ASTKind::OPTION_TYPE:
        case ASTKind::CONSTANT_TYPE:
        case ASTKind::VARRAY_TYPE:
        case ASTKind::FUNC_TYPE:
        case ASTKind::TUPLE_TYPE:
        case ASTKind::PAREN_TYPE:
        case ASTKind::THIS_TYPE:
        case ASTKind::INVALID_TYPE: {
            break;
        }
        // Other types can't be extended
        default: {
            CJC_ABORT();
            break;
        }
    }
    if (typeId) {
        for (auto pair : mangled2Decl) {
            if (pair.second->identifier == *typeId) {
                if (pair.second->IsNominalDecl()) {
                    return pair.first;
                } else if (auto typeAlias = DynamicCast<const TypeAliasDecl*>(pair.second)) {
                    // For type alias, we need to get its real type and then continue the search
                    return GetExtendedTypeRawMangleNameImpl(*typeAlias->type);
                }
            }
        }
    }
    return {};
}

std::optional<std::string> PollutionAnalyzer::GetExtendedTypeRawMangleName(const ExtendDecl& extend)
{
    return GetExtendedTypeRawMangleNameImpl(*extend.extendedType);
}

void PollutionAnalyzer::PolluteAPIOfDecl(const Decl& decl)
{
    if (visitedAPIPollutedDecls.find(&decl) != visitedAPIPollutedDecls.end()) {
        return;
    }
    visitedAPIPollutedDecls.emplace(&decl);

    AddToPollutedDecls(decl);
    PolluteBoxUsesFromDecl(decl);
    PollutedInstantiationChangeFromDecl(decl);

    if (decl.outerDecl && decl.outerDecl->astKind == ASTKind::EXTEND_DECL) {
        AdditionPollutionAPIOfExtendDecl(StaticCast<ExtendDecl&>(*decl.outerDecl));
    }

    switch (decl.astKind) {
        case ASTKind::PROP_DECL: {
            auto propDecl = StaticCast<PropDecl*>(&decl);
            for (auto& getter : propDecl->getters) {
                PolluteAPIOfDecl(*getter);
            }
            for (auto& setter : propDecl->setters) {
                PolluteAPIOfDecl(*setter);
            }
            break;
        }
        case ASTKind::VAR_DECL: {
            // If an instance member variable is polluted in API, then the parent struct/class will be
            // impacted in layout. Mostly this is already handled when we detect the layout hash changes.
            // However, there is a special case when the variable is with enum type, where the layout of enum
            // will be changed according to the constructors inside. Sometimes it is a single integer, sometimes it is a
            // tuple or even a class object. Thus the layout of parent struct/class will be changed in codegen without
            // layout hash changes. So here we make sure the parent decl will be polluted.
            if (!decl.TestAttr(Attribute::STATIC)) {
                if (auto parentDecl = DynamicCast<InheritableDecl*>(decl.outerDecl)) {
                    AddToPollutedDecls(*parentDecl);
                    PollutionForLayoutChangedDecl(*parentDecl);
                }
            }
            [[fallthrough]];
        }
        case ASTKind::FUNC_PARAM:
        case ASTKind::VAR_WITH_PATTERN_DECL:
        case ASTKind::FUNC_DECL:
        case ASTKind::PRIMARY_CTOR_DECL:
        case ASTKind::ENUM_DECL:
        case ASTKind::STRUCT_DECL:
        case ASTKind::CLASS_DECL:
        case ASTKind::INTERFACE_DECL: {
            PollutePreciseUsages(decl);
            PollutedUnqualifiedUses(decl);
            PollutedGlobalChangeToPackageQualifiedUses(decl);
            PolluteGlobalChangeToQualifiedUses(decl);
            for (auto ext : FindAllExtendDeclsOfType(decl.rawMangleName)) {
                AddToPollutedDecls(*ext);
                PollutedInstantiationChangeFromDecl(*ext);
            }
            break;
        }
        case ASTKind::EXTEND_DECL: {
            AdditionPollutionAPIOfExtendDecl(StaticCast<ExtendDecl&>(decl));
            break;
        }
        case ASTKind::MAIN_DECL:
            break;
        default:
            CJC_ABORT();
    }
}

void PollutionAnalyzer::AdditionPollutionAPIOfDirectExtendDecls(const RawMangledName& mangle)
{
    if (auto extendsIt = std::as_const(directExtends).find(mangle); extendsIt != directExtends.cend()) {
        for (auto extendWithSameMangleName : extendsIt->second) {
            PolluteAPIOfDecl(*extendWithSameMangleName);
        }
    }
}

std::unordered_set<Ptr<Decl>> PollutionAnalyzer::GetPollutionResult() const
{
    std::unordered_set<Ptr<Decl>> ret;
    for (auto it : pollutedDecls) {
        (void)ret.insert(Ptr(const_cast<Decl*>(it.get())));
    }

    return ret;
}

std::list<RawMangledName> PollutionAnalyzer::GetDeletedResult() const
{
    return deletedDecls;
}

bool PollutionAnalyzer::NeedPollutedInstantiationChange(const Decl& decl) const
{
    return IsInDeclWithAttribute(decl, Attribute::GENERIC);
}

void PollutionAnalyzer::PollutedInstantiationChangeFromDecl(const Decl& decl)
{
    if (otherChanges[decl.rawMangleName].instantiation) {
        return;
    }
    otherChanges[decl.rawMangleName].instantiation = true;

    if (decl.outerDecl) {
        PollutedInstantiationChangeFromDecl(*decl.outerDecl);
    }
    if (NeedPollutedInstantiationChange(decl)) {
        AddToPollutedDecls(decl);
        const auto& allMembers = GetAllMembers(decl);
        for (const auto member : std::as_const(allMembers)) {
            AddToPollutedDecls(*member);
        }
        PollutePreciseUsages(decl);
    }
}

void PollutionAnalyzer::PolluteCHIROptAffectDecl(const Decl& decl)
{
    // Specially, for VarWithPattern decl, we need to propagate
    // the pollution based on the contained VarDecl
    if (const auto varWithPatternDecl = DynamicCast<const VarWithPatternDecl*>(&decl)) {
        auto allPatterns = FlattenVarWithPatternDecl(*varWithPatternDecl);
        for (auto pattern : allPatterns) {
            if (pattern->astKind != ASTKind::VAR_PATTERN) {
                continue;
            }
            auto varPattern = StaticCast<VarPattern*>(pattern);
            PolluteCHIROptAffectDecl(*varPattern->varDecl);
        }
    }

    auto chirOptIt = chirOptMap.find(decl.rawMangleName);
    if (chirOptIt == chirOptMap.cend()) {
        return;
    }
    if (otherChanges[decl.rawMangleName].chirOpt) {
        return;
    }
    otherChanges[decl.rawMangleName].chirOpt = true;
    for (auto user : chirOptIt->second) {
        // it is a trick where box-generated functions inline a func/var, the ExtendDecl against which the box is
        // generated is marked CHIR opt target. In this case, trigger the propagation rules of box on the extend.
        if (auto extend = DynamicCast<ExtendDecl*>(user)) {
            PolluteAPIOfDecl(*extend);
            PolluteCHIROptAffectDecl(*extend);
        } else {
            PolluteBodyOfDecl(*user);
        }
    }
}

// propagate to box sites and interface extend of the boxed type to correctly trigger rebox. Also propagate to that
// of subclasses
void PollutionAnalyzer::PollutedToBoxUses(const RawMangledName& mangle)
{
    if (otherChanges[mangle].box) {
        return;
    }
    otherChanges[mangle].box = true;
    reBoxedTypes.emplace_back(mangle);

    if (auto usesIt = p.boxUses.find(mangle); usesIt != p.boxUses.cend()) {
        for (auto b : std::as_const(usesIt->second)) {
            PollutionForBodyChangedDecl(*b);
        }
    }

    // The box usages of the downstream type are also impacted
    if (auto decl = mangled2Decl.find(mangle); decl != mangled2Decl.cend()) {
        if (auto children = t.children.find(decl->second); children != t.children.cend()) {
            for (auto downstreamType : children->second) {
                AddToPollutedDecls(*downstreamType);
                PollutedToBoxUses(downstreamType->rawMangleName);
            }
        }
    }

    // Direct extends may share the same mangle name, but they are never propagated to by box. Only interface extends
    // are. Add all interface extends of this type to recompilation to trigger recheck of interface copy behaviour as
    // interfaces may be implemented by extend
    for (auto ext : FindAllExtendDeclsOfType(mangle)) {
        auto extend = StaticCast<ExtendDecl*>(ext);
        if (!extend->inheritedTypes.empty()) {
            AddToPollutedDecls(*extend);
            PolluteCHIROptAffectDecl(*extend);
        }
    }
}

void PollutionAnalyzer::PollutedToExprUsages(const std::set<Ptr<const Decl>>& usages)
{
    for (auto usageDecl : usages) {
        PolluteBodyOfDecl(*usageDecl);
    }
}

void PollutionAnalyzer::PollutePreciseUsages(const Decl& decl)
{
    if (decl.rawMangleName.empty()) {
        return;
    }
    PollutePreciseUsages(decl.rawMangleName);
}
void PollutionAnalyzer::PollutePreciseUsages(const RawMangledName& mangled)
{
    for (auto u : p.directUses[ChangePollutedMap::Idx::BODY][mangled]) {
        PolluteBodyOfDecl(*u);
    }
    for (auto u : p.directUses[ChangePollutedMap::Idx::API][mangled]) {
        PolluteAPIOfDecl(*u);
    }
}
void PollutionAnalyzer::PolluteDownStreamTypes(const Decl& decl)
{
    CJC_ASSERT(decl.astKind == ASTKind::STRUCT_DECL || decl.astKind == ASTKind::CLASS_DECL ||
        decl.astKind == ASTKind::INTERFACE_DECL || decl.astKind == ASTKind::ENUM_DECL ||
        decl.astKind == ASTKind::EXTEND_DECL);

    if (auto extendDecl = DynamicCast<const ExtendDecl*>(&decl)) {
        auto extendedType = extendDecl->extendedType.get();
        std::string extendedTypeIdentifier;
        if (auto rt = DynamicCast<const RefType*>(extendedType)) {
            extendedTypeIdentifier = rt->ref.identifier;
        } else if (auto qt = DynamicCast<const QualifiedType*>(extendedType)) {
            extendedTypeIdentifier = qt->field;
        }

        Ptr<const AST::Decl> extendedTypeDecl = nullptr;
        for (auto element : mangled2Decl) {
            if (element.second->identifier == extendedTypeIdentifier) {
                extendedTypeDecl = element.second;
                break;
            }
        }

        if (extendedTypeDecl) {
            PolluteDownStreamTypes(*extendedTypeDecl);
        } else {
            // so this should be a extend of a imported type decl,
            // how do we check this and what we should do for this?
        }
    } else {
        if (auto it = t.children.find(&decl); it != t.children.cend()) {
            for (auto downstreamTy : it->second) {
                PolluteAPIOfDecl(*downstreamTy);
            }
        }
        if (auto it = t.interfaceExtendTypes.find(decl.rawMangleName); it != t.interfaceExtendTypes.cend()) {
            for (auto f : it->second) {
                if (auto downstreamTy = mangled2Decl.find(f); downstreamTy != mangled2Decl.cend()) {
                    PolluteAPIOfDecl(*downstreamTy->second);
                } else {
                    // For primitive type and built-in type, they don't have decl thus we have to manually
                    // pollute all their other extends
                    for (auto extend : t.GetAllExtendsOfType(f)) {
                        if (auto itt = std::as_const(mangled2Decl).find(extend); itt != mangled2Decl.cend()) {
                            AddToPollutedDecls(*itt->second);
                            PollutedInstantiationChangeFromDecl(*itt->second);
                        }
                    }
                }
            }
        }
    }
}

// populate to changes of a global name to unqualified usages
void PollutionAnalyzer::PollutedUnqualifiedUses(const Decl& decl)
{
    auto identifiers = p.GetAccessibleDeclName(decl); // Get real accessible decl name after alias.
    for (auto& identifier : identifiers) {
        auto& bodyUses = IsImported(decl) ? p.unqUsesOfImported[ChangePollutedMap::Idx::BODY]
                                          : p.unqUses[ChangePollutedMap::Idx::BODY];
        auto it = bodyUses.find(identifier);
        if (it == bodyUses.cend()) {
            continue;
        }
        for (auto& mp : std::as_const(it->second)) {
            for (auto g : mp.second) {
                PolluteBodyOfDecl(*g);
            }
        }
    }
    if (!decl.IsNominalDecl()) {
        return;
    }
    for (auto& identifier : identifiers) {
        // Only type decl needs checking 'API' usage.
        auto& apiUses = IsImported(decl) ? p.unqUsesOfImported[ChangePollutedMap::Idx::API]
                                         : p.unqUses[ChangePollutedMap::Idx::API];
        auto it = apiUses.find(identifier);
        if (it == apiUses.cend()) {
            continue;
        }
        for (auto& mp : std::as_const(it->second)) {
            for (auto g : mp.second) {
                PolluteAPIOfDecl(*g);
            }
        }
    }
}

void PollutionAnalyzer::PolluteGlobalChangeToQualifiedUses(const Decl& decl)
{
    if (!decl.outerDecl) {
        return;
    }
    for (auto user : p.GetQUses(ChangePollutedMap::Idx::BODY, decl.identifier)) {
        PolluteBodyOfDecl(*user);
    }
}

// Populate to changes of a global name to qualified usages.
void PollutionAnalyzer::PollutedGlobalChangeToPackageQualifiedUses(const Decl& decl)
{
    // NOTE: decl alias and it's package's alias cannot exist at same time.
    auto fullPackageNames = p.GetAccessiblePackageName(decl.fullPackageName);
    for (auto& fullPackageName : fullPackageNames) {
        for (auto& user : p.pqUses[ChangePollutedMap::Idx::BODY][decl.identifier][fullPackageName]) {
            PolluteBodyOfDecl(*user);
        }
    }
    if (!decl.IsNominalDecl()) {
        return;
    }
    for (auto& fullPackageName : fullPackageNames) {
        for (auto& user : p.pqUses[ChangePollutedMap::Idx::API][decl.identifier][fullPackageName]) {
            PolluteAPIOfDecl(*user);
        }
    }
}

// Returns true if we can tell incremental compilation must rollback to full
bool PollutionAnalyzer::FallBack() const
{
    return !typeAliases.empty() || !unfoundExtends.empty() || !unfoundNames.empty() || !removedNotSupported.empty();
}

void PollutionAnalyzer::PrintFallbackInfo()
{
    if (!IncrementalCompilationLogger::GetInstance().IsEnable()) {
        return;
    }
    std::stringstream out{};
    for (int i{0}; i < DELIMITER_NUM; ++i) {
        out << '=';
    }
    out << "\nFallback info:\n";
    if (!typeAliases.empty()) {
        for (auto decl : std::as_const(typeAliases)) {
            out << "changed typealias: ";
            PrintDecl(out, *decl);
            out << '\n';
        }
    }
    if (!unfoundExtends.empty()) {
        unfoundExtends.unique();
        for (auto decl : std::as_const(unfoundExtends)) {
            out << "unfound extend: ";
            PrintDecl(out, *decl);
            out << '\n';
        }
    }
    if (!unfoundNames.empty()) {
        unfoundNames.unique();
        for (auto& name : std::as_const(unfoundNames)) {
            out << "unfound name: " << name << '\n';
        }
    }
    for (auto& r : std::as_const(removedNotSupported)) {
        out << "removed type " << r << '\n';
    }
    for (int i{0}; i < DELIMITER_NUM; ++i) {
        out << '=';
    }
    IncrementalCompilationLogger::GetInstance().LogLn(out.str());
}
