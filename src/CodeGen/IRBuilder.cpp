// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "llvm/IR/DerivedTypes.h"

#include "Base/CGTypes/CGType.h"
#include "CGModule.h"
#include "DIBuilder.h"
#include "IRBuilder.h"

namespace Cangjie {
namespace CodeGen {

IRBuilder2::IRBuilder2(CGModule& cgMod) : LLVMIRBuilder2(cgMod.GetLLVMContext()), cgMod(cgMod)
{
    if (cgMod.GetCGContext().GetCompileOptions().fastMathMode) {
        llvm::FastMathFlags fmf;
        fmf.setFast(true);
        setFastMathFlags(fmf);
    }
}

CGContext& IRBuilder2::GetCGContext() const
{
    return cgMod.GetCGContext();
}

llvm::LLVMContext& IRBuilder2::GetLLVMContext() const
{
    return cgMod.GetLLVMContext();
}

llvm::CallBase* IRBuilder2::CreateCallOrInvoke(
    llvm::FunctionType* fTy, llvm::Value* callee, std::vector<llvm::Value*> args)
{
    // Emit the call or invoke instruction.
    auto unwindBlock = cgMod.GetCGContext().TopUnwindBlockStack();
    if (unwindBlock.has_value()) {
        auto currentBB = GetInsertBlock();
        auto normalDest = llvm::BasicBlock::Create(getContext(), "normalDest", currentBB->getParent());
        normalDest->moveAfter(currentBB);
        auto resultVal = CreateInvoke(fTy, callee, normalDest, unwindBlock.value(), args);
        SetInsertPoint(normalDest);
        return resultVal;
    } else {
        return CreateCall(fTy, callee, args);
    }
}

llvm::CallBase* IRBuilder2::CreateCallOrInvoke(llvm::Function* callee, const llvm::ArrayRef<llvm::Value*> args)
{
    // Emit the call or invoke instruction.
    auto unwindBlock = cgMod.GetCGContext().TopUnwindBlockStack();
    llvm::CallBase* callBase = nullptr;
    if (unwindBlock.has_value()) {
        auto currentBB = GetInsertBlock();
        auto normalDest = llvm::BasicBlock::Create(getContext(), "normalDest", currentBB->getParent());
        normalDest->moveAfter(currentBB);
        callBase = CreateInvoke(callee, normalDest, unwindBlock.value(), args);
        SetInsertPoint(normalDest);
    } else {
        callBase = CreateCall(callee, args);
    }
    if (callee->hasStructRetAttr()) {
        callBase->addAttributeAtIndex(
            static_cast<unsigned>(llvm::AttributeList::FirstArgIndex), llvm::Attribute::NoAlias);
        auto sRetAttr =
            llvm::Attribute::getWithStructRetType(callBase->getContext(), callee->getArg(0)->getParamStructRetType());
        callBase->addAttributeAtIndex(static_cast<unsigned>(llvm::AttributeList::FirstArgIndex), sRetAttr);
    }
    return callBase;
}

// DebugLoc will be deleted.
void IRBuilder2::SetInsertPointToAllocas(llvm::Function* curFunc)
{
    auto entryBB = &curFunc->getEntryBlock();
    if (entryBB->getName() != "allocas") {
        auto allocasBB = llvm::BasicBlock::Create(getContext(), "allocas", GetInsertFunction(), entryBB);
        SetInsertPoint(allocasBB);
        auto inst = CreateBr(entryBB);
        inst->setDebugLoc(llvm::DebugLoc());
        entryBB = allocasBB;
    }
    SetInsertPoint(entryBB->getTerminator());
}

llvm::AllocaInst* IRBuilder2::CreateEntryAlloca(llvm::Type* type, llvm::Value* arraySize, const llvm::Twine& name)
{
    CJC_ASSERT(type);
    auto curPt = LLVMIRBuilder2::GetInsertPoint();
    auto curBB = LLVMIRBuilder2::GetInsertBlock();
    CJC_ASSERT(curBB && "Can't get the current basic block.");
    auto curFunc = curBB->getParent();
    CJC_ASSERT(curFunc && "Can't get the current function.");
    auto currentLoc = getCurrentDebugLocation();
    SetInsertPointToAllocas(curFunc);
    auto allocaInst = LLVMIRBuilder2::CreateAlloca(type, arraySize, name);
    LLVMIRBuilder2::SetInsertPoint(curBB, curPt);
    SetCurrentDebugLocation(currentLoc);
    return allocaInst;
}

llvm::Instruction* IRBuilder2::CreateEntryAlloca(const CGType& cgType, const llvm::Twine& name)
{
    if (!GetCGContext().GetCompileOptions().disableInstantiation || cgType.GetSize()) {
        auto allocatedType = cgType.IsCGFunction()
            ? dynamic_cast<const CGFunctionType&>(cgType).GetLLVMFunctionType()->getPointerTo()
            : cgType.GetLLVMType();
        auto allocaInst = CreateEntryAlloca(allocatedType, nullptr, name);
        auto& options = cgMod.GetCGContext().GetCompileOptions();
        if (allocatedType->isStructTy() && options.enableCompileDebug &&
            options.optimizationLevel == GlobalOptions::OptimizationLevel::O0) {
            (void)CreateMemsetStructWith0(allocaInst);
        }
        return allocaInst;
    }

    if (cgType.GetOriginal().IsGeneric()) {
        return CreateEntryAlloca(getInt8PtrTy(1), nullptr, name);
    }

    llvm::Value* size = GetLayoutSize_32(cgType.GetOriginal());
    auto allocaInst = CallIntrinsicAllocaGeneric({CreateTypeInfo(cgType.GetOriginal()), size});
    allocaInst->setDebugLoc(llvm::DebugLoc());
    return allocaInst;
}

llvm::Value* IRBuilder2::CreateLoad(const CGValue& cgVal, const llvm::Twine& name)
{
    auto elementCGType = cgVal.GetCGType()->GetPointerElementType();
    auto elementType = elementCGType->GetLLVMType();
    if (cgMod.GetCGContext().GetCompileOptions().disableInstantiation) {
        if (auto& ori = elementCGType->GetOriginal(); !elementCGType->GetSize() && !ori.IsGeneric()) {
            auto ti = CreateTypeInfo(ori);
            auto payloadSize = GetLayoutSize_32(ori);
            auto tmp = CallIntrinsicAllocaGeneric({ti, payloadSize});
            if (GetCGContext().GetBasePtrOf(cgVal.GetRawValue()) ||
                cgVal.GetRawValue()->getType()->getPointerAddressSpace() == 0U) {
                CreateMemCpy(GetPayloadFromObject(tmp), llvm::MaybeAlign(), *cgVal, llvm::MaybeAlign(), payloadSize);
            } else {
                CallIntrinsicAssignGeneric({tmp, *cgVal, ti});
            }
            return tmp;
        }
    }
    auto* loadInst = CreateLoad(elementType, cgVal.GetRawValue(), name);
    if (auto& ori = elementCGType->GetOriginal(); ori.IsEnum()) {
        llvm::cast<llvm::Instruction>(loadInst)->setMetadata(
            "untrusted_ref", llvm::MDNode::get(cgMod.GetLLVMContext(), {}));
    }
    return loadInst;
}

llvm::Value* IRBuilder2::CreateStructGEP(llvm::Type* ty, llvm::Value* ptr, unsigned idx, const llvm::Twine& name)
{
    return LLVMIRBuilder2::CreateConstInBoundsGEP2_32(ty, ptr, 0, idx, name);
}

llvm::Value* IRBuilder2::CreateVArrayGEP(
    const CGValue* varrayPtr, std::vector<llvm::Value*>& idxList, const llvm::Twine& name)
{
    (void)idxList.insert(idxList.begin(), getInt64(0));
    auto varray = llvm::dyn_cast<llvm::ArrayType>(varrayPtr->GetCGType()->GetPointerElementType()->GetLLVMType());
    return LLVMIRBuilder2::CreateGEP(varray, varrayPtr->GetRawValue(), idxList, name);
}

llvm::BasicBlock* IRBuilder2::CreateEntryBasicBlock(llvm::Function* parent, const std::string& name) const
{
    auto& functionBBs = parent->getBasicBlockList();
    llvm::BasicBlock* oldEntryBB = nullptr;
    if (!functionBBs.empty()) {
        oldEntryBB = &functionBBs.front();
    }

    return llvm::BasicBlock::Create(getContext(), name, parent, oldEntryBB);
}

std::vector<llvm::BasicBlock*> IRBuilder2::CreateAndInsertBasicBlocks(
    const std::initializer_list<std::string>& basicBlocksName) const
{
    std::vector<llvm::BasicBlock*> ret;
    auto bb = GetInsertBlock();
    auto function = bb->getParent();
    for (auto& bbName : basicBlocksName) {
        auto it = ret.emplace_back(llvm::BasicBlock::Create(getContext(), bbName, function));
        it->moveAfter(bb);
        bb = it;
    }
    return ret;
}

void IRBuilder2::EmitDeclare(const CHIR::Debug& debugNode, bool pointerWrapper)
{
    CJC_ASSERT(cgMod.GetCGContext().GetCompileOptions().enableCompileDebug);
    cgMod.diBuilder->EmitDeclare(debugNode, *GetInsertBlock(), pointerWrapper);
    EmitLocation(CHIRExprWrapper(debugNode));
}

void IRBuilder2::CreateLocalVarPointer(const CHIR::Debug& debugNode, const CGValue& cgValue)
{
    CJC_ASSERT(cgMod.GetCGContext().GetCompileOptions().enableCompileDebug);
    auto cgType = cgValue.GetCGType();
    auto arg = cgValue.GetRawValue();
    auto tmpArg = CreateEntryAlloca(
        llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext(), 1), nullptr, debugNode.GetSrcCodeIdentifier() + ".debug");
    auto inst = CreateStore(arg, tmpArg);
    llvm::cast<llvm::Instruction>(tmpArg)->setDebugLoc(llvm::DebugLoc());
    CJC_NULLPTR_CHECK(inst);
    inst->setDebugLoc(llvm::DebugLoc());
    cgMod.SetOrUpdateMappedCGValue(debugNode.GetValue(), std::make_unique<CGValue>(tmpArg, cgType));
    EmitDeclare(debugNode);
    cgMod.SetValuesToLoad(debugNode.GetValue(), llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext(), 1), tmpArg);
    cgMod.SetOrUpdateMappedCGValue(debugNode.GetValue(), std::make_unique<CGValue>(arg, cgType));
}

