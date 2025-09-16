// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the DebugLocation in CHIR.
 */

#ifndef CANGJIE_CHIR_DEBUGLOCATION_H
#define CANGJIE_CHIR_DEBUGLOCATION_H

#include <string>
#include <vector>

namespace Cangjie::CHIR {
const std::string INVALID_NAME = ""; // Invalid file name

struct Position {
    unsigned line{0};
    unsigned column{0};

    bool IsLegal() const
    {
        return line != 0 && column != 0;
    }
    bool IsZero() const
    {
        return line == 0 && column == 0;
    }
};

/**
 * @brief A Debug location in source code.
 *
 */
class DebugLocation {
public:
    DebugLocation(const std::string& absPath, unsigned fileID,
        const Position& beginPos, const Position& endPos, const std::vector<int>& scopeInfo = {0})
        : absPath(&absPath), fileID(fileID), beginPos(beginPos), endPos(endPos), scopeInfo(scopeInfo)
    {
    }

    DebugLocation() : absPath(&INVALID_NAME), fileID(0), beginPos({0, 0}), endPos({0, 0})
    {
    }
    ~DebugLocation() = default;

    // ===--------------------------------------------------------------------===//
    // Position
    // ===--------------------------------------------------------------------===//
    Position GetBeginPos() const;
    void SetBeginPos(const Position& pos);

    Position GetEndPos() const;
    void SetEndPos(const Position& pos);

    bool IsInvalidPos() const;

    bool IsInvalidMacroPos() const;

    // ===--------------------------------------------------------------------===//
    // Scope Info
    // ===--------------------------------------------------------------------===//
    std::vector<int> GetScopeInfo() const;
    void SetScopeInfo(const std::vector<int>& scope);

    // ===--------------------------------------------------------------------===//
    // File Info
    // ===--------------------------------------------------------------------===//
    unsigned GetFileID() const;

    const std::string& GetAbsPath() const;

    std::string GetFileName() const;

    // ===--------------------------------------------------------------------===//
    // Others
    // ===--------------------------------------------------------------------===//
    bool operator==(const DebugLocation& other) const;
    std::string ToString() const;
    void Dump() const;

private:
    const std::string* absPath; /* the absolute path of file */
    unsigned fileID;            /* the file id */
    Position beginPos;          /* the begin position in file, start from 1, 1 */
    Position endPos;            /* the end position in file, start from 1, 1 */
    std::vector<int> scopeInfo; /* scope info, like 0-0-0 0-1 */
};

const DebugLocation INVALID_LOCATION = DebugLocation();
} // namespace Cangjie::CHIR
#endif // CANGJIE_CHIR_DEBUGLOCATION_H
