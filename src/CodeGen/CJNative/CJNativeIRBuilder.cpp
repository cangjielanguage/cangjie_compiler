// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "IRBuilder.h"

#include "llvm/IR/Value.h"
#include "llvm/Support/Alignment.h"

#include "Base/CGTypes/CGClassType.h"
#include "Base/CGTypes/CGEnumType.h"
#include "Base/CGTypes/CGFunctionType.h"
#include "Base/CGTypes/CGType.h"
#include "Base/CHIRExprWrapper.h"
#include "CGModule.h"
#include "IRAttribute.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/Type/EnumDef.h"
#include "cangjie/CHIR/Value.h"

namespace Cangjie::CodeGen {
llvm::Value* IRBuilder2::FixFuncArg(const CGValue& srcValue, const CGType& destType, bool isThisArgInStructMut)
{
    auto srcRawVal = srcValue.GetRawValue();
    auto srcRawValType = srcRawVal->getType();
    const CGType* destDerefType = CGType::GetOrCreate(cgMod, DeRef(destType.GetOriginal()));
    // Note: this condition implicitly means:
    // - `srcValue` is not of a reference type because it is not a pointer type or its addrspace is 0.
    //   That is, `srcValue` must be of value type.
    if ((!srcRawValType->isPointerTy() || srcRawValType->getPointerAddressSpace() == 0 ||
        srcValue.GetCGType()->IsOptionLikeRef()) && !destDerefType->GetSize()) {
        if (isThisArgInStructMut) {
            // Note: in this branch, we should realize the dest doesn't begin with TypeInfo*.
            CJC_ASSERT(srcRawValType->getPointerAddressSpace() == 0);
            if (destType.GetLLVMType()->getPointerAddressSpace() == 0) {
                return CreateBitCast(srcRawVal, destType.GetLLVMType());
            } else {
                return CreateAddrSpaceCast(srcRawVal, destType.GetLLVMType());
            }
        } else {
            // Note: in this branch, we should realize the dest must begin with TypeInfo*.
            auto srcDerefType = DeRef(srcValue.GetCGType()->GetOriginal());
            // 1. Allocate a stack memory for storing srcValue.
            auto temp =
                CallIntrinsicAllocaGeneric({CreateTypeInfo(*srcDerefType), GetLayoutSize_32(*srcDerefType)});
            // 2. store srcValue to temp
            auto payloadPtr = GetPayloadFromObject(temp);
            const CGType* srcDerefCGType = CGType::GetOrCreate(cgMod, srcDerefType);
            if (srcDerefType->IsStruct()) {
                // Note: in this branch, it means:
                // - we are assigning a "struct" that doesn't begin with TypeInfo* to an address
                //   that should begin with TypeInfo*.
                // - we are in the scope of a struct instance method(without "$withTI" postfix),
                //   the "struct" mentioned above is the `this` parameter of the method.
                auto size = GetLayoutSize_64(*srcDerefType);
                if (IsTypeContainsRef(srcDerefCGType->GetLLVMType())) {
                    CallGCWriteAgg({temp, payloadPtr, srcRawVal, size});
                } else {
                    CreateMemCpy(payloadPtr, llvm::MaybeAlign(), srcRawVal, llvm::MaybeAlign(), size);
                }
            } else {
                (void)CreateStore(srcValue,
                    CGValue(CreateBitCast(payloadPtr, srcDerefCGType->GetLLVMType()->getPointerTo(1)),
                        CGType::GetOrCreate(cgMod,
                            CGType::GetRefTypeOf(cgMod.GetCGContext().GetCHIRBuilder(), *srcDerefType),
                            CGType::TypeExtraInfo{1U})));
            }
            return temp;
        }
    }

    const CGType* srcType = srcValue.GetCGType();
    if (!srcType->IsStructPtrType() && !srcType->IsVArrayPtrType()) {
        return srcRawVal;
    }
    CJC_ASSERT(destType.GetLLVMType()->isPointerTy());
    auto srcTypeAddrspace = srcRawVal->getType()->getPointerAddressSpace(); // 1
    auto destTypeAddrspace = destType.GetAddrspace();                       // 0
    auto res = srcRawVal;
    if (srcTypeAddrspace == 0 && destTypeAddrspace == 1) {
        auto destTypePointerElementType = destType.GetPointerElementType()->GetLLVMType();
        if (srcType->GetPointerElementType()->GetLLVMType() != destTypePointerElementType) {
            res = CreateBitCast(srcRawVal, destTypePointerElementType->getPointerTo(srcTypeAddrspace));
        }
        return CreateAddrSpaceCast(res, destTypePointerElementType->getPointerTo(1));
    }
    CJC_ASSERT(srcTypeAddrspace == destTypeAddrspace && "incorrect addrspace for argument");
    return CreateBitCast(res, destType.GetLLVMType());
}

llvm::Value* IRBuilder2::CreateCallOrInvoke(const CGFunctionType& calleeType, llvm::Value* callee,
    std::vector<CGValue*> args, bool isClosureCall, llvm::Value* thisTypeInfo)
{
    const auto& structParamNeedsBasePtr = calleeType.GetStructParamNeedsBasePtrIndices();
    const auto& realArgIndices = calleeType.GetRealArgIndices();
    std::vector<llvm::Value*> argsVal;
    size_t idx = 0;
    for (auto arg : args) {
        bool isThisArgInStruct = false;
        if (idx == 0 && this->chirExpr && dynamic_cast<const CHIRCallExpr*>(this->chirExpr)) {
            auto applyWrapper = static_cast<const CHIRCallExpr*>(this->chirExpr);
            isThisArgInStruct = applyWrapper->IsCalleeStructInstanceMethod();
        }
        auto llvmVal = FixFuncArg(*arg, *calleeType.GetParamType(idx), isThisArgInStruct);
        (void)argsVal.emplace_back(llvmVal); // Insert the fixed argument, may do some casting meanwhile.
        auto cgType = arg->GetCGType();
        CJC_ASSERT(!cgType->IsStructType());
        if (structParamNeedsBasePtr.find(realArgIndices[idx]) != structParamNeedsBasePtr.end()) {
            // Insert the basePtr for the structure argument.
            if (auto basePtr = cgMod.GetCGContext().GetBasePtrOf(llvmVal)) {
                (void)argsVal.emplace_back(basePtr);
            } else {
                auto addrspace = calleeType.GetParamType(idx)->GetAddrspace();
                (void)argsVal.emplace_back(
                    llvm::Constant::getNullValue(llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext(), addrspace)));
            }
        }
        ++idx;
    }

    // Determine whether we need to add an extra argument at the beginning to store the result.
    auto& chirFuncType = StaticCast<const CHIR::FuncType&>(calleeType.GetOriginal());
    auto returnCHIRType = chirFuncType.GetReturnType();
    auto returnCGType = CGType::GetOrCreate(cgMod, returnCHIRType);
    if (calleeType.HasSRet()) {
        llvm::Value* allocaForRetVal = nullptr;
        if (!returnCGType->GetSize()) {
            CHIR::Type* retValType = nullptr;
            if (auto applyWrapper = dynamic_cast<const CHIRCallExpr*>(this->chirExpr); applyWrapper) {
                retValType = applyWrapper->GetResult()->GetType();
            } else if (this->chirExpr->GetExprKind() == CHIR::ExprKind::VARRAY_BUILDER) {
                auto& varrayBuilder = StaticCast<const CHIR::VArrayBuilder&>(this->chirExpr->GetChirExpr());
                retValType = StaticCast<CHIR::VArrayType*>(varrayBuilder.GetResult()->GetType())->GetElementType();
            } else {
                CJC_ASSERT(false && "Should not reach here.");
            }
            if (retValType->IsGeneric()) { // `retValType` is `T`
                allocaForRetVal = CreateEntryAlloca(*returnCGType);
                CreateStore(llvm::ConstantPointerNull::get(getInt8PtrTy(1U)), allocaForRetVal);
                auto [prepareForNonRefBB, endBB] = Vec2Tuple<2>(CreateAndInsertBasicBlocks({"prepNRSRet", "end"}));
                auto ti = CreateTypeInfo(*retValType);
                CreateCondBr(CreateTypeInfoIsReferenceCall(*retValType), endBB, prepareForNonRefBB);
                SetInsertPoint(prepareForNonRefBB);
                CreateStore(CallIntrinsicAllocaGeneric({ti, GetLayoutSize_32(*retValType)}), allocaForRetVal);
                CreateBr(endBB);
                SetInsertPoint(endBB);
            } else { // `retValType` is NOT `T`
                auto retValCGType = CGType::GetOrCreate(cgMod, retValType);
                if (returnCHIRType->IsGeneric()) {
                    // generate a pointer to ti memory
                    allocaForRetVal = CreateEntryAlloca(*returnCGType);
                    CreateStore(llvm::ConstantPointerNull::get(getInt8PtrTy(1U)), allocaForRetVal);
                    if (!retValCGType->IsReference()) {
                        std::vector<llvm::Value*> parameters{
                            CreateTypeInfo(*retValType), GetLayoutSize_32(*retValType)};
                        CreateStore(CallIntrinsicAllocaGeneric(parameters), allocaForRetVal);
                    }
                } else if (returnCHIRType->IsStruct() || returnCHIRType->IsTuple() ||
                    (returnCHIRType->IsEnum() && StaticCast<CGEnumType*>(returnCGType)->IsOptionLike())) {
                    std::vector<llvm::Value*> parameters{CreateTypeInfo(*retValType), GetLayoutSize_32(*retValType)};
                    allocaForRetVal = CallIntrinsicAllocaGeneric(parameters);
                } else {
                    allocaForRetVal = CreateEntryAlloca(*retValCGType);
                }
            }
        } else {
            allocaForRetVal = CreateEntryAlloca(*returnCGType);
        }
        argsVal.insert(argsVal.begin(), allocaForRetVal);
    }

