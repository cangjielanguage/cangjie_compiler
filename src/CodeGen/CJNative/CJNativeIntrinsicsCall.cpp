// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "IRBuilder.h"

#include "Base/CGTypes/CGArrayType.h"
#include "Base/CGTypes/CGClassType.h"
#include "Base/CHIRExprWrapper.h"
#include "CGModule.h"
#include "IRAttribute.h"
#include "Utils/CGCommonDef.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/Type/ClassDef.h"

namespace Cangjie::CodeGen {

llvm::Value* IRBuilder2::CallIntrinsic(
    const CHIRIntrinsicWrapper& intrinsic, const std::vector<CGValue*>& parameters, [[maybe_unused]] bool isC)
{
    if (auto it = INTRINSIC_KIND_TO_FUNCNAME_MAP.find(intrinsic.GetIntrinsicKind());
        it != INTRINSIC_KIND_TO_FUNCNAME_MAP.end()) {
        auto funcInfo = it->second;
        auto retCGType = CGType::GetOrCreate(cgMod, intrinsic.GetResult()->GetType());
        return CallIntrinsicFunction(retCGType->GetLLVMType(), funcInfo.funcName, parameters, funcInfo.attributes);
    }
    std::vector<llvm::Value*> args;
    transform(parameters.begin(), parameters.end(), back_inserter(args),
        [](const CGValue* value) { return value->GetRawValue(); });
    return CallIntrinsic(intrinsic, args);
}

llvm::Value* IRBuilder2::CallIntrinsic(
    const CHIRIntrinsicWrapper& intrinsic, const std::vector<llvm::Value*>& args, const std::vector<llvm::Type*>& tys)
{
    if (auto iter = INTRINSIC_KIND_TO_ID_MAP.find(intrinsic.GetIntrinsicKind());
        iter != INTRINSIC_KIND_TO_ID_MAP.end()) {
        auto func = llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), iter->second, tys);
        return CreateCall(func, args);
    }
    CJC_ASSERT(false && "Unsupported intrinsic.");
    return nullptr;
}

llvm::Function* IRBuilder2::GetExceptionIntrinsicThrow() const
{
    // declare void @cj_throw_exception(i8 addrspace(1)* param)
    return llvm::Intrinsic::getDeclaration(GetLLVMModule(), llvm::Intrinsic::cj_throw_exception);
}

llvm::Value* IRBuilder2::CallSetLocation()
{
    if (!GetCGContext().GetCompileOptions().enableCompileDebug) {
        return nullptr;
    }
    // Func: anyregcc void @llvm.cj.set.location()
    auto setLocation = llvm::Intrinsic::getDeclaration(GetLLVMModule(), llvm::Intrinsic::cj_set_location);
    auto callInst = CreateCall(setLocation);
    callInst->setCallingConv(llvm::CallingConv::AnyReg);
    return callInst;
}

llvm::Value* IRBuilder2::CallExceptionIntrinsicGetExceptionValue()
{
    // declare i8* cj_get_exception_wrapper()
    auto getExceptionValue =
        llvm::Intrinsic::getDeclaration(GetLLVMModule(), llvm::Intrinsic::cj_get_exception_wrapper);
    return CreateCall(getExceptionValue);
}

llvm::Value* IRBuilder2::CallPostThrowExceptionIntrinsic(llvm::Value* exceptionValue)
{
    // declare i8 addrspace(1)* @llvm.cj.post.throw.exception(i8* param)
    auto beginCatch = llvm::Intrinsic::getDeclaration(GetLLVMModule(), llvm::Intrinsic::cj_post_throw_exception);
    return CreateCall(beginCatch, {exceptionValue});
}

llvm::Value* IRBuilder2::CallStackTraceIntrinsic(const CHIRIntrinsicWrapper& syscall, std::vector<CGValue*>& parameters)
{
    auto intrinsicKind = syscall.GetIntrinsicKind();
    if (intrinsicKind == CHIR::IntrinsicKind::FILL_IN_STACK_TRACE) {
        CJC_ASSERT(!parameters.empty() && "the arguments number of parameters is incorrect.");

        const auto& uint64ArrayCHIRType = *StaticCast<CHIR::RawArrayType*>(
            StaticCast<CHIR::RefType*>(syscall.GetResult()->GetType())->GetBaseType());

        std::vector<llvm::Value*> funcArgs;
        transform(parameters.begin(), parameters.end(), back_inserter(funcArgs),
            [](const CGValue* cgValue) { return cgValue->GetRawValue(); });
        funcArgs[0] = CreateBitCast(CreateTypeInfo(uint64ArrayCHIRType), getInt8PtrTy());
        // declare i8 addrspace(1)* @cj_get_exception_wrapper(i8* rawArray, i8 addrspace(1)* rawArray)
        auto fillStackTrace = llvm::Intrinsic::getDeclaration(GetLLVMModule(), llvm::Intrinsic::cj_fill_in_stack_trace);
        return llvm::cast<llvm::Value>(CreateCall(fillStackTrace, funcArgs));
    } else if (intrinsicKind == CHIR::IntrinsicKind::DECODE_STACK_TRACE) {
        auto& cgCtx = GetCGContext();
        auto retCHIRType = syscall.GetResult()->GetType();
        auto retCGType = CGType::GetOrCreate(cgMod, retCHIRType);

        // Get the def of struct-StackTraceData
        auto resultStructDef = StaticCast<CHIR::StructType*>(retCHIRType)->GetStructDef();
        // Get the CHIRType of uint8Array
        const auto& uint8ArrayCHIRType = *StaticCast<CHIR::RawArrayType*>(
            StaticCast<CHIR::RefType*>(resultStructDef->GetInstanceVar(0).type)->GetBaseType());

        // The last parameter, uint8ArrayClassInfo, is used to construct CJ Ojbect(RawArray) at runtime.
        // Since runtime know nothing about CJ Object, CodeGen need to pass the ClassInfo to runtime.
        auto tempVal = CGValue(CreateBitCast(CreateTypeInfo(uint8ArrayCHIRType), getInt8PtrTy()),
            CGType::GetOrCreate(cgMod, CGType::GetRefTypeOfCHIRInt8(cgCtx.GetCHIRBuilder())));
        parameters.emplace_back(&tempVal);
        return CallIntrinsicFunction(retCGType->GetLLVMType(), "CJ_MCC_DecodeStackTrace", parameters, {});
    }
    CJC_ASSERT(false && "Unsupported syscall.");
    return nullptr;
}

void IRBuilder2::CallIntrinsicIndexCheck(llvm::Value* len, llvm::Value* index)
{
    auto zero = llvm::ConstantInt::get(llvm::Type::getInt64Ty(GetLLVMContext()), 0);
    // index is lower than zero
    auto cmpZero = CreateICmpSLT(index, zero);
    // greater than or equal to len
    auto cmpLen = CreateICmpSGE(index, len);
    auto isThrow = CreateOr(cmpZero, cmpLen);
    auto [thenBB, elseBB] =
        Vec2Tuple<2>(CreateAndInsertBasicBlocks({GenNameForBB("arr.if.then"), GenNameForBB("arr.if.else")}));
    (void)CreateCondBr(isThrow, thenBB, elseBB);
    // 1: generate then branch(throw exception)
    SetInsertPoint(thenBB);
    // IndexOutOfBoundsException: throw IndexOutOfBoundsException()
    CreateOutOfBoundException();
    // after calling CJ_MCC_ThrowException should be set unreachable
    (void)CreateUnreachable();
    // 2: generate else branch
    SetInsertPoint(elseBB);
}

void IRBuilder2::CallArrayIntrinsicIndexCheck(llvm::Value* array, llvm::Value* index)
{
    CallIntrinsicIndexCheck(CallArrayIntrinsicGetSize(array), index);
}

llvm::Value* IRBuilder2::CallArrayIntrinsicGetSize(llvm::Value* array)
{
    auto& cgCtx = GetCGContext();
    auto i64Ty = getInt64Ty();
    auto arrayBaseType = CGType::GetArrayBaseType(cgCtx);
    auto payloadPtr = CreateBitCast(GetPayloadFromObject(array), arrayBaseType->getPointerTo(1U));
    auto ptrToArrLen = CreateBitCast(payloadPtr, i64Ty->getPointerTo(1U)); // Array size is index 1. type i64.
    return LLVMIRBuilder2::CreateLoad(i64Ty, ptrToArrLen);
}

llvm::Value* IRBuilder2::GetArrayElementAddr(
    const CHIR::RawArrayType& arrTy, llvm::Value* array, llvm::Value* index, bool isChecked)
{
    // Safe get will generate index check in codegen.
    if (isChecked) {
        CallArrayIntrinsicIndexCheck(array, index);
    }

    auto arrayType = CGArrayType::GenerateArrayLayoutType(GetCGModule(), arrTy);
    auto arrPtr = CreateBitCast(GetPayloadFromObject(array), arrayType->getPointerTo(1u));
    auto int32Type = llvm::Type::getInt32Ty(cgMod.GetLLVMContext());
    auto zero = llvm::ConstantInt::get(int32Type, 0);
    auto arrHeadIndex = llvm::ConstantInt::get(int32Type, 1);
    return CreateGEP(arrayType, arrPtr, {zero, arrHeadIndex, index}, "arr.idx.get.gep");
}

