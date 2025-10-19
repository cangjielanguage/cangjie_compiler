// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the entrypoint to handle Objective-C mirrors and theirs subtypes desugaring.
 */

#include "Desugar.h"
#include "Interop/Handlers.h"

void Cangjie::Interop::ObjC::Desugar(InteropContext&& ctx)
{
    HandlerFactory<InteropContext>::Start<FindMirrors>()
        .Use<CheckMirrorTypes>()
        .Use<CheckImplTypes>()
        .Use<InsertNativeHandleField>()
        .Use<InsertMirrorCtorDecl>()
        .Use<InsertMirrorCtorBody>()
        .Use<InsertFinalizer>()
        .Use<DesugarMirrors>()
        .Use<GenerateObjCImplMembers>()
        .Use<GenerateInitCJObjectMethods>()
        .Use<GenerateDeleteCJObjectMethod>()
        .Use<DesugarImpls>()
        .Use<GenerateWrappers>()
        .Use<GenerateGlueCode>()
        .Use<CheckObjCPointerTypeArguments>()
        .Use<RewriteObjCPointerAccess>()
        .Use<DrainGeneratedDecls>()
        .Handle(ctx);
}
