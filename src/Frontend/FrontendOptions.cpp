// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the FrontendOptions.
 */

#include "cangjie/Frontend/FrontendOptions.h"

#define OPTION_TRUE_ACTION(EXPR) [](FrontendOptions& opts, OptionArgInstance&) { (EXPR); return true; }

using namespace Cangjie;

namespace {
std::unordered_map<Options::ID, std::function<bool(FrontendOptions&, OptionArgInstance&)>> g_actions = {
    {Options::ID::DUMP_TOKENS, OPTION_TRUE_ACTION(opts.dumpAction = FrontendOptions::DumpAction::DUMP_TOKENS)},
    {Options::ID::DUMP_SYMBOLS, OPTION_TRUE_ACTION(opts.dumpAction = FrontendOptions::DumpAction::DUMP_SYMBOLS)},
    {Options::ID::TYPE_CHECK, OPTION_TRUE_ACTION(opts.dumpAction = FrontendOptions::DumpAction::TYPE_CHECK)},
    // DUMP_DEPENDENT_PACKAGE (--scan-dependency option) should be handled with extra care. The behavior of
    // the option has defined in GlobalOptions. Since, we are overriding its behavior here, we need to
    // set `scanDepPkg` field again.
    {Options::ID::DUMP_DEPENDENT_PACKAGE,
        [](FrontendOptions& opts, OptionArgInstance& /* arg */) {
            opts.scanDepPkg = true;
            opts.dumpAction = FrontendOptions::DumpAction::DUMP_DEP_PKG;
            return true;
        }},
    {Options::ID::DESERIALIZE_CHIR_AND_DUMP,
        [](FrontendOptions& opts, [[maybe_unused]] const OptionArgInstance& arg) {
            opts.chirDeserialize = true;
            opts.dumpCHIR = true;
            opts.chirDeserializePath = arg.value;
            opts.dumpAction = FrontendOptions::DumpAction::DESERIALIZE_CHIR;
            return true;
        }},
    {Options::ID::COMPILE_CJD, OPTION_TRUE_ACTION(opts.compileCjd = true)},
};
}

std::optional<bool> FrontendOptions::ParseOption(OptionArgInstance& arg)
{
    if (g_actions.find(arg.info.GetID()) == g_actions.end()) {
        return GlobalOptions::ParseOption(arg);
    }
    return {g_actions[arg.info.GetID()](*this, arg)};
}
