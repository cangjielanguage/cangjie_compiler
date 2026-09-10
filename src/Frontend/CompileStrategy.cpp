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
#include "cangjie/Utils/ProfileRecorder.h"
#include "cangjie/Utils/TaskQueue.h"

using namespace Cangjie;
using namespace Utils;
using namespace FileUtil;

void CompileStrategy::TypeCheck() const
{
    Utils::ProfileRecorder recorder("Semantic", "TypeCheck");
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
    auto packages = ci->GetSourcePackages();
    ci->typeChecker->PerformDesugarAfterSema(packages);
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
    Utils::ProfileRecorder recorder("Semantic", "Desugar Before TypeCheck");
    const auto found = ci->invocation.globalOptions.passedWhenKeyValue.find("APILevel_level");
    const std::string compatibleSDKVersion =
        found == ci->invocation.globalOptions.passedWhenKeyValue.end() ? "" : found->second;
    for (auto& [pkg, ctx] : ci->pkgCtxMap) {
        Cangjie::PerformDesugarBeforeTypeCheck(
            *pkg, ci->invocation.globalOptions.enableMacroInLSP, compatibleSDKVersion);
    }
}

namespace Cangjie {
class FullCompileStrategyImpl final {
private:
    using ParseResult = std::tuple<OwnedPtr<File>, TokenVecMap, size_t>;
    using ParseTaskResult = TaskResult<ParseResult>;
    using ParseTaskResults = std::vector<ParseTaskResult>;

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

        for (auto& package : s.ci->srcPkgs) {
            std::sort(package->files.begin(), package->files.end(),
                [](const OwnedPtr<File>& fileOne, const OwnedPtr<File>& fileTwo) {
                    return fileOne->fileName < fileTwo->fileName;
                });
        }
    }

    /**
     * Initialize package information from the first file's package specification.
     * @param package The package to initialize
     */
    void InitializePackageInfoFromFirstFile(Package& package) const
    {
        if (package.files.empty()) {
            return;
        }
        if (auto packageSpec = package.files[0]->package.get()) {
            package.fullPackageName = packageSpec->GetPackageName();
            package.accessible = !packageSpec->modifier                  ? AccessLevel::PUBLIC
                : packageSpec->modifier->modifier == TokenKind::PROTECTED ? AccessLevel::PROTECTED
                : packageSpec->modifier->modifier == TokenKind::INTERNAL  ? AccessLevel::INTERNAL
                                                                            : AccessLevel::PUBLIC;
        }
    }

    /**
     * Add parsed files from task results to an existing package.
     * @param package The package to add files to
     * @param taskResults Vector of task results containing parsed files
     * @return Total line count of added files
     */
    size_t AddFilesToPackage(Package& package, ParseTaskResults& taskResults) const
    {
        size_t lineCountOfAddedFiles = 0;
        const size_t filePtrIdx = 0;
        const size_t commentIdx = 1;
        const size_t lineNumIdx = 2;
        for (auto& taskResult : taskResults) {
            auto curResult = taskResult.get();
            std::get<filePtrIdx>(curResult)->curPackage = &package;
            std::get<filePtrIdx>(curResult)->indexOfPackage = package.files.size();
            package.files.push_back(std::move(std::get<filePtrIdx>(curResult)));
            s.ci->GetSourceManager().AddComments(std::get<commentIdx>(curResult));
            lineCountOfAddedFiles += std::get<lineNumIdx>(curResult);
        }
        InitializePackageInfoFromFirstFile(package);
        return lineCountOfAddedFiles;
    }

