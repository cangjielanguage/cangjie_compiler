// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "DesugarTypeCheckingAndCasting.h"
#include "NativeFFI/Java/AfterTypeCheck/Utils.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/AST/Walker.h"

namespace Cangjie::Native::FFI::Java {
using namespace Interop::Java;

namespace {
bool ShouldDesugarTypecheck(Ptr<Type> type, Ptr<Expr> expr)
{
    // When obj is not class or interface or type is not of Java class
    // then will be desugared as regular as
    auto castDecl = DynamicCast<ClassLikeDecl>(Ty::GetDeclOfTy(type->GetTy()));
    auto objDecl = DynamicCast<ClassLikeDecl>(Ty::GetDeclOfTy(expr->GetTy()));
    if (!objDecl || !castDecl) {
        return false;
    }
    if (!castDecl->IsJavaMirror() && !castDecl->IsJavaImpl()) {
        return false;
    }

    return true;
}

std::vector<Ptr<TypePattern>> CollectTypePatternsWithJavaClass(Ptr<Pattern> pat)
{
    std::vector<Ptr<TypePattern>> res;

    Walker(pat, [&res](auto node) {
        CJC_ASSERT(node);

        if (auto tpat = As<ASTKind::TYPE_PATTERN>(node.get())) {
            auto decl = Ty::GetDeclOfTy(tpat->type->GetTy());
            // Saving for all Java classes, except JObject
            if (decl && (decl->IsJavaMirror() || decl->IsJavaImpl()) && !IsJObject(*decl)) {
                res.push_back(tpat);
            }
        }

        return VisitAction::WALK_CHILDREN;
    }).Walk();

    return res;
}

std::vector<Ptr<TypePattern>> CollectTypePatternsWithJavaClass(const std::vector<OwnedPtr<Pattern>>& patterns)
{
    std::vector<Ptr<TypePattern>> res;

    for (auto& pat : patterns) {
        auto pats = CollectTypePatternsWithJavaClass(pat.get());
        std::move(pats.begin(), pats.end(), std::back_inserter(res));
    }

    return res;
}

OwnedPtr<VarPattern> CreateTmpVarPattern(ModalTy ty)
{
    auto var = CreateTmpVarDecl();
    auto varPat = MakeOwned<VarPattern>();
    var->parentPattern = varPat;
    var->SetTy(ty);
    varPat->SetTy(ty);
    varPat->varDecl = std::move(var);
    return varPat;
}
}

OwnedPtr<Expr> DesugarTypeCheckingAndCasting::CreateIsInstanceCall(
    Ptr<VarDecl> jObjectVar, ModalTy classTy, Ptr<File> curFile) const
{
    auto isInstanceOfDecl = ilib.GetIsInstanceOf();

    auto jniEnvCall = ilib.CreateGetJniEnvCall(curFile);

    auto javaRefExpr = CreateJavaRefCall(WithinFile(CreateRefExpr(*jObjectVar), curFile));

    auto nameLit = CreateLitConstExpr(LitConstKind::STRING, utils.GetJavaClassNormalizeSignature(*classTy),
        isInstanceOfDecl->funcBody->paramLists[0]->params[2]->GetTy());

    return CreateCall(isInstanceOfDecl, curFile, std::move(jniEnvCall), std::move(javaRefExpr), std::move(nameLit));
}

OwnedPtr<Expr> DesugarTypeCheckingAndCasting::CreateJObjectCast(Ptr<VarDecl> jObjectVar,
    Ptr<ClassLikeDecl> castDecl, Ptr<File> curFile) const
{
    CJC_ASSERT(castDecl->IsJavaMirror() || castDecl->IsJavaImpl());
    auto castTy = castDecl->GetTy();

    // match (IsInstance(obj.javaref, ...))
    auto isInstanceCall = CreateIsInstanceCall(jObjectVar, castTy, curFile);

    auto javarefExpr = CreateJavaRefCall(WithinFile(CreateRefExpr(*jObjectVar), curFile));

    // cast true => ...
    // wrap into mirror constructor or into wrapping constructor of java impl on the reference from registry
    OwnedPtr<Expr> trueBranch =
        utils.CreateOptionSomeCall(ilib.UnwrapJavaEntity(std::move(javarefExpr), castTy.Ty(), *castDecl), castTy);

    // case false => None
    OwnedPtr<Expr> falseBranch = utils.CreateOptionNoneRef(castTy);

    return CreateBoolMatch(
        std::move(isInstanceCall), std::move(trueBranch), std::move(falseBranch), utils.GetOptionTy(castTy));
}

OwnedPtr<Block> DesugarTypeCheckingAndCasting::CastAndSubstituteVars(
    Expr& expr, const std::vector<std::tuple<Ptr<VarDecl>, ModalTy>>& patternVars) const
{
    auto curFile = expr.curFile;
    auto varsBlock = WithinFile(MakeOwned<Block>(), curFile);
    std::unordered_map<Ptr<Decl>, Ptr<Decl>> varsMapping;
    for (auto [varDecl, castTy] : patternVars) {
        auto castDecl = StaticAs<ASTKind::CLASS_LIKE_DECL>(Ty::GetDeclOfTy(castTy));

        auto javarefExpr = CreateJavaRefCall(WithinFile(CreateRefExpr(*varDecl), curFile));
        OwnedPtr<Expr> initializer = ilib.UnwrapJavaEntity(std::move(javarefExpr), castDecl->DataTy(), *castDecl);
        auto castedVar = WithinFile(CreateTmpVarDecl(CreateType(castDecl->GetTy()), std::move(initializer)), curFile);
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

void DesugarTypeCheckingAndCasting::DesugarIsExpression(IsExpr& ie) const
{
    auto curFile = ie.curFile;
    CJC_NULLPTR_CHECK(curFile);
    static const ModalTy BOOL_TY{TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)};

    CJC_ASSERT(!ie.desugarExpr);

    auto castTy = ie.isType->GetTy();
    auto jObjectDecl = utils.GetJObjectDecl();
    CJC_ASSERT(jObjectDecl);

    // match (x)
    std::vector<OwnedPtr<MatchCase>> matchCases;

    // case x : JObject => IsInstance(..)
    auto jObjVarPattern = WithinFile(CreateVarPattern(V_COMPILER, jObjectDecl->GetTy()), curFile);
    jObjVarPattern->varDecl->curFile = curFile;
    auto isInstanceCall = CreateIsInstanceCall(jObjVarPattern->varDecl, castTy, curFile);
    auto jObjectType = CreateType(jObjectDecl->GetTy());
    auto typePattern = CreateTypePattern(std::move(jObjVarPattern), std::move(jObjectType), *ie.leftExpr);
    typePattern->needRuntimeTypeCheck = true;
    typePattern->matchBeforeRuntime = false;
    matchCases.emplace_back(CreateMatchCase(std::move(typePattern), std::move(isInstanceCall)));

    // case _ => false
    auto falseLit = CreateLitConstExpr(LitConstKind::BOOL, "false", BOOL_TY);
    matchCases.emplace_back(CreateMatchCase(MakeOwned<WildcardPattern>(), std::move(falseLit)));

    ie.desugarExpr = WithinFile(CreateMatchExpr(std::move(ie.leftExpr), std::move(matchCases), BOOL_TY), curFile);
}

void DesugarTypeCheckingAndCasting::DesugarAsExpression(AsExpr& ae) const
{
    auto curFile = ae.curFile;
    CJC_NULLPTR_CHECK(curFile);
    CJC_ASSERT(!ae.desugarExpr);

    auto castTy = ae.asType->GetTy();
    auto castDecl = DynamicCast<ClassLikeDecl>(Ty::GetDeclOfTy(castTy));
    auto jObjectDecl = utils.GetJObjectDecl();

    auto castResultTy = utils.GetOptionTy(castTy);

    // match (obj)
    std::vector<OwnedPtr<MatchCase>> typeMatchCases;

    // case obj : JObject => match ...
    auto jObjVarPattern = WithinFile(CreateVarPattern(V_COMPILER, jObjectDecl->GetTy()), curFile);
    jObjVarPattern->varDecl->curFile = curFile;
    auto jObjectType = CreateType(jObjectDecl->GetTy());
    auto isInstanceMatch = CreateJObjectCast(jObjVarPattern->varDecl, castDecl, curFile);
    auto typePattern = CreateTypePattern(std::move(jObjVarPattern), std::move(jObjectType), *ae.leftExpr);
    typePattern->needRuntimeTypeCheck = true;
    typePattern->matchBeforeRuntime = false;
    typeMatchCases.emplace_back(CreateMatchCase(std::move(typePattern), std::move(isInstanceMatch)));

    // case _ => None
    auto noneRef = utils.CreateOptionNoneRef(castTy);
    typeMatchCases.emplace_back(CreateMatchCase(MakeOwned<WildcardPattern>(), std::move(noneRef)));
    ae.desugarExpr =
        WithinFile(CreateMatchExpr(std::move(ae.leftExpr), std::move(typeMatchCases), castResultTy), curFile);
}

void DesugarTypeCheckingAndCasting::DesugarMatchCase(MatchCase& matchCase) const
{
    std::vector<Ptr<TypePattern>> typePatterns = CollectTypePatternsWithJavaClass(matchCase.patterns);

    if (typePatterns.empty()) {
        return;
    }

    static auto jObjectDecl = utils.GetJObjectDecl();

    std::vector<OwnedPtr<Expr>> isInstanceGuards;
    std::vector<std::tuple<Ptr<VarDecl>, ModalTy>> patternVars;
    for (auto pat : typePatterns) {
        if (DynamicCast<WildcardPattern>(pat->pattern.get())) {
            pat->pattern = CreateTmpVarPattern(pat->type->GetTy());
            pat->pattern->curFile = matchCase.curFile;
        }

        auto varPat = DynamicCast<VarPattern>(pat->pattern.get());
        // Pattern under type pattern is always either wildcard or var
        CJC_ASSERT(varPat);
        auto originalTy = varPat->GetTy();
        CJC_NULLPTR_CHECK(originalTy);

        pat->type = CreateType(jObjectDecl->GetTy());
        varPat->SetTy(jObjectDecl->GetTy());
        varPat->varDecl->SetTy(jObjectDecl->GetTy());

        patternVars.emplace_back(varPat->varDecl, originalTy);
        isInstanceGuards.emplace_back(CreateIsInstanceCall(varPat->varDecl, originalTy, matchCase.curFile));
    }

    OwnedPtr<Expr> guard;
    if (!matchCase.patternGuard) {
        guard = std::move(isInstanceGuards.back());
        isInstanceGuards.pop_back();
    } else {
        OwnedPtr<Block> guardVarsBlock = CastAndSubstituteVars(*matchCase.patternGuard, patternVars);
        guard = ASTCloner::Clone(matchCase.patternGuard.get());
        guardVarsBlock->body.emplace_back(std::move(matchCase.patternGuard));
        guardVarsBlock->SetTy(guard->GetTy());
        guard->desugarExpr = std::move(guardVarsBlock);
    }

    while (!isInstanceGuards.empty()) {
        guard = CreateBinaryExpr(std::move(isInstanceGuards.back()), std::move(guard), TokenKind::AND);
        isInstanceGuards.pop_back();
    }

    matchCase.patternGuard = std::move(guard);

    auto bodyVarsBlock = CastAndSubstituteVars(*matchCase.exprOrDecls, patternVars);
    bodyVarsBlock->SetTy(matchCase.exprOrDecls->GetTy());
    std::move(matchCase.exprOrDecls->body.begin(), matchCase.exprOrDecls->body.end(),
        std::back_inserter(bodyVarsBlock->body));
    matchCase.exprOrDecls = std::move(bodyVarsBlock);
}

void DesugarTypeCheckingAndCasting::DesugarLetPattern(LetPatternDestructor& letPat) const
{
    std::vector<Ptr<TypePattern>> typePatterns = CollectTypePatternsWithJavaClass(letPat.patterns);

    if (typePatterns.empty()) {
        return;
    }

    for (auto typePat : typePatterns) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_java_interop_not_supported, *typePat,
            "let type patterns with JavaImpl or JavaMirror types");
    }
}

