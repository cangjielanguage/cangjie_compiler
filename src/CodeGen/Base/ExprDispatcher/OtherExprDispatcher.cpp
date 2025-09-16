// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/ExprDispatcher/ExprDispatcher.h"

#include <cinttypes>

#include "Base/ApplyImpl.h"
#include "Base/ArrayImpl.h"
#include "Base/CGTypes/CGType.h"
#include "Base/CHIRExprWrapper.h"
#include "Base/InstanceOfImpl.h"
#include "Base/IntrinsicsDispatcher.h"
#include "Base/InvokeImpl.h"
#include "Base/SpawnExprImpl.h"
#include "Base/TupleExprImpl.h"
#include "Base/TypeCastImpl.h"
#include "Base/VArrayExprImpl.h"
#include "CGModule.h"
#include "IRAttribute.h"
#include "IRBuilder.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Type/ClassDef.h"
#include "cangjie/CHIR/Value.h"

namespace {
using namespace Cangjie;
using namespace CodeGen;

llvm::Value* HandleApplyExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    irBuilder.EmitLocation(CHIRExprWrapper(chirExpr));
    return GenerateApply(irBuilder, CHIRApplyWrapper(StaticCast<const CHIR::Apply&>(chirExpr)));
}

llvm::Value* HandleInvokeExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    irBuilder.EmitLocation(CHIRExprWrapper(chirExpr));
    return GenerateInvoke(irBuilder, CHIRInvokeWrapper(StaticCast<const CHIR::Invoke&>(chirExpr)));
}

llvm::Value* HandleInvokeStaticExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    irBuilder.EmitLocation(CHIRExprWrapper(chirExpr));
    return GenerateInvokeStatic(irBuilder, CHIRInvokeStaticWrapper(StaticCast<const CHIR::InvokeStatic&>(chirExpr)));
}

llvm::Value* HandleDebugExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    auto& debug = StaticCast<const CHIR::Debug&>(chirExpr);
    auto& cgMod = irBuilder.GetCGModule();
    auto cgVal = *(cgMod | debug.GetValue());
    auto rawVal = cgVal.GetRawValue();
    // Usually value is a local variable,
    // when it is a global variable, it means that it is a const literal and be lifted to global.
    if (!rawVal->getType()->isVoidTy() && !llvm::isa<llvm::GlobalObject>(rawVal)) {
        rawVal->setName(debug.GetSrcCodeIdentifier());
    }
    if (!irBuilder.GetCGContext().GetCompileOptions().enableCompileDebug) {
        return nullptr;
    }
    auto ty = DeRef(*debug.GetValue()->GetType());
    bool isBoxClass = ty->IsClass() && IsCapturedClass(*StaticCast<CHIR::ClassType*>(ty)->GetClassDef());
    auto hasSize = CGType::GetOrCreate(cgMod, ty)->GetSize();
    if (!IsReferenceType(*ty, cgMod) && hasSize && ty->IsGenericRelated()) {
        irBuilder.CreateBoxedValueForValueType(debug, cgVal);
    } else if (debug.GetValue()->IsParameter()) {
        irBuilder.CreateValuePointer(debug, cgVal);
    } else if (!llvm::isa<llvm::AllocaInst>(cgVal.GetRawValue())) {
        CJC_ASSERT(!llvm::isa<llvm::AllocaInst>(cgVal.GetRawValue()) == (!hasSize && !ty->IsClass()));
        irBuilder.CreateLocalVarPointer(debug, cgVal);
    } else if (isBoxClass) {
        irBuilder.CreateUnBoxDeclare(debug, cgVal);
    } else {
        irBuilder.EmitDeclare(debug);
    }
    return rawVal;
}

