// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_IRBUILDER2_H
#define CANGJIE_IRBUILDER2_H

#include "llvm/IR/IRBuilder.h"

#include "Base/CHIRExprWrapper.h"
#include "CGModule.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie {
namespace CHIR {
class Type;
}
namespace CodeGen {
class CGModule;
class CGContext;
class CGValue;
class CGFunction;
class CGType;
class CHIRIntrinsicWrapper;
using LLVMIRBuilder2 = llvm::IRBuilder<>;

class IRBuilder2 : public LLVMIRBuilder2 {
public:
    explicit IRBuilder2(CGModule& cgMod);
    explicit IRBuilder2(CGModule& cgMod, llvm::BasicBlock* theBB) : LLVMIRBuilder2(theBB), cgMod(cgMod)
    {
        if (cgMod.GetCGContext().GetCompileOptions().fastMathMode) {
            llvm::FastMathFlags fmf;
            fmf.setFast(true);
            setFastMathFlags(fmf);
        }
    }

    void SetCHIRExpr(const CHIRExprWrapper* chirExprWrapper)
    {
        if (cgMod.GetCGContext().GetCompileOptions().disableInstantiation) {
            this->chirExpr = chirExprWrapper;
            this->cgFunction = cgMod.GetOrInsertCGFunction(chirExprWrapper->GetTopLevelFunc());
        }
    }

    inline void SetInsertCGFunction(const CGFunction& cgFunc)
    {
        this->cgFunction = &cgFunc;
    }

    const CGFunction* GetInsertCGFunction() const
    {
        return cgFunction;
    }

    inline CGModule& GetCGModule() const
    {
        return cgMod;
    }

    CGContext& GetCGContext() const;

    llvm::LLVMContext& GetLLVMContext() const;

    inline llvm::Module* GetLLVMModule() const
    {
        return cgMod.GetLLVMModule();
    }

    inline llvm::Value* GetTrue() const
    {
        return llvm::ConstantInt::getTrue(llvm::Type::getInt1Ty(GetLLVMContext()));
    }

    inline llvm::Value* GetFalse() const
    {
        return llvm::ConstantInt::getFalse(llvm::Type::getInt1Ty(GetLLVMContext()));
    }

    llvm::Function* GetInsertFunction() const
    {
        auto bb = GetInsertBlock();
        return bb != nullptr ? bb->getParent() : nullptr;
    }

    llvm::AllocaInst* CreateEntryAlloca(
        llvm::Type* type, llvm::Value* arraySize = nullptr, const llvm::Twine& name = "");
    llvm::Instruction* CreateEntryAlloca(const CGType& cgType, const llvm::Twine& name = "");

    llvm::Value* CreateLoad(llvm::Type* elementType, llvm::Value* addr, const llvm::Twine& name = "");

    llvm::Value* CreateLoad(const CGValue& cgVal, const llvm::Twine& name = "");

    inline llvm::StoreInst* CreateStore(llvm::Value* val, llvm::Value* addr, bool isVolatile = false)
    {
        return LLVMIRBuilder2::CreateStore(val, addr, isVolatile);
    }

    llvm::StoreInst* CreateStore(llvm::Value* val, const CGValue& cgDestAddr, bool isVolatile = false);
    llvm::Instruction* CreateStore(
        llvm::Value* val, llvm::Value* ptr, const Cangjie::CHIR::Type* type, bool isVolatile = false);

    llvm::Instruction* CreateStore(const CGValue& cgVal, const CGValue& cgDestAddr, CHIR::Type* boxType = nullptr);

    CGValue CreateGEP(const CGValue& cgVal, const std::vector<uint64_t>& idxList, const llvm::Twine& name = "");
    llvm::Value* CreateGEP(
        llvm::Type* type, llvm::Value* value, llvm::ArrayRef<llvm::Value*> place, const llvm::Twine& name = "");

    llvm::Value* CreateStructGEP(llvm::Type* ty, llvm::Value* ptr, unsigned idx, const llvm::Twine& name = "");
    llvm::Value* CreateVArrayGEP(const CGValue* varrayPtr, std::vector<llvm::Value*>& idxList, const llvm::Twine& name);

