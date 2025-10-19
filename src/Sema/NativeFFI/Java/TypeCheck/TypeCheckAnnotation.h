// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares JavaFFI annotations checks
 */
#ifndef CANGJIE_SEMA_NATIVE_FFI_JAVA_TYPE_CHECK_ANNOTATION_H
#define CANGJIE_SEMA_NATIVE_FFI_JAVA_TYPE_CHECK_ANNOTATION_H

#include "cangjie/AST/Node.h"
#include "cangjie/Basic/DiagnosticEngine.h"

using namespace Cangjie;
using namespace Cangjie::AST;

namespace Cangjie::Interop::Java {

void CheckJavaHasDefaultAnnotation(DiagnosticEngine& diag, const Annotation& ann, const Decl& decl);

} // namespace Cangjie::Interop::Java

#endif // CANGJIE_SEMA_NATIVE_FFI_JAVA_TYPE_CHECK_ANNOTATION_H
