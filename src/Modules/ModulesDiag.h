// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares diagnostic related functions for Modules.
 */

#ifndef CANGJIE_MODULES_DIAGS_H
#define CANGJIE_MODULES_DIAGS_H

#include <string>

#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Modules/ImportManager.h"

namespace Cangjie::Modules {
using namespace AST;
void WarnUselessImport(DiagnosticEngine& diag, const Range& importRange, const Decl& decl);
void WarnConflictImport(DiagnosticEngine& diag, const std::string& name, const Range& current, const Range& previous);
void WarnRepeatedFeatureName(DiagnosticEngine& diag, std::string& name, const Range& current, const Range& previous);
void DiagForNullPackageFeature(DiagnosticEngine& diag, const Range& current, const Ptr<FeaturesDirective> refFeature);
void DiagForDifferentPackageFeatureConsistency(DiagnosticEngine& diag,
    const Ptr<FeaturesDirective> feature, const Ptr<FeaturesDirective> refFeature);
void DiagForDifferentPackageNames(DiagnosticEngine& diag,
    const std::map<std::pair<std::string, std::string>, std::pair<Position, bool>>& packageNamePosMap);
void DiagRootPackageModifier(DiagnosticEngine& diag, const PackageSpec& packageSpec);
} // namespace Cangjie::Modules

#endif
