// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include <numeric>

#include "BuiltInOperatorUtil.h"
#include "Desugar/DesugarInTypeCheck.h"
#include "DiagSuppressor.h"
#include "Diags.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/RecoverDesugar.h"

using namespace Cangjie;
using namespace AST;
using namespace Sema;
using namespace TypeCheckUtil;

namespace {
const std::vector<TokenKind> ARITHMETIC_OPERATOR = {TokenKind::ADD, TokenKind::SUB, TokenKind::MUL, TokenKind::DIV,
    TokenKind::EXP, TokenKind::MOD, TokenKind::BITAND, TokenKind::BITOR, TokenKind::BITXOR};
const std::vector<TokenKind> RELATIONAL_OPERATOR = {
    TokenKind::EQUAL, TokenKind::NOTEQ, TokenKind::GT, TokenKind::LT, TokenKind::GE, TokenKind::LE};
const std::vector<TokenKind> LOGICAL_OPERATOR = {TokenKind::AND, TokenKind::OR};
const std::vector<TokenKind> SHIFT_OPERATOR = {TokenKind::LSHIFT, TokenKind::RSHIFT};
const std::vector<TokenKind> FLOW_OPERATOR = {TokenKind::PIPELINE, TokenKind::COMPOSITION};

Ptr<BinaryExpr> GetChildBinaryExpr(Expr& child)
{
    // If a `BinaryExpr` has `desugarExpr`, it is guaranteed to be correct and cannot be the pivot.
    if (child.astKind == ASTKind::BINARY_EXPR && child.desugarExpr == nullptr) {
        return StaticAs<ASTKind::BINARY_EXPR>(&child);
    }
    if (child.astKind == ASTKind::PAREN_EXPR) {
        ParenExpr& pe = *StaticAs<ASTKind::PAREN_EXPR>(&child);
        CJC_NULLPTR_CHECK(pe.expr);
        if (pe.expr->astKind == ASTKind::BINARY_EXPR && pe.expr->desugarExpr == nullptr) {
            return StaticAs<ASTKind::BINARY_EXPR>(pe.expr.get());
        }
    }
    return nullptr;
}

// This function is used to check whether binaryExpr contains a invalid child node which cannot be inferred.
bool HasInvalidChild(BinaryExpr& be)
{
    bool hasInvalid = false;
    auto id = Walker::GetNextWalkerID();
    std::function<VisitAction(Ptr<Node>)> containsInvalid = [id, &containsInvalid, &hasInvalid](auto node) {
        CJC_ASSERT(node);
        if (auto expr = DynamicCast<Expr*>(node); expr && expr->desugarExpr) {
            Walker(expr, id, containsInvalid).Walk();
            return VisitAction::SKIP_CHILDREN;
        }
        if (node->TestAttr(AST::Attribute::IS_BROKEN) || node->TestAttr(AST::Attribute::HAS_BROKEN)) {
            hasInvalid = true;
        } else if (auto expr = DynamicCast<Expr*>(node);
            expr && expr->IsReferenceExpr() && !Ty::IsInitialTy(expr->DataTy())) {
            // If there exists node 'a.b' or 'a' which has been synthesized and type of expression is invalid,
            // the binaryExpr will definitely failed the typeCheck, so the overload checking can be skipped.
            hasInvalid = !expr->GetTarget() && !expr->GetTy().IsCorrect();
            return hasInvalid ? VisitAction::STOP_NOW : VisitAction::SKIP_CHILDREN;
        }
        return hasInvalid ? VisitAction::STOP_NOW : VisitAction::WALK_CHILDREN;
    };
    Walker(&be, id, containsInvalid).Walk();
    return hasInvalid;
}

bool RefExprTargetIsFuncDeclAndHasNamedParam(const RefExpr& re)
{
    if (!re.ref.target || re.ref.target->astKind != ASTKind::FUNC_DECL) {
        return false;
    }
    const FuncDecl& funcDecl = *StaticCast<FuncDecl*>(re.ref.target);
    CJC_NULLPTR_CHECK(funcDecl.funcBody);
    auto& params = funcDecl.funcBody->paramLists[0]->params;
    return std::any_of(
        params.cbegin(), params.cend(), [](const OwnedPtr<FuncParam>& param) { return param && param->isNamedParam; });
}

void MarkOutermostBinaryExpr(bool setOutermost, Node& node)
{
    Walker(&node, [&setOutermost](auto n) {
        if (n->astKind == ASTKind::BINARY_EXPR) {
            if (setOutermost) {
                n->EnableAttr(Attribute::IS_OUTERMOST);
            }
            auto& be = StaticCast<BinaryExpr&>(*n);
            if (be.leftExpr) {
                MarkOutermostBinaryExpr(false, *be.leftExpr);
            }
            if (be.rightExpr) {
                MarkOutermostBinaryExpr(false, *be.rightExpr);
            }
            setOutermost = true;
            return VisitAction::SKIP_CHILDREN;
        } else if (n->astKind == ASTKind::PAREN_EXPR) {
            // Parentheses are considered as part of a binary expression.
            auto& pe = StaticCast<ParenExpr&>(*n);
            if (pe.expr) {
                MarkOutermostBinaryExpr(setOutermost, *pe.expr);
            }
            return VisitAction::SKIP_CHILDREN;
        } else {
            // For binary expressions nested in other expressions, i.e., `1 + if (a < b) { 0 } else { 1 }`,
            // `a < b` is also outermost.
            setOutermost = true;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

inline bool HasSideEffect(const Expr& expr)
{
    return Utils::NotIn(expr.astKind, {ASTKind::TUPLE_LIT, ASTKind::REF_EXPR});
}

// Get the element accesses of a tuple.
// For tuple literal, copy the elements.
// For other tuple type expression, create subscript expressions to access the elements.
std::vector<OwnedPtr<Expr>> GetTupleElements(Expr& expr)
{
    CJC_ASSERT(expr.GetTy().IsCorrect() && expr.GetTy()->IsTuple());
    auto int64Ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64);
    std::vector<OwnedPtr<Expr>> tupleElements;
    if (expr.astKind == ASTKind::TUPLE_LIT) {
        // Get children of a tuple literal.
        for (OwnedPtr<Expr>& child : static_cast<TupleLit&>(expr).children) {
            tupleElements.push_back(ASTCloner::Clone(child.get()));
        }
    } else {
        const bool sideEffect = HasSideEffect(expr);
        Ptr<Expr> mapExpr = nullptr;
        // Create TupleAccess node to get children.
        for (size_t i = 0; i < expr.GetTy()->typeArgs.size(); i++) {
            auto tupleAccessExpr = MakeOwnedNode<SubscriptExpr>();
            tupleAccessExpr->isTupleAccess = true;
            auto clonedExpr = ASTCloner::Clone(Ptr(&expr));
            if (sideEffect) {
                if (!mapExpr) {
                    mapExpr = clonedExpr.get();
                }
                clonedExpr->mapExpr = mapExpr;
            }
            tupleAccessExpr->baseExpr = std::move(clonedExpr);
            tupleAccessExpr->indexExprs.push_back(
                CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(i), ModalTy{int64Ty}));
            tupleAccessExpr->indexExprs[0]->SetTy({int64Ty});
            tupleAccessExpr->SetTy(expr.GetTy()->typeArgs[i]);
            tupleAccessExpr->curFile = expr.curFile;
            tupleElements.push_back(std::move(tupleAccessExpr));
        }
    }
    return tupleElements;
}

std::pair<std::string, std::string> GetDesugaredBinaryExprArgTys(const BinaryExpr& be)
{
    CJC_NULLPTR_CHECK(be.desugarExpr);
    CJC_ASSERT(be.desugarExpr->astKind == ASTKind::CALL_EXPR);
    auto callexpr = RawStaticCast<CallExpr*>(be.desugarExpr.get());
    CJC_ASSERT(callexpr->baseFunc->astKind == ASTKind::MEMBER_ACCESS);
    auto ceBaseFunc = RawStaticCast<MemberAccess*>(callexpr->baseFunc.get());
    CJC_NULLPTR_CHECK(ceBaseFunc->baseExpr);
    CJC_ASSERT(ceBaseFunc->baseExpr->GetTy().IsCorrect());
    CJC_ASSERT(callexpr->args.size() == 1);
    CJC_NULLPTR_CHECK(callexpr->args[0]->expr);
    CJC_ASSERT(callexpr->args[0]->expr->GetTy().IsCorrect());
    return std::make_pair(ceBaseFunc->baseExpr->GetTy().String(), callexpr->args[0]->expr->GetTy().String());
}
} // namespace

// Helper function to check return type inference errors in operator functions
// Returns the operator function declaration if it has return type inference error, nullptr otherwise
bool TypeChecker::TypeCheckerImpl::ChkOperatorFuncIfTyCannotBeInferred(ASTContext& ctx, BinaryExpr& be)
{
    auto callExpr = StaticCast<CallExpr*>(be.desugarExpr.get());
    auto operatorFuncDecl = As<ASTKind::FUNC_DECL>(callExpr->resolvedFunction ? callExpr->resolvedFunction
                                                                              : callExpr->baseFunc->GetTarget());
    if (operatorFuncDecl && NeedSynOnUsed(*operatorFuncDecl)) {
        auto targetTy = Synthesize({ctx, SynPos::EXPR_ARG}, operatorFuncDecl);
        if (targetTy->HasQuestTy()) {
            // Mark nodes as broken but don't report diagnostics here
            be.SetTy({TypeManager::GetInvalidTy()});
            be.EnableAttr(Attribute::IS_BROKEN);
            operatorFuncDecl->EnableAttr(Attribute::IS_BROKEN);
            DiagUnableToInferReturnType(diag, *operatorFuncDecl, be);
            return true;
        }
    }
    return false;
}

// Helper function to handle operator overload checking and return type inference errors
// Returns true if operator overload succeeded, false otherwise
bool TypeChecker::TypeCheckerImpl::TryCheckOperatorOverload(ASTContext& ctx, ModalTy target, BinaryExpr& be)
{
    DesugarOperatorOverloadExpr(ctx, be);
    {
        auto ds = DiagSuppressor(diag);
        if (Check(ctx, target, be.desugarExpr.get())) {
            ds.ReportDiag();
            CJC_NULLPTR_CHECK(be.desugarExpr);
            be.SetTy(be.desugarExpr->GetTy());
            CJC_ASSERT(be.desugarExpr->astKind == ASTKind::CALL_EXPR);
            ReplaceTarget(&be, StaticCast<CallExpr*>(be.desugarExpr.get())->resolvedFunction);
            return true;
        }
    }
    // Check for return type inference errors but always return false since operator overload failed
    ChkOperatorFuncIfTyCannotBeInferred(ctx, be);
    RecoverToBinaryExpr(be);
    PData::Reset(typeManager.constraints);
    return false;
}

// Helper function to handle operator overload synthesis and return type inference errors
// Returns true if operator overload succeeded, false otherwise
bool TypeChecker::TypeCheckerImpl::TrySynthesizeOperatorOverload(ASTContext& ctx, BinaryExpr& be)
{
    DesugarOperatorOverloadExpr(ctx, be);
    {
        auto ds = DiagSuppressor(diag);
        if (Synthesize({ctx, SynPos::EXPR_ARG}, be.desugarExpr.get()).IsCorrect()) {
            ds.ReportDiag();
            be.SetTy(be.desugarExpr->GetTy());
            ReplaceTarget(&be, StaticCast<CallExpr*>(be.desugarExpr.get())->resolvedFunction);
            return true;
        }
    }
    ChkOperatorFuncIfTyCannotBeInferred(ctx, be);
    RecoverToBinaryExpr(be);
    be.SetTy({TypeManager::GetInvalidTy()});
    PData::Reset(typeManager.constraints);
    return false;
}

void TypeChecker::TypeCheckerImpl::MarkOutermostBinaryExpressions(Package& pkg) const
{
    MarkOutermostBinaryExpr(true, pkg);
}

bool TypeChecker::TypeCheckerImpl::ChkBinaryExpr(ASTContext& ctx, ModalTy target, BinaryExpr& be)
{
    auto cs = PData::CommitScope(typeManager.constraints);
    if (be.desugarExpr) {
        return typeManager.IsSubtype(be.desugarExpr->GetTy(), target);
    }
    bool invalid = !be.leftExpr || !be.rightExpr;
    if (invalid) {
        be.SetTy({TypeManager::GetNonNullTy(be.GetTy())});
        return false;
    }
    // 1. Check built-in operator expr.
    { // Create a scope for DiagSuppressor.
        auto ds = DiagSuppressor(diag);
        if (auto optRes = CheckBinaryExprCaseBuiltIn(ctx, be, target)) {
            ds.ReportDiag();
            return *optRes;
        }
        PData::Reset(typeManager.constraints);
    } // Do not throw stored errors, re-generated below.

    // 2. Try operator overload if applicable
    if (TypeCheckUtil::IsOverloadableOperator(be.op)) {
        if (TryCheckOperatorOverload(ctx, target, be)) {
            return true;
        }
    }
    // 3. Handle failure case
    // Clear the node to `Synthesize` the literals and the corresponding expressions again.
    be.Clear();
    if (SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, &be).IsCorrect()) {
        DiagMismatchedTypes(diag, be, target);
        RecoverToBinaryExpr(be);
    }
    if (be.TestAttr(Attribute::IS_OUTERMOST)) {
        SynBinaryLeafs(ctx, be);
    }
    be.SetTy({TypeManager::GetInvalidTy()});
    return false;
}

bool TypeChecker::TypeCheckerImpl::ChkArithmeticExpr(ASTContext& ctx, ModalTy target, BinaryExpr& be)
{
    if (!IsBinaryOperator(be.op)) {
        return false;
    }
    // Update the targetTy so that the binary expression is of a primitive type, which can be boxed properly
    // to an interface type later. Forced by the current box algorithm.
    ModalTy optionUnboxTy = TypeCheckUtil::UnboxOptionType(target);
    if (!Ty::IsTyCorrect(optionUnboxTy)) {
        return false;
    }
    // Exponentiation expressions have special rules and thus be handled differently.
    if (be.op == TokenKind::EXP) {
        return Ty::IsTyCorrect(target) && ChkExpoExpr(ctx, target, be);
    }
    std::map<AST::TypeKind, AST::TypeKind> typeCandidates = GetBinaryOpTypeCandidates(be.op);
    Utils::EraseIf(typeCandidates, [optionUnboxTy, this](auto& e) {
        auto primitiveTy = TypeManager::GetPrimitiveTy(e.second);
        return primitiveTy->IsIdeal() || !typeManager.IsSubtype(ModalTy{primitiveTy}, optionUnboxTy);
    });
    std::vector<Diagnostic> thisDiags;
    for (auto& type : std::as_const(typeCandidates)) {
        auto ds = DiagSuppressor(diag);
        auto tgtTy = TypeManager::GetPrimitiveTy(type.second);
        bool isWellTyped = CheckWithNegCache(ctx, ModalTy{tgtTy}, be.leftExpr.get());
        isWellTyped = CheckWithNegCache(ctx, ModalTy{tgtTy}, be.rightExpr.get()) && isWellTyped;
        if (isWellTyped) {
            ds.ReportDiag();
            be.SetTy({tgtTy});
            return true;
        } else {
            auto tmpDiag = ds.GetSuppressedDiag();
            (void)thisDiags.insert(thisDiags.end(), tmpDiag.begin(), tmpDiag.end());
        }
    }
    (void)std::for_each(thisDiags.begin(), thisDiags.end(), [this](auto& d) { (void)diag.Diagnose(d); });
    be.SetTy({TypeManager::GetInvalidTy()});
    return false;
}

bool TypeChecker::TypeCheckerImpl::ChkExpoExpr(ASTContext& ctx, ModalTy tgtTy, BinaryExpr& be)
{
    ModalTy i64{TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64)};
    ModalTy f64{TypeManager::GetPrimitiveTy(TypeKind::TYPE_FLOAT64)};
    ModalTy u64{TypeManager::GetPrimitiveTy(TypeKind::TYPE_UINT64)};
    // Using the IsSubtype function to fit the ImplicitConvert interface in the future.
    bool isBaseWellTyped = false;
    bool isExponentWellTyped = false;
    bool i64Compatible = typeManager.IsSubtype(i64, tgtTy);
    bool f64Compatible = typeManager.IsSubtype(f64, tgtTy);
    bool maybef64 = f64Compatible;
    auto cs = PData::CommitScope(typeManager.constraints);
    if (i64Compatible) {
        auto ds = DiagSuppressor(diag);
        isBaseWellTyped = ChkExpoExprBase(ctx, be, i64);
        std::vector<ModalTy> exponentTys = {u64};
        isExponentWellTyped = ChkExpoExprExponent(ctx, *be.rightExpr, exponentTys);
        maybef64 = maybef64 && !(isBaseWellTyped && isExponentWellTyped);
        if (!maybef64) {
            ds.ReportDiag();
        } else {
            PData::Reset(typeManager.constraints);
        }
    }
    if (maybef64) {
        isBaseWellTyped = ChkExpoExprBase(ctx, be, f64);
        std::vector<ModalTy> exponentTys = {i64, f64};
        isExponentWellTyped = ChkExpoExprExponent(ctx, *be.rightExpr, exponentTys);
    }
    if (!i64Compatible && !f64Compatible) {
        (void)diag.Diagnose(be, DiagKind::sema_incompatible_expo_target_type, tgtTy.String());
    }

