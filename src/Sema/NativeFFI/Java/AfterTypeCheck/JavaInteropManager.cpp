// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "JavaInteropManager.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/FileUtil.h"

namespace {
using namespace Cangjie;

std::string GetActualExportJavaPath(const std::string outputPath, const std::optional<std::string>& exportJavaPath)
{
    if (exportJavaPath.has_value()) {
        return exportJavaPath.value();
    }
    auto javaPath = outputPath;
    if (!FileUtil::IsDir(javaPath)) {
        javaPath = FileUtil::GetDirPath(javaPath);
    }
    return javaPath;
}
} // namespace

namespace Cangjie::Interop::Java {
using namespace AST;

JavaInteropManager::JavaInteropManager(ImportManager& importManager, TypeManager& typeManager, DiagnosticEngine& diag,
    const BaseMangler& mangler, const std::optional<std::string>& javagenOutputPath, const std::string outputPath,
    GlobalOptions::InteropLanguage& targetInteropLanguage, const std::optional<std::string>& exportJavaPath)
    : importManager(importManager),
      typeManager(typeManager),
      diag(diag),
      mangler(mangler),
      javagenOutputPath(javagenOutputPath),
      outputPath(outputPath),
      targetInteropLanguage(targetInteropLanguage),
      exportJavaPath(GetActualExportJavaPath(outputPath, exportJavaPath))
{
}
} // namespace Cangjie::Interop::Java