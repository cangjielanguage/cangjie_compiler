// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Desugar functions used during typecheck step.
 */

#include "DesugarInTypeCheck.h"

#include <memory>
#include <utility>
#include <vector>

#include "TypeCheckUtil.h"
#include "TypeCheckerImpl.h"

#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie {
using namespace AST;
using namespace TypeCheckUtil;

namespace {
OwnedPtr<AST::Expr> TryDesugarFunctionCallExpr(OwnedPtr<AST::Expr> base)
{
    if (!Ty::IsInitialTy(base->ty) && base->ty->kind != TypeKind::TYPE_FUNC) {
        // Try to desugar base as base.operator().
        // NOTE: 'curFile' and 'curMacroCall' will be set from caller of current method.
        auto newBase = MakeOwnedNode<MemberAccess>();
        newBase->field = "()";
        newBase->scopeName = base->scopeName;
        newBase->begin = base->begin;
        newBase->end = base->end;
        newBase->baseExpr = std::move(base);
        return newBase;
    }
    return base;
}

void DesugarPipelineExpr(ASTContext& ctx, BinaryExpr& be)
{
    auto callExpr = MakeOwnedNode<CallExpr>();
    CopyBasicInfo(&be, callExpr.get());
    ctx.RemoveTypeCheckCache(*callExpr);
    callExpr->baseFunc = TryDesugarFunctionCallExpr(std::move(be.rightExpr));
    ctx.RemoveTypeCheckCache(*(callExpr->baseFunc));
    auto funcArg = MakeOwned<FuncArg>();
    CopyBasicInfo(be.leftExpr.get(), funcArg.get());
    ctx.RemoveTypeCheckCache(*funcArg);
    funcArg->expr = std::move(be.leftExpr);
    callExpr->args.push_back(std::move(funcArg));
    callExpr->sourceExpr = &be;
    be.desugarExpr = std::move(callExpr);
    if (be.TestAttr(Attribute::UNSAFE)) {
        be.desugarExpr->EnableAttr(Attribute::UNSAFE);
    }
    AddCurFile(be, be.curFile);
}

void DesugarCompositionExpr(ASTContext& ctx, BinaryExpr& be)
{
    auto callExpr = MakeOwnedNode<CallExpr>();
    CopyBasicInfo(&be, callExpr.get());
    ctx.RemoveTypeCheckCache(*callExpr);
    auto refExpr = CreateRefExprInCore("composition");
    CopyBasicInfo(&be, refExpr.get());
    ctx.RemoveTypeCheckCache(*refExpr);
    callExpr->baseFunc = std::move(refExpr);
    auto arg1 = MakeOwned<FuncArg>();
    CopyBasicInfo(be.leftExpr.get(), arg1.get());
    ctx.RemoveTypeCheckCache(*arg1);
    arg1->expr = TryDesugarFunctionCallExpr(std::move(be.leftExpr));
    arg1->expr->isInFlowExpr = true;
    ctx.RemoveTypeCheckCache(*(arg1->expr));
    auto arg2 = MakeOwned<FuncArg>();
    CopyBasicInfo(be.rightExpr.get(), arg2.get());
    ctx.RemoveTypeCheckCache(*arg2);
    arg2->expr = TryDesugarFunctionCallExpr(std::move(be.rightExpr));
    arg2->expr->isInFlowExpr = true;
    ctx.RemoveTypeCheckCache(*(arg2->expr));
    callExpr->args.push_back(std::move(arg1));
    callExpr->args.push_back(std::move(arg2));
    callExpr->sourceExpr = &be;
    be.desugarExpr = std::move(callExpr);
    if (be.TestAttr(Attribute::UNSAFE)) {
        be.desugarExpr->EnableAttr(Attribute::UNSAFE);
    }
    AddCurFile(be, be.curFile);
}

// Ignore empty intersection ty added by typealias substitution.
void IgnoreEmptyIntersection(std::vector<Ptr<Type>>& baseArgs)
{
    for (auto it = baseArgs.begin(); it != baseArgs.end();) {
        CJC_ASSERT(*it != nullptr);
        auto iTy = DynamicCast<IntersectionTy*>((*it)->ty);
        bool isEmpty = iTy && iTy->tys.empty();
        it = isEmpty ? baseArgs.erase(it) : it + 1;
    }
}

OwnedPtr<Expr> CreateArgumentForCompoundAssignOverload(
    AssignExpr& ae, CallExpr& createdCall, Ptr<Expr> basePtr, const std::vector<Ptr<Expr>>& indexPtrs)
{
    // Compound assign will be desugared as a[i] = x ==> a.[](i, a.[](i) + x).
    // The arg 'createdCall' is outer call of desugared result and this function will create the inner callExpr.
    auto binaryExpr = MakeOwnedNode<BinaryExpr>();
    CopyBasicInfo(&ae, binaryExpr.get());
    binaryExpr->leftExpr = ASTCloner::Clone(Ptr(&createdCall));
    // Set mapped expr node for later process for side effect.
    auto ce = RawStaticCast<CallExpr*>(binaryExpr->leftExpr.get());
    auto base = StaticCast<MemberAccess*>(ce->baseFunc.get());
    base->baseExpr->mapExpr = basePtr;
    for (size_t i = 0; i < indexPtrs.size(); ++i) {
        ce->args[i]->expr->mapExpr = indexPtrs[i];
    }
    binaryExpr->leftExpr->sourceExpr = &ae;
    binaryExpr->rightExpr = std::move(ae.rightExpr);
    if (COMPOUND_ASSIGN_EXPR_MAP.find(ae.op) != COMPOUND_ASSIGN_EXPR_MAP.end()) {
        binaryExpr->op = COMPOUND_ASSIGN_EXPR_MAP.at(ae.op);
    }
    binaryExpr->operatorPos = ae.assignPos;
    return binaryExpr;
}

void DesugarPrimaryCtorHandleSuper(const Decl& decl, const PrimaryCtorDecl& fd,
    const OwnedPtr<FuncBody>& funcBody, std::vector<OwnedPtr<Node>>::iterator& it)
{
    if (decl.astKind != ASTKind::CLASS_DECL || !fd.funcBody->body || fd.funcBody->body->body.empty()) {
        return;
    }
    if (auto ce = DynamicCast<CallExpr*>(fd.funcBody->body->body.front().get()); ce) {
        if (auto re = DynamicCast<RefExpr*>(ce->baseFunc.get()); re && re->isSuper) {
            funcBody->body->body.push_back(
                ASTCloner::Clone(fd.funcBody->body->body.front().get(), SetIsClonedSourceCode));
            ++it;
        }
    }
}

void DesugarPrimaryCtorHandleParamSetEachParam(
    Decl& decl, const PrimaryCtorDecl& fd, const OwnedPtr<FuncBody>& funcBody)
{
    if (fd.funcBody->paramLists.empty()) {
        return;
    }
    for (auto& param : fd.funcBody->paramLists[0]->params) {
        if (!param->isMemberParam) {
            continue;
        }
        if (auto vd = DynamicCast<VarDecl*>(param.get()); vd && !vd->TestAttr(Attribute::STATIC)) {
            // Create declare variables.
            OwnedPtr<VarDecl> varDecl = MakeOwned<VarDecl>();
            CopyBasicInfo(vd, varDecl.get());
            varDecl->annotations = ASTCloner::CloneVector(vd->annotations);
            varDecl->annotationsArray = ASTCloner::Clone(vd->annotationsArray.get());
            varDecl->isVar = vd->isVar;
            varDecl->modifiers.insert(vd->modifiers.begin(), vd->modifiers.end());
            varDecl->CloneAttrs(*vd);
            varDecl->keywordPos = vd->begin;
            varDecl->colonPos = vd->colonPos;
            varDecl->type = ASTCloner::Clone(vd->type.get());
            varDecl->identifier = vd->identifier;
            varDecl->isMemberParam = true; // Lsp will use this to found related funcparam.
            varDecl->rawMangleName = vd->rawMangleName;
            varDecl->toBeCompiled = fd.toBeCompiled;
            varDecl->outerDecl = &decl;
            varDecl->EnableAttr(Attribute::COMPILER_ADD, Attribute::IS_CLONED_SOURCE_CODE);
            if (decl.astKind == ASTKind::CLASS_DECL) {
                StaticAs<ASTKind::CLASS_DECL>(&decl)->body->decls.push_back(std::move(varDecl));
            } else if (decl.astKind == ASTKind::STRUCT_DECL) {
                StaticAs<ASTKind::STRUCT_DECL>(&decl)->body->decls.push_back(std::move(varDecl));
            }

            // Create initialize expression.
            auto thisExpr = CreateRefExpr("this");
            thisExpr->isThis = true;
            thisExpr->EnableAttr(Attribute::IMPLICIT_ADD); // Requried for LSP usage.
            CopyBasicInfo(vd, thisExpr.get());
            thisExpr->ref.identifier.SetFileID(fd.funcBody->begin.fileID);
            auto left = CreateMemberAccess(std::move(thisExpr), vd->identifier);
            left->EnableAttr(Attribute::IMPLICIT_ADD); // Requried for LSP usage.
            CopyBasicInfo(vd, left.get());
            auto right = CreateRefExpr(vd->identifier);
            right->EnableAttr(Attribute::IMPLICIT_ADD); // Requried for LSP usage.
            CopyBasicInfo(vd->type.get(), right.get()); // The right position must be later than left.
            right->sourceExpr = left.get(); // Used to mark the 'right' is desugar created and should not diag.
            right->ref.identifier.SetFileID(fd.funcBody->begin.fileID);
            auto assignment = MakeOwned<AssignExpr>();
            CopyBasicInfo(vd, assignment.get());
            assignment->EnableAttr(Attribute::COMPILER_ADD);
            assignment->leftValue = std::move(left);
            assignment->rightExpr = std::move(right);
            assignment->op = TokenKind::ASSIGN;
            funcBody->body->body.push_back(std::move(assignment));
        }
    }
}

void DesugarPrimaryCtorHandleParam(Decl& decl, const PrimaryCtorDecl& fd, const OwnedPtr<FuncBody>& funcBody,
    const OwnedPtr<FuncParamList>& funcParamList)
{
    if (fd.funcBody->paramLists.empty()) {
        return;
    }
    CopyBasicInfo(fd.funcBody->paramLists[0].get(), funcParamList.get());
    for (auto& p : fd.funcBody->paramLists[0]->params) {
        // FundParam is cloned from user written source code, which should be used to highlight in LSP.
        auto param = CreateFuncParam(p->identifier, ASTCloner::Clone(p->type.get(), SetIsClonedSourceCode),
            ASTCloner::Clone(p->assignment.get(), SetIsClonedSourceCode));
        param->EnableAttr(Attribute::IS_CLONED_SOURCE_CODE);
        // Original user written source code should be ignored for LSP.
        p->EnableAttr(Attribute::COMPILER_ADD);
        CopyBasicInfo(p.get(), param.get());
        param->identifier.SetPos(p->identifier.Begin(), p->identifier.End());
        param->identifier.SetRaw(p->identifier.IsRaw());
        param->isNamedParam = p->isNamedParam;
        param->isMemberParam = p->isMemberParam; // Lsp will use this to found related member variable.
        for (auto& anno : p->annotations) {
            if (anno->kind == AnnotationKind::DEPRECATED || anno->kind == AnnotationKind::CUSTOM) {
                param->annotations.emplace_back(ASTCloner::Clone(anno.get(), SetIsClonedSourceCode));
            }
        }
        funcParamList->params.push_back(std::move(param));
    }
    // Member variable param.
    DesugarPrimaryCtorHandleParamSetEachParam(decl, fd, funcBody);
}

void DesugarPrimaryCtorSetPrimaryFunc(Decl& decl, PrimaryCtorDecl& fd, OwnedPtr<FuncBody>& funcBody)
{
    OwnedPtr<FuncDecl> primaryFunc = MakeOwned<FuncDecl>();
    CopyBasicInfo(&fd, primaryFunc.get());
    primaryFunc->toBeCompiled = fd.toBeCompiled;
    primaryFunc->funcBody = std::move(funcBody);
    primaryFunc->funcBody->funcDecl = primaryFunc.get();
    primaryFunc->identifier = "init";
    primaryFunc->identifierForLsp = fd.identifier;
    primaryFunc->identifier.SetPos(fd.identifier.Begin(), fd.identifier.End());
    primaryFunc->modifiers.insert(fd.modifiers.begin(), fd.modifiers.end());
    primaryFunc->constructorCall = ConstructorCall::NONE;
    for (const auto& anno : fd.annotations) {
        // Annotation is cloned from user written source code, which should be used to highlight in LSP.
        (void)primaryFunc->annotations.emplace_back(ASTCloner::Clone(anno.get(), SetIsClonedSourceCode));
    }
    primaryFunc->CloneAttrs(fd);
    primaryFunc->EnableAttr(Attribute::COMPILER_ADD, Attribute::IS_CLONED_SOURCE_CODE);
    primaryFunc->EnableAttr(Attribute::CONSTRUCTOR, Attribute::PRIMARY_CONSTRUCTOR);
    primaryFunc->annotations = std::move(fd.annotations);
    primaryFunc->overflowStrategy = fd.overflowStrategy;
    primaryFunc->isFastNative = fd.isFastNative;
    primaryFunc->isConst = fd.isConst;
    primaryFunc->isFrozen = primaryFunc->HasAnno(AnnotationKind::FROZEN);
    primaryFunc->rawMangleName = fd.rawMangleName;
    if (decl.astKind == ASTKind::CLASS_DECL) {
        auto classDecl = As<ASTKind::CLASS_DECL>(&decl);
        CJC_ASSERT(classDecl);
        primaryFunc->funcBody->parentClassLike = classDecl;
        primaryFunc->outerDecl = classDecl;
        primaryFunc->EnableAttr(Attribute::IN_CLASSLIKE);
        classDecl->body->decls.push_back(std::move(primaryFunc));
    } else if (decl.astKind == ASTKind::STRUCT_DECL) {
        auto structDecl = As<ASTKind::STRUCT_DECL>(&decl);
        CJC_ASSERT(structDecl);
        primaryFunc->funcBody->parentStruct = structDecl;
        primaryFunc->outerDecl = structDecl;
        primaryFunc->EnableAttr(Attribute::IN_STRUCT);
        structDecl->body->decls.push_back(std::move(primaryFunc));
    }
}
} // namespace

