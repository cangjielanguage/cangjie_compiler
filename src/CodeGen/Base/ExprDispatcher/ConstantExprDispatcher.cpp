// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Base/ExprDispatcher/ExprDispatcher.h"

#include "CGModule.h"
#include "DIBuilder.h"
#include "IRBuilder.h"
#include "cangjie/CHIR/Value.h"

namespace Cangjie::CodeGen {
llvm::Value* HandleConstantExpression(IRBuilder2& irBuilder, const CHIR::Constant& chirConst)
{
    return HandleLiteralValue(irBuilder, *chirConst.GetValue());
}

llvm::Value* HandleLiteralValue(IRBuilder2& irBuilder, const CHIR::LiteralValue& chirLiteral)
{
    CGModule& cgMod = irBuilder.GetCGModule();
    llvm::Value* literalValue = nullptr;
    // remove loc for constant instruction.
    auto curLoc = irBuilder.GetInsertFunction() && irBuilder.GetInsertFunction()->getSubprogram()
        ? cgMod.diBuilder->CreateDILoc(irBuilder.GetInsertFunction()->getSubprogram(), {0, 0})
        : llvm::DebugLoc().get();
    irBuilder.SetCurrentDebugLocation(curLoc);
    if (chirLiteral.IsUnitLiteral()) {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        literalValue = cgMod.GenerateUnitTypeValue();
#endif
    } else if (chirLiteral.IsNullLiteral()) {
        literalValue = irBuilder.CreateNullValue(*chirLiteral.GetType());
    } else if (chirLiteral.IsBoolLiteral()) {
        literalValue = irBuilder.getInt1(StaticCast<CHIR::BoolLiteral&>(chirLiteral).GetVal());
    } else if (chirLiteral.IsRuneLiteral()) {
        literalValue = irBuilder.getInt32(StaticCast<CHIR::RuneLiteral&>(chirLiteral).GetVal());
    } else if (chirLiteral.IsIntLiteral()) {
        auto type = CGType::GetOrCreate(cgMod, chirLiteral.GetType())->GetLLVMType();
        auto intConst = StaticCast<CHIR::IntLiteral*>(&chirLiteral);
        if (intConst->IsSigned()) {
            literalValue = llvm::ConstantInt::getSigned(type, intConst->GetSignedVal());
        } else {
            literalValue = llvm::ConstantInt::get(type, intConst->GetUnsignedVal());
        }
    } else if (chirLiteral.IsFloatLiteral()) {
        literalValue = llvm::ConstantFP::get(CGType::GetOrCreate(cgMod, chirLiteral.GetType())->GetLLVMType(),
            StaticCast<CHIR::FloatLiteral&>(chirLiteral).GetVal());
    } else if (chirLiteral.IsStringLiteral()) {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        literalValue = irBuilder.CreateStringLiteral(StaticCast<CHIR::StringLiteral&>(chirLiteral).GetVal());
#endif
    } else {
        CJC_ASSERT(false);
        return nullptr;
    }
    return literalValue;
}

} // namespace Cangjie::CodeGen
