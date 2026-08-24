// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the TypeManager related classes.
 */

#include "cangjie/Sema/TestManager.h"

#include <memory>

#include "Desugar/AfterTypeCheck.h"
#include "TypeCheckUtil.h"
#include "cangjie/AST/Match.h"
#include "cangjie/Sema/GenericInstantiationManager.h"
#include "GenericInstantiation/GenericInstantiationManagerImpl.h"
#include "GenericInstantiation/PartialInstantiation.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/Driver/DriverOptions.h"

#include "MockManager.h"
#include "MockSupportManager.h"
#include "MockUtils.h"
#include "MockContext.h"

namespace Cangjie {

using namespace AST;
using namespace TypeCheckUtil;

static const std::string MOCK_ON_COMPILATION_OPTION = "--mock=on";
static const std::string TEST_COMPILATION_OPTION = "--test";

Ptr<Expr> DeparenthesizeExpr(Ptr<Expr> expr)
{
    if (auto parenExpr = As<ASTKind::PAREN_EXPR>(expr); parenExpr) {
        return DeparenthesizeExpr(parenExpr->expr);
    } else {
        return expr;
    }
}

bool IsAnyTypeParamUsedInTypeArgs(
    std::vector<OwnedPtr<GenericParamDecl>>& typeParams,
    std::vector<OwnedPtr<Type>>& typeArgs)
{
    for (auto& typeParam : typeParams) {
        for (auto& typeArg : std::as_const(typeArgs)) {
            if (typeArg->GetTy()->Contains(typeParam->DataTy())) {
                return true;
            }
        }
    }
    return false;
}

TestManager::TestManager(
    ImportManager& im, TypeManager& tm, DiagnosticEngine& diag, const GlobalOptions& compilationOptions)
    : ctx(MakeOwned<MockContext>()),
      importManager(im),
      typeManager(tm),
      diag(diag),
      testEnabled(compilationOptions.enableCompileTest),
      mockMode(compilationOptions.mock),
      mockCompatibleIfNeeded(testEnabled && mockMode == MockMode::DEFAULT),
      mockCompatible(mockCompatibleIfNeeded || mockMode == MockMode::ON),
      exportForTest(compilationOptions.exportForTest)
{
    mockUtils = new MockUtils(importManager, typeManager, diag, ctx->mangler);

    if (!mockCompatible) {
        return;
    }

    mockSupportManager = MakeOwned<MockSupportManager>(typeManager, diag, mockUtils);

    if (mockCompatible && testEnabled) {
        mockManager = MakeOwned<MockManager>(importManager, typeManager, mockUtils);
    }
}

void TestManager::ReportDoesntSupportMocking(
    const Expr& reportOn, const std::string& name, const std::string& package)
{
    diag.DiagnoseRefactor(
        DiagKindRefactor::sema_mock_doesnt_support_mocking,
        reportOn,
        name,
        package,
        MOCK_ON_COMPILATION_OPTION);
}

void TestManager::ReportDoesntSupportFrozen(const Expr& reportOn)
{
    diag.DiagnoseRefactor(DiagKindRefactor::sema_mock_frozen_unsupported, reportOn);
}

void TestManager::ReportFrozenRequired(const FuncDecl& reportOn)
{
    diag.DiagnoseRefactor(DiagKindRefactor::sema_mock_frozen_required, reportOn, reportOn.identifier);
}

void TestManager::ReportUnsupportedType(const Expr& reportOn)
{
    diag.DiagnoseRefactor(DiagKindRefactor::sema_mock_unsupported_type, reportOn);
}

void TestManager::ReportNotInTestMode(const Expr& reportOn)
{
    diag.DiagnoseRefactor(DiagKindRefactor::sema_mock_not_in_test_mode, reportOn, TEST_COMPILATION_OPTION);
}

void TestManager::ReportMockDisabled(const Expr& reportOn)
{
    diag.DiagnoseRefactor(DiagKindRefactor::sema_mock_disabled, reportOn, MOCK_ON_COMPILATION_OPTION);
}

void TestManager::ReportWrongStaticDecl(const Expr& reportOn)
{
    if (auto lambda = DynamicCast<LambdaExpr>(&reportOn)) {
        // excluding @EnsurePreparedToMock itself from error position
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_mock_wrong_static_decl, MakeRange(lambda->funcBody->body->leftCurlPos, lambda->end));
        return;
    }
    diag.DiagnoseRefactor(
        DiagKindRefactor::sema_mock_wrong_static_decl, reportOn);
}

