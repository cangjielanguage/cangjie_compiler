// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file defines functions for looking up symbol.
 */

#include "TypeCheckUtil.h"
#include "TypeCheckerImpl.h"

#include "cangjie/AST/ScopeManagerApi.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Modules/ModulesUtils.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;

namespace {
class LookUpImpl {
public:
    LookUpImpl(const ASTContext& ctx, DiagnosticEngine& diag, TypeManager& manger, ImportManager& importManager)
        : ctx(ctx), diag(diag), typeManager(manger), importManager(importManager)
    {
    }

    ~LookUpImpl() = default;

    /**
     * Search target by namespace(like class, interface, struct, enum) and field name.
     * NOTE: decl which has 'IN_REFERENCE_CYCLE' should only be intercepted during checking 'inheritedTypes'.
     */
    std::vector<Ptr<Decl>> FieldLookup(Ptr<Decl> decl, const std::string& fieldName, const LookupInfo& info = {});
    std::vector<Ptr<Decl>> Lookup(const std::string& name, const std::string& scopeName, const Node& node,
        bool onlyLookUpTopLevel = false, bool isSetter = false);
    void FieldLookupExtend(
        Ty& ty, const std::string& fieldName, std::vector<Ptr<Decl>>& results, const LookupInfo& info);

private:
    void AddMemberIfValidForLookup(std::vector<Ptr<Decl>>& results, Ty& baseTy, bool isSetter, Decl& decl);
    /**
     * @brief Determine whether @param decl forms an override or implementation with decl in @param results.
     *        If decl in @param results override the @param decl, do nothing.
     *        If @param decl override the decl in @param results, replace the match item in @param results.
     * @param results the results that have been collected.
     * @param decl currently processed declaration.
     * @param baseTy type of MemberAccess' baseExpr.
     * @param parentTy inherited instantiated types. eg:
     *                 'I1<T1> <: I2<Int64>', parentTy should be 'I2<Int64>'.
     *                 'I1<T1> <: I2<T1>', parentTy should be 'I2<T1>'.
     */
    void ResolveOverrideOrShadow(
        std::vector<Ptr<Decl>>& results, Decl& decl, Ptr<Ty> baseTy, Ptr<InterfaceTy> parentTy);
    void FieldLookup(const ClassDecl& cd, const std::string& fieldName, std::vector<Ptr<Decl>>& results,
        const LookupInfo& info);
    void FieldLookup(
        InterfaceTy& idTy, const std::string& fieldName, std::vector<Ptr<Decl>>& results, const LookupInfo& info);
    std::vector<Ptr<Decl>> FieldLookup(const EnumDecl& ed, const std::string& fieldName, const LookupInfo& info);
    std::vector<Ptr<Decl>> FieldLookup(const StructDecl& sd, const std::string& fieldName, const LookupInfo& info);
    std::vector<Ptr<Decl>> FieldLookup(const PackageDecl& pd, const std::string& fieldName);
    std::vector<Ptr<Decl>> StdLibFieldLookup(const Node& node, const std::string& fieldName);
    void ProcessStructDeclBody(
        const std::string& name, const std::string& scopeName, const Node& node, std::vector<Ptr<Decl>>& results);
    bool LookupImpl(const std::string& name, std::string scopeName, const Node& node, bool onlyLookUpTopLevel,
        bool isSetter, std::vector<Ptr<Decl>>& results);
    bool FindRealResult(const Node& node, bool isSetter, std::vector<Ptr<Decl>>& results,
        std::multimap<Position, Ptr<Decl>>& resultsMap, bool isInDeclBody);
    bool IsTargetMemberVisibleToNode(const Decl& target, const Node& node);

    const ASTContext& ctx;
    DiagnosticEngine& diag;
    TypeManager& typeManager;
    ImportManager& importManager;
};

bool IgnoredMember(const Decl& decl)
{
    // The cjdb expression calculation may involve calling the A().init() function within a member function.
    if (decl.TestAttr(Attribute::TOOL_ADD)) {
        return false;
    }
    // Constructor, static init, primary ctor and main entry cannot be used by user, just ignore it in field lookup.
    return decl.TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::MAIN_ENTRY) ||
        decl.astKind == ASTKind::PRIMARY_CTOR_DECL;
}

