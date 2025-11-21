// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 * This file implements the AST Writer related classes.
 */

#include "ASTWriterImpl.h"

#include "flatbuffers/ModuleFormat_generated.h"

#include "cangjie/AST/ASTCasting.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Utils/CheckUtils.h"

using namespace Cangjie;
using namespace AST;

void ASTWriter::ASTWriterImpl::SaveBasicNodeInfo(PackageFormat::ExprBuilder& dbuilder, const NodeInfo& info)
{
    dbuilder.add_begin(&info.begin);
    dbuilder.add_end(&info.end);
    dbuilder.add_type(info.ty);
    FormattedIndex mapIndx = info.mapExpr ? GetExprIndex(*info.mapExpr) : INVALID_FORMAT_INDEX;
    dbuilder.add_mapExpr(mapIndx);
    dbuilder.add_overflowPolicy(STRATEGY_MAP.at(info.ov));
}

NodeInfo ASTWriter::ASTWriterImpl::PackNodeInfo(const Node& node)
{
    auto begin = node.GetBegin();
    auto end = node.GetEnd();
    auto [pkgIndex, fileIndex] = GetFileIndex(begin.fileID);
    TPosition posBegin(fileIndex, pkgIndex, begin.line, begin.column, begin.GetStatus() == PositionStatus::IGNORE);
    TPosition posEnd(fileIndex, pkgIndex, end.line, end.column, end.GetStatus() == PositionStatus::IGNORE);
    auto ty = SaveType(node.ty);

    return {posBegin, posEnd, ty};
}

// Only get desugared expression. 'ParenExpr' is just a wrapper which can be ignore.
Ptr<const Expr> ASTWriter::ASTWriterImpl::GetRealExpr(const Expr& expr)
{
    auto realExpr = &expr;
    while (realExpr->desugarExpr != nullptr) {
        realExpr = realExpr->desugarExpr.get();
    }
    if (auto pe = DynamicCast<const ParenExpr*>(realExpr)) {
        CJC_NULLPTR_CHECK(pe->expr);
        realExpr = GetRealExpr(*pe->expr);
    }
    return realExpr;
}

FormattedIndex ASTWriter::ASTWriterImpl::SaveExpr(const Expr& expr)
{
    auto realExpr = GetRealExpr(expr);
    auto found = savedExprMap.find(realExpr);
    if (found != savedExprMap.end()) {
        return found->second;
    }
    FormattedIndex exprIndex = static_cast<FormattedIndex>(allExprs.size()) + 1;
    allExprs.emplace_back(TExprOffset());
    (void)savedExprMap.emplace(realExpr, exprIndex);

    auto exprInfo = PackNodeInfo(*realExpr);
    exprInfo.ov = expr.overflowStrategy;
    // 'JumpExpr' will not have mapExpr, so re-use this field for 'refLoop'.
    exprInfo.mapExpr =
        realExpr->astKind == ASTKind::JUMP_EXPR ? StaticCast<const JumpExpr*>(realExpr)->refLoop : realExpr->mapExpr;
    TExprOffset offset;
    auto foundWriter = exprWriterMap.find(realExpr->astKind);
    if (foundWriter != exprWriterMap.end()) {
        offset = foundWriter->second(*realExpr, exprInfo);
    } else {
        offset = SaveUnsupportExpr(*realExpr, exprInfo);
    }
    // Overwrite the slot with the actual expr.
    allExprs[static_cast<unsigned long>(exprIndex - 1)] = offset;
    return exprIndex;
}

