// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Dominators.h"

#include "Base/CGTypes/CGFunctionType.h"
#include "CGModule.h"
#include "IRBuilder.h"
#include "Utils/BlockScopeImpl.h"
#include "Utils/CGCommonDef.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/IR/AttributeInfo.h"

namespace Cangjie::CodeGen {
void EmitBasicBlockIR(CGModule& cgMod, const CHIR::Block& chirBB);
void BuildCJFunc(CGModule& cgMod, const CHIR::Func& chirFunc, const CGFunction& cgFunc);

namespace {
llvm::Function* CreateFunctionWrapperForNoBasePtrCases(const CHIR::Value* chirFunc, CGModule& cgMod)
{
    auto func = VirtualCast<CHIR::Func*>(chirFunc);
    CJC_ASSERT(func);
    auto cgFunc = cgMod.GetOrInsertCGFunction(chirFunc, true);
    BuildCJFunc(cgMod, *func, *cgFunc);
    CodeGenUnwindBlockScope unwindBlockScope(cgMod, nullptr);
    EmitBasicBlockIR(cgMod, *func->GetBody()->GetEntryBlock());
    for (auto bb : func->GetBody()->GetBlocks()) {
        cgMod.SetOrUpdateMappedBB(bb, nullptr);
    }
    return cgFunc->GetRawFunction();
}

void CreateFunctionWrapperForNormalCases(
    llvm::Function* function, llvm::Function* wrapperF, const CGFunctionType& cgType, CGModule& cgMod)
{
    wrapperF->setPersonalityFn(cgMod.GetExceptionIntrinsicPersonality());
    SetGCCangjie(wrapperF);

    std::vector<llvm::Value*> args;
    for (size_t i = 0; i < wrapperF->arg_size(); ++i) {
        args.emplace_back(wrapperF->getArg(static_cast<unsigned>(i)));
    }
    auto bb = llvm::BasicBlock::Create(cgMod.GetLLVMContext(), "entry", wrapperF);
    IRBuilder2 builder(cgMod, bb);
    CodeGenUnwindBlockScope unwindBlockScope(cgMod, nullptr);
    if (cgMod.GetCGContext().GetCompileOptions().enableCompileDebug) {
        auto allocasBB = llvm::BasicBlock::Create(cgMod.GetLLVMContext(), "allocas", wrapperF, bb);
        builder.SetInsertPoint(allocasBB);
        builder.CreateBr(bb);
        builder.SetInsertPoint(bb);
    }
    auto p1i8 = builder.getInt8PtrTy(1);
    auto thisValOffset = cgType.HasSRet() ? 1U : 0U;
    auto thisVal = wrapperF->getArg(thisValOffset);
    thisVal->setName("this.withTI");
    auto p1This =
        CGType::GetOrCreate(cgMod, DeRef(cgType.GetParamType(0)->GetOriginal()))->GetLLVMType()->getPointerTo(1U);
    auto dataPtr = builder.CreateBitCast(builder.GetPayloadFromObject(thisVal), p1This);
    if (!cgType.HasBasePtr()) {
        auto ti = builder.GetTypeInfoFromObject(thisVal);
        auto size = builder.GetSizeFromTypeInfo(ti);
        auto paramDerefType = CGType::GetOrCreate(cgMod, DeRef(cgType.GetParamType(0)->GetOriginal()))->GetLLVMType();
        CJC_ASSERT(paramDerefType->isIntegerTy(8U) && "Should not reach here.");
        auto tmp = builder.LLVMIRBuilder2::CreateAlloca(paramDerefType, size);
        builder.CallGCReadAgg({tmp, thisVal, dataPtr, builder.CreateSExt(size, builder.getInt64Ty())});
        dataPtr = tmp;
    }
    args[thisValOffset] = dataPtr;
    if (cgType.HasBasePtr()) { // add basePtr
        args.insert(args.begin() + thisValOffset + 1U, builder.CreateBitCast(thisVal, p1i8));
    }
    auto res = builder.CreateCallOrInvoke(function, args);
    llvm::ReturnInst* retInst =
        function->getReturnType()->isVoidTy() ? builder.CreateRetVoid() : builder.CreateRet(res);
    cgMod.GetCGContext().AddCallBaseToInline(res, retInst);
}

llvm::Function* CreateFunctionWrapper(
    llvm::Function* function, const CGFunctionType* cgType, const CHIR::Value* chirFunc, CGModule& cgMod)
{
    if (function->hasFnAttribute("wrapper")) {
        return function;
    }
    auto& cgCtx = cgMod.GetCGContext();
    if (chirFunc->TestAttr(CHIR::Attribute::STATIC) || chirFunc->Get<CHIR::WrappedRawMethod>()) {
        return nullptr;
    }

    auto outerType = VirtualCast<CHIR::FuncBase*>(chirFunc)->GetParentCustomTypeOrExtendedType();
    if (!outerType || !outerType->IsStruct() || CGType::GetOrCreate(cgMod, outerType)->GetSize()) {
        return nullptr;
    }

    auto wrapperFName = function->getName().str();
    function->setName(wrapperFName + POSTFIX_WITHOUT_TI);
    function->addFnAttr(HAS_WITH_TI_WRAPPER_ATTR);

    if (!cgCtx.GetCompileOptions().enableCompileDebug && !cgType->HasBasePtr() &&
        !cgCtx.IsValueOfOtherLLVMModule(*chirFunc)) {
        return CreateFunctionWrapperForNoBasePtrCases(chirFunc, cgMod);
    }

    auto wrapperFType = function->getFunctionType();
    auto p1This =
        CGType::GetOrCreate(cgMod, DeRef(cgType->GetParamType(0)->GetOriginal()))->GetLLVMType()->getPointerTo(1U);
    if (cgType->HasBasePtr()) {
        std::vector<llvm::Type*> params = function->getFunctionType()->params();
        params.erase(params.begin() + (cgType->HasSRet() ? 2U : 1U));
        wrapperFType = llvm::FunctionType::get(function->getReturnType(), params, function->isVarArg());
    } else {
        std::vector<llvm::Type*> params = function->getFunctionType()->params();
        params[cgType->HasSRet() ? 1U : 0U] = p1This;
        wrapperFType = llvm::FunctionType::get(function->getReturnType(), params, function->isVarArg());
    }

    auto wrapperF =
        llvm::cast<llvm::Function>(cgMod.GetLLVMModule()->getOrInsertFunction(wrapperFName, wrapperFType).getCallee());
    if (cgType->HasSRet()) {
        auto sRetAttr = function->getParamAttribute(0, llvm::Attribute::StructRet);
        wrapperF->addParamAttr(0, sRetAttr);
        wrapperF->addParamAttr(0, llvm::Attribute::NoAlias);
    }
    AddLinkageTypeMetadata(*wrapperF, GetLinkageTypeOfGlobalObject(*function), cgCtx.IsCGParallelEnabled());
    if (!cgCtx.IsValueOfOtherLLVMModule(*chirFunc)) {
        CreateFunctionWrapperForNormalCases(function, wrapperF, *cgType, cgMod);
    }
    return wrapperF;
}
} // namespace

CGFunction::CGFunction(
    llvm::Function* function, const CGFunctionType* cgType, const CHIR::Value* chirFunc, CGModule& cgMod)
    : CGValue(function, cgType), cgMod(cgMod), isStructRet(cgType->HasSRet()), chirFunc(*chirFunc)
{
    auto& cgCtx = cgMod.GetCGContext();
    cgCtx.function2CGFunc.emplace(function, this);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (isStructRet) {
        auto retCGType = cgType->GetContainedTypeAt(0)->GetPointerElementType();
        function->getArg(0)->addAttr(llvm::Attribute::NoAlias);
        auto sret = llvm::Attribute::getWithStructRetType(function->getContext(),
            (!retCGType->GetSize() && !retCGType->GetOriginal().IsGeneric())
                ? llvm::Type::getInt8Ty(cgMod.GetLLVMContext())
                : retCGType->GetLLVMType());
        function->getArg(0)->addAttr(sret);
    }
    if (!cgType->closureParamIndices.empty()) {
        function->setMetadata(
            "ClosureParamIndices", llvm::MDTuple::get(function->getContext(), cgType->closureParamIndices));
    }

    CJC_ASSERT(chirFunc->IsFunc());
    bool isFastNative = VirtualCast<CHIR::FuncBase*>(chirFunc)->IsFastNative();
    if (isFastNative) {
        function->addAttributeAtIndex(static_cast<unsigned>(llvm::AttributeList::FunctionIndex),
            llvm::Attribute::get(function->getContext(), FAST_NATIVE_ATTR));
    } else if (chirFunc->TestAttr(CHIR::Attribute::FOREIGN)) {
        function->addAttributeAtIndex(static_cast<unsigned>(llvm::AttributeList::FunctionIndex),
            llvm::Attribute::get(function->getContext(), CJ2C_ATTR));
    }

    if (cgType->IsCFunc()) {
        return;
    }

    for (auto [structArgIdx, idxInContainedTypes] : std::as_const(cgType->structParamNeedsBasePtr)) {
        auto basePtrArg = function->getArg(static_cast<unsigned>(structArgIdx + 1));
        auto curArg = function->getArg(static_cast<unsigned>(structArgIdx));
        basePtrArg->setName(curArg->getName() + BASEPTR_SUFFIX);
        cgCtx.SetBasePtr(curArg, basePtrArg);
    }
    wrapperF = CreateFunctionWrapper(function, cgType, chirFunc, cgMod);
#endif
}

llvm::Argument* CGFunction::GetArgByIndexFromCHIR(size_t idx) const
{
    CJC_ASSERT(dynamic_cast<const CGFunctionType*>(GetCGType()));
    return GetRawFunction()->getArg(
        static_cast<unsigned>(dynamic_cast<const CGFunctionType*>(GetCGType())->realParamIndices[idx]));
}

void CGFunction::Opt() const
{
    auto function = GetRawFunction();
    RemoveUnreachableBlocks(*function);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    EraseReplaceableAlloca(cgMod, *function);
    AddZeroInitForStructWithRefField(cgMod, *function);
#endif
}

void CGFunction::DumpIR() const
{
    GetRawFunction()->print(llvm::outs(), nullptr);
}

void CGFunction::RemoveUnreachableBlocks(llvm::Function& function)
{
    if (function.isDeclaration()) {
        return;
    }
    llvm::removeUnreachableBlocks(function);
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
namespace {
bool CompareType(llvm::Type* t1, llvm::Type* t2)
{
    if (!t1 || !t2) {
        return t1 > t2;
    }
    if (t1->getTypeID() != t2->getTypeID()) {
        return t1->getTypeID() > t2->getTypeID();
    } else if (t1->isStructTy()) {
        return t1->getStructName() > t2->getStructName();
    } else if (t1->isPointerTy()) {
        return CompareType(GetPointerElementType(t1), GetPointerElementType(t2));
    } else {
        return t1 > t2;
    }
}

struct Compare {
    bool operator()(llvm::Type* t1, llvm::Type* t2) const
    {
        return CompareType(t1, t2);
    }
};

void EraseReplaceableInsts(llvm::Function& function, const std::vector<llvm::Instruction*>& insts)
{
    if (insts.size() <= 1) {
        return;
    }
    llvm::Instruction* specificInst = insts.front();
    auto& entryBB = function.getEntryBlock();
    specificInst->moveBefore(&entryBB.front());

    // Separate replace & erase steps to avoid iterator smashing.
    std::for_each(
        insts.begin() + 1, insts.end(), [&specificInst](auto inst) { inst->replaceAllUsesWith(specificInst); });
    std::for_each(insts.begin() + 1, insts.end(), [](auto inst) { inst->eraseFromParent(); });
    if (HasNoUse(*specificInst)) {
        specificInst->eraseFromParent();
    }
}

llvm::BasicBlock* GetNearestCommonAncestorOfBasicBlocks(const std::set<llvm::BasicBlock*>& bbs)
{
    if (bbs.empty()) {
        return nullptr;
    }
    auto pre = *bbs.begin();
    auto function = pre->getParent();
    llvm::DominatorTree dt(*function);
    for (auto curIter = ++bbs.begin(); curIter != bbs.end(); ++curIter) {
        auto cur = *curIter;
        pre = dt.findNearestCommonDominator(pre, cur);
    }
    return pre;
}
} // namespace

void CGFunction::EraseReplaceableAlloca(const CGModule& cgModule, llvm::Function& function)
{
    if (cgModule.GetCGContext().GetCompileOptions().enableCompileDebug || function.isDeclaration()) {
        return;
    }
    // Erase redundant 'Unit.Type' allocations from function.
    // Erase unused 'Unit.Type' allocations from function.
    // Erase redundant unnamed allocations from function, clustered by type.
    std::map<llvm::Type*, std::vector<llvm::Instruction*>, Compare> unnamedAllocas;
    auto replaceableAlloca = [](const llvm::Instruction& inst) {
        if (!llvm::isa<llvm::AllocaInst>(inst)) {
            return false;
        }
        auto type = inst.getType();
        bool isUnit = IsStructPtrType(type) && GetCodeGenTypeName(GetPointerElementType(type)) == UNIT_TYPE_STR;
        return isUnit;
    };
    for (auto& inst : function.getEntryBlock()) {
        if (replaceableAlloca(inst)) {
            unnamedAllocas[inst.getType()].push_back(&inst);
        }
    }
    std::for_each(unnamedAllocas.begin(), unnamedAllocas.end(),
        [&function](auto it) { EraseReplaceableInsts(function, it.second); });
}

void CGFunction::AddZeroInitForStructWithRefField(CGModule& cgModule, llvm::Function& function)
{
    if (function.isDeclaration()) {
        return;
    }
    // All allocations are at the beginning of the entry block of a function.
    auto entryBB = &function.getEntryBlock();
    // Collect all the allocations for structs that contain ref element(s) directly or indirectly.
    std::vector<llvm::AllocaInst*> structAllocas;
    std::for_each(entryBB->begin(), entryBB->end(), [&](llvm::Instruction& inst) {
        if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(&inst);
            alloca && alloca->getAllocatedType()->isStructTy() && IsTypeContainsRef(alloca->getAllocatedType())) {
            structAllocas.emplace_back(alloca);
        }
    });

    llvm::DominatorTree domTree(function);
    auto loopInfo = llvm::LoopInfoBase<llvm::BasicBlock, llvm::Loop>();
    loopInfo.analyze(domTree);

    IRBuilder2 builder(cgModule);

    // Add the memset-with-zero logic for each collected allocation before it is used for the first time.
    for (auto alloca : structAllocas) {
        std::set<llvm::BasicBlock*> bbs;
        for (auto user : alloca->users()) {
            if (auto tmp = llvm::dyn_cast<llvm::Instruction>(user); tmp) {
                (void)bbs.emplace(tmp->getParent());
            }
        }
        llvm::BasicBlock* nca = bbs.empty() ? alloca->getParent() : GetNearestCommonAncestorOfBasicBlocks(bbs);
        CJC_ASSERT(nca != nullptr);
        llvm::Instruction* insertPt = nca->getTerminator();
        for (auto user : alloca->users()) {
            if (auto tmp = llvm::dyn_cast<llvm::Instruction>(user);
                tmp && tmp->getParent() == nca && tmp->comesBefore(insertPt)) {
                insertPt = tmp;
            }
        }
        CJC_NULLPTR_CHECK(insertPt);
        if (auto loop = loopInfo.getLoopFor(insertPt->getParent()); loop) {
            auto outermostLoop = loop->getOutermostLoop();
            for (auto pred : llvm::predecessors(outermostLoop->getHeader())) {
                if (outermostLoop->contains(pred)) {
                    continue;
                }
                builder.SetInsertPoint(pred->getTerminator());
                (void)builder.CreateCJMemSetStructWith0(alloca);
            }
        } else {
            builder.SetInsertPoint(insertPt);
            (void)builder.CreateCJMemSetStructWith0(alloca);
        }
    }
}
#endif
} // namespace Cangjie::CodeGen