llvm::Value* HandleFieldExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    auto& field = StaticCast<const CHIR::Field&>(chirExpr);
    auto& cgMod = irBuilder.GetCGModule();
    auto base = cgMod | field.GetBase();

    if (auto lv = DynamicCast<CHIR::LocalVar>(field.GetBase())) {
        auto typeCast = DynamicCast<CHIR::TypeCast*>(lv->GetExpr());
        if (typeCast && typeCast->GetSourceValue()->GetType()->IsEnum() && typeCast->GetTargetTy()->IsTuple()) {
            return irBuilder.CreateEnumGEP(field);
        }
    }
    if (field.GetBase()->GetType()->IsEnum()) {
        return irBuilder.CreateEnumGEP(field);
    }
    if (base->GetCGType()->IsPointerType()) {
        auto val = irBuilder.CreateGEP(*base, field.GetPath());
        if (base->GetRawValue()->getType()->getPointerAddressSpace() == 1U) {
            cgMod.GetCGContext().SetBasePtr(val.GetRawValue(), base->GetRawValue());
        }
        return irBuilder.CreateLoad(val);
    } else {
        std::vector<unsigned> idxes;
        for (auto idx : field.GetPath()) {
            idxes.emplace_back(idx);
        }
        return irBuilder.CreateExtractValue(base->GetRawValue(), idxes);
    }
}

llvm::Value* HandleTypecastExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    auto& typeCast = StaticCast<const CHIR::TypeCast&>(chirExpr);
    return GenerateTypeCast(irBuilder, CHIRTypeCastWrapper(typeCast));
}

llvm::Value* HandleTupleExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    auto& tuple = StaticCast<const CHIR::Tuple&>(chirExpr);
    if (tuple.GetResult()->GetType()->IsEnum()) {
        return GenerateEnum(irBuilder, tuple);
    } else if (tuple.GetResult()->GetType()->IsStruct()) {
        return GenerateStruct(irBuilder, tuple);
    }
    return GenerateNativeTuple(irBuilder, tuple);
}

llvm::Value* HandleVArrayExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    auto& varray = StaticCast<const CHIR::VArray&>(chirExpr);
    return GenerateVArray(irBuilder, varray);
}

llvm::Value* HandleVArrayBuilderExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    auto& varray = StaticCast<const CHIR::VArrayBuilder&>(chirExpr);
    return GenerateVArrayBuilder(irBuilder, varray);
}

llvm::Value* HandleIntrinsicExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    irBuilder.EmitLocation(CHIRExprWrapper(chirExpr));
    return GenerateIntrinsic(irBuilder, CHIRIntrinsicWrapper(StaticCast<const CHIR::Intrinsic&>(chirExpr)));
}

llvm::Value* HandleRawArrayLiteralInitExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    auto& literalRawArrayInit = StaticCast<const CHIR::RawArrayLiteralInit&>(chirExpr);
    return irBuilder.CallArrayIntrinsicInitWithContent(literalRawArrayInit);
}

llvm::Value* HandleRawArrayAllocateExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    auto& rawArray = StaticCast<const CHIR::RawArrayAllocate&>(chirExpr);
    return GenerateRawArrayAllocate(irBuilder, CHIRRawArrayAllocateWrapper(rawArray));
}

llvm::Value* HandleRawArrayInitByValueExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    auto& rawArrayInitByValue = StaticCast<const CHIR::RawArrayInitByValue&>(chirExpr);
    return GenerateRawArrayInitByValue(irBuilder, rawArrayInitByValue);
}

llvm::Value* HandleInstanceOfExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    auto& instanceOf = StaticCast<const CHIR::InstanceOf&>(chirExpr);
    return GenerateInstanceOf(irBuilder, instanceOf);
}

llvm::Value* HandleSpawnExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    auto& spawn = StaticCast<const CHIR::Spawn&>(chirExpr);
    return GenerateSpawn(irBuilder, CHIRSpawnWrapper(spawn));
}

llvm::Value* HandleGetExceptionExpr(IRBuilder2& irBuilder, const CHIR::Expression&)
{
    return irBuilder.CallExceptionIntrinsicGetExceptionValue();
}

