// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the CompileStrategy related classes.
 */

#include "cangjie/Frontend/CompileStrategy.h"

#include "MergeAnnoFromCjd.h"
#include "cangjie/AST/PrintNode.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/ConditionalCompilation/ConditionalCompilation.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Macro/MacroExpansion.h"
#include "cangjie/Parse/Parser.h"
#if defined(CMAKE_ENABLE_ASSERT) || !defined(NDEBUG)
#include "cangjie/Parse/ASTChecker.h"
#endif
#include "cangjie/Sema/Desugar.h"
#include "cangjie/Sema/TypeChecker.h"
#if (defined RELEASE)
#include "cangjie/Utils/Signal.h"
#endif
#include "cangjie/Utils/Utils.h"
#include "cangjie/Utils/ProfileRecorder.h"

using namespace Cangjie;
using namespace Utils;
using namespace FileUtil;

void CompileStrategy::TypeCheck() const
{
    if (!ci->typeChecker) {
        ci->typeChecker = new TypeChecker(ci);
        CJC_NULLPTR_CHECK(ci->typeChecker);
    }
    ci->typeChecker->TypeCheckForPackages(ci->GetSourcePackages());
}

bool CompileStrategy::ConditionCompile() const
{
    auto beforeErrCnt = ci->diag.GetErrorCount();
    ConditionalCompilation cc{ci};
    for (auto& pkg : ci->srcPkgs) {
        cc.HandleConditionalCompilation(*pkg.get());
    }
    return beforeErrCnt == ci->diag.GetErrorCount();
}

void CompileStrategy::DesugarAfterSema() const
{
    ci->typeChecker->PerformDesugarAfterSema(ci->GetSourcePackages());
}

bool CompileStrategy::OverflowStrategy() const
{
    if (!ci->typeChecker) {
        auto typeChecker = new TypeChecker(ci);
        CJC_NULLPTR_CHECK(typeChecker);
        ci->typeChecker = typeChecker;
    }
    CJC_ASSERT(ci->invocation.globalOptions.overflowStrategy != OverflowStrategy::NA);
    ci->typeChecker->SetOverflowStrategy(ci->GetSourcePackages());
    return true;
}

void CompileStrategy::PerformDesugar() const
{
    for (auto& [pkg, ctx] : ci->pkgCtxMap) {
        Cangjie::PerformDesugarBeforeTypeCheck(*pkg, ci->invocation.globalOptions.enableMacroInLSP);
    }
}

namespace Cangjie {
class FullCompileStrategyImpl final {
public:
    explicit FullCompileStrategyImpl(FullCompileStrategy& strategy) : s{strategy}
    {
    }

    void ParseModule(bool& success)
    {
        std::string moduleSrcPath = s.ci->invocation.globalOptions.moduleSrcPath;
        std::unordered_set<std::string> includeFileSet;
        if (!s.ci->invocation.globalOptions.srcFiles.empty()) {
            includeFileSet.insert(
                s.ci->invocation.globalOptions.srcFiles.begin(), s.ci->invocation.globalOptions.srcFiles.end());
        }
        for (auto& srcDir : s.ci->srcDirs) {
            std::vector<std::string> allSrcFiles;
            auto currentPkg = DEFAULT_PACKAGE_NAME;
            if (!moduleSrcPath.empty()) {
                auto basePath = IsDir(moduleSrcPath) ? JoinPath(moduleSrcPath, "") : moduleSrcPath;
                currentPkg = GetPkgNameFromRelativePath(GetRelativePath(basePath, srcDir) | IdenticalFunc);
            }
            auto parseTest = s.ci->invocation.globalOptions.parseTest;
            auto compileTestsOnly = s.ci->invocation.globalOptions.compileTestsOnly;
            for (auto& srcFile : GetAllFilesUnderCurrentPath(srcDir, "cj", !parseTest, compileTestsOnly)) {
                std::string filename = JoinPath(srcDir, srcFile);
                if (includeFileSet.empty()) {
                    // If no srcFiles, compile the whole module defaultly
                    allSrcFiles.push_back(filename);
                } else if (includeFileSet.find(filename) != includeFileSet.end()) {
                    // If there are srcFiles, use them to select files to compile
                    allSrcFiles.push_back(filename);
                }
            }
            auto package = ParseOnePackage(allSrcFiles, success, currentPkg);
            if (srcDir == moduleSrcPath) {
                package->needExported = false;
            }
            s.ci->srcPkgs.emplace_back(std::move(package));
        }
    }

