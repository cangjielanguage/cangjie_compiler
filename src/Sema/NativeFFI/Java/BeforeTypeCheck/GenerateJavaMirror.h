// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_SEMA_NATIVE_FFI_JAVA_BEFORE_TYPECHECK_GENERATE_JAVA_MIRROR
#define CANGJIE_SEMA_NATIVE_FFI_JAVA_BEFORE_TYPECHECK_GENERATE_JAVA_MIRROR

#include "cangjie/AST/Node.h"
#include "cangjie/Modules/ImportManager.h"


namespace Cangjie::Interop::Java {
using namespace AST;

void PrepareTypeCheck(Package& pkg, const ImportManager& importManager, TypeManager& typeManager);
}

#endif