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

#ifndef CANGJIE_SEMA_TYPE_MANAGER_H
#define CANGJIE_SEMA_TYPE_MANAGER_H

#include <cassert>
#include <stack>
#include <unordered_map>

#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/Sema/CommonTypeAlias.h"
#include "cangjie/Utils/Utils.h"

namespace Cangjie {
enum class TypeCompatibility { INCOMPATIBLE, SUBTYPE, IDENTICAL };
const std::string FUTURE_TYPE_NAME = "Future";

const size_t INT32_SIZE = 4;
#define TYPE_PRIMITIVE_MIN AST::TypeKind::TYPE_UNIT
#define TYPE_PRIMITIVE_MAX AST::TypeKind::TYPE_BOOLEAN

class TyVarScope;
class InstCtxScope;

class TypeManager {
public:
    TypeManager();
    ~TypeManager();

    // Primitive types.
    static Ptr<AST::PrimitiveTy> GetPrimitiveTy(AST::TypeKind kind);

    static Ptr<AST::InvalidTy> GetInvalidTy()
    {
        return &theInvalidTy;
    }
    static Ptr<AST::PrimitiveTy> GetNothingTy()
    {
        return GetPrimitiveTy(AST::TypeKind::TYPE_NOTHING);
    }
    static Ptr<AST::QuestTy> GetQuestTy()
    {
        return &theQuestTy;
    }
    static Ptr<AST::CStringTy> GetCStringTy()
    {
        return &theCStringTy;
    }
    /**
     * Check if there are generic types in ty.
     */
    static Ptr<AST::Ty> GetNonNullTy(Ptr<AST::Ty> ty);
    static bool IsCoreFutureType(const AST::Ty& ty);

    /** APIs to generate new type or get existed type from cache. */
    Ptr<AST::GenericsTy> GetGenericsTy(AST::GenericParamDecl& gpd);
    Ptr<AST::EnumTy> GetEnumTy(AST::EnumDecl& ed, const std::vector<Ptr<AST::Ty>>& typeArgs = {});
    Ptr<AST::RefEnumTy> GetRefEnumTy(AST::EnumDecl& ed, const std::vector<Ptr<AST::Ty>>& typeArgs);
    Ptr<AST::StructTy> GetStructTy(AST::StructDecl& sd, const std::vector<Ptr<AST::Ty>>& typeArgs);
    Ptr<AST::TupleTy> GetTupleTy(const std::vector<Ptr<AST::Ty>>& typeArgs, bool isClosureTy = false);
    Ptr<AST::FuncTy> GetFunctionTy(const std::vector<Ptr<AST::Ty>>& paramTys, Ptr<AST::Ty> retTy,
        AST::FuncTy::Config cfg = {false, false, false, false});

    Ptr<AST::ArrayTy> GetArrayTy(Ptr<AST::Ty> elemTy, unsigned int dims);
    Ptr<AST::VArrayTy> GetVArrayTy(AST::Ty& elemTy, int64_t size);
    Ptr<AST::PointerTy> GetPointerTy(Ptr<AST::Ty> elemTy);
    Ptr<AST::ArrayTy> GetArrayTy();
    Ptr<AST::ClassTy> GetClassTy(AST::ClassDecl& cd, const std::vector<Ptr<AST::Ty>>& typeArgs);
    Ptr<AST::ClassThisTy> GetClassThisTy(AST::ClassDecl& cd, const std::vector<Ptr<AST::Ty>>& typeArgs);
    Ptr<AST::InterfaceTy> GetInterfaceTy(AST::InterfaceDecl& id, const std::vector<Ptr<AST::Ty>>& typeArgs);
    Ptr<AST::TypeAliasTy> GetTypeAliasTy(AST::TypeAliasDecl& tad, const std::vector<Ptr<AST::Ty>>& typeArgs);
    Ptr<AST::Ty> GetIntersectionTy(const std::set<Ptr<AST::Ty>>& tys);
    Ptr<AST::Ty> GetUnionTy(const std::set<Ptr<AST::Ty>>& tys);
    Ptr<AST::Ty> GetAnyTy()
    {
        return anyTy;
    }
    Ptr<AST::Ty> GetCTypeTy() const
    {
        // `ctypeTy` is nullptr only when the core package of the standard library does not exist.
        // In this case, `anyTy` is used as a placeholder.
        return ctypeTy ? ctypeTy : anyTy;
    }
    void SetSemaAnyTy(Ptr<AST::Ty> semAnyTy)
    {
        anyTy = semAnyTy;
    }
    void SetSemaCTypeTy(Ptr<AST::Ty> semCTypeTy)
    {
        ctypeTy = semCTypeTy;
    }