    if (this->chirExpr && dynamic_cast<const CHIRCallExpr*>(this->chirExpr)) {
        auto applyWrapper = static_cast<const CHIRCallExpr*>(this->chirExpr);
        for (auto instantiateArg : applyWrapper->GetInstantiatedTypeArgs()) {
            auto typeInfo = CreateTypeInfo(*instantiateArg);
            typeInfo = CreateBitCast(typeInfo, CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext()));
            (void)argsVal.emplace_back(typeInfo);
        }
        // OuterTypeInfo
        if (applyWrapper->IsCalleeMethod()) {
            llvm::Value* typeInfo{nullptr};
            if (applyWrapper->IsCalleeStatic()) {
                if (this->chirExpr->GetExprKind() == CHIR::ExprKind::APPLY ||
                    this->chirExpr->GetExprKind() == CHIR::ExprKind::APPLY_WITH_EXCEPTION) {
                    typeInfo = CreateBitCast(CreateTypeInfo(
                        applyWrapper->GetOuterType(GetCGContext().GetCHIRBuilder())),
                        CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext()));
                } else {
                    if (DeRef(*applyWrapper->GetThisType())->IsThis()) {
                        typeInfo = thisTypeInfo;
                    } else {
                        typeInfo = CreateBitCast(CreateTypeInfo(applyWrapper->GetThisType()),
                            CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext()));
                    }
                    auto introType = CreateBitCast(CreateTypeInfo(
                        applyWrapper->GetOuterType(GetCGContext().GetCHIRBuilder())),
                        CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext()));
                    typeInfo = CreateBitCast(CallIntrinsicMethodOuterType({typeInfo, introType, getInt64(StaticCast<CHIRInvokeStaticWrapper>(applyWrapper)->GetVirtualMethodOffset())}),
                        CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext()));
                }
            } else {
                if (this->chirExpr->GetExprKind() == CHIR::ExprKind::APPLY ||
                    this->chirExpr->GetExprKind() == CHIR::ExprKind::APPLY_WITH_EXCEPTION) {
                    typeInfo = CreateBitCast(CreateTypeInfo(
                        applyWrapper->GetOuterType(GetCGContext().GetCHIRBuilder())),
                        CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext()));
                } else {
                    auto thisVal = **(cgMod | applyWrapper->GetThisParam());
                    typeInfo = GetTypeInfoFromObject(thisVal);
                    if (!DeRef(*StaticCast<CHIRInvokeWrapper>(applyWrapper)->GetObject()->GetType())->IsAutoEnv()) {
                        auto introType = CreateBitCast(CreateTypeInfo(
                            applyWrapper->GetOuterType(GetCGContext().GetCHIRBuilder())),
                            CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext()));
                        typeInfo = CreateBitCast(CallIntrinsicMethodOuterType({typeInfo, introType, getInt64(StaticCast<CHIRInvokeWrapper>(applyWrapper)->GetVirtualMethodOffset())}),
                            CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext()));
                    }
                }
            }
            (void)argsVal.emplace_back(typeInfo);
        } else if (isClosureCall) {
            // Handle invocation via closure
            auto thisVal = **(cgMod | applyWrapper->GetOperand(1));
            (void)argsVal.emplace_back(GetTypeInfoFromObject(thisVal));
        }
        // ThisTypeInfo
        if (applyWrapper->IsCalleeStatic()) {
            CJC_NULLPTR_CHECK(thisTypeInfo);
            (void)argsVal.emplace_back(thisTypeInfo);
        }
    } else if (this->chirExpr && this->chirExpr->GetExprKind() == CHIR::ExprKind::VARRAY_BUILDER) {
        auto& varrayBuilder = StaticCast<const CHIR::VArrayBuilder&>(this->chirExpr->GetChirExpr());
        auto autoEnvOfInitFunc = varrayBuilder.GetInitFunc();
        auto autoEnvVal = **(cgMod | autoEnvOfInitFunc);
        auto typeInfo = CreateBitCast(
            GetTypeInfoFromObject(autoEnvVal), CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext()));
        (void)argsVal.emplace_back(typeInfo);
    }

    // Emit the call or invoke instruction.
    llvm::CallBase* callBaseInst = nullptr;
    CallSetLocation();
    auto unwindBlock = cgMod.GetCGContext().TopUnwindBlockStack();
    if (unwindBlock.has_value()) {
        auto currentBB = GetInsertBlock();
        auto normalDest = llvm::BasicBlock::Create(getContext(), "normalDest", currentBB->getParent());
        normalDest->moveAfter(currentBB);
        callBaseInst = CreateInvoke(calleeType.GetLLVMFunctionType(), callee, normalDest, unwindBlock.value(), argsVal);
        SetInsertPoint(normalDest);
    } else {
        callBaseInst = CreateCall(calleeType.GetLLVMFunctionType(), callee, argsVal);
    }
    if (auto f = llvm::dyn_cast<llvm::Function>(callee); f && f->hasFnAttribute(HAS_WITH_TI_WRAPPER_ATTR)) {
        auto applyExprW = dynamic_cast<const CHIRApplyWrapper*>(chirExpr);
        CJC_ASSERT(applyExprW);
        cgMod.GetCGContext().AddCallBaseToReplace(callBaseInst, *applyExprW);
    }

    // Determine which value we should return:
    // - if this is an SRet call, the first one argument is the result.
    // - otherwise, the value of callBaseInst is the result.
    llvm::Value* ret = callBaseInst;
    if (calleeType.HasSRet()) {
        callBaseInst->addAttributeAtIndex(llvm::AttributeList::FirstArgIndex, llvm::Attribute::NoAlias);
        auto sretAttr = llvm::Attribute::getWithStructRetType(callBaseInst->getContext(),
            (!returnCGType->GetSize() && !returnCGType->GetOriginal().IsGeneric())
                ? llvm::Type::getInt8Ty(GetLLVMContext())
                : returnCGType->GetLLVMType());
        callBaseInst->addAttributeAtIndex(llvm::AttributeList::FirstArgIndex, sretAttr);
        ret = argsVal.front();
        if (!returnCGType->GetSize()) {
            CHIR::Type* rstType = nullptr;
            if (this->chirExpr->GetExprKind() == CHIR::ExprKind::VARRAY_BUILDER) {
                auto& varrayBuilder = StaticCast<const CHIR::VArrayBuilder&>(this->chirExpr->GetChirExpr());
                rstType = StaticCast<CHIR::VArrayType*>(varrayBuilder.GetResult()->GetType())->GetElementType();
            } else {
                rstType = this->chirExpr->GetResult()->GetType();
            }
            auto rstCGType = CGType::GetOrCreate(cgMod, rstType);
            if (chirFuncType.GetReturnType()->IsGeneric()) {
                ret = CreateLoad(returnCGType->GetLLVMType(), ret);
            }
            if (!rstCGType->GetSize() && !rstType->IsGeneric()) {
                // Opt: if we can return `ret` without the copy?
                auto ti = CreateTypeInfo(*rstType);
                auto tmp = CallIntrinsicAllocaGeneric({ti, GetLayoutSize_32(*rstType)});
                CallIntrinsicAssignGeneric({tmp, ret, ti});
                ret = tmp;
            } else if (rstCGType->GetSize() && !rstCGType->IsReference()) {
                auto elementType = rstCGType->GetLLVMType();
                auto srcPayload = CreateBitCast(GetPayloadFromObject(ret), elementType->getPointerTo(1U));
                return CreateLoad(elementType, srcPayload);
            }
        }
    } else if (chirFuncType.GetReturnType()->IsGeneric()) {
        CJC_ASSERT(false);
        auto rstType = chirExpr->GetResult()->GetType();
        auto rstCGType = CGType::GetOrCreate(cgMod, rstType);
        if (rstCGType->GetSize() && !rstCGType->IsReference()) {
            auto elementType = rstCGType->GetLLVMType();
            auto srcPayload = CreateBitCast(GetPayloadFromObject(ret), elementType->getPointerTo(1U));
            return CreateLoad(elementType, srcPayload);
        }
    }

    // Determine whether we need to add the metadata that would infer backend to do some optimizations.
    if (isClosureCall) {
        auto& ctx = getContext();
        // `IsClosureCall` is used to confirm that the call does not
        // cause env escape when analyzing whether an escape occurs.
        // In this way, the env can be allocated to the stack.
        const std::string closureMeta = "IsClosureCall";
        callBaseInst->setMetadata(closureMeta, llvm::MDTuple::get(ctx, {llvm::MDString::get(ctx, closureMeta)}));
    }
    if (chirFuncType.GetReturnType()->IsUnit()) {
        ret = cgMod.GenerateUnitTypeValue();
    }
    return ret;
}

namespace {
void ConvertArgsType(llvm::IRBuilder<>& irBuilder, const llvm::Function* func, std::vector<llvm::Value*>& args)
{
    // Callee guarantees func not null and arguments size is equal to function parameters size.
    CJC_ASSERT(args.size() == func->arg_size());
    auto functionType = func->getFunctionType();
    for (unsigned i = 0; i < func->arg_size(); ++i) {
        if (!args[i]->getType()->isPointerTy()) {
            continue;
        }
        args[i] = irBuilder.CreatePointerCast(args[i], functionType->getParamType(i));
    }
}
} // namespace

llvm::Type* IRBuilder2::GetSizetLLVMType() const
{
    if (GetCGContext().GetCompileOptions().target.arch == Triple::ArchType::ARM32) {
        return llvm::Type::getInt32Ty(GetLLVMContext());
    } else {
        return llvm::Type::getInt64Ty(GetLLVMContext());
    }
}

CHIR::Type::TypeKind IRBuilder2::GetTypeKindFromType(const CHIR::Type& ty) const
{
    auto typeKind = ty.GetTypeKind();
    auto isArm32 = GetCGContext().GetCompileOptions().target.arch == Triple::ArchType::ARM32;
    if (typeKind == CHIR::Type::TypeKind::TYPE_UINT_NATIVE) {
        typeKind = isArm32 ? CHIR::Type::TypeKind::TYPE_UINT32 : CHIR::Type::TypeKind::TYPE_UINT64;
    } else if (typeKind == CHIR::Type::TypeKind::TYPE_INT_NATIVE) {
        typeKind = isArm32 ? CHIR::Type::TypeKind::TYPE_INT32 : CHIR::Type::TypeKind::TYPE_INT64;
    }
    return typeKind;
}

// For 64 bit OS: sizeof a pointer is 8U
// For 32 bit OS: sizeof a pointer is 4U
size_t IRBuilder2::GetPtrSize() const
{
    return GetCGContext().GetCompileOptions().target.arch == Triple::ArchType::ARM32 ? 4U : 8U;
}

// For 64 bit OS: {typeinfo*, <payload>}
// For 32 bit OS: {typeinfo*, i32, <payload>} 
size_t IRBuilder2::GetPayloadOffset() const
{
    return GetCGContext().GetCompileOptions().target.arch == Triple::ArchType::ARM32 ? GetPtrSize() + 4U : GetPtrSize();
}

bool IRBuilder2::IsGlobalVariableBasePtr(llvm::Value* val) const
{
    return llvm::isa<llvm::GlobalVariable>(val->stripInBoundsOffsets());
}

llvm::Instruction* IRBuilder2::CallGCRead(std::vector<llvm::Value*> args)
{
    // Func: i8 addr1* @llvm.cj.gcread.ref(i8 addr1* baseObj, i8 addr1*addr1* place)
    auto func = llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_gcread_ref);
    ConvertArgsType(*this, func, args);
    return CreateCall(func, args);
}

llvm::Instruction* IRBuilder2::CallGCReadWeakRef(std::vector<llvm::Value*> args)
{
    // Func: i8 addr1* @llvm.cj.gcread.weakref(i8 addr1* baseObj, i8 addr1*addr1* place)
    auto func = llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_gcread_weakref);
    ConvertArgsType(*this, func, args);
    return CreateCall(func, args);
}

llvm::Instruction* IRBuilder2::CallGCReadAgg(std::vector<llvm::Value*> args)
{
    // Func: void @llvm.cj.gcread.struct(i8 addr1* baseObj, i8 addr1* dst, i8 addr1*/ i8* src, i64 length)
    auto func = llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_gcread_struct, {GetSizetLLVMType()});
    args[3] = CreateZExtOrTrunc(args[3], GetSizetLLVMType());
    ConvertArgsType(*this, func, args);
    return CreateCall(func, args);
}

llvm::Instruction* IRBuilder2::CallGCReadStaticRef(const std::vector<llvm::Value*>& args)
{
    // Func: void @llvm.cj.gcread.static.ref(i8 addr1** val)
    return CreateCall(
        llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_gcread_static_ref), args);
}

llvm::Instruction* IRBuilder2::CallGCReadStaticAgg(llvm::StructType* type, std::vector<llvm::Value*> args)
{
    // Func: void @llvm.cj.gcread.static.struct(i8* dst, i8* src, i64 len)
    // 3 arguments.
    CJC_ASSERT(args.size() == 3);
    llvm::LLVMContext& ctx = getContext();
    auto func = llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_gcread_static_struct, {GetSizetLLVMType()});
    args[2] = CreateZExtOrTrunc(args[2], GetSizetLLVMType());
    auto typeName = GetCodeGenTypeName(type);
    auto meta = llvm::MDTuple::get(ctx, {llvm::MDString::get(ctx, typeName)});
    ConvertArgsType(*this, func, args);
    auto inst = CreateCall(func, args);
    inst->setMetadata("AggType", meta);
    return inst;
}

llvm::Instruction* IRBuilder2::CallGCWrite(std::vector<llvm::Value*> args)
{
    // Func: void @llvm.cj.gcwrite.ref(type val, i8 addr1* baseObj, type addr1* place)
    // E.g. void @llvm.cj.gcwrite.ref(i8 addr1* val, i8 addr1* baseObj, i8 addr1*addr1* refPtr)
    auto func = llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_gcwrite_ref);
    ConvertArgsType(*this, func, args);
    return CreateCall(func, args);
}

llvm::Instruction* IRBuilder2::CallGCWriteAgg(std::vector<llvm::Value*> args)
{
    // The intrinsic function has 4 arguments.
    CJC_ASSERT(args.size() == 4);
    // The last one (3rd) argument means size.
    if (auto size = llvm::dyn_cast<llvm::ConstantInt>(args[3]); size && size->isZero()) {
        return nullptr;
    }
    // real type of third parameter: args[2]->getType()
    llvm::Type* type = llvm::Type::getInt8PtrTy(GetLLVMContext(), args[2]->getType()->getPointerAddressSpace());
    // Func: void @llvm.cj.gcwrite.struct(i8 addr1* baseObj, i8 addr1* dst, i8 addr1*/ i8* src, i64 length)
    auto func = llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_gcwrite_struct, {type, GetSizetLLVMType()});
    args[3] = CreateZExtOrTrunc(args[3], GetSizetLLVMType());
    ConvertArgsType(*this, func, args);
    return CreateCall(func, args);
}

llvm::Instruction* IRBuilder2::CallGCWriteStaticRef(const std::vector<llvm::Value*>& args)
{
    // Func: void @llvm.cj.gcwrite.static(i8 addr1* val, i8 addr1** place)
    return CreateCall(
        llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_gcwrite_static_ref), args);
}

llvm::Instruction* IRBuilder2::CallGCWriteStaticAgg(llvm::StructType* type, std::vector<llvm::Value*> args)
{
    // Func: void @llvm.cj.gcwrite.static.struct(i8* dst, i8* src, i64 len)
    // 3 arguments.
    CJC_ASSERT(args.size() == 3);
    // The last one (2nd) argument means size.
    if (auto size = llvm::dyn_cast<llvm::ConstantInt>(args[2]); size && size->isZero()) {
        return nullptr;
    }
    llvm::LLVMContext& ctx = getContext();
    auto func = llvm::Intrinsic::getDeclaration(
        cgMod.GetLLVMModule(), static_cast<llvm::Intrinsic::ID>(llvm::Intrinsic::cj_gcwrite_static_struct), {GetSizetLLVMType()});
    args[2] = CreateZExtOrTrunc(args[2], GetSizetLLVMType());
    auto typeName = GetCodeGenTypeName(type);
    auto meta = llvm::MDTuple::get(ctx, {llvm::MDString::get(ctx, typeName)});
    ConvertArgsType(*this, func, args);
    auto inst = CreateCall(func, args);
    inst->setMetadata("AggType", meta);
    return inst;
}
llvm::Instruction* IRBuilder2::CreateWriteBarrierForGlobalVariable(const CGValue& value, const CGValue& globalVar)
{
    const CGType* valueType = value.GetCGType();
    CJC_ASSERT(valueType != nullptr);
    llvm::Instruction* inst = nullptr;
    if (valueType->IsStructPtrType()) {
        auto structType = llvm::cast<llvm::StructType>(valueType->GetPointerElementType()->GetLLVMType());
        auto layOut = cgMod.GetLLVMModule()->getDataLayout().getStructLayout(structType);
        auto size = llvm::ConstantInt::get(llvm::Type::getInt64Ty(GetLLVMContext()), layOut->getSizeInBytes());
        inst = CallGCWriteStaticAgg(structType, {globalVar.GetRawValue(), value.GetRawValue(), size});
    } else {
        inst = CallGCWriteStaticRef({value.GetRawValue(), globalVar.GetRawValue()});
    }
    if (llvm::isa<llvm::ConstantPointerNull>(value.GetRawValue()) && inst) {
        inst->setDebugLoc(llvm::DebugLoc());
    }
    return inst;
}

llvm::StoreInst* IRBuilder2::CreateStore(llvm::Value* val, const CGValue& cgDestAddr, bool isVolatile)
{
    return LLVMIRBuilder2::CreateStore(val, *cgDestAddr, isVolatile);
}

llvm::Instruction* IRBuilder2::CreateStore(
    llvm::Value* val, llvm::Value* ptr, const Cangjie::CHIR::Type* type, bool isVolatile)
{
    auto valCGType = CGType::GetOrCreate(cgMod, CGType::GetRefTypeOf(GetCGContext().GetCHIRBuilder(), *type));
    auto ptrCGType = CGType::GetOrCreate(cgMod, CGType::GetRefTypeOf(GetCGContext().GetCHIRBuilder(), *type));
    return CreateStore(CGValue(val, valCGType), CGValue(ptr, ptrCGType));
}

