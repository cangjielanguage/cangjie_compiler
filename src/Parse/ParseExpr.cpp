// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the expression parsing (except for atoms, which are
 * in ParseAtom.cpp).
 */

#include "ParserImpl.h"

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace Cangjie::AST;

namespace {
// ++ or -- (used later in ParseIncOrDec)
const int INCREMENT_OP_LENGTH = 2;
inline bool IsCompoundAssign(TokenKind token)
{
    return token >= TokenKind::ADD_ASSIGN && token <= TokenKind::RSHIFT_ASSIGN;
}
// For fmt, these exprs no need to add semicolon info, because child expr of these nodes has semicolon already.
const static std::vector<ASTKind> G_NO_NEED_SEMI_EXPR_LIST = {ASTKind::THROW_EXPR, ASTKind::RETURN_EXPR,
    ASTKind::ASSIGN_EXPR, ASTKind::UNARY_EXPR, ASTKind::SPAWN_EXPR, ASTKind::IF_EXPR, ASTKind::TRY_EXPR,
    ASTKind::FOR_IN_EXPR, ASTKind::WHILE_EXPR};

// It looks like operators, but not. For better error reporting.
const static std::vector<TokenKind> FAKE_OPERATORS = {TokenKind::COLON};
} // namespace

bool ParserImpl::SeeingExprOperator()
{
    auto token = Peek();
    if (newlineSkipped && (token.kind == TokenKind::SUB || token.kind == TokenKind::QUEST)) {
        return false;
    }
    for (auto& [k, _] : combinator) {
        if (SeeingCombinator(k)) {
            return true;
        }
    }
    if (Precedence(token.kind) != INVALID_PRECEDENCE) {
        return true;
    }
    return IsCompoundAssign(token.kind) || token.kind == TokenKind::ASSIGN;
}

bool ParserImpl::TypeArgsMaybeConfusedWithExprWithComma(const std::vector<OwnedPtr<AST::Type>>& typeArgs) const
{
    if (typeArgs.size() <= 1) {
        return false; // "a<b>", "a<b<c,d>>", "a<(b)>"... no confusion.
    }
    if (TypesMaybeConfusedWithExprWithComma(ConverVector(typeArgs))) {
        return true;
    }
    return false;
}

bool ParserImpl::TypesMaybeConfusedWithExprWithComma(const std::vector<Ptr<AST::Type>>& types) const
{
    for (auto itr = types.rbegin(); itr != types.rend(); ++itr) {
        if ((*itr)->astKind == ASTKind::TUPLE_TYPE) {
            // "(a<b, (c, d,), f>g)" may be confused
            auto curType = RawStaticCast<TupleType*>(*itr);
            if (!TypesMaybeConfusedWithExprWithComma(ConverVector(curType->fieldTypes))) {
                return false;
            }
        } else if ((*itr)->astKind == ASTKind::PAREN_TYPE) {
            // "(a<b, (c), f>g)"" may be confused
            auto curType = RawStaticCast<ParenType*>(*itr);
            if (!TypesMaybeConfusedWithExprWithComma({curType->type.get()})) {
                return false;
            }
        } else if ((*itr)->astKind == ASTKind::REF_TYPE || (*itr)->astKind == ASTKind::QUALIFIED_TYPE) {
            // "(x.a<b, c<e, f>>=g)", "(a<b.x, c<d, e>, f>g)", "(a<b, c<d>, e>f)" may be confused
            if ((*itr)->GetTypeArgs().empty()) {
                continue;
            }
            if (!TypesMaybeConfusedWithExprWithComma((*itr)->GetTypeArgs())) {
                return false;
            }
        } else {
            // PrimitiveType OptionType VArrayType(ConstantType) ThisType FuncType cannot be used as expression
            return false;
        }
    }
    return true;
}

bool ParserImpl::IsLegFollowForGenArgInExprWithComma(ExprKind ek)
{
    CJC_ASSERT(IsExprFollowedComma(ek));
    if (Seeing(exprsFollowedCommas.at(ek).second)) { return true; }
    if (newlineSkipped &&
        SeeingAny({TokenKind::LCURL, TokenKind::QUEST, TokenKind::LPAREN, TokenKind::LSQUARE, TokenKind::SUB})) {
        return false;
    }
    if (SeeingAny({TokenKind::LPAREN, TokenKind::LSQUARE, TokenKind::COMMA, TokenKind::DOT, TokenKind::LCURL})) {
        return true;
    }
    for (auto& [k, _] : combinator) {
        if (SeeingCombinator(k)) {
            return true;
        }
    }
    auto token = Peek();
    if (Precedence(token.kind) != INVALID_PRECEDENCE) {
        return true;
    }
    return false;
}

bool ParserImpl::IsNeedToCreateOptionalChain(TokenKind token, AST::Expr& expr) const
{
    if (!IsCompoundAssign(token) && token != TokenKind::ASSIGN && expr.hasQuestSuffix) {
        return true;
    }
    if (auto incdecl = DynamicCast<IncOrDecExpr*>(&expr); incdecl && incdecl->expr && incdecl->expr->hasQuestSuffix) {
        return true;
    }
    return false;
}

Token ParserImpl::GetExprOperator()
{
    auto token = Peek();
    auto iter =
        std::find_if(combinator.rbegin(), combinator.rend(), [this](auto& k) { return SeeingCombinator(k.first); });
    if (iter != combinator.rend()) {
        token.kind = iter->second.first;
        std::string_view op{iter->second.second};
        token.SetValuePos(std::string{op}, token.Begin(), token.Begin() + op.size());
    }
    return token;
}

void ParserImpl::SkipExprOperator()
{
    Peek();
    auto iter =
        std::find_if(combinator.rbegin(), combinator.rend(), [this](auto& k) { return SeeingCombinator(k.first); });
    if (iter != combinator.rend()) {
        SkipCombinator(iter->first);
        return;
    }
    Next();
}

