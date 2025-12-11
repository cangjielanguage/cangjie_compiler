// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements inheritance checking of structure declarations.
 */
#include "StructInheritanceChecker.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Modules/ModulesUtils.h"
#include "cangjie/Sema/TestManager.h"
#include "cangjie/Sema/TypeManager.h"
#include "../NativeFFI/Java/AfterTypeCheck/Utils.h"


#include "Diags.h"
#include "TypeCheckUtil.h"
#include "TypeCheckerImpl.h"
#include "NativeFFI/Java/TypeCheck/InheritanceChecker.h"

using namespace Cangjie;
using namespace AST;
using namespace Sema;
using namespace TypeCheckUtil;

namespace {
/** Remove members which cannot be inherited. */
void RemoveMembersShouldNotInherit(MemberMap& members)
{
    for (auto it = members.begin(); it != members.end();) {
        it = it->second.decl->TestAttr(Attribute::PRIVATE) ? members.erase(it) : ++it;
    }
}

bool IsInvisibleMember(const Decl& member, const std::string& currentPkg)
{
    if (member.TestAttr(Attribute::PRIVATE)) {
        return true;
    }
    auto relation = Modules::GetPackageRelation(currentPkg, member.fullPackageName);
    // 'IsVisible' is used for toplevel decl, member decl with 'PROTECTED' is also visible in children.
    return !Modules::IsVisible(member, relation) && !member.TestAttr(Attribute::PROTECTED);
}

/** Remove members which is only visible in defined package. */
void RemoveInvisibleMember(MemberMap& members, const std::string& currentPkg)
{
    for (auto it = members.cbegin(); it != members.cend();) {
        CJC_NULLPTR_CHECK(it->second.decl);
        it = IsInvisibleMember(*it->second.decl, currentPkg) ? members.erase(it) : ++it;
    }
}

bool AreFuncParameterNameIdentical(const FuncDecl& parent, const FuncDecl& child)
{
    CJC_NULLPTR_CHECK(parent.funcBody->paramLists[0]);
    CJC_NULLPTR_CHECK(child.funcBody->paramLists[0]);
    auto& parentParams = parent.funcBody->paramLists[0]->params;
    auto& childParams = child.funcBody->paramLists[0]->params;
    CJC_ASSERT(parentParams.size() == childParams.size());
    for (size_t i = 0; i < parentParams.size(); ++i) {
        auto& parentParam = parentParams[i];
        auto& childParam = childParams[i];
        CJC_NULLPTR_CHECK(parentParam);
        CJC_NULLPTR_CHECK(childParam);
        if (parentParam->isNamedParam != childParam->isNamedParam) {
            return false;
        }
        if (parentParam->isNamedParam && parentParam->identifier != childParam->identifier) {
            return false;
        }
    }
    return true;
}

void DiagnoseParameterName(DiagnosticEngine& diag, const FuncDecl& parent, const FuncDecl& child)
{
    // The caller should guarantee the pointers are non-null and valid!
    auto diagBuilder =
        diag.DiagnoseRefactor(DiagKindRefactor::sema_param_named_mismatched, child, MakeRange(child.identifier));
    auto& parentParams = parent.funcBody->paramLists[0]->params;
    auto& childParams = child.funcBody->paramLists[0]->params;
    CJC_ASSERT(parentParams.size() == childParams.size());
    for (size_t i = 0; i < parentParams.size(); ++i) {
        auto& parentParam = parentParams[i];
        auto& childParam = childParams[i];
        if (!parentParam->isNamedParam && childParam->isNamedParam) {
            diagBuilder.AddHint(MakeRange(childParam->GetIdentifierPos(), childParam->identifier),
                "this named parameter conflicts with the corresponding positional parameter in the supertype");
        } else if (parentParam->isNamedParam && !childParam->isNamedParam) {
            diagBuilder.AddHint(MakeRange(childParam->GetIdentifierPos(), childParam->identifier),
                "this positional parameter conflicts with the corresponding named parameter in the supertype");
        } else if (parentParam->isNamedParam && childParam->isNamedParam &&
            parentParam->identifier != childParam->identifier) {
            diagBuilder.AddHint(MakeRange(childParam->GetIdentifierPos(), childParam->identifier),
                "the corresponding parameter in the supertype is named '" + parentParam->identifier + "'");
        }
    }
    auto subDiag = SubDiagnostic(
        MakeRange(parent.GetIdentifierPos(), parent.identifier), "the corresponding function of the supertype is");
    diagBuilder.AddNote(subDiag);
}

std::string StringifyInconsistentTypes(
    const std::unordered_set<Ptr<const Ty>>& inconsistentTypes, Ptr<const Ty> childTy)
{
    std::set<Ptr<const Ty>, CmpTyByName> sortedTys(inconsistentTypes.cbegin(), inconsistentTypes.cend());
    sortedTys.erase(childTy);
    return "'" + Ty::GetTypesToStr(sortedTys, "', '") + "'";
}

std::vector<std::unordered_set<Ptr<Ty>>> GetAllGenericUpperBounds(TypeManager& tyMgr, const Decl& decl)
{
    auto generic = decl.GetGeneric();
    if (!generic) {
        return {};
    }
    std::vector<std::unordered_set<Ptr<Ty>>> allUpperBounds;
    for (auto& type : generic->typeParameters) {
        auto genericTy = DynamicCast<GenericsTy*>(type->ty);
        if (!genericTy) {
            continue;
        }
        // NOTE: Since 'upperBounds' contains all direct and transitive non-generic upperbounds,
        //       we also need to collect generic upperBounds to check generic constraints correctly.
        std::set<Ptr<Ty>> tys(genericTy->upperBounds.begin(), genericTy->upperBounds.end());
        std::queue<Ptr<GenericsTy>> q;
        q.push(genericTy);
        std::unordered_set<Ptr<GenericsTy>> traversedTy = {};
        while (!q.empty()) {
            auto gTy = q.front();
            q.pop();
            if (auto [_, success] = traversedTy.emplace(gTy); !success) {
                continue;
            }
            for (auto upper : gTy->upperBounds) {
                if (upper->IsGeneric()) {
                    q.push(RawStaticCast<GenericsTy*>(upper));
                    tys.emplace(upper);
                }
            }
        }
        if (tys.size() > 1) {
            allUpperBounds.emplace_back(std::unordered_set<Ptr<Ty>>{tyMgr.GetIntersectionTy(tys)});
        } else {
            allUpperBounds.emplace_back(tys.begin(), tys.end());
        }
    }
    return allUpperBounds;
}

// Caller guarantees the given 'index' has corresponding constraint node.
Ptr<Node> GetConstraintNodeByIndex(size_t index, Generic& generic)
{
    CJC_ASSERT(index < generic.typeParameters.size());
    CJC_NULLPTR_CHECK(generic.typeParameters[index]);
    auto gTy = generic.typeParameters[index]->ty;
    for (auto& constraint : generic.genericConstraints) {
        CJC_NULLPTR_CHECK(constraint->type);
        if (constraint->type->ty == gTy) {
            return constraint.get();
        }
    }
    // Should not reach here.
    CJC_ABORT();
    return generic.typeParameters[index].get();
}

void DiagWeakVisibility(DiagnosticEngine& diag, const Decl& parent, const Decl& child, const Decl& diagNode)
{
    auto visibilityToString = [](const Decl& decl) -> std::string {
        if (decl.TestAttr(Attribute::PUBLIC)) {
            return "public";
        } else if (decl.TestAttr(Attribute::PROTECTED)) {
            return "protected";
        } else if (decl.TestAttr(Attribute::PRIVATE)) {
            return "private";
        } else {
            return "internal";
        }
    };
    auto parentRange = MakeRangeForDeclIdentifier(parent);
    auto childRange = MakeRangeForDeclIdentifier(child);
    std::string childHint =
        "the visibility of the deriving '" + child.identifier + "' is '" + visibilityToString(child) + "'";
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_weak_visibility, MakeRangeForDeclIdentifier(diagNode));
    if (&child == &diagNode) {
        builder.AddMainHintArguments(childHint);
    } else { // child is inherited from other parent
        builder.AddMainHintArguments("'" + child.identifier + "' is inherited");
        SubDiagnostic childNote(childRange, "the deriving '" + child.identifier + "' is defined here");
        childNote.AddMainHint(childRange, childHint);
        builder.AddNote(childNote);
    }
    SubDiagnostic parentNote(
        parentRange, "the visibility of the base '" + parent.identifier + "' is '" + visibilityToString(parent) + "'");
    if (parent.outerDecl->astKind == ASTKind::INTERFACE_DECL) {
        parentNote.AddMainHint(parentRange, "the base '" + parent.identifier + "' is in 'interface'");
    }
    builder.AddNote(parentNote);
}

