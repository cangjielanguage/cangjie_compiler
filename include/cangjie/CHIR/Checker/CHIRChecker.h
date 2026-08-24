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
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/Option/Option.h"

namespace Cangjie::CHIR {
class CHIRChecker {
public:
    enum class Rule {
        EMPTY_BLOCK,                        // there must be expressions in block, one block can't be empty
        CHECK_FUNC_BODY,                    // check all expressions in func body, including their types
        GET_INSTANTIATE_VALUE_SHOULD_GONE,  // `GetInstantiateValue` shouldn't be in IR
        CHIR_GET_RTTI_STATIC_TYPE,          // type in GetRTTIStatic should be This or Generic type
        RETURN_TYPE_NEED_BE_VOID,           // global func's return type need to be `Void`
        IMPORTED_CONST_VAR_SHOULD_HAVE_INITIALIZER // imported const var should have initializer
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
    void ErrorInLambdaOrFunc(const Function& func, const Lambda* lambda, const std::string& info);
    void ErrorInExpr(const Value& func, const Expression& expr, const std::string& info);
    void TypeCheckError(
        const Expression& expr, const Value& value, const std::string& expectedType, const Function& topLevelFunc);
    void ErrorInGlobalVar(const GlobalVar& var, const std::string& info);

    // ===--------------------------------------------------------------------===//
    // Check Values
    // ===--------------------------------------------------------------------===//
    /**
     * @brief global identifier must be unique
     */
    bool CheckGlobalValueIdentifier(const Value& value);
    void CheckTopLevelFunc(const Function* calculatedFunc,
        const Function& realFunc, const std::string& valueName, const std::string& valueId);
    void CheckFunc(const Function& func);
    void CheckGlobalVar(const GlobalVar& var);
    void CheckGlobalVarType(const GlobalVar& var);
    /**
     * @brief type in `params` must equal to type in `funcType`
     */
    void CheckFuncParams(
        const std::vector<Parameter*>& params, const FuncType& funcType, const std::string& funcIdentifier);
    bool CheckFuncType(const Type* type, const Lambda* lambda, const Function& topLevelFunc);
    bool CheckParamTypes(const std::vector<Type*>& paramTypes, const Lambda* lambda, const Function& topLevelFunc);
    bool CheckCFuncType(const FuncType& funcType, const Lambda* lambda, const Function& topLevelFunc);
    void CheckFuncRetValue(
        const LocalVar* retVal, const Type& retType, const Lambda* lambda, const Function& topLevelFunc);
    void CheckRetureTypeIfIsVoid(const Function& topLevelFunc, const FuncType& funcType, bool needBeVoid);
    void CheckBlockGroup(const BlockGroup& blockGroup, const Function& topLevelFunc);
    void CheckBlock(const Block& block, const Function& topLevelFunc);
    /**
     * @brief the successor of current block's predecessor must be current block
     */
    void CheckPredecessors(const Block& block, const Function& topLevelFunc);
    void CheckLocalId(BlockGroup& blockGroup, const Function& topLevelFunc);
    void CheckUnreachableOpAndGenericTyInFuncBody(const BlockGroup& blockGroup);
    void CheckUnreachableOpAndGenericTyInBG(const BlockGroup& blockGroup,
        std::vector<Value*>& reachableValues, std::vector<GenericType*>& reachableGenericTypes);
    void CheckUnreachableOpAndGenericTyInBlock(const Block& block, std::vector<Value*>& reachableValues,
        std::vector<GenericType*>& reachableGenericTypes, std::unordered_set<const Block*>& visitedBlocks);
    void CheckUnreachableOpAndGenericTyInExpr(const Expression& expr,
        std::vector<Value*>& reachableValues, std::vector<GenericType*>& reachableGenericTypes);
    void CheckUnreachableOperandInExpr(const Expression& expr, std::vector<Value*>& reachableValues);
    void CheckUnreachableGenericTypeInExpr(const Expression& expr, std::vector<GenericType*>& reachableGenericTypes);
    bool CheckFuncBase(const Function& func);
    bool CheckOriginalLambdaInfo(const Function& func);
    bool CheckLiftedLambdaType(const Function& func);
    
