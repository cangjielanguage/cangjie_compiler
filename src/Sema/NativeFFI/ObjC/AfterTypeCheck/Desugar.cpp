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
        .Use<InsertNativeHandleGetterDecl>()
        .Use<InsertNativeHandleGetterBody>()
        .Use<InsertBaseCtorDecl>()
        .Use<InsertBaseCtorBody>()
        .Use<InsertFinalizer>()
        .Use<DesugarMirrors>()
        .Use<DesugarSyntheticWrappers>()
        .Use<GenerateObjCImplMembers>()
        .Use<GenerateInitCJObjectMethods>(InteropType::ObjC_Mirror)
        .Use<GenerateDeleteCJObjectMethod>(InteropType::ObjC_Mirror)
        .Use<DesugarImpls>()
        .Use<GenerateWrappers>(InteropType::ObjC_Mirror)
        .Use<GenerateGlueCode>(InteropType::ObjC_Mirror)
        .Use<CheckObjCPointerTypeArguments>()
        .Use<RewriteObjCPointerAccess>()
        .Use<DrainGeneratedDecls>()
        .Handle(ctx);

    HandlerFactory<InteropContext>::Start<FindCJMapping>()
        .Use<CheckCJMappingTypes>()
        .Use<GenerateInitCJObjectMethods>(InteropType::CJ_Mapping)
        .Use<GenerateDeleteCJObjectMethod>(InteropType::CJ_Mapping)
        .Use<GenerateWrappers>(InteropType::CJ_Mapping)
        .Use<GenerateGlueCode>(InteropType::CJ_Mapping)
        .Use<CheckObjCPointerTypeArguments>()
        .Use<RewriteObjCPointerAccess>()
        .Use<DrainGeneratedDecls>()
        .Handle(ctx);
}
