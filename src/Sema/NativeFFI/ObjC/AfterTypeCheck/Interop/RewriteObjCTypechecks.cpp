// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements desugaring of typechecks/typecasts involving ObjC-compatible types
 */


#include "NativeFFI/Utils.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Clone.h"
#include "Handlers.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;
using namespace Cangjie::Native::FFI;
using namespace Cangjie;

namespace {

bool ShouldDesugarTypecheck(Ptr<Type> type, Ptr<Expr> expr)
{
    // When obj is not class or interface or type is not of ObjC class
    // then will be desugared as regular is/as
    auto castDecl = DynamicCast<ClassLikeDecl>(Ty::GetDeclOfTy(type->GetTy()));
    auto objDecl = DynamicCast<ClassLikeDecl>(Ty::GetDeclOfTy(expr->GetTy()));
    if (!objDecl || !castDecl || !castDecl->TestAnyAttr(Attribute::OBJ_C_MIRROR_SUBTYPE, Attribute::OBJ_C_MIRROR)) {
        return false;
    }

    return true;
}

void DesugarIsExpr(InteropContext& ctx, IsExpr& expr)
{
    if (!ShouldDesugarTypecheck(expr.isType, expr.leftExpr)) {
        return;
    }

    auto type = StaticCast<ClassTy>(expr.isType->DataTy());
    auto targetName = ctx.nameGenerator.GetObjCDeclName(*type->decl);
    if (ctx.typeMapper.IsObjCId(*type->decl)) {
        return;
    }

    auto curFile = expr.curFile;
    CJC_NULLPTR_CHECK(curFile);
    static const auto BOOL_TY = ctx.typeManager.GetBoolTy();

    CJC_ASSERT(!expr.desugarExpr);

    auto objCIdDecl = ctx.bridge.GetObjCIdDecl();
    CJC_ASSERT(objCIdDecl);

    auto falseLit = CreateLitConstExpr(LitConstKind::BOOL, "false", {BOOL_TY});
    auto trueLit = CreateLitConstExpr(LitConstKind::BOOL, "true", {BOOL_TY});

    // match (x)
    std::vector<OwnedPtr<MatchCase>> matchCases;

    // case _ : OriginalType => true
    auto originalTypePattern = CreateTypePattern(MakeOwned<WildcardPattern>(), CreateType({type}), *expr.leftExpr);
    originalTypePattern->needRuntimeTypeCheck = true;
    originalTypePattern->matchBeforeRuntime = false;
    auto originalCase = CreateMatchCase(std::move(originalTypePattern), std::move(trueLit));
    matchCases.emplace_back(std::move(originalCase));

    // case _ : ObjCId => isKindOfClass/conformsToProtocol
    auto checkVarPattern = WithinFile(CreateVarPattern(V_COMPILER, objCIdDecl->GetTy()), curFile);
    checkVarPattern->varDecl->curFile = curFile;
    auto objCIdType = CreateType(objCIdDecl->DataTy());

    OwnedPtr<Cangjie::AST::Expr> checkCall = nullptr;
    switch (expr.isType->TyKind()) {
        case TypeKind::TYPE_CLASS:
            checkCall = ctx.factory.CreateObjCIsKindOfClassCall(
                ctx.factory.CreateNativeHandleExpr(WithinFile(CreateRefExpr(*checkVarPattern->varDecl), curFile)),
                    ctx.factory.CreateGetCachedClassAccess(targetName, curFile), curFile
            );
            break;
        case TypeKind::TYPE_INTERFACE:
            checkCall = ctx.factory.CreateObjCConformsToProtocolCall(
                ctx.factory.CreateNativeHandleExpr(WithinFile(CreateRefExpr(*checkVarPattern->varDecl), curFile)),
                    ctx.factory.CreateGetProtoCall(targetName, curFile), curFile
            );
            break;
        default:
            return; // do not desugar if right side not class or protocol
    }
    CJC_ASSERT(checkCall); // should not reach, safeguard

    auto checkTypePattern = CreateTypePattern(std::move(checkVarPattern), std::move(objCIdType), *expr.leftExpr);
    checkTypePattern->needRuntimeTypeCheck = true;
    checkTypePattern->matchBeforeRuntime = false;
    auto objCIdCase = CreateMatchCase(std::move(checkTypePattern), std::move(checkCall));
    matchCases.emplace_back(std::move(objCIdCase));

    // case _ => false
    auto wildCase = CreateMatchCase(MakeOwned<WildcardPattern>(), std::move(falseLit));
    matchCases.emplace_back(std::move(wildCase));

    expr.desugarExpr = WithinFile(CreateMatchExpr(std::move(expr.leftExpr), std::move(matchCases), {BOOL_TY}), curFile);
}

void DesugarAsExpr(InteropContext& ctx, AsExpr& expr)
{
    if (!ShouldDesugarTypecheck(expr.asType, expr.leftExpr)) {
        return;
    }

    auto curFile = expr.curFile;
    CJC_NULLPTR_CHECK(curFile);
    CJC_ASSERT(!expr.desugarExpr);

    auto castTy = expr.asType->GetTy();
    auto type = StaticCast<ClassTy>(expr.asType->DataTy());
    if (ctx.typeMapper.IsObjCId(*type->decl)) {
        return;
    }

    auto objCIdDecl = ctx.bridge.GetObjCIdDecl();
    CJC_ASSERT(objCIdDecl);

    auto castResultTy = ctx.factory.GetOptionTy(castTy.Ty());
    auto targetName = ctx.nameGenerator.GetObjCDeclName(*type->decl);

    // match (x)
    std::vector<OwnedPtr<MatchCase>> matchCases;

    // case x: Class => Some(x)
    auto ogVarPattern = WithinFile(CreateVarPattern(V_COMPILER, castTy), curFile);

    OwnedPtr<Expr> originalBranch =
        ctx.factory.CreateOptionSomeCall(WithinFile(CreateRefExpr(*ogVarPattern->varDecl), curFile), castTy.Ty());
    auto originalTypePattern = CreateTypePattern(std::move(ogVarPattern), CreateType(castTy.Ty()), *expr.leftExpr);
    originalTypePattern->needRuntimeTypeCheck = true;
    originalTypePattern->matchBeforeRuntime = false;
    auto originalMatchCase = CreateMatchCase(std::move(originalTypePattern), std::move(originalBranch));
    matchCases.emplace_back(std::move(originalMatchCase));

    // case x: ObjCId && isKindOfClass
    auto objCIdVarPattern = WithinFile(CreateVarPattern(V_COMPILER, objCIdDecl->GetTy()), curFile);
    objCIdVarPattern->varDecl->curFile = curFile;
    
    OwnedPtr<Cangjie::AST::Expr> checkCall = nullptr;
    switch (expr.asType->TyKind()) {
        case TypeKind::TYPE_CLASS:
            checkCall = ctx.factory.CreateObjCIsKindOfClassCall(
                ctx.factory.CreateNativeHandleExpr(WithinFile(CreateRefExpr(*objCIdVarPattern->varDecl), curFile)),
                    ctx.factory.CreateGetCachedClassAccess(targetName, curFile), curFile
            );
            break;
        case TypeKind::TYPE_INTERFACE:
            checkCall = ctx.factory.CreateObjCConformsToProtocolCall(
                ctx.factory.CreateNativeHandleExpr(WithinFile(CreateRefExpr(*objCIdVarPattern->varDecl), curFile)),
                    ctx.factory.CreateGetProtoCall(targetName, curFile), curFile
            );
            break;
        default:
            return; // do not desugar if right side not class or protocol
    }
    CJC_ASSERT(checkCall); // should not reach, safeguard

    auto nativeHandle = ctx.factory.CreateNativeHandleExpr(
        WithinFile(CreateRefExpr(*objCIdVarPattern->varDecl), curFile)
    );

    auto objCIdType = CreateType(objCIdDecl->DataTy());
    auto objCIdTypePattern = CreateTypePattern(std::move(objCIdVarPattern),
        std::move(objCIdType), *expr.leftExpr);
    objCIdTypePattern->needRuntimeTypeCheck = true;
    objCIdTypePattern->matchBeforeRuntime = false;

    // We use objc_retain here, because a successfull typecast creates a new @ObjCMirror object that will call
    // objCRelease in its finalizer.
    OwnedPtr<Expr> objCIdBranch = ctx.factory.WrapEntity(std::move(nativeHandle), *castResultTy, Retain::RETAINED);

    auto objCIdMatchCase = CreateMatchCase(std::move(objCIdTypePattern), std::move(objCIdBranch));
    objCIdMatchCase->patternGuard = std::move(checkCall);
    matchCases.emplace_back(std::move(objCIdMatchCase));

    // case _ => false
    auto wildcardMatchCase = CreateMatchCase(MakeOwned<WildcardPattern>(), ctx.factory.CreateOptionNoneRef(type));
    matchCases.emplace_back(std::move(wildcardMatchCase));

    expr.desugarExpr =
        WithinFile(CreateMatchExpr(std::move(expr.leftExpr), std::move(matchCases), {castResultTy}), curFile);
}

std::vector<Ptr<TypePattern>> CollectTypePatternWithObjCClass(InteropContext& ctx, Ptr<Pattern> pat)
{
    std::vector<Ptr<TypePattern>> res;
    Walker(pat, [&res, &ctx](auto node) {
        CJC_ASSERT(node);
        if (auto tpat = As<ASTKind::TYPE_PATTERN>(node.get())) {
            auto decl = Ty::GetDeclOfTy(tpat->type->GetTy());

            if (decl && decl->TestAnyAttr(Attribute::OBJ_C_MIRROR, Attribute::OBJ_C_MIRROR_SUBTYPE)) {
                if (!ctx.typeMapper.IsObjCId(*decl)) {
                    res.push_back(tpat);
                }
            }
        }

        return VisitAction::WALK_CHILDREN;
    }).Walk();

    return res;
}

std::vector<Ptr<TypePattern>> CollectTypePatternWithObjCClass(InteropContext& ctx,
    const std::vector<OwnedPtr<Pattern>>& patterns)
{
    std::vector<Ptr<TypePattern>> res;

    // temporary solution, in order to support `_: A | _: B`
    // we need to redesign match-case desugaring
    bool multipleTypePatterns = false;
    for (auto& pat : patterns) {
        if (auto tpat = As<ASTKind::TYPE_PATTERN>(pat.get())) {
            if (multipleTypePatterns) {
                ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_objc_interop_not_supported, *tpat,
                    "multiple type patterns inside match case (_: A | _: B)");
                return {};
            } else {
                multipleTypePatterns = true;
            }
        }

        auto pats = CollectTypePatternWithObjCClass(ctx, pat.get());
        std::move(pats.begin(), pats.end(), std::back_inserter(res));
    }

