// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the SourceManager related classes.
 */

#include "cangjie/Basic/SourceManager.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/FileUtil.h"

using namespace Cangjie;

size_t Source::PosToOffset(const Position& pos) const
{
    if (pos.line > static_cast<int>(lineOffsets.size())) {
        return buffer.length();
    }
    if (pos.line < 1 || pos.column < 1) {
        return 0;
    }
    size_t index = static_cast<size_t>(pos.line) - 1;
    auto pStart = buffer.data() + lineOffsets[index];
    auto pEnd = buffer.data() + buffer.length();
    size_t columnOffset = 0;
    while (Utils::GetLineTerminatorLength(pStart + columnOffset, pEnd) == 0 &&
        columnOffset < (static_cast<size_t>(pos.column) - 1) && pStart + columnOffset < pEnd) {
        columnOffset++;
    }
    if (columnOffset < (static_cast<size_t>(pos.column) - 1) && pStart + columnOffset < pEnd) {
        // there's line terminator before `pos.column`
        columnOffset += Utils::GetLineTerminatorLength(pStart + columnOffset, pEnd);
    }

    return std::min(lineOffsets[index] + columnOffset, buffer.length());
}

Source::Source(unsigned int fileID, std::string path, std::string buffer, uint64_t fileHash,
    const std::optional<std::string>& packageName)
    : fileID(fileID), path(std::move(path)), buffer(std::move(buffer)), fileHash(fileHash), packageName(packageName)
{
    if (this->buffer.empty()) {
        return;
    }

    // build lineOffsets
    auto pStart = this->buffer.data();
    size_t length = this->buffer.length();
    auto pEnd = this->buffer.data() + length;
    for (auto ptr = pStart; ptr < pEnd;) {
        while (Utils::GetLineTerminatorLength(ptr, pEnd) == 0 && ptr < pEnd) {
            ptr++;
        }
        if (ptr >= pEnd) {
            break;
        }
        ptr += Utils::GetLineTerminatorLength(ptr, pEnd);
        lineOffsets.emplace_back(ptr - pStart);
    }
}

void SourceManager::SaveSourceFile(
    unsigned int fileID,
    std::string normalizedPath,
    std::string buffer,
    uint64_t fileHash,
    std::optional<std::string> packageName)
{
    sources.emplace_back(fileID, normalizedPath, buffer, fileHash, packageName);
    filePathToFileIDMap.emplace(normalizedPath, fileID);
}

void SourceManager::ReserveCommonPartSources(std::vector<std::string> files)
{
    for (size_t i = 0; i < files.size(); i++) {
        auto file = files.at(i);
        uint64_t fileHash = 0;
        SaveSourceFile(static_cast<unsigned int>(i + 1), files.at(i), "", fileHash);
        filePathToFileIDMap.emplace(file, i + 1);
    }
}

unsigned int SourceManager::AddSource(
    const std::string& path, const std::string& buffer, std::optional<std::string> packageName)
{
    // path canonicalize
    std::string normalizePath = FileUtil::Normalize(path);
    // Change fileHash from content hash to path hash.
    uint64_t fileHash = Utils::GetHash(normalizePath);
    auto existed = filePathToFileIDMap.find(normalizePath);
    if (existed != filePathToFileIDMap.end()) {
        sources[static_cast<size_t>(existed->second)] =
            Source{static_cast<unsigned>(existed->second), normalizePath, buffer, fileHash, packageName};
        return static_cast<unsigned>(existed->second);
    } else {
        auto fileID = static_cast<unsigned int>(sources.size());
        SaveSourceFile(fileID, normalizePath, buffer, fileHash, packageName);
        return fileID;
    }
}

unsigned int SourceManager::AppendSource(const std::string& path, const std::string& buffer)
{
    // path canonicalize
    std::string normalizePath = FileUtil::Normalize(path);
    uint64_t fileHash = Utils::GetHash(normalizePath);
    auto existed = filePathToFileIDMap.find(normalizePath);
    if (existed != filePathToFileIDMap.end()) {
        auto newBuffer = sources[static_cast<size_t>(existed->second)].buffer + buffer;
        sources[static_cast<size_t>(existed->second)] =
            Source{static_cast<unsigned>(existed->second), normalizePath, newBuffer, fileHash};
        return static_cast<unsigned>(existed->second);
    } else {
        auto fileID = static_cast<unsigned int>(sources.size());
        sources.emplace_back(fileID, normalizePath, buffer, fileHash);
        filePathToFileIDMap.emplace(normalizePath, fileID);
        return fileID;
    }
}

bool SourceManager::IsSourceFileExist(const unsigned int id)
{
    // Check whether the *.macrocall exists or not.
    if (id < sources.size()) {
        auto path = sources[id].path;
        if (!path.empty() && FileUtil::GetFileExtension(path) != "cj") {
            return FileUtil::FileExist(path);
        }
    }
    return true;
}

int SourceManager::GetLineEnd(const Position& pos)
{
    if (pos.fileID >= sources.size()) {
        return 0;
    }
    auto buffer = sources[pos.fileID].buffer;
    auto sourceSplited = Utils::SplitLines(buffer);
    if (pos.line > static_cast<int>(sourceSplited.size())) {
        return 0;
    }
    CJC_ASSERT(pos.line > 0);
    if (pos.line <= 0) {
        return 0;
    }
    return static_cast<int>(sourceSplited[static_cast<size_t>(pos.line - 1)].size());
}

std::string SourceManager::GetContentBetween(
    const Position& begin, const Position& end, const std::string& importGenericContent) const
{
    return GetContentBetween(begin.fileID, begin, end, importGenericContent);
}

std::string SourceManager::GetContentBetween(
    unsigned int fileID, const Position& begin, const Position& end, const std::string& importGenericContent) const
{
    if (fileID == 0 || begin <= INVALID_POSITION || end <= INVALID_POSITION || end < begin) {
        return "";
    }

    CJC_ASSERT(INVALID_POSITION < begin && begin <= end);

    auto& sourceWithFileID = fileID >= sources.size() ? sources[0] : sources[fileID];
    const auto& source = sourceWithFileID.buffer.empty() && !importGenericContent.empty()
        ? Source(sourceWithFileID.fileID, sourceWithFileID.path, importGenericContent)
        : sourceWithFileID;
    const auto& buffer = source.buffer;

    if (buffer.empty()) {
        return "";
    }

    auto startOffset = source.PosToOffset(begin);
    auto endOffset = source.PosToOffset(end);
    return buffer.substr(startOffset, endOffset - startOffset);
}

void SourceManager::AddComments(const TokenVecMap& commentsMap)
{
    for (const auto& it : commentsMap) {
        CJC_ASSERT(it.first < sources.size());
        auto& source = sources[it.first];
        for (auto tok : it.second) {
            (void)source.offsetCommentsMap.insert_or_assign(source.PosToOffset(tok.Begin()), tok);
        }
    }
}

namespace Cangjie {
const std::string SourceManager::testPkgSuffix = "$test";
}
