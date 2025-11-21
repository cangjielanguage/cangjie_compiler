// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CGCUSTOMTYPE_H
#define CANGJIE_CGCUSTOMTYPE_H

#include "Base/CGTypes/CGType.h"

namespace Cangjie {
namespace CodeGen {
class CGCustomType : public CGType {
public:
    static std::vector<llvm::Constant*> GenTypeInfoConstantVectorForTypes(
        CGModule& cgMod, const std::vector<CHIR::Type*>& chirTypes);
    static llvm::Constant* GenTypeInfoArray(
        CGModule& cgMod, std::string name, std::vector<llvm::Constant*> constants, const std::string_view& attr);
    static llvm::Constant* GenOffsetsArray(CGModule& cgMod, std::string name, llvm::StructType* layoutType);

protected:
    CGCustomType(
        CGModule& cgMod, CGContext& cgCtx, const CHIR::Type& chirType, CGTypeKind cgTypeKind = CGTypeKind::OTHERS);

    llvm::Constant* GenFieldsNumOfTypeInfo() override;
    llvm::Constant* GenFieldsOfTypeInfo() override;
    llvm::Constant* GenOffsetsOfTypeInfo() override;
    llvm::Constant* GenSourceGenericOfTypeInfo() override;
    llvm::Constant* GenTypeArgsNumOfTypeInfo() override;
    llvm::Constant* GenTypeArgsOfTypeInfo() override;
    llvm::Constant* GenReflectionOfTypeInfo() override;

    llvm::Constant* GenNameOfTypeTemplate();
    llvm::Constant* GenKindOfTypeTemplate();
    llvm::Constant* GenTypeArgsNumOfTypeTemplate();

    bool IsSized() const;

    virtual void PreActionOfGenTypeTemplate() {}
    virtual void PostActionOfGenTypeTemplate() {}
    virtual llvm::Constant* GenFieldsNumOfTypeTemplate();
    virtual llvm::Constant* GenFieldsFnsOfTypeTemplate();
    virtual llvm::Constant* GenSuperFnOfTypeTemplate();
    virtual llvm::Constant* GenFinalizerOfTypeTemplate();

private:
    CGCustomType() = delete;
    void GenTypeTemplate() override;
};
} // namespace CodeGen
} // namespace Cangjie
#endif // CANGJIE_CGCUSTOMTYPE_H