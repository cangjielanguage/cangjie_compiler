// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the DefaultCompilerInstance, which performs the default compile flow.
 */

#ifndef CANGJIE_FRONTEND_DEFAULTCOMPILERINSTANCE_H
#define CANGJIE_FRONTEND_DEFAULTCOMPILERINSTANCE_H

#include "cangjie/Frontend/CompilerInstance.h"

namespace Cangjie {
namespace CodeGen {
    class CGModule;
}
class DefaultCompilerInstance : public CompilerInstance {
public:
    DefaultCompilerInstance(CompilerInvocation& invocation, DiagnosticEngine& diag);
    ~DefaultCompilerInstance() override;
    bool PerformParse() override;
    bool PerformImportPackage() override;
    bool PerformConditionCompile() override;
    bool PerformMacroExpand() override;
    bool PerformSema() override;
    bool PerformOverflowStrategy() override;
    bool PerformDesugarAfterSema() override;
    bool PerformGenericInstantiation() override;
    bool PerformCHIRCompilation() override;
    bool PerformCodeGen() override;
    bool PerformCjoAndBchirSaving() override;

    bool PerformMangling() override;
    void DumpDepPackage();

protected:
    bool SaveCjoAndBchir(AST::Package& pkg) const;
    bool SaveCjo(const AST::Package& pkg) const;
    void RearrangeImportedPackageDependence() const;
    bool CodegenOnePackage(AST::Package& pkg, bool enableIncrement) const;

private:
    class DefaultCIImpl* impl;
};
} // namespace Cangjie

#endif // CANGJIE_FRONTEND_DEFAULTCOMPILERINSTANCE_H