    OwnedPtr<AST::Package> GetMultiThreadParseOnePackage(
        std::queue<std::future<std::tuple<OwnedPtr<File>, TokenVecMap, size_t>>>& futureQueue,
        const std::string& defaultPackageName) const
    {
        auto package = MakeOwned<Package>(defaultPackageName);
        size_t lineNumInOnePackage = 0;
        const size_t filePtrIdx = 0;
        const size_t commentIdx = 1;
        const size_t lineNumIdx = 2;
        while (!futureQueue.empty()) {
            auto curFuture = futureQueue.front().get();
            std::get<filePtrIdx>(curFuture)->curPackage = package.get();
            std::get<filePtrIdx>(curFuture)->indexOfPackage = package->files.size();
            package->files.push_back(std::move(std::get<filePtrIdx>(curFuture)));
            s.ci->GetSourceManager().AddComments(std::get<commentIdx>(curFuture));
            lineNumInOnePackage += std::get<lineNumIdx>(curFuture);
            futureQueue.pop();
        }
        Utils::ProfileRecorder::RecordCodeInfo("package line num", static_cast<int64_t>(lineNumInOnePackage));
        if (!package->files.empty()) {
            // Only update name of package node for first parsed file.
            if (auto packageSpec = package->files[0]->package.get()) {
                auto names = packageSpec->prefixPaths;
                names.emplace_back(packageSpec->packageName);
                package->fullPackageName = Utils::JoinStrings(names, ".");
                package->accessible = !packageSpec->modifier                  ? AccessLevel::PUBLIC
                    : packageSpec->modifier->modifier == TokenKind::PROTECTED ? AccessLevel::PROTECTED
                    : packageSpec->modifier->modifier == TokenKind::INTERNAL  ? AccessLevel::INTERNAL
                                                                              : AccessLevel::PUBLIC;
            }
        }
        // Checking package consistency: The macro definition package cannot contain the declaration of a common
        // package.
        CheckPackageConsistency(*package);
        return package;
    }

    void CheckPackageConsistency(Package& package) const
    {
        if (package.files.empty() || !package.files[0]->package) {
            return;
        }
        for (const auto& file : package.files) {
            if (file->package && package.files[0]->package->hasMacro != file->package->hasMacro) {
                (void)s.ci->diag.DiagnoseRefactor(DiagKindRefactor::package_name_inconsistent_with_macro, file->begin);
                return;
            }
        }
        package.isMacroPackage = package.files[0]->package->hasMacro;
    }

    OwnedPtr<Package> MultiThreadParseOnePackage(
        std::queue<std::tuple<std::string, unsigned>>& fileInfoQueue, const std::string& defaultPackageName) const
    {
        std::queue<std::future<std::tuple<OwnedPtr<File>, TokenVecMap, size_t>>> futureQueue;
        while (!fileInfoQueue.empty()) {
            auto curFile = fileInfoQueue.front();
            futureQueue.push(
                std::async(std::launch::async, [this, curFile]() -> std::tuple<OwnedPtr<File>, TokenVecMap, size_t> {
#if (defined RELEASE)
#if (defined __unix__)
                    // Since alternate signal stack is per thread, we have to create an alternate signal stack for each
                    // thread.
                    Cangjie::CreateAltSignalStack();
#elif _WIN32
                    // When the SIGABRT, SIGFPE, SIGSEGV and SIGILL signals are triggered in a subthread,
                    // the signals cannot be captured and the process exits directly. Therefore,
                    // the signal processing function must be set for each thread.
                    Cangjie::RegisterCrashSignalHandler();
#endif
#endif
                    auto parser = CreateParser(curFile);
                    parser->SetCompileOptions(s.ci->invocation.globalOptions);
                    auto file = parser->ParseTopLevel();
#ifdef SIGNAL_TEST
                    // The interrupt signal triggers the function. In normal cases, this function does not take effect.
                    Cangjie::SignalTest::ExecuteSignalTestCallbackFunc(
                        Cangjie::SignalTest::TriggerPointer::PARSER_POINTER);
#endif
                    return {std::move(file), parser->GetCommentsMap(), parser->GetLineNum()};
                }));
            fileInfoQueue.pop();
        }

        auto package = GetMultiThreadParseOnePackage(futureQueue, defaultPackageName);
        return package;
    }

