// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_TRANSFORMATION_RANGE_PROPAGATION_H
#define CANGJIE_CHIR_TRANSFORMATION_RANGE_PROPAGATION_H

#include "cangjie/CHIR/Analysis/AnalysisWrapper.h"
#include "cangjie/CHIR/Analysis/ValueRangeAnalysis.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Transformation/DeadCodeElimination.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/CHIR/Value.h"

namespace Cangjie::CHIR {
/**
 * CHIR Opt Pass: do optimization with analysis results of range analysis.
 */
class RangePropagation {
public:
    /**
     * @brief range analysis wrapper to call range analysis.
     */
    using RangeAnalysisWrapper = AnalysisWrapper<RangeAnalysis, RangeDomain>;

    /**
     * @brief constructor to do range propagation.
     * @param builder CHIR builder for generating IR.
     * @param rangeAnalysisWrapper range analysis wrapper which produce analysis results.
     * @param diag reporter to print warning
     * @param enIncre flag whether is incremental compile.
     */
    explicit RangePropagation(
        CHIRBuilder& builder, RangeAnalysisWrapper* rangeAnalysisWrapper, DiagAdapter* diag, bool enIncre);

    /**
     * @brief Main process to do range propagation.
     * @param package package to do optimization.
     * @param isDebug flag whether print debug log.
     */
    void RunOnPackage(const Ptr<const Package>& package, bool isDebug);

    /**
     * @brief Main process to do const propagation per func.
     * @param func func to do optimization.
     * @param isDebug flag whether print debug log.
     */
    void RunOnFunc(const Ptr<const Func>& func, bool isDebug);

    /**
     * @brief Get effect map after this pass.
     * @return effect map affected by this pass.
     */
    const OptEffectCHIRMap& GetEffectMap() const;

    /**
     * @brief Get all funcs need to remove unreachable blocks.
     * @return functions
     */
    const std::vector<const Func*>& GetFuncsNeedRemoveBlocks() const;
private:
    struct RewriteInfo {
        Expression* oldExpr;
        size_t index; // the index of the oldExpr in its parent block
        LiteralValue* literalVal;

        RewriteInfo(Expression* oldExpr, size_t index, LiteralValue* literalVal)
            : oldExpr(oldExpr), index(index), literalVal(literalVal)
        {
        }
    };

    // ==================== Rewrite Non-terminator Expressions ==================== //

    /**
     * This function will generate a literal value based on the range information
     * from @p constVal. The type of the literal value is @p type.
     */
    Ptr<LiteralValue> GenerateConstExpr(const Ptr<Type>& type, const Ptr<const ValueRange>& rangeVal);

    /**
     * This function will rewrite an expression based on the @p rewriteInfo, which stores
     * the exrpession to be rewrited, the index of this expression and the new expression.
     */
    void RewriteToConstExpr(const RewriteInfo& rewriteInfo, bool isDebug) const;

    // ==================== Rewrite Terminator Expressions ==================== //

    /**
     * This function will rewrite a Branch terminator or a MultiBranch terminator to a GoTo
     * terminator. The new successor will be @p targetSucc.
     */
    void RewriteBranchTerminator(const Ptr<Terminator>& branch, const Ptr<Block>& targetSucc, bool isDebug);

    void RecordEffectMap(const Expression* expr, const Func* func) const;

    void CheckVarrayIndex(const Ptr<Intrinsic>& intrin, const RangeDomain& state) const;

    CHIRBuilder& builder;
    RangeAnalysisWrapper* analysisWrapper;
    DiagAdapter* diag;
    bool enIncre;
    static OptEffectCHIRMap effectMap;
    std::vector<const Func*> funcsNeedRemoveBlocks;
};

} // namespace Cangjie::CHIR
#endif
