// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements codegen for erasing useless IRs.
 */

#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "CGModule.h"
#include "IRBuilder.h"
#include "cangjie/Utils/ProfileRecorder.h"

using namespace Cangjie;
using namespace CodeGen;

namespace {
const std::string MUST_RETAINED_VAR = "MustBeRetained"; // Indicates that a variable must be retained.

inline bool HasNoUse(const llvm::Value& value)
{
    return !value.hasNUsesOrMore(1);
}

void TryPropagateLocalConstantVars(llvm::Module& module)
{
    // For the constants with local linkage type, they can be propagated
    // and then able to be eliminated in release compilation.
    for (auto& gv : module.globals()) {
        if (llvm::GlobalObject::isLocalLinkage(gv.getLinkage()) && gv.hasInitializer() && gv.isConstant()) {
            auto it = gv.user_begin();
            while (it != gv.user_end()) {
                auto loadI = llvm::dyn_cast<llvm::LoadInst>(*it);
                ++it; // to avoid iterator failure, let it self-increase first.
                if (loadI) {
                    loadI->replaceAllUsesWith(gv.getInitializer());
                    (void)loadI->eraseFromParent();
                }
            }
        }
    }
}

std::vector<llvm::Instruction*> CollectInstructions(
    llvm::BasicBlock* block, const std::function<bool(const llvm::Instruction&)>& condition = nullptr)
{
    std::vector<llvm::Instruction*> collectedInsts;
    for (auto& inst : block->getInstList()) {
        if (!condition || condition(inst)) {
            collectedInsts.emplace_back(&inst);
        }
    }
    return collectedInsts;
}

std::vector<llvm::Instruction*> CollectInstructions(
    llvm::Function* function, const std::function<bool(const llvm::Instruction&)>& condition = nullptr)
{
    std::vector<llvm::Instruction*> collectedInsts;
    for (auto& block : function->getBasicBlockList()) {
        auto bbRes = CollectInstructions(&block, condition);
        collectedInsts.insert(collectedInsts.end(), bbRes.begin(), bbRes.end());
    }
    return collectedInsts;
}

void EraseUselessLoad(llvm::Function* function)
{
    // Erase unused load instruction from function blocks.
    auto isUnusedLoad = [](const llvm::Instruction& inst) { return llvm::isa<llvm::LoadInst>(inst) && HasNoUse(inst); };
    std::vector<llvm::Instruction*> insts = CollectInstructions(function, isUnusedLoad);
    std::for_each(insts.begin(), insts.end(), [](auto inst) { inst->eraseFromParent(); });
}

void EraseTmpMetadata(llvm::Function* function)
{
    std::vector<llvm::Instruction*> insts = CollectInstructions(function);
    std::for_each(insts.begin(), insts.end(), [](const auto inst) { inst->setMetadata(MUST_RETAINED_VAR, nullptr); });
}

void EraseUnreachableBBs(llvm::Function* function)
{
    llvm::removeUnreachableBlocks(*function);
}
} // namespace

void CGModule::MergeUselessBBIntoPreds(llvm::Function* function) const
{
    // MergeBlockIntoPredecessor causes function UpdateCallingOrderOfInitFunction to not find filesInitBB
    if (function->getName().find(PKG_GV_INIT_PREFIX)) {
        return;
    }
    auto& tmpBlocks = function->getBasicBlockList();
    std::vector<llvm::BasicBlock*> blocks;
    std::for_each(tmpBlocks.begin(), tmpBlocks.end(), [&blocks](auto& block) { blocks.emplace_back(&block); });
    // The first BB cannot be merged to any basic block, so erase it from `blocks`.
    blocks.erase(blocks.begin());
    auto entryBB = &function->getEntryBlock();
    // If a basic block has only one predecessor and the predecessor has only one successor,
    // merge the basic block with its predecessor.
    // The exception is that we do not want the content of the entry block to be
    // affected. Therefore, when the predecessor is an entry, we skip it.
    for (auto& block : blocks) {
        // We would like to save the basic blocks which are used to specify the `start` and `end`
        // of each global variable.
        if (block->getName().equals("init.files")) {
            continue;
        }
        auto uniquePreBB = block->getUniquePredecessor();
        if (!uniquePreBB || uniquePreBB == entryBB) {
            continue;
        }
        // If the terminator of predecessor has debug location,
        // we skip it with '-g' or '--coverage' option to guarantee proper procedural behavior.
        if (GetCGContext().GetCompileOptions().enableCompileDebug ||
            GetCGContext().GetCompileOptions().enableCoverage) {
            auto terminator = uniquePreBB->getTerminator();
            CJC_NULLPTR_CHECK(terminator);
            if (terminator->getDebugLoc()) {
                continue;
            }
        }
        if (uniquePreBB->getUniqueSuccessor()) {
            llvm::MergeBlockIntoPredecessor(block);
        }
    }
}