void IRBuilder2::CallArrayIntrinsicSet(
    const CHIR::RawArrayType& arrTy, llvm::Value* array, llvm::Value* index, CGValue& cgVal, bool isChecked)
{
    auto elemCGType = CGType::GetOrCreate(cgMod, arrTy.GetElementType());
    auto elemType = elemCGType->GetLLVMType();
    auto rawArrayCGType = CGType::GetOrCreate(cgMod, &arrTy);
    if (!rawArrayCGType->GetSize()) {
        llvm::Value* offset = CreateMul(GetSize_64(*arrTy.GetElementType()), index);
        offset = CreateAdd(offset, getInt64(sizeof(void*) + 8U)); // 8U: size of rawArray's len field
        auto fieldAddr = CreateInBoundsGEP(getInt8Ty(), array, offset);
        fieldAddr = CreateBitCast(fieldAddr, elemType->getPointerTo(array->getType()->getPointerAddressSpace()));
        GetCGContext().SetBasePtr(fieldAddr, array);
        auto fieldAddrCGType =
            CGType::GetOrCreate(cgMod, CGType::GetRefTypeOf(GetCGContext().GetCHIRBuilder(), *arrTy.GetElementType()));
        CreateStore(cgVal, CGValue(fieldAddr, fieldAddrCGType));
        return;
    }

    auto value = cgVal.GetRawValue();
    auto elePtr = GetArrayElementAddr(arrTy, array, index, isChecked);
    if (elemType->isStructTy()) {
        if (IsTypeContainsRef(elemType)) {
            auto structType = llvm::cast<llvm::StructType>(elemType);
            auto layOut = GetLLVMModule()->getDataLayout().getStructLayout(structType);
            auto size = getInt64(layOut->getSizeInBytes());
            CallGCWriteAgg({array, elePtr, value, size});
        } else {
            auto layOut =
                GetCGModule().GetLLVMModule()->getDataLayout().getStructLayout(llvm::cast<llvm::StructType>(elemType));
            CJC_NULLPTR_CHECK(layOut);
            auto size = layOut->getSizeInBytes();
            auto align = layOut->getAlignment();
            CreateMemCpy(elePtr, align, value, align, size);
        }
    } else if (elemType->isArrayTy()) {
        auto size = getInt64(GetLLVMModule()->getDataLayout().getTypeAllocSize(elemType));
        CallGCWriteAgg({array, elePtr, value, size});
    } else if (elemType == CGType::GetRefType(GetLLVMContext())) {
        CallGCWrite({value, array, elePtr});
    } else {
        CreateStore(value, elePtr);
    }
}

llvm::Value* IRBuilder2::CallArrayIntrinsicGet(
    const CHIR::RawArrayType& arrTy, llvm::Value* array, llvm::Value* index, bool isChecked)
{
    llvm::Value* elePtr = nullptr;
    auto elemCHIRTy = arrTy.GetElementType();
    auto elemType = CGType::GetOrCreate(cgMod, elemCHIRTy)->GetLLVMType();
    auto rawArrayCGType = CGType::GetOrCreate(cgMod, &arrTy);
    if (!rawArrayCGType->GetSize()) {
        llvm::Value* offset = CreateMul(GetSize_64(*elemCHIRTy), index);
        offset = CreateAdd(offset, getInt64(sizeof(void*) + 8U)); // 8U: size of rawArray's len field
        elePtr = CreateInBoundsGEP(getInt8Ty(), array, offset);
        elePtr = CreateBitCast(elePtr, elemType->getPointerTo(array->getType()->getPointerAddressSpace()));
        GetCGContext().SetBasePtr(elePtr, array);
    } else {
        elePtr = GetArrayElementAddr(arrTy, array, index, isChecked);
    }
    auto loadInst = CreateLoad(elemType, elePtr);
    if (elemCHIRTy->IsEnum()) {
        llvm::cast<llvm::Instruction>(loadInst)->setMetadata("untrusted_ref", llvm::MDNode::get(GetLLVMContext(), {}));
    }
    return loadInst;
}

llvm::Value* IRBuilder2::AcquireRawData(const CHIRIntrinsicWrapper& intrinsic)
{
    std::vector<llvm::Value*> args;
    for (auto arg : intrinsic.GetOperands()) {
        args.emplace_back(**(cgMod | arg));
    }
    CJC_ASSERT(args.size() == 1);
    auto& llvmCtx = GetLLVMContext();
    auto int1ty = llvm::Type::getInt1Ty(llvmCtx);
    CJC_NULLPTR_CHECK(int1ty);
    auto tmp = CreateEntryAlloca(int1ty);
    (void)CreateStore(getFalse(), tmp);
    auto isCopyPtr = CreatePtrToInt(tmp, llvm::Type::getInt64Ty(llvmCtx));
    // i8* @cj_acquire_rawdata(i8 addrspace(1)*, i64)
    auto func = llvm::Intrinsic::getDeclaration(
        cgMod.GetLLVMModule(), static_cast<llvm::Intrinsic::ID>(llvm::Intrinsic::cj_acquire_rawdata));
    return CreateCallOrInvoke(func, {args[0], isCopyPtr});
}

llvm::Value* IRBuilder2::ReleaseRawData(const CHIRIntrinsicWrapper& intrinsic)
{
    std::vector<llvm::Value*> args;
    for (auto arg : intrinsic.GetOperands()) {
        args.emplace_back(**(cgMod | arg));
    }
    // The size of parameters must be 2.
    CJC_ASSERT(args.size() == 2);
    // CPointer is 2nd.
    auto recordVal = args[1];
    auto mode = llvm::cast<llvm::Value>(
        llvm::ConstantInt::getSigned(llvm::Type::getInt32Ty(GetLLVMContext()), static_cast<int64_t>(0)));
    // void @cj_release_rawdata(i8 addrspace(1)*, i8*, i32)
    auto func = llvm::Intrinsic::getDeclaration(
        GetLLVMModule(), static_cast<llvm::Intrinsic::ID>(llvm::Intrinsic::cj_release_rawdata));
    return CreateCallOrInvoke(func, {args[0], recordVal, mode});
}

void IRBuilder2::CreateRefStore(CGValue* cgValue, llvm::Value* basePtr, llvm::Value* place, bool isBaseObjStruct)
{
    auto currentFunction = GetInsertFunction();
    CJC_ASSERT(currentFunction && "Can't get the current function.");
    auto inStructMut = currentFunction->hasFnAttribute(STRUCT_MUT_FUNC_ATTR);
    auto valueCGType = cgValue->GetCGType();
    auto value = cgValue->GetRawValue();
    auto base = basePtr;
    bool isDstAddrspace1 = base->getType()->getPointerAddressSpace() == 1;
    if (isBaseObjStruct) {
        if (IsGlobalVariableBasePtr(place) && valueCGType->IsPointerType() &&
            IsTypeContainsRef(valueCGType->GetPointerElementType()->GetLLVMType())) {
            CreateWriteBarrierForGlobalVariable(*cgValue, CGValue(place, valueCGType));
            return;
        }
        if (!inStructMut || !llvm::isa<llvm::Argument>(base)) {
            if (valueCGType->IsPointerType()) {
                CreateStore(*cgValue, CGValue(place, valueCGType));
            } else {
                CreateStore(value, place);
            }
            return;
        } else {
            // If base object is 'this' of struct mut function, base should be previous
            // param（e.g. __auto_v_xx$BP).
            if (base->getName() == "this") {
                base = &currentFunction->arg_begin()[llvm::cast<llvm::Argument>(base)->getArgNo() - 1];
            }
        }
    }
    if (valueCGType->IsStructPtrType()) {
        if (isDstAddrspace1 && IsTypeContainsRef(valueCGType->GetPointerElementType()->GetLLVMType())) {
            auto structType = llvm::cast<llvm::StructType>(valueCGType->GetPointerElementType()->GetLLVMType());
            auto layOut = cgMod.GetLLVMModule()->getDataLayout().getStructLayout(structType);
            auto size = llvm::ConstantInt::get(llvm::Type::getInt64Ty(GetLLVMContext()), layOut->getSizeInBytes());
            CallGCWriteAgg({base, place, value, size});
        } else {
            auto layOut = GetCGModule().GetLLVMModule()->getDataLayout().getStructLayout(
                llvm::cast<llvm::StructType>(valueCGType->GetPointerElementType()->GetLLVMType()));
            CJC_NULLPTR_CHECK(layOut);
            auto size = layOut->getSizeInBytes();
            auto align = layOut->getAlignment();
            CreateMemCpy(place, align, value, align, size);
        }
    } else if (isDstAddrspace1 && valueCGType->IsVArrayPtrType()) {
        auto valueType = valueCGType->GetPointerElementType()->GetLLVMType();
        auto size = getInt64(cgMod.GetLLVMModule()->getDataLayout().getTypeAllocSize(valueType));
        CallGCWriteAgg({base, place, value, size});
    } else if (isDstAddrspace1 && valueCGType->IsRefType()) {
        CallGCWrite({value, base, place});
    } else {
        CreateStore(value, place);
    }
}

// This method compare 32 bytes is equal or not, <32 x i8>
// It does not check the boundary which is the caller's duty
// func vectorCompare32(arr1: RawArray<Byte>, offset1: Int64,
//                    arr2: RawArray<Byte>, offset2: Int64): i64
// Return the index of first not equal byte
// Return 32 if all 32 bytes are equal
llvm::Value* IRBuilder2::EmitVectorCompare32(
    llvm::Value* arr1, llvm::Value* offset1, llvm::Value* arr2, llvm::Value* offset2)
{
    auto& ctx = GetLLVMContext();
    llvm::Type* arrayType = CGArrayType::GenerateArrayLayoutTypeInfo(
        cgMod.GetCGContext(), ARRAY_LAYOUT_PREFIX + "UInt8", llvm::Type::getInt8Ty(ctx));
    llvm::Type* int32Type = llvm::Type::getInt32Ty(ctx);
    // <32 x i8>
    llvm::VectorType* vecType = llvm::VectorType::get(llvm::Type::getInt8Ty(ctx), 32, false);
    llvm::ConstantInt* zero = getInt32(0);

    // load <32 x i8> vec1 from arr1
    llvm::Value* arrPtr1 = CreateBitCast(GetPayloadFromObject(arr1), arrayType->getPointerTo(1u));
    llvm::Value* ptr1 = CreateGEP(arrayType, arrPtr1, {zero, llvm::ConstantInt::get(int32Type, 1), offset1});
    llvm::Value* vec1 = static_cast<LLVMIRBuilder2*>(this)->CreateAlignedLoad(
        vecType, CreateBitCast(ptr1, vecType->getPointerTo(1u)), llvm::MaybeAlign(1), "vec1");

    // load <32 x i8> vec2 from arr2
    llvm::Value* arrPtr2 = CreateBitCast(GetPayloadFromObject(arr2), arrayType->getPointerTo(1u));
    llvm::Value* ptr2 = CreateGEP(arrayType, arrPtr2, {zero, llvm::ConstantInt::get(int32Type, 1), offset2});
    llvm::Value* vec2 = static_cast<LLVMIRBuilder2*>(this)->CreateAlignedLoad(
        vecType, CreateBitCast(ptr2, vecType->getPointerTo(1u)), llvm::MaybeAlign(1), "vec2");

    // compare vec1 and vec2 and get the result <32 x i8> vec3
    // CmpNE is used here since we are concerned about which byte is not matched
    llvm::Value* vec3 = CreateICmpNE(vec1, vec2);
    // trunc <32 x i8> to <32 x i1>
    llvm::Value* bitVec = CreateTrunc(vec3, llvm::VectorType::get(llvm::Type::getInt1Ty(ctx), 32, false));

    // call i32 @llvm.cttz.i32(i32 bitcast (<32 x i1> to i32)) to get the index of first not equal byte
    llvm::Value* res = CreateCall(
        llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cttz, {llvm::Type::getInt32Ty(ctx)}),
        {CreateBitCast(bitVec, llvm::Type::getInt32Ty(ctx)), llvm::ConstantInt::getFalse(ctx)});
    return CreateZExt(res, llvm::Type::getInt64Ty(ctx));
}

