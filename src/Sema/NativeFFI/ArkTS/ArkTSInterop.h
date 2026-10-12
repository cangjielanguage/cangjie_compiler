// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_PRIVATE_NATIVE_FFI_ARKTS_INTEROP_H
#define CANGJIE_PRIVATE_NATIVE_FFI_ARKTS_INTEROP_H

#include "cangjie/AST/Node.h"

namespace Cangjie {
class DiagnosticEngine;
class SourceManager;
namespace Native::FFI::ArkTS {
void PrepareTypeCheck(AST::Package& pkg, DiagnosticEngine& diag, SourceManager& sourceManager);

std::string ArkTSEntryWrapperSource(const std::string& suffix, const std::string& prefix = "__cj_arkts_");
} // namespace Native::FFI::ArkTS
} // namespace Cangjie
#endif