llvm::Instruction* IRBuilder2::CreateStore(const CGValue& cgVal, const CGValue& cgDestAddr)
{
    bool isMemberWrite = GetCGContext().GetBasePtrOf(cgDestAddr.GetRawValue()) != nullptr;
    bool isVolatile = false;
    const CGType* valType = cgVal.GetCGType();
    const CGType* destDerefType = cgDestAddr.GetCGType()->GetPointerElementType();
    CJC_ASSERT(destDerefType != nullptr);
    auto val = cgVal.GetRawValue();
    auto destAddr = cgDestAddr.GetRawValue();
    // GetTypeInfoIsReference
    if (valType && valType->GetOriginal().IsGeneric() && !valType->GetSize() &&
        destDerefType->GetOriginal().IsGeneric() && !destDerefType->GetSize() && !isMemberWrite) {
        auto dstTypeInfo = CreateTypeInfo(destDerefType->GetOriginal());
        // Check whether dstType is a reference.
        auto [handleRefBB, handleNonRefBB, exitBB] = Vec2Tuple<3>(CreateAndInsertBasicBlocks(
            {GenNameForBB("handle_store_ref"), GenNameForBB("handle_store_non_ref"), GenNameForBB("store_exit")}));
        destAddr =
            CreateBitCast(destAddr, getInt8PtrTy(1U)->getPointerTo(destAddr->getType()->getPointerAddressSpace()));
        CreateCondBr(CreateTypeInfoIsReferenceCall(destDerefType->GetOriginal()), handleRefBB, handleNonRefBB);

        SetInsertPoint(handleRefBB);
        LLVMIRBuilder2::CreateStore(val, destAddr, isVolatile);
        CreateBr(exitBB);

        SetInsertPoint(handleNonRefBB);
        llvm::Value* tmpPtr = nullptr;
        if (cgDestAddr.IsSRetArg()) {
            tmpPtr = CreateLoad(cgDestAddr);
        } else {
            auto dstTypeSize = GetLayoutSize_32(destDerefType->GetOriginal());
            tmpPtr = CallIntrinsicAllocaGeneric({dstTypeInfo, dstTypeSize});
            (void)CreateStore(tmpPtr, destAddr);
        }
        std::vector<llvm::Value*> copyGenericParams{tmpPtr, val, dstTypeInfo};
        CallIntrinsicAssignGeneric(copyGenericParams);
        CreateBr(exitBB);

        SetInsertPoint(exitBB);
        return nullptr;
    }

    if (!isMemberWrite) {
        if (destDerefType && !destDerefType->GetOriginal().IsRef() && cgVal.GetCGType() &&
            cgVal.GetCGType()->GetOriginal().IsRef()) {
            if (auto valSize = cgVal.GetCGType()->GetPointerElementType()->GetSize()) {
                if (!destDerefType->GetSize()) {
                    auto dataPtr = GetPayloadFromObject(destAddr);
                    auto size = GetLayoutSize_32(cgVal.GetCGType()->GetOriginal());
                    return CallGCWriteAgg({destAddr, dataPtr, val, size});
                }
            } else {
                CJC_ASSERT(!destDerefType->GetSize());
                if (destAddr->getType()->getPointerAddressSpace() == 1U
                    && val->getType()->getPointerAddressSpace() == 0U) {
                    auto size = GetSize_32(cgVal.GetCGType()->GetOriginal());
                    return CallGCWriteGenericPayload({destAddr, val, size});
                }
                auto ti = CreateTypeInfo(cgVal.GetCGType()->GetOriginal());
                return CallIntrinsicAssignGeneric({destAddr, val, ti});
            }
        }
    }

    // When storing to global variable and cgVal contains a cj class type value,
    // we need to use gc write barrier, i.e. llvm.cj.gcwrite.static.xxx
    if (IsGlobalVariableBasePtr(destAddr) && IsTypeContainsRef(destDerefType->GetLLVMType())) {
        return CreateWriteBarrierForGlobalVariable(cgVal, cgDestAddr);
    }

    auto dealWithGCWriteBarrier = [this, &cgDestAddr, &cgVal, &isMemberWrite, &isVolatile](
                                      const CGType* cgValType, llvm::Value* basePtr) -> llvm::Instruction* {
        if (auto elemType = cgDestAddr.GetCGType()->GetPointerElementType(); !elemType->GetSize()) {
            CJC_ASSERT(isMemberWrite);
            auto size = GetSize_32(cgValType->GetOriginal());
            auto [gcwriteRefBB, gcwriteNonRefBB, gcwriteExitBB] = Vec2Tuple<3>(CreateAndInsertBasicBlocks(
                {GenNameForBB("gcwrite_ref"), GenNameForBB("gcwrite_non_ref"), GenNameForBB("gcwrite_exit")}));
            CreateCondBr(CreateTypeInfoIsReferenceCall(elemType->GetOriginal()), gcwriteRefBB, gcwriteNonRefBB);

            SetInsertPoint(gcwriteRefBB);
            CallGCWrite({cgVal.GetRawValue(), basePtr, cgDestAddr.GetRawValue()});
            CreateBr(gcwriteExitBB);

            SetInsertPoint(gcwriteNonRefBB);
            CallIntrinsicGCWriteGeneric({basePtr, cgDestAddr.GetRawValue(), cgVal.GetRawValue(), size});
            CreateBr(gcwriteExitBB);
            SetInsertPoint(gcwriteExitBB);
            return nullptr;
        }
        if (cgValType->IsStructPtrType()) {
            auto structType = llvm::cast<llvm::StructType>(cgValType->GetPointerElementType()->GetLLVMType());
            auto layOut = cgMod.GetLLVMModule()->getDataLayout().getStructLayout(structType);
            CJC_NULLPTR_CHECK(layOut);
            if (IsTypeContainsRef(cgValType->GetPointerElementType()->GetLLVMType())) {
                if (cgVal.GetRawValue()->getType()->getPointerAddressSpace() == 1U) {
                    auto base = GetCGContext().GetBasePtrOf(cgVal.GetRawValue());
                    auto heapStructType =
                        llvm::cast<llvm::StructType>(cgValType->GetPointerElementType()->GetLLVMType());
                    auto tempVal = CreateEntryAlloca(heapStructType);
                    auto heapLayout = cgMod.GetLLVMModule()->getDataLayout().getStructLayout(heapStructType);
                    auto size = getInt64(heapLayout->getSizeInBytes());
                    CallGCReadAgg({tempVal, base, cgVal.GetRawValue(), size});
                    return CallGCWriteAgg({basePtr, cgDestAddr.GetRawValue(), tempVal, size});
                }
                auto size = llvm::ConstantInt::get(llvm::Type::getInt64Ty(GetLLVMContext()), layOut->getSizeInBytes());
                return CallGCWriteAgg({basePtr, cgDestAddr.GetRawValue(), cgVal.GetRawValue(), size});
            }
            auto align = layOut->getAlignment();
            return CreateMemCpy(cgDestAddr.GetRawValue(), align, cgVal.GetRawValue(), align, layOut->getSizeInBytes());
        } else if (cgValType->IsVArrayPtrType() && cgDestAddr.GetCGType()->IsVArrayPtrType()) {
            auto valueType = cgValType->GetPointerElementType()->GetLLVMType();
            auto layout = cgMod.GetLLVMModule()->getDataLayout();
            auto size = getInt64(layout.getTypeAllocSize(valueType));
            if (IsTypeContainsRef(cgValType->GetPointerElementType()->GetLLVMType())) {
                return CallGCWriteAgg({basePtr, cgDestAddr.GetRawValue(), cgVal.GetRawValue(), size});
            }
            auto align = llvm::Align(layout.getABITypeAlignment(valueType));
            return CreateMemCpy(cgDestAddr.GetRawValue(), align, cgVal.GetRawValue(), align, size);
        } else if (cgValType->IsReference() || cgValType->IsOptionLikeRef()) {
            return CallGCWrite({cgVal.GetRawValue(), basePtr, cgDestAddr.GetRawValue()});
        }
        return LLVMIRBuilder2::CreateStore(*cgVal, *cgDestAddr, isVolatile);
    };

    // When writing to an object field, it need to use GC write barrier, i.e. llvm.cj.gcwrite intrinsic
    if (auto basePtr = cgMod.GetCGContext().GetBasePtrOf(destAddr);
        basePtr && basePtr->getType()->getPointerAddressSpace() == 1) {
        if (auto cgValType = cgVal.GetCGType(); cgValType) {
            return dealWithGCWriteBarrier(cgValType, basePtr);
        }
        return LLVMIRBuilder2::CreateStore(val, destAddr, isVolatile);
    }

    if (cgVal.GetCGType() && cgVal.GetCGType()->IsStructPtrType() && cgDestAddr.GetCGType() &&
        cgDestAddr.GetCGType()->IsStructPtrType()) {
        auto layOut = cgMod.GetLLVMModule()->getDataLayout().getStructLayout(
            llvm::cast<llvm::StructType>(cgVal.GetCGType()->GetPointerElementType()->GetLLVMType()));
        CJC_NULLPTR_CHECK(layOut);
        if (auto sizeInBytes = layOut->getSizeInBytes(); sizeInBytes) {
            auto align = layOut->getAlignment();
            return CreateMemCpy(destAddr, align, val, align, sizeInBytes);
        } else {
            return nullptr;
        }
    }

    if (cgVal.GetCGType() && cgVal.GetCGType()->IsVArrayPtrType() && cgDestAddr.GetCGType() &&
        cgDestAddr.GetCGType()->IsVArrayPtrType()) {
        auto valueType = cgVal.GetCGType()->GetPointerElementType()->GetLLVMType();
        auto size = getInt64(cgMod.GetLLVMModule()->getDataLayout().getTypeAllocSize(valueType));
        auto align = llvm::Align(cgMod.GetLLVMModule()->getDataLayout().getABITypeAlignment(valueType));
        return CreateMemCpy(destAddr, align, val, align, size);
    }
    if (IsFuncPtrType(val->getType()) && IsLitStructPtrType(GetPointerElementType((destAddr)->getType()))) {
        val = CreateBitCast(val, llvm::PointerType::get(llvm::StructType::get(cgMod.GetLLVMContext()), 0));
        return LLVMIRBuilder2::CreateStore(val, destAddr, isVolatile);
    }

    return LLVMIRBuilder2::CreateStore(val, destAddr, isVolatile);
}

namespace {
bool IsLoadExprForAllocatedGenericRef(const CHIR::Expression& chirExpr)
{
    if (chirExpr.GetExprKind() != CHIR::ExprKind::LOAD) {
        return false;
    }

    auto location = StaticCast<const CHIR::Load&>(chirExpr).GetLocation();
    CJC_NULLPTR_CHECK(location);
    if (!location->IsLocalVar() || !IsGenericRef(*location->GetType())) {
        return false;
    }

    auto locationExpr = StaticCast<CHIR::LocalVar*>(location)->GetExpr();
    CJC_NULLPTR_CHECK(locationExpr);
    auto locationExprKind = locationExpr->GetExprKind();
    if (locationExprKind != CHIR::ExprKind::ALLOCATE && locationExprKind != CHIR::ExprKind::ALLOCATE_WITH_EXCEPTION) {
        return false;
    }

    return true;
}
} // namespace

