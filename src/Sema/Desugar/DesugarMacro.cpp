// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Desugar macro.
 */

#include "DesugarMacro.h"

#include <algorithm>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "TypeCheckUtil.h"

#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Macro/TokenSerialization.h"
#include "cangjie/Utils/Utils.h"
#include "cangjie/Basic/StringConvertor.h"

namespace Cangjie {
using namespace AST;
using namespace TypeCheckUtil;

namespace {

void DesugarTokensNormalizeString(std::vector<Token>& tokens)
{
    for (unsigned long i = 0; i < tokens.size(); i++) {
        if (tokens[i].kind == TokenKind::MULTILINE_RAW_STRING) {
            tokens[i].SetValue(StringConvertor::Normalize(tokens[i].Value(), true));
        }
    }
}

/*
 * Encode the token into bytes and then bytes will be stored in ArrayLit's children
 * For example, quote(0) will be desugared to:
 * Tokens([1,0,0,0,134,0,1,0,0,0,48,1,0,0,0,3,0,0,0,24,0,0,0])
 */
OwnedPtr<ArrayLit> DesugarTokensToArrayLiteral(std::vector<Token>& tokens)
{
    DesugarTokensNormalizeString(tokens);
    std::vector<uint8_t> buffers = TokenSerialization::GetTokensBytes(tokens);
    OwnedPtr<ArrayLit> arrayLit = MakeOwned<ArrayLit>();
    arrayLit->children.reserve(buffers.size());

    std::transform(buffers.begin(), buffers.end(), std::back_inserter(arrayLit->children),
        [](auto& byte) { return CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(byte), nullptr); });