    if (isBaseWellTyped && isExponentWellTyped) {
        be.SetTy(be.leftExpr->GetTy());
        return true;
    } else {
        be.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
}

bool TypeChecker::TypeCheckerImpl::ChkExpoExprBase(ASTContext& ctx, const BinaryExpr& be, ModalTy baseTy)
{
    const std::string noteMsg = "the context requires this exponentiation expression to be of type '" +
        baseTy.String() + "'; the type of the left operand must also be '" + baseTy.String() + "' in this case";
    if (!CheckWithNegCache(ctx, baseTy, be.leftExpr.get())) {
        if (!CanSkipDiag(*be.leftExpr)) {
            DiagMismatchedTypes(diag, *be.leftExpr, baseTy, noteMsg);
        }
        return false;
    } else {
        return true;
    }
}

bool TypeChecker::TypeCheckerImpl::ChkExpoExprExponent(ASTContext& ctx, Expr& exponent, std::vector<ModalTy> exTys)
{
    bool isWellTyped = true;
    std::vector<short> pass;
    auto cs = PData::CommitScope(typeManager.constraints);
    (void)std::for_each(exTys.cbegin(), exTys.cend(), [this, &ctx, &exponent, &pass](ModalTy ty) {
        auto ds = DiagSuppressor(diag);
        // Use check mode to let more cases such as a ** f() where f is overloaded pass type checking.
        (void)pass.emplace_back(CheckWithNegCache(ctx, ty, Ptr<Expr>(&exponent)) ? 1 : 0);
    });
    PData::Reset(typeManager.constraints);

    auto answers = std::accumulate(pass.begin(), pass.end(), decltype(pass)::value_type(0));
    if (answers > 1) {
        (void)diag.Diagnose(exponent, DiagKind::sema_ambiguous_expo_right_operand_type);
        isWellTyped = false;
    } else if (answers == 1) {
        // Get the correct answer's position
        size_t i = 0;
        while (pass.at(i) != true) {
            i++;
        }
        auto ty = exTys.at(i);
        // Recheck to set correct semantic types
        isWellTyped = CheckWithNegCache(ctx, ty, Ptr<Expr>(&exponent));
    } else {
        // The nested if reflects the checking logic better.
        isWellTyped = false;
        if (CanSkipDiag(exponent)) {
            SynthesizeWithNegCache(
                {ctx, SynPos::EXPR_ARG}, &exponent); // Synthesize 'exponent' to report errors.
        } else {
            std::string tgtTyStr;
            if (exTys.size() == 1) {
                tgtTyStr = exTys.front().String();
            } else {
                (void)std::for_each(exTys.begin(), std::prev(exTys.end()),
                    [&tgtTyStr](const auto ty) { tgtTyStr += ty.String() + "' or '"; });
                CJC_ASSERT(!exTys.empty() && exTys.back());
                tgtTyStr += exTys.back()->String();
            }
            DiagMismatchedTypesWithFoundTy(diag, exponent, tgtTyStr, exponent.GetTy().String(),
                "the type of the right operand must be " + tgtTyStr + " in this case");
        }
    }
    return isWellTyped;
}

ModalTy TypeChecker::TypeCheckerImpl::SynExpoExpr(ASTContext& ctx, BinaryExpr& be)
{
    SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, be.leftExpr.get());
    ReplaceIdealTy(*be.leftExpr);

