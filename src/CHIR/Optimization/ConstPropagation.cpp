// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Optimization/ConstPropagation.h"

#include "cangjie/CHIR/Analysis/Engine.h"
#include "cangjie/CHIR/IR/Annotation.h"

using namespace Cangjie::CHIR;

ConstPropagation::ConstPropagation(CHIRBuilder& builder, ConstAnalysisWrapper* constAnalysisWrapper,
    const GlobalOptions& options)
    : builder(builder), analysisWrapper(constAnalysisWrapper), opts{options}
{
}

void ConstPropagation::RunOnPackage(const Ptr<const Package>& package, bool isDebug, bool isCJLint)
{
    for (auto func : package->GetGlobalFuncs()) {
        RunOnFunc(func, isDebug, isCJLint);
    }
}

void ConstPropagation::RunOnFunc(const Ptr<const Func>& func, bool isDebug, bool isCJLint)
{
    bool isCommonFunctionWithoutBody = func->TestAttr(Attribute::SKIP_ANALYSIS);
    if (isCommonFunctionWithoutBody) {
        return; // Nothing to check
    }
    if (auto result = analysisWrapper->CheckFuncResult(*func); result) {
        VisitFunc<ConstDomain>(*func, isDebug, isCJLint, *result);
    } else if (auto resultPool = analysisWrapper->CheckFuncActiveResult(*func); resultPool) {
        VisitFunc<ConstPoolDomain>(*func, isDebug, isCJLint, *resultPool);
    }
}

const OptEffectCHIRMap& ConstPropagation::GetEffectMap() const
{
    return effectMap;
}

const std::vector<const Func*>& ConstPropagation::GetFuncsNeedRemoveBlocks() const
{
    return funcsNeedRemoveBlocks;
}

template <typename TConstDomain>
void ConstPropagation::VisitFunc(const Func& func, bool isDebug, bool isCJLint, Results<TConstDomain>& result)
{
    std::vector<RewriteInfo> toBeRewrited;
    std::unordered_map<Terminator*, std::pair<LiteralValue*, Block*>> targetSuccMap;
    const auto actionBeforeVisitExpr = [](const TConstDomain&, Expression*, size_t) {};
    const auto actionAfterVisitExpr = [this, &toBeRewrited, &func, isDebug, isCJLint](
                                          const TConstDomain& state, Expression* expr, size_t index) -> void {
        auto exprType = expr->GetResult()->GetType();
        if (expr->IsBinaryExpr()) {
            if (auto absVal = state.CheckAbstractValue(expr->GetResult()); absVal) {
                return (void)toBeRewrited.emplace_back(expr, index, GenerateConstExpr(exprType, absVal, isCJLint));
            } else if (expr->GetResult()->GetType()->IsInteger()) {
                auto binary = StaticCast<BinaryExpression*>(expr);
                if (auto intTy = StaticCast<IntType*>(expr->GetResult()->GetType()); intTy->IsSigned()) {
                    return TrySimplifyingBinaryExpr<ConstIntVal>(state, binary, isDebug);
                } else {
                    return TrySimplifyingBinaryExpr<ConstUIntVal>(state, binary, isDebug);
                }
            }
        }
        if (expr->IsUnaryExpr()) {
            if (auto absVal = state.CheckAbstractValue(expr->GetResult()); absVal) {
                return (void)toBeRewrited.emplace_back(expr, index, GenerateConstExpr(exprType, absVal, isCJLint));
            } else {
                return TrySimplifyingUnaryExpr(StaticCast<UnaryExpression*>(expr), isDebug);
            }
        }
        if ((exprType->IsInteger() || exprType->IsFloat() || exprType->IsRune() ||
            exprType->IsBoolean() || exprType->IsString()) &&
            (expr->IsLoad() || expr->IsTypeCast() || expr->IsField())) {
            auto absVal = state.CheckAbstractValue(expr->GetResult());
            if (absVal) {
                toBeRewrited.emplace_back(expr, index, GenerateConstExpr(exprType, absVal, isCJLint));
                RecordEffectMap(expr, &func);
            }
        }
    };
    const auto actionOnTerminator = [this, &targetSuccMap, isCJLint](const TConstDomain& state, Terminator* terminator,
                                        std::optional<Block*> targetSucc) {
        if (!targetSucc.has_value()) {
            return;
        }
        switch (terminator->GetExprKind()) {
            case ExprKind::BRANCH:
            case ExprKind::MULTIBRANCH:
                return (void)targetSuccMap.emplace(terminator, std::make_pair(nullptr, targetSucc.value()));
            case ExprKind::INT_OP_WITH_EXCEPTION:
            case ExprKind::TYPECAST_WITH_EXCEPTION: {
                auto res = terminator->GetResult();
                if (auto absVal = state.CheckAbstractValue(res)) {
                    targetSuccMap.emplace(terminator,
                        std::make_pair(GenerateConstExpr(res->GetType(), absVal, isCJLint), targetSucc.value()));
                }
                return;
            }
            default:
                break;
        }
    };
    result.VisitWith(actionBeforeVisitExpr, actionAfterVisitExpr, actionOnTerminator);
    for (auto& rewriteInfo : toBeRewrited) {
        RewriteToConstExpr(rewriteInfo, isDebug);
    }
    for (auto& [terminator, v] : targetSuccMap) {
        RewriteTerminator(terminator, v.first, v.second, isDebug);
    }
    if (!targetSuccMap.empty()) {
        funcsNeedRemoveBlocks.push_back(&func);
    }
}