    return arrayLit;
}

OwnedPtr<Expr> CreateQuoteDesugarExpr(const QuoteExpr& qexpr)
{
    auto tksRefExpr = CreateRefExprInAST("Tokens");
    tksRefExpr->begin = qexpr.begin;
    tksRefExpr->end = qexpr.end;
    auto tokensExpr = CreateCallExpr(std::move(tksRefExpr), {});
    tokensExpr->EnableAttr(Attribute::COMPILER_ADD);
    tokensExpr->begin = qexpr.begin;
    tokensExpr->end = qexpr.end;
    return tokensExpr;
}

OwnedPtr<ForInExpr> CreateReadingForInExpr(const std::tuple<std::string, std::string, std::string>& declArgs)
{
    auto [declName, argPtr, argSize] = declArgs;
    auto varPattern = CreateVarPattern("i");
    auto start = CreateLitConstExpr(LitConstKind::INTEGER, "0", nullptr);
    auto end = CreateRefExpr(argSize);
    auto rangeExpr = MakeOwned<RangeExpr>();
    rangeExpr->startExpr = std::move(start);
    rangeExpr->stopExpr = std::move(end);
    rangeExpr->isClosed = false;
    rangeExpr->EnableAttr(Attribute::COMPILER_ADD);

    auto callBase = MakeOwnedNode<MemberAccess>();
    callBase->baseExpr = CreateRefExpr(declName);
    callBase->field = "[]";

    auto funcAgr1 = CreateFuncArg(CreateRefExpr("i"));

    auto readCallBase = MakeOwnedNode<MemberAccess>();
    readCallBase->baseExpr = CreateRefExpr(argPtr);
    readCallBase->field = "read";

    std::vector<OwnedPtr<FuncArg>> readVector;
    readVector.emplace_back(CreateFuncArg(CreateRefExpr("i")));
    auto readCall = CreateCallExpr(std::move(readCallBase), std::move(readVector));
    readCall->EnableAttr(Attribute::UNSAFE);

    std::vector<OwnedPtr<FuncArg>> funcVector;
    funcVector.emplace_back(std::move(funcAgr1));
    funcVector.emplace_back(CreateFuncArg(std::move(readCall), "value"));

    auto callExpr = CreateCallExpr(std::move(callBase), std::move(funcVector));
    std::vector<OwnedPtr<Node>> nodes;
    nodes.emplace_back(std::move(callExpr));
    auto forInBody = CreateBlock(std::move(nodes));
    return CreateForInExpr(std::move(varPattern), std::move(rangeExpr), std::move(forInBody));
}

/**
 * Create var decl, like : let bufParam: RefArray<UInt8> = Array<UInt8>(paramSize, repeat: 0).
 * @param newPos
 * @param declArgs
 * @return
 */
OwnedPtr<VarDecl> CreateReadingVarDecl(
    const Position& newPos, const std::pair<std::string, std::string>& declArgs)
{
    auto [declName, argSize] = declArgs;
    auto typeArgs = MakeOwned<PrimitiveType>();
    typeArgs->str = "UInt8";
    typeArgs->kind = TypeKind::TYPE_UINT8;

    std::string v = "Array";
    auto arrType = CreateRefType(v, {typeArgs.get()});
    arrType->EnableAttr(Attribute::IN_CORE);
    auto baseF = CreateRefExpr({v, newPos, newPos, false}, nullptr, newPos, {typeArgs.get()});
    baseF->EnableAttr(Attribute::IN_CORE);
    auto funcArg1 = CreateFuncArg(CreateRefExpr(argSize));
    auto litConst = CreateLitConstExpr(LitConstKind::INTEGER, "0", nullptr);

    std::vector<OwnedPtr<FuncArg>> funcVector;
    funcVector.emplace_back(std::move(funcArg1));
    funcVector.emplace_back(CreateFuncArg(std::move(litConst), "repeat"));

    auto callExpr = CreateCallExpr(std::move(baseF), std::move(funcVector));
    return CreateVarDecl(declName, std::move(callExpr), arrType.get());
}

/**
 * Create var decl, like : let attr: Tokens = Tokens().
 * @param declArgs
 * @return
 */
OwnedPtr<VarDecl> CreateTokensParamDecl(const std::string argName, const Position& pos)
{
    auto refExpr = CreateRefExprInAST("Tokens");
    refExpr->begin = pos;
    refExpr->end = pos;
    auto tokensAttrCall = CreateCallExpr(std::move(refExpr), {});
    return CreateVarDecl(argName, std::move(tokensAttrCall));
}

/**
 * Create var decl, like : var tok : Tokens = Tokens() + Token(TokenKind.ILLEGAL)
 * @param declArgs
 * @return
 */
OwnedPtr<AssignExpr> CreateIllegalTokensDecl(const std::string argName, const Position& pos)
{
    // Create assign expr, like: ret = ret + Token(TokenKind.ILLEGAL)
    auto tokenKind = CreateRefExprInAST("TokenKind");
    auto tok = CreateFuncArg(CreateMemberAccess(std::move(tokenKind), "ILLEGAL"));
    tok->begin = pos;
    tok->end = pos;
    std::vector<OwnedPtr<FuncArg>> tokensAttrArgs;
    tokensAttrArgs.emplace_back(std::move(tok));

    auto fileID = CreateFuncArg(CreateLitConstExpr(
        LitConstKind::INTEGER, std::to_string(pos.fileID), TypeManager::GetPrimitiveTy(TypeKind::TYPE_UINT32)));
    auto line = CreateFuncArg(CreateLitConstExpr(
        LitConstKind::INTEGER, std::to_string(pos.line), TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT32)));
    auto column = CreateFuncArg(CreateLitConstExpr(
        LitConstKind::INTEGER, std::to_string(pos.column), TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT32)));
    auto refExpr = CreateRefExprInAST("Token");
    refExpr->begin = pos;
    refExpr->end = pos;
    auto baseExpr = CreateCallExpr(std::move(refExpr), std::move(tokensAttrArgs));
    std::vector<OwnedPtr<FuncArg>> arguments;
    arguments.emplace_back(CreateFuncArg(std::move(baseExpr)));
    arguments.emplace_back(std::move(fileID));
    arguments.emplace_back(std::move(line));
    arguments.emplace_back(std::move(column));
    auto refreshExpr = CreateRefExprInAST("refreshPos");
    refreshExpr->begin = pos;
    refreshExpr->end = pos;
    auto callExpr = CreateCallExpr(std::move(refreshExpr), std::move(arguments));
    auto binExpr = CreateBinaryExpr(CreateRefExpr(argName), std::move(callExpr), TokenKind::ADD);
    auto assignExpr = CreateAssignExpr(CreateRefExpr(argName), std::move(binExpr));
    return assignExpr;
}

/**
 * Create attribute macro call, like : ident(attr, params)
 * @param ident
 * @return
 */
OwnedPtr<CallExpr> CreateAttrCall(const std::string& ident, const Position& pos)
{
    auto attr = CreateFuncArg(CreateRefExpr("attr", pos));
    auto param = CreateFuncArg(CreateRefExpr("params", pos));
    std::vector<OwnedPtr<FuncArg>> retAttrArgs;
    (void)retAttrArgs.emplace_back(std::move(attr));
    (void)retAttrArgs.emplace_back(std::move(param));
    auto baseExpr = CreateRefExpr(ident, pos);
    auto ret = CreateCallExpr(std::move(baseExpr), std::move(retAttrArgs));
    return ret;
}