    auto& exponent = *be.rightExpr;
    ModalTy baseTy = be.leftExpr->GetTy();
    CJC_NULLPTR_CHECK(baseTy);
    bool isExponentWellTyped = CheckExponentByBaseTy(ctx, baseTy, *be.leftExpr, exponent);

    be.SetTy(be.leftExpr->GetTy().IsCorrect() && isExponentWellTyped ? be.leftExpr->GetTy()
                                                                     : ModalTy{TypeManager::GetInvalidTy()});
    return be.GetTy();
}

bool TypeChecker::TypeCheckerImpl::CheckExponentByBaseTy(
    ASTContext& ctx, ModalTy baseTy, const Expr& base, Expr& exponent)
{
    ModalTy i64{TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64)};
    ModalTy u64{TypeManager::GetPrimitiveTy(TypeKind::TYPE_UINT64)};
    ModalTy f64{TypeManager::GetPrimitiveTy(TypeKind::TYPE_FLOAT64)};
    // The left operand of ** can only be of type Int64 or Float64.
    bool isExponentWellTyped = true;
    if (baseTy->IsPlaceholder()) {
        std::set<std::pair<ModalTy, ModalTy>> validCombos = {{i64, u64}, {f64, i64}, {f64, f64}};
        SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, &exponent);
        ReplaceIdealTy(exponent);
        if (!exponent.GetTy().IsCorrect()) {
            isExponentWellTyped = false;
        } else {
            switch (PickConstaintFromTys(baseTy, exponent.GetTy(), validCombos, true)) {
                case MatchResult::UNIQUE:
                    break;
                case MatchResult::AMBIGUOUS:
                case MatchResult::NONE:
                    isExponentWellTyped = false;
                    break;
                default:
                    CJC_ABORT();
                    break;
            }
        }
    }
    if (isExponentWellTyped && typeManager.IsSubtype(baseTy, i64)) {
        std::vector<ModalTy> exponentTys = {u64};
        isExponentWellTyped = ChkExpoExprExponent(ctx, exponent, exponentTys);
    } else if (isExponentWellTyped && typeManager.IsSubtype(baseTy, f64)) {
        std::vector<ModalTy> exponentTys = {i64, f64};
        isExponentWellTyped = ChkExpoExprExponent(ctx, exponent, exponentTys);
    } else {
        isExponentWellTyped = false;
        if (Ty::IsTyCorrect(baseTy)) {
            DiagMismatchedTypesWithFoundTy(diag, base, "Int64' or 'Float64", baseTy.String(),
                "the type of the left operand of an exponentiation expression must be either 'Int64' or 'Float64'");
        }
        SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, &exponent);
    }
    return isExponentWellTyped;
}