void IRBuilder2::CreateUnBoxDeclare(const CHIR::Debug& debugNode, const CGValue& cgValue)
{
    // For capturedVars, should unbox from the class "$Captured_xxx", and store as a pointer.
    CJC_ASSERT(DeRef(*debugNode.GetOperand(0)->GetType())->IsClass());
    auto capturedTy = StaticCast<CHIR::ClassType*>(DeRef(*debugNode.GetOperand(0)->GetType()))
                          ->GetClassDef()
                          ->GetDirectInstanceVar(0)
                          .type;
    auto capturedCGType = CGType::GetOrCreate(cgMod, capturedTy);
    auto load = CreateLoad(cgValue);
    auto eleType = cgValue.GetCGType()->GetPointerElementType()->GetPointerElementType();
    auto ret = CreateConstGEP1_32(getInt8Ty(), load, sizeof(void*));
    auto layoutType = dynamic_cast<const CGClassType*>(eleType)->GetLayoutType();
    ret = CreateBitCast(ret, layoutType->getPointerTo(1));
    ret = LLVMIRBuilder2::CreateStructGEP(layoutType, ret, 0);
    CJC_ASSERT(ret->getType()->isPointerTy());
    llvm::Value* alloca;
    if (capturedCGType->IsRefType() || capturedCGType->IsStructType()) {
        alloca = CreateEntryAlloca(getInt8PtrTy(1), nullptr, debugNode.GetSrcCodeIdentifier() + ".debug");
        auto bitCast = CreateBitCast(ret, getInt8PtrTy(1));
        CreateStore(bitCast, alloca);
    } else {
        alloca = CreateEntryAlloca(ret->getType(), nullptr, debugNode.GetSrcCodeIdentifier() + ".debug");
        CreateStore(ret, alloca);
    }
    cgMod.diBuilder->EmitUnBoxDeclare(
        debugNode, *DeRef(*capturedTy), debugNode.GetSrcCodeIdentifier(), alloca, *GetInsertBlock());
    EmitLocation(CHIRExprWrapper(debugNode));
}

