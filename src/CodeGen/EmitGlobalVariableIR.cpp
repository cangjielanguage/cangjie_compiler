// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "EmitGlobalVariableIR.h"

#include "Base/ExprDispatcher/ExprDispatcher.h"
#include "CGModule.h"
#include "DIBuilder.h"
#include "EmitExpressionIR.h"
#include "IRBuilder.h"
#include "IRGenerator.h"
#include "cangjie/CHIR/Value.h"

namespace Cangjie {
namespace CodeGen {
class GlobalVariableGeneratorImpl : public IRGeneratorImpl {
public:
    GlobalVariableGeneratorImpl(CGModule& cgMod, const std::vector<CHIR::GlobalVar*>& chirGVs)
        : cgMod(cgMod), chirGVs(chirGVs)
    {
    }

    void EmitIR() override;

private:
    CGModule& cgMod;
    const std::vector<CHIR::GlobalVar*> chirGVs;
};

template <> class IRGenerator<GlobalVariableGeneratorImpl> : public IRGenerator<> {
public:
    IRGenerator(CGModule& cgMod, const std::vector<CHIR::GlobalVar*>& chirGVs)
        : IRGenerator<>(std::make_unique<GlobalVariableGeneratorImpl>(cgMod, chirGVs))
    {
    }
};

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
void GlobalVariableGeneratorImpl::EmitIR()
{
    IRBuilder2 irBuilder(cgMod);
    std::set<CHIR::GlobalVar*> quickGVs(chirGVs.begin(), chirGVs.end());
    for (auto chirGV : cgMod.GetCGContext().GetCHIRPackage().GetGlobalVars()) {
        auto rawGV = llvm::cast<llvm::GlobalVariable>(cgMod.GetOrInsertGlobalVariable(chirGV)->GetRawValue());
        cgMod.diBuilder->CreateGlobalVar(*chirGV);
        if (quickGVs.find(chirGV) == quickGVs.end()) {
            continue;
        }
        const auto align = cgMod.GetLLVMModule()->getDataLayout().getPrefTypeAlignment(rawGV->getType());
        rawGV->setAlignment(llvm::MaybeAlign(align));
        if (auto literal = chirGV->GetInitializer()) {
            auto literalValue = HandleLiteralValue(irBuilder, *literal);
            if (literal->GetType()->IsString()) {
                cgMod.GetCGContext().AddCJString(
                    rawGV->getName().str(), StaticCast<CHIR::StringLiteral*>(literal)->GetVal());
            } else {
                rawGV->setInitializer(llvm::cast<llvm::Constant>(literalValue));
            }
            if (chirGV->TestAttr(CHIR::Attribute::READONLY)) {
                rawGV->addAttribute(llvm::Attribute::ReadOnly);
                rawGV->setConstant(true);
            }
        } else {
            auto chirType = StaticCast<CHIR::RefType*>(chirGV->GetType())->GetBaseType();
            rawGV->setInitializer(llvm::cast<llvm::Constant>(irBuilder.CreateNullValue(*chirType)));
        }
        if (!chirGV->GetParentCustomTypeDef()) {
            auto fieldMeta = llvm::MDTuple::get(
                cgMod.GetLLVMContext(), {llvm::MDString::get(cgMod.GetLLVMContext(), MangleType(*chirGV->GetType()))});
            rawGV->setMetadata(GC_GLOBAL_VAR_TYPE, fieldMeta);
        }
    }
}
#endif

void EmitGlobalVariableIR(CGModule& cgMod, const std::vector<CHIR::GlobalVar*>& chirGVs)
{
    IRGenerator<GlobalVariableGeneratorImpl>(cgMod, chirGVs).EmitIR();
}
} // namespace CodeGen
} // namespace Cangjie
