// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_SEMA_NATIVE_FFI_JAVA_DIAGS
#define CANGJIE_SEMA_NATIVE_FFI_JAVA_DIAGS

#include "cangjie/Basic/DiagnosticEngine.h"

namespace Cangjie::Interop::Java {
using namespace Cangjie;
using namespace AST;

void DiagJavaImplRedefinitionInJava(DiagnosticEngine& diag, const ClassLikeDecl& decl, const ClassLikeDecl& prevDecl);
void DiagJavaMirrorChildMustBeAnnotated(DiagnosticEngine& diag, const ClassLikeDecl& decl);
void DiagJavaDeclCannotInheritPureCangjieType(DiagnosticEngine& diag, ClassLikeDecl& decl);
void DiagJavaDeclCannotBeExtendedWithInterface(DiagnosticEngine& diag, ExtendDecl& decl);
}

#endif // CANGJIE_SEMA_NATIVE_FFI_JAVA_DIAGS