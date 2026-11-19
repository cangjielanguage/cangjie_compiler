// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the OptionTable related classes.
 */

#include "cangjie/Option/OptionTable.h"
#include "cangjie/Option/Option.h"

#include <algorithm>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cangjie/Basic/Print.h"

namespace {
#define BACKEND(backend) Cangjie::Options::Backend::backend
#define GROUP(group) Cangjie::Options::Group::group
#define PREDEFVALUE(NAME, VALUE) std::vector<Cangjie::Options::OptionValue> NAME = VALUE
#define VALUE(VALUE, HELPTEXT, BACKENDS, GROUPS)                                                                       \
    Cangjie::Options::OptionValue{std::string(VALUE), std::string(HELPTEXT), BACKENDS, GROUPS}
#include "cangjie/Option/Options.inc"
#undef VALUE
#undef PREDEFVALUE
const std::vector<Cangjie::OptionTable::OptionInfo> INFO_LIST = {
#define OPTION(NAME, id, KIND, BACKENDS, GROUPS, ALIAS, FLAGS, OCCURRENCE, HELPTEXT)                                   \
    {NAME, static_cast<uint16_t>(Cangjie::Options::ID::id), Cangjie::Options::Kind::KIND, BACKENDS, GROUPS, ALIAS,     \
        FLAGS, Cangjie::Options::Occurrence::OCCURRENCE, HELPTEXT},
#include "cangjie/Option/Options.inc"
#undef OPTION
};
#undef GROUP
#undef BACKEND
} // namespace

const std::vector<Cangjie::Options::OptionValue>& Cangjie::Options::GetPredefinedValues(Cangjie::Options::ID id)
{
    static const std::unordered_map<uint16_t, std::vector<Cangjie::Options::OptionValue>> predefinedValues = [] {
        std::unordered_map<uint16_t, std::vector<Cangjie::Options::OptionValue>> values;
        for (const auto& info : INFO_LIST) {
            if (!info.values.empty()) {
                values.emplace(info.id, info.values);
            }
        }
        return values;
    }();
    static const std::vector<Cangjie::Options::OptionValue> empty;
    auto it = predefinedValues.find(static_cast<uint16_t>(id));
    return it != predefinedValues.end() ? it->second : empty;
}

using namespace Cangjie;
using namespace Cangjie::Options;

static bool CheckValueInFlags(const std::string& value, const OptionTable::OptionInfo& info)
{
    auto optionValues = info.GetOptionValues();
    if (!optionValues.empty()) {
        return std::any_of(optionValues.begin(), optionValues.end(), [value](auto item) {
            return item.value == value;
            });
    }
    return true;
}

static void ReportInvalidOptionValue(const OptionTable::OptionInfo& info, const std::string& value)
{
    if (info.GetID() != Options::ID::COMPAT_VERSION) {
        Errorf("invalid value: '%s'\n", value.c_str());
        return;
    }
    std::string supportedVersions;
    for (const auto& version : GlobalOptions::GetSupportedCompatVersions()) {
        if (!supportedVersions.empty()) {
            supportedVersions += ", ";
        }
        supportedVersions += std::to_string(version.major) + "." + std::to_string(version.minor);
    }
    // Note: Please do not modify the error message. The language plugin relies on this message to obtain the
    // compatibility mode whitelist.
    Errorf("invalid compat version, supported compatible versions: [%s]\n", supportedVersions.c_str());
}

std::pair<std::string, std::optional<std::string>> OptionTable::ParseArgumentName(const std::string& argStr) const
{
    // Might be a joined value arg
    size_t pos = argStr.find_first_of('=');
    std::string argName;
    if (pos != std::string::npos) {
        argName = argStr.substr(0, pos);
        return std::make_pair(argName, std::make_optional(argStr.substr(pos + 1)));
    }
    argName = argStr;
    return std::make_pair(argName, std::nullopt);
}

bool OptionTable::ShouldArgumentBeRecognized(
    const std::string& argName, bool hasValue, const OptionTable::OptionInfo& i)
{
#ifdef CANGJIE_VISIBLE_OPTIONS_ONLY
    if (std::count(i.groups.begin(), i.groups.end(), Options::Group::VISIBLE) == 0) {
        return false;
    };
#endif
    if (argName != i.name && argName != (i.alias ? i.alias : "")) {
        return false;
    }
    if (!BelongsTo(enabledBackends, i.backends, Options::Backend::ALL) ||
        !BelongsTo(enabledGroups, i.groups, Options::Group::GLOBAL)) {
        return false;
    }
    if (i.kind == Kind::FLAG_WITH_ARG && hasValue) {
        return true;
    }
    // The option name is recognized but it is not a SEPARATED option (which doesn't match its usage).
    if (i.kind != Kind::SEPARATED && hasValue) {
        return false;
    }
    return true;
}

