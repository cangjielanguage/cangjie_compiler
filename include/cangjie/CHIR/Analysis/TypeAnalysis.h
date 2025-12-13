// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_TYPE_ANALYSIS_H
#define CANGJIE_CHIR_ANALYSIS_TYPE_ANALYSIS_H

#include "cangjie/CHIR/Analysis/DevirtualizationInfo.h"
#include "cangjie/CHIR/Analysis/ValueAnalysis.h"
#include "cangjie/CHIR/IR/Value/Value.h"

namespace Cangjie::CHIR {

struct TypeValue {
    TypeValue() = delete;

    /**
     * @brief constructor from devirtual type kind and CHIR Type
     * @param kind devirtual type kind, is exactly this type or have derived type.
     * @param baseLineType CHIR Type of value
     */
    TypeValue(DevirtualTyKind kind, Type* baseLineType);

    /// constructor of type value.
    virtual ~TypeValue()
    {
    }

    /**
     * @brief join with other type value.
     * @param rhs type value to join with.
     * @return type value if value is changed otherwise nullopt.
     */
    std::optional<std::unique_ptr<TypeValue>> Join(const TypeValue& rhs) const;

    /// print type value to string.
    std::string ToString() const;

    /// clone a type value.
    std::unique_ptr<TypeValue> Clone() const;

    /// get devirtual type kind, is exactly this type or have derived type.
    DevirtualTyKind GetTypeKind() const;

    /// get CHIR type.
    Type* GetSpecificType() const;

    /// get CHIR builder to create CHIR IR.
    static void SetCHIRBuilder(CHIRBuilder* chirBuilder);

protected:
    DevirtualTyKind kind;
    Type* baseLineType;

private:
    static CHIRBuilder* builder;

    std::string GetKindString(DevirtualTyKind clsTyKind) const;
};

/**
 * @brief the abstract value domain of type value
 */
using TypeValueDomain = ValueDomain<TypeValue>;
/**
 * @brief the state of type value domain
 */
using TypeDomain = State<TypeValueDomain>;
/**
 * @brief partially specialized analysis import value.
 */
template <> const std::string Analysis<TypeDomain>::name;
template <> const std::optional<unsigned> Analysis<TypeDomain>::blockLimit;
template <> TypeDomain::ChildrenMap ValueAnalysis<TypeValueDomain>::globalChildrenMap;
template <> TypeDomain::AllocatedRefMap ValueAnalysis<TypeValueDomain>::globalAllocatedRefMap;
template <> TypeDomain::AllocatedObjMap ValueAnalysis<TypeValueDomain>::globalAllocatedObjMap;
template <> std::vector<std::unique_ptr<Ref>> ValueAnalysis<TypeValueDomain>::globalRefPool;
template <> std::vector<std::unique_ptr<AbstractObject>> ValueAnalysis<TypeValueDomain>::globalAbsObjPool;
template <> TypeDomain ValueAnalysis<TypeValueDomain>::globalState;

/**
 * @brief check whether global var need type analysis.
 * @param gv global var to check.
 * @return flag global var need analyse
 */
template <> bool IsTrackedGV<ValueDomain<TypeValue>>(const GlobalVar& gv);

/**
 * @brief constant type analysis for CHIR IR to devirt invoke call.
 */
class TypeAnalysis final : public ValueAnalysis<TypeValueDomain> {
public:
    TypeAnalysis() = delete;

    /**
     * @brief constructor for type analysis.
     * @param func function to analyse
     * @param builder CHIR builder for generating IR.
     * @param isDebug flag whether print debug log.
     * @param realRetTyMap extra info to detect real type from apply expression.
     */
    TypeAnalysis(const Func* func, CHIRBuilder& builder, bool isDebug, const DevirtualizationInfo& devirtInfo);

    ~TypeAnalysis() final
    {
    }

    /**
     * @brief check CHIR function whether has invoke expression.
     * @param body function body to detect
     * @return flag if has invoke expression.
     */
    static bool CheckFuncHasInvoke(const BlockGroup& body);

    /**
     * @brief call CheckFuncHasInvoke to detect CHIR function whether has invoke expression.
     * @param method function to detect.
     * @return flag if has invoke expression.
     */
    static bool Filter(const Func& method);

    /// print type value with its expression.
    void PrintDebugMessage(const Expression* expr, const TypeValue* absVal) const;

private:
    void HandleFuncParam(TypeDomain& state, Parameter* param, Value* refObj) override;

    void HandleAllocateExpr(TypeDomain& state, const Allocate* expression, Value* newObj) override;

    void HandleNormalExpressionEffect(TypeDomain& state, const Expression* expression) override;

    void HandleApplyExpr(TypeDomain& state, const Apply* apply, Value* refObj) override;

    void HandleInvokeExpr(TypeDomain& state, const Invoke* invoke, Value* refObj) override;

    template <typename TTypeCast> void HandleTypeCastExpr(TypeDomain& state, const TTypeCast* typecast) const;

    template <typename TMemberAccess>
    void HandleMemberAccess(TypeDomain& state, const TMemberAccess* memberAccess) const;

    void HandleBoxExpr(TypeDomain& state, const Box* boxExpr) const;

    void HandleDefaultExpr(TypeDomain& state, const Expression* expr) const;

    Value* PreHandleDefaultExpr(TypeDomain& state, const Expression* expr) const;

    void UpdateDefaultValue(TypeDomain& state, Value* value, Value* refObj, Type* relType = nullptr) const;

    void PreHandleGetElementRefExpr(TypeDomain& state, const GetElementRef* getElemRef) override;

    void PreHandleFieldExpr(TypeDomain& state, const Field* field) override;

    std::optional<Block*> HandleTerminatorEffect(TypeDomain& state, const Terminator* terminator) override;

    const std::unordered_map<Func*, Type*>& realRetTyMap;

    const ConstMemberVarCollector::ConstMemberMapType& constMemberTypeMap;
};

} // namespace Cangjie::CHIR

#endif