TExprOffset ASTWriter::ASTWriterImpl::SaveUnsupportExpr(const Expr& expr, const NodeInfo& info)
{
    (void)diag.DiagnoseRefactor(
        DiagKindRefactor::package_unsupport_save, expr, "expression", std::string(typeid(expr).name()));

    PackageFormat::ExprBuilder dbuilder(builder);
    SaveBasicNodeInfo(dbuilder, info);
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const PrimitiveTypeExpr& /* pte */, const NodeInfo& info)
{
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_PrimitiveTypeExpr);
    SaveBasicNodeInfo(dbuilder, info);
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const WildcardExpr& /* we */, const NodeInfo& info)
{
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_WildcardExpr);
    SaveBasicNodeInfo(dbuilder, info);
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const ReturnExpr& re, const NodeInfo& info)
{
    CJC_NULLPTR_CHECK(re.expr);
    auto subExprIdx = SaveExpr(*re.expr);
    auto subExprs = builder.CreateVector<FormattedIndex>({subExprIdx});
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_ReturnExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(subExprs);
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const JumpExpr& je, const NodeInfo& info)
{
    auto eInfo = PackageFormat::CreateJumpInfo(builder, je.isBreak);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_JumpExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_info_type(PackageFormat::ExprInfo_JumpInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const MemberAccess& ma, const NodeInfo& info)
{
    CJC_NULLPTR_CHECK(ma.baseExpr);
    auto baseIdx = SaveExpr(*ma.baseExpr);
    auto operands = builder.CreateVector<FormattedIndex>({baseIdx});
    auto fieldIdx = builder.CreateString(ma.field.Val());
    auto target = GetFullDeclIndex(ma.target);
    std::vector<FormattedIndex> types(ma.instTys.size());
    for (size_t i = 0; i < ma.instTys.size(); ++i) {
        types[i] = SaveType(ma.instTys[i]);
    }
    auto tyIdx = builder.CreateVector<FormattedIndex>(types);
    auto parentTy = SaveType(ma.matchedParentTy);
    auto eInfo = PackageFormat::CreateReferenceInfo(builder, fieldIdx, target, tyIdx, parentTy);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_MemberAccess);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operands);
    dbuilder.add_info_type(PackageFormat::ExprInfo_ReferenceInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const RefExpr& re, const NodeInfo& info)
{
    auto name = builder.CreateString(re.ref.identifier.Val());
    auto refTarget = re.ref.target && re.ref.target->genericDecl ? re.ref.target->genericDecl : re.ref.target;
    auto target = GetFullDeclIndex(refTarget);
    std::vector<FormattedIndex> types(re.instTys.size());
    for (size_t i = 0; i < re.instTys.size(); ++i) {
        types[i] = SaveType(re.instTys[i]);
    }
    auto tyIdx = builder.CreateVector<FormattedIndex>(types);
    auto parentTy = SaveType(re.matchedParentTy);
    auto eInfo = PackageFormat::CreateReferenceInfo(builder, name, target, tyIdx, parentTy);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_RefExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_info_type(PackageFormat::ExprInfo_ReferenceInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const CallExpr& ce, const NodeInfo& info)
{
    CJC_NULLPTR_CHECK(ce.baseFunc);
    std::vector<FormattedIndex> operands;
    operands.emplace_back(SaveExpr(*ce.baseFunc));
    auto saveArgs = [this, &operands](auto& args) {
        for (auto& arg : args) {
            operands.emplace_back(SaveFuncArg(*arg));
        }
    };
    ce.desugarArgs.has_value() ? saveArgs(ce.desugarArgs.value()) : saveArgs(ce.args);
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    // For now, only 'SIDE_EFFECT' attribute is needed for exporting.
    auto eInfo =
        PackageFormat::CreateCallInfo(builder, ce.TestAttr(Attribute::SIDE_EFFECT), CALL_KIND_MAP.at(ce.callKind));
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_CallExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    dbuilder.add_info_type(PackageFormat::ExprInfo_CallInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const UnaryExpr& ue, const NodeInfo& info)
{
    CJC_NULLPTR_CHECK(ue.expr);
    auto baseIdx = SaveExpr(*ue.expr);
    auto operands = builder.CreateVector<FormattedIndex>({baseIdx});
    auto eInfo = PackageFormat::CreateUnaryInfo(builder, OP_KIND_MAP.at(ue.op));
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_UnaryExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operands);
    dbuilder.add_info_type(PackageFormat::ExprInfo_UnaryInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const IncOrDecExpr& ide, const NodeInfo& info)
{
    CJC_NULLPTR_CHECK(ide.expr);
    auto baseIdx = SaveExpr(*ide.expr);
    auto operands = builder.CreateVector<FormattedIndex>({baseIdx});
    auto eInfo = PackageFormat::CreateIncOrDecInfo(builder, OP_KIND_MAP.at(ide.op));
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_IncOrDecExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operands);
    dbuilder.add_info_type(PackageFormat::ExprInfo_IncOrDecInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const LitConstExpr& lce, const NodeInfo& info)
{
    auto stringIdx = builder.CreateString(lce.stringValue);
    auto eInfo = PackageFormat::CreateLitConstInfo(
        builder, stringIdx, LIT_CONST_KIND_MAP.at(lce.kind), STRING_KIND_MAP.at(lce.stringKind));
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_LitConstExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_info_type(PackageFormat::ExprInfo_LitConstInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const BinaryExpr& be, const NodeInfo& info)
{
    CJC_NULLPTR_CHECK(be.leftExpr);
    CJC_NULLPTR_CHECK(be.rightExpr);
    std::vector<FormattedIndex> operands;
    operands.emplace_back(SaveExpr(*be.leftExpr));
    operands.emplace_back(SaveExpr(*be.rightExpr));
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    auto eInfo = PackageFormat::CreateBinaryInfo(builder, OP_KIND_MAP.at(be.op));
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_BinaryExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    dbuilder.add_info_type(PackageFormat::ExprInfo_BinaryInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const SubscriptExpr& se, const NodeInfo& info)
{
    CJC_NULLPTR_CHECK(se.baseExpr);
    std::vector<FormattedIndex> operands;
    operands.emplace_back(SaveExpr(*se.baseExpr));
    for (auto& idxExpr : se.indexExprs) {
        CJC_NULLPTR_CHECK(idxExpr);
        operands.emplace_back(SaveExpr(*idxExpr));
    }
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    auto eInfo = PackageFormat::CreateSubscriptInfo(builder, se.isTupleAccess);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_SubscriptExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    dbuilder.add_info_type(PackageFormat::ExprInfo_SubscriptInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const AssignExpr& ae, const NodeInfo& info)
{
    CJC_NULLPTR_CHECK(ae.leftValue);
    CJC_NULLPTR_CHECK(ae.rightExpr);
    std::vector<FormattedIndex> operands;
    operands.emplace_back(SaveExpr(*ae.leftValue));
    operands.emplace_back(SaveExpr(*ae.rightExpr));
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    auto eInfo = PackageFormat::CreateAssignInfo(builder, ae.isCompound, OP_KIND_MAP.at(ae.op));
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_AssignExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    dbuilder.add_info_type(PackageFormat::ExprInfo_AssignInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const ArrayExpr& ae, const NodeInfo& info)
{
    std::vector<FormattedIndex> operands;
    for (auto& arg : ae.args) {
        operands.emplace_back(SaveFuncArg(*arg));
    }
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    auto initFunc = GetFullDeclIndex(ae.initFunc);
    auto eInfo = PackageFormat::CreateArrayInfo(builder, initFunc, ae.isValueArray);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_ArrayExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    dbuilder.add_info_type(PackageFormat::ExprInfo_ArrayInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const PointerExpr& pe, const NodeInfo& info)
{
    std::vector<FormattedIndex> operands;
    if (pe.arg) {
        operands.emplace_back(SaveFuncArg(*pe.arg));
    }
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_PointerExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const TypeConvExpr& tce, const NodeInfo& info)
{
    CJC_NULLPTR_CHECK(tce.expr);
    std::vector<FormattedIndex> operands;
    operands.emplace_back(SaveExpr(*tce.expr));
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_TypeConvExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const ThrowExpr& te, const NodeInfo& info)
{
    CJC_NULLPTR_CHECK(te.expr);
    std::vector<FormattedIndex> operands;
    operands.emplace_back(SaveExpr(*te.expr));
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_ThrowExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const SpawnExpr& se, const NodeInfo& info)
{
    // Sub node 'task' does not used for SpawnExpr with futureObj.
    CJC_NULLPTR_CHECK(se.futureObj);
    std::vector<FormattedIndex> operands;
    if (se.arg) { // Optional argument.
        operands.emplace_back(SaveExpr(*se.arg));
    }
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    // 'futureObj' is a local decl which should be serialized first.
    auto targetIdx = SaveDecl(*se.futureObj);
    auto future = PackageFormat::CreateFullId(builder, CURRENT_PKG_INDEX, 0, targetIdx);
    auto eInfo = PackageFormat::CreateSpawnInfo(builder, future);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_SpawnExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    dbuilder.add_info_type(PackageFormat::ExprInfo_SpawnInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const ArrayLit& al, const NodeInfo& info)
{
    std::vector<FormattedIndex> operands(al.children.size());
    for (size_t i = 0; i < al.children.size(); ++i) {
        operands[i] = SaveExpr(*al.children[i]);
    }
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    auto initFunc = GetFullDeclIndex(al.initFunc);
    auto eInfo = PackageFormat::CreateArrayInfo(builder, initFunc);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_ArrayLit);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    dbuilder.add_info_type(PackageFormat::ExprInfo_ArrayInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const TupleLit& tl, const NodeInfo& info)
{
    std::vector<FormattedIndex> operands(tl.children.size());
    for (size_t i = 0; i < tl.children.size(); ++i) {
        operands[i] = SaveExpr(*tl.children[i]);
    }
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_TupleLit);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const IfExpr& ie, const NodeInfo& info)
{
    CJC_NULLPTR_CHECK(ie.condExpr);
    CJC_NULLPTR_CHECK(ie.thenBody);
    std::vector<FormattedIndex> operands;
    operands.emplace_back(SaveExpr(*ie.condExpr));
    operands.emplace_back(SaveExpr(*ie.thenBody));
    FormattedIndex elseIdx = ie.elseBody ? SaveExpr(*ie.elseBody) : INVALID_FORMAT_INDEX;
    operands.emplace_back(elseIdx);
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_IfExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const TryExpr& te, const NodeInfo& info)
{
    std::vector<TFullIdOffset> resources;
    for (auto& it : te.resourceSpec) {
        // Local variables should be saved directly.
        auto index = SaveDecl(*it);
        auto fullId = PackageFormat::CreateFullId(builder, CURRENT_PKG_INDEX, 0, index);
        resources.emplace_back(fullId);
    }
    auto targetsIdx = builder.CreateVector<TFullIdOffset>({resources});
    std::vector<FormattedIndex> operands;
    operands.emplace_back(SaveExpr(*te.tryBlock));
    for (auto& catchBlock : te.catchBlocks) {
        operands.emplace_back(SaveExpr(*catchBlock));
    }
    // Always save finally index to make size of operands retrievable.
    operands.emplace_back(te.finallyBlock ? SaveExpr(*te.finallyBlock) : INVALID_FORMAT_INDEX);
    std::vector<TPatternOffset> patterns;
    for (auto& pattern : te.catchPatterns) {
        patterns.emplace_back(SavePattern(*pattern));
    }
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    auto patternsIdx = builder.CreateVector<TPatternOffset>(patterns);
    auto eInfo = PackageFormat::CreateTryInfo(builder, targetsIdx, patternsIdx);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_TryExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    dbuilder.add_info_type(PackageFormat::ExprInfo_TryInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const WhileExpr& we, const NodeInfo& info)
{
    CJC_NULLPTR_CHECK(we.condExpr);
    CJC_NULLPTR_CHECK(we.body);
    std::vector<FormattedIndex> operands;
    operands.emplace_back(SaveExpr(*we.condExpr));
    operands.emplace_back(SaveExpr(*we.body));
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_WhileExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const DoWhileExpr& dwe, const NodeInfo& info)
{
    CJC_NULLPTR_CHECK(dwe.body);
    CJC_NULLPTR_CHECK(dwe.condExpr);
    std::vector<FormattedIndex> operands;
    operands.emplace_back(SaveExpr(*dwe.body));
    operands.emplace_back(SaveExpr(*dwe.condExpr));
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_DoWhileExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const LambdaExpr& le, const NodeInfo& info)
{
    CJC_NULLPTR_CHECK(le.funcBody);
    auto funcBody = SaveFuncBody(*le.funcBody);
    auto eInfo = PackageFormat::CreateLambdaInfo(builder, funcBody, le.TestAttr(Attribute::MOCK_SUPPORTED));
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_LambdaExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_info_type(PackageFormat::ExprInfo_LambdaInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const Block& block, const NodeInfo& info)
{
    auto size = block.body.size();
    std::vector<FormattedIndex> operands(size);
    std::vector<bool> childrenCond(size + 1);
    for (uoffset_t i = 0; i < size; ++i) {
        auto node = block.body[i].get();
        if (auto expr = DynamicCast<Expr*>(node)) {
            operands[i] = SaveExpr(*expr);
            childrenCond[i] = true;
        } else if (auto decl = DynamicCast<Decl*>(node)) {
            operands[i] = SaveDecl(*decl);
            childrenCond[i] = false;
        } else {
            CJC_ABORT();
        }
    }
    // Save the 'unsafe' condition as the last value.
    childrenCond[size] = block.TestAttr(Attribute::UNSAFE);
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    auto condIdx = builder.CreateVector(childrenCond);
    auto eInfo = PackageFormat::CreateBlockInfo(builder, condIdx);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_Block);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    dbuilder.add_info_type(PackageFormat::ExprInfo_BlockInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const MatchExpr& me, const NodeInfo& info)
{
    std::vector<FormattedIndex> operands;
    if (me.matchMode) {
        CJC_NULLPTR_CHECK(me.selector);
        operands.emplace_back(SaveExpr(*me.selector));
        for (auto& matchCase : me.matchCases) {
            CJC_NULLPTR_CHECK(matchCase);
            operands.emplace_back(SaveMatchCase(*matchCase));
        }
    } else {
        for (auto& matchCase : me.matchCaseOthers) {
            CJC_NULLPTR_CHECK(matchCase);
            operands.emplace_back(SaveMatchCaseOther(*matchCase));
        }
    }
    // Do not export for box desugars, it will be re-boxed in imported package.s
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    auto eInfo = PackageFormat::CreateMatchInfo(builder, me.matchMode);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_MatchExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    dbuilder.add_info_type(PackageFormat::ExprInfo_MatchInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const AST::LetPatternDestructor& e, const NodeInfo& info)
{
    std::vector<TPatternOffset> ps{};
    for (auto& p : e.patterns) {
        ps.push_back(SavePattern(*p));
    }
    auto init = SaveExpr(*e.initializer);
    auto patternsIdx = builder.CreateVector(ps);
    auto operandsIdx = builder.CreateVector({init});
    auto eInfo = PackageFormat::CreateLetPatternDestructorInfo(builder, patternsIdx);
    PackageFormat::ExprBuilder dbuilder{builder};
    dbuilder.add_kind(PackageFormat::ExprKind_LetPatternDestructor);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    dbuilder.add_info_type(PackageFormat::ExprInfo_LetPatternDestructorInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

TExprOffset ASTWriter::ASTWriterImpl::SaveExpression(const ForInExpr& fie, const NodeInfo& info)
{
    std::vector<FormattedIndex> operands;
    CJC_NULLPTR_CHECK(fie.pattern);
    TPatternOffset pIdx = SavePattern(*fie.pattern);
    CJC_NULLPTR_CHECK(fie.inExpression);
    CJC_NULLPTR_CHECK(fie.body);
    operands.emplace_back(SaveExpr(*fie.inExpression));
    FormattedIndex guradIdx = fie.patternGuard ? SaveExpr(*fie.patternGuard) : INVALID_FORMAT_INDEX;
    operands.emplace_back(guradIdx);
    operands.emplace_back(SaveExpr(*fie.body));
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    auto eInfo = PackageFormat::CreateForInInfo(builder, pIdx, FOR_IN_KIND_MAP.at(fie.forInKind));
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_ForInExpr);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    dbuilder.add_info_type(PackageFormat::ExprInfo_ForInInfo);
    dbuilder.add_info(eInfo.Union());
    return dbuilder.Finish();
}

FormattedIndex ASTWriter::ASTWriterImpl::SaveMatchCase(const MatchCase& mc)
{
    FormattedIndex index = static_cast<FormattedIndex>(allExprs.size()) + 1;
    allExprs.emplace_back(TExprOffset());
    auto info = PackNodeInfo(mc);
    std::vector<FormattedIndex> operands;
    FormattedIndex guradIdx = mc.patternGuard ? SaveExpr(*mc.patternGuard) : INVALID_FORMAT_INDEX;
    operands.emplace_back(guradIdx);
    std::vector<TPatternOffset> patterns(mc.patterns.size());
    for (size_t i = 0; i < mc.patterns.size(); ++i) {
        patterns[i] = SavePattern(*mc.patterns[i]);
    }
    CJC_NULLPTR_CHECK(mc.exprOrDecls);
    operands.emplace_back(SaveExpr(*mc.exprOrDecls));
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    auto patternsIdx = builder.CreateVector<TPatternOffset>(patterns);
    auto eInfo = PackageFormat::CreateMatchCaseInfo(builder, patternsIdx);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_MatchCase);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    dbuilder.add_info_type(PackageFormat::ExprInfo_MatchCaseInfo);
    dbuilder.add_info(eInfo.Union());
    allExprs[static_cast<unsigned long>(index - 1)] = dbuilder.Finish();
    return index;
}

FormattedIndex ASTWriter::ASTWriterImpl::SaveMatchCaseOther(const MatchCaseOther& mco)
{
    FormattedIndex index = static_cast<FormattedIndex>(allExprs.size()) + 1;
    allExprs.emplace_back(TExprOffset());
    auto info = PackNodeInfo(mco);
    CJC_NULLPTR_CHECK(mco.matchExpr);
    CJC_NULLPTR_CHECK(mco.exprOrDecls);
    std::vector<FormattedIndex> operands;
    operands.emplace_back(SaveExpr(*mco.matchExpr));
    operands.emplace_back(SaveExpr(*mco.exprOrDecls));
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    PackageFormat::ExprBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::ExprKind_MatchCaseOther);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    allExprs[static_cast<unsigned long>(index - 1)] = dbuilder.Finish();
    return index;
}

// FuncArg wrapper, this is directly used when saving 'CallExpr'.
FormattedIndex ASTWriter::ASTWriterImpl::SaveFuncArg(const FuncArg& arg)
{
    FormattedIndex index = static_cast<FormattedIndex>(allExprs.size()) + 1;
    allExprs.emplace_back(TExprOffset());
    auto info = PackNodeInfo(arg);
    // If the function arg is default parameter's placeholder, do not export it's expression.
    bool isDefaultVal = arg.TestAttr(Attribute::HAS_INITIAL);
    std::vector<FormattedIndex> operands;
    if (!isDefaultVal) {
        operands.emplace_back(SaveExpr(*arg.expr));
    }
    auto operandsIdx = builder.CreateVector<FormattedIndex>(operands);
    auto eInfo = PackageFormat::CreateFuncArgInfo(builder, arg.withInout, isDefaultVal);
    PackageFormat::ExprBuilder dbuilder(builder);
    SaveBasicNodeInfo(dbuilder, info);
    dbuilder.add_operands(operandsIdx);
    dbuilder.add_info_type(PackageFormat::ExprInfo_FuncArgInfo);
    dbuilder.add_info(eInfo.Union());
    allExprs[static_cast<unsigned long>(index - 1)] = dbuilder.Finish();
    return index;
}

TPatternOffset ASTWriter::ASTWriterImpl::SavePattern(const Pattern& pattern)
{
    switch (pattern.astKind) {
        case ASTKind::CONST_PATTERN:
            return SaveConstPattern(StaticCast<const ConstPattern&>(pattern));
        case ASTKind::WILDCARD_PATTERN:
            return SaveWildcardPattern(StaticCast<const WildcardPattern&>(pattern));
        case ASTKind::VAR_PATTERN:
            return SaveVarPattern(StaticCast<const VarPattern&>(pattern));
        case ASTKind::TUPLE_PATTERN:
            return SaveTuplePattern(StaticCast<const TuplePattern&>(pattern));
        case ASTKind::TYPE_PATTERN:
            return SaveTypePattern(StaticCast<const TypePattern&>(pattern));
        case ASTKind::ENUM_PATTERN:
            return SaveEnumPattern(StaticCast<const EnumPattern&>(pattern));
        case ASTKind::EXCEPT_TYPE_PATTERN:
            return SaveExceptTypePattern(StaticCast<const ExceptTypePattern&>(pattern));
        case ASTKind::VAR_OR_ENUM_PATTERN: {
            auto& vep = StaticCast<const VarOrEnumPattern&>(pattern);
            CJC_NULLPTR_CHECK(vep.pattern);
            return SavePattern(*vep.pattern);
        }
        default:
            // Should be unreachable.
            CJC_ABORT();
            return TPatternOffset();
    }
}

TPatternOffset ASTWriter::ASTWriterImpl::SaveConstPattern(const ConstPattern& cp)
{
    CJC_NULLPTR_CHECK(cp.literal);
    std::vector<FormattedIndex> exprs;
    exprs.emplace_back(SaveExpr(*cp.literal));
    if (cp.operatorCallExpr) {
        exprs.emplace_back(SaveExpr(*cp.operatorCallExpr));
    }
    auto info = PackNodeInfo(cp);
    auto tyIdx = builder.CreateVector<FormattedIndex>({info.ty});
    auto exprsIdx = builder.CreateVector<FormattedIndex>(exprs);
    PackageFormat::PatternBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::PatternKind_ConstPattern);
    dbuilder.add_begin(&info.begin);
    dbuilder.add_end(&info.end);
    dbuilder.add_exprs(exprsIdx);
    dbuilder.add_types(tyIdx);
    return dbuilder.Finish();
}

TPatternOffset ASTWriter::ASTWriterImpl::SaveWildcardPattern(const WildcardPattern& wp)
{
    auto info = PackNodeInfo(wp);
    auto tyIdx = builder.CreateVector<FormattedIndex>({info.ty});
    PackageFormat::PatternBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::PatternKind_WildcardPattern);
    dbuilder.add_begin(&info.begin);
    dbuilder.add_end(&info.end);
    dbuilder.add_types(tyIdx);
    return dbuilder.Finish();
}

TPatternOffset ASTWriter::ASTWriterImpl::SaveVarPattern(const VarPattern& vp)
{
    CJC_NULLPTR_CHECK(vp.varDecl);
    auto declIdx = SaveDecl(*vp.varDecl);
    auto info = PackNodeInfo(vp);
    auto tyIdx = builder.CreateVector<FormattedIndex>({info.ty});
    auto exprsIdx = builder.CreateVector<FormattedIndex>({declIdx});
    PackageFormat::PatternBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::PatternKind_VarPattern);
    dbuilder.add_begin(&info.begin);
    dbuilder.add_end(&info.end);
    dbuilder.add_types(tyIdx);
    dbuilder.add_exprs(exprsIdx);
    return dbuilder.Finish();
}

TPatternOffset ASTWriter::ASTWriterImpl::SaveTuplePattern(const TuplePattern& tp)
{
    std::vector<TPatternOffset> patterns(tp.patterns.size());
    for (size_t i = 0; i < tp.patterns.size(); ++i) {
        patterns[i] = SavePattern(*tp.patterns[i]);
    }
    auto info = PackNodeInfo(tp);
    auto tyIdx = builder.CreateVector<FormattedIndex>({info.ty});
    auto patternsIdx = builder.CreateVector<TPatternOffset>(patterns);
    PackageFormat::PatternBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::PatternKind_TuplePattern);
    dbuilder.add_begin(&info.begin);
    dbuilder.add_end(&info.end);
    dbuilder.add_types(tyIdx);
    dbuilder.add_patterns(patternsIdx);
    return dbuilder.Finish();
}

TPatternOffset ASTWriter::ASTWriterImpl::SaveTypePattern(const TypePattern& tp)
{
    CJC_NULLPTR_CHECK(tp.pattern);
    auto pIdx = SavePattern(*tp.pattern);
    auto info = PackNodeInfo(tp);
    auto tyIdx = builder.CreateVector<FormattedIndex>({info.ty});
    auto patternsIdx = builder.CreateVector<TPatternOffset>({pIdx});
    PackageFormat::PatternBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::PatternKind_TypePattern);
    dbuilder.add_begin(&info.begin);
    dbuilder.add_end(&info.end);
    dbuilder.add_types(tyIdx);
    dbuilder.add_patterns(patternsIdx);
    dbuilder.add_matchBeforeRuntime(tp.matchBeforeRuntime);
    dbuilder.add_needRuntimeTypeCheck(tp.needRuntimeTypeCheck);
    return dbuilder.Finish();
}

TPatternOffset ASTWriter::ASTWriterImpl::SaveEnumPattern(const EnumPattern& ep)
{
    CJC_NULLPTR_CHECK(ep.constructor);
    auto ctor = SaveExpr(*ep.constructor);
    std::vector<TPatternOffset> patterns(ep.patterns.size());
    for (size_t i = 0; i < ep.patterns.size(); ++i) {
        patterns[i] = SavePattern(*ep.patterns[i]);
    }
    auto info = PackNodeInfo(ep);
    auto tyIdx = builder.CreateVector<FormattedIndex>({info.ty});
    auto exprsIdx = builder.CreateVector<FormattedIndex>({ctor});
    auto patternsIdx = builder.CreateVector<TPatternOffset>(patterns);
    PackageFormat::PatternBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::PatternKind_EnumPattern);
    dbuilder.add_begin(&info.begin);
    dbuilder.add_end(&info.end);
    dbuilder.add_types(tyIdx);
    dbuilder.add_exprs(exprsIdx);
    dbuilder.add_patterns(patternsIdx);
    return dbuilder.Finish();
}

TPatternOffset ASTWriter::ASTWriterImpl::SaveExceptTypePattern(const ExceptTypePattern& etp)
{
    auto pIdx = SavePattern(*etp.pattern);
    auto info = PackNodeInfo(etp);
    std::vector<FormattedIndex> types{info.ty};
    for (auto& type : etp.types) {
        CJC_NULLPTR_CHECK(type);
        types.emplace_back(SaveType(type->ty));
    }
    auto tyIdx = builder.CreateVector<FormattedIndex>(types);
    auto patternsIdx = builder.CreateVector<TPatternOffset>({pIdx});
    PackageFormat::PatternBuilder dbuilder(builder);
    dbuilder.add_kind(PackageFormat::PatternKind_ExceptTypePattern);
    dbuilder.add_begin(&info.begin);
    dbuilder.add_end(&info.end);
    dbuilder.add_types(tyIdx);
    dbuilder.add_patterns(patternsIdx);
    return dbuilder.Finish();
}
