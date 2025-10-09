// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements draining generated top-level declarations to its files.
 */

#include "Handlers.h"

using namespace Cangjie::Interop::ObjC;

void DrainGeneratedDecls::HandleImpl(InteropContext& ctx)
{
    for (auto pdecl = ctx.genDecls.begin(); pdecl < ctx.genDecls.end(); pdecl++) {
        auto& file = (*pdecl)->curFile;
        file->decls.push_back(std::move(*pdecl));
    }

    ctx.genDecls.clear();
}