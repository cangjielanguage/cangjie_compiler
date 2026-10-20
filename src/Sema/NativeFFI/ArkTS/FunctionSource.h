// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_PRIVATE_SEMA_ARKTS_FUNCTIONS_H
#define CANGJIE_PRIVATE_SEMA_ARKTS_FUNCTIONS_H

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "cangjie/AST/Node.h"

// Private Sema implementation surface, also used by compiler unit tests.
namespace Cangjie::Native::FFI::ArkTS {
std::string ArkTSDefaultModule(const std::string& package);
std::string ArkTSScalarName(Ptr<AST::Ty> ty);
bool ArkTSFunctionOptions(const AST::FuncDecl& fd, std::unordered_map<std::string, std::string>& options);
std::string ArkTSForeignLocalPrefix(const std::vector<Ptr<AST::FuncParam>>& params);
std::string ArkTSForeignWrapperSource(const std::vector<std::string>& types, const std::string& resultType,
    size_t namespaceDepth, const std::string& prefix);
} // namespace Cangjie::Native::FFI::ArkTS
#endif