std::optional<OptionArgInstance> OptionTable::ParseOptionArg(const std::string& argStr)
{
    std::string argName;
    std::optional<std::string> maybeValue;
    std::tie(argName, maybeValue) = ParseArgumentName(argStr);

    // We first iterate entire INFO_LIST and look for an option exactly matching the argument name.
    auto result = std::find_if(INFO_LIST.begin(), INFO_LIST.end(), [&argName, hasValue = maybeValue.has_value(), this]
        (const OptionTable::OptionInfo& i) { return ShouldArgumentBeRecognized(argName, hasValue, i); });

    auto value = maybeValue.value_or("");

    // According to GNU argument syntax conventions, an option and its argument may or may not appear as
    // separate tokens (This rule is only applied to single alphanumeric character options). For example,
    // '-oa.out' should be equivalent to '-o a.out'.
    // Here we iterate INFO_LIST again if there is no exact match. If the prefix of the argument matches
    // any option alias, we will take the argument as the option matched.
    bool isSeparatedOptionWithoutSpace = false;
    if (result == std::end(INFO_LIST)) {
        result = std::find_if(INFO_LIST.begin(), INFO_LIST.end(), [&argStr, this] (const OptionTable::OptionInfo& i) {
#ifdef CANGJIE_VISIBLE_OPTIONS_ONLY
            if (std::count(i.groups.begin(), i.groups.end(), Options::Group::VISIBLE) == 0) {
                return false;
            };
#endif
            auto shouldArgumentBeRecognized = BelongsTo(enabledBackends, i.backends, Options::Backend::ALL) &&
                BelongsTo(enabledGroups, i.groups, Options::Group::GLOBAL);
            return shouldArgumentBeRecognized && ((i.kind == Kind::SEPARATED &&
                i.alias && argStr.find(i.alias, 0) == 0) ||
                (i.kind == Kind::CONTINOUS && argStr.find(i.name, 0) == 0));
        });
        if (result != std::end(INFO_LIST)) {
            isSeparatedOptionWithoutSpace = true;
            // In the case of SEPARATED option which takes this branch, alias must be defined.
            argName = result->kind == Kind::SEPARATED ? result->alias : result->name;
            value = argStr.substr(argName.size());
        }
    }

    if (result == std::end(INFO_LIST)) {
        Errorf("invalid option: '%s'.\n", argName.c_str());
        return std::nullopt;
    }

    OptionArgInstance arg = OptionArgInstance(*result, argName);
    arg.str = argStr;
    arg.value = value;
    arg.hasJoinedValue = maybeValue.has_value();

    if (!frontendMode && arg.info.BelongsGroup(Options::Group::FRONTEND)) {
        Errorf("invalid option: '%s'.\n", argName.c_str());
        return std::nullopt;
    }

    return SetArgValue(arg, value, isSeparatedOptionWithoutSpace);
}

std::optional<OptionArgInstance> OptionTable::SetArgValue(
    OptionArgInstance& arg, const std::string& value, bool isSeparatedOptionWithoutSpace) const
{
    // --lto-keep-pkg-visibility="" is valid (means hide all packages), don't report warning
    if (arg.hasJoinedValue || arg.info.GetKind() == Kind::CONTINOUS) {
        if (value.empty() && arg.info.GetID() != Options::ID::LTO_KEEP_PKG_VISIBILITY) {
            Warningf("option '%s' requires some values, format: option=value or option=\"v1, v2...\"\n",
                arg.name.c_str());
        }
        if (!CheckValueInFlags(value, arg.info)) {
            ReportInvalidOptionValue(arg.info, value);
            return std::nullopt;
        }
        arg.value = value;
    } else if (isSeparatedOptionWithoutSpace) {
        if (!CheckValueInFlags(value, arg.info)) {
            ReportInvalidOptionValue(arg.info, value);
            return std::nullopt;
        }
        arg.value = value;
    }

    bool partialParsed =
        !arg.hasJoinedValue && arg.info.GetKind() == Options::Kind::SEPARATED && !isSeparatedOptionWithoutSpace;
    arg.argType = partialParsed ? ArgType::PartiallyParsed : ArgType::FullyParsed;
    return {arg};
}

bool OptionTable::ParseShortTermArgs(const std::vector<std::string>& argsStrs, size_t& idx,
    ArgList& argList)
{
    auto isSpace = [](bool res, const char c) { return res && isspace(c); };

    const auto& argStr = argsStrs[idx];
    // Ignore empty argument
    if (argStr.empty() || std::accumulate(argStr.begin(), argStr.end(), true, isSpace)) {
        return true;
    }

    // This is an input source.
    if (argStr[0] != '-') {
        argList.args.emplace_back(std::make_unique<InputArgInstance>(argStr));
        return true;
    }

    // If the length of the option is 1, then it must contain a dash only.
    if (argStr.size() <= 1) {
        Errorln("invalid option: '-'.");
        return false;
    }

    std::optional<OptionArgInstance> maybeArg = ParseOptionArg(argStr);
    if (!maybeArg) {
        return false;
    }
    auto arg = maybeArg.value();
    if (arg.argType == ArgType::PartiallyParsed && !ParseSeparatedArgValue(arg, argsStrs, idx)) {
        return false;
    }
    argList.args.emplace_back(std::make_unique<OptionArgInstance>(arg));
    return true;
}

