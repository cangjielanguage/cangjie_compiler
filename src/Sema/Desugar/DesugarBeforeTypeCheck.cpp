// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Desugar functions used before typecheck step.
 */

#include "cangjie/Sema/Desugar.h"

#include "DesugarInTypeCheck.h"
#include "DesugarMacro.h"
#include "TypeCheckUtil.h"

#include "../Plugin/APILevelVersion.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/Utils.h"

namespace Cangjie {
using namespace AST;
using namespace TypeCheckUtil;

namespace {
/**
 * Desugar the SynchronizedExpr as a tryExpr. Add `lock()` call to the beginning of try expression.
 * Add `unlock()` call to the finally block.
 */
void DesugarSynchronizedExpr(SynchronizedExpr& se)
{
    if (se.TestAttr(Attribute::IS_BROKEN) || se.desugarExpr != nullptr) {
        return;
    }
    CJC_ASSERT(se.mutex);
    if (se.mutex->IsInvalid()) {
        return;
    }
    OwnedPtr<Block> block = MakeOwned<Block>();
    auto mtxVar = CreateVarDecl(".mtx", ASTCloner::Clone(se.mutex.get()));
    block->body.push_back(std::move(mtxVar));

    OwnedPtr<TryExpr> tryExpr = MakeOwned<TryExpr>();
    tryExpr->tryPos = se.begin;
    tryExpr->begin = se.begin;
    tryExpr->finallyBlock = MakeOwned<Block>();
    tryExpr->finallyPos = se.end;

    // Add `lock` call.
    auto mtxRef = CreateRefExpr(".mtx");
    mtxRef->EnableAttr(Attribute::COMPILER_ADD);
    auto mtxLockAccess = CreateMemberAccess(std::move(mtxRef), "lock");
    CopyBasicInfo(se.mutex.get(), mtxLockAccess.get());
    auto lockCall = CreateCallExpr(std::move(mtxLockAccess), {});
    block->body.push_back(std::move(lockCall));

    // Move synchronized expression's body.
    tryExpr->tryBlock = std::move(se.body);

    // Add `unlock` call to the `finally` block.
    auto mtxRef2 = CreateRefExpr(".mtx");
    mtxRef2->EnableAttr(Attribute::COMPILER_ADD);
    auto mtxUnlockAccess = CreateMemberAccess(std::move(mtxRef2), "unlock");
    CopyBasicInfo(se.mutex.get(), mtxUnlockAccess.get());
    auto unlockCall = CreateCallExpr(std::move(mtxUnlockAccess), {});

    tryExpr->finallyBlock->body.push_back(std::move(unlockCall));
    tryExpr->isDesugaredFromSyncBlock = true;
    block->body.push_back(std::move(tryExpr));
    AddCurFile(*block, se.curFile);
    se.desugarExpr = std::move(block);
}

// Find the OptionalExpr in the left field of expr.
// For example, the GetOptionalExpr(`a?.b.c?.d`) = `a?.b.c?`.
// The selector of the desugared MatchExpr is based on it.
Ptr<OptionalExpr> GetOptionalExpr(Expr& expr)
{
    switch (expr.astKind) {
        case ASTKind::OPTIONAL_EXPR: {
            return StaticAs<ASTKind::OPTIONAL_EXPR>(&expr);
        }
        case ASTKind::MEMBER_ACCESS: {
            auto ma = StaticAs<ASTKind::MEMBER_ACCESS>(&expr);
            CJC_NULLPTR_CHECK(ma->baseExpr);
            return GetOptionalExpr(*ma->baseExpr);
        }
        case ASTKind::CALL_EXPR: {
            auto ce = StaticAs<ASTKind::CALL_EXPR>(&expr);
            CJC_NULLPTR_CHECK(ce->baseFunc);
            return GetOptionalExpr(*ce->baseFunc);
        }
        case ASTKind::TRAIL_CLOSURE_EXPR: {
            auto tce = StaticAs<ASTKind::TRAIL_CLOSURE_EXPR>(&expr);
            CJC_NULLPTR_CHECK(tce->expr);
            return GetOptionalExpr(*tce->expr);
        }
        case ASTKind::SUBSCRIPT_EXPR: {
            auto se = StaticAs<ASTKind::SUBSCRIPT_EXPR>(&expr);
            CJC_NULLPTR_CHECK(se->baseExpr);
            return GetOptionalExpr(*se->baseExpr);
        }
        case ASTKind::ASSIGN_EXPR: {
            auto ae = StaticAs<ASTKind::ASSIGN_EXPR>(&expr);
            CJC_NULLPTR_CHECK(ae->leftValue);
            return GetOptionalExpr(*ae->leftValue);
        }
        case ASTKind::INC_OR_DEC_EXPR: {
            auto ide = StaticAs<ASTKind::INC_OR_DEC_EXPR>(&expr);
            CJC_NULLPTR_CHECK(ide->expr);
            return GetOptionalExpr(*ide->expr);
        }
        default: {
            return nullptr;
        }
    }
}

// Replace the OptionalExpr with a RefExpr `v`.
// For example, CreateSelector(`a?.b.c?.d`) = `v.d`, CreateSelector(`a?.b.c`) = `v.b.c`.
// This function creates the selectors of nested MatchExprs and the innermost exprOrDecls of case Some.
OwnedPtr<Expr> CreateSelector(OwnedPtr<Expr>&& expr)
{
    CJC_NULLPTR_CHECK(expr);
    switch (expr->astKind) {
        case ASTKind::OPTIONAL_EXPR: {
            auto oe = StaticAs<ASTKind::OPTIONAL_EXPR>(expr.get());
            auto re = CreateRefExpr(V_COMPILER);
            CopyBasicInfo(oe->baseExpr.get(), re.get());
            re->begin = oe->questPos;
            re->end = oe->questPos + 1;
            return re;
        }
        case ASTKind::MEMBER_ACCESS: {
            auto ma = StaticAs<ASTKind::MEMBER_ACCESS>(expr.get());
            ma->baseExpr = CreateSelector(std::move(ma->baseExpr));
            return std::move(expr);
        }
        case ASTKind::CALL_EXPR: {
            auto ce = StaticAs<ASTKind::CALL_EXPR>(expr.get());
            ce->baseFunc = CreateSelector(std::move(ce->baseFunc));
            return std::move(expr);
        }
        case ASTKind::TRAIL_CLOSURE_EXPR: {
            auto tce = StaticAs<ASTKind::TRAIL_CLOSURE_EXPR>(expr.get());
            tce->expr = CreateSelector(std::move(tce->expr));
            return std::move(expr);
        }
        case ASTKind::SUBSCRIPT_EXPR: {
            auto se = StaticAs<ASTKind::SUBSCRIPT_EXPR>(expr.get());
            se->baseExpr = CreateSelector(std::move(se->baseExpr));
            return std::move(expr);
        }
        case ASTKind::ASSIGN_EXPR: {
            auto ae = StaticAs<ASTKind::ASSIGN_EXPR>(expr.get());
            ae->leftValue = CreateSelector(std::move(ae->leftValue));
            return std::move(expr);
        }
        case ASTKind::INC_OR_DEC_EXPR: {
            auto ide = StaticAs<ASTKind::INC_OR_DEC_EXPR>(expr.get());
            ide->expr = CreateSelector(std::move(ide->expr));
            return std::move(expr);
        }
        default: {
            return std::move(expr);
        }
    }
}

/**
 * Create a MatchExpr and desugar recursively.
 *
 * @param expr is the `Expr` to be desugared.
 * @param caseExpr is the accumulator of the recursion, i.e., the `exprOrDecls` of the `Some` case
 *                 in the desugared `MatchExpr`. In the base case of the recursion, it is returned
 *                 directly as the desugared `Expr`.
 * @param isAssign indicates whether the optional chain is an `AssignExpr'. If it is true, the desugar
 *                 result is of type `Unit`, i.e., a `MatchExpr` whose `None` case is `()`. Otherwise,
 *                 the desugar result is of type `Option<T>`, i.e., a `MatchExpr` whose `None` case is
 *                 `None`.
 */
OwnedPtr<Expr> DesugarOptionalChainWithMatchCase(Expr& expr, OwnedPtr<Expr>&& caseExpr, bool isAssign)
{
    Ptr<OptionalExpr> opt = GetOptionalExpr(expr);
    if (opt == nullptr) {
        return std::move(caseExpr);
    } else {
        CJC_NULLPTR_CHECK(opt->baseExpr);
        // Create a `MatchExpr`:
        // match (CreateSelector(opt->baseExpr)) {
        //     case Some(v) => caseExpr
        //     case None => caseNoneExpr
        // }
        // where `caseNoneExpr` is `()` if `isAssign` is true, otherwise, it is `None`.
        std::vector<OwnedPtr<MatchCase>> matchCases;
        auto varPattern = CreateVarPattern(V_COMPILER);
        CopyBasicInfo(opt->baseExpr.get(), varPattern->varDecl.get());
        auto somePattern = MakeOwnedNode<EnumPattern>();
        somePattern->constructor = CreateRefExprInCore(OPTION_VALUE_CTOR);
        somePattern->patterns.emplace_back(std::move(varPattern));
        matchCases.emplace_back(CreateMatchCase(std::move(somePattern), std::move(caseExpr)));
        auto nonePattern = MakeOwnedNode<EnumPattern>();
        nonePattern->constructor = CreateRefExprInCore(OPTION_NONE_CTOR);
        auto caseNoneExpr = isAssign ? CreateUnitExpr() : CreateRefExprInCore(OPTION_NONE_CTOR);
        matchCases.emplace_back(CreateMatchCase(std::move(nonePattern), std::move(caseNoneExpr)));
        auto matchExpr = CreateMatchExpr(CreateSelector(ASTCloner::Clone(opt->baseExpr.get(), SetIsClonedSourceCode)),
            std::move(matchCases), Ty::GetInitialTy(), Expr::SugarKind::QUEST);
        CopyBasicInfo(opt->baseExpr.get(), matchExpr.get());
        matchExpr->EnableAttr(Attribute::IS_CLONED_SOURCE_CODE);
        return DesugarOptionalChainWithMatchCase(*opt->baseExpr, std::move(matchExpr), isAssign);
    }
}

OwnedPtr<CallExpr> CreateOptionalChainCallSome(OwnedPtr<Expr>&& expr)
{
    OwnedPtr<RefExpr> someRef = CreateRefExprInCore(OPTION_VALUE_CTOR);
    CopyBasicInfo(expr.get(), someRef.get());
    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(CreateSelector(std::move(expr))));
    OwnedPtr<CallExpr> callSome = CreateCallExpr(std::move(someRef), std::move(args));
    CopyBasicInfo(callSome->args[0].get(), callSome.get());
    return callSome;
}

/**
 * Desugar OptionalChainExpr to MatchExpr
 * *************** before desugar ****************
 * a?.b.c?.d
 * *************** after desugar ****************
 * match (a) {
 *     case Some(v) => match (v.b.c) {
 *                         case Some(v) => Some(v.d)
 *                         case None => None
 *                     }
 *     case None => None
 * }
 * *************** before desugar ****************
 * a?.b.c?.d = x
 * *************** after desugar ****************
 * match (a) {
 *     case Some(v) => match (v.b.c) {
 *                         case Some(v) => v.d = x
 *                         case None => ()
 *                     }
 *     case None => ()
 * }
 * */
void DesugarOptionalChainExpr(OptionalChainExpr& oce)
{
    if (oce.desugarExpr != nullptr) {
        return;
    }
    CJC_NULLPTR_CHECK(oce.expr);
    auto expr = ASTCloner::Clone(oce.expr.get(), SetIsClonedSourceCode);
    oce.desugarExpr = expr->astKind == ASTKind::ASSIGN_EXPR || expr->astKind == ASTKind::INC_OR_DEC_EXPR
        ? DesugarOptionalChainWithMatchCase(*expr, CreateSelector(std::move(oce.expr)), true)
        : DesugarOptionalChainWithMatchCase(*expr, CreateOptionalChainCallSome(std::move(oce.expr)), false);
    CopyBasicInfo(expr.get(), oce.desugarExpr.get());
    AddCurFile(*oce.desugarExpr, oce.curFile);
}

// Get OptionalChainExpr appearing in leftTupleLit.
Ptr<OptionalChainExpr> GetOptionalChainExprForLeftVale(Expr& leftValue)
{
    Ptr<Expr> expr = &leftValue;
    while (expr->astKind == ASTKind::PAREN_EXPR) {
        CJC_NULLPTR_CHECK(StaticAs<ASTKind::PAREN_EXPR>(expr)->expr.get());
        expr = StaticAs<ASTKind::PAREN_EXPR>(expr)->expr.get();
    }
    if (expr->astKind == ASTKind::OPTIONAL_CHAIN_EXPR) {
        return StaticAs<ASTKind::OPTIONAL_CHAIN_EXPR>(expr);
    }
    return nullptr;
}

OwnedPtr<AssignExpr> CreateCompoundAssignExpr(OwnedPtr<Expr> left, OwnedPtr<Expr> right, TokenKind op)
{
    auto ae = CreateAssignExpr(std::move(left), std::move(right));
    ae->op = op;
    ae->isCompound = true;
    return ae;
}

/**
 * *************** before desugar ****************
 * a++
 * *************** after desugar ****************
 * a += 1
 * */
void DesugarIncOrDecExpr(IncOrDecExpr& ide)
{
    if (ide.desugarExpr != nullptr) {
        return;
    }
    CJC_NULLPTR_CHECK(ide.expr);
    auto one = CreateLitConstExpr(LitConstKind::INTEGER, "1", Ty::GetInitialTy());
    CopyBasicInfo(&ide, one.get());
    auto ae = CreateCompoundAssignExpr(
        std::move(ide.expr), std::move(one), ide.op == TokenKind::INCR ? TokenKind::ADD_ASSIGN : TokenKind::SUB_ASSIGN);
    CopyBasicInfo(&ide, ae.get());
    ide.desugarExpr = std::move(ae);
}

void DesugarAssignExprRecursively(const TupleLit& leftValues, Expr& rightExprs, std::vector<OwnedPtr<Node>>& nodes)
{
    uint64_t indexOfRightExpr = 0;
    auto varDecl = CreateTmpVarDecl(nullptr, &rightExprs);
    std::string identifier = varDecl->identifier;
    nodes.emplace_back(std::move(varDecl));
    for (auto& leftValue : leftValues.children) {
        auto tempRefExpr = CreateRefExpr(identifier);
        tempRefExpr->EnableAttr(Attribute::COMPILER_ADD);
        auto rightExpr = MakeOwned<SubscriptExpr>();
        rightExpr->baseExpr = std::move(tempRefExpr);
        rightExpr->indexExprs.emplace_back(
            MakeOwned<LitConstExpr>(LitConstKind::INTEGER, std::to_string(indexOfRightExpr)));
        rightExpr->begin = rightExprs.begin;
        rightExpr->end = rightExprs.end;
        rightExpr->EnableAttr(Attribute::COMPILER_ADD, Attribute::IMPLICIT_ADD);
        rightExpr->sourceExpr = &rightExprs;
        if (leftValue->astKind == ASTKind::TUPLE_LIT) {
            DesugarAssignExprRecursively(*StaticAs<ASTKind::TUPLE_LIT>(leftValue.get()), *rightExpr, nodes);
        } else if (Ptr<OptionalChainExpr> leftIsOptionalChain = GetOptionalChainExprForLeftVale(*leftValue)) {
            auto optionalChainExpr = MakeOwned<OptionalChainExpr>();
            optionalChainExpr->EnableAttr(Attribute::COMPILER_ADD);
            optionalChainExpr->expr = CreateAssignExpr(
                ASTCloner::Clone(leftIsOptionalChain->expr.get(), SetIsClonedSourceCode), std::move(rightExpr));
            CopyBasicInfo(leftIsOptionalChain, optionalChainExpr->expr.get());
            nodes.emplace_back(std::move(optionalChainExpr));
        } else {
            auto tmpAssignExpr =
                CreateAssignExpr(ASTCloner::Clone(leftValue.get(), SetIsClonedSourceCode), std::move(rightExpr));
            tmpAssignExpr->begin = leftValue->begin;
            tmpAssignExpr->end = leftValue->end;
            nodes.emplace_back(std::move(tmpAssignExpr));
        }
        ++indexOfRightExpr;
    }
}

/**
 * Desugar multiple assignment expression to a series of single assignment expressions.
 * *************** before desugar ****************
 * var (a, b, c, d) = (0, 0, 0, 0)
 * var f = { => ((1, 2), (3, 4))}
 * ((a, b), (c, d)) = f()
 * *************** after desugar ****************
 * {
 * var $tmp1 = f()
 * var $tmp2 = $tmp1[0]
 * a = $tmp2[0]
 * b = $tmp2[1]
 * var $tmp3 = $tmp0[1]
 * c = $tmp3[0]
 * d = $tmp3[1]
 * } //  wrap with a block.
 */
void DesugarAssignExpr(AssignExpr& ae)
{
    CJC_ASSERT(ae.leftValue);
    if (ae.leftValue->astKind != ASTKind::TUPLE_LIT) {
        return;
    }
    std::vector<OwnedPtr<Node>> nodes;
    DesugarAssignExprRecursively(*StaticAs<ASTKind::TUPLE_LIT>(ae.leftValue.get()), *ae.rightExpr, nodes);
    ae.desugarExpr = CreateBlock(std::move(nodes));
}

void DesugarOptionType(OptionType& optionType)
{
    unsigned int questNum = optionType.questNum;
    // Create new RefType whose 'name' is 'Option' and 'typeArgs' is the 'componentType' of the optionType
    auto refType = CreateRefTypeInCore(STD_LIB_OPTION);
    std::vector<OwnedPtr<Type>> typeArgs;
    typeArgs.push_back(ASTCloner::Clone(optionType.componentType.get()));
    refType->typeArguments = std::move(typeArgs);
    // Handle nested option types
    for (unsigned int i = 1; i < questNum; i++) {
        std::vector<OwnedPtr<Type>> tempTypeArgs;
        tempTypeArgs.push_back(ASTCloner::Clone(refType.get()));
        refType->typeArguments = std::move(tempTypeArgs);
    }
    CopyBasicInfo(&optionType, refType.get());
    // Set the desugarType of the optionType
    optionType.desugarType = std::move(refType);
}

void DesugarMainDecl(MainDecl& mainDecl)
{
    if (mainDecl.desugarDecl) {
        return; // NOTE: During incremental compilation, this may be set.
    }
    auto funcDecl = MakeOwnedNode<FuncDecl>();
    funcDecl->curFile = mainDecl.curFile;
    funcDecl->begin = mainDecl.begin;
    funcDecl->keywordPos = mainDecl.keywordPos;
    funcDecl->identifier = mainDecl.identifier;
    funcDecl->CloneAttrs(mainDecl);
    funcDecl->overflowStrategy = mainDecl.overflowStrategy;
    funcDecl->funcBody = std::move(mainDecl.funcBody);
    funcDecl->end = funcDecl->funcBody->end;
    funcDecl->toBeCompiled = mainDecl.toBeCompiled;
    funcDecl->comments = std::move(mainDecl.comments);
    funcDecl->rawMangleName = std::move(mainDecl.rawMangleName);
    funcDecl->hash = mainDecl.hash;
    funcDecl->EnableAttr(Attribute::MAIN_ENTRY);
    auto cl = ASTCloner();
    for (auto& anno : mainDecl.annotations) {
        funcDecl->annotations.push_back(cl.Clone(anno.get()));
    }
    mainDecl.desugarDecl = std::move(funcDecl);
}

OwnedPtr<Expr> DesugarTrailClosureAsCall(TrailingClosureExpr& trailingClosure)
{
    // Desugar trailing closure by move.
    std::vector<OwnedPtr<FuncArg>> parameters;
    auto end = trailingClosure.lambda->end;
    parameters.emplace_back(CreateFuncArg(std::move(trailingClosure.lambda)));
    parameters[0]->EnableAttr(Attribute::IMPLICIT_ADD);
    auto callExpr = CreateCallExpr(std::move(trailingClosure.expr), std::move(parameters));
    CopyBasicInfo(&trailingClosure, callExpr.get());
    callExpr->end = end;
    return callExpr;
}

/**
 * Desugar TrailingClosureExpr to CallExpr. For example:
 * *************** before desugar ****************
 * func f(a : Int64, b : Int64, g : (Int64, Int64) -> Int64){
 *       return g(a, b)
 *  }
 *   var t = f(1,2){x, y => x + 2 * y}
 * *************** after desugar ****************
 * var t = f(1,2,{x, y => x + 2 * y}) or var t = f(1,2)({x, y => x + 2 * y})
 * */
void DesugarTrailingClosureExpr(TrailingClosureExpr& trailingClosure)
{
    if (trailingClosure.desugarExpr || !trailingClosure.lambda) {
        return;
    }
    if (auto ae = AST::As<ASTKind::ARRAY_EXPR>(trailingClosure.expr.get()); ae) {
        // If baseExpr of trailing closure is arrayExpr, move lambda as last argument of arrayExpr.
        auto end = trailingClosure.lambda->end;
        (void)ae->args.emplace_back(CreateFuncArg(std::move(trailingClosure.lambda)));
        trailingClosure.desugarExpr = std::move(trailingClosure.expr);
        trailingClosure.desugarExpr->end = end;
    } else if (auto ce = AST::As<ASTKind::CALL_EXPR>(trailingClosure.expr.get()); ce) {
        // If baseExpr of trailing closure is call, the closure is passed as base call expression's last argument.
        ce->args.emplace_back(CreateFuncArg(std::move(trailingClosure.lambda)));
        ce->args.back()->EnableAttr(Attribute::IMPLICIT_ADD);
        trailingClosure.desugarExpr = std::move(trailingClosure.expr);
        trailingClosure.desugarExpr->end = ce->args.back()->end;
    } else {
        trailingClosure.desugarExpr = DesugarTrailClosureAsCall(trailingClosure);
    }
    CopyBasicInfo(&trailingClosure, trailingClosure.desugarExpr.get());
}

const std::string LEVEL_IDENTGIFIER = "level";
const std::string SYSCAP_IDENTGIFIER = "syscap";
// For level check:
const std::string DEVICE_INFO = "DeviceInfo";
const std::string SDK_API_VERSION = "sdkApiVersion";
const std::string API_AVAILABLE = "apiAvailable";
// For syscap check:
const std::string CANIUSE_IDENTIFIER = "canIUse";
// SDK 26 is the minimum version that introduces the apiAvailable(...) runtime check
// (from ohos.base, per issue #824). For compatibleSDKVersion >= 26 the desugar path
// emits apiAvailable(...); for < 26 it falls back to DeviceInfo.sdkApiVersion >= N,
// because apiAvailable does not exist in older SDKs.
// See DesugarIfAvailableLevelCondition for the full 5-way desugar matrix.
constexpr uint32_t IFAVAILABLE_RUNTIME_APILEVEL_MAJOR = 26; // apiAvailable introduced at SDK 26

/*
 * Build a `DeviceInfo.sdkApiVersion >= <levelExpr>` boolean condition node,
 * with source-position info copied from iae and levelExprBase.
 * Used by DesugarIfAvailableLevelCondition for the legacy integer-comparison
 * path (compatibleSDKVersion < 26 or argument major < 26).
 * *************** before desugar ****************
   @IfAvailable(level: 11, {=>...}, {=>...})
 * *************** after desugar (condition only) ***
   DeviceInfo.sdkApiVersion >= 11
 * **********************************************
 */
OwnedPtr<Expr> CreateSDKApiVersionCheck(IfAvailableExpr& iae, OwnedPtr<Expr> levelExpr, const Expr& levelExprBase)
{
    auto me = CreateMemberAccess(CreateRefExpr(SrcIdentifier(DEVICE_INFO)), SDK_API_VERSION);
    CopyBasicInfo(&iae, me->baseExpr.get());
    auto condition = CreateBinaryExpr(std::move(me), std::move(levelExpr), TokenKind::GE);
    CopyBasicInfo(&levelExprBase, condition->rightExpr.get());
    CopyBasicInfo(&iae, condition->leftExpr.get());
    AddCurFile(*condition, iae.curFile);
    CopyBasicInfo(&iae, condition.get());
    return condition;
}

/*
 * Build an `apiAvailable(<levelExpr>)` call expression node, with
 * source-position info copied from iae. levelExpr may be an integer literal
 * or a string literal depending on the @IfAvailable argument shape.
 * Used by DesugarIfAvailableLevelCondition for the SDK-26+ path.
 * apiAvailable is defined in ohos.base and is only available from SDK 26
 * onwards — callers must guard with IFAVAILABLE_RUNTIME_APILEVEL_MAJOR.
 * *************** before desugar ****************
   @IfAvailable(level: 26, {=>...}, {=>...})
 * *************** after desugar (condition only) ***
   apiAvailable(26)
 * **********************************************
 */
OwnedPtr<Expr> CreateApiAvailableCheck(IfAvailableExpr& iae, OwnedPtr<Expr> levelExpr)
{
    auto apiAvailableRef = CreateRefExpr(SrcIdentifier(API_AVAILABLE));
    CopyBasicInfo(&iae, apiAvailableRef.get());
    std::vector<OwnedPtr<FuncArg>> argList;
    argList.emplace_back(CreateFuncArg(std::move(levelExpr)));
    auto condition = CreateCallExpr(std::move(apiAvailableRef), std::move(argList));
    AddCurFile(*condition, iae.curFile);
    CopyBasicInfo(&iae, condition.get());
    return condition;
}

/*
 * Desugar the level condition of an @IfAvailable expression into a boolean
 * check expression. Implements the 5-way matrix from issue #824; the exact
 * output depends on the integer or string argument and compatibleSDKVersion
 * (--cfg=APILevel_level). The surrounding if-expr is built by DesugarIfAvailableExpr.
 *
 * 26 is the minimum SDK major version that introduces the apiAvailable(...)
 * runtime API (ohos.base). Older devices only expose DeviceInfo.sdkApiVersion.
 *
 * *************** before desugar ****************
   @IfAvailable(level: 11, {=>...}, {=>...})          // compatibleSDKVersion < 26
 * *************** after desugar (condition only) ***
   DeviceInfo.sdkApiVersion >= 11
 * **********************************************
 * *************** before desugar ****************
   @IfAvailable(level: 26, {=>...}, {=>...})          // compatibleSDKVersion >= 26
 * *************** after desugar (condition only) ***
   apiAvailable(26)
 * **********************************************
 * *************** before desugar ****************
   @IfAvailable(level: "26.0.0", {=>...}, {=>...})    // compatibleSDKVersion < 26, major >= 26
 * *************** after desugar (condition only) ***
   DeviceInfo.sdkApiVersion >= 26 && apiAvailable("26.0.0")
 * **********************************************
 */
OwnedPtr<Expr> DesugarIfAvailableLevelCondition(IfAvailableExpr& iae, const std::string& compatibleSDKVersion)
{
    auto compatibleLevel = Cangjie::APILevelVersion::ParseChecked(
        compatibleSDKVersion, Cangjie::APILevelVersion::ParseRule::MAJOR_OR_TRIPLE)
        .value_or(Cangjie::APILevelVersion());
    auto argExpr = iae.GetArg()->expr.get();
    if (!argExpr || argExpr->astKind != ASTKind::LIT_CONST_EXPR) {
        return ASTCloner::Clone(iae.GetArg()->expr.get());
    }
    auto lce = StaticCast<LitConstExpr>(argExpr);
    if (lce->kind == LitConstKind::INTEGER) {
        if (compatibleLevel >= Cangjie::APILevelVersion(IFAVAILABLE_RUNTIME_APILEVEL_MAJOR)) {
            return CreateApiAvailableCheck(iae, ASTCloner::Clone(iae.GetArg()->expr.get()));
        }
        return CreateSDKApiVersionCheck(iae, ASTCloner::Clone(iae.GetArg()->expr.get()), *lce);
    }
    if (lce->kind != LitConstKind::STRING) {
        return ASTCloner::Clone(iae.GetArg()->expr.get());
    }
    // Check compatibleSDKVersion before parsing the level string:
    // if compatibleLevel >= 26, we can directly call apiAvailable regardless of format validity
    // (CheckIfAvailableExpr will ERROR on invalid format before codegen runs).
    if (compatibleLevel >= Cangjie::APILevelVersion(IFAVAILABLE_RUNTIME_APILEVEL_MAJOR)) {
        return CreateApiAvailableCheck(iae, ASTCloner::Clone(iae.GetArg()->expr.get()));
    }
    auto parsedLevel =
        Cangjie::APILevelVersion::ParseChecked(lce->stringValue, Cangjie::APILevelVersion::ParseRule::TRIPLE_ONLY);
    if (!parsedLevel.has_value()) {
        // Invalid format: do not generate apiAvailable("bad string") — CheckIfAvailableExpr
        // will emit a hard ERROR (sema_apilevel_invalid_version_format) for this node.
        // Return a clone of the raw argument so the error fires on the correct AST node.
        return ASTCloner::Clone(iae.GetArg()->expr.get());
    }
    if (parsedLevel->major < IFAVAILABLE_RUNTIME_APILEVEL_MAJOR) {
        auto majorExpr =
            CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(parsedLevel->major), Ty::GetInitialTy());
        CopyBasicInfo(lce, majorExpr.get());
        return CreateSDKApiVersionCheck(iae, std::move(majorExpr), *lce);
    }
    auto majorGate = CreateLitConstExpr(
        LitConstKind::INTEGER, std::to_string(IFAVAILABLE_RUNTIME_APILEVEL_MAJOR), Ty::GetInitialTy());
    CopyBasicInfo(lce, majorGate.get());
    auto majorCondition = CreateSDKApiVersionCheck(iae, std::move(majorGate), *lce);
    auto runtimeCondition = CreateApiAvailableCheck(iae, ASTCloner::Clone(iae.GetArg()->expr.get()));
    auto condition = CreateBinaryExpr(std::move(majorCondition), std::move(runtimeCondition), TokenKind::AND);
    AddCurFile(*condition, iae.curFile);
    CopyBasicInfo(&iae, condition.get());
    return condition;
}

