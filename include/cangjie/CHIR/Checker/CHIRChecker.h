// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_CHIR_CHECKER_H
#define CANGJIE_CHIR_CHIR_CHECKER_H

#include <iostream>

#include "cangjie/CHIR/CHIR.h"
#include "cangjie/CHIR/IR/CHIRBuilder.h"
#include "cangjie/CHIR/IR/Expression/ExpressionWrapper.h"
#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/Option/Option.h"

namespace Cangjie::CHIR {
class CHIRChecker {
public:
    enum class Rule {
        EMPTY_BLOCK,            // there must be expressions in block, one block can't be empty
        CHECK_FUNC_BODY,         // check all expressions in func body, including their types
        GET_INSTANTIATE_VALUE_SHOULD_GONE,  // `GetInstantiateValue` shouldn't be in IR
        CHIR_GET_RTTI_STATIC_TYPE   // type in GetRTTIStatic should be This or Generic type
    };
    CHIRChecker(const Package& package, const Cangjie::GlobalOptions& opts, CHIRBuilder& builder);

    bool CheckPackage(const std::unordered_set<Rule>& r);

private:
    struct VirMethodFullContext {
        std::string srcCodeIdentifier;
        FuncType* originalFuncType{nullptr};
        std::vector<GenericType*> genericTypeParams;
        size_t offset{0};
        Type* thisType{nullptr};
        ClassType* srcParentType{nullptr};
    };
    template <typename F, typename Arg>
    void ParallelCheck(F check, const std::vector<Arg>& items)
    {
        size_t threadNum = opts.GetJobs();

        Cangjie::Utils::TaskQueue taskQueue(threadNum);
        // Place for holding the results.
        std::vector<Cangjie::Utils::TaskResult<void>> results;

        for (auto item : items) {
            results.emplace_back(
                taskQueue.AddTask<void>([check, this, item]() { (this->*check)(*item); }));
        }

        taskQueue.RunAndWaitForAllTasksCompleted();
    }

    /**
     * @brief `srcType` can be set to `dstType`
     * 1. store `srcType` to `dstType`
     * 2. func call from arg type(`srcType`) to param type(`dstType`)
     * 3. get element ref from declared type(`srcType`) to user point type(`dstType`)
     * 4. store element ref from user point type(`srcType`) to declared type(`dstType`)
     */
    bool TypeIsExpected(const Type& srcType, const Type& dstType);
    bool InstTypeArgsSatisfyGenericConstraints(
        const std::vector<Type*>& instTypeArgs, const std::vector<Type*>& genericTypeArgs);
    bool InstTypeCanSetToGenericRelatedType(Type& instType, const Type& genericRelatedType);

    // ===--------------------------------------------------------------------===//
    // API for report
    // ===--------------------------------------------------------------------===//
    void Warningln(const std::string& info);
    void Errorln(const std::string& info);
    void WarningInFunc(const Value& func, const std::string& info);
    void WarningInExpr(const Value& func, const Expression& expr, const std::string& info);
    void ErrorInFunc(const Value& func, const std::string& info);
    void ErrorInLambdaOrFunc(const FuncBase& func, const Lambda* lambda, const std::string& info);
    void ErrorInExpr(const Value& func, const Expression& expr, const std::string& info);
    void TypeCheckError(
        const Expression& expr, const Value& value, const std::string& expectedType, const Func& topLevelFunc);
    void ErrorInGlobalVar(const GlobalVarBase& var, const std::string& info);