void UpdatePropOverriddenCache(
    TypeManager& typeManager, const PropDecl& src, std::vector<Ptr<Decl>>& results, Ptr<Ty> baseTy)
{
    for (auto it = results.begin(); it != results.end(); ++it) {
        if (auto fd2 = DynamicCast<PropDecl*>(*it)) {
            IsOverrideOrShadow(typeManager, *fd2, src, baseTy);
        }
    }
}

template <typename T>
inline Ptr<T> GetPlatformDecl(Ptr<Decl> decl)
{
    CJC_ASSERT(decl);
    return StaticCast<T*>(decl->platformImplementation == nullptr ? decl : decl->platformImplementation);
}
} // namespace

void LookUpImpl::AddMemberIfValidForLookup(std::vector<Ptr<Decl>>& results, Ty& baseTy, bool isSetter, Decl& decl)
{
    bool covered{false};
    if (auto pd = DynamicCast<PropDecl*>(&decl); pd) {
        if (!isSetter) {
            if (pd->getters.empty()) {
                return;
            }
        } else {
            if (pd->isVar && pd->setters.empty()) {
                return;
            }
        }
    }
    if (decl.astKind != ASTKind::FUNC_DECL) {
        if (auto prop = DynamicCast<PropDecl>(&decl)) {
            UpdatePropOverriddenCache(typeManager, *prop, results, &baseTy);
        }
        results.push_back(&decl);
        return;
    }
    for (auto it = results.begin(); it != results.end();) {
        if (auto fd2 = DynamicCast<FuncDecl*>(*it);
            fd2 && IsOverrideOrShadow(typeManager, *fd2, static_cast<FuncDecl&>(decl), &baseTy)) {
            // if fd2 is abstract and fd1 is the implementation, then we remove fd2 from results and push
            // fd1 into results
            if (fd2->TestAttr(Attribute::ABSTRACT) && !decl.TestAttr(Attribute::ABSTRACT)) {
                it = results.erase(it);
                results.push_back(&decl);
            }
            covered = true;
            break;
        } else {
            ++it;
        }
    }
    if (!covered) {
        results.push_back(&decl);
    }
}

void LookUpImpl::FieldLookupExtend(
    Ty& ty, const std::string& fieldName, std::vector<Ptr<Decl>>& results, const LookupInfo& info)
{
    CJC_NULLPTR_CHECK(info.file);
    // NOTE: decl which has 'IN_REFERENCE_CYCLE' should only be intercepted during checking 'inheritedTypes'.
    OrderedDeclSet extendFuncs; // Ordered set for diagnostic consistency.
    auto extends = typeManager.GetAllExtendsByTy(ty);
    std::set<Ptr<ExtendDecl>, CmpNodeByPos> orderExtends(extends.begin(), extends.end());
    for (auto& extend : orderExtends) {
        CJC_NULLPTR_CHECK(extend);
        if (!importManager.IsExtendAccessible(*info.file, *extend)) {
            continue;
        }
        for (auto& it : extend->members) {
            if (it->identifier == fieldName) {
                extendFuncs.emplace(it.get());
            }
        }
    }
    results.insert(results.end(), extendFuncs.begin(), extendFuncs.end());
    // For interface functions found in different extend's inherited interfaces, add them to results when:
    // 1. interface function is not shadowed by already found instance functions.
    // 2. interface functions belong to different extends are all needed to be added to the 'results'
    //    since they will not shadow each other in extend. (Collision will be reported later when checking extend)
    for (auto& extend : orderExtends) {
        CJC_NULLPTR_CHECK(extend);
        if (!importManager.IsExtendAccessible(*info.file, *extend)) {
            continue;
        }
        for (auto& it : extend->inheritedTypes) {
            if (it == nullptr) {
                continue;
            }
            if (auto interfaceTy = DynamicCast<InterfaceTy*>(it->ty);
                interfaceTy && interfaceTy->decl && !interfaceTy->decl->TestAttr(Attribute::IN_REFERENCE_CYCLE)) {
                FieldLookup(*interfaceTy, fieldName, results, info);
            }
        }
    }
}