llvm::Value* IRBuilder2::CreateLoad(llvm::Type* elementType, llvm::Value* addr, const llvm::Twine& name)
{
    // For CJNative backend, the structure type is coerced passed by reference,
    // no real load operation happens. To simplify this process, we prefer to do
    // a lazy loading instead of an instant loading. So return the value directly.
    if (IsGlobalVariableBasePtr(addr) && IsTypeContainsRef(elementType)) {
        if (elementType->isStructTy()) {
            auto tempVal = CreateEntryAlloca(elementType);
            auto structType = llvm::cast<llvm::StructType>(elementType);
            auto layOut = GetLLVMModule()->getDataLayout().getStructLayout(structType);
            auto size = getInt64(layOut->getSizeInBytes());
            CallGCReadStaticAgg(structType, {tempVal, addr, size});
            return tempVal;
        } else {
            return CallGCReadStaticRef({addr});
        }
    }
    if (elementType->isStructTy() && elementType->getStructName() == UNIT_TYPE_STR) {
        return cgMod.GenerateUnitTypeValue();
    }
    if (elementType->isStructTy()) {
        auto tempVal = CreateEntryAlloca(elementType);
        if (auto base = GetCGContext().GetBasePtrOf(addr);
            base && base->getType()->getPointerAddressSpace() == 1 && IsTypeContainsRef(elementType)) {
            auto structType = llvm::cast<llvm::StructType>(elementType);
            auto layOut = GetLLVMModule()->getDataLayout().getStructLayout(structType);
            auto size = getInt64(layOut->getSizeInBytes());
            CallGCReadAgg({tempVal, base, addr, size});
        } else {
            auto layOut = GetLLVMModule()->getDataLayout().getStructLayout(llvm::cast<llvm::StructType>(elementType));
            CJC_NULLPTR_CHECK(layOut);
            if (auto sizeInBytes = layOut->getSizeInBytes(); sizeInBytes) {
                auto align = layOut->getAlignment();
                (void)CreateMemCpy(tempVal, align, addr, align, sizeInBytes);
            }
        }
        return tempVal;
    }
    if (elementType->isArrayTy()) {
        auto tempVal = CreateEntryAlloca(elementType);
        auto size = getInt64(GetLLVMModule()->getDataLayout().getTypeAllocSize(elementType));
        auto align = llvm::Align(GetLLVMModule()->getDataLayout().getABITypeAlignment(elementType));
        (void)CreateMemCpy(tempVal, align, addr, align, size);
        return tempVal;
    }
    if (elementType->isFunctionTy()) {
        return addr;
    }
    // If the field is of generic type:
    if (this->chirExpr &&
        (this->chirExpr->GetExprKind() == CHIR::ExprKind::INTRINSIC ||
            this->chirExpr->GetExprKind() == CHIR::ExprKind::LOAD ||
            this->chirExpr->GetExprKind() == CHIR::ExprKind::FIELD)) {
        auto elemCHIRType = this->chirExpr->GetResult()->GetType();
        if (auto t = DeRef(*elemCHIRType); t && t->IsGeneric()) {
            if (IsLoadExprForAllocatedGenericRef(chirExpr->GetChirExpr())) {
                return LLVMIRBuilder2::CreateLoad(elementType, addr, name);
            }
            auto [handleRefBB, handleNonRefBB, exitBB] = Vec2Tuple<3>(CreateAndInsertBasicBlocks(
                {GenNameForBB("handle_load_ref"), GenNameForBB("handle_load_non_ref"), GenNameForBB("load_exit")}));
            auto ti = CreateTypeInfo(*t);
            CreateCondBr(CreateTypeInfoIsReferenceCall(*t), handleRefBB, handleNonRefBB);
            // gcread.ref while T is reference type:
            SetInsertPoint(handleRefBB);
            llvm::Value* refVal{nullptr};
            if (auto base = GetCGContext().GetBasePtrOf(addr)) {
                refVal = CallGCRead({base, addr});
            } else {
                auto addrFixedType = getInt8PtrTy(1)->getPointerTo(addr->getType()->getPointerAddressSpace());
                auto casetedAddr = CreateBitCast(addr, addrFixedType);
                refVal = LLVMIRBuilder2::CreateLoad(getInt8PtrTy(1), casetedAddr);
            }
            CreateBr(exitBB);
            // gcread.generic while T is value type:
            SetInsertPoint(handleNonRefBB);
            auto tiSize = GetLayoutSize_32(*t);
            auto valueVal = CallIntrinsicAllocaGeneric({ti, tiSize});
            if (auto base = GetCGContext().GetBasePtrOf(addr)) {
                CallGCReadGeneric({valueVal, base, addr, tiSize});
            } else if (addr->getType() == getInt8PtrTy()) {
                CallGCWriteGenericPayload({valueVal, addr, tiSize});
            } else {
                CJC_ASSERT(addr->getType() == getInt8PtrTy(1U)->getPointerTo());
                CallIntrinsicAssignGeneric({valueVal, LLVMIRBuilder2::CreateLoad(getInt8PtrTy(1), addr), ti});
            }
            CreateBr(exitBB);
            SetInsertPoint(exitBB);
            auto phi = CreatePHI(elementType, 2U);
            phi->addIncoming(refVal, handleRefBB);
            phi->addIncoming(valueVal, handleNonRefBB);
            return phi;
        } else if (!CGType::GetOrCreate(cgMod, elemCHIRType)->GetSize()) {
            auto tiOfElement = CreateTypeInfo(elemCHIRType);
            auto tiSize = GetSizeFromTypeInfo(tiOfElement);
            auto valueVal = CallIntrinsicAllocaGeneric({tiOfElement, tiSize});
            if (auto base = GetCGContext().GetBasePtrOf(addr)) {
                CallGCReadGeneric({valueVal, base, addr, tiSize});
            } else if (addr->getType() == getInt8PtrTy()) {
                CallGCWriteGenericPayload({valueVal, addr, tiSize});
            } else {
                CJC_ASSERT(addr->getType() == getInt8PtrTy(1U)->getPointerTo());
                CallIntrinsicAssignGeneric({valueVal, LLVMIRBuilder2::CreateLoad(getInt8PtrTy(1), addr), tiOfElement});
            }
            return valueVal;
        }
    }
    if (auto base = GetCGContext().GetBasePtrOf(addr); base && elementType == CGType::GetRefType(GetLLVMContext())) {
        CJC_ASSERT(base->getType()->getPointerAddressSpace() == 1);
        return CallGCRead({base, addr});
    }
    return LLVMIRBuilder2::CreateLoad(elementType, addr, name);
}

llvm::CallInst* IRBuilder2::CreateMemsetStructWith0(llvm::AllocaInst* addr)
{
    auto curPt = LLVMIRBuilder2::GetInsertPoint();
    auto curBB = LLVMIRBuilder2::GetInsertBlock();
    CJC_ASSERT(curBB && "Can't get the current basic block.");
    auto curFunc = curBB->getParent();
    CJC_ASSERT(curFunc && "Can't get the current function.");
    auto entryBB = &curFunc->getEntryBlock();
    auto currentLoc = getCurrentDebugLocation();
    SetInsertPointToAllocas(curFunc);
    SetInsertPoint(entryBB->getTerminator());

    CJC_ASSERT(addr->getAllocatedType()->isStructTy() && "incorrect argument");
    auto structType = llvm::cast<llvm::StructType>(addr->getAllocatedType());
    auto layOut = GetLLVMModule()->getDataLayout().getStructLayout(structType);
    CJC_NULLPTR_CHECK(layOut);
    auto size = layOut->getSizeInBytes();
    auto align = layOut->getAlignment();
    auto memSetInst = CreateMemSet(addr, getInt8(0), size, align);

    LLVMIRBuilder2::SetInsertPoint(curBB, curPt);
    SetCurrentDebugLocation(currentLoc);
    return memSetInst;
}

llvm::CallInst* IRBuilder2::CreateCJMemSetStructWith0(llvm::AllocaInst* addr)
{
    CJC_ASSERT(addr->getAllocatedType()->isStructTy() && "incorrect argument");
    auto structType = llvm::cast<llvm::StructType>(addr->getAllocatedType());
    auto layOut = GetLLVMModule()->getDataLayout().getStructLayout(structType);
    CJC_NULLPTR_CHECK(layOut);
    auto size = layOut->getSizeInBytes();
    auto align = layOut->getAlignment();
    auto currentLoc = getCurrentDebugLocation();
    SetCurrentDebugLocation(llvm::DebugLoc());
    auto res = CreateMemSet(addr, getInt8(0), size, align);
    SetCurrentDebugLocation(currentLoc);
    auto cjMemsetFunc = llvm::Intrinsic::getDeclaration(
        GetLLVMModule(), llvm::Intrinsic::cj_memset, {*res->getFunctionType()->param_begin()});
    res->setCalledFunction(cjMemsetFunc);
    return res;
}

llvm::Value* IRBuilder2::CreateNullValue(const CHIR::Type& ty)
{
    CGType* cgType = CGType::GetOrCreate(cgMod, &ty);
    // There are some conventions for creating null value of a type:
    // -----------------------------------------------------------------------------------------
    //  Codegen    |  GV(global variable)  |  LV(local variable)
    // ------------|-----------------------|----------------------------------------------------
    //  CJNATIVE     |  llvm::Constant::     |  1) struct type: memset a struct with 0, returns
    //             |  getNullValue         |     the struct;
    //             |                       |  2) non-struct type: llvm::Constant::getNullValue;
    //             |                       |  3) array type: return alloca arrayType;
    // -----------------------------------------------------------------------------------------
    // It indicates that we are generating null value for a GV while the current Insert Block is nullptr.
    llvm::Type* type = cgType->GetLLVMType();
    if (!GetInsertBlock()) {
        return llvm::Constant::getNullValue(type);
    }
    if (!cgType->GetSize()) {
        auto typeInfoOfSrc = CreateTypeInfo(ty);
        std::vector<llvm::Value*> parameters{typeInfoOfSrc, GetLayoutSize_32(ty)};
        return CallIntrinsicAllocaGeneric(parameters);
    } else if (type == CGType::GetUnitCGType(cgMod)->GetLLVMType()) {
        return cgMod.GenerateUnitTypeValue();
    } else if (type->isStructTy()) {
        auto alloca = CreateEntryAlloca(type);
        // Why we just handle with the structs that don't contain ref element?
        // Since `AddZeroInitForStructWithRefField` method will be invoked to deal with the
        // structs that contain ref element(s) uniformly.
        if (!IsTypeContainsRef(llvm::cast<llvm::StructType>(alloca->getAllocatedType()))) {
            (void)CreateMemsetStructWith0(alloca);
        }
        return alloca;
    } else if (type->isArrayTy()) {
        auto alloca = CreateEntryAlloca(type);
        auto typeSize = GetCGModule().GetTypeSize(type);

        size_t idx = 0;
        constexpr auto SIZE_OF_2G = INT32_MAX;
        while (idx + SIZE_OF_2G < typeSize) {
            auto dataPtr = CreateGEP(type, alloca, {getInt64(0), getInt64(idx)});
            (void)CreateMemSet(dataPtr, getInt8(0), SIZE_OF_2G, llvm::MaybeAlign(1));
            idx += SIZE_OF_2G;
        }
        auto dataPtr = CreateGEP(type, alloca, {getInt64(0), getInt64(idx)});
        (void)CreateMemSet(dataPtr, getInt8(0), typeSize - idx, llvm::MaybeAlign(1));
        return alloca;
    }
    return llvm::Constant::getNullValue(type);
}

llvm::Value* IRBuilder2::CreateGEP(
    llvm::Type* type, llvm::Value* value, llvm::ArrayRef<llvm::Value*> place, const llvm::Twine& name)
{
    auto res = CreateInBoundsGEP(type, value, place, name);
    if (auto basePtr = GetCGContext().GetBasePtrOf(value)) {
        GetCGContext().SetBasePtr(res, basePtr);
    } else if (value->getType()->getPointerAddressSpace() == 1) {
        GetCGContext().SetBasePtr(res, value);
    }
    return res;
}

CGValue IRBuilder2::CreateGEP(
    const CGValue& cgVal, const std::vector<uint64_t>& idxList, [[maybe_unused]] const llvm::Twine& name)
{
    std::vector<llvm::Value*> idxes;
    auto ret = cgVal.GetRawValue();
    auto eleType = cgVal.GetCGType()->GetPointerElementType();
    CJC_NULLPTR_CHECK(eleType);
    llvm::Value* basePtr = GetCGContext().GetBasePtrOf(ret);
    bool hasSkippedTypeInfo = basePtr && !eleType->IsReference();
    if (!basePtr && !eleType->GetSize() && ret->getType() == getInt8PtrTy(1U)) {
        basePtr = ret;
    }
    // Sometimes we need to skip TypeInfo* at most 1 time.
    // For example:
    //      %x: Tuple<Int64,Tuple<Generic-T0,Generic-_T1>> = ...
    //      %field: Generic-T1 = Field(%x, 1, 1)
    //                                      \  \__ no TypeInfo* to skip when getting the second subscript element
    //                                       \__ skip the TypeInfo* of %x when getting the first subscript element
    for (auto idx : idxList) {
        if (eleType->IsReference() || eleType->IsOptionLikeRef()) {
            // Cache the basePtr for the field
            basePtr = ret;
        }
        size_t offsetForAutoEnv = 0U;
        if (eleType->GetOriginal().IsAutoEnv()) {
            offsetForAutoEnv = 2U;
        }
        // Firstly, get the element.
        if (chirExpr && !eleType->GetSize()) {
            std::vector<llvm::Value*> params = {
                CreateTypeInfo(eleType->GetOriginal()), getInt64(idx + offsetForAutoEnv)};
            if (eleType->GetOriginal().IsRawArray()) {
                /* pre-offset: arrLength */
                params.emplace_back(getInt32(8U));
            }
            llvm::Value* offset = CallIntrinsicGetFieldOffset(params);
            CJC_NULLPTR_CHECK(chirExpr->GetTopLevelFunc());
            CJC_NULLPTR_CHECK(GetInsertFunction());
            if ((GetInsertFunction()->hasFnAttribute(HAS_WITH_TI_WRAPPER_ATTR) ||
                    GetInsertFunction()->hasFnAttribute(THIS_PARAM_HAS_BP)) &&
                **(cgMod | chirExpr->GetTopLevelFunc()->GetParam(0)) == ret) {
                // It means this param doesn't have typeinfo, no need to add the offset.
            } else if (!hasSkippedTypeInfo) {
                offset = CreateAdd(offset, llvm::ConstantInt::get(offset->getType(), GetPayloadOffset()));
                hasSkippedTypeInfo = true;
            }
            ret = CreateInBoundsGEP(getInt8Ty(), ret, offset);
        } else if (auto classCGType = dynamic_cast<const CGClassType*>(eleType)) {
            auto baseType = classCGType;
            ret = GetPayloadFromObject(ret);
            auto layoutType = static_cast<const CGClassType*>(baseType)->GetLayoutType();
            ret = CreateBitCast(ret, layoutType->getPointerTo(1));
            ret = LLVMIRBuilder2::CreateStructGEP(layoutType, ret, idx + offsetForAutoEnv);
        } else if (auto arrayCGType = dynamic_cast<const CGArrayType*>(eleType)) {
            ret = GetArrayElementAddr(
                StaticCast<const CHIR::RawArrayType&>(arrayCGType->GetOriginal()), ret, getInt64(idx), false);
        } else {
            CJC_ASSERT(!eleType->GetOriginal().IsRef());
            ret = CreateBitCast(ret, eleType->GetLLVMType()->getPointerTo(ret->getType()->getPointerAddressSpace()));
            ret = LLVMIRBuilder2::CreateStructGEP(eleType->GetLLVMType(), ret, idx);
        }
        // Then, update the eleType for next iteration.
        if (auto arrayCGType = dynamic_cast<const CGArrayType*>(eleType)) {
            eleType = arrayCGType->GetElementCGType();
        } else {
            eleType = eleType->GetContainedTypeAt(idx + offsetForAutoEnv);
            if (!eleType->GetOriginal().IsGeneric()) {
                ret =
                    CreateBitCast(ret, eleType->GetLLVMType()->getPointerTo(ret->getType()->getPointerAddressSpace()));
            }
        }
        // Set the basePtr of the field, if any.
        if (basePtr) {
            GetCGContext().SetBasePtr(ret, basePtr);
        }
    }
    auto eleRefType =
        CGType::GetOrCreate(cgMod, CGType::GetRefTypeOf(GetCGContext().GetCHIRBuilder(), eleType->GetOriginal()));
    return CGValue(ret, eleRefType);
}

