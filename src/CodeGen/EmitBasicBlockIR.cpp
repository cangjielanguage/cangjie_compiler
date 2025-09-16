// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "EmitBasicBlockIR.h"

#include <deque>

#include "CGModule.h"
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
#include "CJNative/CJNativeCGCFFI.h"
#endif
#include "EmitExpressionIR.h"
#include "IRBuilder.h"
#include "IRGenerator.h"
#include "Utils/BlockScopeImpl.h"
#include "cangjie/CHIR/Value.h"

namespace Cangjie {
namespace CodeGen {
class BasicBlockGeneratorImpl : public IRGeneratorImpl {
public:
    BasicBlockGeneratorImpl(CGModule& cgMod, const CHIR::Block& chirBB) : cgMod(cgMod)
    {
        worklist.reserve(chirBB.GetParentBlockGroup()->GetBlocks().size());
        (void)worklist.emplace_back(&chirBB);
    }

    void EmitIR() override;

private:
    void CreateLandingPad(const CHIR::Block* block) const;

private:
    CGModule& cgMod;
    std::vector<const CHIR::Block*> worklist;
};

template <> class IRGenerator<BasicBlockGeneratorImpl> : public IRGenerator<> {
public:
    IRGenerator(CGModule& cgMod, const CHIR::Block& chirBB)
        : IRGenerator<>(std::make_unique<BasicBlockGeneratorImpl>(cgMod, chirBB))
    {
    }
};

void BasicBlockGeneratorImpl::EmitIR()
{
    if (worklist.empty()) {
        return;
    }

    auto parentFunc = worklist.at(0)->GetTopLevelFunc();
    auto functionToEmitIR = cgMod.GetOrInsertCGFunction(parentFunc)->GetRawFunction();
    //  Emit all basicBlock to function.
    for (size_t idx = 0; idx < worklist.size(); ++idx) {
        auto currChirBB = worklist.at(idx);
        if (auto bb = cgMod.GetMappedBB(currChirBB); bb == nullptr) {
            bb = llvm::BasicBlock::Create(cgMod.GetLLVMContext(),
                PREFIX_FOR_BB_NAME + currChirBB->GetIdentifierWithoutPrefix(), functionToEmitIR);
            cgMod.SetOrUpdateMappedBB(currChirBB, bb);
        }
        CreateLandingPad(currChirBB);

        for (auto succChirBB : currChirBB->GetSuccessors()) {
            // Some BasicBlock may have back edges.
            if (std::find(worklist.rbegin(), worklist.rend(), succChirBB) != worklist.rend()) {
                continue;
            }
            (void)worklist.emplace_back(succChirBB);
        }
    }
    // Emit expressions for each basicBlock.
    for (auto& idx : worklist) {
        EmitExpressionIR(cgMod, idx->GetExpressions());
    }
}

void BasicBlockGeneratorImpl::CreateLandingPad(const CHIR::Block* block) const
{
    if (!block->IsLandingPadBlock()) {
        return;
    }

    IRBuilder2 irBuilder(cgMod);
    CodeGenBlockScope codeGenBlockScope(irBuilder, *block);
    auto landingPad = irBuilder.CreateLandingPad(CGType::GetLandingPadType(cgMod.GetLLVMContext()), 0);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (block->GetExceptions().empty()) {
        landingPad->addClause(llvm::Constant::getNullValue(irBuilder.getInt8PtrTy()));
    } else {
        for (auto exceptClass : block->GetExceptions()) {
            auto exceptName = exceptClass->GetClassDef()->GetIdentifierWithoutPrefix();
            auto typeInfo = irBuilder.CreateTypeInfo(*exceptClass);
            auto clause = irBuilder.CreateBitCast(typeInfo, irBuilder.getInt8PtrTy());
            landingPad->addClause(static_cast<llvm::Constant*>(clause));
        }
    }
#endif
}

void EmitBasicBlockIR(CGModule& cgMod, const CHIR::Block& chirBB)
{
    IRGenerator<BasicBlockGeneratorImpl>(cgMod, chirBB).EmitIR();
}
} // namespace CodeGen
} // namespace Cangjie
