// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file realizes generate Intrinsic APIs for codegen.
 */

#include "Base/IntrinsicsDispatcher.h"

#include "Base/OverflowDispatcher.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/ToStringUtils.h"
#include "cangjie/CHIR/Value.h"

namespace Cangjie {
namespace CodeGen {

namespace {
CGIntrinsicKind GetCGIntrinsicKind(CHIR::IntrinsicKind intrinsicKind)
{
    switch (intrinsicKind) {
        case CHIR::IntrinsicKind::CG_UNSAFE_BEGIN:
        case CHIR::IntrinsicKind::CG_UNSAFE_END:
            return CGIntrinsicKind::UNSAFE_MARK;
        case CHIR::IntrinsicKind::CPOINTER_INIT0:
        case CHIR::IntrinsicKind::CPOINTER_INIT1:
            return CGIntrinsicKind::CPOINTER_INIT;
        case CHIR::IntrinsicKind::CSTRING_INIT:
            return CGIntrinsicKind::CSTRING_INIT;
        case CHIR::IntrinsicKind::INOUT_PARAM:
            return CGIntrinsicKind::INOUT_PARAM;
        case CHIR::IntrinsicKind::CROSS_ACCESS_BARRIER:
        case CHIR::IntrinsicKind::CREATE_EXPORT_HANDLE:
        case CHIR::IntrinsicKind::GET_EXPORTED_REF:
        case CHIR::IntrinsicKind::REMOVE_EXPORTED_REF:
            return CGIntrinsicKind::INTEROP;
        default:
            break;
    }
    if (IsOverflowIntrinsic(intrinsicKind)) {
        return CGIntrinsicKind::OVERFLOW_APPLY;
    } else if (IsArrayIntrinsic(intrinsicKind)) {
        return CGIntrinsicKind::ARRAY;
    } else if (IsSourceIntrinsic(intrinsicKind)) {
        return CGIntrinsicKind::SOURCE;
    } else if (IsBuiltinIntrinsic(intrinsicKind)) {
        return CGIntrinsicKind::BUILTIN;
    } else if (IsFutureIntrinsic(intrinsicKind)) {
        return CGIntrinsicKind::FUTURE;
    } else if (IsSyncIntrinsic(intrinsicKind)) {
        return CGIntrinsicKind::SYNC;
    } else if (IsMathIntrinsic(intrinsicKind)) {
        return CGIntrinsicKind::MATH;
    } else if (IsStackTraceIntrinsic(intrinsicKind)) {
        return CGIntrinsicKind::STACK_TRACE;
    } else if (IsReflectIntrinsic(intrinsicKind)) {
        return CGIntrinsicKind::REFLECT;
    } else if (IsVArrayIntrinsic(intrinsicKind)) {
        return CGIntrinsicKind::VARRAY;
    } else if (IsRuntimeIntrinsic(intrinsicKind)) {
        return CGIntrinsicKind::RUNTIME;
    } else if (IsExceptionCatchIntrinsic(intrinsicKind)) {
        return CGIntrinsicKind::EXCEPTION_CATCH;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    } else if (IsVectorIntrinsic(intrinsicKind)) {
        return CGIntrinsicKind::VECTOR;
    } else if (IsPreInitializeIntrinsic(intrinsicKind)) {
        return CGIntrinsicKind::PREINITIALIZE;
#endif
    }
    return CGIntrinsicKind::UNKNOWN;
}
} // namespace

std::vector<CGValue*> HandleSyscallIntrinsicArguments(
    const IRBuilder2& irBuilder, const std::vector<CHIR::Value*>& nodeArgs)
{
    std::vector<CGValue*> args;
    for (auto arg : nodeArgs) {
        auto argVal = (irBuilder.GetCGModule() | arg);
        args.push_back(argVal);
    }
    return args;
}

llvm::Value* GenerateExceptionCatchIntrinsics(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    switch (intrinsic.GetIntrinsicKind()) {
        case CHIR::BEGIN_CATCH: {
            auto exceptionValue = irBuilder.GetCGModule() | intrinsic.GetOperand(0);
            return irBuilder.CallPostThrowExceptionIntrinsic(exceptionValue->GetRawValue());
        }
        default:
            CJC_ASSERT(false && "unimplemented exception-catch intrinsic.");
            return nullptr;
    }
}

llvm::Value* GenerateArrayIndex(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic, bool isChecked)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto arrayValue = cgMod | intrinsic.GetOperand(0); // array
    auto indexValue = cgMod | intrinsic.GetOperand(1); // index
    if (arrayValue && indexValue) {
        auto arrTy = static_cast<CHIR::RawArrayType*>(intrinsic.GetOperand(0)->GetType()->GetTypeArgs()[0]);
        CJC_NULLPTR_CHECK(arrTy);
        if (intrinsic.GetNumOfOperands() >= 3) { // An argument length of at least 3 is required
            // set the value at an index in an array using arr[index] = value
            auto value = cgMod | intrinsic.GetOperand(2);
            irBuilder.CallArrayIntrinsicSet(*arrTy, **arrayValue, **indexValue, *value, isChecked);
            return nullptr;
        } else {
            // get the value at an index in an array using arr[index]
            return irBuilder.CallArrayIntrinsicGet(*arrTy, **arrayValue, **indexValue, isChecked);
        }
    }
    return nullptr;
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
llvm::Value* GenerateVarrayIntrinsics(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    auto parameters = HandleSyscallIntrinsicArguments(irBuilder, intrinsic.GetOperands());
    auto varrPtr = parameters[0];
    bool needIdxCheck = intrinsic.Get<CHIR::NeedCheckArrayBound>();
    switch (intrinsic.GetIntrinsicKind()) {
        case CHIR::VARRAY_SET: {
            // varraySet(ref(varray), value, index)
            std::vector<llvm::Value*> idxList{};
            for (size_t i = 2; i < parameters.size(); i++) {
                idxList.emplace_back(parameters[i]->GetRawValue());
            }
            if (needIdxCheck) {
                irBuilder.CallVArrayIntrinsicIndexCheck(varrPtr, idxList);
            }
            auto elePtr = irBuilder.CreateVArrayGEP(varrPtr, idxList, "varr.idx.set.gep");
            irBuilder.CreateVArrayStore(parameters[1], elePtr);
            return nullptr;
        }
        case CHIR::VARRAY_GET: {
            // "varrayGet"(ref(varray), index1, index2...)
            std::vector<llvm::Value*> idxList{};
            for (size_t i = 1; i < parameters.size(); i++) {
                idxList.emplace_back(parameters[i]->GetRawValue());
            }
            irBuilder.CallVArrayIntrinsicIndexCheck(varrPtr, idxList);
            auto elePtr = irBuilder.CreateVArrayGEP(varrPtr, idxList, "varr.idx.get.gep");
            // If element type is varray/struct in a varray, it should return element pointer.
            // only when it be used we need to load it.
            auto retChirType = intrinsic.GetResult()->GetType();
            auto retCGType = CGType::GetOrCreate(irBuilder.GetCGModule(), retChirType);
            return irBuilder.CreateLoad(retCGType->GetLLVMType(), elePtr);
        }
        default:
            CJC_ASSERT(false && "unimplemented varray intrinsic.");
            return nullptr;
    }
}

// Get the address of an element of array.
llvm::Value* GenerateArrayGetElemRef(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic, bool isChecked)
{
    CJC_ASSERT(intrinsic.GetIntrinsicKind() == CHIR::IntrinsicKind::ARRAY_GET_REF_UNCHECKED);
    CJC_ASSERT(intrinsic.GetNumOfOperands() == 2U);
    auto& cgMod = irBuilder.GetCGModule();
    auto arrayCGValue = cgMod | intrinsic.GetOperand(0); // array
    auto indexCGValue = cgMod | intrinsic.GetOperand(1); // index
    CJC_ASSERT(arrayCGValue && indexCGValue);
    auto arrayVal = **arrayCGValue;
    auto indexVal = **indexCGValue;
    auto arrTy = StaticCast<CHIR::RawArrayType*>(intrinsic.GetOperand(0)->GetType()->GetTypeArgs()[0]);
    CJC_NULLPTR_CHECK(arrTy);
    if (!CGType::GetOrCreate(cgMod, arrTy)->GetSize()) {
        auto elemCGType = CGType::GetOrCreate(cgMod, arrTy->GetElementType());
        llvm::Value* offset = irBuilder.CreateMul(irBuilder.GetSize_64(elemCGType->GetOriginal()), indexVal);
        offset = irBuilder.CreateAdd(offset, irBuilder.getInt64(irBuilder.GetVoidPtrSize() + 8U)); // 8U:size of rawArray's len field
        auto elePtr = irBuilder.CreateInBoundsGEP(irBuilder.getInt8Ty(), arrayVal, offset);
        irBuilder.GetCGContext().SetBasePtr(elePtr, arrayVal);
        return irBuilder.CreateBitCast(
            elePtr, elemCGType->GetLLVMType()->getPointerTo(arrayVal->getType()->getPointerAddressSpace()));
    } else {
        return irBuilder.GetArrayElementAddr(*arrTy, arrayVal, indexVal, isChecked);
    }
}

/**
 * Create a memcpy or copyto by the specified ArrayCopyToInfo.
 * Since the restriction of `memcpy` and `copyto`, we have to split the array as small chunks if the array size >= 2G,
 * and `memcpy` or `copyto` each chunk.
 * var iterator = 0
 * while (iterator + 2G ) < copylen) {
 *      copyTo(src,                 // address of the source array (i8Ptr)
 *             dst,                 // address of the destination array (i8Ptr)
 *             srcArrPtr,           // address of the source array (arrayTypePtr)
 *             dstArrPtr,           // address of the destination array (arrayTypePtr)
 *             2G,                  // size to memcpy or copyto
 *             elemType,            // array element type
 *             srcIndex + iterator, // start index of source array
 *             dstIndex + iterator) // start index of destination array
 *      iterator += 2G
 * }
 * copyTo(src, dst, srcArrPtr, dstArrPtr, elemType, copylen - iterator, srcIndex + iterator ,dstIndex + iterator)
 */

void ArrayMemCpyOrCopyTo(IRBuilder2& irBuilder, const ArrayCopyToInfo& arrayCopyToInfo)
{
    auto copylen = arrayCopyToInfo.dataSize;
    // In order to avoid long IR, If array size less than or equal to INT32_MAX, quit early.
    if (auto constVal = llvm::dyn_cast<llvm::ConstantInt>(copylen);
        constVal && *constVal->getValue().getRawData() <= INT32_MAX) {
        irBuilder.CreateCopyTo(arrayCopyToInfo);
        return;
    }

    auto [whileLoopHeader, whileLoopBody, afterWhileLoop] = Vec2Tuple<3>(irBuilder.CreateAndInsertBasicBlocks(
        {GenNameForBB("arr.copy.start"), GenNameForBB("arr.copy.body"), GenNameForBB("arr.copy.end")}));

    constexpr uint64_t align = 8;
    constexpr uint64_t alignDown = INT32_MAX & ~(align - 1);
    const auto SIZE_OF_SINGLE_COPY = irBuilder.getInt64(alignDown);
    auto i64Ty = irBuilder.getInt64Ty();
    auto iterator = irBuilder.CreateEntryAlloca(i64Ty, nullptr, "iterator");
    (void)irBuilder.CreateStore(irBuilder.getInt64(0), iterator);
    (void)irBuilder.CreateBr(whileLoopHeader);

    // generate whileLoopHeader
    // `while(iterator + 2G) < copylen) {`
    irBuilder.SetInsertPoint(whileLoopHeader);
    auto cmpResult = irBuilder.CreateICmpSLT(
        irBuilder.CreateAdd(irBuilder.CreateLoad(i64Ty, iterator), SIZE_OF_SINGLE_COPY), copylen, "max.size");
    (void)irBuilder.CreateCondBr(cmpResult, whileLoopBody, afterWhileLoop);

    // generate whileLoopBody
    // `copyToFunc(src, dst, srcArrPtr, dstArrPtr, elemType, 2G, srcIndex + iterator, dstIndex + iterator)`
    irBuilder.SetInsertPoint(whileLoopBody);
    auto iteratorIndex = irBuilder.CreateLoad(i64Ty, iterator);
    irBuilder.CreateCopyTo(ArrayCopyToInfo(arrayCopyToInfo.srcBP, arrayCopyToInfo.dstBP, arrayCopyToInfo.srcArrPtr,
        arrayCopyToInfo.dstArrPtr, SIZE_OF_SINGLE_COPY, irBuilder.CreateAdd(iteratorIndex, arrayCopyToInfo.srcIndex),
        irBuilder.CreateAdd(iteratorIndex, arrayCopyToInfo.dstIndex), arrayCopyToInfo.elemType, SIZE_OF_SINGLE_COPY));
    // `iterator += 2G`
    auto newIterator = irBuilder.CreateAdd(iteratorIndex, SIZE_OF_SINGLE_COPY);
    (void)irBuilder.CreateStore(newIterator, iterator);
    (void)irBuilder.CreateBr(whileLoopHeader);

    // generate afterWhileLoop
    // `copyToFunc(src, dst, srcArrPtr, dstArrPtr, elemType, copylen - iterator, srcIndex + iterator ,dstIndex +
    // iterator)`
    irBuilder.SetInsertPoint(afterWhileLoop);
    auto index = irBuilder.CreateLoad(i64Ty, iterator);
    auto residualSize = irBuilder.CreateSub(copylen, index);
    irBuilder.CreateCopyTo(ArrayCopyToInfo(arrayCopyToInfo.srcBP, arrayCopyToInfo.dstBP, arrayCopyToInfo.srcArrPtr,
        arrayCopyToInfo.dstArrPtr, residualSize, irBuilder.CreateAdd(index, arrayCopyToInfo.srcIndex),
        irBuilder.CreateAdd(index, arrayCopyToInfo.dstIndex), arrayCopyToInfo.elemType, residualSize));
}

struct ArrayPosition {
    llvm::Value* array{nullptr};
    llvm::Value* index{nullptr};
};

void CallArrayIntrinsicCopyTo(IRBuilder2& irBuilder, const ArrayPosition& dst, const ArrayPosition& src,
    llvm::Value* copyLen, const CHIR::RawArrayType& arrTy)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto elemType = CGType::GetOrCreate(cgMod, arrTy.GetElementType());
    CJC_NULLPTR_CHECK(elemType);
    auto typeSize = irBuilder.GetSize_64(*arrTy.GetElementType());
    auto dataSize = irBuilder.CreateMul(copyLen, typeSize, "arr.data.len");
    auto arrayType = CGArrayType::GenerateArrayLayoutType(cgMod, arrTy);
    auto srcArrPtr = irBuilder.CreateBitCast(irBuilder.GetPayloadFromObject(src.array), arrayType->getPointerTo(1u));
    auto dstArrPtr = irBuilder.CreateBitCast(irBuilder.GetPayloadFromObject(dst.array), arrayType->getPointerTo(1u));
    srcArrPtr = irBuilder.CreateStructGEP(arrayType, srcArrPtr, 1);
    dstArrPtr = irBuilder.CreateStructGEP(arrayType, dstArrPtr, 1);
    auto srcIndex = irBuilder.CreateMul(src.index, typeSize, "src.index");
    auto dstIndex = irBuilder.CreateMul(dst.index, typeSize, "dst.index");
    ArrayMemCpyOrCopyTo(irBuilder,
        ArrayCopyToInfo(src.array, dst.array, srcArrPtr, dstArrPtr, dataSize, srcIndex, dstIndex, elemType, copyLen));
}

void GenerateArrayCopyTo(IRBuilder2& irBuilder, const std::vector<CHIR::Value*>& nodeArgs)
{
    /**
     * CopyTo function must have 5 arguments.
     * SRC_ARR: src array.
     * DST_ARR: src array.
     * SRC_START: src array start index.
     * DST_START: dst array start index.
     * ARRAY_COPY_LEN: array copy length.
     */
    enum class ARG { SRC_ARR = 0, DST_ARR, SRC_START, DST_START, ARRAY_COPY_LEN, NUM_OF_ELEMENTS };
    CJC_ASSERT(nodeArgs.size() == static_cast<size_t>(ARG::NUM_OF_ELEMENTS));
    auto& cgMod = irBuilder.GetCGModule();
    auto srcArr = cgMod | nodeArgs[static_cast<size_t>(ARG::SRC_ARR)];
    auto dstArr = cgMod | nodeArgs[static_cast<size_t>(ARG::DST_ARR)];
    auto srcStart = cgMod | nodeArgs[static_cast<size_t>(ARG::SRC_START)];
    auto dstStart = cgMod | nodeArgs[static_cast<size_t>(ARG::DST_START)];
    auto copyLen = cgMod | nodeArgs[static_cast<size_t>(ARG::ARRAY_COPY_LEN)];
    CJC_ASSERT(srcArr && dstArr && srcStart && dstStart && copyLen);

    auto arrTy = static_cast<CHIR::RawArrayType*>(nodeArgs[0]->GetType()->GetTypeArgs()[0]);
    ArrayPosition dstArrayPos = {**dstArr, **dstStart};
    ArrayPosition srcArrayPos = {**srcArr, **srcStart};
    CallArrayIntrinsicCopyTo(irBuilder, dstArrayPos, srcArrayPos, copyLen->GetRawValue(), *arrTy);
}

llvm::Value* CallArrayIntrinsicCopy(
    IRBuilder2& irBuilder, const CGValue& srcArr, const CGType& elemType, const CHIR::RawArrayType& arrTy)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto srcLenVal = irBuilder.CallArrayIntrinsicGetSize(*srcArr);
    CJC_NULLPTR_CHECK(srcLenVal);
    auto dst = irBuilder.AllocateArray(arrTy, srcLenVal);
    auto arrayType = CGArrayType::GenerateArrayLayoutType(cgMod, arrTy);
    auto srcArrPtr =
        irBuilder.CreateBitCast(irBuilder.GetPayloadFromObject(srcArr.GetRawValue()), arrayType->getPointerTo(1u));
    auto dstArrPtr = irBuilder.CreateBitCast(irBuilder.GetPayloadFromObject(dst), arrayType->getPointerTo(1u));
    srcArrPtr = irBuilder.CreateStructGEP(arrayType, srcArrPtr, 1);
    dstArrPtr = irBuilder.CreateStructGEP(arrayType, dstArrPtr, 1);