llvm::Value* IRBuilder2::InitArrayFilledWithConstant(llvm::Value* array, const CHIR::RawArrayType& arrTy,
    const std::vector<llvm::Constant*>& arrayConstantElems, const std::string& serialized)
{
    auto arrayType = static_cast<CGArrayType*>(CGType::GetOrCreate(cgMod, &arrTy))->GetLayoutType();
    auto type = CGType::GetOrCreate(cgMod, arrTy.GetElementType());
    CJC_NULLPTR_CHECK(type);
    auto elemType = type->IsStructPtrType() ? type->GetPointerElementType() : type;
    auto arrayLitLen = arrayConstantElems.size();
    auto arrayLitType = llvm::ArrayType::get(elemType->GetLLVMType(), arrayLitLen);
    auto constVal = llvm::ConstantArray::get(arrayLitType, arrayConstantElems);
    auto layOut = GetLLVMModule()->getDataLayout();
    auto& cgCtx = GetCGContext();
    llvm::GlobalVariable* arrayConstant = GetCGModule().GetOrCreateGlobalVariable(constVal, serialized, false);
    auto arrPayloadPtr = GetPayloadFromObject(array);
    auto arrPtr = CreateBitCast(arrPayloadPtr, arrayType->getPointerTo(1u));
    auto arrDataPtr = CreateStructGEP(arrayType, arrPtr, 1);
    auto srcArrI8Ptr = CreatePointerBitCastOrAddrSpaceCast(arrayConstant, getInt8PtrTy(1));
    auto dstArrI8Ptr = CreateBitCast(arrDataPtr, getInt8PtrTy(1));
    auto int64Type = llvm::Type::getInt64Ty(cgCtx.GetLLVMContext());
    auto size = layOut.getTypeAllocSize(arrayConstant->getValueType());
    auto dataSize = llvm::ConstantInt::get(int64Type, size);
    CreateCopyTo(ArrayCopyToInfo(srcArrI8Ptr, array, srcArrI8Ptr, dstArrI8Ptr, dataSize, getInt64(0), getInt64(0),
        elemType, getInt64(arrayLitLen)));
    return array;
}

void IRBuilder2::CreateCopyTo(ArrayCopyToInfo arrayCopyToInfo)
{
    auto i8Ty = llvm::Type::getInt8Ty(GetLLVMContext());
    auto i8PtrTy = llvm::Type::getInt8PtrTy(GetLLVMContext(), 1u);
    auto srcArrPtr = CreateBitCast(arrayCopyToInfo.srcArrPtr, i8PtrTy);
    auto dstArrPtr = CreateBitCast(arrayCopyToInfo.dstArrPtr, i8PtrTy);
    auto srcArrI8Ptr = CreateGEP(i8Ty, srcArrPtr, arrayCopyToInfo.srcIndex);
    auto dstArrI8Ptr = CreateGEP(i8Ty, dstArrPtr, arrayCopyToInfo.dstIndex);
    auto elemCGType = arrayCopyToInfo.elemType;

    if (!elemCGType->GetSize()) {
        auto func = llvm::Intrinsic::getDeclaration(
            GetLLVMModule(), static_cast<llvm::Intrinsic::ID>(llvm::Intrinsic::cj_array_copy_generic), {GetSizetLLVMType()});
        CreateCall(func->getFunctionType(), func,
            {arrayCopyToInfo.dstBP, dstArrI8Ptr, arrayCopyToInfo.srcBP, srcArrI8Ptr, CreateZExtOrTrunc(arrayCopyToInfo.dataSize, GetSizetLLVMType())});
        return;
    }

    if (IsTypeContainsRef(elemCGType->GetLLVMType())) {
        bool isRefType = arrayCopyToInfo.elemType->GetLLVMType() == CGType::GetRefType(GetLLVMContext());
        auto copyToInstrinsic = isRefType ? llvm::Intrinsic::cj_array_copy_ref : llvm::Intrinsic::cj_array_copy_struct;
        auto func =
            llvm::Intrinsic::getDeclaration(GetLLVMModule(), static_cast<llvm::Intrinsic::ID>(copyToInstrinsic), {GetSizetLLVMType()});
        // llvm.cj.array.copyto(i8 addrspace(1)* destBP, i8 addrspace(1)* <dest>,
        //    i8 addrspace(1)* srcBP, i8
        // addrspace(1)* <src>,  i64 <len>)
        CreateCall(func->getFunctionType(), func,
            {arrayCopyToInfo.dstBP, dstArrI8Ptr, arrayCopyToInfo.srcBP, srcArrI8Ptr, CreateZExtOrTrunc(arrayCopyToInfo.dataSize, GetSizetLLVMType())});
    } else {
        auto align = llvm::Align(GetLLVMModule()->getDataLayout().getPrefTypeAlignment(elemCGType->GetLLVMType()));
        CreateMemMove(dstArrI8Ptr, align, srcArrI8Ptr, align, arrayCopyToInfo.dataSize); // Copy array data.
    }
}

llvm::Value* IRBuilder2::CallArrayIntrinsicAllocWithConstantContent(llvm::Value* array,
    const std::vector<CGValue*>& args, const CHIR::RawArrayType& arrTy, const std::string& serialized)
{
    // For the array literal that filled with constants
    // e.g., [1, 3, 5],
    // it is better to be initialized with array constant by using `memcpy`.
    auto isAllElemsConstant = IsAllConstantValue(args);
    CJC_ASSERT(isAllElemsConstant);
    std::vector<llvm::Constant*> cons(args.size());
    std::vector<llvm::Value*> newArgs;
    std::for_each(args.begin(), args.end(), [&newArgs](const CGValue* it) { newArgs.emplace_back(it->GetRawValue()); });
    std::transform(newArgs.begin(), newArgs.end(), cons.begin(), [](llvm::Value* it) {
        auto tmp = llvm::dyn_cast<llvm::GlobalVariable>(it);
        return tmp ? tmp->getInitializer() : llvm::cast<llvm::Constant>(it);
    });
    return InitArrayFilledWithConstant(array, arrTy, cons, serialized);
}

llvm::Value* IRBuilder2::AllocateArray(const CHIR::RawArrayType& rawArrayType, llvm::Value* length)
{
    auto& llvmCtx = GetLLVMContext();
    auto rawArrayTI = CreateBitCast(CreateTypeInfo(rawArrayType), llvm::Type::getInt8PtrTy(llvmCtx, 0));
    auto int64Type = llvm::Type::getInt64Ty(llvmCtx);
    if (length->getType()->getIntegerBitWidth() < int64Type->getIntegerBitWidth()) {
        length = CreateSExt(length, int64Type);
    }
    auto elementSize = GetSize_64(*rawArrayType.GetElementType());
    bool isArrayElementGeneric = CGType::GetOrCreate(cgMod, rawArrayType.GetElementType())->IsDynamicGI();
    auto allocFunc = isArrayElementGeneric ? GetArrayGenericElemMalloc() : GetArrayNonGenericElemMalloc();
    std::vector<llvm::Value*> args = {rawArrayTI, length};
    if (!isArrayElementGeneric) {
        args.emplace_back(elementSize);
    }
    auto callInst = CreateCallOrInvoke(allocFunc, args);
    AddRetAttr(llvm::cast<llvm::CallBase>(callInst), llvm::Attribute::NoAlias);
    return callInst;
}

llvm::Function* IRBuilder2::GetArrayNonGenericElemMalloc() const
{
    auto gcArrayMallocFunc =
        llvm::Intrinsic::getDeclaration(GetLLVMModule(), static_cast<unsigned>(llvm::Intrinsic::cj_malloc_array));
    AddRetAttr(gcArrayMallocFunc, llvm::Attribute::NoAlias);
    return gcArrayMallocFunc;
}

llvm::Function* IRBuilder2::GetArrayGenericElemMalloc() const
{
    auto gcArrayMallocFunc = llvm::Intrinsic::getDeclaration(
        GetLLVMModule(), static_cast<unsigned>(llvm::Intrinsic::cj_malloc_array_generic));
    AddRetAttr(gcArrayMallocFunc, llvm::Attribute::NoAlias);
    return gcArrayMallocFunc;
}

llvm::Value* IRBuilder2::CallArrayIntrinsicInitWithContent(const CHIR::RawArrayLiteralInit& arrayLiteralInit)
{
    auto arrTy = arrayLiteralInit.GetRawArray()->GetType()->GetTypeArgs()[0];
    CJC_ASSERT(arrTy->GetTypeKind() == CHIR::Type::TYPE_RAWARRAY);
    auto value = cgMod.GetMappedValue(arrayLiteralInit.GetRawArray());
    CJC_ASSERT(value);
    auto arrayV = **value;
    auto rawArrTy = static_cast<CHIR::RawArrayType*>(arrTy);
    auto elements = arrayLiteralInit.GetElements();
    auto [isConstantArray, serialized] = IsConstantArray(arrayLiteralInit);
    if (isConstantArray) {
        std::vector<CGValue*> params;
        for (auto ele : elements) {
            params.emplace_back(cgMod.GetMappedValue(ele));
        }
        return CallArrayIntrinsicAllocWithConstantContent(arrayV, params, *rawArrTy, serialized);
    }

    auto elemType = CGType::GetOrCreate(cgMod, rawArrTy->GetElementType());
    for (size_t i = 0; i < elements.size(); ++i) {
        auto element = cgMod.GetMappedValue(elements[i]);
        CJC_ASSERT(element);
        llvm::Value* elemValue = element->GetRawValue();
        CJC_ASSERT(elemValue);
#ifdef NDEBUG
        auto idxName = "arr.idx" + std::to_string(i) + "E";
#else
        auto idxName = "arr" + arrayLiteralInit.GetResult()->GetSrcCodeIdentifier() + ".idx" + std::to_string(i) + "E";
#endif
        auto elemPtr = CreateGEP(*value, {i}, idxName).GetRawValue();
        // fixme need to implement CreateRefStore
        if (elemType->IsStructType()) {
            auto structType = llvm::cast<llvm::StructType>(elemType->GetLLVMType());
            auto layOut = GetLLVMModule()->getDataLayout().getStructLayout(structType);
            auto size = getInt64(layOut->getSizeInBytes());
            CJC_NULLPTR_CHECK(layOut);
            if (IsTypeContainsRef(elemType->GetLLVMType())) {
                CallGCWriteAgg({arrayV, elemPtr, elemValue, size});
            } else {
                auto align = layOut->getAlignment();
                CreateMemCpy(elemPtr, align, elemValue, align, size);
            }
        } else if (elemType->IsVArrayType()) {
            auto layout = GetLLVMModule()->getDataLayout();
            auto size = getInt64(layout.getTypeAllocSize(elemType->GetLLVMType()));
            if (IsTypeContainsRef(elemType->GetLLVMType())) {
                CallGCWriteAgg({arrayV, elemPtr, elemValue, size});
            } else {
                auto align = llvm::Align(layout.getABITypeAlignment(elemType->GetLLVMType()));
                CreateMemCpy(elemPtr, align, elemValue, align, size);
            }
        } else if (elemType->IsRefType()) {
            CallGCWrite({elemValue, arrayV, elemPtr});
        } else {
            CreateStore(elemValue, elemPtr);
        }
    }
    return arrayV;
}

llvm::Value* IRBuilder2::CreateStringLiteral(const std::string& str)
{
    std::string cjStringName = GetCjStringLiteralName(str);
    auto cachedCjString = cgMod.GetLLVMModule()->getNamedValue(cjStringName);
    if (cachedCjString) {
        return cachedCjString;
    }

    // Generate ArrayLayout.UInt8 here to ensure it is inserted to func for_keeping_some_types$
    (void)CGArrayType::GenerateArrayLayoutTypeInfo(cgMod.GetCGContext(), ARRAY_LAYOUT_PREFIX + "UInt8", getInt8Ty());

    // in Cangjie
    // struct String {
    //      private let myData: RawArray<UInt8>
    //      private let start: UInt32 = 0
    //      private let length: UInt32 = 0
    // }
    // in LLVM IR:
    // cj string = type { i8 addrspace(1)*, i32, i32 }
    auto cjStringType = cgMod.GetCGContext().GetCjStringType();
    auto gvCjString =
        llvm::cast<llvm::GlobalVariable>(cgMod.GetLLVMModule()->getOrInsertGlobal(cjStringName, cjStringType));
    CJC_NULLPTR_CHECK(gvCjString);
    gvCjString->setConstant(true);
    gvCjString->addAttribute(CJSTRING_LITERAL_ATTR);
    gvCjString->setLinkage(llvm::GlobalValue::PrivateLinkage);
    GetCGContext().AddCJString(gvCjString->getName().str(), str);
    return gvCjString;
}