/**
 * Create common macro call, like : ident(params)
 * @param ident
 * @return
 */
OwnedPtr<CallExpr> CreateCommonCall(const std::string& ident, const Position& pos)
{
    auto param = CreateFuncArg(CreateRefExpr("params", pos));
    std::vector<OwnedPtr<FuncArg>> retCommonArgs;
    (void)retCommonArgs.emplace_back(std::move(param));
    auto baseExpr = CreateRefExpr(ident, pos);
    auto ret = CreateCallExpr(std::move(baseExpr), std::move(retCommonArgs));
    return ret;
}

/**
 * Create var decl, like : let tBuffer = ret.toBytes()
 * @return
 */
OwnedPtr<VarDecl> CreateToBytesVar(const std::string& refName, const Position& pos)
{
    auto toBytesCall = CreateCallExpr(CreateMemberAccess(CreateRefExpr(refName, pos), "toBytes"), {});
    return CreateVarDecl("tBuffer", std::move(toBytesCall));
}

/**
 * Create return expression, like : return unsafePointerCastFromUint8Array(tBuffer).
 * @return
 */
OwnedPtr<CallExpr> CreateReturnExpr(const Position& pos)
{
    auto tBuffer = CreateFuncArg(CreateRefExpr("tBuffer", pos));
    std::vector<OwnedPtr<FuncArg>> retExprArgs;
    retExprArgs.emplace_back(std::move(tBuffer));
    auto baseExpr = CreateRefExprInAST("unsafePointerCastFromUint8Array");
    baseExpr->begin = pos;
    baseExpr->end = pos;
    auto ret = CreateCallExpr(std::move(baseExpr), std::move(retExprArgs));
    return ret;
}

/**
 * Create call param, like :
 * (attrPtr: CPointer<UInt8>, attrSize: Int64, paramPtr: CPointer<UInt8>,
 *     paramSize: Int64, callMacroCallPtr: CPointer<Unit>).
 * @return
 */
using ParamPtr = OwnedPtr<FuncParam>;
std::tuple<ParamPtr, ParamPtr, ParamPtr, ParamPtr, ParamPtr> CreateCallParam(
    const Position& pos, RefType& unsafePtrType, RefType& callBackPtrType)
{
    auto int64Type = MakeOwned<PrimitiveType>();
    int64Type->kind = TypeKind::TYPE_INT64;
    int64Type->str = "Int64";

    // Create func param and Bind pos in it, for refExpr's lookup.
    auto attrPtrParam = CreateFuncParam("attrPtr", ASTCloner::Clone(Ptr(&unsafePtrType)));
    attrPtrParam->begin = pos;
    attrPtrParam->end = pos;
    auto attrSizeParam = CreateFuncParam("attrSize", ASTCloner::Clone(int64Type.get()));
    attrSizeParam->begin = pos;
    attrSizeParam->end = pos;
    auto paramPtrParam = CreateFuncParam("paramPtr", ASTCloner::Clone(Ptr(&unsafePtrType)));
    paramPtrParam->begin = pos;
    paramPtrParam->end = pos;
    auto paramSizeParam = CreateFuncParam("paramSize", std::move(int64Type));
    paramSizeParam->begin = pos;
    paramSizeParam->end = pos;
    auto callMacroCallPtr = CreateFuncParam("callMacroCallPtr", ASTCloner::Clone(Ptr(&callBackPtrType)));
    return {std::move(attrPtrParam), std::move(attrSizeParam), std::move(paramPtrParam), std::move(paramSizeParam),
        std::move(callMacroCallPtr)};
}

/**
 * Create the wrapper function: external func macroCall_a_ident(attrPtr: CPointer<UInt8>, attrSize: Int64,
 *     paramPtr: CPointer<UInt8>, paramSize: Int64, callMacroCallPtr: CPointer<Unit>): CPointer<UInt8>.
 * @param funcName
 * @param pos
 * @param body
 * @param params
 * @param unsafePtrType
 * @return
 */