// Will only create leftExpr part and operator, the right part will register later.
OwnedPtr<AST::Expr> ParserImpl::MakeOperatorExpr(OwnedPtr<AST::Expr>& lExpr, const Token& oTok)
{
    if (oTok.kind == TokenKind::IS) {
        OwnedPtr<IsExpr> isExpr = MakeOwned<IsExpr>();
        SpreadAttrAndConsume(lExpr.get(), isExpr.get(), {});
        if (lExpr->astKind == ASTKind::IS_EXPR || lExpr->astKind == ASTKind::AS_EXPR) {
            DiagChainedAsExpr(*lExpr, oTok);
        }
        isExpr->begin = lExpr->begin;
        isExpr->isPos = oTok.Begin();
        isExpr->leftExpr = std::move(lExpr);
        return isExpr;
    } else if (oTok.kind == TokenKind::AS) {
        OwnedPtr<AsExpr> asExpr = MakeOwned<AsExpr>();
        SpreadAttrAndConsume(lExpr.get(), asExpr.get(), {});
        if (lExpr->astKind == ASTKind::IS_EXPR || lExpr->astKind == ASTKind::AS_EXPR) {
            DiagChainedAsExpr(*lExpr, oTok);
        }
        asExpr->begin = lExpr->begin;
        asExpr->asPos = oTok.Begin();
        asExpr->leftExpr = std::move(lExpr);
        return asExpr;
    } else if (oTok.kind == TokenKind::RANGEOP || oTok.kind == TokenKind::CLOSEDRANGEOP) {
        OwnedPtr<RangeExpr> rangeExpr = MakeOwned<RangeExpr>();
        SpreadAttrAndConsume(lExpr.get(), rangeExpr.get(), {});
        rangeExpr->begin = lExpr->begin;
        rangeExpr->isClosed = oTok.kind == TokenKind::CLOSEDRANGEOP;
        rangeExpr->rangePos = oTok.Begin();
        rangeExpr->startExpr = std::move(lExpr);
        return rangeExpr;
    } else if (IsCompoundAssign(oTok.kind) || oTok.kind == TokenKind::ASSIGN) {
        OwnedPtr<AssignExpr> asinExpr = MakeOwned<AssignExpr>();
        SpreadAttrAndConsume(lExpr.get(), asinExpr.get(), {});
        asinExpr->begin = lExpr->begin;
        asinExpr->hasQuestSuffix = lExpr->hasQuestSuffix;
        asinExpr->leftValue = std::move(lExpr);
        asinExpr->assignPos = oTok.Begin();
        asinExpr->op = oTok.kind;
        asinExpr->isCompound = IsCompoundAssign(oTok.kind);
        return asinExpr;
    }
    OwnedPtr<BinaryExpr> biExpr = MakeOwned<BinaryExpr>();
    SpreadAttrAndConsume(lExpr.get(), biExpr.get(), {});
    biExpr->begin = lExpr->begin;
    biExpr->op = oTok.kind;
    biExpr->operatorPos = oTok.Begin();
    biExpr->leftExpr = std::move(lExpr);
    return biExpr;
}

