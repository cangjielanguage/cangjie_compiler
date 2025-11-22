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
    BasicBlockGeneratorImpl(CGModule& cgMod, const CHIR::Block& entryBB)
        : cgMod(cgMod),
          entryBB(entryBB),
          functionToEmitIR(cgMod.GetOrInsertCGFunction(entryBB.GetTopLevelFunc())->GetRawFunction())
    {
    }

    void EmitIR() override;

private:
    void CreateBasicBlocks(const CHIR::Block& chirBB);
    void CreateBasicBlocksIRs(const CHIR::Block& chirBB);
    void CreateLandingPad(const CHIR::Block& chirBB) const;

private:
    CGModule& cgMod;
    const CHIR::Block& entryBB;
    llvm::Function* functionToEmitIR;
    // This DFS auxiliary set specifically addresses cycle detection and handling within Block structures.
    std::set<const CHIR::Block*> auxSet;
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
    // Emit all basicBlock to function.
    auxSet.clear();
    CreateBasicBlocks(entryBB);

    // Emit expressions for each basicBlock.
    auxSet.clear();
    CreateBasicBlocksIRs(entryBB);
}

void BasicBlockGeneratorImpl::CreateBasicBlocks(const CHIR::Block& chirBB)
{
    if (auxSet.find(&chirBB) != auxSet.end()) {
        return;
    }

    if (!cgMod.GetMappedBB(&chirBB)) {
        auto bbName = PREFIX_FOR_BB_NAME + chirBB.GetIdentifierWithoutPrefix();
        auto bb = llvm::BasicBlock::Create(cgMod.GetLLVMContext(), bbName, functionToEmitIR);
        cgMod.SetOrUpdateMappedBB(&chirBB, bb);
    }
    CreateLandingPad(chirBB);
    auxSet.emplace(&chirBB);

    for (auto succChirBB : chirBB.GetSuccessors()) {
        CreateBasicBlocks(*succChirBB);
    }
}

void BasicBlockGeneratorImpl::CreateBasicBlocksIRs(const CHIR::Block& chirBB)
{
    if (auxSet.find(&chirBB) != auxSet.end()) {
        return;
    }

    CJC_ASSERT(cgMod.GetMappedBB(&chirBB));
    EmitExpressionIR(cgMod, chirBB.GetExpressions());
    auxSet.emplace(&chirBB);

    for (auto succChirBB : chirBB.GetSuccessors()) {
        CreateBasicBlocksIRs(*succChirBB);
    }
}

void BasicBlockGeneratorImpl::CreateLandingPad(const CHIR::Block& chirBB) const
{
    if (!chirBB.IsLandingPadBlock()) {
        return;
    }

    IRBuilder2 irBuilder(cgMod);
    CodeGenBlockScope codeGenBlockScope(irBuilder, chirBB);
    auto landingPad = irBuilder.CreateLandingPad(CGType::GetLandingPadType(cgMod.GetLLVMContext()), 0);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (chirBB.GetExceptions().empty()) {
        landingPad->addClause(llvm::Constant::getNullValue(irBuilder.getInt8PtrTy()));
    } else {
        for (auto exceptClass : chirBB.GetExceptions()) {
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
