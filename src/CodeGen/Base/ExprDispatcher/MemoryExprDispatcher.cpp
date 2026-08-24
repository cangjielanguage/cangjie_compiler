// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/ExprDispatcher/ExprDispatcher.h"

#include <cinttypes>

#include "Base/AllocateImpl.h"
#include "Base/CGTypes/CGEnumType.h"
#include "Base/CHIRExprWrapper.h"
#include "CGModule.h"
#include "IRBuilder.h"
#include "Utils/ModalWrite.h"
#include "cangjie/CHIR/IR/Annotation.h"
#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/IR/Value/Value.h"

namespace Cangjie::CodeGen {
namespace {
llvm::Value* TryGetSpecialRetValueStorage(IRBuilder2& irBuilder, const CHIR::Allocate& alloca)
{
    auto parentFunc = alloca.GetTopLevelFunc();
    if (!parentFunc || parentFunc->GetReturnValue() != alloca.GetResult()) {
        return nullptr;
    }

    auto llvmFunc = irBuilder.GetCGModule().GetOrInsertCGFunction(parentFunc)->GetRawFunction();
    if (llvmFunc->hasStructRetAttr()) {
        return llvmFunc->getArg(0);
    }

    auto overrideSrcFuncType = parentFunc->Get<CHIR::OverrideSrcFuncType>();
    if (!overrideSrcFuncType || !DeRef(*alloca.GetType())->IsBox()) {
        return nullptr;
    }

    auto retValueCGType = CGType::GetOrCreate(irBuilder.GetCGModule(), overrideSrcFuncType->GetReturnType());
    return retValueCGType->GetSize() ? irBuilder.CreateEntryAlloca(*retValueCGType) : nullptr;
}

void TryEmitNullInitForDebugLocalClass(IRBuilder2& irBuilder, const CHIR::Allocate& alloca, llvm::Value* result)
{
    if (irBuilder.GetCGContext().GetCompileOptions().optimizationLevel != GlobalOptions::OptimizationLevel::O0) {
        return;
    }
    auto* localVar = alloca.GetResult();
    if (!localVar || localVar->GetSrcCodeIdentifier().empty()) {
        return;
    }
    auto chirType = DeRef(*alloca.GetType());
    if (!chirType->IsClass() || !llvm::isa<llvm::AllocaInst>(result)) {
        return;
    }

    // Zero the stack slot for class-type variables at -O0 so cjdb does not read
    // garbage typeinfo pointers before the variable is initialised.
    auto allocTy = llvm::cast<llvm::AllocaInst>(result)->getAllocatedType();
    auto nullPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(allocTy));
    auto entryBB = &irBuilder.GetInsertBlock()->getParent()->getEntryBlock();
    auto savedIP = irBuilder.saveIP();
    irBuilder.SetInsertPoint(entryBB->getTerminator());
    auto storeInst = irBuilder.CreateStore(nullPtr, result);
    storeInst->setDebugLoc(llvm::DebugLoc());
    irBuilder.restoreIP(savedIP);
}

bool IsNoOpStoreToRet(CGModule& cgMod, const CHIR::Store& store)
{
    auto value = store.GetValue();
    auto addr = store.GetLocation();
    auto var = DynamicCast<CHIR::LocalVar*>(addr);
    if (!var || !var->IsRetValue()) {
        return false;
    }
    if (value->GetType()->IsUnit()) {
        return true;
    }
    if (cgMod.GetCGContext().GetCompileOptions().optimizationLevel == GlobalOptions::OptimizationLevel::O0) {
        return false;
    }
    if (!value->IsLocalVar()) {
        return false;
    }

    auto* valueCG = cgMod | value;
    auto* addrCG = cgMod | addr;
    return valueCG && addrCG && valueCG->GetRawValue() == addrCG->GetRawValue();
}
} // namespace

