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
#include <unordered_map>

#include "cangjie/AST/Types.h"
#include "cangjie/Sema/CommonTypeAlias.h"
#include "cangjie/Utils/Utils.h"

namespace Cangjie {
namespace AST {
struct Block;
struct ExtendDecl;
struct MemberAccess;
struct RefType;
} // namespace AST

enum class TypeCompatibility { INCOMPATIBLE, SUBTYPE, IDENTICAL };
const std::string FUTURE_TYPE_NAME = "Future";
const size_t INT32_SIZE = 4;
#define TYPE_PRIMITIVE_MIN AST::TypeKind::TYPE_UNIT
#define TYPE_PRIMITIVE_MAX AST::TypeKind::TYPE_BOOLEAN

class TyVarScope;
class InstCtxScope;
enum class ModalMatchMode {
    SUBTYPE,
    EXACT,
};

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
    static Ptr<AST::PrimitiveTy> GetNothingTy() { return GetPrimitiveTy(AST::TypeKind::TYPE_NOTHING); }
    static Ptr<AST::QuestTy> GetQuestTy() { return &theQuestTy; }
    static Ptr<AST::CStringTy> GetCStringTy() { return &theCStringTy; }
    static Ptr<AST::PrimitiveTy> GetBoolTy() { return GetPrimitiveTy(AST::TypeKind::TYPE_BOOLEAN); }
    AST::DataTy GetCopyTy() const;
    bool IsCopyInterfaceTy(AST::DataTy ty) const;
    /**
     * Check if there are generic types in ty.
     */
    static AST::ModalTy GetNonNullTy(AST::ModalTy ty);
    static bool IsCoreFutureType(const AST::Ty& ty);

    /** APIs to generate new type or get existed type from cache. */
    Ptr<AST::GenericsTy> GetGenericsTy(AST::GenericParamDecl& gpd);
    Ptr<AST::EnumTy> GetEnumTy(AST::EnumDecl& ed, const std::vector<AST::DataTy>& typeArgs);
    Ptr<AST::RefEnumTy> GetRefEnumTy(AST::EnumDecl& ed, const std::vector<AST::DataTy>& typeArgs);
    Ptr<AST::StructTy> GetStructTy(AST::StructDecl& sd, const std::vector<AST::DataTy>& typeArgs);
    Ptr<AST::TupleTy> GetTupleTy(const std::vector<AST::DataTy>& typeArgs, bool isClosureTy = false);
    Ptr<AST::FuncTy> GetFunctionTy(const std::vector<AST::ModalTy>& paramTys, AST::ModalTy retTy,
        AST::FuncTy::Config cfg = {false, false, false, false});

    Ptr<AST::ArrayTy> GetArrayTy(AST::DataTy elemTy, unsigned int dims);
    Ptr<AST::VArrayTy> GetVArrayTy(AST::Ty& elemTy, int64_t size);
    Ptr<AST::PointerTy> GetPointerTy(AST::DataTy elemTy);
    Ptr<AST::ArrayTy> GetArrayTy();
    Ptr<AST::ClassTy> GetClassTy(AST::ClassDecl& cd, const std::vector<AST::DataTy>& typeArgs);
    Ptr<AST::ClassThisTy> GetClassThisTy(AST::ClassDecl& cd, const std::vector<AST::DataTy>& typeArgs);
    Ptr<AST::InterfaceTy> GetInterfaceTy(AST::InterfaceDecl& id, const std::vector<AST::DataTy>& typeArgs);
    Ptr<AST::TypeAliasTy> GetTypeAliasTy(AST::TypeAliasDecl& tad, const std::vector<AST::DataTy>& typeArgs);
    AST::DataTy GetIntersectionTy(const std::set<AST::DataTy>& tys);
    AST::DataTy GetUnionTy(const std::set<AST::DataTy>& tys);
    // TODO: there should be anyTy to all modal
    AST::DataTy GetAnyTy() { return anyTy; }
    AST::DataTy GetCTypeTy() const
    {
        // `ctypeTy` is nullptr only when the core package of the standard library does not exist.
        // In this case, `anyTy` is used as a placeholder.
        return ctypeTy ? ctypeTy : anyTy;
    }
    void SetSemaAnyTy(AST::DataTy semAnyTy) { anyTy = semAnyTy; }
    void SetSemaCTypeTy(AST::DataTy semCTypeTy) { ctypeTy = semCTypeTy; }