OwnedPtr<FuncDecl> CreateWrapperFuncDecl(const std::string& funcName, const Position& pos,
    OwnedPtr<Block> body, std::vector<Ptr<FuncParam>>& params, OwnedPtr<RefType> unsafePtrType)
{
    // Create func param list, func body and func decl.
    auto paramList = CreateFuncParamList(std::move(params));
    std::vector<OwnedPtr<FuncParamList>> funcParamLists;
    funcParamLists.emplace_back(std::move(paramList));
    auto funcBody = CreateFuncBody(std::move(funcParamLists), std::move(unsafePtrType), std::move(body));
    funcBody->EnableAttr(Attribute::C, Attribute::MACRO_INVOKE_BODY, Attribute::PUBLIC);
    auto funcDecl = CreateFuncDecl(funcName, std::move(funcBody));
    funcDecl->toBeCompiled = true; // For incremental compilation.
    funcDecl->identifier.SetPos(pos, pos);
    funcDecl->begin = pos;
    funcDecl->end = pos;
    std::set<Modifier> modifiers;
    modifiers.emplace(TokenKind::PUBLIC, pos);
    funcDecl->modifiers.insert(modifiers.begin(), modifiers.end());
    funcDecl->EnableAttr(Attribute::NO_MANGLE, Attribute::C, Attribute::MACRO_INVOKE_FUNC, Attribute::PUBLIC);
    return funcDecl;
}

/**
 * Create return expression, like : return unsafePointerCastFromUint8Array(@{}).
 * @return
 */
OwnedPtr<CallExpr> CreateCatchReturnExpr(const Position& pos)
{
    auto arg = CreateFuncArg(CreateRefExpr("tBuffer", pos));
    std::vector<OwnedPtr<FuncArg>> retExprArgs;
    retExprArgs.emplace_back(std::move(arg));
    auto baseExpr = CreateRefExprInAST("unsafePointerCastFromUint8Array");
    baseExpr->begin = pos;
    baseExpr->end = pos;
    return CreateCallExpr(std::move(baseExpr), std::move(retExprArgs));
}

/**
 * Create catch block, like
 * {
 *     var tok = Tokens()
 *     tok = tok + Token(TokenKind::ILLEGAL)
 *     return unsafePointerCastFromUint8Array(@{})
 * }.
 * @return
 */
void CreateCatchBlock(TryExpr& tryExpr, const Position& pos, bool printStack = false)
{
    // To create ExceptTypePattern e: Exception
    std::vector<OwnedPtr<Node>> nodes;
    // Create print stack, like : e.printStack().
    if (printStack) {
        auto printStackNode = CreateCallExpr(CreateMemberAccess(CreateRefExpr("e"), "printStackTrace"), {});
        nodes.emplace_back(std::move(printStackNode));
    }
    auto tokVar = CreateTokensParamDecl("tokVar", pos);
    tokVar->isVar = true;
    nodes.emplace_back(std::move(tokVar));
    auto assignExpr = CreateIllegalTokensDecl("tokVar", pos);
    nodes.emplace_back(std::move(assignExpr));
    auto tBufferVar = CreateToBytesVar("tokVar", pos);
    nodes.emplace_back(std::move(tBufferVar));
    auto retExpr = CreateCatchReturnExpr(pos);
    nodes.emplace_back(std::move(retExpr));
    auto catchBlock = CreateBlock(std::move(nodes));
    tryExpr.catchPosVector.emplace_back(INVALID_POSITION);
    tryExpr.catchBlocks.push_back(std::move(catchBlock));
}

/**
 * Create ExceptTypePattern, like : 'catch : (e: Exception)' or 'catch : (e: MacroWithContextException)'
 * @return
 */
void CreateCatchPattern(TryExpr& tryExpr, const std::string& exceptionTypeStr)
{
    auto exceptTypePattern = MakeOwnedNode<ExceptTypePattern>();
    auto varPattern = MakeOwned<VarPattern>();
    varPattern->varDecl = CreateVarDecl("e");
    varPattern->varDecl->parentPattern = varPattern.get();

    OwnedPtr<RefType> exceptionType;
    if (exceptionTypeStr == CLASS_EXCEPTION) {
        exceptionType = CreateRefTypeInCore(CLASS_EXCEPTION);
    } else if (exceptionTypeStr == CLASS_ERROR) {
        exceptionType = CreateRefTypeInCore(CLASS_ERROR);
    } else {
        exceptionType = MakeOwned<RefType>();
        exceptionType->ref.identifier = exceptionTypeStr;
        exceptionType->EnableAttr(Attribute::IN_MACRO);
    }

    exceptionType->begin = tryExpr.tryBlock->begin;
    exceptionType->end = tryExpr.tryBlock->begin;
    exceptionType->EnableAttr(Attribute::COMPILER_ADD);
    exceptTypePattern->types.push_back(std::move(exceptionType));
    exceptTypePattern->pattern = std::move(varPattern);
    tryExpr.catchPatterns.push_back(std::move(exceptTypePattern));
}
// Create thread_local assign expr, like : MACRO_OBJECT.set(XXX).
static OwnedPtr<Expr> CreateTLAssignExpr(const std::string& refName, const Position& newPos)
{
    auto tlBase = MakeOwnedNode<MemberAccess>();
    tlBase->baseExpr = CreateRefExpr(MACRO_OBJECT_NAME);
    tlBase->baseExpr->EnableAttr(Attribute::IN_MACRO);
    tlBase->baseExpr->begin = newPos;
    tlBase->baseExpr->end = newPos;
    tlBase->field = "set";
    std::vector<OwnedPtr<FuncArg>> funcArgVector;
    funcArgVector.emplace_back(CreateFuncArg(CreateRefExpr(refName)));
    auto assignCall = CreateCallExpr(std::move(tlBase), std::move(funcArgVector));
    assignCall->EnableAttr(Attribute::COMPILER_ADD);
    assignCall->begin = newPos;
    assignCall->end = newPos;
    return assignCall;
}

