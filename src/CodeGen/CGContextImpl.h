// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CGCONTEXT_IMPL_H
#define CANGJIE_CGCONTEXT_IMPL_H

#include <unordered_set>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"

#include "Base/CGTypes/CGType.h"
#include "Utils/CGCommonDef.h"
#include "cangjie/CHIR/IR/Type/Type.h"

namespace Cangjie {
namespace CodeGen {

class CGContextImpl {
    friend class CGContext;
    friend class CGType;
    friend class CGTupleType;

public:
    CGContextImpl() = default;
    ~CGContextImpl() = default;
    void Clear();

private:
    std::vector<CGType*> cgTypePool;
    std::unordered_map<CGType::TypeExtraInfo, std::unordered_map<const CHIR::Type*, CGType*>,
        CGType::TypeExtraInfoHasher> chirType2CGTypeMap;
    std::unordered_map<std::string, CGType*> chirTypeName2CGTypeMap;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    std::unordered_map<const llvm::Value*, llvm::Value*> valueAndBasePtrMap;
    std::unordered_set<llvm::Value*> nullableReference; // Record those i8(1)* which are nullable.
#endif
};

} // namespace CodeGen
} // namespace Cangjie
#endif // CANGJIE_CGCONTEXT_IMPL_H
