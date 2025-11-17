// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements searching for CJMapping which is used in Objective-C side declarations.
 */

#include "Handlers.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;

void FindCJMapping::HandleImpl(InteropContext& ctx)
{
    for (auto& file : ctx.pkg.files) {
        for (auto& decl : file->decls) {
            if (ctx.typeMapper.IsObjCCJMapping(*decl)) {
                ctx.cjMappings.emplace_back(decl);
            }
        }
    }
}