bool TypeChecker::TypeCheckerImpl::ChkRelationalExpr(ASTContext& ctx, ModalTy target, BinaryExpr& be)
{
    // Check target type is bool type.
    if (!target->IsBoolean()) {
        // Option type allow auto box.
        if (!typeManager.IsLitBoxableType(TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN), target.Ty())) {
            return false;
        }
    }
    auto ty = SynLiteralInBinaryExpr(ctx, be);
    if (!Ty::IsTyCorrect(ty)) {
        return false;
    }
    // Check expression is numeric type.
    if (!IsBinaryOperator(be.op)) {
        return false;
    }
    const auto& typeCandidates = GetBinaryOpTypeCandidates(be.op);
    if (ty->IsNothing() || Utils::InKeys(ty->kind, typeCandidates)) {
        be.SetTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)});
        return true;
    }
    be.SetTy({TypeManager::GetInvalidTy()});
    return false;
}

bool TypeChecker::TypeCheckerImpl::ChkLogicalExpr(ASTContext& ctx, ModalTy target, BinaryExpr& be)
{
    ModalTy boolTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)};
    // Check T is bool type.
    if (!target->IsBoolean()) {
        // Option type allow auto box.
        if (!typeManager.IsLitBoxableType(boolTy.Ty(), target.Ty())) {
            DiagMismatchedTypesWithFoundTy(diag, be, target, boolTy);
            be.SetTy({TypeManager::GetInvalidTy()});
            return false;
        }
    }
    auto ret = true;
    if (!CheckWithNegCache(ctx, boolTy, be.leftExpr.get())) {
        ret = false;
    }
    if (!CheckWithNegCache(ctx, boolTy, be.rightExpr.get())) {
        ret = false;
    }
    if (ret) {
        be.SetTy(boolTy);
        return true;
    }
    be.SetTy({TypeManager::GetInvalidTy()});
    return false;
}

bool TypeChecker::TypeCheckerImpl::CheckTupleCanEqual(ASTContext& ctx, BinaryExpr& be)
{
    CJC_ASSERT(be.leftExpr != nullptr && be.leftExpr->GetTy().IsCorrect() && be.leftExpr->GetTy()->IsTuple());
    CJC_ASSERT(be.rightExpr != nullptr && be.rightExpr->GetTy().IsCorrect() && be.rightExpr->GetTy()->IsTuple());
    TupleTy& leftTupleTy = *RawStaticCast<TupleTy*>(be.leftExpr->DataTy());
    TupleTy& rightTupleTy = *RawStaticCast<TupleTy*>(be.rightExpr->DataTy());
    if (leftTupleTy.typeArgs.size() != rightTupleTy.typeArgs.size()) {
        (void)diag.Diagnose(be, DiagKind::sema_tuple_cmp_not_supported, TOKENS[static_cast<int>(be.op)],
            leftTupleTy.String(), rightTupleTy.String());
        be.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
    TokenKind connectionOp = be.op == TokenKind::EQUAL ? TokenKind::AND : TokenKind::OR;
    auto boolTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    OwnedPtr<Expr> tupleCmpExpr =
        CreateLitConstExpr(LitConstKind::BOOL, be.op == TokenKind::EQUAL ? "true" : "false", ModalTy{boolTy});
    std::vector<OwnedPtr<Expr>> leftElements = GetTupleElements(*be.leftExpr);
    std::vector<OwnedPtr<Expr>> rightElements = GetTupleElements(*be.rightExpr);
    /* Try to convert tupleA == (or !=) tupleB into
     * true && tupleA[0] == tupleB[0] && tupleA[1] == tupleB[1] && ... tupleA[n] == tupleB[n] (if op is ==)
     * false || tupleA[0] != tupleB[0] || tupleA[1] != tupleB[1] || ... tupleA[n] != tupleB[n] (if op is !=)
     * and store that expression in be.desugarExpr.
     */
    for (size_t i = 0; i < leftElements.size(); i++) {
        CJC_ASSERT(i < rightElements.size());
        auto elementCmpExpr = CreateBinaryExpr(std::move(leftElements[i]), std::move(rightElements[i]), be.op);
        elementCmpExpr->EnableAttr(Attribute::IMPLICIT_ADD);
        CopyBasicInfo(&be, elementCmpExpr.get());
        ModalTy returnTy{};
        {
            auto ds = DiagSuppressor(diag);
            returnTy = SynBinaryExpr(ctx, *elementCmpExpr);
        }
        if (!Ty::IsTyCorrect(returnTy)) { // These two elements do not support == (or !=).
            (void)diag.Diagnose(be, DiagKind::sema_tuple_cmp_not_supported, TOKENS[static_cast<int>(be.op)],
                leftTupleTy.String(), rightTupleTy.String());
            be.SetTy({TypeManager::GetInvalidTy()});
            return false;
        }
        if (!returnTy->IsBoolean()) {
            auto argTys = GetDesugaredBinaryExprArgTys(*elementCmpExpr);
            (void)diag.Diagnose(be, DiagKind::sema_tuple_element_cmp_not_bool, TOKENS[static_cast<int>(be.op)],
                argTys.first, argTys.second);
            be.SetTy({TypeManager::GetInvalidTy()});
            return false;
        }
        auto connectionExpr = CreateBinaryExpr(std::move(tupleCmpExpr), std::move(elementCmpExpr), connectionOp);
        tupleCmpExpr = std::move(connectionExpr);
    }
    if (HasSideEffect(*be.leftExpr) || HasSideEffect(*be.rightExpr)) {
        tupleCmpExpr->EnableAttr(Attribute::SIDE_EFFECT);
    }
    be.SetTy({boolTy}); // The built-in == (or !=) is always evaluated to a bool.
    be.desugarExpr = std::move(tupleCmpExpr);
    return true;
}

std::optional<bool> TypeChecker::TypeCheckerImpl::CheckBinaryExprCaseBuiltIn(
    ASTContext& ctx, BinaryExpr& be, ModalTy target)
{
    CJC_ASSERT(target);
    bool checkRet = false;
    if (Utils::In(be.op, ARITHMETIC_OPERATOR)) {
        checkRet = ChkArithmeticExpr(ctx, target, be);
    } else if (Utils::In(be.op, RELATIONAL_OPERATOR)) {
        checkRet = ChkRelationalExpr(ctx, target, be);
        if (checkRet) {
            return {true};
        }
        // Tuple equality is handled specially.
        bool cannotBeTuple = (!be.leftExpr->GetTy().IsCorrect() && !Ty::IsInitialTy(be.leftExpr->DataTy())) ||
            (!be.rightExpr->GetTy().IsCorrect() && !Ty::IsInitialTy(be.rightExpr->DataTy())) ||
            (be.op != TokenKind::EQUAL && be.op != TokenKind::NOTEQ);
        if (cannotBeTuple) {
            return {};
        }
        bool bothAreTuple = SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, be.leftExpr.get()).IsCorrect() &&
            be.leftExpr->GetTy()->IsTuple() &&
            SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, be.rightExpr.get()).IsCorrect() &&
            be.rightExpr->GetTy()->IsTuple();
        if (bothAreTuple) {
            if (!CheckTupleCanEqual(ctx, be)) {
                return {false};
            }
            if (!typeManager.IsSubtype(be.GetTy(), target)) {
                DiagMismatchedTypes(diag, be, target);
                return {false};
            }
            return {true};
        }
    } else if (Utils::In(be.op, LOGICAL_OPERATOR)) {
        checkRet = ChkLogicalExpr(ctx, target, be);
    } else if (Utils::In(be.op, SHIFT_OPERATOR)) {
        checkRet = ChkShiftExpr(ctx, target, be);
    } else if (Utils::In(be.op, FLOW_OPERATOR)) {
        if (ChkFlowExpr(ctx, target, be)) {
            return {true};
        }
    } else if (be.op == TokenKind::COALESCING) {
        return {ChkCoalescingExpr(ctx, target, be)};
    } else {
        checkRet = ChkArithmeticExpr(ctx, target, be);
    }

    if (checkRet) {
        return {true};
    }
    return {};
}

