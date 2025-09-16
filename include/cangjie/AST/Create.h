// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares AST create apis.
 */

#ifndef CANGJIE_AST_CREATE_H
#define CANGJIE_AST_CREATE_H

#include <memory>
#include <string>

#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Node.h"

namespace Cangjie::AST {
void CopyNodeScopeInfo(Ptr<Node> ret, Ptr<const Node> e);
void CopyFileID(Ptr<Node> ret, Ptr<const Node> e);
void CopyNodeWithFileID(Ptr<Node> ret, Ptr<const Node> e);
/** Create an expression node of type Unit. */
OwnedPtr<Expr> CreateUnitExpr(Ptr<Ty> ty = nullptr);
OwnedPtr<LitConstExpr> CreateBoolLit(bool isTrue);

OwnedPtr<ForInExpr> CreateForInExpr(
    OwnedPtr<Pattern>&& pattern, OwnedPtr<Expr>&& inExpression, OwnedPtr<Block>&& body);
OwnedPtr<JumpExpr> CreateBreakExpr(Expr& refLoop);
OwnedPtr<CallExpr> CreateCallExpr(OwnedPtr<Expr> funcExpr,
    std::vector<OwnedPtr<FuncArg>> args, Ptr<FuncDecl> resolvedFunc = nullptr, Ptr<Ty> ty = nullptr,
    CallKind callTy = CallKind::CALL_INVALID);
OwnedPtr<FuncArg> CreateFuncArgForOptional(const FuncParam& param);
OwnedPtr<FuncParam> CreateFuncParamForOptional(const FuncParam& param);
/** Create FuncParamList node. */
OwnedPtr<FuncParamList> CreateFuncParamList(
    std::vector<Ptr<FuncParam>> params, const VisitFunc& visitor = DefaultVisitFunc);
/** Create Type in core node. */
OwnedPtr<RefType> CreateRefTypeInCore(const std::string& name);
OwnedPtr<VarDecl> CreateTmpVarDecl(Ptr<Type> type = nullptr, Ptr<Expr> initializer = nullptr);
OwnedPtr<VarPattern> CreateVarPattern(const std::string& varName, Ptr<Ty> ty = nullptr);
/** Create RefExpr node. */
OwnedPtr<RefExpr> CreateRefExpr(
    const SrcIdentifier& id, Ptr<Ty> ty = nullptr, const Position& pos = {0, 0, 0}, std::vector<Ptr<Type>> args = {});
/**
 * Create a compiler-generated RefExpr node. This node may not have proper location info.
 * Note that this funnction do not add COMPILER_ADD attribute to the result expr.
 */
OwnedPtr<RefExpr> CreateRefExpr(const std::string& name, const Position& pos = {0, 0, 0});
OwnedPtr<RefExpr> CreateRefExprInCore(const std::string& name);
OwnedPtr<RefExpr> CreateRefExprInAST(const std::string& name);
OwnedPtr<RefExpr> CreateRefExpr(Decl& vd);
/// \p pos copy begin and end from node \ref pos
OwnedPtr<RefExpr> CreateRefExpr(Decl& vd, const Node& pos);
/** Create RefType node */
OwnedPtr<RefType> CreateRefType(const std::string& refName, std::vector<Ptr<Type>> args = {});
OwnedPtr<RefType> CreateRefType(InheritableDecl& typeDecl);
/** Create MemberAccess node with given target sema. */
OwnedPtr<MemberAccess> CreateMemberAccess(OwnedPtr<Expr> expr, Decl& field);
OwnedPtr<MemberAccess> CreateMemberAccess(OwnedPtr<Expr> expr, const std::string& field);
/** Create CType Generic Constraint */
OwnedPtr<GenericConstraint> CreateConstraintForFFI(const std::string& upperBound);
OwnedPtr<MatchCase> CreateMatchCase(OwnedPtr<Pattern> pattern, OwnedPtr<Expr> expr);
OwnedPtr<MatchExpr> CreateMatchExpr(OwnedPtr<Expr> selector,
    std::vector<OwnedPtr<MatchCase>> matchCases, Ptr<Ty> ty,
    Expr::SugarKind sugarKind = Expr::SugarKind::NO_SUGAR);
OwnedPtr<LitConstExpr> CreateLitConstExpr(
    LitConstKind kind, const std::string& val, Ptr<Ty> ty, bool needToMakeRef = false
);
OwnedPtr<TupleLit> CreateTupleLit(std::vector<OwnedPtr<Expr>> elements, Ptr<Ty> ty);
OwnedPtr<ArrayLit> CreateArrayLit(std::vector<OwnedPtr<Expr>> elements, Ptr<Ty> ty);
OwnedPtr<SubscriptExpr> CreateTupleAccess(OwnedPtr<Expr> expr, size_t index);
OwnedPtr<UnaryExpr> CreateUnaryExpr(OwnedPtr<Expr> expr, TokenKind op);
OwnedPtr<BinaryExpr> CreateBinaryExpr(
    OwnedPtr<Expr> leftExpr, OwnedPtr<Expr> rightExpr, TokenKind op);
OwnedPtr<ReturnExpr> CreateReturnExpr(OwnedPtr<Expr> expr, Ptr<FuncBody> refFuncBody = nullptr);
OwnedPtr<LambdaExpr> CreateLambdaExpr(OwnedPtr<FuncBody> funcBody);
OwnedPtr<AssignExpr> CreateAssignExpr(
    OwnedPtr<Expr> leftValue, OwnedPtr<Expr> rightExpr, Ptr<Ty> ty = nullptr);
OwnedPtr<FuncArg> CreateFuncArg(OwnedPtr<Expr> expr, const std::string& argName = "", Ptr<Ty> ty = nullptr);
OwnedPtr<FuncDecl> CreateFuncDecl(
    const std::string& funcName, OwnedPtr<FuncBody> body = nullptr, Ptr<Ty> ty = nullptr);
OwnedPtr<FuncBody> CreateFuncBody(std::vector<OwnedPtr<FuncParamList>> paramLists,
    OwnedPtr<Type> retType, OwnedPtr<Block> body, Ptr<Ty> ty = nullptr);
OwnedPtr<FuncParam> CreateFuncParam(const std::string& paramName, OwnedPtr<Type> paramType = nullptr,
    OwnedPtr<Expr> paramValue = nullptr, Ptr<Ty> ty = nullptr);
OwnedPtr<FuncParamList> CreateFuncParamList(std::vector<OwnedPtr<FuncParam>> params, Ptr<Ty> ty = nullptr);
OwnedPtr<Block> CreateBlock(std::vector<OwnedPtr<Node>> nodes, Ptr<Ty> ty = nullptr);
OwnedPtr<IfExpr> CreateIfExpr(OwnedPtr<Expr> condExpr, OwnedPtr<Block> body,
    OwnedPtr<Block> elseBody = nullptr, Ptr<Ty> semaType = nullptr);
OwnedPtr<VarDecl> CreateVarDecl(
    const std::string& varName, OwnedPtr<Expr> initializer = nullptr, Ptr<Type> type = nullptr);
OwnedPtr<ThrowExpr> CreateThrowExpr(Decl& var);
OwnedPtr<PerformExpr> CreatePerformExpr(Decl& var);
OwnedPtr<ResumeExpr> CreateResumeExpr(Decl& var);
OwnedPtr<TypePattern> CreateTypePattern(
    OwnedPtr<Pattern>&& pattern, OwnedPtr<Type>&& type, Expr& selector
);
} // namespace Cangjie::AST

#endif // CANGJIE_AST_CREATE_H