bool TestManager::IsDeclOpenToMock(const Decl& decl)
{
    return MockSupportManager::IsDeclOpenToMock(decl);
}

bool TestManager::IsDeclGeneratedForTest(const Decl& decl)
{
    return MockUtils::IsMockAccessor(decl) || MockManager::IsMockClass(decl);
}

bool TestManager::IsMockAccessor(const Decl& decl)
{
    return MockUtils::IsMockAccessor(decl);
}

bool IsLocalDecl(const Decl& decl)
{
    return (decl.outerDecl == nullptr || !decl.outerDecl->IsNominalDecl()) && !decl.TestAttr(Attribute::GLOBAL);
}

VisitAction TestManager::CollectToCreateMockCalls(CallExpr& callExpr, std::vector<Ptr<CallExpr>>& callExprs)
{
    bool isMockCall = MockManager::IsMockCall(callExpr);
    if (!isMockCall || (callExpr.GetTy() && callExpr.GetTy()->HasGeneric())) {
        if (isMockCall) {
            callExpr.desugarExpr =
                MockManager::CreateIllegalMockCallException(*callExpr.curFile, typeManager, importManager);
        }
        return VisitAction::WALK_CHILDREN;
    }

    // The first type argument is a declaration to mock
    auto typeArgument = callExpr.baseFunc->GetTy()->typeArgs[1];
    if (!typeArgument->IsClass() && !typeArgument->IsInterface()) {
        ReportUnsupportedType(callExpr);
        return VisitAction::SKIP_CHILDREN;
    }

    if (mockCompatible && testEnabled) {
        callExprs.push_back(&callExpr);
    } else if (mockMode == MockMode::RUNTIME_ERROR && testEnabled) {
        callExpr.desugarExpr = MockManager::CreateIllegalMockCallException(
            *callExpr.curFile, typeManager, importManager);
    } else if (!testEnabled) {
        ReportNotInTestMode(callExpr);
    } else {
        ReportMockDisabled(callExpr);
    }
    return VisitAction::WALK_CHILDREN;
}

void TestManager::CreateMockCalls(Package& pkg, const std::vector<Ptr<CallExpr>>& callExprs)
{
    for (auto callExpr : callExprs) {
        auto mockClass = GenerateMockClassIfNeededAndGet(*callExpr, pkg);
        if (!mockClass) {
            continue;
        }

        std::vector<ModalTy> valueParamTys;
        valueParamTys.emplace_back(callExpr->args[0]->GetTy());

        if (MockManager::GetMockKind(*callExpr) == MockKind::SPY) {
            valueParamTys.emplace_back(mockClass->GetTy());
        }

        // The first type argument is a declaration to mock
        auto typeArgument = callExpr->baseFunc->GetTy()->typeArgs[1];

        callExpr->desugarExpr = MockManager::CreateInitCallOfMockClass(
            *mockClass, callExpr->args, typeManager, typeArgument->typeArgs, valueParamTys);
    }
}

namespace {

bool ShouldHandleMockAnnotatedLambdaValue(Ptr<Decl> target)
{
    bool isInExtend = target->TestAttr(Attribute::IN_EXTEND);
    bool isInInterfaceWithDefault =
        target->outerDecl && target->outerDecl->astKind == ASTKind::INTERFACE_DECL &&
        target->TestAttr(Attribute::DEFAULT);

    return isInExtend || isInInterfaceWithDefault || target->IsStaticOrGlobal();
}

} // namespace


void TestManager::WrapWithRequireMockObjectIfNeeded(Ptr<Expr> expr, Ptr<Decl> target)
{
    // For non-static/non-global decls, generate an assertion that their receiver is a real mock object
    if (target->IsStaticOrGlobal()) {
        return;
    }

    auto memberAccess = ExtractMemberAccessFromExpr(expr);
    CJC_ASSERT(memberAccess);
    mockManager->WrapWithRequireMockObject(*memberAccess->baseExpr.get());
}