    /**
     * Instantiate the @p ty of a node, change the key in @p typeMapping, to the value in @p typeMapping.
     * If a substituted result type is appeared in @p ctxVars
     * then this type is considered as fully qualified and cannot be substituted to other type.
     * @return the instantiated type.
     */
    std::set<AST::ModalTy> GetInstantiatedTys(AST::ModalTy ty, const MultiTypeSubst& mts);
    std::set<AST::DataTy> GetInstantiatedTys(AST::DataTy ty, const MultiTypeSubst& mts);

    AST::ModalTy GetBestInstantiatedTy(AST::ModalTy ty, const MultiTypeSubst& mts);
    AST::ModalTy GetInstantiatedTy(AST::ModalTy ty, const TypeSubst& typeMapping);
    AST::DataTy GetInstantiatedTy(AST::DataTy ty, const TypeSubst& typeMapping);

    /**
     * Apply type substitution if typeMapping is not empty.
     * This is a helper function that combines PackMapping and ApplySubstPack.
     */
    AST::DataTy ApplyTypeSubstForTy(const TypeSubst& typeMapping, AST::DataTy ty);
    std::set<AST::ModalTy> ApplyTypeSubstForTys(const TypeSubst& subst, const std::set<Ptr<TyVar>>& tys);
    AST::ModalTy ApplySubstPack(AST::ModalTy declaredTy, const SubstPack& maps, bool ignoreUnsolved = false);
    AST::DataTy ApplySubstPack(AST::DataTy declaredTy, const SubstPack& maps, bool ignoreUnsolved = false);
    std::set<AST::ModalTy> ApplySubstPackNonUniq(
        AST::ModalTy declaredTy, const SubstPack& maps, bool ignoreUnsolved = false);

    /** instantiate using the current InstCtxScope */
    AST::ModalTy InstOf(AST::ModalTy ty);
    /** recover instance ty vars back to universal ty vars for err reporting */
    AST::ModalTy RecoverUnivTyVar(AST::ModalTy ty);
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

    std::vector<AST::DataTy> GetTypeArgs(const AST::Ty& ty);
    AST::DataTy GetBlockRealTy(const AST::Block& block) const;
    AST::DataTy GetRealExtendedTy(AST::Ty& child, const AST::Ty& interfaceTy);

    /**
     * Get all super type of @p ty.
     */
    std::unordered_set<AST::DataTy> GetAllSuperTys(
        AST::Ty& ty, const TypeSubst& typeMapping = {}, bool withExtended = true);
    /**
     * Use this instead of GetAllSuperTys whenever possible.
     */
    bool HasSuperTy(AST::Ty& ty, AST::Ty& superTy, const TypeSubst& typeMapping, bool withExtended = true);
    std::unordered_set<AST::DataTy> GetAllCommonSuperTys(const std::unordered_set<AST::DataTy>& tys);
    TypeSubst GetSubstituteMapping(const AST::Ty& nominalTy, const TypeSubst& typeMapping);
    std::vector<Ptr<AST::InterfaceTy>> GetAllSuperInterfaceTysBFS(const AST::InheritableDecl& decl);

    /** APIs to check type relations. */
    bool IsSubtype(AST::ModalTy leaf, AST::ModalTy root, bool implicitBoxed = true, bool allowOptionBox = true,
        ModalMatchMode modalMatchMode = ModalMatchMode::SUBTYPE);
    bool IsSubtype(AST::DataTy leaf, AST::DataTy root, bool implicitBoxed = true, bool allowOptionBox = true);
    bool IsFuncSubtype(const AST::Ty& leaf, const AST::Ty& root);
    bool IsFuncParametersSubtype(const AST::FuncTy& leaf, const AST::FuncTy& root);
    bool IsTupleSubtype(const AST::Ty& leaf, const AST::Ty& root);
    bool IsTyEqual(AST::DataTy subTy, AST::DataTy baseTy);
    bool IsTyEqual(AST::ModalTy subTy, AST::ModalTy baseTy);
    bool IsPlaceholderEqual(AST::Ty& leaf, AST::Ty& root);
    bool IsLitBoxableType(AST::DataTy leaf, AST::DataTy root);
    bool HasExtensionRelation(AST::Ty& childTy, AST::Ty& interfaceTy);

    bool IsModalSubtype(AST::ModalTy leaf, AST::ModalTy root);
    bool IsModalSubtype(ModalInfo leaf, ModalInfo root);
    /** Copy type for modal/locality checking; see `TypeManager::ImplementsCopyInterface` in TypeManager.cpp. */
    bool ImplementsCopyInterface(AST::DataTy ty);
    bool NeverImplementsCopyInterface(AST::DataTy ty);