// func vectorEqualWithByte32(arr: RawArray<Byte>, offset: Int64, byte: Byte): Int64
// `CallVectorEqualWithByte32` compares each byte in arr[offset..(offset+32)] with the given argument byte
// It does not check the boundary which is the caller's duty
// Return the position of matched byte in arr which is [0, 32]
// Return 32 if the input byte is not found in arr[offset..(offset+32)]
// e.g. vectorEqualWithByte32([1, 2, 3, ..., 32], 0, 2) returns 1, since arr[1] == 2
llvm::Value* IRBuilder2::EmitVectorIndexByte32(llvm::Value* arr, llvm::Value* offset, llvm::Value* byte)
{
    auto& ctx = GetLLVMContext();
    llvm::Type* arrayType = CGArrayType::GenerateArrayLayoutTypeInfo(
        cgMod.GetCGContext(), ARRAY_LAYOUT_PREFIX + "UInt8", llvm::Type::getInt8Ty(ctx));
    llvm::Type* int32Type = llvm::Type::getInt32Ty(ctx);
    // <32 x i8>
    llvm::VectorType* vecType = llvm::VectorType::get(llvm::Type::getInt8Ty(ctx), 32, false);
    llvm::ConstantInt* zero = getInt32(0);

    // load <32 x i8> vec1 from arr
    llvm::Value* arrPtr = CreateBitCast(GetPayloadFromObject(arr), arrayType->getPointerTo(1u));
    llvm::Value* ptr = CreateGEP(arrayType, arrPtr, {zero, llvm::ConstantInt::get(int32Type, 1), offset});
    llvm::Value* vec1 = static_cast<LLVMIRBuilder2*>(this)->CreateAlignedLoad(
        vecType, CreateBitCast(ptr, vecType->getPointerTo(1u)), llvm::MaybeAlign(1), "vec1");

    // create <32 x i8> vec from argument byte
    llvm::Value* tempVec2 =
        static_cast<LLVMIRBuilder2*>(this)->CreateInsertElement(vecType, byte, llvm::ConstantInt::get(int32Type, 0));
    llvm::Value* vec2 =
        static_cast<LLVMIRBuilder2*>(this)->CreateShuffleVector(tempVec2, std::vector<int>(32, 0), "vec2");

    // compare vec1 and vec2 and get the result <64 x i8> vec3
    llvm::Value* vec3 = CreateICmpEQ(vec1, vec2);
    // trunc <32 x i8> to <32 x i1>
    llvm::Value* bitVec = CreateTrunc(vec3, llvm::VectorType::get(llvm::Type::getInt1Ty(ctx), 32, false));
    // call i32 @llvm.cttz.i32(i32 bitcast (<32 x i1> to i32)) to get the index of first not equal byte
    llvm::Value* res = CreateCall(
        llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cttz, {llvm::Type::getInt32Ty(ctx)}),
        {CreateBitCast(bitVec, llvm::Type::getInt32Ty(ctx)), llvm::ConstantInt::getFalse(ctx)});
    return CreateZExt(res, llvm::Type::getInt64Ty(ctx));
}

// (index > len || index < 0)
void IRBuilder2::CallVArrayIntrinsicIndexCheck(const CGValue* arrayPtr, std::vector<llvm::Value*>& index)
{
    auto varray = llvm::cast<llvm::ArrayType>(arrayPtr->GetCGType()->GetPointerElementType()->GetLLVMType());
    for (auto& iter : index) {
        auto len = llvm::ConstantInt::get(llvm::Type::getInt64Ty(GetLLVMContext()), varray->getNumElements());
        CallIntrinsicIndexCheck(len, iter);
        if (!varray->getElementType()->isArrayTy()) {
            return;
        }
        varray = llvm::dyn_cast<llvm::ArrayType>(varray->getElementType());
    }
}

void IRBuilder2::CreateVArrayStore(CGValue* cgValue, llvm::Value* place)
{
    llvm::Value* basePtr = llvm::Constant::getNullValue(CGType::GetRefType(GetLLVMContext()));
    bool isBaseObjStruct = true;
    if (llvm::isa<llvm::GetElementPtrInst>(place)) {
        auto operand = llvm::cast<llvm::GetElementPtrInst>(place)->getPointerOperand();
        while (llvm::isa<llvm::GetElementPtrInst>(operand)) {
            operand = llvm::cast<llvm::Instruction>(operand)->getOperand(0);
            if (llvm::isa<llvm::BitCastInst>(operand)) {
                basePtr = llvm::cast<llvm::Instruction>(operand)->getOperand(0);
                isBaseObjStruct = false;
                break;
            }
        }
    }
    CreateRefStore(cgValue, basePtr, place, isBaseObjStruct);
}

namespace {
inline llvm::Function* GenerateIntrinsicFunctionDecl(
    IRBuilder2& irBuilder, llvm::Type* retType, const std::string& name, const std::vector<CGValue*>& parameters,
    const std::vector<std::string>& attributes)
{
    auto& llvmCtx = irBuilder.GetLLVMContext();
    std::vector<llvm::Type*> parameterTypes;
    for (auto value : parameters) {
        CJC_ASSERT(value);
        CJC_ASSERT(value->GetRawValue());
        auto paramType = value->GetRawValue()->getType();
        if (IsStructPtrType(paramType) && !value->GetCGType()->IsCGTI()) {
            if (REFLECT_INTRINSIC_FUNC.find(name) == REFLECT_INTRINSIC_FUNC.end()) {
                (void)parameterTypes.emplace_back(irBuilder.getInt8PtrTy(1));
            }
            if (paramType->getPointerAddressSpace() != 1) {
                paramType = value->GetCGType()->GetPointerElementType()->GetLLVMType()->getPointerTo(1);
            }
        }
        (void)parameterTypes.emplace_back(paramType);
    }
    // The return value is treated as void type when it is Unit type.
    // Otherwise, if it is struct type, sret needs to be added.
    llvm::FunctionType* functionType = retType == CGType::GetUnitType(llvmCtx)
        ? llvm::FunctionType::get(retType, parameterTypes, false)
        : CGType::GetCodeGenFunctionType(llvmCtx, retType, parameterTypes);
    llvm::Function* func = irBuilder.GetCGModule().GetOrInsertFunction(name, functionType);
    if (retType != CGType::GetUnitType(llvmCtx) && retType->isStructTy()) {
        func->arg_begin()->addAttr(llvm::Attribute::NoAlias);
        func->arg_begin()->addAttr(llvm::Attribute::getWithStructRetType(llvmCtx, retType));
    }
    AddLinkageTypeMetadata(*func, llvm::GlobalValue::ExternalLinkage, irBuilder.GetCGContext().IsCGParallelEnabled());
    for (auto& attribute : attributes) {
        AddFnAttr(func, llvm::Attribute::get(llvmCtx, attribute));
    }
    return func;
}
} // namespace

// Cangjie function calls the intrinsic function. LLVMCustomizedCreationAPI.cpp:99
// For CJNATIVE: If Cangjie call C functions and C functions call Cangjie functions back, need to add "cj2c" attribute.
llvm::Value* IRBuilder2::CallIntrinsicFunction(llvm::Type* retType, const std::string& name,
    const std::vector<CGValue*>& parameters, const std::vector<std::string>& attributes)
{
    CJC_ASSERT(retType && "The return type of intrinsic function is a nullptr.");
    auto module = cgMod.GetLLVMModule();
    llvm::Function* func = module->getFunction(name);
    std::vector<llvm::Value*> parametersValues;
    if (!func) {
        func = GenerateIntrinsicFunctionDecl(*this, retType, name, parameters, attributes);
    }
    if (retType != CGType::GetUnitType(GetLLVMContext()) && retType->isStructTy()) {
        parametersValues.emplace_back(CreateEntryAlloca(retType));
    }
    for (auto value : parameters) {
        CJC_ASSERT(value);
        CJC_ASSERT(value->GetRawValue());
        auto llvmVal = value->GetRawValue();
        if (IsStructPtrType(llvmVal->getType()) && !value->GetCGType()->IsCGTI()) {
            // Insert the basePtr for the structure argument.
            if (REFLECT_INTRINSIC_FUNC.find(name) == REFLECT_INTRINSIC_FUNC.end()) {
                if (auto basePtr = cgMod.GetCGContext().GetBasePtrOf(llvmVal)) {
                    parametersValues.emplace_back(basePtr);
                } else {
                    parametersValues.emplace_back(llvm::Constant::getNullValue(getInt8PtrTy(1)));
                }
            }
            if (llvmVal->getType()->getPointerAddressSpace() != 1) {
                llvmVal = CreateAddrSpaceCast(
                    llvmVal, value->GetCGType()->GetPointerElementType()->GetLLVMType()->getPointerTo(1));
            }
        }
        parametersValues.emplace_back(llvmVal);
    }
    auto callInst = CreateCallOrInvoke(func, parametersValues);
    if (func->hasStructRetAttr()) {
        return callInst->getArgOperand(0);
    }
    return callInst;
}

