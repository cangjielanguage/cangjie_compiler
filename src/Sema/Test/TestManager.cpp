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
            if (typeArg->ty->Contains(typeParam->ty)) {
                return true;
            }
        }
    }
    return false;
}

TestManager::TestManager(
    ImportManager& im, TypeManager& tm, DiagnosticEngine& diag, const GlobalOptions& compilationOptions)
    : importManager(im),
      typeManager(tm),
      diag(diag),
      testEnabled(compilationOptions.enableCompileTest),
      mockMode(compilationOptions.mock),
      mockCompatibleIfNeeded(testEnabled && mockMode == MockMode::DEFAULT),
      mockCompatible(mockCompatibleIfNeeded || mockMode == MockMode::ON)
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
      , exportForTest(compilationOptions.exportForTest)
#endif
{}

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

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
void TestManager::ReportFrozenRequired(const FuncDecl& reportOn)
{
    diag.DiagnoseRefactor(DiagKindRefactor::sema_mock_frozen_required, reportOn, reportOn.identifier);
}
#endif

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

VisitAction TestManager::HandleCreateMockCall(CallExpr& callExpr, Package& pkg)
{
    bool isMockCall = MockManager::IsMockCall(callExpr);
    if (!isMockCall || (callExpr.ty && callExpr.ty->HasGeneric())) {
        if (isMockCall) {
            callExpr.desugarExpr =
                MockManager::CreateIllegalMockCallException(*callExpr.curFile, typeManager, importManager);
        }
        return VisitAction::WALK_CHILDREN;
    }

    // The first type argument is a declaration to mock
    auto typeArgument = callExpr.baseFunc->ty->typeArgs[1];

    if (!typeArgument->IsClass() && !typeArgument->IsInterface()) {
        ReportUnsupportedType(callExpr);
        return VisitAction::SKIP_CHILDREN;
    }

    if (mockCompatible && testEnabled) {
        auto mockClass = GenerateMockClassIfNeededAndGet(callExpr, pkg);
        if (!mockClass) {
            return VisitAction::WALK_CHILDREN;
        }

        std::vector<Ptr<Ty>> valueParamTys;
        valueParamTys.emplace_back(callExpr.args[0]->ty);

        if (MockManager::GetMockKind(callExpr) == MockKind::SPY) {
            valueParamTys.emplace_back(mockClass->ty);
        }

        callExpr.desugarExpr = MockManager::CreateInitCallOfMockClass(
            *mockClass, callExpr.args, typeManager, typeArgument->typeArgs, valueParamTys);
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


void TestManager::WrapWithRequireMockObjectIfNeeded(Ptr<AST::Expr> expr, Ptr<AST::Decl> target)
{
    // For non-static/non-global decls, generate an assertion that their receiver is a real mock object
    if (!target->IsStaticOrGlobal()) {
        auto callExpr = As<ASTKind::CALL_EXPR>(expr->desugarExpr ? expr->desugarExpr : expr);
        // After preparing decls and calls in MockSupportManager,
        // all exprs inside @EnsurePreparedToMock-marked lambda
        // should be represented as a call expr (either direct calling or through func accessor)
        CJC_ASSERT(callExpr);

        auto ma = As<ASTKind::MEMBER_ACCESS>(callExpr->baseFunc);
        // After desugaring, baseFunc for member decls should be always member access expression
        CJC_ASSERT(ma);

        mockManager->WrapWithRequireMockObject(*ma->baseExpr.get());
    }
}

VisitAction TestManager::HandleMockAnnotatedLambda(const LambdaExpr& lambda)
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

    mockManager->HandleMockAnnotatedLambdaValue(*expr);

    return VisitAction::WALK_CHILDREN;
}

void TestManager::HandleMockCalls(Package& pkg)
{
    Walker(&pkg, Walker::GetNextWalkerID(), [this, &pkg](auto node) {
        if (!node->IsSamePackage(pkg)) {
            return VisitAction::WALK_CHILDREN;
        }
        if (auto callExpr = As<ASTKind::CALL_EXPR>(node); callExpr) {
            return HandleCreateMockCall(*callExpr, pkg);
        } else if (auto lambda = As<ASTKind::LAMBDA_EXPR>(node); lambda) {
            return HandleMockAnnotatedLambda(*lambda);
        }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        if (auto funcDecl = As<ASTKind::FUNC_DECL>(node); funcDecl &&
            funcDecl->TestAttr(Attribute::CONTAINS_MOCK_CREATION_CALL) && funcDecl->funcBody &&
            funcDecl->funcBody->generic && !funcDecl->HasAnno(AnnotationKind::FROZEN)) {
            ReportFrozenRequired(*funcDecl);
        }
#endif
        return VisitAction::WALK_CHILDREN;
    }).Walk();

    if (mockCompatible && testEnabled) {
        mockManager->WriteGeneratedClasses();
    }
}

Ptr<ClassDecl> TestManager::GenerateMockClassIfNeededAndGet(const CallExpr& callExpr, Package& pkg)
{
    auto typeArgument = callExpr.baseFunc->ty->typeArgs[1];
    if (!typeArgument->IsClass() && !typeArgument->IsInterface()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_mock_unsupported_type, callExpr);
        return nullptr;
    }

    auto declToMock = mockUtils->GetInstantiatedDeclInCurrentPackage(
        DynamicCast<const ClassLikeTy*>(typeArgument.get()));
    if (MockSupportManager::DoesClassLikeSupportMocking(*declToMock)) {
        auto [classDecl, generated] = mockManager->GenerateMockClassIfNeededAndGet(
            *declToMock, pkg, MockManager::GetMockKind(callExpr));
        if (generated) {
            CJC_ASSERT(classDecl);
            if (auto ifaceDecl = DynamicCast<InterfaceDecl>(declToMock)) {
                mockSupportManager->PrepareClassWithDefaults(*classDecl, *ifaceDecl);
                mockSupportManager->WriteGeneratedMockDecls();
            }
        }
        return classDecl;
    } else {
        auto packageName =
            declToMock->genericDecl ? declToMock->genericDecl->fullPackageName : declToMock->fullPackageName;
        ReportDoesntSupportMocking(callExpr, Ty::ToString(typeArgument), packageName);
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

    return true;
}

}