/*
 * Desugar the syscap condition of an @IfAvailable expression into a
 * canIUse(string) call expression. The syscap string literal argument is
 * moved directly into the generated call. The surrounding if-expr is built
 * by DesugarIfAvailableExpr.
 * *************** before desugar ****************
   @IfAvailable(syscap: "SystemCapability.ArkUI.ArkUI.Full", {=>...}, {=>...})
 * *************** after desugar (condition only) ***
   canIUse("SystemCapability.ArkUI.ArkUI.Full")
 * **********************************************
 */
OwnedPtr<Expr> DesugarIfAvailableSyscapCondition(IfAvailableExpr& iae)
{
    auto canIUseRef = CreateRefExpr(SrcIdentifier(CANIUSE_IDENTIFIER));
    CopyBasicInfo(&iae, canIUseRef.get());
    std::vector<OwnedPtr<FuncArg>> argList;
    argList.emplace_back(CreateFuncArg(std::move(iae.GetArg()->expr)));
    auto condition = CreateCallExpr(std::move(canIUseRef), std::move(argList));
    AddCurFile(*condition, iae.curFile);
    CopyBasicInfo(&iae, condition.get());
    return std::move(condition);
}

/*
 * Dispatch @IfAvailable condition desugaring based on the named argument kind.
 * Delegates to DesugarIfAvailableLevelCondition for "level" arguments and to
 * DesugarIfAvailableSyscapCondition for "syscap" arguments. Returns an
 * InvalidExpr for any unrecognised argument name (sema will diagnose).
 * *************** before desugar ****************
   @IfAvailable(level: 11, {=>body1}, {=>body2})
 * *************** after desugar (condition only) ***
   DesugarIfAvailableLevelCondition(iae, compatibleSDKVersion)
   // → DeviceInfo.sdkApiVersion >= 11  (or apiAvailable(...), see that function)
 * **********************************************
 * *************** before desugar ****************
   @IfAvailable(syscap: "SystemCapability.X", {=>body1}, {=>body2})
 * *************** after desugar (condition only) ***
   DesugarIfAvailableSyscapCondition(iae)
   // → canIUse("SystemCapability.X")
 * **********************************************
 */