void LookUpImpl::ResolveOverrideOrShadow(
    std::vector<Ptr<Decl>>& results, Decl& decl, Ptr<Ty> baseTy, Ptr<InterfaceTy> parentTy)
{
    if (decl.astKind == ASTKind::PROP_DECL) {
        if (!decl.TestAttr(Attribute::ABSTRACT)) {
            Utils::EraseIf(results, [](auto it) { return it->TestAttr(Attribute::ABSTRACT); });
        }
    }
    for (auto resIter = results.begin(); resIter != results.end(); ++resIter) {
        // Caller guarantees 'decl' must be func or prop.
        if ((*resIter) == nullptr ||
            ((*resIter)->astKind != ASTKind::FUNC_DECL && (*resIter)->astKind != ASTKind::PROP_DECL)) {
            continue;
        }
        auto decl1 = RawStaticCast<Decl*>(&decl);
        auto decl2 = RawStaticCast<Decl*>(*resIter);
        if (!typeManager.PairIsOverrideOrImpl(*decl2, *decl1, baseTy, parentTy) &&
            !typeManager.PairIsOverrideOrImpl(*decl1, *decl2, baseTy)) {
            continue;
        }
        auto isSub = [this](const Ptr<Ty> leaf, const Ptr<Ty> root) {
            auto tys = Promotion(typeManager).Promote(*leaf, *root);
            bool ret = false;
            for (auto ty : tys) {
                ret = typeManager.IsSubtype(leaf, ty) || ret;
            }
            return ret;
        };
        if (isSub(decl1->outerDecl->ty, decl2->outerDecl->ty) ||
            (decl2->TestAttr(Attribute::ABSTRACT) && !decl1->TestAttr(Attribute::ABSTRACT))) {
            // If decl1 override or shadow the decl2, only reserved decl1.
            results.erase(resIter);
            results.emplace_back(&decl);
        } else if (isSub(decl2->outerDecl->ty, decl1->outerDecl->ty) ||
            (decl1->TestAttr(Attribute::ABSTRACT) && !decl2->TestAttr(Attribute::ABSTRACT))) {
            // Do nothing. Reserved decl2.
        } else {
            // If the relationship is not override or shadow, both are saved.
            results.emplace_back(&decl);
        }
        // Otherwise, do not insert current candidate into 'results'.
        return;
    }
    results.emplace_back(&decl);
}

void LookUpImpl::FieldLookup(
    const ClassDecl& cd, const std::string& fieldName, std::vector<Ptr<Decl>>& results, const LookupInfo& info)
{
    // NOTE: decl which has 'IN_REFERENCE_CYCLE' should only be intercepted during checking 'inheritedTypes'.
    if (!cd.body) {
        return;
    }
    std::vector<Ptr<Decl>> decls;
    for (auto& it : cd.body->decls) {
        if (!it || IgnoredMember(*it)) {
            continue;
        }
        decls.push_back(it.get());
    }

    for (auto& decl : decls) {
        auto notValid = decl == nullptr || decl->identifier != fieldName;
        if (notValid) {
            continue;
        }
        auto staticAndNotClassLike = !decl->IsClassLikeDecl() && decl->TestAttr(Attribute::STATIC);
        auto foundTy = info.baseTy;
        if (staticAndNotClassLike) {
            auto genericTy = foundTy ? Ty::GetGenericTyOfInsTy(*foundTy) : nullptr;
            if (genericTy) {
                foundTy = genericTy;
            }
        }
        AddMemberIfValidForLookup(results, *TypeManager::GetNonNullTy(foundTy), info.isSetter, *decl);
    }
    if (!info.lookupInherit) {
        return;
    }
    // Lookup field in super class and its extend or super interfaces.
    for (auto& it : cd.inheritedTypes) {
        auto super = it ? Ty::GetDeclPtrOfTy(it->ty) : nullptr;
        if (!super || super->TestAttr(Attribute::IN_REFERENCE_CYCLE)) {
            continue;
        }
        if (auto superClass = DynamicCast<ClassDecl*>(super)) {
            auto superInfo = info;
            superInfo.lookupExtend = true;
            FieldLookup(*superClass, fieldName, results, superInfo);
        } else if (auto superInterface = DynamicCast<InterfaceDecl*>(super)) {
            auto parentInstTys = Promotion(typeManager).Promote(*cd.ty, *superInterface->ty);
            for (auto parentInstTy : parentInstTys) {
                FieldLookup(*StaticCast<InterfaceTy>(parentInstTy), fieldName, results, {info.baseTy});
            }
        }
    }
    if (info.lookupExtend) {
        FieldLookupExtend(*cd.ty, fieldName, results, info);
    }
}

