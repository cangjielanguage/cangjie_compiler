// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 * This file implements the AST Loader related classes.
 */

#include "ASTLoaderImpl.h"

#include "flatbuffers/ModuleFormat_generated.h"

#include "cangjie/AST/ASTCasting.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/Version.h"
#include "cangjie/Lex/Token.h"
#include "cangjie/Utils/CheckUtils.h"

using namespace Cangjie;
using namespace AST;

namespace Cangjie {
const std::unordered_map<PackageFormat::AccessLevel, AST::AccessLevel> ACCESS_LEVEL_RMAP = {
#define ACCESS_LEVEL(AST_KIND, FBS_KIND) {PackageFormat::AccessLevel_##FBS_KIND, AST::AccessLevel::AST_KIND},
#include "Mapping.h"
#undef ACCESS_LEVEL
};

const std::unordered_map<PackageFormat::AccessModifier, std::pair<TokenKind, AST::Attribute>> ACCESS_MODIFIER_RMAP = {
#define ACCESS_MODIFIER(AST_KIND, FBS_KIND)                                                                            \
    {PackageFormat::AccessModifier_##FBS_KIND, {TokenKind::AST_KIND, AST::Attribute::AST_KIND}},
#include "Mapping.h"
#undef ACCESS_MODIFIER
};

const std::unordered_map<PackageFormat::BuiltInType, AST::BuiltInType> BUILTIN_TYPE_RMAP = {
#define BUILTIN_TYPE(AST_KIND, FBS_KIND) {PackageFormat::BuiltInType_##FBS_KIND, AST::BuiltInType::AST_KIND},
#include "Mapping.h"
#undef BUILTIN_TYPE
};

const std::unordered_map<PackageFormat::OverflowPolicy, OverflowStrategy> STRATEGY_RMAP = {
#define OVERFLOW_STRATEGY(AST_KIND, FBS_KIND) {PackageFormat::OverflowPolicy_##FBS_KIND, OverflowStrategy::AST_KIND},
#include "Mapping.h"
#undef OVERFLOW_STRATEGY
};

const std::unordered_map<PackageFormat::TypeKind, AST::TypeKind> TYPE_KIND_RMAP = {
#define TYPE_KIND(AST_KIND, FBS_KIND) {PackageFormat::TypeKind_##FBS_KIND, AST::TypeKind::AST_KIND},
#include "Mapping.h"
#undef TYPE_KIND
};

const std::unordered_map<PackageFormat::OperatorKind, TokenKind> OP_KIND_RMAP = {
#define OPERATOR_KIND(AST_KIND, FBS_KIND) {PackageFormat::OperatorKind_##FBS_KIND, TokenKind::AST_KIND},
#include "Mapping.h"
#undef OPERATOR_KIND
};

const std::unordered_map<PackageFormat::CallKind, AST::CallKind> CALL_KIND_RMAP = {
#define CALL_KIND(AST_KIND, FBS_KIND) {PackageFormat::CallKind_##FBS_KIND, AST::CallKind::AST_KIND},
#include "Mapping.h"
#undef CALL_KIND
};

const std::unordered_map<PackageFormat::LitConstKind, AST::LitConstKind> LIT_CONST_KIND_RMAP = {
#define LIT_CONST_KIND(AST_KIND, FBS_KIND) {PackageFormat::LitConstKind_##FBS_KIND, AST::LitConstKind::AST_KIND},
#include "Mapping.h"
#undef LIT_CONST_KIND
};

const std::unordered_map<PackageFormat::StringKind, AST::StringKind> STRING_KIND_RMAP = {
#define STRING_KIND(AST_KIND, FBS_KIND) {PackageFormat::StringKind_##FBS_KIND, AST::StringKind::AST_KIND},
#include "Mapping.h"
#undef STRING_KIND
};

const std::unordered_map<PackageFormat::ForInKind, AST::ForInKind> FOR_IN_KIND_RMAP = {
#define FOR_IN_KIND(AST_KIND, FBS_KIND) {PackageFormat::ForInKind_##FBS_KIND, AST::ForInKind::AST_KIND},
#include "Mapping.h"
#undef FOR_IN_KIND
};
} // namespace Cangjie

namespace {
template <typename DeclT> void SetDeclBody(DeclT& decl)
{
    if constexpr (std::is_same_v<DeclT, EnumDecl> || std::is_same_v<DeclT, ExtendDecl>) {
        decl.bodyScope = MakeOwned<DummyBody>();
    } else if constexpr (std::is_same_v<DeclT, FuncDecl>) {
        // 'funBody' will be loaded in 'LoadFuncDeclAdvancedInfo'.
    } else if constexpr (std::is_same_v<DeclT, ClassDecl>) {
        decl.body = MakeOwned<ClassBody>();
    } else if constexpr (std::is_same_v<DeclT, InterfaceDecl>) {
        decl.body = MakeOwned<InterfaceBody>();
    } else if constexpr (std::is_same_v<DeclT, StructDecl>) {
        decl.body = MakeOwned<StructBody>();
    } else {
        // Always false, not reachable, guarantees this function is not used in unexpected situations.
        static_assert(std::is_same_v<DeclT, Decl>);
    }
}

void SetOuterDeclForParamDecl(FuncDecl& func, Decl& parentDecl)
{
    if (func.funcBody->paramLists.empty()) {
        return;
    }
    for (auto& param : func.funcBody->paramLists[0]->params) {
        if (param->desugarDecl) {
            param->desugarDecl->outerDecl = &parentDecl;
        }
    }
}

void SetOuterDeclForSetterAndGetter(PropDecl& propDecl)
{
    for (auto& it : propDecl.setters) {
        it->propDecl = &propDecl;
        it->outerDecl = propDecl.outerDecl;
        it->scopeLevel = propDecl.scopeLevel;
        it->isSetter = true;
    }
    for (auto& it : propDecl.getters) {
        it->propDecl = &propDecl;
        it->outerDecl = propDecl.outerDecl;
        it->scopeLevel = propDecl.scopeLevel;
        it->isGetter = true;
    }
}

// Used for set imported struct decls' member's scope level.
const uint32_t MEMBER_FUNC_SCOPE_LEVEL = 2;

void SetOuterDeclForMemberDecl(Decl& member, Decl& parentDecl)
{
    member.outerDecl = &parentDecl;
    member.scopeLevel = MEMBER_FUNC_SCOPE_LEVEL;
    if (auto propDecl = DynamicCast<PropDecl*>(&member); propDecl) {
        SetOuterDeclForSetterAndGetter(*propDecl);
    } else if (auto func = DynamicCast<FuncDecl*>(&member); func && func->funcBody) {
        SetOuterDeclForParamDecl(*func, parentDecl);
    }
}
} // namespace

ASTLoader::ASTLoader(std::vector<uint8_t>&& data, const std::string& fullPackageName, TypeManager& typeManager,
    const CjoManager& cjoManager, const GlobalOptions& opts)
{
    pImpl = MakeOwned<ASTLoaderImpl>(std::move(data), fullPackageName, typeManager, cjoManager, opts);
}

ASTLoader::~ASTLoader()
{
}

void ASTLoader::SetImportSourceCode(bool enable) const
{
    pImpl->SetImportSourceCode(enable);
}

std::string ASTLoader::GetImportedPackageName() const
{
    CJC_NULLPTR_CHECK(pImpl);
    return pImpl->importedPackageName;
}

std::string ASTLoader::PreReadAndSetPackageName()
{
    return pImpl->PreReadAndSetPackageName();
}

std::string ASTLoader::ASTLoaderImpl::PreReadAndSetPackageName()
{
    if (!package) {
        package = PackageFormat::GetPackage(data.data());
    }
    CJC_NULLPTR_CHECK(package);
    CJC_NULLPTR_CHECK(package->fullPkgName());
    importedPackageName = package->fullPkgName()->str();

    return importedPackageName;
}

std::vector<std::string> ASTLoader::ReadFileNames() const
{
    return pImpl->ReadFileNames();
}

std::vector<std::string> ASTLoader::ASTLoaderImpl::ReadFileNames() const
{
    std::vector<std::string> files;
    auto allFiles = package->allFiles();
    CJC_NULLPTR_CHECK(allFiles);
    for (uoffset_t i = 0; i < allFiles->size(); i++) {
        files.push_back(allFiles->Get(i)->str());
    }
    return files;
}

const std::vector<std::string> ASTLoader::GetDependentPackageNames() const
{
    std::vector<std::string> dep;
    (void)std::copy_if(pImpl->importedFullPackageNames.begin(), pImpl->importedFullPackageNames.end(),
        std::back_inserter(dep), [this](auto& it) { return it != pImpl->importedPackageName; });
    return dep;
}

bool ASTLoader::ASTLoaderImpl::VerifyForData(const std::string& id)
{
    size_t size = data.size();
    // We need to verify the size first.
    flatbuffers::Verifier verifier(data.data(), size, FB_MAX_DEPTH, FB_MAX_TABLES);
    if (!PackageFormat::VerifyPackageBuffer(verifier)) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::module_loaded_ast_failed, DEFAULT_POSITION, id, importedPackageName, CANGJIE_VERSION);
        return false;
    }
    return true;
}

