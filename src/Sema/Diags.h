// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares diagnostic related functions for Sema.
 */

#ifndef CANGJIE_SEMA_DIAGS_H
#define CANGJIE_SEMA_DIAGS_H

#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Modules/ImportManager.h"

#include <string>

namespace Cangjie::Sema {
using namespace AST;
Range MakeRangeForDeclIdentifier(const AST::Decl& decl);
void DiagRedefinitionWithFoundNode(DiagnosticEngine& diag, const Decl& current, const Decl& previous);
void DiagOverloadConflict(DiagnosticEngine& diag, const std::vector<Ptr<FuncDecl>>& sameSigFuncs);
void DiagMismatchedTypesWithFoundTy(DiagnosticEngine& diag, const Node& node, const std::string& expected,
    const std::string& found, const std::string& note = "");
void DiagMismatchedTypesWithFoundTy(
    DiagnosticEngine& diag, const Node& node, const Ty& expected, const Ty& found, const std::string& note = "");
void DiagMismatchedTypes(DiagnosticEngine& diag, const Node& node, const Ty& type, const std::string& note = "");
void DiagMismatchedTypes(DiagnosticEngine& diag, const Node& node, const Node& type, const std::string& because = "");
void DiagInvalidMultipleAssignExpr(
    DiagnosticEngine& diag, const Node& leftNode, const Expr& rightExpr, const std::string& because = "");
void DiagInvalidBinaryExpr(DiagnosticEngine& diag, const BinaryExpr& be);
void DiagInvalidUnaryExpr(DiagnosticEngine& diag, const UnaryExpr& ue);
void DiagInvalidUnaryExprWithTarget(DiagnosticEngine& diag, const UnaryExpr& ue, Ty& target);
void DiagInvalidSubscriptExpr(
    DiagnosticEngine& diag, const SubscriptExpr& se, const Ty& baseTy, const std::vector<Ptr<Ty>>& indexTys);
void DiagUnableToInferExpr(DiagnosticEngine& diag, const Expr& expr);
void DiagUnableToInferReturnType(DiagnosticEngine& diag, const FuncDecl& fd);
void DiagUnableToInferReturnType(DiagnosticEngine& diag, const FuncBody& fb);
void DiagUnableToInferReturnType(DiagnosticEngine& diag, const FuncDecl& fd, const Expr& expr);
void DiagWrongNumberOfArguments(DiagnosticEngine& diag, const CallExpr& ce, const std::vector<Ptr<Ty>>& paramTys);
void DiagWrongNumberOfArguments(DiagnosticEngine& diag, const CallExpr& ce, const FuncDecl& fd);
void DiagGenericFuncWithoutTypeArg(DiagnosticEngine& diag, const Expr& expr);
void DiagStaticAndNonStaticOverload(DiagnosticEngine& diag, const FuncDecl& fd, const FuncDecl& firstNonStatic);
void DiagImmutableAccessMutableFunc(DiagnosticEngine& diag, const MemberAccess& outerMa, const MemberAccess& ma);
void DiagCannotAssignToImmutable(DiagnosticEngine& diag, const Expr& ae, const Expr& perpetrator);
void DiagCannotOverride(DiagnosticEngine& diag, const Decl& child, const Decl& parent);
void DiagCannotHaveDefaultParam(DiagnosticEngine& diag, const FuncDecl& fd, const FuncParam& fp);
void DiagCannotInheritSealed(
    DiagnosticEngine& diag, const Decl& child, const Type& sealed, const bool& isCommon = false);
void DiagInvalidAssign(DiagnosticEngine& diag, const Node& node, const std::string& name);
void DiagLowerAccessLevelTypesUse(DiagnosticEngine& diag, const Decl& outDecl,
    const std::vector<std::pair<Node&, Decl&>>& limitedDecls, const std::vector<Ptr<Decl>>& hintDecls = {});
void DiagPatternInternalTypesUse(DiagnosticEngine& diag, const std::vector<std::pair<Node&, Decl&>>& inDecls);
void DiagAmbiguousUse(
    DiagnosticEngine& diag, const Node& node, const std::string& name, std::vector<Ptr<Decl>>& targets,
    const ImportManager& importManager);
void AddDiagNotesForImportedDecls(DiagnosticBuilder& builder, const ImportManager::DeclImportsMap& declToImports,
    const std::vector<Ptr<Decl>>& targets);
void DiagAmbiguousUpperBoundTargets(DiagnosticEngine& diag, const MemberAccess& ma, const OrderedDeclSet& targets);
void DiagPackageMemberNotFound(
    DiagnosticEngine& diag, const ImportManager& importManager, const MemberAccess& ma, const PackageDecl& pd);
void DiagUseClosureCaptureVarAlone(DiagnosticEngine& diag, const Expr& expr);
void DiagNeedNamedArgument(
    DiagnosticEngine& diag, const CallExpr& ce, const FuncDecl& fd, size_t paramPos, size_t argPos);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
void DiagForStaticVariableDependsGeneric(DiagnosticEngine& diag, const Node& node, const std::set<Ptr<Ty>>& targetTys);
#endif
/**
 * @brief Returns a vector of recommendations.
 * @details Users have to import the interfaces in order to use the methods defined in the extensions.
 *         The first of the pair is the function/property declaration in the extension.
 *         The second of the pair is the interface declaration.
 * @param typeManager Reference to the TypeManager, used for type checking and management.
 * @param importManager Reference to the ImportManager, used for managing imported modules and symbols.
 * @param ma Object of MemberAccess, representing the member access needed in the code.
 * @param builder Smart pointer to the DiagnosticBuilder, used for building and reporting diagnostic information.
 */
void RecommendImportForMemberAccess(TypeManager& typeManager, const ImportManager& importManager,
    const MemberAccess& ma, const Ptr<DiagnosticBuilder> builder);
} // namespace Cangjie::Sema

#endif