OwnedPtr<Expr> DesugarIfAvailableCondition(IfAvailableExpr& iae, const std::string& compatibleSDKVersion)
{
    if (iae.GetArg()->name == LEVEL_IDENTGIFIER) {
        return DesugarIfAvailableLevelCondition(iae, compatibleSDKVersion);
    } else if (iae.GetArg()->name == SYSCAP_IDENTGIFIER) {
        return DesugarIfAvailableSyscapCondition(iae);
    } else {
        return MakeOwned<InvalidExpr>();
    }
}

/*
 * Desugar an @IfAvailable expression into an if-expr. Produces the boolean
 * condition via DesugarIfAvailableCondition, then wraps it in an if-expr
 * whose then-block and else-block are cloned from the first and second
 * lambda arguments of the original macro call respectively.
 * *************** before desugar ****************
   @IfAvailable(level: 11, {=> dostuff() }, {=> fallback() })
 * *************** after desugar ****************
   if (DeviceInfo.sdkApiVersion >= 11) {
       dostuff()
   } else {
       fallback()
   }
 * **********************************************
 * *************** before desugar ****************
   @IfAvailable(syscap: "SystemCapability.ArkUI.ArkUI.Full",
                {=> dostuff() }, {=> fallback() })
 * *************** after desugar ****************
   if (canIUse("SystemCapability.ArkUI.ArkUI.Full")) {
       dostuff()
   } else {
       fallback()
   }
 * **********************************************
 */