// Create finally block, like : { MACRO_OBJECT.set(None) }
static void CreateFinallyBlock(TryExpr& tryExpr, const Position& pos)
{
    std::vector<OwnedPtr<Node>> nodes;

    auto tlAssignCall = CreateTLAssignExpr("None", pos);
    nodes.emplace_back(std::move(tlAssignCall));

    tryExpr.finallyBlock = CreateBlock(std::move(nodes));
}
/**
 * Create try expression, like
 * try {
 *     MACRO_OBJECT.set(callMacroCallPtr)      // CJNATIVE
 *     let bufParam: Array<UInt8> = Array<UInt8>(paramSize, repeat: 0)
 *     for (i in 0..paramSize) {
 *         bufParam[i] = unsafe{ paramPtr.read(i) }
 *     }
 *     let params: Tokens = Tokens(bufParam)
 *     let ret: Tokens = ident(params)                    // CJNATIVE
 *     let tBuffer = ret.toBytes()
 *     return unsafePointerCastFromUint8Array(tBuffer)
 * }
 * catch (e: MacroWithContextException) {
 *     var tok = Tokens()
 *     tok = tok + Token(TokenKind::ILLEGAL)
 *     return unsafePointerCastFromUint8Array(tok)
 * }
 * catch (e: Exception) {
 *     var tok = Tokens()
 *     tok = tok + Token(TokenKind::ILLEGAL)
 *     return unsafePointerCastFromUint8Array(tok)
 * }
 * catch (e: Error) {
 *     var tok = Tokens()
 *     tok = tok + Token(TokenKind::ILLEGAL)
 *     return unsafePointerCastFromUint8Array(tok)
 * } finally {
 *     MACRO_OBJECT.set(None)          // CJNATIVE only
 * }
 * @return
 */
OwnedPtr<TryExpr> CreateWrapperTryExpr(OwnedPtr<Block> tryBlock)
{
    auto tryExpr = MakeOwned<TryExpr>();
    tryExpr->tryBlock = std::move(tryBlock);
    CreateCatchPattern(*tryExpr, MC_EXCEPTION);
    CreateCatchBlock(*tryExpr, tryExpr->tryBlock->begin);
    CreateCatchPattern(*tryExpr, CLASS_EXCEPTION);
    CreateCatchBlock(*tryExpr, tryExpr->tryBlock->begin, true);
    // For oom, catch error is required because oom inherits from error rather than exception.
    CreateCatchPattern(*tryExpr, CLASS_ERROR);
    CreateCatchBlock(*tryExpr, tryExpr->tryBlock->begin, true);
    CreateFinallyBlock(*tryExpr, tryExpr->tryBlock->begin);
    return tryExpr;
}

/**
 * Create if expression, like
 * if (paramSize > 0) {
 *     params = Tokens(bufParam)
 * }
 * @return
 */
OwnedPtr<IfExpr> CreateWrapperIfExpr(
    const std::tuple<std::string, std::string, std::string>& declArgs, const Position& pos)
{
    // Create condition expr, like: paramSize > 0
    auto [argName, argBuf, argSize] = declArgs;
    auto argSizeExpr = CreateRefExpr(argSize);
    auto litConst = CreateLitConstExpr(LitConstKind::INTEGER, "0", nullptr);
    auto cond = CreateBinaryExpr(std::move(argSizeExpr), std::move(litConst), TokenKind::GT);

    // Create assign expr, like: params = Tokens(bufParam)
    auto thenExprBlock = MakeOwnedNode<Block>();
    auto bufAttr = CreateFuncArg(CreateRefExpr(argBuf));
    std::vector<OwnedPtr<FuncArg>> tokensAttrArgs;
    tokensAttrArgs.emplace_back(std::move(bufAttr));
    auto refExpr = CreateRefExprInAST("Tokens");
    refExpr->begin = pos;
    refExpr->end = pos;
    auto callExpr = CreateCallExpr(std::move(refExpr), std::move(tokensAttrArgs));
    auto assignExpr = CreateAssignExpr(CreateRefExpr(argName), std::move(callExpr));

    thenExprBlock->body.push_back(std::move(assignExpr));
    auto elseExprBlock = MakeOwnedNode<Block>();
    auto ifExpr = CreateIfExpr(std::move(cond), std::move(thenExprBlock), std::move(elseExprBlock));
    ifExpr->hasElse = false;
    ifExpr->EnableAttr(Attribute::COMPILER_ADD);
    return ifExpr;
}

