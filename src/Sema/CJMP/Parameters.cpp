// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements handling common/specific function parameters, especially default arguments.
 * It provides functions to match parameters between common and specific functions (CJMP rules),
 * and to propagate default arguments from common to specific functions.
 *
 */
#include "MPTypeCheckerImpl.h"
#include "cangjie/AST/AttributePack.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Basic/Position.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/SafePointer.h"

using namespace Cangjie;
using namespace AST;

namespace {
/*
 * Checks whether the parameter is a named parameter with default argument.
 */
bool HasDefaultArgument(Ptr<FuncParam> param)
{
    return param->isNamedParam && param->TestAttr(Attribute::HAS_INITIAL);
}

/**
 * Clone parameter default argument from common parameter to specific
 */
void CloneDefaultArgument(Ptr<FuncParam> from, Ptr<FuncParam> to, const FuncDecl& toFunc)
{
    CJC_ASSERT_WITH_MSG(!to->desugarDecl, "The specific decl shouldn't have desugarDecl");
    CJC_ASSERT_WITH_MSG(!to->assignment, "The specific decl shouldn't have assignment expression");

    CJC_ASSERT_WITH_MSG(from->desugarDecl, "The common decl must have desugarDecl");

    to->desugarDecl = ASTCloner::Clone(from->desugarDecl.get());
    to->desugarDecl->begin = from->desugarDecl->begin;
    to->desugarDecl->end = from->desugarDecl->end;
    to->desugarDecl->outerDecl = toFunc.outerDecl;

    // an assignment expression can be null here in some cases if deserialized from CJO
    // fortunately it's not a problem and no crashes caused by null assignment
    if (from->assignment) {
        to->assignment = ASTCloner::Clone(from->assignment.get());
    }
    to->EnableAttr(Attribute::HAS_INITIAL);
}

/**
 * @brief Iterates over corresponding parameter lists in common and specific functions.
 *
 * @tparam F Callback function type
 * @param commonFunc The common function declaration
 * @param specificFunc The specific function declaration
 * @param callback Function to call for each param list pair, returns false to stop iteration
 * @return true if all param lists processed successfully, false if sizes mismatch or callback returned false
 */
template <typename F> bool ForEachParamList(const FuncDecl& commonFunc, const FuncDecl& specificFunc, F&& callback)
{
    if (commonFunc.funcBody->paramLists.size() != specificFunc.funcBody->paramLists.size()) {
        return false;
    }

    const auto paramListSize = commonFunc.funcBody->paramLists.size();
    for (size_t i = 0; i < paramListSize; i++) {
        auto& commonParams = commonFunc.funcBody->paramLists[i];
        auto& specificParams = specificFunc.funcBody->paramLists[i];
        if (!callback(*specificParams, *commonParams)) {
            return false;
        }
    }
    return true;
}
} // namespace

/**
 * Checks whether the provided functions has compatible function parameters conforming to CJMP rules.
 * It reports diagnostics in the cases when parameters are not compatible and for sure there cannot be
 * any function overloads with compatible parameters (otherwise they would lead to conflicting overloads error)
 */
bool MPTypeCheckerImpl::MatchCJMPFunctionParameters(FuncParamList& specificParams, FuncParamList& commonParams)
{
    if (commonParams.params.size() != specificParams.params.size()) {
        return false;
    }

    for (size_t i = 0; i < commonParams.params.size(); i++) {
        auto& commonParam = commonParams.params[i];
        auto& specificParam = specificParams.params[i];

        if (commonParam->isNamedParam != specificParam->isNamedParam) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_specific_has_different_parameter, *specificParam);
            return false;
        }
        if (commonParam->isNamedParam && specificParam->isNamedParam) {
            if (commonParam->identifier.GetRawText() != specificParam->identifier.GetRawText()) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_specific_has_different_parameter, *specificParam);
                return false;
            }
        }
        if (commonParam->isMemberParam != specificParam->isMemberParam) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_specific_primary_unmatched_var_decl, *specificParam);
            return false;
        }
    }

    return true;
}

/**
 * Checks whether the provided functions has compatible function parameters conforming to CJMP rules.
 * It reports diagnostics in the cases when parameters are not compatible and for sure there cannot be
 * any function overloads with compatible parameters (otherwise they would lead to conflicting overloads error)
 */
