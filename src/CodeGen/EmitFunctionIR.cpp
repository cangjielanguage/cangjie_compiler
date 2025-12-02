// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "EmitFunctionIR.h"

#include "Base/CGTypes/CGEnumType.h"
#include "CGModule.h"
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
#include "CJNative/CJNativeCGCFFI.h"
#endif
#include "DIBuilder.h"
#include "EmitBasicBlockIR.h"
#include "IRAttribute.h"
#include "IRBuilder.h"
#include "IRGenerator.h"
#include "Utils/CGCommonDef.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/CHIR/IR/Value/Value.h"

namespace {
using namespace Cangjie;
using namespace CodeGen;

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
inline bool NeedProcessParamOnDemand(const CGModule& cgMod, const CHIR::FuncType& chirFuncTy, size_t index)
{
    return cgMod.GetCGCFFI().NeedProcessParam(chirFuncTy, index);
}

inline llvm::Value* ProcessParamOnDemand(
    CGModule& cgMod, CHIR::Type& chirParamTy, llvm::Function::arg_iterator& arg, IRBuilder2& builder)
{
    auto cgType = CGType::GetOrCreate(cgMod, &chirParamTy);
    if (chirParamTy.IsVArray()) {
        auto paramType = cgType->GetLLVMType();
        return builder.CreateBitCast(arg, paramType->getPointerTo());
    }
    auto place = builder.CreateEntryAlloca(*cgType);
    cgMod.GetCGCFFI().ProcessParam(chirParamTy, arg, place, builder);
    return place;
}

inline void InitIRBuilder(
    std::unique_ptr<IRBuilder2>& builder, CGModule& cgMod, const CHIR::Func& chirFunc, llvm::Function& llvmFunc)
{
    if (!builder) {
        builder = std::make_unique<IRBuilder2>(cgMod);
        auto chirEntryBB = chirFunc.GetEntryBlock();
        auto entryBB = llvm::BasicBlock::Create(
            cgMod.GetLLVMContext(), "entry:" + chirEntryBB->GetIdentifierWithoutPrefix(), &llvmFunc);
        builder->SetInsertPoint(entryBB);
        cgMod.SetOrUpdateMappedBB(chirEntryBB, entryBB);
    }
}

void HandleCFuncParams(CGModule& cgMod, const CHIR::Func& chirFunc, llvm::Function& llvmFunc)
{
    auto chirFuncTy = chirFunc.GetFuncType();
    std::unique_ptr<IRBuilder2> builder;
    auto llvmArgIt = llvmFunc.arg_begin();
    if (chirFunc.GetNumOfParams() == 0) {
        return;
    }
    if (llvmArgIt != llvmFunc.arg_end() && llvmArgIt->hasStructRetAttr()) {
        ++llvmArgIt;
    }
    for (size_t chirArgIdx = 0; chirArgIdx < chirFunc.GetNumOfParams(); ++chirArgIdx) {
        CJC_ASSERT(chirArgIdx < chirFunc.GetNumOfParams());
        auto chirFuncArg = chirFunc.GetParam(chirArgIdx);
        auto chirFuncArgTy = chirFuncArg->GetType();
        llvm::Value* llvmValue = nullptr;
        if (IsZeroSizedTypeInC(cgMod, *chirFuncArgTy)) {
            InitIRBuilder(builder, cgMod, chirFunc, llvmFunc);
            llvmValue = builder->CreateEntryAlloca(*CGType::GetOrCreate(cgMod, chirFuncArgTy));
        } else {
            CJC_ASSERT(llvmArgIt != llvmFunc.arg_end());
            llvmValue = llvmArgIt;
            if (NeedProcessParamOnDemand(cgMod, *chirFuncTy, chirArgIdx)) {
                InitIRBuilder(builder, cgMod, chirFunc, llvmFunc);
                llvmValue = ProcessParamOnDemand(cgMod, *chirFuncArgTy, llvmArgIt, *builder);
            }
            ++llvmArgIt;
        }
        FixedCGTypeOfFuncArg(cgMod, *chirFuncArg, *llvmValue);
    }
}

inline void HandleCFuncParams(CGModule& cgMod, const CHIR::Value& chirFunc, llvm::Function& llvmFunc)
{
    if (chirFunc.IsFuncWithBody()) {
        HandleCFuncParams(cgMod, *VirtualCast<const CHIR::Func>(&chirFunc), llvmFunc);
    } else {
        // The remaining case can only be imported function, we needn't process params for it.
        CJC_ASSERT(chirFunc.IsImportedFunc());
    }
}
#endif

void HandleFuncParams(CGModule& cgMod, const CHIR::Func& chirFunc, const CGFunction& cgFunc)
{
    for (size_t idx = 0; idx < chirFunc.GetNumOfParams(); ++idx) {
        auto chirFuncArg = chirFunc.GetParam(idx);
        auto llvmArg = cgFunc.GetArgByIndexFromCHIR(idx);
        auto fixedCGType = FixedCGTypeOfFuncArg(cgMod, *chirFuncArg, *llvmArg);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        if (chirFunc.IsConstructor() && idx == 0) {
            llvmArg->addAttr(llvm::Attribute::NoAlias);
        }
#endif
        if (fixedCGType->IsStructPtrType() || fixedCGType->IsVArrayPtrType()) {
            llvmArg->addAttr(llvm::Attribute::NoAlias);
        }
    }
}

inline void BuildCFunc(CGModule& cgMod, const CHIR::Value& chirFunc, const CGFunction& cgFunc, IRBuilder2& builder)
{
    CJC_ASSERT(chirFunc.GetType()->IsFunc() && StaticCast<CHIR::FuncType*>(chirFunc.GetType())->IsCFunc());
    llvm::Function* llvmFunc = cgFunc.GetRawFunction();

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    auto chirFuncTy = StaticCast<CHIR::FuncType*>(chirFunc.GetType());
    cgMod.GetCGCFFI().AddFunctionAttr(*chirFuncTy, *llvmFunc);
#endif

    if (chirFunc.TestAttr(CHIR::Attribute::FOREIGN)) {
        return;
    }

    if (chirFunc.IsFuncWithBody()) {
        // CFFI wrap func should not set Personality
        auto funcNode = VirtualCast<const CHIR::Func*>(&chirFunc);
        if (!funcNode->IsCFFIWrapper()) {
            llvmFunc->setPersonalityFn(cgMod.GetExceptionIntrinsicPersonality());
        } else {
            auto scope = llvmFunc->getSubprogram();
            auto curLoc = builder.GetCGModule().diBuilder->CreateDILoc(scope, {scope ? scope->getLine() : 0, 0});
            builder.SetCurrentDebugLocation(curLoc);
        }
    } else {
        CJC_ASSERT(chirFunc.IsImportedFunc());
    }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    HandleCFuncParams(cgMod, chirFunc, *llvmFunc);
#endif
    return;
}
} // namespace