void DesugarFlowExpr(ASTContext& ctx, BinaryExpr& fe)
{
    if (fe.desugarExpr != nullptr) {
        return; // Avoid re-enter.
    }
    if (fe.op == TokenKind::PIPELINE) {
        DesugarPipelineExpr(ctx, fe);
    } else if (fe.op == TokenKind::COMPOSITION) {
        DesugarCompositionExpr(ctx, fe);
    }
}

void DesugarOperatorOverloadExpr(ASTContext& ctx, BinaryExpr& be)
{
    if (be.desugarExpr != nullptr || !be.leftExpr || !be.rightExpr) {
        return;
    }
    auto callExpr = MakeOwnedNode<CallExpr>();
    CopyBasicInfo(&be, callExpr.get());
    ctx.RemoveTypeCheckCache(*callExpr);
    auto callBase = MakeOwnedNode<MemberAccess>();
    CopyBasicInfo(&be, callBase.get());
    ctx.RemoveTypeCheckCache(*callBase);
    callBase->baseExpr = std::move(be.leftExpr);
    callBase->field = SrcIdentifier{TOKENS[static_cast<int64_t>(be.op)]};
    callBase->field.SetPos(be.operatorPos, be.operatorPos + strlen(TOKENS[static_cast<int64_t>(be.op)]));
    callExpr->baseFunc = std::move(callBase);
    auto funcArg = MakeOwned<FuncArg>();
    CopyBasicInfo(be.rightExpr.get(), funcArg.get());
    ctx.RemoveTypeCheckCache(*funcArg);
    funcArg->expr = std::move(be.rightExpr);
    funcArg->ty = funcArg->expr->ty;
    callExpr->args.push_back(std::move(funcArg));
    callExpr->sourceExpr = &be;
    be.desugarExpr = std::move(callExpr);
    if (be.TestAttr(Attribute::UNSAFE)) {
        be.desugarExpr->EnableAttr(Attribute::UNSAFE);
    }
    AddCurFile(be, be.curFile);
}