    /**
     * Check if two function types have the same parameter type. This is used for checking function overloading and
     * function overriding.
     */
    bool IsFuncParameterTypesIdentical(const AST::FuncTy& t1, const AST::FuncTy& t2);
    /**
     * Check if two set of param types are same.
     * Firstly, substituting @p paramTys1 with the @p typeMapping and then checking with @p paramTys2 .
     */
    bool IsFuncParameterTypesIdentical(const std::vector<AST::ModalTy>& paramTys1,
        const std::vector<AST::ModalTy>& paramTys2, const TypeSubst& typeMapping = {});
    static bool HasThisParam(const AST::FuncDecl& fd);
    static bool HasThisParam(const AST::Decl& decl);
    static AST::ModalTy GetThisParamTy(const AST::FuncDecl& fd);
    static ModalInfo GetThisParamMode(const AST::Decl& decl);
    /// Get modal of this param of prop accessor
    ModalInfo GetAccessorThisModal(const AST::FuncDecl& fd);
    /// Get modal of target (i.e. ret of getter, value type of settter) of prop acessor
    AST::ModalTy GetAccessorTargetTy(const AST::FuncDecl& fd);

    TypeCompatibility CheckTypeCompatibility(
        AST::ModalTy lvalue, AST::ModalTy rvalue, bool implicitBoxed = true, bool isGeneric = false);
    bool CheckGenericDeclInstantiation(Ptr<const AST::Decl> d, const std::vector<AST::DataTy>& typeArgs);
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
    AST::DataTy ReplaceIdealTy(AST::DataTy ty);
    AST::ModalTy ReplaceIdealTy(AST::ModalTy ty);
    void RestoreJavaGenericsTy(AST::Decl& decl) const;