void DesugarIfAvailableExpr(IfAvailableExpr& iae, const std::string& compatibleSDKVersion)
{
    if (iae.desugarExpr) {
        return;
    }
    // Create condition.
    OwnedPtr<Expr> condition = DesugarIfAvailableCondition(iae, compatibleSDKVersion);
    auto ifBlock = ASTCloner::Clone(iae.GetLambda1()->funcBody->body.get());
    auto elseBlock = ASTCloner::Clone(iae.GetLambda2()->funcBody->body.get());
    auto ifExpr = CreateIfExpr(std::move(condition), std::move(ifBlock), std::move(elseBlock), iae.GetTy());
    ifExpr->sourceExpr = &iae;
    CopyBasicInfo(&iae, ifExpr);
    iae.desugarExpr = std::move(ifExpr);
}

struct VisitContext {
    void Push(bool isDiscarded, Ptr<Node> parent)
    {
        isDiscardedStack.push_back(isDiscarded);
        parentStack.push_back(parent);
    }

    void Pop(Ptr<const Node> expected)
    {
        CJC_ASSERT((!parentStack.empty()) && (parentStack.back() == expected));
        isDiscardedStack.pop_back();
        parentStack.pop_back();
    }

    /* Whether the current context does not require a return value of the expr */
    std::vector<bool> isDiscardedStack;
    std::vector<Ptr<Node>> parentStack;
};