void DesugarOperatorOverloadExpr(ASTContext& ctx, UnaryExpr& ue)
{
    if (ue.desugarExpr != nullptr) {
        return; // Avoid re-enter.
    }
    auto callExpr = MakeOwnedNode<CallExpr>();
    ctx.RemoveTypeCheckCache(*callExpr);
    auto callBase = MakeOwnedNode<MemberAccess>();
    CopyBasicInfo(&ue, callBase.get());
    ctx.RemoveTypeCheckCache(*callBase);
    callBase->baseExpr = std::move(ue.expr);
    callBase->field = SrcIdentifier{TOKENS[static_cast<int64_t>(ue.op)]};
    callBase->field.SetPos(ue.operatorPos, ue.operatorPos +
        strlen(TOKENS[static_cast<int64_t>(ue.op)])); // Requried for LSP usage.
    callExpr->baseFunc = std::move(callBase);
    callExpr->sourceExpr = &ue;
    callExpr->begin = ue.begin;
    callExpr->end = ue.end;
    if (ue.TestAttr(Attribute::UNSAFE)) {
        callExpr->EnableAttr(Attribute::UNSAFE);
    }
    ue.desugarExpr = std::move(callExpr);
    AddCurFile(ue, ue.curFile);
}

void DesugarOperatorOverloadExpr(ASTContext& ctx, SubscriptExpr& se)
{
    if (se.desugarExpr != nullptr) {
        return; // Avoid re-enter.
    }
    auto callExpr = MakeOwnedNode<CallExpr>();
    CopyBasicInfo(&se, callExpr.get());
    ctx.RemoveTypeCheckCache(*callExpr);
    auto callBase = MakeOwnedNode<MemberAccess>();
    CopyBasicInfo(&se, callBase.get());
    ctx.RemoveTypeCheckCache(*callBase);
    callBase->baseExpr = std::move(se.baseExpr);
    callBase->field = "[]";
    callBase->field.SetPos(se.leftParenPos, se.leftParenPos + std::string_view{"[]"}.size()); // Requried for LSP usage.
    callExpr->baseFunc = std::move(callBase);
    for (auto& expr : se.indexExprs) {
        auto funcArg = MakeOwned<FuncArg>();
        CopyBasicInfo(expr.get(), funcArg.get());
        ctx.RemoveTypeCheckCache(*funcArg);
        funcArg->expr = std::move(expr);
        callExpr->args.push_back(std::move(funcArg));
    }
    callExpr->sourceExpr = &se;
    if (se.TestAttr(Attribute::UNSAFE)) {
        callExpr->EnableAttr(Attribute::UNSAFE);
    }
    se.desugarExpr = std::move(callExpr);
    AddCurFile(se, se.curFile);
}