// If node is from common part, check for decls extend or implement common decl.
bool NeedRecheck(InheritableDecl& id)
{
    auto superDecls = id.GetAllSuperDecls();
    std::vector<Ptr<ClassLikeDecl>> extendDecls;
    if (id.astKind == ASTKind::EXTEND_DECL) {
        auto ed = RawStaticCast<ExtendDecl*>(&id);
        if (auto decl = Ty::GetDeclPtrOfTy(ed->extendedType->ty); decl && decl->IsNominalDecl()) {
            // extend in common part for platform decl.
            if (decl->TestAttr(Attribute::PLATFORM)) {
                return true;
            }
            extendDecls = RawStaticCast<InheritableDecl*>(decl)->GetAllSuperDecls();
        }
    }
    auto checkCommon = [](Ptr<ClassLikeDecl> it) { return it->TestAttr(Attribute::COMMON); };
    return std::any_of(superDecls.cbegin(), superDecls.cend(), checkCommon) ||
        std::any_of(extendDecls.cbegin(), extendDecls.cend(), checkCommon);
}

// Check whether inheritance rules need to be checked.
bool NeedCheck(Node& node)
{
    if (!node.IsNominalDecl()) {
        return false;
    }
    // Only incremental compilation case will meet generic instantiated decl.
    // Decl from common part do not check again.
    if (node.TestAttr(Attribute::GENERIC_INSTANTIATED)) {
        return false;
    }
    // If node is from common part, check for decls extend or implement common decl.
    if (node.TestAttr(Attribute::FROM_COMMON_PART)) {
        // Except common decls.
        if (node.TestAttr(Attribute::COMMON)) {
            return false;
        }
        return NeedRecheck(*RawStaticCast<InheritableDecl*>(&node));
    }
    return true;
}

bool CompMemberSignatureByPosAndTy(Ptr<const MemberSignature> m1, Ptr<const MemberSignature> m2)
{
    if (m1->decl != m2->decl) {
        return CompNodeByPos(m1->decl, m2->decl);
    }
    return CompTyByNames(m1->ty, m2->ty);
}
} // namespace

void TypeChecker::TypeCheckerImpl::CheckInheritance(Package& pkg)
{
    StructInheritanceChecker checker(diag, typeManager, pkg, importManager, ci->invocation.globalOptions);
    checker.Check();
}