Ptr<MemberAccess> TestManager::ExtractMemberAccessFromExpr(Ptr<AST::Expr> expr)
{
    if (auto callExpr = As<ASTKind::CALL_EXPR>(expr->desugarExpr ? expr->desugarExpr : expr)) {
        // After preparing decls and calls in MockSupportManager,
        // all exprs inside @EnsurePreparedToMock-marked lambda
        // should be represented as a call expr (either direct calling or through func accessor)
        return As<ASTKind::MEMBER_ACCESS>(callExpr->baseFunc);
    }

    if (auto block = As<ASTKind::BLOCK>(expr->desugarExpr)) {
        // AssignExpr may contain a block in desugarExpr with two nodes,
        // because the mock transformation reorders some operations.
        //
        // Example:
        //   B().b = C().c
        // is transformed into:
        //   B().b$set$ToMock_B(C().c$get$ToMock_C())
        //
        // To preserve correct evaluation order, we rewrite it as:
        //   var tmp = C().c$get$ToMock_C();
        //   B().b$set$ToMock_B(tmp);
        //
        // See issue: https://gitcode.com/Cangjie/cangjie_compiler/issues/787
        CJC_ASSERT(block->body.size() == 2);
        auto callExpr = As<ASTKind::CALL_EXPR>(block->body[1]);
        CJC_ASSERT(callExpr);
        return As<ASTKind::MEMBER_ACCESS>(callExpr->baseFunc);
    }

    return nullptr;
}

VisitAction TestManager::CollectToMockAnnotatedLambdas(const LambdaExpr& lambda, std::vector<Ptr<Expr>>& inLambdaExprs)
{
    if (!lambda.TestAttr(Attribute::MOCK_SUPPORTED) || (mockMode == MockMode::RUNTIME_ERROR && testEnabled)) {
        return VisitAction::WALK_CHILDREN;
    }

    if (!testEnabled) {
        ReportNotInTestMode(lambda);
        return VisitAction::WALK_CHILDREN;
    } else if (!mockCompatible) {
        ReportMockDisabled(lambda);
        return VisitAction::WALK_CHILDREN;
    }

    auto lastExpr = As<ASTKind::RETURN_EXPR>(lambda.funcBody->body->GetLastExprOrDecl());
    if (!lastExpr) {
        return VisitAction::WALK_CHILDREN;
    }

    auto expr = DeparenthesizeExpr(lastExpr->expr);

    Ptr<Decl> lastExprTarget = nullptr;

    if (auto assignExpr = As<ASTKind::ASSIGN_EXPR>(expr); assignExpr) {
        lastExprTarget = assignExpr->leftValue->GetTarget();
    } else if (auto callExpr = As<ASTKind::CALL_EXPR>(expr); callExpr) {
        lastExprTarget = callExpr->resolvedFunction;
    } else {
        lastExprTarget = expr->GetTarget();
    }

    if (!lastExprTarget) {
        return VisitAction::WALK_CHILDREN;
    }

    if (lastExprTarget->TestAnyAttr(Attribute::PRIVATE, Attribute::CONSTRUCTOR) ||
        IsLocalDecl(*lastExprTarget) || lastExprTarget->IsConst() ||
        (lastExprTarget->outerDecl && lastExprTarget->outerDecl->TestAttr(Attribute::PRIVATE))
    ) {
        ReportWrongStaticDecl(lambda);
        return VisitAction::WALK_CHILDREN;
    }

    if (auto funcDecl = As<ASTKind::FUNC_DECL>(lastExprTarget); funcDecl && funcDecl->isFrozen) {
        ReportDoesntSupportFrozen(lambda);
        return VisitAction::WALK_CHILDREN;
    }

    WrapWithRequireMockObjectIfNeeded(expr, lastExprTarget);

    if (!ShouldHandleMockAnnotatedLambdaValue(lastExprTarget)) {
        return VisitAction::WALK_CHILDREN;
    }

    if (!lastExprTarget->TestAttr(Attribute::MOCK_SUPPORTED)) {
        ReportDoesntSupportMocking(*expr, lastExprTarget->identifier, lastExprTarget->fullPackageName);
        return VisitAction::SKIP_CHILDREN;
    }

    inLambdaExprs.push_back(expr);

    return VisitAction::WALK_CHILDREN;
}