    /**
     * Get all extend interface types.
     */
    std::unordered_set<AST::DataTy> GetAllExtendInterfaceTy(AST::Ty& ty);
    bool HasExtendedInterfaceTy(AST::Ty& ty, AST::Ty& superTy, const TypeSubst& typeMapping);
    /** Get the ty used for getting related extends. */
    AST::DataTy GetTyForExtendMap(AST::Ty& ty);
    /** Get builtin ty extends. For array ty & cpointer ty, will only return instantiated extends. */
    std::set<Ptr<AST::ExtendDecl>> GetBuiltinTyExtends(AST::Ty& ty);
    std::optional<bool> GetOverrideCache(const AST::FuncDecl* src, const AST::FuncDecl* target, AST::DataTy baseTy,
        ModalInfo baseMode, AST::DataTy expectInstParent, ModalInfo parentMode);
    void AddOverrideCache(const AST::FuncDecl& src, const AST::FuncDecl& target, AST::DataTy baseTy, ModalInfo baseMode,
        AST::DataTy expectInstParent, ModalInfo parentMode, bool val);
    /** Get extends for given generic/instantiated @p decl */
    std::set<Ptr<AST::ExtendDecl>> GetDeclExtends(const AST::InheritableDecl& decl);
    /** Get origin extends for given builtin ty. For array ty & cpointer ty, will only return generic extends.*/
    std::set<Ptr<AST::ExtendDecl>> GetAllExtendsByTy(AST::Ty& ty);
    std::unordered_set<Ptr<const AST::InheritableDecl>> GetAllExtendedDecls();
    std::unordered_set<AST::DataTy> GetAllExtendedBuiltIn();
    void UpdateBuiltInTyExtendDecl(AST::Ty& builtinTy, AST::ExtendDecl& ed);
    void RecordUsedExtend(AST::Ty& child, AST::Ty& interfaceTy);
    void RecordUsedGenericExtend(AST::Ty& boxedTy, Ptr<AST::ExtendDecl> extend = nullptr);
    void RemoveExtendFromMap(AST::ExtendDecl& ed);
    std::unordered_set<AST::DataTy> GetAllBoxedTys() const { return boxedTys; }

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
        overrideMap.clear();
        ClearRecordUsedExtends();
        subtypeCache.clear();
    }

    std::unordered_set<Ptr<AST::ExtendDecl>> GetBoxUsedExtends() const
    {
        return boxUsedExtends;
    }

    std::unordered_set<Ptr<AST::InheritableDecl>> GetBoxedNonGenericDecls() const
    {
        return boxedNonGenericDecls;
    }

    std::unordered_set<Ptr<AST::ExtendDecl>> GetTyUsedExtends(AST::DataTy ty) const
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

    void UpdateTopOverriddenFuncDeclMap(const AST::Decl* src, const AST::Decl* target);
    /**
     * Return the top-most function in the override chain of @p funcDecl.
     * If @p funcDecl is already the top-most (does not override anything), return itself.
     * Walks overrideMap first, then continues on the inheritance chain so a partial
     * map (e.g. Expr.toTokens → Node.toTokens without Node → ToTokens) still reaches
     * the true top.
     */
    Ptr<const AST::FuncDecl> GetTopOverriddenFuncDecl(const AST::FuncDecl* funcDecl) const;
    /**
     * whether the decl is override the funcDecl.
     */
    bool IsFuncDeclSubType(const AST::FuncDecl& decl, const AST::FuncDecl& funcDecl);
    bool IsFuncTySubType(const AST::FuncTy& type1, const AST::FuncTy& type2);

    bool IsFuncDeclEqualType(const AST::FuncDecl& decl, const AST::FuncDecl& funcDecl);

    /**
     * Determine whether the interfaces implemented by two extension declarations have inheritance relationships.
     * @param r, l the two extension declarations being compared
     * @return two bool values, the first indicating whether a relationship exists.
     *         the second indicates that r is the parent interface of l if the former is true.
     */
    std::pair<bool, bool> IsExtendInheritRelation(const AST::ExtendDecl& r, const AST::ExtendDecl& l);

    bool PairIsOverrideOrImpl(const AST::Decl& child, const AST::Decl& parent, AST::DataTy baseTy = {},
        ModalInfo baseMode = {}, AST::DataTy parentTy = {}, ModalInfo parentMode = {});

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
    AST::ModalTy ConstrainByCtor(AST::GenericsTy& tv, AST::Ty& tyCtor);
    // Similar to ConstrainByCtor, but the upperbound will be added to sum instead of ubs.
    // Also, in case some of the constructors are generic, the ty args between upperbounds
    // can be shared, in order to sync the constraints (in a best-effort manner).
    AST::DataTy AddSumByCtor(AST::GenericsTy& tv, AST::Ty& tyCtor, std::vector<Ptr<AST::GenericsTy>>& tyArgs);
    bool OfSameCtor(AST::DataTy ty, AST::DataTy tyCtor);
    // currently only for collecting map from member to decls
    Ptr<AST::Decl> GetDummyBuiltInDecl(AST::DataTy ty);
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
    AST::ModalTy TryGreedySubst(AST::ModalTy ty);
    // constraints for placeholder type vars
    Constraint constraints;

    /// recursively replace This in type args
    AST::ModalTy ReplaceThisTy(AST::ModalTy now);
    /// get the class ty that This refers to if it is a This ty
    AST::ModalTy GetThisRealTy(AST::ModalTy now);

    /**
     * @brief Obtains the alias type of node if node's ty have alias type reference.
     * @param node the node which map contain alias type reference.
     * @return the TypeAliasTy or Ty contain TypeAliasTy.
     */
    AST::ModalTy ObtainsAliasType(Ptr<const AST::Node> node);

    /**
     * @brief Substitute type alias in ty recursively.
     * @param ty the type need to substitute type alias.
     * @param needSubstituteGeneric whether need to substitute generic type vars.
     * @param typeMapping the custom type mapping for substitution.
     * @return the substituted type.
     */
    AST::ModalTy SubstituteTypeAliasInTy(
        AST::ModalTy ty, bool needSubstituteGeneric = false, const TypeSubst& typeMapping = {});

private:
    friend class TyVarScope;
    friend class InstCtxScope;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    friend class TyGeneralizer;
#endif
    void ReleaseTyVar(Ptr<AST::GenericsTy> genTy);

public:
    /** The extend context maps. */
    std::unordered_map<AST::DataTy, std::set<Ptr<AST::ExtendDecl>>> builtinTyToExtendMap;
    std::unordered_map<Ptr<const AST::InheritableDecl>, std::set<Ptr<AST::ExtendDecl>>> declToExtendMap;

