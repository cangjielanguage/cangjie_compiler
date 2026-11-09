// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements inheritance checking of structure declarations.
 */
#include "StructInheritanceChecker.h"

#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Sema/TypeManager.h"

#include "BuiltInOperatorUtil.h"
#include "TypeCheckUtil.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;

bool StructInheritanceChecker::IsBuiltInOperatorFuncInExtend(
    const MemberSignature& member, const Decl& structDecl) const
{
    if (structDecl.astKind != ASTKind::EXTEND_DECL || !member.decl->IsFunc() || !member.decl->GetTy().IsCorrect() ||
        !member.decl->TestAttr(Attribute::ABSTRACT, Attribute::OPERATOR)) {
        return false;
    }
    auto ed = RawStaticCast<const ExtendDecl*>(&structDecl);
    auto fd = RawStaticCast<const FuncDecl*>(member.decl);
    auto funcTy = RawStaticCast<FuncTy*>(member.ty);
    auto iFuncRetTy = funcTy->retTy;
    const std::vector<ModalTy>& paramTys = funcTy->paramTys;
    ModalTy thisTy = ed->extendedType->GetTy();
    if (paramTys.size() == 1 && thisTy && paramTys[0] && IsBuiltinBinaryExpr(fd->op, *thisTy, *paramTys[0])) {
        TypeKind returnTyKind = GetBuiltinBinaryExprReturnKind(fd->op, thisTy->kind);
        auto expectedRetTy = TypeManager::GetPrimitiveTy(returnTyKind);
        if (ModalTy{expectedRetTy} == iFuncRetTy) {
            CreateBuiltInBinaryOperatorFunc(fd->op, paramTys[0], *const_cast<ExtendDecl*>(ed), returnTyKind);
        } else {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_return_type_incompatible, structDecl, fd->identifier);
        }
        return true;
    } else if (paramTys.empty() && thisTy && IsBuiltinUnaryExpr(fd->op, *thisTy)) {
        TypeKind returnTyKind = GetBuiltinUnaryOpReturnKind(fd->op, ed->TyKind());
        auto expectedRetTy = TypeManager::GetPrimitiveTy(returnTyKind);
        if (ModalTy{expectedRetTy} == iFuncRetTy) {
            CreateBuiltInUnaryOperatorFunc(fd->op, *const_cast<ExtendDecl*>(ed));
        } else {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_return_type_incompatible, structDecl, fd->identifier);
        }
        return true;
    }
    return false;
}

void StructInheritanceChecker::CreateBuiltInUnaryOperatorFunc(TokenKind op, ExtendDecl& ed) const
{
    TypeKind returnTyKind = GetBuiltinUnaryOpReturnKind(op, ed.TyKind());
    auto returnTy = TypeManager::GetPrimitiveTy(returnTyKind);
    auto nothingTy = TypeManager::GetNothingTy();

    auto fd = MakeOwnedNode<FuncDecl>();
    fd->toBeCompiled = true; // For incremental compilation.
    fd->EnableAttr(Attribute::IN_EXTEND, Attribute::PUBLIC, Attribute::OPERATOR, Attribute::IMPLICIT_ADD);
    CopyBasicInfo(&ed, fd.get());
    fd->moduleName = ed.moduleName;
    fd->fullPackageName = ed.fullPackageName;
    fd->op = op;
    fd->identifier = SrcIdentifier{TOKENS[static_cast<int>(op)]};
    fd->SetTy({typeManager.GetFunctionTy({}, {returnTy})});
    fd->outerDecl = &ed;

    auto funcBody = MakeOwnedNode<FuncBody>();
    funcBody->paramLists.emplace_back(MakeOwnedNode<FuncParamList>());
    funcBody->funcDecl = fd.get();
    funcBody->SetTy(fd->GetTy());
    auto retType = MakeOwnedNode<PrimitiveType>();
    retType->kind = returnTyKind;
    retType->SetTy({returnTy});
    funcBody->retType = std::move(retType);

    auto block = MakeOwnedNode<Block>();
    block->SetTy({nothingTy});

    auto thisExpr = MakeOwnedNode<RefExpr>();
    thisExpr->isThis = true;
    thisExpr->ref.identifier = "this";
    thisExpr->SetTy(ed.GetTy());

    auto ue = MakeOwnedNode<UnaryExpr>();
    ue->op = op;
    ue->expr = std::move(thisExpr);
    ue->SetTy({returnTy});

    auto returnExpr = MakeOwnedNode<ReturnExpr>();
    returnExpr->expr = std::move(ue);
    returnExpr->SetTy({nothingTy});
    returnExpr->refFuncBody = funcBody.get();

    block->body.emplace_back(std::move(returnExpr));
    funcBody->body = std::move(block);
    fd->funcBody = std::move(funcBody);
    AddCurFile(*fd, ed.curFile);
    ed.members.push_back(std::move(fd));
}

