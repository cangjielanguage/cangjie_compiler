// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Desugar/AfterTypeCheck.h"
#include "cangjie/AST/Create.h"

#include "TypeCheckUtil.h"

using namespace Cangjie;
using namespace TypeCheckUtil;

namespace Cangjie::Sema::Desugar::AfterTypeCheck {
OwnedPtr<TypePattern> CreateRuntimePreparedTypePattern(
    TypeManager& typeManager, OwnedPtr<Pattern> pattern, OwnedPtr<Type>  type, Expr& selector)
{
    auto typePattern = CreateTypePattern(std::move(pattern), std::move(type), selector);
    // The ArkTSType implicit-supertype relation is static-only: a runtime
    // check must follow the declarations, so this site opts out of it. An
    // @ArkTS value therefore keeps the ordinary declaration-based `is`/`as`
    // behavior instead of silently matching every case.
    typePattern->matchBeforeRuntime =
        typeManager.IsSubtype(selector.GetTy(), typePattern->GetTy(), true, false, ModalMatchMode::SUBTYPE, false);
    typePattern->needRuntimeTypeCheck =
        !typePattern->matchBeforeRuntime && IsNeedRuntimeCheck(typeManager, selector.DataTy(), typePattern->DataTy());
    return typePattern;
}
} // namespace Cangjie::Sema::Desugar::AfterTypeCheck
