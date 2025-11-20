// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements generating and inserting a forward class for each CJ-Mapping interface
 */

#include "Handlers.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "NativeFFI/Utils.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;
using namespace Cangjie::Native::FFI;

void InsertFwdClasses::HandleImpl(InteropContext& ctx)
{
    for (auto& interfaceDecl : ctx.cjMappingInterfaces) {
        auto fwdclassDecl = MakeOwned<ClassDecl>();
        fwdclassDecl->identifier = interfaceDecl->identifier.Val() + OBJ_C_FWD_CLASS_SUFFIX;
        fwdclassDecl->identifier.SetPos(interfaceDecl->identifier.Begin(), interfaceDecl->identifier.End());
        fwdclassDecl->fullPackageName = interfaceDecl->fullPackageName;
        fwdclassDecl->moduleName = ::Cangjie::Utils::GetRootPackageName(interfaceDecl->fullPackageName);
        fwdclassDecl->curFile = interfaceDecl->curFile;

        fwdclassDecl->inheritedTypes.emplace_back(CreateRefType(*interfaceDecl));

        fwdclassDecl->ty = ctx.typeManager.GetClassTy(*fwdclassDecl, interfaceDecl->ty->typeArgs);
        auto classLikeTy = DynamicCast<ClassLikeTy*>(interfaceDecl->ty);
        CJC_ASSERT(classLikeTy);
        classLikeTy->directSubtypes.insert(fwdclassDecl->ty);

        fwdclassDecl->EnableAttr(Attribute::PUBLIC, Attribute::COMPILER_ADD, Attribute::CJ_MIRROR_OBJC_INTERFACE_FWD,  Attribute::ABSTRACT);

        fwdclassDecl->body = MakeOwned<ClassBody>();
        for (auto& decl : interfaceDecl->GetMemberDecls()) {
            if (FuncDecl* fd = As<ASTKind::FUNC_DECL>(decl.get());
                fd && !fd->TestAttr(Attribute::CONSTRUCTOR) && !fd->TestAttr(Attribute::STATIC)) {
                auto funcDecl = ASTCloner::Clone(Ptr<FuncDecl>(fd));
                funcDecl->DisableAttr(Attribute::OPEN);
                funcDecl->EnableAttr(Attribute::PUBLIC, Attribute::CJ_MIRROR_JAVA_INTERFACE_FWD);
                funcDecl->outerDecl = Ptr<Decl>(fwdclassDecl);
                fwdclassDecl->body->decls.emplace_back(std::move(funcDecl));
            }
        }

        ctx.genDecls.emplace_back(std::move(fwdclassDecl));
    }
}