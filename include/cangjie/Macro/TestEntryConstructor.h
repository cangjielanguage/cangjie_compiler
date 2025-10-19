// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Test Entry Constructor related classes, which provide to construct test entry ast.
 */

#ifndef CANGJIE_FRONTEND_TESTCONSTRUCTION_H
#define CANGJIE_FRONTEND_TESTCONSTRUCTION_H

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Node.h"

namespace Cangjie {
template <typename T> inline std::unique_ptr<T> MakeUniquePtr()
{
    auto ptr = std::make_unique<T>();
    ptr->EnableAttr(AST::Attribute::COMPILER_ADD);
    return ptr;
}
template <typename T> inline OwnedPtr<T> MakeOwnedNode()
{
    auto ptr = MakeOwned<T>();
    ptr->EnableAttr(AST::Attribute::COMPILER_ADD);
    return ptr;
}
class TestPackage {
public:
    explicit TestPackage(const std::string& packageName) : packageName(packageName) {};
    const std::string& packageName;
    std::vector<Ptr<AST::FuncDecl>> testRegisterFunctions;
};

class TestModule {
public:
    explicit TestModule(const std::string& moduleName) : moduleName(moduleName) {};
    const std::string& moduleName;
    std::vector<OwnedPtr<TestPackage>> testPackages;
};

class TestEntryConstructor {
public:
    explicit TestEntryConstructor(DiagnosticEngine& diag) : diag(diag) {};
    void CheckTestSuite(const std::vector<OwnedPtr<AST::Package>>& packages);
    static void ConstructTestSuite(const std::string& moduleName,
        std::vector<OwnedPtr<AST::Package>>& srcPkgs,
        const std::vector<Ptr<AST::PackageDecl>> importedPkgs, bool compileTestsOnly, bool mockEnabled);

    static bool IsTestRegistrationFunction(const Ptr<AST::Decl> funcDecl);
    DiagnosticEngine& diag;

private:
    void CheckTestSuiteConstraints(AST::Node& root, const std::vector<Ptr<AST::FuncDecl>>& funcs);
    void CheckClassWithMacro(AST::MacroExpandDecl& med);
    void CheckFunctionWithAtTest(
        AST::MacroExpandDecl& med, const std::vector<Ptr<AST::FuncDecl>>& funcs, const std::string& macroName);
    static void ConstructTestImports(AST::Package& pkg, TestModule& module);
    static void ConstructTestEntry(AST::Package& pkg, TestModule& module, bool mockEnabled);
    static Ptr<AST::Package> FindMainPartPkgForTestPkg(
        const Ptr<AST::Package> testPackage, std::vector<Ptr<AST::PackageDecl>> importedPkgs);
};
} // namespace Cangjie
#endif // CANGJIE_FRONTEND_TESTCONSTRUCTION_H