void IRBuilder2::CreateEnvDeclare(const CHIR::GetElementRef& getEleRef, llvm::Value* gep)
{
    auto envTy = DeRef(*getEleRef.GetOperand(0)->GetType());
    CJC_ASSERT(envTy->IsClass());
    /*
        for capturedVar with var of CHIR is like:
        func (%1: env) {
            %2: $Captured_XXE&& = GetElementRef(%1, memberIndex) // this gep what we are translating now
            %3: $Captured_XXE& = Load(%2)
            ...
            %n: XX& = GetElementRef(%3, 0)
        }
        and for capturedVar with let of CHIR is like:
        func (%1: env) {
            %2: XXE& = GetElementRef(%1, memberIndex) // this gep what we are translating now
        }
    */
    CJC_NULLPTR_CHECK(getEleRef.GetResultType());
    auto retTy = DeRef(*getEleRef.GetResultType());
    bool isCapturedWithVar = retTy->IsClass() && IsCapturedClass(*StaticCast<CHIR::ClassType*>(retTy)->GetClassDef());
    auto name = StaticCast<CHIR::ClassType*>(envTy)->GetClassDef()->GetDirectInstanceVar(getEleRef.GetPath()[0]).name;
    if (isCapturedWithVar) {
        auto capturedClass = getEleRef.GetResultType();
        // $Captured_XXE&
        auto load = CreateLoad(CGValue(gep, CGType::GetOrCreate(cgMod, capturedClass)));
        CJC_ASSERT(capturedClass->IsRef());
        auto capturedVarPointer = CreateGEP(
            CGValue(load, CGType::GetOrCreate(cgMod, StaticCast<CHIR::RefType*>(capturedClass)->GetBaseType())), {0});
        auto capturedTy =
            StaticCast<CHIR::ClassType*>(DeRef(*capturedClass))->GetClassDef()->GetDirectInstanceVar(0).type;
        auto capturedCGType = CGType::GetOrCreate(cgMod, capturedTy);
        auto pointer = capturedCGType->IsRefType() || capturedCGType->IsStructType()
            ? getInt8PtrTy(1)
            : capturedVarPointer.GetRawValue()->getType();
        auto ret = capturedCGType->IsRefType() || capturedCGType->IsStructType()
            ? CreateBitCast(capturedVarPointer.GetRawValue(), getInt8PtrTy(1))
            : capturedVarPointer.GetRawValue();
        auto alloca = CreateEntryAlloca(pointer, nullptr, name + ".debug");
        CreateStore(ret, alloca);
        cgMod.diBuilder->EmitUnBoxDeclare(getEleRef, *DeRef(*capturedTy), name, alloca, *GetInsertBlock());
    } else {
        // the result of gep must be a pointer.
        CJC_NULLPTR_CHECK(getEleRef.GetResultType());
        CJC_ASSERT(getEleRef.GetResultType()->IsRef());
        auto capturedCGType =
            CGType::GetOrCreate(cgMod, StaticCast<CHIR::RefType*>(getEleRef.GetResultType())->GetBaseType());
        auto pointer = capturedCGType->IsRefType() || capturedCGType->IsStructType() ? getInt8PtrTy(1) : gep->getType();
        auto ret =
            capturedCGType->IsRefType() || capturedCGType->IsStructType() ? CreateBitCast(gep, getInt8PtrTy(1)) : gep;
        auto alloca = CreateEntryAlloca(pointer, nullptr, name + ".debug");
        CreateStore(ret, alloca);
        cgMod.diBuilder->EmitUnBoxDeclare(getEleRef, *retTy, name, alloca, *GetInsertBlock());
    }
}