llvm::Value* HandleTransformToGenericExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    auto& castToGenericExpr = StaticCast<const CHIR::TransformToGeneric&>(chirExpr);
    auto& cgMod = irBuilder.GetCGModule();
    auto cgVal = *(cgMod | castToGenericExpr.GetSourceValue());
    auto srcCGType = CGType::GetOrCreate(cgMod, castToGenericExpr.GetSourceTy());
    auto targetCGType = CGType::GetOrCreate(cgMod, castToGenericExpr.GetTargetTy());
    CJC_ASSERT(srcCGType->GetSize() && "Source type must have size.");
    if (srcCGType->IsReference() && castToGenericExpr.GetTargetTy()->IsGeneric()) {
        return cgVal.GetRawValue();
    }
    if (srcCGType->GetSize() == targetCGType->GetSize()) {
        return cgVal.GetRawValue();
    }
    auto srcCHIRType = castToGenericExpr.GetSourceTy();
    // 1. Allocate memory for boxing srcValue.
    auto typeInfoOfSrc = irBuilder.CreateTypeInfo(*srcCHIRType);
    llvm::Value* temp =
        irBuilder.CallClassIntrinsicAlloc({typeInfoOfSrc, irBuilder.GetLayoutSize_32(*DeRef(*srcCHIRType))});
    // 2. store srcValue to temp
    auto payloadPtr = irBuilder.GetPayloadFromObject(temp);

    auto addr = irBuilder.CreateBitCast(payloadPtr,
        srcCHIRType->IsRef() ? srcCGType->GetPointerElementType()->GetLLVMType()->getPointerTo(1U)
                             : srcCGType->GetLLVMType()->getPointerTo(1));
    auto addrType = CGType::GetOrCreate(cgMod,
        srcCHIRType->IsRef() ? srcCHIRType : CGType::GetRefTypeOf(cgMod.GetCGContext().GetCHIRBuilder(), *srcCHIRType),
        1U);
    (void)irBuilder.CreateStore(cgVal, CGValue(addr, addrType));
    return temp;
}

llvm::Value* HandleBoxExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    auto& boxExpr = StaticCast<const CHIR::Box&>(chirExpr);
    auto& cgMod = irBuilder.GetCGModule();
    auto srcObject = boxExpr.GetSourceValue();
    auto cgVal = *(cgMod | srcObject);
    auto srcCHIRType = boxExpr.GetSourceTy();
    auto srcCGType = CGType::GetOrCreate(cgMod, srcCHIRType);

    auto result = GenerateGenericTypeCast(irBuilder, cgVal, *srcCHIRType, *boxExpr.GetResult()->GetType());
    if (result) {
        return result;
    }

    if (srcCGType->IsReference()) {
        return cgVal.GetRawValue();
    }

    if (srcCGType->GetSize()) {
        // 1. Allocate memory for boxing srcValue.
        auto typeInfoOfSrc = irBuilder.CreateTypeInfo(*srcCHIRType);
        llvm::Value* temp =
            irBuilder.CallClassIntrinsicAlloc({typeInfoOfSrc, irBuilder.GetLayoutSize_32(*DeRef(*srcCHIRType))});
        // 2. store srcValue to temp
        auto payloadPtr = irBuilder.GetPayloadFromObject(temp);
        auto addr = irBuilder.CreateBitCast(payloadPtr,
            srcCHIRType->IsRef() ? srcCGType->GetPointerElementType()->GetLLVMType()->getPointerTo(1U)
                                 : srcCGType->GetLLVMType()->getPointerTo(1));
        auto addrType = CGType::GetOrCreate(cgMod,
            srcCHIRType->IsRef() ? srcCHIRType
                                 : CGType::GetRefTypeOf(cgMod.GetCGContext().GetCHIRBuilder(), *srcCHIRType),
            1U);
        (void)irBuilder.CreateStore(cgVal, CGValue(addr, addrType));
        return temp;
    } else if (IsThisArgOfStructMethod(*srcObject)) {
        auto typeInfoOfSrc = irBuilder.CreateTypeInfo(*srcCHIRType);
        auto size = irBuilder.GetLayoutSize_32(*DeRef(*srcCHIRType));
        auto tmp = irBuilder.CallClassIntrinsicAlloc({typeInfoOfSrc, size});
        auto payloadPtr = irBuilder.GetPayloadFromObject(tmp);
        // Note: in this branch, it means:
        // - we are assigning a "struct" that doesn't begin with TypeInfo* to an address
        //   that should begin with TypeInfo*.
        // - we are in the scope of a struct instance method(without "$withTI" postfix),
        //   the "struct" mentioned above is the `this` parameter of the method.
        if (IsTypeContainsRef(srcCGType->GetLLVMType())) {
            irBuilder.CallGCWriteAgg({tmp, payloadPtr, cgVal.GetRawValue(), size});
        } else {
            irBuilder.CreateMemCpy(payloadPtr, llvm::MaybeAlign(), cgVal.GetRawValue(), llvm::MaybeAlign(), size);
        }
        return tmp;
    }
    return cgVal.GetRawValue();
}