    auto typeSizeVal = irBuilder.GetSize_64(elemType.GetOriginal());
    auto dataSize = irBuilder.CreateMul(srcLenVal, typeSizeVal, "arr.data.len");
    auto zeroVal = irBuilder.getInt64(0);
    ArrayCopyToInfo arrayCopyToInfo(
        srcArr.GetRawValue(), dst, srcArrPtr, dstArrPtr, dataSize, zeroVal, zeroVal, &elemType, srcLenVal);
    ArrayMemCpyOrCopyTo(irBuilder, arrayCopyToInfo);
    return dst;
}

inline bool IsVArrayPtrType(llvm::Type* type)
{
    return type->isPointerTy() && GetPointerElementType(type)->isArrayTy();
}

inline void InstrumentPointerOps(
    IRBuilder2& irBuilder, const std::string& instFunc, llvm::Value* ptr, llvm::Value* typeSize)
{
    auto& llvmCtx = irBuilder.GetLLVMContext();
    auto castVal = irBuilder.CreateBitCast(ptr, llvm::Type::getInt8PtrTy(llvmCtx));
    auto& cgMod = irBuilder.GetCGModule();
    irBuilder.CallIntrinsicFunction(llvm::Type::getVoidTy(llvmCtx), instFunc,
        {cgMod.CreateGhostCFuncArgValue(*castVal, *CGType::GetCStringCGType(cgMod)),
            cgMod.CreateGhostCFuncArgValue(*typeSize, *CGType::GetInt64CGType(cgMod))},
        {FAST_NATIVE_ATTR});
}