void DesugarSubscriptOverloadExpr(ASTContext& ctx, AssignExpr& ae)
{
    if (ae.leftValue == nullptr || ae.rightExpr == nullptr) {
        return;
    }
    if (ae.leftValue->astKind != ASTKind::SUBSCRIPT_EXPR) {
        return;
    }
    auto subscript = StaticAs<ASTKind::SUBSCRIPT_EXPR>(ae.leftValue.get());
    auto callExpr = MakeOwnedNode<CallExpr>();
    auto basePtr = subscript->baseExpr.get();
    std::vector<Ptr<Expr>> indexPtrs{};
    for (const auto& expr : subscript->indexExprs) {
        indexPtrs.push_back(expr.get());
    }
    CopyBasicInfo(&ae, callExpr.get());
    ctx.RemoveTypeCheckCache(*callExpr);
    auto callBase = MakeOwnedNode<MemberAccess>();
    CopyBasicInfo(&ae, callBase.get());
    ctx.RemoveTypeCheckCache(*callBase);
    callBase->baseExpr = std::move(subscript->baseExpr);
    if (ae.isCompound) { // Store mapped expr node for later process for side effect.
        callBase->baseExpr->mapExpr = basePtr;
    }
    callBase->field = "[]";
    callBase->field.SetPos(subscript->leftParenPos,
        subscript->leftParenPos + std::string_view("[]").size()); // Requried for LSP usage.
    callExpr->baseFunc = std::move(callBase);
    callExpr->sourceExpr = &ae;
    for (size_t i = 0; i < subscript->indexExprs.size(); ++i) {
        auto funcArg1 = MakeOwned<FuncArg>();
        CopyBasicInfo(subscript->indexExprs[i].get(), funcArg1.get());
        ctx.RemoveTypeCheckCache(*funcArg1);
        funcArg1->expr = std::move(subscript->indexExprs[i]);
        if (ae.isCompound) { // Store mapped expr node for later process for side effect.
            funcArg1->expr->mapExpr = indexPtrs[i];
        }
        funcArg1->scopeName = funcArg1->expr->scopeName;
        callExpr->args.push_back(std::move(funcArg1));
    }
    auto funcArg2 = MakeOwned<FuncArg>();
    ctx.RemoveTypeCheckCache(*funcArg2);
    funcArg2->name = "value";
    // NOTE: This desugar will cause side effect, so need special handling in codeGen.
    funcArg2->expr = ae.isCompound ? CreateArgumentForCompoundAssignOverload(ae, *callExpr, basePtr, indexPtrs)
                                   : std::move(ae.rightExpr);
    CopyBasicInfo(funcArg2->expr.get(), funcArg2.get());
    ctx.ClearTypeCheckCache(*(funcArg2->expr));
    callExpr->args.push_back(std::move(funcArg2));
    if (ae.TestAttr(Attribute::UNSAFE)) {
        callExpr->EnableAttr(Attribute::UNSAFE);
    }
    if (ae.isCompound) {
        callExpr->EnableAttr(Attribute::SIDE_EFFECT);
    }
    ae.desugarExpr = std::move(callExpr);
    AddCurFile(ae, ae.curFile);
}

