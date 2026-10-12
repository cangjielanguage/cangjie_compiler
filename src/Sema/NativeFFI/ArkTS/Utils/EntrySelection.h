// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_PRIVATE_ARKTS_ENTRY_SELECTION_H
#define CANGJIE_PRIVATE_ARKTS_ENTRY_SELECTION_H

#include "cangjie/AST/Node.h"

// ArkTS-private entry discovery. This file is
// deliberately outside include/: no installed SDK or external-module API.
namespace Cangjie::Native::FFI::ArkTS {
bool IsArkTSEntry(const AST::FuncDecl& fd);
} // namespace Cangjie::Native::FFI::ArkTS
#endif
