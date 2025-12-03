// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the class for determine the memory layout of Class/Interface.
 */

#ifndef CANGJIE_CGENUMTYPELAYOUT_H
#define CANGJIE_CGENUMTYPELAYOUT_H

#include "Base/CGTypes/CGType.h"

#include "Utils/CGCommonDef.h"
#include "cangjie/CHIR/IR/Type/EnumDef.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/CHIR/IR/Value/Value.h"

namespace Cangjie {
namespace CodeGen {
class EnumCtorTIOrTTGenerator {
public:
    explicit EnumCtorTIOrTTGenerator(CGModule& cgMod, const CHIR::EnumType& chirEnumType, std::size_t ctorIndex);

    void Emit();

private:
    void EmitForDynamicGI();
    void EmitForStaticGI();
    void EmitForConcrete();

    void GenerateNonGenericEnumCtorTypeInfo(llvm::GlobalVariable& ti);
    llvm::Constant* GenTypeArgsNumOfTypeInfo();
    llvm::Constant* GenTypeArgsOfTypeInfo();
    llvm::Constant* GenSourceGenericOfTypeInfo();

    void GenerateGenericEnumCtorTypeTemplate(llvm::GlobalVariable& tt);
    llvm::Constant* GenTypeArgsNumOfTypeTemplate();
    llvm::Constant* GenSuperFnOfTypeTemplate(const std::string& funcName);

private:
    CGModule& cgMod;
    CGContext& cgCtx;
    const CHIR::EnumType& chirEnumType;
    std::size_t ctorIndex;
};
} // namespace CodeGen
} // namespace Cangjie

#endif // CANGJIE_CGENUMTYPELAYOUT_H