void DesugarOperatorOverloadExpr(ASTContext& ctx, AssignExpr& ae)
{
    if (ae.leftValue == nullptr || ae.rightExpr == nullptr) {
        return;
    }

    auto assignExpr = MakeOwnedNode<AssignExpr>();
    CopyBasicInfo(&ae, assignExpr.get());
    ctx.RemoveTypeCheckCache(*assignExpr);
    assignExpr->sourceExpr = &ae;
    assignExpr->op = TokenKind::ASSIGN;
    assignExpr->leftValue = std::move(ae.leftValue);
    assignExpr->leftValue->mapExpr = assignExpr->leftValue.get();

    auto callBase = MakeOwnedNode<MemberAccess>();
    CopyBasicInfo(&ae, callBase.get());
    ctx.RemoveTypeCheckCache(*callBase);
    callBase->baseExpr = ASTCloner::Clone(assignExpr->leftValue.get());
    ctx.ClearTypeCheckCache(*(callBase->baseExpr));
    // Created 'callBase' is not left value, should remove attr.
    callBase->baseExpr->DisableAttr(Attribute::LEFT_VALUE);
    callBase->baseExpr->mapExpr = assignExpr->leftValue.get();
    callBase->field = std::string(TOKENS[static_cast<int>(COMPOUND_ASSIGN_EXPR_MAP.find(ae.op)->second)]);

    auto funcArg = MakeOwned<FuncArg>();
    CopyBasicInfo(&ae, funcArg.get());
    ctx.RemoveTypeCheckCache(*funcArg);
    funcArg->expr = std::move(ae.rightExpr);

    auto callExpr = MakeOwnedNode<CallExpr>();
    CopyBasicInfo(&ae, callExpr.get());
    ctx.RemoveTypeCheckCache(*callExpr);
    callExpr->sourceExpr = &ae;
    callExpr->baseFunc = std::move(callBase);
    callExpr->args.push_back(std::move(funcArg));
    if (ae.TestAttr(Attribute::UNSAFE)) {
        callExpr->EnableAttr(Attribute::UNSAFE);
    }
    assignExpr->rightExpr = std::move(callExpr);
    assignExpr->EnableAttr(Attribute::SIDE_EFFECT);

    ae.desugarExpr = std::move(assignExpr);
    AddCurFile(ae, ae.curFile);
}

