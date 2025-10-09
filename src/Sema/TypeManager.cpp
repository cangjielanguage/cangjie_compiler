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

#include "cangjie/Sema/TypeManager.h"

#include <string>
#include <utility>
#include <vector>

#include "TypeCheckUtil.h"
#include "LocalTypeArgumentSynthesis.h"

#include "ExtraScopes.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/ScopeManagerApi.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie {
using namespace AST;
using namespace TypeCheckUtil;

TypeManager::TypeManager()
{
    topScope = new TyVarScope(*this);
}

TypeManager::~TypeManager()
{
    delete topScope;
    Clear();
}

Ptr<PrimitiveTy> TypeManager::GetPrimitiveTy(TypeKind kind)
{
    bool validParam = static_cast<int32_t>(kind) >= static_cast<int32_t>(TYPE_PRIMITIVE_MIN) &&
        static_cast<int32_t>(kind) <= static_cast<int32_t>(TYPE_PRIMITIVE_MAX);
    CJC_ASSERT(validParam);
    size_t index = validParam ? static_cast<uint32_t>(kind) - static_cast<uint32_t>(TYPE_PRIMITIVE_MIN) : 0;
    CJC_ASSERT(index < primitiveTys.size());
    return &primitiveTys[index];
}

template <typename TypeT, typename... Args> TypeT* TypeManager::GetTypeTy(Args&&... args)
{
    TypeT tmpTy(std::forward<Args>(args)...);
    auto it = allocatedTys.find(TypePointer(&tmpTy));
    if (it == allocatedTys.end()) {
        auto ty = new TypeT(std::forward<Args>(args)...);
        allocatedTys.insert(TypePointer(ty));
        return ty;
    } else {
        return RawStaticCast<TypeT*>((*it).Get());
    }
}

Ptr<GenericsTy> TypeManager::GetGenericsTy(GenericParamDecl& gpd)
{
    auto ty = new GenericsTy(gpd.identifier, gpd);
    auto it = allocatedTys.find(TypePointer(ty));
    if (it != allocatedTys.end()) {
        delete ty;
        return RawStaticCast<GenericsTy*>((*it).Get());
    }
    allocatedTys.insert(TypePointer(ty));
    return ty;
}

Ptr<EnumTy> TypeManager::GetEnumTy(EnumDecl& ed, const std::vector<Ptr<Ty>>& typeArgs)
{
    return GetTypeTy<EnumTy>(ed.identifier, ed, typeArgs);
}

Ptr<RefEnumTy> TypeManager::GetRefEnumTy(EnumDecl& ed, const std::vector<Ptr<Ty>>& typeArgs)
{
    return GetTypeTy<RefEnumTy>(ed.identifier, ed, typeArgs);
}

Ptr<ClassTy> TypeManager::GetClassTy(ClassDecl& cd, const std::vector<Ptr<Ty>>& typeArgs)
{
    return GetTypeTy<ClassTy>(cd.identifier, cd, typeArgs);
}

Ptr<ClassThisTy> TypeManager::GetClassThisTy(ClassDecl& cd, const std::vector<Ptr<Ty>>& typeArgs)
{
    return GetTypeTy<ClassThisTy>(cd.identifier, cd, typeArgs);
}

Ptr<InterfaceTy> TypeManager::GetInterfaceTy(InterfaceDecl& id, const std::vector<Ptr<Ty>>& typeArgs)
{
    return GetTypeTy<InterfaceTy>(id.identifier, id, typeArgs);
}

Ptr<StructTy> TypeManager::GetStructTy(StructDecl& sd, const std::vector<Ptr<Ty>>& typeArgs)
{
    return GetTypeTy<StructTy>(sd.identifier, sd, typeArgs);
}

Ptr<TypeAliasTy> TypeManager::GetTypeAliasTy(TypeAliasDecl& tad, const std::vector<Ptr<Ty>>& typeArgs)
{
    return GetTypeTy<TypeAliasTy>(tad.identifier, tad, typeArgs);
}

Ptr<ArrayTy> TypeManager::GetArrayTy(Ptr<Ty> elemTy, unsigned int dims)
{
    Ptr<Ty> tmpElemTy = elemTy;
    unsigned int tmpDims = dims;
    if (Ty::IsTyCorrect(elemTy)) {
        if (elemTy->IsArray()) {
            auto arrayTy = RawStaticCast<ArrayTy*>(elemTy);
            tmpElemTy = arrayTy->typeArgs[0];
            tmpDims += arrayTy->dims;
        }
    } else {
        tmpElemTy = GetInvalidTy();
    }
    return GetTypeTy<ArrayTy>(tmpElemTy, tmpDims);
}

Ptr<VArrayTy> TypeManager::GetVArrayTy(Ty& elemTy, int64_t size)
{
    return GetTypeTy<VArrayTy>(&elemTy, size);
}

Ptr<PointerTy> TypeManager::GetPointerTy(Ptr<Ty> elemTy)
{
    Ptr<Ty> tmpElemTy = elemTy;
    if (!Ty::IsTyCorrect(elemTy)) {
        tmpElemTy = GetInvalidTy();
    }
    return GetTypeTy<PointerTy>(tmpElemTy);
}

Ptr<ArrayTy> TypeManager::GetArrayTy()
{
    // Use 'Array<Invalid>' to present unique array type for extend lookup.
    return GetArrayTy(GetInvalidTy(), 1);
}

Ptr<TupleTy> TypeManager::GetTupleTy(const std::vector<Ptr<Ty>>& typeArgs, bool isClosureTy)
{
    return GetTypeTy<TupleTy>(typeArgs, isClosureTy);
}

Ptr<FuncTy> TypeManager::GetFunctionTy(const std::vector<Ptr<Ty>>& paramTys, Ptr<Ty> retTy, FuncTy::Config cfg)
{
    return GetTypeTy<FuncTy>(paramTys, retTy, cfg);
}

Ptr<Ty> TypeManager::GetIntersectionTy(const std::set<Ptr<Ty>>& tys)
{
    return GetTypeTy<IntersectionTy>(tys);
}

Ptr<Ty> TypeManager::GetUnionTy(const std::set<Ptr<Ty>>& tys)
{
    return GetTypeTy<UnionTy>(tys);
}

Ptr<Ty> TypeManager::GetBlockRealTy(const Block& block) const
{
    if (block.desugarExpr) {
        return block.desugarExpr->ty;
    }
    if (block.body.empty()) {
        return GetPrimitiveTy(TypeKind::TYPE_UNIT);
    }
    Ptr<Node> lastNode = block.body[block.body.size() - 1].get();
    if (lastNode->IsDecl()) {
        return GetPrimitiveTy(TypeKind::TYPE_UNIT);
    }
    if (Is<Expr>(lastNode)) {
        auto expr = RawStaticCast<Expr*>(lastNode);
        return expr->desugarExpr.get() ? expr->desugarExpr->ty : expr->ty;
    }
    return block.ty;
}

Ptr<Ty> TypeManager::TyInstantiator::GetInstantiatedGenericTy(GenericsTy& ty)
{
    Ptr<Ty> curTy = &ty;
    auto mapping = typeMapping;
    TypeSubst::const_iterator it = mapping.find(&ty);
    // Direct mapping from genericsTy to genericsTy.
    while (it != mapping.cend()) {
        curTy = it->second;
        // Erase current substitution from mapping, avoid circular substitution.
        // Shouldn't be needed after entirely shifting to SubstPack
        mapping.erase(it->first);
        if (auto genTy = DynamicCast<GenericsTy*>(curTy)) {
            it = mapping.find(genTy);
        } else {
            break;
        }
    }
    // Instantiates from composite type which contains genericsTy.
    bool needInstantiate = !mapping.empty() && !curTy->IsGeneric() && curTy->HasGeneric();
    if (needInstantiate) {
        auto instantiator = TyInstantiator(tyMgr, mapping);
        curTy = instantiator.Instantiate(curTy);
    }
    return curTy;
}

Ptr<Ty> TypeManager::TyInstantiator::GetInstantiatedStructTy(StructTy& structTy)
{
    // If is a struct without generic parameter, no need do instantiation.
    if (!structTy.declPtr || !structTy.declPtr->generic) {
        return &structTy;
    }
    std::vector<Ptr<Ty>> typeArgs;
    // Build type arguments.
    for (auto& it : structTy.typeArgs) {
        typeArgs.push_back(Instantiate(it));
    }
    auto recTy = tyMgr.GetStructTy(*structTy.declPtr, typeArgs);
    return recTy;
}

Ptr<Ty> TypeManager::TyInstantiator::GetInstantiatedClassTy(ClassTy& classTy)
{
    if (!classTy.declPtr || !classTy.declPtr->generic) {
        return &classTy;
    }
    std::vector<Ptr<Ty>> typeArgs;
    for (auto& it : classTy.typeArgs) {
        typeArgs.push_back(Instantiate(it));
    }
    Ptr<ClassTy> insTy = nullptr;
    if (Is<ClassThisTy>(classTy)) {
        insTy = tyMgr.GetClassThisTy(*classTy.declPtr, typeArgs);
    } else {
        insTy = tyMgr.GetClassTy(*classTy.declPtr, typeArgs);
    }
    return insTy;
}

Ptr<Ty> TypeManager::TyInstantiator::GetInstantiatedInterfaceTy(InterfaceTy& interfaceTy)
{
    if (!interfaceTy.declPtr || !interfaceTy.declPtr->generic) {
        return &interfaceTy;
    }
    std::vector<Ptr<Ty>> typeArgs;
    for (auto& it : interfaceTy.typeArgs) {
        typeArgs.push_back(Instantiate(it));
    }
    auto insTy = tyMgr.GetInterfaceTy(*interfaceTy.declPtr, typeArgs);
    return insTy;
}

Ptr<Ty> TypeManager::TyInstantiator::GetInstantiatedEnumTy(EnumTy& enumTy)
{
    // If is an enum without generic parameter, no need to do instantiation.
    if (!enumTy.declPtr || !enumTy.declPtr->generic) {
        return &enumTy;
    }
    std::vector<Ptr<Ty>> typeArgs;
    // Build type arguments.
    for (auto& it : enumTy.typeArgs) {
        typeArgs.push_back(Instantiate(it));
    }
    if (Is<RefEnumTy>(enumTy)) {
        return tyMgr.GetRefEnumTy(*enumTy.declPtr, typeArgs);
    }
    auto tmp = tyMgr.GetEnumTy(*enumTy.declPtr, typeArgs);
    tmp->hasCorrespondRefEnumTy = enumTy.hasCorrespondRefEnumTy;
    return tmp;
}

Ptr<Ty> TypeManager::TyInstantiator::GetInstantiatedArrayTy(ArrayTy& arrayTy)
{
    if (arrayTy.typeArgs.empty()) {
        return &arrayTy;
    }
    auto elemTy = Instantiate(arrayTy.typeArgs[0]);
    auto dims = arrayTy.dims;
    return tyMgr.GetArrayTy(elemTy, dims);
}

Ptr<Ty> TypeManager::TyInstantiator::GetInstantiatedPointerTy(PointerTy& cptrTy)
{
    if (cptrTy.typeArgs.empty()) {
        return &cptrTy;
    }
    auto elemTy = Instantiate(cptrTy.typeArgs[0]);
    return tyMgr.GetPointerTy(elemTy);
}

template <typename SetTy> Ptr<Ty> TypeManager::TyInstantiator::GetInstantiatedSetTy(SetTy& ty)
{
    std::set<Ptr<Ty>> tys;
    for (auto it : ty.tys) {
        tys.emplace(Instantiate(it));
    }
    return tyMgr.GetTypeTy<SetTy>(tys);
}