void LookUpImpl::FieldLookup(
    InterfaceTy& idTy, const std::string& fieldName, std::vector<Ptr<Decl>>& results, const LookupInfo& info)
{
    auto id = GetPlatformDecl<InterfaceDecl>(idTy.declPtr);
    // NOTE: decl which has 'IN_REFERENCE_CYCLE' should only be intercepted during checking 'inheritedTypes'.
    if (!id->body) {
        return;
    }
    for (auto& decl : id->body->decls) {
        if (decl == nullptr || decl->identifier != fieldName) {
            continue;
        }
        // static member can also be inherited
        auto foundTy = info.baseTy;
        if (!decl->IsClassLikeDecl() && decl.get()->TestAttr(Attribute::STATIC)) {
            auto genericTy = foundTy ? Ty::GetGenericTyOfInsTy(*foundTy) : nullptr;
            if (genericTy) {
                foundTy = genericTy;
            }
        }
        if (decl->IsFuncOrProp()) {
            ResolveOverrideOrShadow(results, *decl, foundTy, &idTy);
        } else {
            (void)results.emplace_back(decl.get());
        }
    }
    if (!info.lookupInherit) {
        return;
    }
    // Lookup field in super interfaces.
    for (auto& it : id->inheritedTypes) {
        if (!it) {
            continue;
        }
        if (auto interfaceTy = DynamicCast<InterfaceTy*>(it->ty);
            interfaceTy && interfaceTy->decl && !interfaceTy->decl->TestAttr(Attribute::IN_REFERENCE_CYCLE)) {
            auto promTys = Promotion(typeManager).Promote(idTy, *interfaceTy);
            for (auto promTy : promTys) {
                FieldLookup(*StaticCast<InterfaceTy>(promTy), fieldName, results, {info.baseTy});
            }
        }
    }
}

std::vector<Ptr<Decl>> LookUpImpl::FieldLookup(const EnumDecl& ed, const std::string& fieldName, const LookupInfo& info)
{
    // NOTE: decl which has 'IN_REFERENCE_CYCLE' should only be intercepted during checking 'inheritedTypes'.
    std::vector<Ptr<Decl>> results;
    for (auto& ctor : ed.constructors) {
        if (ctor->identifier == fieldName) {
            results.push_back(ctor.get());
        }
    }
    for (auto& func : ed.members) {
        if (func->identifier == fieldName) {
            results.push_back(func.get());
        }
    }
    for (auto& it : ed.inheritedTypes) {
        if (auto interfaceTy = DynamicCast<InterfaceTy*>(it->ty);
            interfaceTy && interfaceTy->decl && !interfaceTy->decl->TestAttr(Attribute::IN_REFERENCE_CYCLE)) {
            FieldLookup(*interfaceTy, fieldName, results, info);
        }
    }
    if (info.lookupExtend) {
        FieldLookupExtend(*ed.ty, fieldName, results, info);
    }
    return results;
}