ModalTy TypeChecker::TypeCheckerImpl::SynLiteralInBinaryExpr(ASTContext& ctx, BinaryExpr& be)
{
    CJC_NULLPTR_CHECK(be.leftExpr);
    CJC_NULLPTR_CHECK(be.rightExpr);

    if (auto optTy = SynLiteralInBinaryExprFromRight(ctx, be)) {
        return *optTy;
    }
    be.Clear();
    return SynLiteralInBinaryExprFromLeft(ctx, be);
}

std::optional<ModalTy> TypeChecker::TypeCheckerImpl::SynLiteralInBinaryExprFromRight(ASTContext& ctx, BinaryExpr& be)
{
    CJC_NULLPTR_CHECK(be.rightExpr);
    CJC_NULLPTR_CHECK(be.leftExpr);
    {
        auto ds = DiagSuppressor(diag);
        bool isWellTyped = SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, be.rightExpr.get()) ? true : false;
        // Keep ideal literal types pending for a bare literal operand while synthesizing the body
        // of a generic-call lambda argument, so `0 + 0` can later unify against the expected
        // contextual type (e.g. `TypeTest({=> 0 + 0})` expecting `() -> Int32`). A literal whose
        // type is already concrete (e.g. `count():Int64 + 1`) is replaced as usual.
        if (!(ctx.inFuncArgLambdaBody > 0 && be.rightExpr->GetTy()->IsIdeal())) {
            isWellTyped = isWellTyped && ReplaceIdealTy(*be.rightExpr);
        }
        if (!isWellTyped) {
            be.SetTy({TypeManager::GetInvalidTy()});
            return {};
        }
    }
    auto targetRight = be.rightExpr->GetTy();
    // In this case, type variables' upper bounds must be primitive types.
    targetRight = ModalTy{Ty::GetPrimitiveUpperBound(targetRight.Ty())};

    bool isWellTyped = Ty::IsTyCorrect(targetRight) &&
        (targetRight->IsNothing() || targetRight->IsPrimitiveSubType() || targetRight->IsTuple() ||
            targetRight->IsPlaceholder());
    if (!isWellTyped) {
        // Bind target when error happens for LSP.
        SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, be.leftExpr.get());
        be.SetTy({TypeManager::GetInvalidTy()});
        return ModalTy{TypeManager::GetInvalidTy()};
    }

    auto ds = DiagSuppressor(diag);
    be.leftExpr->Clear();
    isWellTyped = CheckWithNegCache(ctx, targetRight, be.leftExpr.get()) && isWellTyped;
    if (isWellTyped) {
        ds.ReportDiag();
        targetRight = typeManager.TryGreedySubst(targetRight);
        be.rightExpr->SetTy(targetRight);
        be.SetTy(targetRight);
        return {targetRight};
    } else {
        be.SetTy({TypeManager::GetInvalidTy()});
        return {};
    }
}

ModalTy TypeChecker::TypeCheckerImpl::SynLiteralInBinaryExprFromLeft(ASTContext& ctx, BinaryExpr& be)
{
    bool isWellTyped =
        SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, be.leftExpr.get()) && ReplaceIdealTy(*be.leftExpr);
    auto targetLeft = be.leftExpr->GetTy();
    isWellTyped = isWellTyped &&
        (targetLeft->IsNothing() || targetLeft->IsPrimitiveSubType() || targetLeft->IsTuple() ||
            targetLeft->IsPlaceholder());
    if (!isWellTyped) {
        // Bind target when error happens for LSP.
        SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, be.rightExpr.get());
        be.SetTy({TypeManager::GetInvalidTy()});
        return ModalTy{TypeManager::GetInvalidTy()};
    }

    auto ds = DiagSuppressor(diag);
    isWellTyped = CheckWithNegCache(ctx, targetLeft, be.rightExpr.get()) && isWellTyped;
    if (isWellTyped) {
        ds.ReportDiag();
        be.SetTy(targetLeft);
        return targetLeft;
    } else {
        be.Clear();
        return ModalTy{TypeManager::GetInvalidTy()};
    }
}

std::optional<ModalTy> TypeChecker::TypeCheckerImpl::SynArithmeticOrRelationalExpr(
    ASTContext& ctx, BinaryExpr& be, bool isArithmetic)
{
    // Exponentiation expressions have special rules and thus be handled differently.
    if (isArithmetic && be.op == TokenKind::EXP) {
        return SynExpoExpr(ctx, be);
    }
    auto ty = SynLiteralInBinaryExpr(ctx, be);
    if (!Ty::IsTyCorrect(ty)) {
        return ty;
    }
    // Check expression is numeric type.
    if (!IsBinaryOperator(be.op)) {
        return ModalTy{TypeManager::GetInvalidTy()};
    }
    const auto& typeCandidates = GetBinaryOpTypeCandidates(be.op);
    ty = ModalTy{Ty::GetPrimitiveUpperBound(ty.Ty())}; // If the ty is generic, it must be subtype of primitive.
    // placeholder without any info
    if (auto tv = DynamicCast<TyVar*>(ty.get()); tv && tv->isPlaceholder) {
        std::set<ModalTy> modalCandidates;
        for (auto dt : TypeMapToTys(typeCandidates, true)) {
            modalCandidates.insert(ModalTy{dt});
        }
        switch (PickConstaintFromTys(*tv, modalCandidates, true)) {
            case MatchResult::UNIQUE:
                be.SetTy(typeManager.TryGreedySubst(ModalTy{static_cast<DataTy>(tv)}));
                return {be.GetTy()};
            case MatchResult::AMBIGUOUS:
                return {};
            case MatchResult::NONE:
                break;
            default:
                CJC_ABORT();
                break;
        }
    }
    if (!ty->IsNothing() && !Utils::InKeys(ty.Kind(), typeCandidates)) {
        return ModalTy{TypeManager::GetInvalidTy()};
    }
    be.SetTy(isArithmetic ? ty : ModalTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)});
    return {be.GetTy()};
}

