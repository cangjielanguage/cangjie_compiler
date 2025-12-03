// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_STRUCT_H
#define CANGJIE_CHIR_STRUCT_H

#include "cangjie/CHIR/IR/Type/CustomTypeDef.h"
#include <string>
#include <vector>

namespace Cangjie::CHIR {
class StructDef : public CustomTypeDef {
public:
    // ===--------------------------------------------------------------------===//
    // Base Infomation
    // ===--------------------------------------------------------------------===//
    StructType* GetType() const override;
    void SetType(CustomType& ty) override;

    /**
     * @brief return true if this struct annotated with @C
     */
    bool IsCStruct() const;
    void SetCStruct(bool value);

protected:
    void PrintComment(std::stringstream& ss) const override;
    
private:
    explicit StructDef(std::string srcCodeIdentifier, std::string identifier, std::string pkgName)
        : CustomTypeDef(srcCodeIdentifier, identifier, pkgName, CustomDefKind::TYPE_STRUCT)
    {}
    ~StructDef() override = default;
    friend class CHIRContext;
    friend class CHIRBuilder;

    bool isC = false;
};
} // namespace Cangjie::CHIR

#endif