void ParserImpl::CheckWildcardInExpr(const OwnedPtr<Expr>& root)
{
    // if any expr contains wildcard(_), e.g TupleLit、ParenExpr, complains
    Walker(root.get(), [this](Ptr<Node> node) -> VisitAction {
        if (node->astKind == ASTKind::MATCH_CASE_OTHER) {
            // Simple match expr, no match selector，so skip wildcard expr in the match case.
            return VisitAction::SKIP_CHILDREN;
        }
        if (node->astKind == ASTKind::ASSIGN_EXPR && StaticAs<ASTKind::ASSIGN_EXPR>(node)->op == TokenKind::ASSIGN) {
            return VisitAction::SKIP_CHILDREN;
        }
        if (node->astKind == ASTKind::WILDCARD_EXPR) {
            if ((node->begin.line == lookahead.Begin().line) && (lookahead.Begin().line == lastToken.Begin().line) &&
                lookahead.kind != TokenKind::SENTINEL) {
                ConsumeUntilAny({TokenKind::NL, TokenKind::RPAREN, TokenKind::COMMA, TokenKind::RCURL}, false);
            }
            auto builder =
                ParseDiagnoseRefactor(DiagKindRefactor::parse_unexpected_expected_found, *node, "_ wildcard");
            builder.AddMainHintArguments("expression", "wildcard");
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

void ParserImpl::RegisterRightExpr(const OwnedPtr<AST::Expr>& expr, OwnedPtr<AST::Expr>&& rExpr)
{
    // rExpr cannot be a wildcard, or tuple containing wildcards, e.g (1, _) 、(1, (2, _))
    if (expr->astKind == ASTKind::ASSIGN_EXPR && StaticAs<ASTKind::ASSIGN_EXPR>(expr.get())->op == TokenKind::ASSIGN) {
        CheckWildcardInExpr(rExpr);
    }

    expr->end = rExpr->end;
    if (expr->astKind == ASTKind::BINARY_EXPR) {
        auto biExpr = StaticAs<ASTKind::BINARY_EXPR>(expr.get());
        biExpr->rightExpr = std::move(rExpr);
        return;
    }
    if (expr->astKind == ASTKind::ASSIGN_EXPR) {
        auto assiExpr = StaticAs<ASTKind::ASSIGN_EXPR>(expr.get());
        assiExpr->rightExpr = std::move(rExpr);
        return;
    }
    if (expr->astKind == ASTKind::RANGE_EXPR) {
        auto rangeExpr = StaticAs<ASTKind::RANGE_EXPR>(expr.get());
        rangeExpr->end = rExpr->end;
        rangeExpr->stopExpr = std::move(rExpr);
        if (Skip(TokenKind::COLON)) {
            rangeExpr->colonPos = lastToken.Begin();
            constexpr std::string_view dot2{".."};
            auto preT = Token{
                TokenKind::RANGEOP, std::string{dot2}, rangeExpr->rangePos, rangeExpr->rangePos + dot2.size()};
            preT.kind = rangeExpr->isClosed ? TokenKind::CLOSEDRANGEOP : preT.kind;
            std::string op = rangeExpr->isClosed ? std::string{"..="} : preT.Value();
            preT.SetValuePos(op, preT.Begin(), preT.Begin() + op.size());
            rangeExpr->stepExpr = ParseExpr(preT);
            rangeExpr->end = rangeExpr->stepExpr->end;
        }
        return;
    }
}

const std::vector<TokenKind> ParserImpl::combinedBackarrow{TokenKind::LT, TokenKind::SUB};
OwnedPtr<LetPatternDestructor> ParserImpl::ParseLetPattern(ExprKind ek)
{
    auto res = MakeOwned<LetPatternDestructor>();
    (void)Skip(TokenKind::LET);
    res->begin = lastToken.Begin();
    // parse patterns
    auto first = ParsePattern();
    first->ctxExpr = res;
    res->patterns.emplace_back(std::move(first));
    while (Skip(TokenKind::BITOR)) {
        res->orPos.push_back(lastToken.Begin());
        auto pattern = ParsePattern();
        pattern->ctxExpr = res;
        res->patterns.emplace_back(std::move(pattern));
    }
    
    // parse initializer
    auto consumeTarget = [this]() {
        return SeeingAny({TokenKind::AND, TokenKind::OR, TokenKind::RPAREN, TokenKind::LET, TokenKind::BACKARROW}) ||
            SeeingCombinator(combinedBackarrow);
    };

    bool foundBackarrow{true};
    if (!Skip(TokenKind::BACKARROW) && !SeeingCombinator(combinedBackarrow)) {
        auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_backarrow_in_let_cond, lookahead,
            ek == ExprKind::WHILE_COND_EXPR ? "while-let" : "if-let", ConvertToken(lookahead));
        ConsumeUntilAny(consumeTarget, false);
        foundBackarrow = false;
        res->initializer = MakeInvalid<Block>(lookahead.Begin());
        res->end = res->initializer->end;
        res->EnableAttr(Attribute::HAS_BROKEN);
    }
    res->backarrowPos = lastToken.Begin();
    if (foundBackarrow) {
        res->initializer = ParseExpr(ExprKind::LET_PATTERN);
    }
    res->end = res->initializer->end;
    if (!res->initializer && !res->TestAttr(Attribute::HAS_BROKEN)) {
        DiagMatchCaseBodyCannotBeEmpty(res->backarrowPos + std::string_view("<-").size());
        res->EnableAttr(Attribute::HAS_BROKEN);
    }
    return res;
}

// Parse IndexAccess, e.g a[1]、a[1..]、a[..]、a[..1]、a[1..2]
// It's important to note that IndexAccess and RangeExpr are different, RangeExpr is a subset of IndexAccess
// RangeExpr must have two subexpressions, like 1 and 2 in a[1..2],
// however IndexAccess can be a range with zero, one or two subexpression, like a[..]、 a[1..]、a[1..3]
// or IndexAccess is an expression in itself, like 1 in a[1]
// If IndexAccess is a range, here we just reuse AST::RangeExpr to store subexpression(s)
OwnedPtr<AST::Expr> ParserImpl::ParseIndexAccess()
{
    if (IsCompoundAssign(Peek().kind) || Peek().kind == TokenKind::ASSIGN) {
        // an error is reported for illeal index access, e.g: a[+=1], a[=1]
        ParseDiagnoseRefactor(
            DiagKindRefactor::parse_expected_expression, lastToken.End(), lastToken.Value(), lookahead.Value());
        ConsumeUntilAny({TokenKind::NL, TokenKind::SEMI, TokenKind::RSQUARE}, false);
        return MakeInvalid<InvalidExpr>(lookahead.Begin());
    }
    auto ret = MakeOwned<RangeExpr>();
    ret->begin = Peek().Begin();
    OwnedPtr<Expr> firstExpr;
    // dealing with expr that follows '['
    if (!Seeing(TokenKind::RANGEOP) && !Seeing(TokenKind::CLOSEDRANGEOP)) {
        // parse an expr and ensure that operator(s)'s priority in the expr is not lower than TokenKind::RANGEOP
        firstExpr = ParseExpr(Token{TokenKind::RANGEOP});
    }
    if (!Seeing(TokenKind::RANGEOP) && !Seeing(TokenKind::CLOSEDRANGEOP)) {
        // two scenarios will get to this branch
        // 1. means IndexAccess is an expression in itself
        // 2. firstExpr is followed by an operator
        //    and the priority of the operator is lower than that of TokenKind::RANGEOP
        return ParseExpr(Token{TokenKind::DOT}, std::move(firstExpr));
    }

    // Seeing TokenKind::RANGEOP || Seeing(TokenKind::CLOSEDRANGEOP)
    ret->startExpr = std::move(firstExpr);
    ret->isClosed = lookahead.kind == TokenKind::CLOSEDRANGEOP;
    ret->rangePos = lookahead.Begin();
    Next();
    ret->end = lastToken.End();
    if (!Seeing(TokenKind::RSQUARE) && !Seeing(TokenKind::COLON)) {
        // parse stop expr
        ret->stopExpr = ParseExpr(Token{TokenKind::RANGEOP});
        ret->end = ret->stopExpr->end;
    }
    if (ret->isClosed && ret->stopExpr == nullptr) {
        // an error is reported for illeal index access,e.g: a[1..=]、a[..=]
        ParseDiagnoseRefactor(
            DiagKindRefactor::parse_expected_expression, lastToken.End(), lastToken.Value(), lookahead.Value());
    }
    if (Seeing(TokenKind::COLON)) {
        if (!ret->startExpr || !ret->stopExpr) {
            DiagUnexpectedColonInRange(*ret);
            ConsumeUntilAny({TokenKind::NL, TokenKind::SEMI, TokenKind::RSQUARE}, false);
            return ret;
        }
        Next();
        ret->colonPos = lastToken.Begin();
        ret->stepExpr = ParseExpr(Token{TokenKind::RANGEOP});
        ret->end = ret->stepExpr->end;
    }
    return ParseExpr(Token{TokenKind::DOT}, std::move(ret));
}

OwnedPtr<Expr> ParserImpl::ParseExpr(ExprKind ek)
{
    // The unknown_expr only work for libast.
    if (ek == ExprKind::UNKNOWN_EXPR) {
        if (Seeing(TokenKind::LET)) {
            return ParseLetPattern(ek);
        }
    }
    OwnedPtr<Expr> ret;
    if (ek == ExprKind::INDEX_EXPR) {
        ret = ParseIndexAccess();
    } else if (enableCustomAnno && SeeingIfAvailable()) { // only parse @IfAvailable after macro expansion
        ret = ParseIfAvailable();
    } else if (ek == ExprKind::VAR_INIT) {
        ret = ParseExpr(Token{TokenKind::DOT});
        if (ret->astKind == ASTKind::ASSIGN_EXPR) {
            DiagCannotHaveAssignmentInInit(*ret);
        }
    } else {
        ret = ParseExpr(Token{TokenKind::DOT}, nullptr, ek);
    }
    if (!SeeingExprOperator() && ek != ExprKind::EXPR_IN_TUPLE && ek != ExprKind::EXPR_IN_IF_COND_TUPLE &&
        ek != ExprKind::EXPR_IN_WHILE_COND_TUPLE) {
        // wildcard and tuple that contains wildcard cannot appear alone,e.g: if(_){},
        // judging EXPR_IN_TUPLE is to check(traverse) only once.
        CheckWildcardInExpr(ret);
    }
    // For fmt, record the semicolon position.
    if (Seeing(TokenKind::SEMI) && Utils::NotIn(ret->astKind, G_NO_NEED_SEMI_EXPR_LIST)) {
        ret->hasSemi = true;
        ret->semiPos = lookahead.Begin();
    }
    if (ret->astKind == ASTKind::MACRO_EXPAND_EXPR) {
        auto mee = StaticAs<ASTKind::MACRO_EXPAND_EXPR>(ret.get());
        mee->invocation.scope = ek;
    }
    if (SeeingAny(FAKE_OPERATORS)) {
        DiagExpecetedOpeOrEnd();
        ConsumeUntilAny({TokenKind::NL});
    }
    return ret;
}

OwnedPtr<IfAvailableExpr> ParserImpl::ParseIfAvailable()
{
    OwnedPtr<FuncArg> arg{};
    OwnedPtr<LambdaExpr> lambda1{};
    OwnedPtr<LambdaExpr> lambda2{};
    Skip(TokenKind::AT);
    auto begin = lastToken.Begin();
    Skip(TokenKind::IDENTIFIER);

    auto fail = [this, &begin, &arg, &lambda1, &lambda2]() {
        Next();
        auto diagBag = ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_character,
            lastToken.Begin(), "','", "'" + lastToken.Value() + "'");
        diagBag.AddMainHintArguments("','");
        auto ret = MakeOwned<IfAvailableExpr>(std::move(arg), std::move(lambda1), std::move(lambda2));
        ret->begin = std::move(begin);
        ret->EnableAttr(Attribute::IS_BROKEN);
        ret->end = lastToken.End();
        return ret;
    };

    bool bad{false};
    Position lparenPos{};
    bool lparenSkipped{false};
    if (Skip(TokenKind::LPAREN)) {
        lparenPos = lastToken.Begin();
        lparenSkipped = true;
    } else {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_character, lookahead.Begin(), "(", lookahead.Value());
        bad = true;
    }
    arg = ParseFuncArg();
    if (arg->name.Empty()) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_ifavailable_arg_no_name, lookahead);
        bad = true;
    }
    if (!Skip(TokenKind::COMMA)) {
        auto ret = fail();
        ConsumeUntil(TokenKind::RPAREN);
        return ret;
    }
    
    auto parseLambda = [this, &bad] () -> OwnedPtr<LambdaExpr> {
        auto expr1 = ParseExpr();
        if (Is<LambdaExpr>(expr1)) {
            return OwnedPtr{StaticCast<LambdaExpr>(expr1.release())};
        } else {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_ifavailable_not_lambda, *expr1);
            bad = true;
        }
        return {};
    };
    lambda1 = parseLambda();
    
    if (!Skip(TokenKind::COMMA)) {
        auto ret = fail();
        ConsumeUntil(TokenKind::RPAREN);
        return ret;
    }
    lambda2 = parseLambda();
    auto ret = MakeOwned<IfAvailableExpr>(std::move(arg), std::move(lambda1), std::move(lambda2));
    ret->begin = std::move(begin);
    if (!Skip(TokenKind::RPAREN)) {
        if (lparenSkipped) {
            auto db = ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_right_delimiter, lookahead.Begin(), "(");
            db.AddMainHintArguments(")");
            db.AddHint(lparenPos, "(");
        }
        bad = true;
    }
    if (bad) {
        ret->EnableAttr(Attribute::IS_BROKEN);
    }
    ret->end = lastToken.End();
    return ret;
}

