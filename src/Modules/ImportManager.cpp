// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the ImportManager related classes.
 */

#include "cangjie/Modules/ImportManager.h"

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "ModulesDiag.h"

#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/SourceManager.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Modules/ASTSerialization.h"
#include "cangjie/Modules/ModulesUtils.h"

using namespace Cangjie;
using namespace AST;
using namespace Modules;

namespace {
OwnedPtr<ImportSpec> CreateImportSpec(
    const std::string& fullPackageName, const std::string& item = "*", const std::string& alias = "")
{
    auto import = MakeOwned<ImportSpec>();
    auto names = Utils::SplitQualifiedName(fullPackageName);
    CJC_ASSERT(names.size() >= 1 && !item.empty());
    if (!alias.empty()) {
        import->content.kind = ImportKind::IMPORT_ALIAS;
    } else if (item != "*") {
        import->content.kind = ImportKind::IMPORT_SINGLE;
    } else {
        import->content.kind = ImportKind::IMPORT_ALL;
    }
    import->content.prefixPaths = names;
    import->content.identifier = item;
    import->content.identifier.SetPos(DEFAULT_POSITION, DEFAULT_POSITION);
    import->content.aliasName = alias;
    import->content.aliasName.SetPos(DEFAULT_POSITION, DEFAULT_POSITION);
    import->EnableAttr(Attribute::COMPILER_ADD, Attribute::IMPLICIT_ADD, Attribute::PRIVATE);
    return import;
}

void AddImport(
    File& file, const std::string& fullPackageName, const std::string& item = "*", const std::string& alias = "")
{
    file.imports.emplace_back(CreateImportSpec(fullPackageName, item, alias));
}

bool HasIfAvailable(Package& pkg)
{
    bool ret = false;
    auto hasIfAvailable = [&ret](Ptr<Node> node) {
        if (auto mee = DynamicCast<MacroExpandExpr>(node); mee && mee->invocation.IsIfAvailable()) {
            ret = true;
            return VisitAction::STOP_NOW;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(&pkg, hasIfAvailable).Walk();
    return ret;
}

void AddImplicitImports(Package& pkg, const GlobalOptions& opts)
{
    for (auto& file : pkg.files) {
        CJC_NULLPTR_CHECK(file);
        if (opts.implicitPrelude) {
            AddImport(*file, CORE_PACKAGE_NAME);
        }
        if (opts.enableCompileTest) {
            AddImport(*file, "std.unittest");
            AddImport(*file, "std.unittest.testmacro");
        }
        if (opts.compileTestsOnly && ImportManager::IsTestPackage(pkg.fullPackageName)) {
            AddImport(*file, ImportManager::GetMainPartPkgNameForTestPkg(pkg.fullPackageName));
        }
        if (opts.target.env == Triple::Environment::OHOS && HasIfAvailable(pkg)) {
            // For @IfAvailable desugar, depend on 'ohos.device_info.DeviceInfo' and 'ohos.base.canIUse'.
            AddImport(*file, "ohos.device_info");
            AddImport(*file, "ohos.base");
        }
    }
}

struct DependencyInfoItem {
    std::string package;
    std::string orPackage;
    bool isStd{false};
    std::set<Ptr<ImportSpec>, CmpNodeByPos> imports;
};

std::string Jsonfy(bool b)
{
    return b ? "true" : "false";
}

std::string Jsonfy(AccessLevel level)
{
    CJC_ASSERT(level != AccessLevel::PRIVATE);
    return level == AccessLevel::PUBLIC ? "public" : level == AccessLevel::PROTECTED ? "protected" : "internal";
}

std::string Jsonfy(const std::string& s)
{
    // Accroding to RFC 8259: All Unicode characters may be placed within the quotation marks,
    // except for the characters that MUST be escaped: quotation mark, reverse solidus,
    // and the control characters (U+0000 through U+001F)
    std::ostringstream out;
    for (char c : s) {
        if (c == '\\' || c == '"') {
            out << '\\' << c;
        } else if ('\x00' <= c && c <= '\x1f') {
            // According to RFC 8259: 4 hexadecimal digits are used to encode the character's code point.
            out << '\\' << 'u' << std::setfill('0') << std::setw(4) << std::hex << static_cast<int>(c);
        } else {
            out << c;
        }
    }
    return out.str();
}

std::string Jsonfy(const Position& pos)
{
    std::ostringstream out;
    out << "{"
        << "\"line\":" << pos.line << ","
        << "\"column\":" << pos.column << "}";
    return out.str();
}

std::string Jsonfy(const ImportSpec& import, bool exportCJO)
{
    CJC_NULLPTR_CHECK(import.curFile);
    std::ostringstream out;
    out << "{"
        << "\"file\":\"" << Jsonfy(exportCJO ? import.curFile->fileName : import.curFile->filePath) << "\","
        << "\"begin\":" << Jsonfy(import.begin) << ","
        << "\"end\":" << Jsonfy(import.end) << "}";
    return out.str();
}

std::string Jsonfy(const std::set<Ptr<ImportSpec>, CmpNodeByPos>& imports, bool exportCJO)
{
    if (imports.empty()) {
        return "[]";
    }
    std::ostringstream out;
    out << "[";
    auto iter = imports.cbegin();
    out << Jsonfy(**iter, exportCJO);
    ++iter;
    while (iter != imports.cend()) {
        out << "," << Jsonfy(**iter, exportCJO);
        ++iter;
    }
    out << "]";
    return out.str();
}

std::string Jsonfy(const DependencyInfoItem& info, bool exportCJO)
{
    std::ostringstream out;
    out << "{" << "\"package\":\"" << Jsonfy(info.package) << "\",";
    if (!info.orPackage.empty()) {
        out << "\"orPackage\":\"" << Jsonfy(info.orPackage) << "\",";
    }
    out << "\"isStd\":" << Jsonfy(info.isStd) << ",";
    out << "\"imports\":" << Jsonfy(info.imports, exportCJO) << "}";
    return out.str();
}

std::string Jsonfy(const std::map<std::string, DependencyInfoItem>& dependencies, bool exportCJO)
{
    if (dependencies.empty()) {
        return "[]";
    }
    std::ostringstream out;
    out << "[";
    auto iter = dependencies.cbegin();
    out << Jsonfy(iter->second, exportCJO);
    ++iter;
    while (iter != dependencies.cend()) {
        out << "," << Jsonfy(iter->second, exportCJO);
        ++iter;
    }
    out << "]";
    return out.str();
}

Range GetPackageNameRange(const CjoManager& cjoManager, const ImportSpec& import)
{
    auto& im = import.content;
    return import.TestAttr(Attribute::IMPLICIT_ADD)
        ? MakeRange(DEFAULT_POSITION, im.identifier)
        : (cjoManager.IsImportPackage(import)
                  ? (im.prefixPoses.empty() ? MakeRange(im.identifier)
                                            : MakeRange(im.prefixPoses.front(), im.identifier.End()))
                  : MakeRange(im.prefixPoses.front(), im.prefixPoses.back() + im.prefixPaths.back().size()));
}
} // namespace

void ImportManager::ExportAST(bool saveFileWithAbsPath, std::vector<uint8_t>& astData, const Package& pkg,
    const std::function<void(ASTWriter&)> additionalSerializations)
{
    ASTWriter writer(
        diag, GeneratePkgDepInfo(pkg), {importSrcCode, false, saveFileWithAbsPath, opts.compileCjd}, *cjoManager);
    auto realWriter = &writer;

    auto packageDecl = cjoManager->GetPackageDecl(pkg.fullPackageName);
    CJC_NULLPTR_CHECK(packageDecl);
    auto found = astWriters.find(&pkg);
    if (found != astWriters.end()) {
        realWriter = found->second;
    } else {
        writer.PreSaveFullExportDecls(*packageDecl->srcPackage);
    }
    realWriter->ExportAST(*packageDecl);

    additionalSerializations(*realWriter);

    realWriter->AST2FB(astData, *packageDecl);
}

std::vector<uint8_t> ImportManager::ExportASTSignature(const Package& pkg)
{
    ASTWriter writer(
        diag, {}, {.exportContent = true, .exportForIncr = true, .compileCjd = opts.compileCjd}, *cjoManager);
    auto packageDecl = cjoManager->GetPackageDecl(pkg.fullPackageName);
    CJC_NULLPTR_CHECK(packageDecl);
    writer.PreSaveFullExportDecls(*packageDecl->srcPackage);
    writer.ExportAST(*packageDecl);

    std::vector<uint8_t> astData;
    writer.AST2FB(astData, *packageDecl);
    return astData;
}

void ImportManager::ExportDeclsWithContent(bool saveFileWithAbsPath, Package& package)
{
    // NOTE: If 'importSrcCode' is disabled, we also do not need to export source code.
    auto writer = new ASTWriter(diag, GeneratePkgDepInfo(package), {importSrcCode, false, opts.exportForTest,
        saveFileWithAbsPath, opts.compileCjd}, *cjoManager);
    writer->PreSaveFullExportDecls(package);
    if (auto [it, success] = astWriters.emplace(&package, writer); !success) {
        delete it->second;
        astWriters[&package] = writer;
    }
}

std::unordered_set<std::string> ImportManager::LoadCachedTypeForPackage(
    const Package& pkg, const std::map<std::string, Ptr<Decl>>& mangledName2DeclMap)
{
    auto cachedCjo = opts.GenerateCachedPathName(pkg.fullPackageName, SERIALIZED_FILE_EXTENSION);
    return cjoManager->LoadCachedPackage(pkg, cachedCjo, mangledName2DeclMap);
}

bool ImportManager::HandleParsedPackage(
    const Package& package, const std::string& filePath, bool isUsedAsCommon, bool isRecursive)
{
    // `onlyUsedByMacro = false` means we have collected STD deps before, but `isRecursive`
    // may be different from previous scene, so we need handle for STD deps again.
    if (cjoManager->IsOnlyUsedByMacro(package.fullPackageName) &&
        (curPackage->isMacroPackage || (isUsedAsCommon && !package.isMacroPackage))) {
        // The current package has ever been macro-related, but now it's used in non-macro scene,
        // so we need collect STD deps for this package and its dependent packages.
        cjoManager->SetOnlyUsedByMacro(package.fullPackageName, false);
        HandleSTDPackage(package.fullPackageName, filePath, isRecursive);
        return ResolveImportedPackageHeaders(package, true);
    }
    return true;
}

void ImportManager::SaveDepPkgCjdPath(const std::string& fullPackageName, const std::string& cjoPath)
{
    // Replace the suffix directly. Ensure that .cjo and .cj.d are in the same path.
    std::string cjdPath = cjoPath.substr(0, cjoPath.rfind(SERIALIZED_FILE_EXTENSION));
    cjdPath = cjdPath + CJ_D_FILE_EXTENSION;
    cjdFilePaths.emplace(fullPackageName, cjdPath);
}

bool ImportManager::ResolveImportedPackageForFile(File& file, bool isRecursive)
{
    bool success{true};
    auto curPkg = file.curPackage;
    CJC_NULLPTR_CHECK(curPkg);
    bool isMacroRelated = curPkg->isMacroPackage || IsMacroRelatedPackageName(curPkg->fullPackageName);
    auto relation = Modules::GetPackageRelation(curPackage->fullPackageName, curPkg->fullPackageName);
    for (auto& import : file.imports) {
        if (import->IsImportMulti()) {
            continue;
        }
        auto isVisible = import->IsReExport() && Modules::IsVisible(*import, relation);
        auto [fullPackageName, cjoPath] = cjoManager->GetPackageCjo(*import);
        SaveDepPkgCjdPath(fullPackageName, cjoPath);
        // 1. Handle the package which has been parsed before.
        auto package = cjoManager->GetPackage(fullPackageName);
        if (package != nullptr) {
            CJC_ASSERT(
                !package->TestAttr(Attribute::IMPORTED) || package->TestAttr(Attribute::TOOL_ADD) || !cjoPath.empty());
            if (!package->TestAttr(Attribute::IMPORTED) && curPkg->TestAttr(Attribute::IMPORTED)) {
                // Source package have same name with indirect dependent package. Was reported during loading package.
                success = false;
                diag.DiagnoseRefactor(DiagKindRefactor::module_same_name_with_indirect_dependent_pkg, DEFAULT_POSITION,
                    fullPackageName, curPkg->fullPackageName);
            } else {
                success = HandleParsedPackage(*package, cjoPath, !isMacroRelated || isVisible, isRecursive) && success;
            }
            continue;
        }

        auto isMainPartPkgForTestPkg = opts.compileTestsOnly && IsTestPackage(curPkg->fullPackageName) &&
            GetMainPartPkgNameForTestPkg(curPkg->fullPackageName) == fullPackageName;
        // 2. Check and try to load cjo data.
        if (!CheckCjoPathLegality(import, cjoPath, fullPackageName, isRecursive, isMainPartPkgForTestPkg) ||
            !cjoManager->LoadPackageHeader(fullPackageName, cjoPath)) {
            success = false;
            continue;
        }
        package = cjoManager->GetPackage(fullPackageName);
        CJC_NULLPTR_CHECK(package);
        // 3. Store loaded package node.
        // Do not need to load indirectly dependent macro package which is not re-exported and is macro related.
        // NOTE: since allowing macro package to re-export normal package,
        //       we cannot ignore macro package imported by normal package.
        if (package->isMacroPackage && isRecursive && !isVisible && isMacroRelated) {
            // If it's the first time to load `fullPackageName` and current package is macro-related,
            // `fullPackageName` isn't being used now, we need to remove it and then re-import it when it's being used.
            cjoManager->RemovePackage(fullPackageName, package);
            continue;
        }
        // Package are needed for CodeGen in the following scenarios:
        // a. If the package is not a macro dependency package (import by normal package or import by case c's package).
        // b. In the scenario of compiling a macro package.
        // c. If the package is not a macro package and non-internal reExported by a macro package. If the package is
        //    internal, it's only necessary if current package is its subpackage.
        auto macroReExportCommonPackage = isVisible && isMacroRelated && !package->isMacroPackage;
        if ((!IsMacroRelatedPackageName(file.curPackage->fullPackageName) && !package->isMacroPackage) ||
            curPackage->isMacroPackage || macroReExportCommonPackage) {
            // If package is needed for CodeGen, we also should collect the standard library dependencies.
            HandleSTDPackage(fullPackageName, cjoPath, isRecursive);
        } else {
            cjoManager->SetOnlyUsedByMacro(fullPackageName, true);
        }
        // 4. Resolve current package's dependent packages by DFS.
        success = ResolveImportedPackageHeaders(*package, true) && success;
    }
    return success;
}

void ImportManager::SetImportedPackageFromASTNode(std::vector<OwnedPtr<AST::Package>>& pkgs)
{
    for (auto& pkg : pkgs) {
        cjoManager->AddImportedPackageFromASTNode(std::move(pkg));
    }
}

void ImportManager::HandleSTDPackage(const std::string& fullPackageName, const std::string& cjoPath, bool isRecursive)
{
    auto type = isRecursive ? DepType::INDIRECT : DepType::DIRECT;
    auto item = stdDepsMap.find(cjoPath);
    if (item != stdDepsMap.end()) {
        if (item->second != type) {
            item->second = DepType::BOTH;
        }
        return;
    }
    if ((STANDARD_LIBS.find(fullPackageName) != STANDARD_LIBS.end()) && FileUtil::FileExist(cjoPath)) {
        stdDepsMap.emplace(cjoPath, type);
    }
}

bool ImportManager::CheckCjoPathLegality(const OwnedPtr<ImportSpec>& import, const std::string& cjoPath,
    const std::string& fullPackageName, bool isRecursive, bool isMainPartPkgForTestPkg)
{
    // Call guarantees the 'cjoPath' existing when it is not empty.
    if (cjoPath.empty()) {
        const std::string helpInfo =
            "check if the .cjo file of the package exists in CANGJIE_PATH or CANGJIE_HOME, or use "
            "'--import-path' to specify the .cjo file path";
        Range range =
            isRecursive ? MakeRange(import->begin, import->begin + 1) : GetPackageNameRange(*cjoManager, *import);
        CJC_ASSERT(!range.HasZero()); // Except for IMPLICIT_ADD, there should be no zero.

        DiagnosticBuilder builder = diag.DiagnoseRefactor(isMainPartPkgForTestPkg
                ? DiagKindRefactor::package_missed_cjo_main_pkg_part_for_test_pkg
                : DiagKindRefactor::package_search_error,
            range, fullPackageName);
        builder.AddHelp(DiagHelp(helpInfo));
        return false;
    }
    return true;
}

bool ImportManager::ResolveImportedPackageHeaders(const Package& package, bool isRecursive)
{
    bool success{true};
    for (auto& file : package.files) {
        if (!ResolveImportedPackageForFile(*file, isRecursive)) {
            success = false;
        }
    }
    return success;
}

void ImportManager::UpdateMacroPackageUsage(const AST::Package& pkg)
{
    for (auto& pd : GetCurImportedPackages(pkg.fullPackageName)) {
        // Collect dependent cangjie packages.
        if (!pd->srcPackage->TestAttr(Attribute::TOOL_ADD) && pd->srcPackage->isMacroPackage) {
            (void)directMacroDeps.emplace(pd->srcPackage);
        }
    }
}

bool ImportManager::ResolveImportedPackages(const std::vector<Ptr<Package>>& packages)
{
    bool success = true;
    for (auto pkg : packages) {
        curPackage = pkg;
        success = ResolveImportedPackageHeaders(*curPackage, false) && success;
    }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    cjoManager->LoadPackageDeclsOnDemand(packages);
#endif
    for (auto pkg : packages) {
        cjoManager->AddPackageDeclMap(pkg->fullPackageName);
    }
    return success;
}

void ImportManager::DeleteASTWriters() noexcept
{
    for (auto& [_, writer] : astWriters) {
        delete writer;
        writer = nullptr;
    }
    astWriters.clear();
}

void ImportManager::DeleteASTLoaders() noexcept
{
    if (cjoManager) {
        cjoManager->DeleteASTLoaders();
    }
}

void ImportManager::ResolveImports(const AST::Package& pkg)
{
    // NOTE: only update macro package usage for source package.
    UpdateMacroPackageUsage(pkg);
    // 1. Report errors for invalid imports.
    CheckImports(pkg);
    // 2. Report shadowing & collision warnings for source package.
    CheckRedefinition(pkg);
}

void ImportManager::CheckImports(const AST::Package& pkg)
{
    for (auto& file : pkg.files) {
        for (auto& import : file->imports) {
            if (import->IsImportMulti()) {
                continue;
            }
            auto fullPackageName = cjoManager->GetPackageNameByImport(*import);
            auto package = cjoManager->GetPackage(fullPackageName);
            if (fullPackageName.empty() || !package) {
                continue; // Failed to import the package.
            }
            auto range = GetPackageNameRange(*cjoManager, *import);
            if (fullPackageName != package->fullPackageName) {
                diag.DiagnoseRefactor(
                    DiagKindRefactor::package_import_inconsistent, range, package->fullPackageName, fullPackageName);
                continue;
            } else if (fullPackageName == pkg.fullPackageName) {
                diag.DiagnoseRefactor(DiagKindRefactor::package_import_itself_illegal, range, fullPackageName);
                continue;
            }
            if (import->TestAttr(Attribute::IMPLICIT_ADD)) {
                continue;
            }
            auto relation = GetPackageRelation(pkg.fullPackageName, fullPackageName);
            if ((package->accessible == AccessLevel::PROTECTED && relation == PackageRelation::NONE) ||
                (package->accessible == AccessLevel::INTERNAL && relation != PackageRelation::CHILD &&
                    !IsSuperPackage(pkg.fullPackageName, fullPackageName))) {
                // Report error when the package is not accessible.
                diag.DiagnoseRefactor(DiagKindRefactor::package_accessibility, range, fullPackageName,
                    GetAccessLevelStr(*package), RelationToString(relation), pkg.fullPackageName);
            } else if (import->IsReExport() && !pkg.isMacroPackage && package->isMacroPackage) {
                diag.DiagnoseRefactor(DiagKindRefactor::package_cannot_export_macro_package, range);
                continue;
            } else if (import->IsImportAll()) {
                continue;
            } else if (cjoManager->IsImportPackage(*import)) {
                if (import->IsReExport()) {
                    diag.DiagnoseRefactor(DiagKindRefactor::package_re_export_package_name, range, fullPackageName,
                        GetAccessLevelStr(*import));
                }
                continue;
            }
            // Report error when the imported name cannot be found.
            auto decls = cjoManager->GetPackageMembersByName(package->fullPackageName, import->content.identifier);
            Utils::EraseIf(decls, [relation](auto it) { return !Modules::IsVisible(*it, relation); });
            if (decls.empty()) {
                auto& im = import->content;
                auto nameRange = MakeRange(im.prefixPoses.front(), im.identifier.End());
                diag.DiagnoseRefactor(
                    DiagKindRefactor::package_decl_not_find_in_package, nameRange, im.identifier, fullPackageName);
            }
        }
    }
}

void ImportManager::CheckRedefinition(const AST::Package& pkg)
{
    std::unordered_map<std::string, Range> pkgImportNamePos;
    std::unordered_map<Ptr<File>, std::unordered_map<std::string, Range>> fileImportNamePos;
    for (auto& file : pkg.files) {
        for (auto& import : file->imports) {
            if (import->IsImportMulti() || import->IsImportAll()) {
                continue;
            }
            auto& posMap = import->IsReExport() ? pkgImportNamePos : fileImportNamePos[file.get()];
            auto& im = import->content;
            auto& name = im.kind == ImportKind::IMPORT_SINGLE ? im.identifier : im.aliasName;
            auto decls = GetImportedDeclsByName(*file, name);
            if (decls.empty()) {
                continue; // Was failed to import package member.
            }
            auto range = im.kind == ImportKind::IMPORT_SINGLE ? MakeRange(im.identifier) : MakeRange(im.aliasName);
            auto [it, success] = posMap.emplace(name, range);
            if (import->IsReExport() && success) {
                std::tie(it, success) = fileImportNamePos[file.get()].emplace(name, range);
            }
            auto& pkgMembers = cjoManager->GetPackageMembersByName(pkg.fullPackageName, name);
            std::vector<Ptr<Decl>> sourceDecls;
            std::copy_if(pkgMembers.cbegin(), pkgMembers.cend(), std::back_inserter(sourceDecls),
                [&pkg](auto it1) { return it1->curFile->curPackage == &pkg; });
            if (!IsAllFuncDecl(sourceDecls)) {
                WarnUselessImport(diag, range, *(*sourceDecls.begin()));
            } else if (!success) {
                WarnConflictImport(diag, name, range, it->second);
            }
        }
    }
}

std::string ImportManager::GeneratePkgDepInfo(const Package& pkg, bool exportCJO) const
{
    auto isStd = [](const std::string& fullPackageName) { return STANDARD_LIBS.count(fullPackageName) > 0; };
    std::map<std::string, DependencyInfoItem> dependencies;
    for (auto& file : pkg.files) {
        for (auto& import : file->imports) {
            if (import->IsImportMulti() || import->TestAttr(Attribute::IMPLICIT_ADD)) {
                continue;
            }
            auto names = cjoManager->GetFullPackageNames(*import);
            CJC_ASSERT(!names.empty());
            std::string longName = names.front();
            std::string shortName = names.back();
            auto iter = dependencies.find(longName);
            if (iter == dependencies.cend()) {
                DependencyInfoItem dependInfo{"", "", false, {}};
                if (isStd(longName)) {
                    dependInfo.package = longName;
                    dependInfo.isStd = true;
                } else if (isStd(shortName)) {
                    dependInfo.package = shortName;
                    dependInfo.isStd = true;
                } else {
                    dependInfo.package = shortName;
                    dependInfo.orPackage = longName == shortName ? "" : longName;
                    dependInfo.isStd = isStd(shortName);
                }
                dependencies.emplace(longName, dependInfo);
            }
            dependencies.at(longName).imports.emplace(import.get());
        }
    }
    std::ostringstream out;
    out << "{"
        << "\"package\":\"" << Jsonfy(pkg.fullPackageName) << "\","
        << "\"isMacro\":" << Jsonfy(pkg.isMacroPackage) << ","
        << "\"accessLevel\":\"" << Jsonfy(pkg.accessible) << "\","
        << "\"dependencies\":" << Jsonfy(dependencies, exportCJO) << "}";
    return out.str();
}

std::vector<std::string> ImportManager::GeneratePkgDepInfoByCjo(const std::string& cjoPath)
{
    std::string packageDepInfo = cjoManager->GetPackageDepInfo(cjoPath);
    if (packageDepInfo.empty()) {
        return {};
    }
    // Note: wrap with std::vector to adapt current interface.
    return {packageDepInfo};
}

std::vector<std::string> ImportManager::GeneratePkgDepInfo(const std::vector<Ptr<Package>>& packages) const
{
    std::vector<std::string> res;
    for (auto& pkg : packages) {
        CJC_NULLPTR_CHECK(pkg);
        std::string depInfo = GeneratePkgDepInfo(*pkg, false);
        res.emplace_back(depInfo);
    }
    return res;
}

std::set<std::string> ImportManager::CollectDirectDepPkg(const Package& package) const
{
    std::set<std::string> depPkgs;
    for (auto& file : package.files) {
        for (auto& import : file->imports) {
            if (import->IsImportMulti()) {
                continue;
            }
            // Collect all possible package names.
            for (auto name : cjoManager->GetFullPackageNames(*import)) {
                depPkgs.emplace(name);
            }
        }
    }
    return depPkgs;
}

static void CheckPackageSpecsIdentical(DiagnosticEngine& diag, const Package& pkg)
{
    // the map of the key is declared package name with it's modifier (default is 'public'),
    // and the value is a pair whose first element is package name
    // position and second indicates whether current package declaration is an implicit default package.
    std::map<std::pair<std::string, std::string>, std::pair<Position, bool>> packageNamePosMap;
    for (auto& file : pkg.files) {
        CJC_NULLPTR_CHECK(file);
        if ((file->package == nullptr || file->begin == file->end) &&
            !Utils::InKeys(std::make_pair(DEFAULT_PACKAGE_NAME, std::string("public")), packageNamePosMap)) {
            packageNamePosMap[std::make_pair(DEFAULT_PACKAGE_NAME, "public")] = {file->begin, false};
        } else if (file->package != nullptr) {
            auto names = file->package->prefixPaths;
            if (names.empty() && !file->package->TestAttr(Attribute::PUBLIC)) {
                DiagRootPackageModifier(diag, *file->package);
            }
            names.emplace_back(file->package->packageName);
            auto fullPackageName = Utils::JoinStrings(names, ".");
            auto keyPair = std::make_pair(fullPackageName, GetAccessLevelStr(*file->package));
            if (!Utils::InKeys(keyPair, packageNamePosMap)) {
                auto position =
                    names.size() == 1 ? file->package->packageName.Begin() : file->package->prefixPoses.front();
                packageNamePosMap[keyPair] = {position, true};
            }
        }
    }
    if (packageNamePosMap.size() > 1) {
        DiagForDifferentPackageNames(diag, packageNamePosMap);
    }
}

bool ImportManager::BuildIndex(
    const std::string& cangjieModules, const GlobalOptions& globalOptions, std::vector<Ptr<Package>>& packages)
{
    CJC_ASSERT(!packages.empty());
    cjoManager->UpdateSearchPath(cangjieModules);

    for (auto pkg : packages) {
        CheckPackageSpecsIdentical(diag, *pkg);
        if (opts.compileTestsOnly) {
            pkg->fullPackageName = pkg->fullPackageName + SourceManager::testPkgSuffix;
        }
        AddImplicitImports(*pkg, globalOptions);
        cjoManager->AddSourcePackage(*pkg);
    }
    ResolveImportedPackages(packages);
    ClearPackageCjoCache();
    // Create dependency info after load all packages.
    for (auto pkg : packages) {
        dependencyGraph->AddDependenciesForPackage(*pkg);
    }
    for (auto pkg : packages) {
        AddImportedDeclsForSourcePackage(*pkg);
    }
    for (auto pkg : packages) {
        ResolveImports(*pkg);
    }
    for (auto& pd : GetAllImportedPackages()) {
        if (pd->srcPackage->hasSourceImportedDecl) {
            packages.push_back(pd->srcPackage);
        }
    }
    curPackage = packages.front();
    return true;
}

Ptr<Package> ImportManager::LoadPackageFromCjo(const std::string& fullPkgName, const std::string& cjoPath) const
{
    if (!cjoManager->LoadPackageHeader(fullPkgName, cjoPath)) {
        return nullptr;
    }

    std::function<bool(const Package&)> loadDependentPkgHeader = [&loadDependentPkgHeader, this](const Package& pkg) {
        for (auto& file : std::as_const(pkg.files)) {
            for (auto& import : std::as_const(file->imports)) {
                if (import->IsImportMulti()) {
                    continue;
                }
                auto [fullDepPkgName, cjoPath1] = cjoManager->GetPackageCjo(*import);
                if (auto depPkg = cjoManager->GetPackage(fullDepPkgName); depPkg) {
                    continue;
                }
                if (!cjoManager->LoadPackageHeader(fullDepPkgName, cjoPath1)) {
                    return false;
                }
                auto depPkg = cjoManager->GetPackage(fullDepPkgName);
                CJC_NULLPTR_CHECK(depPkg);
                if (!loadDependentPkgHeader(*depPkg)) {
                    return false;
                }
            }
        }
        return true;
    };

    auto package = cjoManager->GetPackage(fullPkgName);
    CJC_NULLPTR_CHECK(package);
    if (!loadDependentPkgHeader(*package)) {
        return nullptr;
    }

    cjoManager->LoadPackageDeclsOnDemand({package}, true);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    cjoManager->ClearVisitedPkgs();
#endif
    cjoManager->AddPackageDeclMap(fullPkgName);
    return package;
}

// Use for macro debug: update the debug file info
void ImportManager::UpdateFileNodeImportInfo(Package& package, const File& file, OwnedPtr<File>& newFile)
{
    CJC_ASSERT(newFile);
    newFile->curPackage = &package;
    // Replace the current file.
    CJC_ASSERT(file.fileHash != newFile->fileHash);
    // Copy the compiler added implicitly imports.
    for (auto& import : file.imports) {
        if (import->TestAttr(Attribute::IMPLICIT_ADD)) {
            (void)newFile->imports.emplace_back(ASTCloner::Clone(import.get()));
        }
    }
    for (auto& import : newFile->imports) {
        if (import->IsImportMulti()) {
            continue;
        }
        auto [_, cjoPath] = cjoManager->GetPackageCjo(*import);
        if (cjoPath.empty()) {
            InternalError("Failed to resolve imports for macro file");
        }
    }
    // MacroExpansion will never change import declarations, just copy file import map for new file and remove old one.
    auto found = fileImportedDeclsMap.find(file.fileHash);
    if (found != fileImportedDeclsMap.end()) {
        (void)fileImportedDeclsMap.emplace(newFile->fileHash, std::move(found->second));
        (void)fileImportedDeclsMap.erase(file.fileHash);
    }
    auto index = file.indexOfPackage;
    newFile->indexOfPackage = index;
    package.files[index] = std::move(newFile);
}

void ImportManager::SetPackageBchirCache(const std::string& fullPackageName, const std::vector<uint8_t>& bchirData)
{
    if (fullPackageName.empty() || bchirData.empty()) {
        return;
    }
    bchirFileCacheMap[fullPackageName] = bchirData;
}

const std::vector<uint8_t>* ImportManager::GetBchirCache(const std::string& fullPackageName)
{
    auto it = bchirFileCacheMap.find(fullPackageName);
    if (it != bchirFileCacheMap.end()) {
        return &(it->second);
    }
    return nullptr;
}

Ptr<PackageDecl> ImportManager::GetPackageDecl(const std::string& fullPackageName) const
{
    return cjoManager->GetPackageDecl(fullPackageName);
}

Ptr<Package> ImportManager::GetPackage(const std::string& fullPackageName) const
{
    return cjoManager->GetPackage(fullPackageName);
}

void ImportManager::AddUsedMacroDecls(const Ptr<const AST::File> file, Ptr<const AST::Decl> decl)
{
    auto& usedDeclMap = fileUsedMacroDeclsMap[file->indexOfPackage];

    auto& declSet = usedDeclMap[decl->fullPackageName];
    declSet.emplace(decl);
}

std::map<std::string, std::set<Ptr<const AST::Decl>, AST::CmpNodeByPos>>& ImportManager::GetUsedMacroDecls(
    const AST::File& file)
{
    return fileUsedMacroDeclsMap[file.indexOfPackage];
}

void ImportManager::AddImportedDeclsForSourcePackage(const AST::Package& pkg)
{
    for (auto& file : pkg.files) {
        for (auto& import : file->imports) {
            auto& declMap = import->IsReExport() ? importedDeclsMap : fileImportedDeclsMap[file->fileHash];
            CJC_NULLPTR_CHECK(import);
            if (import->IsImportMulti()) {
                continue;
            }
            auto fullPackageName = cjoManager->GetPackageNameByImport(*import);
            auto pkgDecl = cjoManager->GetPackageDecl(fullPackageName);
            if (fullPackageName.empty() || !pkgDecl) {
                continue; // Load package failed.
            }
            auto relation = GetPackageRelation(pkg.fullPackageName, fullPackageName);
            auto importLevel = GetAccessLevel(*import);
            if (import->content.kind == ImportKind::IMPORT_ALL) {
                auto members = cjoManager->GetPackageMembers(fullPackageName);
                for (auto& member : std::as_const(members)) {
                    auto& targetMap = declMap[member.first];
                    auto visibleDecls = GetVisibleDeclToMap(member.second, importLevel, relation);
                    std::for_each(visibleDecls.cbegin(), visibleDecls.cend(), [this, &import, &pkg](auto it) {
                        declsImportedByNodeMap[pkg.fullPackageName][it].emplace_back(import.get());
                    });
                    targetMap.merge(visibleDecls);
                    std::for_each(member.second.begin(), member.second.end(), [this, member](auto decl) {
                        if (decl->identifier != member.first) {
                            declToTypeAlias[decl].emplace(decl->identifier);
                        }
                    });
                }
                continue;
            }
            if (cjoManager->IsImportPackage(*import)) {
                auto name = import->content.kind == ImportKind::IMPORT_SINGLE
                    ? Utils::SplitQualifiedName(fullPackageName).back()
                    : import->content.aliasName.Val();
                declMap[name].emplace(pkgDecl);
                declsImportedByNodeMap[pkg.fullPackageName][pkgDecl].emplace_back(import.get());
            } else {
                auto members = cjoManager->GetPackageMembersByName(fullPackageName, import->content.identifier);
                const auto& name = import->content.kind == ImportKind::IMPORT_SINGLE ? import->content.identifier.Val()
                                                                                     : import->content.aliasName.Val();
                auto& targetMap = declMap[name];
                auto visibleDecls = GetVisibleDeclToMap(members, importLevel, relation);
                for (auto member : members) {
                    if (auto tad = DynamicCast<TypeAliasDecl>(member); tad && tad->type) {
                        auto originDecl = tad->type->GetTarget();
                        declToTypeAlias[originDecl].emplace(name);
                    } else if (member->identifier != name) {
                        declToTypeAlias[member].emplace(name);
                        declToTypeAlias[member].emplace(member->identifier);
                    }
                }
                std::for_each(visibleDecls.cbegin(), visibleDecls.cend(), [this, &import, &pkg](auto it) {
                    declsImportedByNodeMap[pkg.fullPackageName][it].emplace_back(import.get());
                });
                targetMap.merge(visibleDecls);
                import->content.isDecl = true;
            }
        }
    }
}

std::vector<Ptr<Decl>> ImportManager::GetImportedDeclsByName(const File& file, const std::string& name) const
{
    OrderedDeclSet ret;
    auto found = importedDeclsMap.find(name);
    if (found != importedDeclsMap.cend()) {
        ret.insert(found->second.cbegin(), found->second.cend());
    }
    auto fileIter = fileImportedDeclsMap.find(file.fileHash);
    if (fileIter != fileImportedDeclsMap.cend()) {
        auto iter = fileIter->second.find(name);
        if (iter != fileIter->second.cend()) {
            ret.insert(iter->second.cbegin(), iter->second.cend());
        }
    }
    return Utils::SetToVec<Ptr<Decl>>(ret);
}

std::vector<std::pair<std::string, std::vector<Ptr<Decl>>>> ImportManager::GetImportedDecls(const File& file) const
{
    std::vector<std::pair<std::string, std::vector<Ptr<Decl>>>> res;
    std::transform(importedDeclsMap.cbegin(), importedDeclsMap.cend(), std::back_inserter(res), [](auto& pair) {
        std::vector<Ptr<Decl>> decls = Utils::SetToVec<Ptr<Decl>>(pair.second);
        return std::make_pair(pair.first, decls);
    });
    auto iter = fileImportedDeclsMap.find(file.fileHash);
    if (iter != fileImportedDeclsMap.cend()) {
        std::transform(iter->second.cbegin(), iter->second.cend(), std::back_inserter(res), [](auto& pair) {
            std::vector<Ptr<Decl>> decls = Utils::SetToVec<Ptr<Decl>>(pair.second);
            return std::make_pair(pair.first, decls);
        });
    }
    return res;
}

const OrderedDeclSet& ImportManager::GetPackageMembersByName(const Package& package, const std::string& name) const
{
    return cjoManager->GetPackageMembersByName(package.fullPackageName, name);
}

// For LSP
AST::OrderedDeclSet ImportManager::GetPackageMembers(
    const std::string& srcFullPackageName, const std::string& targetFullPackageName) const
{
    if (srcFullPackageName == targetFullPackageName) {
        return {};
    }
    auto relation = Modules::GetPackageRelation(srcFullPackageName, targetFullPackageName);
    auto members = cjoManager->GetPackageMembers(targetFullPackageName);
    AST::OrderedDeclSet res;
    for (auto& [_, decls] : members) {
        for (auto it : decls) {
            if (Modules::IsVisible(*it, relation)) {
                res.emplace(it.get());
            }
        }
    }
    return res;
}

std::pair<Ptr<PackageDecl>, bool> ImportManager::GetImportedPackageDecl(
    Ptr<const Node> node, const std::string& packageName) const
{
    CJC_NULLPTR_CHECK(node->curFile);
    auto decls = GetImportedDeclsByName(*node->curFile, packageName);
    Ptr<PackageDecl> ret = nullptr;
    for (auto decl : decls) {
        if (auto pd = DynamicCast<PackageDecl>(decl)) {
            if (ret == nullptr) {
                ret = pd;
            } else {
                return {ret, true};
            }
        }
    }
    return {ret, false};
}

std::vector<Ptr<PackageDecl>> ImportManager::GetAllImportedPackages(bool includeMacroPkg) const
{
    return cjoManager->GetAllPackageDecls(includeMacroPkg);
}

const std::vector<Ptr<PackageDecl>>& ImportManager::GetCurImportedPackages(const std::string& fullPackageName) const
{
    return dependencyGraph->GetDirectDependencyPackageDecls(fullPackageName);
}

const std::vector<Ptr<PackageDecl>>& ImportManager::GetAllDependentPackageDecls(
    const std::string& fullPackageName, bool includeMacroPkg) const
{
    return dependencyGraph->GetAllDependencyPackageDecls(fullPackageName, includeMacroPkg);
}

const std::set<std::string>& ImportManager::GetAllDependentPackageNames(const std::string& fullPackageName) const
{
    // NOTE: exclude packages which is only used by macro expansion.
    return dependencyGraph->GetAllDependencyPackageNames(fullPackageName, false);
}

Ptr<Decl> ImportManager::GetImportedDecl(const std::string& fullPackageName, const std::string& name) const
{
    auto& decls = cjoManager->GetPackageMembersByName(fullPackageName, name);
    if (decls.empty()) {
        auto decl = cjoManager->GetImplicitPackageMembersByName(fullPackageName, name);
        return decl;
    }
    return *decls.begin();
}

void ImportManager::SetPackageCjoCache(const std::string& fullPackageName, const std::vector<uint8_t>& cjoData) const
{
    cjoManager->SetPackageCjoCache(fullPackageName, cjoData);
}

void ImportManager::ClearPackageCjoCache() const
{
    cjoManager->ClearCjoCache();
}

bool ImportManager::IsMacroRelatedPackageName(const std::string& fullPackageName) const
{
    return cjoManager->IsMacroRelatedPackageName(fullPackageName);
}

std::string ImportManager::GetMainPartPkgNameForTestPkg(const std::string& testPkgName)
{
    CJC_ASSERT(IsTestPackage(testPkgName));
    return testPkgName.substr(0, testPkgName.length() - SourceManager::testPkgSuffix.length());
}

bool ImportManager::IsTestPackage(const std::string& pkgName)
{
    return pkgName.length() >= SourceManager::testPkgSuffix.length() &&
        pkgName.compare(pkgName.length() - SourceManager::testPkgSuffix.length(), SourceManager::testPkgSuffix.length(),
            SourceManager::testPkgSuffix) == 0;
}

bool ImportManager::IsTypeAccessible(const File& file, const Type& type) const
{
    auto decl = type.GetTarget();
    if (!decl) {
        // Builtin type and primitive type is always accessible.
        return true;
    }
    return IsDeclAccessible(file, *decl);
}

bool ImportManager::IsTyAccessible(const File& file, const Ty& ty) const
{
    Ptr<Decl> decl = nullptr;
    if (auto sTy = DynamicCast<StructTy>(&ty)) {
        decl = sTy->declPtr;
    } else if (auto cTy = DynamicCast<ClassTy>(&ty)) {
        decl = cTy->declPtr;
    } else if (auto iTy = DynamicCast<InterfaceTy>(&ty)) {
        decl = iTy->declPtr;
    } else if (auto eTy = DynamicCast<EnumTy>(&ty)) {
        decl = eTy->declPtr;
    }
    if (decl) {
        return IsDeclAccessible(file, *decl);
    }
    return true;
}

bool ImportManager::IsDeclAccessible(const File& file, Decl& decl) const
{
    auto ret = decl.fullPackageName == file.curPackage->fullPackageName;
    auto relation = Modules::GetPackageRelation(file.curPackage->fullPackageName, decl.fullPackageName);
    if (!Utils::In(Ptr(&decl), GetImportedDeclsByName(file, decl.identifier))) {
        // Process 'import pkg1.pkg2; pkg2.Decl.extendMember', If 'Decl' is imported, it must visible in cur package.
        auto pkgNames = Utils::SplitQualifiedName(decl.fullPackageName);
        for (auto it = pkgNames.crbegin(); it != pkgNames.crend(); ++it) {
            ret = ret || (!GetImportedDeclsByName(file, *it).empty() && Modules::IsVisible(decl, relation));
        }
        // Process import alias decl or alias-import.
        // If we have 'type TA = A; extend TA {...}' in pkg1, and other pkg import 'pkg1.TA', 'extend A' is accessable.
        auto found = declToTypeAlias.find(&decl);
        if (found != declToTypeAlias.end()) {
            for (auto& tadId : found->second) {
                ret = ret || !GetImportedDeclsByName(file, tadId).empty();
            }
        }
    } else {
        ret = true;
    }
    return ret;
}
namespace {
inline void AddNoteForExtendExportDiag(const Ptr<DiagnosticBuilder> builder, const std::string note)
{
    if (builder) {
        builder->AddNote(note);
    }
}

inline void AddNoteForExtendExportDiag(const Ptr<DiagnosticBuilder> builder, const Range range, const std::string note)
{
    if (builder) {
        builder->AddNote(range, "to use the following extension, import '" + note + "'");
    }
}
} // namespace
bool ImportManager::IsExtendAllUpperBoundsImported(
    const ExtendDecl& ed, const File& file, const Ptr<DiagnosticBuilder> builder) const
{
    if (!ed.generic) {
        return true;
    }
    std::set<std::string> upperboundsNotImported;
    bool areAllUpperBoundsImported = true;
    for (auto& gc : ed.generic->genericConstraints) {
        for (auto& ub : gc->upperBounds) {
            CJC_NULLPTR_CHECK(ub);
            if (!IsTypeAccessible(file, *ub)) {
                areAllUpperBoundsImported = false;
                upperboundsNotImported.emplace(ub->ty->String());
                break;
            }
        }
    }
    for (auto& up : upperboundsNotImported) {
        AddNoteForExtendExportDiag(builder, MakeRange(ed.begin, ed.end), up);
    }
    return areAllUpperBoundsImported;
}

bool ImportManager::IsExtendAccessible(
    const File& file, const ExtendDecl& ed, const Ptr<DiagnosticBuilder> builder) const
{
    if (!file.curPackage || ed.fullPackageName == file.curPackage->fullPackageName) {
        return true;
    }
    auto curPkgName = file.curPackage->fullPackageName;

    if (opts.compileTestsOnly && ed.TestAttr(Attribute::FOR_TEST) && IsTestPackage(curPkgName) &&
        GetMainPartPkgNameForTestPkg(curPkgName) == ed.fullPackageName) {
        return true;
    }
    bool areAllUpperBoundsImported = IsExtendAllUpperBoundsImported(ed, file, builder);
    bool hasAnyInterfacesImported = false;
    bool isExtendedTypeAccessible = false;
    auto extendedDecl = Ty::GetDeclPtrOfTy<InheritableDecl>(ed.ty);
    bool isInSamePkg =
        extendedDecl ? ed.fullPackageName == extendedDecl->fullPackageName : ed.fullPackageName == "std.core";
    if (!isInSamePkg) {
        for (auto& inhert : ed.inheritedTypes) {
            if (inhert && IsTypeAccessible(file, *inhert)) {
                hasAnyInterfacesImported = true;
            }
        }
        isExtendedTypeAccessible = IsTypeAccessible(file, *ed.extendedType);
    } else {
        hasAnyInterfacesImported = true;
        if (extendedDecl) {
            auto relation = GetPackageRelation(file.curPackage->fullPackageName, extendedDecl->fullPackageName);
            isExtendedTypeAccessible = IsVisible(*extendedDecl, relation);
        } else {
            isExtendedTypeAccessible = true;
        }
    }
    if (!hasAnyInterfacesImported) {
        if (builder) {
            builder->AddNote(MakeRange(ed.begin, ed.end),
                "to use the following extension, you must import at least one of its inherited interfaces");
        }
    }
    // For direct extension, all upperbound (if any) need to be imported.
    // For interface extension, all upperbound (if any) and at lest one interface need to be imported.
    if (!isExtendedTypeAccessible) {
        AddNoteForExtendExportDiag(builder, MakeRange(ed.begin, ed.end), ed.extendedType->ty->String());
    }
    bool isExtendImported = areAllUpperBoundsImported && hasAnyInterfacesImported && isExtendedTypeAccessible;
    return ed.IsExportedDecl() && isExtendImported;
}

const Ptr<Type> ImportManager::FindImplmentInterface(const File& file, const Decl& member, const Ptr<Type>& it) const
{
    auto targetDecl = DynamicCast<InheritableDecl>(it->GetTarget());
    if (targetDecl == nullptr) {
        return nullptr;
    }
    for (auto& super : targetDecl->inheritedTypes) {
        if (auto implInterface = FindImplmentInterface(file, member, super)) {
            return implInterface;
        }
    }
    for (auto& m : targetDecl->GetMemberDecls()) {
        bool isImpl = typeManager.PairIsOverrideOrImpl(member, *m);
        if (isImpl) {
            return it;
        }
    }
    return nullptr;
}

bool ImportManager::IsExtendMemberImported(
    const Ptr<ExtendDecl> extend, const File& file, const Decl& member, const Ptr<DiagnosticBuilder> builder) const
{
    if (extend->inheritedTypes.empty()) {
        return true;
    }
    auto extendedDecl = Ty::GetDeclPtrOfTy<InheritableDecl>(extend->ty);
    bool isInSamePkg =
        extendedDecl ? extend->fullPackageName == extendedDecl->fullPackageName : extend->fullPackageName == "std.core";
    if (isInSamePkg) {
        if (!IsTypeAccessible(file, *extend->extendedType)) {
            AddNoteForExtendExportDiag(builder, MakeRange(extend->begin, extend->end), extend->ty->String());
            return false;
        }
        return true;
    }
    // Check whether the interface to which the member belongs is imported.
    for (auto& super : extend->inheritedTypes) {
        if (auto implInterface = FindImplmentInterface(file, member, super)) {
            if (!IsTypeAccessible(file, *implInterface)) {
                AddNoteForExtendExportDiag(builder, MakeRange(extend->begin, extend->end), implInterface->ty->String());
                return false;
            }
            return true;
        }
    }
    return false;
}

bool ImportManager::IsExtendMemberAccessible(
    const File& file, const Decl& member, Ty& baseTy, const Ptr<DiagnosticBuilder> builder) const
{
    Ptr<ExtendDecl> extend = typeManager.GetExtendDeclByMember(member, baseTy);
    if (extend == nullptr || file.curPackage == nullptr) {
        return true;
    }

    auto curPkgName = file.curPackage->fullPackageName;
    if (opts.compileTestsOnly && !member.TestAttr(Attribute::PRIVATE) && extend->TestAttr(Attribute::FOR_TEST) &&
        IsTestPackage(curPkgName) &&
        GetMainPartPkgNameForTestPkg(curPkgName) == member.fullPackageName) {
        return true;
    }
    // Access to extended members must meet the following four requirements:
    // 1. extension is exported.
    // 2. extension is imported. `IsExtendAccessible` checks 1 and 2.
    // 3. extension member is exported. `IsExportedDecl` checks 3.
    // 4. extension member is imported.
    if (file.curPackage && extend->fullPackageName == file.curPackage->fullPackageName) {
        return true;
    }
    if (!IsExtendAccessible(file, *extend, builder)) {
        return false;
    }
    if (!member.IsExportedDecl()) {
        AddNoteForExtendExportDiag(builder,
            "cannot access this member because it is not declared in any interface implemented by the extension");
        return false;
    }

    return IsExtendMemberImported(extend, file, member, builder);
}

ImportManager::~ImportManager()
{
    DeleteASTWriters();
    DeleteASTLoaders();
    curPackage = nullptr;
}