    /**
     * Instantiate the @p ty of a node, change the key in @p typeMapping, to the value in @p typeMapping.
     * If a substituted result type is appeared in @p ctxVars
     * then this type is considered as fully qualified and cannot be substituted to other type.
     * @return the instantiated type.
     */
    std::set<Ptr<AST::Ty>> GetInstantiatedTys(Ptr<AST::Ty> ty, const MultiTypeSubst& mts);
    Ptr<AST::Ty> GetBestInstantiatedTy(Ptr<AST::Ty> ty, const MultiTypeSubst& mts);
    Ptr<AST::Ty> GetInstantiatedTy(Ptr<AST::Ty> ty, const TypeSubst& typeMapping);
    std::set<Ptr<AST::Ty>> ApplyTypeSubstForTys(const TypeSubst& subst, const std::set<Ptr<TyVar>>& tys);
    Ptr<AST::Ty> ApplySubstPack(const Ptr<AST::Ty> declaredTy, const SubstPack& maps, bool ignoreUnsolved = false);
    std::set<Ptr<AST::Ty>> ApplySubstPackNonUniq(
        const Ptr<AST::Ty> declaredTy, const SubstPack& maps, bool ignoreUnsolved = false);

    /** instantiate using the current InstCtxScope */
    Ptr<AST::Ty> InstOf(Ptr<AST::Ty> ty);
    /** recover instance ty vars back to universal ty vars for err reporting */
    Ptr<AST::Ty> RecoverUnivTyVar(Ptr<AST::Ty> ty);
    SubstPack GetInstMapping();
    void PackMapping(SubstPack& maps, const MultiTypeSubst m);
    void PackMapping(SubstPack& maps, const TypeSubst m);
    void PackMapping(SubstPack& maps, AST::GenericsTy& tv, AST::Ty& instTy);
    /*
     * Flatten the two-step substitution of SubstPack into one-step.
     * Mainly for places where updating to SubstPack is too much work.
     * An example:
     * input: u2i = [T |-> T'], inst = [T' |-> {Int64 | String}]
     * output: [T |-> {Int64 | String}]
     */
    MultiTypeSubst ZipSubstPack(const SubstPack& mapping);
    void MakeInstTyVar(SubstPack& maps, AST::GenericsTy& uTv);
    void MakeInstTyVar(SubstPack& maps, const AST::Decl& d);

    std::vector<Ptr<AST::Ty>> GetTypeArgs(const AST::Ty& ty);
    Ptr<AST::Ty> GetBlockRealTy(const AST::Block& block) const;
    Ptr<AST::Ty> GetRealExtendedTy(AST::Ty& child, const AST::Ty& interfaceTy);

    /**
     * Get all super type of @p ty.
     */
    std::unordered_set<Ptr<AST::Ty>> GetAllSuperTys(
        AST::Ty& ty, const TypeSubst& typeMapping = {}, bool withExtended = true);
    /**
     * Use this instead of GetAllSuperTys whenever possible.
     */
    bool HasSuperTy(AST::Ty& ty, AST::Ty& superTy, const TypeSubst& typeMapping, bool withExtended = true);
    std::unordered_set<Ptr<AST::Ty>> GetAllCommonSuperTys(const std::unordered_set<Ptr<AST::Ty>>& tys);
    TypeSubst GetSubstituteMapping(const AST::Ty& nominalTy, const TypeSubst& typeMapping);
    std::vector<Ptr<AST::InterfaceTy>> GetAllSuperInterfaceTysBFS(const AST::InheritableDecl& decl);

