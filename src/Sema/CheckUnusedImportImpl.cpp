// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file defines functions for check unused import.
 */

#include "TypeCheckerImpl.h"

using namespace Cangjie;
using namespace AST;

namespace {
class CheckUnusedImportImpl {
public:
    CheckUnusedImportImpl(Package& pkg, DiagnosticEngine& diag, ImportManager& importManager)
        : pkg(pkg), diag(diag), importManager(importManager)
    {
    }

    ~CheckUnusedImportImpl() = default;
    void Check();

private:
    void CollectUsedPackages(Node& node);
    void CollectNeedCheckImports();
    bool IsImportContentUsed(AST::ImportSpec& importSpec);
    bool IsImportContentUsedInMacro(AST::ImportSpec& importSpec);
    void AddUsedExtendDeclTarget(const Ptr<AST::ExtendDecl> ed, std::set<Ptr<Decl>>& usedSet) const;
    void AddUsedTarget(Node& node, Ptr<Decl> target);
    void AddUsedPackage(Node& node);
    void ReportUnusedImports();
    std::map<std::string, std::set<Ptr<Decl>>> usedPackageInAST;
    std::map<Ptr<File>, std::map<std::string, std::set<Ptr<Decl>>>> usedPackageInFile;
    std::map<std::string, std::set<Ptr<Decl>>> cacheUsedPackageInAST;
    std::map<Ptr<File>, std::map<std::string, std::set<Ptr<Decl>>>> cacheUsedPackageInFile;
    std::vector<Ptr<ImportSpec>> needCheckImport;
    Package& pkg;
    DiagnosticEngine& diag;
    ImportManager& importManager;
};

} // namespace

void CheckUnusedImportImpl::AddUsedExtendDeclTarget(const Ptr<AST::ExtendDecl> ed, std::set<Ptr<Decl>>& usedSet) const
{
    for (auto& type : ed->inheritedTypes) {
        usedSet.emplace(type->GetTarget());
    }
    if (ed->extendedType && ed->extendedType->GetTarget()) {
        usedSet.emplace(ed->extendedType->GetTarget());
    }

    if (!ed->generic) {
        return;
    }

    for (auto& gc : ed->generic->genericConstraints) {
        for (auto& ub : gc->upperBounds) {
            if (auto ubDecl = ub->GetTarget(); ubDecl) {
                usedSet.emplace(ubDecl);
            }
        }
    }
}

void CheckUnusedImportImpl::AddUsedTarget(Node& node, Ptr<Decl> target)
{
    auto& foundInAST = usedPackageInAST[target->fullPackageName];
    foundInAST.emplace(target);
    if (target->outerDecl != nullptr) {
        foundInAST.emplace(target->outerDecl);
        if (auto ed = DynamicCast<ExtendDecl>(target->outerDecl); ed != nullptr) {
            AddUsedExtendDeclTarget(ed, foundInAST);
        }
    }

    if (!node.curFile) {
        return;
    }

    auto& fileUsed = usedPackageInFile[node.curFile];
    auto& foundInFile = fileUsed[target->fullPackageName];

    foundInFile.emplace(target);
    if (target->outerDecl != nullptr) {
        foundInFile.emplace(target->outerDecl);
        if (auto ed = DynamicCast<ExtendDecl>(target->outerDecl); ed != nullptr) {
            AddUsedExtendDeclTarget(ed, foundInFile);
        }
    }
}

void CheckUnusedImportImpl::AddUsedPackage(Node& node)
{
    auto target = node.GetTarget();
    if (target == nullptr) {
        return;
    }

    AddUsedTarget(node, target);

    auto targets = node.GetTargets();
    for (auto decl : targets) {
        AddUsedTarget(node, decl);
    }
}

void CheckUnusedImportImpl::CollectNeedCheckImports()
{
    for (auto& file : pkg.files) {
        for (auto& import : file->imports) {
            // IMPLICIT_ADD(std.core) scenes does not need to be checked.
            if (import->TestAttr(AST::Attribute::IMPLICIT_ADD) || import->begin.IsZero() || import->end.IsZero()) {
                continue;
            }
            // multi-import or reExport scenes does not need to be checked.
            auto modifier = import->modifier ? import->modifier->modifier : TokenKind::PRIVATE;
            if (modifier == TokenKind::PUBLIC || modifier == TokenKind::PROTECTED || import->IsImportMulti()) {
                continue;
            }
            bool isInternalNeedExport =
                import->curFile && import->curFile->curPackage && !import->curFile->curPackage->noSubPkg;
            if (modifier == TokenKind::INTERNAL && isInternalNeedExport) {
                continue;
            }
            needCheckImport.emplace_back(import.get());
        }
    }
}