    return res;
}

OwnedPtr<VarPattern> CreateTmpVarPattern(Ptr<Ty> ty)
{
    auto var = CreateTmpVarDecl();
    auto varPat = MakeOwned<VarPattern>();
    var->parentPattern = varPat;
    var->SetTy(ty);
    varPat->SetTy(ty);
    varPat->varDecl = std::move(var);
    return varPat;
}

OwnedPtr<Block> CastAndSubstitudeVars(
    InteropContext& ctx, Expr& expr, const std::vector<std::tuple<Ptr<VarDecl>, Ptr<Ty>>>& patternVars)
{
    auto curFile = expr.curFile;
    auto varsBlock = WithinFile(MakeOwned<Block>(), curFile);
    std::unordered_map<Ptr<Decl>, Ptr<Decl>> varsMapping;
    for (auto [varDecl, castTy] : patternVars) {
        auto castDecl = DynamicCast<ClassLikeDecl>(Ty::GetDeclOfTy(castTy));
        CJC_ASSERT(castDecl);

        auto nativeHandle = ctx.factory.CreateNativeHandleExpr(WithinFile(CreateRefExpr(*varDecl), curFile));
        // We use objc_retain here, because a successfull typecast creates a new @ObjCMirror object that will call
        // objCRelease in its finalizer.
        OwnedPtr<Expr> initializer = ctx.factory.WrapEntity(std::move(nativeHandle), *castTy, Retain::RETAINED);
        auto castedVar = WithinFile(CreateTmpVarDecl(CreateType(castDecl->DataTy()), std::move(initializer)), curFile);
        varsMapping[varDecl] = castedVar;
        varsBlock->body.emplace_back(std::move(castedVar));
    }

    Walker(&expr, [&varsMapping](Ptr<Node> node) {
        CJC_ASSERT(node);

        if (auto refExpr = DynamicCast<RefExpr>(node.get())) {
            auto target = refExpr->ref.target;
            if (auto castedVarIt = varsMapping.find(target); castedVarIt != varsMapping.end()) {
                refExpr->ref.identifier = castedVarIt->second->identifier;
                refExpr->ref.target = castedVarIt->second;
            }
        }

        return VisitAction::WALK_CHILDREN;
    }).Walk();

    return varsBlock;
}