llvm::Type* IRBuilder2::HandleArgPointerType(const CGType& argType) const
{
    // If the pointer type is Ref type, it should be stored in addrSpace(1)**.
    // If the pointer type is Derive type, it should be stored in addrSpace(0)*.
    if (argType.IsRefType()) {
        return argType.GetPointerElementType()->GetLLVMType()->getPointerTo(1);
    } else {
        return GetPointerToWithSpecificAddrSpace(&argType, 0);
    }
}

void IRBuilder2::CreateValuePointer(const CHIR::Debug& debugNode, const CGValue& cgValue)
{
    auto cgType = cgValue.GetCGType();
    CJC_NULLPTR_CHECK(cgType);
    bool isRefType = cgType->IsRefType();
    auto arg = cgValue.GetRawValue();
    CJC_NULLPTR_CHECK(arg);
    auto argType = arg->getType();
    CJC_NULLPTR_CHECK(argType);
    auto isDeriveType = argType->isPointerTy() && !isRefType;
    argType = cgType->IsPointerType() ? HandleArgPointerType(*cgType) : argType;
    // For deriveType, the argPtr should cast to addrSpace(0)*
    // AddrSpaceCast instruction can be used for store.
    llvm::Value* debugArg = isDeriveType ? CreateAddrSpaceCast(arg, argType) : arg;
    auto tmpArg = CreateEntryAlloca(argType, nullptr, arg->getName() + ".debug");
    auto inst = CreateStore(debugArg, tmpArg);
    // Parameter info should not have debug location, and we set an empty location for it.
    llvm::cast<llvm::Instruction>(tmpArg)->setDebugLoc(llvm::DebugLoc());
    CJC_NULLPTR_CHECK(inst);
    inst->setDebugLoc(llvm::DebugLoc());
    cgMod.SetOrUpdateMappedCGValue(debugNode.GetValue(), std::make_unique<CGValue>(tmpArg, cgType));
    EmitDeclare(debugNode);
    if (!isDeriveType) {
        cgMod.SetValuesToLoad(debugNode.GetValue(), argType, tmpArg);
    }
    cgMod.SetOrUpdateMappedCGValue(debugNode.GetValue(), std::make_unique<CGValue>(arg, cgType));
}