DesugarTypeCheckingAndCasting::DesugarTypeCheckingAndCasting(
    InteropLibBridge& ilib,
    DiagnosticEngine& diag,
    Interop::Java::Utils& utils) : ilib(ilib), diag(diag), utils(utils)
{
}

void DesugarTypeCheckingAndCasting::Process(AfterTypeCheckContext& ctx)
{
    Walker(&ctx.pkg, [this](auto node) {
        CJC_ASSERT(node);

        if (auto ie = As<ASTKind::IS_EXPR>(node.get())) {
            if (!ShouldDesugarTypecheck(ie->isType, ie->leftExpr)) {
                return VisitAction::WALK_CHILDREN;
            }

            DesugarIsExpression(*ie);
        } else if (auto ae = As<ASTKind::AS_EXPR>(node.get())) {
            if (!ShouldDesugarTypecheck(ae->asType, ae->leftExpr)) {
                return VisitAction::WALK_CHILDREN;
            }

            DesugarAsExpression(*ae);
        } else if (auto mc = As<ASTKind::MATCH_CASE>(node.get())) {
            DesugarMatchCase(*mc);
        } else if (auto lpd = As<ASTKind::LET_PATTERN_DESTRUCTOR>(node.get())) {
            DesugarLetPattern(*lpd);
        }

        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

}