// Call runtime API to create a thread.
// There are two APIs
// 1. Create a thread with a future object and `Future.execute` as the entry function
// 2. Create a thread with a closure and `Future.executeClosure` as the entry function
llvm::Value* IRBuilder2::CallSpawnIntrinsic(
    CGValue& obj, CGFunction& cgFunc, std::optional<CGValue*>& threadCtx, bool spawnWithFuture, llvm::Value* futureTI)
{
    // Choose the runtime API to create a new thread
    // If `spawnWithFuture` is true, `obj` refers to a future object and `executeFuncName` is `Future.execute`;
    // otherwise, `obj` is a struct Clsoure and `executeFuncName` is `Future.execuClosure`.
    auto rtFuncName =
        spawnWithFuture ? PREFIX_OF_BACKEND_SYMS + "NewCJThread" : PREFIX_OF_BACKEND_SYMS + "NewCJThreadNoReturn";
    auto i8PtrTy = llvm::Type::getInt8PtrTy(GetLLVMContext());
    std::vector<CGValue*> args = {&cgFunc, &obj};
    auto futureTIVal = CGValue(spawnWithFuture ? futureTI : CreateBitCast(futureTI, i8PtrTy), CGType::GetCGTI(cgMod));
    if (threadCtx.has_value()) {
        (void)args.emplace_back(threadCtx.value());
        if (!spawnWithFuture) {
            (void)args.emplace_back(&futureTIVal);
        }
        return CallIntrinsicFunction(i8PtrTy, rtFuncName, args, {});
    } else {
        auto chirType = CGType::GetRefTypeOfCHIRInt8(GetCGContext().GetCHIRBuilder());
        auto cgType = CGType::GetOrCreate(cgMod, chirType);
        auto nullThreadCtx = CGValue(CreateNullValue(*chirType), cgType);
        (void)args.emplace_back(&nullThreadCtx);
        if (!spawnWithFuture) {
            (void)args.emplace_back(&futureTIVal);
        }
        return CallIntrinsicFunction(i8PtrTy, rtFuncName, args, {});
    }
}

llvm::Value* IRBuilder2::GenerateDivLikeCheck(llvm::Value* dividend, llvm::Value* divisor, bool isSigned, bool isDiv)
{
    auto zero = llvm::ConstantInt::get(divisor->getType(), 0);
    auto cond = CreateICmpEQ(divisor, zero);
    auto [isZeroBB, notZeroBB, endBB] = Vec2Tuple<3>(
        CreateAndInsertBasicBlocks({GenNameForBB("divisorIs0"), GenNameForBB("divisorIsNot0"), GenNameForBB("end")}));
    CreateCondBr(cond, isZeroBB, notZeroBB);

    SetInsertPoint(isZeroBB);
    CreateOverflowOrArithmeticException("Divided by zero!", false);
    CreateUnreachable();

    SetInsertPoint(notZeroBB);
    if (isDiv) {
        return isSigned ? CreateSDiv(dividend, divisor) : CreateUDiv(dividend, divisor);
    } else {
        return isSigned ? CreateSRem(dividend, divisor) : CreateURem(dividend, divisor);
    }
    CreateBr(endBB);

    SetInsertPoint(endBB);
}

llvm::Value* IRBuilder2::GenerateOverflowCheckedFunc(
    CHIR::ExprKind opKind, const CHIR::Type& ty, std::vector<llvm::Value*>& argGenValues)
{
    bool isNeg = false;
    CGType* cgType = CGType::GetOrCreate(cgMod, &ty);
    llvm::Type* type = cgType->GetLLVMType();
    // ident: sadd/uadd/ssub/usub/smul/umul.
    bool isSignedInteger = ty.IsInteger() ? StaticCast<const CHIR::IntType&>(ty).IsSigned() : false;
    std::string ident = isSignedInteger ? "s" : "u";
    if (opKind == CHIR::ExprKind::NEG) {
        isNeg = true;
        ident += std::string("sub");
        argGenValues.insert(argGenValues.begin(), llvm::ConstantInt::get(type, 0));
    } else {
        ident += OPERATOR_KIND_TO_OP_MAP.at(opKind);
    }
    // funcName: llvm.sadd.with.overflow.i8
    auto funcName = "llvm." + ident + ".with.overflow." + GetTypeName(cgType);
    auto boolType = llvm::Type::getInt1Ty(GetLLVMContext());
    std::vector<llvm::Type*> params{type, boolType};
    auto retType = llvm::StructType::get(GetLLVMContext(), params);
    std::vector<llvm::Type*> paramsTypes{type, type};
    llvm::FunctionType::get(retType, paramsTypes, false);
    auto funcType = llvm::FunctionType::get(retType, paramsTypes, false);
    auto func = cgMod.GetOrInsertFunction(funcName, funcType);
    // call llvm.sadd.with.overflow.i8
    auto rightVal = CreateCall(func, argGenValues);
    if (isNeg) {
        argGenValues.erase(argGenValues.begin());
    }
    return rightVal;
}

namespace {
llvm::Function* GetGCIntrinsicAlloc(const CGModule& cgMod)
{
    auto module = cgMod.GetLLVMModule();
    auto function = llvm::Intrinsic::getDeclaration(module, llvm::Intrinsic::cj_malloc_object);
    function->addAttributeAtIndex(static_cast<llvm::Intrinsic::ID>(llvm::AttributeList::ReturnIndex),
        llvm::Attribute::get(function->getContext(), llvm::Attribute::NoAlias));
    return function;
}
} // namespace

llvm::Value* IRBuilder2::CallClassIntrinsicAlloc(const CHIR::Type& type)
{
    auto value = CallClassIntrinsicAlloc({CreateTypeInfo(type), GetLayoutSize_32(type)});
    if (type.IsClass()) {
        CJC_ASSERT(!type.IsAutoEnvBase() && "Should not reach here, please check CHIR.");
        auto& classDef = *static_cast<const CHIR::ClassType&>(type).GetClassDef();
        std::vector<llvm::Metadata*> mallocType;
        auto& llvmCtx = getContext();
        if (classDef.GetFinalizer()) {
            mallocType.emplace_back(llvm::MDString::get(llvmCtx, GC_FINALIZER_ATTR));
        }
        if (IsCoreFutureClass(classDef) || IsSyncRelatedClass(classDef) || IsWeakRefClass(classDef)) {
            mallocType.emplace_back(llvm::MDString::get(llvmCtx, classDef.GetSrcCodeIdentifier()));
        }
        if (!mallocType.empty()) {
            auto meta = llvm::MDTuple::get(llvmCtx, mallocType);
            value->setMetadata("MallocType", meta);
        }
    }
    return value;
}

// parameters = {TypeInfo* ti, i32 size}
llvm::Instruction* IRBuilder2::CallClassIntrinsicAlloc(const std::vector<llvm::Value*>& parameters)
{
    CJC_ASSERT(parameters.size() == 2U);
    auto allocFunc = GetGCIntrinsicAlloc(cgMod);
    auto fixedParams = {CreateBitCast(parameters[0], getInt8PtrTy()), parameters[1]};
    auto callInst = CreateCallOrInvoke(allocFunc, fixedParams);
    callInst->addAttributeAtIndex(
        llvm::AttributeList::ReturnIndex, llvm::Attribute::get(GetLLVMContext(), llvm::Attribute::NoAlias));
    return callInst;
}

llvm::Value* IRBuilder2::GenerateOverflowSaturatingFunc(
    CHIR::ExprKind opKind, const CHIR::Type& ty, const std::vector<llvm::Value*>& argGenValues)
{
    CGType* cgType = CGType::GetOrCreate(cgMod, &ty);
    llvm::Type* type = cgType->GetLLVMType();
    bool isSignedInteger = ty.IsInteger() ? StaticCast<const CHIR::IntType&>(ty).IsSigned() : false;
    if (opKind == CHIR::ExprKind::ADD || (opKind == CHIR::ExprKind::SUB)) {
        // ident: sadd/uadd/ssub/usub.
        auto ident = (isSignedInteger ? "s" : "u") + OPERATOR_KIND_TO_OP_MAP.at(opKind);
        // funcName: llvm.sadd.sat.i8
        auto funcName = "llvm." + ident + ".sat." + GetTypeName(cgType);
        std::vector<llvm::Type*> paramsTypes{type, type};
        llvm::FunctionType::get(type, paramsTypes, false);
        auto funcType = llvm::FunctionType::get(type, paramsTypes, false);
        auto func = cgMod.GetOrInsertFunction(funcName, funcType);
        // call llvm.sadd.sat.i8
        return CreateCall(func, argGenValues);
    }
    if (opKind == CHIR::ExprKind::NEG && !isSignedInteger) {
        // saturatingNeg
        return llvm::ConstantInt::get(type, 0);
    }
    return nullptr;
}

llvm::Value* IRBuilder2::GenerateOverflowWrappingFunc(
    CHIR::ExprKind opKind, const CHIR::Type& ty, [[maybe_unused]] std::vector<llvm::Value*>& argGenValues)
{
    llvm::Type* type = CGType::GetOrCreate(cgMod, &ty)->GetLLVMType();
    auto minVal = CodeGen::GetIntMaxOrMin(StaticCast<const CHIR::IntType&>(ty).GetTypeKind(), false);
    return llvm::ConstantInt::getSigned(type, opKind == CHIR::ExprKind::MOD ? 0 : minVal);
}

llvm::Value* IRBuilder2::GenerateCallExpectFunction(CGType* cgType, llvm::Value* val, llvm::Value* expectVal)
{
    // intrinsic func: <type> llvm.expect.{type}(<type> <val>, <type> <expectVal>)
    llvm::Type* type = cgType->GetLLVMType();
    auto funcName = "llvm.expect." + GetTypeName(cgType);
    auto retType = cgType->GetLLVMType();
    std::vector<llvm::Type*> paramsTypes{type, type};
    llvm::FunctionType::get(retType, paramsTypes, false);
    auto funcType = llvm::FunctionType::get(retType, paramsTypes, false);
    auto func = cgMod.GetOrInsertFunction(funcName, funcType);
    // call the intrinsic function: llvm.expect.{type}
    std::vector<llvm::Value*> argValues{val, expectVal};
    return CreateCall(func, argValues);
}