    OwnedPtr<AST::Package> GetMultiThreadParseOnePackage(
        ParseTaskResults& taskResults, const std::string& defaultPackageName) const
    {
        auto package = MakeOwned<Package>(defaultPackageName);
        size_t lineNumInOnePackage = AddFilesToPackage(*package, taskResults);
        Utils::ProfileRecorder::RecordCodeInfo("package line num", static_cast<int64_t>(lineNumInOnePackage));
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

    /**
     * Parse files using TaskQueue and collect results.
     * @param fileInfoQueue Queue of file information (content, fileID) to parse
     * @return Vector of futures containing parsed files
     */
    ParseTaskResults ParseFilesWithTaskQueue(
        std::queue<std::tuple<std::string, unsigned>>& fileInfoQueue) const
    {
        size_t threadsNum = s.ci->invocation.globalOptions.GetJobs();
        Utils::TaskQueue taskQueue(threadsNum);
        ParseTaskResults taskResults;

        // Add all parsing tasks to the queue
        while (!fileInfoQueue.empty()) {
            auto curFile = fileInfoQueue.front();
            auto taskResult = taskQueue.AddTask<ParseResult>(
                [this, curFile]() -> ParseResult {
#if (defined RELEASE)
#if (defined __unix__)
                    // Since alternate signal stack is per thread, we have to create an alternate signal stack for each
                    // thread.
                    Cangjie::CreateAltSignalStack();
#elif defined(_WIN32)
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
                });
            taskResults.push_back(std::move(taskResult));
            fileInfoQueue.pop();
        }
        taskQueue.RunAndWaitForAllTasksCompleted();
        return taskResults;
    }

    /**
     * Parse files in the current thread and return results in order.
     * This path must not use task queues or similar scheduling.
     */
    std::vector<ParseResult> ParseFilesSingleThread(std::queue<std::tuple<std::string, unsigned>>& fileInfoQueue) const
    {
        std::vector<ParseResult> results;
        while (!fileInfoQueue.empty()) {
            auto curFile = fileInfoQueue.front();
            fileInfoQueue.pop();

            auto parser = CreateParser(curFile);
            parser->SetCompileOptions(s.ci->invocation.globalOptions);
            auto file = parser->ParseTopLevel();
#ifdef SIGNAL_TEST
            // The interrupt signal triggers the function. In normal cases, this function does not take effect.
            Cangjie::SignalTest::ExecuteSignalTestCallbackFunc(Cangjie::SignalTest::TriggerPointer::PARSER_POINTER);
#endif
            results.emplace_back(std::move(file), parser->GetCommentsMap(), parser->GetLineNum());
        }
        return results;
    }

    /**
     * Add parsed files from results to an existing package.
     * Returns total line count of added files.
     */
    size_t AddFilesToPackage(Package& package, std::vector<ParseResult>& results) const
    {
        size_t lineCountOfAddedFiles = 0;
        for (auto& result : results) {
            auto& [file, comments, lineCount] = result;
            file->curPackage = &package;
            file->indexOfPackage = package.files.size();
            package.files.push_back(std::move(file));
            s.ci->GetSourceManager().AddComments(comments);
            lineCountOfAddedFiles += lineCount;
        }
        InitializePackageInfoFromFirstFile(package);
        return lineCountOfAddedFiles;
    }

    OwnedPtr<Package> SingleThreadParseOnePackage(
        std::queue<std::tuple<std::string, unsigned>>& fileInfoQueue, const std::string& defaultPackageName) const
    {
        auto package = MakeOwned<Package>(defaultPackageName);
        auto results = ParseFilesSingleThread(fileInfoQueue);
        size_t lineNumInOnePackage = AddFilesToPackage(*package, results);
        Utils::ProfileRecorder::RecordCodeInfo("package line num", static_cast<int64_t>(lineNumInOnePackage));
        CheckPackageConsistency(*package);
        return package;
    }

    void SingleThreadParseOnePackage(
        Package& package, std::queue<std::tuple<std::string, unsigned>>& fileInfoQueue) const
    {
        auto results = ParseFilesSingleThread(fileInfoQueue);
        size_t lineCountOfAddedFiles = AddFilesToPackage(package, results);
        for (auto& file : package.files) {
            CJC_ASSERT_WITH_MSG(file->curPackage == &package, "curPackage should be set to package");
        }
        Utils::ProfileRecorder::RecordCodeInfo("added line num", static_cast<int64_t>(lineCountOfAddedFiles));
        CheckPackageConsistency(package);
    }

    OwnedPtr<Package> MultiThreadParseOnePackage(
        std::queue<std::tuple<std::string, unsigned>>& fileInfoQueue, const std::string& defaultPackageName) const
    {
        auto taskResults = ParseFilesWithTaskQueue(fileInfoQueue);
        auto package = GetMultiThreadParseOnePackage(taskResults, defaultPackageName);
        return package;
    }

    /**
     * Multi-threaded parse files and add them to an existing package.
     * @param package The existing package to add parsed files to
     * @param fileInfoQueue Queue of file information (content, fileID) to parse
     */
    void MultiThreadParseOnePackage(Package& package,
        std::queue<std::tuple<std::string, unsigned>>& fileInfoQueue) const
    {
        auto taskResults = ParseFilesWithTaskQueue(fileInfoQueue);
        size_t lineCountOfAddedFiles = AddFilesToPackage(package, taskResults);
        // Check curPackage for each file in the package to ensure it is not changed in lsp or some other reason.
        for (auto& file : package.files) {
            CJC_ASSERT_WITH_MSG(file->curPackage == &package, "curPackage should be set to package");
        }
        Utils::ProfileRecorder::RecordCodeInfo("added line num", static_cast<int64_t>(lineCountOfAddedFiles));
        // Checking package consistency: The macro definition package cannot contain the declaration of a common
        // package.
        CheckPackageConsistency(package);
    }

    OwnedPtr<Parser> CreateParser(const std::tuple<std::string, unsigned>& curFile) const
    {
        return MakeOwned<Parser>(std::get<1>(curFile), std::get<0>(curFile), s.ci->diag, s.ci->GetSourceManager(),
            s.ci->invocation.globalOptions.enableAddCommentToAst, s.ci->invocation.globalOptions.compileCjd);
    }

    void DeleteFileInPackage(Package& pkg, const std::string& filePath)
    {
        pkg.files.erase(
            std::remove_if(pkg.files.begin(), pkg.files.end(),
            [&filePath](const OwnedPtr<File>& file) { return file->filePath == filePath; }),
            pkg.files.end());
    }

    /**
     * Build fileInfoQueue from bufferCache for incremental parsing.
     * @param package The package to delete files from for DELETED/CHANGED files
     * @param fileInfoQueue Output queue to add file information
     */
    void BuildFileInfoQueueFromCache(Package& package, std::queue<std::tuple<std::string, unsigned>>& fileInfoQueue)
    {
        std::list<std::string> deletedFiles;
        for (auto& it : s.ci->bufferCache) {
            const std::string& filePath = it.first;
            const CompilerInstance::SrcCodeChangeState state = it.second.state;

            if (state == CompilerInstance::SrcCodeChangeState::UNCHANGED) {
                continue;
            }

            if (state == CompilerInstance::SrcCodeChangeState::DELETED) {
                DeleteFileInPackage(package, filePath);
                deletedFiles.push_back(filePath);
                continue;
            }
            if (state != CompilerInstance::SrcCodeChangeState::ADDED) {
                // CHANGED: delete old file first
                DeleteFileInPackage(package, filePath);
            }
            CJC_ASSERT_WITH_MSG(state != CompilerInstance::SrcCodeChangeState::ADDED ||
                    std::find_if(package.files.begin(), package.files.end(),
                        [filePath](const OwnedPtr<File>& file) { return file->filePath == filePath; }) ==
                        package.files.end(),
                "File already exists");
            unsigned int fileID = s.ci->GetSourceManager().AddSource(filePath, it.second.code);
            s.fileIds.insert(fileID);
            fileInfoQueue.emplace(it.second.code, fileID);
        }
        for (auto& filePath : deletedFiles) {
            s.ci->bufferCache.erase(filePath);
        }
    }

    /**
     * Parse files and add them to an existing package (incremental parse).
     * @param package The existing package to add parsed files to
     */
    void ParseOnePackage(Package& package)
    {
        std::queue<std::tuple<std::string, unsigned>> fileInfoQueue;
        CJC_ASSERT_WITH_MSG(s.ci->loadSrcFilesFromCache, "loadSrcFilesFromCache must be true for incremental parse");
        BuildFileInfoQueueFromCache(package, fileInfoQueue);

        if (!fileInfoQueue.empty()) {
            if (s.ci->invocation.globalOptions.GetJobs() == 1) {
                SingleThreadParseOnePackage(package, fileInfoQueue);
            } else {
                MultiThreadParseOnePackage(package, fileInfoQueue);
            }
            s.ci->diag.EmitCategoryGroup();
            std::sort(package.files.begin(), package.files.end(),
                [](const OwnedPtr<File>& fileOne, const OwnedPtr<File>& fileTwo) {
                    return fileOne->fileName < fileTwo->fileName;
                });
        }
    }

    OwnedPtr<Package> ParseOnePackage(
        const std::vector<std::string>& files, bool& success, const std::string& defaultPackageName)
    {
        std::queue<std::tuple<std::string, unsigned>> fileInfoQueue;

        // Parse source code files to File node list.
        if (s.ci->loadSrcFilesFromCache) {
            for (auto& it : s.ci->bufferCache) {
                bool isCjmpFile = s.ci->invocation.globalOptions.IsCompilingCJMP();
                const unsigned int fileID = s.ci->GetSourceManager().AddSource(it.first, it.second.code,
                    std::nullopt, isCjmpFile);
                if (s.fileIds.count(fileID) > 0) {
                    s.ci->diag.DiagnoseRefactor(
                        DiagKindRefactor::module_read_file_conflicted, DEFAULT_POSITION, it.first);
                }
                s.fileIds.insert(fileID);
                fileInfoQueue.emplace(it.second.code, fileID);
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
                bool isCjmpFile = s.ci->invocation.globalOptions.IsCompilingCJMP();
                const unsigned int fileID = s.ci->GetSourceManager().AddSource(file | IdenticalFunc,
                    content.value(), std::nullopt, isCjmpFile);
                if (s.fileIds.count(fileID) > 0) {
                    (void)s.ci->diag.DiagnoseRefactor(
                        DiagKindRefactor::module_read_file_conflicted, DEFAULT_POSITION, file);
                    return;
                }

                (void)s.fileIds.insert(fileID);
                fileInfoQueue.emplace(std::move(content.value()), fileID);
            });
        }

        auto package = (s.ci->invocation.globalOptions.GetJobs() == 1)
            ? SingleThreadParseOnePackage(fileInfoQueue, defaultPackageName)
            : MultiThreadParseOnePackage(fileInfoQueue, defaultPackageName);
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
        // just incremental parse if srcPkgs is not empty and type checker is not enabled for lsp completion.
        bool incrParse = ci->loadSrcFilesFromCache && !ci->srcPkgs.empty() && !ci->HasTypeChecker();
        if (incrParse) {
            // Use the new overload for incremental parse
            impl->ParseOnePackage(*ci->srcPkgs[0]);
        } else {
            auto package = impl->ParseOnePackage(ci->srcFilePaths, ret, DEFAULT_PACKAGE_NAME);
            ci->srcPkgs.emplace_back(std::move(package));
        }
    } else {
        impl->ParseModule(ret);
    }
    return ret;
}