Ptr<LiteralValue> ConstPropagation::GenerateConstExpr(
    const Ptr<Type>& type, const Ptr<const ConstValue>& constVal, bool isCJLint)
{
    switch (constVal->GetConstKind()) {
        case ConstValue::ConstKind::UINT:
            return builder.CreateLiteralValue<IntLiteral>(type, StaticCast<const ConstUIntVal*>(constVal)->GetVal());
        case ConstValue::ConstKind::INT:
            return builder.CreateLiteralValue<IntLiteral>(
                type, static_cast<uint64_t>(StaticCast<const ConstIntVal*>(constVal)->GetVal()));
        case ConstValue::ConstKind::FLOAT:
            return builder.CreateLiteralValue<FloatLiteral>(type, StaticCast<const ConstFloatVal*>(constVal)->GetVal());
        case ConstValue::ConstKind::RUNE:
            return builder.CreateLiteralValue<RuneLiteral>(type, StaticCast<const ConstRuneVal*>(constVal)->GetVal());
        case ConstValue::ConstKind::BOOL:
            return builder.CreateLiteralValue<BoolLiteral>(type, StaticCast<const ConstBoolVal*>(constVal)->GetVal());
        case ConstValue::ConstKind::STRING: {
            if (isCJLint) {
                return builder.CreateLiteralValue<StringLiteral>(
                    type, StaticCast<const ConstStrVal*>(constVal)->GetVal());
            } else {
                return nullptr;
            }
        }

        default:
#ifndef NDEBUG
            CJC_ABORT();
#else
            return nullptr;
#endif
    }
}

using namespace Cangjie;
static bool SkipCP(const Expression& expr, const GlobalOptions& opts)
{
    if (!opts.IsOptimizationExisted(GlobalOptions::OptimizationFlag::CONST_PROPAGATION)) {
        // const propagation disabled, operate only on nodes generated from for-in
        if (expr.Get<GeneratedFromForIn>()) {
            return false;
        }
        if (auto br = DynamicCast<Branch>(&expr)) {
            return br->GetSourceExpr() != SourceExpr::FOR_IN_EXPR;
        }
        if (auto st = DynamicCast<Load>(&expr)) {
            if (auto alloc = DynamicCast<LocalVar>(st->GetLocation())) {
                return !alloc->GetExpr()->Get<GeneratedFromForIn>();
            }
        }
        return true;
    }
    return false;
}