/**
 * Create block expression, like
 * {
 *     MACRO_OBJECT.set(callMacroCallPtr)      // CJNATIVE
 *     let bufParam: Array<UInt8> = Array<UInt8>(paramSize, repeat: 0)
 *     for (i in 0..paramSize) {
 *         bufParam[i] = unsafe{ paramPtr.read(i) }
 *     }
 *     var params: Tokens = Tokens()
 *     if (paramSize > 0) {
 *         params = Tokens(bufParam)
 *     }
 *     let ret: Tokens = ident(params)                    // CJNATIVE
 *     let tBuffer = ret.toBytes()
 *     return unsafePointerCastFromUint8Array(tBuffer)
 * }
 * @return
 */
OwnedPtr<Block> CreateWrapperTryBlock(const Position& pos, const std::string& ident, bool isAttr)
{
    // Pos after 'pos' to lookup field.
    auto newPos = pos + BEGIN_POSITION;
    auto tlAssignCall = CreateTLAssignExpr("callMacroCallPtr", newPos);
    // Create var decl, like : let bufParam: RefArray<UInt8> = Array<UInt8>(paramSize, repeat: 0).
    auto bufAttrVar = CreateReadingVarDecl(newPos, {"bufAttr", "attrSize"});
    // Create forIn expression.
    auto bufAttrForIn = CreateReadingForInExpr({"bufAttr", "attrPtr", "attrSize"});
    auto bufParamVar = CreateReadingVarDecl(newPos, {"bufParam", "paramSize"});
    auto bufParamForIn = CreateReadingForInExpr({"bufParam", "paramPtr", "paramSize"});

    auto attrVar = CreateTokensParamDecl("attr", pos);
    attrVar->isVar = true;
    auto attrIfExpr = CreateWrapperIfExpr({"attr", "bufAttr", "attrSize"}, pos);
    auto paramsVar = CreateTokensParamDecl("params", pos);
    paramsVar->isVar = true;
    auto argsIfExpr = CreateWrapperIfExpr({"params", "bufParam", "paramSize"}, pos);

    auto callExpr = isAttr ? CreateAttrCall(ident, newPos) : CreateCommonCall(ident, newPos);
    callExpr->baseFunc->EnableAttr(Attribute::MACRO_INVOKE_BODY);
    callExpr->EnableAttr(Attribute::MACRO_INVOKE_BODY);
    auto retVar = CreateVarDecl("ret", std::move(callExpr));
    retVar->isVar = true;
    auto tBufferVar = CreateToBytesVar("ret", newPos);
    auto retExpr = CreateReturnExpr(newPos);
    std::vector<OwnedPtr<Node>> nodes;
    nodes.emplace_back(std::move(tlAssignCall));
    // Add attr var decl.
    if (isAttr) {
        nodes.emplace_back(std::move(bufAttrVar));
        nodes.emplace_back(std::move(bufAttrForIn));
        nodes.emplace_back(std::move(attrVar));
        nodes.emplace_back(std::move(attrIfExpr));
    }
    nodes.emplace_back(std::move(bufParamVar));
    nodes.emplace_back(std::move(bufParamForIn));
    nodes.emplace_back(std::move(paramsVar));
    nodes.emplace_back(std::move(argsIfExpr));
    nodes.emplace_back(std::move(retVar));
    nodes.emplace_back(std::move(tBufferVar));
    nodes.emplace_back(std::move(retExpr));
    return CreateBlock(std::move(nodes));
}