    /** APIs to check type relations. */
    bool IsSubtype(Ptr<AST::Ty> leaf, Ptr<AST::Ty> root, bool implicitBoxed = true, bool allowOptionBox = true);
    bool IsFuncSubtype(const AST::Ty& leaf, const AST::Ty& root);
    bool IsFuncParametersSubtype(const AST::FuncTy& leaf, const AST::FuncTy& root);
    bool IsTupleSubtype(const AST::Ty& leaf, const AST::Ty& root);
    bool IsTyEqual(Ptr<AST::Ty> subTy, Ptr<AST::Ty> baseTy);
    bool IsPlaceholderEqual(AST::Ty& leaf, AST::Ty& root);
    bool IsLitBoxableType(Ptr<AST::Ty> leaf, Ptr<AST::Ty> root);
    bool HasExtensionRelation(AST::Ty& childTy, AST::Ty& interfaceTy);

    /**
     * Check if two function types have the same parameter type. This is used for checking function overloading and
     * function overriding.
     */
    bool IsFuncParameterTypesIdentical(const AST::FuncTy& t1, const AST::FuncTy& t2);
    /**
     * Check if two set of param types are same.
     * Firstly, substituting @p paramTys1 with the @p typeMapping and then checking with @p paramTys2 .
     */
    bool IsFuncParameterTypesIdentical(const std::vector<Ptr<AST::Ty>>& paramTys1,
        const std::vector<Ptr<AST::Ty>>& paramTys2, const TypeSubst& typeMapping = {});
    TypeCompatibility CheckTypeCompatibility(
        Ptr<AST::Ty> lvalue, Ptr<AST::Ty> rvalue, bool implicitBoxed = true, bool isGeneric = false);
    bool CheckGenericDeclInstantiation(Ptr<const AST::Decl> d, const std::vector<Ptr<AST::Ty>>& typeArgs);
    bool CheckExtendWithConstraint(const AST::Ty& ty, Ptr<AST::ExtendDecl> extend);

    /**
     * Use the base ty of memberAccess to get the map of all the generic ty to instant ty, use to handle generic param
     * pass.
     */
    void GenerateStructDeclGenericMapping(MultiTypeSubst& m, const AST::InheritableDecl& decl, const AST::Ty& targetTy);
    void GenerateTypeMappingForUpperBounds(MultiTypeSubst& m, const AST::MemberAccess& ma, AST::Decl& target);
    void GenerateTypeMappingForUpperBounds(SubstPack& m, const AST::MemberAccess& ma, AST::Decl& target);
    void GenerateGenericMapping(MultiTypeSubst& m, AST::Ty& baseType);
    void GenerateGenericMapping(SubstPack& m, AST::Ty& baseType);
    TypeSubst GenerateGenericMappingFromGeneric(const AST::Decl& parentDecl, const AST::Decl& childDecl) const;
    MultiTypeSubst GenerateStructDeclTypeMapping(const AST::Decl& decl);
    void ReplaceIdealTy(Ptr<AST::Ty>* ty);
    void RestoreJavaGenericsTy(AST::Decl& decl) const;

    /**
     * Get all extend interface types.
     */
    std::unordered_set<Ptr<AST::Ty>> GetAllExtendInterfaceTy(AST::Ty& ty);
    bool HasExtendedInterfaceTy(AST::Ty& ty, AST::Ty& superTy, const TypeSubst& typeMapping);
    /** Get the ty used for getting related extends. */
    Ptr<AST::Ty> GetTyForExtendMap(AST::Ty& ty);
    /** Get builtin ty extends. For array ty & cpointer ty, will only return instantiated extends. */
    std::set<Ptr<AST::ExtendDecl>> GetBuiltinTyExtends(AST::Ty& ty);
    std::optional<bool> GetOverrideCache(
        const AST::FuncDecl* src, const AST::FuncDecl* target, AST::Ty* baseTy, AST::Ty* expectInstParent);
    void AddOverrideCache(
        const AST::FuncDecl& src, const AST::FuncDecl& target, AST::Ty* baseTy, AST::Ty* expectInstParent, bool val);
    /** Get extends for given generic/instantiated @p decl */
    std::set<Ptr<AST::ExtendDecl>> GetDeclExtends(const AST::InheritableDecl& decl);
    /** Get origin extends for given builtin ty. For array ty & cpointer ty, will only return generic extends.*/
    std::set<Ptr<AST::ExtendDecl>> GetAllExtendsByTy(AST::Ty& ty);
    std::unordered_set<Ptr<const AST::InheritableDecl>> GetAllExtendedDecls();
    std::unordered_set<Ptr<AST::Ty>> GetAllExtendedBuiltIn();
    void UpdateBuiltInTyExtendDecl(AST::Ty& builtinTy, AST::ExtendDecl& ed);
    void RecordUsedExtend(AST::Ty& child, AST::Ty& interfaceTy);
    void RecordUsedGenericExtend(AST::Ty& boxedTy, Ptr<AST::ExtendDecl> extend = nullptr);
    void RemoveExtendFromMap(AST::ExtendDecl& ed);
    std::unordered_set<Ptr<AST::Ty>> GetAllBoxedTys() const
    {
        return boxedTys;
    }