template <typename T, typename TConstDomain>
void ConstPropagation::TrySimplifyingBinaryExpr(
    const TConstDomain& state, const Ptr<BinaryExpression>& binary, bool isDebug)
{
    if (SkipCP(*binary, opts)) {
        return;
    }
    auto lhs = static_cast<const T*>(state.CheckAbstractValue(binary->GetLHSOperand()));
    auto rhs = static_cast<const T*>(state.CheckAbstractValue(binary->GetRHSOperand()));

    switch (binary->GetExprKind()) {
        case ExprKind::ADD: {
            // `0 + a` => a, `a + 0` => a
            if (lhs && lhs->GetVal() == 0) {
                return ReplaceUsageOfExprResult(binary, binary->GetRHSOperand(), isDebug);
            } else if (rhs && rhs->GetVal() == 0) {
                return ReplaceUsageOfExprResult(binary, binary->GetLHSOperand(), isDebug);
            }
            return;
        }
        case ExprKind::LSHIFT:
        case ExprKind::RSHIFT:
        case ExprKind::SUB: {
            // `a << 0` => a, `a >> 0` => a, `a - 0` => a
            if (rhs && rhs->GetVal() == 0) {
                return ReplaceUsageOfExprResult(binary, binary->GetLHSOperand(), isDebug);
            }
            return;
        }
        case ExprKind::MUL: {
            // `1 * a` => a, `a * 1` => a
            if (lhs && lhs->GetVal() == 1) {
                return ReplaceUsageOfExprResult(binary, binary->GetRHSOperand(), isDebug);
            } else if (rhs && rhs->GetVal() == 1) {
                return ReplaceUsageOfExprResult(binary, binary->GetLHSOperand(), isDebug);
            }
            return;
        }
        case ExprKind::DIV:
        case ExprKind::EXP: {
            // `a / 1` => a, `a ** 1` => a
            if (rhs && rhs->GetVal() == 1) {
                return ReplaceUsageOfExprResult(binary, binary->GetLHSOperand(), isDebug);
            }
            return;
        }
        case ExprKind::BITAND:
        case ExprKind::BITOR: {
            // `a & a` => a, `a | a` => a
            if (binary->GetLHSOperand() == binary->GetRHSOperand()) {
                return ReplaceUsageOfExprResult(binary, binary->GetLHSOperand(), isDebug);
            }
            return;
        }
        default:
            return;
    }
}

void ConstPropagation::RewriteToConstExpr(const RewriteInfo& rewriteInfo, bool isDebug) const
{
    if (!rewriteInfo.literalVal) {
        return;
    }
    auto oldExpr = rewriteInfo.oldExpr;
    if (SkipCP(*oldExpr, opts)) {
        return;
    }
    auto oldExprResult = oldExpr->GetResult();
    auto oldExprParent = oldExpr->GetParentBlock();
    auto newExpr = builder.CreateExpression<Constant>(oldExprResult->GetType(), rewriteInfo.literalVal, oldExprParent);
    newExpr->SetDebugLocation(oldExpr->GetDebugLocation());
    oldExprParent->GetExpressionByIdx(rewriteInfo.index)->ReplaceWith(*newExpr);
    if (isDebug) {
        std::string message = "[ConstPropagation] The " +
            ExprKindMgr::Instance()->GetKindName(static_cast<size_t>(oldExpr->GetExprKind())) +
            ToPosInfo(oldExpr->GetDebugLocation(), true) + " has been rewrited to a constant\n";
        std::cout << message;
    }
}

void ConstPropagation::TrySimplifyingUnaryExpr(const Ptr<UnaryExpression>& unary, bool isDebug) const
{
    if (SkipCP(*unary, opts)) {
        return;
    }
    // NOT: !(!b) = b
    // BITNOT: !(!x) = x
    if (unary->GetExprKind() != ExprKind::NOT && unary->GetExprKind() != ExprKind::BITNOT) {
        return;
    }

    // e.g.
    // %1 : Bool = Not(%0)
    // %2 : Bool = Not(%1)
    // any usage to %2 can be replaced by %0
    auto operand = unary->GetOperand(); // `operand` is `%1` in the example
    if (operand->IsLocalVar()) {
        auto operandExpr = StaticCast<LocalVar*>(operand)->GetExpr(); // `operandExpr` is `Not(%0)` in the example
        if (operandExpr->GetExprKind() != ExprKind::NOT && operandExpr->GetExprKind() != ExprKind::BITNOT) {
            return;
        }
        auto targetVal = StaticCast<UnaryExpression*>(operandExpr)->GetOperand(); // `%0` in the example

        ReplaceUsageOfExprResult(unary, targetVal, isDebug);
    }
}

