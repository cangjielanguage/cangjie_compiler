// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_SEMA_NATIVE_FFI_OBJ_C_BEFORE_TYPECHECK_DESUGAR
#define CANGJIE_SEMA_NATIVE_FFI_OBJ_C_BEFORE_TYPECHECK_DESUGAR

#include "cangjie/AST/Node.h"

namespace Cangjie::Interop::ObjC {

void PrepareTypeCheck(AST::Package& pkg);

}

#endif // CANGJIE_SEMA_NATIVE_FFI_OBJ_C_BEFORE_TYPECHECK_DESUGAR