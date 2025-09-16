// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements codegen for CHIR Apply.
 */

#include "Base/ApplyImpl.h"

#include "Base/CHIRExprWrapper.h"
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
#include "CJNative/CGCFFI.h"
#endif
#include "IRAttribute.h"
#include "IRBuilder.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Type/Type.h"

using namespace Cangjie;
using namespace CodeGen;

namespace {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
void AddAttributeForWrapper(const IRBuilder2& builder, const CHIR::FuncType& funcTy,
    const llvm::FunctionType* varFuncTy, llvm::Function* func, llvm::CallBase& callInst, const bool sret)
{
    AddLinkageTypeMetadata(*func, llvm::GlobalValue::LinkageTypes::InternalLinkage, false);
    AddFnAttr(func, llvm::Attribute::NoInline);
    AddFnAttr(func, llvm::Attribute::get(func->getContext(), CJ2C_ATTR));

    auto& context = builder.GetCGContext();
    auto& options = context.GetCompileOptions();
    auto& cgMod = builder.GetCGModule();
    if (sret) {
        callInst.addParamAttr(0, llvm::Attribute::NoAlias);
        AddSRetAttribute(func->arg_begin());
        AddSRetAttribute(&callInst);
        if (options.target.os == Triple::OSType::WINDOWS) {
            auto elemType = GetPointerElementType(varFuncTy->getParamType(0));
            auto align = llvm::Align(GetTypeAlignment(cgMod, *elemType));
            auto alignAttr = llvm::Attribute::getWithAlignment(func->getContext(), align);
            callInst.addParamAttr(0, alignAttr);
            func->arg_begin()->addAttr(alignAttr);
        }
    }

    if (options.target.arch == Triple::ArchType::AARCH64) {
        return;
    } else if (func->getReturnType()->isIntegerTy(1)) {
        AddRetAttr(func, llvm::Attribute::ZExt);
        SetZExtAttrForCFunc(callInst);
    }

    if (options.target.os == Triple::OSType::WINDOWS) {
        return;
    }

    unsigned sretOffset = sret ? 1 : 0;
    unsigned cumulativeOffset = 0;
    auto chirParamTys = funcTy.GetParamTypes();
    for (unsigned i = 0; i < chirParamTys.size(); i++) {
        CJC_ASSERT(i < chirParamTys.size() && chirParamTys[i]);
        auto chirParamTy = chirParamTys[i];
        if (!IsCommonStruct(*chirParamTy)) {
            continue;
        }
        if (IsZeroSizedTypeInC(cgMod, *chirParamTy)) {
            cumulativeOffset--;
            continue;
        }
        unsigned idx = i + cumulativeOffset + sretOffset;
        auto paramType = varFuncTy->getParamType(idx);
        if (!IsLitStructPtrType(paramType) && IsStructPtrType(paramType)) {
            // 1 means the actual callee.
            auto argument = func->getArg(idx + 1);
            AddByValAttribute(argument, GetTypeAlignment(cgMod, *argument->getType()));
            auto byValAttr =
                llvm::Attribute::getWithByValType(context.GetLLVMContext(), GetPointerElementType(paramType));
            callInst.addParamAttr(idx, byValAttr);
        } else if (GetTypeSize(cgMod, *paramType) < GetTypeSize(cgMod, *chirParamTy)) {
            cumulativeOffset++;
        }
    }
}

void GenerateWrapperFuncBody(IRBuilder2& builder, const CHIR::FuncType& funcTy, llvm::FunctionType* varFuncTy,
    llvm::Function* function, const bool sret)
{
    llvm::IRBuilderBase::InsertPointGuard builderGuard(builder);
    auto entryBB = builder.CreateEntryBasicBlock(function, "entry");
    builder.SetInsertPoint(entryBB);
    // Clear debug location info, we don't need it here.
    builder.SetCurrentDebugLocation({});

    // In the wrapper function, we only need to call the actual function pointer and return its return value.
    // First, we construct the arguments to call the actual function pointer.
    std::vector<llvm::Value*> args;
    args.reserve(varFuncTy->getNumParams());
    auto argIt = function->arg_begin();
    if (sret) {
        // In this case, the first parameter of the wrapper function is the pointer to the return value.
        args.push_back(argIt);
        ++argIt;
    }
    // Skip the called function pointer in the wrapper function parameters.
    // The following parameters are the arguments of the called function pointer.
    for (++argIt; argIt != function->arg_end(); ++argIt) {
        args.push_back(argIt);
    }

    auto callInst = builder.LLVMIRBuilder2::CreateCall(varFuncTy, function->getArg(sret ? 1 : 0), args);
    if (varFuncTy->getReturnType()->isVoidTy()) {
        (void)builder.CreateRetVoid();
    } else {
        (void)builder.CreateRet(callInst);
    }

    AddAttributeForWrapper(builder, funcTy, varFuncTy, function, *callInst, sret);
}

/**
 * On cjnative backend, invoking CFunc requires a stub, but indirect invocations cannot be detected.
 * Therefore, in this scenario, we add a wrapper function for the CFunc variable to ensure that the stub function is
 * still used.
 */
llvm::Function* GetIndirectCFuncCallWrapper(IRBuilder2& builder, const CHIR::FuncType& funcTy, llvm::Type* funcPtrType)
{
    llvm::FunctionType* funcType = nullptr;
    auto& cgMod = builder.GetCGModule();
    if (IsLitStructPtrType(funcPtrType)) {
        // In indirect calling scenario, we can get the actual cfunc type by funcTy.
        auto cgType = static_cast<CGFunctionType*>(CGType::GetOrCreate(cgMod, &funcTy));
        CJC_ASSERT(cgType && llvm::isa<llvm::FunctionType>(cgType->GetLLVMFunctionType()));
        funcType = llvm::cast<llvm::FunctionType>(cgType->GetLLVMFunctionType());
    } else {
        funcType = llvm::dyn_cast<llvm::FunctionType>(GetPointerElementType(funcPtrType));
    }
    CJC_NULLPTR_CHECK(funcType);

    // First, we generate a wrapper function based on the type of the called function.
    // The type of the wrapper function complies with the following rules:
    //   1. If the return value is not a structure, or is a structure does not need to be placed in the first parameter
    //      position, we put the actual called function pointer in the first argument, and it's parameters in turn
    //      follow.
    //   2. If the return value is a structure that needs to be placed in the first parameter position, we keep the
    //      return value as the first argument, then put the actual called function pointer in the second argument, and
    //      it's parameters in turn follow.
    std::vector<llvm::Type*> paramTys;
    paramTys.reserve(funcType->getNumParams() + 1);
    auto beginIt = funcType->param_begin();
    auto chirRetTy = funcTy.GetReturnType();
    bool sret =
        IsCommonStruct(*chirRetTy) && !IsZeroSizedTypeInC(cgMod, *chirRetTy) && funcType->getReturnType()->isVoidTy();
    if (sret) {
        CJC_ASSERT(!funcType->params().empty() && "CFunc with sret has at least one parameter.");
        paramTys.emplace_back(*funcType->param_begin());
        beginIt++;
    }
    paramTys.emplace_back(funcType->getPointerTo());
    paramTys.insert(paramTys.end(), beginIt, funcType->param_end());
    auto wrapperFuncType = llvm::FunctionType::get(funcType->getReturnType(), paramTys, funcType->isVarArg());
    auto wrapperFunc = cgMod.GetOrInsertFunction("wrapper." + MangleType(funcTy), wrapperFuncType);
    // For CFunc of the same type, we only need to generate one wrapper function. If we get a declaration,
    // it means that this is the first generation, and we need to build its function body then.
    if (wrapperFunc->isDeclaration()) {
        GenerateWrapperFuncBody(builder, funcTy, funcType, wrapperFunc, sret);
    }
    return wrapperFunc;
}

llvm::Function* UpdateCFuncCalleeAndArgs(
    IRBuilder2& builder, const CHIR::FuncType& funcTy, llvm::Value* callee, std::vector<CGValue*>& args)
{
    CJC_ASSERT(funcTy.IsCFunc());
    if (!llvm::isa<llvm::Function>(callee)) {
        auto calleeType = callee->getType();
        auto wrapperFunc = GetIndirectCFuncCallWrapper(builder, funcTy, calleeType);
        auto& cgMod = builder.GetCGModule();
        auto cgType = CGType::GetOrCreate(builder.GetCGModule(), &funcTy);
        // A literal struct pointer type means a cfunc pointer here.
        // So we need insert a bitcast instruction for calling it then.
        if (IsLitStructPtrType(calleeType)) {
            auto cast = builder.CreateBitCast(callee, cgType->GetLLVMType());
            (void)args.insert(args.begin(), cgMod.CreateGhostCFuncArgValue(*cast, *cgType));
        } else {
            (void)args.insert(args.begin(), cgMod.CreateGhostCFuncArgValue(*callee, *cgType));
        }
        return wrapperFunc;
    }
    return llvm::cast<llvm::Function>(callee);
}

inline bool HasAddrSpace(const CGValue* cgVal, unsigned int addrSpace)
{
    auto llvmVal = cgVal->GetRawValue();
    CJC_NULLPTR_CHECK(llvmVal);
    auto llvmType = llvmVal->getType();
    return llvmType->isPointerTy() && llvmType->getPointerAddressSpace() == addrSpace;
}
#ifdef __APPLE__
inline void AddAttrForIntTypeArgOnMacMx(llvm::CallBase& callRet, const llvm::Use& arg)
{
    auto intType = llvm::dyn_cast<llvm::IntegerType>(arg->getType());
    if (!intType) {
        return;
    }
    AddParamAttr(&callRet, arg.getOperandNo(), llvm::Attribute::NoUndef);
    if (intType->isIntegerTy(1u)) { // 1u means bool type.
        AddParamAttr(&callRet, arg.getOperandNo(), llvm::Attribute::ZExt);
    } else if (intType->getBitWidth() < 32) { // 32 means int32_t type.
        AddParamAttr(&callRet, arg.getOperandNo(), llvm::Attribute::SExt);
    }
}
#endif
llvm::Value* CreateCFuncCallOrInvoke(IRBuilder2& irBuilder, llvm::Function& callee,
    const std::vector<CGValue*>& argsVal, const CHIR::Type& chirRetTy, bool isSRet)
{
    auto& cgMod = irBuilder.GetCGModule();
    std::vector<llvm::Value*> llvmArgs;
    if (isSRet) {
        llvmArgs.reserve(argsVal.size() + 1);
        auto retVal = irBuilder.CreateEntryAlloca(*CGType::GetOrCreate(cgMod, &chirRetTy));
        llvmArgs.emplace_back(retVal);
    } else {
        llvmArgs.reserve(argsVal.size());
    }
    for (auto cgVal : argsVal) {
        auto& cgValTy = cgVal->GetCGType()->GetOriginal();
        if (IsZeroSizedTypeInC(cgMod, cgValTy)) {
            continue;
        }
        if (HasAddrSpace(cgVal, 1u)) {
            auto cgType = CGType::GetOrCreate(cgMod, &cgValTy);
            auto cast = irBuilder.CreateAddrSpaceCast(cgVal->GetRawValue(), cgType->GetLLVMType());
            llvmArgs.emplace_back(cast);
        } else {
            llvmArgs.emplace_back(cgVal->GetRawValue());
        }
    }
    auto callRet = irBuilder.CreateCallOrInvoke(callee.getFunctionType(), &callee, llvmArgs);
    const auto& target = cgMod.GetCGContext().GetCompileOptions().target;
    const bool nonAarch64 = target.arch != Triple::ArchType::AARCH64;
    for (auto it = callee.arg_begin(); it != callee.arg_end(); ++it) {
        if (nonAarch64 && it->hasByValAttr()) {
            auto byValAttr =
                llvm::Attribute::getWithByValType(cgMod.GetLLVMContext(), GetPointerElementType(it->getType()));
            AddParamAttr(callRet, it->getArgNo(), byValAttr);
        }
    }
#ifdef __APPLE__
    const bool macOnMx = !nonAarch64 && target.os == Triple::OSType::DARWIN;
    if (macOnMx) {
        for (auto ext : {llvm::Attribute::SExt, llvm::Attribute::ZExt}) {
            if (callee.hasRetAttribute(ext)) {
                callRet->addRetAttr(ext);
            }
        }
        for (auto& arg : callRet->args()) {
            AddAttrForIntTypeArgOnMacMx(*callRet, arg);
        }
    }
#endif
    if (isSRet) {
        AddParamAttr(callRet, 0, llvm::Attribute::NoAlias);
        AddSRetAttribute(callRet);
    }
    return isSRet ? *llvmArgs.begin() : callRet;
}

CGValue* HandleVarargTypePromotion(IRBuilder2& irBuilder, CGValue* argVal)
{
    using CHIRTypeKind = CHIR::Type::TypeKind;
    auto& cgMod = irBuilder.GetCGModule();
    auto chirArgTy = argVal->GetCGType()->GetOriginal();
    llvm::Value* ret = nullptr;
    if (chirArgTy.IsFloat()) {
        ret = irBuilder.CreateFPExt(argVal->GetRawValue(), llvm::Type::getDoubleTy(cgMod.GetLLVMContext()));
    } else if (Utils::In(chirArgTy.GetTypeKind(), {CHIRTypeKind::TYPE_INT8, CHIRTypeKind::TYPE_INT16})) {
        ret = irBuilder.CreateSExt(argVal->GetRawValue(), llvm::Type::getInt32Ty(cgMod.GetLLVMContext()));
    } else if (Utils::In(chirArgTy.GetTypeKind(), {CHIRTypeKind::TYPE_UINT8, CHIRTypeKind::TYPE_UINT16})) {
        ret = irBuilder.CreateZExt(argVal->GetRawValue(), llvm::Type::getInt32Ty(cgMod.GetLLVMContext()));
    } else if (chirArgTy.IsBoolean()) {
        ret = irBuilder.CreateZExt(argVal->GetRawValue(), llvm::Type::getInt32Ty(cgMod.GetLLVMContext()));
    } else {
        return argVal;
    }
    CJC_NULLPTR_CHECK(ret);
    auto cgType = chirArgTy.IsFloat() ? CGType::GetFloat64CGType(cgMod) : CGType::GetInt32CGType(cgMod);
    return cgMod.CreateGhostCFuncArgValue(*ret, *cgType);
}

void HandleForeignFuncCall(IRBuilder2& irBuilder, const CHIR::FuncType& chirFuncTy, std::vector<CGValue*>& argsVal)
{
    // Only foreign functions can have variable-length arguments.
    if (!chirFuncTy.HasVarArg()) {
        return;
    }

    size_t argCount = 0;
    size_t paramSize = chirFuncTy.GetParamTypes().size();
    for (auto& arg : argsVal) {
        argCount++;
        if (IsZeroSizedTypeInC(irBuilder.GetCGModule(), arg->GetCGType()->GetOriginal())) {
            continue;
        }
        if (argCount > paramSize) {
            arg = HandleVarargTypePromotion(irBuilder, arg);
        }
    }
}

llvm::Value* HandleApplyCFunc(
    IRBuilder2& irBuilder, const CHIRApplyWrapper& apply, const CHIR::FuncType& chirFuncTy, const CGValue& callee)
{
    auto& cgMod = irBuilder.GetCGModule();
    std::vector<CGValue*> argsVal;
    for (auto arg : apply.GetArgs()) {
        if (!IsZeroSizedTypeInC(cgMod, *arg->GetType())) {
            argsVal.emplace_back(cgMod | arg);
        }
    }
    HandleForeignFuncCall(irBuilder, chirFuncTy, argsVal);
    bool handleRet = cgMod.GetCGCFFI().ProcessInvocation(chirFuncTy, argsVal, irBuilder);
    auto actualCallee = UpdateCFuncCalleeAndArgs(irBuilder, chirFuncTy, callee.GetRawValue(), argsVal);
    auto chirRetTy = chirFuncTy.GetReturnType();
    CJC_ASSERT(actualCallee && chirRetTy);
    bool isSRet = !handleRet && chirRetTy->IsStruct() && !IsZeroSizedTypeInC(cgMod, *chirRetTy);
    auto callRet = CreateCFuncCallOrInvoke(irBuilder, *actualCallee, argsVal, *chirRetTy, isSRet);
    if (IsZeroSizedTypeInC(cgMod, *chirRetTy)) {
        return irBuilder.CreateEntryAlloca(
            *CGType::GetOrCreate(cgMod, chirRetTy), apply.GetCallee()->GetSrcCodeIdentifier() + ".ret");
    }
    if (!handleRet) {
        return callRet;
    }
    auto sret = irBuilder.CreateEntryAlloca(
        *CGType::GetOrCreate(cgMod, chirRetTy), apply.GetCallee()->GetSrcCodeIdentifier() + ".ret");
    return cgMod.GetCGCFFI().ProcessCallRet(*chirRetTy, *callRet, *sret, irBuilder);
}
#endif
} // namespace