void StructInheritanceChecker::Check()
{
    if (pkg.TestAnyAttr(Attribute::IMPORTED, Attribute::TOOL_ADD)) {
        return;
    }
    std::vector<Ptr<InheritableDecl>> structDecls;
    std::vector<Ptr<ExtendDecl>> extendDecls;
    Walker(&pkg, [&structDecls, &extendDecls](auto node) {
        if (NeedCheck(*node)) {
            if (node->astKind == ASTKind::EXTEND_DECL) {
                extendDecls.emplace_back(RawStaticCast<ExtendDecl*>(node));
            } else {
                structDecls.emplace_back(RawStaticCast<InheritableDecl*>(node));
            }
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    auto sortedExtends = GetAllNeedCheckExtended();
    extendDecls.insert(extendDecls.end(), sortedExtends.cbegin(), sortedExtends.cend());
#endif

    for (auto decl : structDecls) {
        if (decl->TestAttr(Attribute::IS_BROKEN) || !Ty::IsTyCorrect(decl->ty)) {
            continue; // Do not check for incorrect structure declaration.
        }
        CheckMembersWithInheritedDecls(*decl);
    }
    for (auto decl : extendDecls) {
        if (decl->TestAttr(Attribute::IS_BROKEN) || !Ty::IsTyCorrect(decl->ty) || !IsExtendVisibleInCurpkg(*decl)) {
            continue; // Do not check for incorrect structure declaration.
        }
        CheckMembersWithInheritedDecls(*decl);
    }
    // Check generic upper bounds member confliction.
    CheckAllUpperBoundsConfliction();
    // Check function confliction for instantiated decl.
    CheckInstDupFuncsInNominalDecls();
}

void StructInheritanceChecker::CheckMembersWithInheritedDecls(const InheritableDecl& decl)
{
    if (structInheritedMembers.count(&decl) > 0) {
        return;
    }
    checkingDecls.push_back(&decl);
    MemberMap interfaceMembers = GetAndCheckInheritedInterfaces(decl);
    MemberMap instanceMembers = GetAndCheckInheritedMembers(decl);
    auto [visibleExtendMembers, invisibleMembers] = GetVisibleExtendMembersForExtend(decl);
    // 0. Merge inherited members for extend decl. Must merge 'instanceMembers' to 'visibleExtendMembers'.
    // 'instanceMembers' will replace any interface members in 'visibleExtendMembers'.
    MergeInheritedMembers(visibleExtendMembers, instanceMembers, *decl.ty);
    // Spec check: "The exported extension cannot indirectly export the functions of the non-exported extension."
    for (auto& interface : interfaceMembers) {
        CheckExtendExportDependence(decl, interface.second, visibleExtendMembers);
    }
    // 1. Merge & check members inherited in from super class or extended type of extend decl first.
    for (auto& member : decl.GetMemberDecls()) {
        if (!Ty::IsTyCorrect(member->ty) || !member->outerDecl || member->TestAttr(Attribute::CONSTRUCTOR)) {
            continue;
        }
        std::pair<MemberMap::const_iterator, MemberMap::const_iterator> inherited =
            visibleExtendMembers.equal_range(member->identifier);
        MemberSignature memberSig{member, member->ty, decl.ty, nullptr, GetAllGenericUpperBounds(typeManager, *member)};
        for (auto it = inherited.first; it != inherited.second; ++it) {
            DiagnoseForInheritedMember(it->second, memberSig);
        }
        std::pair<MemberMap::const_iterator, MemberMap::const_iterator> invisibleMatches =
            invisibleMembers.equal_range(member->identifier);
        for (auto it = invisibleMatches.first; it != invisibleMatches.second; ++it) {
            DiagnoseForInheritedMember(it->second, memberSig);
        }
        memberSig = UpdateInheritedMemberIfNeeded(visibleExtendMembers, memberSig);
    }
    // When processing extend decl, do not report error for interface implementation.
    // 2. Check whether every interface is implemented.
    for (auto& interface : interfaceMembers) {
        interface.second.isInheritedInterface = true;
        DiagnoseForInheritedInterfaces(interface.second, visibleExtendMembers);
    }
    // 3. Merge final inherited members. 'visibleExtendMembers' is merged to 'interfaceMembers'.
    MergeInheritedMembers(interfaceMembers, visibleExtendMembers, *decl.ty);
    for (const auto& memberSig : std::as_const(interfaceMembers)) {
        DiagnoseForOverriddenMember(memberSig.second);
    }
    // 4. Check unimplemented interface function.
    DiagnoseForUnimplementedInterfaces(interfaceMembers, decl);
    RemoveMembersShouldNotInherit(interfaceMembers);
    checkingDecls.pop_back();
    // Unset local checking status.
    for (auto& member : interfaceMembers) {
        member.second.isInheritedInterface = false;
        member.second.extendDecl = nullptr;
    }
    structInheritedMembers.emplace(&decl, interfaceMembers);
}

/**
 * Return inherited interfaces' members's map with instantiated type declared in inheritance.
 * eg: class A <: I1<Int32> & I2 -> will return members with ty instantiated as I<Int32> I2.
 */
MemberMap StructInheritanceChecker::GetAndCheckInheritedInterfaces(const InheritableDecl& decl)
{
    std::set<Ptr<Ty>, CmpTyByName> interfaceTys;
    for (auto& type : decl.inheritedTypes) {
        auto baseDecl = Ty::GetDeclPtrOfTy<InheritableDecl>(type->ty);
        if (!baseDecl || baseDecl->astKind != ASTKind::INTERFACE_DECL ||
            baseDecl->TestAttr(Attribute::IN_REFERENCE_CYCLE)) {
            continue;
        }
        CheckMembersWithInheritedDecls(*baseDecl);
        interfaceTys.emplace(type->ty);
    }
    MemberMap members;
    // Merge inherited interfaces' members.
    for (auto iTy : interfaceTys) {
        auto interfaceDecl = Ty::GetDeclPtrOfTy<InheritableDecl>(iTy);
        auto interfaceMembers = structInheritedMembers[interfaceDecl];
        MergeInheritedMembers(members, interfaceMembers, *iTy, true);
    }
    DiagnoseForConflictInheritance(decl, members);
    return members;
}

/**
 * Get given decl's members' map with extended members of geiven decl.
 * eg: class A <: B -> will return members in B and in extend decls of B.
 *     extend A -> will return members in A.
 */
MemberMap StructInheritanceChecker::GetInheritedSuperMembers(
    const InheritableDecl& decl, Ty& baseTy, const File& curFile, bool ignoreExtends)
{
    MemberMap members;
    // Merge inherited class members.
    MergeInheritedMembers(members, structInheritedMembers[&decl], baseTy);
    RemoveInvisibleMember(members, curFile.curPackage->fullPackageName);
    if (ignoreExtends) {
        return members;
    }
    // Merge extend decls' members of given class.
    auto extends = typeManager.GetDeclExtends(decl);
    std::set<Ptr<ExtendDecl>, CmpNodeByPos> ordered(extends.begin(), extends.end());
    for (auto extend : ordered) {
        if (!extend->extendedType || !Ty::IsTyCorrect(extend->extendedType->ty)) {
            continue;
        }
        if (!importManager.IsExtendAccessible(curFile, *extend)) {
            continue;
        }
        CheckMembersWithInheritedDecls(*extend);
        auto extendMembers = structInheritedMembers[extend];
        RemoveInvisibleMember(members, curFile.curPackage->fullPackageName);
        MergeInheritedMembers(members, extendMembers, baseTy);
    }
    return members;
}

/**
 * Return inherited members' map with instantiated type declared in class inheritance.
 * Or inherited members of extended type of extend decl.
 * eg: class A <: B -> will return members in B and in extend decls of B.
 *     extend A -> will return members in A.
 */
MemberMap StructInheritanceChecker::GetAndCheckInheritedMembers(const InheritableDecl& decl)
{
    if (!Ty::IsTyCorrect(decl.ty)) {
        return {};
    }
    Ptr<InheritableDecl> baseDecl = nullptr;
    Ptr<Ty> baseTy = nullptr;
    if (decl.astKind == ASTKind::CLASS_DECL) {
        auto& cd = static_cast<const ClassDecl&>(decl);
        baseDecl = cd.GetSuperClassDecl();
        baseTy = RawStaticCast<ClassTy*>(cd.ty)->GetSuperClassTy();
    } else if (decl.astKind == ASTKind::EXTEND_DECL) {
        auto& ed = static_cast<const ExtendDecl&>(decl);
        if (ed.extendedType) {
            baseDecl = Ty::GetDeclPtrOfTy<InheritableDecl>(ed.extendedType->ty);
            baseTy = ed.extendedType->ty;
        }
    }
    if (!baseDecl || !Ty::IsTyCorrect(baseTy) || baseDecl->TestAttr(Attribute::IN_REFERENCE_CYCLE)) {
        return {};
    }
    // If common decl is with platform implementation, using platform one.
    if (baseDecl->platformImplementation) {
        baseDecl = RawStaticCast<InheritableDecl*>(baseDecl->platformImplementation);
    }
    CheckMembersWithInheritedDecls(*baseDecl);
    if (decl.curFile) {
        return GetInheritedSuperMembers(*baseDecl, *baseTy, *decl.curFile, decl.astKind == ASTKind::EXTEND_DECL);
    }
    return {};
}

/**
 * Spec: If the parent and child interfaces are implemented separately for extensions of the same type within the same
 * package, then the compiler will first check for extensions that implement the parent interface and then for
 * extensions that implement the child interface.
 *
 * So when checking extend declarations, only the parent interface extension of the current extension implementing
 * interface are collected.
 * If extended interface have no parent-child relationship, both extensions are collected.
 * If the interfaces of two extension implementations are cross-inherited, an error is reported.
 *
 * eg: interface I1 {}
 *     interface I2 <: I1 {}
 *     class A {}
 *     extend A <: I1 {}
 *     extend A <: I2 {}
 * when check 'extend A <: I1', member of 'extend A <: I2' will not be collected.
 * when check 'extend A <: I2', both will be collected.
 */
void StructInheritanceChecker::CollectExtendByInterfaceInherit(const std::set<Ptr<ExtendDecl>>& otherExtends,
    const ExtendDecl& curDecl, std::set<Ptr<ExtendDecl>, CmpNodeByPos>& ordered)
{
    auto extendedDecl = Ty::GetDeclPtrOfTy(curDecl.ty);
    if (auto rt = DynamicCast<RefType>(curDecl.extendedType.get()); rt && rt->GetTarget() && !extendedDecl) {
        // For Built-in generic type 'CPointer<T>'.
        extendedDecl = rt->GetTarget();
    }
    for (auto ed : std::as_const(otherExtends)) {
        if (ed == &curDecl) {
            continue;
        }
        // In the following scenarios, need to check whether to skip extension based on the inheritance relationship.
        // 1.both extensions are from the same package
        // 2.both extensions are from different packages, but both are visible in current package
        // If the two expansions are from different packages and ed is accessible in curDecl's package, need to collect.
        bool areAllFromOtherPkg = ed->fullPackageName != pkg.fullPackageName &&
            curDecl.fullPackageName != pkg.fullPackageName && IsExtendVisibleInCurpkg(*ed);
        if (ed->fullPackageName != curDecl.fullPackageName && !areAllFromOtherPkg) {
            if (importManager.IsExtendAccessible(*curDecl.curFile, *ed)) {
                ordered.emplace(ed);
            }
            continue;
        }
        std::optional<bool> skipExtend = DeterminingSkipExtendByInheritanceRelationship(curDecl, *ed, extendedDecl);
        // If 'skipExtend' is nullopt, the two extensions are not sequence-sensitive.
        if (!skipExtend.has_value() || !skipExtend.value()) {
            ordered.emplace(ed);
        }
    }
}

std::optional<bool> StructInheritanceChecker::DeterminingSkipExtendByInheritanceRelationship(
    const ExtendDecl& curDecl, const ExtendDecl& ed, const Ptr<Decl>& extendedDecl)
{
    std::optional<bool> skipExtend = std::nullopt;
    std::pair<Ptr<Ty>, Ptr<Ty>> lastInherTy;
    auto mappingOfExtended2Ed =
        extendedDecl ? GenerateTypeMapping(*extendedDecl, ed.extendedType->ty->typeArgs) : TypeSubst();
    auto mappingOfExtended2CurExtend =
        extendedDecl ? GenerateTypeMapping(*extendedDecl, curDecl.extendedType->ty->typeArgs) : TypeSubst();
    for (auto& curDeclSuper : std::as_const(curDecl.inheritedTypes)) {
        Ptr<Ty> hasSubImpl = nullptr;
        Ptr<Ty> hasSuperImpl = nullptr;
        Ptr<Ty> curDeclSuperInsTy = curDeclSuper->ty;
        auto typeArgs = extendedDecl ? extendedDecl->ty->typeArgs : std::vector<Ptr<Ty>>();
        for (auto typeArg : typeArgs) {
            auto tyArgGen = StaticCast<GenericsTy*>(typeArg);
            auto mappingOfCurExtend2Extend =
                GenerateTypeMappingByTy(mappingOfExtended2CurExtend[tyArgGen], mappingOfExtended2Ed[tyArgGen]);
            curDeclSuperInsTy = typeManager.GetInstantiatedTy(curDeclSuperInsTy, mappingOfCurExtend2Extend);
        }
        for (auto& edSuper : std::as_const(ed.inheritedTypes)) {
            if (edSuper->ty == curDeclSuperInsTy) {
                continue;
            }
            // Whether at least one extension's interface is the current sub-interface.
            if (typeManager.IsSubtype(edSuper->ty, curDeclSuperInsTy)) {
                hasSubImpl = edSuper->ty;
            }
            // Whether at least one extension's interface is the current super-interface.
            if (typeManager.IsSubtype(curDeclSuperInsTy, edSuper->ty)) {
                hasSuperImpl = edSuper->ty;
            }
        }
        // Another extension implement sub-interface and super-interface of current extension interface at same
        // time, unable to decide which extension happens first.
        if (hasSubImpl && hasSuperImpl) {
            auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_extend_check_sequence_cannot_decide, curDecl);
            std::string inherChainStr =
                hasSubImpl->String() + " <: " + curDeclSuperInsTy->String() + " <: " + hasSuperImpl->String();
            builder.AddNote(
                MakeRange(ed.identifier), "conflict with this extension, beacase of '" + inherChainStr + "'");
            return {};
        } else if (!hasSubImpl && !hasSuperImpl) {
            continue;
        }
        bool shouldDiag = skipExtend.has_value() && skipExtend.value() != (hasSubImpl != nullptr);
        if (shouldDiag) {
            auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_extend_check_sequence_cannot_decide, curDecl);
            std::string lastInherStr = lastInherTy.first->String() + " <: " + lastInherTy.second->String();
            std::string curInherStr = hasSubImpl ? hasSubImpl->String() + " <: " + curDeclSuperInsTy->String()
                                                 : curDeclSuperInsTy->String() + " <: " + hasSuperImpl->String();
            builder.AddNote(MakeRange(ed.identifier),
                "conflict with this extension, beacase of '" + lastInherStr + "' and '" + curInherStr + "'");
            return {};
        }
        skipExtend = hasSubImpl != nullptr;
        lastInherTy = skipExtend.value() ? std::make_pair(hasSubImpl, curDeclSuperInsTy)
                                         : std::make_pair(curDeclSuperInsTy, hasSuperImpl);
    }
    return skipExtend;
}

void StructInheritanceChecker::CheckExtendExportDependence(
    const InheritableDecl& curDecl, const MemberSignature& interface, const MemberMap& implDecls)
{
    auto curExtend = DynamicCast<ExtendDecl>(&curDecl);
    if (!curExtend || !curDecl.IsExportedDecl()) {
        return;
    }
    auto identifier = interface.decl->identifier;
    auto foundMembers = implDecls.equal_range(identifier);
    for (auto it = foundMembers.first; it != foundMembers.second; ++it) {
        auto child = it->second;
        if (child.decl->astKind != interface.decl->astKind) {
            continue;
        }
        bool isImplFuncOrProp = interface.decl->astKind == ASTKind::PROP_DECL ||
            (interface.decl->IsFunc() && CheckImplementationRelation(interface, child));
        auto parentOuter = interface.decl->outerDecl;
        auto childOuter = child.decl->outerDecl;
        CJC_NULLPTR_CHECK(childOuter);
        CJC_NULLPTR_CHECK(parentOuter);
        if (isImplFuncOrProp && parentOuter->IsExportedDecl() && !childOuter->IsExportedDecl()) {
            auto builder = diag.DiagnoseRefactor(
                DiagKindRefactor::sema_export_extend_depend_non_export_extend, curDecl, interface.decl->identifier);
            builder.AddNote(MakeRange(child.decl->outerDecl->begin, child.decl->outerDecl->end),
                "following extension cannot be exported");
        }
    }
}

std::pair<MemberMap, MemberMap> StructInheritanceChecker::GetVisibleExtendMembersForExtend(const InheritableDecl& decl)
{
    if (decl.astKind != ASTKind::EXTEND_DECL) {
        return {};
    }
    auto ed = RawStaticCast<const ExtendDecl*>(&decl);
    if (!ed->extendedType || !Ty::IsTyCorrect(ed->extendedType->ty)) {
        return {};
    }
    MemberMap interfaceMembers;
    MemberMap extendMembers;
    MemberMap invisibleInterfaceMembers;
    MemberMap invisibleExtendMembers;
    MultiTypeSubst mts;
    typeManager.GenerateGenericMapping(mts, *ed->extendedType->ty);
    auto extends = typeManager.GetAllExtendsByTy(*ed->extendedType->ty);
    std::set<Ptr<ExtendDecl>, CmpNodeByPos> ordered;
    CollectExtendByInterfaceInherit(extends, *ed, ordered);
    for (auto extend : ordered) {
        if (!extend->extendedType || !Ty::IsTyCorrect(extend->extendedType->ty) || extend == ed) {
            continue;
        }
        if (decl.fullPackageName != extend->fullPackageName && !extend->TestAttr(Attribute::PUBLIC)) {
            continue;
        }
        // Check if the 'extend' is visible from current decl. Store visible and invisble members in different maps.
        bool visibleExtend = typeManager.CheckGenericDeclInstantiation(extend, ed->extendedType->ty->typeArgs);
        if (!visibleExtend) {
            // Invisible extend's MultiTypeSubst will not be generated by 'GenerateGenericMapping'. Generate here.
            typeManager.GenerateStructDeclGenericMapping(mts, *extend, *ed->extendedType->ty);
        }
        auto& interfaceMap = visibleExtend ? interfaceMembers : invisibleInterfaceMembers;
        auto& extendMap = visibleExtend ? extendMembers : invisibleExtendMembers;
        auto curInterfaceMap = GetAndCheckInheritedInterfaces(*extend);
        std::for_each(
            curInterfaceMap.begin(), curInterfaceMap.end(), [&extend](auto& m) { m.second.extendDecl = extend; });
        MergeInheritedMembers(interfaceMap, curInterfaceMap, *ed->extendedType->ty);
        auto typeMapping = MultiTypeSubstToTypeSubst(mts);
        for (auto& edMember : extend->GetMemberDecls()) {
            if (!Ty::IsTyCorrect(edMember->ty) || IsInvisibleMember(*edMember, decl.fullPackageName)) {
                continue;
            }
            auto memberTy = typeManager.GetInstantiatedTy(edMember->ty, typeMapping);
            auto structTy = typeManager.GetInstantiatedTy(extend->ty, typeMapping);
            MemberSignature sig{edMember, memberTy, structTy, extend, GetAllGenericUpperBounds(typeManager, *edMember)};
            (void)UpdateInheritedMemberIfNeeded(extendMap, sig);
        }
    }
    MergeInheritedMembers(interfaceMembers, extendMembers, *ed->extendedType->ty);
    MergeInheritedMembers(invisibleInterfaceMembers, invisibleExtendMembers, *ed->extendedType->ty);
    // Remove if member is abstract;
    auto isAbstract = [](auto it) { return it.second.decl->TestAttr(Attribute::ABSTRACT); };
    Utils::EraseIf(interfaceMembers, isAbstract);
    Utils::EraseIf(invisibleInterfaceMembers, isAbstract);
    return {interfaceMembers, invisibleInterfaceMembers};
}

// Check & report error with inherited members.
void StructInheritanceChecker::DiagnoseForInheritedMember(
    const MemberSignature& parent, const MemberSignature& child) const
{
    if (parent.decl->TestAttr(Attribute::ENUM_CONSTRUCTOR) && child.decl->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
        return; // Do not check between enum constructors.
    }
    if (parent.decl->outerDecl == child.decl->outerDecl) {
        return; // Do not check functions in same decl.
    }
    CheckSameNameInheritanceInfo(parent, *child.decl);
    if (child.decl->astKind != parent.decl->astKind) {
        return;
    }
    if (parent.decl->astKind == ASTKind::PROP_DECL) {
        CheckInheritanceAttributes(parent, *child.decl);
        CheckPropertyInheritance(parent, *child.decl);
    } else if (parent.decl->IsFunc()) {
        if (CheckImplementationRelation(parent, child)) {
            CheckInheritanceAttributes(parent, *child.decl);
        }
    }
    CheckNativeFFI(parent, child);
}

void StructInheritanceChecker::DiagnoseInheritedInsconsistType(const MemberSignature& member, const Node& node) const
{
    CJC_ASSERT(!member.inconsistentTypes.empty() && member.decl->IsFuncOrProp());
    std::string typeName = member.decl->astKind == ASTKind::PROP_DECL ? "type" : "return type";
    auto range = MakeRange(node.begin, node.end);
    if (auto decl = DynamicCast<const Decl*>(&node)) {
        range = MakeRange(decl->identifier);
    }
    auto diagBuilder = diag.DiagnoseRefactor(DiagKindRefactor::sema_inherit_member_type_inconsistent, node, range,
        typeName, DeclKindToString(*member.decl), member.decl->identifier.Val());
    std::string message = StringifyInconsistentTypes(member.inconsistentTypes, TypeManager::GetInvalidTy());
    diagBuilder.AddNote("conflict types are " + message);
}

void StructInheritanceChecker::DiagnoseForConflictInheritance(
    const InheritableDecl& decl, const MemberMap& members) const
{
    std::set<std::string> names;
    for (auto& it : std::as_const(members)) {
        names.emplace(it.first);
    }

    for (auto it = names.begin(); it != names.end();) {
        std::string identifier = *it;
        auto found = members.equal_range(identifier);
        auto count = static_cast<size_t>(std::distance(found.first, found.second));
        // Only function allows overloading.
        bool allFunc = std::all_of(found.first, found.second, [](auto& it) { return it.second.decl->IsFunc(); });
        if (count > 1 && !allFunc) {
            auto diagBuilder = diag.DiagnoseRefactor(DiagKindRefactor::sema_inherit_super_member_kind_inconsistent,
                decl, MakeRange(decl.identifier), identifier);
            OrderedDeclSet sorted;
            for (auto mIt = found.first; mIt != found.second; ++mIt) {
                sorted.emplace(mIt->second.decl);
            }
            for (auto member : sorted) {
                auto message = DeclKindToString(*member) + " member";
                diagBuilder.AddNote(*member, MakeRange(member->identifier), message);
            }
            // If astKind already conflicted, do not report further inherited types inconsistent error.
            it = names.erase(it);
        } else {
            ++it;
        }
    }
    // Report error when inherited property members types are inconsistent.
    for (const auto& identifier : std::as_const(names)) {
        auto found = members.equal_range(identifier);
        for (auto it = found.first; it != found.second; ++it) {
            auto member = it->second;
            if (!member.inconsistentTypes.empty() && member.decl->astKind == ASTKind::PROP_DECL) {
                DiagnoseInheritedInsconsistType(member, decl);
            }
        }
    }
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
/**
 * Check whether the member in the parent class extension can completely overwrite the member in the interface.
 * eg:
 * interface I1 {
 *     func test(): Unit {}
 * }
 * interface I2 {}
 * class A<T> {}
 * extend<T> A<T> where T <: I2 {
 *     public func test(): Unit {}
 * }
 * class B<T> <: A<T> & I1 {}
 *
 * The generic constraint of `extend A` is inconsistent with `class B <: A`, function `test` cannot override or
 * implement it in interface `I1`.
 */
void StructInheritanceChecker::CheckIncompleteOverrideOrImplOfExtend(
    const MemberSignature& interface, const MemberSignature& child) const
{
    auto childOuter = child.decl->outerDecl;
    if (!childOuter || childOuter->astKind != ASTKind::EXTEND_DECL) {
        return;
    }
    auto childOuterEd = StaticCast<ExtendDecl*>(childOuter);
    auto childOuterDecl = childOuterEd->extendedType->GetTarget();
    auto isChildInCheckingDecl = [this, &childOuterDecl]() {
        if (checkingDecls.empty()) {
            return false;
        }
        if (checkingDecls[0] == childOuterDecl) {
            return true;
        }
        auto allChildExtends = typeManager.GetAllExtendsByTy(*childOuterDecl->ty);
        for (auto extend : allChildExtends) {
            if (extend == checkingDecls[0]) {
                return true;
            }
        }
        return false;
    };
    if (!childOuterDecl || !childOuterDecl->ty->HasGeneric() || isChildInCheckingDecl()) {
        return;
    }
    auto curClassTy = DynamicCast<ClassTy>(checkingDecls[0]->ty);
    if (!curClassTy || childOuterDecl->astKind != ASTKind::CLASS_DECL) {
        return;
    }
    MultiTypeSubst m;
    typeManager.GenerateGenericMapping(m, *curClassTy);
    auto instSupers = typeManager.GetInstantiatedTys(childOuterDecl->ty, m);
    auto diagForIncompleteOverrideOrImplement = [this, &interface, &child, &childOuterDecl]() {
        std::string funcOrProp = child.decl->astKind == ASTKind::FUNC_DECL ? "function" : "prop";
        std::string memberStr = funcOrProp + " '" + child.decl->identifier.Val() + "' in '" +
            childOuterDecl->identifier.Val() + "''s extension";
        std::string interfaceBeOverrided = "interface '" + interface.decl->outerDecl->identifier + "'";
        if (interface.decl->TestAttr(Attribute::DEFAULT)) {
            auto builder = diag.DiagnoseRefactor(
                DiagKindRefactor::sema_cannot_override, *checkingDecls[0], funcOrProp, child.decl->identifier);
            builder.AddNote(MakeRange(child.decl->identifier),
                "member " + memberStr + " conflict with that in " + interfaceBeOverrided);
        } else {
            std::string prefix = checkingDecls[0]->astKind == ASTKind::EXTEND_DECL ? "extend " : "";
            std::string classNameOverride = prefix +
                (checkingDecls[0]->ty->IsNominal() ? checkingDecls[0]->ty->name : checkingDecls[0]->ty->String());
            auto builder =
                diag.Diagnose(*checkingDecls[0], DiagKind::sema_interface_member_must_be_implemented_in_struct,
                    funcOrProp, child.decl->identifier.Val(), classNameOverride);
            builder.AddNote(MakeRange(child.decl->identifier),
                "member " + memberStr + " may not be visible to '" + classNameOverride + "'");
        }
    };
    for (auto inst : instSupers) {
        if (!typeManager.CheckGenericDeclInstantiation(childOuterEd, inst->typeArgs)) {
            diagForIncompleteOverrideOrImplement();
            return;
        }
    }
}
#endif

void StructInheritanceChecker::DiagnoseForInheritedInterfaces(
    const MemberSignature& interface, const MemberMap& implDecls) const
{
    auto identifier = interface.decl->identifier;
    auto foundMembers = implDecls.equal_range(identifier);
    for (auto it = foundMembers.first; it != foundMembers.second; ++it) {
        auto child = it->second;
        CheckSameNameInheritanceInfo(interface, *child.decl);
        if (child.decl->astKind != interface.decl->astKind) {
            continue;
        }
        if (interface.decl->astKind == ASTKind::PROP_DECL) {
            CheckInheritanceForInterface(interface, child);
            CheckPropertyInheritance(interface, *child.decl);
        } else if (interface.decl->IsFunc()) {
            if (CheckImplementationRelation(interface, child)) {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
                CheckIncompleteOverrideOrImplOfExtend(interface, child);
#endif
                CheckInheritanceForInterface(interface, child);
                CheckMutModifierCompatible(interface, *child.decl);
            }
        }
        CheckNativeFFI(interface, child);
    }
    if (interface.decl->TestAttr(Attribute::DEFAULT)) {
        interface.decl->EnableAttr(Attribute::INTERFACE_IMPL);
    }
}

void StructInheritanceChecker::DiagnoseForUnimplementedInterfaces(const MemberMap& members, const Decl& structDecl)
{
    // Do not check unimplemented function for:
    // 1. Foreign struct.
    // 2. Mirror struct.
    if (structDecl.TestAttr(Attribute::FOREIGN) || structDecl.TestAnyAttr(Attribute::OBJ_C_MIRROR)) {
        return;
    }
    std::string prefix = structDecl.astKind == ASTKind::EXTEND_DECL ? "extend " : "";
    std::string structName = prefix + (structDecl.ty->IsNominal() ? structDecl.ty->name : structDecl.ty->String());
    std::multimap<Ptr<Decl>, const MemberSignature*> unImplementedDecl;
    std::vector<Ptr<const MemberSignature>> unImplementedMembers;
    for (auto& [identifier, member] : members) {
        bool notInheritable = member.decl->astKind != ASTKind::PROP_DECL && !member.decl->IsFunc();
        bool ignored = notInheritable || member.extendDecl || opts.compileCjd;
        if (ignored) {
            continue; // Do not report error for not inheritable member or member in other extends.
        }
        if (IsBuiltInOperatorFuncInExtend(member, structDecl)) {
            continue;
        }
        // common inherit decl may not impletement all interface members.
        if (structDecl.TestAttr(Attribute::COMMON) && member.isInheritedInterface) {
            // impletement but no body
            if (IsCommonWithoutDefault(*member.decl) && member.decl->outerDecl == &structDecl) {
                continue;
            }
        }
        // Unimplemented decls can be ignored if:
        // 1. decl is defined in foreign struct;
        // 2. abstract decl in abstract class && current is extend decl.
        // 3. abstract decl in interface inherited by extended type decl.
        bool inForeignType = member.decl->outerDecl->TestAttr(Attribute::FOREIGN);
        bool extendInheritedAbstract = member.decl->outerDecl->TestAttr(Attribute::ABSTRACT) &&
            member.decl->TestAttr(Attribute::ABSTRACT) && structDecl.astKind == ASTKind::EXTEND_DECL;
        bool extendAbsInheritedInterface = structDecl.astKind == ASTKind::EXTEND_DECL &&
            member.decl->TestAttr(Attribute::ABSTRACT) && !member.isInheritedInterface;
        if (inForeignType || extendInheritedAbstract || extendAbsInheritedInterface) {
            continue;
        }
        std::string type = DeclKindToString(*member.decl);
        bool isAbstractClass = structDecl.TestAttr(Attribute::ABSTRACT) && structDecl.astKind == ASTKind::CLASS_DECL;
        bool isAbstractDecl = isAbstractClass || structDecl.astKind == ASTKind::INTERFACE_DECL;
        bool isStaticAbsMember = member.decl->TestAttr(Attribute::STATIC, Attribute::ABSTRACT);
        if (member.decl->TestAttr(Attribute::ABSTRACT) && !isAbstractDecl) {
            unImplementedDecl.emplace(member.decl, &member);
            unImplementedMembers.emplace_back(&member);
        } else if (member.shouldBeImplemented) {
            diag.Diagnose(
                structDecl, DiagKind::sema_interface_member_must_be_implemented, type, identifier, structName);
        } else if (member.decl->outerDecl != &structDecl) {
            if (isStaticAbsMember && isAbstractClass) {
                auto structNameRange = MakeRange(structDecl.identifier);
                (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_inherit_abstract_class_static_unimplement_func,
                    structDecl, structNameRange, structName, type, identifier);
            } else if (!member.inconsistentTypes.empty()) {
                // When inherit functions from interfaces which has inconsistent types, we need report error here.
                DiagnoseInheritedInsconsistType(member, structDecl);
            }
        }
    }
    if (!unImplementedMembers.empty()) {
        DiagKindRefactor kind;
        if (structDecl.IsClassLikeDecl()) {
            kind = DiagKindRefactor::sema_class_need_abstract_modifier_or_func_need_impl;
        } else {
            kind = DiagKindRefactor::sema_need_member_implementation;
        }
        DiagnosticBuilder builder = diag.DiagnoseRefactor(kind, structDecl.begin, structName);
        std::stable_sort(unImplementedMembers.begin(), unImplementedMembers.end(), CompMemberSignatureByPosAndTy);
        for (auto member : unImplementedMembers) {
            std::string identifierName;
            if (unImplementedDecl.count(member->decl) > 1) {
                identifierName = "'" + member->decl->identifier.Val() + "', of type: " + member->ty->String();
            } else {
                identifierName = "'" + member->decl->identifier.Val() + "'";
            }
            std::string abstractType =
                member->decl->outerDecl->astKind == ASTKind::CLASS_DECL ? "abstract" : "interface";
            std::string note =
                "unimplemented " + abstractType + " " + DeclKindToString(*member->decl) + " " + identifierName;
            builder.AddNote(member->decl->GetBegin(), note);
        }
    }
}

/**
 * Check if child overrides the extended parent with default implementation in interface extension.
 */
bool StructInheritanceChecker::IsExtendedDefaultImpl(const MemberSignature& parent, const Decl& child) const
{
    if (Utils::NotIn(parent.decl->astKind, {ASTKind::FUNC_DECL, ASTKind::PROP_DECL}) ||
        !parent.decl->TestAttr(Attribute::DEFAULT) || parent.decl->outerDecl->astKind != ASTKind::INTERFACE_DECL ||
        child.outerDecl->astKind == ASTKind::EXTEND_DECL) {
        return false;
    }
    return typeManager.HasExtensionRelation(*child.outerDecl->ty, *parent.decl->outerDecl->ty);
}

/**
 * Check if:
 * 1. member in extend is not shadow parent members,
 * 2. member in class is not shadow extend members of parent decl.
 * return true if not shadow, return false if shadowed.
 */
bool StructInheritanceChecker::CheckExtendMemberValid(const MemberSignature& parent, const Decl& child) const
{
    auto childRange = child.identifier.Empty() ? MakeRange(child.begin, child.end) : MakeRange(child.identifier);
    if (parent.extendDecl) {
        // When parent is default implement and has been copyed by other extend, or parent is defined in other extend,
        // it should be check shadow between parent and child which is in extend too.
        if (!parent.decl->TestAnyAttr(Attribute::DEFAULT, Attribute::IN_EXTEND) &&
            child.TestAttr(Attribute::IN_EXTEND)) {
            return true;
        }
        auto structTy = child.outerDecl->ty;
        std::string typeName = "extend " + (structTy->IsNominal() ? structTy->name : structTy->String());
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_extend_member_cannot_shadow, child, childRange, child.identifier, typeName);
    } else if (auto isExtended = IsExtendedDefaultImpl(parent, child);
               isExtended || parent.decl->outerDecl->astKind == ASTKind::EXTEND_DECL) {
        std::string type = DeclKindToString(*parent.decl);
        auto diagnose = diag.DiagnoseRefactor(
            DiagKindRefactor::sema_extend_function_cannot_overridden, child, childRange, type, child.identifier);
        if (!isExtended) {
            auto parentRange = parent.decl->identifier.Empty() ? MakeRange(parent.decl->begin, parent.decl->end)
                                                               : MakeRange(parent.decl->identifier);
            diagnose.AddNote(*parent.decl, parentRange, "overridden definition of '" + child.identifier + "' is here");
        }
    } else if (child.outerDecl->astKind == ASTKind::EXTEND_DECL) {
        auto parentRange = parent.decl->identifier.Empty() ? MakeRange(parent.decl->begin, parent.decl->end)
                                                           : MakeRange(parent.decl->identifier);
        auto ed = RawStaticCast<ExtendDecl*>(child.outerDecl);
        auto diagnose = diag.DiagnoseRefactor(
            DiagKindRefactor::sema_extend_member_cannot_shadow, child, childRange, child.identifier, ed->ty->String());
        diagnose.AddNote(*parent.decl, parentRange, "shadowed definition of '" + child.identifier + "' is here");
    } else {
        return true;
    }
    return false;
}

/**
 * @p parent and @p child are considered as inherited members in structure declaration not interfaces' members.
 * Report errors when:
 * 1. parent and child have different 'static' status.
 * 2. child conflict with parent when two have different node kind.
 * 3. child shadows parent when two have same node kind, but parent is not opened.
 */
void StructInheritanceChecker::CheckSameNameInheritanceInfo(const MemberSignature& parent, const Decl& child) const
{
    std::string name = child.identifier;
    auto parentDecl = parent.decl;
    std::string childStatic = child.TestAttr(Attribute::STATIC) ? "static" : "non-static";
    std::string parentStatic = parentDecl->TestAttr(Attribute::STATIC) ? "static" : "non-static";
    std::string inheritance =
        checkingDecls[0]->astKind == ASTKind::EXTEND_DECL ? "extended type" : "parent class or interfaces";
    if (child.TestAttr(Attribute::STATIC) != parentDecl->TestAttr(Attribute::STATIC)) {
        diag.Diagnose(child, DiagKind::sema_static_and_non_static_member_cannot_have_same_name, childStatic, name,
            parentStatic, inheritance);
        return;
    }
    if (child.astKind != parentDecl->astKind) {
        if (parent.extendDecl || parent.isInheritedInterface || CheckExtendMemberValid(parent, child)) {
            auto build = diag.DiagnoseRefactor(DiagKindRefactor::sema_inherit_member_kind_inconsistent, child,
                MakeRange(child.identifier), DeclKindToString(child), name, DeclKindToString(*parentDecl), inheritance);
            build.AddNote(*parentDecl, MakeRange(parentDecl->identifier), "found conflict here");
        }
    } else if (parentDecl->astKind == ASTKind::VAR_DECL) {
        diag.Diagnose(child, DiagKind::sema_member_variable_can_not_shadow, name);
    }
}

void StructInheritanceChecker::CheckInheritanceAttributes(const MemberSignature& parent, const Decl& child) const
{
    auto parentDecl = parent.decl;
    if (child.astKind != parentDecl->astKind || (parentDecl->astKind != ASTKind::PROP_DECL && !parentDecl->IsFunc())) {
        return;
    }
    std::string type = DeclKindToString(*parentDecl);
    std::string name = child.identifier;
    if (!CheckExtendMemberValid(parent, child) || checkingDecls[0]->astKind == ASTKind::EXTEND_DECL) {
        return;
    }
    if (!parentDecl->TestAttr(Attribute::STATIC) && child.TestAttr(Attribute::REDEF)) {
        diag.Diagnose(child, DiagKind::sema_func_no_override_or_redefine_modifier, "redef", type, name);
    } else if (!parentDecl->TestAttr(Attribute::ABSTRACT) && !parentDecl->TestAttr(Attribute::STATIC)) {
        bool notInInterface = parentDecl->outerDecl->astKind != ASTKind::INTERFACE_DECL;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        if ((!parentDecl->TestAttr(Attribute::OPEN) && notInInterface) || TestManager::IsDeclOpenToMock(*parentDecl)) {
            DiagCannotOverride(diag, child, *parentDecl);
#endif
        } else if (child.TestAttr(Attribute::ABSTRACT) && notInInterface) {
            diag.Diagnose(child, DiagKind::sema_invalid_override_member_in_class, type, child.identifier.Val(), type);
        }
    }
}

void StructInheritanceChecker::CheckInheritanceForInterface(
    const MemberSignature& interface, const MemberSignature& child) const
{
    auto interfaceMember = interface.decl;
    auto childDecl = child.decl;
    if (childDecl->astKind != interfaceMember->astKind ||
        (interfaceMember->astKind != ASTKind::PROP_DECL && !interfaceMember->IsFunc())) {
        return;
    }
    std::string type = DeclKindToString(*interfaceMember);
    if (childDecl->TestAttr(Attribute::ABSTRACT) && !interfaceMember->TestAttr(Attribute::ABSTRACT)) {
        diag.Diagnose(*childDecl, DiagKind::sema_invalid_override_or_redefine_member_in_interface, type,
            childDecl->identifier.Val(), type);
    }
    // Mark child member implment abstract function of inherted interface. Used to determine whether child needs to
    // be exported.
    childDecl->EnableAttr(Attribute::INTERFACE_IMPL);
    auto setInterfaceImplAttr = [](const OwnedPtr<FuncDecl>& fd) { fd->EnableAttr(Attribute::INTERFACE_IMPL); };
    if (auto pd = DynamicCast<PropDecl>(childDecl)) {
        std::for_each(pd->getters.begin(), pd->getters.end(), setInterfaceImplAttr);
        std::for_each(pd->setters.begin(), pd->setters.end(), setInterfaceImplAttr);
    }
}

/**
 * Report error when no inherited which has same identifier existing for current member.
 */
void StructInheritanceChecker::DiagnoseForOverriddenMember(const MemberSignature& child) const
{
    if (child.replaceOther) {
        return;
    }
    auto childDecl = child.decl;
    std::string type = DeclKindToString(*childDecl);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (childDecl->TestAttr(Attribute::OVERRIDE)) {
#endif
        diag.Diagnose(*childDecl, DiagKind::sema_missing_overridden_func, type, childDecl->identifier.Val(), type);
    } else if (childDecl->TestAttr(Attribute::REDEF) && childDecl->TestAttr(Attribute::STATIC)) {
        diag.Diagnose(*childDecl, DiagKind::sema_missing_redefined_func, type, childDecl->identifier.Val(), type);
    }
}

void StructInheritanceChecker::CheckAccessVisibility(const Decl& parent, const Decl& child, const Decl& diagNode) const
{
    CJC_NULLPTR_CHECK(parent.outerDecl);
    // Do not report error if the `parent` cannot be implemented.
    bool canBeImplemented = parent.TestAnyAttr(Attribute::ABSTRACT, Attribute::OPEN, Attribute::STATIC) ||
        parent.outerDecl->astKind == ASTKind::INTERFACE_DECL;
    if (!canBeImplemented) {
        return;
    }
    // |--------------|--------|-----------|---------|---------|
    // | parent\child | public | protected | default | private |
    // |--------------|--------|-----------|---------|---------|
    // | public       | ok     | case 1    | case 1  | case 1  |
    // | protected    | ok     | ok        | case 2  | case 2  |
    // | default      | ok     | ok        | ok      | case 3  |
    // | private      | ok     | ok        | ok      | ok      |
    // |--------------|--------|-----------|---------|---------|
    bool weakVisibility =
        // case 1
        (parent.TestAttr(Attribute::PUBLIC) && !child.TestAttr(Attribute::PUBLIC)) ||
        // case 2
        (parent.TestAttr(Attribute::PROTECTED) && !child.TestAnyAttr(Attribute::PUBLIC, Attribute::PROTECTED)) ||
        // case 3
        (!parent.TestAnyAttr(Attribute::PUBLIC, Attribute::PROTECTED, Attribute::PRIVATE) &&
            child.TestAttr(Attribute::PRIVATE));
    if (weakVisibility) {
        DiagWeakVisibility(diag, parent, child, diagNode);
    }
}

void StructInheritanceChecker::CheckGenericTypeArgInfo(
    const MemberSignature& parent, const MemberSignature& child) const
{
    auto parentGeneric = parent.decl->GetGeneric();
    auto childGeneric = child.decl->GetGeneric();
    bool diffStatus = parent.decl->TestAttr(Attribute::STATIC) != child.decl->TestAttr(Attribute::STATIC);
    if (!parentGeneric || !childGeneric || diffStatus) {
        return;
    }
    if (parentGeneric->typeParameters.size() != childGeneric->typeParameters.size()) {
        diag.Diagnose(*child.decl, DiagKind::sema_generic_member_type_argument_different, child.decl->identifier.Val());
        return;
    }
    TypeSubst typeMapping = typeManager.GenerateGenericMappingFromGeneric(*parent.decl, *child.decl);
    CJC_ASSERT(parent.upperBounds.size() == child.upperBounds.size());
    // Child's constraint should be looser or same with the parent's constraint;
    for (size_t i = 0; i < parent.upperBounds.size(); ++i) {
        auto childUppers = child.upperBounds[i];
        (void)childUppers.erase(typeManager.GetAnyTy()); // Remove upper bound of type 'Any'.
        if (childUppers.empty()) {
            continue; // Empty upperBounds is always looser.
        }
        auto& parentUppers = parent.upperBounds[i];
        std::unordered_set<Ptr<Ty>> instUppers;
        for (auto it : parentUppers) {
            instUppers.emplace(typeManager.GetInstantiatedTy(it, typeMapping));
        }
        bool childLooser = true;
        for (auto upper : childUppers) {
            // At least one parent upperBounds is the subtype of child upperBound.
            childLooser = std::any_of(instUppers.begin(), instUppers.end(),
                [this, &upper](auto it) { return typeManager.IsSubtype(it, upper); });
            if (!childLooser) {
                break;
            }
        }
        for (auto upper : instUppers) {
            if (!childLooser) {
                break;
            }
            // Cannot exist any child upperBound that is the subtype of parent upperBound.
            childLooser = !std::any_of(childUppers.begin(), childUppers.end(), [this, &upper](auto it) {
                return !typeManager.IsTyEqual(it, upper) && typeManager.IsSubtype(it, upper);
            });
        }
        if (!childLooser) {
            auto parentConstraints =
                instUppers.empty() ? std::string("empty") : "'" + Ty::GetTypesToStableStr(instUppers, "', '") + "'";
            auto gc = GetConstraintNodeByIndex(i, *childGeneric);
            auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_generic_constraint_not_looser, *gc);
            builder.AddNote("parent constraint is " + parentConstraints);
        }
    }
}

