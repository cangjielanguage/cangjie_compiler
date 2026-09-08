// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 * This file implements CJMP (Cangjie Multi-Platform) related loading functions.
 */

#ifndef CANGJIE_MODULES_ASTSERIALIZATION_ASTLOADER_CJMP_H
#define CANGJIE_MODULES_ASTSERIALIZATION_ASTLOADER_CJMP_H

#include "ASTLoaderImpl.h"

#include "cangjie/AST/Node.h"
#include "cangjie/Basic/DiagnosticEngine.h"

#include "ASTSerializeUtils.h"
#include "flatbuffers/CjoFormat_generated.h"

namespace Cangjie {

namespace ASTLoaderCJMP {

OwnedPtr<AST::FeaturesDirective> LoadFeaturesDirective(const PackageFormat::FeaturesDirective* raw);

void ReportPackageMismatch(
    DiagnosticEngine& diag, AST::Package& node, std::string& expectedPackageName, std::string& actualPackageName);

std::set<std::string> CollectFeaturesFromPackage(AST::Package& pkg);

void ValidateCommonSpecificFeatureSetsRelations(
    const AST::File& commonFile,
    const std::set<std::string>& specificFeatures,
    DiagnosticEngine& diag,
    const AST::Package& pkg);

} // namespace ASTLoaderCJMP
} // namespace Cangjie

#endif
