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

#ifndef CANGJIE_SEMA_MOCK_UTILS_H
#define CANGJIE_SEMA_MOCK_UTILS_H

#include "cangjie/AST/Walker.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/GenericInstantiationManager.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie {

enum class AccessorKind : uint8_t {
    FIELD_GETTER,
    FIELD_SETTER,
    PROP,
    PROP_GETTER,
    PROP_SETTER,
    METHOD,
    TOP_LEVEL_FUNCTION,
    STATIC_METHOD,
    STATIC_PROP_GETTER,
    STATIC_PROP_SETTER,
    STATIC_FIELD_GETTER,
    STATIC_FIELD_SETTER,
    TOP_LEVEL_VARIABLE_GETTER,
    TOP_LEVEL_VARIABLE_SETTER
};

class MockUtils {
public:
    MockUtils(ImportManager& im, TypeManager& tm, DiagnosticEngine& d, BaseMangler& mang);
    static bool CanMock(AST::Node& node);
    static bool IsMockAccessor(const AST::Decl& decl);
    static std::vector<AST::Decl*> ListExistingMembers(Ptr<AST::Decl> decl);
    void Walk(Ptr<AST::Node> node, std::function<AST::VisitAction(Ptr<AST::Node>)> visitPre = nullptr,
        std::function<AST::VisitAction(Ptr<AST::Node>)> visitPost = nullptr);
    bool isWalking{false};

    void LoadStdDecls();

    template <typename T> static OwnedPtr<T> CreateType(AST::ModalTy ty)
    {
        auto type = MakeOwned<T>();
        type->SetTy(ty);
        return type;
    }

    Ptr<Cangjie::AST::PointerTy> WrapTy2CPointer(AST::ModalTy ty)
    {
        CJC_NULLPTR_CHECK(ty);
        auto pointerTy = typeManager.GetPointerTy(ty.Ty());
        return pointerTy;
    }

    static std::string mockAccessorSuffix;
    static std::string spyObjVarName;
    static std::string spyCallMarkerVarName;
    static std::string defaultAccessorSuffix;

    template <typename... Args> static std::string Concatenate(const Args&... args)
    {
        static_assert((true && ... && std::is_same_v<Args, std::string>));
        std::string result;
        size_t size = (0 + ... + args.size());
        result.reserve(size);
        (result.append(args), ...);
        return result;
    }

    template <typename T> Ptr<T> FindGlobalDecl(Ptr<AST::File> file, const std::string& identifier)
    {
        auto& indexMap = DemandIndex(file);
        auto it = indexMap.find(identifier);
        return it == indexMap.end() ? nullptr : DynamicCast<T>(it->second);
    }

    template <typename T> Ptr<T> FindGlobalDecl(Ptr<AST::Package> package, const std::string& identifier)
    {
        auto& indexMap = DemandIndex(package);
        auto it = indexMap.find(identifier);
        return it == indexMap.end() ? nullptr : DynamicCast<T>(it->second);
    }

    template <typename T> Ptr<T> FindMemberDecl(Ptr<AST::Decl> decl, const std::string& identifier)
    {
        auto& indexMap = DemandIndex(decl);
        auto it = indexMap.find(identifier);
        return it == indexMap.end() ? nullptr : DynamicCast<T>(it->second);
    }

    std::unordered_map<Ptr<AST::Package>, std::unordered_map<std::string, Ptr<AST::Decl>>> globalsByPackage;
    std::unordered_map<Ptr<AST::File>, std::unordered_map<std::string, Ptr<AST::Decl>>> globalsByFile;
    std::unordered_map<Ptr<AST::Decl>, std::unordered_map<std::string, Ptr<AST::Decl>>> localsByOuter;

    std::unordered_map<std::string, std::string> DemandMappingIndex(Ptr<AST::Package> package);
    std::unordered_map<std::string, Ptr<AST::Decl>>& DemandIndex(Ptr<AST::Package> package);
    std::unordered_map<std::string, Ptr<AST::Decl>>& DemandIndex(Ptr<AST::File> file);
    std::unordered_map<std::string, Ptr<AST::Decl>>& DemandIndex(Ptr<AST::Decl> decl);

    void AttachGeneratedDecl(OwnedPtr<AST::Decl>&& decl);
    void AttachGeneratedDecl(OwnedPtr<AST::Decl>&& decl, const AST::Package& originPackage);
    void AttachGeneratedDecl(OwnedPtr<AST::Decl>&& decl, const AST::Decl& originDecl);

    /**
     * throw Exception([message])
     **/
    OwnedPtr<AST::Expr> CreateThrowExpr(const std::string& message, Ptr<AST::File> curFile);

    /**
     * match ([selector]) {
     *   case v : [castTy] => [createMatchedBranch($v)]
     *   case _ => [otherwiseBranch]
     * }
     */
    static OwnedPtr<AST::Expr> CreateTypeCast(OwnedPtr<AST::Expr> selector, AST::ModalTy castTy,
        std::function<OwnedPtr<AST::Expr>(Ptr<AST::VarDecl>)> createMatchedBranch, OwnedPtr<AST::Expr> otherwiseBranch,
        AST::ModalTy ty);

    /**
     * match ([selector]) {
     *   case v : [castTy] => v
     *   case _ => throw Exception([message])
     * }
     */
    OwnedPtr<AST::Expr> CreateTypeCastOrThrow(
        OwnedPtr<AST::Expr> selector, AST::DataTy castTy, const std::string& message);

    /**
     * match ([selector]) {
     *   case v : [castTy] => v
     *   case _ => zerValue<[castTy]>()
     * }
     */
    OwnedPtr<AST::Expr> CreateTypeCastOrZeroValue(OwnedPtr<AST::Expr> selector, AST::DataTy castTy) const;