std::string ASTLoader::LoadPackageDepInfo() const
{
    CJC_NULLPTR_CHECK(pImpl);
    return pImpl->LoadPackageDepInfo();
}

std::string ASTLoader::ASTLoaderImpl::LoadPackageDepInfo()
{
    if (!VerifyForData("ast")) {
        return "";
    }
    package = PackageFormat::GetPackage(data.data());
    return package->pkgDepInfo()->str();
}

OwnedPtr<Package> ASTLoader::LoadPackageDependencies() const
{
    CJC_NULLPTR_CHECK(pImpl);
    return pImpl->LoadPackageDependencies();
}

OwnedPtr<Package> ASTLoader::ASTLoaderImpl::LoadPackageDependencies()
{
    if (!VerifyForData("ast")) {
        return {};
    }
    return PreLoadImportedPackageNode();
}

// Deserialize common part of package into already existing Package node
void ASTLoader::PreloadCommonPartOfPackage(AST::Package& pkg) const
{
    CJC_NULLPTR_CHECK(pImpl);
    pImpl->deserializingCommon = true;
    return pImpl->PreloadCommonPartOfPackage(pkg);
}

std::vector<OwnedPtr<ImportSpec>> ASTLoader::ASTLoaderImpl::LoadImportSpecs(const PackageFormat::Imports* imports)
{
    std::vector<OwnedPtr<ImportSpec>> importSpecsVec;
    auto importSpecs = imports->importSpecs();
    CJC_NULLPTR_CHECK(importSpecs);
    for (uoffset_t i = 0; i < importSpecs->size(); i++) {
        auto rawImportSpec = importSpecs->Get(i);
        OwnedPtr<ImportSpec> importSpec = MakeOwned<ImportSpec>();
        auto begin = rawImportSpec->begin();
        auto end = rawImportSpec->end();
        importSpec->begin = LoadPos(begin);
        importSpec->end = LoadPos(end);

        importSpec->EnableAttr(Attribute::IMPORTED);
        // This is WA to pass checks, because attributes do not deserialized for ImportSpec
        bool compilerAdded = rawImportSpec->end()->line() == 0;
        if (compilerAdded) {
            importSpec->EnableAttr(Attribute::COMPILER_ADD);
        }
        auto [modifierKind, attr] = ACCESS_MODIFIER_RMAP.at(rawImportSpec->reExport());
        importSpec->modifier = MakeOwned<Modifier>(modifierKind, DEFAULT_POSITION);
        importSpec->EnableAttr(attr);

        LoadImportContent(importSpec->content, *rawImportSpec);
        LoadNodePos(*rawImportSpec, *importSpec);

        importSpecsVec.emplace_back(std::move(importSpec));
    }
    return importSpecsVec;
}