    // ===--------------------------------------------------------------------===//
    // Check Values
    // ===--------------------------------------------------------------------===//
    /**
     * @brief global identifier must be unique
     */
    bool CheckGlobalValueIdentifier(const Value& value);
    void CheckTopLevelFunc(
        const Func* calculatedFunc, const Func& realFunc, const std::string& valueName, const std::string& valueId);
    void CheckFunc(const Func& func);
    void CheckGlobalVar(const GlobalVarBase& var);
    void CheckImportedVarAndFuncs(const ImportedValue& value);
    void CheckGlobalVarType(const GlobalVarBase& var);
    /**
     * @brief type in `params` must equal to type in `funcType`
     */
    void CheckFuncParams(
        const std::vector<Parameter*>& params, const FuncType& funcType, const std::string& funcIdentifier);
    bool CheckFuncType(const Type* type, const Lambda* lambda, const FuncBase& topLevelFunc);
    bool CheckParamTypes(const std::vector<Type*>& paramTypes, const Lambda* lambda, const FuncBase& topLevelFunc);
    bool CheckCFuncType(const FuncType& funcType, const Lambda* lambda, const FuncBase& topLevelFunc);
    void CheckFuncRetValue(
        const LocalVar* retVal, const Type& retType, const Lambda* lambda, const Func& topLevelFunc);
    void CheckBlockGroup(const BlockGroup& blockGroup, const Func& topLevelFunc);
    void CheckBlock(const Block& block, const Func& topLevelFunc);
    /**
     * @brief the successor of current block's predecessor must be current block
     */
    void CheckPredecessors(const Block& block, const Func& topLevelFunc);
    void CheckLocalId(BlockGroup& blockGroup, const Func& topLevelFunc);
    void CheckUnreachableOpAndGenericTyInFuncBody(const BlockGroup& blockGroup);
    void CheckUnreachableOpAndGenericTyInBG(const BlockGroup& blockGroup,
        std::vector<Value*>& reachableValues, std::vector<GenericType*>& reachableGenericTypes);
    void CheckUnreachableOpAndGenericTyInBlock(const Block& block, std::vector<Value*>& reachableValues,
        std::vector<GenericType*>& reachableGenericTypes, std::unordered_set<const Block*>& visitedBlocks);
    void CheckUnreachableOpAndGenericTyInExpr(const Expression& expr,
        std::vector<Value*>& reachableValues, std::vector<GenericType*>& reachableGenericTypes);
    void CheckUnreachableOperandInExpr(const Expression& expr, std::vector<Value*>& reachableValues);
    void CheckUnreachableGenericTypeInExpr(const Expression& expr, std::vector<GenericType*>& reachableGenericTypes);
    bool CheckFuncBase(const FuncBase& func);
    bool CheckOriginalLambdaInfo(const FuncBase& func);
    bool CheckLiftedLambdaType(const FuncBase& func);
    
    // ===--------------------------------------------------------------------===//
    // Check CustomTypeDef
    // ===--------------------------------------------------------------------===//
    void CheckStructDef(const StructDef& def);
    bool CheckParentCustomTypeDef(const FuncBase& func, const CustomTypeDef& def, bool isInDef);
    void CheckCustomTypeDef(const CustomTypeDef& def);
    bool CheckCustomTypeDefIdentifier(const CustomTypeDef& def);
    void CheckCustomType(const CustomTypeDef& def);
    void CheckInstanceMemberVar(const CustomTypeDef& def);
    void CheckStaticMemberVar(const CustomTypeDef& def);
    void CheckVTable(const CustomTypeDef& def);
    void CheckCStruct(const StructDef& def);
    void CheckClassDef(const ClassDef& def);
    void CheckAbstractMethod(const ClassDef& def);
    void CheckEnumDef(const EnumDef& def);
    void CheckExtendDef(const ExtendDef& def);

    // ===--------------------------------------------------------------------===//
    // Check Terminator
    // ===--------------------------------------------------------------------===//
    void CheckTerminator(const Expression& expr, const Func& topLevelFunc);
    void CheckGoTo(const GoTo& expr, const Func& topLevelFunc);
    void CheckExit(const Exit& expr, const Func& topLevelFunc);
    void CheckRaiseException(const RaiseException& expr, const Func& topLevelFunc);
    void CheckBranch(const Branch& expr, const Func& topLevelFunc);
    void CheckMultiBranch(const MultiBranch& expr, const Func& topLevelFunc);
    void CheckApplyWithException(const ApplyWithException& expr, const Func& topLevelFunc);
    void CheckApplyBase(const ApplyBase& expr, const Func& topLevelFunc);
    void CheckInvokeWithException(const InvokeWithException& expr, const Func& topLevelFunc);
    void CheckInvokeBase(const InvokeBase& expr, const Func& topLevelFunc);
    void CheckInvokeStaticWithException(const InvokeStaticWithException& expr, const Func& topLevelFunc);
    void CheckInvokeStaticBase(const InvokeStaticBase& expr, const Func& topLevelFunc);
    void CheckIntOpWithException(const IntOpWithException& expr, const Func& topLevelFunc);
    void CheckUnaryExprBase(const UnaryExprBase& expr, const Func& topLevelFunc);
    void CheckBinaryExprBase(const BinaryExprBase& expr, const Func& topLevelFunc);
    void CheckSpawnWithException(const SpawnWithException& expr, const Func& topLevelFunc);
    void CheckSpawnBase(const SpawnBase& expr, const Func& topLevelFunc);
    void CheckTypeCastWithException(const TypeCastWithException& expr, const Func& topLevelFunc);
    void CheckIntrinsicWithException(const IntrinsicWithException& expr, const Func& topLevelFunc);
    void CheckIntrinsicBase(const IntrinsicBase& expr, const Func& topLevelFunc);
    void CheckAllocateWithException(const AllocateWithException& expr, const Func& topLevelFunc);
    void CheckAllocateBase(const AllocateBase& expr, const Func& topLevelFunc);
    void CheckRawArrayAllocateWithException(const RawArrayAllocateWithException& expr, const Func& topLevelFunc);
    void CheckRawArrayAllocateBase(const RawArrayAllocateBase& expr, const Func& topLevelFunc);
    // ===--------------------------------------------------------------------===//
    // Check Unary Expression
    // ===--------------------------------------------------------------------===//
    void CheckUnaryExpression(const UnaryExpression& expr, const Func& topLevelFunc);

