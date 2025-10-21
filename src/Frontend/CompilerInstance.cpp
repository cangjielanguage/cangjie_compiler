// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the CompilerInstance.
 */

#include "cangjie/Frontend/CompilerInstance.h"

#include <fstream>

#include "PrintSymbolTable.h"

#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/Basic/Version.h"
#include "cangjie/CHIR/CHIR.h"
#include "cangjie/CHIR/CHIRPrinter.h"
#include "cangjie/CHIR/Interpreter/CHIR2BCHIR.h"
#include "cangjie/CHIR/Serializer/CHIRDeserializer.h"
#include "cangjie/CHIR/Serializer/CHIRSerializer.h"
#include "cangjie/CHIR/UserDefinedType.h"
#include "cangjie/Frontend/CompileStrategy.h"
#include "cangjie/IncrementalCompilation/ASTCacheCalculator.h"
#include "cangjie/IncrementalCompilation/IncrementalCompilationLogger.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Modules/ModulesUtils.h"
#include "cangjie/Modules/PackageManager.h"
#include "cangjie/Parse/ASTHasher.h"
#include "cangjie/Sema/Desugar.h"
#include "cangjie/Sema/GenericInstantiationManager.h"
#include "cangjie/Sema/TestManager.h"
#include "cangjie/Sema/TypeChecker.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/ICEUtil.h"
#include "cangjie/Utils/ProfileRecorder.h"
#include "cangjie/Utils/TaskQueue.h"
#include "cangjie/Utils/Utils.h"

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
#include "cangjie/Mangle/CHIRMangler.h"
#include "cangjie/CHIR/Checker/ComputeAnnotations.h"
#endif
#ifdef RELEASE
#include "cangjie/Utils/Signal.h"
#endif

using namespace Cangjie;

CompilerInstance::CompilerInstance(CompilerInvocation& invocation, DiagnosticEngine& diag)
    : invocation(invocation),
      diag(diag),
      packageManager(new PackageManager(importManager)),
      typeManager(new TypeManager()),
      importManager(diag, *typeManager, invocation.globalOptions),
      testManager(new TestManager(importManager, *typeManager, diag, invocation.globalOptions)),
      mangler(std::make_unique<BaseMangler>())
{
    CJC_NULLPTR_CHECK(typeManager);
    CJC_NULLPTR_CHECK(packageManager);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    chirData.InitData(&fileNameMap, invocation.globalOptions.GetJobs());
#endif
    if (invocation.globalOptions.compilePackage) {
        std::copy(invocation.globalOptions.packagePaths.begin(), invocation.globalOptions.packagePaths.end(),
            std::inserter(srcDirs, srcDirs.end()));
        // Check whether compiling one package with multi source files according to srcDirs.
        compileOnePackageFromSrcFiles = srcDirs.empty();
    } else {
        compileOnePackageFromSrcFiles = true;
        srcFilePaths = invocation.globalOptions.srcFiles;
    }
    compileStrategy = new FullCompileStrategy(this);
    CJC_NULLPTR_CHECK(compileStrategy);
    diag.SetSourceManager(&sm);
    AST::ASTHasher::Init(invocation.globalOptions);
}

CompilerInstance::~CompilerInstance()
{
    // AST must be released before ASTContext for correct symbol detaching.
    srcPkgs.clear();
    pkgCtxMap.clear();
    delete compileStrategy;
    compileStrategy = nullptr;
    delete typeChecker;
    typeChecker = nullptr;
    delete typeManager;
    typeManager = nullptr;
    delete testManager;
    testManager = nullptr;
    delete gim;
    gim = nullptr;
    delete packageManager;
    packageManager = nullptr;
}

bool CompilerInstance::InitCompilerInstance()
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    performMap.insert_or_assign(CompileStage::LOAD_PLUGINS, &CompilerInstance::PerformPluginLoad);
#endif
    performMap.insert_or_assign(CompileStage::PARSE, &CompilerInstance::PerformParse);
    performMap.insert_or_assign(CompileStage::CONDITION_COMPILE, &CompilerInstance::PerformConditionCompile);
    performMap.insert_or_assign(CompileStage::IMPORT_PACKAGE, &CompilerInstance::PerformImportPackage);
    performMap.insert_or_assign(CompileStage::MACRO_EXPAND, &CompilerInstance::PerformMacroExpand);
    performMap.insert_or_assign(CompileStage::AST_DIFF, &CompilerInstance::PerformIncrementalScopeAnalysis);
    performMap.insert_or_assign(CompileStage::SEMA, &CompilerInstance::PerformSema);
    performMap.insert_or_assign(CompileStage::DESUGAR_AFTER_SEMA, &CompilerInstance::PerformDesugarAfterSema);
    performMap.insert_or_assign(CompileStage::GENERIC_INSTANTIATION, &CompilerInstance::PerformGenericInstantiation);
    performMap.insert_or_assign(CompileStage::OVERFLOW_STRATEGY, &CompilerInstance::PerformOverflowStrategy);
    performMap.insert_or_assign(CompileStage::MANGLING, &CompilerInstance::PerformMangling);
    performMap.insert_or_assign(CompileStage::CHIR, &CompilerInstance::PerformCHIRCompilation);
    performMap.insert_or_assign(CompileStage::CODEGEN, &CompilerInstance::PerformCodeGen);
    performMap.insert_or_assign(CompileStage::SAVE_RESULTS, &CompilerInstance::PerformCjoAndBchirSaving);
    return true;
}