void ASTLoader::ASTLoaderImpl::PreloadCommonPartOfPackage(AST::Package& pkg)
{
    if (!VerifyForData("ast")) {
        CJC_ABORT();
    }

    package = PackageFormat::GetPackage(data.data());
    CJC_NULLPTR_CHECK(package);

    curPackage = &pkg; // Deserialize common part AST into current platform package AST

    allTypes.resize(package->allTypes()->size(), nullptr);
    auto fileSize = package->allFiles()->size();
    allFileIds.resize(fileSize);
    for (uoffset_t i = 0; i < fileSize; i++) {
        CJC_NULLPTR_CHECK(package->allFileImports());
        auto&& importInfos = LoadImportSpecs(package->allFileImports()->Get(i));
        CJC_NULLPTR_CHECK(package->allFileInfo());
        // Load file info for CJMP
        auto fileInfo = package->allFileInfo()->Get(i);
        allFileIds[i] = fileInfo->fileID();
        auto file = CreateFileNode(*curPackage, fileInfo->fileID(), std::move(importInfos));
        file->EnableAttr(Attribute::COMMON);
        file->isCommon = true;
        file->begin = LoadPos(fileInfo->begin());
        file->end = LoadPos(fileInfo->end());
        pkg.files.emplace_back(std::move(file));
    }
    AddCurFile(pkg);
    auto imports = package->imports();
    CJC_NULLPTR_CHECK(imports);
    uoffset_t nImports = imports->size();
    for (uoffset_t i = 0; i < nImports; i++) {
        std::string importItem = imports->Get(i)->str();
        importedFullPackageNames.emplace_back(importItem);
    }
}

void ASTLoader::LoadPackageDecls() const
{
    pImpl->LoadPackageDecls();
}

/** 'LoadPackageDependencies' must be called before this method. */
void ASTLoader::ASTLoaderImpl::LoadPackageDecls()
{
    CJC_ASSERT(curPackage && package);
    // Get toplevel decls.
    uoffset_t nDecls = package->allDecls()->size();
    for (uoffset_t i = 0; i < nDecls; i++) {
        // Only toplevel decls are loaded.
        if (package->allDecls()->Get(i)->isTopLevel()) {
            if (IgnoreDecl(i)) {
                continue;
            }
            // NOTE: FormattedIndex is vector offset plus 1.
            auto tmpDecl = LoadDecl(i + 1);
            if (!tmpDecl) {
                continue;
            }
            auto fileID = tmpDecl->begin.fileID;
            if (auto found = idToFileMap.find(fileID); found != idToFileMap.end()) {
                tmpDecl->curFile = found->second;
            }
            CJC_NULLPTR_CHECK(tmpDecl->curFile);
            AddCurFile(*tmpDecl, tmpDecl->curFile); // Guarantees all sub-nodes have 'curFile'.
            if (tmpDecl->TestAttr(Attribute::GENERIC_INSTANTIATED)) {
                curPackage->genericInstantiatedDecls.emplace_back(std::move(tmpDecl));
                continue;
            }
            // In this branch, decl's 'curFile' nodes must exist in current package.
            CJC_ASSERT(tmpDecl->curFile->curPackage == curPackage);
            if (tmpDecl->IsExportedDecl() || tmpDecl->TestAttr(Attribute::FROM_COMMON_PART)) {
                tmpDecl->curFile->decls.emplace_back(std::move(tmpDecl));
            } else {
                tmpDecl->curFile->exportedInternalDecls.emplace_back(std::move(tmpDecl));
            }
        }
    }
    // Remove decls inside any generic decl.
    Utils::EraseIf(
        importNonGenericSrcFuncDecls, [](auto it) { return IsInDeclWithAttribute(*it, Attribute::GENERIC); });
    curPackage->srcImportedNonGenericDecls = importNonGenericSrcFuncDecls;
    // hasSourceImportedDecl true will trait as import package, but common part is not when compile platform
    if (!deserializingCommon) {
        curPackage->hasSourceImportedDecl = !allLoadedExprs.empty() || curPackage->TestAttr(Attribute::TOOL_ADD);
    }
    AddCurFile(*curPackage); // Guarantees all nodes have 'curFile'.
}

OwnedPtr<AST::File> ASTLoader::ASTLoaderImpl::CreateFileNode(
    Package& pkg, unsigned int fileId, std::vector<OwnedPtr<AST::ImportSpec>>&& imports)
{
    auto file = MakeOwned<File>();
    file->curFile = file.get();
    file->curPackage = &pkg;
    Source& source = sourceManager.GetSource(fileId);
    file->fileName = FileUtil::GetFileName(source.path);
    file->filePath = source.path;
    file->fileHash = source.fileHash;
    file->imports = std::move(imports);
    file->begin.fileID = fileId;
    idToFileMap.emplace(fileId, file.get());
    return file;
}