Ptr<Ty> TypeManager::TyInstantiator::Instantiate(Ty& ty)
{
    if (typeMapping.empty()) {
        return &ty;
    }
    switch (ty.kind) {
        // If is generic type, should replace it with type mapping.
        case TypeKind::TYPE_GENERICS:
            return GetInstantiatedGenericTy(StaticCast<GenericsTy>(ty));
        case TypeKind::TYPE_FUNC: {
            std::vector<Ptr<Ty>> paramTys;
            auto& funcTy = static_cast<FuncTy&>(ty);
            for (auto& it : funcTy.paramTys) {
                paramTys.push_back(Instantiate(it));
            }
            auto retType = Instantiate(funcTy.retTy);
            Ptr<Ty> ret = tyMgr.GetFunctionTy(
                paramTys, retType, {funcTy.IsCFunc(), funcTy.isClosureTy, funcTy.hasVariableLenArg});
            return ret;
        }
        case TypeKind::TYPE_TUPLE: {
            std::vector<Ptr<Ty>> typeArgs;
            std::transform(ty.typeArgs.begin(), ty.typeArgs.end(), std::back_inserter(typeArgs),
                [this](auto it) { return Instantiate(it); });
            return tyMgr.GetTupleTy(typeArgs, static_cast<TupleTy&>(ty).isClosureTy);
        }
        case TypeKind::TYPE_ARRAY:
            return GetInstantiatedArrayTy(static_cast<ArrayTy&>(ty));
        case TypeKind::TYPE_POINTER:
            return GetInstantiatedPointerTy(static_cast<PointerTy&>(ty));
        case TypeKind::TYPE_STRUCT:
            return GetInstantiatedStructTy(static_cast<StructTy&>(ty));
        case TypeKind::TYPE_CLASS:
            return GetInstantiatedClassTy(static_cast<ClassTy&>(ty));
        case TypeKind::TYPE_INTERFACE:
            return GetInstantiatedInterfaceTy(static_cast<InterfaceTy&>(ty));
        case TypeKind::TYPE_ENUM:
            return GetInstantiatedEnumTy(static_cast<EnumTy&>(ty));
        case TypeKind::TYPE: {
            std::vector<Ptr<Ty>> typeArgs;
            for (auto& it : ty.typeArgs) {
                typeArgs.push_back(Instantiate(it));
            }
            return tyMgr.GetTypeAliasTy(*static_cast<TypeAliasTy&>(ty).declPtr, typeArgs);
        }
        case TypeKind::TYPE_INTERSECTION:
            return GetInstantiatedSetTy(static_cast<IntersectionTy&>(ty));
        case TypeKind::TYPE_UNION:
            return GetInstantiatedSetTy(static_cast<UnionTy&>(ty));
        default:;
    }
    return &ty;
}

// Temporarily for backward compatibility.
Ptr<Ty> TypeManager::GetBestInstantiatedTy(Ptr<Ty> ty, const MultiTypeSubst& mts)
{
    if (mts.empty()) {
        return ty;
    }
    if (!Ty::IsTyCorrect(ty)) {
        return TypeManager::GetInvalidTy();
    }
    auto instTys = GetInstantiatedTys(ty, mts);
    return instTys.empty() ? ty : *instTys.begin();
}

std::set<Ptr<Ty>> TypeManager::GetInstantiatedTys(Ptr<Ty> ty, const MultiTypeSubst& mts)
{
    if (mts.empty()) {
        return {ty};
    }
    std::set<TypeSubst> ms =
        ExpandMultiTypeSubst(ReduceMultiTypeSubst(*this, StaticToTyVars(GetAllGenericTys(ty)), mts), {ty});
    std::set<Ptr<Ty>> res;
    std::for_each(ms.cbegin(), ms.cend(), [this, &ty, &res](const TypeSubst& m) {
        if (auto instTy = GetInstantiatedTy(ty, m)) {
            res.insert(instTy);
        }
    });
    if (res.size() > 1) {
        res.erase(ty); // Ignore self ty.
    }
    return res;
}

Ptr<Ty> TypeManager::GetInstantiatedTy(Ptr<Ty> ty, const TypeSubst& typeMapping)
{
    auto instantiator = TyInstantiator(*this, typeMapping);
    return instantiator.Instantiate(ty);
}

std::set<Ptr<Ty>> TypeManager::ApplyTypeSubstForTys(const TypeSubst& subst, const std::set<Ptr<TyVar>>& tys)
{
    std::set<Ptr<Ty>> res;
    for (auto& i : tys) {
        res.insert(GetInstantiatedTy(i, subst));
    }
    return res;
}

void TypeManager::GenerateExtendGenericMappingVisit(
    MultiTypeSubst& typeMapping, AST::Ty& baseType, std::unordered_set<Ptr<AST::Ty>>& visited)
{
    if (baseType.IsInvalid()) {
        return;
    }
    std::set<Ptr<ExtendDecl>> extends = GetAllExtendsByTy(baseType);
    for (auto& extend : extends) {
        GenerateStructDeclGenericMappingVisit(typeMapping, *extend, baseType, visited);
    }
}

namespace {
bool IsInheritableType(Ptr<const Ty> ty)
{
    // Non-classlike type cannot be treated as inherited type.
    if (!Ty::IsTyCorrect(ty) || !ty->IsClassLike()) {
        return false;
    }
    auto inherit = Ty::GetDeclPtrOfTy<InheritableDecl>(ty);
    return inherit && !inherit->TestAttr(Attribute::IN_REFERENCE_CYCLE);
}
} // namespace

void TypeManager::PackMapping(SubstPack& maps, const MultiTypeSubst m)
{
    for (auto [tv, instTys] : m) {
        if (tv->isPlaceholder) {
            instTys.erase(tv);
            maps.inst[tv].merge(instTys);
        } else {
            if (maps.u2i.count(tv) == 0) {
                maps.u2i[tv] = AllocTyVar(tv->name);
            }
            if (!instTys.empty()) {
                auto instTv = StaticCast<TyVar*>(maps.u2i[tv]);
                maps.inst[instTv].merge(instTys);
            }
        }
    }
}

void TypeManager::PackMapping(SubstPack& maps, const TypeSubst m)
{
    for (auto [tv, instTy] : m) {
        if (tv->isPlaceholder) {
            if (tv != instTy) {
                maps.inst[tv].insert(instTy);
            }
        } else {
            if (maps.u2i.count(tv) == 0) {
                maps.u2i[tv] = AllocTyVar(tv->name);
            }
            auto instTv = StaticCast<TyVar*>(maps.u2i[tv]);
            maps.inst[instTv].insert(instTy);
        }
    }
}

void TypeManager::PackMapping(SubstPack& maps, GenericsTy& tv, Ty& instTy)
{
    if (tv.isPlaceholder) {
        if (&tv != &instTy) {
            maps.inst[&tv].emplace(&instTy);
        }
    } else {
        if (maps.u2i.count(&tv) == 0) {
            maps.u2i[&tv] = AllocTyVar(tv.name);
        }
        auto instTv = StaticCast<TyVar*>(maps.u2i[&tv]);
        maps.inst[instTv].emplace(&instTy);
    }
}

MultiTypeSubst TypeManager::ZipSubstPack(const SubstPack& mapping)
{
    MultiTypeSubst mts;
    for (auto& [tvu, tyi] : mapping.u2i) {
        auto tvi = StaticCast<TyVar*>(tyi);
        if (mapping.inst.count(tvi) > 0) {
            mts[tvu] = ApplySubstPackNonUniq(tvu, mapping);
        }
    }
    return mts;
}

void TypeManager::MakeInstTyVar(SubstPack& maps, const AST::Decl& d)
{
    if (d.generic) {
        for (auto& genParam : d.generic->typeParameters) {
            if (Ty::IsTyCorrect(genParam->ty)) {
                MakeInstTyVar(maps, *StaticCast<GenericsTy*>(genParam->ty));
            }
        }
    }
}

void TypeManager::MakeInstTyVar(SubstPack& maps, AST::GenericsTy& uTv)
{
    CJC_ASSERT(!uTv.isPlaceholder);
    if (maps.u2i.count(&uTv) == 0) {
        maps.u2i[&uTv] = AllocTyVar(uTv.name);
    }
}

void TypeManager::GenerateStructDeclGenericMapping(MultiTypeSubst& m, const InheritableDecl& decl, const Ty& targetTy)
{
    std::unordered_set<Ptr<Ty>> visited;
    GenerateStructDeclGenericMappingVisit(m, decl, targetTy, visited);
}

void TypeManager::GenerateStructDeclGenericMappingVisit(MultiTypeSubst& m, const AST::InheritableDecl& decl,
    const AST::Ty& targetTy, std::unordered_set<Ptr<AST::Ty>>& visited)
{
    if (targetTy.IsInvalid()) {
        return;
    }
    MergeTypeSubstToMultiTypeSubst(m, GenerateTypeMapping(decl, targetTy.typeArgs));
    for (auto& inheritedType : decl.inheritedTypes) {
        if (IsInheritableType(inheritedType->ty)) {
            GenerateGenericMappingVisit(m, *inheritedType->ty, visited);
        }
    }
}

void TypeManager::GenerateTypeMappingForUpperBounds(SubstPack& m, const MemberAccess& ma, Decl& target)
{
    auto found = ma.foundUpperBoundMap.find(&target);
    if (found == ma.foundUpperBoundMap.end()) {
        return;
    }
    for (auto upper : found->second) {
        if (upper) {
            GenerateGenericMapping(m, *upper);
        }
    }
}

void TypeManager::GenerateGenericMapping(SubstPack& m, Ty& baseType)
{
    std::unordered_set<Ptr<Ty>> visited;
    GenerateGenericMappingVisit(m, baseType, visited, true);
}

void TypeManager::GenerateGenericMappingVisit(
    SubstPack& m, AST::Ty& baseType, std::unordered_set<Ptr<AST::Ty>>& visited, bool contextual)
{
    if (baseType.IsInvalid() || visited.count(&baseType) > 0) {
        return;
    }
    // avoid repeatedly visiting same super-type when there are multiple paths to it
    visited.emplace(&baseType);
    GenerateExtendGenericMappingVisit(m, baseType, visited, contextual);
    if (auto inheritableDecl = Ty::GetDeclPtrOfTy<InheritableDecl>(&baseType)) {
        GenerateStructDeclGenericMappingVisit(m, *inheritableDecl, baseType, visited, contextual);
    }
}

void TypeManager::GenerateExtendGenericMappingVisit(
    SubstPack& typeMapping, AST::Ty& baseType, std::unordered_set<Ptr<AST::Ty>>& visited, bool contextual)
{
    if (baseType.IsInvalid()) {
        return;
    }
    std::set<Ptr<ExtendDecl>> extends = GetAllExtendsByTy(baseType);
    for (auto& extend : extends) {
        GenerateStructDeclGenericMappingVisit(typeMapping, *extend, baseType, visited, contextual);
    }
}

void TypeManager::GenerateStructDeclGenericMappingVisit(SubstPack& m, const AST::InheritableDecl& decl,
    const AST::Ty& targetTy, std::unordered_set<Ptr<AST::Ty>>& visited, bool contextual)
{
    if (targetTy.IsInvalid()) {
        return;
    }
    if (contextual) {
        GenerateTypeMapping(*this, m, decl, targetTy.typeArgs);
    } else {
        auto typeArgs = targetTy.typeArgs;
        for (auto& ty : typeArgs) {
            ty = GetInstantiatedTy(ty, m.u2i); // use placeholder ty var for mid-level targetTys
        }
        GenerateTypeMapping(*this, m, decl, typeArgs);
    }
    for (auto& inheritedType : decl.inheritedTypes) {
        if (IsInheritableType(inheritedType->ty)) {
            GenerateGenericMappingVisit(m, *inheritedType->ty, visited, false);
        }
    }
}

void TypeManager::GenerateTypeMappingForUpperBounds(MultiTypeSubst& m, const MemberAccess& ma, Decl& target)
{
    auto found = ma.foundUpperBoundMap.find(&target);
    if (found == ma.foundUpperBoundMap.end()) {
        return;
    }
    for (auto upper : found->second) {
        if (upper) {
            GenerateGenericMapping(m, *upper);
        }
    }
}