namespace {
void DumpASTToScreen(const std::vector<Ptr<Package>>& srcPkgs)
{
    if (srcPkgs.empty()) {
        return;
    }
    for (auto& srcPkg : srcPkgs) {
        PrintNode(srcPkg.get());
    }
}
void DumpAST(const std::vector<Ptr<Package>>& srcPkgs, const std::string& outputOfProduct = "",
    const std::string& prefix = "", bool dumpToScreen = false)
{
    if (srcPkgs.empty()) {
        return;
    }
    CJC_ASSERT(srcPkgs.size() == 1);
    const std::string& pkgName = srcPkgs[0]->fullPackageName;
    if (dumpToScreen) {
        DumpASTToScreen(srcPkgs);
        return;
    }
    std::string dumpPath, dumpDir;
    if (FileUtil::IsDir(outputOfProduct)) {
        dumpDir = FileUtil::JoinPath(outputOfProduct, pkgName + "_AST");
    } else {
        dumpDir = FileUtil::GetFileBase(outputOfProduct) + "_AST";
    }
    static bool checkDumpDir = false;
    static size_t fileNum = 0;
    if (!checkDumpDir) {
        if (FileUtil::FileExist(dumpDir)) {
            for (auto file : FileUtil::GetAllFilesUnderCurrentPath(dumpDir, ".txt")) {
                std::string fullPath = FileUtil::JoinPath(dumpDir, file);
                (void)FileUtil::Remove(fullPath);
            }
        }
        checkDumpDir = true;
    }
    dumpPath = FileUtil::JoinPath(dumpDir, std::to_string(fileNum) + "_" + prefix + "_ast" + ".txt");
    if (!FileUtil::FileExist(dumpPath)) {
        FileUtil::CreateDirs(dumpPath);
    }
    std::ofstream ofs(dumpPath);
    if (!ofs) {
        Errorln("Cannot open file to dump AST: " + dumpPath);
        return;
    }
    for (auto& srcPkg : srcPkgs) {
        PrintNode(srcPkg.get(), 0, "", ofs);
    }
    ofs.close();
    fileNum++;
}

bool NeedDumpAST(const GlobalOptions& opts)
{
    return opts.dumpAll || opts.dumpAST;
}

bool NeedDumpASTToFile(const GlobalOptions& opts)
{
    return NeedDumpAST(opts) && !opts.dumpToScreen;
}

bool NeedDumpASTToScreen(const GlobalOptions& opts)
{
    return NeedDumpAST(opts) && opts.dumpToScreen;
}

bool NeedDumpCHIR(const GlobalOptions& opts)
{
    return opts.dumpAll || opts.dumpCHIR;
}

bool NeedDumpCHIRToScreen(const GlobalOptions& opts)
{
    return NeedDumpCHIR(opts) && opts.dumpToScreen;
}
} // namespace

bool CompilerInstance::Compile(CompileStage stage)
{
    if (!InitCompilerInstance()) {
        diag.ReportErrorAndWarningCount();
        return false;
    }
    auto endStageNum = static_cast<int>(stage);
    int i = 0;
    bool success = true;
    for (; i <= endStageNum; i++) {
        Cangjie::ICE::TriggerPointSetter iceSetter(static_cast<CompileStage>(i));
        if (!performMap[static_cast<CompileStage>(i)](this)) {
            success = false;
            break;
        }
    }
    // Dump AST to screen if needed before mangling stage.
    if (NeedDumpASTToScreen(invocation.globalOptions) && i < static_cast<int>(CompileStage::MANGLING)) {
        DumpASTToScreen(GetSourcePackages());
    }
    diag.ReportErrorAndWarningCount();
    return success;
}

