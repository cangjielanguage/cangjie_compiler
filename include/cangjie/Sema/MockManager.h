// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * MockManager is the global manager to generate the mocked version of a class or interface
 * which is passed to createMock/createSpy calls.
 */

#ifndef CANGJIE_SEMA_MOCK_MANAGER_H
#define CANGJIE_SEMA_MOCK_MANAGER_H

#include "cangjie/Sema/MockUtils.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Mangle/BaseMangler.h"

namespace Cangjie {

enum class MockKind : uint8_t;

class MockManager {
public:
    explicit MockManager(ImportManager& importManager, TypeManager& typeManager, const Ptr<MockUtils> mockUtils);

    struct GeneratedClassResult {
        Ptr<AST::ClassDecl> classDecl;
        bool generated;
    };
    GeneratedClassResult GenerateMockClassIfNeededAndGet(
        AST::ClassLikeDecl& originalDecl, AST::Package& curPkg, MockKind mockKind);
    void WriteGeneratedClasses();
    void HandleMockAnnotatedLambdaValue(AST::Expr& expr);
    void WrapWithRequireMockObject(AST::Expr& receiverExpr);

    static OwnedPtr<AST::ThrowExpr> CreateIllegalMockCallException(
        AST::File& curFile, TypeManager& typeMgr, ImportManager& importMgr);
    static OwnedPtr<AST::CallExpr> CreateInitCallOfMockClass(
        AST::ClassDecl& mockClass, std::vector<OwnedPtr<AST::FuncArg>>& mockCallArgs,
        TypeManager& typeManager, const std::vector<Ptr<AST::Ty>> instTys,
        const std::vector<Ptr<AST::Ty>> valueParamTys);
    static bool IsMockCall(const AST::CallExpr& ce);
    static bool IsMockClass(const AST::Decl& decl);
    static MockKind GetMockKind(const AST::CallExpr& ce);
    static Ptr<AST::ClassDecl> FindMockedClassOf(const AST::ClassLikeDecl& decl);

private:
    static OwnedPtr<AST::VarDecl> CreateFieldDecl(
        AST::ClassLikeDecl& decl, const std::string& identifier, const Ptr<AST::Ty> ty, const AST::Package& curPkg
    );
    static std::optional<std::pair<Ptr<AST::FuncDecl>, Ptr<AST::Ty>>> FindInitDecl(
        AST::InheritableDecl& decl, TypeManager& typeManager,
        std::vector<OwnedPtr<AST::Expr>>& args, const std::vector<Ptr<AST::Ty>> instTys = {});
    static std::optional<std::pair<Ptr<AST::FuncDecl>, Ptr<AST::Ty>>> FindInitDecl(
        AST::InheritableDecl& decl, TypeManager& typeManager,
        const std::vector<Ptr<AST::Ty>> valueParamTys, const std::vector<Ptr<AST::Ty>> instTys = {});
    static OwnedPtr<AST::CallExpr> CreateInitCall(
        const std::pair<Ptr<AST::FuncDecl>, Ptr<AST::Ty>> initDeclInfo,
        std::vector<OwnedPtr<AST::Expr>>& valueArgs,
        AST::File& curFile, const std::vector<Ptr<AST::Ty>> instTys = {});
    static OwnedPtr<AST::ThrowExpr> CreateThrowException(
        AST::ClassDecl& exceptionDecl, std::vector<OwnedPtr<AST::Expr>> args,
        AST::File& curFile, TypeManager& typeManager);

    TypeManager& typeManager;
    ImportManager& importManager;
    Ptr<MockUtils> mockUtils;

    std::map<std::string, OwnedPtr<AST::ClassDecl>> mockedClassDecls;
    std::map<Ptr<AST::ClassLikeDecl>, int> instantiationCounters;
    std::map<Ptr<const AST::FuncDecl>, bool> defaultForTypePresence;

    Ptr<AST::ClassDecl> objectDecl;