void TestManager::HandleCreateMock(Package& pkg)
{
    std::vector<Ptr<AST::CallExpr>> callExprs;
    mockUtils->Walk(&pkg, [this, &pkg, &callExprs](auto node) {
        if (!node->IsSamePackage(pkg)) {
            return VisitAction::WALK_CHILDREN;
        }
        if (auto callExpr = As<ASTKind::CALL_EXPR>(node); callExpr) {
            return CollectToCreateMockCalls(*callExpr, callExprs);
        }
        if (auto funcDecl = As<ASTKind::FUNC_DECL>(node); funcDecl &&
            funcDecl->TestAttr(Attribute::CONTAINS_MOCK_CREATION_CALL) && funcDecl->funcBody &&
            funcDecl->funcBody->generic && !funcDecl->HasAnno(AnnotationKind::FROZEN)) {
            ReportFrozenRequired(*funcDecl);
        }

        return VisitAction::WALK_CHILDREN;
    });

    CreateMockCalls(pkg, callExprs);
}

void TestManager::HandleEnsurePreparedToMock(Package& pkg)
{
    std::vector<Ptr<AST::Expr>> inLambdaExprs;
    mockUtils->Walk(&pkg, [this, &pkg, &inLambdaExprs](auto node) {
        if (!node->IsSamePackage(pkg)) {
            return VisitAction::WALK_CHILDREN;
        }
        if (auto lambda = As<ASTKind::LAMBDA_EXPR>(node); lambda) {
            return CollectToMockAnnotatedLambdas(*lambda, inLambdaExprs);
        }

        return VisitAction::WALK_CHILDREN;
    });

    for (auto expr : inLambdaExprs) {
        mockManager->HandleMockAnnotatedLambdaValue(*expr);
    }
}


Ptr<ClassDecl> TestManager::GenerateMockClassIfNeededAndGet(const CallExpr& callExpr, Package& pkg)
{
    auto typeArgument = callExpr.baseFunc->GetTy()->typeArgs[1];
    if (!typeArgument->IsClass() && !typeArgument->IsInterface()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_mock_unsupported_type, callExpr);
        return nullptr;
    }

    auto declToMock =
        RawStaticCast<ClassLikeDecl*>(Ty::GetDeclOfTy(DynamicCast<const ClassLikeTy*>(typeArgument.get())));
    if (MockSupportManager::DoesClassLikeSupportMocking(*declToMock)) {
        auto [classDecl, generated] = mockManager->GenerateMockClassIfNeededAndGet(
            *declToMock, pkg, MockManager::GetMockKind(callExpr));
        if (generated) {
            CJC_ASSERT(classDecl);
            if (auto ifaceDecl = DynamicCast<InterfaceDecl>(declToMock)) {
                mockSupportManager->PrepareClassLikeWithDefaults(*classDecl, *ifaceDecl, nullptr);
            }
        }
        return classDecl;
    } else {
        auto packageName =
            declToMock->genericDecl ? declToMock->genericDecl->fullPackageName : declToMock->fullPackageName;
        ReportDoesntSupportMocking(callExpr, Ty::ToString(typeArgument.Ty()), packageName);
        return nullptr;
    }
}

namespace {

bool ShouldPrepareDecl(Node& node, const Package& pkg)
{
    if (!node.curFile) {
        return false;
    }

    if (node.curFile->curPackage != &pkg) {
        if (auto decl = As<ASTKind::DECL>(&node); decl && decl->genericDecl &&
            decl->genericDecl->TestAttr(Attribute::MOCK_SUPPORTED)) {
            return true;
        }
        return false;
    } else {
        if (auto decl = As<ASTKind::DECL>(&node); decl && decl->genericDecl) {
            // Not preparing instantiated decls from current package
            // Will prepare them when encounter their generic decl
            return false;
        }
    }

    return MockUtils::CanMock(node);
}

}