// The IR generated manually by this function depends on the ABI of the Cangjie CPointer. If the processing of the
// CPointer changes, adaptation is required.
void IRBuilder2::CreateOverflowOrArithmeticException(const std::string& ident, bool isOverflow)
{
    const std::string createOverflowExceptionFuncName = "rt$CreateOverflowException_msg";
    const std::string createArithmeticExceptionFuncName = "rt$CreateArithmeticException_msg";
    auto funcName = isOverflow ? createOverflowExceptionFuncName : createArithmeticExceptionFuncName;
    auto createExceptionFuncCHIRNode = GetCGContext().GetImplicitUsedFunc(funcName);
    auto createExceptionFunc = cgMod.GetOrInsertCGFunction(createExceptionFuncCHIRNode);
    auto rawFunction = createExceptionFunc->GetRawFunction();
    auto createExceptionFuncRawType = rawFunction->getFunctionType();
    CJC_ASSERT(createExceptionFuncRawType->getNumParams() == 1U && "Incorrect number of parameters");

    auto chirTypeOfParam = StaticCast<CHIR::FuncType*>(createExceptionFuncCHIRNode->GetType())->GetParamTypes()[0];
    auto cgTypeOfParam =
        CGType::GetOrCreate(cgMod, CGType::GetRefTypeOf(GetCGContext().GetCHIRBuilder(), *chirTypeOfParam));
    auto cjString = FixFuncArg(
        CGValue(CreateStringLiteral(ident), cgTypeOfParam), *createExceptionFunc->GetCGFunctionType()->GetParamType(0));
    auto exceptionValue = CreateCallOrInvoke(rawFunction, {cjString});
    CallExceptionIntrinsicThrow(exceptionValue);
}

namespace {
inline void CreateImplicitUsedFuncCall(
    IRBuilder2& irBuilder, const std::string& funcMangledName, const std::vector<llvm::Value*>& args)
{
    auto implicitImportedFunc =
        irBuilder.GetCGModule().GetOrInsertCGFunction(irBuilder.GetCGContext().GetImplicitUsedFunc(funcMangledName));
    CJC_ASSERT(implicitImportedFunc && "Cant get the implicit Imported function.");
    auto implicitImportedFuncRaw = implicitImportedFunc->GetRawFunction();
    irBuilder.CreateCallOrInvoke(implicitImportedFuncRaw, args);
}

inline void CreateExceptionAndThrow(
    IRBuilder2& irBuilder, const std::string& exceptionClassName, const std::string& exceptionInitFuncName)
{
    auto exceptionClassDef = DynamicCast<CHIR::ClassDef*>(
        GetTypeDefFromImplicitUsedFuncParam(irBuilder.GetCGModule(), exceptionInitFuncName, 0));
    CJC_NULLPTR_CHECK(exceptionClassDef);
    auto exceptionValue = irBuilder.CallClassIntrinsicAlloc(*exceptionClassDef->GetType());
    CreateImplicitUsedFuncCall(
        irBuilder, exceptionInitFuncName, {exceptionValue, irBuilder.CreateTypeInfo(*exceptionClassDef->GetType())});
    irBuilder.CallExceptionIntrinsicThrow(exceptionValue);
}
} // namespace

void IRBuilder2::CreateNegativeArraySizeException()
{
    const std::string negativeArraySizeExceptionClassName = "_CNat26NegativeArraySizeExceptionE";
    const std::string negativeArraySizeExceptionInitFuncName = "_CNat26NegativeArraySizeException6<init>Hv";
    CreateExceptionAndThrow(*this, negativeArraySizeExceptionClassName, negativeArraySizeExceptionInitFuncName);
}

void IRBuilder2::CreateOutOfBoundException()
{
    const std::string indexOutOfBoundsExceptionClassName = "_CNat25IndexOutOfBoundsExceptionE";
    const std::string indexOutOfBoundsExceptionInitFuncName = "_CNat25IndexOutOfBoundsException6<init>Hv";
    CreateExceptionAndThrow(*this, indexOutOfBoundsExceptionClassName, indexOutOfBoundsExceptionInitFuncName);
}

void IRBuilder2::CreateRunTimeInvalidGenericParamException()
{
    const std::string invalidGenericParamExceptionClassName = "_CNat9ExceptionE";
    const std::string invalidGenericParamExceptionInitFuncName = "_CNat9Exception6<init>Hv";
    CreateExceptionAndThrow(*this, invalidGenericParamExceptionClassName, invalidGenericParamExceptionInitFuncName);
}

void IRBuilder2::CreateSpawnException()
{
    const std::string spawnExceptionClassName = "_CNat14SpawnExceptionE";
    const std::string spawnExceptionInitFuncName = "_CNat14SpawnException6<init>Hv";
    CreateExceptionAndThrow(*this, spawnExceptionClassName, spawnExceptionInitFuncName);
}

void IRBuilder2::CreateExecAtExitCallbacksCall()
{
    const std::string execAtExitCallbacksFuncName = "_CNat27CJ_CORE_ExecAtexitCallbacksHv";
    CreateImplicitUsedFuncCall(
        *this, execAtExitCallbacksFuncName, {CreateEntryAlloca(CGType::GetUnitType(GetLLVMContext()))});
}

void IRBuilder2::CreateGetCommandLineArgsCall(const std::vector<llvm::Value*>& args)
{
    const std::string getCommandLineArgsFuncName = "_CNat18getCommandLineArgsHv";
    CreateImplicitUsedFuncCall(*this, getCommandLineArgsFuncName, args);
}

void IRBuilder2::CreateHandleExceptionCall(const std::vector<llvm::Value*>& args)
{
    const std::string handleExceptionFuncName = "_CNat15handleExceptionHCNat9ExceptionE";
    CreateImplicitUsedFuncCall(*this, handleExceptionFuncName, args);
}

void IRBuilder2::CreateSetRuntimeCJThreadHandleCall(const std::vector<llvm::Value*>& args)
{
    const std::string setRuntimeCJThreadHandleFuncName = "_CNat6Thread24setRuntimeCJThreadHandleHPu";
    CreateImplicitUsedFuncCall(*this, setRuntimeCJThreadHandleFuncName, args);
}

void IRBuilder2::CreateEPrintlnCall(const std::string& eMsg)
{
    const std::string eprintlnFuncName = "_CNat8eprintlnHRNat6StringE";
    auto eprintlnFuncCHIRNode = GetCGContext().GetImplicitUsedFunc(eprintlnFuncName);
    auto eprintlnFunc = cgMod.GetOrInsertCGFunction(eprintlnFuncCHIRNode);
    auto rawFunction = eprintlnFunc->GetRawFunction();
    auto eprintlnFuncRawType = rawFunction->getFunctionType();
    constexpr size_t size2 = 2;
    CJC_ASSERT(eprintlnFuncRawType->getNumParams() == size2 && "Incorrect number of parameters");
    auto chirTypeOfParam = StaticCast<CHIR::FuncType*>(eprintlnFuncCHIRNode->GetType())->GetParamTypes()[0];
    auto cgTypeOfParam =
        CGType::GetOrCreate(cgMod, CGType::GetRefTypeOf(GetCGContext().GetCHIRBuilder(), *chirTypeOfParam));
    std::vector<llvm::Value*> params{CreateEntryAlloca(CGType::GetUnitType(GetLLVMContext())),
        FixFuncArg(
            CGValue(CreateStringLiteral(eMsg), cgTypeOfParam), *eprintlnFunc->GetCGFunctionType()->GetParamType(0))};
    CreateCallOrInvoke(rawFunction, params);
}

llvm::Value* IRBuilder2::CallSyncIntrinsics(
    const CHIRIntrinsicWrapper& intrinsic, const std::vector<CGValue*>& parameters)
{
    if (IsAtomicIntrinsic(intrinsic.GetIntrinsicKind())) {
        return CallAtomicIntrinsics(intrinsic, parameters);
    }
    return CallIntrinsic(intrinsic, parameters);
}

llvm::Value* IRBuilder2::CallAtomicIntrinsics(const CHIRIntrinsicWrapper& intrinsic, const std::vector<CGValue*>& args)
{
    llvm::Value* fieldPtr = CreateGEP(*args[0], {0U}).GetRawValue();
    auto atomicIntrinsicGenericTypeInfo = intrinsic.GetInstantiatedTypeArgs();
    CJC_ASSERT(atomicIntrinsicGenericTypeInfo.size() == 2U);
    if (atomicIntrinsicGenericTypeInfo[1]->IsPrimitive()) {
        return CallAtomicPrimitiveIntrinsics(intrinsic, args, fieldPtr);
    }
    std::vector<llvm::Value*> parameters;
    // func compareAndSwap has two memoryorder parameters
    bool isCompareAndSwapFunc = intrinsic.GetIntrinsicKind() == CHIR::IntrinsicKind::ATOMIC_COMPARE_AND_SWAP;
    size_t size = !isCompareAndSwapFunc ? args.size() - 1 : args.size() - 2;
    for (unsigned int i = 1; i < size; i++) {
        parameters.push_back(**args[i]);
    }
    // obj
    parameters.push_back(**args[0]);
    // fieldptr
    fieldPtr = CreateBitCast(fieldPtr, getInt8PtrTy(1U)->getPointerTo(fieldPtr->getType()->getPointerAddressSpace()));
    parameters.push_back(fieldPtr);
    // Prepare memoryOrder arguments.
    // Currently, Cangjie only supports one memory order `SeqCst`.
    // `compareAndSwap` has two memory order parameters, while other methods have one.
    const uint32_t seqCstAsInt32 = 5u; // 5 is memory_order_seq_cst in C11 memory model
    if (isCompareAndSwapFunc) {
        auto successOrder = getInt32(seqCstAsInt32);
        auto failureOrder = getInt32(seqCstAsInt32);
        parameters.push_back(successOrder);
        parameters.push_back(failureOrder);
    } else {
        auto memoryOrder = getInt32(seqCstAsInt32);
        parameters.push_back(memoryOrder);
    }

    if (intrinsic.GetIntrinsicKind() == CHIR::ATOMIC_STORE || intrinsic.GetIntrinsicKind() == CHIR::ATOMIC_SWAP) {
        // Get type of value to be stored or swapped.
        auto type = intrinsic.GetOperand(1)->GetType();
        if (auto cgType = CGType::GetOrCreate(cgMod, type); !cgType->GetSize() && !type->IsGeneric()) {
            CJC_ASSERT(cgType->IsCGEnum());
            auto payloadPtr = GetPayloadFromObject(**args[1]);
            parameters[0] = CallGCRead({**args[1], payloadPtr});
        }
    }

    // llvm.cj.atomic.xx(e.g.llvm.cj.atomic.store)
    auto res = CallIntrinsic(intrinsic, parameters);

    if (auto rstType = intrinsic.GetResult()->GetType(); !rstType->IsGeneric()) {
        if (auto cgType = CGType::GetOrCreate(cgMod, rstType); !cgType->GetSize()) {
            CJC_ASSERT(intrinsic.GetIntrinsicKind() == CHIR::ATOMIC_LOAD ||
                intrinsic.GetIntrinsicKind() == CHIR::ATOMIC_SWAP);
            CJC_ASSERT(cgType->IsCGEnum());
            auto boxVal = CallIntrinsicAllocaGeneric({CreateTypeInfo(*rstType), getInt32(8U)});
            auto payloadPtr = GetPayloadFromObject(boxVal);
            CallGCWrite({res, boxVal, payloadPtr});
            return boxVal;
        }
    }

    return res;
}