OwnedPtr<AST::Package> ASTLoader::ASTLoaderImpl::PreLoadImportedPackageNode()
{
    // Begin to parse the flatbuffer.
    package = PackageFormat::GetPackage(data.data());
    CJC_NULLPTR_CHECK(package);
    // Imported package is PackageDecl Node.
    OwnedPtr<Package> packageNode = MakeOwned<Package>();
    packageNode->EnableAttr(Attribute::IMPORTED);
    curPackage = packageNode.get();
    // Module name and package name
    packageNode->fullPackageName = package->fullPkgName()->str();
    if (package->kind() == PackageFormat::PackageKind_Foreign) {
        packageNode->EnableAttr(Attribute::TOOL_ADD);
    } else if (package->kind() == PackageFormat::PackageKind_Mock) {
        packageNode->EnableAttr(Attribute::MOCK_SUPPORTED);
    }
    packageNode->isMacroPackage = package->kind() == PackageFormat::PackageKind_Macro;
    packageNode->accessible = ACCESS_LEVEL_RMAP.at(package->access());
    allTypes.resize(package->allTypes()->size(), nullptr);
    // Get all files in current package.
    auto fileSize = package->allFiles()->size();
    allFileIds.resize(fileSize);
    for (uoffset_t i = 0; i < fileSize; i++) {
        auto tmpFilePath = package->allFiles()->Get(i)->str();
        auto tmpFileId = sourceManager.AddSource(tmpFilePath, "", package->fullPkgName()->str());
        allFileIds[i] = tmpFileId;
        auto&& importInfos = LoadImportSpecs(package->allFileImports()->Get(i));
        packageNode->files.emplace_back(CreateFileNode(*packageNode, tmpFileId, std::move(importInfos)));
    }
    AddCurFile(*packageNode);
    uoffset_t nImports = package->imports()->size();
    for (uoffset_t i = 0; i < nImports; i++) {
        std::string importItem = package->imports()->Get(i)->str();
        importedFullPackageNames.emplace_back(importItem);
    }
    return packageNode;
}

void ASTLoader::ASTLoaderImpl::LoadImportContent(ImportContent& content, const PackageFormat::ImportSpec& is) const
{
    content.kind = !is.asIdentifier()->string_view().empty() ? ImportKind::IMPORT_ALIAS
        : is.identifier()->string_view() == "*"              ? ImportKind::IMPORT_ALL
                                                             : ImportKind::IMPORT_SINGLE;
    content.prefixPaths.reserve(is.prefixPaths()->size());
    for (const auto& path : *is.prefixPaths()) {
        content.prefixPaths.emplace_back(path->str());
    }
    for (size_t i = 0; i < content.prefixPaths.size(); ++i) {
        content.prefixPoses.emplace_back(DEFAULT_POSITION);
        content.prefixDotPoses.emplace_back(DEFAULT_POSITION);
    }
    content.hasDoubleColon = is.hasDoubleColon();
    content.identifier = is.identifier()->str();
    content.identifier.SetPos(DEFAULT_POSITION, DEFAULT_POSITION);
    content.asPos = DEFAULT_POSITION;
    if (content.kind == ImportKind::IMPORT_ALIAS) {
        content.aliasName = is.asIdentifier()->str();
        content.aliasName.SetPos(DEFAULT_POSITION, DEFAULT_POSITION);
    }
    content.begin = DEFAULT_POSITION;
    content.end = DEFAULT_POSITION;
    content.isDecl = is.isDecl();
}

Position ASTLoader::ASTLoaderImpl::LoadPos(const PackageFormat::Position* posObj)
{
    CJC_NULLPTR_CHECK(posObj);
    auto fileIndex = posObj->file();
    auto pkgIndex = posObj->pkgId();
    auto line = posObj->line();
    auto column = posObj->column();
    unsigned int fileId = 0;
    // When 'pkgIndex' is default, it means the file is from current package.
    if (pkgIndex == INVALID_FORMAT_INDEX) {
        // NOTE: current package's 'fileIndex' is vector offset plus 1.
        fileId = (fileIndex == INVALID_FORMAT_INDEX) ? 0 : allFileIds.at(static_cast<unsigned long>(fileIndex - 1));
    } else {
        // file is from other package.
        // NOTE: other package's 'pkgIndex' is vector offset plus 1.
        CJC_ASSERT(pkgIndex - 1 < importedFullPackageNames.size());
        std::string fullPackageName = importedFullPackageNames.at(pkgIndex - 1);
        if (auto pd = cjoManager.GetPackageDecl(fullPackageName)) {
            CJC_ASSERT(fileIndex < pd->srcPackage->files.size());
            fileId = pd->srcPackage->files[fileIndex]->begin.fileID;
            idToFileMap.emplace(fileId, pd->srcPackage->files[fileIndex].get());
        }
    }

    return {fileId, static_cast<int>(line), static_cast<int>(column)};
}