llvm::Value* HandleLoadExpr(IRBuilder2& irBuilder, const CHIR::Load& load)
{
    auto& cgMod = irBuilder.GetCGModule();
    irBuilder.EmitLocation(CHIRExprWrapper(load));
    auto value = *(cgMod | load.GetLocation());
    CJC_ASSERT(value.GetCGType()->GetOriginal().IsRef());
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    auto elementType = value.GetCGType()->GetPointerElementType()->GetLLVMType();
    if (load.GetLocation()->IsLocalVar() && elementType == CGType::GetRefType(irBuilder.GetLLVMContext())) {
        auto localVar = StaticCast<const CHIR::LocalVar*>(load.GetLocation());
        if (IsGetElementRefOfClass(*localVar->GetExpr(), irBuilder.GetCGContext().GetCHIRBuilder())) {
            auto getEleRefExpr = StaticCast<const CHIR::GetElementRef*>(localVar->GetExpr());
            auto locationCHIRType = DeRef(*getEleRefExpr->GetBase()->GetType());
            if (locationCHIRType->IsClass() &&
                IsWeakRefClass(*StaticCast<CHIR::ClassType*>(locationCHIRType)->GetClassDef())) {
                auto addr = value.GetRawValue();
                auto base = irBuilder.GetCGContext().GetBasePtrOf(addr);
                CJC_NULLPTR_CHECK(base);
                return irBuilder.CallGCReadWeakRef({base, addr});
            }
        }
    }
#endif
    return irBuilder.CreateLoad(value);
}

llvm::Value* HandleStoreExpr(IRBuilder2& irBuilder, const CHIR::Store& store)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto value = store.GetValue();
    auto addr = store.GetLocation();
    if (auto node = DynamicCast<CHIR::LocalVar*>(value);
        node && node->GetExpr()->IsConstantNull() && value->GetType()->IsClass()) {
        return nullptr;
    }
    if (IsNoOpStoreToRet(cgMod, store)) {
        return nullptr;
    }
    irBuilder.EmitLocation(CHIRExprWrapper(store), true);
    auto valueVal = *(cgMod | value);
    if (value->TestAttr(CHIR::Attribute::FOREIGN)) {
        CJC_NULLPTR_CHECK(llvm::dyn_cast<llvm::Function>(valueVal.GetRawValue()));
        llvm::dyn_cast<llvm::Function>(valueVal.GetRawValue())
            ->addAttributeAtIndex(static_cast<unsigned>(llvm::AttributeList::FunctionIndex),
                llvm::Attribute::get(cgMod.GetLLVMContext(), CJ2C_ATTR));
    }
    // When storing to a Box-type [ret] address, check the OverrideSrcFuncType to optimize boxing.
    // The OverrideSrcFuncType represents the overridden parent method's signature, where the return
    // type may be a generic template. When instantiated with concrete types:
    // - For non-generic types with known size: unbox and store directly
    // - For generic types or unknown size types: keep the box
    //
    // Example: Struct-_CNat5ArrayIG_E<UInt8> override Struct-_CNat5ArrayIG_E<T>
    // IR Before:
    //   [ret] %1: Box<Struct-_CNat5ArrayIG_E<UInt8>>&& = Allocate(Box<Struct-_CNat5ArrayIG_E<UInt8>>&)
    //   %3: Struct-_CNat5ArrayIG_E<UInt8> = Apply(...)
    //   %4: Box<Struct-_CNat5ArrayIG_E<UInt8>>& = Box(%3)
    //   Store(%4, %1)
    // IR After:
    //   Store(%3, %1)  -- skip %4's box, use %3 directly
    if (auto var = DynamicCast<CHIR::LocalVar*>(addr); var && var->IsRetValue() &&
        store.GetTopLevelFunc()->Get<CHIR::OverrideSrcFuncType>() && DeRef(*addr->GetType())->IsBox()) {
        auto overrideSrcRetType = store.GetTopLevelFunc()->Get<CHIR::OverrideSrcFuncType>()->GetReturnType();
        auto retAddrCGType = CGType::GetOrCreate(
            cgMod, CGType::GetRefTypeOf(cgMod.GetCGContext().GetCHIRBuilder(), *overrideSrcRetType));
        auto retValueCGType = CGType::GetOrCreate(cgMod, overrideSrcRetType);
        bool needBoxExpr = overrideSrcRetType->IsGeneric() || !retValueCGType->GetSize();
        auto storeValCGType = overrideSrcRetType->IsGeneric() ? retValueCGType : retAddrCGType;
        auto valueToStore = valueVal.GetRawValue();
        if (!needBoxExpr) {
            if (auto valueVar = DynamicCast<CHIR::LocalVar*>(value);
                valueVar && valueVar->GetExpr() && valueVar->GetExpr()->GetExprKind() == CHIR::ExprKind::BOX) {
                auto& boxExpr = StaticCast<const CHIR::Box&>(*valueVar->GetExpr());
                valueToStore = (cgMod | boxExpr.GetSourceValue())->GetRawValue();
            }
        }

        return irBuilder.CreateStore(CGValue(valueToStore, storeValCGType, valueVal.IsSRetArg()),
            CGValue((cgMod | addr)->GetRawValue(), retAddrCGType, (cgMod | addr)->IsSRetArg()),
            DeRef(*addr->GetType())->GetTypeArgs()[0]);
    }
    return irBuilder.CreateStore(valueVal, *(cgMod | addr), nullptr,
        ClassifyModalWrite(irBuilder.GetCGContext().GetCHIRBuilder(), store));
}