void TestManager::PrepareDecls(Package& pkg)
{
    CJC_ASSERT(mockSupportManager);

    MockSupportManager::DeclsToPrepare decls;

    mockUtils->Walk(&pkg, [this, &pkg, &decls](auto node) {
        if (!node->curFile) {
            return VisitAction::WALK_CHILDREN;
        }

        if (!ShouldPrepareDecl(*node, pkg)) {
            return VisitAction::SKIP_CHILDREN;
        }

        if (auto decl = As<ASTKind::DECL>(node); decl) {
            mockSupportManager->CollectDeclsToPrepare(*decl, decls);
            return VisitAction::SKIP_CHILDREN;
        }

        return VisitAction::WALK_CHILDREN;
    });

    mockSupportManager->PrepareDecls(std::move(decls));
}

void TestManager::GenerateAccessors(Package& pkg)
{
    CJC_ASSERT(mockSupportManager);

    std::vector<Ptr<Decl>> decls;
    mockUtils->Walk(&pkg, [&pkg, &decls](auto node) {
        if (!node->IsSamePackage(pkg) || Is<ExtendDecl>(node)) {
            return VisitAction::SKIP_CHILDREN;
        }

        auto decl = As<ASTKind::DECL>(node);

        if (!decl) {
            return VisitAction::WALK_CHILDREN;
        }

        if (!MockUtils::CanMock(*node)) {
            return VisitAction::SKIP_CHILDREN;
        }

        decls.push_back(decl);

        return VisitAction::SKIP_CHILDREN;
    });
    for (auto decl : decls) {
        mockSupportManager->GenerateAccessors(*decl);
    }
}

void TestManager::PrepareToSpy(Package& pkg)
{
    CJC_ASSERT(mockSupportManager);

    mockSupportManager->GenerateSpyCallMarker(pkg);

    std::vector<Ptr<Decl>> decls;
    mockUtils->Walk(&pkg, [&pkg, &decls](auto node) {
        if (!node->IsSamePackage(pkg) || Is<ExtendDecl>(node)) {
            return VisitAction::SKIP_CHILDREN;
        }

        auto decl = As<ASTKind::DECL>(node);

        if (!decl) {
            return VisitAction::WALK_CHILDREN;
        }

        if (!MockUtils::CanMock(*decl)) {
            return VisitAction::SKIP_CHILDREN;
        }

        if (decl->curFile && decl->curFile->curPackage->fullPackageName == pkg.fullPackageName) {
            decls.push_back(decl);
        }

        return VisitAction::SKIP_CHILDREN;
    });
    for (auto decl : decls) {
        mockSupportManager->PrepareToSpy(*decl);
    }
}

void TestManager::ReplaceCallsToForeignFunctions(Package& pkg)
{
    CJC_ASSERT(mockSupportManager);

    mockUtils->Walk(&pkg, [this](const Ptr<Node> node) {
        auto declNode = As<ASTKind::FUNC_DECL>(node.get());
        if (declNode && declNode->TestAttr(Attribute::GENERATED_TO_MOCK)) {
            return VisitAction::SKIP_CHILDREN;
        }

        Ptr<RefExpr> refNode = As<ASTKind::REF_EXPR>(node.get());
        if (!refNode) {
            return VisitAction::WALK_CHILDREN;
        }

        auto target = refNode->ref.target;
        Ptr<FuncDecl> funcDecl = As<ASTKind::FUNC_DECL>(target);
        if (!funcDecl || !funcDecl->TestAttr(Attribute::FOREIGN)) {
            return VisitAction::SKIP_CHILDREN;
        }

        Ptr<Decl> accessorDecl = mockUtils->FindMockGlobalDecl(*funcDecl, MockUtils::GetForeignAccessorName(*funcDecl));
        if (!accessorDecl) {
            return VisitAction::SKIP_CHILDREN;
        }
        CJC_ASSERT(Is<FuncDecl>(accessorDecl));

        refNode->ref = Reference(accessorDecl->identifier);
        refNode->ref.target = accessorDecl;

        if (Ptr<CallExpr> callNode = As<ASTKind::CALL_EXPR>(refNode->callOrPattern)) {
            callNode->resolvedFunction = StaticCast<FuncDecl>(accessorDecl);
        }

        return VisitAction::SKIP_CHILDREN;
    });
}

namespace {

bool IsMockAnnotedLambda(Ptr<Node> node)
{
    return node->astKind == ASTKind::LAMBDA_EXPR && node->TestAttr(Attribute::MOCK_SUPPORTED);
}

} // namespace

