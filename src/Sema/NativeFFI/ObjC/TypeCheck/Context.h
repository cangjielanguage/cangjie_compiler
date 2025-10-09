// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares context for typechecking pipeline of Objective-C mirror/subtype declarations.
 */

#ifndef CANGJIE_SEMA_OBJ_C_TYPECHECK_CONTEXT
#define CANGJIE_SEMA_OBJ_C_TYPECHECK_CONTEXT

#include "NativeFFI/ObjC/Utils/TypeMapper.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Basic/DiagnosticEngine.h"

namespace Cangjie::Interop::ObjC {

struct TypeCheckContext {
    explicit TypeCheckContext(AST::ClassLikeDecl& target, DiagnosticEngine& diag, TypeMapper& typeMapper)
        : target(target), diag(diag), typeMapper(typeMapper)
    {
    }

    AST::ClassLikeDecl& target;
    DiagnosticEngine& diag;
    TypeMapper& typeMapper;
};

} // namespace Cangjie::Interop::ObjC

#endif // CANGJIE_SEMA_OBJ_C_TYPECHECK_CONTEXT
