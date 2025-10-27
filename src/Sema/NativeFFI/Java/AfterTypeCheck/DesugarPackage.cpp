// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "JavaDesugarManager.h"
#include "JavaInteropManager.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Utils/ConstantsUtils.h"

namespace Cangjie::Interop::Java {

void JavaDesugarManager::ProcessJavaMirrorImplStage(DesugarJavaMirrorImplStage stage, File& file)
{
    switch (stage) {
        case DesugarJavaMirrorImplStage::MIRROR_GENERATE_STUB:
            GenerateInMirrors(file, true);
            break;
        case DesugarJavaMirrorImplStage::MIRROR_GENERATE:
            GenerateInMirrors(file, false);
            break;
        case DesugarJavaMirrorImplStage::IMPL_GENERATE:
            GenerateInJavaImpls(file);
            break;
        case DesugarJavaMirrorImplStage::MIRROR_DESUGAR:
            DesugarMirrors(file);
            break;
        case DesugarJavaMirrorImplStage::IMPL_DESUGAR:
            DesugarInJavaImpls(file);
            break;
        case DesugarJavaMirrorImplStage::TYPECHECKS:
            DesugarTypechecks(file);
            break;
        default:
            CJC_ABORT(); // unreachable state
    }

    std::move(generatedDecls.begin(), generatedDecls.end(), std::back_inserter(file.decls));
    generatedDecls.clear();
}

void JavaDesugarManager::ProcessCJImplStage(DesugarCJImplStage stage, File& file)
{
    switch (stage) {
        case DesugarCJImplStage::FWD_GENERATE:
            GenerateFwdClassInCJMapping(file);
            break;
        case DesugarCJImplStage::IMPL_GENERATE:
            GenerateInCJMapping(file);
            break;
        case DesugarCJImplStage::IMPL_DESUGAR:
            DesugarInCJMapping(file);
            break;
        case DesugarCJImplStage::TYPECHECKS:
            DesugarTypechecks(file);
            break;
        default:
            CJC_ABORT(); // unreachable state
    }

    std::move(generatedDecls.begin(), generatedDecls.end(), std::back_inserter(file.decls));
    generatedDecls.clear();
}

void JavaInteropManager::DesugarPackage(Package& pkg)
{
    if (!(hasMirrorOrImpl || targetInteropLanguage == GlobalOptions::InteropLanguage::Java)) {
        return;
    }
    JavaDesugarManager desugarer{importManager, typeManager, diag, mangler, javagenOutputPath, outputPath};

    if (hasMirrorOrImpl) {
        auto nbegin = static_cast<uint8_t>(DesugarJavaMirrorImplStage::BEGIN);
        auto nend = static_cast<uint8_t>(DesugarJavaMirrorImplStage::END);
        for (uint8_t nstage = nbegin; nstage != nend; nstage++) {
            auto stage = static_cast<DesugarJavaMirrorImplStage>(nstage);
            if (stage == DesugarJavaMirrorImplStage::BEGIN) {
                continue;
            }
            for (auto& file : pkg.files) {
                desugarer.ProcessJavaMirrorImplStage(stage, *file);
            }
        }
    }

    // Currently CJMapping is enable by compile config --enable-interop-cjmapping
    if (targetInteropLanguage == GlobalOptions::InteropLanguage::Java) {
        auto nbegin = static_cast<uint8_t>(DesugarCJImplStage::BEGIN);
        auto nend = static_cast<uint8_t>(DesugarCJImplStage::END);
        for (uint8_t nstage = nbegin; nstage != nend; nstage++) {
            auto stage = static_cast<DesugarCJImplStage>(nstage);
            if (stage == DesugarCJImplStage::BEGIN) {
                continue;
            }
            for (auto& file : pkg.files) {
                desugarer.ProcessCJImplStage(stage, *file);
            }
        }
    }
}

} // namespace Cangjie::Interop::Java