private:
    inline static AST::InvalidTy theInvalidTy = AST::InvalidTy();
    inline static AST::AnyTy theAnyTy = AST::AnyTy();
    static AST::QuestTy theQuestTy;
    static AST::CStringTy theCStringTy;
    static std::vector<AST::PrimitiveTy> primitiveTys;
    AST::InterfaceDecl* copyInterfaceDecl = nullptr;

    struct TypePointer {
        explicit TypePointer(AST::DataTy ptr) : ptr(ptr) {}
        ~TypePointer() = default;
        Ptr<const AST::Ty> operator->() const
        {
            return ptr;
        }
        AST::DataTy Get() const { return ptr; }
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
        AST::DataTy const ptr;
    };

    struct TypeHash {
        size_t operator()(const TypePointer& p) const
        {
            return p.HasValue() ? p->Hash() : 0;
        }
    };
    // These unordered sets are hash tables to save types.
    std::unordered_set<TypePointer, TypeHash> allocatedTys;
    std::unordered_set<std::pair<AST::DataTy, AST::DataTy>, HashPair> checkedTyExtendRelation;
    std::unordered_set<AST::DataTy> boxedTys;
    std::unordered_set<Ptr<AST::ExtendDecl>> boxUsedExtends;
    std::unordered_set<Ptr<AST::InheritableDecl>> boxedNonGenericDecls;
    /** Used generic extends for each instantiated type. */
    std::unordered_map<AST::DataTy, std::unordered_set<Ptr<AST::ExtendDecl>>> tyUsedExtends;
    std::unordered_map<AST::DataTy, std::set<Ptr<AST::ExtendDecl>>> instantiateBuiltInTyToExtendMap;
    /** TypeManager caches. */
    std::unordered_map<Ptr<const AST::Ty>, std::unordered_set<AST::DataTy>> tyExtendInterfaceTyMap;
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
                ret = hash_combine<AST::DataTy>(ret, n.first);
                ret = hash_combine<AST::DataTy>(ret, n.second);
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
    std::unordered_map<TypeInfo, std::unordered_set<AST::DataTy>, TypeInfoHash, TypeInfoEqual> tyToSuperTysMap;
    /** Store checked typeArgs instantiation result for generic decls. */
    std::unordered_map<Ptr<const AST::Decl>, std::map<std::vector<AST::DataTy>, bool>> declInstantiationStatus;
    AST::DataTy anyTy = &theAnyTy;
    AST::DataTy ctypeTy = nullptr;

    struct SubtypeCacheKey {
        AST::ModalTy leaf{nullptr};
        AST::ModalTy root{nullptr};
        bool implicitBoxed{false};
        bool allowOptionBox{false};
        ModalMatchMode mode;

        SubtypeCacheKey(AST::ModalTy leaf, AST::ModalTy root, bool implicitBoxed = true, bool allowOptionBox = true,
            ModalMatchMode m = ModalMatchMode::SUBTYPE)
            : leaf(leaf), root(root), implicitBoxed(implicitBoxed), allowOptionBox(allowOptionBox), mode{m}
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
        const AST::DataTy baseTy{nullptr};
        const AST::DataTy expectInstParent{nullptr};
        ModalInfo baseMode;
        ModalInfo parentMode;

        OverrideOrShadowKey(const AST::FuncDecl* s, const AST::FuncDecl* t, const AST::DataTy b, ModalInfo bm,
            const AST::DataTy e, ModalInfo pm)
            : src(s), target(t), baseTy(b), expectInstParent(e), baseMode{bm}, parentMode{pm}
        {
        }
    };
    struct OverrideOrShadowEqual {
        bool operator()(const OverrideOrShadowKey& lhs, const OverrideOrShadowKey& rhs) const
        {
            return std::tie(lhs.src, lhs.target, lhs.baseTy, lhs.baseMode, lhs.expectInstParent, lhs.parentMode) ==
                std::tie(rhs.src, rhs.target, rhs.baseTy, rhs.baseMode, rhs.expectInstParent, rhs.parentMode);
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
    /**
     * Cache that maps a function declaration to the list of function declarations it overrides.
     * For each key `f`, `overrideMap[f]` contains the parent/super function(s) overridden by `f`.
     * This is used to find the top overridden function(s) for a given function declaration.
     */
    std::unordered_map<Ptr<const AST::FuncDecl>, std::vector<Ptr<const AST::FuncDecl>>> overrideMap;

    // a counter for naming tyvars
    unsigned long long nextUniqId{0};
    // all tyvar resources
    std::unordered_set<TypePointer, TypeHash> tyVarPool;
    // dummy decls to be associated with tyvar
    std::vector<OwnedPtr<AST::GenericParamDecl>> dummyGenDecls;
    std::map<AST::DataTy, OwnedPtr<AST::Decl>> dummyBuiltInDecls;
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
    AST::DataTy GetExtendInterfaceSuperTy(AST::ClassTy& classTy, const AST::Ty& interfaceTy);

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

        inline AST::ModalTy Instantiate(AST::ModalTy ty) { return {Instantiate(ty.Ty()), ty.Mode()}; }
        inline AST::DataTy Instantiate(AST::DataTy ty) { return AST::Ty::IsTyCorrect(ty) ? Instantiate(*ty) : ty; }
        AST::DataTy Instantiate(AST::Ty& ty);
        AST::DataTy GetInstantiatedStructTy(AST::StructTy& structTy);
        AST::DataTy GetInstantiatedClassTy(AST::ClassTy& classTy);
        AST::DataTy GetInstantiatedInterfaceTy(AST::InterfaceTy& interfaceTy);
        AST::DataTy GetInstantiatedEnumTy(AST::EnumTy& enumTy);
        AST::DataTy GetInstantiatedArrayTy(AST::ArrayTy& arrayTy);
        AST::DataTy GetInstantiatedPointerTy(AST::PointerTy& cptrTy);
        // Get instantiated ty of set type 'IntersectionTy' and 'UnionTy'.
        template <typename SetTy> AST::DataTy GetInstantiatedSetTy(SetTy& ty);
        AST::DataTy GetInstantiatedGenericTy(AST::GenericsTy& ty);

        TypeManager& tyMgr;
        const TypeSubst& typeMapping;
    };

    void GetNominalSuperTy(
        const AST::Ty& nominalTy, const TypeSubst& typeMapping, std::unordered_set<AST::DataTy>& tyList);
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

    bool CheckGenericType(AST::ModalTy lvalue, AST::ModalTy rvalue, bool implicitBoxed = true);

    void GenerateExtendGenericMappingVisit(
        MultiTypeSubst& typeMapping, AST::Ty& baseType, std::unordered_set<AST::DataTy>& visited);
    void GenerateStructDeclGenericMappingVisit(MultiTypeSubst& m, const AST::InheritableDecl& decl,
        const AST::Ty& targetTy, std::unordered_set<AST::DataTy>& visited);
    void GenerateGenericMappingVisit(MultiTypeSubst& m, AST::Ty& baseType, std::unordered_set<AST::DataTy>& visited);

    /* Alternative version that generate SubstPack.
     * Should migrate to this version everywhere in the future. */
    void GenerateGenericMappingVisit(
        SubstPack& m, AST::Ty& baseType, std::unordered_set<AST::DataTy>& visited, bool contextual);
    void GenerateExtendGenericMappingVisit(
        SubstPack& typeMapping, AST::Ty& baseType, std::unordered_set<AST::DataTy>& visited, bool contextual);
    void GenerateStructDeclGenericMappingVisit(SubstPack& m, const AST::InheritableDecl& decl, const AST::Ty& targetTy,
        std::unordered_set<AST::DataTy>& visited, bool contextual);

    std::unordered_set<AST::DataTy> GetAllExtendInterfaceTyHelper(
        const std::set<Ptr<AST::ExtendDecl>>& extends, const std::vector<AST::DataTy>& typeArgs);
    bool HasExtendInterfaceTyHelper(
        AST::Ty& superTy, const std::set<Ptr<AST::ExtendDecl>>& extends, const std::vector<AST::DataTy>& typeArgs);

    AST::ModalTy SubstituteTypeArgs(AST::ModalTy baseTy, std::vector<AST::ModalTy>& typeArgs);
    AST::ModalTy SubstituteTypeArgs(AST::ModalTy baseTy, std::vector<AST::DataTy>& typeArgs);
    std::vector<AST::ModalTy> RecursiveSubstituteTypeAliasInTy(
        Ptr<const AST::Ty> ty, bool needSubstituteGeneric, const TypeSubst& typeMapping = {});
    AST::DataTy GetUnaliasedTypeFromTypeAlias(const AST::TypeAliasTy& target, const std::vector<AST::DataTy>& typeArgs,
        bool needSubstituteGeneric, const TypeSubst& customMapping);
    AST::ModalTy ObtainsAliasTypeOfRefType(Ptr<const AST::RefType> rt);
    AST::ModalTy ObtainsAliasTypeOfFuncDecl(Ptr<const AST::FuncDecl> fd);
};
} // namespace Cangjie

#endif // CANGJIE_SEMA_TYPE_MANAGER_H