bool CompileStrategy::ImportPackages() const
{
    auto ret = ci->ImportPackages();
    ParseAndMergeCjds();
    return ret;
}

namespace {
// All instance objects share, do not clean. The cjd content of the same process should not be inconsistent.
std::unordered_map<std::string, OwnedPtr<Package>> g_cjdAstCache;
std::mutex g_cjdAstCacheLock;
std::mutex g_sourceManageLock;

void ParseAndMergeCjd(Ptr<CompilerInstance> ci, std::pair<const std::string, std::string> cjdInfo)
{
    std::string failedReason;
    auto cjoPath = cjdInfo.second;
    auto sourceCode = FileUtil::ReadFileContent(cjoPath, failedReason);
    if (!failedReason.empty() || !sourceCode.has_value()) {
        // In the LSP scenario, the cjd file path cannot be obtained based on the dependency package information
        // configured in the cache. The cjd file path is searched in searchPath.
        auto searchPath = ci->importManager->GetSearchPath();
        auto cjdPath = FileUtil::FindSerializationFile(cjdInfo.first, CJ_D_FILE_EXTENSION, searchPath);
        if (cjdPath.empty()) {
            return;
        }
        sourceCode = FileUtil::ReadFileContent(cjdPath, failedReason);
        if (!failedReason.empty() || !sourceCode.has_value()) {
            return;
        }
    }

    // Parse
    unsigned int fileId = 0;
    SourceManager& sm = ci->diag.GetSourceManager();
    {
        std::lock_guard<std::mutex> guardOfSm(g_sourceManageLock);
        bool isCjmpFile = ci->invocation.globalOptions.IsCompilingCJMP() ||
            !ci->invocation.globalOptions.commonPartCjos.empty();
        fileId = sm.AddSource(cjoPath, sourceCode.value(), cjdInfo.first, isCjmpFile);
    }
    auto fileAst =
        Parser(fileId, sourceCode.value(), ci->diag, ci->diag.GetSourceManager(), false, true).ParseTopLevel();
    auto pkg = MakeOwned<Package>(cjdInfo.first);
    fileAst->curPackage = pkg.get();
    pkg->files.emplace_back(std::move(fileAst));
    auto originPkg = ci->importManager->GetPackage(cjdInfo.first);
    if (!originPkg) {
        InternalError(cjdInfo.first + " cannot find origin ast");
    }
    MergeCusAnno(originPkg, pkg.get());
    {
        std::lock_guard<std::mutex> guard(g_cjdAstCacheLock);
        g_cjdAstCache[cjdInfo.first] = std::move(pkg);
    }
}
} // namespace