    inline llvm::BranchInst* CreateBr(llvm::BasicBlock* dest)
    {
        return LLVMIRBuilder2::CreateBr(dest);
    }

    inline llvm::BranchInst* CreateCondBr(
        llvm::Value* cond, llvm::BasicBlock* trueBranch, llvm::BasicBlock* falseBranch)
    {
        CJC_ASSERT(!GetInsertBlock()->getTerminator());
        return LLVMIRBuilder2::CreateCondBr(cond, trueBranch, falseBranch);
    }

    llvm::BasicBlock* CreateEntryBasicBlock(llvm::Function* parent, const std::string& name) const;

    std::vector<llvm::BasicBlock*> CreateAndInsertBasicBlocks(
        const std::initializer_list<std::string>& basicBlocksName) const;

private:
    inline llvm::CallInst* CreateCall(llvm::Function* callee, llvm::ArrayRef<llvm::Value*> args = llvm::None,
        const llvm::Twine& name = "", llvm::MDNode* fpMathTag = nullptr)
    {
        return LLVMIRBuilder2::CreateCall(callee, args, name, fpMathTag);
    }
    inline llvm::CallInst* CreateCall(llvm::FunctionType* fTy, llvm::Value* callee,
        llvm::ArrayRef<llvm::Value*> args = llvm::None, const llvm::Twine& name = "", llvm::MDNode* fpMathTag = nullptr)
    {
        if (auto f = llvm::dyn_cast<llvm::Function>(callee)) {
            return CreateCall(f, args, name, fpMathTag);
        }
        return LLVMIRBuilder2::CreateCall(fTy, callee, args, name, fpMathTag);
    }

    inline llvm::InvokeInst* CreateInvoke(llvm::Function* callee, llvm::BasicBlock* normalDest,
        llvm::BasicBlock* unwindDest, llvm::ArrayRef<llvm::Value*> args = llvm::None, const llvm::Twine& name = "")
    {
        return LLVMIRBuilder2::CreateInvoke(callee, normalDest, unwindDest, args, name);
    }
    inline llvm::InvokeInst* CreateInvoke(llvm::FunctionType* fTy, llvm::Value* callee, llvm::BasicBlock* normalDest,
        llvm::BasicBlock* unwindDest, llvm::ArrayRef<llvm::Value*> args = llvm::None, const llvm::Twine& name = "")
    {
        if (auto f = llvm::dyn_cast<llvm::Function>(callee)) {
            return CreateInvoke(f, normalDest, unwindDest, args, name);
        }
        return LLVMIRBuilder2::CreateInvoke(fTy, callee, normalDest, unwindDest, args, name);
    }

public:
    llvm::CallBase* CreateCallOrInvoke(llvm::FunctionType* fTy, llvm::Value* callee, std::vector<llvm::Value*> args);
    llvm::CallBase* CreateCallOrInvoke(llvm::Function* callee, const llvm::ArrayRef<llvm::Value*> args = llvm::None);
    llvm::Value* CreateCallOrInvoke(const CGFunctionType& calleeType, llvm::Value* callee, std::vector<CGValue*> args,
        bool isClosureCall = false, llvm::Value* thisTypeInfo = nullptr);
    llvm::Value* CreateCallOrInvoke(
        const CGFunction& callee, std::vector<CGValue*> args = {}, bool isClosureCall = false)
    {
        return CreateCallOrInvoke(
            *dynamic_cast<const CGFunctionType*>(callee.GetCGType()), callee.GetRawFunction(), args, isClosureCall);
    }

