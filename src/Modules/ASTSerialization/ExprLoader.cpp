// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 * This file implements the AST Loader related classes.
 */

#include "ASTLoaderImpl.h"

#include "flatbuffers/ModuleFormat_generated.h"

#include "cangjie/AST/ASTCasting.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/Lex/Token.h"
#include "cangjie/Utils/CheckUtils.h"

using namespace Cangjie;
using namespace AST;

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadInvalidExpr(const PackageFormat::Expr& expr)
{
    (void)diag.DiagnoseRefactor(DiagKindRefactor::package_unsupported_load, DEFAULT_POSITION, "expression");
    auto invalidExpr = MakeOwned<InvalidExpr>(INVALID_POSITION);
    LoadNodePos(expr, *invalidExpr);
    return invalidExpr;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadWildcardExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto we = CreateAndLoadBasicInfo<WildcardExpr>(expr, exprIndex);
    return we;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadPrimitiveTypeExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto index = expr.type();
    CJC_ASSERT(index != INVALID_FORMAT_INDEX);
    auto typeObj = package->allTypes()->Get(static_cast<uoffset_t>(expr.type() - 1));
    auto kind = GetASTTypeKind(typeObj->kind());
    CJC_ASSERT(kind != TypeKind::TYPE_INVALID);
    auto pte = CreateAndLoadBasicInfo<PrimitiveTypeExpr>(expr, exprIndex, kind);
    return pte;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadReturnExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    CJC_ASSERT(expr.operands()->size() == 1);
    auto re = CreateAndLoadBasicInfo<ReturnExpr>(expr, exprIndex);
    re->expr = LoadExpr(expr.operands()->Get(0));
    return re;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadJumpExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto je = CreateAndLoadBasicInfo<JumpExpr>(expr, exprIndex);
    auto info = expr.info_as_JumpInfo();
    CJC_NULLPTR_CHECK(info);
    je->isBreak = info->isBreak();
    return je;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadMemberAccess(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto ma = CreateAndLoadBasicInfo<MemberAccess>(expr, exprIndex);
    CJC_ASSERT(expr.operands()->size() == 1);
    ma->baseExpr = LoadExpr(expr.operands()->Get(0));
    auto info = expr.info_as_ReferenceInfo();
    CJC_NULLPTR_CHECK(info);
    ma->field = info->reference()->str();
    if (auto nre = DynamicCast<NameReferenceExpr*>(ma->baseExpr.get())) {
        nre->isAlone = false;
    }
    return ma;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadRefExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto re = CreateAndLoadBasicInfo<RefExpr>(expr, exprIndex);
    auto info = expr.info_as_ReferenceInfo();
    CJC_NULLPTR_CHECK(info);
    re->ref.identifier = info->reference()->str();
    re->isThis = re->ref.identifier == "this";
    re->isSuper = re->ref.identifier == "super";
    return re;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadCallExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto ce = CreateAndLoadBasicInfo<CallExpr>(expr, exprIndex);
    CJC_ASSERT(expr.operands()->size() >= 1);
    ce->baseFunc = LoadExpr(expr.operands()->Get(0));
    std::vector<Ptr<FuncArg>> args;
    for (uoffset_t i = 1; i < expr.operands()->size(); i++) {
        auto fa = LoadFuncArg(expr.operands()->Get(i));
        args.emplace_back(fa.get());
        ce->args.emplace_back(std::move(fa));
    }
    ce->desugarArgs = args;
    auto info = expr.info_as_CallInfo();
    CJC_NULLPTR_CHECK(info);
    ce->callKind = CALL_KIND_RMAP.at(info->callKind());
    if (info->hasSideEffect()) {
        ce->EnableAttr(Attribute::SIDE_EFFECT);
    }
    return ce;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadUnaryExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto ue = CreateAndLoadBasicInfo<UnaryExpr>(expr, exprIndex);
    CJC_NULLPTR_CHECK(expr.operands());
    CJC_ASSERT(expr.operands()->size() == 1);
    ue->expr = LoadExpr(expr.operands()->Get(0));
    auto info = expr.info_as_UnaryInfo();
    CJC_NULLPTR_CHECK(info);
    ue->op = OP_KIND_RMAP.at(info->op());
    return ue;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadIncOrDecExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto ide = CreateAndLoadBasicInfo<IncOrDecExpr>(expr, exprIndex);
    CJC_ASSERT(expr.operands()->size() == 1);
    ide->expr = LoadExpr(expr.operands()->Get(0));
    auto info = expr.info_as_IncOrDecInfo();
    CJC_NULLPTR_CHECK(info);
    ide->op = OP_KIND_RMAP.at(info->op());
    return ide;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadLitConstExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto info = expr.info_as_LitConstInfo();
    CJC_NULLPTR_CHECK(info);
    auto lce = CreateAndLoadBasicInfo<LitConstExpr>(
        expr, exprIndex, LIT_CONST_KIND_RMAP.at(info->constKind()), info->strValue()->str());
    lce->stringKind = STRING_KIND_RMAP.at(info->strKind());
    if (lce->kind == LitConstKind::RUNE || lce->kind == LitConstKind::STRING) {
        lce->codepoint = StringConvertor::UTF8ToCodepoint(lce->stringValue);
    }
    return lce;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadBinaryExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto be = CreateAndLoadBasicInfo<BinaryExpr>(expr, exprIndex);
    CJC_ASSERT(expr.operands()->size() == 2); // Has 2 sub nodes.
    be->leftExpr = LoadExpr(expr.operands()->Get(0));
    be->rightExpr = LoadExpr(expr.operands()->Get(1));
    auto info = expr.info_as_BinaryInfo();
    CJC_NULLPTR_CHECK(info);
    be->op = OP_KIND_RMAP.at(info->op());
    return be;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadSubscriptExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto se = CreateAndLoadBasicInfo<SubscriptExpr>(expr, exprIndex);
    CJC_ASSERT(expr.operands()->size() > 1);
    se->baseExpr = LoadExpr(expr.operands()->Get(0));
    for (uoffset_t i = 1; i < expr.operands()->size(); i++) {
        auto index = expr.operands()->Get(i);
        se->indexExprs.emplace_back(LoadExpr(index));
    }
    auto info = expr.info_as_SubscriptInfo();
    CJC_NULLPTR_CHECK(info);
    se->isTupleAccess = info->isTupleAccess();
    return se;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadAssignExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto ae = CreateAndLoadBasicInfo<AssignExpr>(expr, exprIndex);
    ae->leftValue = LoadExpr(expr.operands()->Get(0));
    ae->rightExpr = LoadExpr(expr.operands()->Get(1));
    auto info = expr.info_as_AssignInfo();
    CJC_NULLPTR_CHECK(info);
    ae->op = OP_KIND_RMAP.at(info->op());
    ae->isCompound = info->isCompound();
    return ae;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadArrayExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto ae = CreateAndLoadBasicInfo<ArrayExpr>(expr, exprIndex);
    for (uoffset_t i = 0; i < expr.operands()->size(); i++) {
        auto index = expr.operands()->Get(i);
        ae->args.emplace_back(LoadFuncArg(index));
    }
    auto info = expr.info_as_ArrayInfo();
    CJC_NULLPTR_CHECK(info);
    ae->isValueArray = info->isValueArray();
    return ae;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadPointerExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto pe = CreateAndLoadBasicInfo<PointerExpr>(expr, exprIndex);
    if (expr.operands()->size() == 1) {
        pe->arg = LoadFuncArg(expr.operands()->Get(0));
    }
    return pe;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadTypeConvExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto tce = CreateAndLoadBasicInfo<TypeConvExpr>(expr, exprIndex);
    CJC_ASSERT(expr.operands()->size() == 1);
    tce->expr = LoadExpr(expr.operands()->Get(0));
    return tce;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadThrowExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto te = CreateAndLoadBasicInfo<ThrowExpr>(expr, exprIndex);
    CJC_ASSERT(expr.operands()->size() == 1);
    te->expr = LoadExpr(expr.operands()->Get(0));
    return te;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadSpawnExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto se = CreateAndLoadBasicInfo<SpawnExpr>(expr, exprIndex);
    // Sub node 'task' does not used for SpawnExpr with futureObj.
    if (expr.operands()->size() > 0) { // Optional argument.
        se->arg = LoadExpr(expr.operands()->Get(0));
    }
    // 'futureObj' is local decl, can be loaded at now.
    auto info = expr.info_as_SpawnInfo();
    CJC_NULLPTR_CHECK(info);
    auto fullId = info->future();
    se->futureObj = LoadDecl<VarDecl>(fullId->index());
    return se;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadArrayLit(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto al = CreateAndLoadBasicInfo<ArrayLit>(expr, exprIndex);
    for (uoffset_t i = 0; i < expr.operands()->size(); i++) {
        auto index = expr.operands()->Get(i);
        al->children.emplace_back(LoadExpr(index));
    }
    return al;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadTupleLit(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto tl = CreateAndLoadBasicInfo<TupleLit>(expr, exprIndex);
    for (uoffset_t i = 0; i < expr.operands()->size(); i++) {
        auto index = expr.operands()->Get(i);
        tl->children.emplace_back(LoadExpr(index));
    }
    return tl;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadIfExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto ie = CreateAndLoadBasicInfo<IfExpr>(expr, exprIndex);
    CJC_ASSERT(expr.operands()->size() >= 2); // Has at least 2 sub nodes.
    uoffset_t idx = 0;
    ie->condExpr = LoadExpr(expr.operands()->Get(idx++));
    ie->thenBody = LoadExpr<Block>(expr.operands()->Get(idx++));
    ie->elseBody = idx < expr.operands()->size() ? LoadExpr(expr.operands()->Get(idx)) : nullptr;
    ie->hasElse = ie->elseBody != nullptr;
    ie->isElseIf = ie->hasElse && ie->elseBody->astKind == ASTKind::IF_EXPR;
    if (ie->hasElse && ie->elseBody->desugarExpr) {
        // Recover normal block condition for else body.
        allDummyExprs.erase(ie->elseBody.get());
        ie->elseBody = std::move(ie->elseBody->desugarExpr);
    }
    return ie;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadTryExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto te = CreateAndLoadBasicInfo<TryExpr>(expr, exprIndex);
    CJC_ASSERT(expr.operands()->size() >= 1); // Has at least 1 sub node.
    uoffset_t idx = 0;
    te->tryBlock = LoadExpr<Block>(expr.operands()->Get(idx++));
    CJC_ASSERT(expr.operands()->size() > 0);
    for (; idx < expr.operands()->size() - 1; idx++) {
        auto index = expr.operands()->Get(idx);
        te->catchBlocks.emplace_back(LoadExpr<Block>(index));
    }
    te->finallyBlock = idx < expr.operands()->size() ? LoadExpr<Block>(expr.operands()->Get(idx)) : nullptr;
    auto info = expr.info_as_TryInfo();
    CJC_NULLPTR_CHECK(info);
    te->catchPatterns.resize(info->patterns()->size());
    for (uoffset_t i = 0; i < info->patterns()->size(); i++) {
        te->catchPatterns[i] = LoadPattern(*info->patterns()->Get(i));
    }
    // 'resourceSpec' is local decl, can be loaded at now.
    te->resourceSpec.resize(info->resources()->size());
    for (uoffset_t i = 0; i < info->resources()->size(); i++) {
        auto fullId = info->resources()->Get(i);
        te->resourceSpec[i] = LoadDecl<VarDecl>(fullId->index());
    }
    return te;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadWhileExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto we = CreateAndLoadBasicInfo<WhileExpr>(expr, exprIndex);
    CJC_ASSERT(expr.operands()->size() == 2); // Has 2 sub nodes.
    we->condExpr = LoadExpr(expr.operands()->Get(0));
    we->body = LoadExpr<Block>(expr.operands()->Get(1));
    return we;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadDoWhileExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto dwe = CreateAndLoadBasicInfo<DoWhileExpr>(expr, exprIndex);
    CJC_ASSERT(expr.operands()->size() == 2); // Has 2 sub nodes.
    dwe->body = LoadExpr<Block>(expr.operands()->Get(0));
    dwe->condExpr = LoadExpr(expr.operands()->Get(1));
    return dwe;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadLambdaExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto le = CreateAndLoadBasicInfo<LambdaExpr>(expr, exprIndex);
    auto info = expr.info_as_LambdaInfo();
    CJC_NULLPTR_CHECK(info);
    CJC_NULLPTR_CHECK(info->funcBody());
    le->funcBody = LoadFuncBody(*info->funcBody());
    if (info->supportMock()) {
        le->EnableAttr(Attribute::MOCK_SUPPORTED);
    }
    return le;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadBlock(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto block = CreateAndLoadBasicInfo<Block>(expr, exprIndex);
    // Additional last condition is whether the block is unsafe.
    auto info = expr.info_as_BlockInfo();
    CJC_NULLPTR_CHECK(info);
    CJC_ASSERT(expr.operands()->size() <= info->isExpr()->size());
    auto size = expr.operands()->size();
    block->body.resize(size);
    for (uoffset_t i = 0; i < size; ++i) {
        if (info->isExpr()->Get(i)) {
            block->body[i] = LoadExpr(expr.operands()->Get(i));
        } else {
            block->body[i] = LoadDecl(expr.operands()->Get(i));
        }
    }
    if (info->isExpr()->size() > size && info->isExpr()->Get(size) != 0) {
        block->EnableAttr(Attribute::UNSAFE);
    }
    return block;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadMatchExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto me = CreateAndLoadBasicInfo<MatchExpr>(expr, exprIndex);
    auto info = expr.info_as_MatchInfo();
    CJC_NULLPTR_CHECK(info);
    me->matchMode = info->matchMode();
    if (me->matchMode) {
        me->selector = LoadExpr(expr.operands()->Get(0));
        for (uoffset_t i = 1; i < expr.operands()->size(); i++) {
            auto index = expr.operands()->Get(i);
            me->matchCases.emplace_back(LoadMatchCase(index));
        }
    } else {
        for (uoffset_t i = 0; i < expr.operands()->size(); i++) {
            auto index = expr.operands()->Get(i);
            me->matchCaseOthers.emplace_back(LoadMatchCaseOther(index));
        }
    }
    return me;
}

OwnedPtr<AST::Expr> ASTLoader::ASTLoaderImpl::LoadLetPatternDestructor(
    const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto res = CreateAndLoadBasicInfo<LetPatternDestructor>(expr, exprIndex);
    res->initializer = LoadExpr(expr.operands()->Get(0));
    auto info = expr.info_as_LetPatternDestructorInfo();
    CJC_NULLPTR_CHECK(info);
    for (auto pattern : *info->patterns()) {
        res->patterns.push_back(LoadPattern(*pattern));
    }
    return res;
}

template <typename ExprT>
OwnedPtr<ExprT> ASTLoader::ASTLoaderImpl::LoadExpr(FormattedIndex exprIndex)
{
    if (exprIndex == INVALID_FORMAT_INDEX) {
        return {};
    }
    OwnedPtr<Expr> ret;
    auto expr = GetFormatExprByIndex(exprIndex);
    auto foundHandler = exprLoaderMap.find(expr->kind());
    if (foundHandler != exprLoaderMap.end()) {
        ret = foundHandler->second(this, *expr, exprIndex);
    } else {
        ret = LoadInvalidExpr(*expr);
    }
    if constexpr (!std::is_same_v<ExprT, Block>) {
        // Create wrapped 'desugarExpr' for CHIR to generate 'goto' correctly.
        if (ret->astKind == ASTKind::BLOCK) {
            auto dummy = MakeOwned<RefExpr>();
            dummy->EnableAttr(Attribute::COMPILER_ADD);
            dummy->desugarExpr = std::move(ret);
            ret = std::move(dummy);
            allDummyExprs.emplace(ret.get());
        }
    }
    // Caller guarantees casting type is matched.
    return OwnedPtr<ExprT>(StaticCast<ExprT*>(ret.release()));
}

OwnedPtr<MatchCase> ASTLoader::ASTLoaderImpl::LoadMatchCase(FormattedIndex index)
{
    auto mcObj = GetFormatExprByIndex(index);
    auto mc = CreateAndLoadBasicInfo<MatchCase>(*mcObj, INVALID_FORMAT_INDEX);
    CJC_ASSERT(mcObj->operands()->size() == 2); // Has 2 sub nodes.
    mc->patternGuard = LoadExpr(mcObj->operands()->Get(0));
    mc->exprOrDecls = LoadExpr<Block>(mcObj->operands()->Get(1));
    auto info = mcObj->info_as_MatchCaseInfo();
    CJC_NULLPTR_CHECK(info);
    for (uoffset_t i = 0; i < info->patterns()->size(); i++) {
        mc->patterns.emplace_back(LoadPattern(*info->patterns()->Get(i)));
    }
    return mc;
}

OwnedPtr<Expr> ASTLoader::ASTLoaderImpl::LoadForInExpr(const PackageFormat::Expr& expr, int64_t exprIndex)
{
    auto fie = CreateAndLoadBasicInfo<ForInExpr>(expr, exprIndex);
    CJC_ASSERT(expr.operands()->size() == 3U);
    auto info = expr.info_as_ForInInfo();
    CJC_NULLPTR_CHECK(info);
    fie->forInKind = FOR_IN_KIND_RMAP.at(info->forInKind());
    if (fie->forInKind == ForInKind::FORIN_INVALID) {
        fie->inExpression = LoadExpr(expr.operands()->Get(0));
    } else {
        fie->inExpression = LoadExpr<Block>(expr.operands()->Get(0));
    }
    fie->patternGuard = LoadExpr(expr.operands()->Get(1));
    fie->body = LoadExpr<Block>(expr.operands()->Get(2U));
    fie->pattern = LoadPattern(*info->pattern());
    return fie;
}

OwnedPtr<MatchCaseOther> ASTLoader::ASTLoaderImpl::LoadMatchCaseOther(FormattedIndex index)
{
    auto mcoObj = GetFormatExprByIndex(index);
    auto mco = CreateAndLoadBasicInfo<MatchCaseOther>(*mcoObj, INVALID_FORMAT_INDEX);
    CJC_ASSERT(mcoObj->operands()->size() == 2); // Has 2 sub nodes.
    mco->matchExpr = LoadExpr(mcoObj->operands()->Get(0));
    mco->exprOrDecls = LoadExpr<Block>(mcoObj->operands()->Get(1));
    return mco;
}

OwnedPtr<FuncBody> ASTLoader::ASTLoaderImpl::LoadFuncBody(
    const PackageFormat::FuncBody& body, bool isInline, bool isConst)
{
    auto funcBody = MakeOwned<FuncBody>();
    // FuncParamList
    for (uoffset_t i = 0; i < body.paramLists()->size(); i++) {
        funcBody->paramLists.emplace_back(LoadFuncParamList(body.paramLists()->Get(i)));
    }
    bool shouldImportBody = body.always() || isInline || isConst;
    if (importSrcCode && shouldImportBody) {
        funcBody->body = LoadExpr<Block>(body.body());
        funcBody->captureKind = static_cast<CaptureKind>(body.captureKind());
        // Set returnExpr's reference inside current funcBody.
        Walker(funcBody->body.get(), [&funcBody](auto node) {
            if (node->astKind == ASTKind::FUNC_BODY) {
                return VisitAction::SKIP_CHILDREN;
            }
            if (auto re = DynamicCast<ReturnExpr*>(node)) {
                re->refFuncBody = funcBody.get();
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
    // Mark imported for funcBody to create filter in generic instantiation step for partial instantiate.
    funcBody->EnableAttr(Attribute::IMPORTED);
    return funcBody;
}

OwnedPtr<FuncArg> ASTLoader::ASTLoaderImpl::LoadFuncArg(FormattedIndex index)
{
    auto argObj = GetFormatExprByIndex(index);
    auto arg = CreateAndLoadBasicInfo<FuncArg>(*argObj, INVALID_FORMAT_INDEX);
    // FuncArg has 2 condition values for 'withInout' and 'isDefaultVal'.
    auto info = argObj->info_as_FuncArgInfo();
    CJC_NULLPTR_CHECK(info);
    arg->withInout = info->withInout();
    if (info->isDefaultVal()) {
        arg->expr = MakeOwned<InvalidExpr>(INVALID_POSITION); // Create as a placeholder for default arg.
        arg->EnableAttr(Attribute::HAS_INITIAL);
    } else {
        CJC_ASSERT(argObj->operands()->size() == 1);
        arg->expr = LoadExpr(argObj->operands()->Get(0));
    }
    return arg;
}

OwnedPtr<Pattern> ASTLoader::ASTLoaderImpl::LoadPattern(const PackageFormat::Pattern& pattern)
{
    switch (pattern.kind()) {
        case PackageFormat::PatternKind_ConstPattern:
            return LoadConstPattern(pattern);
        case PackageFormat::PatternKind_WildcardPattern:
            return LoadWildcardPattern(pattern);
        case PackageFormat::PatternKind_VarPattern:
            return LoadVarPattern(pattern);
        case PackageFormat::PatternKind_TuplePattern:
            return LoadTuplePattern(pattern);
        case PackageFormat::PatternKind_TypePattern:
            return LoadTypePattern(pattern);
        case PackageFormat::PatternKind_EnumPattern:
            return LoadEnumPattern(pattern);
        case PackageFormat::PatternKind_ExceptTypePattern:
            return LoadExceptTypePattern(pattern);
        default:
            CJC_ABORT(); // Should be unreachable.
            return CreateAndLoadBasicInfo<InvalidPattern>(pattern, INVALID_FORMAT_INDEX);
    }
}

OwnedPtr<Pattern> ASTLoader::ASTLoaderImpl::LoadConstPattern(const PackageFormat::Pattern& pattern)
{
    auto cp = CreateAndLoadBasicInfo<ConstPattern>(pattern, INVALID_FORMAT_INDEX);
    CJC_ASSERT(pattern.exprs()->size() >= 1);
    uoffset_t idx = 0;
    cp->literal = LoadExpr(pattern.exprs()->Get(idx++));
    if (idx < pattern.exprs()->size()) {
        cp->operatorCallExpr = LoadExpr<CallExpr>(pattern.exprs()->Get(idx));
    }
    return cp;
}

OwnedPtr<Pattern> ASTLoader::ASTLoaderImpl::LoadWildcardPattern(const PackageFormat::Pattern& pattern)
{
    auto wp = CreateAndLoadBasicInfo<WildcardPattern>(pattern, INVALID_FORMAT_INDEX);
    return wp;
}

OwnedPtr<Pattern> ASTLoader::ASTLoaderImpl::LoadVarPattern(const PackageFormat::Pattern& pattern)
{
    auto vp = CreateAndLoadBasicInfo<VarPattern>(pattern, INVALID_FORMAT_INDEX);
    CJC_ASSERT(pattern.exprs()->size() == 1);
    vp->varDecl = LoadDecl<VarDecl>(pattern.exprs()->Get(0));
    CJC_NULLPTR_CHECK(vp->varDecl);
    vp->varDecl->parentPattern = vp.get();
    return vp;
}

OwnedPtr<Pattern> ASTLoader::ASTLoaderImpl::LoadTuplePattern(const PackageFormat::Pattern& pattern)
{
    auto tp = CreateAndLoadBasicInfo<TuplePattern>(pattern, INVALID_FORMAT_INDEX);
    CJC_NULLPTR_CHECK(pattern.patterns());
    for (uoffset_t i = 0; i < pattern.patterns()->size(); ++i) {
        tp->patterns.emplace_back(LoadPattern(*pattern.patterns()->Get(i)));
    }
    return tp;
}

OwnedPtr<Pattern> ASTLoader::ASTLoaderImpl::LoadTypePattern(const PackageFormat::Pattern& pattern)
{
    auto tp = CreateAndLoadBasicInfo<TypePattern>(pattern, INVALID_FORMAT_INDEX);
    CJC_ASSERT(pattern.patterns()->size() == 1);
    tp->pattern = LoadPattern(*pattern.patterns()->Get(0));
    tp->matchBeforeRuntime = pattern.matchBeforeRuntime();
    tp->needRuntimeTypeCheck = pattern.needRuntimeTypeCheck();
    return tp;
}

OwnedPtr<Pattern> ASTLoader::ASTLoaderImpl::LoadEnumPattern(const PackageFormat::Pattern& pattern)
{
    auto ep = CreateAndLoadBasicInfo<EnumPattern>(pattern, INVALID_FORMAT_INDEX);
    CJC_NULLPTR_CHECK(pattern.patterns());
    for (uoffset_t i = 0; i < pattern.patterns()->size(); ++i) {
        ep->patterns.emplace_back(LoadPattern(*pattern.patterns()->Get(i)));
    }
    CJC_NULLPTR_CHECK(pattern.exprs());
    CJC_ASSERT(pattern.exprs()->size() == 1);
    ep->constructor = LoadExpr(pattern.exprs()->Get(0));
    return ep;
}

OwnedPtr<Pattern> ASTLoader::ASTLoaderImpl::LoadExceptTypePattern(const PackageFormat::Pattern& pattern)
{
    auto etp = CreateAndLoadBasicInfo<ExceptTypePattern>(pattern, INVALID_FORMAT_INDEX);
    CJC_ASSERT(pattern.patterns()->size() == 1);
    etp->pattern = LoadPattern(*pattern.patterns()->Get(0));
    return etp;
}