    // ===--------------------------------------------------------------------===//
    // Check CustomTypeDef
    // ===--------------------------------------------------------------------===//
    void CheckStructDef(const StructDef& def);
    bool CheckParentCustomTypeDef(const Function& func, const CustomTypeDef& def, bool isInDef);
    void CheckCustomTypeDef(const CustomTypeDef& def);
    bool CheckCustomTypeDefIdentifier(const CustomTypeDef& def);
    void CheckCustomType(const CustomTypeDef& def);
    void CheckInstanceMemberVar(const CustomTypeDef& def);
    void CheckStaticMemberVar(const CustomTypeDef& def);
    void CheckVTable(const CustomTypeDef& def);
    void CheckCStruct(const StructDef& def);
    void CheckClassDef(const ClassDef& def);
    void CheckEnumDef(const EnumDef& def);
    void CheckExtendDef(const ExtendDef& def);

    // ===--------------------------------------------------------------------===//
    // Check Terminator
    // ===--------------------------------------------------------------------===//
    void CheckTerminator(const Expression& expr, const Function& topLevelFunc);
    void CheckGoTo(const GoTo& expr, const Function& topLevelFunc);
    void CheckExit(const Exit& expr, const Function& topLevelFunc);
    void CheckRaiseException(const RaiseException& expr, const Function& topLevelFunc);
    void CheckBranch(const Branch& expr, const Function& topLevelFunc);
    void CheckMultiBranch(const MultiBranch& expr, const Function& topLevelFunc);
    void CheckTryApply(const TryApply& expr, const Function& topLevelFunc);
    void CheckApplyBase(const ApplyBase& expr, const Function& topLevelFunc);
    void CheckTryInvoke(const TryInvoke& expr, const Function& topLevelFunc);
    void CheckInvokeBase(const InvokeBase& expr, const Function& topLevelFunc);
    void CheckTryInvokeStatic(const TryInvokeStatic& expr, const Function& topLevelFunc);
    void CheckInvokeStaticBase(const InvokeStaticBase& expr, const Function& topLevelFunc);
    void CheckTryUnaryExpression(const TryUnaryExpression& expr, const Function& topLevelFunc);
    void CheckTryBinaryExpression(const TryBinaryExpression& expr, const Function& topLevelFunc);
    void CheckUnaryExprBase(const UnaryExpressionBase& expr, const Function& topLevelFunc);
    void CheckBinaryExprBase(const BinaryExpressionBase& expr, const Function& topLevelFunc);
    void CheckTrySpawn(const TrySpawn& expr, const Function& topLevelFunc);
    void CheckSpawnBase(const SpawnBase& expr, const Function& topLevelFunc);
    void CheckTryNumericCast(const TryNumericCast& expr, const Function& topLevelFunc);
    void CheckTryIntrinsic(const TryIntrinsic& expr, const Function& topLevelFunc);
    void CheckIntrinsicBase(const IntrinsicBase& expr, const Function& topLevelFunc);
    void CheckTryAllocate(const TryAllocate& expr, const Function& topLevelFunc);
    void CheckAllocateBase(const AllocateBase& expr, const Function& topLevelFunc);
    void CheckTryRawArrayAllocate(const TryRawArrayAllocate& expr, const Function& topLevelFunc);
    void CheckRawArrayAllocateBase(const RawArrayAllocateBase& expr, const Function& topLevelFunc);
    void CheckExclave(const Exclave& expr, const Function& topLevelFunc);
    // ===--------------------------------------------------------------------===//
    // Check Unary Expression
    // ===--------------------------------------------------------------------===//
    void CheckUnaryExpression(const UnaryExpression& expr, const Function& topLevelFunc);

    // ===--------------------------------------------------------------------===//
    // Check Binary Expression
    // ===--------------------------------------------------------------------===//
    void CheckBinaryExpression(const BinaryExpression& expr, const Function& topLevelFunc);
    void CheckCalculExpression(const BinaryExpressionBase& expr, const Function& topLevelFunc);
    void CheckExponentiationExpression(const BinaryExpressionBase& expr, const Function& topLevelFunc);
    void CheckBitExpression(const BinaryExpressionBase& expr, const Function& topLevelFunc);
    void CheckCompareExpression(const BinaryExpressionBase& expr, const Function& topLevelFunc);
    void CheckLogicExpression(const BinaryExpressionBase& expr, const Function& topLevelFunc);