void CheckUnusedImportImpl::CollectUsedPackages(Node& node)
{
    Walker walker(&node, nullptr, [this](Ptr<Node> node) -> VisitAction {
        if (node->astKind == ASTKind::IMPORT_SPEC) {
            return VisitAction::SKIP_CHILDREN;
        }
        AddUsedPackage(*node);
        return VisitAction::WALK_CHILDREN;
    });

    walker.Walk();
}

bool CheckUnusedImportImpl::IsImportContentUsedInMacro(AST::ImportSpec& importSpec)
{
    CJC_ASSERT(importSpec.curFile);
    auto cjoManager = importManager.GetCjoManager();
    std::string packageName = cjoManager->GetPackageNameByImport(importSpec);

    auto usedMacroInFile = importManager.GetUsedMacroDecls(*importSpec.curFile);
    auto declsMap = cjoManager->GetPackageMembers(packageName);
    if (importSpec.IsImportAll() || !importSpec.content.isDecl) {
        if (!usedMacroInFile[packageName].empty()) {
            return true;
        }
        for (auto [_, decls] : declsMap) {
            for (auto decl : decls) {
                if (usedMacroInFile[decl->fullPackageName].count(decl) > 0) {
                    cacheUsedPackageInFile[importSpec.curFile][packageName].emplace(decl);
                    return true;
                }
            }
        }
    } else {
        auto decls = declsMap[importSpec.content.identifier];
        for (auto decl : decls) {
            if (usedMacroInFile[decl->fullPackageName].count(decl) > 0) {
                cacheUsedPackageInFile[importSpec.curFile][packageName].emplace(decl);
                return true;
            }
        }
    }

    return false;
}

bool CheckUnusedImportImpl::IsImportContentUsed(ImportSpec& importSpec)
{
    auto cjoManager = importManager.GetCjoManager();
    std::string packageName = cjoManager->GetPackageNameByImport(importSpec);

    std::map<std::string, std::set<Ptr<Decl>>>& usedPackage =
        (importSpec.IsPrivateImport() && importSpec.curFile) ? usedPackageInFile[importSpec.curFile] : usedPackageInAST;

    std::set<Ptr<Decl>> usedDecls = usedPackage[packageName];
    if (!usedDecls.empty() && (importSpec.IsImportAll() || !importSpec.content.isDecl)) {
        return true;
    }

    std::map<std::string, std::set<Ptr<Decl>>>& cacheUsedPackage = (importSpec.IsPrivateImport() && importSpec.curFile)
        ? cacheUsedPackageInFile[importSpec.curFile]
        : cacheUsedPackageInAST;

    auto declsMap = cjoManager->GetPackageMembers(packageName);
    if (importSpec.IsImportAll() || !importSpec.content.isDecl) {
        if (!cacheUsedPackage[packageName].empty()) {
            return true;
        }
        for (auto [_, decls] : declsMap) {
            for (auto decl : decls) {
                usedDecls = usedPackage[decl->fullPackageName];
                if (usedDecls.find(decl) != usedDecls.end()) {
                    cacheUsedPackageInAST[packageName].emplace(decl);
                    cacheUsedPackageInFile[importSpec.curFile][packageName].emplace(decl);
                    return true;
                }
            }
        }
    } else {
        auto decls = declsMap[importSpec.content.identifier];
        for (auto decl : decls) {
            if (cacheUsedPackage[packageName].find(decl) != cacheUsedPackage[packageName].end()) {
                return true;
            }
            usedDecls = usedPackage[decl->fullPackageName];
            if (usedDecls.find(decl) != usedDecls.end()) {
                cacheUsedPackageInAST[packageName].emplace(decl);
                cacheUsedPackageInFile[importSpec.curFile][packageName].emplace(decl);
                return true;
            }
        }
    }

    auto package = cjoManager->GetPackage(packageName);
    if (package && package->isMacroPackage) {
        return IsImportContentUsedInMacro(importSpec);
    }
    return false;
}

void CheckUnusedImportImpl::ReportUnusedImports()
{
    for (auto& importSpec : needCheckImport) {
        if (IsImportContentUsed(*importSpec)) {
            continue;
        }
        diag.DiagnoseRefactor(DiagKindRefactor::sema_unused_import, MakeRange(importSpec->begin, importSpec->end),
            importSpec->content.ToString());
    }
}

void CheckUnusedImportImpl::Check()
{
    CollectUsedPackages(pkg);
    CollectNeedCheckImports();
    ReportUnusedImports();
}

void TypeChecker::TypeCheckerImpl::CheckUnusedImportSpec(Package& pkg)
{
    CheckUnusedImportImpl(pkg, diag, importManager).Check();
}