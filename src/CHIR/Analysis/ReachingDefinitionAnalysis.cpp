// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Analysis/ReachingDefinitionAnalysis.h"

#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/Type/StructDef.h"
#include "cangjie/CHIR/Visitor/Visitor.h"

using namespace Cangjie;
using namespace Cangjie::CHIR;

ReachingDefinitionDomain::ReachingDefinitionDomain(std::unordered_map<const Value*, size_t>* allocateIdxMap)
    : AbstractDomain(),
      reachingDefs(std::vector<FlatSet<const Store*>>(allocateIdxMap->size(), FlatSet<const Store*>(false))),
      reachingLoadDefs(std::vector<FlatSet<const Load*>>(allocateIdxMap->size(), FlatSet<const Load*>(false))),
      allocateIdxMap(allocateIdxMap)
{
}

bool ReachingDefinitionDomain::Join(const ReachingDefinitionDomain& rhs)
{
    this->kind = ReachableKind::REACHABLE;
    // should not use ||, cannot be short-circuited
    auto res1 = VectorJoin(reachingDefs, rhs.reachingDefs);
    auto res2 = VectorJoin(reachingLoadDefs, rhs.reachingLoadDefs);
    return res1 || res2;
}

std::string ReachingDefinitionDomain::ToString() const
{
    if (this->kind == ReachableKind::UNREACHABLE) {
        return "Unreachable";
    } else {
        std::stringstream ss;
        ss << "{ ";
        for (auto& def : reachingDefs) {
            ss << def.ToString() << ", ";
        }
        ss << "} ";
        ss << " { ";
        for (auto& def : reachingLoadDefs) {
            ss << def.ToString() << ", ";
        }
        ss << "}";
        return ss.str();
    }
}

const Store* ReachingDefinitionDomain::CheckReachingDef(const Value* location) const
{
    if (auto it = allocateIdxMap->find(location); it != allocateIdxMap->end()) {
        return reachingDefs[it->second].GetElem().value_or(nullptr);
    } else {
        return nullptr;
    }
}

const Load* ReachingDefinitionDomain::CheckReachingLoadDef(const Value* location) const
{
    if (auto it = allocateIdxMap->find(location); it != allocateIdxMap->end()) {
        return reachingLoadDefs[it->second].GetElem().value_or(nullptr);
    } else {
        return nullptr;
    }
}

template <> const std::string Analysis<ReachingDefinitionDomain>::name = "reaching-definition";
template <> const std::optional<unsigned> Analysis<ReachingDefinitionDomain>::blockLimit = std::nullopt;

ReachingDefinitionAnalysis::ReachingDefinitionAnalysis(const Func* func) : Analysis(func)
{
    size_t allocateIdx = 0;
    size_t worklistIdx = 0;
    auto worklist = func->GetBody()->GetBlocks();
    while (worklistIdx != worklist.size()) {
        auto bb = worklist[worklistIdx];
        for (auto expr : bb->GetExpressions()) {
            Type* allocatedTy = nullptr;
            auto kind = expr->GetExprKind();
            if (kind == ExprKind::ALLOCATE) {
                allocatedTy = StaticCast<Allocate*>(expr)->GetType();
            } else if (kind == ExprKind::ALLOCATE_WITH_EXCEPTION) {
                allocatedTy = StaticCast<AllocateWithException*>(expr)->GetType();
            } else if (kind == ExprKind::LAMBDA) {
                auto blocks = StaticCast<const Lambda*>(expr)->GetBody()->GetBlocks();
                worklist.insert(worklist.end(), blocks.begin(), blocks.end());
            }
            const auto canBeTracked = [](const Type* allocatedTy) {
                if (allocatedTy->IsClass()) {
                    // It's useless to track an allocation like Allocate(Class-CA).
                    // We will never directly store to a Class-CA&.
                    return false;
                }
                if (allocatedTy->IsVArray()) {
                    // A VArray can be passed into a CFunc.
                    return false;
                }
                if (allocatedTy->IsStruct()) {
                    auto structDef = StaticCast<const StructType*>(allocatedTy)->GetStructDef();
                    if (structDef->IsCStruct()) {
                        return false;
                    }
                }
                if (allocatedTy->IsGeneric()) {
                    return false;
                }
                return true;
            };
            if (allocatedTy && canBeTracked(allocatedTy)) {
                allocateIdxMap.emplace(expr->GetResult(), allocateIdx++);
            }
        }
        ++worklistIdx;
    }
}

ReachingDefinitionDomain ReachingDefinitionAnalysis::Bottom()
{
    return ReachingDefinitionDomain(&allocateIdxMap);
}