static bool IsNeedSaveIncrCompilationLogFile(const GlobalOptions& globalOpts, const FrontendOptions& frontOpts)
{
    if (!globalOpts.enIncrementalCompilation) {
        return false;
    }
    if (globalOpts.scanDepPkg) {
        return false;
    }
    if (frontOpts.dumpAction != FrontendOptions::DumpAction::NO_ACTION) {
        return false;
    }
    if (globalOpts.compileCjd) {
        return false;
    }
    return true;
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
namespace {
class MetaTransformPlugin {
public:
    static MetaTransformPlugin Get(const std::string& path);
    void RegisterCallbackTo(MetaTransformPluginBuilder& mtm) const;

    bool IsValid() const
    {
        return !pluginPath.empty() && metaTransformPluginInfo.cjcVersion == CANGJIE_VERSION &&
            metaTransformPluginInfo.registerTo;
    }

    void* GetHandle() const
    {
        return handle;
    }

private:
    MetaTransformPlugin() = default;
    MetaTransformPlugin(const std::string& pluginPath, const MetaTransformPluginInfo& info, HANDLE handle);

private:
    std::string pluginPath;
    MetaTransformPluginInfo metaTransformPluginInfo;
    HANDLE handle;
};

MetaTransformPlugin::MetaTransformPlugin(
    const std::string& pluginPath, const MetaTransformPluginInfo& info, HANDLE handle)
    : pluginPath(pluginPath), metaTransformPluginInfo(info), handle(handle)
{
}

MetaTransformPlugin MetaTransformPlugin::Get(const std::string& path)
{
    HANDLE handle = nullptr;
#ifdef _WIN32
    handle = InvokeRuntime::OpenSymbolTable(path);
#elif defined(__linux__) || defined(__APPLE__)
    handle = InvokeRuntime::OpenSymbolTable(path, RTLD_NOW | RTLD_LOCAL);
#endif
    if (!handle) {
#ifndef CANGJIE_ENABLE_GCOV
        throw NullPointerException();
#else
        CJC_ABORT();
#endif
    }
    void* fPtr = InvokeRuntime::GetMethod(handle, "getMetaTransformPluginInfo");
    if (!fPtr) {
#ifndef CANGJIE_ENABLE_GCOV
        throw NullPointerException();
#else
        CJC_ABORT();
#endif
    }
    auto pluginInfo = reinterpret_cast<MetaTransformPluginInfo (*)()>(fPtr)();
    return MetaTransformPlugin(path, pluginInfo, handle);
}

void MetaTransformPlugin::RegisterCallbackTo(MetaTransformPluginBuilder& mtm) const
{
    metaTransformPluginInfo.registerTo(mtm);
}
} // namespace

bool CompilerInstance::PerformPluginLoad()
{
    for (auto pluginPath : invocation.globalOptions.pluginPaths) { // loop for all plugins
        try {
            auto metaTransformPlugin = MetaTransformPlugin::Get(pluginPath);
            if (!metaTransformPlugin.IsValid()) {
                diag.DiagnoseRefactor(DiagKindRefactor::not_a_valid_plugin, DEFAULT_POSITION, pluginPath);
            }
            AddPluginHandle(metaTransformPlugin.GetHandle());
            metaTransformPlugin.RegisterCallbackTo(metaTransformPluginBuilder); // register MetaTransform into builder
        } catch (...) {
            diag.DiagnoseRefactor(DiagKindRefactor::not_a_valid_plugin, DEFAULT_POSITION, pluginPath);
            return false;
        }
    }
    return true;
}
#endif

bool CompilerInstance::PerformParse()
{
    auto ret = compileStrategy->Parse();
    if (!srcPkgs.empty()) {
        const auto& globalOpts = invocation.globalOptions;
        srcPkgs.front()->noSubPkg = globalOpts.noSubPkg;
        Utils::ProfileRecorder::SetPackageName(srcPkgs[0]->fullPackageName);
        Utils::ProfileRecorder::SetOutputDir(globalOpts.output);
        if (IsNeedSaveIncrCompilationLogFile(globalOpts, invocation.frontendOptions)) {
            std::string incrLogPath =
                invocation.globalOptions.GenerateCachedPathName(srcPkgs[0]->fullPackageName, CACHED_LOG_EXTENSION);
            IncrementalCompilationLogger::GetInstance().InitLogFile(incrLogPath);
            IncrementalCompilationLogger::GetInstance().WriteBuffToFile();
        }
        if (NeedDumpASTToFile(globalOpts)) {
            DumpAST(GetSourcePackages(), globalOpts.output, "parse");
        }
    }
    return ret;
}

bool CompilerInstance::PerformConditionCompile()
{
    auto ret = compileStrategy->ConditionCompile();
    if (!srcPkgs.empty() && NeedDumpASTToFile(invocation.globalOptions)) {
        DumpAST(GetSourcePackages(), invocation.globalOptions.output, "condcomp");
    }
    return ret;
}

bool CompilerInstance::PerformMacroExpand()
{
    auto ret = compileStrategy->MacroExpand();

    // Constant evaluation and the interpreter needs to load bchir, which requires an AST loader.
    if (!invocation.globalOptions.IsConstEvalEnabled() && !invocation.globalOptions.interpreter) {
        importManager.DeleteASTLoaders();
    }
    importManager.ClearPackageBCHIRCache();

    if (invocation.globalOptions.compileTestsOnly && invocation.globalOptions.enableVerbose) {
        Print("Source files to compile for the test-only mode: {");
        for (auto& pkg : srcPkgs) {
            for (auto& srcFile : pkg->files) {
                Print(pkg->fullPackageName + "::" + srcFile->fileName);
            }
        }
        Println("}");
    }
    if (!srcPkgs.empty() && NeedDumpASTToFile(invocation.globalOptions)) {
        DumpAST(GetSourcePackages(), invocation.globalOptions.output, "macroexp");
    }
    return ret;
}

void CompilerInstance::CacheCompileArgs()
{
    cachedInfo.compileArgs = invocation.globalOptions.ToSerialized();
}

void CompilerInstance::CacheSemaUsage(SemanticInfo&& info)
{
    // Move previous unchanged sema usage info to current info.
    for (auto& [decl, usage] : cachedInfo.semaInfo.usages) {
        if (decl->toBeCompiled) {
            continue;
        }
        auto found = info.usages.find(decl);
        // MainDecl's usage is recorded as desugared.
        if (auto mainDecl = DynamicCast<MainDecl*>(decl); mainDecl && mainDecl->desugarDecl) {
            found = info.usages.find(mainDecl->desugarDecl.get());
        } else if (auto macroDecl = DynamicCast<MacroDecl*>(decl); macroDecl && macroDecl->desugarDecl) {
            found = info.usages.find(macroDecl->desugarDecl.get());
        }
        if (found != info.usages.end()) {
            found->second = std::move(usage);
        }
    }
    // Update cached info.
    cachedInfo.semaInfo = std::move(info);
}

bool CompilerInstance::ShouldWriteCacheFile() const
{
    if (!invocation.globalOptions.enIncrementalCompilation) {
        return false;
    }
    // if set "printVersionOnly" and "showUsage", must not get here
    if (invocation.globalOptions.printVersionOnly || invocation.globalOptions.showUsage) {
        InternalError("'printVersionOnly' or 'showUsage' cannot be true.");
    }
    // if compiled with "cjc-frontend --dump-xxx ...", we don't write cache file
    // that's OK, in this case, we can not run incremental compiling
    if (invocation.frontendOptions.dumpAction != FrontendOptions::DumpAction::NO_ACTION) {
        return false;
    }
    if (invocation.globalOptions.IsEmitCHIREnable()) {
        return false;
    }
    if (invocation.globalOptions.compileCjd) {
        return false;
    }
    if (srcPkgs.size() != 1) {
        InternalError("source packages should only have one element.");
    }
    // if current package is empty package, we don't write cache file
    if (srcPkgs[0]->IsEmpty() || kind == IncreKind::NO_CHANGE) {
        return false;
    }
    return true;
}

namespace {
void UpdateMemberDeclMangleNameForCachedInfo(const RawMangled2DeclMap& rawMangleName2DeclMap, MemberDeclCache& memCache)
{
    if (auto d = rawMangleName2DeclMap.find(memCache.rawMangle); d != rawMangleName2DeclMap.end()) {
        memCache.cgMangle = d->second->mangledName;
    } else {
        CJC_ABORT();
    }
    for (auto &m : memCache.members) {
        UpdateMemberDeclMangleNameForCachedInfo(rawMangleName2DeclMap, m);
    }
}

void UpdateTopLevelDeclMangleNameForCachedInfo(
    const RawMangled2DeclMap& rawMangleName2DeclMap, const std::string& rawMangle, TopLevelDeclCache& topCache)
{
    if (auto d = rawMangleName2DeclMap.find(rawMangle); d != rawMangleName2DeclMap.end()) {
        topCache.cgMangle = d->second->mangledName;
    } else {
        CJC_ABORT();
    }
    for (auto &m : topCache.members) {
        UpdateMemberDeclMangleNameForCachedInfo(rawMangleName2DeclMap, m);
    }
}
}

void CompilerInstance::UpdateMangleNameForCachedInfo()
{
    for (auto &it : cachedInfo.curPkgASTCache) {
        auto rawMangle = it.first;
        UpdateTopLevelDeclMangleNameForCachedInfo(rawMangleName2DeclMap, rawMangle, it.second);
    }
    for (auto &it : cachedInfo.importedASTCache) {
        auto rawMangle = it.first;
        UpdateTopLevelDeclMangleNameForCachedInfo(rawMangleName2DeclMap, rawMangle, it.second);
    }
}

void CompilerInstance::UpdateCachedInfo()
{
    // update mangle name for codegen
    UpdateMangleNameForCachedInfo();
}

bool CompilerInstance::WriteCachedInfo()
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    auto& pkg{*srcPkgs[0]};
    std::string path{invocation.globalOptions.GenerateCachedPathName(pkg.fullPackageName, CACHED_AST_EXTENSION)};
    WriteCache(pkg, std::move(cachedInfo), std::move(order), path);
#endif
    return true;
}

