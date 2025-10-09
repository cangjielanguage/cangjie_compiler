// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements diagnostics for modules.
 */

#include "ModulesDiag.h"

#include "cangjie/AST/Match.h"
#include "cangjie/Modules/ModulesUtils.h"

namespace Cangjie::Modules {
namespace {
bool IsSamePosition(const Position& pos1, const Position& pos2)
{
    return pos1 == pos2 && pos1.fileID == pos2.fileID;
}
} // namespace

void WarnUselessImport(DiagnosticEngine& diag, const Range& importRange, const Decl& decl)
{
    auto& name = decl.identifier;
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::package_shadowed_import, importRange, name);
    builder.AddNote(MakeRange(decl.identifier.Begin(), name), "'" + name + "' is declared here");
}

void WarnConflictImport(DiagnosticEngine& diag, const std::string& name, const Range& current, const Range& previous)
{
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::package_conflict_import, current, name);
    builder.AddNote(previous, "The previous was imported here");
}

void WarnRepeatedFeatureName(DiagnosticEngine& diag, std::string& name, const Range& current, const Range& previous)
{
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::feature_already_seen_name, current);
    builder.AddNote(previous, "feature '" +  name + "' previously used here");
}

// void DiagForNullPackageFeature(DiagnosticEngine& diag, const Ptr<File> file, const Ptr<FeaturesDirective> refFeature)
void DiagForNullPackageFeature(DiagnosticEngine& diag, const Range& current, const Ptr<FeaturesDirective> refFeature)
{
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::feature_null_declaration, current);
    builder.AddNote(
        MakeRange(refFeature->content[0].begin, refFeature->end),
        "perhapse you meant these features");
}

void DiagForDifferentPackageFeatureConsistency(DiagnosticEngine& diag, const Ptr<FeaturesDirective> feature,
    const Ptr<FeaturesDirective> refFeature)
{
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::feature_different_consistency,
        MakeRange(feature->content[0].begin, feature->end));
    builder.AddNote(
        MakeRange(refFeature->content[0].begin, refFeature->end),
        "perhapse you meant these features");
}

void DiagForDifferentPackageNames(DiagnosticEngine& diag,
    const std::map<std::pair<std::string, std::string>, std::pair<Position, bool>>& packageNamePosMap)
{
    Position diagPosition;
    std::pair<std::string, std::string> diagPackageDecl;
    for (auto [pkgPair, pair] : packageNamePosMap) {
        if (pair.second) {
            diagPosition = pair.first;
            diagPackageDecl = pkgPair;
            break;
        }
    }
    auto builder = diag.DiagnoseRefactor(
        DiagKindRefactor::package_multiple_package_declarations, MakeRange(diagPosition, diagPackageDecl.first));
    uint8_t counter = 0;
    for (auto [pkgPair, pair] : packageNamePosMap) {
        // 2 is maximum number of diagnostic prints.
        if (counter >= 2) {
            break;
        }
        if (IsSamePosition(diagPosition, pair.first)) {
            continue;
        }
        if (pair.second) {
            builder.AddNote(MakeRange(pair.first, pkgPair.first),
                "another different package declaration '" + pkgPair.second + " package " + pkgPair.first + "'");
        } else {
            builder.AddNote(MakeRange(pair.first, ""),
                "another different package declaration 'public package " + DEFAULT_PACKAGE_NAME + "'");
        }
        counter++;
    }
}

void DiagRootPackageModifier(DiagnosticEngine& diag, const PackageSpec& packageSpec)
{
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::package_root_package_should_be_public, packageSpec,
        MakeRange(packageSpec.packageName));
    auto packageMsg = "package " + packageSpec.packageName;
    builder.AddNote("default modifier of 'package' is 'public', you can use '" + packageMsg + "' instead");
}
} // namespace Cangjie::Modules
