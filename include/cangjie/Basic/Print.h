// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares Print related apis.
 */

#ifndef CANGJIE_BASIC_PRINT_H
#define CANGJIE_BASIC_PRINT_H

#include <algorithm>
#include <cstdarg>
#include <iomanip>
#include <iostream>
#include <unordered_map>
#include <utility>

#include "cangjie/Basic/Position.h"
#include "cangjie/Basic/Utils.h"
#include "cangjie/Basic/Color.h"
#include "cangjie/Utils/ICEUtil.h"

namespace Cangjie {
// No color means nothing, reset means reset state.
enum class DiagColor : uint8_t {
    NO_COLOR,
    RESET,
    BLACK,
    RED,
    GREEN,
    YELLOW,
    BLUE,
    MAGENTA,
    CYAN,
    WHITE,
    REVERSE,
};

///@{
/// These functions are used in driver/backend tools error. They do not have code position info and therefore do
/// not fit in DiagnosticEngine. These print functions can handle colored printing on Linux.
/// Errors are printed red, warnings yellow, and info green.
inline void Print() {}
template <typename... Args> inline void Print(Args&&... args)
{
    ((std::cout << args << ' '), ...);
}

inline void Println()
{
    std::cout << std::endl;
}

template <typename Arg> inline void Println(Arg&& arg)
{
    std::cout << std::forward<Arg>(arg) << std::endl;
}

template <typename Arg, typename... Args> inline void Println(Arg&& arg, Args&&... args)
{
    std::cout << std::forward<Arg>(arg);
    ((std::cout << ' ' << std::forward<Args>(args)), ...);
    std::cout << std::endl;
}

inline void PrintNoSplit() {}
template <typename... Args> inline void PrintNoSplit(Args&&... args)
{
    (std::cout << ... << args);
}

inline void PrintIndentOnly(unsigned indent, unsigned numSpaces = 2)
{
    std::cout << std::string(indent * numSpaces, ' ');
}

template <typename... Args> inline void PrintIndent(unsigned indent, const Args... args)
{
    PrintIndentOnly(indent);
    Println(args...);
}

template <typename... Args> inline void Print(std::ostream& stream, Args&&... args)
{
    ((stream << args << ' '), ...);
}

inline void Println(std::ostream& stream)
{
    stream << std::endl;
}

template <typename Arg> inline void Println(std::ostream& stream, Arg&& arg)
{
    stream << std::forward<Arg>(arg) << std::endl;
}

template <typename Arg, typename... Args> inline void Println(std::ostream& stream, Arg&& arg, Args&&... args)
{
    stream << std::forward<Arg>(arg);
    ((stream << ' ' << std::forward<Args>(args)), ...);
    stream << std::endl;
}

template <typename... Args> inline void PrintNoSplit(std::ostream& stream, Args&&... args)
{
    (stream << ... << args);
}

inline void PrintIndentOnly(std::ostream& stream, unsigned indent, unsigned numSpaces = 2)
{
    stream << std::string(indent * numSpaces, ' ');
}

template <typename... Args> inline void PrintIndent(std::ostream& stream, unsigned indent, const Args... args)
{
    PrintIndentOnly(stream, indent);
    Println(stream, args...);
}

template <typename Opt, typename Desc>
inline void PrintCommandDesc(const Opt& option, const Desc& desc, int optionWidth)
{
    std::cout << "  " << std::left << std::setfill(' ') << std::setw(optionWidth) << option << desc << std::endl;
}

const std::string RED_ERROR_MARK = ANSI_COLOR_RED + "error" + ANSI_COLOR_RESET + ": ";
const std::string YELLOW_WARNING_MARK = ANSI_COLOR_YELLOW + "warning" + ANSI_COLOR_RESET + ": ";
const std::string GREEN_INFO_MARK = ANSI_COLOR_GREEN + "info" + ANSI_COLOR_RESET + ": ";
const std::string GREEN_DEBUG_MARK = ANSI_COLOR_GREEN + "debug" + ANSI_COLOR_RESET + ": ";

static const std::unordered_map<DiagColor, std::string> ColorPrintMap = {
    {DiagColor::NO_COLOR, ANSI_NO_COLOR},
    {DiagColor::RESET, ANSI_COLOR_RESET},
    {DiagColor::BLACK, ANSI_COLOR_BLACK},
    {DiagColor::RED, ANSI_COLOR_RED},
    {DiagColor::GREEN, ANSI_COLOR_GREEN},
    {DiagColor::YELLOW, ANSI_COLOR_YELLOW},
    {DiagColor::BLUE, ANSI_COLOR_BLUE},
    {DiagColor::MAGENTA, ANSI_COLOR_MAGENTA},
    {DiagColor::CYAN, ANSI_COLOR_CYAN},
    {DiagColor::WHITE, ANSI_COLOR_WHITE},
    {DiagColor::REVERSE, ANSI_COLOR_WHITE_BACKGROUND_BLACK_FOREGROUND},
};

inline void ErrorWithColor(const DiagColor& color, const std::string& content, bool isBright = false)
{
    if (isBright) {
        std::cerr << ANSI_COLOR_BRIGHT;
    }
    std::cerr << ColorPrintMap.at(color);
    std::cerr << content;
    if (color != DiagColor::NO_COLOR) {
        std::cerr << ANSI_COLOR_RESET;
    }
}

// no format Error print with new line
template <typename... Args> inline void Errorln(Args&&... args) noexcept
{
    std::cerr << RED_ERROR_MARK;
    ((std::cerr << args), ...);
    std::cerr << std::endl;
}

/**
 * Write given args to std error
 */
template <typename... Args> inline void WriteError(Args&&... args)
{
    ((std::cerr << args), ...);
}

// no format Error print without new line
template <typename... Args> inline void Error(Args&&... args)
{
    std::cerr << RED_ERROR_MARK;
    ((std::cerr << args), ...);
}

// format Error print with new line
inline void Errorf(const char* fmt, ...)
{
    std::cerr << RED_ERROR_MARK;
    va_list myargs;
    va_start(myargs, fmt);
    (void)vfprintf(stderr, fmt, myargs);
    va_end(myargs);
    std::cerr << std::flush;
}

template <typename... Args> inline void Warning(Args&&... args)
{
    std::cerr << YELLOW_WARNING_MARK;
    ((std::cerr << args), ...);
}

template <typename... Args> inline void Warningln(Args&&... args)
{
    std::cerr << YELLOW_WARNING_MARK;
    ((std::cerr << args), ...);
    std::cerr << std::endl;
}

inline void Warningf(const char* fmt, ...)
{
    std::cerr << YELLOW_WARNING_MARK;
    va_list myargs;
    va_start(myargs, fmt);
    (void)vfprintf(stderr, fmt, myargs);
    va_end(myargs);
}

template <typename... Args> inline void Info(Args&&... args)
{
    std::cout << GREEN_INFO_MARK;
    ((std::cout << args), ...);
}

template <typename... Args> inline void Infoln(Args&&... args)
{
    std::cout << GREEN_INFO_MARK;
    ((std::cout << args), ...);
    std::cout << std::endl;
}

inline void Infof(const char* fmt, ...)
{
    PrintNoSplit(GREEN_INFO_MARK);
    va_list myargs;
    va_start(myargs, fmt);
    vprintf(fmt, myargs);
    va_end(myargs);
}

inline void Printf(const char* fmt, ...)
{
    va_list myargs;
    va_start(myargs, fmt);
    vprintf(fmt, myargs);
    va_end(myargs);
}

template <typename... Args> inline void Debug([[maybe_unused]] Args&&... args)
{
#ifndef NDEBUG
    PrintNoSplit(GREEN_DEBUG_MARK);
    Print(args...);
#endif
}
 
template <typename... Args> inline void Debugln([[maybe_unused]] Args&&... args)
{
#ifndef NDEBUG
    PrintNoSplit(GREEN_DEBUG_MARK);
    Println(args...);
#endif
}
 
inline void Debugf([[maybe_unused]] const char* fmt, ...)
{
#ifndef NDEBUG
    PrintNoSplit(GREEN_DEBUG_MARK);
    va_list myargs;
    va_start(myargs, fmt);
    vprintf(fmt, myargs);
    va_end(myargs);
#endif
}
///@}
} // namespace Cangjie
#endif // CANGJIE_BASIC_PRINT_H