    OwnedPtr<Parser> CreateParser(const std::tuple<std::string, unsigned>& curFile) const
    {
        return MakeOwned<Parser>(std::get<1>(curFile), std::get<0>(curFile), s.ci->diag, s.ci->GetSourceManager(),
            s.ci->invocation.globalOptions.enableAddCommentToAst, s.ci->invocation.globalOptions.compileCjd);
    }

    OwnedPtr<Package> ParseOnePackage(
        const std::vector<std::string>& files, bool& success, const std::string& defaultPackageName)
    {
        std::queue<std::tuple<std::string, unsigned>> fileInfoQueue;

        // Parse source code files to File node list.
        if (s.ci->loadSrcFilesFromCache) {
            for (auto& it : s.ci->bufferCache) {
                const unsigned int fileID = s.ci->GetSourceManager().AddSource(it.first, it.second);
                if (s.fileIds.count(fileID) > 0) {
                    (void)s.ci->diag.DiagnoseRefactor(
                        DiagKindRefactor::module_read_file_conflicted, DEFAULT_POSITION, it.first);
                }
                (void)s.fileIds.insert(fileID);
                fileInfoQueue.emplace(it.second, fileID);
            }
        } else {
            // The readdir cannot guarantee stable order of inputted files, need sort before adding to sourceManager.
            std::vector<std::string> parseFiles{files};
            std::sort(parseFiles.begin(), parseFiles.end(),
                [&](auto& f, auto& second) { return GetFileName(f) < GetFileName(second); });
            std::for_each(parseFiles.begin(), parseFiles.end(), [this, &success, &fileInfoQueue](auto file) {
                std::string failedReason;
                auto content = ReadFileContent(file, failedReason);
                if (!content.has_value()) {
                    s.ci->diag.DiagnoseRefactor(
                        DiagKindRefactor::module_read_file_to_buffer_failed, DEFAULT_POSITION, file, failedReason);
                    success = false;
                    return;
                }
                const unsigned int fileID = s.ci->GetSourceManager().AddSource(file | IdenticalFunc, content.value());
                if (s.fileIds.count(fileID) > 0) {
                    (void)s.ci->diag.DiagnoseRefactor(
                        DiagKindRefactor::module_read_file_conflicted, DEFAULT_POSITION, file);
                    return;
                }

                (void)s.fileIds.insert(fileID);
                fileInfoQueue.emplace(std::move(content.value()), fileID);
            });
        }

        auto package = MultiThreadParseOnePackage(fileInfoQueue, defaultPackageName);
        s.ci->diag.EmitCategoryGroup();
        std::sort(package->files.begin(), package->files.end(),
            [](const OwnedPtr<File>& fileOne, const OwnedPtr<File>& fileTwo) {
                return fileOne->fileName < fileTwo->fileName;
            });
        return package;
    }
    FullCompileStrategy& s;
};
} // namespace Cangjie

FullCompileStrategy::FullCompileStrategy(CompilerInstance* ci)
    : CompileStrategy(ci), impl{new FullCompileStrategyImpl{*this}}
{
    type = StrategyType::FULL_COMPILE;
}

FullCompileStrategy::~FullCompileStrategy()
{
    delete impl;
}

bool FullCompileStrategy::Parse()
{
    bool ret = true;
    if (ci->loadSrcFilesFromCache || ci->compileOnePackageFromSrcFiles) {
        auto package = impl->ParseOnePackage(ci->srcFilePaths, ret, DEFAULT_PACKAGE_NAME);
        ci->srcPkgs.emplace_back(std::move(package));
    } else {
        impl->ParseModule(ret);
    }
    return ret;
}

bool CompileStrategy::ImportPackages() const
{
    auto ret = ci->ImportPackages();
    ParseAndMacroExpandCjd();
    return ret;
}