    Ptr<AST::InterfaceDecl> callHandlerDecl;
    Ptr<AST::InterfaceDecl> mockedInterfaceDecl;
    Ptr<AST::StructDecl> parameterInfoDecl;
    Ptr<AST::StructDecl> declIdDecl;
    Ptr<AST::StructDecl> funcInfoDecl;
    Ptr<AST::StructDecl> callDecl;
    Ptr<AST::EnumDecl> onCallEnumDecl;
    Ptr<AST::EnumDecl> declKindEnumDecl;
    Ptr<AST::InterfaceDecl> hasDefaultValueForStubDecl;
    Ptr<AST::ClassDecl> noDefaultValueForMockException;
    Ptr<AST::ClassDecl> mockReturnValueTypeMismatchException;
    Ptr<AST::FuncDecl> requireMockObject;
    Ptr<AST::StructDecl> mockZeroValueDecl;

    int ComputeInstantiationNumberToMangleMockedDecl(const AST::ClassLikeDecl& declToMock);
    void FindOverridesInSuperDecl(
        AST::ClassLikeDecl& superDecl, const AST::Decl& member, std::set<Ptr<AST::Decl>>& foundOverrides
    ) const;
    void AddObjectSuperTypeIfNeeded(
        const AST::ClassLikeDecl& originalDecl, AST::ClassDecl& mockedDecl
    ) const;
    void AddMockedMembers(AST::ClassLikeDecl& originalDecl, AST::ClassDecl& mockedDecl,
        std::vector<TypeSubst>& classGenericSubsts);
    void AddMockedInterface(AST::ClassDecl& mockedDecl, AST::VarDecl& handlerFieldDecl);

    OwnedPtr<AST::MatchExpr> CreateTypeCastForOnCallReturnValue(
        OwnedPtr<AST::Expr> exprToCast, const Ptr<AST::Ty> castTy
    ) const;
    OwnedPtr<AST::AssignExpr> CreateMemberAssignment(
        AST::VarDecl& member, OwnedPtr<AST::Expr> rhsExpr
    ) const;

    OwnedPtr<AST::MatchCase> CreateOnCallReturnZeroMatchCase(
        const AST::FuncDecl& originalFunc, const Ptr<AST::Ty> zeroValueTy, AST::Decl& enumConstructor) const;
    OwnedPtr<AST::MatchCase> CreateOnCallReturnMatchCase(
        const AST::FuncDecl& originalFunc, const Ptr<AST::Ty> retTy, AST::Decl& enumConstructor) const;
    OwnedPtr<AST::MatchCase> CreateOnCallThrowMatchCase(
        const AST::FuncDecl& originalFunc, AST::Decl& enumConstructor) const;
    OwnedPtr<AST::MatchCase> CreateOnCallCallBaseMatchCase(
        AST::FuncDecl& originalFunc, const AST::FuncDecl& mockedFunc, AST::Decl& enumConstructor) const;
    OwnedPtr<AST::MatchCase> CreateOnCallReturnDefaultMatchCase(
        const AST::FuncDecl& originalFunc, const Ptr<AST::Ty> retTy, AST::Decl& enumConstructor);
    Ptr<AST::FuncDecl> FindDefaultValueForStubMethod(const Ptr<AST::Ty> retTy) const;
    OwnedPtr<AST::MatchCase> CreateOnCallResultMatchCase(
        AST::FuncDecl& originalFunc, const Ptr<AST::FuncDecl> mockedFunc, AST::Decl& enumConstructor);
    OwnedPtr<AST::FuncDecl> CreateEmptyConstructorDecl(
        AST::ClassDecl& mockedClass, std::vector<OwnedPtr<AST::FuncParam>> params, AST::File& curFile) const;
    OwnedPtr<AST::FuncDecl> CreateConstructorDecl(
        AST::ClassDecl& mockedClass, std::set<std::string> membersToAssign, MockKind mockKind, AST::File& curFile
    ) const;
    void AddAssignmentsForSuperFields(const AST::FuncDecl& constructorOfMockedDecl) const;
    OwnedPtr<AST::CallExpr> CreateDeclId(const AST::Decl& decl, AST::File& curFile) const;
    OwnedPtr<AST::CallExpr> CreateParamInfo(const AST::FuncParam& param, int position, AST::File& curFile
    ) const;
    OwnedPtr<AST::ArrayLit> CreateParamsInfo(const AST::FuncDecl& decl, AST::File& curFile) const;
    OwnedPtr<AST::ArrayLit> CreateTypeParamsInfo(const AST::FuncDecl& decl, AST::File& curFile) const;
    OwnedPtr<AST::CallExpr> CreateFuncInfo(AST::FuncDecl& funcDecl, AST::File& curFile);
    OwnedPtr<AST::CallExpr> CreateDeclKind(const AST::FuncDecl& decl) const;
    OwnedPtr<AST::CallExpr> CreateCallInfo(
        AST::FuncDecl& originalFunction, OwnedPtr<AST::Expr> mockedArgsArray, OwnedPtr<AST::Expr> typeArgsArray,
        const Ptr<AST::File> curFile, OwnedPtr<AST::RefExpr> objRef
    );
    OwnedPtr<AST::CallExpr> CreateOnCallInvocation(
        OwnedPtr<AST::Expr> mockedArgsArray, OwnedPtr<AST::Expr> typeArgsArray, AST::FuncDecl& originalFunc,
        OwnedPtr<AST::RefExpr> objRef, OwnedPtr<AST::Expr> handler
    );