inline void InsertAsanInstrument([[maybe_unused]] const CGModule& cgMod, [[maybe_unused]] IRBuilder2& irBuilder,
    [[maybe_unused]] const CHIRIntrinsicWrapper& intrinsic, [[maybe_unused]] llvm::Value* gep,
    [[maybe_unused]] const std::string& asanFunc)
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (cgMod.GetCGContext().GetCompileOptions().EnableAsan() ||
        cgMod.GetCGContext().GetCompileOptions().EnableHwAsan()) {
        auto genericInfo = intrinsic.GetInstantiatedTypeArgs();
        CJC_ASSERT(genericInfo.size() == 1);
        // CJ_MCC_AsanRead(uptr addr, uptr size)
        // CJ_MCC_AsanWrite(uptr addr, uptr size)
        auto typeSize = irBuilder.GetSize_64(*genericInfo[0]);
        InstrumentPointerOps(irBuilder, asanFunc, gep, typeSize);
    }
#endif
}

llvm::Value* GenerateVectorIntrinsics(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    auto cgValArgs = HandleSyscallIntrinsicArguments(irBuilder, intrinsic.GetOperands());
    std::vector<llvm::Value*> args;
    transform(cgValArgs.begin(), cgValArgs.end(), back_inserter(args),
        [](const CGValue* value) { return value->GetRawValue(); });
    const size_t idxArr1 = 0;
    const size_t idxOffset1 = 1;
    const size_t idxArr2 = 2;
    const size_t idxOffset2 = 3;
    switch (intrinsic.GetIntrinsicKind()) {
        case CHIR::IntrinsicKind::VECTOR_COMPARE_32:
            return irBuilder.EmitVectorCompare32(args[idxArr1], args[idxOffset1], args[idxArr2], args[idxOffset2]);
        case CHIR::IntrinsicKind::VECTOR_INDEX_BYTE_32:
            return irBuilder.EmitVectorIndexByte32(args[idxArr1], args[idxOffset1], args[idxArr2]);
        default:
            break;
    }
    CJC_ASSERT(false && "unreachable at GenerateVectorIntrinsic");
    return nullptr;
}