void TypeManager::GenerateGenericMapping(MultiTypeSubst& m, Ty& baseType)
{
    std::unordered_set<Ptr<Ty>> visited;
    GenerateGenericMappingVisit(m, baseType, visited);
}

void TypeManager::GenerateGenericMappingVisit(
    MultiTypeSubst& m, AST::Ty& baseType, std::unordered_set<Ptr<AST::Ty>>& visited)
{
    if (baseType.IsInvalid() || visited.count(&baseType) > 0) {
        return;
    }
    // avoid repeatedly visiting same super-type when there are multiple paths to it
    visited.emplace(&baseType);
    GenerateExtendGenericMappingVisit(m, baseType, visited);
    if (auto inheritableDecl = Ty::GetDeclPtrOfTy<InheritableDecl>(&baseType)) {
        GenerateStructDeclGenericMappingVisit(m, *inheritableDecl, baseType, visited);
    }
}

TypeSubst TypeManager::GenerateGenericMappingFromGeneric(const Decl& parentDecl, const Decl& childDecl) const
{
    TypeSubst typeMapping;
    Ptr<Generic> parentGeneric = parentDecl.GetGeneric();
    Ptr<Generic> childGeneric = childDecl.GetGeneric();
    bool validGenerics = parentGeneric != nullptr && childGeneric != nullptr &&
        parentGeneric->typeParameters.size() == childGeneric->typeParameters.size();
    if (validGenerics) {
        for (size_t i = 0; i < parentGeneric->typeParameters.size(); ++i) {
            typeMapping[StaticCast<TyVar*>(parentGeneric->typeParameters[i]->ty)] = childGeneric->typeParameters[i]->ty;
        }
    }
    return typeMapping;
}

MultiTypeSubst TypeManager::GenerateStructDeclTypeMapping(const Decl& decl)
{
    if (!decl.IsNominalDecl()) {
        return {};
    }
    auto parentTy = decl.ty;
    if (decl.astKind == ASTKind::EXTEND_DECL) {
        auto& ed = static_cast<const ExtendDecl&>(decl);
        CJC_ASSERT(ed.extendedType);
        parentTy = ed.extendedType->ty;
    }
    if (!parentTy) {
        return {};
    }
    MultiTypeSubst typeMapping;
    GenerateGenericMapping(typeMapping, *parentTy);
    return typeMapping;
}

bool TypeManager::IsCoreFutureType(const Ty& ty)
{
    if (!ty.IsClass()) {
        return false;
    }
    auto declPtr = Ty::GetDeclPtrOfTy(&ty);
    return declPtr && declPtr->identifier == FUTURE_TYPE_NAME &&
        declPtr->curFile->curPackage->fullPackageName == CORE_PACKAGE_NAME && ty.typeArgs.size() == 1;
}

bool TypeManager::IsPlaceholderSubtype(Ty& leaf, Ty& root)
{
    if (leaf.IsPlaceholder() || root.IsPlaceholder()) {
        return LocalTypeArgumentSynthesis::Unify(*this, constraints, leaf, root);
    }
    return false;
}

bool TypeManager::IsGenericSubtype(Ty& leaf, Ty& root, bool implicitBoxed, bool allowOptionBox)
{
    if (leaf.IsGeneric()) {
        auto& gTy = static_cast<GenericsTy&>(leaf);
        if (root.IsAny() && !implicitBoxed) {
            // If implicit box is not allowed and the target type is 'Any',
            // only return true when there exists any class type upper found.
            return std::any_of(gTy.upperBounds.begin(), gTy.upperBounds.end(), [](auto it) { return it->IsClass(); });
        }
        // NOTE: transmitting upperBound of generic types are flattened in 'PreCheck' step.
        // If the constraint can be found in typeConstraintCollection, return true.
        if (gTy.upperBounds.find(&root) != gTy.upperBounds.end()) {
            return true;
        }
        if (!gTy.isUpperBoundLegal) {
            return false;
        }
        if (gTy.isAliasParam) {
            return true;
        }
        for (auto& upperbound : gTy.upperBounds) {
            if (IsSubtype(upperbound, &root, implicitBoxed, allowOptionBox)) {
                return true;
            }
        }
    }
    if (leaf.kind == TypeKind::TYPE_INTERSECTION) {
        auto& iSectTy = static_cast<IntersectionTy&>(leaf);
        for (auto& ty : iSectTy.tys) {
            if (IsSubtype(ty, &root, implicitBoxed, allowOptionBox)) {
                return true;
            }
        }
    }
    if (root.kind == TypeKind::TYPE_INTERSECTION) {
        auto& iSectTy = static_cast<IntersectionTy&>(root);
        auto success = true;
        for (auto& ty : iSectTy.tys) {
            success = success && IsSubtype(&leaf, ty, implicitBoxed, allowOptionBox);
        }
        return success;
    }
    if (leaf.kind == TypeKind::TYPE_UNION) {
        auto& unionTy = static_cast<UnionTy&>(leaf);
        auto success = true;
        for (auto& ty : unionTy.tys) {
            success = success && IsSubtype(ty, &root, implicitBoxed, allowOptionBox);
        }
        return success;
    }
    return false;
}

// Treating platform and common classes/struct/enums types as subtypes of each other
bool IsCommonAndPlatformRelation(const Ty& leafTy, const Ty& rootTy)
{
    if (!leafTy.IsNominal() || !rootTy.IsNominal()) {
        return false;
    }
    auto leafDecl = Ty::GetDeclOfTy(&leafTy);
    auto rootDecl = Ty::GetDeclOfTy(&rootTy);
    if (leafDecl && rootDecl) {
        leafDecl = leafDecl->platformImplementation == nullptr ? leafDecl : leafDecl->platformImplementation;
        rootDecl = rootDecl->platformImplementation == nullptr ? rootDecl : rootDecl->platformImplementation;
        if (!leafDecl->TestAttr(Attribute::PLATFORM) || !rootDecl->TestAttr(Attribute::PLATFORM)) {
            return false;
        }
        return leafDecl == rootDecl;
    }
    return false;
}

bool TypeManager::IsClassLikeSubtype(Ty& leaf, Ty& root, bool implicitBoxed, bool allowOptionBox)
{
    if (auto thisTyOfLeaf = DynamicCast<ClassThisTy*>(&leaf); thisTyOfLeaf && thisTyOfLeaf->declPtr) {
        auto leafClassTy = GetClassTy(*thisTyOfLeaf->declPtr, thisTyOfLeaf->typeArgs);
        if (auto thisTyOfRoot = DynamicCast<ClassThisTy*>(&root); thisTyOfRoot && thisTyOfRoot->declPtr) {
            // 'root' is class type, must not exist boxing relation.
            return IsSubtype(leafClassTy, GetClassTy(*thisTyOfRoot->declPtr, thisTyOfRoot->typeArgs));
        }
        return IsSubtype(leafClassTy, &root, implicitBoxed, allowOptionBox);
    }
    if (leaf.IsClassLike() && root.IsClassLike()) {
        // Types are class like, only may existing extend boxing relation.
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        auto superTys = GetAllSuperTys(leaf, {});
#endif
        auto cs = PData::CommitScope(constraints);
        for (auto ty : superTys) {
            if (ty && IsClassTyEqual(*ty, root)) {
                return true;
            }
            PData::Reset(constraints);
        }
    }
    return false;
}

bool TypeManager::IsPlaceholderEqual(Ty& leaf, Ty& root)
{
    if (leaf.IsPlaceholder() || root.IsPlaceholder()) {
        return IsTyEqual(&leaf, &root);
    }
    return false;
}

bool TypeManager::IsClassTyEqual(Ty& leaf, Ty& root)
{
    if (&leaf == &root || IsCommonAndPlatformRelation(leaf, root)) {
        return true;
    }
    if (auto ctt = DynamicCast<ClassThisTy*>(&leaf); ctt) {
        return false;
    }
    if (auto ctt = DynamicCast<ClassThisTy*>(&root); ctt) {
        return false;
    }
    size_t index = 0;
    auto predFunc = [&index, &root, this](Ptr<Ty> ty) {
        if (root.typeArgs[index]->IsIntersection()) {
            if (IsSubtype(ty, root.typeArgs[index])) {
                // This is for partial generic type-alias case.
                root.typeArgs[index] = ty;
                ++index;
                return true;
            }
        } else {
            auto rootArg = root.typeArgs[index];
            if (rootArg == ty || IsPlaceholderEqual(*rootArg, *ty)) {
                ++index;
                return true;
            }
        }
        return false;
    };
    // Need add test case to verify this logic.
    return static_cast<ClassLikeTy&>(leaf).commonDecl->ty == static_cast<ClassLikeTy&>(root).commonDecl->ty &&
        (leaf.typeArgs.size() == root.typeArgs.size()) && !leaf.IsClass() &&
        std::all_of(leaf.typeArgs.begin(), leaf.typeArgs.end(), predFunc);
}

bool TypeManager::IsStructOrEnumSubtype(Ty& leaf, Ty& root, bool implicitBoxed, bool allowOptionBox)
{
    auto b1 = Is<RefEnumTy>(leaf) && Is<EnumTy>(root);
    auto b2 = Is<RefEnumTy>(root) && Is<EnumTy>(leaf);
    auto isEnumCompatible = [this, &implicitBoxed](const EnumTy& p, const EnumTy& q) {
        if (p.typeArgs.size() != q.typeArgs.size() || p.name != q.name || p.declPtr != q.declPtr) {
            return false;
        }
        bool flag = true;
        for (size_t idx = 0; idx < p.typeArgs.size(); ++idx) {
            if (CheckTypeCompatibility(p.typeArgs[idx], q.typeArgs[idx], implicitBoxed, p.typeArgs[idx]->IsGeneric()) ==
                TypeCompatibility::INCOMPATIBLE) {
                flag = false;
                break;
            }
        }
        return flag;
    };
    if ((b1 || b2) && isEnumCompatible(static_cast<EnumTy&>(leaf), static_cast<EnumTy&>(root))) {
        return true;
    }

    if (implicitBoxed) {
        if (leaf.IsStruct() && root.kind == TypeKind::TYPE_INTERFACE) {
            if (HasSuperTy(leaf, root, {}, implicitBoxed)) {
                return true;
            }
        }

        if (root.IsCoreOptionType() && allowOptionBox && CountOptionNestedLevel(leaf) < CountOptionNestedLevel(root)) {
            // Core's enum Option check, support for auto package Option.
            return IsSubtype(&leaf, root.typeArgs[0], implicitBoxed);
        }

        if (leaf.IsEnum() && root.kind == TypeKind::TYPE_INTERFACE) {
            if (HasSuperTy(leaf, root, {}, implicitBoxed)) {
                return true;
            }
        }
    }

    return false;
}

bool TypeManager::IsFuncSubtype(const Ty& leaf, const Ty& root)
{
    if (!leaf.IsFunc() || !root.IsFunc()) {
        return false;
    }
    auto& leafFuncType = static_cast<const FuncTy&>(leaf);
    auto& rootFuncType = static_cast<const FuncTy&>(root);
    if (IsFuncParametersSubtype(leafFuncType, rootFuncType)) {
        bool noCast = leafFuncType.noCast || rootFuncType.noCast;
        return IsSubtype(leafFuncType.retTy, rootFuncType.retTy, noCast);
    }
    return false;
}

bool TypeManager::IsFuncParametersSubtype(const FuncTy& leaf, const FuncTy& root)
{
    bool noCast = leaf.noCast || root.noCast;
    if (leaf.paramTys.size() == root.paramTys.size()) {
        bool result = true;
        for (size_t i = 0; i < leaf.paramTys.size(); i++) {
            result = result && IsSubtype(root.paramTys[i], leaf.paramTys[i], noCast);
            if (!result) {
                return false;
            }
        }
        result = result && leaf.isC == root.isC;
        result = result && leaf.hasVariableLenArg == root.hasVariableLenArg;
        return result;
    }
    return false;
}