llvm::Value* IRBuilder2::CallAtomicPrimitiveIntrinsics(
    const CHIRIntrinsicWrapper& intrinsic, const std::vector<CGValue*>& args, llvm::Value* fieldPtr)
{
    llvm::Type* fieldType = llvm::cast<llvm::GetElementPtrInst>(fieldPtr)->getResultElementType();
    if (intrinsic.GetIntrinsicKind() == CHIR::IntrinsicKind::ATOMIC_COMPARE_AND_SWAP) {
        auto cas = CreateAtomicCmpXchg(fieldPtr, **args[1], **args[2], llvm::AtomicOrdering::SequentiallyConsistent,
            llvm::AtomicOrdering::SequentiallyConsistent);
        // The type of the return value cas is a structure type that contains two fields. The first field is the old
        // value, and the second field is of the bool type, indicating whether the CmpXchg operation is successful.
        return llvm::ExtractValueInst::Create(cas, 1, "", GetInsertBlock());
    }
    static const std::unordered_map<CHIR::IntrinsicKind, llvm::AtomicRMWInst::BinOp> OPMAP = {
        {CHIR::IntrinsicKind::ATOMIC_SWAP, llvm::AtomicRMWInst::Xchg},
        {CHIR::IntrinsicKind::ATOMIC_FETCH_ADD, llvm::AtomicRMWInst::Add},
        {CHIR::IntrinsicKind::ATOMIC_FETCH_SUB, llvm::AtomicRMWInst::Sub},
        {CHIR::IntrinsicKind::ATOMIC_FETCH_AND, llvm::AtomicRMWInst::And},
        {CHIR::IntrinsicKind::ATOMIC_FETCH_OR, llvm::AtomicRMWInst::Or},
        {CHIR::IntrinsicKind::ATOMIC_FETCH_XOR, llvm::AtomicRMWInst::Xor}};
    if (OPMAP.find(intrinsic.GetIntrinsicKind()) != OPMAP.end()) {
        return CreateAtomicRMW(OPMAP.at(intrinsic.GetIntrinsicKind()), fieldPtr, **args[1], llvm::MaybeAlign(),
            llvm::AtomicOrdering::SequentiallyConsistent);
    }
    // Integer type's alignment depends on bit width.
    unsigned long align = fieldType->getPrimitiveSizeInBits();
    if (intrinsic.GetIntrinsicKind() == CHIR::IntrinsicKind::ATOMIC_STORE) {
        auto store = CreateStore(**args[1], fieldPtr);
        CJC_NULLPTR_CHECK(store);
        store->setAlignment(llvm::Align(align));
        store->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
        return nullptr;
    }
    if (intrinsic.GetIntrinsicKind() == CHIR::IntrinsicKind::ATOMIC_LOAD) {
        auto load = LLVMIRBuilder2::CreateLoad(fieldType, fieldPtr);
        load->setAlignment(llvm::Align(align));
        load->setAtomic(llvm::AtomicOrdering::SequentiallyConsistent);
        return load;
    }
    return nullptr;
}

llvm::Value* IRBuilder2::CallMathIntrinsics(
    const CHIRIntrinsicWrapper& intrinsic, std::vector<llvm::Value*>& parameters)
{
    switch (intrinsic.GetIntrinsicKind()) {
        case CHIR::IntrinsicKind::POWI: {
            constexpr size_t size2 = 2;
            CJC_ASSERT(parameters.size() == size2);
            return CallIntrinsic(intrinsic, parameters, {parameters[0]->getType(), parameters[1]->getType()});
        }
        case CHIR::IntrinsicKind::ABS: {
            // llvm.abs.i8/i16/i32/i64(type, bool)
            parameters.emplace_back(getFalse());
            return CallIntrinsic(intrinsic, parameters, {parameters[0]->getType()});
        }
        default: {
            return CallIntrinsic(intrinsic, parameters, {parameters[0]->getType()});
        }
    }
}

llvm::Value* IRBuilder2::CallIntrinsicForUninitialized(const CHIR::Type& ty)
{
    if (ty.IsGeneric()) {
        auto [handleRef, handleNonRef, handleNothing, getRes] =
            Vec2Tuple<4>(CreateAndInsertBasicBlocks({GenNameForBB("handle_ref"), GenNameForBB("handle_non_ref"),
                GenNameForBB("handle_nothing"), GenNameForBB("get_uninitialized_value")}));
        auto ti = CreateBitCast(CreateTypeInfo(ty), getInt8PtrTy());
        auto nothingTi = CreateBitCast(CGType::GetNothingCGType(cgMod)->GetOrCreateTypeInfo(), getInt8PtrTy());
        auto isNothing =
            CreateIntrinsic(llvm::Intrinsic::expect, {getInt1Ty()}, {CreateICmpEQ(ti, nothingTi), getFalse()});
        CreateCondBr(isNothing, handleNothing, handleRef);
        // For zeroValue<Nothing>, throw exception:
        SetInsertPoint(handleNothing);
        CreateRunTimeInvalidGenericParamException();
        CreateUnreachable();
        // For zeroValue<Ref>, use null pointer:
        SetInsertPoint(handleRef);
        auto isRef = CreateTypeInfoIsReferenceCall(ti);
        auto lastBBOfHandleRef = GetInsertBlock();
        CreateCondBr(isRef, getRes, handleNonRef);
        // For zeroValue<NonRef>, create null value:
        SetInsertPoint(handleNonRef);
        auto zeroValue = CreateNullValue(ty);
        auto lastBBOfHandleNonRef = GetInsertBlock();
        CreateBr(getRes);
        // Select value according to the predecessor:
        SetInsertPoint(getRes);
        auto p1i8 = getInt8PtrTy(1U);
        auto phi = CreatePHI(p1i8, 2U);
        phi->addIncoming(llvm::ConstantPointerNull::get(p1i8), lastBBOfHandleRef);
        phi->addIncoming(zeroValue, lastBBOfHandleNonRef);
        return phi;
    }
    return CreateNullValue(ty);
}

llvm::Value* IRBuilder2::CallRuntimeIntrinsics(
    const CHIRIntrinsicWrapper& syscall, const std::vector<CGValue*>& parameters)
{
    return CallIntrinsic(syscall, parameters);
}

llvm::Value* IRBuilder2::VArrayInitedByLambda(
    llvm::Value* varrayLen, CGValue& autoEnv, const CHIR::VArrayType& vArrayType)
{
    // Get InitFunc.
    auto autoEnvCGType = autoEnv.GetCGType();
    auto autoEnvClsDef = StaticCast<const CHIR::ClassType*>(DeRef(autoEnvCGType->GetOriginal()))->GetClassDef();
    auto abstractMethods = autoEnvClsDef->GetAbstractMethods();
    CJC_ASSERT(abstractMethods.size() == 1);
    auto abstractMethod = abstractMethods.back();
    auto abstractMethodIdx = CHIR::GetMethodIdxInAutoEnvObject(abstractMethod.methodName);
    auto initFuncCGType = static_cast<CGFunctionType*>(
        CGType::GetOrCreate(cgMod, abstractMethod.methodTy, CGType::TypeExtraInfo{0, true, false, true, {}}));
    auto autoEnvPayload = GetPayloadFromObject(*autoEnv);
    auto initFuncPtr =
        CreateConstGEP1_32(getInt8PtrTy(), autoEnvPayload, static_cast<unsigned>(abstractMethodIdx), "virtualFPtr");
    initFuncPtr = CreateLoad(getInt8PtrTy(), initFuncPtr);
    initFuncPtr = CreateBitCast(initFuncPtr, initFuncCGType->GetLLVMFunctionType()->getPointerTo());

    auto arrayCGType = CGType::GetOrCreate(cgMod, &vArrayType);
    auto varrPtr = CreateEntryAlloca(*arrayCGType, "varray.initialized.by.lambda");
    // Prepare basic blocks.
    auto [startBB, bodyBB, endBB] = Vec2Tuple<3>(CreateAndInsertBasicBlocks(
        {GenNameForBB("varr.init.start"), GenNameForBB("varr.init.body"), GenNameForBB("varr.init.end")}));

    // Prepare iterator to assign the varray element.
    (void)CreateBr(startBB);
    SetInsertPoint(startBB);
    auto indexCGType = CGType::GetInt64CGType(cgMod);
    auto iterator = CreateEntryAlloca(*indexCGType, "iterator");
    (void)CreateStore(getInt64(0), iterator);
    (void)CreateBr(bodyBB);
    // Generate body.
    SetInsertPoint(bodyBB);
    auto index = CreateLoad(indexCGType->GetLLVMType(), iterator);
    auto cgValueOfIdx = CGValue(index, indexCGType);
    llvm::Value* value = CreateCallOrInvoke(*initFuncCGType, initFuncPtr, {&autoEnv, &cgValueOfIdx});
    auto elemType = vArrayType.GetElementType();
    auto elemCGType = CGType::GetOrCreate(cgMod, elemType);
    auto elemPtrCGType = CGType::GetOrCreate(cgMod, CGType::GetRefTypeOf(GetCGContext().GetCHIRBuilder(), *elemType));
    auto elemPtr = CreateGEP(arrayCGType->GetLLVMType(), varrPtr, {getInt64(0), index}, "varr.idx");
    if (elemCGType->IsVArrayType() || elemCGType->IsStructType()) {
        (void)CreateStore(CGValue(value, elemPtrCGType), CGValue(elemPtr, elemPtrCGType));
    } else {
        (void)CreateStore(CGValue(value, elemCGType), CGValue(elemPtr, elemPtrCGType));
    }

    // Generate increment and compare size.
    auto incremented = CreateAdd(index, getInt64(1));
    (void)CreateStore(incremented, iterator);
    auto cmpResult = CreateICmpSLT(incremented, varrayLen, "iterator.size.cmp");
    (void)CreateCondBr(cmpResult, bodyBB, endBB);
    // Change to end BB.
    SetInsertPoint(endBB);
    return varrPtr;
}

