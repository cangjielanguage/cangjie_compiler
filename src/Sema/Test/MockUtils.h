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

#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/GenericInstantiationManager.h"

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
    explicit MockUtils(ImportManager& importManager, TypeManager& typeManager, GenericInstantiationManager* gim);
    static bool IsMockAccessor(const AST::Decl& decl);

    template <typename T> static OwnedPtr<T> CreateType(const Ptr<AST::Ty> ty)
    {
        auto type = MakeOwned<T>();
        type->ty = ty;
        return type;
    }

    static std::string mockAccessorSuffix;
    static std::string spyObjVarName;
    static std::string spyCallMarkerVarName;
    static std::string defaultAccessorSuffix;

    template <typename T>
    static Ptr<T> FindGlobalDecl(Ptr<AST::File> file, const std::string& identifier)
    {
        for (auto& decl : file->decls) {
            if (decl->identifier == identifier) {
                return DynamicCast<T>(decl.get());
            }
        }
        return nullptr;
    }

    template <typename T>
    static Ptr<T> FindMemberDecl(AST::Decl& decl, const std::string& identifier)
    {
        for (auto& member : decl.GetMemberDecls()) {
            if (member->identifier == identifier) {
                return DynamicCast<T>(member.get());
            }
        }
    
        return nullptr;
    }
    
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
    static OwnedPtr<AST::Expr> CreateTypeCast(
        OwnedPtr<AST::Expr> selector, Ptr<AST::Ty> castTy,
        std::function<OwnedPtr<AST::Expr>(Ptr<AST::VarDecl>)> createMatchedBranch,
        OwnedPtr<AST::Expr> otherwiseBranch, Ptr<AST::Ty> ty);

    /**
     * match ([selector]) {
     *   case v : [castTy] => v
     *   case _ => throw Exception([message])
     * }
     */
    OwnedPtr<AST::Expr> CreateTypeCastOrThrow(
        OwnedPtr<AST::Expr> selector, Ptr<AST::Ty> castTy, const std::string& message);

    /**
     * match ([selector]) {
     *   case v : [castTy] => v
     *   case _ => zerValue<[castTy]>()
     * }
     */
    OwnedPtr<AST::Expr> CreateTypeCastOrZeroValue(OwnedPtr<AST::Expr> selector, Ptr<AST::Ty> castTy) const;

    /**
     * Replaces all argument's types and return type with Any
     */
    Ptr<AST::FuncTy> EraseFuncTypes(Ptr<AST::FuncTy> funcTy);

    std::string BuildMockAccessorIdentifier(
        const AST::Decl& originalDecl, AccessorKind kind, bool includeArgumentTypes = false) const;
    std::string BuildArgumentList(const AST::Decl& decl) const;
    std::string GetOriginalIdentifierOfAccessor(const AST::FuncDecl& decl) const;
    std::string GetOriginalIdentifierOfMockAccessor(const AST::Decl& decl) const;

    bool MayContainInternalTypes(Ptr<AST::Ty> ty) const;