void StructInheritanceChecker::CheckPropertyInheritance(const MemberSignature& parent, const Decl& child) const
{
    auto parentDecl = parent.decl;
    // Caller guarantees parent and child have same astKind.
    if (parentDecl->astKind != ASTKind::PROP_DECL) {
        return;
    }

    if (parent.ty != child.ty || !parent.inconsistentTypes.empty()) {
        diag.Diagnose(child, DiagKind::sema_property_override_implement_type_diff);
    }
    auto parentProp = RawStaticCast<PropDecl*>(parentDecl);
    auto childProp = RawStaticCast<const PropDecl*>(&child);
    if (childProp->isVar != parentProp->isVar) {
        if (parentProp->isVar) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_property_have_same_declaration_in_inherit_mut, *childProp,
                childProp->identifier.Val());
        } else {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_property_have_same_declaration_in_inherit_immut, *childProp,
                childProp->identifier.Val());
        }
    } else if (childProp->isVar && (childProp->getters.empty() || childProp->setters.empty()) &&
        parentProp->TestAnyAttr(Attribute::DEFAULT, Attribute::ABSTRACT)) {
        // Currently, implemenation of default interface property must implement both getter/setter.
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_property_must_implement_both, *childProp, childProp->identifier.Val());
    }
    CheckAccessVisibility(*parentDecl, child, child);
}