llvm::Value* GenerateMathIntrinsics(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    auto parameters = HandleSyscallIntrinsicArguments(irBuilder, intrinsic.GetOperands());
    std::vector<llvm::Value*> args;
    transform(parameters.begin(), parameters.end(), back_inserter(args),
        [](const CGValue* value) { return value->GetRawValue(); });
    return irBuilder.CallMathIntrinsics(intrinsic, args);
}

llvm::Value* GenerateInteropIntrinsics(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    auto parameters = HandleSyscallIntrinsicArguments(irBuilder, intrinsic.GetOperands());
    return irBuilder.CallInteropIntrinsics(intrinsic, parameters);
}
#endif

llvm::Value* ConvertCStringToCPointer(const IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    CJC_ASSERT(intrinsic.GetNumOfOperands() == 1);
    return **(irBuilder.GetCGModule() | intrinsic.GetOperand(0));
}

inline llvm::Value* HandleValuePtr(IRBuilder2& irBuilder, llvm::Value* valPtr)
{
    llvm::Type* type = valPtr->getType();
    CJC_ASSERT(type->isPointerTy());
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    // For cjnative backend, structTy/varrayTy is passed by reference
    if (IsStructPtrType(type) || IsVArrayPtrType(type)) {
        return valPtr;
    }
#endif

    return irBuilder.CreateLoad(GetPointerElementType(valPtr->getType()), valPtr);
}