void ASTLoader::ASTLoaderImpl::AddDeclToImportedPackage(Decl& decl)
{
    if (decl.astKind == ASTKind::EXTEND_DECL) {
        return; // ExtendDecl will never be referenced.
    }
    // NOTE: only exactly one 'ASTLoader' will write 'exportIdDeclMap' with given 'importedPackageName'.
    //       So following process will not exist racing condition in multithreading.
    auto& exportId = decl.exportId;
    auto exportIdDeclMap = cjoManager.GetExportIdDeclMap(importedPackageName);
    // Current loading package must existed in 'packageMap'.
    CJC_NULLPTR_CHECK(exportIdDeclMap);
    if (exportId.empty()) {
        exportIdDeclMap->emplace(decl.identifier, &decl);
    } else {
        auto it1 = exportIdDeclMap->find(exportId);
        if (it1 == exportIdDeclMap->end()) {
            exportIdDeclMap->emplace(exportId, &decl);
        } else {
            // NOTE: when 'importSrcCode' is disabled, the imported ast cannot to be used for code generation,
            //       this kind of situation is for LSP usage now, so the duplication error can be ignored.
            bool reportError = (it1->second != &decl) && importSrcCode;
            reportError = reportError && !isChirNow;
            if (reportError) {
                InternalError("Found same exportID when import a package.");
            }
        }
    }
}

/**
 * Get decl pointer according to packageIndex and declIndex.
 * NOTE: This function should only be used during 'LoadRef' stage that all decls are loaded.
 */
Ptr<Decl> ASTLoader::ASTLoaderImpl::GetDeclFromIndex(const PackageFormat::FullId* fullId)
{
    CJC_NULLPTR_CHECK(fullId);
    auto pkgIndex = fullId->pkgId();
    if (pkgIndex == INVALID_PACKAGE_INDEX) {
        return nullptr;
    }

    if (pkgIndex == PKG_REFERENCE_INDEX) {
        // When index is 'PKG_REFERENCE_INDEX' the 'decl' is referenced full package name.
        return cjoManager.GetPackageDecl(fullId->decl()->str());
    } else if (pkgIndex == CURRENT_PKG_INDEX) {
        // NOTE: for incremental loading case, the decl may not exist in cache.
        auto found = allLoadedDecls.find(fullId->index());
        return found != allLoadedDecls.end() ? found->second : nullptr;
    }
    std::string fullPackageName = importedFullPackageNames.at(static_cast<unsigned>(pkgIndex));
    // Get decl from imported packages.
    auto exportIdDeclMap = cjoManager.GetExportIdDeclMap(fullPackageName);
    auto exportId = fullId->decl()->str();
    if (exportIdDeclMap != nullptr) {
        auto it1 = exportIdDeclMap->find(exportId);
        if (it1 != exportIdDeclMap->end()) {
            return it1->second;
        }
    }
    if (cjoManager.GetPackageDecl(fullPackageName) == nullptr && !isLoadCache) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::package_invalid_cjo_dependency, DEFAULT_POSITION, importedPackageName, fullPackageName);
    }
    return nullptr;
}

/** Start of loading AST Nodes */
OwnedPtr<Generic> ASTLoader::ASTLoaderImpl::LoadGeneric(Decl& decl, const PackageFormat::Generic* generic)
{
    if (generic == nullptr) {
        return {};
    }
    CJC_NULLPTR_CHECK(generic->typeParameters());
    auto genericNode = MakeOwned<Generic>();
    uoffset_t length = static_cast<uoffset_t>(generic->typeParameters()->size());
    for (uoffset_t i = 0; i < length; i++) {
        auto index = generic->typeParameters()->Get(i);
        auto gpd = LoadDecl<GenericParamDecl>(index);
        CJC_NULLPTR_CHECK(gpd);
        gpd->outerDecl = &decl;
        genericNode->typeParameters.emplace_back(std::move(gpd));
    }
    return genericNode;
}

void ASTLoader::ASTLoaderImpl::LoadDeclBasicInfo(const PackageFormat::Decl& decl, Decl& astDecl)
{
    astDecl.identifier = decl.identifier()->str();
    astDecl.exportId = decl.exportId()->str();
    astDecl.mangledName = decl.mangledName()->str();
    astDecl.fullPackageName = decl.fullPkgName()->str();
    astDecl.moduleName = package->moduleName()->str();
    auto pos = LoadPos(decl.identifierPos());
    auto endPos = pos;
    if (pos != INVALID_POSITION && pos != DEFAULT_POSITION) {
        endPos = endPos + astDecl.identifier.Val().size();
    }
    astDecl.identifier.SetPos(pos, endPos);
    astDecl.CopyAttrs(GetAttributes(decl));
    if (!astDecl.TestAttr(Attribute::FROM_COMMON_PART) || !deserializingCommon) {
        astDecl.EnableAttr(Attribute::IMPORTED);
    }
    // Position is loaded in 'CreateAndLoadBasicInfo'.
    // Load mangle and hash for incremental compilation.
    astDecl.rawMangleName = decl.mangledBeforeSema()->str();
    CJC_NULLPTR_CHECK(decl.hash());
    astDecl.hash.instVar = decl.hash()->instVar();
    astDecl.hash.virt = decl.hash()->virt();
    astDecl.hash.sig = decl.hash()->sig();
    astDecl.hash.srcUse = decl.hash()->srcUse();
    astDecl.hash.bodyHash = decl.hash()->bodyHash();
    if (auto fullId = decl.genericDecl()) {
        astDecl.genericDecl = GetDeclFromIndex(fullId);
    }

    uoffset_t length = static_cast<uoffset_t>(decl.annotations()->size());
    for (uoffset_t i = 0; i < length; i++) {
        auto rawAnno = decl.annotations()->Get(i);
        auto anno = LoadAnnotation(*rawAnno);
        astDecl.annotations.emplace_back(std::move(anno));
    }
}