ModalTy TypeChecker::TypeCheckerImpl::SynLogicalExpr(ASTContext& ctx, BinaryExpr& be)
{
    CJC_NULLPTR_CHECK(be.leftExpr);
    CJC_NULLPTR_CHECK(be.rightExpr);
    auto leftTy = SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, be.leftExpr.get());
    auto rightTy = SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, be.rightExpr.get());
    if (!Ty::IsTyCorrect(leftTy) || !Ty::IsTyCorrect(rightTy)) {
        return ModalTy{TypeManager::GetInvalidTy()};
    }
    ModalTy boolMTy{TypeManager::GetPrimitiveTy(AST::TypeKind::TYPE_BOOLEAN)};
    if (typeManager.IsSubtype(leftTy, boolMTy) && typeManager.IsSubtype(rightTy, boolMTy)) {
        be.SetTy(boolMTy);
        return be.GetTy();
    } else {
        return ModalTy{TypeManager::GetInvalidTy()};
    }
}

bool TypeChecker::TypeCheckerImpl::ChkShiftExpr(ASTContext& ctx, ModalTy target, BinaryExpr& be)
{
    be.SetTy({TypeManager::GetInvalidTy()});
    if (!be.leftExpr || !be.rightExpr) {
        return false;
    }

    auto isWellTyped = CheckWithNegCache(ctx, target, be.leftExpr.get());
    auto leftTy = be.leftExpr->GetTy();
    SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, be.rightExpr.get());
    ReplaceIdealTy(*be.rightExpr);
    auto rightTy = be.rightExpr->GetTy();

    isWellTyped = Ty::IsTyCorrect(rightTy) && isWellTyped;
    isWellTyped = IsBinaryOperator(be.op) && isWellTyped;
    if (!isWellTyped) {
        return false;
    }
    const auto& typeCandidates = GetBinaryOpTypeCandidates(be.op);
    isWellTyped = (leftTy->IsNothing() || Utils::InKeys(leftTy.Kind(), typeCandidates)) && isWellTyped;
    isWellTyped = (rightTy->IsNothing() || Utils::InKeys(rightTy.Kind(), typeCandidates)) && isWellTyped;
    if (isWellTyped) {
        be.SetTy(be.leftExpr->GetTy());
    }

    return isWellTyped;
}

ModalTy TypeChecker::TypeCheckerImpl::SynShiftExpr(ASTContext& ctx, BinaryExpr& be)
{
    CJC_NULLPTR_CHECK(be.leftExpr);
    CJC_NULLPTR_CHECK(be.rightExpr);
    auto leftTy = SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, be.leftExpr.get());
    auto rightTy = SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, be.rightExpr.get());
    if (!Ty::IsTyCorrect(leftTy) || !Ty::IsTyCorrect(rightTy)) {
        return ModalTy{TypeManager::GetInvalidTy()};
    }
    // Check expression is numeric type.
    if (!IsBinaryOperator(be.op)) {
        return ModalTy{TypeManager::GetInvalidTy()};
    }
    const auto& typeCandidates = GetBinaryOpTypeCandidates(be.op);
    leftTy = ModalTy{Ty::GetPrimitiveUpperBound(leftTy.Ty())}; // If the ty is generic, it must be subtype of primitive.
    rightTy =
        ModalTy{Ty::GetPrimitiveUpperBound(rightTy.Ty())}; // If the ty is generic, it must be subtype of primitive.
    if (!leftTy->IsNothing() && !Utils::InKeys(leftTy.Kind(), typeCandidates)) {
        return ModalTy{TypeManager::GetInvalidTy()};
    }
    if (!rightTy->IsNothing() && !Utils::InKeys(rightTy.Kind(), typeCandidates)) {
        return ModalTy{TypeManager::GetInvalidTy()};
    }
    ReplaceIdealTy(*be.leftExpr);
    ReplaceIdealTy(*be.rightExpr);
    be.SetTy(leftTy);
    ReplaceIdealTy(be);
    return be.GetTy();
}

void TypeChecker::TypeCheckerImpl::DiagnoseForBinaryExpr(ASTContext& ctx, BinaryExpr& be)
{
    // `pivot` is responsible for the invalid type.
    std::vector<Ptr<BinaryExpr>> pivotStack = {&be};
    while (true) {
        Ptr<BinaryExpr> pivot = pivotStack.back();
        CJC_NULLPTR_CHECK(pivot);
        if (pivot->GetTy().IsCorrect() || !pivot->ShouldDiagnose(true)) {
            return;
        }
        if (Ty::IsInitialTy(pivot->leftExpr->DataTy())) {
            auto ds = DiagSuppressor(diag);
            SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, pivot->leftExpr.get()); // in case the node was cleared and then skipped
        }
        if (!pivot->leftExpr->GetTy().IsCorrect()) {
            auto newPivot = GetChildBinaryExpr(*pivot->leftExpr);
            if (newPivot) {
                pivotStack.emplace_back(newPivot);
                continue;
            }
            if (!SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, pivot->leftExpr.get()).IsCorrect()) {
                // `leftExpr` is the pivot, and the `Synthesize` should have diagnosed errors in `leftExpr`.
                // Now we report diagnostics in the `rightExpr`,
                SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, pivot->rightExpr.get());
                return;
            }
        }
        // So far, `leftExpr` is correct.
        if (Ty::IsInitialTy(pivot->rightExpr->DataTy())) {
            auto ds = DiagSuppressor(diag);
            SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, pivot->rightExpr);
        }
        if (!pivot->rightExpr->GetTy().IsCorrect()) {
            auto newPivot = GetChildBinaryExpr(*pivot->rightExpr);
            if (newPivot) {
                pivotStack.emplace_back(newPivot);
                continue;
            }
            if (!SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, pivot->rightExpr.get()).IsCorrect()) {
                // `rightExpr` is the pivot, and the `Synthesize` should have diagnosed.
                return;
            }
        }
        // So far, both `leftExpr` and `rightExpr` are valid, i.e., we've got the pivot.
        break;
    }
    while (pivotStack.size() > 1) {
        Ptr<BinaryExpr> pivot = pivotStack.back();
        pivotStack.pop_back();
        if (!SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, pivot).IsCorrect()) {
            return;
        }
    }
    Ptr<BinaryExpr> pivot = pivotStack.back();
    SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, pivot->leftExpr.get());
    SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, pivot->rightExpr.get());
    if (ReplaceIdealTy(*pivot->leftExpr) && ReplaceIdealTy(*pivot->rightExpr)) {
        DiagInvalidBinaryExpr(diag, *pivot);
    }
}