    void EmitDeclare(const CHIR::Debug& debugNode, bool pointerWrapper = false);
    void CreateLocalVarPointer(const CHIR::Debug& debugNode, const CGValue& cgValue);
    void CreateUnBoxDeclare(const CHIR::Debug& debugNode, const CGValue& cgValue);
    void CreateEnvDeclare(const CHIR::GetElementRef& getEleRef, llvm::Value* gep);
    void EmitLocation(const CHIRExprWrapper& debugNode, const bool removable = false);
    llvm::Type* HandleArgPointerType(const CGType& argType) const;
    void CreateValuePointer(const CHIR::Debug& debugNode, const CGValue& cgValue);
    void CreateBoxedValueForValueType(const CHIR::Debug& debugNode, const CGValue& cgValue);
    void CreateLoadInstForParameter(const CHIR::Expression& chirExpression);
    void CreateGenericParaDeclare(const CGFunction& cgFunc);

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    llvm::Value* CreateBitCast(llvm::Value* value, llvm::Type* destTy, const llvm::Twine& name = "")
    {
        auto res = LLVMIRBuilder2::CreateBitCast(value, destTy, name);
        if (auto basePtr = GetCGContext().GetBasePtrOf(value)) {
            CJC_ASSERT(value->getType()->getPointerAddressSpace() == 1U);
            GetCGContext().SetBasePtr(res, basePtr);
        }
        return res;
    }

    /** Platform dependent */
    llvm::Type* GetSizetLLVMType() const;
    size_t GetPtrSize() const;
    size_t GetPayloadOffset() const;
    CHIR::Type::TypeKind GetTypeKindFromType(const CHIR::Type& ty) const;
    
    /** GC Write Barrier Intrinsics */
    bool IsGlobalVariableBasePtr(llvm::Value* val) const;
    llvm::Instruction* CallGCRead(std::vector<llvm::Value*> args);
    llvm::Instruction* CallGCReadWeakRef(std::vector<llvm::Value*> args);
    llvm::Instruction* CallGCReadAgg(std::vector<llvm::Value*> args);
    llvm::Instruction* CallGCReadStaticRef(const std::vector<llvm::Value*>& args);
    llvm::Instruction* CallGCReadStaticAgg(llvm::StructType* type, std::vector<llvm::Value*> args);
    llvm::Instruction* CallGCWrite(std::vector<llvm::Value*> args);
    llvm::Instruction* CallGCWriteAgg(std::vector<llvm::Value*> args);
    llvm::Instruction* CallGCWriteStaticRef(const std::vector<llvm::Value*>& args);
    llvm::Instruction* CallGCWriteStaticAgg(llvm::StructType* type, std::vector<llvm::Value*> args);
    llvm::Instruction* CreateWriteBarrierForGlobalVariable(const CGValue& value, const CGValue& globalVar);

    /**
     * @brief Use `llvm.memset` to zero initialize the memory where a
     * struct pointer points to.
     */
    llvm::CallInst* CreateMemsetStructWith0(llvm::AllocaInst* addr);

    /**
     * Since `llvm.memset` will be optimized if the llvm finds that the
     * object to be memset-ed is completely initialized later.
     * The FE and BE conventions use a customized `llvm.cj_memset`, and
     * the BE will ensure that it can be retained.
     */
    llvm::CallInst* CreateCJMemSetStructWith0(llvm::AllocaInst* addr);
#endif

    /**
     * Do not create allocations these ways.
     * Please use CreateEntryAlloca instead.
     */
    llvm::AllocaInst* CreateAlloca(
        llvm::Type* type, unsigned addrSpace, llvm::Value* ArraySize = nullptr, const llvm::Twine& Name = "") = delete;
    llvm::AllocaInst* CreateAlloca(
        llvm::Type* type, llvm::Value* arraySize = nullptr, const llvm::Twine& name = "") = delete;

    /**
     * @brief Create a null value of a specific CHIR type.
     */
    llvm::Value* CreateNullValue(const CHIR::Type& ty);

    void CreateOverflowOrArithmeticException(const std::string& ident, bool isOverflow = true);
    void CreateNegativeArraySizeException();
    void CreateOutOfBoundException();
    void CreateSpawnException();
    void CreateRunTimeInvalidGenericParamException();
    void CreateExecAtExitCallbacksCall();
    void CreateGetCommandLineArgsCall(const std::vector<llvm::Value*>& args);
    void CreateHandleExceptionCall(const std::vector<llvm::Value*>& args);
    void CreateSetRuntimeCJThreadHandleCall(const std::vector<llvm::Value*>& args);
    void CreateEPrintlnCall(const std::string& eMsg);

