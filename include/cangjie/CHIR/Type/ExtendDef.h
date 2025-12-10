// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_EXTENDDEF_H
#define CANGJIE_EXTENDDEF_H

#include "cangjie/CHIR/Type/CustomTypeDef.h"
#include "cangjie/CHIR/Type/Type.h"

namespace Cangjie::CHIR {
class ExtendDef : public CustomTypeDef {
friend class CHIRContext;
friend class CHIRBuilder;
friend class CustomDefTypeConverter;

public:
    // ===--------------------------------------------------------------------===//
    // Base Information
    // ===--------------------------------------------------------------------===//
    Type* GetType() const override;
    void SetType(CustomType& ty) override;
    
    Type* GetExtendedType() const;
    void SetExtendedType(Type& ty);
    
    CustomTypeDef* GetExtendedCustomTypeDef() const;
    virtual std::vector<GenericType*> GetGenericTypeParams() const override;

    // ===--------------------------------------------------------------------===//
    // Super Parent
    // ===--------------------------------------------------------------------===//
    void RemoveParent(ClassType& parent);
    
protected:
    void PrintAttrAndTitle(std::stringstream& ss) const override;
    void PrintComment(std::stringstream& ss) const override;

private:
    explicit ExtendDef(
        const std::string& identifier, const std::string& pkgName, std::vector<GenericType*> genericParams = {});

    Type* extendedType{nullptr};
    std::vector<GenericType*> genericParams;
};
}
#endif