std::vector<Ptr<Decl>> LookUpImpl::FieldLookup(
    const StructDecl& sd, const std::string& fieldName, const LookupInfo& info)
{
    // NOTE: decl which has 'IN_REFERENCE_CYCLE' should only be intercepted during checking 'inheritedTypes'.
    std::vector<Ptr<Decl>> results;
    auto bodySetter = [&fieldName, &results](auto& decl) {
        if (decl != nullptr && decl->identifier == fieldName && !IgnoredMember(*decl)) {
            results.push_back(decl.get());
        }
    };
    auto inheritedTypesSetter = [&fieldName, &results, &info, this](auto& it) {
        if (it == nullptr) {
            return;
        }
        if (auto interfaceTy = DynamicCast<InterfaceTy*>(it->ty);
            interfaceTy && interfaceTy->decl && !interfaceTy->decl->TestAttr(Attribute::IN_REFERENCE_CYCLE)) {
            FieldLookup(*interfaceTy, fieldName, results, info);
        }
    };
    std::for_each(sd.body->decls.begin(), sd.body->decls.end(), bodySetter);
    std::for_each(sd.inheritedTypes.begin(), sd.inheritedTypes.end(), inheritedTypesSetter);
    if (info.lookupExtend) {
        FieldLookupExtend(*sd.ty, fieldName, results, info);
    }
    return results;
}

std::vector<Ptr<Decl>> LookUpImpl::FieldLookup(const PackageDecl& pd, const std::string& fieldName)
{
    // Must be imported package decl, decls in source package cannot be accessed by package name.
    CJC_ASSERT(pd.TestAttr(Attribute::IMPORTED));
    auto decls = importManager.GetPackageMembersByName(*pd.srcPackage, fieldName);
    // Main entry cannot be referenced.
    Utils::EraseIf(decls, [](auto decl) { return decl->TestAttr(Attribute::MAIN_ENTRY); });
    return Utils::SetToVec<Ptr<Decl>>(decls);
}

std::vector<Ptr<Decl>> LookUpImpl::FieldLookup(Ptr<Decl> decl, const std::string& fieldName, const LookupInfo& info)
{
    std::vector<Ptr<Decl>> results;
    if (!decl) {
        return results;
    }
    // All method from common type are moved to platform one
    // So looking up method in platform type
    decl = GetPlatformDecl<Decl>(decl);
    if (auto cd = DynamicCast<ClassDecl*>(decl)) {
        FieldLookup(*cd, fieldName, results, info);
        return results;
    }
    if (auto id = DynamicCast<InterfaceDecl*>(decl); id && Ty::IsTyCorrect(id->ty)) {
        CJC_ASSERT(id->ty->kind == TypeKind::TYPE_INTERFACE);
        FieldLookup(*StaticCast<InterfaceTy>(id->ty), fieldName, results, info);
        return results;
    }
    if (auto ed = DynamicCast<EnumDecl*>(decl)) {
        return FieldLookup(*ed, fieldName, info);
    }
    if (auto sd = DynamicCast<StructDecl*>(decl)) {
        return FieldLookup(*sd, fieldName, info);
    }
    if (auto pd = DynamicCast<PackageDecl*>(decl)) {
        // Lookup package decl.
        return FieldLookup(*pd, fieldName);
    }
    return results;
}

std::vector<Ptr<Decl>> LookUpImpl::StdLibFieldLookup(const Node& node, const std::string& fieldName)
{
    std::vector<Ptr<Decl>> results;
    Ptr<Decl> target = nullptr;
    if (node.TestAttr(Attribute::IN_CORE)) {
        target = importManager.GetCoreDecl(fieldName);
    } else if (node.TestAttr(Attribute::IN_MACRO)) {
        target = importManager.GetAstDecl(fieldName);
    }
    if (target) {
        results.emplace_back(target);
    }
    return results;
}