void CompileStrategy::ParseAndMergeCjds() const
{
    auto& option = ci->invocation.globalOptions;
    auto hasLevelFlg = option.passedWhenKeyValue.find("APILevel_level") != option.passedWhenKeyValue.end();
    auto hasSyscapFlg = option.passedWhenKeyValue.find("APILevel_syscap") != option.passedWhenKeyValue.end();
    if (!hasLevelFlg && !hasSyscapFlg) {
        return;
    }
    Utils::ProfileRecorder::Start("ImportPackages", "ParseAndMergeCjds");
    auto cjdInfos = ci->importManager->GetDepPkgCjdPaths();
    std::vector<std::future<void>> futures;
    futures.reserve(cjdInfos.size());
    // Reuse current CompilerInstance, but the Parser in the macro expansion phase uses the DParser.
    option.compileCjd = true;
    // cjdInfos is [fullPackageName, cjdPath].
    for (auto& cjdInfo : cjdInfos) {
        if (option.jobs == 1) {
            ParseAndMergeCjd(ci, cjdInfo);
        } else {
            // In the LSP scenario, concurrent calls may occur.
            std::lock_guard<std::mutex> guard(g_cjdAstCacheLock);
            auto [iter, succ] = g_cjdAstCache.try_emplace(cjdInfo.first, nullptr);
            if (!succ && iter->second) {
                auto originPkg = ci->importManager->GetPackage(cjdInfo.first);
                if (!originPkg) {
                    InternalError(cjdInfo.first + " cannot find origin ast");
                }
                MergeCusAnno(originPkg, iter->second.get());
            } else {
                futures.emplace_back(std::async(std::launch::async, ParseAndMergeCjd, ci, cjdInfo));
            }
        }
    }
    for (auto& future : futures) {
        future.get();
    }
    ci->diag.EmitCategoryGroup();
    option.compileCjd = false;
    Utils::ProfileRecorder::Stop("ImportPackages", "ParseAndMergeCjds");
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
    PerformDesugar();
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
