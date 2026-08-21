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

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "ExtraScopes.h"
#include "LocalTypeArgumentSynthesis.h"
#include "OverrideFunctionResolver.h"
#include "TypeCheckUtil.h"

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

QuestTy TypeManager::theQuestTy{QuestTy{}};
CStringTy TypeManager::theCStringTy{CStringTy{}};

bool TypeManager::IsCopyInterfaceTy(DataTy ty) const
{
    if (auto interf = DynamicCast<InterfaceTy>(ty)) {
        if (interf->declPtr->fullPackageName == CORE_PACKAGE_NAME && interf->declPtr->identifier == COPY_NAME) {
            return true;
        }
    }
    return false;
}

static std::vector<PrimitiveTy> GeneratePrimitiveTys()
{
    std::vector<PrimitiveTy> tys;
    for (auto i = static_cast<int32_t>(TYPE_PRIMITIVE_MIN); i <= static_cast<int32_t>(TYPE_PRIMITIVE_MAX); i++) {
        if (i == static_cast<int32_t>(TypeKind::TYPE_NOTHING)) {
            tys.emplace_back(NothingTy());
        } else {
            tys.emplace_back(PrimitiveTy(static_cast<TypeKind>(i)));
        }
    }
    return tys;
}

std::vector<PrimitiveTy> TypeManager::primitiveTys{GeneratePrimitiveTys()};

Ptr<EnumTy> TypeManager::GetEnumTy(EnumDecl& ed, const std::vector<DataTy>& typeArgs)
{
    return GetTypeTy<EnumTy>(ed.identifier, ed, typeArgs);
}

Ptr<RefEnumTy> TypeManager::GetRefEnumTy(EnumDecl& ed, const std::vector<DataTy>& typeArgs)
{
    return GetTypeTy<RefEnumTy>(ed.identifier, ed, typeArgs);
}

Ptr<ClassTy> TypeManager::GetClassTy(ClassDecl& cd, const std::vector<DataTy>& typeArgs)
{
    return GetTypeTy<ClassTy>(cd.identifier, cd, typeArgs);
}

Ptr<ClassThisTy> TypeManager::GetClassThisTy(ClassDecl& cd, const std::vector<DataTy>& typeArgs)
{
    return GetTypeTy<ClassThisTy>(cd.identifier, cd, typeArgs);
}

Ptr<InterfaceTy> TypeManager::GetInterfaceTy(InterfaceDecl& id, const std::vector<DataTy>& typeArgs)
{
    return GetTypeTy<InterfaceTy>(id.identifier, id, typeArgs);
}

Ptr<StructTy> TypeManager::GetStructTy(StructDecl& sd, const std::vector<DataTy>& typeArgs)
{
    return GetTypeTy<StructTy>(sd.identifier, sd, typeArgs);
}

Ptr<TypeAliasTy> TypeManager::GetTypeAliasTy(TypeAliasDecl& tad, const std::vector<DataTy>& typeArgs)
{
    return GetTypeTy<TypeAliasTy>(tad.identifier, tad, typeArgs);
}