void IRBuilder2::CreateBoxedValueForValueType(const CHIR::Debug& debugNode, const CGValue& cgValue)
{
    auto curLoc = getCurrentDebugLocation();
    SetCurrentDebugLocation(llvm::DebugLoc());
    auto arg = cgValue.GetRawValue();
    auto cgType = cgValue.GetCGType();
    auto payloadSize = GetLayoutSize_32(*debugNode.GetValue()->GetType());
    CJC_NULLPTR_CHECK(arg);
    auto argType = arg->getType();

    // Alloca and store typeInfo
    auto tiValue = CreateTypeInfo(DeRef(*debugNode.GetValue()->GetType()));
    auto thisDebug = CallIntrinsicAllocaGeneric({tiValue, payloadSize});
    thisDebug->setName(arg->getName() + ".generic.alloca");

    // Store value for parameter
    auto ty = DeRef(*debugNode.GetValue()->GetType());
    CJC_NULLPTR_CHECK(ty);
    if (debugNode.GetValue()->IsParameter()) {
        auto payload = GetPayloadFromObject(thisDebug);
        auto castedPtr = CreateBitCast(payload, argType->getPointerTo(1));
        auto addrType = CGType::GetOrCreate(
            cgMod, CGType::GetRefTypeOf(cgMod.GetCGContext().GetCHIRBuilder(), *debugNode.GetValue()->GetType()), 1U);
        CreateStore(cgValue, CGValue(castedPtr, addrType));
        if (ty->IsStruct()) {
            auto curCHIRFunc = DynamicCast<const CHIR::Func*>(&GetInsertCGFunction()->GetOriginal());
            CJC_NULLPTR_CHECK(curCHIRFunc);
            bool shouldUpdateThis =
                curCHIRFunc->GetSrcCodeIdentifier() == "init" || curCHIRFunc->TestAttr(CHIR::Attribute::MUT);
            cgMod.GetCGContext().debugValue =
                shouldUpdateThis && arg->getName() == "this" ? thisDebug : cgMod.GetCGContext().debugValue;
        }
    }

    // Create dbg declare.
    auto tempAlloca = thisDebug;
    bool pointerWrapper = false;
    bool isZeroSize = CGType::GetOrCreate(cgMod, ty)->GetSize() == 0;
    if (arg->getName() != "this" || !argType->isPointerTy() || isZeroSize) {
        tempAlloca = CreateEntryAlloca(getInt8PtrTy(1), nullptr, arg->getName() + ".debug");
        CreateStore(thisDebug, tempAlloca);
        pointerWrapper = true;
    } else {
        thisDebug->setName(arg->getName() + ".debug");
    }
    cgMod.SetOrUpdateMappedCGValue(debugNode.GetValue(), std::make_unique<CGValue>(tempAlloca, cgType));
    EmitDeclare(debugNode, pointerWrapper);
    bool isStructThis = ty->IsStruct() && debugNode.GetSrcCodeIdentifier() == "this";
    if (!debugNode.GetValue()->IsParameter() || isStructThis) {
        cgMod.SetBoxedValuesToLoad(debugNode.GetValue(), argType, thisDebug);
    }
    cgMod.SetOrUpdateMappedCGValue(debugNode.GetValue(), std::make_unique<CGValue>(arg, cgType));
    SetCurrentDebugLocation(curLoc);
}

