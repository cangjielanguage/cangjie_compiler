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

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Sema/TypeManager.h"

#include "Diags.h"
#include "TypeCheckUtil.h"

using namespace Cangjie;
using namespace AST;
using namespace Sema;
using namespace TypeCheckUtil;

namespace {
void MarkUpperBoundIgnored(const Node& node, Generic& generic)
{
    for (const auto& constraint : generic.genericConstraints) {
        for (const auto& upperBound : constraint->upperBounds) {
            if (upperBound->ty->HasGeneric()) {
                // If the upper bound has generic param, must not check it to prevent infinite recursion.
                upperBound->visitedByWalkerID = node.visitedByWalkerID;
            }
        }
    }
}

std::pair<Ptr<Ty>, Ptr<Decl>> GetRealReferenceType(const Node& node)
{
    auto target = GetRealTarget(node.GetTarget());
    auto baseTy = node.ty; // Ty is guaranteed by caller.
    // If target is constructor, instantiate outer structure declaration.
    if (target && IsClassOrEnumConstructor(*target)) {
        target = target->outerDecl;
        CJC_ASSERT(target != nullptr);
        if (node.ty->IsFunc()) {
            baseTy = RawStaticCast<FuncTy*>(node.ty)->retTy;
        }
    }
    return {baseTy, target};
}
} // namespace

void StructInheritanceChecker::CheckInstMemberSignatures(
    InheritableDecl& decl, const std::vector<Ptr<Ty>>& instTys)
{
    if (instTriggerInfos.empty()) {
        return;
    }
    auto searchKey = std::make_pair(&decl, instTys);
    if (instantiatedDecls.count(searchKey) != 0) {
        for (auto& [parent, member] : std::as_const(genericMembersForInstantiatedDecl[searchKey])) {
            DiagnoseForInstantiatedMember(parent, member);
        }
        return;
    }
    auto typeMapping = GenerateTypeMapping(decl, instTys);
    CheckMembersWithInheritedDecls(decl);
    auto members = structInheritedMembers[&decl];
    for (auto& member : decl.GetMemberDecls()) {
        CJC_NULLPTR_CHECK(member);
        if (!Ty::IsTyCorrect(member->ty) || !member->outerDecl) {
            continue;
        }
        // Add constructors and private functions again for instantiated member's checking.
        if (IsInstanceConstructor(*member) || member->TestAttr(Attribute::PRIVATE)) {
            (void)UpdateInheritedMemberIfNeeded(members, MemberSignature{member, member->ty, decl.ty});
        }
    }
    MemberMap genericTyMembers;
    for (auto it = members.begin(); it != members.end();) {
        // Ignore generic member and non-function which will not causing collision after instantiation.
        if (it->second.decl->TestAttr(Attribute::GENERIC) || it->second.decl->astKind != ASTKind::FUNC_DECL) {
            it = members.erase(it);
        } else if (it->second.ty->HasGeneric()) {
            // Move generic ty member to other container.
            genericTyMembers.emplace(it->first, it->second);
            it = members.erase(it);
        } else {
            ++it;
        }
    }
    auto& cache = instantiatedTyCache[searchKey];
    auto getAndUpdateCache = [this, &cache, &typeMapping](auto ty) {
        auto found = cache.find(ty);
        if (found != cache.end()) {
            return found->second;
        } else {
            auto instTy = typeManager.GetInstantiatedTy(ty, typeMapping);
            cache[ty] = instTy;
            return instTy;
        }
    };
    for (auto [identifier, memberSig] : std::as_const(genericTyMembers)) {
        memberSig.ty = getAndUpdateCache(memberSig.ty);
        memberSig.structTy = getAndUpdateCache(memberSig.structTy);
        auto matches = members.equal_range(identifier);
        for (auto it = matches.first; it != matches.second; ++it) {
            auto parent = it->second;
            parent.structTy = getAndUpdateCache(parent.structTy);
            // Check & Diagnose for instantiated collision.
            DiagnoseForInstantiatedMember(parent, memberSig);
            genericMembersForInstantiatedDecl[searchKey].emplace_back(std::make_pair(parent, memberSig));
        }
        members.emplace(identifier, memberSig);
    }
}

std::set<Ptr<ExtendDecl>, CmpNodeByPos> StructInheritanceChecker::GetVisibleExtendsForInstantiation(
    const Decl& decl, const std::vector<Ptr<Ty>>& instTys)
{
    if (decl.astKind == ASTKind::EXTEND_DECL || !Ty::IsTyCorrect(decl.ty)) {
        return {};
    }
    std::set<Ptr<ExtendDecl>, CmpNodeByPos> orderedVisibleExtends;
    auto extends = typeManager.GetAllExtendsByTy(*decl.ty);
    for (auto& extend : extends) {
        // Check for extend decl if it is defined in current package or it has external attribute,
        // and also the instantiation meets extend constraints.
        bool visibleExtend = (IsExtendVisibleInCurpkg(*extend)) && extend->TestAttr(Attribute::GENERIC) &&
            typeManager.CheckGenericDeclInstantiation(extend, instTys);
        if (visibleExtend) {
            orderedVisibleExtends.emplace(extend);
        }
    }
    return orderedVisibleExtends;
}

