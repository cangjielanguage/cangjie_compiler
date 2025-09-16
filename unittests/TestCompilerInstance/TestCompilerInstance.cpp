// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the TestCompilerInstance.
 */

#include "TestCompilerInstance.h"

#include "cangjie/IncrementalCompilation/ASTCacheCalculator.h"
#include "cangjie/Parse/Parser.h"

using namespace Cangjie;
using namespace AST;

namespace {
RawMangled2DeclMap RunASTCacheCalculation(const Package& sourcePackage, const GlobalOptions op)
{
    Cangjie::IncrementalCompilation::ASTCacheCalculator pc{
        sourcePackage, std::make_pair(op.enableCompileDebug, op.displayLineInfo)};
    pc.Walk();
    return pc.mangled2Decl;
}
} // namespace

bool TestCompilerInstance::Compile(CompileStage stage)
{
    if (!PerformParse()) {
        return false;
    }
    if (stage == CompileStage::PARSE) {
        return true;
    }
    // Preprocess for incremental mangle.
    if (!srcPkgs.empty() && stage == CompileStage::DESUGAR_AFTER_SEMA) {
        rawMangleName2DeclMap = RunASTCacheCalculation(*srcPkgs[0], invocation.globalOptions);
    }
    auto modular = ModularizeCompilation();
    auto importRes = PerformImportPackage();
    if (stage == CompileStage::IMPORT_PACKAGE) {
        return modular && importRes;
    }
    auto macroRes = PerformMacroExpand();
    auto semaRes = PerformSema();
    if (stage == CompileStage::SEMA || stage == CompileStage::DESUGAR_AFTER_SEMA) {
        return Utils::AllOf(importRes, macroRes, semaRes, modular);
    }
    auto giRes = PerformGenericInstantiation();
    return Utils::AllOf(importRes, macroRes, semaRes, giRes, modular);
}

bool TestCompilerInstance::ParseCode()
{
    auto package = MakeOwned<Package>();
    auto fileID = GetSourceManager().AddSource("", code);
    Parser parser(fileID, code, diag, GetSourceManager(), invocation.globalOptions.enableAddCommentToAst,
        invocation.globalOptions.compileCjd);
    parser.SetCompileOptions(invocation.globalOptions);
    auto file = parser.ParseTopLevel();
    GetSourceManager().AddComments(parser.GetCommentsMap());
    if (!file->package) {
        package->fullPackageName = DEFAULT_PACKAGE_NAME;
    } else {
        package->fullPackageName = file->package->packageName;
    }
    file->curPackage = package.get();
    package->files.push_back(std::move(file));
    srcPkgs.push_back(std::move(package));
    return true;
}

bool TestCompilerInstance::PerformParse()
{
    if (!code.empty()) {
        return ParseCode();
    }
    return compileStrategy->Parse();
}
