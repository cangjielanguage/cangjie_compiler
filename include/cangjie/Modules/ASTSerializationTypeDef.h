// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_MODULES_ASTSERIALIZATIONTYPEDEF_H
#define CANGJIE_MODULES_ASTSERIALIZATIONTYPEDEF_H

#include <flatbuffers/flatbuffers.h>

#include "flatbuffers/CachedASTFormat_generated.h"
#include "flatbuffers/CjoFormat_generated.h"

namespace Cangjie {
using FormattedIndex = uint32_t;
using TStringOffset = flatbuffers::Offset<flatbuffers::String>;
using TDeclDepOffset = flatbuffers::Offset<CachedASTFormat::DeclDep>;
using TEffectMapOffset = flatbuffers::Offset<CachedASTFormat::EffectMap>;
using uoffset_t = flatbuffers::uoffset_t;

struct ExportConfig {
    // Whether save initializer of var decl and func body of function.
    bool exportContent{false};
    // Whether write cacahed cjo astData which used for load cached type info in incremental compilation.
    bool exportForIncr{false};
    bool exportForTest{false};
    // Whether save source files with their absolute paths.
    // Used when compiled with the `--coverage` option.
    bool needAbsPath{false};
    bool compileCjd{false};
};
} // namespace Cangjie

#endif