    // ===--------------------------------------------------------------------===//
    // Check Binary Expression
    // ===--------------------------------------------------------------------===//
    void CheckBinaryExpression(const BinaryExpression& expr, const Func& topLevelFunc);
    void CheckCalculExpression(const BinaryExprBase& expr, const Func& topLevelFunc);
    void CheckExponentiationExpression(const BinaryExprBase& expr, const Func& topLevelFunc);
    void CheckBitExpression(const BinaryExprBase& expr, const Func& topLevelFunc);
    void CheckCompareExpression(const BinaryExprBase& expr, const Func& topLevelFunc);
    void CheckLogicExpression(const BinaryExprBase& expr, const Func& topLevelFunc);

    // ===--------------------------------------------------------------------===//
    // Check Memory Expression
    // ===--------------------------------------------------------------------===//
    void CheckMemoryExpression(const Expression& expr, const Func& topLevelFunc);
    void CheckAllocate(const Allocate& expr, const Func& topLevelFunc);
    void CheckLoad(const Load& expr, const Func& topLevelFunc);
    void CheckStore(const Store& expr, const Func& topLevelFunc);
    void CheckGetElementRef(const GetElementRef& expr, const Func& topLevelFunc);
    void CheckGetElementByName(const GetElementByName& expr, const Func& topLevelFunc);
    void CheckStoreElementRef(const StoreElementRef& expr, const Func& topLevelFunc);
    void CheckStoreElementByName(const StoreElementByName& expr, const Func& topLevelFunc);

    // ===--------------------------------------------------------------------===//
    // Check Control Flow Expression
    // ===--------------------------------------------------------------------===//
    void CheckControlFlowExpression(const Expression& expr, const Func& topLevelFunc);
    void CheckLambda(const Lambda& expr, const Func& topLevelFunc);