OwnedPtr<Annotation> ASTLoader::ASTLoaderImpl::LoadAnnotation(const PackageFormat::Anno& rawAnno)
{
    auto annotation = MakeOwned<Annotation>();

    if (rawAnno.kind() == PackageFormat::AnnoKind_Deprecated) {
        annotation->kind = AnnotationKind::DEPRECATED;
    } else if (rawAnno.kind() == PackageFormat::AnnoKind_TestRegistration) {
        annotation->kind = AnnotationKind::ATTRIBUTE;
        annotation->attrs.emplace_back(TokenKind::IDENTIFIER, "TEST_REGISTER");
    } else if (rawAnno.kind() == PackageFormat::AnnoKind_Frozen) {
        annotation->kind = AnnotationKind::FROZEN;
    } else if (rawAnno.kind() == PackageFormat::AnnoKind_JavaMirror) {
        annotation->kind = AnnotationKind::JAVA_MIRROR;
    } else if (rawAnno.kind() == PackageFormat::AnnoKind_JavaImpl) {
        annotation->kind = AnnotationKind::JAVA_IMPL;
    } else if (rawAnno.kind() == PackageFormat::AnnoKind_ObjCMirror) {
        annotation->kind = AnnotationKind::OBJ_C_MIRROR;
    } else if (rawAnno.kind() == PackageFormat::AnnoKind_ObjCImpl) {
        annotation->kind = AnnotationKind::OBJ_C_IMPL;
    } else if (rawAnno.kind() == PackageFormat::AnnoKind_ForeignName) {
        annotation->kind = AnnotationKind::FOREIGN_NAME;
    } else if (rawAnno.kind() == PackageFormat::AnnoKind_Custom) {
        annotation->kind = AnnotationKind::CUSTOM;
        annotation->isCompileTimeVisible = true;
    } else {
        InternalError("Unhandled annotation kind.");
    }
    CJC_NULLPTR_CHECK(rawAnno.identifier());
    annotation->identifier = rawAnno.identifier()->str();
    CJC_NULLPTR_CHECK(rawAnno.args());
    uoffset_t length = static_cast<uoffset_t>(rawAnno.args()->size());
    for (uoffset_t i = 0; i < length; i++) {
        auto rawArg = rawAnno.args()->Get(i);
        auto arg = LoadAnnotationArg(*rawArg);
        annotation->args.emplace_back(std::move(arg));
    }

    return annotation;
}

OwnedPtr<FuncArg> ASTLoader::ASTLoaderImpl::LoadAnnotationArg(const PackageFormat::AnnoArg& rawArg)
{
    auto arg = MakeOwned<FuncArg>();

    arg->name = rawArg.name()->str();
    arg->expr = LoadExpr(rawArg.expr());

    if (arg->expr->astKind != ASTKind::LIT_CONST_EXPR) {
        InternalError("Kind of deserialized argument must be constant literal.");
    }

    return arg;
}

OwnedPtr<Decl> ASTLoader::ASTLoaderImpl::LoadVarDecl(const PackageFormat::Decl& decl, int64_t declIndex)
{
    auto varDecl = CreateAndLoadBasicInfo<VarDecl>(decl, declIndex);
    auto info = decl.info_as_VarInfo();
    CJC_NULLPTR_CHECK(info);
    varDecl->isVar = info->isVar();
    varDecl->isMemberParam = info->isMemberParam();
    varDecl->isConst = info->isConst();
    LoadDeclBasicInfo(decl, *varDecl);
    if ((importSrcCode && !varDecl->TestAttr(AST::Attribute::FROM_COMMON_PART)) || deserializingCommon) {
        varDecl->initializer = LoadExpr(info->initializer());
    }
    if (varDecl->initializer && IsGlobalOrMember(*varDecl)) {
        // Only collect global and member decls.
        importNonGenericSrcFuncDecls.emplace_back(varDecl.get());
        varDecl->EnableAttr(Attribute::SRC_IMPORTED);
    }
    if (IsGlobalOrMember(*varDecl)) {
        // Global or member variable may be referenced by other package.
        AddDeclToImportedPackage(*varDecl);
    }
    return varDecl;
}

OwnedPtr<Decl> ASTLoader::ASTLoaderImpl::LoadVarWithPatternDecl(const PackageFormat::Decl& decl, int64_t declIndex)
{
    auto vpd = CreateAndLoadBasicInfo<VarWithPatternDecl>(decl, declIndex);
    auto info = decl.info_as_VarWithPatternInfo();
    CJC_NULLPTR_CHECK(info);
    vpd->isVar = info->isVar();
    vpd->isConst = info->isConst();
    LoadDeclBasicInfo(decl, *vpd);
    vpd->initializer = LoadExpr(info->initializer());
    vpd->irrefutablePattern = LoadPattern(*info->irrefutablePattern());
    return vpd;
}

OwnedPtr<Decl> ASTLoader::ASTLoaderImpl::LoadPropDecl(const PackageFormat::Decl& decl, int64_t declIndex)
{
    auto propDecl = CreateAndLoadBasicInfo<PropDecl>(decl, declIndex);
    auto info = decl.info_as_PropInfo();
    CJC_NULLPTR_CHECK(info);
    propDecl->isVar = info->isMutable();
    LoadDeclBasicInfo(decl, *propDecl);
    // setters and getters
    auto length = static_cast<uoffset_t>(info->setters()->size());
    for (uoffset_t i = 0; i < length; i++) {
        auto index = info->setters()->Get(i);
        auto setter = LoadDecl<FuncDecl>(index);
        propDecl->setters.emplace_back(std::move(setter));
    }
    length = static_cast<uoffset_t>(info->getters()->size());
    for (uoffset_t i = 0; i < length; i++) {
        auto index = info->getters()->Get(i);
        auto getter = LoadDecl<FuncDecl>(index);
        propDecl->getters.emplace_back(std::move(getter));
    }
    return propDecl;
}