// ----------- helpers for DesugarBrExpr ----------------------------------------

bool IsUnitExpr(Node& n)
{
    return n.astKind == ASTKind::LIT_CONST_EXPR &&
        StaticAs<ASTKind::LIT_CONST_EXPR>(&n)->kind == LitConstKind::UNIT;
}

bool IsNothingExpr(const Node& n)
{
    return std::set<ASTKind>{ASTKind::JUMP_EXPR, ASTKind::THROW_EXPR, ASTKind::RETURN_EXPR}.count(n.astKind) > 0;
}

void UnitifyBlock(Block& b)
{
    if (b.body.empty() ||
        (b.body.back() && (!IsUnitExpr(*b.body.back())) && (!IsNothingExpr(*b.body.back())))) {
        b.body.push_back(CreateUnitExpr());
    }
}

void UnitifyIf(const IfExpr& ie)
{
    if (ie.thenBody && ie.elseBody) {
        UnitifyBlock(*StaticAs<ASTKind::BLOCK>(ie.thenBody.get()));
        if (ie.elseBody->astKind == ASTKind::BLOCK) {
            UnitifyBlock(*StaticAs<ASTKind::BLOCK>(ie.elseBody.get()));
        }
    }
}

void UnitifyTry(TryExpr& te)
{
    if (te.tryLambda) {
        UnitifyBlock(*(te.tryLambda->funcBody->body));
    } else {
        UnitifyBlock(*(te.tryBlock));
    }
    for (auto& cb : te.catchBlocks) {
        UnitifyBlock(*cb);
    }
    for (auto& h : te.handlers) {
        if (h.desugaredLambda) {
            UnitifyBlock(*h.desugaredLambda->funcBody->body);
        }
        CJC_NULLPTR_CHECK(h.block);
        UnitifyBlock(*h.block);
    }
}