bool MPTypeCheckerImpl::MatchCJMPFunctionParameters(AST::FuncDecl& specificFunc, AST::FuncDecl& commonFunc)
{
    CJC_ASSERT_WITH_MSG(commonFunc.funcBody, "common function must have a body");
    CJC_ASSERT_WITH_MSG(specificFunc.funcBody, "specific function must have a body");

    auto commonGenericsCount = commonFunc.GetGenericsCount();
    auto specificGenericsCount = specificFunc.GetGenericsCount();

    if (commonGenericsCount != specificGenericsCount) {
        diag.Diagnose(
            specificFunc, DiagKind::sema_generic_member_type_argument_different, specificFunc.identifier.Val());
        return false;
    } else if (commonGenericsCount > 0) {
        if (!MatchCJMPFunctionGenerics(specificFunc, commonFunc)) {
            return false;
        }
    } else {
        if (!typeManager.IsFuncDeclSubType(specificFunc, commonFunc)) {
            return false;
        }
    }

    return ForEachParamList(commonFunc, specificFunc, [this](auto& specificParams, auto& commonParams) {
        return MatchCJMPFunctionParameters(specificParams, commonParams);
    });
}

/**
 * @brief Checks whether generic function signatures match between common and specific functions in CJMP.
 *
 * This function validates that the generic type parameters of a specific function implementation
 * are compatible with the corresponding generic function declaration in the common code. It performs
 * type mapping and subtype checking to ensure the specific implementation correctly conforms to the
 * common declaration.
 *
 * The matching process involves:
 * 1. Mapping generic type arguments from the common function to the specific function
 * 2. Instantiating the common function type with the mapped type substitutions
 * 3. Checking if the specific function type is a subtype of the instantiated common function type
 *
 * @param specificFunc The specific function declaration to validate
 * @param commonFunc The common function declaration to match against
 *
 * @return true if the generic function signatures match, false otherwise
 *
 * @note This function is called from MatchCJMPFunctionParameters when both functions have generic parameters
 * @see MapCJMPGenericTypeArgs for the type mapping logic
 * @see TypeManager::IsFuncTySubType for the subtype checking logic
 */
bool MPTypeCheckerImpl::MatchCJMPFunctionGenerics(AST::FuncDecl& specificFunc, AST::FuncDecl& commonFunc)
{
    bool isGenericFuncMatch = true;
    TypeSubst genericTyMap;
    MapCJMPGenericTypeArgs(genericTyMap, commonFunc, specificFunc);
    if (!genericTyMap.empty()) {
        isGenericFuncMatch = false;
        auto newCommonFuncTy = StaticCast<FuncTy>(typeManager.GetInstantiatedTy(commonFunc.GetTy(), genericTyMap).Ty());
        auto specificFuncTy = StaticCast<FuncTy>(specificFunc.DataTy());
        if (typeManager.IsFuncTySubType(*specificFuncTy, *newCommonFuncTy)) {
            isGenericFuncMatch = true;
        }
    }

    if (!isGenericFuncMatch) {
        return false;
    }

    return true;
}

/**
 * @brief Propagates parameter default arguments from common function parameters to specific.
 *
 * @param common The common function parameters potentially containing parameters to handle
 * @param specific The specific function parameters to receive the propagated arguments
 */
void MPTypeCheckerImpl::PropagateDefaultArguments(const AST::FuncParamList& commonParams,
    const AST::FuncParamList& specificParams, const AST::FuncDecl& specificFunc) const
{
    const auto paramCount = commonParams.params.size();
    CJC_ASSERT_WITH_MSG(paramCount == specificParams.params.size(), "Matched param lists must have the same size");

    for (size_t i = 0; i < paramCount; ++i) {
        auto& specificParam = specificParams.params[i];
        if (!specificParam->isNamedParam) {
            continue;
        }

        auto& commonParam = commonParams.params[i];
        if (HasDefaultArgument(commonParam)) {
            if (HasDefaultArgument(specificParam)) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_cjmp_parameter_default_value_both_sides,
                    MakeRange(specificParam->identifier));
                continue;
            }

            CloneDefaultArgument(commonParam, specificParam, specificFunc);
        }
    }
}

/**
 * @brief Propagates parameter default arguments from common function to specific one.
 *
 * @param common The common function declaration potentially containing parameters to handle
 * @param specific The specific declaration to receive the propagated arguments
 */
void MPTypeCheckerImpl::PropagateDefaultArguments(const FuncDecl& commonFunc, const FuncDecl& specificFunc) const
{
    CJC_ASSERT_WITH_MSG(commonFunc.funcBody, "common function must have a body");
    CJC_ASSERT_WITH_MSG(specificFunc.funcBody, "specific function must have a body");

    ForEachParamList(commonFunc, specificFunc, [this, &specificFunc](auto& specificParams, auto& commonParams) {
        PropagateDefaultArguments(commonParams, specificParams, specificFunc);
        return true;
    });
}
