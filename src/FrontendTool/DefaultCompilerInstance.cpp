// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the DefaultCompilerInstance.
 */

#include "cangjie/FrontendTool/DefaultCompilerInstance.h"

#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Verifier.h"

#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/CodeGen/EmitPackageIR.h"
#include "cangjie/Driver/StdlibMap.h"
#include "cangjie/Driver/TempFileManager.h"
#include "cangjie/Modules/PackageManager.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/ProfileRecorder.h"
#include "cangjie/AST/Walker.h"

#if (defined RELEASE)
#include "cangjie/Utils/Signal.h"
#endif

using namespace Cangjie;
using namespace AST;

namespace Cangjie {
class DefaultCIImpl final {
public:
    explicit DefaultCIImpl(DefaultCompilerInstance& ref) : ci{ref}
    {
    }
    ~DefaultCIImpl();

    bool PerformCodeGen();
    bool PerformCjoAndBchirSaving();
    void DumpDepPackage();
    bool SaveCjoAndBchir(AST::Package& pkg);
    bool SaveCjo(const AST::Package& pkg);
    void RearrangeImportedPackageDependence();
    bool CodegenOnePackage(AST::Package& pkg, bool enableIncrement);

private:
    DefaultCompilerInstance& ci;

    bool EmitLLVMSimilarBytecode(AST::Package& pkg, bool enableIncrement);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    void SaveBchir([[maybe_unused]] const AST::Package& pkg) const
    {
    }
#endif
    std::string GenerateFileName(const std::string& fullPackageName, const std::string& idx) const;
    std::string GenerateBCFilePathAndUpdateToInvocation(
        const TempFileKind& kind, const std::string& pkgName, const std::string& idx = "");

