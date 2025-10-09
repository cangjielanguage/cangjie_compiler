// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the TypeManager related classes, which manages all types.
 */

#ifndef CANGJIE_SEMA_TEST_MANAGER_H
#define CANGJIE_SEMA_TEST_MANAGER_H

#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Option/Option.h"
#include "cangjie/Sema/GenericInstantiationManager.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie {

class MockManager;
class MockSupportManager;
class MockUtils;

enum class MockKind : uint8_t {
    PLAIN_MOCK,
    SPY,
    NOT_MOCK
};

class TestManager {
public:
    explicit TestManager(
        ImportManager& im, TypeManager& tm, DiagnosticEngine& diag, const GlobalOptions& compilationOptions
    );
    void PreparePackageForTestIfNeeded(AST::Package& pkg);
    void MarkDeclsForTestIfNeeded(std::vector<Ptr<AST::Package>> pkgs) const;
    static bool IsDeclOpenToMock(const AST::Decl& decl);
    static bool IsDeclGeneratedForTest(const AST::Decl& decl);
    static bool IsMockAccessor(const AST::Decl& decl);
    void Init(GenericInstantiationManager* instantiationManager);
    ~TestManager();

private:
    ImportManager& importManager;
    TypeManager& typeManager;
    DiagnosticEngine& diag;
    const bool testEnabled;
    MockMode mockMode;
    const bool mockCompatibleIfNeeded;
    const bool mockCompatible;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    const bool exportForTest;
#endif
    OwnedPtr<MockManager> mockManager {nullptr};
    OwnedPtr<MockSupportManager> mockSupportManager {nullptr};
    Ptr<MockUtils> mockUtils {nullptr};

    Ptr<AST::ClassDecl> GenerateMockClassIfNeededAndGet(const AST::CallExpr& callExpr, AST::Package& pkg);
    void GenerateAccessors(AST::Package& pkg);
    void ReplaceCallsWithAccessors(AST::Package& pkg);
    void ReplaceCallsToForeignFunctions(AST::Package& pkg);
    void HandleMockCalls(AST::Package& pkg);
    Ptr<AST::ClassLikeDecl> GetInstantiatedDeclInCurrentPackage(const Ptr<const AST::ClassLikeTy> classLikeToMockTy);
    void CheckIfNoMockSupportDependencies(const AST::Package& curPkg);
    bool IsThereMockUsage(AST::Package& pkg) const;
    static bool ArePackagesMockSupportConsistent(
        const AST::Package& currentPackage, const AST::Package& importedPackage);
    AST::VisitAction HandleCreateMockCall(AST::CallExpr& callExpr, AST::Package& pkg);
    void WrapWithRequireMockObjectIfNeeded(Ptr<AST::Expr> expr, Ptr<AST::Decl> target);
    AST::VisitAction HandleMockAnnotatedLambda(const AST::LambdaExpr& lambda);
    void ReportDoesntSupportMocking(const AST::Expr& reportOn, const std::string& name, const std::string& package);
    void ReportUnsupportedType(const AST::Expr& reportOn);
    void ReportNotInTestMode(const AST::Expr& reportOn);
    void ReportMockDisabled(const AST::Expr& reportOn);
    void ReportWrongStaticDecl(const AST::Expr& reportOn);
    void PrepareDecls(AST::Package& pkg);
    void PrepareToSpy(AST::Package& pkg);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    void ReportFrozenRequired(const AST::FuncDecl& reportOn);
    void MarkMockCreationContainingGenericFuncs(AST::Package& pkg) const;
    bool ShouldBeMarkedAsContainingMockCreationCall(
        const AST::CallExpr& callExpr, const Ptr<AST::FuncDecl> enclosingFunc) const;
    void HandleDeclsToExportForTest(std::vector<Ptr<AST::Package>> pkgs) const;
#endif
};

} // namespace Cangjie

#endif // CANGJIE_SEMA_TYPE_MANAGER_H