llvm::Value* GetElementRefOfOptionLikeT(
    IRBuilder2& irBuilder, const CHIR::EnumType& chirEnumType, bool isAntiOptionLike, llvm::Value* enumValue)
{
    auto ret = irBuilder.CreateEntryAlloca(irBuilder.getInt1Ty());
    auto payload = irBuilder.GetPayloadFromObject(enumValue);
    auto isRef = irBuilder.CreateTypeInfoIsReferenceCall(*chirEnumType.GetTypeArgs()[0]);
    auto [refBB, nonRefBB, endBB] = Vec2Tuple<3>(irBuilder.CreateAndInsertBasicBlocks({"ref", "nonRef", "end"}));
    irBuilder.CreateCondBr(isRef, refBB, nonRefBB);

    irBuilder.SetInsertPoint(refBB);
    auto refResult = isAntiOptionLike ? irBuilder.CallIntrinsicIsNonNull(irBuilder.CallGCRead({enumValue, payload}))
                                      : irBuilder.CallIntrinsicIsNull(irBuilder.CallGCRead({enumValue, payload}));
    irBuilder.CreateStore(refResult, ret);
    irBuilder.CreateBr(endBB);

    irBuilder.SetInsertPoint(nonRefBB);
    auto i1Ty = irBuilder.getInt1Ty();
    auto nonRefResult =
        irBuilder.LLVMIRBuilder2::CreateLoad(i1Ty, irBuilder.CreateBitCast(payload, i1Ty->getPointerTo(1U)));
    irBuilder.CreateStore(nonRefResult, ret);
    irBuilder.CreateBr(endBB);

    irBuilder.SetInsertPoint(endBB);
    return ret;
}

