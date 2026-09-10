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

#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeManager.h"

#include "MockUtils.h"

namespace Cangjie {

class MockSupportManager {
public:
    explicit MockSupportManager(TypeManager& typeManager, DiagnosticEngine& d, Ptr<MockUtils> mockUtils);
    static bool IsDeclOpenToMock(const AST::Decl& decl);
    static bool DoesClassLikeSupportMocking(AST::ClassLikeDecl& classLikeToCheck);
    static bool NeedToSearchCallsToReplaceWithAccessors(AST::Node& node);
    void GenerateSpyCallMarker(AST::Package& package);
    void GenerateAccessors(AST::Decl& decl);
    Ptr<AST::Expr> ReplaceExprWithAccessor(
        AST::Expr& originalExpr, bool isInConstructor, bool isSubMemberAccess = false);
    void ReplaceInterfaceDefaultFunc(AST::Expr& originalExpr, AST::ModalTy outerTy, bool isInMockAnnotatedLambda);
    void ReplaceInterfaceDefaultFuncInCall(AST::Node& node, AST::ModalTy outerty, bool isInMockAnnotatedLambda);
    void MarkNodeMockSupportedIfNeeded(AST::Node& node);
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
        // with methods with default implementation. If implemented using extend, then there
        // is also an extend decl
        std::vector<std::tuple<Ptr<AST::ClassLikeDecl>, Ptr<AST::InterfaceDecl>, Ptr<AST::ExtendDecl>>>
            classWithInterfaceDefaults;
    };

    void PrepareDecls(DeclsToPrepare&& decls);
    void CollectDeclsToPrepare(AST::Decl& decl, DeclsToPrepare& decls);

    void PrepareClassLikeWithDefaults(
        AST::ClassLikeDecl& classDecl, AST::InterfaceDecl& interfaceDecl, Ptr<AST::ExtendDecl> originalExtendDecl);
    void WriteUsedInternalDecl(AST::Decl& decl);

private:
    TypeManager& typeManager;
    DiagnosticEngine& diag;
    Ptr<MockUtils> mockUtils;
    std::unordered_set<Ptr<AST::Decl>> usedInternalDecls;

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
    OwnedPtr<AST::FuncBody> CreateForeignFunctionAccessorBody(
        AST::FuncDecl& funcDecl, std::vector<OwnedPtr<AST::FuncParamList>> accessorFuncParamLists) const;
    OwnedPtr<AST::FuncDecl> GenerateVarDeclAccessor(AST::VarDecl& fieldDecl, AccessorKind kind);
    OwnedPtr<AST::CallExpr> GenerateGetterCall(const AST::NameReferenceExpr& nameRefExpr);
    OwnedPtr<AST::CallExpr> GenerateSetterCall(const AST::NameReferenceExpr& nameRefExpr);
    OwnedPtr<AST::CallExpr> GenerateAccessorCallForField(const AST::NameReferenceExpr& nameRefExpr, AccessorKind kind);
    OwnedPtr<AST::CallExpr> GenerateAccessorCallForField(OwnedPtr<AST::Expr> baseExpr, Ptr<AST::Decl> memberDecl,
        AST::ModalTy memberRefTy, AccessorKind kind, Ptr<AST::File> curFile);
    Ptr<AST::Expr> ReplaceFieldGetWithAccessor(AST::MemberAccess& memberAccess, bool isInConstructor);
    OwnedPtr<AST::Block> GenerateBlockForAssignExpr(AST::AssignExpr& assignExpr,
        OwnedPtr<AST::CallExpr> accessorCall);
    Ptr<AST::Expr> ReplaceFieldSetWithAccessor(AST::AssignExpr& assignExpr, bool isInConstructor);
    Ptr<AST::Expr> ReplaceVarRefExprWithGetAccessor(AST::RefExpr& refExpr);
    OwnedPtr<AST::CallExpr> ReplaceRefExprFieldSetWithAccessor(AST::RefExpr& refExpr, bool isInConstructor);
    OwnedPtr<AST::CallExpr> ReplaceMemberAccessFieldSetWithAccessor(AST::MemberAccess& memAccess, bool isInConstructor);
    Ptr<AST::Expr> ReplaceRefExprWithGetAccessor(AST::RefExpr& refExpr);
    OwnedPtr<AST::CallExpr> ReplaceRefExprFieldSetWithAccessorImpl(AST::RefExpr& refExpr);
    Ptr<AST::Expr> ReplaceMemberAccessWithAccessor(AST::MemberAccess& memberAccess, bool isInConstructor);
    Ptr<AST::Expr> ReplaceMemberAccess(
        AST::MemberAccess& member, bool isInConstructor, bool isSubMemberAccess);
    Ptr<AST::Expr> ReplaceAssignment(AST::AssignExpr& assignment, bool isInConstructor);
    Ptr<AST::Expr> ReplaceRefExpr(AST::RefExpr& refExpr);
    Ptr<AST::Expr> ReplaceCallExpr(AST::CallExpr& callExpr);