bool StructInheritanceChecker::CheckReturnOverrideByGeneric(const FuncTy& parentTy, const FuncTy& childTy) const
{
    if (!Ty::IsTyCorrect(childTy.retTy) || !Ty::IsTyCorrect(parentTy.retTy)) {
        return true;
    }
    if (!childTy.retTy->IsGeneric() || parentTy.retTy == childTy.retTy) {
        return true;
    }
    // 'func foo(): I1' cannot be overridden by a function 'func foo(): T' that return generic type 'T' and 'T''s
    // upperbound is empty or all upperbounds are interface.
    auto genericTy = StaticCast<GenericsTy*>(childTy.retTy);
    if (genericTy->upperBounds.empty()) {
        return false;
    }
    for (auto up : genericTy->upperBounds) {
        if (!up->IsInterface() && !up->IsGeneric()) {
            return true;
        }
    }
    return false;
}

bool StructInheritanceChecker::CheckImplementationRelation(
    const MemberSignature& parent, const MemberSignature& child) const
{
    auto parentDecl = parent.decl;
    // Caller guarantees parent and child have same astKind.
    if (!parentDecl->IsFunc()) {
        return false;
    }
    auto parentTy = parent.ty;
    auto parentFunc = RawStaticCast<FuncDecl*>(parent.decl);
    auto childFunc = RawStaticCast<const FuncDecl*>(child.decl);
    if (childFunc->TestAttr(Attribute::GENERIC) && parentDecl->TestAttr(Attribute::GENERIC)) {
        TypeSubst typeMapping = typeManager.GenerateGenericMappingFromGeneric(*parentFunc, *childFunc);
        parentTy = typeManager.GetInstantiatedTy(parentTy, typeMapping);
    }
    auto parentFuncTy = DynamicCast<FuncTy*>(parentTy);
    auto childFuncTy = DynamicCast<FuncTy*>(child.ty);
    bool sameSignature =
        parentFuncTy && childFuncTy && typeManager.IsFuncParameterTypesIdentical(*parentFuncTy, *childFuncTy);
    if (sameSignature) {
        CJC_ASSERT(parentFunc->funcBody && childFunc->funcBody);
        bool isParamListCorrect = !parentFunc->funcBody->paramLists.empty() && !childFunc->funcBody->paramLists.empty();
        bool inheritedByExtension = childFuncTy->retTy && parentFuncTy->retTy &&
            typeManager.HasExtensionRelation(*childFuncTy->retTy, *parentFuncTy->retTy);
        if (isParamListCorrect && !AreFuncParameterNameIdentical(*parentFunc, *childFunc)) {
            DiagnoseParameterName(diag, *parentFunc, *childFunc);
        } else if (!CheckThisTypeCompatibility(*parentFunc, *childFunc)) {
            auto diagBuilder =
                diag.DiagnoseRefactor(DiagKindRefactor::sema_inherit_not_return_this, MakeRange(childFunc->identifier));
            diagBuilder.AddNote(MakeRange(parentFunc->identifier), "the overriden function");
        } else if (AreReturnTypesCompatible(*parentFuncTy, *childFuncTy, parent.inconsistentTypes)) {
            const Decl& diagNode = childFunc->outerDecl == checkingDecls.front() ? *childFunc : *checkingDecls.front();
            CheckAccessVisibility(*parentFunc, *childFunc, diagNode);
            CheckGenericTypeArgInfo(parent, child);
        } else if (inheritedByExtension || !CheckReturnOverrideByGeneric(*parentFuncTy, *childFuncTy)) {
            // A type that does not meet the type variance relationship cannot be used as the basis for subtypes when
            // override occurs.
            auto builder = diag.Diagnose(*childFunc, DiagKind::sema_return_type_invariance, childFunc->identifier.Val(),
                Ty::ToString(parentFuncTy->retTy));
            builder.AddNote(*parentFunc, "cannot override/implement the following function");
            // If `diagDecl == nullptr`, both child and parent are imported which is orphan extension.
            // And error should have been reported.
        } else {
            Ptr<const FuncDecl> targetFuncDecl = parentFunc;
            std::string inconsistentTypesForDiag;
            if (!parent.inconsistentTypes.empty()) {
                inconsistentTypesForDiag = StringifyInconsistentTypes(parent.inconsistentTypes, childFuncTy->retTy);
                targetFuncDecl = childFunc;
            } else {
                inconsistentTypesForDiag = "'" + parentFuncTy->retTy->String() + "'";
            }
            auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_return_type_incompatible,
                MakeRangeForDeclIdentifier(*childFunc), childFunc->identifier.Val());
            builder.AddNote(MakeRangeForDeclIdentifier(*targetFuncDecl),
                "'" + childFuncTy->retTy->String() + "' is not a subtype of " + inconsistentTypesForDiag);
        }
        if (!childFunc->IsConst() && parentDecl->IsConst()) {
            std::string inheritance =
                checkingDecls[0]->astKind == ASTKind::EXTEND_DECL ? "extended type" : "parent class or interfaces";
            auto build = diag.DiagnoseRefactor(DiagKindRefactor::sema_inherit_member_kind_inconsistent, *childFunc,
                MakeRange(childFunc->identifier), "non-constant function", childFunc->identifier.Val(),
                "'const' function", inheritance);
            build.AddNote(*parentDecl, MakeRange(parentDecl->identifier), "found 'const' function here");
        }
    }
    return sameSignature;
}