llvm::Value* IRBuilder2::VArrayInitedByItem(llvm::Value* varrayLen, const CGValue& cgVal, const CHIR::Type& vArrayType)
{
    auto& cgCtx = GetCGContext();
    CGType* arrayCGType = CGType::GetOrCreate(cgMod, &vArrayType);
    auto arrayType = arrayCGType->GetLLVMType();
    auto varrPtr = CreateEntryAlloca(*arrayCGType, "varray.initialized.by.item");
    // Prepare basic blocks.
    auto [startBB, bodyBB, endBB] = Vec2Tuple<3>(CreateAndInsertBasicBlocks(
        {GenNameForBB("varr.init.start"), GenNameForBB("varr.init.body"), GenNameForBB("varr.init.end")}));

    // Prepare iterator to assign the varray element.
    (void)CreateBr(startBB);
    SetInsertPoint(startBB);
    auto iterator = CreateEntryAlloca(*CGType::GetInt64CGType(cgMod), "iterator");
    (void)CreateStore(getInt64(0), iterator);
    (void)CreateBr(bodyBB);
    // Generate body.
    SetInsertPoint(bodyBB);
    auto index = CreateLoad(CGType::GetInt64CGType(cgMod)->GetLLVMType(), iterator);
    auto elemPtr = CreateGEP(arrayType, varrPtr, {getInt64(0), index}, "varr.idx");

    auto elementPtrCGType = CGType::GetOrCreate(cgMod,
        CGType::GetRefTypeOf(
            cgCtx.GetCHIRBuilder(), *StaticCast<const CHIR::VArrayType*>(&vArrayType)->GetElementType()));
    (void)CreateStore(cgVal, CGValue(elemPtr, elementPtrCGType));
    // Generate increment and compare size.
    auto incremented = CreateAdd(index, getInt64(1));
    (void)CreateStore(incremented, iterator);
    auto cmpResult = CreateICmpSLT(incremented, varrayLen, "iterator.size.cmp");
    (void)CreateCondBr(cmpResult, bodyBB, endBB);
    // Change to end BB.
    SetInsertPoint(endBB);
    return varrPtr;
}

llvm::Value* IRBuilder2::CallIntrinsicFloatToIntegerSat(llvm::Type& destTy, llvm::Value& srcValue, bool isSigned)
{
    auto id = static_cast<llvm::Intrinsic::ID>(isSigned ? llvm::Intrinsic::fptosi_sat : llvm::Intrinsic::fptoui_sat);
    llvm::Function* func = llvm::Intrinsic::getDeclaration(GetLLVMModule(), id, {&destTy, srcValue.getType()});
    return CreateCall(func, &srcValue);
}

llvm::Instruction* IRBuilder2::CallIntrinsicGetTypeInfo(const std::vector<llvm::Value*>& parameters)
{
    CJC_ASSERT(parameters.size() == 3U);
    llvm::Function* func = llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_get_type_info);
    auto fixedParams = {
        CreateBitCast(parameters[0], getInt8PtrTy()),
        parameters[1],
        CreateBitCast(parameters[2], getInt8PtrTy()->getPointerTo()),
    };
    return CreateCall(func, fixedParams);
}

llvm::Instruction* IRBuilder2::CallIntrinsicGetFieldOffset(const std::vector<llvm::Value*>& parameters)
{
    CJC_ASSERT(parameters.size() == 2U || parameters.size() == 3U);
    CJC_ASSERT(parameters[1]->getType()->isIntegerTy(64U));
    llvm::Function* func = llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_get_field_offset);
    const size_t idxTypeinfo = 0;
    const size_t idxIndex = 1;
    std::vector<llvm::Value*> fixedParams = {
        CreateBitCast(parameters[idxTypeinfo], getInt8PtrTy()), parameters[idxIndex]};
    // llvm.cj.get.field.offset needs 3 arguments: typeinfo, index, pre-offset
    // If only 2 arguments are provided, it means the default value of pre-offset is 0.
    if (parameters.size() == 2U) {
        (void)fixedParams.emplace_back(getInt32(0));
    } else {
        const size_t idxPreOffset = 2;
        (void)fixedParams.emplace_back(parameters[idxPreOffset]);
    }
    return CreateCall(func, fixedParams);
}

llvm::Instruction* IRBuilder2::CallIntrinsicIsSubtype(const std::vector<llvm::Value*>& parameters)
{
    CJC_ASSERT(parameters.size() == 2U);
    llvm::Function* func = llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_is_subtype);
    auto fixedParams = {CreateBitCast(parameters[0], getInt8PtrTy()), CreateBitCast(parameters[1], getInt8PtrTy())};
    return CreateCall(func, fixedParams);
}

llvm::Instruction* IRBuilder2::CallIntrinsicIsTupleTypeOf(const std::vector<llvm::Value*>& parameters)
{
    CJC_ASSERT(parameters.size() == 3U);
    auto boolTy = getInt1Ty();
    auto pi8 = getInt8PtrTy();
    auto p1i8 = getInt8PtrTy(1U);
    auto objectLLVMType = parameters[0]->getType();

    bool isRefType = objectLLVMType->isPointerTy() && objectLLVMType->getPointerAddressSpace() == 1U;
    auto funcName = std::string("llvm.cj.is.tupletype.of.") + (isRefType ? "p1i8" : "p0i8");
    auto funcType = llvm::FunctionType::get(boolTy, {(isRefType ? p1i8 : pi8), pi8, pi8}, false);
    auto func = cgMod.GetOrInsertFunction(funcName, funcType);
    auto fixedParams = {CreateBitCast(parameters[0], isRefType ? p1i8 : pi8), CreateBitCast(parameters[1], pi8),
        CreateBitCast(parameters[2], pi8)};
    return CreateCall(func, fixedParams);
}

llvm::Instruction* IRBuilder2::CallIntrinsicIsTypeEqualTo(const std::vector<llvm::Value*>& parameters)
{
    CJC_ASSERT(parameters.size() == 2U);
    llvm::Function* func =
        llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_is_typeinfo_equal);
    auto fixedParams = {CreateBitCast(parameters[0], getInt8PtrTy()), CreateBitCast(parameters[1], getInt8PtrTy())};
    return CreateCall(func, fixedParams);
}

// parameters = {TypeInfo* ti, i32 size}
llvm::Instruction* IRBuilder2::CallIntrinsicAllocaGeneric(const std::vector<llvm::Value*>& parameters)
{
    auto curLoc = getCurrentDebugLocation();
    SetCurrentDebugLocation(llvm::DebugLoc());
    CJC_ASSERT(parameters.size() == 2U);
    llvm::Function* func = llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_alloca_generic);
    auto fixedParams = {CreateBitCast(parameters[0], getInt8PtrTy()), parameters[1]};
    auto inst = CreateCallOrInvoke(func, fixedParams);
    SetCurrentDebugLocation(curLoc);
    return inst;
}

llvm::Instruction* IRBuilder2::CallIntrinsicGCWriteGeneric(const std::vector<llvm::Value*>& parameters)
{
    CJC_ASSERT(parameters.size() == 4U);
    llvm::Function* func = llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_gcwrite_generic);
    auto fixedParams = {parameters[0], CreateBitCast(parameters[1], getInt8PtrTy(1U)), parameters[2], parameters[3]};
    return CreateCall(func, fixedParams);
}

llvm::Instruction* IRBuilder2::CallGCWriteGenericPayload(const std::vector<llvm::Value*>& parameters)
{
    CJC_ASSERT(parameters.size() == 3U);
    llvm::Function* func =
        llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_gcwrite_generic_payload);
    return CreateCall(func, parameters);
}

llvm::Instruction* IRBuilder2::CallGCReadGeneric(const std::vector<llvm::Value*>& parameters)
{
    CJC_ASSERT(parameters.size() == 4U);
    llvm::Function* func = llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_gcread_generic);
    auto fixedParams = {parameters[0], parameters[1], CreateBitCast(parameters[2], getInt8PtrTy(1U)), parameters[3]};
    return CreateCall(func, fixedParams);
}

llvm::Instruction* IRBuilder2::CallIntrinsicMTable(const std::vector<llvm::Value*>& parameters)
{
    CJC_ASSERT(parameters.size() == 3U);
    llvm::Function* func = llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_get_mtable_func);
    auto fixedParams = {
        CreateBitCast(parameters[0], getInt8PtrTy()), CreateBitCast(parameters[1], getInt8PtrTy()), parameters[2]};
    return CreateCall(func, fixedParams);
}

llvm::Instruction* IRBuilder2::CallIntrinsicGetVTableFunc(
    llvm::Value* ti, llvm::Value* introTypeIdx, llvm::Value* funcOffset)
{
    llvm::Function* func = llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_get_vtable_func);
    return CreateCall(func, {CreateBitCast(ti, getInt8PtrTy()), introTypeIdx, funcOffset});
}