llvm::Value* HandleGetElementRef(IRBuilder2& irBuilder, const CHIR::GetElementRef& getEleRef)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto base = getEleRef.GetBase();
    CJC_ASSERT(base->GetType()->IsRef() || base->GetType()->IsGeneric());
    irBuilder.EmitLocation(CHIRExprWrapper(getEleRef));
    llvm::Value* retValue = nullptr;
    if (auto t = DeRef(*base->GetType()); t && t->IsEnum()) {
        CJC_ASSERT(!getEleRef.GetPath().empty() && getEleRef.GetPath()[0] == 0U);
        auto enumType = static_cast<const CHIR::EnumType*>(t);
        auto cgEnumType = static_cast<const CGEnumType*>(CGType::GetOrCreate(cgMod, enumType));
        auto layoutType = cgEnumType->GetLayoutType();
        auto value = irBuilder.CreateLoad(*(cgMod | base));
        if (cgEnumType->IsCommonEnum()) {
            auto payload = irBuilder.GetPayloadFromObject(value);
            auto tagLLVMType = llvm::Type::getInt32PtrTy(cgMod.GetLLVMContext(), 1U);
            retValue = irBuilder.CreateBitCast(payload, tagLLVMType);
        } else if (cgEnumType->IsOptionLikeNonRef()) {
            llvm::Value* payload = cgEnumType->GetSize().has_value()
                ? value
                : irBuilder.CreateBitCast(irBuilder.GetPayloadFromObject(value), layoutType->getPointerTo(1U));
            retValue = irBuilder.CreateStructGEP(layoutType, payload, 0);
        } else if (cgEnumType->IsOptionLikeRef()) {
            retValue = irBuilder.CreateEntryAlloca(irBuilder.getInt1Ty());
            auto tmp = cgEnumType->IsAntiOptionLike() ? irBuilder.CallIntrinsicIsNonNull(value)
                                                      : irBuilder.CallIntrinsicIsNull(value);
            irBuilder.CreateStore(tmp, retValue);
        } else if (cgEnumType->IsOptionLikeT()) {
            retValue = GetElementRefOfOptionLikeT(irBuilder, *enumType, cgEnumType->IsAntiOptionLike(), value);
        } else if (cgEnumType->IsAllAssociatedValuesAreNonRef()) {
            retValue = irBuilder.CreateBitCast(value, llvm::Type::getInt32PtrTy(cgMod.GetLLVMContext()));
        } else if (cgEnumType->IsZeroSizeEnum()) {
            retValue = irBuilder.CreateEntryAlloca(irBuilder.getInt32Ty());
            irBuilder.CreateStore(llvm::ConstantInt::get(irBuilder.getInt32Ty(), 0U), retValue);
        } else {
            CJC_ABORT();
        }
    }
    if (!retValue) {
        retValue = irBuilder.CreateGEP(*(cgMod | base), getEleRef.GetPath()).GetRawValue();
    }

    auto opTy = DeRef(*getEleRef.GetOperand(0)->GetType());
    bool isAutoEnv =
        opTy->IsClass() && IsClosureConversionEnvClass(*StaticCast<CHIR::ClassType*>(opTy)->GetClassDef());
    bool isLambda = dynamic_cast<const CHIR::Function*>(&irBuilder.GetInsertCGFunction()->GetOriginal())->IsLambda();
    if (isLambda && irBuilder.GetCGContext().GetCompileOptions().enableCompileDebug && isAutoEnv) {
        irBuilder.CreateEnvDeclare(getEleRef, retValue);
    }
    return retValue;
}