void DesugarCallExpr(ASTContext& ctx, CallExpr& ce)
{
    if (ce.desugarExpr != nullptr || ce.baseFunc == nullptr) {
        return;
    }
    auto callExpr = MakeOwnedNode<CallExpr>();
    ctx.RemoveTypeCheckCache(*callExpr);
    callExpr->sourceExpr = &ce;
    callExpr->begin = ce.begin;
    callExpr->end = ce.end;
    auto ma = MakeOwnedNode<MemberAccess>();
    ctx.RemoveTypeCheckCache(*ma);
    ma->scopeName = ce.baseFunc->scopeName;
    ma->field = "()";
    ma->field.SetPos(ce.leftParenPos, ce.leftParenPos + std::string_view{"()"}.size()); // Requried for LSP usage.
    ma->begin = ce.baseFunc->begin;
    ma->end = ce.baseFunc->end;
    ma->baseExpr = std::move(ce.baseFunc);
    callExpr->baseFunc = std::move(ma);
    callExpr->args = std::move(ce.args);
    if (ce.TestAttr(Attribute::UNSAFE)) {
        callExpr->EnableAttr(Attribute::UNSAFE);
    }
    ce.desugarExpr = std::move(callExpr);
    // Contains 'AddMacroAttr' for diag after macro expansion.
    AddCurFile(ce, ce.curFile);
}