void ConstPropagation::ReplaceUsageOfExprResult(
    const Ptr<const Expression>& expr, const Ptr<Value>& newVal, bool isDebug) const
{
    // note: The scope of rewriting should be revised when nested block groups occur.
    expr->GetResult()->ReplaceWith(*newVal, expr->GetParentBlockGroup());

    if (isDebug) {
        std::string message = "[ConstPropagation] The result of the trivial " +
            ExprKindMgr::Instance()->GetKindName(static_cast<size_t>(expr->GetExprKind())) +
            ToPosInfo(expr->GetDebugLocation()) + " has been optimised\n";
        std::cout << message;
    }
}

void ConstPropagation::RewriteTerminator(
    Terminator* oldTerminator, LiteralValue* newValue, Block* newTarget, bool isDebug) const
{
    if (SkipCP(*oldTerminator, opts)) {
        return;
    }
    auto parentBlock = oldTerminator->GetParentBlock();
    oldTerminator->RemoveSelfFromBlock();
    const auto& loc = oldTerminator->GetDebugLocation();
    if (newValue) {
        auto constant = builder.CreateExpression<Constant>(loc, newValue->GetType(), newValue, parentBlock);
        parentBlock->AppendExpression(constant);
        oldTerminator->GetResult()->ReplaceWith(*constant->GetResult(), constant->GetParentBlockGroup());
    }
    auto newTerminator = builder.CreateTerminator<GoTo>(loc, newTarget, parentBlock);
    parentBlock->AppendExpression(newTerminator);

    if (isDebug) {
        std::string message = "[ConstPropagation] The terminator " +
            ExprKindMgr::Instance()->GetKindName(static_cast<size_t>(oldTerminator->GetExprKind())) +
            ToPosInfo(oldTerminator->GetDebugLocation()) + " has been optimised\n";
        std::cout << message;
    }
}

static std::mutex g_mtx;
OptEffectCHIRMap ConstPropagation::effectMap;
void ConstPropagation::RecordEffectMap(const Expression* expr, const Func* func) const
{
    if (!opts.enIncrementalCompilation ||
        !opts.IsOptimizationExisted(GlobalOptions::OptimizationFlag::CONST_PROPAGATION)) {
        return;
    }
    GlobalVar* gv = nullptr;
    if (expr->GetExprKind() == ExprKind::LOAD) {
        auto loc = StaticCast<Load*>(expr)->GetLocation();
        if (loc->IsGlobalVarInCurPackage()) {
            // let a = 3
            // Load(gv_a)
            gv = DynamicCast<GlobalVar*>(loc);
        } else if (loc->IsLocalVar()) {
            // let sa = SA(); sa.x
            // %0 = GetElementRef(gv_sa); %1 = Load(%0)
            auto locExpr = StaticCast<LocalVar*>(loc)->GetExpr();
            if (locExpr->GetExprKind() == ExprKind::GET_ELEMENT_REF) {
                auto base = StaticCast<GetElementRef*>(locExpr)->GetLocation();
                if (base->IsGlobalVarInCurPackage()) {
                    gv = DynamicCast<GlobalVar*>(base);
                }
            }
        }
    } else if (expr->GetExprKind() == ExprKind::FIELD) {
        auto base = StaticCast<Field*>(expr)->GetBase();
        if (base->IsLocalVar()) {
            auto baseExpr = StaticCast<LocalVar*>(base)->GetExpr();
            if (baseExpr->GetExprKind() == ExprKind::LOAD) {
                auto loc = StaticCast<Load*>(baseExpr)->GetLocation();
                if (loc->IsGlobalVarInCurPackage()) {
                    // let a = (1, 2); a[0]
                    // %0 = Load(gv_a); %1 = Field(%0, 0)
                    gv = DynamicCast<GlobalVar*>(loc);
                }
            }
        }
    }
    if (gv) {
        if (opts.GetJobs() > 1) {
            std::lock_guard<std::mutex> guard(g_mtx);
            effectMap[gv].emplace(const_cast<Func*>(func));
        } else {
            effectMap[gv].emplace(const_cast<Func*>(func));
        }
    }
}