void HandleStoreElementRef(IRBuilder2& irBuilder, const CHIR::StoreElementRef& storeElementRef)
{
    auto& cgMod = irBuilder.GetCGModule();
    auto rhs = storeElementRef.GetValue();
    auto lhs = storeElementRef.GetBase();
    auto& path = storeElementRef.GetPath();
    CJC_ASSERT(lhs->GetType()->IsRef() && !path.empty());
    auto value = cgMod | rhs;
    auto place = cgMod | lhs;
    auto srcCGType = CGType::GetOrCreate(cgMod, rhs->GetType());
    if (!srcCGType->GetSize() && IsThisArgOfStructMethod(*rhs)) {
        auto typeInfoOfSrc = irBuilder.CreateTypeInfo(srcCGType->GetOriginal());
        auto size = irBuilder.GetLayoutSize_32(srcCGType->GetOriginal());
        auto tmp = irBuilder.CallClassIntrinsicAlloc({typeInfoOfSrc, size}, false);
        auto payloadPtr = irBuilder.GetPayloadFromObject(tmp);
        // Note: in this branch, it means:
        // - we are assigning a "struct" that doesn't begin with TypeInfo* to an address
        //   that should begin with TypeInfo*.
        // - we are in the scope of a struct instance method(without "$withTI" postfix),
        //   the "struct" mentioned above is the `this` parameter of the method.
        if (IsTypeContainsRef(srcCGType->GetLLVMType())) {
            irBuilder.CallGCWriteAgg(
                srcCGType->GetLayoutType(), {tmp, payloadPtr, value->GetRawValue(), size});
        } else {
            irBuilder.CreateMemCpy(payloadPtr, llvm::MaybeAlign(), value->GetRawValue(), llvm::MaybeAlign(), size);
        }
        auto destAddr = irBuilder.CreateGEP(*place, path);
        irBuilder.CreateStore(CGValue(tmp, destAddr.GetCGType()->GetPointerElementType()), destAddr);
    } else if (auto node = DynamicCast<CHIR::LocalVar*>(rhs);
               node && node->GetExpr()->IsConstantNull() && rhs->GetType()->IsClass()) {
        // If the source is null or class, do nothing.
    } else if (auto var = DynamicCast<CHIR::LocalVar*>(lhs); var && var->IsRetValue() && rhs->GetType()->IsUnit()) {
        // If stored location is function's ret of unit type, do nothing.
    } else {
        irBuilder.EmitLocation(CHIRExprWrapper(storeElementRef));
        if (rhs->TestAttr(CHIR::Attribute::FOREIGN)) {
            llvm::cast<llvm::Function>(value->GetRawValue())
                ->addAttributeAtIndex(static_cast<unsigned>(llvm::AttributeList::FunctionIndex),
                    llvm::Attribute::get(cgMod.GetLLVMContext(), CJ2C_ATTR));
        }
        irBuilder.CreateStore(*value, irBuilder.CreateGEP(*place, path), nullptr,
            ClassifyModalWrite(irBuilder.GetCGContext().GetCHIRBuilder(), storeElementRef));
    }
}

llvm::Value* HandleMemoryExpression(IRBuilder2& irBuilder, const CHIR::Expression& chirExpr)
{
    CJC_ASSERT(chirExpr.GetExprMajorKind() == CHIR::ExprMajorKind::MEMORY_EXPR);
    switch (chirExpr.GetExprKind()) {
        case CHIR::ExprKind::ALLOCATE: {
            auto& alloca = StaticCast<const CHIR::Allocate&>(chirExpr);
            // Opt: For the function that returns value by an `sret` argument,
            // we don't need to allocate another place to store the return value.
            if (auto retValueStorage = TryGetSpecialRetValueStorage(irBuilder, alloca)) {
                return retValueStorage;
            }
            irBuilder.EmitLocation(CHIRExprWrapper(alloca));
            auto result = GenerateAllocate(irBuilder, alloca);
            TryEmitNullInitForDebugLocalClass(irBuilder, alloca, result);
            return result;
        }
        case CHIR::ExprKind::LOAD: {
            auto& load = StaticCast<const CHIR::Load&>(chirExpr);
            return HandleLoadExpr(irBuilder, load);
        }
        case CHIR::ExprKind::STORE: {
            auto& store = StaticCast<const CHIR::Store&>(chirExpr);
            return HandleStoreExpr(irBuilder, store);
        }
        case CHIR::ExprKind::GET_ELEMENT_REF: {
            auto& getEleRef = StaticCast<const CHIR::GetElementRef&>(chirExpr);
            return HandleGetElementRef(irBuilder, getEleRef);
        }
        case CHIR::ExprKind::STORE_ELEMENT_REF: {
            auto& storeElementRef = StaticCast<const CHIR::StoreElementRef&>(chirExpr);
            HandleStoreElementRef(irBuilder, storeElementRef);
            return nullptr;
        }
        default: {
            auto exprKindStr = std::to_string(static_cast<uint64_t>(chirExpr.GetExprKind()));
            CJC_ASSERT_WITH_MSG(false, std::string("Unexpected CHIRExprKind: " + exprKindStr + "\n").c_str());
            return nullptr;
        }
    }
}
} // namespace Cangjie::CodeGen
