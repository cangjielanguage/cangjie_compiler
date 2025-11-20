// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements AST create apis.
 */

#include "cangjie/AST/Create.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Match.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/Basic/Position.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/Basic/StringConvertor.h"

namespace Cangjie::AST {
using namespace Meta;

void CopyNodeScopeInfo(Ptr<Node> ret, Ptr<const Node> e)
{
    if (!ret || !e) {
        return;
    }
    ret->curFile = e->curFile;
    ret->scopeName = e->scopeName;
    ret->scopeLevel = e->scopeLevel;
}

void CopyFileID(Ptr<Node> ret, Ptr<const Node> e)
{
    if (!ret || !e) {
        return;
    }
    ret->begin.fileID = e->begin.fileID;
    ret->end.fileID = e->end.fileID;
}

void CopyNodeWithFileID(Ptr<Node> ret, Ptr<const Node> e)
{
    if (!ret || !e) {
        return;
    }
    CopyNodeScopeInfo(ret, e);
    CopyFileID(ret, e);
}

OwnedPtr<Expr> CreateUnitExpr(Ptr<Ty> ty)
{
    OwnedPtr<LitConstExpr> ret = MakeOwned<LitConstExpr>();
    ret->kind = LitConstKind::UNIT;
    ret->stringValue = "()";
    if (Ty::IsTyCorrect(ty)) {
        ret->ty = ty;
    }
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<UnaryExpr> CreateUnaryExpr(OwnedPtr<Expr> expr, TokenKind op)
{
    OwnedPtr<UnaryExpr> ret = MakeOwned<UnaryExpr>();
    CJC_NULLPTR_CHECK(expr);
    CJC_ASSERT(op == TokenKind::ADD || op == TokenKind::SUB || op == TokenKind::NOT);
    ret->expr = std::move(expr);
    ret->op = op;
    ret->ty = ret->expr->ty;
    CopyNodeScopeInfo(ret.get(), ret->expr.get());
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<BinaryExpr> CreateBinaryExpr(
    OwnedPtr<Expr> leftExpr, OwnedPtr<Expr> rightExpr, TokenKind op)
{
    OwnedPtr<BinaryExpr> ret = MakeOwned<BinaryExpr>();
    CJC_NULLPTR_CHECK(leftExpr);
    CJC_NULLPTR_CHECK(rightExpr);
    CJC_ASSERT((op >= TokenKind::EXP && op <= TokenKind::SUB) ||
        (op >= TokenKind::AND && op <= TokenKind::COMPOSITION) ||
        (op >= TokenKind::BITAND && op <= TokenKind::BITXOR) || (op >= TokenKind::LSHIFT && op <= TokenKind::RSHIFT) ||
        (op >= TokenKind::LT && op <= TokenKind::EQUAL));
    ret->leftExpr = std::move(leftExpr);
    ret->rightExpr = std::move(rightExpr);
    ret->ty = ret->leftExpr->ty;
    ret->op = op;
    CopyNodeScopeInfo(ret.get(), ret->leftExpr.get());
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<JumpExpr> CreateBreakExpr(Expr& refLoop)
{
    OwnedPtr<JumpExpr> ret = MakeOwned<JumpExpr>();
    ret->isBreak = true;
    ret->refLoop = &refLoop;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<LitConstExpr> CreateBoolLit(bool isTrue)
{
    OwnedPtr<LitConstExpr> ret = MakeOwned<LitConstExpr>();
    ret->kind = LitConstKind::BOOL;
    ret->stringValue = isTrue ? "true" : "false";
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<ReturnExpr> CreateReturnExpr(OwnedPtr<Expr> expr, Ptr<FuncBody> refFuncBody)
{
    auto ret = MakeOwned<ReturnExpr>();
    CJC_NULLPTR_CHECK(expr);
    ret->expr = std::move(expr);
    CopyNodeScopeInfo(ret.get(), ret->expr.get());
    ret->EnableAttr(Attribute::COMPILER_ADD);
    if (refFuncBody) {
        ret->refFuncBody = refFuncBody;
    }
    return ret;
}

OwnedPtr<ForInExpr> CreateForInExpr(
    OwnedPtr<Pattern>&& pattern, OwnedPtr<Expr>&& inExpression, OwnedPtr<Block>&& body)
{
    auto ret = MakeOwned<ForInExpr>();
    ret->pattern = std::move(pattern);
    ret->inExpression = std::move(inExpression);
    ret->body = std::move(body);
    ret->EnableAttr(Attribute::COMPILER_ADD);
    CJC_NULLPTR_CHECK(ret->pattern);
    CJC_NULLPTR_CHECK(ret->inExpression);
    CJC_NULLPTR_CHECK(ret->body);
    return ret;
}

OwnedPtr<AssignExpr> CreateAssignExpr(OwnedPtr<Expr> leftValue, OwnedPtr<Expr> rightExpr, Ptr<Ty> ty)
{
    auto ret = MakeOwned<AssignExpr>();
    CJC_NULLPTR_CHECK(leftValue);
    CJC_NULLPTR_CHECK(rightExpr);
    ret->leftValue = std::move(leftValue);
    ret->rightExpr = std::move(rightExpr);
    ret->op = TokenKind::ASSIGN;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    CopyNodeScopeInfo(ret.get(), ret->leftValue.get());
    if (Ty::IsTyCorrect(ty)) {
        ret->ty = ty;
    }
    return ret;
}

OwnedPtr<FuncArg> CreateFuncArg(OwnedPtr<Expr> expr, const std::string& argName, Ptr<Ty> ty)
{
    auto ret = MakeOwned<FuncArg>();
    CJC_NULLPTR_CHECK(expr);
    CopyBasicInfo(expr.get(), ret.get());
    if (expr->astKind == ASTKind::BLOCK) {
        // NOTE: Block should be attached to 'desugarExpr'.
        // Create a dummy desugar block expr, in order to create a CHIR `Goto` node.
        auto re = CreateRefExpr("$dummy");
        re->ty = expr->ty;
        re->desugarExpr = std::move(expr);
        ret->expr = std::move(re);
    } else {
        ret->expr = std::move(expr);
    }
    ret->name = argName;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    ret->ty = Ty::IsTyCorrect(ty) ? ty : ret->expr->ty;
    return ret;
}

OwnedPtr<RefType> CreateRefTypeInCore(const std::string& name)
{
    OwnedPtr<RefType> ret = MakeOwned<RefType>();
    ret->ref.identifier = name;
    ret->EnableAttr(Attribute::IN_CORE);
    return ret;
}

OwnedPtr<FuncArg> CreateFuncArgForOptional(const FuncParam& param)
{
    auto arg = MakeOwned<FuncArg>();
    arg->name = param.identifier;
    arg->begin = param.begin;
    arg->expr = ASTCloner::Clone<Expr>(param.assignment.get());
    arg->EnableAttr(Attribute::COMPILER_ADD);
    arg->ty = param.ty;
    arg->end = param.end;
    CopyNodeScopeInfo(arg.get(), &param);
    return arg;
}

OwnedPtr<FuncParam> CreateFuncParamForOptional(const FuncParam& param)
{
    auto pa = MakeOwned<FuncParam>();
    pa->identifier = param.identifier;
    pa->begin = param.begin;
    pa->type = ASTCloner::Clone<Type>(param.type.get());
    pa->ty = param.ty;
    pa->curFile = param.curFile;
    pa->end = param.end;
    pa->EnableAttr(Attribute::COMPILER_ADD);
    CopyNodeScopeInfo(pa.get(), &param);
    return pa;
}

OwnedPtr<FuncDecl> CreateFuncDecl(const std::string& funcName, OwnedPtr<FuncBody> body, Ptr<Ty> ty)
{
    auto ret = MakeOwned<FuncDecl>();
    ret->identifier = funcName;
    ret->funcBody = std::move(body);
    if (Ty::IsTyCorrect(ty)) {
        ret->ty = ty;
    } else if (ret->funcBody) {
        ret->funcBody->funcDecl = ret.get();
        CopyNodeScopeInfo(ret.get(), ret->funcBody.get());
        ret->ty = ret->funcBody->ty;
    }
    ret->toBeCompiled = true; // For incremental compilation.
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<FuncBody> CreateFuncBody(std::vector<OwnedPtr<FuncParamList>> paramLists,
    OwnedPtr<AST::Type> retType, OwnedPtr<Block> body, Ptr<Ty> ty)
{
    auto ret = MakeOwned<FuncBody>();
    ret->paramLists = std::move(paramLists);
    ret->retType = std::move(retType);
    ret->body = std::move(body);
    ret->EnableAttr(Attribute::COMPILER_ADD);
    if (ret->body) {
        CopyNodeScopeInfo(ret.get(), ret->body.get());
    }
    if (Ty::IsTyCorrect(ty)) {
        ret->ty = ty;
    }
    return ret;
}

OwnedPtr<FuncParam> CreateFuncParam(
    const std::string& paramName, OwnedPtr<AST::Type> paramType, OwnedPtr<Expr> paramValue, Ptr<Ty> ty)
{
    auto ret = MakeOwned<FuncParam>();
    ret->identifier = paramName;
    ret->type = std::move(paramType);
    ret->assignment = std::move(paramValue);
    ret->EnableAttr(Attribute::COMPILER_ADD);
    if (ret->assignment) {
        CopyNodeScopeInfo(ret.get(), ret->assignment.get());
    }
    if (Ty::IsTyCorrect(ty)) {
        ret->ty = ty;
    } else if (ret->type) {
        ret->ty = ret->type->ty;
    }
    return ret;
}

OwnedPtr<FuncParamList> CreateFuncParamList(std::vector<Ptr<FuncParam>> params, const VisitFunc& visitor)
{
    auto ret = MakeOwned<FuncParamList>();
    params.erase(std::remove_if(params.begin(), params.end(), [](auto& e) { return !e; }), params.end());
    for (auto& it : params) {
        auto funcParam = MakeOwned<FuncParam>();
        auto d = ASTCloner::Clone<Decl>(it, visitor);
        funcParam.reset(As<ASTKind::FUNC_PARAM>(d.release()));
        CopyNodeScopeInfo(ret.get(), funcParam.get());
        ret->params.push_back(std::move(funcParam));
    }
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<Block> CreateBlock(std::vector<OwnedPtr<Node>> nodes, Ptr<Ty> ty)
{
    auto ret = MakeOwned<Block>();
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [](auto& e) { return e == nullptr; }), nodes.end());
    for (auto& it : nodes) {
        CJC_ASSERT(it->IsDecl() || it->IsExpr());
        if (it->IsDecl()) {
            ret->body.push_back(std::move(it));
        } else if (it->IsExpr()) {
            ret->body.push_back(std::move(it));
        }
    }
    ret->EnableAttr(Attribute::COMPILER_ADD);
    if (Ty::IsTyCorrect(ty)) {
        ret->ty = ty;
    }
    return ret;
}
OwnedPtr<VarDecl> CreateVarDecl(const std::string& varName, OwnedPtr<Expr> initializer, Ptr<AST::Type> type)
{
    auto ret = MakeOwned<VarDecl>();
    ret->identifier = varName;
    ret->type = ASTCloner::Clone(type);
    ret->initializer = std::move(initializer);
    ret->EnableAttr(Attribute::COMPILER_ADD, Attribute::IMPLICIT_ADD);
    if (ret->initializer) {
        ret->ty = ret->initializer->ty;
        CopyNodeScopeInfo(ret.get(), ret->initializer.get());
    }
    ret->toBeCompiled = true; // For incremental compilation.
    return ret;
}

OwnedPtr<VarDecl> CreateTmpVarDecl(Ptr<Type> type, Ptr<Expr> initializer)
{
    static std::atomic_uint i = 1;
    auto tmpVarName = "$tmp" + std::to_string(i);
    ++i;
    return CreateVarDecl(tmpVarName, ASTCloner::Clone(initializer), type);
}

OwnedPtr<VarPattern> CreateVarPattern(const std::string& varName, Ptr<AST::Ty> ty)
{
    auto v = MakeOwned<VarPattern>();
    v->varDecl = MakeOwned<VarDecl>();
    v->varDecl->identifier = varName;
    v->varDecl->parentPattern = v.get();
    v->varDecl->EnableAttr(Attribute::COMPILER_ADD, Attribute::IMPLICIT_ADD);
    if (Ty::IsTyCorrect(ty)) {
        v->ty = ty;
        v->varDecl->ty = ty;
    }
    v->EnableAttr(Attribute::COMPILER_ADD);
    return v;
}

OwnedPtr<RefExpr> CreateRefExpr(const SrcIdentifier& id, Ptr<Ty> ty, const Position& pos, std::vector<Ptr<Type>> args)
{
    auto ret = MakeOwned<RefExpr>();
    ret->ref.identifier = id;
    ret->begin = pos;
    ret->end = pos;
    if (!id.IsRaw()) {
        ret->isThis = ret->ref.identifier == "this";
        ret->isSuper = ret->ref.identifier == "super";
    }
    for (auto type : args) {
        CJC_NULLPTR_CHECK(type);
        ret->typeArguments.emplace_back(ASTCloner::Clone<Type>(type));
    }
    if (Ty::IsTyCorrect(ty)) {
        ret->ty = ty;
    }
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<RefExpr> CreateRefExpr(const std::string& name, const Position& pos)
{
    return CreateRefExpr({name, pos, pos, false}, nullptr, pos);
}

OwnedPtr<RefExpr> CreateRefExprInCore(const std::string& name)
{
    OwnedPtr<RefExpr> ret = CreateRefExpr(name);
    ret->EnableAttr(Attribute::IN_CORE);
    return ret;
}

OwnedPtr<RefExpr> CreateRefExprInAST(const std::string& name)
{
    OwnedPtr<RefExpr> ret = CreateRefExpr(name);
    ret->EnableAttr(Attribute::IN_MACRO);
    return ret;
}

OwnedPtr<RefExpr> CreateRefExpr(Decl& vd)
{
    auto ret = MakeOwned<RefExpr>();
    ret->ref.identifier = vd.identifier;
    ret->ref.target = &vd;
    ret->ty = vd.ty;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}
OwnedPtr<RefExpr> CreateRefExpr(Decl& vd, const Node& pos)
{
    auto ret = MakeOwned<RefExpr>();
    ret->ref.identifier = {vd.identifier.Val(), pos.GetBegin(), pos.GetEnd()};
    ret->ref.target = &vd;
    ret->ty = vd.ty;
    ret->begin = pos.GetBegin();
    ret->end = pos.GetEnd();
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<RefType> CreateRefType(const std::string& refName, std::vector<Ptr<Type>> args)
{
    auto ret = MakeOwned<RefType>();
    ret->ref.identifier = refName;
    for (auto type : args) {
        ret->typeArguments.emplace_back(ASTCloner::Clone<Type>(type));
    }
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<RefType> CreateRefType(InheritableDecl& typeDecl)
{
    auto ret = MakeOwned<RefType>();
    ret->ref.identifier = typeDecl.identifier;
    ret->ref.target = &typeDecl;
    ret->ty = typeDecl.ty;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<ArrayLit> CreateArrayLit(std::vector<OwnedPtr<Expr>> elements, Ptr<Ty> ty)
{
    auto ret = MakeOwned<ArrayLit>();
    auto arrayType = DynamicCast<StructTy*>(ty);
    CJC_ASSERT(arrayType);
    ret->ty = ty;
    ret->children = std::move(elements);
    return ret;
}

OwnedPtr<MemberAccess> CreateMemberAccess(OwnedPtr<Expr> expr, const std::string& field)
{
    auto memberAccess = MakeOwned<MemberAccess>();
    memberAccess->field = field;
    memberAccess->EnableAttr(Attribute::COMPILER_ADD);
    if (expr->astKind == ASTKind::BLOCK) {
        // NOTE: Block should be attached to 'desugarExpr'.
        // Create a dummy desugar block expr, in order to create a CHIR `Goto` node.
        auto re = CreateRefExpr("$dummy");
        re->ty = expr->ty;
        re->desugarExpr = std::move(expr);
        memberAccess->baseExpr = std::move(re);
    } else {
        memberAccess->baseExpr = std::move(expr);
    }
    CopyBasicInfo(memberAccess->baseExpr.get(), memberAccess.get());
    if (!memberAccess->baseExpr || !Ty::IsTyCorrect(memberAccess->baseExpr->ty)) {
        return memberAccess;
    }
    // Only if there is a unique implementation name in the declaration can be handled correctly.
    auto foundMember = [&memberAccess, &field](const InheritableDecl& decl) {
        for (auto fieldDecl : decl.GetMemberDeclPtrs()) {
            if (fieldDecl->identifier == field) {
                memberAccess->ty = fieldDecl->ty;
                memberAccess->target = fieldDecl;
                return true;
            }
        }
        return false;
    };
    if (auto classType = DynamicCast<ClassTy*>(memberAccess->baseExpr->ty); classType) {
        auto currentClass = classType->decl;
        CJC_NULLPTR_CHECK(currentClass);
        while (currentClass != nullptr && Ty::IsInitialTy(memberAccess->ty)) {
            if (foundMember(*currentClass)) {
                break;
            }
            currentClass = currentClass->GetSuperClassDecl();
        }
    } else if (memberAccess->baseExpr->ty->IsNominal()) {
        auto decl = Ty::GetDeclOfTy<InheritableDecl>(memberAccess->baseExpr->ty);
        CJC_NULLPTR_CHECK(decl);
        (void)foundMember(*decl);
    } else if (memberAccess->baseExpr->ty->IsBuiltin()) {
        // extended methods
    } else {
        CJC_ABORT();
    }
    return memberAccess;
}

OwnedPtr<AST::MemberAccess> CreateMemberAccess(OwnedPtr<AST::Expr> expr, Decl& field)
{
    auto ret = MakeOwned<MemberAccess>();
    if (expr->astKind == ASTKind::BLOCK) {
        // NOTE: Block should be attached to 'desugarExpr'.
        // Create a dummy desugar block expr, in order to create a CHIR `Goto` node.
        auto re = CreateRefExpr("$dummy");
        re->ty = expr->ty;
        re->desugarExpr = std::move(expr);
        ret->baseExpr = std::move(re);
    } else {
        ret->baseExpr = std::move(expr);
    }
    ret->field = field.identifier.Val();
    ret->field.SetRaw(field.identifier.IsRaw());
    ret->target = &field;
    ret->ty = field.ty;
    ret->isExposedAccess = field.ty && field.ty->IsGeneric();
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<GenericConstraint> CreateConstraintForFFI(const std::string& upperBound)
{
    auto gc = MakeOwned<GenericConstraint>();
    OwnedPtr<RefType> t = MakeOwned<RefType>();
    t->ref.identifier = "T";
    gc->type = std::move(t);
    gc->begin = DEFAULT_POSITION;
    gc->end = DEFAULT_POSITION;

    OwnedPtr<RefType> ctype = MakeOwned<RefType>();
    ctype->ref.identifier = upperBound;
    gc->upperBounds.emplace_back(std::move(ctype));
    return gc;
}

OwnedPtr<MatchCase> CreateMatchCase(OwnedPtr<Pattern> pattern, OwnedPtr<Expr> expr)
{
    auto ty = expr->ty;
    auto matchCase = MakeOwned<MatchCase>();
    matchCase->EnableAttr(Attribute::COMPILER_ADD);
    matchCase->patterns.emplace_back(std::move(pattern));
    matchCase->ty = ty;
    auto caseBlock = MakeOwned<Block>();
    caseBlock->EnableAttr(Attribute::COMPILER_ADD);
    caseBlock->body.emplace_back(std::move(expr));
    caseBlock->ty = ty;
    matchCase->exprOrDecls = std::move(caseBlock);
    return matchCase;
}

OwnedPtr<MatchExpr> CreateMatchExpr(OwnedPtr<Expr> selector,
    std::vector<OwnedPtr<MatchCase>> matchCases, Ptr<Ty> ty, Expr::SugarKind sugarKind)
{
    auto matchExpr = MakeOwned<MatchExpr>();
    matchExpr->EnableAttr(Attribute::COMPILER_ADD);
    matchExpr->matchMode = true;
    matchExpr->selector = std::move(selector);
    matchExpr->matchCases = std::move(matchCases);
    matchExpr->sugarKind = sugarKind;
    matchExpr->ty = ty;
    return matchExpr;
}

OwnedPtr<LitConstExpr> CreateLitConstExpr(LitConstKind kind, const std::string& val, Ptr<Ty> ty, bool needToMakeRef)
{
    auto ret = MakeOwned<LitConstExpr>();
    ret->kind = kind;
    ret->stringValue = val;
    if (kind == LitConstKind::RUNE) {
        ret->codepoint = StringConvertor::UTF8ToCodepoint(val);
    }
    ret->ty = ty;
    if (!Ty::IsTyCorrect(ty)) {
        return ret;
    }
    if (ty->IsFloating()) {
        ret->constNumValue.asFloat.value = std::stold(val);
    } else if (ty->IsInteger()) {
        if (ty->IsUnsignedInteger()) {
            ret->constNumValue.asInt.SetUint64(std::stoull(val));
        } else {
            ret->constNumValue.asInt.SetInt64(std::stoll(val));
        }
    } else if (ty->IsBoolean()) {
        if (val == "true") {
            ret->constNumValue.asBoolean = true;
        } else if (val == "false") {
            ret->constNumValue.asBoolean = false;
        } else {
            CJC_ABORT();
        }
    } else if (ty->IsString()) {
        ret->stringKind = StringKind::NORMAL;
    } else if (ty->kind != TypeKind::TYPE_RUNE && !ty->IsUnit()) {
        CJC_ABORT();
    }
    if (needToMakeRef) {
        if (auto decl = Ty::GetDeclOfTy<InheritableDecl>(ty); decl) {
            ret->ref = CreateRefType(*decl);
        }
    }
    return ret;
}

OwnedPtr<TupleLit> CreateTupleLit(std::vector<OwnedPtr<Expr>> elements, Ptr<Ty> ty)
{
    auto ret = MakeOwned<TupleLit>();
    auto tupleType = DynamicCast<TupleTy*>(ty);
    CJC_ASSERT(tupleType != nullptr);
    ret->ty = ty;
    ret->children = std::move(elements);
    return ret;
}

OwnedPtr<CallExpr> CreateCallExpr(OwnedPtr<Expr> funcExpr,
    std::vector<OwnedPtr<FuncArg>> args, Ptr<FuncDecl> resolvedFunc, Ptr<Ty> ty, CallKind callTy)
{
    auto ret = MakeOwned<CallExpr>();
    CJC_NULLPTR_CHECK(funcExpr);
    if (Is<MemberAccess>(funcExpr.get())) {
        auto ma = StaticAs<ASTKind::MEMBER_ACCESS>(funcExpr.get());
        ma->callOrPattern = ret.get();
    }
    if (Is<RefExpr>(funcExpr.get())) {
        auto re = StaticAs<ASTKind::REF_EXPR>(funcExpr.get());
        re->callOrPattern = ret.get();
    }
    ret->baseFunc = std::move(funcExpr);
    ret->args = std::move(args);
    ret->callKind = callTy;
    if (Ty::IsTyCorrect(ty)) {
        ret->ty = ty;
    }
    CopyBasicInfo(ret->baseFunc.get(), ret.get());
    if (resolvedFunc != nullptr) {
        ret->resolvedFunction = resolvedFunc;
        if (resolvedFunc->TestAttr(Attribute::CONSTRUCTOR)) {
            if (Is<StructTy*>(ty)) {
                ret->callKind = CallKind::CALL_STRUCT_CREATION;
            } else if (Is<ClassTy*>(ty)) {
                ret->callKind = CallKind::CALL_OBJECT_CREATION;
            }
        }
        if (ret->callKind == CallKind::CALL_INVALID) {
            ret->callKind = CallKind::CALL_DECLARED_FUNCTION;
        }
    }
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<LambdaExpr> CreateLambdaExpr(OwnedPtr<FuncBody> funcBody)
{
    auto ret = MakeOwned<LambdaExpr>(std::move(funcBody));
    ret->ty = ret->funcBody->ty;
    return ret;
}

OwnedPtr<FuncParamList> CreateFuncParamList(std::vector<OwnedPtr<FuncParam>> params, Ptr<Ty> ty)
{
    auto ret = MakeOwned<FuncParamList>();
    ret->params = std::move(params);
    if (Ty::IsTyCorrect(ty)) {
        ret->ty = ty;
    }
    return ret;
}

OwnedPtr<IfExpr> CreateIfExpr(
    OwnedPtr<Expr> condExpr, OwnedPtr<Block> body, OwnedPtr<Block> elseBody, Ptr<Ty> semaType)
{
    auto ret = MakeOwned<IfExpr>();
    ret->condExpr = std::move(condExpr);
    ret->thenBody = std::move(body);
    ret->elseBody = std::move(elseBody);
    if (Ty::IsTyCorrect(semaType)) {
        ret->ty = semaType;
    } else if (ret->thenBody && ret->elseBody && ret->thenBody->ty == ret->elseBody->ty) {
        ret->ty = ret->thenBody->ty;
    }
    ret->hasElse = ret->elseBody != nullptr;
    ret->EnableAttr(Attribute::COMPILER_ADD);
    return ret;
}

OwnedPtr<SubscriptExpr> CreateTupleAccess(OwnedPtr<Expr> expr, size_t index)
{
    auto elem = MakeOwned<SubscriptExpr>();
    if (auto type = DynamicCast<TupleTy*>(expr->ty)) {
        elem->isTupleAccess = true;
        elem->ty = type->typeArgs[index];
        elem->baseExpr = std::move(expr);
        elem->indexExprs.emplace_back(MakeOwned<LitConstExpr>(LitConstKind::INTEGER, std::to_string(index)));
        elem->indexExprs[0]->constNumValue.asInt.SetUint64(index);
        return elem;
    }
    CJC_ABORT();
    return nullptr;
}

OwnedPtr<ThrowExpr> CreateThrowExpr(Decl& var)
{
    auto refExpr = MakeOwned<RefExpr>();
    refExpr->ref.identifier = var.identifier;
    refExpr->ref.target = &var;
    auto ret = MakeOwned<ThrowExpr>();
    ret->expr = std::move(refExpr);
    return ret;
}

OwnedPtr<PerformExpr> CreatePerformExpr(Decl& var)
{
    auto refExpr = MakeOwned<RefExpr>();
    refExpr->ref.identifier = var.identifier;
    refExpr->ref.target = &var;
    auto ret = MakeOwned<PerformExpr>();
    ret->expr = std::move(refExpr);
    return ret;
}

OwnedPtr<TypePattern> CreateTypePattern(
    OwnedPtr<Pattern> && pattern, OwnedPtr<Type> && type, Expr& selector
)
{
    Ptr<Ty> ty = type->ty;
    CJC_NULLPTR_CHECK(ty);
    CJC_NULLPTR_CHECK(selector.ty);
    auto typePattern = MakeOwned<TypePattern>();
    typePattern->EnableAttr(AST::Attribute::COMPILER_ADD);
    typePattern->ctxExpr = &selector;
    typePattern->pattern = std::move(pattern);
    typePattern->type = std::move(type);
    typePattern->pattern->ty = ty;
    typePattern->ty = ty;
    typePattern->curFile = selector.curFile;
    return typePattern;
}

OwnedPtr<ImportSpec> CreateImportSpec(
    const std::string& fullPackageName, const std::string& item, const std::string& alias)
{
    auto import = MakeOwned<ImportSpec>();
    auto names = Utils::SplitQualifiedName(fullPackageName);
    CJC_ASSERT(names.size() >= 1 && !item.empty());
    if (!alias.empty()) {
        import->content.kind = ImportKind::IMPORT_ALIAS;
    } else if (item != "*") {
        import->content.kind = ImportKind::IMPORT_SINGLE;
    } else {
        import->content.kind = ImportKind::IMPORT_ALL;
    }
    import->content.prefixPaths = names;
    import->content.identifier = item;
    import->content.identifier.SetPos(DEFAULT_POSITION, DEFAULT_POSITION);
    import->content.aliasName = alias;
    import->content.aliasName.SetPos(DEFAULT_POSITION, DEFAULT_POSITION);
    import->EnableAttr(Attribute::COMPILER_ADD, Attribute::IMPLICIT_ADD, Attribute::PRIVATE);
    return import;
}
} // namespace Cangjie::AST
