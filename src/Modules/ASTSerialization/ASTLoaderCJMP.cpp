// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 * This file implements CJMP (Common JavaScript/Multi-platform) related loading functions.
 */

#include "ASTLoaderCJMP.h"

#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/DiagnosticEngine.h"

#include "ASTLoaderImpl.h"
#include "cangjie/Basic/Position.h"
#include "cangjie/Modules/ASTSerialization.h"
#include "cangjie/Option/Option.h"
#include "flatbuffers/CjoFormat_generated.h"

namespace Cangjie {

namespace ASTLoaderCJMP {

OwnedPtr<AST::FeaturesDirective> LoadFeaturesDirective(const PackageFormat::FeaturesDirective* raw)
{
    auto ftrDirective = MakeOwned<AST::FeaturesDirective>();
    ftrDirective->featuresSet = MakeOwned<AST::FeaturesSet>();

    for (uoffset_t i = 0; i < raw->featuresSet()->features()->size(); i++) {
        auto rawFeature = raw->featuresSet()->features()->Get(i);
        auto feature = AST::FeatureId();
        for (uoffset_t j = 0; j < rawFeature->identifiers()->size(); j++) {
            auto identifier = rawFeature->identifiers()->Get(j);
            feature.identifiers.push_back(SrcIdentifier(identifier->str()));
        }

        ftrDirective->featuresSet->content.push_back(feature);
    }

    return ftrDirective;
}

void ReportPackageMismatch(
    DiagnosticEngine& diag, AST::Package& node, std::string& expectedPackageName, std::string& actualPackageName)
{
    Range range = MakeRange(node.GetBegin(), node.GetEnd());
    if (range.HasZero()) {
        CJC_ASSERT_WITH_MSG(!node.files.empty(), "There are no files in the current package");
        auto& file = *node.files[0];
        range = MakeRange(file.GetBegin(), file.GetEnd());
    }

    diag.DiagnoseRefactor(
        DiagKindRefactor::module_common_cjo_wrong_package, range, actualPackageName, expectedPackageName);
}

std::set<std::string> CollectFeaturesFromPackage(AST::Package& pkg)
{
    return (*pkg.files.begin())->GetFeatures();
}

void ValidateCommonSpecificFeatureSetsRelations(
    const AST::File& commonFile,
    const std::set<std::string>& specificFeatures,
    DiagnosticEngine& diag,
    const AST::Package& pkg)
{
    auto parentFeatures = commonFile.GetFeatures();
    bool isParentSubsetOfChild = std::includes(
        specificFeatures.begin(), specificFeatures.end(),
        parentFeatures.begin(), parentFeatures.end());
    if (!isParentSubsetOfChild) {
        std::vector<std::string> difference;
        std::set_difference(
            parentFeatures.begin(), parentFeatures.end(),
            specificFeatures.begin(), specificFeatures.end(),
            std::back_inserter(difference)
        );
        std::stringstream ss;
        ss << "Extra feature from common part file: ";
        for (const auto& feature : difference) {
            ss << "'" << feature << "' ";
        }
        auto diagBuilder = diag.DiagnoseRefactor(
            DiagKindRefactor::feature_is_not_subset_of_child_set,
            commonFile.GetBegin(), commonFile.GetFullPackageName(), commonFile.filePath
        );
        diagBuilder.AddNote(ss.str());
        Ptr<AST::File> childFile = *pkg.files.begin();
        if (childFile->feature) {
            diagBuilder.AddNote(*childFile->feature, "Conflicting specific feature set:");
        }
    }
}

/**
 * @brief Convert optimization level enum to string for diagnostics
 * @param level The optimization level to convert
 * @return String representation of the optimization level
 */
std::string OptimizationLevelToString(GlobalOptions::OptimizationLevel level)
{
    switch (level) {
        case GlobalOptions::OptimizationLevel::O0:
            return "O0";
        case GlobalOptions::OptimizationLevel::O1:
            return "O1";
        case GlobalOptions::OptimizationLevel::O2:
            return "O2";
        case GlobalOptions::OptimizationLevel::O3:
            return "O3";
        case GlobalOptions::OptimizationLevel::Os:
            return "Os";
        case GlobalOptions::OptimizationLevel::Oz:
            return "Oz";
        default:
            return "(unsupported)";
    }
}

} // namespace ASTLoaderCJMP

OwnedPtr<AST::File> ASTLoader::ASTLoaderImpl::PreloadCommonFile(uoffset_t indexOfFile)
{
    CJC_NULLPTR_CHECK(package->allFileImports());
    uoffset_t i = indexOfFile;
    auto&& importSpecs = LoadImportSpecs(package->allFileImports()->Get(i));
    CJC_NULLPTR_CHECK(package->allFileInfo());

    auto fileInfo = package->allFileInfo()->Get(i);
    auto tmpFilePath = package->allFiles()->Get(i)->str();
    const bool isCJMPFile = true;
    auto tmpFileId = sourceManager.AddSource(tmpFilePath, "", package->fullPkgName()->str(), isCJMPFile);
    CJC_ASSERT(tmpFileId == fileInfo->fileID());
    allFileIds[i] = tmpFileId;

    auto file = CreateFileNode(*curPackage, fileInfo->fileID(), std::move(importSpecs));
    if (fileInfo->feature()) {
        file->feature = ASTLoaderCJMP::LoadFeaturesDirective(fileInfo->feature());
    }

    file->EnableAttr(AST::Attribute::FROM_COMMON_PART);
    file->EnableAttr(AST::Attribute::COMMON);
    file->isCommon = true;
    file->begin = LoadPos(fileInfo->begin());
    file->end = LoadPos(fileInfo->end());
    return file;
}

/**
 * @brief Validate that compilation options match between common cjo and current compile
 * @return true if options match or are compatible, false if there is a mismatch
 */
bool ASTLoader::ASTLoaderImpl::ValidateOptions()
{
    auto options = package->options();
    if (options == nullptr) {
        diag.DiagnoseRefactor(DiagKindRefactor::module_common_cjo_no_options, DEFAULT_POSITION);
        return true;
    }

    bool failed = false;
    if (options->debug() != this->opts.enableCompileDebug) {
        std::string before = options->debug() ? "enabled" : "disabled";
        std::string after = opts.enableCompileDebug ? "enabled" : "disabled";
        diag.DiagnoseRefactor(DiagKindRefactor::module_common_cjo_debug_mismatch, DEFAULT_POSITION, before, after);
        failed = true;
    }

    auto cjoOptLevel = LoadOptimizationLevel(*options);
    if (opts.optimizationLevel != cjoOptLevel) {
        std::string before = ASTLoaderCJMP::OptimizationLevelToString(cjoOptLevel);
        std::string after = ASTLoaderCJMP::OptimizationLevelToString(opts.optimizationLevel);
        diag.DiagnoseRefactor(DiagKindRefactor::module_common_cjo_opt_mismatch, DEFAULT_POSITION, before, after);
        failed = true;
    }

    return !failed;
}

bool ASTLoader::ASTLoaderImpl::PreloadCommonPartOfPackage(AST::Package& pkg)
{
    if (!VerifyForData("ast")) {
        return false;
    }

    package = PackageFormat::GetPackage(data.data());
    CJC_NULLPTR_CHECK(package);
    CJC_NULLPTR_CHECK(package->fullPkgName());

    curPackage = &pkg;

    auto deserializedPackageName = package->fullPkgName()->str();
    if (pkg.fullPackageName != deserializedPackageName) {
        ASTLoaderCJMP::ReportPackageMismatch(diag, pkg, pkg.fullPackageName, deserializedPackageName);
        return false;
    }
    auto specificFeatures = ASTLoaderCJMP::CollectFeaturesFromPackage(pkg);

    allTypes.resize(package->allTypes()->size(), nullptr);
    auto fileSize = package->allFiles()->size();
    allFileIds.resize(fileSize);

    for (uoffset_t i = 0; i < fileSize; i++) {
        auto file = PreloadCommonFile(i);

        ASTLoaderCJMP::ValidateCommonSpecificFeatureSetsRelations(*file, specificFeatures, diag, pkg);

        pkg.files.emplace_back(std::move(file));
    }

    if (!ValidateOptions()) {
        // options mismatch must abort compilation
        // otherwise the difference in desugaring/chir generation
        // may lead to a crash in subsequent compilation stages
        return false;
    }

    AddCurFile(pkg);
    auto imports = package->imports();
    CJC_NULLPTR_CHECK(imports);
    uoffset_t nImports = imports->size();
    for (uoffset_t i = 0; i < nImports; i++) {
        std::string importItem = imports->Get(i)->str();
        importedFullPackageNames.emplace_back(importItem);
    }

    return true;
}

} // namespace Cangjie