bool ParserImpl::IsConditionExpr(ExprKind ek)
{
    return ek == ExprKind::IF_COND_EXPR || ek == ExprKind::WHILE_COND_EXPR || ek == ExprKind::EXPR_IN_IF_COND_TUPLE ||
        ek == ExprKind::EXPR_IN_WHILE_COND_TUPLE;
}

// The preP stands for previous precedence.
OwnedPtr<Expr> ParserImpl::ParseExpr(const Token& preT, OwnedPtr<Expr> expr, ExprKind ek)
{
    OwnedPtr<Expr> base;
    if (IsConditionExpr(ek) && Seeing(TokenKind::LET)) {
        base = ParseLetPattern(ek);
    } else {
        base = ParseBaseExpr(std::move(expr), ek);
    }

    if (!SeeingExprOperator()) {
        return base;
    }
    auto preP = Precedence(preT.kind);
    while (SeeingExprOperator()) {
        auto tok = GetExprOperator();
        if (newlineSkipped) {
            auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_nl_warning, firstNLPosition);
            builder.AddHint(MakeRange(
                lastNoneNLToken.Begin(), lookahead.Begin() + 1), lastNoneNLToken.Value(), tok.Value());
        }
        auto curP = Precedence(tok.kind);
        if (IsConditionExpr(ek) && Is<LetPatternDestructor>(base.get()) && curP < Precedence(TokenKind::RANGEOP) &&
            tok.kind != TokenKind::AND && tok.kind != TokenKind::OR) {
            return base;
        }
        if (ek == ExprKind::LET_PATTERN && curP < Precedence(TokenKind::RANGEOP)) {
            // according to if-let expression, only binary expression whose operator precedence is not lower than .. is
            // allowed in let pattern initializer. Operators with lower precedence would be considered an end of this
            // let initalizer
            return base;
        }
        if (preP > curP) {
            return base;
        } else if (preP == curP) {
            // Means it is default operator whose precedence is 0.
            // Only 'curT' is assignment token will enter this branch.
            CheckLeftExpression(preT, base, tok);
            // Right associative,
            if (tok.kind != TokenKind::COALESCING && tok.kind != TokenKind::EXP &&
                !(preT.Begin().IsZero() && Precedence(tok.kind) == INVALID_PRECEDENCE)) {
                return base;
            }
        }
        auto res = CheckMacroExprRules(preT, tok, *base);
        // make base expr as another expr's sub expr
        base = MakeOperatorExpr(base, tok);
        if (!res && !base->TestAttr(Attribute::HAS_BROKEN)) {
            base->EnableAttr(Attribute::HAS_BROKEN);
        }
        ParseExprWithRightExprOrType(base, tok, ek);
    }
    return base;
}

