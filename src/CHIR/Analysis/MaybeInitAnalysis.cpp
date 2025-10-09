// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Analysis/MaybeInitAnalysis.h"

#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/CHIRCasting.h"

using namespace Cangjie::CHIR;

MaybeInitDomain::MaybeInitDomain(size_t domainSize, const ConstructorInitInfo* ctorInitInfo,
    std::unordered_map<const Value*, size_t>* allocateIdxMap)
    : GenKillDomain(domainSize), ctorInitInfo(ctorInitInfo), allocateIdxMap(allocateIdxMap)
{
}

std::optional<bool> MaybeInitDomain::IsMaybeInitedAllocation(const Value* location) const
{
    if (auto it = allocateIdxMap->find(location); it != allocateIdxMap->end()) {
        return IsTrueAt(it->second);
    } else {
        return std::nullopt;
    }
}

MaybeInitDomain::InitedMemberKind MaybeInitDomain::IsMaybeInitedMember(size_t memberIndex) const
{
    CJC_ASSERT(ctorInitInfo);
    if (memberIndex < ctorInitInfo->superMemberNums) {
        return IsTrueAt(states.size() - 1) ? InitedMemberKind::SUPER_MEMBER : InitedMemberKind::NA;
    } else if (memberIndex < ctorInitInfo->superMemberNums + ctorInitInfo->localMemberNums) {
        return IsTrueAt(memberIndex - ctorInitInfo->superMemberNums) ? InitedMemberKind::LOCAL_MEMBER
                                                                     : InitedMemberKind::NA;
    }

    CJC_ABORT();
    return InitedMemberKind::NA;
}

void MaybeInitDomain::SetAllLocalMemberInited()
{
    for (size_t i = 0; i < ctorInitInfo->localMemberNums; ++i) {
        Gen(i);
    }
}

template <> const std::string Analysis<MaybeInitDomain>::name = "maybe-init";
template <> const std::optional<unsigned> Analysis<MaybeInitDomain>::blockLimit = std::nullopt;
template <> const AnalysisKind GenKillDomain<MaybeInitDomain>::mustOrMaybe = AnalysisKind::MAYBE;

MaybeInitAnalysis::MaybeInitAnalysis(const Func* func, const ConstructorInitInfo* ctorInitInfo)
    : GenKillAnalysis(func), ctorInitInfo(ctorInitInfo)
{
    size_t allocateIdx = ctorInitInfo->localMemberNums;
    SaveAllocateMap(*func->GetBody(), allocateIdx, allocateIdxMap);

    domainSize = ctorInitInfo->localMemberNums + allocateIdx;
    if (ctorInitInfo->superClassDef) {
        ++domainSize;
    }
}

MaybeInitDomain MaybeInitAnalysis::Bottom()
{
    return MaybeInitDomain(domainSize, ctorInitInfo, &allocateIdxMap);
}

void MaybeInitAnalysis::InitializeFuncEntryState(MaybeInitDomain& state)
{
    // all the bits have already been set to 0 (uninitialised)
    state.kind = ReachableKind::REACHABLE;
}

void MaybeInitAnalysis::PropagateExpressionEffect(MaybeInitDomain& state, const Expression* expression)
{
    if (expression->GetExprKind() == ExprKind::ALLOCATE) {
        return HandleAllocateExpr(state, StaticCast<const Allocate*>(expression));
    } else if (expression->GetExprKind() == ExprKind::STORE) {
        return HandleStoreExpr(state, StaticCast<const Store*>(expression));
    } else if (expression->GetExprKind() == ExprKind::STORE_ELEMENT_REF) {
        return HandleStoreElemRefExpr(state, StaticCast<const StoreElementRef*>(expression));
    } else if (expression->GetExprKind() == ExprKind::APPLY) {
        return HandleApplyExpr(state, StaticCast<const Apply*>(expression));
    }
}

std::optional<Block*> MaybeInitAnalysis::PropagateTerminatorEffect(MaybeInitDomain& state, const Terminator* expression)
{
    (void)state;
    (void)expression;
    return std::nullopt;
}

void MaybeInitAnalysis::HandleAllocateExpr(MaybeInitDomain& state, const Allocate* allocate)
{
    auto res = allocate->GetResult();
    if (auto it = allocateIdxMap.find(res); it != allocateIdxMap.end()) {
        auto allcoateIdx = it->second;
        state.Kill(allcoateIdx);
    }
}

void MaybeInitAnalysis::HandleStoreExpr(MaybeInitDomain& state, const Store* store)
{
    auto location = store->GetLocation();
    if (auto it = allocateIdxMap.find(location); it != allocateIdxMap.end()) {
        auto allocateIdx = it->second;
        state.Gen(allocateIdx);
    }
}

void MaybeInitAnalysis::HandleStoreElemRefExpr(MaybeInitDomain& state, const StoreElementRef* store) const
{
    // modify by cjmp, adapt for var init func create
    auto parentFunc = store->GetTopLevelFunc();
    CJC_NULLPTR_CHECK(parentFunc);
    auto memberIdxOpt = IsInitialisingMemberVar(*parentFunc, *store);
    if (!memberIdxOpt.has_value()) {
        return;
    }
    auto memberIdx = memberIdxOpt.value();
    CJC_ASSERT(memberIdx < ctorInitInfo->superMemberNums + ctorInitInfo->localMemberNums);
    if (memberIdx < ctorInitInfo->superMemberNums) {
        return;
    }
    state.Gen(memberIdx - ctorInitInfo->superMemberNums);
}

void MaybeInitAnalysis::HandleApplyExpr(MaybeInitDomain& state, const Apply* apply) const
{
    if (!func->IsConstructor()) {
        return;
    }
    // Check if it is a call to super init function of this class
    if (apply->IsSuperCall()) {
        state.Gen(domainSize - 1);
        return;
    }
    // Check if it is a call to another init function of this class/struct
    auto callee = apply->GetCallee();
    if (callee->IsFuncWithBody()) {
        auto calleeFunc = VirtualCast<Func*>(callee);
        if (calleeFunc->IsConstructor() &&
            calleeFunc->GetOuterDeclaredOrExtendedDef() == ctorInitInfo->thisCustomDef &&
            apply->GetArgs()[0] == func->GetParam(0)) {
            state.SetAllLocalMemberInited();
            if (ctorInitInfo->superClassDef) {
                state.Gen(domainSize - 1);
            }
        }
    }
}