void TestManager::ReplaceCallsWithAccessors(Package& pkg)
{
    CJC_ASSERT(mockSupportManager);

    bool isInConstructor = false;
    bool isInMockAnnotatedLambda = false;
    ModalTy outerTy;

    mockUtils->Walk(&pkg, [this, &isInConstructor, &isInMockAnnotatedLambda, &outerTy, &pkg](const Ptr<Node> node) {
        if (node->astKind == ASTKind::PRIMARY_CTOR_DECL) {
            // Primary init has been already desugared to regular init
            return VisitAction::SKIP_CHILDREN;
        }

        if (IsMockAnnotedLambda(node)) {
            isInMockAnnotatedLambda = true;
        }

        if (auto inheritableDecl = DynamicCast<InheritableDecl>(node)) {
            CJC_ASSERT(!outerTy);
            outerTy = inheritableDecl->GetTy();
        } else if (auto extendDecl = DynamicCast<ExtendDecl>(node)) {
            CJC_ASSERT(!outerTy);
            outerTy = extendDecl->extendedType->GetTy();
        }

        if ((node->curFile && !node->IsSamePackage(pkg))) {
            return VisitAction::SKIP_CHILDREN;
        }

        if (!MockSupportManager::NeedToSearchCallsToReplaceWithAccessors(*node)) {
            return VisitAction::SKIP_CHILDREN;
        }

        if (node->TestAttr(Attribute::CONSTRUCTOR)) {
            isInConstructor = true;
            return VisitAction::WALK_CHILDREN;
        }

        if (auto expr = As<ASTKind::EXPR>(node); expr) {
            mockSupportManager->ReplaceExprWithAccessor(*expr, isInConstructor);
            mockSupportManager->ReplaceInterfaceDefaultFunc(*expr, outerTy, isInMockAnnotatedLambda);
        }

        return VisitAction::WALK_CHILDREN;
    }, [&isInConstructor, &isInMockAnnotatedLambda, &outerTy](const Ptr<Node> node) {
        if (node->TestAttr(Attribute::CONSTRUCTOR)) {
            isInConstructor = false;
        }
        if (IsMockAnnotedLambda(node)) {
            isInMockAnnotatedLambda = false;
        }
        if (auto inheritableDecl = DynamicCast<InheritableDecl>(node)) {
            CJC_ASSERT(outerTy == inheritableDecl->GetTy());
            outerTy = {};
        } else if (auto extendDecl = DynamicCast<ExtendDecl>(node)) {
            CJC_ASSERT(outerTy == extendDecl->extendedType->GetTy());
            outerTy = {};
        }
        return VisitAction::KEEP_DECISION;
    });
}

bool TestManager::ArePackagesMockSupportConsistent(
    const Package& currentPackage, const Package& importedPackage)
{
    auto isCurrentSupportMock = currentPackage.TestAttr(Attribute::MOCK_SUPPORTED);
    auto isImportedSupportMock = importedPackage.TestAttr(Attribute::MOCK_SUPPORTED);
    if (!isImportedSupportMock) {
        // It's ok to have mock-incompatible dependencies,
        // the error would be reported in the actual case of attempt to mock something from such dependency
        return true;
    }

    return isCurrentSupportMock && isImportedSupportMock;
}

void TestManager::CheckIfNoMockSupportDependencies(const Package& curPkg)
{
    for (auto pkg: importManager.GetAllImportedPackages(true)) {
        if (&curPkg != pkg->srcPackage && !ArePackagesMockSupportConsistent(curPkg, *(pkg->srcPackage))) {
            diag.DiagnoseRefactor(
                DiagKindRefactor::package_mocking_support_inconsistency,
                DEFAULT_POSITION,
                pkg->srcPackage->fullPackageName,
                MOCK_ON_COMPILATION_OPTION);
        }
    }
}

/*
 * It marks all generic functions which call createMock/createSpy with their generic parameters,
 * or call other such functions.
 * Further those marks are used:
 *  1) to validate that createMock / createSpy calls are used in the "frozen context"
 *      which means all generic functions in the chain of generic calls should be frozen
 *  2) to force type instantiation if the marked function also has @Frozen anno
 */
