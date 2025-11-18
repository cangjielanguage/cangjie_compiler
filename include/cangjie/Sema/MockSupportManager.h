// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * MockSupportManager is the global manager to prepare classes for further possible mocking.
 */

#ifndef CANGJIE_SEMA_MOCK_SUPPORT_MANAGER_H
#define CANGJIE_SEMA_MOCK_SUPPORT_MANAGER_H

#include "cangjie/Sema/MockUtils.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Mangle/BaseMangler.h"

namespace Cangjie {

class MockSupportManager {
public:
    explicit MockSupportManager(TypeManager& typeManager, const Ptr<MockUtils> mockUtils);
    static bool IsDeclOpenToMock(const AST::Decl& decl);
    static bool DoesClassLikeSupportMocking(AST::ClassLikeDecl& classLikeToCheck);
    static bool NeedToSearchCallsToReplaceWithAccessors(AST::Node& node);
    void GenerateSpyCallMarker(AST::Package& package);
    void GenerateAccessors(AST::Decl& decl);
    Ptr<AST::Expr> ReplaceExprWithAccessor(
        AST::Expr& originalExpr, bool isInConstructor, bool isSubMemberAccess = false);
    void ReplaceInterfaceDefaultFunc(
        AST::Expr& originalExpr, Ptr<AST::Decl> outerClassLike, bool isInMockAnnotatedLambda);
    void ReplaceInterfaceDefaultFuncInCall(
        AST::Node& node, Ptr<AST::Decl> outerClassLike, bool isInMockAnnotatedLambda);
    static void MarkNodeMockSupportedIfNeeded(AST::Node& node);
    void WriteGeneratedMockDecls();
    void PrepareToSpy(AST::Decl& decl);

    struct DeclsToPrepare {
        // - Toplevel, static functions
        // - Instance functions from extends
        std::vector<Ptr<AST::FuncDecl>> functions;

        // Static properties
        std::vector<Ptr<AST::PropDecl>> properties;

        // Interfaces contains any methods with default implementation
        std::vector<Ptr<AST::InterfaceDecl>> interfacesWithDefaults;

        // Classes along with the interfaces that it implements (directly or through extend)
        // with methods with default implementation
        std::vector<std::tuple<Ptr<AST::ClassDecl>, Ptr<AST::InterfaceDecl>>> classWithInterfaceDefaults;
    };

    void PrepareDecls(DeclsToPrepare&& decls);
    void CollectDeclsToPrepare(AST::Decl& decl, DeclsToPrepare& decls);

    void PrepareClassWithDefaults(AST::ClassDecl& classDecl, AST::InterfaceDecl& interfaceDecl);

private:
    TypeManager& typeManager;
    Ptr<MockUtils> mockUtils;
    std::set<OwnedPtr<AST::Decl>> generatedMockDecls;
    std::unordered_map<Ptr<AST::Decl>, Ptr<AST::VarDecl>> genericMockVarsDecls;

    std::unordered_map<Ptr<AST::Ty>, std::unordered_set<Ptr<AST::Ty>>> defaultInterfaceAccessorExtends;

    bool HasDefaultInterfaceAccessor(Ptr<AST::Ty> declTy, Ptr<AST::Ty> accessorInterfaceDeclTy);

    static void MakeOpenToMockIfNeeded(AST::Decl& decl);
    static void MarkMockAccessorWithAttributes(AST::Decl& decl, AST::AccessLevel accessLevel);
    bool IsMemberAccessOnThis(const AST::MemberAccess& memberAccess) const;
    OwnedPtr<AST::FuncDecl> GenerateErasedFuncAccessor(AST::FuncDecl& methodDecl) const;
    OwnedPtr<AST::FuncDecl> GenerateFuncAccessor(AST::FuncDecl& methodDecl);
    OwnedPtr<AST::PropDecl> GeneratePropAccessor(AST::PropDecl& propDecl);
    std::vector<OwnedPtr<AST::Node>> GenerateFieldGetterAccessorBody(
        AST::VarDecl& fieldDecl, AST::FuncBody& funcBody, AccessorKind kind) const;
    std::vector<OwnedPtr<AST::Node>> GenerateFieldSetterAccessorBody(
        AST::VarDecl& fieldDecl, AST::FuncParam& setterParam, AST::FuncBody& funcBody, AccessorKind kind) const;
    OwnedPtr<AST::FuncDecl> CreateFieldAccessorDecl(
        const AST::VarDecl& fieldDecl, AST::FuncTy* accessorTy, AccessorKind kind) const;
    OwnedPtr<AST::FuncDecl> CreateForeignFunctionAccessorDecl(AST::FuncDecl& funcDecl) const;
    OwnedPtr<AST::FuncDecl> GenerateVarDeclAccessor(AST::VarDecl& fieldDecl, AccessorKind kind);
    OwnedPtr<AST::CallExpr> GenerateAccessorCallForField(const AST::MemberAccess& memberAccess, AccessorKind kind);
    Ptr<AST::Expr> ReplaceFieldGetWithAccessor(AST::MemberAccess& memberAccess, bool isInConstructor);
    Ptr<AST::Expr> ReplaceFieldSetWithAccessor(AST::AssignExpr& assignExpr, bool isInConstructor);
    Ptr<AST::Expr> ReplaceMemberAccessWithAccessor(AST::MemberAccess& memberAccess, bool isInConstructor);
    template <typename T>
    Ptr<T> FindGeneratedGlobalDecl(Ptr<AST::File> file, const std::string& identifier);
    std::tuple<Ptr<AST::InterfaceDecl>, Ptr<AST::FuncDecl>> FindDefaultAccessorInterfaceAndFunction(
        Ptr<AST::FuncDecl> original);
    void TransformAccessorCallForMutOperation(
        AST::NameReferenceExpr& originalNre, AST::Expr& replacedNre, AST::Expr& topLevelExpr);
    void ReplaceSubMemberAccessWithAccessor(
        const AST::MemberAccess& memberAccess, bool isInConstructor, const Ptr<AST::Expr> topLevelMutExpr = nullptr);
    Ptr<AST::Expr> ReplaceTopLevelVariableGetWithAccessor(AST::RefExpr& refExpr);
    OwnedPtr<AST::CallExpr> GenerateAccessorCallForTopLevelVariable(
        const AST::RefExpr& refExpr, AccessorKind kind);
    void GenerateVarDeclAccessors(AST::VarDecl& fieldDecl, AccessorKind getterKind, AccessorKind setterKind);
    void PrepareStaticDecl(AST::Decl& decl);
    std::vector<OwnedPtr<AST::MatchCase>> GenerateHandlerMatchCases(
        const AST::FuncDecl& funcDecl,
        OwnedPtr<AST::EnumPattern> optionFuncTyPattern, OwnedPtr<AST::CallExpr> handlerCallExpr);
    Ptr<AST::Decl> GenerateSpiedObjectVar(const AST::Decl& decl);

    std::vector<Ptr<AST::Ty>> CloneFuncDecl(Ptr<AST::FuncDecl> fromDecl, Ptr<AST::FuncDecl> toDecl);
    void GenerateSpyCallHandler(AST::FuncDecl& funcDecl, AST::Decl& spiedObjectDecl);
    void PrepareInterfaceDecl(AST::InterfaceDecl& interfaceDecl);

    bool NeedEraseAccessorTypes(AST::Decl& decl) const;
};
} // namespace Cangjie

#endif // CANGJIE_SEMA_MOCK_SUPPORT_MANAGER_H