void StructInheritanceChecker::CreateBuiltInBinaryOperatorFunc(
    TokenKind op, ModalTy rightTy, ExtendDecl& ed, TypeKind returnTyKind) const
{
    auto retTy = TypeManager::GetPrimitiveTy(returnTyKind);
    auto nothingTy = TypeManager::GetNothingTy();

    auto fd = MakeOwnedNode<FuncDecl>();
    fd->toBeCompiled = true; // For incremental compilation.
    fd->EnableAttr(Attribute::IN_EXTEND, Attribute::PUBLIC, Attribute::OPERATOR, Attribute::IMPLICIT_ADD);
    CopyBasicInfo(&ed, fd.get());
    fd->moduleName = ed.moduleName;
    fd->fullPackageName = ed.fullPackageName;
    fd->op = op;
    fd->identifier = SrcIdentifier{TOKENS[static_cast<int>(op)]};
    fd->SetTy({typeManager.GetFunctionTy({rightTy}, {retTy})});
    fd->outerDecl = &ed;

    auto funcBody = MakeOwnedNode<FuncBody>();
    funcBody->SetTy(fd->GetTy());
    funcBody->funcDecl = fd.get();
    auto retType = MakeOwnedNode<PrimitiveType>();
    retType->kind = returnTyKind;
    retType->SetTy({retTy});
    funcBody->retType = std::move(retType);

    auto rightParam = MakeOwnedNode<FuncParam>();
    rightParam->SetTy(rightTy);
    rightParam->identifier = "right";

    auto block = MakeOwnedNode<Block>();
    block->SetTy({nothingTy});

    auto leftExpr = MakeOwnedNode<RefExpr>();
    leftExpr->isThis = true;
    leftExpr->ref.identifier = "this";
    leftExpr->SetTy(ed.GetTy());

    auto rightExpr = MakeOwnedNode<RefExpr>();
    rightExpr->ref.identifier = "right";
    rightExpr->ref.target = rightParam.get();
    rightExpr->SetTy(rightTy);

    auto be = MakeOwnedNode<BinaryExpr>();
    be->op = op;
    be->leftExpr = std::move(leftExpr);
    be->rightExpr = std::move(rightExpr);
    be->SetTy({retTy});

    auto returnExpr = MakeOwnedNode<ReturnExpr>();
    returnExpr->expr = std::move(be);
    returnExpr->SetTy({nothingTy});
    returnExpr->refFuncBody = funcBody.get();

    auto paramList = MakeOwnedNode<FuncParamList>();
    paramList->params.emplace_back(std::move(rightParam));

    funcBody->paramLists.emplace_back(std::move(paramList));
    block->body.emplace_back(std::move(returnExpr));
    funcBody->body = std::move(block);
    fd->funcBody = std::move(funcBody);
    AddCurFile(*fd, ed.curFile);
    ed.members.push_back(std::move(fd));
}