void DesugarMatchCaseExpr(InteropContext& ctx, MatchCase& expr)
{
    std::vector<Ptr<TypePattern>> typePatterns = CollectTypePatternWithObjCClass(ctx, expr.patterns);
    
    if (typePatterns.empty()) { return; }

    auto objCIdDecl = ctx.bridge.GetObjCIdDecl();
    CJC_ASSERT(objCIdDecl);

    std::vector<OwnedPtr<Expr>> interopGuards;
    std::vector<std::tuple<Ptr<VarDecl>, Ptr<Ty>>> patternVars;
    for (auto pat : typePatterns) {
        if (DynamicCast<WildcardPattern>(pat->pattern.get())) {
            pat->pattern = CreateTmpVarPattern(pat->type->DataTy());
            pat->pattern->curFile = expr.curFile;
        }

        auto varPat = StaticCast<VarPattern>(pat->pattern.get());
        auto originalTy = varPat->DataTy();
        auto type = StaticCast<ClassTy>(originalTy);
        auto targetName = ctx.nameGenerator.GetObjCDeclName(*type->decl);
        pat->type = CreateType(objCIdDecl->DataTy());
        varPat->SetTy(objCIdDecl->GetTy());
        varPat->varDecl->SetTy(objCIdDecl->GetTy());

        patternVars.emplace_back(varPat->varDecl, originalTy);

        OwnedPtr<Cangjie::AST::Expr> checkCall = nullptr;
        switch (originalTy->kind) {
            case TypeKind::TYPE_CLASS:
                checkCall = ctx.factory.CreateObjCIsKindOfClassCall(
                    ctx.factory.CreateNativeHandleExpr(WithinFile(CreateRefExpr(*varPat->varDecl), expr.curFile)),
                        ctx.factory.CreateGetCachedClassAccess(targetName, expr.curFile), expr.curFile
                );
                break;
            case TypeKind::TYPE_INTERFACE:
                checkCall = ctx.factory.CreateObjCConformsToProtocolCall(
                    ctx.factory.CreateNativeHandleExpr(WithinFile(CreateRefExpr(*varPat->varDecl), expr.curFile)),
                        ctx.factory.CreateGetProtoCall(targetName, expr.curFile), expr.curFile
                );
                break;
            default:
                return; // do not desugar if right side not class or protocol
        }
        CJC_ASSERT(checkCall); // should not reach, safeguard

        interopGuards.emplace_back(std::move(checkCall));
    }

    OwnedPtr<Expr> guard;
    if (!expr.patternGuard) {
        guard = std::move(interopGuards.back());
        interopGuards.pop_back();
    } else {
        OwnedPtr<Block> guardVarsBlock = CastAndSubstitudeVars(ctx, *expr.patternGuard, patternVars);
        guard = ASTCloner::Clone(expr.patternGuard.get());
        guardVarsBlock->body.emplace_back(std::move(expr.patternGuard));
        guardVarsBlock->SetTy(guard->GetTy());
        guard->desugarExpr = std::move(guardVarsBlock);
    }

    while (!interopGuards.empty()) {
        guard = CreateBinaryExpr(std::move(interopGuards.back()), std::move(guard), TokenKind::AND);
        interopGuards.pop_back();
    }

    expr.patternGuard = std::move(guard);
    auto bodyVarsBlock = CastAndSubstitudeVars(ctx, *expr.exprOrDecls, patternVars);
    bodyVarsBlock->SetTy(expr.exprOrDecls->GetTy());
    std::move(expr.exprOrDecls->body.begin(), expr.exprOrDecls->body.end(),
        std::back_inserter(bodyVarsBlock->body));
    expr.exprOrDecls = std::move(bodyVarsBlock);
}