    // ===--------------------------------------------------------------------===//
    // Check Memory Expression
    // ===--------------------------------------------------------------------===//
    void CheckMemoryExpression(const Expression& expr, const Function& topLevelFunc);
    void CheckAllocate(const Allocate& expr, const Function& topLevelFunc);
    void CheckLoad(const Load& expr, const Function& topLevelFunc);
    void CheckStore(const Store& expr, const Function& topLevelFunc);
    void CheckGetElementRef(const GetElementRef& expr, const Function& topLevelFunc);
    void CheckGetElementByName(const GetElementByName& expr, const Function& topLevelFunc);
    void CheckStoreElementRef(const StoreElementRef& expr, const Function& topLevelFunc);
    void CheckStoreElementByName(const StoreElementByName& expr, const Function& topLevelFunc);

    // ===--------------------------------------------------------------------===//
    // Check Control Flow Expression
    // ===--------------------------------------------------------------------===//
    void CheckControlFlowExpression(const Expression& expr, const Function& topLevelFunc);
    void CheckLambda(const Lambda& expr, const Function& topLevelFunc);

    // ===--------------------------------------------------------------------===//
    // Check Other Expression
    // ===--------------------------------------------------------------------===//
    void CheckOtherExpression(const Expression& expr, const Function& topLevelFunc);
    void CheckConstant(const Constant& expr, const Function& topLevelFunc);
    void CheckDebug(const Debug& expr, const Function& topLevelFunc);
    void CheckTuple(const Tuple& expr, const Function& topLevelFunc);
    void CheckEnumTuple(const Tuple& expr, const Function& topLevelFunc);
    void CheckStructTuple(const Tuple& expr, const Function& topLevelFunc);
    void CheckNormalTuple(const Tuple& expr, const Function& topLevelFunc);
    void CheckField(const Field& expr, const Function& topLevelFunc);
    void CheckFieldByName(const FieldByName& expr, const Function& topLevelFunc);
    void CheckApply(const Apply& expr, const Function& topLevelFunc);
    bool CheckCallee(const Value& callee, const Expression& expr, const Function& topLevelFunc);
    void CheckApplyFuncArgs(const std::vector<Value*>& args,
        const std::vector<Type*>& instParamTypes, bool varArgs, const Expression& expr, const Function& topLevelFunc);
    FuncType* CalculateInstFuncType(
        FuncType& originalFuncType, const std::vector<Type*>& instantiatedTypeArgs,
        const std::vector<GenericType*>& genericTypeParams, Type* instOuterType);
    bool CheckInstantiatedTypeArgs(const std::vector<Type*>& instantiatedTypeArgs,
        const std::vector<GenericType*>& genericTypeParams, const Expression& expr, const Function& topLevelFunc);
    bool CheckApplyThisType(
        const Value& callee, const Type* thisType, const Expression& expr, const Function& topLevelFunc);
    void CheckApplyFuncRetValue(const Type& instRetType, const Expression& expr, const Function& topLevelFunc);
    void CheckInvoke(const Invoke& expr, const Function& topLevelFunc);
    bool CheckInvokeThisType(
        Type& objType, const Type* thisType, const Expression& expr, const Function& topLevelFunc);
    bool CheckVirtualMethod(
        const VirMethodFullContext& methodCtx, const Expression& expr, const Function& topLevelFunc);
    const std::vector<VirtualMethodInfo>* CheckVTableExist(
        const Type& thisType, const ClassType& srcParentType, const Expression& expr, const Function& topLevelFunc);
    const std::vector<VirtualMethodInfo>* CheckVTableExist(
        const BuiltinType& thisType, const ClassType& srcParentType);
    const std::vector<VirtualMethodInfo>* CheckVTableExist(const CustomType& thisType, const ClassType& srcParentType);
    const std::vector<VirtualMethodInfo>* CheckVTableExist(
        const ClassType& srcParentType, const Function& topLevelFunc);
    const std::vector<VirtualMethodInfo>* CheckVTableExist(
        const GenericType& thisType, const ClassType& srcParentType);
    bool CheckVirtualMethodFuncType(const FuncType& declaredType,
        const FuncType& callSiteType, const std::string& errMsgBase, const Function& topLevelFunc);
    void CheckInvokeFuncArgs(const std::vector<Value*>& args,
        const std::vector<Type*>& originalParamTypes, const Expression& expr, const Function& topLevelFunc);
    void CheckInvokeStatic(const InvokeStatic& expr, const Function& topLevelFunc);
    void CheckInstanceOf(const InstanceOf& expr, const Function& topLevelFunc);
    void CheckTypeCast(const TypeCast& expr, const Function& topLevelFunc);
    void CheckGetException(const GetException& expr, const Function& topLevelFunc);
    void CheckSpawn(const Spawn& expr, const Function& topLevelFunc);
    void CheckRawArrayAllocate(const RawArrayAllocate& expr, const Function& topLevelFunc);
    void CheckRawArrayLiteralInit(const RawArrayLiteralInit& expr, const Function& topLevelFunc);
    void CheckRawArrayInitByValue(const RawArrayInitByValue& expr, const Function& topLevelFunc);
    void CheckVArray(const VArray& expr, const Function& topLevelFunc);
    void CheckVArrayBuilder(const VArrayBuilder& expr, const Function& topLevelFunc);
    void CheckIntrinsic(const Intrinsic& expr, const Function& topLevelFunc);
    void CheckBox(const Box& expr, const Function& topLevelFunc);
    void CheckUnBox(const UnBoxToValue& expr, const Function& topLevelFunc);
    void CheckCastToGeneric(const CastToGeneric& expr, const Function& topLevelFunc);
    void CheckCastToConcrete(const CastToConcrete& expr, const Function& topLevelFunc);
    void CheckGetInstantiateValue(const GetInstantiateValue& expr, const Function& topLevelFunc);
    void CheckUnBoxToRef(const UnBoxToRef& expr, const Function& topLevelFunc);
    void CheckGetRTTI(const GetRTTI& expr, const Function& topLevelFunc);
    void CheckGetRTTIStatic(const GetRTTIStatic& expr, const Function& topLevelFunc);
    void CheckStartRegion(const StartRegion& expr, const Function& topLevelFunc);
    void CheckEndRegion(const EndRegion& expr, const Function& topLevelFunc);
    bool CheckThisTypeIsEqualOrSubTypeOfFuncParentType(
        Type& thisType, const Function& func, const Expression& expr, const Function& topLevelFunc);
    void CheckInout(const IntrinsicBase& expr, const Function& topLevelFunc);
    void CheckInoutOpSrc(const Value& op, const IntrinsicBase& expr, const Function& topLevelFunc);