void UnitifyMatch(MatchExpr& me)
{
    for (auto& mc : me.matchCases) {
        UnitifyBlock(*(mc->exprOrDecls));
    }
    for (auto& mc : me.matchCaseOthers) {
        UnitifyBlock(*(mc->exprOrDecls));
    }
}

// ---------------------------------------------------------------------------

struct DiscardedHelper {
    void PushCtxt(bool isDiscarded, Ptr<Node> parent)
    {
        ctxt.Push(isDiscarded, parent);
    }

    void PopCtxt(Ptr<Node> expected)
    {
        ctxt.Pop(expected);
    }

    bool IsNodeDiscarded(const Node& n) const
    {
        return !ctxt.parentStack.empty() && IsDiscarded(n, *(ctxt.parentStack.back()), ctxt.isDiscardedStack.back());
    }

    /*
     * Try to find branch expressions to desuger in a block.
     * Branch expressions include: IfExpr, TryExpr, MatchExpr.
     * If the return value of the entire exprssion is not used,
     * add () to the end of each branch to skip lowest common
     * parent type check.
     * An example:
     * ************** before desugar ****************
        if (true) {
            1
        } else {
            1.0
        } // Fail. Can't find a return type.
     * *************** after desugar ****************
        if (true) {
            1
            ()
        } else {
            1.0
            ()
        } // Succeed. Return type is Unit.
     * **********************************************
     */
    static void DesugarBrExpr(Node& node)
    {
        if (node.astKind == ASTKind::IF_EXPR) {
            UnitifyIf(*StaticAs<ASTKind::IF_EXPR>(&node));
        } else if (node.astKind == ASTKind::TRY_EXPR) {
            UnitifyTry(*StaticAs<ASTKind::TRY_EXPR>(&node));
        } else if (node.astKind == ASTKind::MATCH_EXPR) {
            UnitifyMatch(*StaticAs<ASTKind::MATCH_EXPR>(&node));
        }
    }

private:
    VisitContext ctxt;

