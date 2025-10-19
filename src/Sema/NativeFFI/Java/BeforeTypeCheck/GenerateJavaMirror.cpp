// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "GenerateJavaMirror.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/ASTCasting.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "Utils.h"
#include "TypeCheckUtil.h"
#include "cangjie/AST/Match.h"


namespace Cangjie::Interop::Java {

    void PrepareTypeCheck(Package& pkg, const ImportManager& importManager, TypeManager& typeManager)
    {
        for (auto& file : pkg.files) {
            for (auto& decl : file->decls) {
                if (IsJObject(*decl, pkg.fullPackageName)) {
                    if (auto cd = DynamicCast<ClassDecl*>(decl.get())) {
                        InsertJavaRefGetterStubWithBody(*cd);
                    }
                }
                if (IsMirror(*decl)) {
                    if (auto cd = DynamicCast<ClassDecl*>(decl.get())) {
                        InsertMirrorVarProp(*cd, Attribute::JAVA_MIRROR);
                    } else if (auto id = As<ASTKind::INTERFACE_DECL>(decl.get())) {
                        RemoveAbstractAttributeForJavaHasDefaultMethods(*id);
                        InsertJavaHasDefaultMethodStubs(*id, importManager, typeManager);
                    }
                }
            }
        }
    }
}