void TestManager::PrepareDecls(Package& pkg)
{
    CJC_ASSERT(mockSupportManager);

    MockSupportManager::DeclsToPrepare decls;

    Walker(&pkg, Walker::GetNextWalkerID(), [this, &pkg, &decls](auto node) {
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
    }).Walk();

    mockSupportManager->PrepareDecls(std::move(decls));
    mockSupportManager->WriteGeneratedMockDecls();
}

void TestManager::GenerateAccessors(Package& pkg)
{
    CJC_ASSERT(mockSupportManager);

    Walker(&pkg, Walker::GetNextWalkerID(), [this, &pkg](auto node) {
        if (!node->IsSamePackage(pkg) || Is<ExtendDecl>(node)) {
            return VisitAction::SKIP_CHILDREN;
        }

        auto decl = As<ASTKind::DECL>(node);

        if (!decl) {
            return VisitAction::WALK_CHILDREN;
        }

        /* Don't generate accessors for instantiated versions
         * if the original generic declaration wasn't compiled with mocking support
         * othewise we cannot guarantee that all other instantiated versions
         * including from other packages' ones would be compatible with this declaration with accessors
         */
        if (decl->genericDecl && !decl->genericDecl->TestAttr(Attribute::MOCK_SUPPORTED)) {
            return VisitAction::SKIP_CHILDREN;
        }

        // Don't generate accessors for generics themthelves, do it for instantiated versions
        if (IS_GENERIC_INSTANTIATION_ENABLED && decl->TestAttr(Attribute::GENERIC)) {
            return VisitAction::SKIP_CHILDREN;
        }

        mockSupportManager->GenerateAccessors(*decl);

        return VisitAction::SKIP_CHILDREN;
    }).Walk();

    mockSupportManager->WriteGeneratedMockDecls();
}