    /*
     * If the return value of the child is only used as a candidate of the parent's return value,
     * the discarded property is transitive from parent to child.
     */
    static bool IsDiscardTransitive(const Node& node, Node& parent)
    {
        static const std::set<ASTKind> caseKinds = {ASTKind::MATCH_CASE, ASTKind::MATCH_CASE_OTHER};
        // if expr to then blk and else blk
        bool ifBody = parent.astKind == ASTKind::IF_EXPR &&
            (&node == StaticAs<ASTKind::IF_EXPR>(&parent)->thenBody.get() ||
                &node == StaticAs<ASTKind::IF_EXPR>(&parent)->elseBody.get());
        // try expr to try blk and catch blk
        bool tryBody = parent.astKind == ASTKind::TRY_EXPR && node.astKind == ASTKind::BLOCK;
        // match expr to all cases
        bool matchCase = parent.astKind == ASTKind::MATCH_EXPR && (caseKinds.count(node.astKind) > 0);
        // match case to its body
        bool matchCaseBody = (caseKinds.count(parent.astKind) > 0) && node.astKind == ASTKind::BLOCK;
        // synchronized to its body
        bool syncBody = parent.astKind == ASTKind::SYNCHRONIZED_EXPR && node.astKind == ASTKind::BLOCK;
        // parentheses to their inner exprssion
        bool parentheses = parent.astKind == ASTKind::PAREN_EXPR;
        // info of FuncDecl has to pass down through FuncBody node
        bool funcBody = parent.astKind == ASTKind::FUNC_BODY;
        return ifBody || tryBody || matchCase || matchCaseBody || syncBody || parentheses || funcBody;
    };