inline llvm::Type* GetPointerToWithSpecificAddrspace(llvm::Type* srcType, unsigned dstAddrspace)
{
    CJC_ASSERT(srcType->isPointerTy() && "a pointer type is expected");
    return GetPointerElementType(srcType)->getPointerTo(dstAddrspace);
}

llvm::Value* CPointerGetAddress(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    CJC_ASSERT(intrinsic.GetNumOfOperands() == 1);
    auto& cgMod = irBuilder.GetCGModule();
    auto pointerRefPtr = **(cgMod | intrinsic.GetOperand(0));
    auto cgRetTy = CGType::GetOrCreate(cgMod, intrinsic.GetResult()->GetType());
    CJC_NULLPTR_CHECK(cgRetTy);
    auto retVal = irBuilder.CreatePtrToInt(pointerRefPtr, cgRetTy->GetLLVMType());
    return retVal;
}

llvm::Value* CPointerRead(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    // The size of args must be 2.
    CJC_ASSERT(intrinsic.GetNumOfOperands() == 2);
    auto& cgMod = irBuilder.GetCGModule();
    auto retTy = intrinsic.GetResult()->GetType();
    auto pointerRefPtr = **(cgMod | intrinsic.GetOperand(0));
    auto pointerIndex = **(cgMod | intrinsic.GetOperand(1));

    llvm::Value* ret = nullptr;
    llvm::Value* gep = nullptr;
    if (retTy->IsGeneric()) {
        auto tiOfResult = irBuilder.CreateTypeInfo(retTy);
        auto retTySize = irBuilder.GetLayoutSize_32(*retTy);
        ret = irBuilder.CallIntrinsicAllocaGeneric({tiOfResult, retTySize});
        auto destPayloadPtr = irBuilder.GetPayloadFromObject(ret);
        auto fixedTypeSize = irBuilder.CreateZExtOrTrunc(retTySize, llvm::Type::getInt64Ty(cgMod.GetLLVMContext()));
        auto offset = irBuilder.CreateMul(fixedTypeSize, pointerIndex);
        gep = irBuilder.CreateGEP(irBuilder.getInt8Ty(), pointerRefPtr, offset, "ele.ptr");
        InsertAsanInstrument(cgMod, irBuilder, intrinsic, gep, "CJ_MCC_AsanRead");
        irBuilder.CreateMemCpy(destPayloadPtr, llvm::MaybeAlign(), gep, llvm::MaybeAlign(), retTySize);
    } else {
        auto cgRetTy = CGType::GetOrCreate(cgMod, retTy);
        CJC_NULLPTR_CHECK(cgRetTy);
        llvm::Type* realValType = cgRetTy->GetLLVMType();
        auto realValPtr = irBuilder.CreateBitCast(pointerRefPtr, realValType->getPointerTo());
        gep = irBuilder.CreateGEP(GetPointerElementType(realValPtr->getType()), realValPtr, pointerIndex);
        InsertAsanInstrument(cgMod, irBuilder, intrinsic, gep, "CJ_MCC_AsanRead");
        ret = HandleValuePtr(irBuilder, gep);
    }

    return ret;
}