void ParserImpl::CheckLeftExpression(const Token& preT, const OwnedPtr<Expr>& base, const Token& tok)
{
    if (preT.Begin().IsZero()) {
        if (!IsLeftValueExpression(base, tok) && !base->TestAttr(Attribute::HAS_BROKEN) &&
            Precedence(tok.kind) == INVALID_PRECEDENCE) {
            DiagInvalidLeftHandExpr(*base, tok);
            base->EnableAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN);
        }
        if (base->astKind == ASTKind::TUPLE_LIT && tok.kind != TokenKind::ASSIGN) {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_cannot_operator_a_tuple, tok, tok.Value());
        }
    } else if (IsNoneAssociative(tok) && !base->TestAttr(Attribute::HAS_BROKEN)) {
        DiagNoneAssociativeOp(preT, tok);
        base->EnableAttr(Attribute::HAS_BROKEN);
    }
}

void ParserImpl::ParseExprWithRightExprOrType(OwnedPtr<Expr>& base, const Token& tok, ExprKind ek)
{
    ChainScope cs(*this, base.get());
    SkipExprOperator();
    if (base->astKind == ASTKind::IS_EXPR || base->astKind == ASTKind::AS_EXPR) {
        OwnedPtr<Type> type;
        if (!SeeingAny(GetTypeFirst()) && !SeeingContextualKeyword()) {
            DiagExpectedTypeNameAfterAs(tok);
            type = MakeInvalid<Type>(lastToken.End());
            base->EnableAttr(Attribute::HAS_BROKEN);
        } else {
            type = ParseType();
        }
        base->end = type->end;
        base->astKind == ASTKind::IS_EXPR ? StaticAs<ASTKind::IS_EXPR>(base.get())->isType = std::move(type)
                                          : StaticAs<ASTKind::AS_EXPR>(base.get())->asType = std::move(type);
    } else {
        auto rExpr = ParseExpr(tok, nullptr, ek);

        auto res = CheckMacroExprRules(tok, Token{TokenKind::DOT}, *rExpr);
        if (!res || rExpr->TestAttr(Attribute::HAS_BROKEN) || rExpr->TestAttr(Attribute::IS_BROKEN)) {
            base->EnableAttr(Attribute::HAS_BROKEN);
        }
        RegisterRightExpr(base, std::move(rExpr));
        if ((IsCompoundAssign(tok.kind) || tok.kind == TokenKind::ASSIGN) && base->hasQuestSuffix) {
            OwnedPtr<OptionalChainExpr> optExpr = MakeOwned<OptionalChainExpr>();
            optExpr->begin = base->begin;
            optExpr->end = base->end;
            optExpr->expr = std::move(base);
            base = std::move(optExpr);
        }
    }
}

bool ParserImpl::IsLeftValueExpression(const OwnedPtr<Expr>& expr, const Token& tok)
{
    /*
     * leftValueExpression
     *      : Identifier
     *      | leftAuxExpression QUEST? assignableSuffix
     *      | tupleLeftValueExpression
     *      | WILDCARD
     *      ;
     * assignableSuffix
     *      : fieldAccess
     *      | indexAccess
     *      ;
     */
    switch (expr->astKind) {
        case ASTKind::REF_EXPR:
        case ASTKind::REF_TYPE:
        case ASTKind::WILDCARD_PATTERN:
        case ASTKind::WILDCARD_EXPR:
        case ASTKind::OPTIONAL_CHAIN_EXPR:
            return true;
        case ASTKind::TUPLE_LIT: {
            auto& children = StaticAs<ASTKind::TUPLE_LIT>(expr.get())->children;
            for (auto& child : children) {
                if (!IsLeftValueExpression(child, tok)) {
                    // to avoid duplicate errors:
                    // if child's astKind is TupleLit，then never come here,
                    // because IsLeftValueExpression(child, tok) will always return true.
                    DiagInvalidLeftHandExpr(*child, tok);
                }
            }
            return true;
        }
        case ASTKind::MEMBER_ACCESS:
            return IsLeftAuxExpression(StaticAs<ASTKind::MEMBER_ACCESS>(expr.get())->baseExpr, tok);
        case ASTKind::SUBSCRIPT_EXPR:
            return IsLeftAuxExpression(StaticAs<ASTKind::SUBSCRIPT_EXPR>(expr.get())->baseExpr, tok);
        default:
            return false;
    }
}