void TestManager::MarkMockCreationContainingGenericFuncs(Package& pkg) const
{
    bool hasDeclsToCheckUsages = true;

    while (hasDeclsToCheckUsages) {
        hasDeclsToCheckUsages = false;

        Ptr<FuncDecl> enclosingGenericFunc = nullptr;
        mockUtils->Walk(&pkg, [this, &enclosingGenericFunc, &hasDeclsToCheckUsages](auto node) {
            if (auto funcDecl = As<ASTKind::FUNC_DECL>(node);
                funcDecl && funcDecl->funcBody && funcDecl->funcBody->generic
            ) {
                if (funcDecl->TestAttr(Attribute::CONTAINS_MOCK_CREATION_CALL)) {
                    return VisitAction::SKIP_CHILDREN;
                }
                enclosingGenericFunc = funcDecl;
            }
            if (auto callExpr = As<ASTKind::CALL_EXPR>(node); callExpr && enclosingGenericFunc &&
                ShouldBeMarkedAsContainingMockCreationCall(*callExpr, enclosingGenericFunc)
            ) {
                enclosingGenericFunc->EnableAttr(Attribute::CONTAINS_MOCK_CREATION_CALL);
                hasDeclsToCheckUsages = true;
            }
            return VisitAction::WALK_CHILDREN;
        }, [&enclosingGenericFunc](const Ptr<Node> node) {
            if (auto funcDecl = As<ASTKind::FUNC_DECL>(node);
                funcDecl && funcDecl->funcBody && funcDecl->funcBody->generic
            ) {
                enclosingGenericFunc = nullptr;
            }
            return VisitAction::KEEP_DECISION;
        });
    }
}

void TestManager::HandleDeclsToExportForTest(std::vector<Ptr<Package>> pkgs) const
{
    if (!exportForTest) {
        return;
    }
    for (auto& pkg : pkgs) {
        BaseMangler mangler;
        auto manglerCtx = mangler.PrepareContextForPackage(pkg);
        mangler.CollectLocalDecls(*manglerCtx, *pkg);
        auto isInExtend = false;

        mockUtils->Walk(pkg, [&mangler, &isInExtend](auto node) {
            if (auto ed = As<ASTKind::EXTEND_DECL>(node); ed && !ed->TestAttr(Attribute::IMPORTED)) {
                isInExtend = true;
            }
            if (auto d = As<ASTKind::DECL>(node); d &&
                !d->TestAttr(Attribute::PRIVATE) &&
                (d->IsFuncOrProp() || Is<FuncParam>(d) || Is<ExtendDecl>(d)) &&
                (isInExtend || d->TestAttr(Attribute::FOREIGN))
            ) {
                d->mangledName = mangler.Mangle(*d);
            }
            return VisitAction::WALK_CHILDREN;
        }, [&isInExtend](const Ptr<Node> node) {
            if (Is<ExtendDecl>(node) && !node->TestAttr(Attribute::IMPORTED)) {
                isInExtend = false;
            }
            return VisitAction::KEEP_DECISION;
        });

        if (manglerCtx) {
            manglerCtx.reset();
        }
    }
}

void TestManager::CollectInternalDeclUsages(Package& pkg)
{
    mockUtils->Walk(&pkg, [&pkg, this](auto node) {
        Ptr<Decl> target;
        if (auto ma = As<ASTKind::MEMBER_ACCESS>(node); ma) {
            target = ma->target;
        }
        if (auto re = As<ASTKind::REF_EXPR>(node); re) {
            target = re->ref.target;
        }
        if (!target) {
            return VisitAction::WALK_CHILDREN;
        }
        if (target->fullPackageName != pkg.fullPackageName) {
            return VisitAction::WALK_CHILDREN;
        }
        if (MockUtils::IsMockAccessorRequired(*target) && target->linkage == Linkage::INTERNAL) {
            mockSupportManager->WriteUsedInternalDecl(*target);
        }
        return VisitAction::WALK_CHILDREN;
    });
}