// Struct/extend types must maintain the same mut modifier when implementing functions of the interface
void StructInheritanceChecker::CheckMutModifierCompatible(const MemberSignature& parent, const Decl& child) const
{
    auto parentDecl = parent.decl;
    // Caller guarantees parent and child have same astKind.
    if (!parentDecl->IsFunc()) {
        return;
    }
    if (parentDecl->outerDecl->astKind != ASTKind::INTERFACE_DECL) {
        return;
    }
    // Only function in structure declaration or extend of struct needs to checking 'mut' attribute.
    if (auto ed = DynamicCast<ExtendDecl*>(child.outerDecl); ed) {
        if (!ed->ty->IsStruct()) {
            return;
        }
    } else if (child.outerDecl->astKind != ASTKind::STRUCT_DECL) {
        return;
    }

    if (parentDecl->TestAttr(Attribute::MUT) != child.TestAttr(Attribute::MUT)) {
        auto& decl = child.outerDecl == checkingDecls[0] ? child : *checkingDecls[0];
        diag.Diagnose(decl, DiagKind::sema_incompatible_mut_modifier_between_struct_and_interface,
            child.identifier.Val(), parentDecl->outerDecl->identifier.Val());
    }
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
std::vector<Ptr<ExtendDecl>> StructInheritanceChecker::GetAllNeedCheckExtended()
{
    std::vector<Ptr<ExtendDecl>> needCheckExtendDecls = {};
    // If the extend decls are all imported from same package, do not check their inheritance again.
    auto filter = [&needCheckExtendDecls](const std::set<Ptr<ExtendDecl>> extends) -> void {
        std::unordered_set<std::string> pkgNames;
        if (std::all_of(extends.cbegin(), extends.cend(), [&pkgNames](auto it) {
                pkgNames.emplace(it->fullPackageName);
                return it->TestAttr(Attribute::IMPORTED) && pkgNames.size() == 1;
            })) {
            return;
        }
        std::set<Ptr<ExtendDecl>, CmpNodeByPos> sortedExtends;
        std::copy_if(extends.begin(), extends.end(), std::inserter(sortedExtends, sortedExtends.end()),
            [](auto it) { return it->TestAttr(Attribute::PUBLIC); });
        needCheckExtendDecls.insert(needCheckExtendDecls.end(), sortedExtends.cbegin(), sortedExtends.cend());
    };
    // Collect all imported extend decls. Used for checking confliction between imported extends.
    auto extendedDecls = typeManager.GetAllExtendedDecls();
    // Use ordered set to diagnose in consistent order.
    std::set<Ptr<const InheritableDecl>, CmpNodeByPos> sorted(extendedDecls.cbegin(), extendedDecls.cend());
    for (auto extendedDecl : sorted) {
        if (extendedDecl->TestAttr(Attribute::IMPORTED, Attribute::PUBLIC)) {
            auto extends = typeManager.GetDeclExtends(*extendedDecl);
            filter(extends);
        }
    }

    // Collect all extended builtIn. Used for checking confliction between imported extends.
    auto extendedBuiltInTys = typeManager.GetAllExtendedBuiltIn();
    // Use ordered set to diagnose in consistent order.
    std::set<Ptr<AST::Ty>, CmpTyByName> sortedBuiltInTys(extendedBuiltInTys.cbegin(), extendedBuiltInTys.cend());
    for (auto extendedBuiltInTy : sortedBuiltInTys) {
        auto extends = typeManager.GetBuiltinTyExtends(*extendedBuiltInTy);
        filter(extends);
    }
    return needCheckExtendDecls;
}
#endif

void StructInheritanceChecker::CheckNativeFFI(
    [[maybe_unused]] const MemberSignature& parent, [[maybe_unused]] const MemberSignature& child) const
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (checkingDecls.size() > 0 && checkingDecls.back()) {
        Interop::Java::CheckForeignName(diag, typeManager, parent, child, *checkingDecls.back());
    }
#endif
}