void IRBuilder2::CreateLoadInstForParameter(const CHIR::Expression& chirExpression)
{
    for (auto operand : chirExpression.GetOperands()) {
        if (auto it = cgMod.GetValuesToLoad(operand); it.has_value()) {
            auto loadInst = LLVMIRBuilder2::CreateLoad(it->first, it->second);
            if (auto basePtr = GetCGContext().GetBasePtrOf(**(cgMod | operand))) {
                GetCGContext().SetBasePtr(loadInst, basePtr);
            }
            FixedCGTypeOfFuncArg(cgMod, *operand, *loadInst);
        }
        if (auto it = cgMod.GetBoxedValuesToLoad(operand); it.has_value()) {
            if (chirExpression.GetExprKind() == CHIR::ExprKind::STORE_ELEMENT_REF &&
                chirExpression.GetOperand(1) == operand) {
                auto gep = CreateGEP(getInt8Ty(), it->second, getInt64(8));
                auto castToCurrentValue = CreateBitCast(gep, it->first);
                FixedCGTypeOfFuncArg(cgMod, *operand, *castToCurrentValue);
                continue;
            }
            auto payload = GetPayloadFromObject(it->second);
            auto castedPtr =
                CreateBitCast(payload, llvm::cast<llvm::PointerType>(it->first)->getElementType()->getPointerTo(1));
            if (chirExpression.GetExprKind() == CHIR::ExprKind::STORE && chirExpression.GetOperand(1) == operand) {
                FixedCGTypeOfFuncArg(cgMod, *operand, *castedPtr);
                continue;
            }
            CJC_ASSERT(castedPtr->getType()->isPointerTy() && it->first->isPointerTy());
            llvm::Value* temp = castedPtr;
            if (llvm::cast<llvm::PointerType>(it->first)->getElementType()->isStructTy() &&
                llvm::cast<llvm::PointerType>(it->first)->getAddressSpace() == 0) {
                auto elemType = llvm::cast<llvm::PointerType>(it->first)->getElementType();
                temp = CreateEntryAlloca(llvm::cast<llvm::PointerType>(it->first)->getElementType(), nullptr,
                    it->second->getName() + ".copied");
                if (IsTypeContainsRef(elemType)) {
                    auto structType = llvm::cast<llvm::StructType>(elemType);
                    auto layOut = GetLLVMModule()->getDataLayout().getStructLayout(structType);
                    auto size = getInt64(layOut->getSizeInBytes());
                    CallGCReadAgg({temp, it->second, castedPtr, size});
                } else {
                    auto layOut = GetCGModule().GetLLVMModule()->getDataLayout().getStructLayout(
                        llvm::cast<llvm::StructType>(elemType));
                    CJC_NULLPTR_CHECK(layOut);
                    auto size = layOut->getSizeInBytes();
                    auto align = layOut->getAlignment();
                    CreateMemCpy(temp, align, castedPtr, align, size);
                }
            }
            FixedCGTypeOfFuncArg(cgMod, *operand, *temp);
        }
    }
}