namespace {
struct ByteArrayInfo {
    llvm::Value* arrPtr{nullptr};
    llvm::Type* arrType{nullptr};
    llvm::Value* dataSize{nullptr};
    llvm::Value* index{nullptr};
};

void CreateArrayMemSet(IRBuilder2& irBuilder, const ByteArrayInfo& arrInfo, llvm::Value* value)
{
    auto& layout = irBuilder.GetCGModule().GetLLVMModule()->getDataLayout();
    bool isInputValid = layout.getTypeAllocSize(value->getType()) == 1;
    CJC_ASSERT(isInputValid && "The type size of the value used to initialize the byte-array should be one byte.");
    // There is no gc points between allocation and initialization of `arrPtr`, so
    // `memset` is feasible to be used.
    // The type of `value` could be i1 or i8.
    value = value->getType()->isIntegerTy(8) ? value : irBuilder.CreateZExt(value, irBuilder.getInt8Ty());
    auto dataPtr = irBuilder.CreateGEP(
        arrInfo.arrType, arrInfo.arrPtr, {irBuilder.getInt32(0), irBuilder.getInt32(1), arrInfo.index});
    irBuilder.CreateMemSet(dataPtr, value, arrInfo.dataSize, llvm::MaybeAlign(1));
}

/**
 * Initialize an array with `memset`.
 * Since the restriction of `memset`, we have to split the array as small chunks if the array size >= 2GB,
 * and initialize each chunk.
 * -------------- Pseudocode in Cangjie -------------
 * var iterator = 0
 * while(iterator + 2GB) < arrayLen) {
 *      memset(arrayPtr, // address of the array
               2GB,       // size to memset
               iterator, // start index to memset of the array
               value)    // value to fill into the array
 *      iterator += 2GB
 * }
 * memset(arrayPtr, arrayLen - iterator, iterator, value)
*/
void InitByteArrayData(
    IRBuilder2& irBuilder, llvm::Value* arrPtr, llvm::Value* arrayLen, llvm::Value* value, llvm::Type* arrayType)
{
    auto arrayPtr = irBuilder.CreateBitCast(irBuilder.GetPayloadFromObject(arrPtr), arrayType->getPointerTo(1u));
    auto valType = value->getType();
    value = valType->isPointerTy()
        ? irBuilder.CreateLoad(irBuilder.getInt8Ty(),
              irBuilder.CreateBitCast(value, irBuilder.getInt8PtrTy(valType->getPointerAddressSpace())))
        : value;
    // In order to avoid long IR, If array size less than or equal to INT32_MAX, quit early.
    if (auto constVal = llvm::dyn_cast<llvm::ConstantInt>(arrayLen);
        constVal && *constVal->getValue().getRawData() <= INT32_MAX) {
        CreateArrayMemSet(irBuilder, {arrayPtr, arrayType, arrayLen, irBuilder.getInt64(0)}, value);
        return;
    }

    auto [whileLoopHeader, whileLoopBody, afterWhileLoop] = Vec2Tuple<3>(irBuilder.CreateAndInsertBasicBlocks(
        {GenNameForBB("byte.arr.init.start"), GenNameForBB("byte.arr.init.body"), GenNameForBB("byte.arr.init.end")}));

    const auto SIZE_OF_2G = irBuilder.getInt64(INT32_MAX);
    auto i64Ty = irBuilder.getInt64Ty();
    auto iterator = irBuilder.CreateEntryAlloca(i64Ty, nullptr, "iterator");
    (void)irBuilder.CreateStore(irBuilder.getInt64(0), iterator);
    (void)irBuilder.CreateBr(whileLoopHeader);

    // generate whileLoopHeader
    // `while(iterator + 2G) < arrayLen) {`
    irBuilder.SetInsertPoint(whileLoopHeader);
    auto cmpResult = irBuilder.CreateICmpSLT(
        irBuilder.CreateAdd(irBuilder.CreateLoad(i64Ty, iterator), SIZE_OF_2G), arrayLen, "max.size");
    (void)irBuilder.CreateCondBr(cmpResult, whileLoopBody, afterWhileLoop);

    // generate whileLoopBody
    // `memset(arrayPtr, 2G, iterator,  value)`
    irBuilder.SetInsertPoint(whileLoopBody);
    auto iteratorIndex = irBuilder.CreateLoad(i64Ty, iterator);
    CreateArrayMemSet(irBuilder, {arrayPtr, arrayType, SIZE_OF_2G, iteratorIndex}, value);
    // `iterator += 2G`
    auto newIterator = irBuilder.CreateAdd(iteratorIndex, SIZE_OF_2G);
    (void)irBuilder.CreateStore(newIterator, iterator);
    (void)irBuilder.CreateBr(whileLoopHeader);

    // generate afterWhileLoop
    // `memset(arrayPtr, arrayLen - iterator, iterator, value)`
    irBuilder.SetInsertPoint(afterWhileLoop);
    auto index = irBuilder.CreateLoad(i64Ty, iterator);
    auto residualSize = irBuilder.CreateSub(arrayLen, index);
    CreateArrayMemSet(irBuilder, {arrayPtr, arrayType, residualSize, index}, value);
}

void InitArrayDataUG(IRBuilder2& irBuilder, llvm::Value* arrPtr, llvm::Value* arrayLen, llvm::Value* value,
    llvm::Type* arrayType, const CHIR::RawArrayType& arrTy)
{
    // Prepare basic blocks.
    auto [startBB, bodyBB, endBB] = Vec2Tuple<3>(irBuilder.CreateAndInsertBasicBlocks(
        {GenNameForBB("arr.init.start"), GenNameForBB("arr.init.body"), GenNameForBB("arr.init.end")}));
    auto isNotZeroLen = irBuilder.CreateIntrinsic(llvm::Intrinsic::expect, {irBuilder.getInt1Ty()},
        {irBuilder.CreateICmpNE(arrayLen, irBuilder.getInt64(0)), irBuilder.getTrue()});
    (void)irBuilder.CreateCondBr(isNotZeroLen, startBB, endBB);
    irBuilder.SetInsertPoint(startBB);
    auto i64Ty = irBuilder.getInt64Ty();
    // Prepare iterator to assign the array element.
    auto iterator = irBuilder.CreateEntryAlloca(i64Ty, nullptr, "iterator");
    (void)irBuilder.CreateStore(irBuilder.getInt64(0), iterator, true);
    (void)irBuilder.CreateBr(bodyBB);
    // Generate body.
    irBuilder.SetInsertPoint(bodyBB);
    auto index = irBuilder.CreateLoad(i64Ty, iterator);
    llvm::Value* offset = irBuilder.CallIntrinsicGetFieldOffset({irBuilder.CreateTypeInfo(arrTy), index,
        /* pre-offset: arrLength */
        irBuilder.getInt32(8U)});
    offset = irBuilder.CreateAdd(offset, llvm::ConstantInt::get(offset->getType(), irBuilder.GetPayloadOffset()));
    auto fieldType = arrTy.GetElementType();
    auto fieldAddr = irBuilder.CreateInBoundsGEP(irBuilder.getInt8Ty(), arrPtr, offset);
    auto fieldAddrCGType = CGType::GetOrCreate(
        irBuilder.GetCGModule(), CGType::GetRefTypeOf(irBuilder.GetCGContext().GetCHIRBuilder(), *fieldType));

    auto fieldCGType = CGType::GetOrCreate(irBuilder.GetCGModule(), fieldType);
    if (!fieldType->IsGeneric()) {
        fieldAddr = irBuilder.CreateBitCast(fieldAddr, fieldCGType->GetLLVMType()->getPointerTo(1U));
    }
    irBuilder.GetCGContext().SetBasePtr(fieldAddr, arrPtr);
    irBuilder.CreateStore(CGValue(value, fieldCGType), CGValue(fieldAddr, fieldAddrCGType));
    // Generate increment and compare size.
    auto incremented = irBuilder.CreateAdd(index, irBuilder.getInt64(1));
    (void)irBuilder.CreateStore(incremented, iterator);
    auto cmpResult = irBuilder.CreateICmpSLT(incremented, arrayLen, "iterator.size.cmp");
    (void)irBuilder.CreateCondBr(cmpResult, bodyBB, endBB);
    // Change to end BB.
    irBuilder.SetInsertPoint(endBB);
}

void InitArrayData(
    IRBuilder2& irBuilder, llvm::Value* arrPtr, llvm::Value* arrayLen, llvm::Value* value, llvm::Type* arrayType)
{
    // Prepare basic blocks.
    auto [startBB, bodyBB, endBB] = Vec2Tuple<3>(irBuilder.CreateAndInsertBasicBlocks(
        {GenNameForBB("arr.init.start"), GenNameForBB("arr.init.body"), GenNameForBB("arr.init.end")}));

    // Prepare iterator to assign the array element.
    (void)irBuilder.CreateBr(startBB);
    irBuilder.SetInsertPoint(startBB);
    auto i64Ty = irBuilder.getInt64Ty();
    auto iterator = irBuilder.CreateEntryAlloca(i64Ty, nullptr, "iterator");
    (void)irBuilder.CreateStore(irBuilder.getInt64(0), iterator);
    (void)irBuilder.CreateBr(bodyBB);
    // Generate body.
    irBuilder.SetInsertPoint(bodyBB);
    auto index = irBuilder.CreateLoad(i64Ty, iterator);
    auto arrHead = irBuilder.CreateBitCast(irBuilder.GetPayloadFromObject(arrPtr), arrayType->getPointerTo(1u));
    auto dataPtr =
        irBuilder.CreateGEP(arrayType, arrHead, {irBuilder.getInt32(0), irBuilder.getInt32(1), irBuilder.getInt32(0)});
    auto elemPtr = irBuilder.CreateGEP(arrayType->getStructElementType(1)->getArrayElementType(), dataPtr, index);

    auto elemType = arrayType->getStructElementType(1)->getArrayElementType();
    if (elemType->isStructTy()) {
        if (IsTypeContainsRef(elemType)) {
            auto structType = llvm::cast<llvm::StructType>(elemType);
            auto layOut = irBuilder.GetLLVMModule()->getDataLayout().getStructLayout(structType);
            auto size = irBuilder.getInt64(layOut->getSizeInBytes());
            irBuilder.CallGCWriteAgg({arrPtr, elemPtr, value, size});
        } else {
            auto layOut = irBuilder.GetCGModule().GetLLVMModule()->getDataLayout().getStructLayout(
                llvm::cast<llvm::StructType>(elemType));
            CJC_NULLPTR_CHECK(layOut);
            auto size = layOut->getSizeInBytes();
            auto align = layOut->getAlignment();
            irBuilder.CreateMemCpy(elemPtr, align, value, align, size);
        }
    } else if (elemType->isArrayTy()) {
        auto size = irBuilder.getInt64(irBuilder.GetLLVMModule()->getDataLayout().getTypeAllocSize(elemType));
        irBuilder.CallGCWriteAgg({arrPtr, elemPtr, value, size});
    } else if (elemType == CGType::GetRefType(irBuilder.GetLLVMContext())) {
        irBuilder.CallGCWrite({value, arrPtr, elemPtr});
    } else {
        irBuilder.CreateStore(value, elemPtr);
    }
    // Generate increment and compare size.
    auto incremented = irBuilder.CreateAdd(index, irBuilder.getInt64(1));
    (void)irBuilder.CreateStore(incremented, iterator);
    auto cmpResult = irBuilder.CreateICmpSLT(incremented, arrayLen, "iterator.size.cmp");
    (void)irBuilder.CreateCondBr(cmpResult, bodyBB, endBB);
    // Change to end BB.
    irBuilder.SetInsertPoint(endBB);
}
} // namespace

void IRBuilder2::CallArrayInit(
    llvm::Value* arrPtr, llvm::Value* arrayLen, llvm::Value* elemValue, const CHIR::RawArrayType& arrTy)
{
    // The memory of the array has been cleared when the memory of the array is
    // allocated at runtime. Therefore, when all elements of the array are null
    // values, unnecessary zero initialization instructions do not need to be
    // inserted.
    if (auto constVal = llvm::dyn_cast<llvm::Constant>(elemValue); constVal && constVal->isNullValue()) {
        // keep ArrayLayout type.
        return;
    }

    auto arrayType = CGArrayType::GenerateArrayLayoutType(cgMod, arrTy);

    if (auto elemCGType = CGType::GetOrCreate(cgMod, arrTy.GetElementType()); !elemCGType->GetSize()) {
        InitArrayDataUG(*this, arrPtr, arrayLen, elemValue, arrayType, arrTy);
        return;
    }

    auto elemType = elemValue->getType();

    auto [nonOptBB, optBB] =
        Vec2Tuple<2>(CreateAndInsertBasicBlocks({GenNameForBB("arr.init.non-opt"), GenNameForBB("arr.init.opt")}));
    // If the value equals null-value, no need to assign that null-value to the
    // elements because the memory of the array is cleared while runtime allocates
    // the array. Otherwise, set the value of each element in the array by looping.
    auto isValNull = IsFPType(elemType) ? CreateFCmpOEQ(elemValue, llvm::ConstantFP::get(elemType, 0), "val.is-zero")
                                        : CreateIsNull(elemValue, "val.is-null");
    auto isZeroSize = CreateICmpEQ(arrayLen, getInt64(0), "size.is-zero");
    // If array is zero size or array value equals null-value, quit initialization step.
    (void)CreateCondBr(CreateOr(isZeroSize, isValNull), optBB, nonOptBB);
    // generate nonOptBB
    SetInsertPoint(nonOptBB);
    size_t typeSize = GetCGModule().GetTypeSize(elemType);
    CJC_ASSERT(typeSize > 0 && "Zero size type is not expected to be array element type.");
    if (typeSize == 1) {
        // There are two kinds of `value`:
        // 1) Pointer to a struct of which type is one byte;
        // 2) The amount that alloca reserves for it is one byte.
        InitByteArrayData(*this, arrPtr, arrayLen, elemValue, arrayType);
        (void)CreateBr(optBB);

        // generate optBB
        SetInsertPoint(optBB);
        return;
    }
    InitArrayData(*this, arrPtr, arrayLen, elemValue, arrayType);
    (void)CreateBr(optBB);

    // generate optBB
    SetInsertPoint(optBB);
}

llvm::Value* IRBuilder2::CreateEnumGEP(const CHIR::Field& field)
{
    CJC_ASSERT(field.GetPath().size() == 1);
    auto index = field.GetPath()[0];
    if (index == 0) {
        return GetEnumTag(field);
    } else {
        auto res = GetEnumAssociatedValue(field);
        if (auto cgType = dynamic_cast<CGEnumType*>(CGType::GetOrCreate(cgMod, field.GetResult()->GetType()));
            cgType && cgType->IsOptionLikeRef()) {
            llvm::cast<llvm::Instruction>(res)->setMetadata(
                "untrusted_ref", llvm::MDNode::get(cgMod.GetLLVMContext(), {}));
        }
        return res;
    }
}