llvm::Value* HandleUnBoxExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto& unboxExpr = StaticCast<const CHIR::UnBox&>(chirExpr);
    auto cgVal = (cgMod | unboxExpr.GetSourceValue());
    auto targetCHIRType = unboxExpr.GetTargetTy();
    auto targetCGType = CGType::GetOrCreate(cgMod, targetCHIRType);
    if (targetCGType->IsReference()) {
        return cgVal->GetRawValue();
    }
    // For UnBox(%0, ValueType&)
    if (targetCHIRType->IsRef()) {
        return irBuilder.GetPayloadFromObject(cgVal->GetRawValue());
    }
    if (targetCGType->GetSize()) {
        auto elementType = targetCGType->GetLLVMType();
        auto srcPayload = irBuilder.GetPayloadFromObject(cgVal->GetRawValue());
        auto castedPayload = irBuilder.CreateBitCast(srcPayload, elementType->getPointerTo(1U));
        return irBuilder.CreateLoad(elementType, castedPayload);
    }
    return cgVal->GetRawValue();
}

llvm::Value* HandleTransformToConcreteExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto& castToConcreteExpr = StaticCast<const CHIR::TransformToConcrete&>(chirExpr);

    auto cgVal = *(cgMod | castToConcreteExpr.GetSourceValue());
    auto targetCHIRType = castToConcreteExpr.GetTargetTy();
    auto targetCGType = CGType::GetOrCreate(cgMod, targetCHIRType);
    CJC_ASSERT(targetCGType->GetSize() && "target type must have size");
    auto srcCHIRType = castToConcreteExpr.GetSourceTy();
    const CGType* srcCGType = CGType::GetOrCreate(cgMod, srcCHIRType);

    if (srcCHIRType->IsRef() && srcCHIRType->GetTypeArgs()[0]->IsGeneric()) {
        srcCHIRType = srcCHIRType->GetTypeArgs()[0];
        srcCGType = srcCGType->GetPointerElementType();
        cgVal = CGValue(irBuilder.CreateLoad(cgVal), srcCGType);
    }

    if (srcCHIRType->IsGeneric() && targetCGType->IsReference()) {
        return cgVal.GetRawValue();
    }

    if (targetCGType->GetSize() == srcCGType->GetSize()) {
        return cgVal.GetRawValue();
    }

    auto elementType = targetCGType->GetLLVMType();
    auto srcPayload = irBuilder.CreateBitCast(
        irBuilder.GetPayloadFromObject(cgVal.GetRawValue()), elementType->getPointerTo(1U));
    return irBuilder.CreateLoad(elementType, srcPayload);
}

