// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "EntrySelection.h"

using namespace Cangjie::AST;
namespace Cangjie::Native::FFI::ArkTS {
// Before: a FuncDecl carrying semantic attributes. After result: true for a
// source ArkTS entry, excluding imported, foreign and already-lowered wrappers.
// This only selects callback generation; the declaration/body are unchanged.
bool IsArkTSEntry(const FuncDecl& fd)
{
    return fd.TestAttr(Attribute::ARKTS) && !fd.TestAnyAttr(
        Attribute::IMPORTED, Attribute::FOREIGN, Attribute::ARKTS_FUNCTION_WRAPPER);
}

// Before: a source FuncDecl with annotations and attributes. After result: true
// for an @ArkTS + FOREIGN declaration, before PrepareTypeCheck replaces foreign
// ABI flags with ARKTS_FUNCTION_WRAPPER. This predicate itself changes no AST.
bool IsArkTSForeignFunction(const FuncDecl& fd)
{
    return fd.HasAnno(AnnotationKind::ARKTS) && fd.TestAttr(Attribute::FOREIGN);
}

} // namespace Cangjie::Native::FFI::ArkTS