    llvm::AtomicCmpXchgInst* CreateAtomicCmpXchg(llvm::Value* Ptr, llvm::Value* Cmp, llvm::Value* New,
        llvm::AtomicOrdering SuccessOrdering, llvm::AtomicOrdering FailureOrdering,
        llvm::SyncScope::ID SSID = llvm::SyncScope::System)
    {
        return LLVMIRBuilder2::CreateAtomicCmpXchg(
            Ptr, Cmp, New, llvm::MaybeAlign(), SuccessOrdering, FailureOrdering, SSID);
    }

    llvm::Value* CallIntrinsicRef2Null(llvm::Value* value);
    llvm::Value* CreateStringLiteral(const std::string& str);

    void CallArrayInit(
        llvm::Value* arrPtr, llvm::Value* arrayLen, llvm::Value* elemValue, const CHIR::RawArrayType& arrTy);

    /// Intrinsics
    struct CodeGenFieldInfo {
        std::string className{""};
        std::string qualifiedName{""};
        std::string fieldName{""};
        const CHIR::Type* fieldChirType;
        CGType* fieldType{nullptr};
        llvm::Value* instance{nullptr};
        const CHIR::Type& baseTy;
        CodeGenFieldInfo(const std::string className, const std::string& qualifiedName, const std::string fieldName,
            const CHIR::Type* fieldChirType, CGType* fieldType, llvm::Value* instance, const CHIR::Type& baseTy)
            : className(className),
              qualifiedName(qualifiedName),
              fieldName(fieldName),
              fieldChirType(fieldChirType),
              fieldType(fieldType),
              instance(instance),
              baseTy(baseTy)
        {
        }
    };