void DesugarLetDestructorExpr(InteropContext& ctx, IfExpr& expr)
{
    if (auto lpat = As<ASTKind::LET_PATTERN_DESTRUCTOR>(expr.condExpr)) {
        std::vector<Ptr<TypePattern>> typePatterns = CollectTypePatternWithObjCClass(ctx, lpat->patterns);
        
        if (typePatterns.empty()) { return; }

        CJC_ASSERT(typePatterns.size() == 1); // if (let a : M <- N)

        auto pat = typePatterns[0];

        OwnedPtr<Expr> interopGuard;
        std::vector<std::tuple<Ptr<VarDecl>, Ptr<Ty>>> patternVars;

        auto varPat = DynamicCast<VarPattern>(pat->pattern.get());
        CJC_ASSERT(varPat);
        auto originalTy = varPat->GetTy();
        CJC_NULLPTR_CHECK(originalTy);

        auto type = static_cast<ClassTy*>(originalTy.get());
        auto targetName = ctx.nameGenerator.GetObjCDeclName(*type->decl);

        auto objCIdDecl = ctx.bridge.GetObjCIdDecl();
        CJC_ASSERT(objCIdDecl);

        pat->type = CreateType(objCIdDecl->DataTy());
        pat->SetTy(objCIdDecl->GetTy());
        varPat->SetTy(objCIdDecl->GetTy());
        varPat->varDecl->SetTy(objCIdDecl->GetTy());

        OwnedPtr<Cangjie::AST::Expr> checkCall = nullptr;
        switch (originalTy->kind) {
            case TypeKind::TYPE_CLASS:
                checkCall = ctx.factory.CreateObjCIsKindOfClassCall(
                    ctx.factory.CreateNativeHandleExpr(WithinFile(CreateRefExpr(*varPat->varDecl), expr.curFile)),
                        ctx.factory.CreateGetCachedClassAccess(targetName, expr.curFile), expr.curFile
                );
                break;
            case TypeKind::TYPE_INTERFACE:
                checkCall = ctx.factory.CreateObjCConformsToProtocolCall(
                    ctx.factory.CreateNativeHandleExpr(WithinFile(CreateRefExpr(*varPat->varDecl), expr.curFile)),
                        ctx.factory.CreateGetProtoCall(targetName, expr.curFile), expr.curFile
                );
                break;
            default:
                return; // do not desugar if right side not class or protocol
        }
        CJC_ASSERT(checkCall); // should not reach, safeguard
        
        auto varDecl = varPat->varDecl.get();
        patternVars.emplace_back(varDecl, originalTy.Ty());
        auto bodyVarsBlock = CastAndSubstitudeVars(ctx, *expr.thenBody, patternVars);
        bodyVarsBlock->SetTy(expr.thenBody->GetTy());
        std::move(expr.thenBody->body.begin(), expr.thenBody->body.end(),
            std::back_inserter(bodyVarsBlock->body));
        expr.condExpr = CreateBinaryExpr(std::move(expr.condExpr), std::move(checkCall), TokenKind::AND);
        expr.thenBody = std::move(bodyVarsBlock);
    }
}

