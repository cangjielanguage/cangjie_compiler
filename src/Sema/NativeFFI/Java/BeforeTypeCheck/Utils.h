// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares auxiliary methods for interop implementation
 */
#ifndef CANGJIE_SEMA_NATIVE_FFI_JAVA_BEFORE_TYPE_CHECK_UTILS
#define CANGJIE_SEMA_NATIVE_FFI_JAVA_BEFORE_TYPE_CHECK_UTILS

#include "cangjie/AST/Node.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeManager.h"

using namespace Cangjie::AST;

namespace Cangjie::Interop::Java {
void InsertJavaHasDefaultMethodStubs(const InterfaceDecl& id, const ImportManager& importManager, TypeManager& typeManager);
void RemoveAbstractAttributeForJavaHasDefaultMethods(const InterfaceDecl& decl);
ClassDecl& GetExceptionDecl(const ImportManager& importManager);
}

#endif // CANGJIE_SEMA_NATIVE_FFI_JAVA_BEFORE_TYPE_CHECK_UTILS