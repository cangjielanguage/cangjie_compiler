// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "JavaDesugarManager.h"
#include "JavaInteropManager.h"
#include "GenerateJavaImplApiStub.h"
#include "GenerateInJavaImplRegistryCompanion.h"
#include "DesugarJavaImplSuperConstructorCall.h"
#include "DesugarJavaImplSuperMethodCall.h"
#include "GenerateInJavaImplReferenceWrapper.h"
#include "NativeFFI/Java/AfterTypeCheck/AfterTypeCheckContext.h"
#include "NativeFFI/Java/AfterTypeCheck/GenerateJavaMirrorApiStub.h"
#include "NativeFFI/Java/AfterTypeCheck/GenerateJavaReferenceFieldInJObject.h"
#include "NativeFFI/Java/AfterTypeCheck/PopulateJavaMirrorStubs.h"
#include "RewriteJavaImplReferenceWrapperFields.h"
#include "GenerateNativeBridgeForJavaImpl.h"
#include "DesugarTypeCheckingAndCasting.h"
#include "DesugarJArray.h"
#include "NativeFFI/Java/AfterTypeCheck/InteropLibBridge.h"

namespace Cangjie::Interop::Java {

void JavaDesugarManager::ProcessJavaMirrorImplStages(AfterTypeCheckContext& ctx,
    std::function<void(AST::Node&)> desugarPropRef)
{
    Process<GenerateJavaReferenceFieldInJObject>(ctx, lib);
    Process<GenerateJavaMirrorApiStub>(ctx, typeManager, lib, utils);
    Process<GenerateJavaImplApiStub>(ctx, typeManager, lib, jniBridge);

    for (auto& file : ctx.pkg.files) {
        GenerateInMirrors(ctx, *file);
    }

    Process<GenerateInJavaImplRegistryCompanion>(ctx, typeManager, lib);
    Process<DesugarJavaImplSuperConstructorCall>(ctx, typeManager, lib, jniBridge, diag, utils);
    Process<GenerateInJavaImplReferenceWrapper>(ctx, typeManager, lib, factory);
    Process<DesugarJavaImplSuperMethodCall>(ctx, lib, factory);
    Process<RewriteJavaImplReferenceWrapperFields>(ctx, typeManager, utils, desugarPropRef);
    Process<GenerateNativeBridgeForJavaImpl>(ctx, typeManager, importManager, lib, jniBridge);

    Process<PopulateJavaMirrorStubs>(ctx, typeManager, lib, factory);

    GenerateJavaSourceCode(ctx);
}

void JavaDesugarManager::ProcessCallSiteDesugarStages(AfterTypeCheckContext& ctx)
{
    Process<DesugarJArray>(ctx, typeManager, importManager, lib);
    Process<DesugarTypeCheckingAndCasting>(ctx, lib, diag, utils);
}

void JavaInteropManager::DesugarPackage(Package& pkg, std::function<void(AST::Node&)> desugarPropRef)
{
    // if interoplib is accessible, there could be some mirror/impl declarations imported
    if (!InteropLibBridge::IsInteropLibAccessible(importManager)) {
        if (hasMirrorOrImpl) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_java_mirror_interoplib_must_be_imported, DEFAULT_POSITION);
        }
        return;
    }

    JavaDesugarManager desugarer{
        importManager, typeManager, diag, mangler, javagenOutputPath, outputPath};

    AfterTypeCheckContext ctx{importManager, typeManager, pkg, hasMirrorOrImpl, this->target};
    if (hasMirrorOrImpl) {
        desugarer.ProcessJavaMirrorImplStages(ctx, desugarPropRef);
    }
    desugarer.ProcessCallSiteDesugarStages(ctx);
}

} // namespace Cangjie::Interop::Java