namespace Cangjie::CodeGen {
void EraseUselessAlloca(llvm::Function* function)
{
    auto isUnusedAlloca = [](const llvm::Instruction& inst) {
        return llvm::isa<llvm::AllocaInst>(inst) && HasNoUse(inst);
    };
    std::vector<llvm::Instruction*> unusedAllocations = CollectInstructions(&function->getEntryBlock(), isUnusedAlloca);
    std::for_each(unusedAllocations.begin(), unusedAllocations.end(), [](auto inst) { inst->eraseFromParent(); });
}
} // namespace Cangjie::CodeGen

bool CGModule::CheckUnusedGV(const llvm::GlobalVariable* var, const std::set<std::string>& llvmUsed)
{
    return var->user_empty() && !cgCtx->IsGlobalsOfCompileUnit(var->getName().str()) &&
        !IsLinkNameUsedInMeta(var->getName().str()) && !llvmUsed.count(var->getName().str());
}

void CGModule::EraseUnusedGVs(const std::function<bool(const llvm::GlobalObject&)> extraCond)
{
    auto& gvList = module->getGlobalList();
    std::vector<llvm::GlobalVariable*> globals;
    std::for_each(gvList.begin(), gvList.end(), [&globals](auto& var) { globals.emplace_back(&var); });
    auto& llvmUsed = GetCGContext().GetLLVMUsedVars();
    for (auto var : globals) {
        var->removeDeadConstantUsers();
        bool unused = CheckUnusedGV(var, llvmUsed);
        if (unused && extraCond && extraCond(*var)) {
            var->eraseFromParent();
        }
    }
}

void CGModule::EraseUnusedFuncs(const std::function<bool(const llvm::GlobalObject&)> extraCond)
{
    std::vector<llvm::Function*> funcs;
    std::vector<llvm::GlobalVariable*> globals;
    auto& funcList = module->getFunctionList();
    std::for_each(funcList.begin(), funcList.end(), [&funcs](auto& func) { funcs.emplace_back(&func); });
    auto& gvList = module->getGlobalList();
    std::for_each(gvList.begin(), gvList.end(), [&globals](auto& var) { globals.emplace_back(&var); });
    auto& llvmUsed = GetCGContext().GetLLVMUsedVars();
    bool erased;
    do {
        erased = false;
        for (auto varIt = globals.begin(); varIt != globals.end();) {
            auto var = *varIt;
            var->removeDeadConstantUsers();
            bool unused = CheckUnusedGV(var, llvmUsed);
            if (unused && extraCond && extraCond(*var)) {
                var->eraseFromParent();
                erased = true;
            }
            varIt = unused ? globals.erase(varIt) : varIt + 1;
        }
        for (auto funcIt = funcs.begin(); funcIt != funcs.end();) {
            auto func = *funcIt;
            func->removeDeadConstantUsers();
            bool unused = func->user_empty() && !func->hasAddressTaken() &&
                !IsLinkNameUsedInMeta(func->getName().str()) && !llvmUsed.count(func->getName().str());
            // erase unused functions.
            if (unused && extraCond && extraCond(*func)) {
                func->eraseFromParent();
                erased = true;
            }
            funcIt = unused ? funcs.erase(funcIt) : funcIt + 1;
        }
    } while (erased);
}

void CGModule::EraseUselessInstsAndDeclarations()
{
    ClearLinkNameUsedInMeta();
    auto& funcList = module->getFunctionList();
    std::vector<llvm::Function*> funcs;
    std::for_each(funcList.begin(), funcList.end(), [&funcs](auto& func) { funcs.emplace_back(&func); });
    std::for_each(funcs.begin(), funcs.end(), [this](auto func) {
        // Function which does not have body and users can be erased from module.
        if (!func->isDeclaration() && !func->getName().equals(FOR_KEEPING_SOME_TYPES_FUNC_NAME)) {
            EraseUnreachableBBs(func);
            MergeUselessBBIntoPreds(func);
            EraseUselessLoad(func);
            EraseUselessAlloca(func);
            EraseTmpMetadata(func);
        }
    });

    auto cond = [](const llvm::GlobalObject& gvOrFunc) { return gvOrFunc.isDeclaration(); };
    EraseUnusedGVs(cond);
    EraseUnusedFuncs(cond);

    // Also clear the insertion point, as the previous insertion point
    // may become invalid after erasing some IRs.
    IRBuilder2 irBuilder(*this);
    irBuilder.ClearInsertionPoint();
}

void CGModule::EraseUselessGVAndFunctions()
{
    if (GetCGContext().GetCompileOptions().optimizationLevel >= GlobalOptions::OptimizationLevel::O2 || 
        GetCGContext().GetCompileOptions().enableCoverage) {
        return;
    }
    TryPropagateLocalConstantVars(*module);
    auto cond = [](const llvm::GlobalObject& gvOrFunc) {
        return llvm::Function::isLocalLinkage(gvOrFunc.getLinkage()) || gvOrFunc.isDeclaration();
    };
    ClearLinkNameUsedInMeta();
    EraseUnusedGVs(cond);
    EraseUnusedFuncs(cond);
}