llvm::Value* CPointerWrite(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    // The size of args must be 3.
    CJC_ASSERT(intrinsic.GetNumOfOperands() == 3);

    auto& cgMod = irBuilder.GetCGModule();
    auto pointerRefPtr = **(cgMod | intrinsic.GetOperand(0));
    auto indexArg = **(cgMod | intrinsic.GetOperand(1));
    auto valueArg = **(cgMod | intrinsic.GetOperand(2)); // The 3rd is the T.

    llvm::Value* gep = nullptr;
    auto chirValueTy = intrinsic.GetOperand(2)->GetType(); // The 3rd is the T.
    CJC_NULLPTR_CHECK(chirValueTy);
    if (chirValueTy->IsGeneric()) {
        auto valueTySize = irBuilder.GetLayoutSize_32(*chirValueTy);
        auto fixedTypeSize = irBuilder.CreateZExtOrTrunc(valueTySize, llvm::Type::getInt64Ty(cgMod.GetLLVMContext()));
        auto offset = irBuilder.CreateMul(fixedTypeSize, indexArg);
        gep = irBuilder.CreateGEP(irBuilder.getInt8Ty(), pointerRefPtr, offset, "ele.ptr");
        auto payloadPtr = irBuilder.GetPayloadFromObject(valueArg);
        InsertAsanInstrument(cgMod, irBuilder, intrinsic, gep, "CJ_MCC_AsanWrite");
        irBuilder.CreateMemCpy(gep, llvm::MaybeAlign(), payloadPtr, llvm::MaybeAlign(), valueTySize);
    } else {
        auto paramType = IsStructPtrType(valueArg->getType())
            ? GetPointerToWithSpecificAddrspace(valueArg->getType(), pointerRefPtr->getType()->getPointerAddressSpace())
            : valueArg->getType()->getPointerTo();
        auto realValPtr = irBuilder.CreateBitCast(pointerRefPtr, paramType);
        gep = irBuilder.CreateGEP(GetPointerElementType(realValPtr->getType()), realValPtr, indexArg);
        InsertAsanInstrument(cgMod, irBuilder, intrinsic, gep, "CJ_MCC_AsanWrite");

        if (chirValueTy->IsStruct() || chirValueTy->IsUnit()) {
            auto layOut = cgMod.GetLLVMModule()->getDataLayout().getStructLayout(
                llvm::cast<llvm::StructType>(GetPointerElementType(valueArg->getType())));
            CJC_NULLPTR_CHECK(layOut);
            auto size = layOut->getSizeInBytes();
            auto align = layOut->getAlignment();
            irBuilder.CreateMemCpy(gep, align, valueArg, align, size);
        } else {
            irBuilder.CreateStore(valueArg, gep);
        }
    }

    return nullptr;
}

llvm::Value* CPointerAdd(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    // The size of args must be 2.
    CJC_ASSERT(intrinsic.GetNumOfOperands() == 2);

    auto& cgMod = irBuilder.GetCGModule();
    auto pointerRefPtr = **(cgMod | intrinsic.GetOperand(0));
    auto offset = **(cgMod | intrinsic.GetOperand(1));

    auto& llvmCtx = irBuilder.GetLLVMContext();
    // Get CPointer address.
    auto ptrAddress = irBuilder.CreatePtrToInt(pointerRefPtr, llvm::Type::getInt64Ty(llvmCtx));
    // Get type size.
    auto chirRetTy = intrinsic.GetResult()->GetType();
    CJC_ASSERT(chirRetTy->GetTypeArgs().size() == 1);
    auto typeSize = irBuilder.GetSize_64(*chirRetTy->GetTypeArgs()[0]);
    // Get actualOffset with size and make plus.
    auto mulRes = irBuilder.CreateMul(offset, typeSize, "mul");
    auto addRes = irBuilder.CreateAdd(ptrAddress, mulRes, "add");
    // Make new CPointer.
    auto realValType = CGType::GetOrCreate(cgMod, chirRetTy)->GetLLVMType();
    auto retVal = irBuilder.CreateIntToPtr(addRes, realValType);
    return retVal;
}

llvm::Value* BitCast(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    CJC_ASSERT(intrinsic.GetNumOfOperands() == 1);
    auto& cgMod = irBuilder.GetCGModule();
    auto resTy = CGType::GetOrCreate(cgMod, intrinsic.GetResult()->GetType());
    auto fromVal = **(irBuilder.GetCGModule() | intrinsic.GetOperand(0));
    return irBuilder.CreateBitCast(fromVal, resTy->GetLLVMType());
}

llvm::Value* GenerateBuiltinCall(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    auto parameters = HandleSyscallIntrinsicArguments(irBuilder, intrinsic.GetOperands());
    auto kind = intrinsic.GetIntrinsicKind();
    switch (kind) {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        case CHIR::IntrinsicKind::RAW_ARRAY_REFEQ:
#endif
        case CHIR::IntrinsicKind::OBJECT_REFEQ:
            return irBuilder.CreateICmpEQ(**parameters[0], **parameters[1]);
        case CHIR::IntrinsicKind::OBJECT_ZERO_VALUE:
            return irBuilder.CallIntrinsicForUninitialized(*intrinsic.GetResult()->GetType());
        case CHIR::IntrinsicKind::SIZE_OF: {
            auto typeArgs = intrinsic.GetInstantiatedTypeArgs();
            return irBuilder.GetSize_Native(*typeArgs[0]);
        }
        case CHIR::IntrinsicKind::ALIGN_OF: {
            auto cgRetTy = CGType::GetOrCreate(irBuilder.GetCGModule(), intrinsic.GetResult()->GetType());
            return irBuilder.GetAlign(*intrinsic.GetInstantiatedTypeArgs()[0], cgRetTy->GetLLVMType());
        }
        case CHIR::IntrinsicKind::GET_TYPE_FOR_TYPE_PARAMETER: {
            auto typeArgs = intrinsic.GetInstantiatedTypeArgs();
            auto ti = irBuilder.CreateTypeInfo(typeArgs[0]);
            return irBuilder.GetTypeForTypeParameter(ti);
        }
        case CHIR::IntrinsicKind::IS_SUBTYPE_TYPES: {
            auto typeArgs = intrinsic.GetInstantiatedTypeArgs();
            auto leftTi = irBuilder.CreateTypeInfo(typeArgs[0]);
            auto rightTi = irBuilder.CreateTypeInfo(typeArgs[1]);
            return irBuilder.CallIntrinsicIsSubtype({leftTi, rightTi});
        }
        case CHIR::IntrinsicKind::CPOINTER_GET_POINTER_ADDRESS:
            return CPointerGetAddress(irBuilder, intrinsic);
        case CHIR::IntrinsicKind::CPOINTER_READ:
            return CPointerRead(irBuilder, intrinsic);
        case CHIR::IntrinsicKind::CPOINTER_WRITE:
            return CPointerWrite(irBuilder, intrinsic);
        case CHIR::IntrinsicKind::CPOINTER_ADD:
            return CPointerAdd(irBuilder, intrinsic);
        case CHIR::IntrinsicKind::CSTRING_CONVERT_CSTR_TO_PTR:
            return ConvertCStringToCPointer(irBuilder, intrinsic);
        case CHIR::IntrinsicKind::BIT_CAST:
            return BitCast(irBuilder, intrinsic);
        case CHIR::IntrinsicKind::ARRAY_RELEASE_RAW_DATA:
            return irBuilder.ReleaseRawData(intrinsic);
        case CHIR::IntrinsicKind::ARRAY_ACQUIRE_RAW_DATA:
            return irBuilder.AcquireRawData(intrinsic);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        case CHIR::IntrinsicKind::IS_NULL: {
            auto value = **parameters[0];
            return irBuilder.CreateICmpNE(value, llvm::Constant::getNullValue(value->getType()));
        }
#endif
        default:
#ifndef NDEBUG
            Errorln("unsupported intrinsic kind: ", CHIR::INTRINSIC_KIND_TO_STRING_MAP.at(kind));
#endif
            break;
    }

    CJC_ASSERT(false && "unimplemented builtin call.");
    return nullptr;
}