    std::vector<std::unique_ptr<llvm::Module>> llvmModules;
};

DefaultCompilerInstance::DefaultCompilerInstance(CompilerInvocation& invocation, DiagnosticEngine& diag)
    : CompilerInstance(invocation, diag), impl{new DefaultCIImpl{*this}}
{
    buildTrie = false;
}

DefaultCIImpl::~DefaultCIImpl()
{
    CodeGen::ClearPackageModules(llvmModules);
}
DefaultCompilerInstance::~DefaultCompilerInstance()
{
    delete impl;
}

bool DefaultCompilerInstance::PerformParse()
{
    Utils::ProfileRecorder recorder("Main Stage", "Parser");
    return CompilerInstance::PerformParse();
}

bool DefaultCompilerInstance::PerformConditionCompile()
{
    Utils::ProfileRecorder recorder("Main Stage", "ConditionalCompilation");
    return CompilerInstance::PerformConditionCompile();
}

bool DefaultCompilerInstance::PerformImportPackage()
{
    Utils::ProfileRecorder recorder("Main Stage", "ImportPackages");
    return CompilerInstance::PerformImportPackage();
}

bool DefaultCompilerInstance::PerformMacroExpand()
{
    Utils::ProfileRecorder recorder("Main Stage", "MacroExpand");
    return CompilerInstance::PerformMacroExpand();
}

bool DefaultCompilerInstance::PerformSema()
{
    Utils::ProfileRecorder recorder("Main Stage", "Semantic");
    return CompilerInstance::PerformSema();
}

bool DefaultCompilerInstance::PerformOverflowStrategy()
{
    Utils::ProfileRecorder recorder("Main Stage", "Overflow Strategy");
    return CompilerInstance::PerformOverflowStrategy();
}

bool DefaultCompilerInstance::PerformDesugarAfterSema()
{
    Utils::ProfileRecorder recorder("Main Stage", "Desugar after Sema");
    return CompilerInstance::PerformDesugarAfterSema();
}

bool DefaultCompilerInstance::PerformGenericInstantiation()
{
    Utils::ProfileRecorder recorder("Main Stage", "Generic Instantiation");
    return CompilerInstance::PerformGenericInstantiation();
}

bool DefaultCompilerInstance::PerformCHIRCompilation()
{
    Utils::ProfileRecorder recorder("Main Stage", "CHIR");
    return CompilerInstance::PerformCHIRCompilation();
}

std::string DefaultCIImpl::GenerateFileName(const std::string& fullPackageName, const std::string& idx) const
{
    std::string fileName;
    auto pkgNameSuffix = FileUtil::ToCjoFileName(fullPackageName);
    if (ci.invocation.globalOptions.compilePackage) {
        fileName = (idx.empty() ? "" : (idx + "-")) + pkgNameSuffix;
    } else if (fullPackageName != DEFAULT_PACKAGE_NAME) {
        fileName = (idx.empty() ? "" : (idx + "-")) + pkgNameSuffix;
    } else if (ci.invocation.globalOptions.srcFiles.empty()) {
        fileName = (idx.empty() ? "" : (idx + "-")) + pkgNameSuffix;
    } else {
        fileName = (idx.empty() ? "" : (idx + "-")) +
            FileUtil::GetFileNameWithoutExtension(ci.invocation.globalOptions.srcFiles[0]);
    }
    return fileName;
}

std::string DefaultCIImpl::GenerateBCFilePathAndUpdateToInvocation(
    const TempFileKind& kind, const std::string& pkgName, const std::string& idx)
{
    std::string fileName = GenerateFileName(pkgName, idx);
    TempFileInfo bcFileInfo = TempFileManager::Instance().CreateNewFileInfo(TempFileInfo{fileName, "", "", true}, kind);
    ci.invocation.globalOptions.frontendOutputFiles.emplace_back(bcFileInfo);

    auto bcFilePath = bcFileInfo.filePath;
    if (FileUtil::FileExist(bcFilePath) && !FileUtil::Remove(bcFilePath)) {
        Errorln("The file " + bcFilePath + " already exists, but it fails to be removed before being updated.");
        return "";
    }
    if (auto dir{FileUtil::GetDirPath(bcFilePath)}; !FileUtil::FileExist(dir) && FileUtil::CreateDirs(dir + "/") != 0) {
        Errorln("The directory " + dir + " fails to be created before creating " + bcFilePath);
        return "";
    }
    // If file is deleted, no more data written to the file.
    if (TempFileManager::Instance().IsDeleted()) {
        return "";
    }
    return bcFilePath;
}

bool DefaultCIImpl::SaveCjo(const AST::Package& pkg)
{
    if (pkg.IsEmpty()) {
        return true;
    }
    auto pkgName = FileUtil::ToCjoFileName(pkg.fullPackageName);
    // If compiled with the `-g` or '--coverage', files should be saved with absolute paths.
    // When compiling stdlib without options '--coverage', do not save file with abs path.
    // Then can not debugging stdlib with abs path.
    bool saveFileWithAbsPath =
        ci.invocation.globalOptions.enableCompileDebug || ci.invocation.globalOptions.enableCoverage;
    if ((STANDARD_LIBS.find(pkg.fullPackageName) != STANDARD_LIBS.end())) {
        saveFileWithAbsPath = ci.invocation.globalOptions.enableCoverage;
    }
    std::vector<uint8_t> astData;
    Utils::ProfileRecorder::Start("Save cjo and bchir", "Serialize ast");
    ci.importManager.ExportAST(saveFileWithAbsPath, astData, pkg);
    Utils::ProfileRecorder::Stop("Save cjo and bchir", "Serialize ast");
    // Write astData into file according to given package name by '--output' opt.
    TempFileInfo astFileInfo =
        TempFileManager::Instance().CreateNewFileInfo(TempFileInfo{pkgName, ""}, TempFileKind::O_CJO);
    std::string astFileName = astFileInfo.filePath;
    Utils::ProfileRecorder::Start("Save cjo and bchir", "Save ast");
    bool res = FileUtil::WriteBufferToASTFile(astFileName, astData);
    Utils::ProfileRecorder::Stop("Save cjo and bchir", "Save ast");
    if (!res) {
        Errorln("fail to generate file: " + astFileName);
    }
    return res;
}

bool DefaultCIImpl::SaveCjoAndBchir(Package& pkg)
{
    SaveBchir(pkg);
    return SaveCjo(pkg);
}

bool DefaultCIImpl::CodegenOnePackage(Package& pkg, bool enableIncrement)
{
    if (pkg.IsEmpty()) {
        return true;
    }
    if (ci.invocation.globalOptions.disableCodeGen) {
        return true;
    }

    auto backend = ci.invocation.globalOptions.backend;
    switch (backend) {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        case Triple::BackendType::CJNATIVE: {
            if (!EmitLLVMSimilarBytecode(pkg, enableIncrement)) {
                return false;
            }
            break;
        }
#endif
        case Triple::BackendType::UNKNOWN: {
            Errorln("unknown backend");
            break;
        }
        default:
            return false;
    }

#ifdef SIGNAL_TEST
    // The interrupt signal triggers the function. In normal cases, this function does not take effect.
    Cangjie::SignalTest::ExecuteSignalTestCallbackFunc(Cangjie::SignalTest::TriggerPointer::CODEGEN_POINTER);
#endif
    return true;
}
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
bool DefaultCIImpl::EmitLLVMSimilarBytecode(Package& pkg, bool enableIncrement)
{
    // 1. translate CHIR to LLVM IR
    CHIR::CHIRBuilder builder(ci.chirData.GetCHIRContext());
    llvmModules = CodeGen::GenPackageModules(builder, ci.chirData, ci.invocation.globalOptions, ci, enableIncrement);

    // 2. save LLVM IR to bc file
    Utils::ProfileRecorder recorder("CodeGen", "Save bc file");
    ci.invocation.globalOptions.UpdateCachedDirName(pkg.fullPackageName);
    if (llvmModules.size() == 1) {
        auto filePath = GenerateBCFilePathAndUpdateToInvocation(TempFileKind::T_BC, pkg.fullPackageName);
        if (filePath.empty()) {
            return false;
        }
        CodeGen::SavePackageModule(*llvmModules[0], filePath);
    } else {
        Utils::TaskQueue taskQueueSaveBitcode(llvmModules.size());
        std::vector<std::string> allBCFilePath;
        for (size_t i = 0; i < llvmModules.size(); ++i) {
            auto filePath =
                GenerateBCFilePathAndUpdateToInvocation(TempFileKind::T_BC, pkg.fullPackageName, std::to_string(i));
            if (filePath.empty()) {
                return false;
            }
            allBCFilePath.emplace_back(filePath);
        }
        for (size_t i = 0; i < llvmModules.size(); ++i) {
            auto& module = *llvmModules[i];
            auto& bcFilePath = allBCFilePath[i];
            taskQueueSaveBitcode.AddTask<void>(
                [&module, &bcFilePath]() { CodeGen::SavePackageModule(module, bcFilePath); });
        }
        taskQueueSaveBitcode.RunAndWaitForAllTasksCompleted();
    }

    if (ci.invocation.globalOptions.enIncrementalCompilation) {
        auto fileName = GenerateFileName(pkg.fullPackageName, "");
        ci.cachedInfo.bitcodeFilesName = std::vector<std::string>{fileName};
    }
    return true;
}
#endif

bool DefaultCompilerInstance::PerformMangling()
{
    Utils::ProfileRecorder recorder("Main Stage", "Perform Mangling");
    return CompilerInstance::PerformMangling();
}

bool DefaultCIImpl::PerformCodeGen()
{
    Utils::ProfileRecorder recorder("Main Stage", "CodeGen");
    // Before CodeGen, the dependency relationship of a package contains only some packages.
    // So this function rearranges the dependencies of all packages.
    RearrangeImportedPackageDependence();
    bool ret = true;
    for (auto& srcPkg : ci.GetSourcePackages()) {
        ret = ret && CodegenOnePackage(*srcPkg, false);
    }
    return ret;
}

bool DefaultCIImpl::PerformCjoAndBchirSaving()
{
    Utils::ProfileRecorder recorder("Main Stage", "Save cjo and bchir");
    bool ret = true;
    for (auto& srcPkg : ci.GetSourcePackages()) {
        ret = ret && SaveCjoAndBchir(*srcPkg);
    }
    return ret;
}

void DefaultCompilerInstance::DumpDepPackage()
{
    for (auto& depPkgInfo : GetDepPkgInfo()) {
        Println(depPkgInfo);
    }
}

void DefaultCIImpl::RearrangeImportedPackageDependence()
{
    Utils::ProfileRecorder recorder("CodeGen", "RearrangeImportedPackageDependence");
    std::vector<Ptr<Package>> allImportedPackages;
    for (auto pd : ci.importManager.GetAllImportedPackages(true)) {
        CJC_ASSERT(pd && pd->srcPackage);
        allImportedPackages.push_back(pd->srcPackage);
    }
    ci.packageManager->ResolveDependence(allImportedPackages);
}

bool DefaultCompilerInstance::PerformCodeGen()
{
    return impl->PerformCodeGen();
}
bool DefaultCompilerInstance::PerformCjoAndBchirSaving()
{
    return impl->PerformCjoAndBchirSaving();
}
bool DefaultCompilerInstance::SaveCjoAndBchir(AST::Package& pkg) const
{
    return impl->SaveCjoAndBchir(pkg);
}
bool DefaultCompilerInstance::SaveCjo(const AST::Package& pkg) const
{
    return impl->SaveCjo(pkg);
}
void DefaultCompilerInstance::RearrangeImportedPackageDependence() const
{
    return impl->RearrangeImportedPackageDependence();
}
bool DefaultCompilerInstance::CodegenOnePackage(AST::Package& pkg, bool enableIncrement) const
{
    return impl->CodegenOnePackage(pkg, enableIncrement);
}
}