bool TestManager::ShouldBeMarkedAsContainingMockCreationCall(
    const CallExpr& callExpr, const Ptr<FuncDecl> enclosingFunc) const
{
    auto resolvedFunc = callExpr.resolvedFunction;
    if (!resolvedFunc || !resolvedFunc->funcBody || !resolvedFunc->funcBody->generic ||
        !enclosingFunc->funcBody || !enclosingFunc->funcBody->generic
    ) {
        return false; // outside generics, mock creation calls (createMock / createSpy) can be used without restrictions
    }

    if (!MockManager::IsMockCall(callExpr) && !resolvedFunc->TestAttr(Attribute::CONTAINS_MOCK_CREATION_CALL)) {
        return false;
    }

    if (auto nre = DynamicCast<NameReferenceExpr>(callExpr.baseFunc.get())) {
        return IsAnyTypeParamUsedInTypeArgs(enclosingFunc->funcBody->generic->typeParameters, nre->typeArguments);
    } else {
        return false;
    }
}

void TestManager::MarkDeclsForTestIfNeeded(const std::vector<Ptr<Package>>& pkgs) const
{
    HandleDeclsToExportForTest(pkgs);
    for (auto& pkg : pkgs) {
        MarkMockCreationContainingGenericFuncs(*pkg);
        if (mockMode != MockMode::ON && (!mockCompatibleIfNeeded || !IsThereMockUsage(*pkg))) {
            continue;
        }

        mockUtils->Walk(pkg, [this](auto node) {
            mockSupportManager->MarkNodeMockSupportedIfNeeded(*node);
            return VisitAction::WALK_CHILDREN;
        });
    }
}

bool TestManager::IsThereMockUsage(Package& pkg) const
{
    bool mockUsageFound = false;

    mockUtils->Walk(&pkg, [&pkg, &mockUsageFound](auto node) {
        if (auto callExpr = As<ASTKind::CALL_EXPR>(node); callExpr && callExpr->IsSamePackage(pkg)) {
            auto resolvedFunc = callExpr->resolvedFunction;
            if (MockManager::IsMockCall(*callExpr) ||
                (resolvedFunc && resolvedFunc->TestAttr(Attribute::CONTAINS_MOCK_CREATION_CALL))
            ) {
                mockUsageFound = true;
                return VisitAction::STOP_NOW;
            }
        }
        if (auto lambdaExpr = As<ASTKind::LAMBDA_EXPR>(node);
            lambdaExpr && lambdaExpr->IsSamePackage(pkg) && lambdaExpr->TestAttr(Attribute::MOCK_SUPPORTED)
        ) {
            mockUsageFound = true;
            return VisitAction::STOP_NOW;
        }
        return VisitAction::WALK_CHILDREN;
    });

    if (mockUsageFound) {
        return true;
    }

    for (auto importedPkg: importManager.GetAllImportedPackages(true)) {
        if (&pkg != importedPkg->srcPackage && importedPkg->srcPackage->TestAttr(Attribute::MOCK_SUPPORTED)) {
            return true;
        }
    }

    return false;
}

void TestManager::PrepareToMock(const std::vector<Ptr<AST::Package>>& pkgs)
{
    for (auto& pkg : pkgs) {
        PrepareToMock(*pkg);
    }
}

void TestManager::PrepareToMock(AST::Package& pkg)
{
    if (pkg.files.empty()) {
        return;
    }
    // FIXME: Load decls lazy
    if (mockUtils) {
        mockUtils->LoadStdDecls();
    }
    if (mockManager) {
        mockManager->LoadMockLibDecls();
    }

    if (mockMode == MockMode::ON || (mockCompatibleIfNeeded && IsThereMockUsage(pkg))) {
        ctx->PrepareManglerContext(&pkg);

        mockUtils->SetGetTypeForTypeParamDecl(pkg);

        // NOTE: In almost every stage there is a AST walk.
        // Do it once, collecting all nodes, that needs to be handled in some way
        CollectInternalDeclUsages(pkg);
        GenerateAccessors(pkg);
        PrepareToSpy(pkg);
        PrepareDecls(pkg);
        ReplaceCallsWithAccessors(pkg);
        ReplaceCallsToForeignFunctions(pkg);
    } else {
        CheckIfNoMockSupportDependencies(pkg);
    }
    HandleEnsurePreparedToMock(pkg);
}

TestManager::~TestManager()
{
    if (mockUtils != nullptr) {
        delete mockUtils;
        mockUtils = nullptr;
    }
}

} // namespace Cangjie