bool CompilerInstance::UpdateAndWriteCachedInfoToDisk()
{
    if (!ShouldWriteCacheFile()) {
        return true;
    }

    UpdateCachedInfo();
    return WriteCachedInfo();
}

bool CompilerInstance::CalculateASTCache()
{
    auto& package = srcPkgs[0];

    // In some case, we will terminate the calculation
    bool terminate = false;
    std::string message;
    if (diag.GetErrorCount() != 0) {
        terminate = true;
        kind = IncreKind::ROLLBACK;
        message = "frontend error detected, roll back to full compilation";
    } else if (package->IsEmpty()) {
        terminate = true;
        kind = IncreKind::EMPTY_PKG;
        message = "empty package, skip";
    }
    if (terminate) {
        IncrementalCompilationLogger::GetInstance().LogLn(message);
        return false;
    }

    // even the incremental compilation is not enabled in current package,
    // we still need to caculate the RawMangleName of the decls in AST,
    // since it will be exported to and used in downstream packages where
    // the incremental compilation is enabled
    auto needCodePosInfo =
        std::make_pair(invocation.globalOptions.enableCompileDebug, invocation.globalOptions.displayLineInfo);
    IncrementalCompilation::ASTCacheCalculator pc{*package, needCodePosInfo};
    pc.Walk();
    rawMangleName2DeclMap = std::move(pc.mangled2Decl);
    astCacheInfo = std::move(pc.ret);
    declsWithDuplicatedRawMangleName = std::move(pc.duplicatedMangleNames);
    directExtends = std::move(pc.directExtends);
    fileMap = std::move(pc.fileMap);
    order = std::move(pc.order);
    return true;
}

bool CompilerInstance::PerformIncrementalScopeAnalysis()
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (invocation.globalOptions.compileCjd) {
        return true;
    }
    Utils::ProfileRecorder recorder("Main Stage", "AST Diff");
    CalculateASTCache();
#endif
    return true;
}

bool CompilerInstance::PerformImportPackage()
{
    auto ret = compileStrategy->ImportPackages();
    if (!srcPkgs.empty() && NeedDumpASTToFile(invocation.globalOptions)) {
        DumpAST(GetSourcePackages(), invocation.globalOptions.output, "import");
    }
    return ret;
}

bool CompilerInstance::PerformSema()
{
    auto ret = compileStrategy->Sema();
    if (!srcPkgs.empty() && NeedDumpASTToFile(invocation.globalOptions)) {
        DumpAST(GetSourcePackages(), invocation.globalOptions.output, "sema");
    }
    return ret;
}

bool CompilerInstance::PerformOverflowStrategy()
{
    if (invocation.globalOptions.overflowStrategy == OverflowStrategy::NA) {
        return true;
    }
    compileStrategy->OverflowStrategy();
    if (!srcPkgs.empty() && NeedDumpASTToFile(invocation.globalOptions)) {
        DumpAST(GetSourcePackages(), invocation.globalOptions.output, "overflow");
    }
    return true;
}

