// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Position, which represents the position in a source file.
 */

#ifndef CANGJIE_BASIC_POSITION_H
#define CANGJIE_BASIC_POSITION_H

#include <cstdint>
#include <string>
#include <cstdint>

namespace Cangjie {
enum class PositionStatus {
    KEEP,   /**< Mark the position is valid and should be kept. */
    IGNORE, /**< Mark the position should be ignored when emitting debug info. */
};

/**
 * A position in a source file. Line and column start at 1 (byte count for column). */
struct Position {
    Position(unsigned int fileID, int line, int column) noexcept : fileID(fileID), line(line), column(column)
    {
    }
    Position(int line, int column) noexcept : line(line), column(column)
    {
    }
    Position() = default;
    
    Position(unsigned int fileID, int line, int column, bool curfile) noexcept
        : fileID(fileID), line(line), column(column), isCurFile(curfile)
    {
    }

    unsigned int fileID = 0;
    int line = 0;
    int column = 0;
    bool isCurFile{false};
    bool operator==(const Position& rhs) const;
    bool operator!=(const Position& rhs) const;
    bool operator<(const Position& rhs) const;
    bool operator<=(const Position& rhs) const;
    bool operator>(const Position& rhs) const;
    bool operator>=(const Position& rhs) const;
    Position operator+(const Position& rhs) const;
    Position& operator+=(const Position& rhs);
    Position operator-(const Position& rhs) const;
    Position& operator-=(const Position& rhs);
    Position operator+(const size_t w) const;
    Position operator-(const size_t w) const;
    std::string ToString() const;
    /**
     * Whether line and column are both zero.
     */
    bool IsZero() const;
    void Mark(PositionStatus newStatus);
    PositionStatus GetStatus() const;

    friend std::ostream& operator<<(std::ostream& out, const Position& pos)
    {
        out << pos.ToString();
        return out;
    }
    inline uint64_t Hash64() const
    {
        return (static_cast<uint64_t>(fileID) << 32u) ^ (static_cast<uint64_t>(line) << 16u) ^
            (static_cast<uint64_t>(column));
    }
    // Hash without fileID for Macro.
    inline uint32_t Hash32() const
    {
        return (static_cast<uint32_t>(line) << 16u) ^ (static_cast<uint32_t>(column));
    }
    // Get the pair<line, column> from the hash value that created by Position.Hash32().
    static std::pair<int, int> RestorePosFromHash(uint32_t hash)
    {
        return std::pair(static_cast<int>(hash >> 16u), static_cast<int>(hash & 0xFFFF));
    }

private:
    PositionStatus status{PositionStatus::KEEP};
};

const Position INVALID_POSITION = Position{0, 0, 0};
const Position BEGIN_POSITION = Position{0, 1, 1};
const Position DEFAULT_POSITION = Position{0, -1, -1};
} // namespace Cangjie

#endif // CANGJIE_BASIC_POSITION_H