    // ===--------------------------------------------------------------------===//
    // Check Expressions
    // ===--------------------------------------------------------------------===//
    void CheckExpression(const Expression& expr, const Function& topLevelFunc);
    /**
     * @brief terminator can't jump to another block group
     */
    void CheckTerminatorJump(const Expression& terminator, const Function& topLevelFunc);

    // ===--------------------------------------------------------------------===//
    // Utils
    // ===--------------------------------------------------------------------===//
    void OverflowStrategyMustBeValid(
        const OverflowStrategy& ofs, const Expression& expr, const Function& topLevelFunc);
    bool CheckTypeIsValid(
        const Type& type, const std::string& typeName, const Expression& expr, const Function& topLevelFunc);
    bool OperandNumIsEqual(size_t expectedNum, const Expression& expr, const Function& topLevelFunc);
    bool OperandNumIsEqual(
        const std::vector<size_t>& expectedNum, const Expression& expr, const Function& topLevelFunc);
    bool SuccessorNumIsEqual(size_t expectedNum, const Expression& expr, const Function& topLevelFunc);
    bool OperandNumAtLeast(size_t expectedNum, const Expression& expr, const Function& topLevelFunc);
    bool SuccessorNumAtLeast(size_t expectedNum, const Expression& expr, const Function& topLevelFunc);
    void ShouldNotHaveResult(const Expression& expr, const Function& topLevelFunc);
    bool CheckHaveResult(const Expression& expr, const Function& topLevelFunc);
    bool GenericTypeIsInContainer(const Type& type, const std::vector<GenericType*>& container);

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