ModalTy TypeChecker::TypeCheckerImpl::SynBinaryExpr(ASTContext& ctx, BinaryExpr& be)
{
    if (be.desugarExpr) {
        return be.desugarExpr->GetTy();
    }
    CJC_NULLPTR_CHECK(be.leftExpr);
    CJC_NULLPTR_CHECK(be.rightExpr);

    auto cs = PData::CommitScope(typeManager.constraints);
    
    // 1. Try built-in binary expression inference
    { // Create a scope for DiagSuppressor.
        auto ds = DiagSuppressor(diag);
        ModalTy inferRet{TypeManager::GetInvalidTy()};
        if (auto optTy = InferBinaryExprCaseBuiltIn(ctx, be, inferRet)) {
            ds.ReportDiag();
            return *optTy;
        }
        if (Ty::IsTyCorrect(inferRet)) {
            ds.ReportDiag();
            return be.GetTy();
        }
    }

    PData::Reset(typeManager.constraints);

    // 2. Try operator overload if applicable
    if (TypeCheckUtil::IsOverloadableOperator(be.op)) {
        TrySynthesizeOperatorOverload(ctx, be);
    } else {
        be.SetTy({TypeManager::GetInvalidTy()});
    }
    
    // 3. Handle failure case
    if (be.TestAttr(Attribute::IS_OUTERMOST)) {
        SynBinaryLeafs(ctx, be); // NOTE: All used reference must be synthesized for lsp usage.
    }
    DiagnoseForBinaryExpr(ctx, be);
    return be.GetTy();
}

/**
 * Synthesize all reference leaf nodes when BinaryExpr is checked as invalid.
 */
void TypeChecker::TypeCheckerImpl::SynBinaryLeafs(ASTContext& ctx, BinaryExpr& be)
{
    auto id = Walker::GetNextWalkerID();
    std::function<VisitAction(Ptr<Node>)> synLeafs = [this, id, &ctx, &synLeafs](auto node) {
        CJC_ASSERT(node);
        if (auto expr = DynamicCast<Expr*>(node); expr && expr->desugarExpr) {
            Walker(expr, id, synLeafs).Walk();
            return VisitAction::SKIP_CHILDREN;
        }
        if (node->TestAnyAttr(AST::Attribute::IS_BROKEN, AST::Attribute::HAS_BROKEN) || Is<Pattern>(node)) {
            return VisitAction::SKIP_CHILDREN;
        }
        if (auto ce = DynamicCast<CallExpr*>(node); ce) {
            CJC_NULLPTR_CHECK(ce->baseFunc);
            SetIsNotAlone(*ce->baseFunc);
            if (auto bf = DynamicCast<NameReferenceExpr*>(ce->baseFunc.get())) {
                bf->callOrPattern = ce;
            }
        } else if (auto ma = DynamicCast<MemberAccess*>(node); ma && !IsFieldOperator(ma->field)) {
            Synthesize({ctx, SynPos::EXPR_ARG}, ma);
            return VisitAction::SKIP_CHILDREN;
        } else if (auto re = DynamicCast<RefExpr*>(node); re) {
            Synthesize({ctx, SynPos::EXPR_ARG}, re);
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(&be, id, synLeafs).Walk();
}

std::optional<ModalTy> TypeChecker::TypeCheckerImpl::InferBinaryExprCaseBuiltIn(
    ASTContext& ctx, BinaryExpr& be, ModalTy& inferRet)
{
    bool failNow = false;
    if (Utils::In(be.op, ARITHMETIC_OPERATOR)) {
        if (auto maybeRet = SynArithmeticOrRelationalExpr(ctx, be)) {
            inferRet = *maybeRet;
        } else {
            failNow = true;
        }
    } else if (Utils::In(be.op, RELATIONAL_OPERATOR)) {
        if (auto maybeRet = SynArithmeticOrRelationalExpr(ctx, be, false)) {
            inferRet = *maybeRet;
        } else {
            failNow = true;
        }
        if (Ty::IsInitialTy(be.leftExpr->DataTy())) {
            SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, be.leftExpr.get());
        }
        if (Ty::IsInitialTy(be.rightExpr->DataTy())) {
            SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, be.rightExpr.get());
        }
        auto leftTupleTy = DynamicCast<TupleTy>(be.leftExpr->DataTy());
        auto rightTupleTy = DynamicCast<TupleTy>(be.rightExpr->DataTy());
        if (Ty::IsTyCorrect(leftTupleTy) && Ty::IsTyCorrect(rightTupleTy) &&
            (be.op == TokenKind::EQUAL || be.op == TokenKind::NOTEQ)) {
            if (!CheckTupleCanEqual(ctx, be)) {
                inferRet = {TypeManager::GetInvalidTy()};
            } else {
                inferRet = be.GetTy();
            }
        }
    } else if (Utils::In(be.op, LOGICAL_OPERATOR)) {
        inferRet = SynLogicalExpr(ctx, be);
    } else if (Utils::In(be.op, SHIFT_OPERATOR)) {
        inferRet = SynShiftExpr(ctx, be);
    } else if (Utils::In(be.op, FLOW_OPERATOR)) {
        inferRet = SynFlowExpr(ctx, be);
        return {be.GetTy()};
    } else if (be.op == TokenKind::COALESCING) {
        return {SynCoalescingExpr(ctx, be)};
    } else {
        if (auto maybeRet = SynArithmeticOrRelationalExpr(ctx, be)) {
            inferRet = *maybeRet;
        } else {
            failNow = true;
        }
    }
    // Only syn leaf nodes when current is the outermost binaryExpr.
    if (be.TestAttr(Attribute::IS_OUTERMOST)) {
        // Since check of 'CallExpr' will clear target of 'RefExpr' and 'MemberAccess',
        // we need to syn leaf before check invalid.
        SynBinaryLeafs(ctx, be);
    }
    if (HasInvalidChild(be)) {
        if (be.TestAttr(Attribute::IS_OUTERMOST)) {
            DiagnoseForBinaryExpr(ctx, be); // Only report error when current is outer most binary expr.
        }
        be.SetTy({TypeManager::GetInvalidTy()});
        return {be.GetTy()};
    } else if (failNow) {
        be.SetTy({TypeManager::GetInvalidTy()});
        return {be.GetTy()};
    } else {
        return {};
    }
}

bool TypeChecker::TypeCheckerImpl::CheckFlowOperandsHaveNamedParam(const CallExpr& ce)
{
    return std::any_of(ce.args.cbegin(), ce.args.cend(), [this](const OwnedPtr<FuncArg>& arg) {
        CJC_NULLPTR_CHECK(arg);
        if (auto re = DynamicCast<const RefExpr*>(arg->expr.get());
            re && RefExprTargetIsFuncDeclAndHasNamedParam(*re)) {
            diag.Diagnose(*arg->expr, DiagKind::sema_fail_flow_expr_operand_has_named_param);
            return true;
        }
        return false;
    });
}