llvm::Value* HandleUnBoxToRefExpr(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto& unboxToRefExpr = StaticCast<const CHIR::UnBoxToRef&>(chirExpr);
    auto cgVal = (cgMod | unboxToRefExpr.GetSourceValue());
    auto targetCHIRType = DeRef(*unboxToRefExpr.GetTargetTy());
    auto targetCGType = CGType::GetOrCreate(cgMod, targetCHIRType);
    if (targetCGType->GetSize()) {
        return irBuilder.GetPayloadFromObject(cgVal->GetRawValue());
    } else {
        return cgVal->GetRawValue();
    }
}

llvm::Value* HandleGetRTTI(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto& expr = StaticCast<const CHIR::GetRTTI&>(chirExpr);
    auto value = irBuilder.GetTypeInfoFromObject(**(cgMod | expr.GetOperand()));
    return irBuilder.CreateBitCast(value, CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext()));
}

llvm::Value* HandleGetRTTIStatic(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto& expr = StaticCast<const CHIR::GetRTTIStatic&>(chirExpr);
    auto value = irBuilder.CreateTypeInfo(*expr.GetRTTIType());
    return irBuilder.CreateBitCast(value, CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext()));
}
} // namespace

namespace Cangjie::CodeGen {
llvm::Value* HandleOthersExpression(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    CJC_ASSERT(chirExpr.GetExprMajorKind() == CHIR::ExprMajorKind::OTHERS);
    static const std::unordered_map<CHIR::ExprKind, std::function<llvm::Value*(IRBuilder2&, const CHIR::Expression&)>>
        handleExprMap = {
            {CHIR::ExprKind::APPLY, HandleApplyExpr}, {CHIR::ExprKind::INVOKE, HandleInvokeExpr},
            {CHIR::ExprKind::INVOKESTATIC, HandleInvokeStaticExpr}, {CHIR::ExprKind::DEBUGEXPR, HandleDebugExpr},
            {CHIR::ExprKind::FIELD, HandleFieldExpr}, {CHIR::ExprKind::TYPECAST, HandleTypecastExpr},
            {CHIR::ExprKind::TUPLE, HandleTupleExpr}, {CHIR::ExprKind::VARRAY, HandleVArrayExpr},
            {CHIR::ExprKind::VARRAY_BUILDER, HandleVArrayBuilderExpr},
            {CHIR::ExprKind::INTRINSIC, HandleIntrinsicExpr},
            {CHIR::ExprKind::RAW_ARRAY_LITERAL_INIT, HandleRawArrayLiteralInitExpr},
            {CHIR::ExprKind::RAW_ARRAY_ALLOCATE, HandleRawArrayAllocateExpr},
            {CHIR::ExprKind::RAW_ARRAY_INIT_BY_VALUE, HandleRawArrayInitByValueExpr},
            {CHIR::ExprKind::INSTANCEOF, HandleInstanceOfExpr}, {CHIR::ExprKind::SPAWN, HandleSpawnExpr},
            {CHIR::ExprKind::GET_EXCEPTION, HandleGetExceptionExpr}, {CHIR::ExprKind::BOX, HandleBoxExpr},
            {CHIR::ExprKind::UNBOX, HandleUnBoxExpr},
            {CHIR::ExprKind::TRANSFORM_TO_GENERIC, HandleTransformToGenericExpr},
            {CHIR::ExprKind::TRANSFORM_TO_CONCRETE, HandleTransformToConcreteExpr},
            {CHIR::ExprKind::UNBOX_TO_REF, HandleUnBoxToRefExpr},
            {CHIR::ExprKind::GET_RTTI, HandleGetRTTI},
            {CHIR::ExprKind::GET_RTTI_STATIC, HandleGetRTTIStatic},
    };
    if (auto found = handleExprMap.find(chirExpr.GetExprKind()); found != handleExprMap.end()) {
        irBuilder.EmitLocation(CHIRExprWrapper(chirExpr));
        return found->second(irBuilder, chirExpr);
    }
    printf("Unexpected expr kind: %" PRIu64 "\n", static_cast<uint64_t>(chirExpr.GetExprKind()));
    CJC_ASSERT(false);
    return nullptr;
}
} // namespace Cangjie::CodeGen