void ReachingDefinitionAnalysis::InitializeFuncEntryState(ReachingDefinitionDomain& state)
{
    state.kind = ReachableKind::REACHABLE;
    for (size_t i = 0; i < state.allocateIdxMap->size(); ++i) {
        state.reachingDefs[i].SetToBound(/* isTop = */ true);
        state.reachingLoadDefs[i].SetToBound(/* isTop = */ true);
    }
}

void ReachingDefinitionAnalysis::HandleVarStateCapturedByLambda(ReachingDefinitionDomain& state, const Lambda* lambda)
{
    for (auto var : GetLambdaCapturedVarsRecursively(*lambda)) {
        if (auto it = state.allocateIdxMap->find(var); it != state.allocateIdxMap->end()) {
            state.reachingDefs[it->second].SetToBound(/* isTop = */ true);
            state.reachingLoadDefs[it->second].SetToBound(/* isTop = */ true);
        }
    }
}

void ReachingDefinitionAnalysis::PropagateExpressionEffect(
    ReachingDefinitionDomain& state, const Expression* expression)
{
    switch (expression->GetExprKind()) {
        case ExprKind::STORE:
            return HandleStoreExpr(state, StaticCast<const Store*>(expression));
        case ExprKind::GET_ELEMENT_REF:
            return HandleGetElemRefExpr(state, StaticCast<const GetElementRef*>(expression));
        case ExprKind::STORE_ELEMENT_REF:
            return HanldeStoreElemRefExpr(state, *StaticCast<const StoreElementRef*>(expression));
        case ExprKind::APPLY:
            return HandleApplyExpr(state, StaticCast<const Apply*>(expression));
        case ExprKind::INTRINSIC:
            return HandleIntrinsicExpr(state, StaticCast<const Intrinsic*>(expression));
        case ExprKind::LOAD:
            return HandleLoadExpr(state, StaticCast<const Load*>(expression));
        default:
            return;
    }
}

std::optional<Block*> ReachingDefinitionAnalysis::PropagateTerminatorEffect(
    ReachingDefinitionDomain& state, const Terminator* terminator)
{
    if (terminator->GetExprKind() == ExprKind::APPLY_WITH_EXCEPTION) {
        HandleApplyExpr(state, StaticCast<const ApplyWithException*>(terminator));
    } else if (terminator->GetExprKind() == ExprKind::INTRINSIC_WITH_EXCEPTION) {
        HandleIntrinsicExpr(state, StaticCast<const IntrinsicWithException*>(terminator));
    }
    return std::nullopt;
}

void ReachingDefinitionAnalysis::HandleStoreExpr(ReachingDefinitionDomain& state, const Store* store)
{
    // If it's a store to a tracked allocation.
    auto target = store->GetLocation();
    if (auto it = allocateIdxMap.find(target); it != allocateIdxMap.end()) {
        state.reachingDefs[it->second].UpdateElem(store);
        state.reachingLoadDefs[it->second].SetToBound(/* isTop = */ true);
    }
}

void ReachingDefinitionAnalysis::HandleGetElemRefExpr(ReachingDefinitionDomain& state, const GetElementRef* getElemRef)
{
    // If it's a non-readonly GetElementRef to a tracked struct.
    auto target = getElemRef->GetLocation();
    if (auto it = allocateIdxMap.find(target); it != allocateIdxMap.end()) {
        if (StaticCast<RefType*>(target->GetType())->GetBaseType()->IsStruct()) {
            if (!getElemRef->GetResult()->TestAttr(Attribute::READONLY)) {
                state.reachingDefs[it->second].SetToBound(/* isTop = */ true);
                state.reachingLoadDefs[it->second].SetToBound(/* isTop = */ true);
            }
        }
    }
}

void ReachingDefinitionAnalysis::HanldeStoreElemRefExpr(
    ReachingDefinitionDomain& state, const StoreElementRef& storeElemRef)
{
    auto target = storeElemRef.GetLocation();
    if (auto it = allocateIdxMap.find(target); it != allocateIdxMap.end()) {
        if (StaticCast<RefType*>(target->GetType())->GetBaseType()->IsStruct()) {
            state.reachingDefs[it->second].SetToBound(/* isTop = */ true);
            state.reachingLoadDefs[it->second].SetToBound(/* isTop = */ true);
        }
    }
}

void ReachingDefinitionAnalysis::HandleLoadExpr(ReachingDefinitionDomain& state, const Load* load)
{
    // If it's a load to a tracked allocation.
    auto target = load->GetLocation();
    if (auto it = allocateIdxMap.find(target); it != allocateIdxMap.end()) {
        // If there is a Top, it means that the current Load we are visiting is the first Load operation after the
        // value stored in an allocation has changed. If there is already a Load expression, it means that the
        // current Load we are visiting is redundant.
        if (state.reachingLoadDefs[it->second].IsTop()) {
            state.reachingLoadDefs[it->second].UpdateElem(load);
        }
    }
}