    /* Some expressions will ignore a child block's return value. */
    static bool IsConstValBlk(const Node& node, Node& parent)
    {
        static const std::set<ASTKind> unitTypeExpr = {
            ASTKind::WHILE_EXPR, ASTKind::DO_WHILE_EXPR, ASTKind::FOR_IN_EXPR};
        // loops always return Unit
        bool constBlock = unitTypeExpr.count(parent.astKind) > 0;
        // finally is always ignored
        bool finallyBlock =
            parent.astKind == ASTKind::TRY_EXPR && &node == StaticAs<ASTKind::TRY_EXPR>(&parent)->finallyBlock.get();
        // func with ret type Unit always return Unit
        bool funcWithUnitRet = false;
        if (auto parentFB = DynamicCast<FuncBody*>(&parent); parentFB && parentFB->retType) {
            funcWithUnitRet = parentFB->retType->astKind == ASTKind::PRIMITIVE_TYPE &&
                StaticAs<ASTKind::PRIMITIVE_TYPE>(parentFB->retType.get())->kind == TypeKind::TYPE_UNIT;
        }
        // constructors don't have return value
        bool constructor = node.astKind == ASTKind::FUNC_BODY &&
            parent.TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::PRIMARY_CONSTRUCTOR);
        // if without else always return Unit
        bool ifNoElse = parent.astKind == ASTKind::IF_EXPR && StaticAs<ASTKind::IF_EXPR>(&parent)->elseBody == nullptr;
        return constBlock || finallyBlock || funcWithUnitRet || constructor || ifNoElse;
    };

    /* Whether the current node is an discarded-value expression */
    static bool IsDiscarded(const Node& node, Node& parent, bool isParentDiscarded)
    {
        // Transitively discarded
        bool flagTransitive = isParentDiscarded && IsDiscardTransitive(node, parent);
        // Child's ret is ignored
        bool flagConst =
            (node.astKind == ASTKind::BLOCK || node.astKind == ASTKind::FUNC_BODY) && IsConstValBlk(node, parent);
        // Immediate child of a Block
        bool flagBlock = parent.astKind == ASTKind::BLOCK &&
            (isParentDiscarded || (&node != StaticAs<ASTKind::BLOCK>(&parent)->body.back().get()));
        return flagTransitive || flagConst || flagBlock;
    }
};

/// Dispatch a single node to the appropriate desugar pass.
/// Returns early from the dispatch table; @p visitor and @p visitorPost are needed
/// for the recursive FILE macro-call walk.
void DispatchDesugar(Node& node, bool desugarMacrocall, const std::string& compatibleSDKVersion,
    const std::function<VisitAction(Ptr<Node>)>& visitor,
    const std::function<VisitAction(Ptr<Node>)>& visitorPost)
{
    switch (node.astKind) {
        case ASTKind::FILE: {
            auto& file = *StaticAs<ASTKind::FILE>(&node);
            if (desugarMacrocall) {
                for (auto& it : file.originalMacroCallNodes) {
                    Walker(it.get(), visitor, visitorPost).Walk();
                }
            }
            DesugarMacroDecl(file);
            break;
        }
        case ASTKind::MAIN_DECL:
            DesugarMainDecl(*StaticAs<ASTKind::MAIN_DECL>(&node));
            break;
        case ASTKind::QUOTE_EXPR:
            DesugarQuoteExpr(*StaticAs<ASTKind::QUOTE_EXPR>(&node));
            break;
        case ASTKind::OPTION_TYPE:
            DesugarOptionType(*StaticAs<ASTKind::OPTION_TYPE>(&node));
            break;
        case ASTKind::TRAIL_CLOSURE_EXPR:
            DesugarTrailingClosureExpr(*StaticAs<ASTKind::TRAIL_CLOSURE_EXPR>(&node));
            break;
        case ASTKind::SYNCHRONIZED_EXPR:
            DesugarSynchronizedExpr(*StaticAs<ASTKind::SYNCHRONIZED_EXPR>(&node));
            break;
        case ASTKind::OPTIONAL_CHAIN_EXPR:
            DesugarOptionalChainExpr(*StaticAs<ASTKind::OPTIONAL_CHAIN_EXPR>(&node));
            break;
        case ASTKind::INC_OR_DEC_EXPR:
            DesugarIncOrDecExpr(StaticCast<IncOrDecExpr&>(node));
            break;
        case ASTKind::ASSIGN_EXPR:
            DesugarAssignExpr(*StaticAs<ASTKind::ASSIGN_EXPR>(&node));
            break;
        case ASTKind::IF_AVAILABLE_EXPR:
            DesugarIfAvailableExpr(*StaticAs<ASTKind::IF_AVAILABLE_EXPR>(&node), compatibleSDKVersion);
            break;
        default:
            break;
    }
}
} // namespace

void PerformDesugarBeforeTypeCheck(Node& root, bool desugarMacrocall, const std::string& compatibleSDKVersion)
{
    DiscardedHelper dHelper;
    std::function<VisitAction(Ptr<Node>)> visitorPost = [&dHelper](Ptr<Node> node) -> VisitAction {
        dHelper.PopCtxt(node);
        return VisitAction::KEEP_DECISION;
    };
    std::function<VisitAction(Ptr<Node>)> visitor =
        [&visitor, &visitorPost, &dHelper, &desugarMacrocall, &compatibleSDKVersion](Ptr<Node> node) -> VisitAction {
        if (node->TestAttr(Attribute::IS_BROKEN)) {
            // must push before return to pair with visitorPost
            dHelper.PushCtxt(false, node);
            return VisitAction::SKIP_CHILDREN;
        }
        DispatchDesugar(*node, desugarMacrocall, compatibleSDKVersion, visitor, visitorPost);
        if (dHelper.IsNodeDiscarded(*node)) {
            dHelper.PushCtxt(true, node);
            DiscardedHelper::DesugarBrExpr(*node);
        } else {
            dHelper.PushCtxt(false, node);
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(&root, visitor, visitorPost).Walk();
}
} // namespace Cangjie