bool TypeManager::IsTupleSubtype(const Ty& leaf, const Ty& root)
{
    if (leaf.IsTuple() && root.IsTuple() && leaf.typeArgs.size() == root.typeArgs.size()) {
        for (size_t i = 0; i < leaf.typeArgs.size(); i++) {
            if (!IsSubtype(leaf.typeArgs[i], root.typeArgs[i], false)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool TypeManager::IsArraySubtype(const Ty& leaf, const Ty& root)
{
    if (!leaf.IsArray() || !root.IsArray()) {
        return false;
    }
    auto& leafArrayType = static_cast<const ArrayTy&>(leaf);
    auto& rootArrayType = static_cast<const ArrayTy&>(root);
    if (leafArrayType.dims == rootArrayType.dims && !leafArrayType.typeArgs.empty() &&
        !rootArrayType.typeArgs.empty()) {
        auto leafArg = leafArrayType.typeArgs[0];
        auto rootArg = rootArrayType.typeArgs[0];
        return IsTyEqual(leafArg, rootArg);
    }
    return false;
}

bool TypeManager::IsVArraySubtype(const Ty& leaf, const Ty& root)
{
    if (!Is<VArrayTy>(leaf) || !Is<VArrayTy>(root)) {
        return false;
    }
    auto& leafVArrayType = static_cast<const VArrayTy&>(leaf);
    auto& rootVArrayType = static_cast<const VArrayTy&>(root);
    if (leafVArrayType.size == rootVArrayType.size && !leafVArrayType.typeArgs.empty() &&
        !rootVArrayType.typeArgs.empty()) {
        auto leafArg = leafVArrayType.typeArgs[0];
        auto rootArg = rootVArrayType.typeArgs[0];
        return IsTyEqual(leafArg, rootArg);
    }
    return false;
}

bool TypeManager::IsPointerSubtype(const Ty& leaf, const Ty& root)
{
    if (!leaf.IsPointer() || !root.IsPointer()) {
        return false;
    }
    auto& leafPtrTy = static_cast<const PointerTy&>(leaf);
    auto& rootPtrTy = static_cast<const PointerTy&>(root);
    if (leafPtrTy.typeArgs.empty() || rootPtrTy.typeArgs.empty()) {
        return false;
    }
    auto leafArg = leafPtrTy.typeArgs[0];
    auto rootArg = rootPtrTy.typeArgs[0];
    return IsTyEqual(leafArg, rootArg);
}

bool TypeManager::IsPrimitiveSubtype(const Ty& leaf, Ty& root)
{
    if (leaf.kind == TypeKind::TYPE_IDEAL_INT && root.IsInteger()) {
        return true;
    }
    if (leaf.kind == TypeKind::TYPE_IDEAL_FLOAT && root.IsFloating()) {
        return true;
    }
    if (leaf.kind == TypeKind::TYPE_IDEAL_INT) {
        if (IsSubtype(GetPrimitiveTy(TypeKind::TYPE_INT64), &root) ||
            IsSubtype(GetPrimitiveTy(TypeKind::TYPE_UINT64), &root) ||
            IsSubtype(GetPrimitiveTy(TypeKind::TYPE_INT32), &root) ||
            IsSubtype(GetPrimitiveTy(TypeKind::TYPE_UINT32), &root) ||
            IsSubtype(GetPrimitiveTy(TypeKind::TYPE_INT16), &root) ||
            IsSubtype(GetPrimitiveTy(TypeKind::TYPE_UINT16), &root) ||
            IsSubtype(GetPrimitiveTy(TypeKind::TYPE_INT8), &root) ||
            IsSubtype(GetPrimitiveTy(TypeKind::TYPE_UINT8), &root)) {
            return true;
        }
    }
    if (leaf.kind == TypeKind::TYPE_IDEAL_FLOAT) {
        if (IsSubtype(GetPrimitiveTy(TypeKind::TYPE_FLOAT64), &root) ||
            IsSubtype(GetPrimitiveTy(TypeKind::TYPE_FLOAT32), &root) ||
            IsSubtype(GetPrimitiveTy(TypeKind::TYPE_FLOAT16), &root)) {
            return true;
        }
    }
    // Since the 'PrimitiveTy' can be implicited copied, also check for equality of primitive types' kind.
    return leaf.IsPrimitive() && root.IsPrimitive() && leaf.kind == root.kind;
}

// Note: By default, @implicitBoxed is true. For function type and tuple type,
// covariant & contravariant are both not allowed for elements' value types (implementing interfaces)
// and class type (when implementing interfaces by extend). For this situation, implicitBoxed is false.
bool TypeManager::IsSubtype(Ptr<Ty> leaf, Ptr<Ty> root, bool implicitBoxed, bool allowOptionBox)
{
    if (!Ty::IsTyCorrect(leaf) || !Ty::IsTyCorrect(root)) {
        return false;
    }
    if (leaf->IsQuest() || root->IsQuest()) {
        return true;
    }
    // Return true if any of the following holds:
    // 1. types are exactly same
    // 2. the 'leaf' is the 'Nothing' type
    // 3. currently allowing implicit boxing and
    //        a) the 'root' type is the 'Any' type, OR
    //        b) the 'leaf' is one of cffi types and the 'root' is 'CType', OR
    // 4. currently disallowing implicit boxing but the 'leaf' is classLike type and the 'root' is the 'Any' type.
    // NOTE: all cffi types are not classLike type, so using conditions as below.
    bool ffiFastCheck = (Ty::IsMetCType(*leaf) && root->IsCType());
    bool fastCheck = ffiFastCheck || leaf == root || (leaf->IsNothing() && !root->IsPlaceholder()) ||
        ((implicitBoxed || leaf->IsClassLike()) && root->IsAny() && !leaf->IsPlaceholder());
    if (fastCheck) {
        return true;
    }
    if (root->IsNothing()) {
        return false;
    }
    // Note: this cache is NOT for speedup, but to avoid recursive judgement on same types
    SubtypeCacheKey cacheKey(leaf, root, implicitBoxed, allowOptionBox);
    auto cacheResult = subtypeCache.find(cacheKey);
    if (cacheResult != subtypeCache.cend()) {
        return cacheResult->second;
    }
    auto cacheEntry = subtypeCache.emplace(std::make_pair(cacheKey, false)).first;
    if (IsCommonAndPlatformRelation(*leaf, *root)) {
        cacheEntry->second = true;
    } else if (IsPlaceholderSubtype(*leaf, *root)) {
        cacheEntry->second = true;
    } else if (IsGenericSubtype(*leaf, *root, implicitBoxed, allowOptionBox)) {
        cacheEntry->second = true;
    } else if (IsClassLikeSubtype(*leaf, *root, implicitBoxed, allowOptionBox)) {
        cacheEntry->second = true;
    } else if (IsPointerSubtype(*leaf, *root)) {
        cacheEntry->second = true;
    } else if (IsStructOrEnumSubtype(*leaf, *root, implicitBoxed, allowOptionBox)) {
        cacheEntry->second = true;
    } else if (IsFuncSubtype(*leaf, *root)) {
        cacheEntry->second = true;
    } else if (IsTupleSubtype(*leaf, *root)) {
        cacheEntry->second = true;
    } else if (IsArraySubtype(*leaf, *root)) {
        cacheEntry->second = true;
    } else if (IsVArraySubtype(*leaf, *root)) {
        cacheEntry->second = true;
    } else if (IsPrimitiveSubtype(*leaf, *root)) {
        cacheEntry->second = true;
    } else if (implicitBoxed && !leaf->IsGeneric() && root->IsInterface()) {
        // The 'GenericTy' will never have extends.
        // Process extends.
        auto extendTys = GetAllExtendInterfaceTy(*leaf);
        if (std::find(extendTys.begin(), extendTys.end(), root) != extendTys.end()) {
            cacheEntry->second = true;
        } else if ((leaf->HasPlaceholder() || root->HasPlaceholder()) &&
            LocalTypeArgumentSynthesis::Unify(*this, constraints, *leaf, *root)) {
            cacheEntry->second = true;
        }
    } else {
        cacheEntry->second = false;
    }
    bool ret = cacheEntry->second;
    // result for placeholder depends on global state, therefore shouldn't be cached beyond one judgement
    if (leaf->HasPlaceholder() || root->HasPlaceholder()) {
        subtypeCache.erase(cacheKey);
    }
    return ret;
}

bool TypeManager::IsTyEqual(Ptr<Ty> subTy, Ptr<Ty> baseTy)
{
    PData::CommitScope cs(constraints);
    if (IsSubtype(subTy, baseTy, false, false) && IsSubtype(baseTy, subTy, false, false)) {
        return true;
    }
    PData::Reset(constraints);
    return false;
}

bool TypeManager::IsLitBoxableType(Ptr<Ty> leaf, Ptr<Ty> root)
{
    if (!Ty::IsTyCorrect(leaf) || !Ty::IsTyCorrect(root)) {
        return false;
    }
    // LitConst leaf type will be int64 for integer and float 64 for float.
    // Automatically Adapt to all numeric literal for Option<T> here.
    if (leaf == root || (root->IsInteger() && leaf->IsInteger()) || (root->IsFloating() && leaf->IsFloating())) {
        return true;
    }

    // Check whether a type with a literal value (leaf) is a subtype of another type (root).
    // e.g. For string literals, whether String is a subtype of an interface that String implements or extends.
    if (IsSubtype(leaf, root)) {
        return true;
    }

    if (root->IsCoreOptionType()) {
        // Core's enum Option check for litconst types.
        return IsLitBoxableType(leaf, root->typeArgs[0]);
    }
    return false;
}

bool TypeManager::IsFuncParameterTypesIdentical(const FuncTy& t1, const FuncTy& t2)
{
    bool result{false};
    if (Ty::IsTyCorrect(&t1) && Ty::IsTyCorrect(&t2) && t1.paramTys.size() == t2.paramTys.size()) {
        result = true;
        for (size_t i = 0; i < t2.paramTys.size(); i++) {
            result =
                result && CheckTypeCompatibility(t1.paramTys[i], t2.paramTys[i], false) == TypeCompatibility::IDENTICAL;
        }
    }
    return result;
}

bool TypeManager::IsFuncParameterTypesIdentical(
    const std::vector<Ptr<Ty>>& paramTys1, const std::vector<Ptr<Ty>>& paramTys2, const TypeSubst& typeMapping)
{
    bool result{false};
    if (paramTys1.size() == paramTys2.size()) {
        result = true;
        for (size_t i = 0; i < paramTys2.size(); i++) {
            auto paramTy1 = GetInstantiatedTy(paramTys1[i], typeMapping);
            result = result && CheckTypeCompatibility(paramTy1, paramTys2[i], false) == TypeCompatibility::IDENTICAL;
        }
    }
    return result;
}

bool TypeManager::CheckGenericType(Ptr<Ty> lvalue, Ptr<Ty> rvalue, bool implicitBoxed)
{
    if (Ty::AreTysCorrect(std::set{lvalue, rvalue}) && lvalue->kind == TypeKind::TYPE_GENERICS &&
        rvalue->kind == TypeKind::TYPE_GENERICS) {
        auto lg = RawStaticCast<GenericsTy*>(lvalue);
        auto rg = RawStaticCast<GenericsTy*>(rvalue);
        if (lg->isPlaceholder || rg->isPlaceholder) {
            return false;
        }
        if (lg->upperBounds.empty() && rg->upperBounds.empty()) {
            return true;
        }
        if (lg->upperBounds.size() != rg->upperBounds.size()) {
            return false;
        }
        auto itl = lg->upperBounds.begin();
        auto itr = rg->upperBounds.begin();
        bool result = true;
        for (; itl != lg->upperBounds.end(); ++itl, ++itr) {
            result = result && CheckTypeCompatibility(*itl, *itr, implicitBoxed) == TypeCompatibility::IDENTICAL;
        }
        return result;
    }
    return IsSubtype(lvalue, rvalue, implicitBoxed, false);
}

// Check type compatibility between two types.
// For type A and type B,
// return TypeCompatibility::IDENTICAL if A and B are identical.
// return TypeCompatibility::SUBTYPE if A is subtype of B.
// return TypeCompatibility::INCOMPATIBLE for other situations.
// Note: By default, @implicitBoxed is true. For function type and tuple type,
// covariance is not allowed for value types (implementing interfaces)
// and class type (when implementing interfaces by extend). For this situation, implicitBoxed is false.
TypeCompatibility TypeManager::CheckTypeCompatibility(
    Ptr<Ty> lvalue, Ptr<Ty> rvalue, bool implicitBoxed, bool isGeneric)
{
    if (!Ty::AreTysCorrect(std::set{lvalue, rvalue})) {
        return TypeCompatibility::INCOMPATIBLE;
    }
    if (lvalue == rvalue || IsCommonAndPlatformRelation(*lvalue, *rvalue)) {
        return TypeCompatibility::IDENTICAL;
    }
    if (IsSubtype(lvalue, rvalue, implicitBoxed)) {
        return TypeCompatibility::SUBTYPE;
    }
    if (isGeneric && CheckGenericType(lvalue, rvalue, implicitBoxed)) {
        return TypeCompatibility::IDENTICAL;
    }
    auto isEnumCompatible = [this, &implicitBoxed](EnumTy& p, EnumTy& q) {
        if (p.typeArgs.size() != q.typeArgs.size() || p.name != q.name || p.declPtr != q.declPtr) {
            return false;
        }
        bool flag = true;
        for (size_t idx = 0; idx < p.typeArgs.size(); ++idx) {
            if (CheckTypeCompatibility(p.typeArgs[idx], q.typeArgs[idx], implicitBoxed, p.typeArgs[idx]->IsGeneric()) !=
                TypeCompatibility::IDENTICAL) {
                flag = false;
                break;
            }
        }
        return flag;
    };
    return lvalue->IsEnum() && rvalue->IsEnum() &&
            isEnumCompatible(*RawStaticCast<EnumTy*>(lvalue), *RawStaticCast<EnumTy*>(rvalue))
        ? TypeCompatibility::IDENTICAL
        : TypeCompatibility::INCOMPATIBLE;
}

void TypeManager::ReplaceIdealTy(Ptr<Ty>* ty)
{
    if (!ty || !Ty::IsTyCorrect(*ty)) {
        return;
    }
    switch ((*ty)->kind) {
        case TypeKind::TYPE_IDEAL_INT:
            *ty = GetPrimitiveTy(TypeKind::TYPE_INT64);
            break;
        case TypeKind::TYPE_IDEAL_FLOAT:
            *ty = GetPrimitiveTy(TypeKind::TYPE_FLOAT64);
            break;
        case TypeKind::TYPE_CLASS:
        case TypeKind::TYPE_INTERFACE:
        case TypeKind::TYPE_ENUM:
        case TypeKind::TYPE_ARRAY:
        case TypeKind::TYPE_POINTER:
        case TypeKind::TYPE_TUPLE:
            for (auto& typeArg : (*ty)->typeArgs) {
                ReplaceIdealTy(&typeArg);
            }
            break;
        default:
            break;
    }
}

/* Check whether the interface is extended by a class type.
 * */
bool TypeManager::IsTyExtendInterface(const Ty& classTy, const Ty& interfaceTy)
{
    auto decl = Ty::GetDeclPtrOfTy<InheritableDecl>(&classTy);
    if (decl == nullptr) {
        return false;
    }
    auto extends = GetDeclExtends(*decl);
    auto typeArgs = GetTypeArgs(classTy);
    auto ret = GetAllExtendInterfaceTyHelper(extends, typeArgs);
    for (auto iTy : ret) {
        if (&interfaceTy == iTy) {
            return true;
        }
    }
    return false;
}

bool TypeManager::HasExtendInterfaceTyHelper(Ty& superTy, const std::set<Ptr<ExtendDecl>>& extends,
    const std::vector<Ptr<Ty>>& typeArgs)
{
    PData::CommitScope cs(constraints);
    for (auto& extend : extends) {
        if (!CheckGenericDeclInstantiation(extend, typeArgs)) {
            PData::Reset(constraints);
            continue;
        }
        bool extendTyNotMatch = (!extend->extendedType || !extend->extendedType->ty ||
            extend->extendedType->ty->typeArgs.size() != typeArgs.size());
        if (extendTyNotMatch) {
            PData::Reset(constraints);
            continue;
        }
        TypeSubst typeMapping;
        for (size_t i = 0; i < typeArgs.size(); ++i) {
            // may be used in generic instantiation
            if (auto genSuper = DynamicCast<TyVar*>(extend->extendedType->ty->typeArgs[i])) {
                typeMapping[genSuper] = typeArgs[i];
            }
        }
        for (auto& superInterfaceTy : extend->inheritedTypes) {
            if (!IsInheritableType(superInterfaceTy->ty)) {
                continue;
            }
            Ptr<Ty> instTy = GetInstantiatedTy(superInterfaceTy->ty, typeMapping);
            // search super interfaceTy recursively.
            if (HasSuperTy(*instTy, superTy, typeMapping)) {
                return true;
            }
        }
        PData::Reset(constraints);
    }
    return false;
}

std::unordered_set<Ptr<Ty>> TypeManager::GetAllExtendInterfaceTyHelper(
    const std::set<Ptr<ExtendDecl>>& extends, const std::vector<Ptr<Ty>>& typeArgs)
{
    PData::CommitScope cs(constraints);
    std::unordered_set<Ptr<Ty>> ret;
    for (auto& extend : extends) {
        if (!CheckGenericDeclInstantiation(extend, typeArgs)) {
            PData::Reset(constraints);
            continue;
        }
        bool extendTyNotMatch = (!extend->extendedType || !extend->extendedType->ty ||
            extend->extendedType->ty->typeArgs.size() != typeArgs.size());
        if (extendTyNotMatch) {
            PData::Reset(constraints);
            continue;
        }
        TypeSubst typeMapping;
        for (size_t i = 0; i < typeArgs.size(); ++i) {
            // may be used in generic instantiation
            if (auto genSuper = DynamicCast<TyVar*>(extend->extendedType->ty->typeArgs[i])) {
                typeMapping[genSuper] = typeArgs[i];
            }
        }
        for (auto& superInterfaceTy : extend->inheritedTypes) {
            if (!IsInheritableType(superInterfaceTy->ty)) {
                continue;
            }
            Ptr<Ty> instTy = GetInstantiatedTy(superInterfaceTy->ty, typeMapping);
            // Get super interfaceTy recursively.
            auto superInterfaceTys = GetAllSuperTys(*instTy);
            for (auto i : superInterfaceTys) {
                ret.insert(i);
            }
        }
    }
    return ret;
}

bool TypeManager::HasExtendedInterfaceTy(Ty& ty, Ty& superTy, const TypeSubst& typeMapping)
{
    if (!Ty::IsTyCorrect(&ty) || !Ty::IsTyCorrect(&superTy)) {
        return false;
    }
    std::set<Ptr<ExtendDecl>> extends = GetAllExtendsByTy(ty);
    if (HasExtendInterfaceTyHelper(superTy, extends, GetTypeArgs(ty))) {
        return true;
    }

    if (ty.IsClass()) {
        auto declPtr = Ty::GetDeclPtrOfTy<InheritableDecl>(&ty);
        CJC_ASSERT(declPtr);
        TypeSubst substituteMapping = GetSubstituteMapping(ty, typeMapping);
        // Get super classTy & interfaceTy.
        for (auto& inheritedType : declPtr->inheritedTypes) {
            CJC_ASSERT(inheritedType);
            // Get inherited types recursively. BUT do not collect inherited type which has cyclic inheritance.
            if (inheritedType->ty->IsClass() && IsInheritableType(inheritedType->ty)) {
                return HasExtendedInterfaceTy(*inheritedType->ty, superTy, substituteMapping);
            }
        }
    }
    return false;
}

std::unordered_set<Ptr<Ty>> TypeManager::GetAllExtendInterfaceTy(Ty& ty)
{
    if (!Ty::IsTyCorrect(&ty)) {
        return {};
    }
    if (auto found = tyExtendInterfaceTyMap.find(&ty); found != tyExtendInterfaceTyMap.end()) {
        return found->second;
    }
    std::set<Ptr<ExtendDecl>> extends = GetAllExtendsByTy(ty);
    auto ret = GetAllExtendInterfaceTyHelper(extends, GetTypeArgs(ty));

    if (ty.IsClass()) {
        Ptr<Decl> decl = Ty::GetDeclPtrOfTy(&ty);
        auto* cd = RawStaticCast<ClassDecl*>(decl);
        CJC_ASSERT(cd);
        std::unordered_set<Ptr<Ty>> superTys;
        GetNominalSuperTy(ty, GenerateTypeMapping(*cd, ty.typeArgs), superTys);
        for (auto superTy : superTys) {
            auto cTy = DynamicCast<ClassTy*>(superTy);
            if (!cTy) {
                continue;
            }
            std::set<Ptr<ExtendDecl>> superExtends = GetDeclExtends(*cTy->declPtr);
            auto extendInterfaces = GetAllExtendInterfaceTyHelper(superExtends, superTy->typeArgs);
            for (auto i : extendInterfaces) {
                ret.insert(i);
            }
        }
    }
    if (!ty.HasPlaceholder()) {
        (void)tyExtendInterfaceTyMap.emplace(&ty, ret);
    }
    return ret;
}

void TypeManager::GetNominalSuperTy(
    const Ty& nominalTy, const TypeSubst& typeMapping, std::unordered_set<Ptr<Ty>>& tyList)
{
    auto declPtr = Ty::GetDeclPtrOfTy<InheritableDecl>(&nominalTy);
    if (!declPtr) {
        return;
    }
    TypeSubst substituteMapping = GetSubstituteMapping(nominalTy, typeMapping);
    // Get super classTy & interfaceTy.
    for (auto& inheritedType : declPtr->inheritedTypes) {
        CJC_ASSERT(inheritedType);
        // Get inherited types recursively. BUT do not collect inherited type which has cyclic inheritance.
        if (IsInheritableType(inheritedType->ty)) {
            auto inheritedTys = GetAllSuperTys(*inheritedType->ty, substituteMapping);
            tyList.merge(inheritedTys);
        }
    }
}

bool TypeManager::HasNominalSuperTy(Ty& nominalTy, Ty& superTy, const TypeSubst& typeMapping)
{
    auto declPtr = Ty::GetDeclPtrOfTy<InheritableDecl>(&nominalTy);
    if (!declPtr) {
        return false;
    }
    TypeSubst substituteMapping = GetSubstituteMapping(nominalTy, typeMapping);
    // Get super classTy & interfaceTy.
    for (auto& inheritedType : declPtr->inheritedTypes) {
        CJC_ASSERT(inheritedType);
        // search inherited types recursively. BUT do not search inherited type which has cyclic inheritance.
        if (IsInheritableType(inheritedType->ty) && HasSuperTy(*inheritedType->ty, superTy, substituteMapping)) {
            return true;
        }
    }
    return false;
}

namespace {
// collect all transitive GenericsTy upperbound in addition to gty.upperBounds
std::unordered_set<Ptr<AST::Ty>> GetAllUpperBounds(const GenericsTy& gty)
{
    std::unordered_set<Ptr<AST::Ty>> ubs(gty.upperBounds.begin(), gty.upperBounds.end());
    std::unordered_set<Ptr<AST::Ty>> newGens;
    std::unordered_set<Ptr<AST::Ty>> newUbs;
    for (auto ty : ubs) {
        if (ty->IsGeneric()) {
            newGens.insert(ty);
        }
    }
    while (!newGens.empty()) {
        for (auto ty : newGens) {
            for (auto ub : RawStaticCast<GenericsTy*>(ty)->upperBounds) {
                if (ub->IsGeneric()) {
                    newUbs.insert(ub);
                }
            }
        }
        ubs.insert(newUbs.begin(), newUbs.end());
        newGens = newUbs;
        newUbs.clear();
    }
    return ubs;
}
} // namespace

bool TypeManager::HasSuperTy(Ty& ty, Ty& superTy, const TypeSubst& typeMapping, bool withExtended)
{
    if (!Ty::IsTyCorrect(&ty) || !Ty::IsTyCorrect(&superTy)) {
        return false;
    }
    auto maybeInstTy = typeMapping.empty() ? Ptr(&ty) : GetInstantiatedTy(&ty, typeMapping);
    if (maybeInstTy == &superTy) {
        return true;
    }
    if (auto genTy = DynamicCast<GenericsTy>(maybeInstTy)) {
        if (GetAllUpperBounds(*genTy).count(&superTy) > 0) {
            return true;
        }
    } else if (auto itsTy = DynamicCast<IntersectionTy>(maybeInstTy)) {
        for (auto iTy : itsTy->tys) {
            if (HasSuperTy(*iTy, superTy, typeMapping, withExtended)) {
                return true;
            }
        }
    }
    if (ty.IsNominal() && HasNominalSuperTy(ty, superTy, typeMapping)) {
        return true;
    }
    if (withExtended && HasExtendedInterfaceTy(ty, superTy, typeMapping)) {
        return true;
    }
    return false;
}

std::unordered_set<Ptr<Ty>> TypeManager::GetAllSuperTys(Ty& ty, const TypeSubst& typeMapping, bool withExtended)
{
    std::unordered_set<Ptr<Ty>> tyList;
    if (!Ty::IsTyCorrect(&ty)) {
        return tyList;
    }
    TypeInfo key{&ty, typeMapping, withExtended};
    if (auto found = tyToSuperTysMap.find(key); found != tyToSuperTysMap.end()) {
        return found->second;
    }
    auto maybeInstTy = typeMapping.empty() ? Ptr(&ty) : GetInstantiatedTy(&ty, typeMapping);
    if (auto classLikeTy = DynamicCast<ClassLikeTy*>(maybeInstTy)) {
        tyList.emplace(classLikeTy);
    } else if (auto structTy = DynamicCast<StructTy*>(maybeInstTy)) {
        tyList.emplace(structTy);
    } else if (auto genTy = DynamicCast<GenericsTy>(maybeInstTy)) {
        tyList.merge(GetAllUpperBounds(*genTy));
        tyList.emplace(genTy);
    } else if (auto itsTy = DynamicCast<IntersectionTy>(maybeInstTy)) {
        for (auto iTy : itsTy->tys) {
            tyList.merge(GetAllSuperTys(*iTy, typeMapping, withExtended));
        }
        tyList.emplace(itsTy);
    }
    if (ty.IsNominal()) {
        GetNominalSuperTy(ty, typeMapping, tyList);
    }
    if (withExtended) {
        // Collect extend interface type.
        auto extendInterfaces = GetAllExtendInterfaceTy(ty);
        for (auto extendInterfaceTy : extendInterfaces) {
            tyList.insert(GetInstantiatedTy(extendInterfaceTy, typeMapping));
        }
    }
    if (!ty.HasPlaceholder()) {
        tyToSuperTysMap.emplace(std::move(key), tyList);
    }
    return tyList;
}

std::unordered_set<Ptr<AST::Ty>> TypeManager::GetAllCommonSuperTys(const std::unordered_set<Ptr<Ty>>& tys)
{
    std::unordered_set<Ptr<Ty>> supers = GetAllSuperTys(**tys.begin());
    std::unordered_set<Ptr<Ty>> supersNext;
    for (auto ty : tys) {
        std::unordered_set<Ptr<Ty>> supersCur;
        if (Ty::IsTyCorrect(ty)) {
            supersCur = GetAllSuperTys(*ty);
            supersCur.insert(ty);
        }
        for (auto tyn : supersCur) {
            if (supers.count(tyn) > 0) {
                supersNext.insert(tyn);
            }
        }
        supers = supersNext;
        supersNext.clear();
    }
    return supers;
}

TypeSubst TypeManager::GetSubstituteMapping(const Ty& nominalTy, const TypeSubst& typeMapping)
{
    auto declPtr = Ty::GetDeclPtrOfTy<InheritableDecl>(&nominalTy);
    if (!declPtr) {
        return {};
    }
    TypeSubst substituteMapping = GenerateTypeMapping(*declPtr, nominalTy.typeArgs);
    // Update 'substituteMapping' with input 'typeMapping'.
    for (auto& it : substituteMapping) {
        it.second = GetInstantiatedTy(it.second, typeMapping);
    }
    return substituteMapping;
}

std::vector<Ptr<InterfaceTy>> TypeManager::GetAllSuperInterfaceTysBFS(const InheritableDecl& decl)
{
    std::unordered_set<Ptr<InterfaceTy>> visitedTys;
    std::vector<Ptr<InterfaceTy>> allSuperTys;
    std::deque<std::pair<Ptr<InterfaceTy>, TypeSubst>> tempDeque{};
    for (auto& type : decl.inheritedTypes) {
        if (!type->ty->IsInterface()) {
            continue;
        }
        auto interfaceTy = RawStaticCast<InterfaceTy*>(type->ty);
        auto typeMapping = GetSubstituteMapping(*interfaceTy, {});
        tempDeque.emplace_back(interfaceTy, typeMapping);
    }
    while (!tempDeque.empty()) {
        auto temp = tempDeque.front();
        tempDeque.pop_front();
        if (visitedTys.count(temp.first) == 0) {
            allSuperTys.push_back(temp.first);
            visitedTys.insert(temp.first);
            for (auto& type : temp.first->declPtr->inheritedTypes) {
                if (!type->ty->IsInterface()) { // Type may be non-interface when user code is invalid.
                    continue;
                }
                auto interfaceTy = RawStaticCast<InterfaceTy*>(type->ty);
                auto typeMapping = GetSubstituteMapping(*interfaceTy, temp.second);
                tempDeque.emplace_back(
                    RawStaticCast<InterfaceTy*>(GetInstantiatedTy(interfaceTy, temp.second)), typeMapping);
            }
        }
    }
    return allSuperTys;
}

bool TypeManager::CheckExtendWithConstraint(const Ty& ty, Ptr<ExtendDecl> extend)
{
    std::vector<Ptr<Ty>> tys = GetTypeArgs(ty);
    return CheckGenericDeclInstantiation(extend, tys);
}

bool TypeManager::CheckGenericDeclInstantiation(Ptr<const Decl> d, const std::vector<Ptr<Ty>>& typeArgs)
{
    if (!d) {
        return false;
    }
    // If 'typeArgs' is empty, return check succeed.
    if (typeArgs.empty()) {
        return true;
    }
    auto genericParams = GetDeclTypeParams(*d);
    Ptr<Generic> genericDecl = d->GetGeneric();
    bool invalid = genericDecl &&
        std::any_of(genericDecl->genericConstraints.begin(), genericDecl->genericConstraints.end(),
            [](auto& gc) { return !gc || !gc->type; });
    if (invalid || genericParams.size() != typeArgs.size()) {
        return false;
    }
    for (size_t i = 0; i < typeArgs.size(); ++i) {
        if (!genericParams[i]->HasGeneric() && genericParams[i] != typeArgs[i]) {
            return false;
        }
    }
    if (!genericDecl) {
        CJC_ASSERT(d->astKind == ASTKind::EXTEND_DECL); // Extend of instantiated type.
        return true;
    }
    // Previous inspections are quicker than map finding when extend and typeArgs set have huge combinations.
    if (auto found = declInstantiationStatus.find(d); found != declInstantiationStatus.end()) {
        if (auto foundTys = found->second.find(typeArgs); foundTys != found->second.end()) {
            return foundTys->second;
        }
    }
    TypeSubst instantiateMap = GenerateTypeMapping(*d, typeArgs);
    if (d->astKind == ASTKind::EXTEND_DECL) {
        for (size_t i = 0; i < typeArgs.size(); ++i) {
            // NOTE: extend may be 'extend<T> A<B<T>>', we need to consider nested generics.
            auto ty = GetInstantiatedTy(genericParams[i], instantiateMap);
            if (ty != typeArgs[i]) {
                declInstantiationStatus[d].emplace(typeArgs, false);
                return false;
            }
        }
    }
    bool result = true;
    // Check generic constraints.
    for (auto& gc : genericDecl->genericConstraints) {
        auto typeTy = GetInstantiatedTy(gc->type->ty, instantiateMap);
        bool isNotGeneric = !typeTy || typeTy->kind != TypeKind::TYPE_GENERICS;
        for (const auto& upperBound : gc->upperBounds) {
            auto upperBoundTy = GetInstantiatedTy(upperBound->ty, instantiateMap);
            bool typeNotMatch = !IsSubtype(typeTy, upperBoundTy);
            if (isNotGeneric && typeNotMatch) {
                result = false;
                break;
            } else if (isNotGeneric) {
                continue;
            }
            auto gt = RawStaticCast<GenericsTy*>(typeTy);
            bool satisfyConstraint = std::any_of(gt->upperBounds.begin(), gt->upperBounds.end(),
                [this, &upperBoundTy](auto assumpUp) { return IsSubtype(assumpUp, upperBoundTy); });
            if (!satisfyConstraint && typeNotMatch) {
                result = false;
                break;
            }
        }
    }
    (void)declInstantiationStatus[d].emplace(typeArgs, result);
    return result;
}

void TypeManager::Clear()
{
    for (auto& i : allocatedTys) {
        delete i.Get();
    }
    allocatedTys.clear();
}

std::set<Ptr<ExtendDecl>> TypeManager::GetBuiltinTyExtends(Ty& ty)
{
    auto foundBuiltin = builtinTyToExtendMap.find(&ty);
    if (foundBuiltin != builtinTyToExtendMap.end()) {
        return foundBuiltin->second;
    }
    auto foundInstantiated = instantiateBuiltInTyToExtendMap.find(&ty);
    if (foundInstantiated != instantiateBuiltInTyToExtendMap.end()) {
        return foundInstantiated->second;
    }
    return {};
}

std::optional<bool> TypeManager::GetOverrideCache(
    const AST::FuncDecl* src, const AST::FuncDecl* target, Ty* baseTy, Ty* expectInstParent)
{
    OverrideOrShadowKey key(src, target, baseTy, expectInstParent);
    auto it = overrideOrShadowCache.find(key);
    if (it != overrideOrShadowCache.end()) {
        return it->second;
    }
    return {};
}

void TypeManager::AddOverrideCache(
    const AST::FuncDecl& src, const AST::FuncDecl& target, Ty* baseTy, Ty* expectInstParent, bool val)
{
    OverrideOrShadowKey key(&src, &target, baseTy, expectInstParent);
    overrideOrShadowCache.emplace(key, val);
    if (val && src.outerDecl && src.outerDecl == Ty::GetDeclPtrOfTy(baseTy)) {
        UpdateTopOverriddenFuncDeclCache(&src, &target);
    }
}

std::set<Ptr<ExtendDecl>> TypeManager::GetDeclExtends(const InheritableDecl& decl)
{
    auto found = declToExtendMap.find(&decl);
    if (found != declToExtendMap.end()) {
        return found->second;
    }
    return {};
}

// Find the original extend decl.
std::set<Ptr<ExtendDecl>> TypeManager::GetAllExtendsByTy(Ty& ty)
{
    auto decl = Ty::GetDeclPtrOfTy<InheritableDecl>(&ty);
    if (decl) {
        return GetDeclExtends(*decl);
    }
    auto builtInTy = GetTyForExtendMap(ty);
    if (builtInTy->IsIdeal()) {
        std::set<Ptr<ExtendDecl>> extends;
        auto kinds = GetIdealTypesByKind(builtInTy->kind);
        for (auto kind : kinds) {
            auto primitivety = GetPrimitiveTy(kind);
            auto found = builtinTyToExtendMap.find(primitivety);
            if (found != builtinTyToExtendMap.end()) {
                extends.insert(found->second.begin(), found->second.end());
            }
        }
        return extends;
    }
    auto found = builtinTyToExtendMap.find(builtInTy);
    return found != builtinTyToExtendMap.end() ? found->second : std::set<Ptr<ExtendDecl>>{};
}

std::unordered_set<Ptr<const InheritableDecl>> TypeManager::GetAllExtendedDecls()
{
    return Utils::GetKeys(declToExtendMap);
}

std::unordered_set<Ptr<AST::Ty>> TypeManager::GetAllExtendedBuiltIn()
{
    return Utils::GetKeys(builtinTyToExtendMap);
}

void TypeManager::RemoveExtendFromMap(ExtendDecl& ed)
{
    for (auto& declIt : declToExtendMap) {
        if (declIt.second.count(&ed) > 0) {
            declIt.second.erase(&ed);
            return;
        }
    }
    for (auto& it : instantiateBuiltInTyToExtendMap) {
        if (it.second.count(&ed) > 0) {
            it.second.erase(&ed);
            return;
        }
    }
}

void TypeManager::UpdateBuiltInTyExtendDecl(Ty& builtinTy, ExtendDecl& ed)
{
    instantiateBuiltInTyToExtendMap[&builtinTy].emplace(&ed);
}

void TypeManager::RecordUsedExtend(Ty& child, Ty& interfaceTy)
{
    auto pair = std::make_pair(&child, &interfaceTy);
    if (checkedTyExtendRelation.count(pair) > 0) {
        return;
    }
    checkedTyExtendRelation.insert(pair);
    auto extendedTy = GetRealExtendedTy(child, interfaceTy);
    (void)boxedTys.emplace(extendedTy);
    RecordUsedGenericExtend(*extendedTy);
    std::set<Ptr<ExtendDecl>> extends;
    auto decl = Ty::GetDeclOfTy<InheritableDecl>(extendedTy);
    if (decl) {
        // Collect non-generic decl.
        if (!decl->GetGeneric()) {
            boxedNonGenericDecls.emplace(decl);
        }
        extends = CollectAllRelatedExtends(*this, *decl);
    } else if (extendedTy->IsArray() || extendedTy->IsPointer()) {
        auto found = instantiateBuiltInTyToExtendMap.find(extendedTy);
        if (found == instantiateBuiltInTyToExtendMap.end()) {
            return;
        }
        extends = found->second;
    } else {
        auto found = builtinTyToExtendMap.find(extendedTy);
        if (found == builtinTyToExtendMap.end()) {
            return;
        }
        extends = found->second;
    }
    // For the generation of boxed decl, all member functions of related extends should be collected,
    // So we need to mark all related extends as used when boxing is happened.
    boxUsedExtends.insert(extends.begin(), extends.end());
}

void TypeManager::RecordUsedGenericExtend(Ty& boxedTy, Ptr<ExtendDecl> extend)
{
    // If given extend decl to struct, the 'extend' must be generic decl.
    // If only given boxedTy, the boxedTy must have typeArguments.
    bool ignored = (extend && (!extend->GetGeneric() || extend->TestAttr(Attribute::GENERIC_INSTANTIATED)));
    if (ignored) {
        return;
    }
    // Record given extend decl or all related extends.
    auto& tyExtends = tyUsedExtends[&boxedTy];
    if (extend) {
        tyExtends.emplace(extend);
    } else {
        auto decl = Ty::GetDeclPtrOfTy<InheritableDecl>(&boxedTy);
        auto extends = decl ? CollectAllRelatedExtends(*this, *decl) : GetAllExtendsByTy(boxedTy);
        tyExtends.insert(extends.begin(), extends.end());
    }
}

/**
 * When a class type does not extend the interface, but its super class does.
 * Get which super class extend the interface.
 */
Ptr<Ty> TypeManager::GetExtendInterfaceSuperTy(ClassTy& classTy, const Ty& interfaceTy)
{
    Ptr<Decl> decl = Ty::GetDeclPtrOfTy(&classTy);
    auto* cd = RawStaticCast<ClassDecl*>(decl);
    CJC_ASSERT(cd);

    auto sd = cd->GetSuperClassDecl();
    if (sd == nullptr) {
        return nullptr;
    }
    // TypeMapping from classTy to super classTy will only have exact one pattern.
    MultiTypeSubst mts;
    GenerateGenericMapping(mts, classTy);
    auto typeMapping = MultiTypeSubstToTypeSubst(mts);
    while (sd != nullptr) {
        auto instTy = GetInstantiatedTy(sd->ty, typeMapping);
        if (instTy && IsTyExtendInterface(*instTy, interfaceTy)) {
            return instTy;
        }
        sd = sd->GetSuperClassDecl();
    }

    return nullptr;
}

Ptr<Ty> TypeManager::GetRealExtendedTy(Ty& child, const Ty& interfaceTy)
{
    auto* extendedTy = &child;
    bool isNotDirectExtend = interfaceTy.IsInterface() && child.IsClass() && !IsTyExtendInterface(child, interfaceTy);
    if (isNotDirectExtend) {
        // If the class type ty does not extend the interface type iTy,
        // then we should find out which super class of the ty extend the interface.
        auto superTy = GetExtendInterfaceSuperTy(*RawStaticCast<ClassTy*>(&child), interfaceTy);
        if (Ty::IsTyCorrect(superTy)) {
            extendedTy = superTy;
        }
    }
    return extendedTy;
}

std::vector<Ptr<Ty>> TypeManager::GetTypeArgs(const Ty& ty)
{
    if (!ty.IsArray()) {
        return ty.typeArgs;
    }
    auto& arrayTy = static_cast<const ArrayTy&>(ty);
    if (arrayTy.dims == 1) {
        return arrayTy.typeArgs;
    }
    // Since Array<T> is a generic type, it's type argument should be array type dimension - 1 when dimension > 1.
    return {GetArrayTy(arrayTy.typeArgs[0], arrayTy.dims - 1)};
}

Ptr<Ty> TypeManager::GetNonNullTy(Ptr<Ty> ty)
{
    return Ty::IsInitialTy(ty) ? GetInvalidTy() : ty;
}

bool TypeManager::HasExtensionRelation(Ty& childTy, Ty& interfaceTy)
{
    if (!Ty::IsTyCorrect(&childTy) || !Ty::IsTyCorrect(&interfaceTy)) {
        return false;
    }
    // Interface type, nothing type and invalid type should not be boxed.
    bool validRelation = childTy.kind != TypeKind::TYPE_INTERFACE && childTy.kind != TypeKind::TYPE_NOTHING &&
        interfaceTy.kind == TypeKind::TYPE_INTERFACE;
    if (!validRelation) {
        return false;
    }
    if (interfaceTy.IsAny()) {
        return true;
    }
    if (childTy.kind != TypeKind::TYPE_CLASS) {
        // Enum/Struct can directly inherit interfaces, but also needs boxing when passing to interface type.
        auto inheritedTypes = GetAllSuperTys(childTy, {}, true);
        return inheritedTypes.count(&interfaceTy) != 0;
    }
    auto extendInterfaceList = GetAllExtendInterfaceTy(childTy);
    return extendInterfaceList.count(&interfaceTy) != 0;
}

Ptr<Ty> TypeManager::GetTyForExtendMap(Ty& ty)
{
    auto genericTy = Ty::GetGenericTyOfInsTy(ty);
    auto baseTy = &ty;
    if (ty.IsArray()) {
        baseTy = GetArrayTy();
    } else if (ty.IsPointer()) {
        baseTy = GetPointerTy(GetInvalidTy());
    } else if (genericTy != nullptr) {
        baseTy = genericTy;
    }
    return baseTy;
}

void TypeManager::RestoreJavaGenericsTy(AST::Decl& decl) const
{
    CJC_ASSERT(decl.TestAttr(Attribute::GENERIC_INSTANTIATED) && HasJavaAttr(decl));
    // When instantiated with erase mode, we make all same class's types pointing to unique
    // instantiated decl.
    for (auto& it : std::as_const(allocatedTys)) {
        auto ty = it.Get();
        if (Ty::GetDeclPtrOfTy(ty) == decl.genericDecl) {
            if (auto cty = DynamicCast<ClassTy*>(ty)) {
                cty->decl = StaticCast<ClassDecl*>(&decl);
                cty->commonDecl = StaticCast<ClassDecl*>(&decl);
            } else if (auto ity = DynamicCast<InterfaceTy*>(ty)) {
                ity->decl = StaticCast<InterfaceDecl*>(&decl);
                ity->commonDecl = StaticCast<InterfaceDecl*>(&decl);
            }
        }
    }
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
bool TypeManager::IsFuncDeclSubType(const AST::FuncDecl& decl, const AST::FuncDecl& funcDecl)
{
    auto declType = StaticCast<FuncTy*>(decl.ty);
    auto resolvedFuncType = DynamicCast<FuncTy*>(funcDecl.ty);
    if (resolvedFuncType && decl.identifier == funcDecl.identifier &&
        IsFuncParameterTypesIdentical(declType->paramTys, resolvedFuncType->paramTys) &&
        IsSubtype(declType->retTy, resolvedFuncType->retTy)) {
        return true;
    }
    return false;
}
#endif

bool TypeManager::IsFuncDeclEqualType(const AST::FuncDecl& decl, const AST::FuncDecl& funcDecl)
{
    return IsFuncDeclSubType(decl, funcDecl) && IsFuncDeclSubType(funcDecl, decl);
}

void TypeManager::UpdateTopOverriddenFuncDeclCache(const AST::Decl* src, const AST::Decl* target)
{
    if (auto funcDecl = DynamicCast<AST::FuncDecl>(src)) {
        auto& temp = overrideCache[funcDecl];
        temp.emplace_back(StaticCast<AST::FuncDecl*>(target));
    } else if (auto propDecl = DynamicCast<AST::PropDecl>(src)) {
        auto targetPropDecl = StaticCast<AST::PropDecl*>(target);
        if (!propDecl->getters.empty() && !targetPropDecl->getters.empty()) {
            auto& temp = overrideCache[propDecl->getters.front().get()];
            temp.emplace_back(targetPropDecl->getters.front().get());
        }
        if (!propDecl->setters.empty() && !targetPropDecl->setters.empty()) {
            auto& temp = overrideCache[propDecl->setters.front().get()];
            temp.emplace_back(targetPropDecl->setters.front().get());
        }
    }
}

Ptr<const AST::FuncDecl> TypeManager::GetTopOverriddenFuncDecl(const AST::FuncDecl* funcDecl) const
{
    const auto& decls = overrideCache.find(funcDecl);
    if (decls == overrideCache.end() || decls->second.empty()) {
        return nullptr;
    }
    Ptr<const AST::FuncDecl> ret = decls->second.front();
    while ((overrideCache.find(ret) != overrideCache.end()) && !(overrideCache.find(ret)->second.empty())) {
        ret = overrideCache.find(ret)->second.front();
    }
    return ret;
}

Ptr<Decl> TypeManager::GetOverrideDeclInClassLike(
    AST::Decl& baseDecl, const AST::FuncDecl& funcDecl, bool withAbstractOverrides)
{
    auto& decls = baseDecl.GetMemberDecls();

    for (auto& decl : decls) {
        if (decl->astKind != ASTKind::FUNC_DECL && decl->astKind != ASTKind::PROP_DECL) {
            continue;
        }
        if ((!withAbstractOverrides && decl->TestAttr(Attribute::ABSTRACT)) || decl->TestAttr(Attribute::GENERIC)) {
            continue;
        }
        if (Is<AST::FuncDecl*>(decl.get())) {
            auto declFunc = StaticCast<FuncDecl*>(decl.get());
            if (IsFuncDeclSubType(*declFunc, funcDecl)) {
                return declFunc;
            }
        } else if (auto propDecl = DynamicCast<PropDecl*>(decl.get()); propDecl) {
            for (auto& tempGetFunc : propDecl->getters) {
                auto declFunc = StaticCast<FuncDecl*>(tempGetFunc.get());
                if (IsFuncDeclSubType(*declFunc, funcDecl)) {
                    return declFunc;
                }
            }
            for (auto& tempSetFunc : propDecl->setters) {
                auto declFunc = StaticCast<FuncDecl*>(tempSetFunc.get());
                if (IsFuncDeclSubType(*declFunc, funcDecl)) {
                    return declFunc;
                }
            }
        }
    }

    return nullptr;
}

std::pair<bool, bool> TypeManager::IsExtendInheritRelation(const ExtendDecl& r, const ExtendDecl& l)
{
    bool hasInheritRelat = false;
    bool isRSuper = false;
    auto mapping = GenerateTypeMappingByTy(r.ty, l.ty);
    for (auto& lSuper : std::as_const(l.inheritedTypes)) {
        for (auto& rSuper : std::as_const(r.inheritedTypes)) {
            auto lTy = GetInstantiatedTy(lSuper->ty, mapping);
            auto rTy = GetInstantiatedTy(rSuper->ty, mapping);
            if (IsSubtype(lTy, rTy)) {
                isRSuper = true;
            }
            if (isRSuper || IsSubtype(rTy, lTy)) {
                hasInheritRelat = true;
                break;
            }
        }
    }
    return {hasInheritRelat, isRSuper};
}

Ptr<AST::GenericsTy> TypeManager::AllocTyVar(const std::string& srcId, bool needSolving, Ptr<TyVar> derivedFrom)
{
    Ptr<AST::GenericsTy> ret;
    // allocate
    if (tyVarPool.empty()) {
        auto dummyDecl = MakeOwned<GenericParamDecl>();
        auto newVar = GetGenericsTy(*dummyDecl);
        dummyGenDecls.emplace_back(std::move(dummyDecl));
        newVar->isPlaceholder = true;
        ret = newVar;
        // name
        nextUniqId++;
        std::string name = srcId + "-" + std::to_string(nextUniqId);
        ret->decl->identifier = name;
        ret->name = name;
    } else {
        auto tv = *tyVarPool.begin();
        tyVarPool.erase(tv);
        ret = RawStaticCast<GenericsTy*>(tv.Get());
    }
    // manage scope
    if (derivedFrom) {
        size_t lv = tyVarScopeDepth[derivedFrom];
        tyVarScopes[lv]->AddTyVar(ret);
        tyVarScopeDepth[ret] = lv;
    } else {
        tyVarScopes.back()->AddTyVar(ret);
        tyVarScopeDepth[ret] = tyVarScopes.size() - 1;
    }
    if (needSolving) {
        unsolvedTyVars.insert(ret);
        constraints[ret].sum = PSet({GetAnyTy()});
    }
    return ret;
}

void TypeManager::ReleaseTyVar(Ptr<AST::GenericsTy> genTy)
{
    CJC_ASSERT(tyVarPool.count(TypePointer(genTy)) == 0);
    genTy->upperBounds.clear();
    tyVarPool.emplace(genTy);
    tyVarScopeDepth.erase(genTy);
    unsolvedTyVars.erase(genTy);
    constraints.erase(genTy);
}

const std::set<Ptr<TyVar>>& TypeManager::GetUnsolvedTyVars()
{
    return unsolvedTyVars;
}

void TypeManager::MarkAsUnsolvedTyVar(GenericsTy& tv)
{
    CJC_ASSERT(tv.isPlaceholder);
    unsolvedTyVars.emplace(&tv);
    constraints[&tv].sum = PSet({GetAnyTy()});
}

std::set<Ptr<TyVar>> TypeManager::GetInnermostUnsolvedTyVars()
{
    std::set<Ptr<TyVar>> ret;
    for (auto tv : tyVarScopes.back()->tyVars) {
        if (unsolvedTyVars.count(tv) > 0) {
            ret.insert(tv);
        }
    }
    return ret;
}

size_t TypeManager::ScopeDepthOfTyVar(const GenericsTy& tyVar)
{
    CJC_ASSERT(tyVarScopeDepth.count(&tyVar) > 0);
    return tyVarScopeDepth.at(&tyVar);
}

Ptr<Ty> TypeManager::InstOf(const Ptr<Ty> ty)
{
    return ApplySubstPack(ty, instCtxScopes.back()->maps);
}

Ptr<AST::Ty> TypeManager::RecoverUnivTyVar(Ptr<AST::Ty> ty)
{
    TypeSubst i2uMap;
    for (auto [univ, inst] : GetInstMapping().u2i) {
        i2uMap[StaticCast<TyVar*>(inst)] = univ;
    }
    return GetInstantiatedTy(ty, i2uMap);
}

SubstPack TypeManager::GetInstMapping()
{
    return instCtxScopes.back()->maps;
}

Ptr<Ty> TypeManager::ApplySubstPack(const Ptr<Ty> declaredTy, const SubstPack& maps, bool ignoreUnsolved)
{
    Ptr<Ty> t1;
    if (ignoreUnsolved) {
        TypeSubst u2iSolved;
        for (auto [tvu, tvi] : maps.u2i) {
            if (maps.inst.count(StaticCast<TyVar*>(tvi)) > 0) {
                u2iSolved.emplace(tvu, tvi);
            }
        }
        t1 = GetInstantiatedTy(declaredTy, u2iSolved);
    } else {
        t1 = GetInstantiatedTy(declaredTy, maps.u2i);
    }
    return GetBestInstantiatedTy(t1, maps.inst);
}

std::set<Ptr<Ty>> TypeManager::ApplySubstPackNonUniq(
    const Ptr<Ty> declaredTy, const SubstPack& maps, bool ignoreUnsolved)
{
    Ptr<Ty> t1;
    if (ignoreUnsolved) {
        TypeSubst u2iSolved;
        for (auto [tvu, tvi] : maps.u2i) {
            if (maps.inst.count(StaticCast<TyVar*>(tvi)) > 0) {
                u2iSolved.emplace(tvu, tvi);
            }
        }
        t1 = GetInstantiatedTy(declaredTy, u2iSolved);
    } else {
        t1 = GetInstantiatedTy(declaredTy, maps.u2i);
    }
    return GetInstantiatedTys(t1, maps.inst);
}

bool TypeManager::PairIsOverrideOrImpl(
    const Decl& child, const Decl& parent, const Ptr<AST::Ty> baseTy, const Ptr<AST::Ty> parentTy)
{
    if (child.astKind != parent.astKind || child.identifier.Val() != parent.identifier.Val()) {
        return false;
    }
    return child.astKind == ASTKind::FUNC_DECL
        ? IsOverrideOrShadow(*this, StaticCast<FuncDecl>(child), StaticCast<FuncDecl>(parent), baseTy, parentTy)
        : IsOverrideOrShadow(*this, StaticCast<PropDecl>(child), StaticCast<PropDecl>(parent), baseTy);
}

std::optional<Ptr<ExtendDecl>> TypeManager::GetExtendDeclByInterface(Ty& baseTy, Ty& interfaceTy)
{
    if (!interfaceTy.IsInterface()) {
        return {};
    }
    auto extendedTy = GetRealExtendedTy(baseTy, interfaceTy);
    auto extends = GetAllExtendsByTy(*extendedTy);
    for (auto extend : extends) {
        CJC_ASSERT(extend);
        auto& types = extend->inheritedTypes;
        bool isExtended = std::any_of(
            types.begin(), types.end(), [this, &interfaceTy](auto& type) { return IsSubtype(type->ty, &interfaceTy); });
        if (isExtended) {
            return extend;
        }
    }
    return {};
}

Ptr<ExtendDecl> TypeManager::GetExtendDeclByMember(const Decl& member, Ty& baseTy)
{
    if (!member.outerDecl) {
        return nullptr;
    }
    Ptr<ExtendDecl> extend = nullptr;
    if (member.outerDecl->astKind == ASTKind::INTERFACE_DECL && member.outerDecl->ty) {
        auto extendDeclOp = GetExtendDeclByInterface(baseTy, *member.outerDecl->ty);
        extend = extendDeclOp.has_value() ? extendDeclOp.value() : nullptr;
    } else if (member.outerDecl->astKind == ASTKind::EXTEND_DECL) {
        extend = StaticCast<ExtendDecl>(member.outerDecl);
    }
    return extend;
}

Ptr<AST::Ty> TypeManager::AddSumByCtor(GenericsTy& tv, Ty& tyCtor, std::vector<Ptr<GenericsTy>>& tyArgs)
{
    CJC_ASSERT(tv.isPlaceholder);
    for (size_t i = tyArgs.size(); i < tyCtor.typeArgs.size(); i++) {
        tyArgs.push_back(AllocTyVar("T-Fly", true, &tv));
    }
    TypeSubst mapping;
    for (size_t i = 0; i < tyCtor.typeArgs.size(); i++) {
        mapping.emplace(StaticCast<GenericsTy*>(tyCtor.typeArgs[i]), tyArgs[i]);
    }
    auto placeholderTy = GetInstantiatedTy(&tyCtor, mapping);
    constraints[&tv].sum.insert(placeholderTy);
    return placeholderTy;
}

Ptr<AST::Ty> TypeManager::ConstrainByCtor(GenericsTy& tv, Ty& tyCtor)
{
    CJC_ASSERT(tv.isPlaceholder);
    TypeSubst mapping;
    for (auto ub : constraints[&tv].ubs) {
        if (OfSameCtor(ub, &tyCtor)) {
            return ub;
        }
    }
    for (auto tyArg : tyCtor.typeArgs) {
        mapping.emplace(StaticCast<GenericsTy*>(tyArg), AllocTyVar("T-Fly", true, &tv));
    }
    auto placeholderTy = GetInstantiatedTy(&tyCtor, mapping);
    if (IsSubtype(&tv, placeholderTy, true, false)) {
        return placeholderTy;
    } else {
        return nullptr;
    }
}

bool TypeManager::OfSameCtor(Ptr<Ty> ty, Ptr<Ty> tyCtor)
{
    if (!Ty::IsTyCorrect(ty) || !Ty::IsTyCorrect(tyCtor)) {
        return false;
    }
    if (ty->typeArgs.size() != tyCtor->typeArgs.size()) {
        return false;
    }
    TypeSubst m;
    for (size_t i = 0; i < tyCtor->typeArgs.size(); i++) {
        m[StaticCast<TyVar*>(tyCtor->typeArgs[i])] = ty->typeArgs[i];
    }
    return GetInstantiatedTy(tyCtor, m) == ty;
}

namespace {
TypeSubst GetGreedySubst(Constraint& cst)
{
    TypeSubst m;
    for (auto& [tv, bound] : cst) {
        if (!bound.eq.empty()) {
            m.emplace(tv, *bound.eq.begin());
        }
    }
    return m;
}
}

Ptr<AST::Ty> TypeManager::TryGreedySubst(Ptr<AST::Ty> ty)
{
    if (Ty::IsTyCorrect(ty) && ty->HasPlaceholder()) {
        return GetInstantiatedTy(ty, GetGreedySubst(constraints));
    }
    return ty;
}

bool TypeManager::TyVarHasNoSum(TyVar& tv) const
{
    return constraints.count(&tv) > 0 && constraints.at(&tv).sum.size() == 1 &&
        (*constraints.at(&tv).sum.cbegin())->IsAny();
}

Ptr<AST::Decl> TypeManager::GetDummyBuiltInDecl(Ptr<Ty> ty)
{
    if (dummyBuiltInDecls.count(ty) == 0) {
        dummyBuiltInDecls.emplace(ty, MakeOwnedNode<Decl>());
        dummyBuiltInDecls[ty]->ty = ty;
    }
    return dummyBuiltInDecls[ty].get();
}
} // namespace Cangjie
