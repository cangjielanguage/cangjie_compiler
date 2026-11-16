// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares Tool class and its functions.
 */

#ifndef CANGJIE_DRIVER_TOOL_H
#define CANGJIE_DRIVER_TOOL_H

#include <future>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Driver/ResponseFilePolicy.h"
#include "cangjie/Driver/ToolFuture.h"
namespace Cangjie {

enum class ToolType { UNKNOWN, FRONTEND, BACKEND, OTHER, INTERNAL_IMPLEMENTED };

class Tool {
public:
    /**
     * @brief The constructor of class Tool.
     *
     * @param name Tool name.
     * @param type Tool type.
     * @param environmentVars Environment for tool execution.
     * @return Tool The instance of Tool.
     */
    Tool(const std::string& name, ToolType type, const std::unordered_map<std::string, std::string>& environmentVars)
        : type(type), name(name), environmentVars(environmentVars)
    {
    }

    /**
     * @brief The destructor of class Tool.
     */
    ~Tool() = default;

    const ToolType type{ToolType::UNKNOWN}; /**< Tool type. */
    const std::string name;                 /**< Tool name, search from path of operation system. */
    const std::unordered_map<std::string, std::string> environmentVars; /**< Environment for tool execution. */

    /**
     * @brief Get arguments.
     *
     * @return std::vector<std::string>& A list of appended arguments.
     */
    const std::vector<std::string>& GetArgs() const
    {
        return args;
    }

    /**
     * @brief Get full arguments.
     *
     * @return std::vector<std::string> A list of full arguments.
     */
    const std::vector<std::string> GetFullArgs() const
    {
        std::vector<std::string> fullArgs{};
        fullArgs.emplace_back(name);
        fullArgs.insert(fullArgs.end(), args.begin(), args.end());
        return fullArgs;
    }

    /**
     * @brief Append an argument to the argument list.
     *
     * @param arg The argument to append.
     */
    void AppendArg(const std::string& arg)
    {
        this->args.emplace_back(arg);
        this->argKinds.push_back(ArgKind::Literal);
    }

    /**
     * @brief Append an argument to the argument list if the condition is true.
     *
     * @param condition If condition is true, argument is appended, otherwise nothing happens.
     * @param argument The argument to append.
     */
    template<typename String>
    void AppendArgIf(bool condition, String&& argument)
    {
        if (condition) {
            this->args.emplace_back(std::forward<String>(argument));
            this->argKinds.push_back(ArgKind::Literal);
        }
    }

    /**
     * @brief Append arguments by using AppendArg(arg).
     *
     * @param arg The first argument to append.
     * @param margs The remaining arguments to append.
     */
    template <typename... Args> void AppendArg(const std::string& arg, Args... margs)
    {
        AppendArg(arg);
        AppendArg(margs...);
    }

    /**
     * @brief Append arguments to the argument list if the condition is true.
     *
     * @param condition If condition is true, arguments are appended, otherwise nothing happens.
     * @param argument The first argument to append.
     * @param arguments The remaining arguments to append.
     */
    template<typename String, typename... Args>
    void AppendArgIf(bool condition, String&& argument, Args&&... arguments)
    {
        if (condition) {
            AppendArg(std::forward<String>(argument));
            AppendArg(std::forward<Args>(arguments)...);
        }
    }

    /**
     * @brief Append a list of arguments by using AppendArg(arg).
     *
     * @param margs The args argument list.
     */
    void AppendArg(const std::vector<std::string>& margs)
    {
        for (const std::string& arg : margs) {
            AppendArg(arg);
        }
    }

    /**
     * @brief Break the input into parts and append the results as arguments for the tool.
     * The input argument will be split into mutiple arguments with spaces. This method
     * could be used to append a sub-command (a list of arguments joined by spaces) to
     * the tool.
     *
     * @param arg The string of arguments to append.
     */
    void AppendMultiArgs(const std::string& argStr);

    /**
     * @brief Append arguments by using AppendMultiArgs(arg).
     *
     * @param arg The first string of arguments to append.
     * @param args The remaining string of arguments to append.
     */
    template <typename... Args> void AppendMultiArgs(const std::string& arg, Args... margs)
    {
        AppendMultiArgs(arg);
        AppendMultiArgs(margs...);
    }