    /**
     * Replaces all argument's types and return type with Any
     */
    Ptr<AST::FuncTy> EraseFuncTypes(Ptr<AST::FuncTy> funcTy);

    std::string BuildMockAccessorIdentifier(
        const AST::Decl& originalDecl, AccessorKind kind, bool forErased = false) const;
    std::string BuildArgumentList(const AST::Decl& decl) const;
    std::string GetOriginalIdentifierOfAccessor(const AST::FuncDecl& decl) const;
    std::string GetOriginalIdentifierOfMockAccessor(const AST::Decl& decl) const;

    bool MayContainInternalTypes(AST::ModalTy ty) const;

private:
    ImportManager& importManager;
    TypeManager& typeManager;
    DiagnosticEngine& diag;
    BaseMangler& mangler;

    Ptr<AST::FuncDecl> getTypeForTypeParamDecl = nullptr;
    Ptr<AST::FuncDecl> isSubtypeTypesDecl = nullptr;
    Ptr<AST::StructDecl> arrayDecl = nullptr;
    Ptr<AST::StructDecl> stringDecl = nullptr;
    Ptr<AST::EnumDecl> optionDecl = nullptr;
    Ptr<AST::InheritableDecl> toStringDecl = nullptr;
    Ptr<AST::ClassDecl> objectDecl = nullptr;
    Ptr<AST::FuncDecl> zeroValueDecl = nullptr;
    Ptr<AST::ClassDecl> exceptionClassDecl = nullptr;

    static bool IsMockAccessorRequired(const AST::Decl& decl);
    AccessorKind ComputeAccessorKind(const AST::FuncDecl& accessorDecl);
    bool IsGetterForMutField(const AST::FuncDecl& accessorDecl);

    Ptr<AST::Decl> FindMockGlobalDecl(const AST::Decl& decl, const std::string& name);
    static void PrependFuncGenericSubst(
        const Ptr<AST::Generic> originalGeneric,
        const Ptr<AST::Generic> mockedGeneric,
        std::vector<TypeSubst>& classSubsts);
    static std::vector<TypeSubst> BuildGenericSubsts(const Ptr<AST::InheritableDecl> decl);
    static std::string GetForeignAccessorName(const AST::FuncDecl& decl);

    Ptr<AST::Decl> FindAccessor(AST::ClassDecl& outerClass, const Ptr<AST::Decl> member, AccessorKind kind);
    Ptr<AST::Decl> FindAccessorForMemberAccess(
        const Ptr<AST::Ty> ty, const Ptr<AST::Decl> resolvedMember, AccessorKind kind);
    Ptr<AST::FuncDecl> FindTopLevelAccessor(Ptr<AST::Decl> member, AccessorKind kind);
    OwnedPtr<AST::Expr> WrapCallTypeArgsIntoArray(const AST::Decl& decl);
    bool IsGeneratedGetter(AccessorKind kind);
    Ptr<AST::FuncDecl> FindAccessor(Ptr<AST::MemberAccess> ma, Ptr<AST::Decl> target, AccessorKind kind);
    std::vector<Ptr<AST::Ty>> AddGenericIfNeeded(AST::Decl& originalDecl, AST::Decl& mockedDecl) const;
    OwnedPtr<AST::ArrayLit> WrapCallArgsIntoArray(const AST::FuncDecl& mockedFunc);
    AST::ModalTy GetInstantiatedTy(AST::ModalTy ty, std::vector<TypeSubst>& typeSubsts);
    void SetGetTypeForTypeParamDecl(AST::Package& pkg);
    OwnedPtr<AST::Expr> CreateGetTypeForTypeParameterCall(const Ptr<AST::GenericParamDecl> genericParam);
    std::string Mangle(const AST::Decl& decl) const;

    OwnedPtr<AST::RefExpr> CreateRefExprWithInstTys(AST::Decl& target, const std::vector<AST::DataTy>& instTys,
        const std::string& refName, AST::File& curFile) const;
    OwnedPtr<AST::RefExpr> CreateDeclBasedReferenceExpr(AST::Decl& target, const std::vector<AST::DataTy>& instTys,
        const std::string& refName, AST::File& curFile) const;

    OwnedPtr<AST::CallExpr> CreateZeroValue(AST::DataTy ty, AST::File& curFile) const;

    template <typename T>
    Ptr<T> GetGenericDecl(Ptr<T> decl) const
    {
        if (decl->genericDecl) {
            return StaticCast<T>(decl->genericDecl);
        }

        return decl;
    }

    /**
     * Extracts outer decl if it's class/interfacr/struct decl
     * If outer decl is extend, extracts extended type
     */
    Ptr<AST::Decl> GetOuterDecl(AST::Decl& decl) const;
    Ptr<AST::Decl> GetExtendedTypeDecl(AST::FuncDecl& decl) const;
    void UpdateRefTypesTarget(
        Ptr<AST::Type> type, Ptr<AST::Generic> oldGeneric, Ptr<AST::Generic> newGeneric) const;
    int GetIndexOfGenericTypeParam(AST::DataTy ty, Ptr<AST::Generic> generic) const;

    friend class TestManager;
    friend class MockManager;
    friend class MockSupportManager;
};

AccessorKind GetVarDeclSetterAccessorKind(Ptr<AST::Decl> varDecl);
AccessorKind GetVarDeclGetterAccessorKind(Ptr<AST::Decl> varDecl);

} // namespace Cangjie

#endif // CANGJIE_SEMA_MOCK_UTILS_H
