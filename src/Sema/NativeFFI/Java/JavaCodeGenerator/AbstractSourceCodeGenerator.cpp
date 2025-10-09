// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements Java class generation.
 */

#include "AbstractSourceCodeGenerator.h"

namespace Cangjie::Interop {
AbstractSourceCodeGenerator::AbstractSourceCodeGenerator(const std::string& outputFilePath)
    : outputFilePath(outputFilePath)
{
}

AbstractSourceCodeGenerator::AbstractSourceCodeGenerator(
    const std::string& outputFolderPath, const std::string& outputFileName)
    : outputFilePath(FileUtil::JoinPath(outputFolderPath, outputFileName))
{
}

void AbstractSourceCodeGenerator::Generate()
{
    ConstructResult();
    WriteToOutputFile();
}

bool AbstractSourceCodeGenerator::WriteToOutputFile()
{
    return FileUtil::WriteToFile(outputFilePath, res);
}

void AbstractSourceCodeGenerator::AddWithIndent(const std::string& indent, const std::string& s)
{
    res += indent;
    res += s;
    res += "\n";
}

const std::string AbstractSourceCodeGenerator::TAB = "    ";
const std::string AbstractSourceCodeGenerator::TAB2 =
    AbstractSourceCodeGenerator::TAB + AbstractSourceCodeGenerator::TAB;

} // namespace Cangjie::Interop