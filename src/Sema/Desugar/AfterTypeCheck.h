// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares private functions of desugar after type check.
 */
#ifndef CANGJIE_SEMA_DESUGAR_AFTER_TYPECHECK_H
#define CANGJIE_SEMA_DESUGAR_AFTER_TYPECHECK_H

#include "cangjie/AST/Node.h"
#include "cangjie/IncrementalCompilation/IncrementalScopeAnalysis.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie::Sema::Desugar::AfterTypeCheck {
using namespace Cangjie;
using namespace AST;

constexpr int8_t G_TOKEN_ARG_NUM = 2;
constexpr int8_t G_DIAG_REPORT_ARG_NUM = 4;

static const std::unordered_map<std::string, TokenKind> semaCoreIntrinsicMap = {
    {"Int64Less",               TokenKind::LT},
    {"Int64Greater",            TokenKind::GT},
    {"Int64LessOrEqual",        TokenKind::LE},
    {"Int64GreaterOrEqual",     TokenKind::GE},
    {"Int64Equal",              TokenKind::EQUAL},
    {"Int64NotEqual",           TokenKind::NOTEQ},
    {"Int32Less",               TokenKind::LT},
    {"Int32Greater",            TokenKind::GT},
    {"Int32LessOrEqual",        TokenKind::LE},
    {"Int32GreaterOrEqual",     TokenKind::GE},
    {"Int32Equal",              TokenKind::EQUAL},
    {"Int32NotEqual",           TokenKind::NOTEQ},
    {"Int16Less",               TokenKind::LT},
    {"Int16Greater",            TokenKind::GT},
    {"Int16LessOrEqual",        TokenKind::LE},
    {"Int16GreaterOrEqual",     TokenKind::GE},
    {"Int16Equal",              TokenKind::EQUAL},
    {"Int16NotEqual",           TokenKind::NOTEQ},
    {"Int8Less",                TokenKind::LT},
    {"Int8Greater",             TokenKind::GT},
    {"Int8LessOrEqual",         TokenKind::LE},
    {"Int8GreaterOrEqual",      TokenKind::GE},
    {"Int8Equal",               TokenKind::EQUAL},
    {"Int8NotEqual",            TokenKind::NOTEQ},
    {"UInt64Less",              TokenKind::LT},
    {"UInt64Greater",           TokenKind::GT},
    {"UInt64LessOrEqual",       TokenKind::LE},
    {"UInt64GreaterOrEqual",    TokenKind::GE},
    {"UInt64Equal",             TokenKind::EQUAL},
    {"UInt64NotEqual",          TokenKind::NOTEQ},
    {"UInt32Less",              TokenKind::LT},
    {"UInt32Greater",           TokenKind::GT},
    {"UInt32LessOrEqual",       TokenKind::LE},
    {"UInt32GreaterOrEqual",    TokenKind::GE},
    {"UInt32Equal",             TokenKind::EQUAL},
    {"UInt32NotEqual",          TokenKind::NOTEQ},
    {"UInt16Less",              TokenKind::LT},
    {"UInt16Greater",           TokenKind::GT},
    {"UInt16LessOrEqual",       TokenKind::LE},
    {"UInt16GreaterOrEqual",    TokenKind::GE},
    {"UInt16Equal",             TokenKind::EQUAL},
    {"UInt16NotEqual",          TokenKind::NOTEQ},
    {"UInt8Less",               TokenKind::LT},
    {"UInt8Greater",            TokenKind::GT},
    {"UInt8LessOrEqual",        TokenKind::LE},
    {"UInt8GreaterOrEqual",     TokenKind::GE},
    {"UInt8Equal",              TokenKind::EQUAL},
    {"UInt8NotEqual",           TokenKind::NOTEQ},
    {"Float16Less",             TokenKind::LT},
    {"Float16Greater",          TokenKind::GT},
    {"Float16LessOrEqual",      TokenKind::LE},
    {"Float16GreaterOrEqual",   TokenKind::GE},
    {"Float16Equal",            TokenKind::EQUAL},
    {"Float16NotEqual",         TokenKind::NOTEQ},
    {"Float32Less",             TokenKind::LT},
    {"Float32Greater",          TokenKind::GT},
    {"Float32LessOrEqual",      TokenKind::LE},
    {"Float32GreaterOrEqual",   TokenKind::GE},
    {"Float32Equal",            TokenKind::EQUAL},
    {"Float32NotEqual",         TokenKind::NOTEQ},
    {"Float64Less",             TokenKind::LT},
    {"Float64Greater",          TokenKind::GT},
    {"Float64LessOrEqual",      TokenKind::LE},
    {"Float64GreaterOrEqual",   TokenKind::GE},
    {"Float64Equal",            TokenKind::EQUAL},
    {"Float64NotEqual",         TokenKind::NOTEQ},
};

const static std::unordered_map<std::string, const std::unordered_map<std::string, TokenKind>> semaPackageMap = {
    {CORE_PACKAGE_NAME, semaCoreIntrinsicMap},
};

OwnedPtr<TypePattern> CreateRuntimePreparedTypePattern(
    TypeManager& typeManager, OwnedPtr<Pattern> pattern, OwnedPtr<Type>  type, Expr& selector);

Ptr<Decl> LookupEnumMember(Ptr<Decl> decl, const std::string& identifier);
void UnitifyBlock(const Expr& posSrc, Block& b, ModalTy unitTy);
void RearrangeRefLoop(const Expr& src, Expr& dst, Ptr<Node> loopBody);

void PostProcessFuncParam(const FuncParam& fp, const GlobalOptions& options);
void DesugarDeclsForPackage(Package& pkg, bool enableCoverage);
void DesugarBinaryExpr(BinaryExpr& be);
void DesugarIsExpr(TypeManager& typeManager, IsExpr& ie);
void DesugarAsExpr(TypeManager& typeManager, AsExpr& ae);
/// Insert Unit if needed. No desugaring requried for if-let expressions.
void DesugarIfExpr(TypeManager& typeManager, IfExpr& ifExpr);
void DesugarRangeExpr(TypeManager& typeManager, RangeExpr& re);
void DesugarIntrinsicCallExpr(AST::CallExpr& expr);

/**
 * Collect semantic usages for incremental complations. Performed before instantiation step.
 */
SemanticInfo GetSemanticUsage(TypeManager& typeManager, const std::vector<Ptr<Package>>& pkgs);
} // namespace Cangjie::Sema::Desugar::AfterTypeCheck

#endif