    void Clear();

    void ClearMapCache()
    {
        builtinTyToExtendMap.clear();
        instantiateBuiltInTyToExtendMap.clear();
        declToExtendMap.clear();
        declInstantiationStatus.clear();
        tyExtendInterfaceTyMap.clear();
        tyToSuperTysMap.clear();
        overrideOrShadowCache.clear();
        ClearRecordUsedExtends();
    }

    std::unordered_set<Ptr<AST::ExtendDecl>> GetBoxUsedExtends() const
    {
        return boxUsedExtends;
    }

    std::unordered_set<Ptr<AST::InheritableDecl>> GetBoxedNonGenericDecls() const
    {
        return boxedNonGenericDecls;
    }

    std::unordered_set<Ptr<AST::ExtendDecl>> GetTyUsedExtends(Ptr<AST::Ty> ty) const
    {
        auto found = tyUsedExtends.find(ty);
        return found != tyUsedExtends.end() ? found->second : std::unordered_set<Ptr<AST::ExtendDecl>>{};
    }

    void ClearRecordUsedExtends()
    {
        boxUsedExtends.clear();
        tyUsedExtends.clear();
        checkedTyExtendRelation.clear();
        boxedTys.clear();
    }

    /**
     * whether the classLike decl has func decl override the base funcDecl.
     * @param baseDecl the classLike decl
     * @param funcDecl the base funcDecl
     */
    Ptr<AST::Decl> GetOverrideDeclInClassLike(
        AST::Decl& baseDecl, const AST::FuncDecl& funcDecl, bool withAbstractOverrides = false);
    /**
     * whether the decl is override the funcDecl.
     */
    bool IsFuncDeclSubType(const AST::FuncDecl& decl, const AST::FuncDecl& funcDecl);

    /**
     * Determine whether the interfaces implemented by two extension declarations have inheritance relationships.
     * @param r, l the two extension declarations being compared
     * @return two bool values, the first indicating whether a relationship exists.
     *         the second indicates that r is the parent interface of l if the former is true.
     */
    std::pair<bool, bool> IsExtendInheritRelation(const AST::ExtendDecl& r, const AST::ExtendDecl& l);

    bool PairIsOverrideOrImpl(const AST::Decl& child, const AST::Decl& parent, const Ptr<AST::Ty> baseTy = nullptr,
        const Ptr<AST::Ty> parentTy = nullptr);

