// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_TRANSFORMATION_DEVIRTUALIZATION_H
#define CANGJIE_CHIR_TRANSFORMATION_DEVIRTUALIZATION_H

#include "cangjie/CHIR/Analysis/AnalysisWrapper.h"
#include "cangjie/CHIR/Analysis/DevirtualizationInfo.h"
#include "cangjie/CHIR/Analysis/TypeAnalysis.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Value.h"
#include <unordered_map>

namespace Cangjie::CHIR {
class Devirtualization {
public:
    /**
     * @brief wrapper for type analysis.
     */
    using TypeAnalysisWrapper = AnalysisWrapper<TypeAnalysis, TypeDomain>;

    /**
     * @brief rewrite info if a invoke can be de-virtualize.
     */
    struct RewriteInfo {
        Invoke* invoke;
        FuncBase* realCallee;
        Type* thisType;
        std::vector<Type*> typeArgs;
        Apply* newApply = nullptr;
    };

    Devirtualization() = delete;

    /**
     * @brief constructor for Devirtualization pass.
     * @param typeAnalysisWrapper
     * @param devirtFuncInfo
     */
    explicit Devirtualization(TypeAnalysisWrapper* typeAnalysisWrapper, DevirtualizationInfo& devirtFuncInfo);

    /**
     * @brief main optimization pass entry.
     * @param funcs funcs to devirtualization.
     * @param builder CHIR builder for generating IR.
     * @param isDebug flag whether print debug log.
     */
    void RunOnFuncs(const std::vector<Func*>& funcs, CHIRBuilder& builder, bool isDebug);

    /**
     * @brief get functions containing invoke expression.
     * @param package user package to optimization.
     * @return return functions containing invoke expression.
     */
    static std::vector<Func*> CollectContainInvokeExprFuncs(const Ptr<const Package>& package);

    /// get optimized functions which are marked frozen.
    const std::vector<Func*>& GetFrozenInstFuns() const;

    /// after first devirt pass, do second devirtualization for frozen func.
    /// this function mainly get results from second type analysis.
    void AppendFrozenFuncState(const Func* func, std::unique_ptr<Results<TypeDomain>> analysisRes);

    /// function signature to determine a certain function.
    struct FuncSig {
        std::string name;
        std::vector<Type*> types;
        std::vector<Type*> typeArgs;
    };

private:
    void RunOnFunc(const Func* func, CHIRBuilder& builder);

    std::pair<FuncBase*, Type*> FindRealCallee(
        CHIRBuilder& builder, const TypeValue* typeState, const FuncSig& method) const;

    bool IsValidSubType(CHIRBuilder& builder, const Type* expected, Type* specific,
        std::unordered_map<const GenericType*, Type*>& replaceTable) const;

    bool IsInstantiationOf(CHIRBuilder& builder, const GenericType* generic, const Type* instantiated) const;
    
    void InstantiateFuncIfPossible(CHIRBuilder& builder, std::vector<RewriteInfo>& rewriteInfoList);
    
    void CollectCandidates(
        CHIRBuilder& builder, ClassType* specific, std::pair<FuncBase*, Type*>& res, const FuncSig& method) const;
    
    FuncBase* GetCandidateFromSpecificType(
        CHIRBuilder& builder, ClassType& specific, const FuncSig& method) const;
    
    static void RewriteToApply(CHIRBuilder& builder, std::vector<RewriteInfo>& rewriteInfos, bool isDebug);
    
    static bool RewriteToBuiltinOp(CHIRBuilder& builder, const RewriteInfo& info, bool isDebug);

    /**
     * check func whether has invoke expression, implement func for CollectContainInvokeExprFuncs
     */
    static bool CheckFuncHasInvoke(const BlockGroup& bg);

    TypeAnalysisWrapper* analysisWrapper;
    DevirtualizationInfo& devirtFuncInfo;
    std::vector<RewriteInfo> rewriteInfos{};

    // frozen inst functions after devirt, these func need a devirt optimization too after first devirt opt
    std::vector<Func*> frozenInstFuns;
    // extra type state from outside
    std::unordered_map<const Func*, std::unique_ptr<Results<TypeDomain>>> frozenStates;

    std::unordered_map<std::string, Func*> frozenInstFuncMap;
};
} // namespace Cangjie::CHIR

#endif