bool ParserImpl::IsLeftAuxExpression(const OwnedPtr<Expr>& expr, const Token& tok)
{
    /*
     * leftAuxExpression
     *      : Identifier (NL* typeArguments)?
     *      | thisSuperExpression
     *      | leftAuxExpression QUEST? NL* DOT NL* Identifier (NL* typeArguments)?
     *      | leftAuxExpression QUEST? callSuffix
     *      | leftAuxExpression QUEST? indexAccess
     *      ;
     */
    switch (expr->astKind) {
        case ASTKind::REF_EXPR:
        case ASTKind::REF_TYPE:
        case ASTKind::PRIMITIVE_TYPE_EXPR:
            return true;
        case ASTKind::OPTIONAL_EXPR:
            return IsLeftAuxExpression(StaticAs<ASTKind::OPTIONAL_EXPR>(expr.get())->baseExpr, tok);
        case ASTKind::MEMBER_ACCESS:
            return IsLeftAuxExpression(StaticAs<ASTKind::MEMBER_ACCESS>(expr.get())->baseExpr, tok);
        case ASTKind::CALL_EXPR:
            return IsLeftAuxExpression(StaticAs<ASTKind::CALL_EXPR>(expr.get())->baseFunc, tok);
        case ASTKind::SUBSCRIPT_EXPR:
            return IsLeftAuxExpression(StaticAs<ASTKind::SUBSCRIPT_EXPR>(expr.get())->baseExpr, tok);
        default:
            return false;
    }
}

void ParserImpl::ParseIncOrDec(OwnedPtr<AST::Expr>& baseExpr)
{
    while (SeeingAny({TokenKind::INCR, TokenKind::DECR}) && !newlineSkipped) {
        std::vector<ASTKind> validSuffix{ASTKind::REF_EXPR, ASTKind::MEMBER_ACCESS, ASTKind::SUBSCRIPT_EXPR};
        if (!Utils::In(baseExpr->astKind, validSuffix) && !baseExpr->TestAttr(Attribute::HAS_BROKEN) &&
            !baseExpr->TestAttr(Attribute::IS_BROKEN)) {
            baseExpr->EnableAttr(Attribute::HAS_BROKEN);
            DiagInvalidIncreExpr(*baseExpr);
        }
        OwnedPtr<IncOrDecExpr> incOrDec = MakeOwned<IncOrDecExpr>();
        SpreadAttrAndConsume(baseExpr.get(), incOrDec.get(), {});
        incOrDec->begin = baseExpr->begin;
        incOrDec->expr = std::move(baseExpr);
        incOrDec->op = Peek().kind;
        Next();
        if (incOrDec->expr) {
            incOrDec->begin = incOrDec->expr->begin;
        }
        incOrDec->operatorPos = lookahead.Begin();
        incOrDec->end = lookahead.Begin();
        incOrDec->end.column += INCREMENT_OP_LENGTH;
        baseExpr = std::move(incOrDec);
    }
}
 
static ExprKind RemoveParsingIfCondExpr(ExprKind ek)
{
    switch (ek) {
        case ExprKind::EXPR_IN_IF_COND_TUPLE:
        case ExprKind::EXPR_IN_WHILE_COND_TUPLE:
        case ExprKind::IF_COND_EXPR:
        case ExprKind::WHILE_COND_EXPR:
            return ExprKind::ALL;
        default:
            return ek;
    }
}

OwnedPtr<Expr> ParserImpl::ParseUnaryExpr(ExprKind ek)
{
    // For prefixUnaryExpression check: No newline between prefix token and expression.
    if (Seeing({TokenKind::SUB, TokenKind::NL}, false) || Seeing({TokenKind::NOT, TokenKind::NL}, false)) {
        DiagExpectedNoNewLine();
        return MakeOwned<InvalidExpr>(lookahead.Begin());
    }
    OwnedPtr<UnaryExpr> ret = MakeOwned<UnaryExpr>();
    ChainScope cs(*this, ret.get());
    Token opToken = lookahead;
    ret->op = opToken.kind;
    ret->begin = opToken.Begin();
    ret->operatorPos = opToken.Begin();
    Next();
    // atomicCondition cannot be nested within unary expr (e.g. if (!(let ... <- ...)))
    auto tmpExpr = ParseBaseExpr(nullptr, RemoveParsingIfCondExpr(ek));
    // "-" + LitConstExpr is also LitConstExpr
    if (opToken.kind == TokenKind::SUB) {
        if (auto le = AST::As<ASTKind::LIT_CONST_EXPR>(tmpExpr.get()); le &&
            (le->kind == LitConstKind::INTEGER || le->kind == LitConstKind::FLOAT) &&
            (!le->stringValue.empty() && le->stringValue[0] != '-')) {
            le->stringValue = '-' + le->stringValue;
            le->rawString = le->stringValue;
            le->begin = opToken.Begin();
            return tmpExpr;
        }
    }
    ret->expr = std::move(tmpExpr);
    ret->end = ret->expr->end;
    return ret;
}

OwnedPtr<Expr> ParserImpl::ParseBaseExpr(OwnedPtr<AST::Expr> expr, ExprKind ek)
{
    if (SeeingAny({TokenKind::SUB, TokenKind::NOT}) && !expr) {
        return ParseUnaryExpr(ek);
    }
    OwnedPtr<Expr> baseExpr;
    if (expr) {
        baseExpr = std::move(expr);
    } else {
        baseExpr = ParseAtom(ek);
    }

    // May change astKind of baseExpr in this function.
    ParseBaseExprPostfix(baseExpr, ek);

    // May change astKind of baseExpr in this function.
    ParseIncOrDec(baseExpr);

    if (IsNeedToCreateOptionalChain(GetExprOperator().kind, *baseExpr)) {
        OwnedPtr<OptionalChainExpr> optExpr = MakeOwned<OptionalChainExpr>();
        optExpr->begin = baseExpr->begin;
        optExpr->end = baseExpr->end;
        optExpr->expr = std::move(baseExpr);
        return optExpr;
    }
    return baseExpr;
}