    // ===--------------------------------------------------------------------===//
    // Check Other Expression
    // ===--------------------------------------------------------------------===//
    void CheckOtherExpression(const Expression& expr, const Func& topLevelFunc);
    void CheckConstant(const Constant& expr, const Func& topLevelFunc);
    void CheckDebug(const Debug& expr, const Func& topLevelFunc);
    void CheckTuple(const Tuple& expr, const Func& topLevelFunc);
    void CheckEnumTuple(const Tuple& expr, const Func& topLevelFunc);
    void CheckStructTuple(const Tuple& expr, const Func& topLevelFunc);
    void CheckNormalTuple(const Tuple& expr, const Func& topLevelFunc);
    void CheckField(const Field& expr, const Func& topLevelFunc);
    void CheckFieldByName(const FieldByName& expr, const Func& topLevelFunc);
    void CheckApply(const Apply& expr, const Func& topLevelFunc);
    bool CheckCallee(const Value& callee, const Expression& expr, const Func& topLevelFunc);
    void CheckApplyFuncArgs(const std::vector<Value*>& args,
        const std::vector<Type*>& instParamTypes, bool varArgs, const Expression& expr, const Func& topLevelFunc);
    FuncType* CalculateInstFuncType(
        FuncType& originalFuncType, const std::vector<Type*>& instantiatedTypeArgs,
        const std::vector<GenericType*>& genericTypeParams, Type* instOuterType);
    bool CheckInstantiatedTypeArgs(const std::vector<Type*>& instantiatedTypeArgs,
        const std::vector<GenericType*>& genericTypeParams, const Expression& expr, const Func& topLevelFunc);
    bool CheckApplyThisType(
        const Value& callee, const Type* thisType, const Expression& expr, const Func& topLevelFunc);
    void CheckApplyFuncRetValue(const Type& instRetType, const Expression& expr, const Func& topLevelFunc);
    void CheckInvoke(const Invoke& expr, const Func& topLevelFunc);
    bool CheckInvokeThisType(
        Type& objType, const Type* thisType, const Expression& expr, const Func& topLevelFunc);
    bool CheckVirtualMethod(const VirMethodFullContext& methodCtx, const Expression& expr, const Func& topLevelFunc);
    const std::vector<VirtualMethodInfo>* CheckVTableExist(
        const Type& thisType, const ClassType& srcParentType, const Expression& expr, const Func& topLevelFunc);
    const std::vector<VirtualMethodInfo>* CheckVTableExist(const BuiltinType& thisType, const ClassType& srcParentType);
    const std::vector<VirtualMethodInfo>* CheckVTableExist(const CustomType& thisType, const ClassType& srcParentType);
    const std::vector<VirtualMethodInfo>* CheckVTableExist(const ClassType& srcParentType, const Func& topLevelFunc);
    const std::vector<VirtualMethodInfo>* CheckVTableExist(const GenericType& thisType, const ClassType& srcParentType);
    bool CheckVirtualMethodFuncType(const FuncType& declaredType,
        const FuncType& callSiteType, const std::string& errMsgBase, const Func& topLevelFunc);
    void CheckInvokeFuncArgs(const std::vector<Value*>& args,
        const std::vector<Type*>& originalParamTypes, const Expression& expr, const Func& topLevelFunc);
    void CheckInvokeStatic(const InvokeStatic& expr, const Func& topLevelFunc);
    void CheckInstanceOf(const InstanceOf& expr, const Func& topLevelFunc);
    void CheckTypeCast(const TypeCast& expr, const Func& topLevelFunc);
    void CheckGetException(const GetException& expr, const Func& topLevelFunc);
    void CheckSpawn(const Spawn& expr, const Func& topLevelFunc);
    void CheckRawArrayAllocate(const RawArrayAllocate& expr, const Func& topLevelFunc);
    void CheckRawArrayLiteralInit(const RawArrayLiteralInit& expr, const Func& topLevelFunc);
    void CheckRawArrayInitByValue(const RawArrayInitByValue& expr, const Func& topLevelFunc);
    void CheckVArray(const VArray& expr, const Func& topLevelFunc);
    void CheckVArrayBuilder(const VArrayBuilder& expr, const Func& topLevelFunc);
    void CheckIntrinsic(const Intrinsic& expr, const Func& topLevelFunc);
    void CheckBox(const Box& expr, const Func& topLevelFunc);
    void CheckUnBox(const UnBox& expr, const Func& topLevelFunc);
    void CheckTransformToGeneric(const TransformToGeneric& expr, const Func& topLevelFunc);
    void CheckTransformToConcrete(const TransformToConcrete& expr, const Func& topLevelFunc);
    void CheckGetInstantiateValue(const GetInstantiateValue& expr, const Func& topLevelFunc);
    void CheckUnBoxToRef(const UnBoxToRef& expr, const Func& topLevelFunc);
    void CheckGetRTTI(const GetRTTI& expr, const Func& topLevelFunc);
    void CheckGetRTTIStatic(const GetRTTIStatic& expr, const Func& topLevelFunc);
    bool CheckThisTypeIsEqualOrSubTypeOfFuncParentType(
        Type& thisType, const FuncBase& func, const Expression& expr, const Func& topLevelFunc);
    void CheckInout(const IntrinsicBase& expr, const Func& topLevelFunc);
    void CheckInoutOpSrc(const Value& op, const IntrinsicBase& expr, const Func& topLevelFunc);

    // ===--------------------------------------------------------------------===//
    // Check Expressions
    // ===--------------------------------------------------------------------===//
    void CheckExpression(const Expression& expr, const Func& topLevelFunc);
    /**
     * @brief terminator can't jump to another block group
     */
    void CheckTerminatorJump(const Terminator& terminator, const Func& topLevelFunc);

    // ===--------------------------------------------------------------------===//
    // Utils
    // ===--------------------------------------------------------------------===//
    void OverflowStrategyMustBeValid(
        const OverflowStrategy& ofs, const Expression& expr, const Func& topLevelFunc);
    bool CheckTypeIsValid(
        const Type& type, const std::string& typeName, const Expression& expr, const Func& topLevelFunc);
    bool OperandNumIsEqual(size_t expectedNum, const Expression& expr, const Func& topLevelFunc);
    bool OperandNumIsEqual(const std::vector<size_t>& expectedNum, const Expression& expr, const Func& topLevelFunc);
    bool SuccessorNumIsEqual(size_t expectedNum, const Terminator& expr, const Func& topLevelFunc);
    bool OperandNumAtLeast(size_t expectedNum, const Expression& expr, const Func& topLevelFunc);
    bool SuccessorNumAtLeast(size_t expectedNum, const Terminator& expr, const Func& topLevelFunc);
    void ShouldNotHaveResult(const Terminator& expr, const Func& topLevelFunc);
    bool CheckHaveResult(const Expression& expr, const Func& topLevelFunc);

private:
    const Package& package;
    const Cangjie::GlobalOptions& opts;
    CHIRBuilder& builder;

    std::unordered_set<std::string> identifiers;
    std::set<std::string> duplicatedGlobalIds;
    std::mutex checkIdentMutex;
    std::ostream& errorMessage = std::cerr;
    std::atomic<bool> checkResult{true};
    std::unordered_set<Rule> optionalRules;
};
}

#endif