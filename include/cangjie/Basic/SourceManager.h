// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the SourceManager related classes, which manages the source files.
 */

#ifndef CANGJIE_BASIC_SOURCEMANAGER_H
#define CANGJIE_BASIC_SOURCEMANAGER_H

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cangjie/Basic/Position.h"
#include "cangjie/Basic/Utils.h"
#include "cangjie/Lex/Token.h"

namespace Cangjie {
/**
 * Source has all information of source code.
 */
struct Source {
    unsigned int fileID = 0;
    std::string path;
    std::string buffer;
    uint64_t fileHash;
    // To differ imported source.
    /*
     * lineOffsets records the offset of the start position of each line in the source code relative to pInputStart.
     * every two adjacent elements in this vector can be seen as a left closed right open interval pair,
     * one pair corresponds to one line
     *
     * e.g.
     * `lineOffsets == {0, 5}` means the source code have two lines,
     * and the first line has five chars (including line terminator), the pair can be abbreviated as [0, 5),
     * 0 is the offset of start of the first line relative to pInputStart,
     * 5 is the offset of start of the second line relative to pInputStart.
     *
     */
    std::vector<size_t> lineOffsets{0}; /**< First offset of each line. */
    std::optional<std::string> packageName = std::nullopt;
    size_t PosToOffset(const Position& pos) const;

    Source(unsigned int fileID, std::string path, std::string buffer, uint64_t fileHash = 0,
        const std::optional<std::string>& packageName = std::nullopt);
    Position GetEndPos() const
    {
        auto sourceSplited = Utils::SplitLines(buffer);
        if (sourceSplited.empty()) {
            return Position{fileID, 1, 1};
        }
        auto sourceLine = static_cast<int>(sourceSplited.size());
        auto column = static_cast<int>(sourceSplited.back().size()) + 1;
        return Position{fileID, sourceLine, column};
    }
    std::unordered_map<size_t, Token> offsetCommentsMap; /**< Offset->Comments map. */
};

/**
 * SourceManager manage all source files.
 */
class SourceManager {
private:
    std::unordered_map<std::string, int> filePathToFileIDMap;
    std::vector<Source> sources{{0, "", ""}};
    
public:
    SourceManager() = default;
    SourceManager(const SourceManager&) = delete;
    SourceManager& operator=(SourceManager&) = delete;
    /**
     * Get the source by file id.
     * @param id File id.
     */
    Source& GetSource(const unsigned int id)
    {
        if (id >= sources.size()) {
            return sources[0];
        } else {
            return sources[id];
        }
    }

    const std::vector<Source>& GetSources() const
    {
        return sources;
    }

    /**
     * Get the file id by file path.
     * @param path file path.
     */
    int GetFileID(const std::string& path)
    {
        auto exist = filePathToFileIDMap.find(path);
        if (exist != filePathToFileIDMap.end()) {
            return exist->second;
        } else {
            return -1;
        }
    }

    /**
     * Get the number of files.
     */
    unsigned int GetNumberOfFiles()
    {
        return static_cast<unsigned int>(sources.size());
    }
    
    bool HasSource()
    {
        // The source manager will create 1 source by default.
        return GetNumberOfFiles() != 1;
    }

    bool IsSourceFileExist(const unsigned int id);
    int GetLineEnd(const Position& pos);

    void SaveSourceFile(unsigned int fileID, std::string normalizedPath,
        std::string buffer, uint64_t fileHash, std::optional<std::string> packageName = std::nullopt);

    /**
    * This add fake files info to `filePathToFileIDMap`,
    * to keep the file id stable and consistent with previous compilations phase.
    */
    void ReserveCommonPartSources(std::vector<std::string> files);

    /**
     * Add a source to SourceManager.
     * @param path File path.
     * @param buffer Source code.
     */
    unsigned int AddSource(const std::string& path, const std::string& buffer,
        std::optional<std::string> packageName = std::nullopt);
    /**
     * Add source to SourceManager. Package name default to null.
     */
    unsigned int AppendSource(const std::string& path, const std::string& buffer);

    /// Overwrite commentsMap with \ref commentsMap
    void AddComments(const TokenVecMap& commentsMap);

    void Clear()
    {
        sources.clear();
        filePathToFileIDMap.clear();
        sources.emplace_back(Source{0, "", ""});
    }

    /**
     * Get content between given position.
     * @param begin: begin position.
     * @param end: end position.
     * @param importGenericContent generic instantiation content.
     */
    std::string GetContentBetween(
        const Position& begin, const Position& end, const std::string& importGenericContent = "") const;

    /**
     * Get content between given position.
     * @param fileID target file.
     * @param begin: begin position.
     * @param end: end position.
     * @param importGenericContent generic instantiation content.
     */
    std::string GetContentBetween(unsigned int fileID, const Position& begin, const Position& end,
        const std::string& importGenericContent = "") const;

    static const std::string testPkgSuffix;
};
} // namespace Cangjie

#endif // CANGJIE_BASIC_SOURCEMANAGER_H
