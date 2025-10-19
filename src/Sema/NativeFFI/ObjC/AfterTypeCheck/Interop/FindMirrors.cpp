// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements searching for Objective-C mirror declarations and theirs subtypes.
 */

#include "Handlers.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void FindMirrors::HandleImpl(InteropContext& ctx)
{
    for (auto& file : ctx.pkg.files) {
        for (auto& decl : file->decls) {
            if (auto classLikeDecl = As<ASTKind::CLASS_LIKE_DECL>(decl);
                classLikeDecl && ctx.typeMapper.IsObjCMirror(*classLikeDecl)) {
                ctx.mirrors.emplace_back(classLikeDecl);
            }

            if (auto classDecl = As<ASTKind::CLASS_DECL>(decl); classDecl &&
                (ctx.typeMapper.IsObjCImpl(*classDecl) ||
                    (ctx.typeMapper.IsObjCMirrorSubtype(*classDecl) && !ctx.typeMapper.IsObjCImpl(*classDecl) &&
                        !ctx.typeMapper.IsObjCMirror(*classDecl)))) {
                ctx.impls.emplace_back(classDecl);
            }

            if (auto funcDecl = As<ASTKind::FUNC_DECL>(decl);
                funcDecl && ctx.typeMapper.IsObjCMirror(*funcDecl)) {
                ctx.mirrorTopLevelFuncs.emplace_back(funcDecl);
            }
        }
    }
}