llvm::Value* CodeGen::GenerateApply(IRBuilder2& irBuilder, const CHIRApplyWrapper& apply)
{
    irBuilder.SetCHIRExpr(&apply);
    irBuilder.EmitLocation(apply);
    auto& cgMod = irBuilder.GetCGModule();
    auto callee = cgMod | apply.GetCallee();
    CJC_ASSERT(apply.GetCallee()->GetType() && apply.GetCallee()->GetType()->IsFunc());

    auto chirFuncTy = StaticCast<const CHIR::FuncType*>(apply.GetCallee()->GetType());
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (chirFuncTy->IsCFunc()) {
        return HandleApplyCFunc(irBuilder, apply, *chirFuncTy, *callee);
    }
#endif

    std::vector<CGValue*> argsVal;
    for (auto arg : apply.GetArgs()) {
        (void)argsVal.emplace_back(cgMod | arg);
    }

    auto funcType = static_cast<CGFunctionType*>(CGType::GetOrCreateWithNode(cgMod, apply.GetCallee()));
    auto funcPtrType = funcType->GetLLVMFunctionType()->getPointerTo();
    llvm::Value* calleeVal = callee->GetRawValue();
    if (!apply.GetCallee()->TestAttr(CHIR::Attribute::STATIC)) {
        if (IsStructOrExtendMethod(*apply.GetCallee()) &&
            argsVal[0]->GetRawValue()->getType()->getPointerAddressSpace() == 1U &&
            !cgMod.GetCGContext().GetBasePtrOf(**argsVal[0])) {
            auto wrapper = StaticCast<CGFunction*>(callee)->GetWrapperFunction();
            funcType = static_cast<CGFunctionType*>(CGType::GetOrCreateWithNode(cgMod, apply.GetCallee(), false));
            funcPtrType = wrapper->getType();
            calleeVal = wrapper;
        }
    }

    auto castedCallee = irBuilder.CreateBitCast(calleeVal, funcPtrType);
    bool isClosureCall = apply.GetCallee()->IsLocalVar();
    llvm::Value* thisTI = nullptr;
    if (apply.IsCalleeStatic()) {
        auto curCGFunc = irBuilder.GetInsertCGFunction();
        CJC_ASSERT(curCGFunc != nullptr);
        auto curCHIRFunc = DynamicCast<const CHIR::Func*>(&curCGFunc->GetOriginal());
        CJC_ASSERT(curCHIRFunc != nullptr);
        auto curCHIRFuncParentTy = curCHIRFunc->GetParentCustomTypeOrExtendedType();
        auto calleeFunc = DynamicCast<const CHIR::FuncBase*>(apply.GetCallee());
        CJC_NULLPTR_CHECK(calleeFunc);
        auto calleeFuncParentTy = calleeFunc->GetParentCustomTypeOrExtendedType();
        auto curFunc = irBuilder.GetInsertFunction();
        CJC_ASSERT(curFunc != nullptr);
        if (curCGFunc && curCGFunc->GetOriginal().TestAttr(CHIR::Attribute::STATIC) &&
            curCHIRFuncParentTy == calleeFuncParentTy) {
            // use the param `thisTI` if we are in context of static func
            thisTI = curFunc->arg_end() - 1;
        } else if (curCHIRFuncParentTy && curCHIRFuncParentTy == calleeFuncParentTy &&
            curCHIRFuncParentTy->IsReferenceType()) {
            // here we want to exclude those case where `this` param has no type info, but maybe the condition is
            // not correct if we are in context of a non-static func, then we can get the `thisTI` from the `this` param
            CJC_NULLPTR_CHECK(curCGFunc);
            CJC_NULLPTR_CHECK(curFunc);
            auto thisParam = curCGFunc->IsSRet() ? curFunc->arg_begin() + 1 : curFunc->arg_begin();
            thisTI = irBuilder.GetTypeInfoFromObject(thisParam);
        } else {
            thisTI = irBuilder.CreateBitCast(irBuilder.CreateTypeInfo(apply.GetThisType()),
                CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext()));
        }
        CJC_ASSERT(thisTI != nullptr);
    }
    return irBuilder.CreateCallOrInvoke(*funcType, castedCallee, argsVal, isClosureCall, thisTI);
}