    // Try to constrain tv by tyCtor as an upperbound.
    // tyCtor's type args must be GenericsTy.
    // The tyCtor's type arguments in tv's upperbound will be replaced
    //   by newly allocated placeholder type vars,
    //   which will eventually be solved together with tv.
    // Returns the new upperbound.
    // Will return nullptr if the new constaint can't possibly be satisfied.
    //
    // e.g. ConstrainByCtor(T, U->R) will:
    // 1. allocate new ty var U' and R'
    // 2. add U'->R' to T's upperbound
    // 3. return U'->R'
    Ptr<AST::Ty> ConstrainByCtor(AST::GenericsTy& tv, AST::Ty& tyCtor);
    // Similar to ConstrainByCtor, but the upperbound will be added to sum instead of ubs.
    // Also, in case some of the constructors are generic, the ty args between upperbounds
    // can be shared, in order to sync the constraints (in a best-effort manner).
    Ptr<AST::Ty> AddSumByCtor(AST::GenericsTy& tv, AST::Ty& tyCtor, std::vector<Ptr<AST::GenericsTy>>& tyArgs);
    bool OfSameCtor(Ptr<AST::Ty> ty, Ptr<AST::Ty> tyCtor);
    // currently only for collecting map from member to decls
    Ptr<AST::Decl> GetDummyBuiltInDecl(Ptr<AST::Ty> ty);
    /** Get extend decls that extend @p baseTy with given @p interfaceTy */
    std::optional<Ptr<AST::ExtendDecl>> GetExtendDeclByInterface(AST::Ty& baseTy, AST::Ty& interfaceTy);
    /**
     * Get extend decls that extend @p baseTy with given @p member, it can be default implementation in interface or
     * extension's member.
     */
    Ptr<AST::ExtendDecl> GetExtendDeclByMember(const AST::Decl& member, AST::Ty& baseTy);

    /**
     * Allocate/release a placeholder TyVar with a dummy declaration. Used as placeholder for instantiation type,
     * or placeholder for temporarily unknown type, such as unannotated lambda param type and recursive function's
     * return type.
     * isPlaceholder field will be set to true.
     * Release should be managed only by TyVarScope.
     */
    Ptr<AST::GenericsTy> AllocTyVar(
        const std::string& srcId = "Ti", bool needSolving = false, Ptr<TyVar> derivedFrom = nullptr);
    size_t ScopeDepthOfTyVar(const AST::GenericsTy& tyVar);
    bool TyVarHasNoSum(TyVar& tv) const;
    const std::set<Ptr<TyVar>>& GetUnsolvedTyVars();
    void MarkAsUnsolvedTyVar(AST::GenericsTy& tv);
    std::set<Ptr<TyVar>> GetInnermostUnsolvedTyVars();
    Ptr<AST::Ty> TryGreedySubst(Ptr<AST::Ty> ty);
    // constraints for placeholder type vars
    Constraint constraints;
private:
    friend class TyVarScope;
    friend class InstCtxScope;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    friend class TyGeneralizer;
#endif
    void ReleaseTyVar(Ptr<AST::GenericsTy> genTy);

public:
    /** The extend context maps. */
    std::unordered_map<Ptr<AST::Ty>, std::set<Ptr<AST::ExtendDecl>>> builtinTyToExtendMap;
    std::unordered_map<Ptr<const AST::InheritableDecl>, std::set<Ptr<AST::ExtendDecl>>> declToExtendMap;

private:
    inline static AST::InvalidTy theInvalidTy = AST::InvalidTy();
    inline static AST::AnyTy theAnyTy = AST::AnyTy();
    inline static AST::QuestTy theQuestTy = AST::QuestTy();
    inline static AST::CStringTy theCStringTy = AST::CStringTy();
    inline static std::vector<AST::PrimitiveTy> primitiveTys = []() {
        std::vector<AST::PrimitiveTy> tys;
        for (auto i = static_cast<int32_t>(TYPE_PRIMITIVE_MIN); i <= static_cast<int32_t>(TYPE_PRIMITIVE_MAX); i++) {
            if (i == static_cast<int32_t>(AST::TypeKind::TYPE_NOTHING)) {
                tys.emplace_back(AST::NothingTy());
            } else {
                tys.emplace_back(AST::PrimitiveTy(static_cast<AST::TypeKind>(i)));
            }
        }
        return tys;
    }();

    struct TypePointer {
        explicit TypePointer(Ptr<AST::Ty> ptr) : ptr(ptr)
        {
        }
        ~TypePointer() = default;
        Ptr<const AST::Ty> operator->() const
        {
            return ptr;
        }
        Ptr<AST::Ty> Get() const
        {
            return ptr;
        }
        bool HasValue() const
        {
            return ptr != nullptr;
        }
        bool operator==(const TypePointer& other) const
        {
            if (!this->HasValue() && !other.HasValue()) {
                return true;
            }
            return this->HasValue() && other.HasValue() && *this->Get() == *other.Get();
        }