void DesugarWhileExpr(InteropContext& ctx, WhileExpr& expr)
{
    if (auto lpat = As<ASTKind::LET_PATTERN_DESTRUCTOR>(expr.condExpr)) {
        std::vector<Ptr<TypePattern>> typePatterns = CollectTypePatternWithObjCClass(ctx, lpat->patterns);

        if (typePatterns.empty()) { return; }

        CJC_ASSERT(typePatterns.size() == 1); // if (let a : M <- N)

        auto pat = typePatterns[0];

        OwnedPtr<Expr> interopGuard;
        std::vector<std::tuple<Ptr<VarDecl>, Ptr<Ty>>> patternVars;

        auto varPat = DynamicCast<VarPattern>(pat->pattern.get());
        CJC_ASSERT(varPat);
        auto originalTy = varPat->GetTy();
        CJC_NULLPTR_CHECK(originalTy);

        auto type = static_cast<ClassTy*>(originalTy.get());
        auto targetName = ctx.nameGenerator.GetObjCDeclName(*type->decl);

        auto objCIdDecl = ctx.bridge.GetObjCIdDecl();
        CJC_ASSERT(objCIdDecl);

        pat->type = CreateType(objCIdDecl->DataTy());
        pat->SetTy(objCIdDecl->GetTy());
        varPat->SetTy(objCIdDecl->GetTy());
        varPat->varDecl->SetTy(objCIdDecl->GetTy());

        OwnedPtr<Cangjie::AST::Expr> checkCall = nullptr;
        switch (originalTy->kind) {
            case TypeKind::TYPE_CLASS:
                checkCall = ctx.factory.CreateObjCIsKindOfClassCall(
                    ctx.factory.CreateNativeHandleExpr(WithinFile(CreateRefExpr(*varPat->varDecl), expr.curFile)),
                        ctx.factory.CreateGetCachedClassAccess(targetName, expr.curFile), expr.curFile
                );
                break;
            case TypeKind::TYPE_INTERFACE:
                checkCall = ctx.factory.CreateObjCConformsToProtocolCall(
                    ctx.factory.CreateNativeHandleExpr(WithinFile(CreateRefExpr(*varPat->varDecl), expr.curFile)),
                        ctx.factory.CreateGetProtoCall(targetName, expr.curFile), expr.curFile
                );
                break;
            default:
                return; // do not desugar if right side not class or protocol
        }
        CJC_ASSERT(checkCall); // should not reach, safeguard

        auto varDecl = varPat->varDecl.get();
        patternVars.emplace_back(varDecl, originalTy.Ty());
        auto bodyVarsBlock = CastAndSubstitudeVars(ctx, *expr.body, patternVars);
        bodyVarsBlock->SetTy(expr.body->GetTy());
        std::move(expr.body->body.begin(), expr.body->body.end(),
            std::back_inserter(bodyVarsBlock->body));
        expr.condExpr = CreateBinaryExpr(std::move(expr.condExpr), std::move(checkCall), TokenKind::AND);
        expr.body = std::move(bodyVarsBlock);
    }
}
}