    llvm::Value* CallIntrinsic(
        const CHIRIntrinsicWrapper& intrinsic, const std::vector<CGValue*>& parameters, bool isC = false);
    llvm::Value* CallIntrinsic(const CHIRIntrinsicWrapper& intrinsic, const std::vector<llvm::Value*>& args,
        const std::vector<llvm::Type*>& tys = {});
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    llvm::Value* CallIntrinsicNull() const;
#endif
    llvm::Value* CallIntrinsicIsNull(llvm::Value* value);
    llvm::Value* CallIntrinsicIsNonNull(llvm::Value* value);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    llvm::Value* CallIntrinsicFloatToIntegerSat(llvm::Type& destTy, llvm::Value& srcValue, bool isSigned);
#endif
    llvm::Value* CallSetLocation();
    ///*----------------- Spawn related --------------------//
    // Call runtime API to create a thread.
    llvm::Value* CallSpawnIntrinsic(CGValue& obj, CGFunction& cgFunc, std::optional<CGValue*>& threadCtx,
        bool spawnWithFuture, llvm::Value* futureTI = nullptr);
    ///*----------------- Sync related --------------------//
    llvm::Value* CallSyncIntrinsics(const CHIRIntrinsicWrapper& intrinsic, const std::vector<CGValue*>& parameters);
    ///*----------------- Overflow related --------------------//
    llvm::Value* GenerateOverflowCheckedFunc(
        CHIR::ExprKind opKind, const CHIR::Type& ty, std::vector<llvm::Value*>& argGenValues);
    llvm::Value* GenerateDivLikeCheck(llvm::Value* dividend, llvm::Value* divisor, bool isSigned, bool isDiv);
    llvm::Value* GenerateOverflowSaturatingFunc(
        CHIR::ExprKind opKind, const CHIR::Type& ty, const std::vector<llvm::Value*>& argGenValues);
    llvm::Value* GenerateOverflowWrappingFunc(
        CHIR::ExprKind opKind, const CHIR::Type& ty, std::vector<llvm::Value*>& argGenValues);
    ///*----------------- Exception related --------------------//
    void CallExceptionIntrinsicThrow(llvm::Value* exceptionValue);
    void CallExceptionIntrinsicThrow(CGValue* exceptionValue)
    {
        CallExceptionIntrinsicThrow(exceptionValue->GetRawValue());
    }
    llvm::Value* CallExceptionIntrinsicGetExceptionValue();
    llvm::Value* CallPostThrowExceptionIntrinsic(llvm::Value* exceptionValue);
    ///*----------------- Runtime related --------------------//
    llvm::Value* GenerateCallExpectFunction(CGType* cgType, llvm::Value* val, llvm::Value* expectVal);
    llvm::Value* CallRuntimeIntrinsics(const CHIRIntrinsicWrapper& syscall, const std::vector<CGValue*>& parameters);
    ///*----------------- Fill Or Get StackTrace related --------------------//
    llvm::Value* CallStackTraceIntrinsic(const CHIRIntrinsicWrapper& syscall, std::vector<CGValue*>& parameters);
    ///*----------------- ThreadInfo related --------------//
    llvm::Value* CallThreadInfoIntrinsic(const CHIRIntrinsicWrapper& syscall, std::vector<CGValue*>& parameters);
    ///*----------------- Math related --------------------//
    llvm::Value* CallMathIntrinsics(const CHIRIntrinsicWrapper& intrinsic, std::vector<llvm::Value*>& parameters);
    ///*----------------- zeroValue related --------------------//
    llvm::Value* CallIntrinsicForUninitialized(const CHIR::Type& ty);
    ///*----------------- Array related --------------------//
    llvm::Value* CallArrayIntrinsicGetSize(llvm::Value* array);
    void CallArrayIntrinsicSet(
        const CHIR::RawArrayType& arrTy, llvm::Value* array, llvm::Value* index, CGValue& cgVal, bool isChecked);
    llvm::Value* CallArrayIntrinsicGet(
        const CHIR::RawArrayType& arrTy, llvm::Value* array, llvm::Value* index, bool isChecked);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    llvm::Value* GetArrayElementAddr(
        const CHIR::RawArrayType& arrTy, llvm::Value* array, llvm::Value* index, bool isChecked);
#endif
    llvm::Value* CallArrayIntrinsicInitWithContent(const CHIR::RawArrayLiteralInit& arrayLiteralInit);
    /**
     * For CJNative, call gc write barrier if store ref to ref object's ref field.
     */
    void CreateRefStore(CGValue* cgValue, llvm::Value* basePtr, llvm::Value* place, bool isBaseObjStruct);
    ///*----------------- unit test related --------------------//
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    ///*----------------- FFI OHOS related --------------------//
    llvm::Value* CallInteropIntrinsics(const CHIRIntrinsicWrapper& intrinsic, const std::vector<CGValue*>& parameters);
    ///*----------------- Func RefEq related --------------------//
    llvm::Value* CallIntrinsicFuncRefEq(std::vector<CGValue*> parameters);
    ///*----------------- VArray related --------------------//
    void CallVArrayIntrinsicIndexCheck(const CGValue* arrayPtr, std::vector<llvm::Value*>& index);
    void CreateVArrayStore(CGValue* cgValue, llvm::Value* place);
    ///*----------------- Atomic related --------------------//
    llvm::Value* CallAtomicIntrinsics(const CHIRIntrinsicWrapper& intrinsic, const std::vector<CGValue*>& args);
    llvm::Value* CallAtomicPrimitiveIntrinsics(
        const CHIRIntrinsicWrapper& intrinsic, const std::vector<CGValue*>& args, llvm::Value* fieldPtr);

    ///*----------------- Array related --------------------//
    void CallArrayIntrinsicIndexCheck(llvm::Value* array, llvm::Value* index);

    ///*----------------- Vector operation related --------------------//
    llvm::Value* EmitVectorCompare32(llvm::Value* arr1, llvm::Value* offset1, llvm::Value* arr2, llvm::Value* offset2);
    llvm::Value* EmitVectorIndexByte32(llvm::Value* arr, llvm::Value* offset, llvm::Value* byte);