// parameters = {i8 any addrspace* dst, i8 addrspace(1)* src, TypeInfo* ti}
llvm::Instruction* IRBuilder2::CallIntrinsicAssignGeneric(const std::vector<llvm::Value*>& parameters)
{
    CJC_ASSERT(parameters.size() == 3U);
    llvm::Function* func = llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_assign_generic);
    auto fixedParams = {parameters[0], parameters[1], CreateBitCast(parameters[2], getInt8PtrTy())};
    return CreateCall(func, fixedParams);
}

llvm::Value* IRBuilder2::CreateTypeInfoArray(const std::vector<CHIR::Type*>& types,
    const std::unordered_map<const CHIR::Type*, std::function<llvm::Value*(IRBuilder2&)>>& map)
{
    auto typeInfoPtrType = CGType::GetOrCreateTypeInfoPtrType(GetLLVMContext());
    auto argSize = types.size();
    auto arrayType = llvm::ArrayType::get(typeInfoPtrType, argSize);
    auto typeArgsArr = CreateEntryAlloca(arrayType);
    auto storeInst = CreateStore(llvm::Constant::getNullValue(arrayType), typeArgsArr);

    std::vector<llvm::Constant*> typeInfoElements;
    for (uint32_t idx = 0; idx < argSize; ++idx) {
        auto type = DeRef(*types[idx]);
        auto localGt = CGType::GetOrCreate(cgMod, type);
        if (localGt->IsDynamicGI()) {
            typeInfoElements.emplace_back(llvm::Constant::getNullValue(typeInfoPtrType));
            auto tiDynamic = CreateBitCast(CreateTypeInfo(*type, map), typeInfoPtrType);
            auto storeAt = CreateConstInBoundsGEP2_32(arrayType, typeArgsArr, 0, idx);
            (void)CreateStore(tiDynamic, storeAt);
        } else {
            typeInfoElements.emplace_back(localGt->GetOrCreateTypeInfo());
        }
    }
    auto realConstantArray = llvm::ConstantArray::get(arrayType, typeInfoElements);
    if (realConstantArray && realConstantArray->isNullValue()) {
        CJC_NULLPTR_CHECK(storeInst);
        storeInst->eraseFromParent();
    } else {
        CJC_NULLPTR_CHECK(storeInst);
        storeInst->setOperand(0, realConstantArray);
    }
    return typeArgsArr;
}

llvm::Value* IRBuilder2::CreateTypeInfoArray(const std::vector<CHIR::Type*>& types)
{
    CJC_ASSERT(
        cgFunction && "The current IRBuilder doesn't correspond to any CGFunction. Use another CreateTypeInfo method.");
    return CreateTypeInfoArray(types, cgFunction->genericParamsMap);
}

llvm::Value* IRBuilder2::CreateTypeInfo(const CHIR::Type& gt,
    const std::unordered_map<const CHIR::Type*, std::function<llvm::Value*(IRBuilder2&)>>& map, bool canChangeBB)
{
    auto baseType = DeRef(const_cast<CHIR::Type&>(gt));
    if (baseType->IsThis()) {
        CJC_ASSERT(cgFunction && "Should not reach here.");
        auto cgFuncType = cgFunction->GetCGFunctionType();
        if (cgFuncType->IsStaticMethodType()) {
            auto thisTIIdx = cgFunction->GetCGFunctionType()->GetThisTypeInfoIndex();
            CJC_ASSERT(thisTIIdx.has_value());
            return cgFunction->GetRawFunction()->getArg(thisTIIdx.value());
        } else {
            CJC_ASSERT(cgFuncType->IsMethodType() && "CHIR should not use `ThisType` outside a method");
            auto f = cgFunction->GetRawFunction();
            auto thisArg = f->getArg(f->hasStructRetAttr() ? 1U : 0U);
            CJC_ASSERT(thisArg->getType()->isPointerTy() && thisArg->getType()->getPointerAddressSpace() == 1U);
            return GetTypeInfoFromObject(thisArg);
        }
    }

    auto& cacheMap = GetCGContext().genericParamsCacheMap[GetInsertFunction()];
    if (auto it = cacheMap.find(baseType); it != cacheMap.end()) {
        return it->second;
    }
    auto cgType = CGType::GetOrCreate(cgMod, baseType);
    auto typeInfoPtrType = CGType::GetOrCreateTypeInfoPtrType(GetLLVMContext());
    std::vector<CHIR::Type*> genericArgs;
    llvm::GlobalVariable* tt{nullptr};
    llvm::Value* res{nullptr};

    TemporarilySetDebugLocInThisScope withEmptyLoc(*this, llvm::DebugLoc());
    if (auto foundIt = map.find(baseType); foundIt != map.end()) {
        auto curPt = LLVMIRBuilder2::GetInsertPoint();
        auto curBB = LLVMIRBuilder2::GetInsertBlock();
        if (canChangeBB) {
            SetInsertPointForPreparingTypeInfo();
        }
        res = foundIt->second(*this);
        LLVMIRBuilder2::SetInsertPoint(curBB, curPt);
    } else if (baseType->IsGeneric()) {
        if (auto upperBounds = static_cast<const CHIR::GenericType*>(baseType)->GetUpperBounds();
            upperBounds.size() == 1 && static_cast<const CHIR::GenericType*>(baseType)->orphanFlag) {
            res = CreateTypeInfo(upperBounds[0]);
        } else {
            CJC_ASSERT(false && "CHIR uses an unexpected Generic-Type.");
            return nullptr;
        }
    } else if (cgType->IsConcrete()) {
        res = cgType->GetOrCreateTypeInfo();
    } else if (cgType->IsStaticGI()) {
        res = cgType->GetOrCreateTypeInfo();
    } else if (baseType->IsTuple()) {
        auto& tupleType = StaticCast<CHIR::TupleType&>(*baseType);
        tt = CGType::GetOrCreate(cgMod, baseType)->GetOrCreateTypeTemplate();
        genericArgs = tupleType.GetElementTypes();
    } else if (baseType->IsFunc()) {
        tt = CGType::GetOrCreate(cgMod, baseType)->GetOrCreateTypeTemplate();
        auto& funcType = StaticCast<CHIR::FuncType&>(*baseType);
        genericArgs = funcType.GetParamTypes();
        genericArgs.insert(genericArgs.begin(), funcType.GetReturnType());
    } else if (baseType->IsVArray()) {
        auto& varrayType = StaticCast<CHIR::VArrayType&>(*baseType);
        tt = CGType::GetOrCreate(cgMod, baseType)->GetOrCreateTypeTemplate();
        auto argType = varrayType.GetElementType();
        for (int64_t arrayIdx = 0; arrayIdx < varrayType.GetSize(); arrayIdx++) {
            genericArgs.emplace_back(argType);
        }
    } else if (baseType->IsRawArray()) {
        auto& rawArrayType = StaticCast<CHIR::RawArrayType&>(*baseType);
        tt = CGType::GetOrCreate(cgMod, baseType)->GetOrCreateTypeTemplate();
        auto argType = rawArrayType.GetElementType();
        genericArgs.emplace_back(argType);
    } else if (baseType->IsCPointer()) {
        auto& cpointerType = StaticCast<CHIR::CPointerType&>(*baseType);
        tt = CGType::GetOrCreate(cgMod, baseType)->GetOrCreateTypeTemplate();
        auto argType = cpointerType.GetElementType();
        genericArgs.emplace_back(argType);
    } else if (baseType->IsBox()) {
        auto& boxType = StaticCast<CHIR::BoxType&>(*baseType);
        tt = CGType::GetOrCreate(cgMod, baseType)->GetOrCreateTypeTemplate();
        genericArgs.emplace_back(boxType.GetBaseType());
    } else {
        // if not tuple/varray/cpointer/Box:
        auto customType = StaticCast<CHIR::CustomType*>(baseType);
        tt = CGType::GetOrCreate(cgMod, customType->GetCustomTypeDef()->GetType())->GetOrCreateTypeTemplate();
        if (customType->IsAutoEnvBase()) {
            if (customType->IsAutoEnvInstBase()) {
                customType = const_cast<CHIR::ClassType*>(static_cast<const CHIR::ClassType*>(customType))
                                 ->GetSuperClassTy(&cgMod.GetCGContext().GetCHIRBuilder());
            }
            CJC_ASSERT(customType->IsAutoEnvGenericBase());
            auto args = customType->GetGenericArgs();
            std::vector<CHIR::Type*> paramTypes(args.begin(), args.end() - 1);
            genericArgs = {GetCGContext().GetCHIRBuilder().GetType<CHIR::FuncType>(paramTypes, args.back())};
        } else {
            genericArgs = customType->GetGenericArgs();
        }
    }
    if (!genericArgs.empty()) {
        auto curPt = LLVMIRBuilder2::GetInsertPoint();
        auto curBB = LLVMIRBuilder2::GetInsertBlock();
        if (canChangeBB) {
            SetInsertPointForPreparingTypeInfo();
        }
        auto typeArgsArr = CreateTypeInfoArray(genericArgs, map);
        res = CallIntrinsicGetTypeInfo({tt, getInt32(genericArgs.size()), typeArgsArr});
        LLVMIRBuilder2::SetInsertPoint(curBB, curPt);
    }

    if (llvm::isa<llvm::GlobalVariable>(res) && res->getType() == typeInfoPtrType->getPointerTo()) {
        res = CreateLoad(typeInfoPtrType, res);
    }
    (void)cacheMap.emplace(baseType, res);
    return res;
}

llvm::Value* IRBuilder2::CreateTypeInfo(const CHIR::Type& type, bool canChangeBB)
{
    return CreateTypeInfo(type,
        cgFunction ? cgFunction->genericParamsMap
                   : std::unordered_map<const CHIR::Type*, std::function<llvm::Value*(IRBuilder2&)>>(),
        canChangeBB);
}

llvm::Value* IRBuilder2::CreateTypeInfo(const CHIR::Type* type, bool canChangeBB)
{
    if (type == nullptr) { // Just a temporary assertion
        CJC_ASSERT(type && "Meets a nullptr from CHIR.");
    }
    return CreateTypeInfo(*type, canChangeBB);
}

llvm::Value* IRBuilder2::CallInteropIntrinsics(
    const CHIRIntrinsicWrapper& intrinsic, const std::vector<CGValue*>& parameters)
{
    return CallIntrinsic(intrinsic, parameters);
}
} // namespace Cangjie::CodeGen
