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
    HandlerFactory<InteropContext>::Start<FindCJMappingInterface>()
        .Use<InsertFwdClasses>()
        .Use<GenerateGlueCode>(InteropType::CJ_Mapping_Interface)
        .Use<DrainGeneratedDecls>()
        .Handle(ctx);

    HandlerFactory<InteropContext>::Start<FindFwdClass>()
        .Use<InsertNativeHandleField>(InteropType::Fwd_Class)
        .Use<InsertNativeHandleGetterDecl>(InteropType::Fwd_Class)
        .Use<InsertNativeHandleGetterBody>(InteropType::Fwd_Class)
        .Use<InsertBaseCtorDecl>(InteropType::Fwd_Class)
        .Use<InsertBaseCtorBody>(InteropType::Fwd_Class)
        .Use<InsertFinalizer>(InteropType::Fwd_Class)
        .Use<DesugarMirrors>(InteropType::Fwd_Class)
        .Use<DrainGeneratedDecls>()
        .Handle(ctx);

    HandlerFactory<InteropContext>::Start<FindCJMapping>()
        .Use<GenerateFwdClass>()
        .Use<GenerateInitCJObjectMethods>(InteropType::CJ_Mapping)
        .Use<GenerateDeleteCJObjectMethod>(InteropType::CJ_Mapping)
        .Use<GenerateWrappers>(InteropType::CJ_Mapping)
        .Use<GenerateGlueCode>(InteropType::CJ_Mapping)
        .Handle(ctx);

    HandlerFactory<InteropContext>::Start<FindMirrors>()
        .Use<CheckMirrorTypes>()
        .Use<CheckImplTypes>()
        .Use<InsertNativeHandleField>(InteropType::ObjC_Mirror)
        .Use<InsertNativeHandleGetterDecl>(InteropType::ObjC_Mirror)
        .Use<InsertNativeHandleGetterBody>(InteropType::ObjC_Mirror)
        .Use<InsertBaseCtorDecl>(InteropType::ObjC_Mirror)
        .Use<InsertBaseCtorBody>(InteropType::ObjC_Mirror)
        .Use<InsertFinalizer>(InteropType::ObjC_Mirror)
        .Use<DesugarMirrors>(InteropType::ObjC_Mirror)
        .Use<DesugarSyntheticWrappers>()
        .Use<GenerateObjCImplMembers>()
        .Use<GenerateInitCJObjectMethods>(InteropType::ObjC_Mirror)
        .Use<GenerateDeleteCJObjectMethod>(InteropType::ObjC_Mirror)
        .Use<DesugarImpls>()
        .Use<GenerateWrappers>(InteropType::ObjC_Mirror)
        .Use<GenerateGlueCode>(InteropType::ObjC_Mirror)
        .Use<CheckObjCPointerTypeArguments>()
        .Use<RewriteObjCPointerAccess>()
        .Use<CheckObjCFuncTypeArguments>()
        .Use<RewriteObjCFuncCall>()
        .Use<DrainGeneratedDecls>()
        .Handle(ctx);
}