void ParserImpl::ParseQuestSuffixExpr(OwnedPtr<Expr>& expr)
{
    if (Skip(TokenKind::QUEST)) {
        auto pos = lastToken.Begin();
        OwnedPtr<OptionalExpr> ret = MakeOwned<OptionalExpr>();
        ret->begin = expr->begin;
        ret->end = expr->end;
        ret->questPos = pos;
        ret->baseExpr = std::move(expr);
        expr = std::move(ret);
    }
}

OwnedPtr<Expr> ParserImpl::ParseOptionalExpr(
    const Position questPos, OwnedPtr<Expr> baseExpr, SuffixKind suffix) const
{
    if (suffix == SuffixKind::QUEST) {
        OwnedPtr<OptionalExpr> ret = MakeOwned<OptionalExpr>();
        ret->questPos = questPos;
        ret->begin = baseExpr->begin;
        ret->end = baseExpr->end;
        ret->baseExpr = std::move(baseExpr);
        return ret;
    }
    return baseExpr;
}

OwnedPtr<TrailingClosureExpr> ParserImpl::ParseTrailingClosureExpr(OwnedPtr<Expr> baseExpr)
{
    OwnedPtr<TrailingClosureExpr> ret = MakeOwned<TrailingClosureExpr>();
    std::vector<OwnedPtr<Annotation>> annos;
    ParseAnnotations(annos);

    ret->leftLambda = lastToken.Begin();
    Next();
    ret->lambda = ParseLambdaExprWithTrailingClosure();
    ret->rightLambda = lastToken.Begin();
    // Overflow lambdaExpr.
    for (auto& it : annos) {
        if (it->kind == AnnotationKind::NUMERIC_OVERFLOW) {
            ret->lambda->EnableAttr(Attribute::NUMERIC_OVERFLOW);
            ret->lambda->overflowStrategy = it->overflowStrategy;
            break;
        }
    }
    CheckBaseOfTrailingClosureExpr(baseExpr);
    ret->expr = std::move(baseExpr);
    ret->begin = ret->expr->begin;
    ret->end = ret->lambda->end;
    return ret;
}

void ParserImpl::CheckBaseOfTrailingClosureExpr(const OwnedPtr<Expr>& baseExpr)
{
    if (baseExpr->TestAttr(Attribute::IS_BROKEN)) {
        return;
    }

    // baseExpr can only be:
    // 1. RefExpr
    // 2. MemberAccess
    // 3. CallExpr
    // 4. VArrayExpr
    // 5. OptionalExpr
    // 6. MacroExpandExpr

    // if baseExpr is CallExpr, then the CallExpr's baseFunc must be RefExpr or MemberAccess
    // RefExpr and MemberAccess have common parent class: NameReferenceExpr
    if (Utils::NotIn(baseExpr->astKind,
                     {ASTKind::MACRO_EXPAND_EXPR, ASTKind::ARRAY_EXPR, ASTKind::OPTIONAL_EXPR, ASTKind::CALL_EXPR,
                     ASTKind::REF_EXPR, ASTKind::MEMBER_ACCESS})) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_trailing_closure_only_follow_name, *baseExpr);
    }
    if (baseExpr->astKind == ASTKind::CALL_EXPR) {
        auto callExpr = StaticAs<ASTKind::CALL_EXPR>(baseExpr.get());
        if (callExpr->baseFunc->TestAttr(Attribute::IS_BROKEN)) {
            return;
        }
        if (Utils::NotIn(
            callExpr->baseFunc->astKind, {ASTKind::MEMBER_ACCESS, ASTKind::REF_EXPR, ASTKind::OPTIONAL_EXPR})) {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_trailing_closure_only_follow_name, *baseExpr);
        }
    }
}

OwnedPtr<Expr> ParserImpl::ParseSubscriptExpr(OwnedPtr<Expr> baseExpr)
{
    auto ret = MakeOwned<SubscriptExpr>();
    ChainScope cs(*this, ret.get());
    ret->begin = baseExpr->begin;
    ret->leftParenPos = lookahead.Begin();
    ret->baseExpr = std::move(baseExpr);
    do {
        if (lastToken.kind == TokenKind::COMMA) {
            ret->commaPos.push_back(lastToken.Begin());
        }
        ret->indexExprs.emplace_back(ParseExpr(ExprKind::INDEX_EXPR));
    } while (Skip(TokenKind::COMMA));
    if (!Skip(TokenKind::RSQUARE)) {
        DiagExpectedRightDelimiter("[", ret->leftParenPos);
        ret->EnableAttr(Attribute::HAS_BROKEN);
    }
    ret->rightParenPos = lookahead.Begin();
    ret->end = lastToken.End();
    return ret;
}

static void GetMemberAccessEndPos(const OwnedPtr<MemberAccess>& ma)
{
    // In LSP case, when the identifier is waiting tobe input, and is at end of the file, we need plus one
    // for the column of end position, then the sort order of REF_EXPR and MEMBER_ACCESS will be determined.
    if (ma->field == INVALID_IDENTIFIER) {
        // The filedPos is the dot position, we need shift 2.
        ma->end = ma->field.Begin() + 2;
    } else {
        if (ma->rightAnglePos.IsZero()) {
            ma->end = ma->field.GetRawEndPos();
        } else {
            ma->end = ma->rightAnglePos;
        }
    }
}

