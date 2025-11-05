// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the ASTChecker related classes.
 */
#include "cangjie/Parse/ASTChecker.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"

namespace Cangjie::AST {
void ASTChecker::CheckNode(Ptr<Node> node)
{
    ZERO_POSITION_CHECK(node, node->begin);
    ZERO_POSITION_CHECK(node, node->end);
    if (node->astKind != ASTKind::PACKAGE && node->astKind != ASTKind::PACKAGE_SPEC) {
        ATTR_NULLPTR_CHECK(node, node->curFile);
    }
}

void ASTChecker::CheckExpr(Ptr<Node> node)
{
    auto expr = StaticAs<ASTKind::EXPR>(node);
    if (expr->hasSemi) {
        ZERO_POSITION_CHECK(node, expr->semiPos);
    }
}

void ASTChecker::CheckIfExpr(Ptr<Node> node)
{
    auto ifExpr = StaticAs<ASTKind::IF_EXPR>(node);
    ZERO_POSITION_CHECK(node, ifExpr->ifPos);
    ZERO_POSITION_CHECK(node, ifExpr->leftParenPos);
    ZERO_POSITION_CHECK(node, ifExpr->rightParenPos);
    AST_NULLPTR_CHECK(node, ifExpr->condExpr);
    AST_NULLPTR_CHECK(node, ifExpr->thenBody);
    if (ifExpr->hasElse) {
        ZERO_POSITION_CHECK(node, ifExpr->elsePos);
        AST_NULLPTR_CHECK(node, ifExpr->elseBody);
    }
}

void ASTChecker::CheckAST(Node& node)
{
    auto visitPre = [this](Ptr<Node> curNode) {
        if (curNode->IsInvalid() || curNode->TestAttr(Attribute::IS_BROKEN)) {
            return VisitAction::SKIP_CHILDREN;
        }
        if (curNode->TestAttr(Attribute::FROM_COMMON_PART)) {
            return VisitAction::SKIP_CHILDREN;
        }
        CheckNode(curNode);
        if (curNode->IsDecl()) {
            CheckDecl(curNode);
        } else if (curNode->IsExpr()) {
            CheckExpr(curNode);
        }
        if (checkFuncMap.count(curNode->astKind) != 0) {
            checkFuncMap[curNode->astKind](this, curNode);
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker = Walker(&node, visitPre);
    walker.Walk();
};

void ASTChecker::CheckAST(const std::vector<OwnedPtr<Package>>& pkgs)
{
    for (auto& pkg : pkgs) {
        CheckAST(*pkg.get());
    }
    if (!checkInfoSet.empty()) {
        for (auto& checkInfo : checkInfoSet) {
            std::cerr << checkInfo << std::endl;
        }
        CJC_ABORT();
    }
}
void ASTChecker::CheckBeginEnd(Ptr<Node> node)
{
    std::set<ASTKind> ignoredKindSet = {ASTKind::PACKAGE, ASTKind::PACKAGE_DECL};
    auto visitPre = [&ignoredKindSet, this](Ptr<Node> curNode) {
        if (curNode->IsInvalid() || curNode->TestAttr(Attribute::IS_BROKEN)) {
            return VisitAction::SKIP_CHILDREN;
        }
        if (curNode->TestAttr(Attribute::FROM_COMMON_PART)) {
            return VisitAction::SKIP_CHILDREN;
        }
        if (ignoredKindSet.count(curNode->astKind) > 0) {
            return VisitAction::WALK_CHILDREN;
        }
        if (curNode->TestAttr(Attribute::COMPILER_ADD)) {
            return VisitAction::WALK_CHILDREN;
        }
        if (curNode->begin.IsZero()) {
            CollectInfo(curNode, "->begin");
        }
        if (curNode->end.IsZero()) {
            CollectInfo(curNode, "->end");
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker walker = Walker(node, visitPre);
    walker.Walk();
}
void ASTChecker::CheckBeginEnd(const std::vector<OwnedPtr<Package>>& pkgs)
{
    for (auto& pkg : pkgs) {
        CheckBeginEnd(pkg.get());
    }
    if (!checkInfoSet.empty()) {
        for (auto& filePath : checkInfoSet) {
            std::cerr << filePath << std::endl;
        }
        CJC_ABORT();
    }
}
void ASTChecker::CheckAnnotation(Ptr<Node> node)
{
    auto anno = StaticAs<ASTKind::ANNOTATION>(node);
    EMPTY_STRING_CHECK(node, anno->identifier.Val());
    VEC_AST_NULLPTR_CHECK(node, anno->args);
    switch (anno->kind) {
        case AnnotationKind::JAVA:
            EMPTY_STRING_CHECK(node, anno->definedPackage);
            break;
        case AnnotationKind::WHEN:
            AST_NULLPTR_CHECK(node, anno->condExpr);
            break;
        case AnnotationKind::ATTRIBUTE:
            VEC_ZERO_POS_CHECK(node, anno->attrCommas);
            break;
        default:
            break;
    }
}
void ASTChecker::CheckClassBody(Ptr<Node> node)
{
    auto cb = StaticAs<ASTKind::CLASS_BODY>(node);
    ZERO_POSITION_CHECK(node, cb->leftCurlPos);
    VEC_AST_NULLPTR_CHECK(node, cb->decls);
    ZERO_POSITION_CHECK(node, cb->rightCurlPos);
}
void ASTChecker::CheckDecl(Ptr<Node> node)
{
    auto decl = StaticAs<ASTKind::DECL>(node);
    VEC_AST_NULLPTR_CHECK(node, decl->annotations);
    EMPTY_IDENTIFIER_CHECK(node, decl->identifier);
    ZERO_POSITION_CHECK(node, decl->keywordPos);

    EMPTY_STRING_CHECK(node, decl->fullPackageName);
}
void ASTChecker::CheckFile(Ptr<Node> node)
{
    auto file = StaticAs<ASTKind::FILE>(node);
    EMPTY_STRING_CHECK(node, file->fileName);
    EMPTY_STRING_CHECK(node, file->filePath);
    VEC_AST_NULLPTR_CHECK(node, file->imports);
    VEC_AST_NULLPTR_CHECK(node, file->decls);
    VEC_AST_NULLPTR_CHECK(node, file->trashBin);
    ATTR_NULLPTR_CHECK(node, file->curPackage);
}
void ASTChecker::CheckFuncArg(Ptr<Node> node)
{
    auto funcArg = StaticAs<ASTKind::FUNC_ARG>(node);
    AST_NULLPTR_CHECK(node, funcArg->expr);
}
void ASTChecker::CheckFuncBody(Ptr<Node> node)
{
    auto funcBody = StaticAs<ASTKind::FUNC_BODY>(node);
    VEC_AST_NULLPTR_CHECK(node, funcBody->paramLists);
}

void ASTChecker::CheckFuncParamList(Ptr<Node> node)
{
    auto fpl = StaticAs<ASTKind::FUNC_PARAM_LIST>(node);
    ZERO_POSITION_CHECK(node, fpl->leftParenPos);
    ZERO_POSITION_CHECK(node, fpl->rightParenPos);
    VEC_AST_NULLPTR_CHECK(node, fpl->params);
}
void ASTChecker::CheckMacroExpandParam(Ptr<Node> node)
{
    auto mp = StaticAs<ASTKind::MACRO_EXPAND_PARAM>(node);
    EMPTY_STRING_CHECK(node, mp->invocation.fullName);
    VEC_ZERO_POS_CHECK(node, mp->invocation.fullNameDotPos);
    EMPTY_STRING_CHECK(node, mp->invocation.identifier);
    ZERO_POSITION_CHECK(node, mp->invocation.identifierPos);
}
void ASTChecker::CheckGeneric(Ptr<Node> node)
{
    auto ge = StaticAs<ASTKind::GENERIC>(node);
    ZERO_POSITION_CHECK(node, ge->leftAnglePos);
    ZERO_POSITION_CHECK(node, ge->rightAnglePos);
    VEC_AST_NULLPTR_CHECK(node, ge->typeParameters);
    VEC_AST_NULLPTR_CHECK(node, ge->genericConstraints);
}
void ASTChecker::CheckGenericConstraint(Ptr<Node> node)
{
    auto gc = StaticAs<ASTKind::GENERIC_CONSTRAINT>(node);
    ZERO_POSITION_CHECK(node, gc->wherePos);
    AST_NULLPTR_CHECK(node, gc->type);
    ZERO_POSITION_CHECK(node, gc->operatorPos);
    VEC_ZERO_POS_CHECK(node, gc->bitAndPos);
    VEC_AST_NULLPTR_CHECK(node, gc->upperBounds);
    ZERO_POSITION_CHECK(node, gc->commaPos);
}
void ASTChecker::CheckImportContent(Ptr<Node> node)
{
    auto ic = StaticAs<ASTKind::IMPORT_CONTENT>(node);

    VEC_EMPTY_STRING_CHECK(node, ic->prefixPaths);
    VEC_ZERO_POS_CHECK(node, ic->prefixPoses);
    VEC_ZERO_POS_CHECK(node, ic->prefixDotPoses);
    if (ic->kind != ImportKind::IMPORT_MULTI) {
        EMPTY_IDENTIFIER_CHECK(node, ic->identifier);
        if (ic->kind == ImportKind::IMPORT_ALIAS) {
            EMPTY_IDENTIFIER_CHECK(node, ic->aliasName);
        }
    } else {
        ZERO_POSITION_CHECK(node, ic->leftCurlPos);
        VEC_ZERO_POS_CHECK(node, ic->commaPoses);
        ZERO_POSITION_CHECK(node, ic->rightCurlPos);
        for (auto& item : ic->items) {
            CheckImportContent(&item);
        }
    }
}

void ASTChecker::CheckImportSpec(Ptr<Node> node)
{
    auto is = StaticAs<ASTKind::IMPORT_SPEC>(node);
    ZERO_POSITION_CHECK(node, is->importPos);
    VEC_AST_NULLPTR_CHECK(node, is->annotations);
}
void ASTChecker::CheckInterfaceBody(Ptr<Node> node)
{
    auto ib = StaticAs<ASTKind::INTERFACE_BODY>(node);
    ZERO_POSITION_CHECK(node, ib->leftCurlPos);
    ZERO_POSITION_CHECK(node, ib->rightCurlPos);
    VEC_AST_NULLPTR_CHECK(node, ib->decls);
}
void ASTChecker::CheckMatchCase(Ptr<Node> node)
{
    auto mc = StaticAs<ASTKind::MATCH_CASE>(node);
    VEC_AST_NULLPTR_CHECK(node, mc->patterns);
    ZERO_POSITION_CHECK(node, mc->arrowPos);
    AST_NULLPTR_CHECK(node, mc->exprOrDecls);
}
void ASTChecker::CheckMatchCaseOther(Ptr<Node> node)
{
    auto mco = StaticAs<ASTKind::MATCH_CASE_OTHER>(node);
    AST_NULLPTR_CHECK(node, mco->matchExpr);
    ZERO_POSITION_CHECK(node, mco->arrowPos);
    AST_NULLPTR_CHECK(node, mco->exprOrDecls);
}
void ASTChecker::CheckPackage(Ptr<Node> node)
{
    auto p = StaticAs<ASTKind::PACKAGE>(node);
    VEC_AST_NULLPTR_CHECK(node, p->files);
    VEC_AST_NULLPTR_CHECK(node, p->genericInstantiatedDecls);
    VEC_AST_NULLPTR_CHECK(node, p->inlineFuncDecls);
}
void ASTChecker::CheckPackageSpec(Ptr<Node> node)
{
    auto ps = StaticAs<ASTKind::PACKAGE_SPEC>(node);
    EMPTY_IDENTIFIER_CHECK(node, ps->packageName);
    ZERO_POSITION_CHECK(node, ps->packagePos);
    VEC_EMPTY_STRING_CHECK(node, ps->prefixPaths);
    VEC_ZERO_POS_CHECK(node, ps->prefixPoses);
    VEC_ZERO_POS_CHECK(node, ps->prefixDotPoses);
    if (ps->hasMacro) {
        ZERO_POSITION_CHECK(node, ps->macroPos);
    }
}
void ASTChecker::CheckFeaturesDirective(Ptr<Node> node)
{
    auto fs = StaticAs<ASTKind::FEATURES_DIRECTIVE>(node);
    EMPTY_VEC_CHECK(node, fs->content);
    VEC_ZERO_POS_CHECK(node, fs->commaPoses);
}
void ASTChecker::CheckFeatureId(Ptr<Node> node)
{
    auto fc = StaticAs<ASTKind::FEATURE_ID>(node);
    for (auto &ident : fc->identifiers) {
        EMPTY_IDENTIFIER_CHECK(node, ident);
    }
    VEC_ZERO_POS_CHECK(node, fc->dotPoses);
}
void ASTChecker::CheckStructBody(Ptr<Node> node)
{
    auto sb = StaticAs<ASTKind::STRUCT_BODY>(node);
    ZERO_POSITION_CHECK(node, sb->leftCurlPos);
    ZERO_POSITION_CHECK(node, sb->rightCurlPos);
    VEC_AST_NULLPTR_CHECK(node, sb->decls);
}
void ASTChecker::CheckFuncDecl(Ptr<Node> node)
{
    auto fd = StaticAs<ASTKind::FUNC_DECL>(node);
    ZERO_POSITION_CHECK(node, fd->leftParenPos);
    ZERO_POSITION_CHECK(node, fd->rightParenPos);
    AST_NULLPTR_CHECK(node, fd->funcBody);
}
void ASTChecker::CheckInheritableDecl(Ptr<Node> node)
{
    auto id = RawStaticCast<InheritableDecl*>(node);

    ZERO_POSITION_CHECK(node, id->upperBoundPos);
    VEC_AST_NULLPTR_CHECK(node, id->inheritedTypes);
}
void ASTChecker::CheckMacroDecl(Ptr<Node> node)
{
    auto md = StaticAs<ASTKind::MACRO_DECL>(node);
    AST_NULLPTR_CHECK(node, md->funcBody);
}
void ASTChecker::CheckMacroExpandDecl(Ptr<Node> node)
{
    auto me = StaticAs<ASTKind::MACRO_EXPAND_DECL>(node);
    EMPTY_STRING_CHECK(node, me->invocation.fullName);
    VEC_ZERO_POS_CHECK(node, me->invocation.fullNameDotPos);
    EMPTY_STRING_CHECK(node, me->invocation.identifier);
    ZERO_POSITION_CHECK(node, me->invocation.identifierPos);
}
void ASTChecker::CheckMainDecl(Ptr<Node> node)
{
    auto md = StaticAs<ASTKind::MAIN_DECL>(node);
    AST_NULLPTR_CHECK(node, md->funcBody);
}
void ASTChecker::CheckPackageDecl(Ptr<Node> node)
{
    auto pd = StaticAs<ASTKind::PACKAGE_DECL>(node);
    ATTR_NULLPTR_CHECK(node, pd->srcPackage);
}

void ASTChecker::CheckPrimaryCtorDecl(Ptr<Node> node)
{
    auto pc = StaticAs<ASTKind::PRIMARY_CTOR_DECL>(node);
    AST_NULLPTR_CHECK(node, pc->funcBody);
}
void ASTChecker::CheckTypeAliasDecl(Ptr<Node> node)
{
    auto ta = StaticAs<ASTKind::TYPE_ALIAS_DECL>(node);
    ZERO_POSITION_CHECK(node, ta->assignPos);
    AST_NULLPTR_CHECK(node, ta->type);
}
void ASTChecker::CheckVarWithPatternDecl(Ptr<Node> node)
{
    auto vwp = StaticAs<ASTKind::VAR_WITH_PATTERN_DECL>(node);
    AST_NULLPTR_CHECK(node, vwp->irrefutablePattern);
}

void ASTChecker::CheckFuncParam(Ptr<Node> node)
{
    auto fp = StaticAs<ASTKind::FUNC_PARAM>(node);
    ZERO_POSITION_CHECK(node, fp->colonPos);
}
void ASTChecker::CheckPropDecl(Ptr<Node> node)
{
    auto pd = StaticAs<ASTKind::PROP_DECL>(node);
    ZERO_POSITION_CHECK(node, pd->colonPos);
    AST_NULLPTR_CHECK(node, pd->type);
    ZERO_POSITION_CHECK(node, pd->leftCurlPos);
    VEC_AST_NULLPTR_CHECK(node, pd->getters);
    VEC_AST_NULLPTR_CHECK(node, pd->setters);
    ZERO_POSITION_CHECK(node, pd->rightCurlPos);
}
void ASTChecker::CheckClassLikeDecl(Ptr<Node> node)
{
    CheckInheritableDecl(node);
}
void ASTChecker::CheckClassDecl(Ptr<Node> node)
{
    CheckClassLikeDecl(node);
    auto cd = StaticAs<ASTKind::CLASS_DECL>(node);
    AST_NULLPTR_CHECK(node, cd->body);
}
void ASTChecker::CheckInterfaceDecl(Ptr<Node> node)
{
    CheckClassLikeDecl(node);
    auto id = StaticAs<ASTKind::INTERFACE_DECL>(node);
    AST_NULLPTR_CHECK(node, id->body);
}
void ASTChecker::CheckEnumDecl(Ptr<Node> node)
{
    CheckInheritableDecl(node);
    auto ed = StaticAs<ASTKind::ENUM_DECL>(node);
    ZERO_POSITION_CHECK(node, ed->leftCurlPos);
    AST_NULLPTR_CHECK(node, ed->bodyScope);
    VEC_AST_NULLPTR_CHECK(node, ed->constructors);
    VEC_ZERO_POS_CHECK(node, ed->bitOrPosVector);
    VEC_AST_NULLPTR_CHECK(node, ed->members);
}
void ASTChecker::CheckExtendDecl(Ptr<Node> node)
{
    CheckInheritableDecl(node);
    auto ed = StaticAs<ASTKind::EXTEND_DECL>(node);
    AST_NULLPTR_CHECK(node, ed->extendedType);
    AST_NULLPTR_CHECK(node, ed->bodyScope);
    ZERO_POSITION_CHECK(node, ed->leftCurlPos);
    ZERO_POSITION_CHECK(node, ed->rightCurlPos);
    VEC_AST_NULLPTR_CHECK(node, ed->members);
}
void ASTChecker::CheckStructDecl(Ptr<Node> node)
{
    CheckInheritableDecl(node);
    auto sd = StaticAs<ASTKind::STRUCT_DECL>(node);
    AST_NULLPTR_CHECK(node, sd->body);
}
void ASTChecker::CheckArrayExpr(Ptr<Node> node)
{
    auto ae = StaticAs<ASTKind::ARRAY_EXPR>(node);
    AST_NULLPTR_CHECK(node, ae->type);
    ZERO_POSITION_CHECK(node, ae->leftParenPos);
    VEC_AST_NULLPTR_CHECK(node, ae->args);
    VEC_ZERO_POS_CHECK(node, ae->commaPosVector);
    ZERO_POSITION_CHECK(node, ae->rightParenPos);
}
void ASTChecker::CheckArrayLit(Ptr<Node> node)
{
    auto al = StaticAs<ASTKind::ARRAY_LIT>(node);
    ZERO_POSITION_CHECK(node, al->leftSquarePos);
    VEC_AST_NULLPTR_CHECK(node, al->children);
    VEC_ZERO_POS_CHECK(node, al->commaPosVector);
    ZERO_POSITION_CHECK(node, al->rightSquarePos);
}
void ASTChecker::CheckAsExpr(Ptr<Node> node)
{
    auto ae = StaticAs<ASTKind::AS_EXPR>(node);
    AST_NULLPTR_CHECK(node, ae->leftExpr);
    AST_NULLPTR_CHECK(node, ae->asType);
    ZERO_POSITION_CHECK(node, ae->asPos);
}
void ASTChecker::CheckBlock(Ptr<Node> node)
{
    auto ae = StaticAs<ASTKind::BLOCK>(node);
    ZERO_POSITION_CHECK(node, ae->leftCurlPos);
    VEC_AST_NULLPTR_CHECK(node, ae->body);
    ZERO_POSITION_CHECK(node, ae->rightCurlPos);
}
void ASTChecker::CheckCallExpr(Ptr<Node> node)
{
    auto ce = StaticAs<ASTKind::CALL_EXPR>(node);
    AST_NULLPTR_CHECK(node, ce->baseFunc);
    ZERO_POSITION_CHECK(node, ce->leftParenPos);
    VEC_AST_NULLPTR_CHECK(node, ce->args);
    ZERO_POSITION_CHECK(node, ce->rightParenPos);
}
void ASTChecker::CheckDoWhileExpr(Ptr<Node> node)
{
    auto dwe = StaticAs<ASTKind::DO_WHILE_EXPR>(node);
    ZERO_POSITION_CHECK(node, dwe->doPos);
    AST_NULLPTR_CHECK(node, dwe->body);
    ZERO_POSITION_CHECK(node, dwe->whilePos);
    ZERO_POSITION_CHECK(node, dwe->leftParenPos);
    AST_NULLPTR_CHECK(node, dwe->condExpr);
    ZERO_POSITION_CHECK(node, dwe->rightParenPos);
}
void ASTChecker::CheckForInExpr(Ptr<Node> node)
{
    auto fie = StaticAs<ASTKind::FOR_IN_EXPR>(node);
    ZERO_POSITION_CHECK(node, fie->leftParenPos);
    AST_NULLPTR_CHECK(node, fie->pattern);
    ZERO_POSITION_CHECK(node, fie->rightParenPos);
    if (!fie->wherePos.IsZero()) {
        AST_NULLPTR_CHECK(node, fie->patternGuard);
    }
    AST_NULLPTR_CHECK(node, fie->body);
}
void ASTChecker::CheckIsExpr(Ptr<Node> node)
{
    auto ie = StaticAs<ASTKind::IS_EXPR>(node);
    AST_NULLPTR_CHECK(node, ie->leftExpr);
    AST_NULLPTR_CHECK(node, ie->isType);
    ZERO_POSITION_CHECK(node, ie->isPos);
}

void ASTChecker::CheckLambdaExpr(Ptr<Node> node)
{
    auto le = StaticAs<ASTKind::LAMBDA_EXPR>(node);
    AST_NULLPTR_CHECK(node, le->funcBody);
}
void ASTChecker::CheckLetPatternDestructor(Ptr<Node> node)
{
    auto lpd = StaticAs<ASTKind::LET_PATTERN_DESTRUCTOR>(node);
    for (auto& p : lpd->patterns) {
        AST_NULLPTR_CHECK(node, p);
    }
    for (auto& ors : lpd->orPos) {
        ZERO_POSITION_CHECK(node, ors);
    }
    AST_NULLPTR_CHECK(node, lpd->initializer);
    ZERO_POSITION_CHECK(node, lpd->backarrowPos);
}

void ASTChecker::CheckLitConstExpr(Ptr<Node> node)
{
    auto lce = StaticAs<ASTKind::LIT_CONST_EXPR>(node);
    EMPTY_STRING_CHECK(node, lce->stringValue);
}
void ASTChecker::CheckMacroExpandExpr(Ptr<Node> node)
{
    auto me = StaticAs<ASTKind::MACRO_EXPAND_EXPR>(node);
    EMPTY_STRING_CHECK(node, me->invocation.fullName);
    VEC_ZERO_POS_CHECK(node, me->invocation.fullNameDotPos);

    EMPTY_STRING_CHECK(node, me->invocation.identifier);
    ZERO_POSITION_CHECK(node, me->invocation.identifierPos);
    VEC_AST_NULLPTR_CHECK(node, me->annotations);
}
void ASTChecker::CheckMatchExpr(Ptr<Node> node)
{
    auto me = StaticAs<ASTKind::MATCH_EXPR>(node);
    ZERO_POSITION_CHECK(node, me->leftCurlPos);
    VEC_AST_NULLPTR_CHECK(node, me->matchCases);
    VEC_AST_NULLPTR_CHECK(node, me->matchCaseOthers);
    ZERO_POSITION_CHECK(node, me->rightCurlPos);
    if (me->matchMode) {
        AST_NULLPTR_CHECK(node, me->selector);
        ZERO_POSITION_CHECK(node, me->leftParenPos);
        ZERO_POSITION_CHECK(node, me->rightParenPos);
    }
}
void ASTChecker::CheckMemberAccess(Ptr<Node> node)
{
    auto ma = StaticAs<ASTKind::MEMBER_ACCESS>(node);
    AST_NULLPTR_CHECK(node, ma->baseExpr);
    ZERO_POSITION_CHECK(node, ma->dotPos);
    EMPTY_IDENTIFIER_CHECK(node, ma->field);
}
void ASTChecker::CheckTypeConvExpr(Ptr<Node> node)
{
    auto ntc = StaticAs<ASTKind::TYPE_CONV_EXPR>(node);
    AST_NULLPTR_CHECK(node, ntc->type);
    ZERO_POSITION_CHECK(node, ntc->leftParenPos);
    AST_NULLPTR_CHECK(node, ntc->expr);
    ZERO_POSITION_CHECK(node, ntc->rightParenPos);
}
void ASTChecker::CheckIfAvailableExpr(Ptr<Node> node)
{
    auto ie = StaticCast<IfAvailableExpr>(node);
    AST_NULLPTR_CHECK(ie, ie->GetArg());
    AST_NULLPTR_CHECK(ie, ie->GetLambda1());
    AST_NULLPTR_CHECK(ie, ie->GetLambda2());
}

void ASTChecker::CheckParenExpr(Ptr<Node> node)
{
    auto pe = StaticAs<ASTKind::PAREN_EXPR>(node);
    ZERO_POSITION_CHECK(node, pe->leftParenPos);
    AST_NULLPTR_CHECK(node, pe->expr);
    ZERO_POSITION_CHECK(node, pe->rightParenPos);
}
void ASTChecker::CheckPointerExpr(Ptr<Node> node)
{
    auto pe = StaticAs<ASTKind::POINTER_EXPR>(node);
    AST_NULLPTR_CHECK(node, pe->type);
    AST_NULLPTR_CHECK(node, pe->arg);
}
void ASTChecker::CheckQuoteExpr(Ptr<Node> node)
{
    auto qe = StaticAs<ASTKind::QUOTE_EXPR>(node);
    ZERO_POSITION_CHECK(node, qe->quotePos);
    ZERO_POSITION_CHECK(node, qe->leftParenPos);
    ZERO_POSITION_CHECK(node, qe->rightParenPos);
}
void ASTChecker::CheckRangeExpr(Ptr<Node> node)
{
    auto re = StaticAs<ASTKind::RANGE_EXPR>(node);
    ZERO_POSITION_CHECK(node, re->rangePos);
}
void ASTChecker::CheckRefExpr(Ptr<Node> node)
{
    auto re = StaticAs<ASTKind::REF_EXPR>(node);
    EMPTY_IDENTIFIER_CHECK(node, re->ref.identifier);
}
void ASTChecker::CheckReturnExpr(Ptr<Node> node)
{
    auto re = StaticAs<ASTKind::RETURN_EXPR>(node);
    ZERO_POSITION_CHECK(node, re->returnPos);
    AST_NULLPTR_CHECK(node, re->expr);
}
void ASTChecker::CheckSpawnExpr(Ptr<Node> node)
{
    auto se = StaticAs<ASTKind::SPAWN_EXPR>(node);
    ZERO_POSITION_CHECK(node, se->spawnPos);
    AST_NULLPTR_CHECK(node, se->task);
}
void ASTChecker::CheckSynchronizedExpr(Ptr<Node> node)
{
    auto se = StaticAs<ASTKind::SYNCHRONIZED_EXPR>(node);
    ZERO_POSITION_CHECK(node, se->syncPos);
    ZERO_POSITION_CHECK(node, se->leftParenPos);
    AST_NULLPTR_CHECK(node, se->mutex);
    ZERO_POSITION_CHECK(node, se->rightParenPos);
    AST_NULLPTR_CHECK(node, se->body);
}
void ASTChecker::CheckThrowExpr(Ptr<Node> node)
{
    auto te = StaticAs<ASTKind::THROW_EXPR>(node);
    ZERO_POSITION_CHECK(node, te->throwPos);
    AST_NULLPTR_CHECK(node, te->expr);
}
void ASTChecker::CheckPerformExpr(Ptr<Node> node)
{
    auto pe = StaticAs<ASTKind::PERFORM_EXPR>(node);
    ZERO_POSITION_CHECK(node, pe->performPos);
    AST_NULLPTR_CHECK(node, pe->expr)   ;
}
void ASTChecker::CheckResumeExpr(Ptr<Node> node)
{
    auto re = StaticAs<ASTKind::RESUME_EXPR>(node);
    ZERO_POSITION_CHECK(node, re->resumePos);
    if (re->withExpr) {
        ZERO_POSITION_CHECK(node, re->withPos);
    }
    if (re->throwingExpr) {
        ZERO_POSITION_CHECK(node, re->throwingPos);
    }
}
void ASTChecker::CheckTrailingClosureExpr(Ptr<Node> node)
{
    auto tce = StaticAs<ASTKind::TRAIL_CLOSURE_EXPR>(node);
    ZERO_POSITION_CHECK(node, tce->leftLambda);
    AST_NULLPTR_CHECK(node, tce->expr);
    AST_NULLPTR_CHECK(node, tce->lambda);
    ZERO_POSITION_CHECK(node, tce->rightLambda);
}
void ASTChecker::CheckTryExpr(Ptr<Node> node)
{
    auto te = StaticAs<ASTKind::TRY_EXPR>(node);
    ZERO_POSITION_CHECK(node, te->tryPos);
    VEC_AST_NULLPTR_CHECK(node, te->resourceSpec);
    AST_NULLPTR_CHECK(node, te->tryBlock);
    VEC_ZERO_POS_CHECK(node, te->catchPosVector);
    VEC_AST_NULLPTR_CHECK(node, te->catchBlocks);
    VEC_AST_NULLPTR_CHECK(node, te->catchPatterns);
    for (const auto& handler : te->handlers) {
        ZERO_POSITION_CHECK(node, handler.pos);
        AST_NULLPTR_CHECK(node, handler.block);
        AST_NULLPTR_CHECK(node, handler.commandPattern);
    }
}
void ASTChecker::CheckTupleLit(Ptr<Node> node)
{
    auto te = StaticAs<ASTKind::TUPLE_LIT>(node);
    ZERO_POSITION_CHECK(node, te->leftParenPos);
    VEC_AST_NULLPTR_CHECK(node, te->children);
    VEC_ZERO_POS_CHECK(node, te->commaPosVector);
    ZERO_POSITION_CHECK(node, te->rightParenPos);
}
void ASTChecker::CheckWhileExpr(Ptr<Node> node)
{
    auto we = StaticAs<ASTKind::WHILE_EXPR>(node);
    ZERO_POSITION_CHECK(node, we->whilePos);
    ZERO_POSITION_CHECK(node, we->leftParenPos);
    AST_NULLPTR_CHECK(node, we->condExpr);
    ZERO_POSITION_CHECK(node, we->rightParenPos);
    AST_NULLPTR_CHECK(node, we->body);
}
void ASTChecker::CheckAssignExpr(Ptr<Node> node)
{
    auto ae = StaticAs<ASTKind::ASSIGN_EXPR>(node);
    AST_NULLPTR_CHECK(node, ae->leftValue);
    AST_NULLPTR_CHECK(node, ae->rightExpr);
    ZERO_POSITION_CHECK(node, ae->assignPos);
}
void ASTChecker::CheckBinaryExpr(Ptr<Node> node)
{
    auto be = StaticAs<ASTKind::BINARY_EXPR>(node);
    AST_NULLPTR_CHECK(node, be->leftExpr);
    AST_NULLPTR_CHECK(node, be->rightExpr);
    ZERO_POSITION_CHECK(node, be->operatorPos);
}
void ASTChecker::CheckIncOrDecExpr(Ptr<Node> node)
{
    auto iod = StaticAs<ASTKind::INC_OR_DEC_EXPR>(node);
    ZERO_POSITION_CHECK(node, iod->operatorPos);
    AST_NULLPTR_CHECK(node, iod->expr);
}
void ASTChecker::CheckSubscriptExpr(Ptr<Node> node)
{
    auto se = StaticAs<ASTKind::SUBSCRIPT_EXPR>(node);
    AST_NULLPTR_CHECK(node, se->baseExpr);
    ZERO_POSITION_CHECK(node, se->leftParenPos);
    VEC_AST_NULLPTR_CHECK(node, se->indexExprs);
    VEC_ZERO_POS_CHECK(node, se->commaPos);
    ZERO_POSITION_CHECK(node, se->rightParenPos);
}
void ASTChecker::CheckUnaryExpr(Ptr<Node> node)
{
    auto ue = StaticAs<ASTKind::UNARY_EXPR>(node);
    AST_NULLPTR_CHECK(node, ue->expr);
    ZERO_POSITION_CHECK(node, ue->operatorPos);
}
void ASTChecker::CheckConstantType(Ptr<Node> node)
{
    auto ct = StaticAs<ASTKind::CONSTANT_TYPE>(node);
    AST_NULLPTR_CHECK(node, ct->constantExpr);
    ZERO_POSITION_CHECK(node, ct->dollarPos);
}
void ASTChecker::CheckConstPattern(Ptr<Node> node)
{
    auto cp = StaticAs<ASTKind::CONST_PATTERN>(node);
    AST_NULLPTR_CHECK(node, cp->literal);
}
void ASTChecker::CheckEnumPattern(Ptr<Node> node)
{
    auto ep = StaticAs<ASTKind::ENUM_PATTERN>(node);
    AST_NULLPTR_CHECK(node, ep->constructor);
    VEC_AST_NULLPTR_CHECK(node, ep->patterns);
    ZERO_POSITION_CHECK(node, ep->leftParenPos);
    ZERO_POSITION_CHECK(node, ep->rightParenPos);
}
void ASTChecker::CheckExceptTypePattern(Ptr<Node> node)
{
    auto etp = StaticAs<ASTKind::EXCEPT_TYPE_PATTERN>(node);
    AST_NULLPTR_CHECK(node, etp->pattern);
    ZERO_POSITION_CHECK(node, etp->patternPos);
    ZERO_POSITION_CHECK(node, etp->colonPos);
    VEC_AST_NULLPTR_CHECK(node, etp->types);
    VEC_ZERO_POS_CHECK(node, etp->bitOrPosVector);
}

void ASTChecker::CheckCommandTypePattern(Ptr<Node> node)
{
    auto etp = StaticAs<ASTKind::COMMAND_TYPE_PATTERN>(node);
    AST_NULLPTR_CHECK(node, etp->pattern);
    ZERO_POSITION_CHECK(node, etp->patternPos);
    ZERO_POSITION_CHECK(node, etp->colonPos);
    VEC_AST_NULLPTR_CHECK(node, etp->types);
    VEC_ZERO_POS_CHECK(node, etp->bitOrPosVector);
}

void ASTChecker::CheckTuplePattern(Ptr<Node> node)
{
    auto tp = StaticAs<ASTKind::TUPLE_PATTERN>(node);
    ZERO_POSITION_CHECK(node, tp->leftBracePos);
    VEC_AST_NULLPTR_CHECK(node, tp->patterns);
    ZERO_POSITION_CHECK(node, tp->rightBracePos);
}
void ASTChecker::CheckTypePattern(Ptr<Node> node)
{
    auto tp = StaticAs<ASTKind::TYPE_PATTERN>(node);
    AST_NULLPTR_CHECK(node, tp->pattern);
    ZERO_POSITION_CHECK(node, tp->colonPos);
    AST_NULLPTR_CHECK(node, tp->type);
}
void ASTChecker::CheckVarOrEnumPattern(Ptr<Node> node)
{
    auto voe = StaticAs<ASTKind::VAR_OR_ENUM_PATTERN>(node);
    EMPTY_STRING_CHECK(node, voe->identifier.Val());
}
void ASTChecker::CheckVarPattern(Ptr<Node> node)
{
    auto vp = StaticAs<ASTKind::VAR_PATTERN>(node);
    AST_NULLPTR_CHECK(node, vp->varDecl);
}
void ASTChecker::CheckVArrayType(Ptr<Node> node)
{
    auto vat = StaticAs<ASTKind::VARRAY_TYPE>(node);
    AST_NULLPTR_CHECK(node, vat->typeArgument);
    AST_NULLPTR_CHECK(node, vat->constantType);
    ZERO_POSITION_CHECK(node, vat->commaPos);
    ZERO_POSITION_CHECK(node, vat->leftAnglePos);
    ZERO_POSITION_CHECK(node, vat->rightAnglePos);
}
void ASTChecker::CheckFuncType(Ptr<Node> node)
{
    auto ft = StaticAs<ASTKind::FUNC_TYPE>(node);
    ZERO_POSITION_CHECK(node, ft->leftParenPos);
    VEC_AST_NULLPTR_CHECK(node, ft->paramTypes);
    ZERO_POSITION_CHECK(node, ft->rightParenPos);
    ZERO_POSITION_CHECK(node, ft->arrowPos);
    AST_NULLPTR_CHECK(node, ft->retType);
}
void ASTChecker::CheckOptionType(Ptr<Node> node)
{
    auto ot = StaticAs<ASTKind::OPTION_TYPE>(node);
    if (ot->questNum > 0) {
        AST_NULLPTR_CHECK(node, ot->componentType);
    }
    VEC_ZERO_POS_CHECK(node, ot->questVector);
}
void ASTChecker::CheckParenType(Ptr<Node> node)
{
    auto pt = StaticAs<ASTKind::PAREN_TYPE>(node);
    ZERO_POSITION_CHECK(node, pt->leftParenPos);
    AST_NULLPTR_CHECK(node, pt->type);
    ZERO_POSITION_CHECK(node, pt->rightParenPos);
}

void ASTChecker::CheckPrimitiveType(Ptr<Node> node)
{
    auto pt = StaticAs<ASTKind::PRIMITIVE_TYPE>(node);
    EMPTY_STRING_CHECK(node, pt->str);
}
void ASTChecker::CheckQualifiedType(Ptr<Node> node)
{
    auto qt = StaticAs<ASTKind::QUALIFIED_TYPE>(node);
    AST_NULLPTR_CHECK(node, qt->baseType);
    ZERO_POSITION_CHECK(node, qt->dotPos);
    EMPTY_IDENTIFIER_CHECK(node, qt->field);
}

void ASTChecker::CheckRefType(Ptr<Node> node)
{
    auto rt = StaticAs<ASTKind::REF_TYPE>(node);

    EMPTY_IDENTIFIER_CHECK(node, rt->ref.identifier);
}

void ASTChecker::CheckTupleType(Ptr<Node> node)
{
    auto tt = StaticAs<ASTKind::TUPLE_TYPE>(node);
    ZERO_POSITION_CHECK(node, tt->leftParenPos);
    ZERO_POSITION_CHECK(node, tt->rightParenPos);
    VEC_AST_NULLPTR_CHECK(node, tt->fieldTypes);
    VEC_ZERO_POS_CHECK(node, tt->commaPosVector);
}

void ASTChecker::CollectInfo(Ptr<Node> node, const std::string& subInfo)
{
    std::ostringstream item;
    item << "ASTChecker: " << ASTKIND_TO_STR.at(node->astKind) << "'s "
         << subInfo.substr(subInfo.rfind("->") + std::string("->").size()) << " is not initialized at ";
    if (node->curFile != nullptr) {
        item << node->curFile->filePath;
    }
    if (!node->begin.IsZero()) {
        item << ":" << node->begin.line << ":" << node->begin.column;
    } else if (!node->end.IsZero()) {
        item << ":" << node->end.line << ":" << node->end.column;
    }

    checkInfoSet.emplace(item.str());
}

// The following syntax tree nodes do not have attributes or do not check their attributes.So generate Null
// implementation
#define NULL_IMPLEMENTATION(nodeName)                                                                                  \
    void ASTChecker::Check##nodeName(Ptr<Node>)                                                                        \
    {                                                                                                                  \
    }
NULL_IMPLEMENTATION(Modifier)
NULL_IMPLEMENTATION(BuiltInDecl)
NULL_IMPLEMENTATION(VarDecl)
NULL_IMPLEMENTATION(GenericParamDecl)
NULL_IMPLEMENTATION(InvalidDecl)
NULL_IMPLEMENTATION(Pattern)
NULL_IMPLEMENTATION(WildcardPattern)
NULL_IMPLEMENTATION(InvalidPattern)
NULL_IMPLEMENTATION(Type)
NULL_IMPLEMENTATION(ThisType)
NULL_IMPLEMENTATION(InvalidType)
NULL_IMPLEMENTATION(OptionalExpr)
NULL_IMPLEMENTATION(OptionalChainExpr)
NULL_IMPLEMENTATION(PrimitiveTypeExpr)
NULL_IMPLEMENTATION(InterpolationExpr)
NULL_IMPLEMENTATION(StrInterpolationExpr)
NULL_IMPLEMENTATION(TokenPart)
NULL_IMPLEMENTATION(JumpExpr)
NULL_IMPLEMENTATION(InvalidExpr)
NULL_IMPLEMENTATION(DummyBody)
NULL_IMPLEMENTATION(WildcardExpr)
#undef NULL_IMPLEMENTATION
} // namespace Cangjie::AST
