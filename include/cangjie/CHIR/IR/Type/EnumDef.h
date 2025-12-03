// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ENUM_H
#define CANGJIE_CHIR_ENUM_H

#include "cangjie/CHIR/IR/Type/CustomTypeDef.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include <string>
#include <vector>

namespace Cangjie::CHIR {
struct EnumCtorInfo {
    std::string name;
    std::string mangledName;
    FuncType* funcType; /**< (AssociatedType_1, ..., AssociatedType_N) -> EnumType */
    AnnoInfo annoInfo;
};

class EnumDef : public CustomTypeDef {
    friend class CustomDefTypeConverter;
public:
    // ===--------------------------------------------------------------------===//
    // Base Infomation
    // ===--------------------------------------------------------------------===//
    EnumType* GetType() const override;
    void SetType(CustomType& ty) override;
    
    /**
    * @brief an enum def like: enum XXX { A | B | ... }, is named NOT exhaustive
    *
    * @return true if enum is exhaustive
    */
    bool IsExhaustive() const;

    std::string ToString() const override;

    void AddCtor(EnumCtorInfo ctor);
    EnumCtorInfo GetCtor(size_t index) const;
    std::vector<EnumCtorInfo> GetCtors() const;
    void SetCtors(const std::vector<EnumCtorInfo>& items);

    /**
    * @brief check if all constructors is trivial
    *
    * @return true if all constructors do not have parameters
    */
    bool IsAllCtorsTrivial() const;

protected:
    void PrintAttrAndTitle(std::stringstream& ss) const override;

private:
    explicit EnumDef(std::string srcCodeIdentifier, std::string identifier, std::string pkgName, bool isNonExhaustive)
        : CustomTypeDef(srcCodeIdentifier, identifier, pkgName, CustomDefKind::TYPE_ENUM),
        nonExhaustive{isNonExhaustive}
    {
    }
    ~EnumDef() override = default;

    void PrintConstructor(std::stringstream& ss) const;
    friend class CHIRContext;
    friend class CHIRBuilder;

    std::vector<EnumCtorInfo> ctors;
    bool nonExhaustive;
};
} // namespace Cangjie::CHIR

#endif