    private:
        Ptr<AST::Ty> const ptr;
    };

    struct TypeHash {
        size_t operator()(const TypePointer& p) const
        {
            return p.HasValue() ? p->Hash() : 0;
        }
    };
    // These unordered sets are hash tables to save types.
    std::unordered_set<TypePointer, TypeHash> allocatedTys;
    std::unordered_set<std::pair<Ptr<AST::Ty>, Ptr<AST::Ty>>, HashPair> checkedTyExtendRelation;
    std::unordered_set<Ptr<AST::Ty>> boxedTys;
    std::unordered_set<Ptr<AST::ExtendDecl>> boxUsedExtends;
    std::unordered_set<Ptr<AST::InheritableDecl>> boxedNonGenericDecls;
    /** Used generic extends for each instantiated type. */
    std::unordered_map<Ptr<AST::Ty>, std::unordered_set<Ptr<AST::ExtendDecl>>> tyUsedExtends;
    std::unordered_map<Ptr<AST::Ty>, std::set<Ptr<AST::ExtendDecl>>> instantiateBuiltInTyToExtendMap;
    /** TypeManager caches. */
    std::unordered_map<Ptr<const AST::Ty>, std::unordered_set<Ptr<AST::Ty>>> tyExtendInterfaceTyMap;
    struct TypeInfo {
        Ptr<const AST::Ty> ty;
        TypeSubst mapping;
        bool withExtended;
    };
    struct TypeInfoHash {
        size_t operator()(const TypeInfo& info) const
        {
            size_t ret = 0;
            ret = hash_combine<Ptr<const AST::Ty>>(ret, info.ty);
            for (auto n : info.mapping) {
                ret = hash_combine<Ptr<AST::Ty>>(ret, n.first);
                ret = hash_combine<Ptr<AST::Ty>>(ret, n.second);
            }
            ret = hash_combine<bool>(ret, info.withExtended);
            return ret;
        }
    };
    struct TypeInfoEqual {
        bool operator()(const TypeInfo& lhs, const TypeInfo& rhs) const
        {
            if (lhs.ty != rhs.ty || lhs.withExtended != rhs.withExtended || lhs.mapping.size() != rhs.mapping.size()) {
                return false;
            }
            for (auto it1 : lhs.mapping) {
                auto it2 = rhs.mapping.find(it1.first);
                if (it2 == rhs.mapping.end() || it1.second != it2->second) {
                    return false;
                }
            }
            return true;
        }
    };
    std::unordered_map<TypeInfo, std::unordered_set<Ptr<AST::Ty>>, TypeInfoHash, TypeInfoEqual> tyToSuperTysMap;
    /** Store checked typeArgs instantiation result for generic decls. */
    std::unordered_map<Ptr<const AST::Decl>, std::map<std::vector<Ptr<AST::Ty>>, bool>> declInstantiationStatus;
    Ptr<AST::Ty> anyTy = &theAnyTy;
    Ptr<AST::Ty> ctypeTy = nullptr;

    struct SubtypeCacheKey {
        Ptr<AST::Ty> leaf{nullptr};
        Ptr<AST::Ty> root{nullptr};
        bool implicitBoxed{false};
        bool allowOptionBox{false};

        SubtypeCacheKey(Ptr<AST::Ty> leaf, Ptr<AST::Ty> root, bool implicitBoxed = true, bool allowOptionBox = true)
            : leaf(leaf), root(root), implicitBoxed(implicitBoxed), allowOptionBox(allowOptionBox)
        {
        }
    };
    struct SubtypeCacheKeyHash {
        size_t operator()(const SubtypeCacheKey& key) const
        {
            size_t ret = 0;
            ret = hash_combine(ret, key.leaf);
            ret = hash_combine(ret, key.root);
            ret = hash_combine(ret, key.implicitBoxed);
            ret = hash_combine(ret, key.allowOptionBox);
            return ret;
        }
    };
    struct SubtypeCacheKeyEqual {
        bool operator()(const SubtypeCacheKey& lhs, const SubtypeCacheKey& rhs) const
        {
            return std::tie(lhs.leaf, lhs.root, lhs.implicitBoxed, lhs.allowOptionBox) ==
                std::tie(rhs.leaf, rhs.root, rhs.implicitBoxed, rhs.allowOptionBox);
        }
    };
    std::unordered_map<SubtypeCacheKey, bool, SubtypeCacheKeyHash, SubtypeCacheKeyEqual> subtypeCache;