    // Since the parameters should be of type CGValue, and llvmValue cannot be converted to CGValue,
    //  which comes from the llvm API, put them into nonStructParams. They MUST NOT be of Structure Type.
    llvm::Value* CallIntrinsicFunction(llvm::Type* retType, const std::string& name,
        const std::vector<CGValue*>& parameters, const std::vector<std::string>& attributes = {});
#endif
    ///*----------------- VArray related --------------------//
    llvm::Value* VArrayInitedByLambda(llvm::Value* varrayLen, CGValue& autoEnv, const CHIR::VArrayType& vArrayType);
    llvm::Value* VArrayInitedByItem(llvm::Value* varrayLen, const CGValue& cgVal, const CHIR::Type& vArrayType);
    ///*----------------- RawData related --------------------//
    llvm::Value* AcquireRawData(const CHIRIntrinsicWrapper& intrinsic);
    llvm::Value* ReleaseRawData(const CHIRIntrinsicWrapper& intrinsic);
    ///*----------------- Class related --------------------//
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    llvm::Value* CallClassIntrinsicAlloc(const CHIR::Type& type);
    llvm::Instruction* CallClassIntrinsicAlloc(const std::vector<llvm::Value*>& parameters);
#endif
    llvm::Value* CallClassIntrinsicInstanceOf(llvm::Value* instance, const CHIR::Type* targetTy);
    llvm::Value* CallArrayIntrinsicAllocWithConstantContent(llvm::Value* array, const std::vector<CGValue*>& args,
        const CHIR::RawArrayType& arrTy, const std::string& serialized);
    llvm::Value* CreateEnumGEP(const CHIR::Field& field);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    llvm::Value* AllocateArray(const CHIR::RawArrayType& rawArrayType, llvm::Value* length);
    llvm::Function* GetArrayNonGenericElemMalloc() const;
    llvm::Function* GetArrayGenericElemMalloc() const;
    llvm::Value* InitArrayFilledWithConstant(llvm::Value* array, const CHIR::RawArrayType& arrTy,
        const std::vector<llvm::Constant*>& arrayConstantElems, const std::string& serialized);
    void CreateCopyTo(ArrayCopyToInfo arrayCopyToInfo);

    // Universal Generic Begin
    llvm::Value* GetSizeFromTypeInfo(llvm::Value* typeInfo);
    llvm::Value* GetLayoutSize_32(const CHIR::Type& type);
    llvm::Value* GetLayoutSize_64(const CHIR::Type& type);
    llvm::Value* GetSize_32(const CHIR::Type& type);
    llvm::Value* GetSize_64(const CHIR::Type& type);
    llvm::Value* GetSize_Native(const CHIR::Type& type);
    llvm::Value* GetAlign(const CHIR::Type& type, llvm::Type* targetType);
    llvm::Value* GetUUIDFromTypeInfo(llvm::Value* typeInfo);
    llvm::Value* GetTypeKindFromTypeInfo(llvm::Value* typeInfo);
    llvm::Value* GetTypeForTypeParameter(llvm::Value* typeInfo);
    llvm::Value* GetTypeArgsFromTypeInfo(llvm::Value* typeInfo);
    llvm::Value* GetTypeInfoFromTiArray(llvm::Value* tiArray, std::size_t index, const std::string& name = "");
    llvm::Value* GetTypeInfoFromObject(llvm::Value* obj);
    llvm::Value* GetPayloadFromObject(llvm::Value* obj);

    // `canChangeBB` is just used in `CJNativeExtensionDef.cpp`, it is a workaround for now :(
    // This parameter should be removed at some point!
    llvm::Value* CreateTypeInfo(const CHIR::Type& type,
        const std::unordered_map<const CHIR::Type*, std::function<llvm::Value*(IRBuilder2&)>>& tiMap,
        bool canChangeBB = true);
    llvm::Value* CreateTypeInfo(const CHIR::Type& type, bool canChangeBB = true);
    llvm::Value* CreateTypeInfo(const CHIR::Type* type, bool canChangeBB = true);