bool TypeChecker::TypeCheckerImpl::ChkFlowExpr(ASTContext& ctx, ModalTy target, BinaryExpr& be)
{
    if (be.GetTy().IsCorrect()) {
        return !(target && typeManager.CheckTypeCompatibility(be.GetTy(), target) == TypeCompatibility::INCOMPATIBLE);
    }
    auto& rightExpr = *be.rightExpr;
    auto& leftExpr = *be.leftExpr;
    rightExpr.isInFlowExpr = true;
    auto cs = PData::CommitScope(typeManager.constraints);
    {
        auto ds = DiagSuppressor(diag);
        SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, &leftExpr);
        SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, &rightExpr);
        if (ds.HasError()) {
            if (be.op == TokenKind::COMPOSITION) {
                be.SetTy({TypeManager::GetInvalidTy()});
                ds.ReportDiag();
                return false;
            } else {
                leftExpr.Clear();
                rightExpr.Clear();
                PData::Reset(typeManager.constraints);
            }
        }
    }
    // Check if flow expr contains 'this' which is illegal.
    // 'super' is not allowed to be used alone and is checked in 'InferSuperExpr'.
    auto isThisExpr = [this](Expr& exp) {
        if (auto re = DynamicCast<RefExpr*>(&exp); re && re->isAlone && re->isThis) {
            diag.Diagnose(*re, DiagKind::sema_flow_expressions_use_this_or_super, re->ref.identifier.Val());
            return false;
        }
        return true;
    };
    // 'this' or 'super' is not allowed to be used in function part of flow expression.
    // NOT allowed: a |> this; a |> super; this ~> f; f ~> this; super ~> f; f ~> super.
    // Allowed: this |> f.
    if ((be.op == TokenKind::COMPOSITION && !isThisExpr(leftExpr)) || !isThisExpr(rightExpr)) {
        be.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
    // Desugar flow expr.
    DesugarFlowExpr(ctx, be);

    CJC_ASSERT(be.desugarExpr && be.desugarExpr->astKind == ASTKind::CALL_EXPR);
    CallExpr& beCallExpr = *StaticCast<CallExpr*>(be.desugarExpr.get());
    {
        auto ds = DiagSuppressor(diag);
        if (!ChkCallExpr(ctx, target, beCallExpr)) {
            (void)ds.GetSuppressedDiag();
            // Should recover the desugaredExpr to binaryExpr if failed.
            RecoverToBinaryExpr(be);
            be.SetTy({TypeManager::GetInvalidTy()});
            DiagnoseForBinaryExpr(ctx, be);
            ds.ReportDiag();
            return false;
        }
        ds.ReportDiag(); // Report warnings.
    }
    if (CheckFlowOperandsHaveNamedParam(beCallExpr)) {
        // Should recover the desugaredExpr to binaryExpr if failed.
        RecoverToBinaryExpr(be);
        be.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
    be.SetTy(be.desugarExpr->GetTy());
    return true;
}

ModalTy TypeChecker::TypeCheckerImpl::SynFlowExpr(ASTContext& ctx, BinaryExpr& be)
{
    if (!ChkFlowExpr(ctx, ModalTy{}, be)) {
        be.SetTy({TypeManager::GetInvalidTy()});
    }
    return be.GetTy();
}

bool TypeChecker::TypeCheckerImpl::IsCoalescingLeftTyValid(ModalTy ty) const
{
    ModalTy realTy = ty;
    if (ty->IsPlaceholder()) {
        auto optionDecl = importManager.GetCoreDecl("Option");
        realTy = typeManager.ConstrainByCtor(StaticCast<GenericsTy&>(*ty), *optionDecl->DataTy());
    }
    if (!Ty::IsTyCorrect(realTy) || !realTy->IsEnum()) {
        return false;
    }
    auto enumTy = RawStaticCast<EnumTy*>(realTy.Ty());
    if (!enumTy->declPtr) {
        return false;
    }
    if (enumTy->declPtr->fullPackageName != CORE_PACKAGE_NAME || enumTy->typeArgs.size() != 1) {
        return false;
    } else if (enumTy->declPtr->identifier == STD_LIB_OPTION) {
        // Coalescing valid for enum 'Option' in core package.
        return true;
    }
    return false;
}

bool TypeChecker::TypeCheckerImpl::ChkCoalescingExpr(ASTContext& ctx, ModalTy tgtTy, BinaryExpr& be)
{
    auto leftTy = SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, be.leftExpr.get());
    bool isLeftTyInvalid = !leftTy || !IsCoalescingLeftTyValid(leftTy);
    be.leftExpr->SetTy(typeManager.TryGreedySubst(leftTy));
    leftTy = typeManager.TryGreedySubst(leftTy);
    isLeftTyInvalid = isLeftTyInvalid || !leftTy->typeArgs[0].Ty();
    if (isLeftTyInvalid) {
        be.SetTy({TypeManager::GetInvalidTy()});
        if (!CanSkipDiag(*be.leftExpr)) {
            (void)diag.Diagnose(*be.leftExpr, DiagKind::sema_invalid_coalescing);
        }
        // Do not set (children of) be.rightExpr to InvalidTy due to the bugs of the type check process.
        // For example, Check(String, B??"") succeeds and Check(Int64, B??"") fails. Then the re-inference
        // Check(String, B??"") fails because the type of "" does not change; the change is prevented by the re-visit
        // detection in CheckReferenceTypeLegality invoked by SynLitConstStringExpr.
        return false;
    }
    CJC_ASSERT(leftTy);
    if (tgtTy != nullptr && !typeManager.IsSubtype(leftTy->typeArgs[0], tgtTy)) {
        // Did not report errors before. Raise a new error message.
        if (Ty::IsTyCorrect(leftTy->typeArgs[0])) {
            DiagMismatchedTypesWithFoundTy(diag, *be.leftExpr, tgtTy, leftTy->typeArgs[0]);
        }
        be.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
    auto realTgtTy = (tgtTy != nullptr) ? tgtTy : leftTy->typeArgs[0];
    if (!CheckWithNegCache(ctx, realTgtTy, be.rightExpr.get())) {
        be.SetTy({TypeManager::GetInvalidTy()});
        DiagMismatchedTypes(diag, *be.rightExpr, realTgtTy);
        return false;
    }
    be.SetTy(realTgtTy);
    return true;
}

ModalTy TypeChecker::TypeCheckerImpl::SynCoalescingExpr(ASTContext& ctx, BinaryExpr& be)
{
    if (!ChkCoalescingExpr(ctx, ModalTy{}, be)) {
        be.SetTy({TypeManager::GetInvalidTy()});
    }
    return be.GetTy();
}

MatchResult TypeChecker::TypeCheckerImpl::PickConstaintFromTys(TyVar& tv, std::set<AST::ModalTy> tys, bool isUB)
{
    PData::CommitScope cs(typeManager.constraints);
    size_t cnt = 0;
    ModalTy tvLeaf{static_cast<DataTy>(&tv)};
    ModalTy target{};
    for (auto ty : tys) {
        if (isUB ? typeManager.IsSubtype(tvLeaf, ty) : typeManager.IsSubtype(ty, tvLeaf)) {
            cnt++;
            target = ty;
        }
        PData::Reset(typeManager.constraints);
    }
    if (cnt > 1) {
        return MatchResult::AMBIGUOUS;
    } else if (cnt == 1) {
        isUB ? typeManager.IsSubtype(tvLeaf, target)
             : typeManager.IsSubtype(target, tvLeaf); // only for adding constraint
        return MatchResult::UNIQUE;
    } else {
        return MatchResult::NONE;
    }
}

MatchResult TypeChecker::TypeCheckerImpl::PickConstaintFromTys(
    ModalTy tv1, ModalTy tv2, std::set<std::pair<AST::ModalTy, AST::ModalTy>> tys, bool isUB)
{
    PData::CommitScope cs(typeManager.constraints);
    size_t cnt = 0;
    ModalTy target1{};
    ModalTy target2{};
    for (auto [ty1, ty2] : tys) {
        if (isUB ? typeManager.IsSubtype(tv1, ty1) && typeManager.IsSubtype(tv2, ty2)
                 : typeManager.IsSubtype(ty1, tv1) && typeManager.IsSubtype(ty2, tv2)) {
            cnt++;
            target1 = ty1;
            target2 = ty2;
        }
        PData::Reset(typeManager.constraints);
    }
    if (cnt > 1) {
        return MatchResult::AMBIGUOUS;
    } else if (cnt == 1) {
        // only for adding constraint
        isUB ? typeManager.IsSubtype(tv1, target1) && typeManager.IsSubtype(tv2, target2)
             : typeManager.IsSubtype(target1, tv1) && typeManager.IsSubtype(target2, tv2);
        return MatchResult::UNIQUE;
    } else {
        return MatchResult::NONE;
    }
}
