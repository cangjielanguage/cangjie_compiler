// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Analysis/GetOrThrowResultAnalysis.h"
#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/IR/Type/StructDef.h"
#include "cangjie/CHIR/Utils/Utils.h"

using namespace Cangjie::CHIR;

GetOrThrowResultDomain::GetOrThrowResultDomain(std::unordered_map<const Value*, size_t>* ArgIdxMap)
    : AbstractDomain(),
      GetOrThrowResults(std::vector<FlatSet<const ApplyBase*>>(ArgIdxMap->size(), FlatSet<const ApplyBase*>(false))),
      ArgIdxMap(ArgIdxMap)
{
}

bool GetOrThrowResultDomain::Join(const GetOrThrowResultDomain& rhs)
{
    this->kind = ReachableKind::REACHABLE;
    return VectorJoin(GetOrThrowResults, rhs.GetOrThrowResults);
}

std::string GetOrThrowResultDomain::ToString() const
{
    if (this->kind == ReachableKind::UNREACHABLE) {
        return "Unreachable";
    } else {
        std::stringstream ss;
        ss << "{ ";
        for (auto& result : GetOrThrowResults) {
            ss << result.ToString() << ", ";
        }
        ss << "}";
        return ss.str();
    }
}

const ApplyBase* GetOrThrowResultDomain::CheckGetOrThrowResult(const Value* location) const
{
    if (auto it = ArgIdxMap->find(location); it != ArgIdxMap->end()) {
        return GetOrThrowResults[it->second].GetElem().value_or(nullptr);
    } else {
        return nullptr;
    }
}

template <> const std::string Analysis<GetOrThrowResultDomain>::name = "getOrThrow-result";
template <> const std::optional<unsigned> Analysis<GetOrThrowResultDomain>::blockLimit = std::nullopt;

GetOrThrowResultAnalysis::GetOrThrowResultAnalysis(const Function* func) : Analysis(func)
{
    size_t argIdx = 0;
    for (auto bb : func->GetBody()->GetBlocks()) {
        for (auto expr : bb->GetExpressions()) {
            if (IsGetOrThrowFunction(*expr)) {
                auto apply = StaticCast<const ApplyBase*>(expr);
                CJC_ASSERT(apply->GetArgs().size() > 0);
                auto arg = apply->GetArgs()[0];
                if (auto it = ArgIdxMap.find(arg); it == ArgIdxMap.end()) {
                    ArgIdxMap.emplace(arg, argIdx++);
                }
            }
        }
    }
}

GetOrThrowResultDomain GetOrThrowResultAnalysis::Bottom()
{
    return GetOrThrowResultDomain(&ArgIdxMap);
}

// Set the initial state of the Function entryBB to Top to make sure the
// initial state of all the BB in the function is correct.
// 1. the entryBB of Function will dominate all the other BB.
// 2. the Top state will dominate all the other state.
void GetOrThrowResultAnalysis::InitializeFuncEntryState(GetOrThrowResultDomain& state)
{
    state.kind = ReachableKind::REACHABLE;
    for (auto i = state.GetOrThrowResults.begin(); i != state.GetOrThrowResults.end(); ++i) {
        i->SetToBound(/* isTop = */ true);
    }
}

void GetOrThrowResultAnalysis::PropagateExpressionEffect(GetOrThrowResultDomain& state, const Expression* expression)
{
    if (IsGetOrThrowFunction(*expression)) {
        auto apply = StaticCast<const ApplyBase*>(expression);
        CJC_ASSERT(apply->GetArgs().size() > 0);
        auto arg = apply->GetArgs()[0];
        if (auto it = ArgIdxMap.find(arg); it != ArgIdxMap.end()) {
            // Update the result of getOrThrow when the arg of getOrThrow is
            // first seen in this block;
            if (state.GetOrThrowResults[it->second].IsBottom() || state.GetOrThrowResults[it->second].IsTop()) {
                state.GetOrThrowResults[it->second].UpdateElem(apply);
            }
        }
    }
}

bool GetOrThrowResultAnalysis::JoinSuccessorState(GetOrThrowResultDomain& successorState,
    const GetOrThrowResultDomain& state, const Expression* terminator, const Block* successor)
{
    if (IsGetOrThrowFunction(*terminator) &&
        StaticCast<const TryApply*>(terminator)->GetSuccessBlock() == successor) {
        auto successState = state;
        PropagateExpressionEffect(successState, terminator);
        return successorState.Join(successState);
    }
    return successorState.Join(state);
}