llvm::Value* GenerateArraySyscall(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto arrayValue = *(cgMod | intrinsic.GetOperand(0)); // array
    switch (intrinsic.GetIntrinsicKind()) {
        case CHIR::IntrinsicKind::ARRAY_GET:
        case CHIR::IntrinsicKind::ARRAY_SET: {
            return GenerateArrayIndex(irBuilder, intrinsic, true);
        }
        case CHIR::IntrinsicKind::ARRAY_GET_UNCHECKED:
        case CHIR::IntrinsicKind::ARRAY_SET_UNCHECKED: {
            return GenerateArrayIndex(irBuilder, intrinsic, false);
        }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        case CHIR::IntrinsicKind::ARRAY_GET_REF_UNCHECKED: {
            return GenerateArrayGetElemRef(irBuilder, intrinsic, false);
        }
#endif
        case CHIR::IntrinsicKind::ARRAY_BUILT_IN_COPY_TO: {
            GenerateArrayCopyTo(irBuilder, intrinsic.GetOperands());
            return irBuilder.CreateNullValue(*intrinsic.GetResult()->GetType());
        }
        case CHIR::IntrinsicKind::ARRAY_SIZE:
            return irBuilder.CallArrayIntrinsicGetSize(*arrayValue);

        case CHIR::IntrinsicKind::ARRAY_CLONE: {
            auto arr = intrinsic.GetOperand(0);
            auto arrTy = StaticCast<CHIR::RawArrayType*>(arr->GetType()->GetTypeArgs()[0]);
            CJC_ASSERT(arrTy);
            auto elemType = CGType::GetOrCreate(cgMod, arrTy->GetElementType());
            return CallArrayIntrinsicCopy(irBuilder, arrayValue, *elemType, *arrTy);
        }
        case CHIR::IntrinsicKind::ARRAY_INIT: {
            auto arr = intrinsic.GetOperand(0);
            auto arrTy = StaticCast<CHIR::RawArrayType*>(arr->GetType()->GetTypeArgs()[0]);
            const int expectedArgs = 3;
            CJC_ASSERT(intrinsic.GetOperands().size() == expectedArgs);
            auto elemValue = **(cgMod | intrinsic.GetOperand(1));
            auto size = **(cgMod | intrinsic.GetOperand(2));
            irBuilder.CallArrayInit(arrayValue.GetRawValue(), size, elemValue, *arrTy);
            return irBuilder.CreateNullValue(*intrinsic.GetResult()->GetType());
        }
        default:
            CJC_ASSERT(false && "Should not reach here");
            return nullptr;
    }
}

llvm::Value* GeneratePreInitializeIntrinsics(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto runtimePreInitializePackageFunc =
        llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_pre_initialize_package);
    auto i8Ptr = llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext());
    return irBuilder.LLVMIRBuilder2::CreateCall(
        runtimePreInitializePackageFunc, {llvm::ConstantPointerNull::get(i8Ptr)});
}

llvm::Value* GenerateRuntimeIntrinsics(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    auto parameters = HandleSyscallIntrinsicArguments(irBuilder, intrinsic.GetOperands());
    return irBuilder.CallRuntimeIntrinsics(intrinsic, parameters);
}

llvm::Value* GenerateSyncIntrinsics(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    auto parameters = HandleSyscallIntrinsicArguments(irBuilder, intrinsic.GetOperands());
    return irBuilder.CallSyncIntrinsics(intrinsic, parameters);
}

llvm::Value* GenerateStackTraceIntrinsics(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    auto parameters = HandleSyscallIntrinsicArguments(irBuilder, intrinsic.GetOperands());
    return irBuilder.CallStackTraceIntrinsic(intrinsic, parameters);
}

llvm::Value* GenerateFutureIntrinsics(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    auto parameters = HandleSyscallIntrinsicArguments(irBuilder, intrinsic.GetOperands());
    return irBuilder.CallIntrinsic(intrinsic, parameters);
}

