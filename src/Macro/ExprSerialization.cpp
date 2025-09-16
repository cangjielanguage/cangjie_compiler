// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/*
 * @file
 *
 * This file implements the NodeWriter.
 */

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/Macro/NodeSerialization.h"
#include "cangjie/AST/Symbol.h"

using namespace Cangjie;
using namespace AST;
using namespace NodeSerialization;

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeWildcardExpr(AstExpr expr)
{
    auto wildcardExpr = RawStaticCast<const WildcardExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(wildcardExpr);
    auto fbWildcardExpr = NodeFormat::CreateWildcardExpr(builder, fbNodeBase);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_WILDCARD_EXPR, fbWildcardExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeBinaryExpr(AstExpr expr)
{
    auto binaryExpr = RawStaticCast<const BinaryExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(binaryExpr);
    auto leftExpr = SerializeExpr(binaryExpr->leftExpr.get());
    auto rightExpr = SerializeExpr(binaryExpr->rightExpr.get());
    auto operatorPos = FlatPosCreateHelper(binaryExpr->operatorPos);
    auto fbBinaryExpr = NodeFormat::CreateBinaryExpr(
        builder, fbNodeBase, leftExpr, rightExpr, static_cast<uint16_t>(binaryExpr->op), &operatorPos);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_BINARY_EXPR, fbBinaryExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeIsExpr(AstExpr expr)
{
    auto isExpr = RawStaticCast<const IsExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(isExpr);
    auto leftExpr = SerializeExpr(isExpr->leftExpr.get());
    auto isType = SerializeType(isExpr->isType.get());
    auto isPos = FlatPosCreateHelper(isExpr->isPos);
    auto fbIsExpr = NodeFormat::CreateIsExpr(builder, fbNodeBase, leftExpr, isType, &isPos);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_IS_EXPR, fbIsExpr.Union());
}
flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeAsExpr(AstExpr expr)
{
    auto asExpr = RawStaticCast<const AsExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(asExpr);
    auto leftExpr = SerializeExpr(asExpr->leftExpr.get());
    auto asType = SerializeType(asExpr->asType.get());
    auto asPos = FlatPosCreateHelper(asExpr->asPos);
    auto fbAsExpr = NodeFormat::CreateAsExpr(builder, fbNodeBase, leftExpr, asType, &asPos);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_AS_EXPR, fbAsExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeLitConstExpr(AstExpr expr)
{
    auto litConstExpr = RawStaticCast<const LitConstExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(litConstExpr);
    auto value = builder.CreateString(litConstExpr->rawString);
    auto fbLitConstExpr = NodeFormat::CreateLitConstExpr(builder, fbNodeBase, value,
        static_cast<uint16_t>(litConstExpr->kind), static_cast<uint16_t>(litConstExpr->delimiterNum),
        static_cast<uint16_t>(litConstExpr->stringKind), static_cast<uint16_t>(litConstExpr->isSingleQuote));
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_LIT_CONST_EXPR, fbLitConstExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeUnaryExpr(AstExpr expr)
{
    auto unaryExpr = RawStaticCast<const UnaryExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(unaryExpr);
    auto onlyExpr = SerializeExpr(unaryExpr->expr.get());
    uint16_t op = static_cast<uint16_t>(unaryExpr->op);
    auto operatorPos = FlatPosCreateHelper(unaryExpr->operatorPos);
    auto fbUnaryExpr = NodeFormat::CreateUnaryExpr(builder, fbNodeBase, onlyExpr, op, &operatorPos);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_UNARY_EXPR, fbUnaryExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeParenExpr(AstExpr expr)
{
    auto parenExpr = RawStaticCast<const ParenExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(parenExpr);
    auto onlyExpr = SerializeExpr(parenExpr->expr.get());
    auto leftParenPos = FlatPosCreateHelper(parenExpr->leftParenPos);
    auto rightParenPos = FlatPosCreateHelper(parenExpr->rightParenPos);
    auto fbParenExpr = NodeFormat::CreateParenExpr(builder, fbNodeBase, &leftParenPos, onlyExpr, &rightParenPos);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_PAREN_EXPR, fbParenExpr.Union());
}

flatbuffers::Offset<NodeFormat::FuncArg> NodeWriter::SerializeFuncArg(AstFuncArg funcArg)
{
    if (funcArg == nullptr) {
        return flatbuffers::Offset<NodeFormat::FuncArg>();
    }
    auto base = SerializeNodeBase(funcArg);
    auto name = builder.CreateString(funcArg->name.GetRawText());
    auto namePos = FlatPosCreateHelper(funcArg->name.GetRawPos());
    auto colonPos = FlatPosCreateHelper(funcArg->colonPos);
    auto fbExpr = SerializeExpr(funcArg->expr.get());
    auto commaPos = FlatPosCreateHelper(funcArg->commaPos);
    return NodeFormat::CreateFuncArg(builder, base, name, &namePos, &colonPos, fbExpr, &commaPos, funcArg->withInout);
}

flatbuffers::Offset<NodeFormat::CallExpr> NodeWriter::SerializeCallExpr(const CallExpr* callExpr)
{
    if (callExpr == nullptr) {
        return flatbuffers::Offset<NodeFormat::CallExpr>();
    }
    auto fbNodeBase = SerializeNodeBase(callExpr);
    auto baseFunc = callExpr->baseFunc.get();
    auto fbBaseFunc = SerializeExpr(baseFunc);
    auto leftParenPos = FlatPosCreateHelper(callExpr->leftParenPos);
    auto rightParenPos = FlatPosCreateHelper(callExpr->rightParenPos);
    auto fbArgs =
        FlatVectorCreateHelper<NodeFormat::FuncArg, FuncArg, AstFuncArg>(callExpr->args, &NodeWriter::SerializeFuncArg);
    return NodeFormat::CreateCallExpr(builder, fbNodeBase, fbBaseFunc, &leftParenPos, fbArgs, &rightParenPos);
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeCallExpr(const Expr* expr)
{
    auto callExpr = RawStaticCast<const CallExpr*>(expr);
    auto fbCallExpr = SerializeCallExpr(callExpr);
    return NodeFormat::CreateExpr(
        builder, SerializeNodeBase(callExpr), NodeFormat::AnyExpr_CALL_EXPR, fbCallExpr.Union());
}

flatbuffers::Offset<NodeFormat::RefExpr> NodeWriter::SerializeRefExpr(const RefExpr* refExpr)
{
    if (refExpr == nullptr) {
        return flatbuffers::Offset<NodeFormat::RefExpr>();
    }
    auto fbNodeBase = SerializeNodeBase(refExpr);
    auto ref = refExpr->ref;
    auto identifier = builder.CreateString(ref.identifier.GetRawText());
    auto identifierPos = FlatPosCreateHelper(ref.identifier.GetRawPos());
    auto fbRef = NodeFormat::CreateReference(builder, identifier, &identifierPos);
    auto leftAnglePos = FlatPosCreateHelper(refExpr->leftAnglePos);
    auto fbTypeVec =
        FlatVectorCreateHelper<NodeFormat::Type, Type, AstType>(refExpr->typeArguments, &NodeWriter::SerializeType);
    auto rightAnglePos = FlatPosCreateHelper(refExpr->rightAnglePos);
    return NodeFormat::CreateRefExpr(builder, fbNodeBase, fbRef, &leftAnglePos, fbTypeVec, &rightAnglePos,
        refExpr->isThis, refExpr->isSuper, refExpr->isQuoteDollar);
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeRefExpr(const Expr* expr)
{
    auto refExpr = RawStaticCast<const RefExpr*>(expr);
    auto fbRefExpr = SerializeRefExpr(refExpr);
    return NodeFormat::CreateExpr(builder, SerializeNodeBase(refExpr), NodeFormat::AnyExpr_REF_EXPR, fbRefExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeBlockExpr(AstExpr expr)
{
    auto block = RawStaticCast<const Block*>(expr);
    auto fbBlock = SerializeBlock(block);
    return NodeFormat::CreateExpr(builder, SerializeNodeBase(block), NodeFormat::AnyExpr_BLOCK, fbBlock.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeReturnExpr(AstExpr expr)
{
    auto returnExpr = RawStaticCast<const ReturnExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(returnExpr);
    auto returnPos = FlatPosCreateHelper(returnExpr->returnPos);
    auto fbExpr = SerializeExpr(returnExpr->expr.get());
    if (returnExpr->expr->TestAttr(Attribute::COMPILER_ADD)) {
        fbExpr = flatbuffers::Offset<NodeFormat::Expr>();
    }
    auto fbReturnExpr = NodeFormat::CreateReturnExpr(builder, fbNodeBase, &returnPos, fbExpr);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_RETURN_EXPR, fbReturnExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeDoWhileExpr(AstExpr expr)
{
    auto doWhileExpr = RawStaticCast<const DoWhileExpr*>(expr);
    auto base = SerializeNodeBase(doWhileExpr);
    auto doPos = FlatPosCreateHelper(doWhileExpr->doPos);
    auto body = SerializeBlock(doWhileExpr->body.get());
    auto whilePos = FlatPosCreateHelper(doWhileExpr->whilePos);
    auto leftParenPos = FlatPosCreateHelper(doWhileExpr->leftParenPos);
    auto condExpr = SerializeExpr(doWhileExpr->condExpr.get());
    auto rightParenPos = FlatPosCreateHelper(doWhileExpr->rightParenPos);
    auto fbDoWhileExpr =
        NodeFormat::CreateDoWhileExpr(builder, base, &doPos, body, &whilePos, &leftParenPos, condExpr, &rightParenPos);
    return NodeFormat::CreateExpr(builder, base, NodeFormat::AnyExpr_DO_WHILE_EXPR, fbDoWhileExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeAssignExpr(AstExpr expr)
{
    auto assignExpr = RawStaticCast<const AssignExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(assignExpr);
    auto fbLeftValue = SerializeExpr(assignExpr->leftValue.get());
    auto assignOp = static_cast<uint16_t>(assignExpr->op);
    auto assignPos = FlatPosCreateHelper(assignExpr->assignPos);
    auto fbRightExpr = SerializeExpr(assignExpr->rightExpr.get());
    auto fbAssignExpr =
        NodeFormat::CreateAssignExpr(builder, fbNodeBase, fbLeftValue, assignOp, &assignPos, fbRightExpr);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_ASSIGN_EXPR, fbAssignExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeMemberAccess(AstExpr expr)
{
    auto memberAccess = RawStaticCast<const MemberAccess*>(expr);
    auto fbNodeBase = SerializeNodeBase(memberAccess);
    auto fbBaseExpr = SerializeExpr(memberAccess->baseExpr.get());
    auto dotPos = FlatPosCreateHelper(memberAccess->dotPos);
    auto field = builder.CreateString(memberAccess->field.GetRawText());
    auto fieldPos = FlatPosCreateHelper(memberAccess->field.GetRawPos());
    auto fbTypeArguments = FlatVectorCreateHelper<NodeFormat::Type, Type, AstType>(
        memberAccess->typeArguments, &NodeWriter::SerializeType);
    auto leftAnglePos = FlatPosCreateHelper(memberAccess->leftAnglePos);
    auto rightAnglePos = FlatPosCreateHelper(memberAccess->rightAnglePos);
    auto fbMemberAccess = NodeFormat::CreateMemberAccess(
        builder, fbNodeBase, fbBaseExpr, &dotPos, field, &fieldPos, &leftAnglePos, fbTypeArguments, &rightAnglePos);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_MEMBER_ACCESS, fbMemberAccess.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeLetPatternDestructor(AstExpr expr)
{
    auto letExpr = RawStaticCast<const LetPatternDestructor*>(expr);
    auto fbNodeBase = SerializeNodeBase(letExpr);
    auto fbPatterns = FlatVectorCreateHelper<NodeFormat::Pattern, Pattern, AstPattern>(
        letExpr->patterns, &NodeWriter::SerializePattern);
    auto backarrowPos = FlatPosCreateHelper(letExpr->backarrowPos);
    auto initializer = SerializeExpr(letExpr->initializer.get());
    auto bitOrPosVector = CreatePositionVector(letExpr->orPos);
    auto fbLetExpr = NodeFormat::CreateLetPatternDestructor(builder, fbNodeBase, fbPatterns, bitOrPosVector,
        &backarrowPos, initializer);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_LET_PATTERN_DESTRUCTOR, fbLetExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeIfExpr(AstExpr expr)
{
    auto ifExpr = RawStaticCast<const IfExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(ifExpr);
    auto ifPos = FlatPosCreateHelper(ifExpr->ifPos);
    auto leftParenPos = FlatPosCreateHelper(ifExpr->leftParenPos);
    auto fbCondExpr = SerializeExpr(ifExpr->condExpr.get());
    auto rightParenPos = FlatPosCreateHelper(ifExpr->rightParenPos);
    auto fbBody = SerializeBlock(ifExpr->thenBody.get());
    auto elsePos = FlatPosCreateHelper(ifExpr->elsePos);
    flatbuffers::Offset<NodeFormat::Expr> fbElseBody;
    if (!ifExpr->hasElse) {
        auto fbIfExpr = NodeFormat::CreateIfExpr(builder, fbNodeBase, &ifPos, fbCondExpr, fbBody, ifExpr->hasElse,
            &elsePos, fbElseBody, ifExpr->isElseIf, &leftParenPos, &rightParenPos);
        return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_IF_EXPR, fbIfExpr.Union());
    }

    auto fbExpr = SerializeExpr(ifExpr->elseBody.get());
    auto fbIfExpr = NodeFormat::CreateIfExpr(builder, fbNodeBase, &ifPos, fbCondExpr, fbBody, ifExpr->hasElse,
        &elsePos, fbExpr, ifExpr->isElseIf, &leftParenPos, &rightParenPos);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_IF_EXPR, fbIfExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeMatchExpr(AstExpr expr)
{
    auto matchExpr = RawStaticCast<const MatchExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(matchExpr);
    auto leftParenPos = FlatPosCreateHelper(matchExpr->leftParenPos);
    auto fbSelector = SerializeExpr(matchExpr->selector.get());
    auto rightParenPos = FlatPosCreateHelper(matchExpr->rightParenPos);
    auto leftCurlPos = FlatPosCreateHelper(matchExpr->leftCurlPos);
    auto fbMatchcases = FlatVectorCreateHelper<NodeFormat::MatchCase, MatchCase, AstMatchCase>(
        matchExpr->matchCases, &NodeWriter::SerializeMatchCase);
    auto fbMatchcaseother = FlatVectorCreateHelper<NodeFormat::MatchCaseOther, MatchCaseOther, AstMatchCaseOther>(
        matchExpr->matchCaseOthers, &NodeWriter::SerializeMatchCaseOther);
    auto rightCurlPos = FlatPosCreateHelper(matchExpr->rightCurlPos);
    auto fbMatchExpr = NodeFormat::CreateMatchExpr(builder, fbNodeBase, matchExpr->matchMode, &leftParenPos, fbSelector,
        &rightParenPos, &leftCurlPos, fbMatchcases, fbMatchcaseother, &rightCurlPos);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_MATCH_EXPR, fbMatchExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeWhileExpr(AstExpr expr)
{
    auto whileExpr = RawStaticCast<const WhileExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(whileExpr);
    auto whilePos = FlatPosCreateHelper(whileExpr->whilePos);
    auto leftParenPos = FlatPosCreateHelper(whileExpr->leftParenPos);
    auto fbCondExpr = SerializeExpr(whileExpr->condExpr.get());
    auto rightParenPos = FlatPosCreateHelper(whileExpr->rightParenPos);
    auto fbBody = SerializeBlock(whileExpr->body.get());
    auto fbWhileExpr =
        NodeFormat::CreateWhileExpr(builder, fbNodeBase, &whilePos, &leftParenPos, fbCondExpr, &rightParenPos, fbBody);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_WHILE_EXPR, fbWhileExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeArrayLit(AstExpr expr)
{
    auto arrayLit = RawStaticCast<const ArrayLit*>(expr);
    auto fbNodeBase = SerializeNodeBase(arrayLit);
    auto leftCurlPos = FlatPosCreateHelper(arrayLit->leftSquarePos);
    auto fbExs =
        FlatVectorCreateHelper<NodeFormat::Expr, Expr, AstExpr>(arrayLit->children, &NodeWriter::SerializeExpr);
    auto commaPosVector = CreatePositionVector(arrayLit->commaPosVector);
    auto rightCurlPos = FlatPosCreateHelper(arrayLit->rightSquarePos);
    auto fbArrayLit =
        NodeFormat::CreateArrayLit(builder, fbNodeBase, &leftCurlPos, fbExs, commaPosVector, &rightCurlPos);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_ARRAY_LIT, fbArrayLit.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeTupleLit(AstExpr expr)
{
    auto tupleLit = RawStaticCast<const TupleLit*>(expr);
    auto fbNodeBase = SerializeNodeBase(tupleLit);
    auto leftParenPos = FlatPosCreateHelper(tupleLit->leftParenPos);
    auto fbExs =
        FlatVectorCreateHelper<NodeFormat::Expr, Expr, AstExpr>(tupleLit->children, &NodeWriter::SerializeExpr);
    auto commaPositions = CreatePositionVector(tupleLit->commaPosVector);
    auto rightParenPos = FlatPosCreateHelper(tupleLit->rightParenPos);
    auto fbTupleLit =
        NodeFormat::CreateTupleLit(builder, fbNodeBase, &leftParenPos, fbExs, commaPositions, &rightParenPos);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_TUPLE_LIT, fbTupleLit.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeSubscriptExpr(AstExpr expr)
{
    auto subscriptExpr = RawStaticCast<const SubscriptExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(subscriptExpr);
    auto fbBaseExpr = SerializeExpr(subscriptExpr->baseExpr.get());
    auto leftSquarePos = FlatPosCreateHelper(subscriptExpr->leftParenPos);
    auto fbIndexExprs =
        FlatVectorCreateHelper<NodeFormat::Expr, Expr, AstExpr>(subscriptExpr->indexExprs, &NodeWriter::SerializeExpr);
    auto rightSquarePos = FlatPosCreateHelper(subscriptExpr->rightParenPos);
    bool isTupleAccess = subscriptExpr->isTupleAccess;
    auto fbSubscriptExpr = NodeFormat::CreateSubscriptExpr(
        builder, fbNodeBase, fbBaseExpr, &leftSquarePos, fbIndexExprs, &rightSquarePos, isTupleAccess);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_SUBSCRIPT_EXPR, fbSubscriptExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeRangeExpr(AstExpr expr)
{
    auto rangeExpr = RawStaticCast<const RangeExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(rangeExpr);
    auto fbStartExpr = SerializeExpr(rangeExpr->startExpr.get());
    auto rangePos = FlatPosCreateHelper(rangeExpr->rangePos);
    auto fbStopExpr = SerializeExpr(rangeExpr->stopExpr.get());
    auto colonPos = FlatPosCreateHelper(rangeExpr->colonPos);
    auto fbStepExpr = SerializeExpr(rangeExpr->stepExpr.get());
    bool isClosed = rangeExpr->isClosed;
    auto fbRangeExpr = NodeFormat::CreateRangeExpr(
        builder, fbNodeBase, fbStartExpr, &rangePos, fbStopExpr, &colonPos, fbStepExpr, isClosed);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_RANGE_EXPR, fbRangeExpr.Union());
}

flatbuffers::Offset<NodeFormat::LambdaExpr> NodeWriter::SerializeLambdaExpr(const LambdaExpr* lambdaExpr)
{
    if (lambdaExpr == nullptr) {
        return flatbuffers::Offset<NodeFormat::LambdaExpr>();
    }
    auto fbNodeBase = SerializeNodeBase(lambdaExpr);
    auto fbBody = SerializeFuncBody(lambdaExpr->funcBody.get());
    auto mockSupported = lambdaExpr->TestAttr(Attribute::MOCK_SUPPORTED);
    return NodeFormat::CreateLambdaExpr(builder, fbNodeBase, fbBody, mockSupported);
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeLambdaExpr(const Expr* expr)
{
    auto lambdaExpr = RawStaticCast<const LambdaExpr*>(expr);
    auto fbLambdaExpr = SerializeLambdaExpr(lambdaExpr);
    return NodeFormat::CreateExpr(
        builder, SerializeNodeBase(lambdaExpr), NodeFormat::AnyExpr_LAMBDA_EXPR, fbLambdaExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeSpawnExpr(AstExpr expr)
{
    auto spawnExpr = RawStaticCast<const SpawnExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(spawnExpr);
    auto spawnPos = FlatPosCreateHelper(spawnExpr->spawnPos);
    auto taskExpr = SerializeExpr(spawnExpr->task.get());
    auto hasArg = (spawnExpr->arg.get() != nullptr);
    auto spawnArgExpr = SerializeExpr(spawnExpr->arg.get());
    auto leftParenPos = FlatPosCreateHelper(spawnExpr->leftParenPos);
    auto rightParenPos = FlatPosCreateHelper(spawnExpr->rightParenPos);
    auto fbSpawnExpr = NodeFormat::CreateSpawnExpr(
        builder, fbNodeBase, &spawnPos, taskExpr, hasArg, spawnArgExpr, &leftParenPos, &rightParenPos);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_SPAWN_EXPR, fbSpawnExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeSynchronizedExpr(AstExpr expr)
{
    auto synchronizedExpr = RawStaticCast<const SynchronizedExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(synchronizedExpr);
    auto syncPos = FlatPosCreateHelper(synchronizedExpr->syncPos);
    auto leftParenPos = FlatPosCreateHelper(synchronizedExpr->leftParenPos);
    auto mutexExpr = SerializeExpr(synchronizedExpr->mutex.get());
    auto rightParenPos = FlatPosCreateHelper(synchronizedExpr->rightParenPos);
    auto body = SerializeBlock(synchronizedExpr->body.get());
    auto fbSynchronizedExpr = NodeFormat::CreateSynchronizedExpr(
        builder, fbNodeBase, &syncPos, &leftParenPos, mutexExpr, &rightParenPos, body);
    return NodeFormat::CreateExpr(
        builder, fbNodeBase, NodeFormat::AnyExpr_SYNCHRONIZED_EXPR, fbSynchronizedExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeTrailingClosureExpr(AstExpr expr)
{
    auto type = NodeFormat::AnyExpr_TRAILING_CLOSURE_EXPR;
    auto trailingClosureExpr = RawStaticCast<const TrailingClosureExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(trailingClosureExpr);
    auto leftLambdaPos = FlatPosCreateHelper(trailingClosureExpr->leftLambda);
    auto fbExpr = SerializeExpr(trailingClosureExpr->expr.get());
    auto fbLambdaExpr = SerializeLambdaExpr(trailingClosureExpr->lambda.get());
    auto rightLambdaPos = FlatPosCreateHelper(trailingClosureExpr->rightLambda);
    auto fbTrailingClosureExpr = NodeFormat::CreateTrailingClosureExpr(
        builder, fbNodeBase, &leftLambdaPos, fbExpr, fbLambdaExpr, &rightLambdaPos);
    return NodeFormat::CreateExpr(builder, fbNodeBase, type, fbTrailingClosureExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeTypeConvExpr(AstExpr expr)
{
    auto type = NodeFormat::AnyExpr_TYPE_CONV_EXPR;
    auto typeConvExpr = RawStaticCast<const TypeConvExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(typeConvExpr);
    auto fbPrimitiveType = SerializeType(typeConvExpr->type.get());
    auto leftParenPos = FlatPosCreateHelper(typeConvExpr->leftParenPos);
    auto fbExpr = SerializeExpr(typeConvExpr->expr.get());
    auto rightParenPos = FlatPosCreateHelper(typeConvExpr->rightParenPos);
    auto fbTypeConvExpr =
        NodeFormat::CreateTypeConvExpr(builder, fbNodeBase, fbPrimitiveType, &leftParenPos, fbExpr, &rightParenPos);
    return NodeFormat::CreateExpr(builder, fbNodeBase, type, fbTypeConvExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeTryExpr(AstExpr expr)
{
    auto tryExpr = RawStaticCast<const TryExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(tryExpr);
    auto fbLParenPos = FlatPosCreateHelper(tryExpr->lParen);
    auto fbResource = FlatVectorCreateHelper<NodeFormat::VarDecl, VarDecl, const VarDecl*>(
        tryExpr->resourceSpec, &NodeWriter::SerializeVarDecl);
    auto fbRParenPos = FlatPosCreateHelper(tryExpr->rParen);
    auto fbCommaPos = CreatePositionVector(tryExpr->resourceSpecCommaPos);
    auto fbTryBlock = SerializeBlock(tryExpr->tryBlock.get());
    auto fbCatchPos = CreatePositionVector(tryExpr->catchPosVector);
    auto fbCatchLParenPos = CreatePositionVector(tryExpr->catchLParenPosVector);
    auto fbCatchRParenPos = CreatePositionVector(tryExpr->catchRParenPosVector);
    auto fbCatchBlocks =
        FlatVectorCreateHelper<NodeFormat::Block, Block, AstBlock>(tryExpr->catchBlocks, &NodeWriter::SerializeBlock);
    auto fbCatchPatterns = FlatVectorCreateHelper<NodeFormat::Pattern, Pattern, AstPattern>(
        tryExpr->catchPatterns, &NodeWriter::SerializePattern);
    std::vector<flatbuffers::Offset<NodeFormat::Handler>> vecHandlers;
    for (auto& handler : tryExpr->handlers) {
        auto fbPos = FlatPosCreateHelper(handler.pos);
        auto fbCommandPattern = SerializePattern(handler.commandPattern.get());
        auto fbHandleBlock = SerializeBlock(handler.block.get());
        auto fbHanlder =
            NodeFormat::CreateHandler(builder, &fbPos, fbCommandPattern, fbHandleBlock);
        vecHandlers.push_back(fbHanlder);
    }
    auto fbHandlers = builder.CreateVector(vecHandlers);
    auto finallyPos = FlatPosCreateHelper(tryExpr->finallyPos);
    auto fbFinallyBlock = SerializeBlock(tryExpr->finallyBlock.get());
    auto fbTryExpr =
        NodeFormat::CreateTryExpr(builder, fbNodeBase, fbResource, tryExpr->isDesugaredFromTryWithResources,
            fbTryBlock, fbCatchBlocks, fbCatchPatterns, &finallyPos, fbFinallyBlock,
            &fbLParenPos, &fbRParenPos, fbCommaPos, fbCatchPos, fbCatchLParenPos, fbCatchRParenPos, fbHandlers);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_TRY_EXPR, fbTryExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeThrowExpr(AstExpr expr)
{
    auto throwExpr = RawStaticCast<const ThrowExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(throwExpr);
    auto fbExpr = SerializeExpr(throwExpr->expr.get());
    auto fbThrowExpr = NodeFormat::CreateThrowExpr(builder, fbNodeBase, fbExpr);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_THROW_EXPR, fbThrowExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializePerformExpr(AstExpr expr)
{
    auto performExpr = RawStaticCast<const PerformExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(performExpr);
    auto fbExpr = SerializeExpr(performExpr->expr.get());
    auto fbPerformExpr = NodeFormat::CreatePerformExpr(builder, fbNodeBase, fbExpr);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_PERFORM_EXPR, fbPerformExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeResumeExpr(AstExpr expr)
{
    auto resumeExpr = RawStaticCast<const ResumeExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(resumeExpr);
    auto withPos = FlatPosCreateHelper(resumeExpr->withPos);
    auto withExpr = SerializeExpr(resumeExpr->withExpr);
    auto throwingPos = FlatPosCreateHelper(resumeExpr->throwingPos);
    auto throwingExpr = SerializeExpr(resumeExpr->throwingExpr);
    auto fbResumeExpr =
        NodeFormat::CreateResumeExpr(builder, fbNodeBase, &withPos, withExpr, &throwingPos, throwingExpr);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_RESUME_EXPR, fbResumeExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializePrimitiveTypeExpr(AstExpr expr)
{
    auto primitiveTypeExpr = RawStaticCast<const PrimitiveTypeExpr*>(expr);
    auto fbTypeBase = SerializeNodeBase(primitiveTypeExpr);
    auto typeKind = static_cast<uint16_t>(primitiveTypeExpr->typeKind);
    auto fbPrimTypeExpr = NodeFormat::CreatePrimitiveTypeExpr(builder, fbTypeBase, typeKind);
    return NodeFormat::CreateExpr(builder, fbTypeBase, NodeFormat::AnyExpr_PRIMITIVE_TYPE_EXPR, fbPrimTypeExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeForInExpr(AstExpr expr)
{
    auto forinExpr = RawStaticCast<const ForInExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(forinExpr);
    auto leftParenPos = FlatPosCreateHelper(forinExpr->leftParenPos);
    auto fbPattern = SerializePattern(forinExpr->pattern.get());
    auto inPos = FlatPosCreateHelper(forinExpr->inPos);
    auto fbInExpr = SerializeExpr(forinExpr->inExpression.get());
    auto rightParenPos = FlatPosCreateHelper(forinExpr->rightParenPos);
    auto ifPos = FlatPosCreateHelper(forinExpr->wherePos);
    auto fbPatternGuard = SerializeExpr(forinExpr->patternGuard.get());
    auto fbBody = SerializeBlock(forinExpr->body.get());
    auto fbForInExpr = NodeFormat::CreateForInExpr(builder, fbNodeBase, &leftParenPos, fbPattern, &inPos, fbInExpr,
        &rightParenPos, &ifPos, fbPatternGuard, fbBody);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_FOR_IN_EXPR, fbForInExpr.Union());
}

flatbuffers::Offset<NodeFormat::NodeBase> NodeWriter::SerializeNodeBase(AstNode node)
{
    if (node == nullptr) {
        return flatbuffers::Offset<NodeFormat::NodeBase>();
    }
    // NodeBase is the attrs every Node inherits from
    auto beginPos = FlatPosCreateHelper(node->begin);
    auto endPos = FlatPosCreateHelper(node->end);
    auto str = Cangjie::AST::ASTKIND_TO_STRING_MAP[node->astKind];
    auto astKind = builder.CreateString(str);
    return NodeFormat::CreateNodeBase(builder, &beginPos, &endPos, astKind);
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeJumpExpr(AstExpr expr)
{
    auto jumpExpr = RawStaticCast<const JumpExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(jumpExpr);
    auto isBreak = jumpExpr->isBreak;
    auto fbJumpExpr = NodeFormat::CreateJumpExpr(builder, fbNodeBase, isBreak);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_JUMP_EXPR, fbJumpExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeIncOrDecExpr(AstExpr expr)
{
    auto incOrDecExpr = RawStaticCast<const IncOrDecExpr*>(expr);
    auto base = SerializeNodeBase(incOrDecExpr);
    uint16_t op = static_cast<uint16_t>(incOrDecExpr->op);
    auto operatorPos = FlatPosCreateHelper(incOrDecExpr->operatorPos);
    auto expr0 = SerializeExpr(incOrDecExpr->expr.get());
    auto fbIncOrDecExpr = NodeFormat::CreateIncOrDecExpr(builder, base, op, &operatorPos, expr0);
    return NodeFormat::CreateExpr(builder, base, NodeFormat::AnyExpr_INC_OR_DEC_EXPR, fbIncOrDecExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeOptionalExpr(AstExpr expr)
{
    auto optionalExpr = RawStaticCast<const OptionalExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(optionalExpr);
    auto baseExpr = SerializeExpr(optionalExpr->baseExpr.get());
    auto questPos = FlatPosCreateHelper(optionalExpr->questPos);
    auto fbOptionalExpr = NodeFormat::CreateOptionalExpr(builder, fbNodeBase, baseExpr, &questPos);
    return NodeFormat::CreateExpr(builder, fbNodeBase, NodeFormat::AnyExpr_OPTIONAL_EXPR, fbOptionalExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeOptionalChainExpr(AstExpr expr)
{
    auto optionalChainExpr = RawStaticCast<const OptionalChainExpr*>(expr);
    auto fbNodeBase = SerializeNodeBase(optionalChainExpr);
    auto optexpr = SerializeExpr(optionalChainExpr->expr.get());
    auto fbOptionalChainExpr = NodeFormat::CreateOptionalChainExpr(builder, fbNodeBase, optexpr);
    return NodeFormat::CreateExpr(
        builder, fbNodeBase, NodeFormat::AnyExpr_OPTIONAL_CHAIN_EXPR, fbOptionalChainExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeTokenPart(AstExpr expr)
{
    auto tokenPart = RawStaticCast<const TokenPart*>(expr);
    auto vecToken = TokensVectorCreateHelper(tokenPart->tokens);
    auto fbTokens = builder.CreateVector(vecToken);
    auto fbTokenPart = NodeFormat::CreateTokenPart(builder, fbTokens);
    return NodeFormat::CreateExpr(
        builder, SerializeNodeBase(tokenPart), NodeFormat::AnyExpr_TOKEN_PART, fbTokenPart.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeQuoteExpr(AstExpr expr)
{
    auto quoteExpr = RawStaticCast<const QuoteExpr*>(expr);
    auto base = SerializeNodeBase(quoteExpr);
    auto leftParenPos = FlatPosCreateHelper(quoteExpr->leftParenPos);
    auto rightParenPos = FlatPosCreateHelper(quoteExpr->rightParenPos);
    std::vector<flatbuffers::Offset<NodeFormat::Expr>> vecExpr;
    for (auto& child : quoteExpr->exprs) {
        vecExpr.push_back(SerializeExpr(child.get()));
    }
    auto fbExprs = builder.CreateVector(vecExpr);
    auto fbQuoteExpr = NodeFormat::CreateQuoteExpr(builder, base, &leftParenPos, fbExprs, &rightParenPos);
    return NodeFormat::CreateExpr(builder, base, NodeFormat::AnyExpr_QUOTE_EXPR, fbQuoteExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeMacroExpandExpr(AstExpr expr)
{
    auto macroExpandExpr = RawStaticCast<const MacroExpandExpr*>(expr);
    auto base = SerializeNodeBase(macroExpandExpr);
    auto invocation = MacroInvocationCreateHelper(macroExpandExpr->invocation);
    auto identifier = builder.CreateString(macroExpandExpr->identifier.Val());
    auto identifierPos = FlatPosCreateHelper(macroExpandExpr->identifier.Begin());
    auto annotationVec = FlatVectorCreateHelper<NodeFormat::Annotation, Annotation, AstAnnotation>(
        macroExpandExpr->annotations, &NodeWriter::SerializeAnnotation);
    std::vector<flatbuffers::Offset<NodeFormat::Modifier>> vecModifier;
    auto modifiersVec = SortModifierByPos(macroExpandExpr->modifiers);
    for (auto& mod : modifiersVec) {
        auto fbMod = SerializeModifier(mod);
        vecModifier.push_back(fbMod);
    }
    auto fbModVec = builder.CreateVector(vecModifier);
    auto fbMacroExpandExpr = NodeFormat::CreateMacroExpandExpr(
        builder, base, invocation, identifier, &identifierPos, annotationVec, fbModVec);
    return NodeFormat::CreateExpr(builder, base, NodeFormat::AnyExpr_MACRO_EXPAND_EXPR, fbMacroExpandExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeArrayExpr(AstExpr expr)
{
    auto arrayExpr = RawStaticCast<const ArrayExpr*>(expr);
    auto base = SerializeNodeBase(arrayExpr);
    auto type = SerializeType(arrayExpr->type.get());
    auto leftParenPos = FlatPosCreateHelper(arrayExpr->leftParenPos);
    auto args = FlatVectorCreateHelper<NodeFormat::FuncArg, FuncArg, AstFuncArg>(
        arrayExpr->args, &NodeWriter::SerializeFuncArg);
    auto rightParenPos = FlatPosCreateHelper(arrayExpr->rightParenPos);
    auto isValueArray = arrayExpr->isValueArray;
    auto fbArrayExpr =
        NodeFormat::CreateArrayExpr(builder, base, type, &leftParenPos, args, &rightParenPos, isValueArray);
    return NodeFormat::CreateExpr(builder, base, NodeFormat::AnyExpr_ARRAY_EXPR, fbArrayExpr.Union());
}

flatbuffers::Offset<NodeFormat::Expr> NodeWriter::SerializeExpr(AstExpr expr)
{
    if (expr == nullptr) {
        return flatbuffers::Offset<NodeFormat::Expr>();
    }
    static std::unordered_map<AST::ASTKind, std::function<NodeFormatExpr(NodeWriter & nw, AstExpr expr)>>
        serializeExprMap = {
            {ASTKind::WILDCARD_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeWildcardExpr(expr); }},
            {ASTKind::BINARY_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeBinaryExpr(expr); }},
            {ASTKind::LIT_CONST_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeLitConstExpr(expr); }},
            {ASTKind::UNARY_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeUnaryExpr(expr); }},
            {ASTKind::PAREN_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeParenExpr(expr); }},
            {ASTKind::CALL_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeCallExpr(expr); }},
            {ASTKind::REF_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeRefExpr(expr); }},
            {ASTKind::RETURN_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeReturnExpr(expr); }},
            {ASTKind::ASSIGN_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeAssignExpr(expr); }},
            {ASTKind::MEMBER_ACCESS, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeMemberAccess(expr); }},
            {ASTKind::IF_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeIfExpr(expr); }},
            {ASTKind::BLOCK, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeBlockExpr(expr); }},
            {ASTKind::LAMBDA_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeLambdaExpr(expr); }},
            {ASTKind::TYPE_CONV_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeTypeConvExpr(expr); }},
            {ASTKind::FOR_IN_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeForInExpr(expr); }},
            {ASTKind::ARRAY_LIT, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeArrayLit(expr); }},
            {ASTKind::TUPLE_LIT, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeTupleLit(expr); }},
            {ASTKind::SUBSCRIPT_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeSubscriptExpr(expr); }},
            {ASTKind::RANGE_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeRangeExpr(expr); }},
            {ASTKind::MATCH_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeMatchExpr(expr); }},
            {ASTKind::TRY_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeTryExpr(expr); }},
            {ASTKind::THROW_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeThrowExpr(expr); }},
            {ASTKind::PERFORM_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializePerformExpr(expr); }},
            {ASTKind::RESUME_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeResumeExpr(expr); }},
            {ASTKind::JUMP_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeJumpExpr(expr); }},
            {ASTKind::WHILE_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeWhileExpr(expr); }},
            {ASTKind::DO_WHILE_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeDoWhileExpr(expr); }},
            {ASTKind::INC_OR_DEC_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeIncOrDecExpr(expr); }},
            {ASTKind::TOKEN_PART, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeTokenPart(expr); }},
            {ASTKind::QUOTE_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeQuoteExpr(expr); }},
            {ASTKind::IS_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeIsExpr(expr); }},
            {ASTKind::AS_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeAsExpr(expr); }},
            {ASTKind::SPAWN_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeSpawnExpr(expr); }},
            {ASTKind::SYNCHRONIZED_EXPR,
                [](NodeWriter& nw, AstExpr expr) { return nw.SerializeSynchronizedExpr(expr); }},
            {ASTKind::OPTIONAL_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeOptionalExpr(expr); }},
            {ASTKind::OPTIONAL_CHAIN_EXPR,
                [](NodeWriter& nw, AstExpr expr) { return nw.SerializeOptionalChainExpr(expr); }},
            {ASTKind::TRAIL_CLOSURE_EXPR,
                [](NodeWriter& nw, AstExpr expr) { return nw.SerializeTrailingClosureExpr(expr); }},
            {ASTKind::PRIMITIVE_TYPE_EXPR,
                [](NodeWriter& nw, AstExpr expr) { return nw.SerializePrimitiveTypeExpr(expr); }},
            {ASTKind::LET_PATTERN_DESTRUCTOR,
                [](NodeWriter& nw, AstExpr expr) { return nw.SerializeLetPatternDestructor(expr); }},
            {ASTKind::MACRO_EXPAND_EXPR,
                [](NodeWriter& nw, AstExpr expr) { return nw.SerializeMacroExpandExpr(expr); }},
            {ASTKind::ARRAY_EXPR, [](NodeWriter& nw, AstExpr expr) { return nw.SerializeArrayExpr(expr); }},
        };
    // Match ReplaceExpr func.
    auto serializeFunc = serializeExprMap.find(expr->astKind);
    if (serializeFunc != serializeExprMap.end()) {
        return serializeFunc->second(*this, expr);
    }
    Errorln("Expr Not Supported in Libast Yet\n");
    return flatbuffers::Offset<NodeFormat::Expr>();
}