llvm::Value* IRBuilder2::GetEnumTag(const CHIR::Field& field)
{
    auto cgEnum = GetCGModule().GetMappedValue(field.GetBase());
    CJC_NULLPTR_CHECK(cgEnum);
    CJC_ASSERT(field.GetBase()->GetType()->IsEnum());
    auto enumVal = cgEnum->GetRawValue();
    auto chirType = StaticCast<CHIR::EnumType*>(field.GetBase()->GetType());
    auto cgEnumType = StaticCast<const CGEnumType*>(CGType::GetOrCreate(cgMod, chirType));
    switch (cgEnumType->GetCGEnumTypeKind()) {
        case CGEnumType::CGEnumTypeKind::NON_EXHAUSTIVE_UNASSOCIATED:
        case CGEnumType::CGEnumTypeKind::EXHAUSTIVE_UNASSOCIATED: {
            return enumVal;
        }
        case CGEnumType::CGEnumTypeKind::NON_EXHAUSTIVE_ASSOCIATED:
        case CGEnumType::CGEnumTypeKind::EXHAUSTIVE_OTHER: {
            auto payload = GetPayloadFromObject(enumVal);
            auto tagType = CGType::GetOrCreate(cgMod, field.GetResult()->GetType())->GetLLVMType();
            return CreateLoad(tagType, CreateBitCast(payload, tagType->getPointerTo(1U)));
        }
        case CGEnumType::CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_NONREF: {
            auto tagType = CGType::GetOrCreate(cgMod, field.GetResult()->GetType())->GetLLVMType();
            return CreateLoad(tagType, CreateBitCast(enumVal, tagType->getPointerTo()));
        }
        case CGEnumType::CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_T: {
            auto i1Ty = getInt1Ty();
            auto payload = GetPayloadFromObject(enumVal);
            auto [refBB, nonRefBB, endBB] = Vec2Tuple<3>(CreateAndInsertBasicBlocks({"ref", "nonRef", "end"}));
            auto isRef = CreateTypeInfoIsReferenceCall(*chirType->GetTypeArgs()[0]);
            CreateCondBr(isRef, refBB, nonRefBB);

            SetInsertPoint(refBB);
            auto refResult = cgEnumType->IsAntiOptionLike() ? CallIntrinsicIsNonNull(CallGCRead({enumVal, payload}))
                                                            : CallIntrinsicIsNull(CallGCRead({enumVal, payload}));
            CreateBr(endBB);

            SetInsertPoint(nonRefBB);
            auto nonRefResult = LLVMIRBuilder2::CreateLoad(i1Ty, CreateBitCast(payload, i1Ty->getPointerTo(1U)));
            CreateBr(endBB);

            SetInsertPoint(endBB);
            auto phi = CreatePHI(i1Ty, 2U); // 2U: ref and nonRef branch
            phi->addIncoming(refResult, refBB);
            phi->addIncoming(nonRefResult, nonRefBB);
            return phi;
        }
        case CGEnumType::CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_REF: {
            return cgEnumType->IsAntiOptionLike() ? CallIntrinsicIsNonNull(cgEnum->GetRawValue())
                                                  : CallIntrinsicIsNull(cgEnum->GetRawValue());
        }
        case CGEnumType::CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_NONREF: {
            if (cgEnumType->GetSize()) {
                auto enumType = cgEnumType->GetLLVMType();
                auto casted =
                    CreateBitCast(enumVal, enumType->getPointerTo(enumVal->getType()->getPointerAddressSpace()));
                return LLVMIRBuilder2::CreateLoad(getInt1Ty(), CreateStructGEP(enumType, casted, 0U));
            }
            auto casted =
                CreateBitCast(GetPayloadFromObject(enumVal), llvm::Type::getInt1PtrTy(cgMod.GetLLVMContext(), 1U));
            return LLVMIRBuilder2::CreateLoad(getInt1Ty(), casted);
        }
        case CGEnumType::CGEnumTypeKind::EXHAUSTIVE_ZERO_SIZE: {
            return llvm::ConstantInt::get(getInt32Ty(), 0U);
        }
        default:
            CJC_ASSERT(false && "Should not reach here: UNKNOWN enum kind");
            return nullptr;
    }
}

namespace {
llvm::Value* GetOptionLikeNonRefAssociatedValue(IRBuilder2& irBuilder, const CHIR::Type& enumType,
    const CHIR::Type& associatedValueType, llvm::Value* enumValueWithTI)
{
    auto associatedValueTypeTI = irBuilder.CreateTypeInfo(associatedValueType);
    auto sizeOfAssociatedValueType = irBuilder.GetLayoutSize_32(associatedValueType);
    auto nonRefResult = irBuilder.CallIntrinsicAllocaGeneric({associatedValueTypeTI, sizeOfAssociatedValueType});

    auto ti = irBuilder.CreateTypeInfo(enumType);
    // `1` means the second field, `8` means skipping TypeInfo* additionally.
    auto associatedValOffset =
        irBuilder.CallIntrinsicGetFieldOffset({ti, irBuilder.getInt64(1U), irBuilder.getInt32(8U)});
    auto associatedValPtr = irBuilder.CreateInBoundsGEP(irBuilder.getInt8Ty(), enumValueWithTI, associatedValOffset);

    irBuilder.CallGCReadGeneric({nonRefResult, enumValueWithTI, associatedValPtr, sizeOfAssociatedValueType});
    return nonRefResult;
}

llvm::Value* GetCommonEnumAssociatedValue(IRBuilder2& irBuilder, const CHIR::Field& field, const CGValue& cgEnum)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto& cgCtx = irBuilder.GetCGContext();

    std::vector<llvm::Type*> elemTypes;
    bool isSized = true;
    auto args = field.GetBase()->GetType()->GetTypeArgs();
    for (size_t idx = 0; idx < args.size(); ++idx) {
        auto cgType = CGType::GetOrCreate(cgMod, args[idx]);
        if (cgType->GetSize()) {
            (void)elemTypes.emplace_back(cgType->GetLLVMType());
        } else {
            isSized = false;
            break;
        }
    }

    auto enumVal = cgEnum.GetRawValue();
    auto associatedValIdx = static_cast<unsigned int>(field.GetPath()[0]);
    if (isSized) {
        auto payload = irBuilder.GetPayloadFromObject(enumVal);
        auto layoutType = llvm::StructType::get(cgCtx.GetLLVMContext(), elemTypes);
        auto castedEnumValPtr = irBuilder.CreateBitCast(payload, layoutType->getPointerTo(1));
        auto fieldPtr = irBuilder.CreateStructGEP(layoutType, castedEnumValPtr, associatedValIdx);
        cgCtx.SetBasePtr(fieldPtr, enumVal);
        return irBuilder.CreateLoad(layoutType->getStructElementType(associatedValIdx), fieldPtr);
    } else {
        auto typeCastExpr = StaticCast<CHIR::TypeCast*>(StaticCast<CHIR::LocalVar*>(field.GetBase())->GetExpr());
        const auto& chirEnumType = *StaticCast<CHIR::EnumType*>(typeCastExpr->GetSourceTy());
        CJC_ASSERT(typeCastExpr->GetResult()->Get<CHIR::EnumCaseIndex>().has_value());

        std::vector<CHIR::Type*> typeArgs;
        for (auto typeArg : GetGenericArgsFromCHIRType(*chirEnumType.GetEnumDef()->GetType())) {
            auto arg = GetTypeInnerType(*typeCastExpr->GetSourceValue()->GetType(), typeArg);
            (void)typeArgs.emplace_back(arg);
        }
        auto fieldType = field.GetResult()->GetType();
        auto fieldAddrType = CGType::GetOrCreate(cgMod, CGType::GetRefTypeOf(cgCtx.GetCHIRBuilder(), *fieldType));
        CJC_ASSERT(typeCastExpr->GetResult()->Get<CHIR::EnumCaseIndex>().has_value());
        auto enumCaseTT = cgMod.GetOrCreateEnumCtorTIOrTT(
            chirEnumType, typeCastExpr->GetResult()->Get<CHIR::EnumCaseIndex>().value());
        auto enumCaseTi = irBuilder.CallIntrinsicGetTypeInfo(
            {enumCaseTT, irBuilder.getInt32(typeArgs.size()), irBuilder.CreateTypeInfoArray(typeArgs)});
        llvm::Value* fieldOffset =
            irBuilder.CallIntrinsicGetFieldOffset({enumCaseTi, irBuilder.getInt64(associatedValIdx)});
        fieldOffset = irBuilder.CreateAdd(fieldOffset, llvm::ConstantInt::get(fieldOffset->getType(), irBuilder.GetPayloadOffset()));
        auto fieldAddr = irBuilder.CreateInBoundsGEP(irBuilder.getInt8Ty(), enumVal, fieldOffset);
        if (!fieldType->IsGeneric()) {
            auto fieldCGType = CGType::GetOrCreate(cgMod, fieldType);
            fieldAddr = irBuilder.CreateBitCast(
                fieldAddr, fieldCGType->GetLLVMType()->getPointerTo(fieldAddr->getType()->getPointerAddressSpace()));
        }
        cgCtx.SetBasePtr(fieldAddr, enumVal);
        auto cgAddr = CGValue(fieldAddr, fieldAddrType);
        return irBuilder.CreateLoad(cgAddr);
    }
}

llvm::Value* GetZeroSizeEnumAssociatedValue(IRBuilder2& irBuilder, const CHIR::Field& field, const CGValue& cgEnum)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto& cgCtx = irBuilder.GetCGContext();
    auto fieldType = field.GetResult()->GetType();
    auto fieldAddrType = CGType::GetOrCreate(cgMod, CGType::GetRefTypeOf(cgCtx.GetCHIRBuilder(), *fieldType));
    auto cgAddr = CGValue(cgEnum.GetRawValue(), fieldAddrType);
    return irBuilder.CreateLoad(cgAddr);
}

llvm::Value* GetAssociatedNonRefEnumAssociatedValue(
    IRBuilder2& irBuilder, const CHIR::Field& field, const CGValue& cgEnum)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto i8Ty = irBuilder.getInt8Ty();

    auto associatedValIdx = static_cast<unsigned int>(field.GetPath()[0]);
    // 1. get the memory layout of the Enum's constructor.
    auto offset = 0U;
    auto args = field.GetBase()->GetType()->GetTypeArgs();
    CJC_ASSERT(associatedValIdx < args.size());
    for (size_t idx = 0U; idx < associatedValIdx; ++idx) {
        offset += CGType::GetOrCreate(cgMod, args[idx])->GetSize().value();
    }
    // 2. get the associated values.
    auto casted = irBuilder.CreateBitCast(cgEnum.GetRawValue(), i8Ty->getPointerTo());
    auto fieldPtr = irBuilder.CreateConstGEP1_32(i8Ty, casted, offset, "enum.field.ptr");
    auto retLLVMType = CGType::GetOrCreate(cgMod, field.GetResult()->GetType())->GetLLVMType();
    return irBuilder.CreateLoad(retLLVMType, irBuilder.CreateBitCast(fieldPtr, retLLVMType->getPointerTo()));
}
} // namespace

llvm::Value* IRBuilder2::GetEnumAssociatedValue(const CHIR::Field& field)
{
    CJC_ASSERT(field.GetBase()->IsLocalVar());
    CJC_ASSERT(StaticCast<CHIR::LocalVar*>(field.GetBase())->GetExpr()->GetExprKind() == CHIR::ExprKind::TYPECAST);
    auto typeCast = StaticCast<CHIR::TypeCast*>(StaticCast<CHIR::LocalVar*>(field.GetBase())->GetExpr());
    auto cgEnumType = static_cast<const CGEnumType*>(CGType::GetOrCreate(cgMod, typeCast->GetSourceValue()->GetType()));

    auto cgEnum = GetCGModule().GetMappedValue(field.GetBase());
    CJC_NULLPTR_CHECK(cgEnum);
    CJC_ASSERT(field.GetPath().size() == 1);
    auto index = static_cast<unsigned int>(field.GetPath()[0]);

    switch (cgEnumType->GetCGEnumTypeKind()) {
        case CGEnumType::CGEnumTypeKind::NON_EXHAUSTIVE_ASSOCIATED:
        case CGEnumType::CGEnumTypeKind::EXHAUSTIVE_OTHER: {
            return GetCommonEnumAssociatedValue(*this, field, *cgEnum);
        }
        case CGEnumType::CGEnumTypeKind::EXHAUSTIVE_ZERO_SIZE: {
            return GetZeroSizeEnumAssociatedValue(*this, field, *cgEnum);
        }
        case CGEnumType::CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_NONREF: {
            return GetAssociatedNonRefEnumAssociatedValue(*this, field, *cgEnum);
        }
        case CGEnumType::CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_T: {
            CJC_ASSERT(index == 1);
            auto [refBB, nonRefBB, endBB] = Vec2Tuple<3>(CreateAndInsertBasicBlocks({"ref", "nonRef", "end"}));
            auto isRef = CreateTypeInfoIsReferenceCall(*cgEnumType->GetOriginal().GetTypeArgs()[0]);
            CreateCondBr(isRef, refBB, nonRefBB);

            SetInsertPoint(refBB);
            auto enumVal = cgEnum->GetRawValue();
            auto payload = CreateBitCast(GetPayloadFromObject(enumVal), getInt8PtrTy(1U));
            auto refResult = CallGCRead({enumVal, payload});
            CreateBr(endBB);

            SetInsertPoint(nonRefBB);
            auto associatedValueType = field.GetResult()->GetType();
            auto nonRefResult = GetOptionLikeNonRefAssociatedValue(
                *this, cgEnum->GetCGType()->GetOriginal(), *associatedValueType, enumVal);
            CreateBr(endBB);

            SetInsertPoint(endBB);
            auto phi = CreatePHI(getInt8PtrTy(1U), 2U); // 2U: ref and nonRef branch
            phi->addIncoming(refResult, refBB);
            phi->addIncoming(nonRefResult, nonRefBB);
            return phi;
        }
        case CGEnumType::CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_REF: {
            CJC_ASSERT(index == 1);
            auto value = cgEnum->GetRawValue();
            cgMod.GetCGContext().AddNullableReference(value);
            return value;
        }
        case CGEnumType::CGEnumTypeKind::EXHAUSTIVE_ASSOCIATED_OPTION_LIKE_NONREF: {
            CJC_ASSERT(index == 1);
            if (cgEnumType->GetSize()) {
                auto enumVal = cgEnum->GetRawValue();
                auto enumType = cgEnumType->GetLLVMType();
                auto casted = CreateBitCast(
                    cgEnum->GetRawValue(), enumType->getPointerTo(enumVal->getType()->getPointerAddressSpace()));
                return CreateLoad(enumType->getStructElementType(1U), CreateStructGEP(enumType, casted, 1U));
            }
            auto associatedValueType = field.GetResult()->GetType();
            auto nonRefResult = GetOptionLikeNonRefAssociatedValue(
                *this, cgEnum->GetCGType()->GetOriginal(), *associatedValueType, cgEnum->GetRawValue());
            return nonRefResult;
        }
        case CGEnumType::CGEnumTypeKind::NON_EXHAUSTIVE_UNASSOCIATED:
        case CGEnumType::CGEnumTypeKind::EXHAUSTIVE_UNASSOCIATED:
        default:
            CJC_ASSERT(false && "Should not reach here: UNKNOWN enum kind");
            return nullptr;
    }
}