    llvm::Value* CreateTypeInfoArray(const std::vector<CHIR::Type*>& types,
        const std::unordered_map<const CHIR::Type*, std::function<llvm::Value*(IRBuilder2&)>>& map);
    llvm::Value* CreateTypeInfoArray(const std::vector<CHIR::Type*>& types);

    llvm::Instruction* CallIntrinsicGetTypeInfo(const std::vector<llvm::Value*>& parameters);
    llvm::Instruction* CallIntrinsicGetFieldOffset(const std::vector<llvm::Value*>& parameters);
    llvm::Instruction* CallIntrinsicIsSubtype(const std::vector<llvm::Value*>& parameters);
    llvm::Instruction* CallIntrinsicIsTupleTypeOf(const std::vector<llvm::Value*>& parameters);
    llvm::Instruction* CallIntrinsicIsTypeEqualTo(const std::vector<llvm::Value*>& parameters);
    llvm::Instruction* CallIntrinsicAllocaGeneric(const std::vector<llvm::Value*>& parameters);
    llvm::Instruction* CallIntrinsicGCWriteGeneric(const std::vector<llvm::Value*>& parameters);
    llvm::Instruction* CallGCWriteGenericPayload(const std::vector<llvm::Value*>& parameters);
    llvm::Instruction* CallGCReadGeneric(const std::vector<llvm::Value*>& parameters);
    llvm::Instruction* CallIntrinsicMTable(const std::vector<llvm::Value*>& parameters);
    llvm::Instruction* CallIntrinsicGetVTableFunc(llvm::Value* ti, llvm::Value* introTypeIdx, llvm::Value* funcOffset);
    llvm::Instruction* CallIntrinsicAssignGeneric(const std::vector<llvm::Value*>& parameters);

    llvm::Value* CreateTypeInfoIsReferenceCall(llvm::Value* ti);
    llvm::Value* CreateTypeInfoIsReferenceCall(const CHIR::Type& chirType);
    // Universal Generic End

    /**
     * ** MOTIVATION: **
     * Yield a specialized basic block as the entry or behind `allocas` to prepare
     * all TypeInfo in advance, to avoid creating the same typeinfo in different
     * blocks repeatedly.
     * ** NOTE: **
     * This may seem like an increase in execution time at the beginning of the
     * function, but overall, you have a better chance of getting both code size and
     * performance benefits.
     * ** USAGE: **
     * This method is used to set the insert point of a builder to this specialized
     * basic block.
     */
    void SetInsertPointForPreparingTypeInfo();
#endif
private:
    void CallIntrinsicIndexCheck(llvm::Value* len, llvm::Value* index);
    void SetInsertPointToAllocas(llvm::Function* curFunc);
    llvm::Function* GetExceptionIntrinsicThrow() const;

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    llvm::Value* FixFuncArg(const CGValue& srcValue, const CGType& destType, bool isThisArgInStructMut = false);
#endif
    llvm::Value* GetEnumTag(const CHIR::Field& field);
    llvm::Value* GetEnumAssociatedValue(const CHIR::Field& field);

private:
    CGModule& cgMod;
    const CHIRExprWrapper* chirExpr{nullptr};
    const CGFunction* cgFunction{nullptr};
};

class TemporarilySetDebugLocInThisScope {
public:
    TemporarilySetDebugLocInThisScope(LLVMIRBuilder2& irBuilder, llvm::DebugLoc newLocation)
        : irBuilder(irBuilder), oldLocation(irBuilder.getCurrentDebugLocation())
    {
        irBuilder.SetCurrentDebugLocation(newLocation);
    }

    ~TemporarilySetDebugLocInThisScope()
    {
        irBuilder.SetCurrentDebugLocation(oldLocation);
    }

private:
    LLVMIRBuilder2& irBuilder;
    llvm::DebugLoc oldLocation;
};
} // namespace CodeGen
} // namespace Cangjie
#endif // CANGJIE_IRBUILDER2_H