namespace {
/** Check whether the @p target is defined after @p ref node. */
inline bool IsDefinedAfter(const Decl& target, const Node& ref)
{
    return target.begin > ref.begin;
}

bool IsNodeInVarDecl(const ASTContext& ctx, const Node& node, VarDecl& vd)
{
    bool found = false;
    auto& nodeToSearch = ctx.GetOuterVarDeclAbstract(vd);
    Walker walker(&nodeToSearch, [&node, &found](Ptr<const Node> n) -> VisitAction {
        if (n == &node) {
            found = true;
            return VisitAction::STOP_NOW;
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    return found;
}

bool IsNodeInDestructed(const Node& node, VarDecl& vd)
{
    if (!vd.parentPattern || !vd.parentPattern->ctxExpr) {
        return false;
    }
    bool found = false;
    Walker walker(vd.parentPattern->ctxExpr, [&node, &found](Ptr<const Node> n) -> VisitAction {
        if (n == &node) {
            found = true;
            return VisitAction::STOP_NOW;
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    return found;
}

bool IsNodeInTypeAliasDecl(const Node& node, const TypeAliasDecl& tad)
{
    bool found = false;
    Walker walker(tad.type.get(), [&node, &found](Ptr<const Node> n) -> VisitAction {
        if (n == &node) {
            found = true;
            return VisitAction::STOP_NOW;
        }
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    return found;
}

bool IsTargetVisibleToNode(const Decl& target, const Node& node)
{
    // In the LSP, the 'node' may be a new ast node, 'curFile' pointer consistency cannot be ensured.
    return !target.TestAttr(Attribute::PRIVATE) || (target.curFile && node.curFile && *target.curFile == *node.curFile);
}
} // namespace

bool LookUpImpl::IsTargetMemberVisibleToNode(const Decl& target, const Node& node)
{
    // In the LSP, the 'node' may be a new ast node, 'curFile' pointer consistency cannot be ensured.
    if (target.curFile && node.curFile && *target.curFile == *node.curFile) {
        return true;
    }
    Symbol* sym = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, node.scopeName);
    return IsLegalAccess(sym, target, node, importManager, typeManager);
}

bool LookUpImpl::FindRealResult(const Node& node, bool isSetter, std::vector<Ptr<Decl>>& results,
    std::multimap<Position, Ptr<Decl>>& resultsMap, bool isInDeclBody)
{
    // If previous found targets are not empty and not all function decls, no need to find more from parent scope.
    bool wasAllFunction = !results.empty() && IsAllFuncDecl(results);
    if (!results.empty() && !wasAllFunction) {
        return true;
    }
    for (auto it : std::as_const(resultsMap)) {
        auto targetDecl = it.second;
        // Compiler added init FuncParam, and it's RHS expr of Assignment.
        bool initFuncParam = targetDecl->begin == INVALID_POSITION && node.begin == INVALID_POSITION;
        // Compiler added init LHS expr of Assignment.
        bool initAssignLHS = node.begin == INVALID_POSITION && node.scopeLevel > targetDecl->scopeLevel;
        if (targetDecl->astKind == ASTKind::VAR_DECL) {
            auto vd = RawStaticCast<VarDecl*>(targetDecl);
            initAssignLHS = initAssignLHS || vd->isResourceVar;
        }
        // Toplevel decls, static decls, compiler added init parameter and left expression are not order related.
        bool orderRelated = !initFuncParam && !initAssignLHS && !(targetDecl->scopeLevel == 0 || isInDeclBody);
        if ((orderRelated && IsDefinedAfter(*targetDecl, node)) || IgnoredMember(*targetDecl)) {
            continue; // Ignore target defined after reference node.
        }
        // If found targets in parent scope are all function decls,
        // stop finding other result from current scope when any non-function is found.
        if ((wasAllFunction && !targetDecl->IsFunc())) {
            return true;
        }
        if (targetDecl->astKind == ASTKind::TYPE_ALIAS_DECL) {
            // Should not put the target type alias which the initializer include this node.
            auto tad = RawStaticCast<TypeAliasDecl*>(targetDecl);
            if (IsNodeInTypeAliasDecl(node, *tad)) {
                continue;
            }
        }
        if (!Is<VarDecl>(targetDecl)) {
            results.emplace_back(RawStaticCast<Decl*>(targetDecl));
            continue;
        }
        // Should not put the target varDecl which the initializer include this node.
        auto vd = RawStaticCast<VarDecl*>(targetDecl);
        if (targetDecl->astKind != ASTKind::PROP_DECL &&
            (IsNodeInVarDecl(ctx, node, *vd) || IsNodeInDestructed(node, *vd))) {
            continue;
        }
        if (targetDecl->astKind == ASTKind::PROP_DECL) {
            auto pd = RawStaticCast<PropDecl*>(targetDecl);
            auto needContinue = (isSetter && pd->isVar && pd->setters.empty()) || (!isSetter && pd->getters.empty());
            if (needContinue) {
                continue;
            }
        }
        results.emplace_back(vd);
        // If the node is a RefExpr of CallExpr, we should continue to collect all candidate Decls.
        if (auto refExpr = DynamicCast<const RefExpr*>(&node); refExpr && refExpr->callOrPattern) {
            continue;
        }
        // Otherwise, we only collect one VarDecl by shadow rules.
        return true;
    }
    return false;
}

void LookUpImpl::ProcessStructDeclBody(
    const std::string& name, const std::string& scopeName, const Node& node, std::vector<Ptr<Decl>>& results)
{
    // Lookup for inherited members, eg:
    // 1. from subclass body finding any member from parent class
    // 2. from extend body finding any member from the extened type decl.
    auto parentScopeName = ScopeManagerApi::GetParentScopeName(scopeName);
    auto parentSopeGateName = ScopeManagerApi::GetScopeGateName(parentScopeName);
    auto parentScopeGateSym = ScopeManagerApi::GetScopeGate(ctx, parentSopeGateName);
    if (parentScopeGateSym == nullptr) {
        return;
    }
    CJC_NULLPTR_CHECK(node.curFile);
    auto currentDecl = StaticCast<Decl*>(parentScopeGateSym->node);
    LookupInfo info{
        .baseTy = currentDecl->ty, .file = node.curFile, .lookupExtend = currentDecl->astKind == ASTKind::EXTEND_DECL};
    auto typeDecl = Ty::GetDeclPtrOfTy(currentDecl->ty);
    if (!typeDecl) {
        // Lookup for extend of builtin type.
        FieldLookupExtend(*currentDecl->ty, name, results, info);
        return;
    }
    auto fields = FieldLookup(typeDecl, name, info);
    for (auto it : fields) {
        if (auto vd = DynamicCast<VarDecl*>(it);
            vd && it->astKind != ASTKind::PROP_DECL && IsNodeInVarDecl(ctx, node, *vd)) {
            continue;
        } else if (IsTargetMemberVisibleToNode(*it, node)) {
            results.emplace_back(it);
        }
    }
}

bool LookUpImpl::LookupImpl(const std::string& name, std::string scopeName, const Node& node, bool onlyLookUpTopLevel,
    bool isSetter, std::vector<Ptr<Decl>>& results)
{
    do {
        auto targetDecls = ctx.GetDeclsByName({name, scopeName});
        std::multimap<Position, Ptr<Decl>> resultsMap;
        for (auto decl : targetDecls) {
            CJC_NULLPTR_CHECK(decl);
            if (IsTargetVisibleToNode(*decl, node)) {
                resultsMap.emplace(decl->begin, decl);
            }
        }
        auto scopeGateName = ScopeManagerApi::GetScopeGateName(scopeName);
        auto scopeGateSym = ScopeManagerApi::GetScopeGate(ctx, scopeGateName);
        bool isInDeclBody = scopeGateSym != nullptr && scopeGateSym->node != nullptr &&
            (scopeGateSym->node->IsNominalDeclBody() || scopeGateSym->node->TestAttr(Attribute::IN_EXTEND) ||
                scopeGateSym->node->astKind == ASTKind::ENUM_DECL);
        if (FindRealResult(node, isSetter, results, resultsMap, isInDeclBody)) {
            return true;
        }
        // onlyLookUpTopLevel is a flag to mark that the LookUp is invoked at resolve decls stage of PreCheck, the
        // reference type must be at top-level.
        if (scopeGateSym && scopeGateSym->node && scopeGateSym->node->IsNominalDeclBody() && !onlyLookUpTopLevel) {
            ProcessStructDeclBody(name, scopeName, node, results);
        }
        scopeName = ScopeManagerApi::GetParentScopeName(scopeName);
        if (!results.empty() && Is<VarDecl>(results[0])) {
            // For var, only find the nearest targets.
            return true;
        }
    } while (!scopeName.empty());
    return false;
}

std::vector<Ptr<Decl>> LookUpImpl::Lookup(
    const std::string& name, const std::string& scopeName, const Node& node, bool onlyLookUpTopLevel, bool isSetter)
{
    std::vector<Ptr<Decl>> results = StdLibFieldLookup(node, name);
    if (!results.empty()) {
        return results;
    }
    if (name == INVALID_IDENTIFIER) {
        return results;
    }
    if (scopeName.empty()) {
        diag.Diagnose(node, DiagKind::sema_symbol_not_collected, name);
        return results;
    }
    if (LookupImpl(name, scopeName, node, onlyLookUpTopLevel, isSetter, results)) {
        return results;
    }

    // If the targets is not empty and the target is not function but other decls, no need to search in imported
    // decl collections.
    if (!results.empty() && !IsAllFuncDecl(results)) {
        return results;
    } else {
        // Insert import symbols (already sorted by API).
        auto importDecls = importManager.GetImportedDeclsByName(*node.curFile, name);
        results.insert(results.end(), importDecls.begin(), importDecls.end());
    }

    // Remove duplicate function declarations.
    for (auto it = results.begin(); it != results.end(); ++it) {
        bool self = true;
        for (auto i = it; i != results.end();) {
            if (self) {
                self = false;
            } else if (*it == *i) {
                i = results.erase(i);
                continue;
            }
            ++i;
        }
    }
    return results;
}

std::vector<Ptr<Decl>> TypeChecker::TypeCheckerImpl::FieldLookup(
    const ASTContext& ctx, Ptr<Decl> decl, const std::string& fieldName, const LookupInfo& info)
{
    LookUpImpl lookUpImpl(ctx, diag, typeManager, importManager);
    return lookUpImpl.FieldLookup(decl, fieldName, info);
}

std::vector<Ptr<Decl>> TypeChecker::TypeCheckerImpl::Lookup(
    const ASTContext& ctx, const std::string& name, const std::string& scopeName, const Node& node, bool isSetter)
{
    LookUpImpl lookUpImpl(ctx, diag, typeManager, importManager);
    return lookUpImpl.Lookup(name, scopeName, node, false, isSetter);
}

std::vector<Ptr<Decl>> TypeChecker::TypeCheckerImpl::LookupTopLevel(
    const ASTContext& ctx, const std::string& name, const std::string& scopeName, const Node& node, bool isSetter)
{
    LookUpImpl lookUpImpl(ctx, diag, typeManager, importManager);
    return lookUpImpl.Lookup(name, scopeName, node, true, isSetter);
}

std::vector<Ptr<Decl>> TypeChecker::TypeCheckerImpl::ExtendFieldLookup(
    const ASTContext& ctx, const File& file, Ptr<Ty> ty, const std::string& fieldName)
{
    LookUpImpl lookUpImpl(ctx, diag, typeManager, importManager);
    std::vector<Ptr<Decl>> results = {};
    if (Ty::IsTyCorrect(ty)) {
        LookupInfo info{ty, &file};
        lookUpImpl.FieldLookupExtend(*ty, fieldName, results, info);
    }
    return results;
}