    /**
     * 
     * Example of desugaring:
     * foreign func times2(x: CPointer<Int64>): Unit
     * 
     * public var x: Int64 = 10
     * unsafe {
     *     times2(inout x)
     * } |->
     * 
     * var $tmp1: Int64 = x$get()
     * times2(inout tmp1)
     * x = x$set($tmp1)
     * 
     */
    Ptr<AST::Expr> ReplaceInoutFuncArgWithAccessor(AST::CallExpr& callExpr);
    Ptr<AST::FuncArg> GenerateDesugarFuncArg(Ptr<AST::FuncArg> funcArg, Ptr<AST::VarDecl> varDecl);
    template <typename T> Ptr<T> FindGeneratedGlobalDecl(Ptr<AST::File> file, const std::string& identifier);
    std::tuple<Ptr<AST::InterfaceDecl>, Ptr<AST::FuncDecl>> FindDefaultAccessorInterfaceAndFunction(
        Ptr<AST::FuncDecl> original);
    Ptr<AST::FuncDecl> FindDefaultAccessorImplementation(AST::ModalTy baseTy, Ptr<AST::FuncDecl> accessorDecl);
    void TransformAccessorCallForMutOperation(
        AST::NameReferenceExpr& originalNre, AST::Expr& replacedNre, AST::Expr& topLevelExpr);
    void ReplaceSubMemberAccessWithAccessor(
        const AST::MemberAccess& memberAccess, bool isInConstructor, const Ptr<AST::Expr> topLevelMutExpr = nullptr);
    Ptr<AST::Expr> ReplaceTopLevelVariableGetWithAccessor(AST::RefExpr& refExpr);
    OwnedPtr<AST::CallExpr> GenerateAccessorCallForTopLevelVariable(
        const AST::NameReferenceExpr& nameRefExpr, AccessorKind kind);
    OwnedPtr<AST::CallExpr> GenerateAccessorCallForTopLevelVariable(
        Ptr<AST::Decl> globalDecl, AccessorKind kind, Ptr<AST::File> curFile);
    void GenerateVarDeclAccessors(AST::VarDecl& fieldDecl, AccessorKind getterKind, AccessorKind setterKind);
    void PrepareStaticDecl(AST::Decl& decl);
    std::vector<OwnedPtr<AST::MatchCase>> GenerateHandlerMatchCases(const AST::FuncDecl& funcDecl,
        OwnedPtr<AST::EnumPattern> optionFuncTyPattern, OwnedPtr<AST::CallExpr> handlerCallExpr);
    Ptr<AST::Decl> GenerateSpiedObjectVar(const AST::Decl& decl);

    std::vector<AST::DataTy> CloneFuncDecl(Ptr<AST::FuncDecl> fromDecl, Ptr<AST::FuncDecl> toDecl,
        Ptr<AST::File> curFile = nullptr, std::string fullPackageName = "");
    void GenerateSpyCallHandler(AST::FuncDecl& funcDecl, AST::Decl& spiedObjectDecl);
    void PrepareInterfaceDecl(AST::InterfaceDecl& interfaceDecl);

    bool NeedEraseAccessorTypes(AST::Decl& decl) const;
};
} // namespace Cangjie

#endif // CANGJIE_SEMA_MOCK_SUPPORT_MANAGER_H
