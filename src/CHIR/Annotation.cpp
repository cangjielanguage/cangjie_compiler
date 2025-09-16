// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Annotation.h"
#include <iostream>
#include <sstream>

#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/Value.h"

namespace Cangjie::CHIR {
std::string AnnotationMap::ToString() const
{
    std::stringstream ss;
    ss << loc.ToString();
    for (auto& pair : annotations) {
        auto str = pair.second->ToString();
        if (str.empty()) {
            continue;
        }
        if (ss.str() != "") {
            ss << ", ";
        }
        ss << str;
    }
    return ss.str();
}

std::string SkipCheck::ToString()
{
    switch (kind) {
        case SkipKind::SKIP_DCE_WARNING:
            return "skip: dce warning";
        case SkipKind::SKIP_FORIN_EXIT:
            return "skip: for-in exit";
        case SkipKind::SKIP_VIC:
            return "skip: vic";
        default:
            return "";
    }
}

std::string WrappedRawMethod::ToString()
{
    // WrappedRawMethod may be removed body when removeUnusedImported，do not form it.
    auto wrapMethod = dynamic_cast<Func*>(rawMethod);
    if (wrapMethod != nullptr && !wrapMethod->GetBody()) {
        return "";
    }

    return "wrapped raw method: " + rawMethod->GetIdentifier();
}
} // namespace Cangjie::CHIR
