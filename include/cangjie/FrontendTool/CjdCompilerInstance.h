// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the CjdCompilerInstance, which performs the default compile flow.
 */

#ifndef CANGJIE_FRONTEND_CJDCOMPILERINSTANCE_H
#define CANGJIE_FRONTEND_CJDCOMPILERINSTANCE_H

#include "cangjie/FrontendTool/DefaultCompilerInstance.h"
#include "cangjie/Utils/ProfileRecorder.h"

namespace Cangjie {
/// Compiler instance that compiles .cj.d file. This ci skips all stages after sema, except that it still produces a
/// cjo file in CjoAndBchirSaving.
class CjdCompilerInstance : public DefaultCompilerInstance {
public:
    CjdCompilerInstance(CompilerInvocation& invocation, DiagnosticEngine& diag)
        : DefaultCompilerInstance(invocation, diag)
    {
        buildTrie = false;
    }
    ~CjdCompilerInstance() override = default;
    ///@{
    /// After sema, skip all stages until cjo saving
    bool PerformDesugarAfterSema() override
    {
        return true;
    }
    bool PerformGenericInstantiation() override
    {
        return true;
    }
    bool PerformOverflowStrategy() override
    {
        return true;
    }
    // use DefaultCompilerInstance::PerformMangling
    bool PerformCHIRCompilation() override
    {
        return true;
    }
    bool PerformCodeGen() override
    {
        return true;
    }
    bool PerformCjoAndBchirSaving() override
    {
        Utils::ProfileRecorder recorder("Main Stage", "Save cjo");
        bool ret = true;
        for (auto& srcPkg : GetSourcePackages()) {
            ret = ret && SaveCjo(*srcPkg);
        }
        return ret;
    }
    ///@}
};
} // namespace Cangjie

#endif // CANGJIE_FRONTEND_CJDCOMPILERINSTANCE_H