private:
    ImportManager& importManager;
    TypeManager& typeManager;
    GenericInstantiationManager* gim {nullptr};
    BaseMangler mangler;

    Ptr<AST::FuncDecl> getTypeForTypeParamDecl = nullptr;
    Ptr<AST::FuncDecl> isSubtypeTypesDecl = nullptr;
    Ptr<AST::StructDecl> arrayDecl;
    Ptr<AST::StructDecl> stringDecl;
    Ptr<AST::EnumDecl> optionDecl;
    Ptr<AST::InheritableDecl> toStringDecl;
    Ptr<AST::ClassDecl> objectDecl;
    Ptr<AST::FuncDecl> zeroValueDecl;
    Ptr<AST::ClassDecl> exceptionClassDecl;

    static bool IsMockAccessorRequired(const AST::Decl& decl);
    static std::string BuildTypeArgumentList(const AST::Decl& decl);
    static AccessorKind ComputeAccessorKind(const AST::FuncDecl& accessorDecl);
    static bool IsGetterForMutField(const AST::FuncDecl& accessorDecl);

    static Ptr<AST::Decl> FindMockGlobalDecl(const AST::Decl& decl, const std::string& name);
    static void PrependFuncGenericSubst(
        const Ptr<AST::Generic> originalGeneric,
        const Ptr<AST::Generic> mockedGeneric,
        std::vector<TypeSubst>& classSubsts);
    static std::vector<TypeSubst> BuildGenericSubsts(const Ptr<AST::InheritableDecl> decl);
    static std::string GetForeignAccessorName(const AST::FuncDecl& decl);

    Ptr<AST::Decl> FindAccessor(AST::ClassDecl& outerClass, const Ptr<AST::Decl> member,
        const std::vector<Ptr<AST::Ty>>& instTys, AccessorKind kind) const;
    Ptr<AST::Decl> FindAccessorForMemberAccess(const AST::MemberAccess& memberAccess,
        const Ptr<AST::Decl> resolvedMember, const std::vector<Ptr<AST::Ty>>& instTys, AccessorKind kind) const;
    Ptr<AST::FuncDecl> FindTopLevelAccessor(Ptr<AST::Decl> member, AccessorKind kind) const;
    OwnedPtr<AST::Expr> WrapCallTypeArgsIntoArray(const AST::Decl& decl);
    bool IsGeneratedGetter(AccessorKind kind);
    Ptr<AST::FuncDecl> FindAccessor(Ptr<AST::MemberAccess> ma, Ptr<AST::Decl> target, AccessorKind kind) const;
    std::vector<Ptr<AST::Ty>> AddGenericIfNeeded(AST::Decl& originalDecl, AST::Decl& mockedDecl) const;
    OwnedPtr<AST::ArrayLit> WrapCallArgsIntoArray(const AST::FuncDecl& mockedFunc);
    Ptr<AST::Ty> GetInstantiatedTy(const Ptr<AST::Ty> ty, std::vector<TypeSubst>& typeSubsts);
    void SetGetTypeForTypeParamDecl(AST::Package& pkg);
    void SetIsSubtypeTypes(AST::Package& pkg);
    OwnedPtr<AST::Expr> CreateGetTypeForTypeParameterCall(const Ptr<AST::GenericParamDecl> genericParam);
    OwnedPtr<AST::Expr> CreateIsSubtypeTypesCall(Ptr<AST::Ty> tyToCheck, Ptr<AST::Ty> ty);
    std::string Mangle(const AST::Decl& decl) const;

    OwnedPtr<AST::RefExpr> CreateRefExprWithInstTys(
        AST::Decl& target, const std::vector<Ptr<AST::Ty>>& instTys,
        const std::string& refName, AST::File& curFile) const;
    OwnedPtr<AST::RefExpr> CreateDeclBasedReferenceExpr(
        AST::Decl& target, const std::vector<Ptr<AST::Ty>>& instTys,
        const std::string& refName, AST::File& curFile
    ) const;

    OwnedPtr<AST::CallExpr> CreateZeroValue(Ptr<AST::Ty> ty, AST::File& curFile) const;

    template <typename T>
    Ptr<T> GetGenericDecl(Ptr<T> decl) const
    {
        if (decl->genericDecl) {
            return StaticCast<T>(decl->genericDecl);
        }

        return decl;
    }

    // Type instantiation helpers
    void Instantiate(AST::Node& node) const;
    Ptr<AST::ClassLikeDecl> GetInstantiatedDeclInCurrentPackage(const Ptr<const AST::ClassLikeTy> classLikeToMockTy);
    std::optional<std::unordered_set<Ptr<AST::Decl>>> TryGetInstantiatedDecls(AST::Decl& decl) const;
    Ptr<AST::Decl> GetInstantiatedMemberTarget(AST::Ty& baseTy, AST::Decl& target) const;
    Ptr<AST::Decl> GetInstantiatedDeclWithGenericInfo(AST::Decl& decl, const std::vector<Ptr<AST::Ty>>& instTys) const;
    template <typename T> Ptr<T> GetInstantiatedDecl(
        Ptr<T> decl, const std::vector<Ptr<AST::Ty>>& instTys, bool isInstEnabled, Ptr<AST::Ty> baseTy = nullptr) const
    {
        if (!isInstEnabled || (!decl->ty->HasGeneric() && !decl->TestAttr(AST::Attribute::GENERIC))) {
            return decl;
        }
        auto memberDeclInInstantiatedClass = baseTy ? GetInstantiatedMemberTarget(*baseTy, *decl) : decl;
        if (!memberDeclInInstantiatedClass->TestAttr(AST::Attribute::GENERIC)) {
            return Ptr(RawStaticCast<T*>(memberDeclInInstantiatedClass));
        }

        return Ptr(RawStaticCast<T*>(GetInstantiatedDeclWithGenericInfo(*memberDeclInInstantiatedClass, instTys)));
    }

    Ptr<AST::ClassDecl> GetExtendedClassDecl(AST::FuncDecl& decl) const;
    void UpdateRefTypesTarget(
        Ptr<AST::Type> type, Ptr<AST::Generic> oldGeneric, Ptr<AST::Generic> newGeneric) const;
    int GetIndexOfGenericTypeParam(Ptr<AST::Ty> ty, Ptr<AST::Generic> generic) const;

    friend class TestManager;
    friend class MockManager;
    friend class MockSupportManager;
};
}

#endif // CANGJIE_SEMA_MOCK_UTILS_H