void TestManager::PrepareToSpy(Package& pkg)
{
    CJC_ASSERT(mockSupportManager);

    mockSupportManager->GenerateSpyCallMarker(pkg);

    Walker(&pkg, Walker::GetNextWalkerID(), [this, &pkg](auto node) {
        if (!node->IsSamePackage(pkg) || Is<ExtendDecl>(node)) {
            return VisitAction::SKIP_CHILDREN;
        }

        auto decl = As<ASTKind::DECL>(node);

        if (!decl) {
            return VisitAction::WALK_CHILDREN;
        }

        if (decl->genericDecl && !decl->genericDecl->TestAttr(Attribute::MOCK_SUPPORTED)) {
            return VisitAction::SKIP_CHILDREN;
        }

        if (IS_GENERIC_INSTANTIATION_ENABLED && decl->TestAttr(Attribute::GENERIC)) {
            return VisitAction::SKIP_CHILDREN;
        }

        if (decl->curFile && decl->curFile->curPackage->fullPackageName == pkg.fullPackageName) {
            mockSupportManager->PrepareToSpy(*decl);
        }

        return VisitAction::SKIP_CHILDREN;
    }).Walk();

    mockSupportManager->WriteGeneratedMockDecls();
}

void TestManager::ReplaceCallsToForeignFunctions(Package& pkg)
{
    CJC_ASSERT(mockSupportManager);

    Walker(&pkg, Walker::GetNextWalkerID(), [](const Ptr<Node> node) {
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

        Ptr<Decl> accessorDecl = MockUtils::FindMockGlobalDecl(*funcDecl, MockUtils::GetForeignAccessorName(*funcDecl));
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
    }).Walk();
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
    Ptr<Decl> outerClassLike;

    Walker(&pkg, Walker::GetNextWalkerID(),
        [this, &isInConstructor, &isInMockAnnotatedLambda, &outerClassLike, &pkg](const Ptr<Node> node) {
        if (node->astKind == ASTKind::PRIMARY_CTOR_DECL) {
            // Primary init has been already desugared to regular init
            return VisitAction::SKIP_CHILDREN;
        }

        if (IsMockAnnotedLambda(node)) {
            isInMockAnnotatedLambda = true;
        }

        if (auto classLikeDecl = DynamicCast<ClassLikeDecl>(node)) {
            CJC_ASSERT(!outerClassLike);
            outerClassLike = classLikeDecl;
        }

        if ((node->curFile && !node->IsSamePackage(pkg))) {
            return VisitAction::SKIP_CHILDREN;
        }

        if (IS_GENERIC_INSTANTIATION_ENABLED &&
            (node->TestAttr(Attribute::GENERIC) || (node->ty && node->ty->HasGeneric()))) {
            return VisitAction::SKIP_CHILDREN;
        }

        if (node->TestAttr(Attribute::CONSTRUCTOR)) {
            isInConstructor = true;
            return VisitAction::WALK_CHILDREN;
        }

        if (!MockSupportManager::NeedToSearchCallsToReplaceWithAccessors(*node)) {
            return VisitAction::SKIP_CHILDREN;
        }

        if (auto expr = As<ASTKind::EXPR>(node); expr) {
            mockSupportManager->ReplaceExprWithAccessor(*expr, isInConstructor);
            mockSupportManager->ReplaceInterfaceDefaultFunc(*expr, outerClassLike, isInMockAnnotatedLambda);
        }

        return VisitAction::WALK_CHILDREN;
    }, [&isInConstructor, &isInMockAnnotatedLambda, &outerClassLike](const Ptr<Node> node) {
        if (node->TestAttr(Attribute::CONSTRUCTOR)) {
            isInConstructor = false;
        }
        if (IsMockAnnotedLambda(node)) {
            isInMockAnnotatedLambda = false;
        }
        if (auto classLikeDecl = DynamicCast<ClassLikeDecl>(node)) {
            CJC_ASSERT(outerClassLike == classLikeDecl);
            outerClassLike = nullptr;
        }
        return VisitAction::KEEP_DECISION;
    }).Walk();
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

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
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
        Walker(&pkg, Walker::GetNextWalkerID(), [this, &enclosingGenericFunc, &hasDeclsToCheckUsages](auto node) {
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
        }).Walk();
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
        auto isInExtend = false;

        Walker(pkg, Walker::GetNextWalkerID(), [&mangler, &manglerCtx, &isInExtend](auto node) {
            if (auto ed = As<ASTKind::EXTEND_DECL>(node); ed && !ed->TestAttr(Attribute::IMPORTED)) {
                manglerCtx->SaveExtend2CurFile(ed->curFile, ed);
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
        }).Walk();

        if (manglerCtx) {
            manglerCtx.reset();
        }
    }
}