bool CompilerInstance::PerformDesugarAfterSema()
{
    testManager->MarkDeclsForTestIfNeeded(GetSourcePackages());
    compileStrategy->DesugarAfterSema();
    if (!srcPkgs.empty() && NeedDumpASTToFile(invocation.globalOptions)) {
        DumpAST(GetSourcePackages(), invocation.globalOptions.output, "desugar");
    }
    return true;
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
bool CompilerInstance::PerformGenericInstantiation()
{
    if (!importManager.IsSourceCodeImported()) {
        InternalError("Generic instantiation should not be performed when imported source code is not reparsed.");
        return false;
    }
    if (gim == nullptr) {
        gim = new GenericInstantiationManager(*this);
        CJC_NULLPTR_CHECK(gim);
        testManager->Init(gim);
    }
    if (!invocation.globalOptions.enIncrementalCompilation) {
        Utils::ProfileRecorder::Start("Generic Instantiation", "ResetGenericInstantiationStage");
        // Perform instantiation for source package. NOTE: must only exist exact one source package except unittest.
        gim->ResetGenericInstantiationStage();
        Utils::ProfileRecorder::Stop("Generic Instantiation", "ResetGenericInstantiationStage");
        Utils::ProfileRecorder::Start("Generic Instantiation", "GenericInstantiatePackage");
        for (auto& srcPkg : srcPkgs) {
            gim->GenericInstantiatePackage(*srcPkg);
            // Avoid enter post-desugar process if error occurs in generic instantiation.
            if (diag.GetErrorCount() != 0) {
                return false;
            }
        }
        Utils::ProfileRecorder::Stop("Generic Instantiation", "GenericInstantiatePackage");
    }
    Utils::ProfileRecorder recorder("Generic Instantiation", "desugar after instantiation");
    // Perform desugar after generic instantiation.
    // NOTE: This step will not generate any error.
    for (auto& srcPkg : srcPkgs) {
        auto astCtx = GetASTContextByPackage(srcPkg.get());
        CJC_ASSERT(astCtx);
        typeChecker->PerformDesugarAfterInstantiation(*astCtx, *srcPkg);
    }
    if (!srcPkgs.empty() && NeedDumpASTToFile(invocation.globalOptions)) {
        DumpAST(GetSourcePackages(), invocation.globalOptions.output, "genericinst");
    }
    return true;
}
#endif

namespace {
using DeclAndPackageName = std::pair<AST::Decl*, std::string>;
using LambdaAndPackageName = std::pair<AST::LambdaExpr*, std::string>;

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
void DoNewMangling(
    const BaseMangler& baseMangler, const std::vector<DeclAndPackageName>& decls, size_t start, size_t end)
{
    std::vector<Ptr<Node>> prefix;
    auto handleMangle = [&baseMangler, &prefix](Ptr<Node> node) -> VisitAction {
        std::vector<Ptr<Node>> filteredPrefix;
        for (size_t i = 0; i < prefix.size(); i++) {
            if (filteredPrefix.size() != 0 && Is<Expr>(filteredPrefix.back().get()) &&
                static_cast<AST::Expr*>(filteredPrefix.back().get())->desugarExpr.get() == prefix[i].get()) {
                filteredPrefix.pop_back();
            }
            filteredPrefix.emplace_back(prefix[i]);
        }
        auto nextAction = Meta::match(*node)(
            [&baseMangler, &filteredPrefix](Decl& decl) {
                if (decl.astKind == ASTKind::MAIN_DECL) {
                    return VisitAction::WALK_CHILDREN;
                }
                if (decl.astKind == ASTKind::MACRO_DECL) {
                    auto& desugar = *StaticCast<MacroDecl*>(&decl)->desugarDecl;
                    desugar.mangledName = baseMangler.Mangle(desugar);
                    return VisitAction::WALK_CHILDREN;
                }
                if (!Ty::IsTyCorrect(decl.ty)) {
                    return VisitAction::SKIP_CHILDREN;
                }
                decl.mangledName = baseMangler.Mangle(decl, filteredPrefix);
                return VisitAction::WALK_CHILDREN;
            },
            [&baseMangler, &filteredPrefix](LambdaExpr& lambda) {
                if (lambda.TestAttr(Attribute::GENERIC) || !Ty::IsTyCorrect(lambda.ty)) {
                    return VisitAction::SKIP_CHILDREN;
                }
                lambda.mangledName = baseMangler.MangleLambda(lambda,
                    filteredPrefix);
                return VisitAction::WALK_CHILDREN;
            },
            []([[maybe_unused]] const Annotation& anno) {
                // The annotation node should be desugared during the Sema, this node is only used in ExportAST.
                return VisitAction::SKIP_CHILDREN;
            },
            []([[maybe_unused]] const Node& node) { return VisitAction::WALK_CHILDREN; },
            []() { return VisitAction::WALK_CHILDREN; });
        prefix.emplace_back(node);
        return nextAction;
    };
    auto cleanUpNode = [&prefix]([[maybe_unused]] const Ptr<Node>& node) -> VisitAction {
        prefix.pop_back();
        return VisitAction::KEEP_DECISION;
    };
    for (size_t idx = start; idx < end; ++idx) {
        auto& decl = decls[idx];
        Walker(decl.first, handleMangle, cleanUpNode).Walk();
    }
}
#endif

/**
 * For all top-level declarations in @p topDecls, split them into tasks in
 * such a way that each task processes 30 decls serially.
 * Use a TaskQueue with a concurrency of parallelNum to execute these tasks.
 * For each decl in a task, the walker is used to mangle the decl and the
 * internal nodes that need to be mangled. When lambda expression nodes are
 * encountered, they are collected because of the particularity of their
 * mangle rules (unsuitable for concurrency).
 * When all tasks are complete, mangle the lambda expression nodes collected
 * in serial mode.
 */
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
void DoMangling(const BaseMangler& baseMangler, size_t parallelNum, const std::vector<DeclAndPackageName>& topDecls)
{
    // when the 'jobs' is 1, we disable the parallel of mangle to simplify the debug
    if (parallelNum == 1) {
        DoNewMangling(baseMangler, topDecls, 0, topDecls.size());
    } else {
        // Mangle 30 decls serially in a task.
        constexpr size_t batchSize = 30U;
        auto tasksNum = topDecls.size() / batchSize;
        size_t start = 0;
        size_t end = batchSize;
        std::unordered_map<int, std::vector<LambdaAndPackageName>> lambdasCollectedByTask;
        // Creating a Concurrent Task Queue
        Utils::TaskQueue taskQueue(parallelNum);
        for (size_t i = 0; i < tasksNum; ++i) {
            taskQueue.AddTask<void>(
                [&baseMangler, &topDecls, start, end]() { DoNewMangling(baseMangler, topDecls, start, end); });
            start += batchSize;
            end += batchSize;
        }
        // Concurrent processing of grouped decls
        taskQueue.RunInBackground();
        // Wait until all mangle tasks are complete.
        taskQueue.WaitForAllTasksCompleted();
        DoNewMangling(baseMangler, topDecls, start, topDecls.size());
    }
}

void SortForBep(Package& pkg)
{
    std::unordered_map<Ptr<Decl>, std::string> declMangleMap;
    auto compare = [&declMangleMap](const Ptr<Decl> d1, const Ptr<Decl> d2) {
        const std::string& mangle1 = declMangleMap[d1];
        const std::string& mangle2 = declMangleMap[d2];
        if (mangle1 == mangle2) {
            return CompNodeByPos(d1, d2);
        }
        return mangle1 < mangle2;
    };
    // Reorder genericInstantiatedDecls for bep.
    std::set<Ptr<Decl>, decltype(compare)> orderedDecls(compare);
    std::for_each(pkg.genericInstantiatedDecls.begin(), pkg.genericInstantiatedDecls.end(),
        [&orderedDecls, &declMangleMap](auto& it) {
            BaseMangler mangler;
            declMangleMap.emplace(it.get(), mangler.Mangle(*it));
            orderedDecls.emplace(it.release());
        });
    pkg.genericInstantiatedDecls.clear();
    std::for_each(orderedDecls.cbegin(), orderedDecls.cend(),
        [&pkg](auto it) { pkg.genericInstantiatedDecls.emplace_back(OwnedPtr<Decl>(it)); });
}
#endif
} // namespace

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
void CompilerInstance::ManglingHelpFunction(const BaseMangler& baseMangler)
#endif
{
    // Collect all top-level decls
    std::vector<DeclAndPackageName> topDecls;
    auto deduplicatedEmplace = [&topDecls](AST::Decl* decl, std::string pkgName) {
        if (std::find(topDecls.begin(), topDecls.end(), std::make_pair(decl, pkgName)) == topDecls.end()) {
            topDecls.emplace_back(decl, pkgName);
        }
    };

    for (auto& package : GetPackages()) {
        auto fullPackageName = package->fullPackageName;
        Walker(package, [&fullPackageName](Ptr<Node> node) {
            if (auto vd = DynamicCast<VarDecl>(node); vd && vd->fullPackageName.empty()) {
                vd->fullPackageName = fullPackageName;
                return VisitAction::SKIP_CHILDREN;
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
        for (auto& file : package->files) {
            for (auto& decl : file->decls) {
                deduplicatedEmplace(decl.get(), package->fullPackageName);
            }
        }
        for (auto& decl : package->genericInstantiatedDecls) {
            deduplicatedEmplace(decl.get(), package->fullPackageName);
        }
    }
    if (invocation.globalOptions.disableInstantiation) {
        for (auto& importPkg : importManager.GetAllImportedPackages()) {
            CJC_NULLPTR_CHECK(importPkg->srcPackage.get());
            // exclude current package
            if (!importPkg->srcPackage->TestAttr(AST::Attribute::IMPORTED)) {
                continue;
            }
            for (auto& file : importPkg->srcPackage->files) {
                for (auto& decl : file->decls) {
                    deduplicatedEmplace(decl.get(), importPkg->fullPackageName);
                }
                for (auto& decl : file->exportedInternalDecls) {
                    deduplicatedEmplace(decl.get(), importPkg->fullPackageName);
                }
            }
            for (auto& decl : importPkg->srcPackage->genericInstantiatedDecls) {
                if (decl->IsNominalDecl()) {
                    deduplicatedEmplace(decl.get(), importPkg->fullPackageName);
                }
            }
        }
    }

    DoMangling(baseMangler, invocation.globalOptions.GetJobs(), topDecls);
}

bool CompilerInstance::PerformMangling()
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    using namespace CHIR;
    mangler = CreateUniquePtr<CHIR::CHIRMangler>(invocation.globalOptions.enableCompileTest);

    // Load the manglerCtxTable
    std::vector<std::unique_ptr<ManglerContext>> manglerCtxVec;

    // Get all imported packages and source packages.
    for (auto& package : importManager.GetAllImportedPackages()) {
        std::string pkgName = ManglerContext::ReduceUnitTestPackageName(package->fullPackageName);
        if (mangler->manglerCtxTable.find(pkgName) == mangler->manglerCtxTable.end()) {
            auto manglerCtx = std::make_unique<ManglerContext>();
            mangler->manglerCtxTable[pkgName] = manglerCtx.get();
            manglerCtxVec.emplace_back(std::move(manglerCtx));
        }
        mangler->CollectVarOrLambda(*mangler->manglerCtxTable.at(pkgName), *package->srcPackage);
    }
#endif
    mangler->lambdaCounter = cachedInfo.lambdaCounter;
    ManglingHelpFunction(*mangler);
    cachedInfo.lambdaCounter = mangler->lambdaCounter;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (!invocation.globalOptions.disableInstantiation) {
        for (auto& package : GetSourcePackages()) {
            SortForBep(*package);
        }
    }
#endif
    // when dump to screen, only dump once and only dump the ast of mangle stage
    if (!srcPkgs.empty() && NeedDumpAST(invocation.globalOptions)) {
        DumpAST(GetSourcePackages(), invocation.globalOptions.output, "mangle", invocation.globalOptions.dumpToScreen);
    }
    return true;
}

bool CompilerInstance::GenerateCHIRForPkg(AST::Package& pkg)
{
    if (pkg.files.empty()) {
        return true;
    }

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    // use this result when APILevel check supports arbitrary const expressions
    (void)CHIR::ComputeAnnotations(pkg, *this);
    auto& constAnalysisWrapper = chirData.GetConstAnalysisResultRef();
#endif
    CHIR::CHIRBuilder builder1(GetCHIRContext(), invocation.globalOptions.GetJobs());
    CHIR::ToCHIR convertor(*this, pkg, constAnalysisWrapper, builder1);
    bool success = convertor.Run();
    auto chirPkg = convertor.GetPackage();
    if (chirPkg && NeedDumpCHIRToScreen(invocation.globalOptions)) {
        CHIR::CHIRPrinter::PrintPackage(*chirPkg, std::cout);
    }
    if (!success) {
        return false;
    }
    CJC_NULLPTR_CHECK(chirPkg);
    astPkg2chirPkgMap.emplace(&pkg, chirPkg);
    chirInfo.optEffectMap = convertor.GetOptEffectMap();
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    chirData.AppendNewPackage(chirPkg);
    chirData.SetImplicitFuncs(convertor.GetImplicitFuncs());
    chirData.SetConstVarInitFuncs(convertor.GetConstVarInitFuncs());
    chirInfo.curVirtFuncWrapDep = convertor.GetCurVirtualFuncWrapperDepForIncr();
    chirInfo.delVirtFuncWrapForIncr = convertor.GetDeleteVirtualFuncWrapperForIncr();
    chirInfo.ccOutFuncsRawMangle = convertor.GetCCOutFuncsRawMangle();
    chirInfo.varInitDepMap = convertor.GetVarInitDepMap();
#endif
#ifdef SIGNAL_TEST
    // The interrupt signal triggers the function. In normal cases, this function does not take effect.
    Cangjie::SignalTest::ExecuteSignalTestCallbackFunc(Cangjie::SignalTest::TriggerPointer::CHIR_POINTER);
#endif
    return true;
}

bool CompilerInstance::PerformCHIRCompilation()
{
    bool ret = true;
    auto sourcePackagesCHIR = GetSourcePackages();
    auto pkgsOrderedCHIR = packageManager->GetBuildOrders();
    for (auto& pkgsInfos : pkgsOrderedCHIR) {
        auto isSrcPkg = [&pkgsInfos](
                            Ptr<AST::Package> pkg) { return pkg->fullPackageName == pkgsInfos[0]->fullPackageName; };
        auto pkg = std::find_if(sourcePackagesCHIR.begin(), sourcePackagesCHIR.end(), isSrcPkg);
        if (pkg != sourcePackagesCHIR.end()) {
            ret = ret && GenerateCHIRForPkg(**pkg);
            sourcePackagesCHIR.erase(pkg);
        }
    };
    for (auto& remainingSourcePackage : sourcePackagesCHIR) {
        ret = ret && GenerateCHIRForPkg(*remainingSourcePackage);
    }
    diag.EmitCategoryDiagnostics(DiagCategory::CHIR);
    return ret;
}

bool CompilerInstance::DumpTokens()
{
    bool ret = true;
    std::for_each(srcFilePaths.begin(), srcFilePaths.end(), [this, &ret](auto filePath) {
        std::string failedReason;
        auto content = FileUtil::ReadFileContent(filePath, failedReason);
        if (!content.has_value()) {
            diag.DiagnoseRefactor(
                DiagKindRefactor::module_read_file_to_buffer_failed, DEFAULT_POSITION, filePath, failedReason);
            ret = false;
            return;
        }
        const unsigned int fileID =
            GetSourceManager().AddSource(FileUtil::GetAbsPath(filePath) | FileUtil::IdenticalFunc, content.value());
        Lexer lexer(fileID, content.value(), diag, GetSourceManager());
        for (;;) {
            Token tok = lexer.Next();
            if (tok.kind == TokenKind::END) {
                break;
            }
            std::string position = "<" + filePath + ":" + std::to_string(tok.Begin().line) + ":" +
                std::to_string(tok.Begin().column) + ">";
            if (tok.kind == TokenKind::COMMENT) {
                Println(TOKEN_KIND_VALUES[static_cast<int>(tok.kind)], position);
            } else if (tok.kind == TokenKind::NL) {
                Println(tok == "\n" ? "\\n" : "\\r\\n", TOKEN_KIND_VALUES[static_cast<int>(tok.kind)], position);
            } else {
                Println(tok.Value(), TOKEN_KIND_VALUES[static_cast<int>(tok.kind)], position);
            }
        }
    });

    return ret;
}

void CompilerInstance::DumpSymbols()
{
    PrintSymbolTable(*this);
}

void CompilerInstance::DumpMacro()
{
    Println("Dump tokens after macro expansion.");
    Println("==== Start Dumping ====\n");
    for (auto ts : tokensEvalInMacro) {
        Println(ts);
    }
}

SourceManager& CompilerInstance::GetSourceManager()
{
    return sm;
}

std::vector<Ptr<Package>> CompilerInstance::GetSourcePackages()
{
    std::vector<Ptr<Package>> packages;
    for (auto& srcPkg : srcPkgs) {
        packages.push_back(srcPkg.get());
    }
    return packages;
}

ASTContext* CompilerInstance::GetASTContextByPackage(Ptr<Package> pkg) const
{
    if (!pkg) {
        return nullptr;
    }
    auto found = pkgCtxMap.find(pkg);
    return found != pkgCtxMap.end() ? found->second.get() : nullptr;
}

void CompilerInstance::AddSourceToMember()
{
    if (!pkgs.empty()) {
        return;
    }
    for (auto& it : GetSourcePackages()) {
        pkgs.push_back(it);
    }
}

bool CompilerInstance::ImportPackages()
{
    // If CANGJIE_HOME not set, detect from executablePath.
    if (cangjieHome.empty()) {
        if (!DetectCangjieHome()) {
            return false;
        }
    }
    if (cangjieModules.empty()) {
        if (!DetectCangjieModules()) {
            return false;
        }
    }
    AddSourceToMember();

    if (invocation.globalOptions.scanDepPkg) {
        if (!invocation.globalOptions.inputCjoFile.empty()) {
            depPackageInfo = importManager.GeneratePkgDepInfoByCjo(invocation.globalOptions.inputCjoFile);
        } else {
            depPackageInfo = importManager.GeneratePkgDepInfo(pkgs);
        }
        return true;
    }

    if (!importManager.BuildIndex(cangjieModules, invocation.globalOptions, pkgs)) {
        return false;
    }

    MergePackages();
    ModularizeCompilation();
    return true;
}

void CompilerInstance::MergePackages()
{
    for (auto& pkg : srcPkgs) {
        pkgCtxMap.insert_or_assign(pkg.get(), std::make_unique<ASTContext>(diag, *pkg));
    }
}

std::set<Ptr<ExtendDecl>> CompilerInstance::GetExtendDecls(
    const std::variant<Ptr<Ty>, Ptr<InheritableDecl>>& type) const
{
    if (!typeManager) {
        return {};
    }
    if (type.index() == 0) {
        if (auto ty = std::get<Ptr<Ty>>(type)) {
            return typeManager->GetAllExtendsByTy(*ty);
        }
    } else if (type.index() == 1) {
        if (auto decl = std::get<Ptr<InheritableDecl>>(type)) {
            return typeManager->GetDeclExtends(*decl);
        }
    }
    return {};
}

std::vector<Ptr<Decl>> CompilerInstance::GetAllVisibleExtendMembers(
    const std::variant<Ptr<Ty>, Ptr<InheritableDecl>>& type, const File& curFile) const
{
    std::set<Ptr<ExtendDecl>> extends = GetExtendDecls(type);
    std::vector<Ptr<Decl>> members;
    Ptr<Ty> exprTy = nullptr;
    if (type.index() == 0) {
        exprTy = std::get<Ptr<Ty>>(type);
    } else if (type.index() == 1) {
        exprTy = std::get<Ptr<InheritableDecl>>(type)->ty;
    }
    if (!Ty::IsTyCorrect(exprTy)) {
        return {};
    }
    for (auto& e : extends) {
        if (!importManager.IsExtendAccessible(curFile, *e)) {
            continue;
        }
        auto& extendMember = e->GetMemberDecls();
        if (curFile.curPackage->fullPackageName == e->fullPackageName) {
            members.insert(members.end(), extendMember.begin(), extendMember.end());
        } else {
            for (auto& m : extendMember) {
                if (importManager.IsExtendMemberAccessible(curFile, *m, *exprTy)) {
                    members.emplace_back(m.get());
                }
            }
        }
        std::function<void(const InheritableDecl&)> collectInheritMember = [&collectInheritMember, &members](
                                                                               const InheritableDecl& id) {
            for (auto& super : id.inheritedTypes) {
                auto targetDecl = DynamicCast<InheritableDecl>(super->GetTarget());
                if (targetDecl == nullptr) {
                    continue;
                }
                collectInheritMember(*targetDecl);
            }
            if (id.astKind != ASTKind::INTERFACE_DECL) {
                return;
            }
            auto& inherMembers = id.GetMemberDecls();
            std::for_each(inherMembers.begin(), inherMembers.end(), [&members](auto& m) {
                if (!m->TestAttr(Attribute::ABSTRACT)) {
                    members.emplace_back(m);
                }
            });
        };
        collectInheritMember(*e);
        typeChecker->RemoveTargetNotMeetExtendConstraint(exprTy, members);
    }
    return members;
}

Candidate CompilerInstance::GetGivenReferenceTarget(
    ASTContext& ctx, const std::string& scopeName, Expr& expr, bool hasLocalDecl) const
{
    if (!typeChecker) {
        return {};
    }
    return typeChecker->SynReferenceSeparately(ctx, scopeName, expr, hasLocalDecl);
}

bool CompilerInstance::DetectCangjieHome()
{
    if (invocation.globalOptions.environment.cangjieHome.has_value()) {
        cangjieHome = invocation.globalOptions.environment.cangjieHome.value();
        return true;
    }
    // Detect from exepath.
    if (invocation.globalOptions.executablePath.empty()) {
        diag.DiagnoseRefactor(DiagKindRefactor::frontend_failed_to_detect_cangjie_home,
            DEFAULT_POSITION, "can not resolve executable path");
        return false;
    } else {
        cangjieHome =
            FileUtil::JoinPath(FileUtil::GetDirPath(FileUtil::GetAbsPath(invocation.globalOptions.executablePath) |
                                   FileUtil::IdenticalFunc),
                "..");
        if (!FileUtil::FileExist(cangjieHome) || !FileUtil::FileExist(FileUtil::JoinPath(cangjieHome, "modules"))) {
            diag.DiagnoseRefactor(
                DiagKindRefactor::frontend_failed_to_detect_cangjie_home, DEFAULT_POSITION, "invalid cjc home");
            return false;
        }
    }
    return true;
}

bool CompilerInstance::DetectCangjieModules()
{
    if (cangjieHome.empty()) {
        diag.DiagnoseRefactor(DiagKindRefactor::frontend_failed_to_detect_cangjie_modules,
            MakeRange(DEFAULT_POSITION, DEFAULT_POSITION), "cangjie home is empty");
        return false;
    }
    auto libPathName = invocation.globalOptions.GetCangjieLibTargetPathName();
    auto modulesName = FileUtil::JoinPath(cangjieHome, "modules");
    if (libPathName.empty()) {
        diag.DiagnoseRefactor(DiagKindRefactor::frontend_failed_to_detect_cangjie_modules,
            MakeRange(DEFAULT_POSITION, DEFAULT_POSITION), "target library path name is empty");
        return false;
    }
    cangjieModules = FileUtil::JoinPath(modulesName, libPathName);
    if (!FileUtil::FileExist(cangjieModules) && (invocation.globalOptions.target.env == Triple::Environment::ANDROID)) {
        auto candidateNames = FileUtil::GetAllDirsUnderCurrentPath(modulesName);
        auto tripleLen = Utils::SplitString(libPathName, "_").size();
        for (auto & candidateName: candidateNames){
            candidateName = FileUtil::GetDirName(candidateName);
            auto parts = Utils::SplitString(candidateName, "_");
            if(parts.size() == tripleLen && Utils::StartsWith(parts[1], "android") 
            && invocation.globalOptions.target.ArchToString() == parts[2]){
                cangjieModules = FileUtil::JoinPath(modulesName, candidateName);
            }
        }
    }
    if (!FileUtil::FileExist(cangjieModules)) {
        diag.DiagnoseRefactor(DiagKindRefactor::frontend_failed_to_detect_cangjie_modules,
            MakeRange(DEFAULT_POSITION, DEFAULT_POSITION), "target library path is not exist : " + cangjieModules);
        return false;
    }
    return true;
}

bool CompilerInstance::ModularizeCompilation()
{
    for (auto& objFile : importManager.GetUsedSTDLibFiles(DepType::DIRECT)) {
        invocation.globalOptions.directBuiltinDependencies.insert(objFile);
    }
    for (auto& objFile : importManager.GetUsedSTDLibFiles(DepType::INDIRECT)) {
        invocation.globalOptions.indirectBuiltinDependencies.insert(objFile);
    }
    return packageManager->ResolveDependence(pkgs);
}

bool CompilerInstance::DeserializeCHIR()
{
    CHIR::CHIRBuilder chirBuilder(GetCHIRContext());
    CHIR::ToCHIR::Phase phase;
    CHIR::CHIRDeserializer::Deserialize(invocation.frontendOptions.chirDeserializePath, chirBuilder, phase);
    // print serialize extension info which just for serialization not necessary for chir nodes
    CHIR::CHIRPrinter::PrintCHIRSerializeInfo(phase, "deserialized.chir");
    CHIR::CHIRPrinter::PrintPackage(*GetCHIRContext().GetCurPackage(), "deserialized.chir");
    return true;
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
CHIR::CHIRContext& CompilerInstance::GetCHIRContext()
{
    return chirData.GetCHIRContext();
}

// used only by cjlint
const CHIR::ConstAnalysisWrapper& CompilerInstance::GetConstAnalysisWrapper() const
{
    return chirData.GetConstAnalysisResult();
}

std::vector<CHIR::Package*> CompilerInstance::GetAllCHIRPackages() const
{
    return chirData.GetAllCHIRPackages();
}

void CHIRData::InitData(std::unordered_map<unsigned int, std::string>* fileNameMap, size_t threadNum)
{
    cctx.SetFileNameMap(fileNameMap);
    cctx.SetThreadNum(threadNum);
}

CHIR::CHIRContext& CHIRData::GetCHIRContext()
{
    return cctx;
}

void CHIRData::AppendNewPackage(CHIR::Package* package)
{
    chirPkgs.emplace_back(package);
}

std::vector<CHIR::Package*> CHIRData::GetAllCHIRPackages() const
{
    return chirPkgs;
}

CHIR::Package* CHIRData::GetCurrentCHIRPackage() const
{
    if (chirPkgs.empty()) {
        return nullptr;
    }
    return chirPkgs[0];
}

void CHIRData::SetImplicitFuncs(const std::unordered_map<std::string, CHIR::FuncBase*>& funcs)
{
    implicitFuncs = funcs;
}

std::unordered_map<std::string, CHIR::FuncBase*> CHIRData::GetImplicitFuncs() const
{
    return implicitFuncs;
}

void CHIRData::SetConstVarInitFuncs(const std::vector<CHIR::FuncBase*>& funcs)
{
    initFuncsForConstVar = funcs;
}

std::vector<CHIR::FuncBase*> CHIRData::GetConstVarInitFuncs() const
{
    return initFuncsForConstVar;
}

CHIR::ConstAnalysisWrapper& CHIRData::GetConstAnalysisResultRef()
{
    return constAnalysisWrapper;
}

const CHIR::ConstAnalysisWrapper& CHIRData::GetConstAnalysisResult() const
{
    return constAnalysisWrapper;
}
#endif