namespace {
// All instance objects share, do not clean. The cjd content of the same process should not be inconsistent.
std::unordered_map<std::string, OwnedPtr<Package>> gCjdAstCache;
} // namespace

void CompileStrategy::ParseAndMacroExpandCjd() const
{
    Utils::ProfileRecorder::Start("ImportPackages", "ParseAndMacroExpandCjd");
    auto cjdPaths = ci->importManager.GetDepPkgCjdPaths();
    auto searchPath = ci->importManager.GetSearchPath();
    // cjdInfo is [fullPackageName, cjdPath].
    for (auto cjdInfo : cjdPaths) {
        if (auto pkgAst = gCjdAstCache.find(cjdInfo.first); pkgAst != gCjdAstCache.end()) {
            auto originPkg = ci->importManager.GetPackage(cjdInfo.first);
            if (!originPkg) {
                InternalError(cjdInfo.first + " cannot find origin ast");
            }
            MergeCusAnno(originPkg, pkgAst->second.get());
            continue;
        }
        std::string failedReason;
        auto sourceCode = FileUtil::ReadFileContent(cjdInfo.second, failedReason);
        if (!failedReason.empty() || !sourceCode.has_value()) {
            // In the LSP scenario, the cjd file path cannot be obtained based on the dependency package information
            // configured in the cache. The cjd file path is searched in searchPath.
            auto cjdPath = FileUtil::FindSerializationFile(cjdInfo.first, CJ_D_FILE_EXTENSION, searchPath);
            if (cjdPath.empty()) {
                continue;
            }
            sourceCode = FileUtil::ReadFileContent(cjdPath, failedReason);
            if (!failedReason.empty() || !sourceCode.has_value()) {
                continue;
            }
        }
        // Reuse current CompilerInstance, but the Parser in the macro expansion phase uses the DParser.
        ci->invocation.globalOptions.compileCjd = true;
        // Parse
        SourceManager& sm = ci->diag.GetSourceManager();
        auto fileId = sm.AddSource(cjdInfo.second, sourceCode.value(), cjdInfo.first);
        auto fileAst =
            Parser(fileId, sourceCode.value(), ci->diag, ci->diag.GetSourceManager(), false, true).ParseTopLevel();
        auto pkg = MakeOwned<Package>(cjdInfo.first);
        fileAst->curPackage = pkg.get();
        pkg->files.emplace_back(std::move(fileAst));
        // MacroExpand
        MacroExpansion me(ci);
        me.Execute(*pkg.get());
        ci->invocation.globalOptions.compileCjd = false;
        auto originPkg = ci->importManager.GetPackage(cjdInfo.first);
        if (!originPkg) {
            InternalError(cjdInfo.first + " cannot find origin ast");
        }
        MergeCusAnno(originPkg, pkg.get());
    }
    Utils::ProfileRecorder::Stop("ImportPackages", "ParseAndMacroExpandCjd");
}

bool CompileStrategy::MacroExpand() const
{
    auto beforeErrCnt = ci->diag.GetErrorCount();
    MacroExpansion me(ci);
    me.Execute(ci->srcPkgs);
    ci->diag.EmitCategoryDiagnostics(DiagCategory::PARSE);

#if defined(CMAKE_ENABLE_ASSERT) || !defined(NDEBUG)
    AST::ASTChecker astChecker;
    astChecker.CheckAST(ci->srcPkgs);
    astChecker.CheckBeginEnd(ci->srcPkgs);
#endif

    ci->tokensEvalInMacro = me.tokensEvalInMacro;
    bool hasNoMacroErr = beforeErrCnt == ci->diag.GetErrorCount();
    return hasNoMacroErr;
}

bool FullCompileStrategy::Sema()
{
    {
        Utils::ProfileRecorder recorder("Semantic", "Desugar Before TypeCheck");
        PerformDesugar();
    }
    TypeCheck();
#ifdef SIGNAL_TEST
    // The interrupt signal triggers the function. In normal cases, this function does not take effect.
    Cangjie::SignalTest::ExecuteSignalTestCallbackFunc(Cangjie::SignalTest::TriggerPointer::SEMA_POINTER);
#endif
    // Report number of warnings and errors.
    if (ci->diag.GetErrorCount() != 0) {
        return false;
    }
    return true;
}