OwnedPtr<FuncParamList> ASTLoader::ASTLoaderImpl::LoadFuncParamList(const PackageFormat::FuncParamList* funcParamList)
{
    auto ret = MakeOwned<FuncParamList>();
    if (funcParamList == nullptr) {
        return ret;
    }
    uoffset_t length = static_cast<uoffset_t>(funcParamList->params()->size());
    for (uoffset_t i = 0; i < length; i++) {
        auto index = funcParamList->params()->Get(i);
        auto param = LoadDecl<FuncParam>(index);
        index = funcParamList->desugars()->Get(i);
        if (index != INVALID_FORMAT_INDEX) {
            param->desugarDecl = LoadDecl<FuncDecl>(index);
        }
        ret->params.emplace_back(std::move(param));
    }
    return ret;
};

OwnedPtr<Decl> ASTLoader::ASTLoaderImpl::LoadFuncParam(const PackageFormat::Decl& decl, int64_t declIndex)
{
    auto funcParam = CreateAndLoadBasicInfo<FuncParam>(decl, declIndex);
    LoadDeclBasicInfo(decl, *funcParam);
    auto info = decl.info_as_ParamInfo();
    CJC_NULLPTR_CHECK(info);
    funcParam->isNamedParam = info->isNamedParam();
    funcParam->isMemberParam = info->isMemberParam();
    funcParam->assignment = LoadExpr(info->defaultVal());
    return funcParam;
}

void ASTLoader::ASTLoaderImpl::LoadFuncDeclAdvancedInfo(const PackageFormat::Decl& decl, FuncDecl& funcDecl)
{
    auto info = decl.info_as_FuncInfo();
    CJC_NULLPTR_CHECK(info);
    funcDecl.overflowStrategy = STRATEGY_RMAP.at(info->overflowPolicy());
    funcDecl.isInline = info->isInline() && cjoManager.GetCanInline();
    funcDecl.isFastNative = info->isFastNative();
    funcDecl.isConst = info->isConst();
    funcDecl.op = OP_KIND_RMAP.at(info->op()); // TokenKind op of operator overload function.
    auto& annos = funcDecl.annotations;
    funcDecl.isFrozen = Utils::In(annos, [](const auto& anno) { return anno->kind == AnnotationKind::FROZEN; });
    CJC_NULLPTR_CHECK(info->funcBody());
    funcDecl.funcBody = LoadFuncBody(*info->funcBody(), funcDecl.isInline);
    auto& funcBody = funcDecl.funcBody;
    funcBody->funcDecl = &funcDecl;
    funcBody->generic = LoadGeneric(funcDecl, decl.generic());
    // If 'funcBody->body' means funcBody implementation is loaded.
    if (importSrcCode && funcBody->body) {
        if (!funcBody->generic && IsGlobalOrMember(funcDecl)) {
            importNonGenericSrcFuncDecls.emplace_back(&funcDecl); // Only collect global and member decls.
        }
        funcDecl.EnableAttr(Attribute::SRC_IMPORTED);
    }
    SetOuterFunctionDecl(funcDecl);
    if (funcDecl.funcBody->paramLists.empty()) { // For LSP usage, this may be empty.
        return;
    }
    // Update param functions' references.
    for (auto& param : funcDecl.funcBody->paramLists[0]->params) {
        if (!param->desugarDecl) {
            continue;
        }
        param->desugarDecl->ownerFunc = &funcDecl;
        param->desugarDecl->isFrozen = funcDecl.isFrozen;
        // If owner function is non-generic global/member function, collecting source imported param function.
        bool needCollect = importSrcCode && !funcDecl.TestAttr(Attribute::GENERIC) && IsGlobalOrMember(funcDecl) &&
            param->desugarDecl->funcBody->body;
        if (needCollect) {
            (void)importNonGenericSrcFuncDecls.emplace_back(param->desugarDecl.get());
        }
    }
}

void ASTLoader::ASTLoaderImpl::LoadEnumDeclAdvancedInfo(const PackageFormat::Decl& decl, EnumDecl& enumDecl)
{
    auto info = decl.info_as_EnumInfo();
    CJC_NULLPTR_CHECK(info);
    enumDecl.hasArguments = info->hasArguments();
    enumDecl.hasEllipsis = info->nonExhaustive();
    if (enumDecl.hasEllipsis) {
        enumDecl.ellipsisPos = LoadPos(info->ellipsisPos());
    }
    // Body.
    auto length = info->body()->size();
    for (uint32_t i = 0; i < length; i++) {
        auto index = static_cast<FormattedIndex>(info->body()->Get(i));
        auto d = LoadDecl(index);
        SetOuterDeclForMemberDecl(*d, enumDecl);
        if (d->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
            enumDecl.constructors.emplace_back(std::move(d));
        } else {
            enumDecl.members.emplace_back(std::move(d));
        }
    }
}

static void LoadAnnoTargets(const PackageFormat::ClassInfo& info, Annotation& anno)
{
    auto anno1 = info.annoTargets();
    constexpr AnnotationTargetT allTargetsEnabled{0xffff};
    constexpr unsigned char allTargetsSerialised{0xff};
    if (anno1 == allTargetsSerialised) {
        anno.target = allTargetsEnabled;
        return;
    }
    auto anno2 = info.annoTargets2();
    constexpr unsigned anno1Bits = CHAR_BIT - 1;
    anno.target = static_cast<AnnotationTargetT>(static_cast<AnnotationTargetT>(anno2) << anno1Bits) | anno1;
}