void RewriteObjCTypechecks::HandleImpl(InteropContext& ctx)
{
    for (auto& file : ctx.pkg.files) {
        Walker(file, Walker::GetNextWalkerID(), [&file, &ctx](auto node) {
            if (!node->IsSamePackage(*file->curPackage)) {
                return VisitAction::WALK_CHILDREN;
            }

            if (node->TestAnyAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN, Attribute::UNREACHABLE,
                    Attribute::LEFT_VALUE)) {
                return VisitAction::SKIP_CHILDREN;
            }

            if (node->astKind == ASTKind::MATCH_CASE) {
                DesugarMatchCaseExpr(ctx, *StaticAs<ASTKind::MATCH_CASE>(node));
            }

            return VisitAction::WALK_CHILDREN;
        }).Walk();


        Walker(file, Walker::GetNextWalkerID(), [&file, &ctx](auto node) {
            if (!node->IsSamePackage(*file->curPackage)) {
                return VisitAction::WALK_CHILDREN;
            }

            if (node->TestAnyAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN, Attribute::UNREACHABLE,
                    Attribute::LEFT_VALUE)) {
                return VisitAction::SKIP_CHILDREN;
            }

            switch (node->astKind) {
                case ASTKind::IS_EXPR:
                    DesugarIsExpr(ctx, *StaticAs<ASTKind::IS_EXPR>(node));
                    break;
                case ASTKind::AS_EXPR:
                    DesugarAsExpr(ctx, *StaticAs<ASTKind::AS_EXPR>(node));
                    break;
                case ASTKind::IF_EXPR:
                    DesugarLetDestructorExpr(ctx, *StaticAs<ASTKind::IF_EXPR>(node));
                    break;
                case ASTKind::WHILE_EXPR:
                    DesugarWhileExpr(ctx, *StaticAs<ASTKind::WHILE_EXPR>(node));
                    break;
                default:
                    break;
            }

            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
}