void TestManager::CollectInternalDeclUsages(Package& pkg)
{
    Walker(&pkg, Walker::GetNextWalkerID(), [&pkg, this](auto node) {
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
    }).Walk();
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
#endif

void TestManager::MarkDeclsForTestIfNeeded(std::vector<Ptr<Package>> pkgs) const
{
    HandleDeclsToExportForTest(pkgs);
    for (auto& pkg : pkgs) {
        MarkMockCreationContainingGenericFuncs(*pkg);
        if (mockMode != MockMode::ON && (!mockCompatibleIfNeeded || !IsThereMockUsage(*pkg))) {
            continue;
        }

        Walker(pkg, Walker::GetNextWalkerID(), [](auto node) {
            MockSupportManager::MarkNodeMockSupportedIfNeeded(*node);
            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
}

bool TestManager::IsThereMockUsage(Package& pkg) const
{
    bool mockUsageFound = false;

    Walker(&pkg, Walker::GetNextWalkerID(), [&pkg, &mockUsageFound](auto node) {
        if (auto callExpr = As<ASTKind::CALL_EXPR>(node); callExpr &&
            (!IS_GENERIC_INSTANTIATION_ENABLED || !callExpr->ty->HasGeneric()) &&
            callExpr->IsSamePackage(pkg)
        ) {
            auto resolvedFunc = callExpr->resolvedFunction;
            if (MockManager::IsMockCall(*callExpr) ||
                (!IS_GENERIC_INSTANTIATION_ENABLED && resolvedFunc &&
                    resolvedFunc->TestAttr(Attribute::CONTAINS_MOCK_CREATION_CALL))
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
    }).Walk();

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

namespace {

struct ManglerCtxGuard final {
public:
    ManglerCtxGuard(BaseMangler& mangler, Package& pkg) : mangler(mangler), pkg(pkg)
    {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        manglerCtx = mangler.PrepareContextForPackage(&pkg);
#endif
    }

    ~ManglerCtxGuard()
    {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        mangler.manglerCtxTable.erase(
            ManglerContext::ReduceUnitTestPackageName(pkg.fullPackageName));
#endif
    }

private:
    [[maybe_unused]] BaseMangler& mangler;
    [[maybe_unused]] Package& pkg;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    std::unique_ptr<ManglerContext> manglerCtx;
#endif
};

} // namespace

void TestManager::PreparePackageForTestIfNeeded(Package& pkg)
{
    if (pkg.files.empty()) {
        return;
    }

    std::optional<ManglerCtxGuard> manglerCtxGuard;
    if (mockMode == MockMode::ON || (mockCompatibleIfNeeded && IsThereMockUsage(pkg))) {
        manglerCtxGuard.emplace(mockUtils->mangler, pkg);

        mockUtils->SetGetTypeForTypeParamDecl(pkg);
        mockUtils->SetIsSubtypeTypes(pkg);
        CollectInternalDeclUsages(pkg);
        GenerateAccessors(pkg);
        PrepareToSpy(pkg);
        PrepareDecls(pkg);
        ReplaceCallsWithAccessors(pkg);
        ReplaceCallsToForeignFunctions(pkg);
    } else {
        CheckIfNoMockSupportDependencies(pkg);
    }
    HandleMockCalls(pkg);
}

void TestManager::Init(GenericInstantiationManager* instantiationManager)
{
    if (!mockCompatible) {
        return;
    }

    mockUtils = new MockUtils(importManager, typeManager, instantiationManager);
    mockSupportManager = MakeOwned<MockSupportManager>(typeManager, mockUtils);

    if (mockCompatible && testEnabled) {
        mockManager = MakeOwned<MockManager>(importManager, typeManager, mockUtils);
    }
}

TestManager::~TestManager()
{
    if (mockUtils != nullptr) {
        delete mockUtils;
        mockUtils = nullptr;
    }
}

} // namespace Cangjie
