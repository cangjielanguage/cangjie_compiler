// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANNOFACTORYINFO_H
#define CANGJIE_CHIR_ANNOFACTORYINFO_H

#include <sstream>
#include "cangjie/CHIR/Annotation.h"
#include "cangjie/CHIR/Value.h"

namespace Cangjie::CHIR {
/// Each annotation object is translated to a global var for consteval requirements.
/// This records all the translated global variables.
struct AnnoFactoryInfo final : public Annotation {
    AnnoFactoryInfo() : value{} {}
    explicit AnnoFactoryInfo(std::vector<class GlobalVar*> values) : value{std::move(values)} {}
    std::unique_ptr<Annotation> Clone() override
    {
        return std::make_unique<AnnoFactoryInfo>(value);
    }
 
    std::string ToString() override
    {
        std::stringstream gvs;
        gvs << "annoGVs:";
        for (auto v : value) {
            gvs << v->GetIdentifier() << ',';
        }
        return gvs.str();
    }
 
    static const std::vector<class GlobalVar*>& Extract(const AnnoFactoryInfo* label)
    {
        return label->value;
    }
 
private:
    std::vector<class GlobalVar*> value;
};
}
#endif