void DesugarVariadicCallExpr(ASTContext& ctx, CallExpr& ce, size_t fixedPositionalArity)
{
    if (ce.desugarExpr != nullptr || ce.baseFunc == nullptr) {
        return;
    }
    auto callExpr = MakeOwnedNode<CallExpr>();
    ctx.RemoveTypeCheckCache(*callExpr);
    callExpr->callKind = ce.callKind;
    ce.callKind = CallKind::CALL_VARIADIC_FUNCTION;
    callExpr->sourceExpr = &ce;
    CopyBasicInfo(&ce, callExpr.get());
    callExpr->baseFunc = std::move(ce.baseFunc);
    CJC_ASSERT(ce.args.size() >= fixedPositionalArity);
    size_t idx = 0;
    // Fixed positional arguments.
    for (; idx < fixedPositionalArity; idx++) {
        CJC_ASSERT(ce.args[idx]->name.Empty());
        callExpr->args.emplace_back(std::move(ce.args[idx]));
    }
    // Variadic arguments.
    auto arrayLit = MakeOwned<ArrayLit>();
    // The position of `arrayLit` should not be used to report diagnostics.
    // We set them here to avoid zero position error while checking candidates.
    CopyBasicInfo(&ce, arrayLit.get());
    if (idx >= ce.args.size()) { // no vararg, it desugars to an empty array
        arrayLit->begin = arrayLit->end = ce.rightParenPos;
    } else {
        arrayLit->begin = ce.args[idx]->begin;
        arrayLit->end = ce.args.back()->end;
    }
    ctx.RemoveTypeCheckCache(*arrayLit);
    for (; idx < ce.args.size(); idx++) {
        if (!ce.args[idx]->name.Empty()) {
            break;
        }
        arrayLit->children.emplace_back(std::move(ce.args[idx]->expr));
        ctx.RemoveTypeCheckCache(*ce.args[idx]);
    }
    callExpr->args.emplace_back(CreateFuncArg(std::move(arrayLit)));
    ctx.RemoveTypeCheckCache(*(callExpr->args.back()));
    // Named arguments.
    for (; idx < ce.args.size(); idx++) {
        callExpr->args.emplace_back(std::move(ce.args[idx]));
    }
    if (ce.TestAttr(Attribute::UNSAFE)) {
        callExpr->EnableAttr(Attribute::UNSAFE);
    }
    ce.desugarExpr = std::move(callExpr);
    AddCurFile(ce, ce.curFile);
}

void TypeChecker::TypeCheckerImpl::DesugarPointerCall(ASTContext& ctx, CallExpr& ce)
{
    auto pointerExpr = MakeOwnedNode<PointerExpr>();
    pointerExpr->type = MakeOwnedNode<Type>();
    ctx.RemoveTypeCheckCache(*pointerExpr);
    ctx.RemoveTypeCheckCache(*(pointerExpr->type));
    auto baseFuncTy = ce.baseFunc->ty ? SubstituteTypeAliasInTy(*ce.baseFunc->ty) : TypeManager::GetInvalidTy();
    CJC_ASSERT(baseFuncTy);
    auto typeArgs = baseFuncTy->typeArgs;
    auto baseArgs = ce.baseFunc->GetTypeArgs();
    IgnoreEmptyIntersection(baseArgs);
    // Eg: type A<T> = Pointer<T>; A(v), type arg of this A is not useful.
    bool usefulTypeArg = !typeArgs.empty() && (!typeArgs[0]->IsGeneric() || !baseArgs.empty());
    Ptr<Ty> argTy = nullptr;
    if (usefulTypeArg) {
        argTy = typeArgs[0];
    } else if (!baseArgs.empty()) {
        argTy = baseArgs[0]->ty;
    }
    pointerExpr->type->ty = typeManager.GetPointerTy(argTy);
    if (!ce.args.empty()) {
        pointerExpr->arg = std::move(ce.args[0]);
    }
    if (ce.args.size() > 1) {
        diag.Diagnose(ce, DiagKind::sema_pointer_too_much_argument);
    }
    ce.args.clear();
    pointerExpr->scopeName = ce.scopeName;
    pointerExpr->sourceExpr = &ce;
    pointerExpr->begin = ce.begin;
    pointerExpr->end = ce.end;
    ce.desugarExpr = std::move(pointerExpr);
    AddCurFile(ce, ce.curFile);
}

