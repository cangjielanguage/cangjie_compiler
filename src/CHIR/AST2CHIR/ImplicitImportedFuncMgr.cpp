// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/ImplicitImportedFuncMgr.h"

using namespace Cangjie;
using namespace CHIR;

ImplicitImportedFuncMgr& ImplicitImportedFuncMgr::Instance() noexcept
{
    static ImplicitImportedFuncMgr instance;
    return instance;
}

void ImplicitImportedFuncMgr::RegImplicitImportedFunc(const ImplicitImportedFunc& func, FuncKind funcKind) noexcept
{
    if (funcKind == FuncKind::GENERIC) {
        implicitImportedGenericFuncs.emplace_back(func);
    } else if (funcKind == FuncKind::NONE_GENERIC) {
        implicitImportedNonGenericFuncs.emplace_back(func);
    } else {
        CJC_ASSERT(false && "Invalid funcKind.");
    }
}

std::vector<ImplicitImportedFunc> ImplicitImportedFuncMgr::GetImplicitImportedFuncs(FuncKind funcKind)
{
    static const auto COMP = [](const ImplicitImportedFunc& lhs, const ImplicitImportedFunc& rhs) {
        return lhs.parentName + lhs.identifier < rhs.parentName + rhs.identifier;
    };

    if (funcKind == FuncKind::GENERIC) {
        sort(implicitImportedGenericFuncs.begin(), implicitImportedGenericFuncs.end(), COMP);
        return implicitImportedGenericFuncs;
    } else if (funcKind == FuncKind::NONE_GENERIC) {
        sort(implicitImportedNonGenericFuncs.begin(), implicitImportedNonGenericFuncs.end(), COMP);
        return implicitImportedNonGenericFuncs;
    } else {
        CJC_ASSERT(false && "Invalid funcKind.");
        return {};
    }
}