namespace Cangjie {
namespace CodeGen {
void BuildCJFunc(CGModule& cgMod, const CHIR::Func& chirFunc, const CGFunction& cgFunc)
{
    llvm::Function* llvmFunc = cgFunc.GetRawFunction();
    llvmFunc->setPersonalityFn(cgMod.GetExceptionIntrinsicPersonality());
    HandleFuncParams(cgMod, chirFunc, cgFunc);
}

class FunctionGeneratorImpl : public IRGeneratorImpl {
    friend class CGModule;

public:
    explicit FunctionGeneratorImpl(CGModule& cgMod, const CHIR::Func& chirFunc) : cgMod(cgMod), chirFunc(chirFunc)
    {
    }

    void EmitIR() override;

private:
    CGModule& cgMod;
    const CHIR::Func& chirFunc;
};

template <> class IRGenerator<FunctionGeneratorImpl> : public IRGenerator<> {
public:
    IRGenerator(CGModule& cgMod, const CHIR::Func& chirFunc)
        : IRGenerator<>(std::make_unique<FunctionGeneratorImpl>(cgMod, chirFunc))
    {
    }
};

void FunctionGeneratorImpl::EmitIR()
{
    IRBuilder2 builder(cgMod);
    auto cgFunc = cgMod.GetOrInsertCGFunction(&chirFunc);
    auto rawFunction = cgFunc->GetRawFunction();
    cgMod.diBuilder->SetSubprogram(&chirFunc, rawFunction);
    auto chirFuncTy = chirFunc.GetFuncType();
    CJC_NULLPTR_CHECK(chirFuncTy);
    if (chirFuncTy->IsCFunc()) {
        CJC_ASSERT(!chirFunc.TestAttr(CHIR::Attribute::FOREIGN));
        BuildCFunc(cgMod, chirFunc, *cgFunc, builder);
    } else {
        BuildCJFunc(cgMod, chirFunc, *cgFunc);
    }

    if (!ShouldReturnVoid(chirFunc)) {
        SetSRetAttrForStructReturnType(*chirFuncTy, *rawFunction);
    }

    auto entryBB = chirFunc.GetBody()->GetEntryBlock();
    EmitBasicBlockIR(cgMod, *entryBB);
    if (rawFunction->getSubprogram()) {
        cgMod.GetCGContext().debugValue = nullptr;
        cgMod.diBuilder->FinalizeSubProgram(*rawFunction);
    }

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    const auto& options = cgMod.GetCGContext().GetCompileOptions();
    if ((options.enableTimer || options.enableMemoryCollect) && (chirFunc.GetGenericDecl() != nullptr)) {
        if (chirFunc.GetGenericDecl()->TestAttr(CHIR::Attribute::IMPORTED)) {
            cgFunc->GetRawFunction()->addFnAttr(GENERIC_DECL_IN_IMPORTED_PKG_ATTR);
        } else {
            cgFunc->GetRawFunction()->addFnAttr(GENERIC_DECL_IN_CURRENT_PKG_ATTR);
        }
    }

    if (chirFunc.IsLambda()) {
        cgFunc->GetRawFunction()->addFnAttr(FUNC_USED_BY_CLOSURE);
    }
#endif
}

void EmitFunctionIR(CGModule& cgMod, const CHIR::Func& chirFunc)
{
    IRGenerator<FunctionGeneratorImpl>(cgMod, chirFunc).EmitIR();
}

void EmitFunctionIR(CGModule& cgMod, const std::vector<CHIR::Func*>& chirFuncs)
{
    for (auto chirFunc : chirFuncs) {
        EmitFunctionIR(cgMod, *chirFunc);
    }
}

void EmitImportedCFuncIR(CGModule& cgMod, const std::vector<CHIR::ImportedFunc*>& importedCFuncs)
{
    IRBuilder2 builder(cgMod);
    for (auto importedCFunc : importedCFuncs) {
        auto chirType = importedCFunc->GetType();
        CJC_ASSERT(chirType->IsFunc() && StaticCast<CHIR::FuncType*>(chirType)->IsCFunc());
        auto chirFuncTy = StaticCast<CHIR::FuncType*>(chirType);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        auto llvmFuncTy = cgMod.GetCGCFFI().GetCFuncType(*chirFuncTy);
#endif
        CJC_NULLPTR_CHECK(llvmFuncTy);

        auto srcPkgName = importedCFunc->GetSourcePackageName();
        const auto& funcPkgName = srcPkgName.empty() ? cgMod.GetCGContext().GetCurrentPkgName() : srcPkgName;
        // Case 1: the CFunc is an implicitly imported one, we need to use its wrapper func when calling it.
        if (funcPkgName != cgMod.GetCGContext().GetCurrentPkgName()) {
            continue;
        }

        // Case 2: the CFunc is a foreign function.
        auto cgFunc = cgMod.GetOrInsertCGFunction(importedCFunc);
        BuildCFunc(cgMod, *importedCFunc, *cgFunc, builder);

        // Adaption cffi wrapper name for incremental compilation
        // If the package of an imported variable is the current package,
        // the imported variable is from the previous compilation product.
        if (cgMod.GetCGContext().GetCompileOptions().enIncrementalCompilation &&
            IsNonPublicCFunc(*chirFuncTy, *importedCFunc) && importedCFunc->TestAttr(CHIR::Attribute::NON_RECOMPILE)) {
            cgFunc->GetRawFunction()->addFnAttr(CodeGen::INCREMENTAL_CFUNC_ATTR);
            cgFunc->GetRawFunction()->addFnAttr(CodeGen::INTERNAL_CFUNC_ATTR);
        }

        SetSRetAttrForStructReturnType(*chirFuncTy, *cgFunc->GetRawFunction());
    }
}
} // namespace CodeGen
} // namespace Cangjie