llvm::Value* GenerateReflectIntrinsic(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    std::vector<CGValue*> parameters;
    if (intrinsic.GetIntrinsicKind() == CHIR::IntrinsicKind::GET_TYPE_BY_MANGLED_NAME) {
        auto instTypes = intrinsic.GetInstantiatedTypeArgs();
        llvm::Value* arg = nullptr;
        if (instTypes[0]->IsGeneric()) {
            CJC_NULLPTR_CHECK(intrinsic.GetTopLevelFunc());
            arg = llvm::cast<llvm::Value>(irBuilder.GetLLVMModule()
                                              ->getFunction(intrinsic.GetTopLevelFunc()->GetIdentifierWithoutPrefix())
                                              ->getArg(0));
        } else {
            arg = llvm::cast<llvm::Value>(
                CGType::GetOrCreate(irBuilder.GetCGModule(), DeRef(*instTypes[0]))->GetOrCreateTypeInfo());
        }
        return irBuilder.CreateBitCast(arg, llvm::Type::getInt8PtrTy(irBuilder.GetLLVMContext()));
    } else {
        parameters = HandleSyscallIntrinsicArguments(irBuilder, intrinsic.GetOperands());
    }
    return irBuilder.CallIntrinsic(intrinsic, parameters);
#endif
}

llvm::Value* GenerateCPointerInit(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto retChirType = intrinsic.GetResult()->GetType();
    CJC_ASSERT(intrinsic.GetNumOfOperands() <= 1);
    auto retVal = intrinsic.GetNumOfOperands() == 0 ? irBuilder.CreateNullValue(*retChirType)
                                                    : **(cgMod | intrinsic.GetOperand(0));
    if (IsFuncPtrType(retVal->getType())) {
        retVal = irBuilder.CreatePointerCast(retVal, CGType::GetOrCreate(cgMod, retChirType)->GetLLVMType());
    }
    return retVal;
}

llvm::Value* GenerateCStringInit(const IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    CJC_ASSERT(intrinsic.GetNumOfOperands() == 1);
    return **(irBuilder.GetCGModule() | intrinsic.GetOperand(0));
}

llvm::Value* GenerateInout(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    auto& cgCtx = irBuilder.GetCGContext();
    if (cgCtx.GetCompileOptions().EnableAsan() || cgCtx.GetCompileOptions().EnableHwAsan()) {
        auto function = irBuilder.GetInsertFunction();
        CJC_NULLPTR_CHECK(function);
        function->addFnAttr("address_sanitize_stack");
    }
#endif
    auto& cgMod = irBuilder.GetCGModule();
    CJC_ASSERT(intrinsic.GetNumOfOperands() == 1);
    auto argVal = **(cgMod | intrinsic.GetOperand(0));
    // All of CPointer<T> will be translated to i8*.
    auto i8PtrTy = llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext());
    if (auto li = llvm::dyn_cast<llvm::LoadInst>(argVal); li) {
        auto addr = li->getPointerOperand();
        return irBuilder.CreatePointerCast(addr, i8PtrTy);
    }
    CJC_ASSERT(argVal->getType()->isPointerTy());
    return irBuilder.CreatePointerCast(argVal, i8PtrTy);
}

llvm::Value* GenerateIntrinsic(IRBuilder2& irBuilder, const CHIRIntrinsicWrapper& intrinsic)
{
    using GenerateFunc = std::function<llvm::Value*(IRBuilder2&, const CHIRIntrinsicWrapper&)>;

    static const std::unordered_map<CGIntrinsicKind, GenerateFunc> generateFuncMap = {
        {CGIntrinsicKind::REFLECT, &GenerateReflectIntrinsic},
        {CGIntrinsicKind::CPOINTER_INIT, &GenerateCPointerInit},
        {CGIntrinsicKind::CSTRING_INIT, &GenerateCStringInit},
        {CGIntrinsicKind::INOUT_PARAM, &GenerateInout},
        {CGIntrinsicKind::ARRAY, &GenerateArraySyscall},
        {CGIntrinsicKind::OVERFLOW_APPLY, &GenerateOverflowApply},
        {CGIntrinsicKind::SYNC, &GenerateSyncIntrinsics},
        {CGIntrinsicKind::STACK_TRACE, &GenerateStackTraceIntrinsics},
        {CGIntrinsicKind::FUTURE, &GenerateFutureIntrinsics},
        {CGIntrinsicKind::EXCEPTION_CATCH, &GenerateExceptionCatchIntrinsics},
        {CGIntrinsicKind::BUILTIN, &GenerateBuiltinCall},
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        {CGIntrinsicKind::VARRAY, &GenerateVarrayIntrinsics},
        {CGIntrinsicKind::VECTOR, &GenerateVectorIntrinsics},
        {CGIntrinsicKind::MATH, &GenerateMathIntrinsics},
        {CGIntrinsicKind::PREINITIALIZE, &GeneratePreInitializeIntrinsics},
        {CGIntrinsicKind::INTEROP, &GenerateInteropIntrinsics},
#endif
        {CGIntrinsicKind::RUNTIME, &GenerateRuntimeIntrinsics},
    };

    CGIntrinsicKind ikind = GetCGIntrinsicKind(intrinsic.GetIntrinsicKind());
    auto iter = generateFuncMap.find(ikind);
    CJC_ASSERT(iter != generateFuncMap.end() && "Unsupported Syscall.");
    llvm::Value* retVal = iter->second(irBuilder, intrinsic);

    // For syscall func, the return value isn't obtained from the first argument.
    // Therefore, when the function returns a void type, we return the Unit type of Cangjie.
    // Otherwise, the return value of the syscall func cannot be assigned to Unit variable.
    return (retVal && retVal->getType()->isVoidTy()) ? irBuilder.GetCGModule().GenerateUnitTypeValue() : retVal;
}
} // namespace CodeGen
} // namespace Cangjie