void AddMacroFuncDecl(File& curFile, const Position& pos, const std::string& ident, bool isAttr)
{
    auto tryBlock = CreateWrapperTryBlock(pos, ident, isAttr);
    tryBlock->begin = pos;
    tryBlock->end = pos;
    auto tryExpr = CreateWrapperTryExpr(std::move(tryBlock));
    tryExpr->EnableAttr(Attribute::MACRO_INVOKE_BODY);
    auto body = MakeOwned<Block>();
    body->begin = pos;
    body->end = pos;
    body->body.emplace_back(std::move(tryExpr));
    auto callBackPtrType = MakeOwned<RefType>();
    callBackPtrType->begin = pos;
    callBackPtrType->ref.identifier = "CPointer";
    auto callBackPrimitiveType = MakeOwned<PrimitiveType>();
    callBackPrimitiveType->kind = TypeKind::TYPE_UNIT;
    callBackPtrType->typeArguments.push_back(std::move(callBackPrimitiveType));
    callBackPtrType->end = pos;
    // Create Types, like 'CPointer<UInt8>' : let attrPtr: CPointer<UInt8>.
    auto unsafePtrType = MakeOwned<RefType>();
    unsafePtrType->begin = pos;
    unsafePtrType->ref.identifier = "CPointer";
    auto unsafePrimitiveType = MakeOwned<PrimitiveType>();
    unsafePrimitiveType->kind = TypeKind::TYPE_UINT8;
    unsafePtrType->typeArguments.push_back(std::move(unsafePrimitiveType));
    auto [attrPtrParam, attrSizeParam, paramPtrParam, paramSizeParam, callMacroCallPtr] =
        CreateCallParam(pos, *unsafePtrType, *callBackPtrType);
    std::vector<Ptr<FuncParam>> params = {
        attrPtrParam.get(), attrSizeParam.get(), paramPtrParam.get(), paramSizeParam.get(), callMacroCallPtr.get()};
    // If is not attr macro, should remove attr func arg.
    if (!isAttr) {
        // The attr macro has 2 more arguments than the normal version.
        // So if it is not attr, we should delete 2 of them.
        params.erase(params.begin(), params.begin() + 2);
    }
    std::string packageName = "";
    if (curFile.curPackage != nullptr) {
        packageName = curFile.curPackage->fullPackageName;
    }
    auto funcName = Utils::GetMacroFuncName(packageName, isAttr, ident);
    auto funcDecl = CreateWrapperFuncDecl(funcName, pos, std::move(body), params, std::move(unsafePtrType));
    funcDecl->EnableAttr(Attribute::GLOBAL);
    curFile.decls.emplace_back(std::move(funcDecl));
}
} // namespace

OwnedPtr<Expr> CreateToTokensMethod(const OwnedPtr<Expr>& expr)
{
    // For case: quote($(Token(RPAREN))), need to add position.
    auto desugarTokenInQuote = [](Ptr<Node> curNode) -> VisitAction {
        if (curNode->astKind != ASTKind::CALL_EXPR) {
            return VisitAction::WALK_CHILDREN;
        }
        auto ce = StaticAs<ASTKind::CALL_EXPR>(curNode);
        if (ce->baseFunc && ce->baseFunc->astKind == ASTKind::REF_EXPR) {
            auto re = StaticAs<ASTKind::REF_EXPR>(ce->baseFunc.get());
            if (re->ref.identifier == "Token") {
                auto uint32Ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UINT32);
                auto int32Ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT32);
                std::vector<OwnedPtr<FuncArg>> args;
                args.emplace_back(CreateFuncArg(ASTCloner::Clone(Ptr(ce))));
                args.emplace_back(CreateFuncArg(
                    CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(ce->begin.fileID), uint32Ty)));
                args.emplace_back(CreateFuncArg(
                    CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(ce->begin.line), int32Ty)));
                args.emplace_back(CreateFuncArg(
                    CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(ce->begin.column), int32Ty)));
                auto refreshExpr = CreateRefExprInAST("refreshPos");
                auto callExpr = CreateCallExpr(std::move(refreshExpr), std::move(args));
                ce->desugarExpr = std::move(callExpr);
                return VisitAction::SKIP_CHILDREN;
            }
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker(expr.get(), desugarTokenInQuote);
    walker.Walk();
    if (expr->desugarExpr) {
        return CreateMemberAccess(std::move(expr->desugarExpr), "toTokens");
    }
    return CreateMemberAccess(ASTCloner::Clone(expr.get(), SetIsClonedSourceCode), "toTokens");
}

