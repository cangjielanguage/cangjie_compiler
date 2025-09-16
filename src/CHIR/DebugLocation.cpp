// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the DebugLocation in CHIR.
 */

#include "cangjie/CHIR/DebugLocation.h"

#include <iostream>
#include <sstream>

using namespace Cangjie::CHIR;

Position DebugLocation::GetBeginPos() const
{
    return beginPos;
}

bool DebugLocation::operator==(const DebugLocation& other) const
{
    return beginPos.line == other.beginPos.line && beginPos.column == other.beginPos.column &&
        endPos.line == other.endPos.line && endPos.column == other.endPos.column &&
        fileID == other.fileID;
}

Position DebugLocation::GetEndPos() const
{
    return endPos;
}

void DebugLocation::SetBeginPos(const Position& pos)
{
    beginPos = pos;
}

void DebugLocation::SetEndPos(const Position& pos)
{
    endPos = pos;
}

void DebugLocation::SetScopeInfo(const std::vector<int>& scope)
{
    scopeInfo = scope;
}

/**
 * @brief get the ID of the file.
 */
unsigned DebugLocation::GetFileID() const
{
    return fileID;
}

const std::string& DebugLocation::GetAbsPath() const
{
    return *absPath;
}

std::vector<int> DebugLocation::GetScopeInfo() const
{
    return scopeInfo;
}

bool DebugLocation::IsInvalidPos() const
{
    return beginPos.line == 0 || beginPos.column == 0 || endPos.line == 0 || endPos.column == 0;
}

bool DebugLocation::IsInvalidMacroPos() const
{
    return beginPos.line == 0 || beginPos.column == 0;
}

std::string DebugLocation::GetFileName() const
{
#ifdef _WIN32
    const std::string dirSeparator = "\\/";
#else
    const std::string dirSeparator = "/";
#endif
    auto fileName = absPath->substr(absPath->find_last_of(dirSeparator) + 1);
    return fileName;
}

std::string DebugLocation::ToString() const
{
    if (*this == INVALID_LOCATION) {
        return "";
    }
#ifdef _WIN32
    const std::string dirSeparator = "\\/";
#else
    const std::string dirSeparator = "/";
#endif
    std::stringstream ss;
    std::string name = absPath->substr(absPath->find_last_of(dirSeparator) + 1);
    ss << "loc: \"" << name << "\"-" << beginPos.line << "-" << beginPos.column;
    if (!scopeInfo.empty()) {
        ss << ", scope: " << scopeInfo[0];
    }
    for (size_t t = 1; t < scopeInfo.size(); t++) {
        ss << "-" << scopeInfo[t];
    }
    return ss.str();
}

void DebugLocation::Dump() const
{
    std::cout << ToString() << std::endl;
}