    struct OverrideOrShadowKey {
        const AST::FuncDecl* src{nullptr};
        const AST::FuncDecl* target{nullptr};
        const AST::Ty* baseTy{nullptr};
        const AST::Ty* expectInstParent{nullptr};

        OverrideOrShadowKey(const AST::FuncDecl* s, const AST::FuncDecl* t, const AST::Ty* b, const AST::Ty* e)
            : src(s), target(t), baseTy(b), expectInstParent(e)
        {
        }
    };
    struct OverrideOrShadowEqual {
        bool operator()(const OverrideOrShadowKey& lhs, const OverrideOrShadowKey& rhs) const
        {
            return std::tie(lhs.src, lhs.target, lhs.baseTy, lhs.expectInstParent) ==
                std::tie(rhs.src, rhs.target, rhs.baseTy, rhs.expectInstParent);
        }
    };
    struct OverrideOrShadowHash {
        size_t operator()(const OverrideOrShadowKey& key) const
        {
            size_t ret = 0;
            ret = hash_combine(ret, key.src);
            ret = hash_combine(ret, key.target);
            ret = hash_combine(ret, key.baseTy);
            ret = hash_combine(ret, key.expectInstParent);
            return ret;
        }
    };
    /** Stores the overwrite or shadow judgment result determined based on BaseTy, src funcDecl, and target funcDecl. */
    std::unordered_map<OverrideOrShadowKey, bool, OverrideOrShadowHash, OverrideOrShadowEqual> overrideOrShadowCache;

    // a counter for naming tyvars
    unsigned long long nextUniqId{0};
    // all tyvar resources
    std::unordered_set<TypePointer, TypeHash> tyVarPool;
    // dummy decls to be associated with tyvar
    std::vector<OwnedPtr<AST::GenericParamDecl>> dummyGenDecls;
    std::map<Ptr<AST::Ty>, OwnedPtr<AST::Decl>> dummyBuiltInDecls;
    // the level each tyvar is introduced, used when unifying 2 tyvars;
    // should be [high level |-> low level], NOT the opposite
    std::map<Ptr<const AST::GenericsTy>, size_t> tyVarScopeDepth;
    std::set<Ptr<TyVar>> unsolvedTyVars;
    // only internal states. remembering the scopes of tyvar introduction and mapping context
    std::vector<Ptr<TyVarScope>> tyVarScopes;
    std::vector<Ptr<InstCtxScope>> instCtxScopes;
    Ptr<TyVarScope> topScope; // wrap all uses of placeholder ty vars in SubstPack, just in case

private:
    template <typename TypeT, typename... Args> TypeT* GetTypeTy(Args&&... args);
    bool IsClassTyEqual(AST::Ty& leaf, AST::Ty& root);
    Ptr<AST::Ty> GetExtendInterfaceSuperTy(AST::ClassTy& classTy, const AST::Ty& interfaceTy);

    /**
     * The class is used to store typeMapping and ctxVars as context condition of the recursive instantiation.
     */
    class TyInstantiator {
    private:
        friend class TypeManager;
        TyInstantiator(TypeManager& tyMgr, const TypeSubst& mapping) : tyMgr(tyMgr), typeMapping(mapping)
        {
        }
        ~TyInstantiator() = default;

