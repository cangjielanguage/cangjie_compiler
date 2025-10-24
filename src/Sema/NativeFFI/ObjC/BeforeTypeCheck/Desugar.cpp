// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Desugar.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/ASTCasting.h"
#include "cangjie/Utils/CastingTemplate.h"

using namespace Cangjie::AST;

namespace Cangjie::Interop::ObjC {
    void PrepareTypeCheck(Package& pkg)
    {
        for (auto& file : pkg.files) {
            for (auto& decl : file->decls) {
                if (decl->TestAttr(Attribute::OBJ_C_MIRROR)) {
                    if (auto cd = DynamicCast<ClassDecl*>(decl.get())) {
                        InsertMirrorVarProp(*cd, Attribute::OBJ_C_MIRROR);
                    }
                }
            }
        }
    }
}