void DesugarQuoteExpr(QuoteExpr& qe)
{
    if (qe.exprs.empty()) {
        qe.desugarExpr = CreateQuoteDesugarExpr(qe);
    }
    for (auto& expr : qe.exprs) {
        OwnedPtr<Expr> tokensExpr;
        if (expr->astKind == ASTKind::TOKEN_PART) {
            // Tokens(ArrayLiteral(...)).
            auto tokenPart = StaticAs<ASTKind::TOKEN_PART>(expr.get());
            auto arrayLit = DesugarTokensToArrayLiteral(tokenPart->tokens);
            CopyBasicInfo(expr.get(), arrayLit.get());
            std::vector<OwnedPtr<FuncArg>> args;
            (void)args.emplace_back(CreateFuncArg(std::move(arrayLit)));
            auto tksRefExpr = CreateRefExprInAST("Tokens");
            tksRefExpr->begin = expr->begin;
            tksRefExpr->end = expr->end;
            tokensExpr = CreateCallExpr(std::move(tksRefExpr), std::move(args));
            std::vector<OwnedPtr<FuncArg>> args0;
            (void)args0.emplace_back(CreateFuncArg(std::move(tokensExpr)));
            auto refreshExpr = CreateRefExprInAST("refreshTokensPosition");
            refreshExpr->begin = expr->begin;
            refreshExpr->end = expr->end;
            tokensExpr = CreateCallExpr(std::move(refreshExpr), std::move(args0));
        } else if (expr->astKind == ASTKind::QUOTE_EXPR) {
            // dollarExpr is quoteExpr.
            auto quoteExpr = StaticAs<ASTKind::QUOTE_EXPR>(expr.get());
            DesugarQuoteExpr(*quoteExpr);
            tokensExpr = std::move(quoteExpr->desugarExpr);
        } else {
            // a.toTokens().
            auto ma = CreateToTokensMethod(expr);
            auto ce = CreateCallExpr(std::move(ma), {});
            ce->needCheckToTokens = true;
            tokensExpr = std::move(ce);
        }
        tokensExpr->EnableAttr(Attribute::COMPILER_ADD);
        CopyBasicInfo(expr.get(), tokensExpr.get());
        // Tokens1 + Tokens2 => Tokens1.concat(Tokens2).
        if (qe.desugarExpr) {
            auto tokens1 = CreateMemberAccess(ASTCloner::Clone(qe.desugarExpr.get()), "concat");
            tokens1->begin = expr->begin;
            tokens1->end = expr->end;
            std::vector<OwnedPtr<FuncArg>> funcArgs;
            funcArgs.emplace_back(CreateFuncArg(std::move(tokensExpr)));
            qe.desugarExpr = CreateCallExpr(std::move(tokens1), std::move(funcArgs));
            qe.desugarExpr->EnableAttr(Attribute::COMPILER_ADD);
        } else {
            qe.desugarExpr = std::move(tokensExpr);
        }
    }
    auto curFile = qe.curFile;
    Walker assignCurFile(qe.desugarExpr.get(), [&curFile](Ptr<Node> curNode) -> VisitAction {
        curNode->curFile = curFile;
        return VisitAction::WALK_CHILDREN;
    });
    assignCurFile.Walk();
}

void DesugarMacroDecl(File& file)
{
    auto size = file.decls.size();
    for (size_t i = 0; i < size; ++i) {
        if (file.decls[i]->astKind == ASTKind::MACRO_DECL && !file.decls[i]->TestAttr(Attribute::HAS_BROKEN)) {
            auto macroDecl = RawStaticCast<MacroDecl*>(file.decls[i].get());
            if (macroDecl->desugarDecl) {
                continue; // NOTE: During incremental compilation, this may be set.
            }
            auto funcDecl = MakeOwnedNode<FuncDecl>();
            funcDecl->curFile = macroDecl->curFile;
            funcDecl->fullPackageName = macroDecl->curFile->curPackage->fullPackageName;
            funcDecl->begin = macroDecl->begin;
            funcDecl->end = macroDecl->end;
            funcDecl->identifier = macroDecl->identifier;
            funcDecl->modifiers.insert(macroDecl->modifiers.begin(), macroDecl->modifiers.end());
            funcDecl->CloneAttrs(*macroDecl);
            funcDecl->rawMangleName = macroDecl->rawMangleName;
            funcDecl->funcBody = std::move(macroDecl->funcBody);
            funcDecl->end = funcDecl->funcBody->end;
            funcDecl->toBeCompiled = macroDecl->toBeCompiled; // For incremental compilation.
            funcDecl->comments = std::move(macroDecl->comments);
            auto isAttr = funcDecl->funcBody->paramLists.front()->params.size() == 2;
            for (auto& anno : macroDecl->annotations) {
                if (anno->kind == AnnotationKind::DEPRECATED) {
                    funcDecl->annotations.emplace_back(ASTCloner::Clone(anno.get(), SetIsClonedSourceCode));
                }
            }
            macroDecl->desugarDecl = std::move(funcDecl);
            AddMacroFuncDecl(file, macroDecl->end, macroDecl->identifier, isAttr);
        }
    }
}
} // namespace Cangjie