OwnedPtr<MemberAccess> ParserImpl::ParseMemberAccess(OwnedPtr<Expr> baseExpr, ExprKind ek)
{
    OwnedPtr<MemberAccess> ret = MakeOwned<MemberAccess>();
    ChainScope cs(*this, ret.get());
    ret->begin = baseExpr->begin;
    ret->baseExpr = std::move(baseExpr);
    ret->dotPos = lastToken.Begin();
    ret->field = ExpectIdentifierWithPos(*ret);
    if (Seeing(TokenKind::LT)) {
        auto leftAnglePos = Peek().Begin();
        ParserScope scope(*this);
        Next();

        // collecting diagnoses in `ParseTypeArguments` and storing these diagnoses to a cache
        diag.Prepare();
        auto [isGenericArgList, typeArguments] = ParseTypeArguments(ek);
        if (isGenericArgList) {
            // parse type success, handle those diagnoses which were stored in the cache
            ret->typeArguments = std::move(typeArguments);
            diag.Commit();
        } else {
            diag.ClearTransaction();
            // if it is like: if a.m<b {} or (a.m < b, c >= d), reset parser.
            scope.ResetParserScope();
            ret->leftAnglePos = INVALID_POSITION;
            ret->rightAnglePos = INVALID_POSITION;
            GetMemberAccessEndPos(ret);
            return ret;
        }
        ret->leftAnglePos = leftAnglePos;
        ret->rightAnglePos = lastToken.Begin();
    }
    GetMemberAccessEndPos(ret);
    return ret;
}

OwnedPtr<Expr> ParserImpl::ParseCallExpr(OwnedPtr<Expr> baseExpr)
{
    OwnedPtr<CallExpr> ret = MakeOwned<CallExpr>();
    ChainScope cs(*this, ret.get());
    ret->begin = baseExpr->begin;
    ret->leftParenPos = lastToken.Begin();
    baseExpr->isBaseFunc = true;
    ret->baseFunc = std::move(baseExpr);
    ParseZeroOrMoreSepTrailing(
        [&ret](const Position& pos) {
            ret->args.back()->commaPos = pos;
        },
        [this, &ret]() {
            ret->args.emplace_back(ParseFuncArg());
        }, TokenKind::RPAREN);
    if (!Skip(TokenKind::RPAREN) && !ret->TestAttr(Attribute::HAS_BROKEN)) {
        ret->EnableAttr(Attribute::HAS_BROKEN);
        DiagExpectedRightDelimiter("(", ret->leftParenPos);
    }
    ret->rightParenPos = lastToken.Begin();
    ret->end = lastToken.End();
    return ret;
}

AST::SuffixKind ParserImpl::ParseSuffix(OwnedPtr<Expr>& baseExpr)
{
    if (newlineSkipped) {
        return SuffixKind::NONE;
    }
    // For BNF:
    // itemAfterQuest
    //     : DOT identifier (NL* typeArguments)?
    //     | callSuffix
    //     | indexAccess
    //     | trailingLambdaExpression
    if (Seeing({TokenKind::QUEST, TokenKind::DOT}) || Seeing({TokenKind::QUEST, TokenKind::LSQUARE}, false) ||
        Seeing({TokenKind::QUEST, TokenKind::LPAREN}, false) || Seeing({TokenKind::QUEST, TokenKind::LCURL}, false)) {
        if (baseExpr->astKind == ASTKind::WILDCARD_EXPR) {
            auto builder =
                ParseDiagnoseRefactor(DiagKindRefactor::parse_unexpected_expected_found, *baseExpr.get(), "_ wildcard");
            builder.AddMainHintArguments("expression", "wildcard");
        }
        ParseQuestSuffixExpr(baseExpr);
        return SuffixKind::QUEST;
    } else if (Seeing(TokenKind::QUEST) && !SeeingCombinator({TokenKind::QUEST, TokenKind::QUEST})) {
        // a single QUEST as operator is grammatically incorrect. e.g. a?b
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_character_after, lookahead.Begin(),
            "'.', '(', '[', '{' or '?'", lookahead.Value());
    }
    return SuffixKind::NONE;
}

void ParserImpl::ParseBaseExprPostfix(OwnedPtr<AST::Expr>& baseExpr, ExprKind ek)
{
    // The check of jump_expr could put in while below.
    if ((newlineSkipped && !Seeing(TokenKind::DOT)) || baseExpr->astKind == ASTKind::JUMP_EXPR) {
        return;
    }
    auto suffix = ParseSuffix(baseExpr);
    bool hasQuestSuffix = (suffix == SuffixKind::QUEST);
    while (SeeingAny({TokenKind::LSQUARE, TokenKind::DOT, TokenKind::LPAREN}) || SeeingTrailingClosure(baseExpr)) {
        // Subscript expr (a[1], b[x]).
        if (!newlineSkipped && Skip(TokenKind::LSQUARE)) {
            auto ret = ParseSubscriptExpr(std::move(baseExpr));
            baseExpr = std::move(ret);
            baseExpr->end = lastToken.End();
            suffix = ParseSuffix(baseExpr);
            hasQuestSuffix = suffix == SuffixKind::QUEST ? true : hasQuestSuffix;
        }
        if (!Seeing(TokenKind::DOT) && newlineSkipped) {
            break;
        }
        // Member access (obj.x, obj.foo()).
        if (Skip(TokenKind::DOT)) {
            auto ret = ParseMemberAccess(std::move(baseExpr), ek);
            baseExpr = std::move(ret);
            suffix = ParseSuffix(baseExpr);
            hasQuestSuffix = suffix == SuffixKind::QUEST ? true : hasQuestSuffix;
        }
        // Function call.
        if (!newlineSkipped && Skip(TokenKind::LPAREN)) {
            auto ret = ParseCallExpr(std::move(baseExpr));
            baseExpr = std::move(ret);
            suffix = ParseSuffix(baseExpr);
            hasQuestSuffix = suffix == SuffixKind::QUEST ? true : hasQuestSuffix;
        }
        // TrailingClosure.
        if (SeeingTrailingClosure(baseExpr) && !newlineSkipped) {
            auto ret = ParseTrailingClosureExpr(std::move(baseExpr));
            baseExpr = std::move(ret);
            suffix = ParseSuffix(baseExpr);
            hasQuestSuffix = suffix == SuffixKind::QUEST ? true : hasQuestSuffix;
        }
    }
    if (hasQuestSuffix) {
        baseExpr->hasQuestSuffix = true;
    }
}