bool StructInheritanceChecker::WillCauseInfiniteInstantiation(
    const Node& triggerNode, const Decl& decl, const std::vector<Ptr<Ty>>& instTys)
{
    auto generic = decl.GetGeneric();
    // Relation between 'generic' and 'instTys' already been checked in previous step in 'CheckInstDupFuncsRecursively'.
    CJC_ASSERT(generic && generic->typeParameters.size() == instTys.size());
    bool triggeredInside = false;
    TypeSubst typeMapping = institutionMaps.empty() ? TypeSubst{} : institutionMaps.top();
    if (HaveCyclicSubstitution(typeManager, GenerateTypeMapping(decl, instTys)) ||
        HaveCyclicSubstitution(typeManager, typeMapping)) {
        diag.Diagnose(triggerNode, DiagKind::sema_generic_infinite_instantiation);
        infiniteInstantiationOccured = true;
        return true;
    }
    if (!instTriggerInfos.empty()) {
        // For indirect instantiated case like:
        // class A<T> {
        //     func test(a: B<A<T>>) {}
        // }
        // class B<T> {
        //     func test(a: A<(T,T)>) {} <== detect infinite instantiation here.
        // }
        // A<Int32>()  <== triggered from here.
        // Index 1 of 'instTriggerInfo' value is the decl which previously triggered instantiation.
        triggeredInside = &decl == std::get<1>(instTriggerInfos.back());
    }
    Ptr<Ty> genericTy = TypeManager::GetInvalidTy();
    auto checkRecursion = [&genericTy, triggeredInside, typeMapping, this](auto ty) -> bool {
        CJC_ASSERT(ty);
        auto substitutedTy = typeManager.GetInstantiatedTy(ty, typeMapping);
        bool hasRecursion = ty->Contains(genericTy) || substitutedTy->Contains(genericTy);
        if (hasRecursion) {
            return true; // Directly and indirectly cause generic infinite instantiation.
        }
        // For indirect instantiated case.
        auto typeArg = Ty::GetGenericTyOfInsTy(*ty);
        bool noRecursion = !triggeredInside || (typeArg && typeArg->Contains(genericTy));
        if (noRecursion) {
            return false;
        }
        substitutedTy = Ty::GetGenericTyOfInsTy(*substitutedTy);
        return substitutedTy && substitutedTy->Contains(genericTy);
    };
    bool hasSelfRecursion = false;
    for (size_t i = 0; i < instTys.size(); ++i) {
        genericTy = generic->typeParameters[i]->ty; // Update ty which captured by lambda;
        hasSelfRecursion = std::any_of(instTys[i]->typeArgs.begin(), instTys[i]->typeArgs.end(), checkRecursion);
        if (hasSelfRecursion) {
            break;
        }
    }
    if (hasSelfRecursion) {
        diag.Diagnose(triggerNode, DiagKind::sema_generic_infinite_instantiation);
        infiniteInstantiationOccured = true;
    }
    return hasSelfRecursion;
}