void TypeChecker::TypeCheckerImpl::DesugarArrayCall(ASTContext& ctx, CallExpr& ce)
{
    auto arrayExpr = MakeOwnedNode<ArrayExpr>();
    arrayExpr->type = MakeOwnedNode<Type>();
    ctx.RemoveTypeCheckCache(*arrayExpr);
    ctx.RemoveTypeCheckCache(*(arrayExpr->type));
    auto baseFuncTy = ce.baseFunc->ty ? SubstituteTypeAliasInTy(*ce.baseFunc->ty) : TypeManager::GetInvalidTy();
    CJC_ASSERT(baseFuncTy);
    auto typeArgs = typeManager.GetTypeArgs(*baseFuncTy); // ArrayTy has 'dims', must using function to get arguments.
    auto baseArgs = ce.baseFunc->GetTypeArgs();
    IgnoreEmptyIntersection(baseArgs);
    if (auto varrTy = DynamicCast<VArrayTy*>(baseFuncTy); varrTy) {
        Ptr<Ty> argTy = TypeManager::GetInvalidTy();
        if (!typeArgs.empty()) {
            argTy = typeArgs[0];
        }
        if (typeArgs[0]->IsGeneric() && !baseArgs.empty()) {
            argTy = baseArgs[0]->ty;
        }
        arrayExpr->type->ty = typeManager.GetVArrayTy(*argTy, varrTy->size);
        arrayExpr->isValueArray = true;
    } else {
        // Eg: type A<T> = Array<T>; A(size, v), type args of this A is not useful.
        bool usefulTypeArg = !typeArgs.empty() && (!typeArgs[0]->IsGeneric() || !baseArgs.empty());
        if (usefulTypeArg) {
            arrayExpr->type->ty = typeManager.GetArrayTy(typeArgs[0], 1);
        } else {
            Ptr<Ty> argTy = TypeManager::GetInvalidTy();
            if (!baseArgs.empty()) {
                argTy = baseArgs[0]->ty;
            }
            arrayExpr->type->ty = typeManager.GetArrayTy(argTy, 1);
        }
    }
    for (auto& it : ce.args) {
        (void)arrayExpr->args.emplace_back(std::move(it));
    }
    ce.args.clear();
    arrayExpr->scopeName = ce.scopeName;
    arrayExpr->sourceExpr = &ce;
    arrayExpr->begin = ce.begin;
    arrayExpr->end = ce.end;
    ce.desugarExpr = std::move(arrayExpr);
    AddCurFile(ce, ce.curFile);
}

void DesugarPrimaryCtor(Decl& decl, PrimaryCtorDecl& fd)
{
    if (decl.astKind != ASTKind::CLASS_DECL && decl.astKind != ASTKind::STRUCT_DECL) {
        return;
    }
    if (fd.funcBody == nullptr) {
        return;
    }
    OwnedPtr<FuncBody> funcBody = MakeOwned<FuncBody>();
    CopyBasicInfo(fd.funcBody.get(), funcBody.get());
    funcBody->body = MakeOwned<Block>();
    CopyBasicInfo(fd.funcBody.get(), funcBody->body.get());
    // RetType is cloned from user written source code, which should be used to highlight in LSP.
    funcBody->retType = ASTCloner::Clone(fd.funcBody->retType.get(), SetIsClonedSourceCode);
    if (funcBody->retType) {
        // Despite that the whole tree is cloned, the retType node is explicitly given by the user.
        funcBody->retType->DisableAttr(Attribute::COMPILER_ADD);
    }
    auto funcParamList = MakeOwned<FuncParamList>();
    CopyFileID(funcParamList.get(), &fd);
    std::vector<OwnedPtr<Node>>::iterator it;
    if (fd.funcBody->body) {
        it = fd.funcBody->body->body.begin();
    }
    // If existing super() for class decl.
    DesugarPrimaryCtorHandleSuper(decl, fd, funcBody, it);
    DesugarPrimaryCtorHandleParam(decl, fd, funcBody, funcParamList);

    if (fd.funcBody->body) {
        // Add primary func other body.
        while (it != fd.funcBody->body->body.end()) {
            // Primary func's other body is cloned from user written source code, which should be used to highlight in
            // LSP.
            funcBody->body->body.push_back(ASTCloner::Clone(it->get(), SetIsClonedSourceCode));
            ++it;
        }
    } else if (fd.TestAttr(Attribute::COMMON)) {
        funcBody->body = nullptr;
    }
    funcBody->paramLists.push_back(std::move(funcParamList));
    DesugarPrimaryCtorSetPrimaryFunc(decl, fd, funcBody);
}
} // namespace Cangjie