bool OptionTable::ParseSeparatedArgValue(
    OptionArgInstance& arg, const std::vector<std::string>& argsStrs, size_t& idx) const
{
    idx++;
    if (idx >= argsStrs.size()) {
        Errorf("this Option needs a value: '%s'\n", arg.name.c_str());
        return false;
    }
    if (CheckValueInFlags(argsStrs[idx], arg.info)) {
        arg.value = {argsStrs[idx]};
        arg.str = arg.name + " " + argsStrs[idx];
        return true;
    }
    ReportInvalidOptionValue(arg.info, argsStrs[idx]);
    return false;
}

bool OptionTable::ParseArgs(const std::vector<std::string>& argsStrs, ArgList& argList)
{
    if (argsStrs.size() <= 1) {
        return true;
    }

    // The first argument is the tool name, like "cjc".
    size_t idx{1};
    size_t len = argsStrs.size();
    while (idx < len) {
        if (!ParseShortTermArgs(argsStrs, idx, argList)) {
            return false;
        }
        idx++;
    }

    return true;
}

void OptionTable::PrintInfo(const OptionInfo& info, Options::Backend backend, bool showExperimental) const
{
    std::string name(info.name);
    if (info.alias != nullptr) {
        name = std::string(info.alias) + ", " + name;
    }
    if (info.kind == Options::Kind::SEPARATED) {
        name = name + " <value>";
    } else if (info.kind == Options::Kind::CONTINOUS) {
        name = name + "<value>";
    }
    std::string desc(info.help);

    if (std::count(info.groups.begin(), info.groups.end(), Options::Group::STABLE) == 0) {
        desc += " (Experimental)";
    }

    if (name.length() < OPTION_WIDTH) {
        PrintCommandDesc(name, desc, OPTION_WIDTH);
    } else {
        PrintCommandDesc(name, "", OPTION_WIDTH);
        PrintCommandDesc("", desc, OPTION_WIDTH);
    }

    for (const Options::OptionValue& optionValue : info.values) {
#if defined(CANGJIE_VISIBLE_OPTIONS_ONLY)
        if (std::count(optionValue.groups.begin(), optionValue.groups.end(), Options::Group::VISIBLE) == 0) {
            continue;
        }
#endif
        if (std::count(optionValue.backends.begin(), optionValue.backends.end(), Options::Backend::ALL) == 0 &&
            std::count(optionValue.backends.begin(), optionValue.backends.end(), backend) == 0) {
            continue;
        }
        if (std::count(optionValue.groups.begin(), optionValue.groups.end(), Options::Group::STABLE) == 0 &&
            !showExperimental) {
            continue;
        }
        // 2 space indent for values
        std::string value = "  <value>=" + optionValue.value;
        std::string help = "  " + optionValue.help;
        if (std::count(optionValue.groups.begin(), optionValue.groups.end(), Options::Group::STABLE) == 0) {
            help += " (Experimental)";
        }
        PrintCommandDesc(value, help, OPTION_WIDTH);
    }
}

void OptionTable::Usage(Options::Backend backend, std::set<Options::Group> groups, bool showExperimental)
{
    if (optionInfos.empty()) {
        return;
    }

    const std::string usage = "Usage:";
    Println(usage);
    PrintIndentOnly(static_cast<unsigned int>(usage.size()), 1);
    if (groups.find(Options::Group::FRONTEND) != groups.end()) {
        Println("cjc-frontend [option] file...");
    } else {
        Println("cjc [option] file...");
    }
    Println();
    Println("Options:");
    for (const auto& info : optionInfos) {
#if defined(CANGJIE_VISIBLE_OPTIONS_ONLY)
        bool doPrint = std::count(info.groups.begin(), info.groups.end(), Options::Group::VISIBLE) != 0;
#else
        bool doPrint = true;
#endif
        if (doPrint && info.BelongsToAnyOfGroup(groups) && info.BelongsToBackend(backend)) {
            PrintInfo(info, backend, showExperimental);
        }
    }
}

namespace Cangjie {
std::unique_ptr<OptionTable> CreateOptionTable(bool frontendMode)
{
    return std::make_unique<OptionTable>(INFO_LIST, frontendMode);
}
} // namespace Cangjie