VisitAction StructInheritanceChecker::CheckInstDupFuncsRecursively(Node& node)
{
    if (infiniteInstantiationOccured) {
        return VisitAction::STOP_NOW;
    }
    if (node.astKind == ASTKind::GENERIC) {
        MarkUpperBoundIgnored(node, static_cast<Generic&>(node));
    }
    if (!Ty::IsTyCorrect(node.ty) || node.TestAttr(Attribute::HAS_BROKEN)) {
        return VisitAction::WALK_CHILDREN;
    }
    auto [baseTy, target] = GetRealReferenceType(node);
    if (!target || target->TestAttr(Attribute::IN_REFERENCE_CYCLE) || !Ty::IsTyCorrect(baseTy)) {
        return VisitAction::WALK_CHILDREN;
    }
    if (target->IsNominalDecl() && target->TestAttr(Attribute::GENERIC)) {
        // NOTE: We must guarantee 'target' decl does not have 'generic infinite instantiation' status
        // before checking for its instantiated status, and also avoid re-entry.
        if (auto [_, success] = instantiatedDecls.insert(std::make_pair(target, std::vector<Ptr<Ty>>{})); success) {
            Walker(target, [this](auto node) { return CheckInstDupFuncsRecursively(*node); }).Walk();
        }
    }
    CheckInstWithCStructTypeArg(node);
    auto ref = DynamicCast<NameReferenceExpr*>(&node);
    std::vector<Ptr<Ty>> instTys = ref ? ref->instTys : typeManager.GetTypeArgs(*baseTy);
    TypeSubst typeMapping = GenerateTypeMapping(*target, instTys);
    // If failed to generate new typeMapping with current target, return now.
    if (typeMapping.empty()) {
        return VisitAction::WALK_CHILDREN;
    }
    // If current instantiation will cause infinite instantiation, return now.
    if (WillCauseInfiniteInstantiation(node, *target, instTys)) {
        return VisitAction::STOP_NOW;
    }
    if (!institutionMaps.empty()) {
        // Update instTys by current typeMapping.
        for (auto& ty : instTys) {
            ty = typeManager.GetInstantiatedTy(ty, institutionMaps.top());
        }
    }
    // Update instantiation trigger node if current node's ty is fully instantiated or current is outermost checking.
    bool updateTriggerInfo = !baseTy->HasGeneric() || institutionMaps.empty();
    auto instantiateCtx = InstantiatedContext(*this, &node, target, instTys, updateTriggerInfo);
    if (target->IsNominalDecl()) {
        // Check for function signatures.
        CheckInstMemberSignatures(*RawStaticCast<InheritableDecl*>(target), instTys);
    }
    auto extends = GetVisibleExtendsForInstantiation(*target, instTys);
    for (auto extend : extends) {
        // Check for function signatures of related extend decls.
        CheckInstMemberSignatures(*extend, instTys);
        // Extend will not appeared as referenced target, so we need cache it here.
        instantiatedDecls.insert(std::make_pair(target, instTys));
    }

    // Walk if current instantiation of target has not been checked.
    if (auto [_, success] = instantiatedDecls.insert(std::make_pair(target, instTys)); success) {
        CheckInstantiatedDecl(*target, instTys);
        // NOTE: since we instantiate extends on use, do not check for extend recursively here.
    }
    return VisitAction::WALK_CHILDREN;
}

void StructInheritanceChecker::CheckInstantiatedDecl(Decl& decl, const std::vector<Ptr<Ty>>& instTys)
{
    if (decl.IsBuiltIn()) {
        return; // Do not check for builtin decl.
    }
    TypeSubst typeMapping;
    if (auto ed = DynamicCast<ExtendDecl*>(&decl); ed && ed->ty && ed->ty->IsNominal()) {
        // For extend decl of a user defined type, treat the extend decl members' type as inside origin type decl.
        auto target = Ty::GetDeclPtrOfTy(ed->ty);
        if (target == nullptr) {
            return;
        }
        typeMapping = GenerateTypeMapping(*target, instTys);
        typeMapping.merge(GenerateTypeMapping(decl, ed->ty->typeArgs));
    } else {
        typeMapping = GenerateTypeMapping(decl, instTys);
    }
    institutionMaps.push(typeMapping);
    Walker(&decl, [this](auto node) { return CheckInstDupFuncsRecursively(*node); }).Walk();
    institutionMaps.pop();
}

void StructInheritanceChecker::CheckInstDupFuncsInNominalDecls()
{
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            // NOTE: We must guarantee 'target' decl does not have 'generic infinite instantiation' status
            // before checking for its instantiated status, and also avoid re-entry.
            if (auto [_, success] = instantiatedDecls.insert(std::make_pair(decl.get(), std::vector<Ptr<Ty>>{}));
                success) {
                Walker(decl.get(), [this](auto node) { return CheckInstDupFuncsRecursively(*node); }).Walk();
            }
        }
    }
}

void StructInheritanceChecker::CheckInstWithCStructTypeArg(const Node& node)
{
    if (!Utils::In(node.astKind, {ASTKind::REF_TYPE, ASTKind::REF_EXPR, ASTKind::MEMBER_ACCESS})) {
        return;
    }
    if (auto rt = DynamicCast<const RefType*>(&node); rt) {
        if (!rt->typeArguments.empty()) {
            CheckCStructArguments(*rt, *rt->ty, rt->leftAnglePos, rt->GetTypeArgs());
        }
        return;
    }
    // For the following case:
    //   @C
    //   struct Data {}
    //   enum TimeUnit<T1, T2> {
    //       | Year(T1)
    //       | Month(T1, T2)
    //   }
    //   var t = TimeUnit<Data, Int32>.Year(Data())
    // To avoid repeated error reporting, we check the RefExpr `TimeUnit<Data, Int32>`,
    // instead of MemberAccess `TimeUnit<Data, Int32>.Year`.
    auto target = node.GetTarget();
    if (!target) {
        return;
    }
    if (node.astKind == ASTKind::MEMBER_ACCESS && target->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
        return;
    }
    // The 'node' will only be 'RefExpr' or 'MemberAccess' here, which is must the type of NameReferenceExpr.
    auto& ref = static_cast<const NameReferenceExpr&>(node);
    if (ref.instTys.empty()) {
        return;
    }
    auto typeArgs = ref.GetTypeArgs();
    if (ref.instTys.size() == typeArgs.size()) {
        auto ty = ref.ty;
        if (ref.astKind == ASTKind::REF_EXPR && target->TestAttr(Attribute::CONSTRUCTOR)) {
            CJC_NULLPTR_CHECK(target->outerDecl);
            ty = target->outerDecl->ty;
        }
        Position leftAnglePos = ref.astKind == ASTKind::REF_EXPR ? static_cast<const RefExpr&>(ref).leftAnglePos
                                                                 : static_cast<const MemberAccess&>(ref).leftAnglePos;
        CheckCStructArguments(ref, *ty, leftAnglePos, typeArgs);
        return;
    }
}