        inline Ptr<AST::Ty> Instantiate(Ptr<AST::Ty> ty)
        {
            return AST::Ty::IsTyCorrect(ty) ? Instantiate(*ty) : ty;
        }
        Ptr<AST::Ty> Instantiate(AST::Ty& ty);
        Ptr<AST::Ty> GetInstantiatedStructTy(AST::StructTy& structTy);
        Ptr<AST::Ty> GetInstantiatedClassTy(AST::ClassTy& classTy);
        Ptr<AST::Ty> GetInstantiatedInterfaceTy(AST::InterfaceTy& interfaceTy);
        Ptr<AST::Ty> GetInstantiatedEnumTy(AST::EnumTy& enumTy);
        Ptr<AST::Ty> GetInstantiatedArrayTy(AST::ArrayTy& arrayTy);
        Ptr<AST::Ty> GetInstantiatedPointerTy(AST::PointerTy& cptrTy);
        // Get instantiated ty of set type 'IntersectionTy' and 'UnionTy'.
        template <typename SetTy> Ptr<AST::Ty> GetInstantiatedSetTy(SetTy& ty);
        Ptr<AST::Ty> GetInstantiatedGenericTy(AST::GenericsTy& ty);

        TypeManager& tyMgr;
        const TypeSubst& typeMapping;
    };

    void GetNominalSuperTy(
        const AST::Ty& nominalTy, const TypeSubst& typeMapping, std::unordered_set<Ptr<AST::Ty>>& tyList);
    bool HasNominalSuperTy(AST::Ty& nominalTy, AST::Ty& superTy, const TypeSubst& typeMapping);

    bool IsPlaceholderSubtype(AST::Ty& leaf, AST::Ty& root);
    bool IsGenericSubtype(AST::Ty& leaf, AST::Ty& root, bool implicitBoxed, bool allowOptionBox);
    bool IsClassLikeSubtype(AST::Ty& leaf, AST::Ty& root, bool implicitBoxed, bool allowOptionBox);
    bool IsStructOrEnumSubtype(AST::Ty& leaf, AST::Ty& root, bool implicitBoxed, bool allowOptionBox);
    bool IsArraySubtype(const AST::Ty& leaf, const AST::Ty& root);
    bool IsVArraySubtype(const AST::Ty& leaf, const AST::Ty& root);
    bool IsPointerSubtype(const AST::Ty& leaf, const AST::Ty& root);
    bool IsPrimitiveSubtype(const AST::Ty& leaf, AST::Ty& root);
    bool IsTyExtendInterface(const AST::Ty& classTy, const AST::Ty& interfaceTy);

    bool CheckGenericType(Ptr<AST::Ty> lvalue, Ptr<AST::Ty> rvalue, bool implicitBoxed = true);

    void GenerateExtendGenericMappingVisit(
        MultiTypeSubst& typeMapping, AST::Ty& baseType, std::unordered_set<Ptr<AST::Ty>>& visited);
    void GenerateStructDeclGenericMappingVisit(MultiTypeSubst& m, const AST::InheritableDecl& decl,
        const AST::Ty& targetTy, std::unordered_set<Ptr<AST::Ty>>& visited);
    void GenerateGenericMappingVisit(MultiTypeSubst& m, AST::Ty& baseType, std::unordered_set<Ptr<AST::Ty>>& visited);

    /* Alternative version that generate SubstPack.
     * Should migrate to this version everywhere in the future. */
    void GenerateGenericMappingVisit(
        SubstPack& m, AST::Ty& baseType, std::unordered_set<Ptr<AST::Ty>>& visited, bool contextual);
    void GenerateExtendGenericMappingVisit(
        SubstPack& typeMapping, AST::Ty& baseType, std::unordered_set<Ptr<AST::Ty>>& visited, bool contextual);
    void GenerateStructDeclGenericMappingVisit(SubstPack& m, const AST::InheritableDecl& decl, const AST::Ty& targetTy,
        std::unordered_set<Ptr<AST::Ty>>& visited, bool contextual);

    std::unordered_set<Ptr<AST::Ty>> GetAllExtendInterfaceTyHelper(
        const std::set<Ptr<AST::ExtendDecl>>& extends, const std::vector<Ptr<AST::Ty>>& typeArgs);
    bool HasExtendInterfaceTyHelper(AST::Ty& superTy, const std::set<Ptr<AST::ExtendDecl>>& extends,
        const std::vector<Ptr<AST::Ty>>& typeArgs);
};
} // namespace Cangjie

#endif // CANGJIE_SEMA_TYPE_MANAGER_H