void ASTLoader::ASTLoaderImpl::LoadInheritableDeclAdvancedInfo(const PackageFormat::Decl& decl, InheritableDecl& id)
{
    auto& members = id.GetMemberDecls();
    if (id.TestAttr(Attribute::IS_ANNOTATION)) {
        auto ann = MakeOwned<Annotation>();
        ann->kind = AnnotationKind::ANNOTATION;

        auto info = decl.info_as_ClassInfo();
        CJC_NULLPTR_CHECK(info);
        LoadAnnoTargets(*info, *ann);
        ann->runtimeVisible = info->runtimeVisible();
        (void)id.annotations.emplace_back(std::move(ann));
    }
    auto body = id.astKind == ASTKind::CLASS_DECL ? decl.info_as_ClassInfo()->body()
        : id.astKind == ASTKind::INTERFACE_DECL   ? decl.info_as_InterfaceInfo()->body()
        : id.astKind == ASTKind::ENUM_DECL        ? decl.info_as_EnumInfo()->body()
        : id.astKind == ASTKind::STRUCT_DECL      ? decl.info_as_StructInfo()->body()
        : id.astKind == ASTKind::EXTEND_DECL      ? decl.info_as_ExtendInfo()->body()
                                                  : nullptr;
    CJC_NULLPTR_CHECK(body);
    for (uoffset_t i = 0; i < body->size(); i++) {
        auto index = body->Get(i);
        members.emplace_back(LoadDecl(index));
        SetOuterDeclForMemberDecl(*members[i], id);
    }
}

template <typename DeclT>
OwnedPtr<Decl> ASTLoader::ASTLoaderImpl::LoadNominalDecl(const PackageFormat::Decl& decl, int64_t declIndex)
{
    auto astDecl = CreateAndLoadBasicInfo<DeclT>(decl, declIndex);
    SetDeclBody(*astDecl);
    LoadDeclBasicInfo(decl, *astDecl);
    AddDeclToImportedPackage(*astDecl);

    // Load unique information for different types.
    if constexpr (std::is_same_v<DeclT, EnumDecl>) {
        LoadEnumDeclAdvancedInfo(decl, *astDecl);
    } else if constexpr (std::is_same_v<DeclT, FuncDecl>) {
        LoadFuncDeclAdvancedInfo(decl, *astDecl);
    } else {
        LoadInheritableDeclAdvancedInfo(decl, *astDecl);
    }
    if constexpr (std::is_base_of<InheritableDecl, DeclT>::value) {
        astDecl->generic = LoadGeneric(*astDecl, decl.generic());
    }
    return astDecl;
}

OwnedPtr<Decl> ASTLoader::ASTLoaderImpl::LoadTypeAliasDecl(const PackageFormat::Decl& decl, int64_t declIndex)
{
    auto tad = CreateAndLoadBasicInfo<TypeAliasDecl>(decl, declIndex);
    LoadDeclBasicInfo(decl, *tad);
    AddDeclToImportedPackage(*tad);
    tad->generic = LoadGeneric(*tad, decl.generic());
    return tad;
}

OwnedPtr<Decl> ASTLoader::ASTLoaderImpl::LoadBuiltInDecl(const PackageFormat::Decl& decl, int64_t declIndex)
{
    auto info = decl.info_as_BuiltInInfo();
    CJC_NULLPTR_CHECK(info);
    auto bid = CreateAndLoadBasicInfo<BuiltInDecl>(decl, declIndex, BUILTIN_TYPE_RMAP.at(info->builtInType()));
    LoadDeclBasicInfo(decl, *bid);
    AddDeclToImportedPackage(*bid);
    bid->generic = LoadGeneric(*bid, decl.generic());
    return bid;
}

OwnedPtr<Decl> ASTLoader::ASTLoaderImpl::LoadGenericParamDecl(const PackageFormat::Decl& decl, int64_t declIndex)
{
    auto gpd = CreateAndLoadBasicInfo<GenericParamDecl>(decl, declIndex);
    LoadDeclBasicInfo(decl, *gpd);
    if (!gpd->exportId.empty()) {
        // Only add reference for the decl which have valid export id.
        AddDeclToImportedPackage(*gpd);
    }
    return gpd;
}

OwnedPtr<Decl> ASTLoader::ASTLoaderImpl::LoadInvalidDecl(const PackageFormat::Decl& decl)
{
    diag.DiagnoseRefactor(DiagKindRefactor::package_unsupported_load, DEFAULT_POSITION, "declaration");
    auto invalidDecl = MakeOwned<InvalidDecl>(INVALID_POSITION);
    LoadDeclBasicInfo(decl, *invalidDecl);
    LoadNodePos(decl, *invalidDecl);
    return invalidDecl;
}

// Load Decl from saved binary according to different decl kind. It should be noted that the load order should be
// exactly the same as the save order, which is important to reproduce the ty vector. Always add to decl pointer to
// loaded decls once the decl is created.
template OwnedPtr<VarDecl> ASTLoader::ASTLoaderImpl::LoadDecl(FormattedIndex declIndex); // For expr loading.
template <typename DeclT> OwnedPtr<DeclT> ASTLoader::ASTLoaderImpl::LoadDecl(FormattedIndex declIndex)
{
    OwnedPtr<Decl> ret;
    auto decl = GetFormatDeclByIndex(declIndex);
    auto foundHandler = declLoaderMap.find(decl->kind());
    if (foundHandler != declLoaderMap.end()) {
        ret = foundHandler->second(this, *decl, declIndex);
    } else {
        ret = LoadInvalidDecl(*decl);
    }
    // Caller guarantees casting type is matched.
    return OwnedPtr<DeclT>(StaticCast<DeclT*>(ret.release()));
}
