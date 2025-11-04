// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Analysis/ConstAnalysisWrapper.h"

namespace Cangjie::CHIR {
// thresholds for selecting analysis strategy
static constexpr size_t OVERHEAD_BLOCK_SIZE = 1000U;
static constexpr size_t USE_ACTIVE_BLOCK_SIZE = 300U;

// Helper: get block size for a lambda expression (0 if not lambda)
size_t ConstAnalysisWrapper::GetBlockSize(const Expression& expr)
{
    size_t blockSize = 0;
    if (expr.GetExprKind() != ExprKind::LAMBDA) {
        return blockSize;
    }
    auto lambdaBody = Cangjie::StaticCast<const Lambda&>(expr).GetBody();
    blockSize += lambdaBody->GetBlocks().size();
    auto postVisit = [&blockSize](Expression& e) {
        blockSize += GetBlockSize(e);
        return VisitResult::CONTINUE;
    };
    Visitor::Visit(*lambdaBody, postVisit);
    return blockSize;
}

// Count all blocks in func, including lambda expr.
size_t ConstAnalysisWrapper::CountBlockSize(const Func& func)
{
    size_t blockSize = func.GetBody()->GetBlocks().size();
    if (blockSize > OVERHEAD_BLOCK_SIZE) {
        return OVERHEAD_BLOCK_SIZE + 1;
    }
    for (auto block : func.GetBody()->GetBlocks()) {
        for (auto e : block->GetExpressions()) {
            blockSize += GetBlockSize(*e);
            if (blockSize > OVERHEAD_BLOCK_SIZE) {
                return OVERHEAD_BLOCK_SIZE + 1;
            }
        }
    }
    return blockSize;
}

ConstAnalysisWrapper::ConstAnalysisWrapper(CHIRBuilder& builder) : builder(builder)
{
}

Results<ConstDomain>* ConstAnalysisWrapper::CheckFuncResult(const Func& func)
{
    if (auto it = resultsMap.find(&func); it != resultsMap.end()) {
        return it->second.get();
    } else {
        // pool domain result only using for analysis, also return nullptr.
        return nullptr;
    }
}

void ConstAnalysisWrapper::InvalidateAllAnalysisResults()
{
    funcWithPoolDomain.clear();
    resultsMap.clear();
};

ConstAnalysisWrapper::AnalysisStrategy ConstAnalysisWrapper::ChooseAnalysisStrategy(const Func& func)
{
    if (resultsMap.find(&func) != resultsMap.end() ||
        funcWithPoolDomain.find(&func) != funcWithPoolDomain.end()) {
        // already analysed
        return AnalysisStrategy::SkipAnalysis;
    }
    if (!ConstAnalysis<ConstStatePool>::Filter(func)) {
        // judge condition inner analysis engine
        return AnalysisStrategy::SkipAnalysis;
    }
    size_t blockSize = CountBlockSize(func);
    if (blockSize > OVERHEAD_BLOCK_SIZE) {
        // huge function, skip analysis
        return AnalysisStrategy::SkipAnalysis;
    } else if (blockSize > USE_ACTIVE_BLOCK_SIZE) {
        // use active pool domain
        return AnalysisStrategy::ActiveStatePool;
    } else {
        // use full state pool domain
        return AnalysisStrategy::FullStatePool;
    }
}
}  // namespace Cangjie::CHIR