Ptr<ArrayTy> TypeManager::GetArrayTy(DataTy elemTy, unsigned int dims)
{
    DataTy tmpElemTy = elemTy;
    unsigned int tmpDims = dims;
    if (Ty::IsTyCorrect(elemTy)) {
        if (elemTy->IsArray()) {
            auto arrayTy = RawStaticCast<ArrayTy*>(elemTy);
            tmpElemTy = arrayTy->typeArgs[0].Ty();
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

Ptr<PointerTy> TypeManager::GetPointerTy(DataTy elemTy)
{
    DataTy tmpElemTy = elemTy;
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

Ptr<TupleTy> TypeManager::GetTupleTy(const std::vector<DataTy>& typeArgs, bool isClosureTy)
{
    return GetTypeTy<TupleTy>(typeArgs, isClosureTy);
}

Ptr<FuncTy> TypeManager::GetFunctionTy(const std::vector<ModalTy>& paramTys, ModalTy retTy, FuncTy::Config cfg)
{
    return GetTypeTy<FuncTy>(paramTys, retTy, cfg);
}

DataTy TypeManager::GetIntersectionTy(const std::set<DataTy>& tys)
{
    return GetTypeTy<IntersectionTy>(tys);
}

DataTy TypeManager::GetUnionTy(const std::set<DataTy>& tys)
{
    return GetTypeTy<UnionTy>(tys);
}

DataTy TypeManager::GetBlockRealTy(const Block& block) const
{
    if (block.desugarExpr) {
        return block.desugarExpr->DataTy();
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
        return expr->desugarExpr.get() ? expr->desugarExpr->DataTy() : expr->DataTy();
    }
    return block.DataTy();
}

DataTy TypeManager::TyInstantiator::GetInstantiatedGenericTy(GenericsTy& ty)
{
    DataTy curTy = &ty;
    auto mapping = typeMapping;
    auto tyToFind = StaticCast<GenericsTy>(&ty);
    auto it = mapping.find(tyToFind);
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

DataTy TypeManager::TyInstantiator::GetInstantiatedStructTy(StructTy& structTy)
{
    // If is a struct without generic parameter, no need do instantiation.
    if (!structTy.declPtr || !structTy.declPtr->generic) {
        return &structTy;
    }
    std::vector<DataTy> typeArgs;
    // Build type arguments.
    for (auto& it : structTy.typeArgs) {
        typeArgs.push_back(Instantiate(it.Ty()));
    }
    return tyMgr.GetStructTy(*structTy.declPtr, typeArgs);
}

DataTy TypeManager::TyInstantiator::GetInstantiatedClassTy(ClassTy& classTy)
{
    if (!classTy.declPtr || !classTy.declPtr->generic) {
        return &classTy;
    }
    std::vector<DataTy> typeArgs;
    for (auto& it : classTy.typeArgs) {
        typeArgs.push_back(Instantiate(it.Ty()));
    }
    Ptr<ClassTy> insTy = nullptr;
    if (Is<ClassThisTy>(classTy)) {
        insTy = tyMgr.GetClassThisTy(*classTy.declPtr, typeArgs);
    } else {
        insTy = tyMgr.GetClassTy(*classTy.declPtr, typeArgs);
    }
    return insTy;
}

DataTy TypeManager::TyInstantiator::GetInstantiatedInterfaceTy(InterfaceTy& interfaceTy)
{
    if (!interfaceTy.declPtr || !interfaceTy.declPtr->generic) {
        return &interfaceTy;
    }
    std::vector<DataTy> typeArgs;
    for (auto& it : interfaceTy.typeArgs) {
        typeArgs.push_back(Instantiate(it.Ty()));
    }
    auto insTy = tyMgr.GetInterfaceTy(*interfaceTy.declPtr, typeArgs);
    return insTy;
}

DataTy TypeManager::TyInstantiator::GetInstantiatedEnumTy(EnumTy& enumTy)
{
    // If is an enum without generic parameter, no need to do instantiation.
    if (!enumTy.declPtr || !enumTy.declPtr->generic) {
        return &enumTy;
    }
    std::vector<DataTy> typeArgs;
    // Build type arguments.
    for (auto& it : enumTy.typeArgs) {
        typeArgs.push_back(Instantiate(it.Ty()));
    }
    if (Is<RefEnumTy>(enumTy)) {
        return tyMgr.GetRefEnumTy(*enumTy.declPtr, typeArgs);
    }
    auto tmp = tyMgr.GetEnumTy(*enumTy.declPtr, typeArgs);
    tmp->hasCorrespondRefEnumTy = enumTy.hasCorrespondRefEnumTy;
    return tmp;
}

DataTy TypeManager::TyInstantiator::GetInstantiatedArrayTy(ArrayTy& arrayTy)
{
    if (arrayTy.typeArgs.empty()) {
        return &arrayTy;
    }
    auto elemTy = Instantiate(arrayTy.typeArgs[0].Ty());
    auto dims = arrayTy.dims;
    return tyMgr.GetArrayTy(elemTy, dims);
}

DataTy TypeManager::TyInstantiator::GetInstantiatedPointerTy(PointerTy& cptrTy)
{
    if (cptrTy.typeArgs.empty()) {
        return &cptrTy;
    }
    auto elemTy = Instantiate(cptrTy.typeArgs[0].Ty());
    return tyMgr.GetPointerTy(elemTy);
}

template <typename SetTy> DataTy TypeManager::TyInstantiator::GetInstantiatedSetTy(SetTy& ty)
{
    std::set<DataTy> tys;
    for (auto it : ty.tys) {
        tys.emplace(Instantiate(it));
    }
    return tyMgr.GetTypeTy<SetTy>(tys);
}

DataTy TypeManager::TyInstantiator::Instantiate(Ty& ty)
{
    if (typeMapping.empty()) {
        return &ty;
    }
    switch (ty.kind) {
        // If is generic type, should replace it with type mapping.
        case TypeKind::TYPE_GENERICS:
            return GetInstantiatedGenericTy(StaticCast<GenericsTy>(ty));
        case TypeKind::TYPE_FUNC: {
            std::vector<ModalTy> paramTys;
            auto& funcTy = StaticCast<FuncTy>(ty);
            for (auto& it : funcTy.paramTys) {
                paramTys.push_back(Instantiate(it));
            }
            auto retType = Instantiate(funcTy.retTy);
            DataTy ret = tyMgr.GetFunctionTy(
                paramTys, retType, {funcTy.IsCFunc(), funcTy.isClosureTy, funcTy.hasVariableLenArg});
            return ret;
        }
        case TypeKind::TYPE_TUPLE: {
            std::vector<DataTy> typeArgs;
            std::transform(ty.typeArgs.begin(), ty.typeArgs.end(), std::back_inserter(typeArgs),
                [this](auto it) { return Instantiate(it.Ty()); });
            return tyMgr.GetTupleTy(typeArgs, StaticCast<TupleTy>(ty).isClosureTy);
        }
        case TypeKind::TYPE_ARRAY:
            return GetInstantiatedArrayTy(StaticCast<ArrayTy>(ty));
        case TypeKind::TYPE_POINTER:
            return GetInstantiatedPointerTy(StaticCast<PointerTy>(ty));
        case TypeKind::TYPE_STRUCT:
            return GetInstantiatedStructTy(StaticCast<StructTy>(ty));
        case TypeKind::TYPE_CLASS:
            return GetInstantiatedClassTy(StaticCast<ClassTy>(ty));
        case TypeKind::TYPE_INTERFACE:
            return GetInstantiatedInterfaceTy(StaticCast<InterfaceTy>(ty));
        case TypeKind::TYPE_ENUM:
            return GetInstantiatedEnumTy(StaticCast<EnumTy>(ty));
        case TypeKind::TYPE: {
            std::vector<DataTy> typeArgs;
            for (auto& it : ty.typeArgs) {
                typeArgs.push_back(Instantiate(it.Ty()));
            }
            return tyMgr.GetTypeAliasTy(*StaticCast<TypeAliasTy>(ty).declPtr, typeArgs);
        }
        case TypeKind::TYPE_INTERSECTION:
            return GetInstantiatedSetTy(StaticCast<IntersectionTy>(ty));
        case TypeKind::TYPE_UNION:
            return GetInstantiatedSetTy(StaticCast<UnionTy>(ty));
        default:;
    }
    return &ty;
}

// Temporarily for backward compatibility.
ModalTy TypeManager::GetBestInstantiatedTy(ModalTy ty, const MultiTypeSubst& mts)
{
    if (mts.empty()) {
        return ty;
    }
    if (!ty.IsCorrect()) {
        return {TypeManager::GetInvalidTy(), ty.Mode()};
    }
    auto instTys = GetInstantiatedTys(ty, mts);
    return instTys.empty() ? ty : *instTys.begin();
}

std::set<ModalTy> TypeManager::GetInstantiatedTys(ModalTy ty, const MultiTypeSubst& mts)
{
    if (mts.empty()) {
        return {ty};
    }
    auto allGenericTys = GetAllGenericTys(ty.Ty());
    TyVars tyVarsForInst = StaticToTyVars(allGenericTys);
    MultiTypeSubst reducedMs = ReduceMultiTypeSubst(*this, tyVarsForInst, mts);
    std::set<TypeSubst> ms = ExpandMultiTypeSubst(*this, reducedMs, {ty.Ty()});
    std::set<ModalTy> res;
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

std::set<DataTy> TypeManager::GetInstantiatedTys(DataTy ty, const MultiTypeSubst& mts)
{
    auto inst = GetInstantiatedTys(ModalTy{ty}, mts);
    std::set<DataTy> res;
    for (auto t : inst) {
        res.insert(t.Ty());
    }
    return res;
}

ModalTy TypeManager::GetInstantiatedTy(ModalTy ty, const TypeSubst& typeMapping)
{
    auto instantiator = TyInstantiator(*this, typeMapping);
    return instantiator.Instantiate(ty);
}

DataTy TypeManager::ApplyTypeSubstForTy(const TypeSubst& typeMapping, DataTy ty)
{
    if (typeMapping.empty()) {
        return ty;
    }
    TyVarScope ts(*this);
    SubstPack substPack;
    PackMapping(substPack, typeMapping);
    return ApplySubstPack(ty, substPack);
}

DataTy TypeManager::GetInstantiatedTy(DataTy ty, const TypeSubst& typeMapping)
{
    auto instantiator = TyInstantiator(*this, typeMapping);
    auto r = instantiator.Instantiate(ModalTy{ty});
    CJC_ASSERT(r.IsDataType());
    return r.Ty();
}

std::set<ModalTy> TypeManager::ApplyTypeSubstForTys(const TypeSubst& subst, const std::set<Ptr<TyVar>>& tys)
{
    std::set<ModalTy> res;
    for (auto& i : tys) {
        res.insert(ApplyTypeSubstForTy(subst, i));
    }
    return res;
}

void TypeManager::GenerateExtendGenericMappingVisit(
    MultiTypeSubst& typeMapping, Ty& baseType, std::unordered_set<DataTy>& visited)
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
            auto tmp = ApplySubstPackNonUniq(ModalTy{tvu}, mapping);
            std::set<DataTy> target;
            for (auto ty : tmp) {
                CJC_ASSERT(ty.IsDataType());
                target.insert(ty.Ty());
            }
            mts[tvu] = target;
        }
    }
    return mts;
}

void TypeManager::MakeInstTyVar(SubstPack& maps, const Decl& d)
{
    if (d.generic) {
        for (auto& genParam : d.generic->typeParameters) {
            if (genParam->GetTy().IsCorrect()) {
                MakeInstTyVar(maps, *StaticCast<GenericsTy*>(genParam->DataTy()));
            }
        }
    }
}

void TypeManager::MakeInstTyVar(SubstPack& maps, GenericsTy& uTv)
{
    CJC_ASSERT(!uTv.isPlaceholder);
    if (maps.u2i.count(&uTv) == 0) {
        maps.u2i[&uTv] = AllocTyVar(uTv.name);
    }
}

void TypeManager::GenerateStructDeclGenericMapping(MultiTypeSubst& m, const InheritableDecl& decl, const Ty& targetTy)
{
    std::unordered_set<DataTy> visited;
    GenerateStructDeclGenericMappingVisit(m, decl, targetTy, visited);
}

void TypeManager::GenerateStructDeclGenericMappingVisit(
    MultiTypeSubst& m, const InheritableDecl& decl, const Ty& targetTy, std::unordered_set<DataTy>& visited)
{
    if (targetTy.IsInvalid()) {
        return;
    }
    MergeTypeSubstToMultiTypeSubst(m, GenerateTypeMapping(decl, targetTy.TyArgs()));
    for (auto& inheritedType : decl.inheritedTypes) {
        if (IsInheritableType(inheritedType->DataTy())) {
            GenerateGenericMappingVisit(m, *inheritedType->GetTy(), visited);
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
    std::unordered_set<DataTy> visited;
    GenerateGenericMappingVisit(m, baseType, visited, true);
}

void TypeManager::GenerateGenericMappingVisit(
    SubstPack& m, Ty& baseType, std::unordered_set<DataTy>& visited, bool contextual)
{
    if (baseType.IsInvalid() || visited.count(&baseType) > 0) {
        return;
    }
    // avoid repeatedly visiting same super-type when there are multiple paths to it
    visited.emplace(&baseType);
    GenerateExtendGenericMappingVisit(m, baseType, visited, contextual);
    if (auto decl = Ty::GetDeclPtrOfTy(&baseType)) {
        if (auto inheritableDecl = DynamicCast<InheritableDecl*>(decl)) {
            GenerateStructDeclGenericMappingVisit(m, *inheritableDecl, baseType, visited, contextual);
        }
    }
}

void TypeManager::GenerateExtendGenericMappingVisit(
    SubstPack& typeMapping, Ty& baseType, std::unordered_set<DataTy>& visited, bool contextual)
{
    if (baseType.IsInvalid()) {
        return;
    }
    std::set<Ptr<ExtendDecl>> extends = GetAllExtendsByTy(baseType);
    for (auto& extend : extends) {
        GenerateStructDeclGenericMappingVisit(typeMapping, *extend, baseType, visited, contextual);
    }
}

void TypeManager::GenerateStructDeclGenericMappingVisit(
    SubstPack& m, const InheritableDecl& decl, const Ty& targetTy, std::unordered_set<DataTy>& visited, bool contextual)
{
    if (targetTy.IsInvalid()) {
        return;
    }
    if (contextual) {
        GenerateTypeMapping(*this, m, decl, targetTy.TyArgs());
    } else {
        auto typeArgs = targetTy.TyArgs();
        for (auto& ty : typeArgs) {
            ty = GetInstantiatedTy(ty, m.u2i); // use placeholder ty var for mid-level targetTys
        }
        GenerateTypeMapping(*this, m, decl, typeArgs);
    }
    for (auto& inheritedType : decl.inheritedTypes) {
        if (IsInheritableType(inheritedType->DataTy())) {
            GenerateGenericMappingVisit(m, *inheritedType->GetTy(), visited, false);
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
    std::unordered_set<DataTy> visited;
    GenerateGenericMappingVisit(m, baseType, visited);
}

void TypeManager::GenerateGenericMappingVisit(MultiTypeSubst& m, Ty& baseType, std::unordered_set<DataTy>& visited)
{
    if (baseType.IsInvalid() || visited.count(&baseType) > 0) {
        return;
    }
    // avoid repeatedly visiting same super-type when there are multiple paths to it
    visited.emplace(&baseType);
    GenerateExtendGenericMappingVisit(m, baseType, visited);
    if (auto decl = Ty::GetDeclPtrOfTy(&baseType)) {
        if (auto inheritableDecl = DynamicCast<InheritableDecl*>(decl)) {
            GenerateStructDeclGenericMappingVisit(m, *inheritableDecl, baseType, visited);
        }
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
            typeMapping[StaticCast<TyVar*>(parentGeneric->typeParameters[i]->DataTy())] =
                childGeneric->typeParameters[i]->DataTy();
        }
    }
    return typeMapping;
}

MultiTypeSubst TypeManager::GenerateStructDeclTypeMapping(const Decl& decl)
{
    if (!decl.IsNominalDecl()) {
        return {};
    }
    auto parentTy = decl.GetTy();
    if (decl.astKind == ASTKind::EXTEND_DECL) {
        auto& ed = static_cast<const ExtendDecl&>(decl);
        CJC_ASSERT(ed.extendedType);
        parentTy = ed.extendedType->GetTy();
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
        return LocalTypeArgumentSynthesis::Unify(*this, constraints, {&leaf}, {&root});
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

// Treating specific and common classes/struct/enums types as subtypes of each other
bool IsCommonAndSpecificRelation(const Ty& leafTy, const Ty& rootTy)
{
    if (!leafTy.IsNominal() || !rootTy.IsNominal()) {
        return false;
    }
    auto leafDecl = Ty::GetDeclOfTy(&leafTy);
    auto rootDecl = Ty::GetDeclOfTy(&rootTy);
    if (leafDecl && rootDecl) {
        leafDecl = leafDecl->specificImplementation == nullptr ? leafDecl : leafDecl->specificImplementation;
        rootDecl = rootDecl->specificImplementation == nullptr ? rootDecl : rootDecl->specificImplementation;
        if (!leafDecl->TestAttr(Attribute::SPECIFIC) || !rootDecl->TestAttr(Attribute::SPECIFIC)) {
            return false;
        }
        return leafDecl == rootDecl;
    }
    return false;
}

bool TypeManager::IsClassLikeSubtype(Ty& leaf, Ty& root, bool implicitBoxed, bool allowOptionBox)
{
    if (auto thisTyOfLeaf = DynamicCast<ClassThisTy*>(&leaf); thisTyOfLeaf && thisTyOfLeaf->declPtr) {
        auto leafClassTy = GetClassTy(*thisTyOfLeaf->declPtr, thisTyOfLeaf->TyArgs());
        if (auto thisTyOfRoot = DynamicCast<ClassThisTy*>(&root); thisTyOfRoot && thisTyOfRoot->declPtr) {
            // 'root' is class type, must not exist boxing relation.
            return IsSubtype(
                leafClassTy, GetClassTy(*thisTyOfRoot->declPtr, thisTyOfRoot->TyArgs()), implicitBoxed, allowOptionBox);
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
        return IsTyEqual(DataTy{&leaf}, DataTy{&root});
    }
    return false;
}

bool TypeManager::IsClassTyEqual(Ty& leaf, Ty& root)
{
    if (&leaf == &root || IsCommonAndSpecificRelation(leaf, root)) {
        return true;
    }
    if (auto ctt = DynamicCast<ClassThisTy*>(&leaf); ctt) {
        return false;
    }
    if (auto ctt = DynamicCast<ClassThisTy*>(&root); ctt) {
        return false;
    }
    size_t index = 0;
    auto predFunc = [&index, &root, this](const ModalTy& mTy) {
        DataTy ty = mTy.Ty();
        if (root.typeArgs[index]->IsIntersection()) {
            if (IsSubtype(ModalTy{ty}, root.typeArgs[index])) {
                // This is for partial generic type-alias case.
                root.typeArgs[index] = ModalTy{ty};
                ++index;
                return true;
            }
        } else {
            auto rootArg = root.typeArgs[index];
            if (rootArg.Ty() == ty || IsPlaceholderEqual(*rootArg, *ty)) {
                ++index;
                return true;
            }
        }
        return false;
    };
    // Need add test case to verify this logic.
    return static_cast<ClassLikeTy&>(leaf).commonDecl->GetTy() == static_cast<ClassLikeTy&>(root).commonDecl->GetTy() &&
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

        if (root.IsCoreOptionType() && allowOptionBox &&
            CountOptionNestedLevel(&leaf) < CountOptionNestedLevel(&root)) {
            // Core's enum Option check, support for auto package Option.
            return IsSubtype(&leaf, root.TyArg(0), implicitBoxed);
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
bool TypeManager::IsSubtype(
    ModalTy leaf, ModalTy root, bool implicitBoxed, bool allowOptionBox, ModalMatchMode modalMatchMode)
{
    if (!leaf.IsCorrect() || !root.IsCorrect()) {
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
    bool modalSubtyping = modalMatchMode == ModalMatchMode::SUBTYPE ? IsModalSubtype(leaf, root)
        : modalMatchMode == ModalMatchMode::EXACT ? leaf.Mode() == root.Mode() : true;
    if (ImplementsCopyInterface(leaf.Ty())) {
        modalSubtyping = true;
    }
    return modalSubtyping && IsSubtype(leaf.Ty(), root.Ty(), implicitBoxed, allowOptionBox);
}

bool TypeManager::IsSubtype(DataTy leaf, DataTy root, bool implicitBoxed, bool allowOptionBox)
{
    // NOTE: all cffi types are not classLike type, so using conditions as below.
    bool ffiFastCheck = (leaf->IsMetCType() && root->IsCType());
    bool fastCheck = ffiFastCheck || leaf == root || (leaf->IsNothing() && !root->IsPlaceholder()) ||
        ((implicitBoxed || leaf->IsClassLike()) && root->IsAny() && !leaf->IsPlaceholder());
    if (fastCheck) {
        return true;
    }
    /// copy type check fastpath
    if (IsCopyInterfaceTy(root) && ImplementsCopyInterface(leaf)) {
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
    if (IsCommonAndSpecificRelation(*leaf, *root)) {
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
            LocalTypeArgumentSynthesis::Unify(*this, constraints, leaf, root)) {
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

bool TypeManager::IsModalSubtype(ModalTy leaf, ModalTy root)
{
    // if T <: Copy, then it is possible to cast a T @local? to a T @local! or T@~local
    // also @local! and @~local are subtype of @local?, so all of them can be cast to one another
    if (ImplementsCopyInterface(leaf.Ty())) {
        return true;
    }
    // The mode of an unsolved placeholder type variable is meaningless: it has not been bound
    // to a concrete type yet, so its default mode cannot be compared against a concrete type's
    // mode. This holds for the placeholder on either side. Allow the subtype so inference can
    // proceed and let the solver record the concrete type (with its mode) as the placeholder's
    // bound. Mirrors UnifyOne's "the mode of placeholder is meaningless" guard.
    if (leaf.Ty()->IsPlaceholder() || root.Ty()->IsPlaceholder()) {
        return true;
    }
    return IsModalSubtype(leaf.Mode(), root.Mode());
}

bool TypeManager::IsModalSubtype(ModalInfo leaf, ModalInfo root)
{
    auto l = static_cast<int>(leaf.local);
    auto r = static_cast<int>(root.local);
    return (l & r) == l;
}

// Whether @p ty is a copy type for modal/locality checking (spec: such types are not subject to locality
// constraints). Copy types include.
// - CType subtypes (excluding CType itself)
// - Primitive types: Int*, Float*, Bool, Rune, Nothing, Invalid
// - Tuple whose elements are all copy types; VArray whose element type is a copy type
// - Generic type variable with a Copyable upper bound
// - struct declared struct S <: Copyable
bool TypeManager::ImplementsCopyInterface(DataTy ty)
{
    if (ty->IsMetCType()) {
        return true;
    }
    if (auto d = DynamicCast<PrimitiveTy>(ty)) {
        switch (d->kind) {
            case TypeKind::TYPE_FLOAT16:
            case TypeKind::TYPE_RUNE:
            case TypeKind::TYPE_NOTHING:
            case TypeKind::TYPE_IDEAL_FLOAT:
            case TypeKind::TYPE_IDEAL_INT:
            case TypeKind::TYPE_INVALID:
                return true;
            default:
                return false;
        }
    }
    if (auto tuple = DynamicCast<TupleTy>(ty)) {
        return std::all_of(tuple->typeArgs.begin(), tuple->typeArgs.end(),
            [this](ModalTy m) { return ImplementsCopyInterface(m.Ty()); });
    }
    if (auto g = DynamicCast<GenericsTy>(ty)) {
        if (!g->isUpperBoundLegal) {
            return false;
        }
        for (auto upper : g->upperBounds) {
            if (ImplementsCopyInterface(upper)) {
                return true;
            }
        }
        return false;
    }
    if (auto array = DynamicCast<VArrayTy>(ty)) {
        return ImplementsCopyInterface(array->TyArg(0));
    }
    auto decl = Ty::GetDeclOfTy(ty);
    if (auto structDecl = DynamicCast<StructDecl>(decl)) {
        return structDecl->IsCopyType();
    }
    return false;
}

bool TypeManager::IsTyEqual(DataTy subTy, DataTy baseTy)
{
    PData::CommitScope cs(constraints);
    if (IsSubtype(subTy, baseTy, false, false) && IsSubtype(baseTy, subTy, false, false)) {
        return true;
    }
    PData::Reset(constraints);
    return false;
}

bool TypeManager::IsTyEqual(ModalTy subTy, ModalTy baseTy)
{
    PData::CommitScope cs(constraints);
    if (IsSubtype(subTy, baseTy, false, false) && IsSubtype(baseTy, subTy, false, false)) {
        return true;
    }
    PData::Reset(constraints);
    return false;
}

bool TypeManager::IsLitBoxableType(DataTy leaf, DataTy root)
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
        return IsLitBoxableType(leaf, root->TyArg(0));
    }
    return false;
}

bool TypeManager::IsFuncParameterTypesIdentical(const FuncTy& t1, const FuncTy& t2)
{
    if (!Ty::IsTyCorrect(&t1) || !Ty::IsTyCorrect(&t2) || t1.paramTys.size() != t2.paramTys.size()) {
        return false;
    }
    for (size_t i = 0; i < t2.paramTys.size(); i++) {
        if (CheckTypeCompatibility(t1.paramTys[i], t2.paramTys[i], false) != TypeCompatibility::IDENTICAL) {
            return false;
        }
    }
    return true;
}

bool TypeManager::IsFuncParameterTypesIdentical(
    const std::vector<ModalTy>& paramTys1, const std::vector<ModalTy>& paramTys2, const TypeSubst& typeMapping)
{
    if (paramTys1.size() != paramTys2.size()) {
        return false;
    }
    for (size_t i = 0; i < paramTys2.size(); i++) {
        auto paramTy1 = GetInstantiatedTy(paramTys1[i], typeMapping);
        if (CheckTypeCompatibility(paramTy1, paramTys2[i], false) != TypeCompatibility::IDENTICAL) {
            return false;
        }
    }
    return true;
}

bool TypeManager::HasThisParam(const FuncDecl& fd)
{
    // Accessor (getter/setter) of an instance property has an implicit `this` whose modal is
    // taken from the property's type. The parser no longer emits an explicit `this` parameter
    // for accessors, so the `funcBody`/`paramLists` checks below are not applicable for them.
    if (fd.propDecl) {
        return !fd.TestAttr(Attribute::STATIC);
    }
    if (fd.ownerFunc) {
        return HasThisParam(*fd.ownerFunc);
    }
    if (!fd.funcBody || fd.funcBody->paramLists.empty()) {
        return false;
    }
    return (Is<InheritableDecl>(fd.outerDecl) || (fd.outerDecl && fd.outerDecl->IsBuiltIn())) &&
        !fd.TestAnyAttr(Attribute::STATIC, Attribute::ENUM_CONSTRUCTOR);
}

bool TypeManager::HasThisParam(const AST::Decl& decl)
{
    if (decl.TestAttr(Attribute::STATIC)) {
        return false;
    }
    if (auto fd = DynamicCast<FuncDecl>(&decl)) {
        return HasThisParam(*fd);
    }
    return false;
}

ModalTy TypeManager::GetThisParamTy(const FuncDecl& fd)
{
    CJC_ASSERT(HasThisParam(fd));
    auto outer = fd.outerDecl;
    CJC_ASSERT(outer);
    // For accessors, the this-modal is the modal of the owning property's type.
    if (fd.propDecl != nullptr) {
        auto propTy = fd.propDecl->type ? fd.propDecl->type->GetTy() : fd.propDecl->GetTy();
        return outer->GetTy().With(propTy.Mode());
    }
    ModalInfo modal{}; // ~local by default
    if (fd.funcBody->paramLists[0]->thisParam) {
        modal = fd.funcBody->paramLists[0]->thisParam->modal;
    }
    return outer->GetTy().With(modal);
}

ModalInfo TypeManager::GetThisParamMode(const Decl& decl)
{
    if (auto fd = DynamicCast<FuncDecl>(&decl)) {
        return GetThisParamTy(*fd).Mode();
    }
    if (auto pd = DynamicCast<PropDecl>(&decl)) {
        return pd->TyMode();
    }
    CJC_ABORT();
    return {};
}

ModalInfo TypeManager::GetAccessorThisModal(const FuncDecl& fd)
{
    if (fd.propDecl != nullptr) {
        auto propTy = fd.propDecl->type ? fd.propDecl->type->GetTy() : fd.propDecl->GetTy();
        return propTy.Mode();
    }
    return {};
}

ModalTy TypeManager::GetAccessorTargetTy(const FuncDecl& fd)
{
    if (fd.isGetter) {
        if (fd.funcBody->retType) {
            return fd.funcBody->retType->GetTy();
        }
        return fd.propDecl->GetTy();
    }
    CJC_ASSERT(fd.isSetter);
    if (fd.funcBody->paramLists[0]->params[0]->type) {
        return fd.funcBody->paramLists[0]->params[0]->GetTy();
    }
    return fd.propDecl->GetTy();
}

bool TypeManager::CheckGenericType(ModalTy lvalue, ModalTy rvalue, bool implicitBoxed)
{
    if (Ty::AreTysCorrect(std::set{lvalue, rvalue}) && lvalue->kind == TypeKind::TYPE_GENERICS &&
        rvalue->kind == TypeKind::TYPE_GENERICS) {
        auto lg = RawStaticCast<GenericsTy*>(lvalue.Ty());
        auto rg = RawStaticCast<GenericsTy*>(rvalue.Ty());
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
    ModalTy lvalue, ModalTy rvalue, bool implicitBoxed, bool isGeneric)
{
    if (!Ty::AreTysCorrect(std::set{lvalue, rvalue})) {
        return TypeCompatibility::INCOMPATIBLE;
    }
    if (lvalue == rvalue || IsCommonAndSpecificRelation(*lvalue, *rvalue)) {
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
    if (lvalue->IsEnum() && rvalue->IsEnum() &&
        isEnumCompatible(*RawStaticCast<EnumTy*>(lvalue.Ty()), *RawStaticCast<EnumTy*>(rvalue.Ty()))) {
        if (lvalue.Mode() == rvalue.Mode()) {
            return TypeCompatibility::IDENTICAL;
        }
        return lvalue.Mode().IsSubModal(rvalue.Mode()) ? TypeCompatibility::SUBTYPE : TypeCompatibility::INCOMPATIBLE;
    }
    return TypeCompatibility::INCOMPATIBLE;
}

Ptr<Ty> TypeManager::ReplaceIdealTy(Ptr<Ty> ty)
{
    if (!Ty::IsTyCorrect(ty)) {
        return ty;
    }
    switch (ty->kind) {
        case TypeKind::TYPE_IDEAL_INT:
            return GetPrimitiveTy(TypeKind::TYPE_INT64);
        case TypeKind::TYPE_IDEAL_FLOAT:
            return GetPrimitiveTy(TypeKind::TYPE_FLOAT64);
        case TypeKind::TYPE_CLASS:
        case TypeKind::TYPE_INTERFACE:
        case TypeKind::TYPE_ENUM:
        case TypeKind::TYPE_ARRAY:
        case TypeKind::TYPE_POINTER:
        case TypeKind::TYPE_TUPLE:
            for (auto& typeArg : ty->typeArgs) {
                typeArg = ReplaceIdealTy(std::move(typeArg));
            }
            return ty;
        default:
            return ty;
    }
}

ModalTy TypeManager::ReplaceIdealTy(ModalTy ty)
{
    if (!Ty::IsTyCorrect(ty)) {
        return ty;
    }
    // A literal's pending IDEAL modal (mirroring IDEAL_INT/IDEAL_FLOAT) must be resolved before
    // Sema ends. If it was not unified with an expected contextual modal, it defaults to ~local
    // (NOT) per the spec: "If a mode cannot be inferred from context, then it defaults to ~local."
    auto mode = ty.Mode().local == Mode::IDEAL ? ModalInfo{Mode::NOT} : ty.Mode();
    return {ReplaceIdealTy(ty.Ty()), mode};
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

bool TypeManager::HasExtendInterfaceTyHelper(
    Ty& superTy, const std::set<Ptr<ExtendDecl>>& extends, const std::vector<DataTy>& typeArgs)
{
    PData::CommitScope cs(constraints);
    for (auto& extend : extends) {
        if (!CheckGenericDeclInstantiation(extend, typeArgs)) {
            PData::Reset(constraints);
            continue;
        }
        bool extendTyNotMatch = (!extend->extendedType || !extend->extendedType->GetTy() ||
            extend->extendedType->GetTy()->typeArgs.size() != typeArgs.size());
        if (extendTyNotMatch) {
            PData::Reset(constraints);
            continue;
        }
        TypeSubst typeMapping;
        for (size_t i = 0; i < typeArgs.size(); ++i) {
            // may be used in generic instantiation
            if (auto genSuper = DynamicCast<TyVar*>(extend->extendedType->GetTy()->TyArg(i))) {
                typeMapping[genSuper] = typeArgs[i];
            }
        }
        for (auto& superInterfaceTy : extend->inheritedTypes) {
            if (!IsInheritableType(superInterfaceTy->DataTy())) {
                continue;
            }
            DataTy instTy = GetInstantiatedTy(superInterfaceTy->DataTy(), typeMapping);
            // search super interfaceTy recursively.
            if (HasSuperTy(*instTy, superTy, typeMapping)) {
                return true;
            }
        }
        PData::Reset(constraints);
    }
    return false;
}

std::unordered_set<DataTy> TypeManager::GetAllExtendInterfaceTyHelper(
    const std::set<Ptr<ExtendDecl>>& extends, const std::vector<DataTy>& typeArgs)
{
    PData::CommitScope cs(constraints);
    std::unordered_set<DataTy> ret;
    for (auto& extend : extends) {
        if (!CheckGenericDeclInstantiation(extend, typeArgs)) {
            PData::Reset(constraints);
            continue;
        }
        bool extendTyNotMatch = (!extend->extendedType || !extend->extendedType->GetTy() ||
            extend->extendedType->GetTy()->typeArgs.size() != typeArgs.size());
        if (extendTyNotMatch) {
            PData::Reset(constraints);
            continue;
        }
        TypeSubst typeMapping;
        for (size_t i = 0; i < typeArgs.size(); ++i) {
            /**
             * typeArgs in extendedType of extend may not be the same as typeVar in the extend declaration,
             * so we need to generate the type mapping, which may be used in generic instantiation.
             * E.g.
             * interface I<T1> {}
             * interface SA<T2> {}
             * open class A<T3> {}
             * extend<T4> A<SA<T4>> <: I<T4> {}
             * let a: I<Int64> = A<SA<Int64>>()
             * then typeArgs in extendedType is [SA<T4>], typeVar in the extend declaration is T4,
             * but typeArgs in the extend used in instantiation of A is [SA<Int64>].
             * so we need to generate the type mapping: [T4 |-> Int64].
             */
            typeMapping.merge(GenerateTypeMappingByTy(extend->extendedType->GetTy()->typeArgs[i].Ty(), typeArgs[i]));
        }
        for (auto& superInterfaceTy : extend->inheritedTypes) {
            if (!IsInheritableType(superInterfaceTy->DataTy())) {
                continue;
            }
            DataTy instTy = GetInstantiatedTy(superInterfaceTy->DataTy(), typeMapping);
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
            auto inhTy = inheritedType->GetTy();
            if (inhTy->IsClass() && IsInheritableType(inhTy.Ty())) {
                return HasExtendedInterfaceTy(*inhTy, superTy, substituteMapping);
            }
        }
    }
    return false;
}

std::unordered_set<DataTy> TypeManager::GetAllExtendInterfaceTy(Ty& ty)
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
        std::unordered_set<DataTy> superTys;
        GetNominalSuperTy(ty, GenerateTypeMapping(*cd, ty.TyArgs()), superTys);
        for (auto superTy : superTys) {
            auto cTy = DynamicCast<ClassTy*>(superTy);
            if (!cTy) {
                continue;
            }
            std::set<Ptr<ExtendDecl>> superExtends = GetDeclExtends(*cTy->declPtr);
            auto extendInterfaces = GetAllExtendInterfaceTyHelper(superExtends, superTy->TyArgs());
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
    const Ty& nominalTy, const TypeSubst& typeMapping, std::unordered_set<DataTy>& tyList)
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
        auto inhTy = inheritedType->GetTy();
        if (IsInheritableType(inhTy.Ty())) {
            auto inheritedTys = GetAllSuperTys(*inhTy, substituteMapping);
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
        auto inhTy = inheritedType->GetTy();
        if (IsInheritableType(inhTy.Ty()) && HasSuperTy(*inhTy, superTy, substituteMapping)) {
            return true;
        }
    }
    return false;
}

namespace {
// collect all transitive GenericsTy upperbound in addition to gty.upperBounds
std::unordered_set<DataTy> GetAllUpperBounds(const GenericsTy& gty)
{
    std::unordered_set<DataTy> ubs(gty.upperBounds.begin(), gty.upperBounds.end());
    std::unordered_set<DataTy> newGens;
    std::unordered_set<DataTy> newUbs;
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

std::unordered_set<DataTy> TypeManager::GetAllSuperTys(Ty& ty, const TypeSubst& typeMapping, bool withExtended)
{
    std::unordered_set<DataTy> tyList;
    if (!Ty::IsTyCorrect(&ty)) {
        return tyList;
    }
    TypeInfo key{&ty, typeMapping, withExtended};
    if (auto found = tyToSuperTysMap.find(key); found != tyToSuperTysMap.end()) {
        return found->second;
    }
    auto maybeInstTy = ApplyTypeSubstForTy(typeMapping, &ty);
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

std::unordered_set<DataTy> TypeManager::GetAllCommonSuperTys(const std::unordered_set<DataTy>& tys)
{
    std::unordered_set<DataTy> supers = GetAllSuperTys(**tys.begin());
    std::unordered_set<DataTy> supersNext;
    for (auto ty : tys) {
        std::unordered_set<DataTy> supersCur;
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
    auto decl = Ty::GetDeclPtrOfTy(&nominalTy);
    auto declPtr = DynamicCast<InheritableDecl*>(decl);
    if (!declPtr) {
        return {};
    }
    TypeSubst substituteMapping = GenerateTypeMapping(*declPtr, nominalTy.TyArgs());
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
        if (!type->GetTy()->IsInterface()) {
            continue;
        }
        auto interfaceTy = RawStaticCast<InterfaceTy*>(type->DataTy());
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
                if (!type->GetTy()->IsInterface()) { // Type may be non-interface when user code is invalid.
                    continue;
                }
                auto interfaceTy = RawStaticCast<InterfaceTy*>(type->DataTy());
                auto typeMapping = GetSubstituteMapping(*interfaceTy, temp.second);
                tempDeque.emplace_back(
                    RawStaticCast<InterfaceTy*>(GetInstantiatedTy(DataTy{interfaceTy}, temp.second)), typeMapping);
            }
        }
    }
    return allSuperTys;
}

bool TypeManager::CheckExtendWithConstraint(const Ty& ty, Ptr<ExtendDecl> extend)
{
    std::vector<DataTy> tys = GetTypeArgs(ty);
    return CheckGenericDeclInstantiation(extend, tys);
}

bool TypeManager::CheckGenericDeclInstantiation(Ptr<const Decl> d, const std::vector<DataTy>& typeArgs)
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
        DataTy typeTy = GetInstantiatedTy(gc->type->DataTy(), instantiateMap);
        bool isNotGeneric = !typeTy || typeTy->kind != TypeKind::TYPE_GENERICS;
        for (const auto& upperBound : gc->upperBounds) {
            DataTy upperBoundTy = GetInstantiatedTy(upperBound->DataTy(), instantiateMap);
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

std::optional<bool> TypeManager::GetOverrideCache(const FuncDecl* src, const FuncDecl* target, DataTy baseTy,
    ModalInfo baseMode, DataTy expectInstParent, ModalInfo parentMode)
{
    OverrideOrShadowKey key(src, target, baseTy, baseMode, expectInstParent, parentMode);
    auto it = overrideOrShadowCache.find(key);
    if (it != overrideOrShadowCache.end()) {
        return it->second;
    }
    return {};
}

void TypeManager::AddOverrideCache(const FuncDecl& src, const FuncDecl& target, DataTy baseTy, ModalInfo baseMode,
    DataTy expectInstParent, ModalInfo parentMode, bool val)
{
    OverrideOrShadowKey key(&src, &target, baseTy, baseMode, expectInstParent, parentMode);
    overrideOrShadowCache.emplace(key, val);
    if (val && HasThisParam(src) && HasThisParam(target) && src.outerDecl &&
        src.outerDecl == Ty::GetDeclPtrOfTy(baseTy)) {
        UpdateTopOverriddenFuncDeclMap(&src, &target);
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
    DataTy builtInTy = GetTyForExtendMap(ty);
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

std::unordered_set<DataTy> TypeManager::GetAllExtendedBuiltIn()
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
DataTy TypeManager::GetExtendInterfaceSuperTy(ClassTy& classTy, const Ty& interfaceTy)
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
        DataTy instTy = GetInstantiatedTy(sd->DataTy(), typeMapping);
        if (instTy && IsTyExtendInterface(*instTy, interfaceTy)) {
            return instTy;
        }
        sd = sd->GetSuperClassDecl();
    }

    return nullptr;
}

DataTy TypeManager::GetRealExtendedTy(Ty& child, const Ty& interfaceTy)
{
    auto extendedTy = &child;
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

std::vector<DataTy> TypeManager::GetTypeArgs(const Ty& ty)
{
    if (!ty.IsArray()) {
        std::vector<DataTy> args{};
        for (auto t : ty.typeArgs) {
            args.push_back(t.Ty());
        }
        return args;
    }
    auto& arrayTy = static_cast<const ArrayTy&>(ty);
    if (arrayTy.dims == 1) {
        return arrayTy.TyArgs();
    }
    // Since Array<T> is a generic type, it's type argument should be array type dimension - 1 when dimension > 1.
    return {GetArrayTy(arrayTy.TyArg(0), arrayTy.dims - 1)};
}

ModalTy TypeManager::GetNonNullTy(ModalTy ty)
{
    return Ty::IsInitialTy(ty.Ty()) ? ModalTy{GetInvalidTy()} : ty;
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

DataTy TypeManager::GetTyForExtendMap(Ty& ty)
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

void TypeManager::RestoreJavaGenericsTy(Decl& decl) const
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

bool TypeManager::IsFuncDeclSubType(const FuncDecl& decl, const FuncDecl& funcDecl)
{
    auto declType = StaticCast<FuncTy>(decl.DataTy());
    auto resolvedFuncType = DynamicCast<FuncTy>(funcDecl.DataTy());
    if (resolvedFuncType && decl.identifier == funcDecl.identifier && IsFuncTySubType(*declType, *resolvedFuncType)) {
        if (TypeManager::HasThisParam(decl) && TypeManager::HasThisParam(funcDecl) &&
            GetThisParamMode(decl) != GetThisParamMode(funcDecl)) {
            return false;
        }
        return true;
    }
    return false;
}

bool TypeManager::IsFuncTySubType(const AST::FuncTy& type1, const AST::FuncTy& type2)
{
    return IsFuncParameterTypesIdentical(type1.paramTys, type2.paramTys) && IsSubtype(type1.retTy, type2.retTy);
}
#endif

bool TypeManager::IsFuncDeclEqualType(const FuncDecl& decl, const FuncDecl& funcDecl)
{
    if (HasThisParam(decl) && GetThisParamMode(decl) != GetThisParamMode(funcDecl)) {
        return false;
    }
    return IsFuncDeclSubType(decl, funcDecl) && IsFuncDeclSubType(funcDecl, decl);
}

void TypeManager::UpdateTopOverriddenFuncDeclMap(const Decl* src, const Decl* target)
{
    if (src == target) {
        return;
    }
    if (auto funcDecl = DynamicCast<FuncDecl>(src)) {
        auto& temp = overrideMap[funcDecl];
        temp.emplace_back(StaticCast<FuncDecl>(target));
    } else if (auto propDecl = DynamicCast<PropDecl>(src)) {
        auto targetPropDecl = StaticCast<PropDecl>(target);
        if (!propDecl->getters.empty() && !targetPropDecl->getters.empty()) {
            auto& temp = overrideMap[propDecl->getters.front().get()];
            temp.emplace_back(targetPropDecl->getters.front().get());
        }
        if (!propDecl->setters.empty() && !targetPropDecl->setters.empty()) {
            auto& temp = overrideMap[propDecl->setters.front().get()];
            temp.emplace_back(targetPropDecl->setters.front().get());
        }
    }
}

namespace {
Ptr<const FuncDecl> LookupTopOverriddenFromMap(
    const std::unordered_map<Ptr<const FuncDecl>, std::vector<Ptr<const FuncDecl>>>& overrideMap,
    const FuncDecl* funcDecl)
{
    const auto decls = overrideMap.find(funcDecl);
    if (decls == overrideMap.end() || decls->second.empty()) {
        return nullptr;
    }
    Ptr<const FuncDecl> ret = decls->second.front();
    std::set<Ptr<const FuncDecl>> traversed{funcDecl};
    while (true) {
        auto it = overrideMap.find(ret);
        if (it == overrideMap.end() || it->second.empty()) {
            return ret;
        }
        if (auto [_, succ] = traversed.emplace(ret); !succ) {
            // Cycle in overrideMap: keep the farthest parent visited so far.
            return ret;
        }
        ret = it->second.front();
    }
}

} // namespace

// IsImplementationFunc is not a pure read: in the generic branch it calls GetInstantiatedTy,
// whose TyInstantiator reaches TypeManager::GetTypeTy and writes the shared allocatedTys table
// (find + insert). When GetTopOverriddenFuncDecl runs concurrently (e.g. AST2CHIR), multiple
// threads could mutate allocatedTys at once. The serialization mutex overrideResolverImplMutex
// (a member of TypeManager) is held only across the IsImplementationFunc call itself, never
// across the recursive inheritance walk below, so there is no self-deadlock.
Ptr<const FuncDecl> TypeManager::FindTopOverriddenByInheritance(
    const FuncDecl& func, std::set<Ptr<const FuncDecl>>& visited)
{
    if (auto [_, inserted] = visited.emplace(&func); !inserted) {
        return nullptr;
    }
    auto outer = DynamicCast<InheritableDecl*>(func.outerDecl);
    if (!outer) {
        return nullptr;
    }
    Ptr<const FuncDecl> topMost = nullptr;
    // GetAllSuperDecls is non-const but only reads the inheritance graph.
    for (auto super : const_cast<InheritableDecl*>(outer)->GetAllSuperDecls()) {
        if (super == outer) {
            continue;
        }
        for (auto& member : super->GetMemberDecls()) {
            auto parentFunc = DynamicCast<FuncDecl*>(member.get());
            if (!parentFunc) {
                continue;
            }
            bool isImpl = false;
            {
                std::lock_guard<std::mutex> lock(overrideResolverImplMutex);
                isImpl = OverrideFunctionResolver(*this).IsImplementationFunc(func, *parentFunc);
            }
            if (!isImpl) {
                continue;
            }
            auto parentTop = FindTopOverriddenByInheritance(*parentFunc, visited);
            Ptr<const FuncDecl> candidate = parentTop ? parentTop : Ptr<const FuncDecl>(parentFunc);
            if (!topMost) {
                topMost = candidate;
            } else if (candidate->TestAttr(Attribute::ABSTRACT) && !topMost->TestAttr(Attribute::ABSTRACT)) {
                // Prefer the abstract root declaration for vtable / Invoke callee.
                topMost = candidate;
            }
        }
    }
    return topMost;
}

Ptr<const AST::FuncDecl> TypeManager::GetTopOverriddenFuncDecl(const AST::FuncDecl* funcDecl)
{
    CJC_NULLPTR_CHECK(funcDecl);

    // 1) Walk overrideMap as far as it goes. The map is often incomplete for imported
    //    decls (e.g. Expr.toTokens → Node.toTokens is recorded, but Node.toTokens →
    //    ToTokens.toTokens is not), so do not return the map result immediately.
    Ptr<const FuncDecl> fromMap = LookupTopOverriddenFromMap(overrideMap, funcDecl);
    if (!fromMap && funcDecl->genericDecl) {
        fromMap = LookupTopOverriddenFromMap(
            overrideMap, StaticCast<const FuncDecl*>(funcDecl->genericDecl));
    }

    // 2) Continue via inheritance from the furthest known ancestor (map result, else
    //    generic decl, else the input), so we still reach the true top (ToTokens.toTokens).
    const FuncDecl* seed = fromMap.get();
    if (!seed) {
        seed = funcDecl->genericDecl ? StaticCast<const FuncDecl*>(funcDecl->genericDecl) : funcDecl;
    }

    std::set<Ptr<const FuncDecl>> visited;
    if (auto top = FindTopOverriddenByInheritance(*seed, visited)) {
        return top;
    }
    if (fromMap) {
        return fromMap;
    }
    // Already the top-most function in the override chain.
    return funcDecl;
}

Ptr<Decl> TypeManager::GetOverrideDeclInClassLike(Decl& baseDecl, const FuncDecl& funcDecl, bool withAbstractOverrides)
{
    auto& decls = baseDecl.GetMemberDecls();

    for (auto& decl : decls) {
        if (decl->astKind != ASTKind::FUNC_DECL && decl->astKind != ASTKind::PROP_DECL) {
            continue;
        }
        if ((!withAbstractOverrides && decl->TestAttr(Attribute::ABSTRACT)) || decl->TestAttr(Attribute::GENERIC)) {
            continue;
        }
        if (Is<FuncDecl*>(decl.get())) {
            auto declFunc = StaticCast<FuncDecl>(decl.get());
            if (IsFuncDeclSubType(*declFunc, funcDecl)) {
                return declFunc;
            }
        } else if (auto propDecl = DynamicCast<PropDecl>(decl.get()); propDecl) {
            for (auto& tempGetFunc : propDecl->getters) {
                auto declFunc = StaticCast<FuncDecl>(tempGetFunc.get());
                if (IsFuncDeclSubType(*declFunc, funcDecl)) {
                    return declFunc;
                }
            }
            for (auto& tempSetFunc : propDecl->setters) {
                auto declFunc = StaticCast<FuncDecl>(tempSetFunc.get());
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
    auto mapping = GenerateTypeMappingByTy(r.DataTy(), l.DataTy());
    for (auto& lSuper : std::as_const(l.inheritedTypes)) {
        for (auto& rSuper : std::as_const(r.inheritedTypes)) {
            auto lTy = GetInstantiatedTy(lSuper->GetTy(), mapping);
            auto rTy = GetInstantiatedTy(rSuper->GetTy(), mapping);
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

Ptr<GenericsTy> TypeManager::AllocTyVar(const std::string& srcId, bool needSolving, Ptr<TyVar> derivedFrom)
{
    Ptr<GenericsTy> ret;
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
        constraints[ret].sum = PSet({ModalTy{GetAnyTy()}});
    }
    return ret;
}

void TypeManager::ReleaseTyVar(Ptr<GenericsTy> genTy)
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
    constraints[&tv].sum = PSet({ModalTy{GetAnyTy()}});
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

ModalTy TypeManager::InstOf(ModalTy ty)
{
    return ApplySubstPack(ty, instCtxScopes.back()->maps);
}

ModalTy TypeManager::RecoverUnivTyVar(ModalTy ty)
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

ModalTy TypeManager::ApplySubstPack(ModalTy declaredTy, const SubstPack& maps, bool ignoreUnsolved)
{
    ModalTy t1;
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

DataTy TypeManager::ApplySubstPack(DataTy declaredTy, const SubstPack& maps, bool ignoreUnsolved)
{
    auto r = ApplySubstPack(ModalTy{declaredTy}, maps, ignoreUnsolved);
    CJC_ASSERT(r.IsDataType());
    return r.Ty();
}

std::set<ModalTy> TypeManager::ApplySubstPackNonUniq(ModalTy declaredTy, const SubstPack& maps, bool ignoreUnsolved)
{
    ModalTy t1;
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

bool TypeManager::PairIsOverrideOrImpl(const Decl& child, const Decl& parent, const DataTy baseTy, ModalInfo baseMode,
    const DataTy parentTy, ModalInfo parentMode)
{
    if (child.astKind != parent.astKind || child.identifier.Val() != parent.identifier.Val()) {
        return false;
    }
    return child.astKind == ASTKind::FUNC_DECL
        ? IsOverrideOrShadow(
            *this, StaticCast<FuncDecl>(child), StaticCast<FuncDecl>(parent), baseTy, baseMode, parentTy, parentMode)
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
        bool isExtended = std::any_of(types.begin(), types.end(),
            [this, &interfaceTy](auto& type) { return IsSubtype(type->GetTy(), {&interfaceTy}); });
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
    if (member.outerDecl->astKind == ASTKind::INTERFACE_DECL && member.outerDecl->GetTy()) {
        auto extendDeclOp = GetExtendDeclByInterface(baseTy, *member.outerDecl->GetTy());
        extend = extendDeclOp.has_value() ? extendDeclOp.value() : nullptr;
    } else if (member.outerDecl->astKind == ASTKind::EXTEND_DECL) {
        extend = StaticCast<ExtendDecl>(member.outerDecl);
    }
    return extend;
}

DataTy TypeManager::AddSumByCtor(GenericsTy& tv, Ty& tyCtor, std::vector<Ptr<GenericsTy>>& tyArgs)
{
    CJC_ASSERT(tv.isPlaceholder);
    for (size_t i = tyArgs.size(); i < tyCtor.typeArgs.size(); i++) {
        tyArgs.push_back(AllocTyVar("T-Fly", true, &tv));
    }
    TypeSubst mapping;
    for (size_t i = 0; i < tyCtor.typeArgs.size(); i++) {
        mapping.emplace(StaticCast<GenericsTy*>(tyCtor.TyArg(i)), tyArgs[i]);
    }
    auto placeholderTy = GetInstantiatedTy(&tyCtor, mapping);
    constraints[&tv].sum.insert(ModalTy{placeholderTy});
    return placeholderTy;
}

ModalTy TypeManager::ConstrainByCtor(GenericsTy& tv, Ty& tyCtor)
{
    CJC_ASSERT(tv.isPlaceholder);
    TypeSubst mapping;
    for (auto ub : constraints[&tv].ubs) {
        if (OfSameCtor(ub.Ty(), &tyCtor)) {
            return ub;
        }
    }
    for (auto tyArg : tyCtor.typeArgs) {
        mapping.emplace(StaticCast<GenericsTy*>(tyArg.Ty()), AllocTyVar("T-Fly", true, &tv));
    }
    auto placeholderTy = GetInstantiatedTy(&tyCtor, mapping);
    if (IsSubtype(&tv, placeholderTy, true, false)) {
        return {placeholderTy};
    } else {
        return {nullptr};
    }
}

bool TypeManager::OfSameCtor(DataTy ty, DataTy tyCtor)
{
    if (!Ty::IsTyCorrect(ty) || !Ty::IsTyCorrect(tyCtor)) {
        return false;
    }
    if (ty->typeArgs.size() != tyCtor->typeArgs.size()) {
        return false;
    }
    TypeSubst m;
    for (size_t i = 0; i < tyCtor->typeArgs.size(); i++) {
        m[StaticCast<TyVar*>(tyCtor->typeArgs[i].Ty())] = ty->typeArgs[i].Ty();
    }
    return GetInstantiatedTy(tyCtor, m) == ty;
}

namespace {
TypeSubst GetGreedySubst(Constraint& cst)
{
    TypeSubst m;
    for (auto& [tv, bound] : cst) {
        if (!bound.eq.empty()) {
            m.emplace(tv, bound.eq.begin()->Ty());
        }
    }
    return m;
}
}

ModalTy TypeManager::TryGreedySubst(ModalTy ty)
{
    if (ty.IsCorrect() && ty->HasPlaceholder()) {
        return GetInstantiatedTy(ty, GetGreedySubst(constraints));
    }
    return ty;
}

bool TypeManager::TyVarHasNoSum(TyVar& tv) const
{
    return constraints.count(&tv) > 0 && constraints.at(&tv).sum.size() == 1 &&
        (*constraints.at(&tv).sum.cbegin())->IsAny();
}

Ptr<Decl> TypeManager::GetDummyBuiltInDecl(DataTy ty)
{
    if (dummyBuiltInDecls.count(ty) == 0) {
        dummyBuiltInDecls.emplace(ty, MakeOwnedNode<Decl>());
        dummyBuiltInDecls[ty]->SetTy(ty);
    }
    return dummyBuiltInDecls[ty].get();
}

ModalTy TypeManager::GetThisRealTy(ModalTy now)
{
    if (!now.IsCorrect()) {
        return now;
    }
    if (auto thisTy = DynamicCast<ClassThisTy*>(now.Ty())) {
        return {GetClassTy(*thisTy->declPtr, thisTy->TyArgs()), now.Mode()};
    }
    return now;
}

ModalTy TypeManager::ReplaceThisTy(ModalTy now)
{
    if (auto thisTy = DynamicCast<ClassThisTy>(now.Ty())) {
        std::vector<DataTy> newArgs;
        for (auto& arg : thisTy->typeArgs) {
            newArgs.emplace_back(ReplaceThisTy(arg).Ty());
        }
        return {GetClassTy(*thisTy->declPtr, newArgs), now.Mode()};
    }
    if (now->typeArgs.empty()) {
        return now;
    }
    std::vector<DataTy> newArgs;
    newArgs.reserve(now->typeArgs.size());
    bool changed = false;
    for (auto& arg : now->typeArgs) {
        auto replaced = ReplaceThisTy(arg);
        newArgs.emplace_back(replaced.Ty());
        if (replaced != arg) {
            changed = true;
        }
    }
    if (!changed) {
        return now;
    }
    switch (now.Kind()) {
        case TypeKind::TYPE_FUNC: {
            auto& funcTy = static_cast<FuncTy&>(*now);
            std::vector<ModalTy> paramTys;
            for (auto& p : funcTy.paramTys) {
                paramTys.push_back(ReplaceThisTy(p));
            }
            auto retTy = ReplaceThisTy(funcTy.retTy);
            return {
                GetFunctionTy(paramTys, retTy, {funcTy.isC, funcTy.isClosureTy, funcTy.hasVariableLenArg}), now.Mode()};
        }
        case TypeKind::TYPE_TUPLE:
            return {GetTupleTy(newArgs, static_cast<TupleTy&>(*now).isClosureTy), now.Mode()};
        case TypeKind::TYPE_ARRAY:
            return {GetArrayTy(newArgs[0], static_cast<ArrayTy&>(*now).dims), now.Mode()};
        case TypeKind::TYPE_POINTER:
            return {GetPointerTy(newArgs[0]), now.Mode()};
        case TypeKind::TYPE_STRUCT:
            return {GetStructTy(*static_cast<StructTy&>(*now).declPtr, newArgs), now.Mode()};
        case TypeKind::TYPE_CLASS:
            return {GetClassTy(*static_cast<ClassTy&>(*now).declPtr, newArgs), now.Mode()};
        case TypeKind::TYPE_INTERFACE:
            return {GetInterfaceTy(*static_cast<InterfaceTy&>(*now).declPtr, newArgs), now.Mode()};
        case TypeKind::TYPE_ENUM:
            return {GetEnumTy(*static_cast<EnumTy&>(*now).declPtr, newArgs), now.Mode()};
        case TypeKind::TYPE:
            return {GetTypeAliasTy(*static_cast<TypeAliasTy&>(*now).declPtr, newArgs), now.Mode()};
        default:
            return now;
    }
}

// Create semantic type by substituting type arguments into base type.
ModalTy TypeManager::SubstituteTypeArgs(ModalTy baseTy, std::vector<ModalTy>& typeArgs)
{
    if (!baseTy.IsCorrect()) {
        return {TypeManager::GetInvalidTy(), baseTy.Mode()};
    }
    DataTy base = baseTy.Ty();
    if (base->kind == TypeKind::TYPE_FUNC) {
        auto returnTy = typeArgs.back();
        typeArgs.pop_back();
        auto funcTy = StaticCast<FuncTy>(base);
        return {GetFunctionTy(typeArgs, returnTy, {funcTy->isC, false, funcTy->hasVariableLenArg}), baseTy.Mode()};
    }
    std::vector<DataTy> dataArgs;
    for (auto ty : typeArgs) {
        dataArgs.push_back(ty.Ty());
    }
    return SubstituteTypeArgs(baseTy, dataArgs);
}

ModalTy TypeManager::SubstituteTypeArgs(ModalTy baseTy, std::vector<DataTy>& typeArgs)
{
    if (!baseTy.IsCorrect()) {
        return {TypeManager::GetInvalidTy(), baseTy.Mode()};
    }
    DataTy base = baseTy.Ty();
    switch (base->kind) {
        case TypeKind::TYPE_CLASS: {
            if (auto ctt = DynamicCast<ClassThisTy>(base)) {
                return {GetClassThisTy(*ctt->declPtr, typeArgs), baseTy.Mode()};
            }
            return {GetClassTy(*StaticCast<ClassTy>(base)->declPtr, typeArgs), baseTy.Mode()};
        }
        case TypeKind::TYPE_STRUCT: {
            return {GetStructTy(*StaticCast<StructTy>(base)->declPtr, typeArgs), baseTy.Mode()};
        }
        case TypeKind::TYPE_INTERFACE: {
            return {GetInterfaceTy(*StaticCast<InterfaceTy>(base)->declPtr, typeArgs), baseTy.Mode()};
        }
        case TypeKind::TYPE_ENUM: {
            return {GetEnumTy(*StaticCast<EnumTy>(base)->declPtr, typeArgs), baseTy.Mode()};
        }
        case TypeKind::TYPE_FUNC: {
            CJC_ASSERT(false && "SubstituteTypeArgs(DataTy): FuncTy requires ModalTy arguments");
            return baseTy;
        }
        case TypeKind::TYPE: {
            return baseTy;
        }
        case TypeKind::TYPE_TUPLE: {
            return {GetTupleTy(typeArgs, false), baseTy.Mode()};
        }
        case TypeKind::TYPE_ARRAY: {
            auto arrayTy = StaticCast<ArrayTy>(base);
            return {GetArrayTy(typeArgs[0], arrayTy->dims), baseTy.Mode()};
        }
        case TypeKind::TYPE_VARRAY: {
            auto varrayTy = StaticCast<VArrayTy>(base);
            return {GetVArrayTy(*typeArgs[0], varrayTy->size), baseTy.Mode()};
        }
        case TypeKind::TYPE_POINTER: {
            return {GetPointerTy(typeArgs[0]), baseTy.Mode()};
        }
        default:
            return baseTy;
    }
}

ModalTy TypeManager::ObtainsAliasTypeOfRefType(Ptr<const RefType> rt)
{
    if (rt->GetTy()->IsCFunc()) {
        CJC_ASSERT(rt->typeArguments.size() == 1);
        auto funcType = DynamicCast<FuncType>(rt->typeArguments[0].get());
        CJC_NULLPTR_CHECK(funcType);
        funcType->isC = true;
        return ObtainsAliasType(funcType);
    }
    // 'This' type cannot use alias types. And typeArguments always be empty for 'This' type.
    if (DynamicCast<ClassThisTy>(rt->DataTy())) {
        return rt->GetTy();
    }
    std::vector<DataTy> typeArgs;
    for (auto& typeArg : rt->typeArguments) {
        typeArgs.emplace_back(ObtainsAliasType(typeArg.get()).Ty());
    }
    return SubstituteTypeArgs(rt->GetTy(), typeArgs);
}

ModalTy TypeManager::ObtainsAliasTypeOfFuncDecl(Ptr<const FuncDecl> fd)
{
    auto& fb = fd->funcBody;
    std::vector<ModalTy> typeArgs;
    for (auto& param : fb->paramLists[0]->params) {
        typeArgs.emplace_back(ObtainsAliasType(param.get()));
    }
    if (fb->retType) {
        typeArgs.emplace_back(ObtainsAliasType(fb->retType.get()));
    } else {
        auto funcTy = StaticCast<FuncTy>(fd->DataTy());
        typeArgs.emplace_back(funcTy->retTy.Ty());
    }
    return SubstituteTypeArgs(fd->GetTy(), typeArgs);
}

ModalTy TypeManager::ObtainsAliasType(Ptr<const Node> node)
{
    if (node == nullptr || !node->GetTy().IsCorrect()) {
        return {TypeManager::GetInvalidTy(), node ? node->TyMode() : ModalInfo{}};
    }
    // Return type create by compiler will be ignored.
    if (node->TestAttr(Attribute::COMPILER_ADD)) {
        return node->GetTy();
    }
    if (auto typeNode = DynamicCast<Type>(node); typeNode && !Ty::IsInitialTy(typeNode->aliasTy)) {
        auto tad = Ty::GetDeclPtrOfTy(typeNode->aliasTy);
        CJC_NULLPTR_CHECK(tad);
        // If typeAlias is not exported, give up replasement. The current semantics allow an exteranl declaration to use
        // internal alias declaration.
        if (!tad->IsExportedDecl() && !tad->TestAttr(Attribute::IMPLICIT_USED)) {
            return node->GetTy();
        }
        return typeNode->aliasTy;
    }
    ModalTy ret = {TypeManager::GetInvalidTy(), node->TyMode()};
    switch (node->astKind) {
        case ASTKind::REF_TYPE: {
            ret = ObtainsAliasTypeOfRefType(StaticCast<RefType>(node));
            break;
        }
        case ASTKind::PAREN_TYPE: {
            ret = ObtainsAliasType(StaticCast<ParenType>(node)->type.get());
            break;
        }
        case ASTKind::VARRAY_TYPE: {
            auto vat = StaticCast<VArrayType>(node);
            std::vector<DataTy> typeArgs;
            typeArgs.emplace_back(ObtainsAliasType(vat->typeArgument).Ty());
            ret = SubstituteTypeArgs(node->GetTy(), typeArgs);
            break;
        }
        case ASTKind::FUNC_TYPE: {
            auto ft = StaticCast<FuncType>(node);
            auto fTy = StaticCast<FuncTy>(ft->DataTy());
            std::vector<ModalTy> params;
            for (auto& param : ft->paramTypes) {
                params.emplace_back(ObtainsAliasType(param.get()));
            }
            auto retTy = ObtainsAliasType(ft->retType.get());
            ret = {GetFunctionTy(params, retTy, {ft->isC, false, fTy->hasVariableLenArg}), node->TyMode()};
            break;
        }
        case ASTKind::TUPLE_TYPE: {
            auto tt = StaticCast<TupleType>(node);
            std::vector<DataTy> typeArgs;
            for (auto& field : tt->fieldTypes) {
                typeArgs.emplace_back(ObtainsAliasType(field.get()).Ty());
            }
            ret = SubstituteTypeArgs(node->GetTy(), typeArgs);
            break;
        }
        case ASTKind::VAR_DECL:
        case ASTKind::FUNC_PARAM:
        case ASTKind::VAR_WITH_PATTERN_DECL: {
            auto vda = StaticCast<VarDeclAbstract>(node);
            ret = vda->type ? ObtainsAliasType(vda->type) : node->GetTy();
            break;
        }
        case ASTKind::FUNC_DECL: {
            ret = ObtainsAliasTypeOfFuncDecl(StaticCast<FuncDecl>(node));
            break;
        }
        case ASTKind::EXTEND_DECL: {
            auto ed = StaticCast<ExtendDecl>(node);
            ret = ObtainsAliasType(ed->extendedType.get());
            break;
        }
        default:
            ret = node->GetTy();
            break;
    }
    return ret;
}

std::vector<ModalTy> TypeManager::RecursiveSubstituteTypeAliasInTy(
    Ptr<const Ty> ty, bool needSubstituteGeneric, const TypeSubst& typeMapping)
{
    CJC_ASSERT(ty); // Caller guarantees;
    std::vector<ModalTy> typeArgs;
    for (const auto& typeArg : ty->typeArgs) {
        CJC_ASSERT(typeArg);
        if (typeArg.IsCorrect() || needSubstituteGeneric) {
            typeArgs.push_back(SubstituteTypeAliasInTy(typeArg, needSubstituteGeneric, typeMapping));
        } else {
            typeArgs.push_back(typeArg);
        }
    }
    return typeArgs;
}

DataTy TypeManager::GetUnaliasedTypeFromTypeAlias(const TypeAliasTy& target, const std::vector<DataTy>& typeArgs,
    bool needSubstituteGeneric, const TypeSubst& customMapping)
{
    CJC_NULLPTR_CHECK(target.declPtr);
    auto tad = target.declPtr;
    if (tad->TestAttr(Attribute::IN_REFERENCE_CYCLE)) {
        return tad->DataTy();
    }
    CJC_NULLPTR_CHECK(tad->type);
    auto aliasedType = tad->type.get();
    TypeSubst typeMapping = GenerateTypeMapping(*tad, typeArgs);
    ModalTy instAliasedTy = GetInstantiatedTy(aliasedType->GetTy(), typeMapping);
    // If the aliased type is still a type alias or contain a alias type, perform recursive substitution.
    if (!Ty::IsInitialTy(aliasedType->aliasTy) || instAliasedTy->kind == TypeKind::TYPE) {
        return SubstituteTypeAliasInTy(instAliasedTy, needSubstituteGeneric, customMapping).Ty();
    }
    return instAliasedTy.Ty();
}

ModalTy TypeManager::SubstituteTypeAliasInTy(ModalTy ty, bool needSubstituteGeneric, const TypeSubst& typeMapping)
{
    if (!Ty::IsTyCorrect(ty)) {
        return {TypeManager::GetInvalidTy(), ty.Mode()};
    }
    Ty& raw = *ty.Ty();
    if (raw.kind <= TypeKind::TYPE_BOOLEAN) {
        return {&raw, ty.Mode()};
    }
    std::vector<ModalTy> typeArgs = RecursiveSubstituteTypeAliasInTy(&raw, needSubstituteGeneric, typeMapping);
    std::vector<DataTy> dataArgs;
    dataArgs.reserve(typeArgs.size());
    for (const auto& m : typeArgs) {
        dataArgs.push_back(m.Ty());
    }
    switch (raw.kind) {
        case TypeKind::TYPE_CLASS: {
            if (auto ctt = DynamicCast<ClassThisTy*>(&raw); ctt) {
                return {GetClassThisTy(*ctt->declPtr, dataArgs), ty.Mode()};
            }
            return {GetClassTy(*static_cast<ClassTy&>(raw).declPtr, dataArgs), ty.Mode()};
        }
        case TypeKind::TYPE_STRUCT: {
            return {GetStructTy(*static_cast<StructTy&>(raw).declPtr, dataArgs), ty.Mode()};
        }
        case TypeKind::TYPE_INTERFACE: {
            return {GetInterfaceTy(*static_cast<InterfaceTy&>(raw).declPtr, dataArgs), ty.Mode()};
        }
        case TypeKind::TYPE_ENUM: {
            return {GetEnumTy(*static_cast<EnumTy&>(raw).declPtr, dataArgs), ty.Mode()};
        }
        case TypeKind::TYPE_FUNC: {
            auto returnTy = typeArgs.back();
            typeArgs.pop_back();
            auto& funcTy = static_cast<FuncTy&>(raw);
            return {GetFunctionTy(typeArgs, returnTy, {funcTy.isC, false, funcTy.hasVariableLenArg}), ty.Mode()};
        }
        case TypeKind::TYPE: {
            return {GetUnaliasedTypeFromTypeAlias(
                static_cast<TypeAliasTy&>(raw), dataArgs, needSubstituteGeneric, typeMapping),
                ty.Mode()};
        }
        case TypeKind::TYPE_TUPLE: {
            return {GetTupleTy(dataArgs, false), ty.Mode()};
        }
        case TypeKind::TYPE_ARRAY: {
            auto& arrayTy = static_cast<ArrayTy&>(raw);
            return {GetArrayTy(dataArgs[0], arrayTy.dims), ty.Mode()};
        }
        case TypeKind::TYPE_VARRAY: {
            auto& varrayTy = static_cast<VArrayTy&>(raw);
            CJC_ASSERT(!dataArgs.empty() && dataArgs[0] != nullptr);
            return {GetVArrayTy(*dataArgs[0], varrayTy.size), ty.Mode()};
        }
        case TypeKind::TYPE_POINTER: {
            return {GetPointerTy(dataArgs[0]), ty.Mode()};
        }
        case TypeKind::TYPE_GENERICS: {
            if (!needSubstituteGeneric) {
                return {&raw, ty.Mode()};
            }
            auto found = typeMapping.find(StaticCast<GenericsTy*>(&raw));
            if (found != typeMapping.end()) {
                return {found->second, ty.Mode()};
            }
            // This type will not be used, just for placeholder and marking current is substituted with typealias.
            return {GetIntersectionTy({{&raw, {}}}), ty.Mode()};
        }
        default:
            return {&raw, ty.Mode()};
    }
}

template IntersectionTy* TypeManager::GetTypeTy<IntersectionTy, std::set<DataTy>&>(std::set<DataTy>&);
template UnionTy* TypeManager::GetTypeTy<UnionTy, std::set<DataTy>&>(std::set<DataTy>&);
} // namespace Cangjie