llvm::Value* IRBuilder2::GetSizeFromTypeInfo(llvm::Value* typeInfo)
{
    auto block = GetInsertBlock();
    auto& cacheMap = GetCGContext().genericParamsSizeBlockLevelCacheMap;
    if (auto blockIt = cacheMap.find(block); blockIt != cacheMap.end()) {
        auto blockCacheMap = blockIt->second;
        if (auto it = blockCacheMap.find(typeInfo); it != blockCacheMap.end()) {
            return it->second;
        }
    }
    auto& llvmCtx = cgMod.GetLLVMContext();
    auto typeInfoTy = CGType::GetOrCreateTypeInfoType(llvmCtx);
    auto typeInfoPtrTy = typeInfoTy->getPointerTo();
    auto ptr = CreateStructGEP(typeInfoTy, LLVMIRBuilder2::CreateBitCast(typeInfo, typeInfoPtrTy),
        static_cast<uint64_t>(TYPEINFO_SIZE), "ti.size");
    auto size = LLVMIRBuilder2::CreateLoad(llvm::Type::getInt32Ty(llvmCtx), ptr);
    size->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(llvmCtx, {}));
    (void)cacheMap[block].emplace(typeInfo, size);
    return size;
}

llvm::Value* IRBuilder2::GetLayoutSize_32(const CHIR::Type& type)
{
    auto baseType = DeRef(type);
    if (auto gt = DynamicCast<const CHIR::GenericType*>(baseType); gt && gt->orphanFlag) {
        CJC_ASSERT(gt->GetUpperBounds().size() == 1U);
        baseType = gt->GetUpperBounds()[0];
    }

    auto cgType = CGType::GetOrCreate(cgMod, baseType);
    if (auto s = cgType->GetSize()) {
        return llvm::ConstantInt::get(getInt32Ty(), s.value());
    }
    auto ti = CreateTypeInfo(*baseType);
    return GetSizeFromTypeInfo(ti);
}

llvm::Value* IRBuilder2::GetLayoutSize_64(const CHIR::Type& type)
{
    auto baseType = DeRef(type);
    if (auto gt = DynamicCast<const CHIR::GenericType*>(baseType); gt && gt->orphanFlag) {
        CJC_ASSERT(gt->GetUpperBounds().size() == 1U);
        baseType = gt->GetUpperBounds()[0];
    }

    auto cgType = CGType::GetOrCreate(cgMod, baseType);
    if (auto s = cgType->GetSize()) {
        return llvm::ConstantInt::get(getInt64Ty(), s.value());
    }
    auto ti = CreateTypeInfo(*baseType);
    return CreateSExt(GetSizeFromTypeInfo(ti), getInt64Ty());
}

llvm::Value* IRBuilder2::GetSize_32(const CHIR::Type& type)
{
    auto baseType = DeRef(type);
    if (auto gt = DynamicCast<const CHIR::GenericType*>(baseType); gt && gt->orphanFlag) {
        CJC_ASSERT(gt->GetUpperBounds().size() == 1U);
        baseType = gt->GetUpperBounds()[0];
    }
    if (baseType->IsGeneric()) {
        auto [refSizeBB, nonRefSizeBB, exitBB] = Vec2Tuple<3>(CreateAndInsertBasicBlocks(
            {GenNameForBB("get_ref_size"), GenNameForBB("get_non_ref_size"), GenNameForBB("get_size_exit")}));
        CreateCondBr(CreateTypeInfoIsReferenceCall(type), refSizeBB, nonRefSizeBB);
        SetInsertPoint(refSizeBB);
        CreateBr(exitBB);
        SetInsertPoint(nonRefSizeBB);
        auto nonRefSize = GetLayoutSize_32(type);
        CreateBr(exitBB);
        SetInsertPoint(exitBB);
        auto sizePHI = CreatePHI(getInt32Ty(), 2U);
        sizePHI->addIncoming(getInt32(GetPtrSize()), refSizeBB);
        sizePHI->addIncoming(nonRefSize, nonRefSizeBB);
        return sizePHI;
    } else if (auto cgType = CGType::GetOrCreate(cgMod, &type); cgType->IsReference()) {
        return getInt32(GetPayloadOffset());
    } else {
        return GetLayoutSize_32(type);
    }
}

llvm::Value* IRBuilder2::GetSize_64(const CHIR::Type& type)
{
    return CreateSExt(GetSize_32(type), getInt64Ty());
}

llvm::Value* IRBuilder2::GetSize_Native(const CHIR::Type& type)
{
    return GetCGContext().GetCompileOptions().target.arch == Triple::ArchType::ARM32 ? GetSize_32(type) : GetSize_64(type);
}

llvm::Value* IRBuilder2::GetAlign(const CHIR::Type& type, llvm::Type* targetType)
{
    auto ti = CreateTypeInfo(type);
    auto [refAlignBB, nonRefAlignBB, exitBB] = Vec2Tuple<3>(CreateAndInsertBasicBlocks(
        {GenNameForBB("get_ref_align"), GenNameForBB("get_non_ref_align"), GenNameForBB("get_align_exit")}));
    CreateCondBr(CreateTypeInfoIsReferenceCall(type), refAlignBB, nonRefAlignBB);
    SetInsertPoint(refAlignBB);
    CreateBr(exitBB);
    SetInsertPoint(nonRefAlignBB);
    auto& llvmCtx = cgMod.GetLLVMContext();
    auto typeInfoTy = CGType::GetOrCreateTypeInfoType(llvmCtx);
    auto ptr = CreateStructGEP(typeInfoTy, LLVMIRBuilder2::CreateBitCast(ti, typeInfoTy->getPointerTo()),
        static_cast<uint64_t>(TYPEINFO_ALIGN), "ti.align");
    auto loadInst = LLVMIRBuilder2::CreateLoad(llvm::Type::getInt8Ty(llvmCtx), ptr);
    loadInst->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(llvmCtx, {}));
    auto nonRefAlign = CreateSExt(loadInst, targetType);
    CreateBr(exitBB);
    SetInsertPoint(exitBB);
    auto sizePHI = CreatePHI(targetType, 2U);
    sizePHI->addIncoming(llvm::ConstantInt::get(targetType, GetPayloadOffset()), refAlignBB);
    sizePHI->addIncoming(nonRefAlign, nonRefAlignBB);
    return sizePHI;
}

llvm::Value* IRBuilder2::GetTypeKindFromTypeInfo(llvm::Value* typeInfo)
{
    auto& llvmCtx = cgMod.GetLLVMContext();
    auto typeInfoTy = CGType::GetOrCreateTypeInfoType(llvmCtx);
    auto typeInfoPtrTy = typeInfoTy->getPointerTo();
    auto ptr = CreateStructGEP(
        typeInfoTy, LLVMIRBuilder2::CreateBitCast(typeInfo, typeInfoPtrTy), static_cast<uint64_t>(TYPEINFO_TYPE_KIND));
    auto inst = LLVMIRBuilder2::CreateLoad(llvm::Type::getInt8Ty(llvmCtx), ptr, "ti.kind");
    inst->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(llvmCtx, {}));
    return inst;
}

llvm::Value* IRBuilder2::GetTypeForTypeParameter(llvm::Value* typeInfo)
{
    auto& llvmCtx = cgMod.GetLLVMContext();
    auto typeInfoTy = CGType::GetOrCreateTypeInfoType(llvmCtx);
    auto typeInfoPtrTy = typeInfoTy->getPointerTo();
    auto ptr = CreateStructGEP(
        typeInfoTy, LLVMIRBuilder2::CreateBitCast(typeInfo, typeInfoPtrTy), static_cast<uint64_t>(TYPEINFO_NAME));
    auto inst = LLVMIRBuilder2::CreateLoad(llvm::Type::getInt8PtrTy(llvmCtx), ptr, "ti.typeInfoName");
    inst->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(llvmCtx, {}));
    return inst;
}

llvm::Value* IRBuilder2::GetTypeArgsFromTypeInfo(llvm::Value* typeInfo)
{
    auto& llvmCtx = cgMod.GetLLVMContext();
    auto typeInfoTy = CGType::GetOrCreateTypeInfoType(llvmCtx);
    auto typeInfoPtrTy = typeInfoTy->getPointerTo();
    auto ptr = CreateStructGEP(typeInfoTy, LLVMIRBuilder2::CreateBitCast(typeInfo, typeInfoPtrTy),
        static_cast<uint64_t>(TYPEINFO_TYPE_ARGS), "ti.typeArgs");
    auto inst = LLVMIRBuilder2::CreateLoad(getInt8PtrTy(), ptr);
    inst->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(llvmCtx, {}));
    return LLVMIRBuilder2::CreateBitCast(inst, typeInfoPtrTy->getPointerTo());
}

llvm::Value* IRBuilder2::GetTypeInfoFromTiArray(llvm::Value* tiArray, std::size_t index, const std::string& name)
{
    auto tiPtrType = CGType::GetOrCreateTypeInfoPtrType(getContext());
    auto tiPtrPtr = LLVMIRBuilder2::CreateConstInBoundsGEP1_32(tiPtrType, tiArray, static_cast<unsigned>(index));
    auto inst = LLVMIRBuilder2::CreateLoad(tiPtrType, tiPtrPtr, name);
    inst->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(getContext(), {}));
    return inst;
}

llvm::Value* IRBuilder2::GetTypeInfoFromObject(llvm::Value* obj)
{
    auto tiPtrType = CGType::GetOrCreateTypeInfoPtrType(getContext());
    auto inst =
        LLVMIRBuilder2::CreateLoad(tiPtrType, LLVMIRBuilder2::CreateBitCast(obj, tiPtrType->getPointerTo(1U)), "ti");
    inst->setMetadata(llvm::LLVMContext::MD_invariant_load, llvm::MDNode::get(getContext(), {}));
    return inst;
}

llvm::Value* IRBuilder2::GetPayloadFromObject(llvm::Value* obj)
{
    auto p0i8 = getInt8PtrTy();
    auto payloadIdx = GetCGContext().GetCompileOptions().target.arch == Triple::ArchType::ARM32 ? 2U : 1U;
    auto payloadPtr =
        CreateConstGEP1_32(p0i8, LLVMIRBuilder2::CreateBitCast(obj, p0i8->getPointerTo(1U)), payloadIdx, "ti.payload");
    GetCGContext().SetBasePtr(payloadPtr, obj);
    return payloadPtr;
}

llvm::Value* IRBuilder2::CreateTypeInfoIsReferenceCall(llvm::Value* ti)
{
    auto func = llvm::Intrinsic::getDeclaration(cgMod.GetLLVMModule(), llvm::Intrinsic::cj_is_reference);
    return CreateCall(func, LLVMIRBuilder2::CreateBitCast(ti, func->getFunctionType()->getParamType(0)));
}

llvm::Value* IRBuilder2::CreateTypeInfoIsReferenceCall(const CHIR::Type& chirType)
{
    auto baseType = DeRef(chirType);
    if (auto gt = DynamicCast<const CHIR::GenericType*>(baseType); gt && gt->orphanFlag) {
        CJC_ASSERT(gt->GetUpperBounds().size() == 1U);
        baseType = gt->GetUpperBounds()[0];
    }

    if (baseType->IsGeneric()) {
        auto baseTypeTI = CreateTypeInfo(*baseType);
        return CreateTypeInfoIsReferenceCall(baseTypeTI);
    } else {
        return getInt1(CGType::GetOrCreate(cgMod, baseType)->IsReference());
    }
}

// Yield a specialized basic block as the entry or behind `allocas` to prepare
// all TypeInfo in advance. For more details, see the header file.
void IRBuilder2::SetInsertPointForPreparingTypeInfo()
{
    auto curFunc = GetInsertFunction();
    CJC_NULLPTR_CHECK(curFunc);
    auto insertBB = &curFunc->getEntryBlock();
    if (auto& entryBB = curFunc->getEntryBlock(); entryBB.getName() == "allocas") {
        auto successor = entryBB.getUniqueSuccessor();
        CJC_ASSERT(successor);
        if (successor->getName() == "prepTi") {
            insertBB = successor;
        } else {
            auto preTi = llvm::BasicBlock::Create(getContext(), "prepTi", curFunc, successor);
            CJC_NULLPTR_CHECK(insertBB->getTerminator());
            insertBB->getTerminator()->setOperand(0, preTi);
            SetInsertPoint(preTi);
            CreateBr(successor);
            insertBB = preTi;
        }
    } else if (insertBB->getName() != "prepTi") {
        auto preTi = CreateEntryBasicBlock(curFunc, "prepTi");
        SetInsertPoint(preTi);
        CreateBr(insertBB);
        insertBB = preTi;
    }
    SetInsertPoint(insertBB->getTerminator());
}
} // namespace Cangjie::CodeGen
