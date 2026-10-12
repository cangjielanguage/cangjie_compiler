// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "EntrySelection.h"

using namespace Cangjie::AST;
namespace Cangjie::Native::FFI::ArkTS {
bool IsArkTSEntry(const FuncDecl& fd)
{
    return fd.TestAttr(Attribute::ARKTS) && !fd.TestAttr(Attribute::IMPORTED);
}

} // namespace Cangjie::Native::FFI::ArkTS