    OwnedPtr<AST::FuncDecl> CreateStaticMethodStub(
        AST::FuncDecl& originalMethod, AST::ClassDecl& mockedDecl, std::vector<TypeSubst>& classGenericSubsts);
    OwnedPtr<AST::FuncDecl> CreateMockedMethod(
        AST::FuncDecl& originalMethod, AST::ClassDecl& mockedDecl, std::vector<TypeSubst>& classGenericSubsts);
    OwnedPtr<AST::FuncDecl> CreateMockedMethodWithoutBody(
        AST::FuncDecl& originalMethod, AST::ClassDecl& mockedDecl, std::vector<TypeSubst>& classGenericSubsts);
    OwnedPtr<AST::PropDecl> CreateMockedProp(
        AST::PropDecl& originalProp, AST::ClassDecl& mockedDecl, std::vector<TypeSubst>& classGenericSubsts);
    void AddMockedMemberIfNeeded(
        AST::ClassDecl& mockedDecl, AST::Decl& member, std::vector<TypeSubst>& classGenericSubsts);
    OwnedPtr<AST::RefExpr> GetHandlerRefFromClass(const Ptr<AST::ClassDecl> decl);
    void GenerateCallHandlerForStaticDecl(
        AST::FuncDecl& decl, AST::Expr& injectTo, Ptr<AST::FuncDecl> declForInfo = nullptr);
    void GenerateCallHandlerForMethodWithDefault(AST::CallExpr& callExpr);
    void HandleMockAnnotatedLambdaWithCall(AST::CallExpr& callExpr);
    void HandleMockAnnotatedLambdaWithMemberAccess(AST::MemberAccess& ma, AST::Expr& injectTo);
    void HandleMockAnnotatedLambdaWithRefExpr(const AST::RefExpr& refExpr, AST::Expr& injectTo);
    void HandleMockAnnotatedLambdaWithAssignExpr(AST::AssignExpr& assignExpr);

    OwnedPtr<AST::Expr> GetCurrentStaticHandler(const Ptr<AST::File> curFile);
    OwnedPtr<AST::Expr> GetMockedObjectHandler(OwnedPtr<AST::RefExpr> objRef, const Ptr<AST::File> curFile);
    OwnedPtr<AST::LambdaExpr> GenerateCallHandlerLambda(
        AST::FuncDecl& decl, const AST::Expr& injectTo, Ptr<AST::FuncDecl> declForInfo = nullptr);
    std::vector<OwnedPtr<AST::MatchCase>> GenerateCallHandlerCases(
        AST::FuncDecl& staticDecl, const AST::Expr& injectTo);
    std::tuple<Ptr<AST::InterfaceDecl>, Ptr<AST::FuncDecl>> FindDefaultAccessorInterfaceAndFunction(
        Ptr<AST::FuncDecl> original, Ptr<AST::MemberAccess> baseFunc);
};
} // namespace Cangjie

#endif // CANGJIE_SEMA_MOCK_MANAGER_H