    /**
     * @brief Append a list of arguments by using AppendMultiArgs(arg).
     *
     * @param args The list of string of arguments to append.
     */
    void AppendMultiArgs(const std::vector<std::string>& margs)
    {
        for (const std::string& arg : margs) {
            AppendMultiArgs(arg);
        }
    }

    /**
     * @brief Append all files with the given extension from a directory when the tool is executed.
     *
     * @param dir The directory to scan.
     * @param extension The file extension for collection.
     */
    void AppendArgsFromDirAtExecution(const std::string& dir, const std::string& extension);

    /**
     * @brief Set LD_LIBRARY_PATH.
     *
     * @param newLdLibraryPath The new LD_LIBRARY_PATH to set.
     */
    void SetLdLibraryPath(const std::string& newLdLibraryPath)
    {
        this->ldLibraryPath = newLdLibraryPath;
    }

    /**
     * @brief Get LD_LIBRARY_PATH.
     */
    std::string GetLdLibraryPath() const
    {
        return ldLibraryPath;
    }

    /**
     * @brief Execute the run method with a fully-resolved argument vector.
     *
     * `execArgs` must contain the program name at index 0 followed by the
     * final arguments (which may be a single `@<rsp-path>` entry when the
     * caller decided to fall back to a response file). INTERNAL_IMPLEMENTED
     * tools ignore `execArgs` and use their own `GetFullArgs()`.
     *
     * @param execArgs The full argument vector including the program name.
     * @return std::unique_ptr<ToolFuture> The pointer of ToolFuture.
     */
    std::unique_ptr<ToolFuture> Run(const std::vector<std::string>& execArgs) const;

    /**
     * @brief Get name.
     *
     * @return std::string The name.
     */
    std::string GetName() const
    {
        return name;
    }

    /**
     * @brief Get command string.
     *
     * @return std::string The command string.
     */
    const std::string GetCommandString() const
    {
        return GenerateCommand();
    }

    /**
     * @brief The execute method.
     *
     * @return std::unique_ptr<ToolFuture> The pointer of ToolFuture.
     */
    std::unique_ptr<ToolFuture> Execute(bool verbose) const;

private:
    enum class ArgKind : uint8_t { Literal, DeferredDir };

    struct DeferredDirArgs {
        std::string dir;
        std::string extension;
    };

    std::vector<std::string> args{};       /**< Arguments for this tool. */
    // Parallel to args: argKinds[i] is the kind of args[i].
    std::vector<ArgKind> argKinds{};
    // Map from arg position → deferredDirArgs index. Only positions with
    // kind == DeferredDir are expanded
    std::unordered_map<size_t, size_t> deferredArgMap{};
    std::vector<DeferredDirArgs> deferredDirArgs{};
    std::string ldLibraryPath{""};       /**< LD_LIBRARY_PATH */

    std::string GenerateCommand() const;
    std::string GenerateCommandFromExecArgs(const std::vector<std::string>& execArgs) const;
    std::vector<std::string> ExpandArgs() const;

#ifndef _WIN32
    std::list<std::string> BuildEnvironmentVector() const;
#endif
    bool InternalImplementedCommandExec() const;
};

enum class ToolID {
#define TOOL(ID, TYPE, NAME) ID,
#include "cangjie/Driver/Tools.inc"
#undef TOOL
};

struct ToolInfo {
    const std::string name;             /**< The external binary tools name. */

    /**
     * @brief The constructor of class ToolInfo.
     *
     * @param name The external binary tools name.
     * @return ToolInfo The thread future.
     */
    explicit ToolInfo(std::string name) : name(name)
    {
    }
};

/**
 * @brief Make single tool batch.
 *
 * @param tools The pointer of Tool.
 * @return ToolBatch A list of tool pointer.
 */
using ToolBatch = std::vector<std::unique_ptr<Tool>>;
inline ToolBatch MakeSingleToolBatch(std::unique_ptr<Tool> tools)
{
    ToolBatch result{};
    result.emplace_back(std::move(tools));
    return result;
}

const std::unordered_map<ToolID, ToolInfo> g_toolList = {
#define TOOL(ID, TYPE, NAME) {ToolID::ID, ToolInfo(NAME)},
#include "cangjie/Driver/Tools.inc"
#undef TOOL
};
} // namespace Cangjie

#endif // CANGJIE_DRIVER_TOOL_H