void StructInheritanceChecker::CheckCStructArguments(const Node& node, const AST::Ty& ty, const Position& leftAnglePos,
    const std::vector<Ptr<Cangjie::AST::Type>>& typeArgs)
{
    if (typeArgs.empty()) {
        return;
    }
    if (Ty::IsCStructType(ty)) {
        (void)diag.Diagnose(node, leftAnglePos, DiagKind::sema_cffi_cannot_have_type_param, "struct with @C");
    }
    for (auto& type : typeArgs) {
        CheckCStructArgument(ty, *type);
    }
}

void StructInheritanceChecker::CheckCStructArgument(const Ty& ty, const Type& typeArg)
{
    if (!Ty::IsTyCorrect(typeArg.ty)) {
        return;
    }
    // Transitional state, only one will be retained in the future.
    if (ty.IsPointer()) {
        // The type arg of CPointer constraint:
        // 1. CPointer<T>, defined in core/CPointer.cj
        // 2. CPointer<`CType`>
        if (!typeArg.ty->IsGeneric() && !Ty::IsMetCType(*typeArg.ty)) {
            diag.Diagnose(typeArg, DiagKind::sema_illegal_cpointer_generic_type);
        }
        return;
    }
    // Only Array<CString>, Array<CPointer> is allowed. Other CType argument is not.
    if (ty.IsStructArray() && (typeArg.ty->IsCString() || typeArg.ty->IsPointer())) {
        return;
    }
    CheckCStruct(*typeArg.ty, typeArg);
}

void StructInheritanceChecker::CheckCStruct(const Ty& ty, const Type& typeArg)
{
    if (!Ty::IsTyCorrect(&ty)) {
        return;
    }
    // String and CPointer type is special, we should solve this after.
    if (ty.IsPointer()) {
        // The type arg of CPointer constraint:
        // 1. CPointer<T>, defined in core/CPointer.cj
        // 2. CPointer<`CType`>
        if (!ty.typeArgs.empty() && ty.typeArgs[0] && !ty.typeArgs[0]->IsGeneric() &&
            !Ty::IsMetCType(*ty.typeArgs[0])) {
            diag.Diagnose(typeArg, DiagKind::sema_illegal_cpointer_generic_type);
        }
        return;
    }
    if (auto fty = DynamicCast<const FuncTy*>(&ty); fty) {
        for (auto it : fty->typeArgs) {
            CheckCStruct(*it, typeArg);
        }
        return;
    }
    for (auto it : ty.typeArgs) {
        auto sty = DynamicCast<StructTy*>(it);
        if (sty == nullptr) {
            continue;
        }
        CheckCStruct(*it, typeArg);
    }
}

void StructInheritanceChecker::DiagnoseForInstantiatedMember(
    const MemberSignature& parent, const MemberSignature& child) const
{
    if (instTriggerInfos.empty()) {
        return;
    }
    auto parentFuncTy = DynamicCast<FuncTy*>(parent.ty);
    auto childFuncTy = DynamicCast<FuncTy*>(child.ty);
    bool sameStatus = parent.decl->TestAttr(Attribute::STATIC) == child.decl->TestAttr(Attribute::STATIC);
    bool isConflicted = sameStatus && parentFuncTy && childFuncTy &&
        typeManager.IsFuncParameterTypesIdentical(*parentFuncTy, *childFuncTy);
    if (isConflicted) {
        std::string functionName = child.decl->identifier;
        OrderedDeclSet candidates = {parent.decl, child.decl};
        auto [triggerNode, triggerDecl, instTys] = instTriggerInfos.back();
        std::string triggeredDeclType = triggerDecl->identifier.Val() + '<' + Ty::GetTypesToStr(instTys, ", ") + ">";
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_generic_instantiation_causes_ambiguous_functions,
            *triggerNode, triggeredDeclType, functionName);
        for (auto& candidate : candidates) {
            builder.AddNote(*candidate, MakeRangeForDeclIdentifier(*candidate), "found candidate");
        }
    }
}