void IRBuilder2::CreateGenericParaDeclare(const CGFunction& cgFunc)
{
    if (!cgMod.GetCGContext().GetCompileOptions().enableCompileDebug) {
        return;
    }
    auto chirFunc = dynamic_cast<const CHIR::Func*>(&cgFunc.GetOriginal());
    size_t genericIndex = 0;
    auto null = llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext()));
    // For member function only.
    if (cgFunc.GetCGFunctionType()->GetOuterTypeInfoIndex().has_value()) {
        auto outerDef = chirFunc->GetParentCustomTypeDef();
        for (auto genericTy : outerDef->GetGenericTypeParams()) {
            cgMod.diBuilder->EmitGenericDeclare(*genericTy, null, *GetInsertBlock(), genericIndex);
            ++genericIndex;
        }
    }
    for (auto genericTy : chirFunc->GetGenericTypeParams()) {
        cgMod.diBuilder->EmitGenericDeclare(*genericTy, null, *GetInsertBlock(), genericIndex);
        ++genericIndex;
    }
}

void IRBuilder2::EmitLocation(const CHIRExprWrapper& debugNode)
{
    if (!cgMod.GetCGContext().IsLineInfoEnabled()) {
        return;
    }
    llvm::DILocation* diLoc = cgMod.diBuilder->CreateDILoc(debugNode);
    SetCurrentDebugLocation(diLoc);
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
llvm::Value* IRBuilder2::CallIntrinsicNull() const
{
    return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(CGType::GetRefType(GetLLVMContext())));
}
#endif

llvm::Value* IRBuilder2::CallIntrinsicIsNull(llvm::Value* value)
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    auto rhs = CallIntrinsicNull();
    CJC_NULLPTR_CHECK(rhs);
    return CreateICmpEQ(value, rhs);
#endif
}

llvm::Value* IRBuilder2::CallIntrinsicIsNonNull(llvm::Value* value)
{
    auto rhs = CallIntrinsicNull();
    CJC_NULLPTR_CHECK(rhs);
    return CreateICmpNE(value, rhs);
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
llvm::Value* IRBuilder2::CallIntrinsicRef2Null(llvm::Value* value)
{
    return value;
}
#endif

llvm::Value* IRBuilder2::CallClassIntrinsicInstanceOf(
    llvm::Value* instance, [[maybe_unused]] const CHIR::Type* targetTy)
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    auto instanceInt8Ptr = CreateBitCast(instance, getInt8PtrTy()->getPointerTo(1));
    auto instanceTi = CreateLoad(getInt8PtrTy(), instanceInt8Ptr);
    auto targetTiPtr = CreateTypeInfo(*targetTy);
    auto targetTi = CreateBitCast(targetTiPtr, llvm::Type::getInt8PtrTy(cgMod.GetLLVMContext()));
    return CallIntrinsicIsSubtype({instanceTi, targetTi});
#endif
}

void IRBuilder2::CallExceptionIntrinsicThrow(llvm::Value* exceptionValue)
{
    auto throwFunc = GetExceptionIntrinsicThrow();
    CJC_NULLPTR_CHECK(throwFunc);
    std::vector<llvm::Value*> params{exceptionValue};
    auto unwindBlock = cgMod.GetCGContext().TopUnwindBlockStack();
    if (unwindBlock.has_value()) {
        auto currentBB = GetInsertBlock();
        auto normalDest = llvm::BasicBlock::Create(getContext(), "normalDest", currentBB->getParent());
        normalDest->moveAfter(currentBB);
        (void)CreateInvoke(throwFunc, normalDest, unwindBlock.value(), params);
        SetInsertPoint(normalDest);
    } else {
        (void)CreateCall(throwFunc, params);
    }
}

} // namespace CodeGen
} // namespace Cangjie
