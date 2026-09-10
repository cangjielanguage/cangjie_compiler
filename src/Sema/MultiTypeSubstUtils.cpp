// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the utility functions for manipulating 'MultiTypeSubst'
 */

#include "TypeCheckUtil.h"
#include "cangjie/Sema/TypeManager.h"

#include <unordered_set>

namespace Cangjie::TypeCheckUtil {
using namespace AST;

namespace {
std::set<Ptr<Ty>> GetDirectMappingTys(
    Ptr<TyVar> const tyVar, const MultiTypeSubst& mts)
{
    std::vector<Ptr<TyVar>> stack{tyVar};
    std::set<Ptr<Ty>> res;
    auto mapping = mts;
    while (!stack.empty()) {
        auto curTy = stack.back();
        stack.pop_back();
        MultiTypeSubst::const_iterator found = mapping.find(curTy);
        if (found == mapping.cend()) {
            continue;
        }
        auto targetTys = found->second;
        mapping.erase(found->first); // Erase current substitution from mapping, avoid circular substitution.
        targetTys.erase(curTy);      // Ignore self mapping.
        if (targetTys.empty()) {
            continue;
        }
        for (auto ty : targetTys) {
            if (auto genTy = DynamicCast<TyVar*>(ty)) {
                stack.emplace_back(genTy);
            }
        }
        // Update result set.
        res.erase(curTy);
        res.insert(targetTys.begin(), targetTys.end());
    }
    return res;
}

// remove mappings not useful(either directly or transitively used) in tys
SubstPack FilterUnusedMapping(const SubstPack& mapping, const std::set<Ptr<Ty>>& tys)
{
    std::set<Ptr<TyVar>> allu;
    std::set<Ptr<TyVar>> alli;
    std::set<Ptr<TyVar>> reachable;
    std::queue<Ptr<TyVar>> worklist;
    // collect all tyvars
    for (auto [tvu, tvi] : mapping.u2i) {
        allu.insert(tvu);
        alli.emplace(StaticCast<TyVar*>(tvi));
    }
    // collect inst tyvars directly used
    for (auto ty : tys) {
        for (auto tvu : ty->GetGenericTyArgs(allu)) {
            reachable.emplace(StaticCast<TyVar*>(mapping.u2i.at(tvu)));
        }
    }
    for (auto tvi : reachable) {
        worklist.push(tvi);
    }
    // collect inst tyvars indirected used
    while (!worklist.empty()) {
        auto tvi = worklist.front();
        worklist.pop();
        if (mapping.inst.count(tvi) > 0) {
            for (auto instTy : mapping.inst.at(tvi)) {
                reachable.merge(instTy->GetGenericTyArgs(alli));
            }
        }
    }
    // return useful mappings
    SubstPack ret;
    for (auto [tvu, tvi] : mapping.u2i) {
        if (reachable.count(StaticCast<TyVar*>(tvi)) > 0) {
            ret.u2i[tvu] = tvi;
        }
    }
    for (auto [tvi, instTys] : mapping.inst) {
        if (reachable.count(tvi) > 0) {
            ret.inst[tvi] = instTys;
        }
    }
    return ret;
}

MultiTypeSubst FilterUnusedMapping(const MultiTypeSubst& mapping, const std::set<Ptr<Ty>>& tys)
{
    std::set<Ptr<TyVar>> all = Utils::GetKeys(mapping);
    std::set<Ptr<TyVar>> reachable;
    std::queue<Ptr<TyVar>> worklist;
    // collect tyvars directly used
    std::vector<Ptr<Ty>> dataTys;
    for (auto ty : tys) {
        dataTys.emplace_back(ty);
    }
    for (auto ty : dataTys) {
        auto a = ty->GetGenericTyArgs(all);
        for (auto b : a) {
            reachable.insert(StaticCast<TyVar>(b));
        }
    }
    for (auto tv : reachable) {
        worklist.push(tv);
    }
    // collect inst tyvars indirected used
    while (!worklist.empty()) {
        auto tv = worklist.front();
        worklist.pop();
        if (mapping.count(tv) > 0) {
            for (auto instTy : mapping.at(tv)) {
                auto a = instTy->GetGenericTyArgs(all);
                for (auto b : a) {
                    reachable.insert(StaticCast<TyVar>(b));
                }
            }
        }
    }
    // return useful mappings
    MultiTypeSubst ret;
    for (auto [tv, instTys] : mapping) {
        if (reachable.count(tv) > 0) {
            ret[tv] = instTys;
        }
    }
    return ret;
}

std::set<TypeSubst> ExpandFilteredMultiTypeSubst(const MultiTypeSubst& mts)
{
    if (mts.empty()) {
        return {{}}; // If TypeSubst is empty, must return set with empty TypeSubst.
    }

    std::set<TypeSubst> res;
    std::vector<Ptr<TyVar>> keys;
    std::for_each(mts.begin(), mts.end(), [&keys](auto& it) { keys.emplace_back(it.first); });
    size_t numKeys = keys.size();
    // Expand every type mapping possibilities.
    std::function<void(TypeSubst&)> expand = [&expand, &mts, &numKeys, &keys, &res](TypeSubst& mapping) {
        size_t cur = mapping.size();
        auto key = keys[cur];
        for (auto& it : mts.at(key)) {
            mapping[key] = it;
            if (cur + 1 == numKeys) {
                res.emplace(mapping);
            } else {
                expand(mapping);
            }
            mapping.erase(key);
        }
    };
    TypeSubst mapping;
    expand(mapping);

    return res;
}
} // namespace

TypeSubst MultiTypeSubstToTypeSubst(const MultiTypeSubst& mts)
{
    TypeSubst m;
    std::for_each(mts.cbegin(), mts.cend(), [&m](auto& kv) {
        auto values = kv.second;
        if (values.size() > 1) {
            values.erase(kv.first); // Avoid choosing self mapping when there are more than one candidates.
        }
        m[kv.first] = *values.begin();
    });
    return m;
}

std::vector<Ptr<Ty>> GetDeclTypeParams(const Decl& decl)
{
    if (decl.astKind == ASTKind::EXTEND_DECL) {
        CJC_NULLPTR_CHECK(decl.GetTy());
        return decl.GetTy()->TyArgs();
    }
    std::vector<Ptr<Ty>> ret;
    auto generic = decl.GetGeneric();
    if (!generic) {
        return ret;
    }
    for (auto& it : generic->typeParameters) {
        ret.emplace_back(it->DataTy());
    }
    return ret;
}

std::unordered_set<Ptr<Ty>> GetAllGenericTys(Ptr<Ty> ty)
{
    std::unordered_set<Ptr<Ty>> res;
    std::unordered_set<Ptr<Ty>> visited;
    std::queue<Ptr<Ty>> q;
    q.emplace(ty);
    while (!q.empty()) {
        auto curTy = q.front();
        q.pop();
        if (auto [_, succ] = visited.emplace(curTy); !succ) {
            continue;
        }
        if (curTy->IsGeneric()) {
            res.emplace(curTy);
            continue;
        }
        for (auto it : curTy->typeArgs) {
            q.emplace(it.Ty());
        }
    }
    return res;
}

MultiTypeSubst ReduceMultiTypeSubst(TypeManager& tyMgr, const TyVars& tyVars,
    const MultiTypeSubst& mts)
{
    if (tyVars.empty()) {
        return {};
    }
    auto mapping = mts;
    // Erase self-reference mappings.
    std::unordered_set<Ptr<Ty>> visited;
    for (auto& p : mts) {
        for (Ptr<Ty> ty : p.second) {
            if (auto [_, succ] = visited.emplace(ty); !succ) {
                continue;
            }
            auto gtys = GetAllGenericTys(ty);
            if (gtys.find(p.first) != gtys.cend()) {
                mapping.erase(p.first);
            }
        }
    }
    MultiTypeSubst res;
    for (auto tyVar : tyVars) {
        auto targetTys = GetDirectMappingTys(tyVar, mts);
        mapping.erase(tyVar); // Erase current substitution from mapping.
        if (!targetTys.empty()) {
            res.emplace(tyVar, targetTys);
        }
    }
    for (auto& it : res) {
        std::set<Ptr<Ty>> targetRes;
        for (auto& ty : it.second) {
            targetRes.merge(tyMgr.GetInstantiatedTys(ty, mapping));
        }
        it.second = targetRes;
    }
    return res;
}

std::vector<SubstPack> ExpandMultiTypeSubst(const SubstPack& maps, const std::set<DataTy>& usefulTys)
{
    std::vector<SubstPack> ret;
    auto filtered = FilterUnusedMapping(maps, usefulTys);
    for (auto m : ExpandFilteredMultiTypeSubst(filtered.inst)) {
        SubstPack mp;
        mp.u2i = filtered.u2i;
        MergeTypeSubstToMultiTypeSubst(mp.inst, m);
        ret.push_back(mp);
    }
    return ret;
}

std::set<TypeSubst> ExpandMultiTypeSubst(const MultiTypeSubst& mts, const std::set<DataTy>& usefulTys)
{
    auto filtered = FilterUnusedMapping(mts, usefulTys);
    return ExpandFilteredMultiTypeSubst(filtered);
}

std::set<TypeSubst> ExpandMultiTypeSubst(
    TypeManager& tyMgr, const MultiTypeSubst& mts, const std::set<DataTy>& usefulTys)
{
    std::unordered_set<Ptr<Ty>> allGeneric;
    for (auto ty : usefulTys) {
        if (ty) {
            allGeneric.merge(GetAllGenericTys(ty));
        }
    }
    TyVars tyVarsForInst = StaticToTyVars(allGeneric);
    MultiTypeSubst reducedMs = ReduceMultiTypeSubst(tyMgr, tyVarsForInst, mts);
    return ExpandMultiTypeSubst(reducedMs, usefulTys);
}

Ptr<Ty> GetMappedTy(const MultiTypeSubst& mts, TyVar* tyVar)
{
    auto found = mts.find(tyVar);
    if (found != mts.end()) {
        for (auto ty : found->second) {
            if (ty != tyVar) {
                return ty;
            }
        }
    }
    return tyVar;
}

Ptr<Ty> GetMappedTy(const TypeSubst& typeMapping, TyVar* tyVar)
{
    auto found = typeMapping.find(tyVar);
    if (found != typeMapping.end()) {
        return found->second;
    }
    return tyVar;
}

namespace {
std::vector<Ptr<Ty>> GetTypeArgTys(const Ty& ty)
{
    std::vector<Ptr<Ty>> tys;
    tys.reserve(ty.typeArgs.size());
    for (const auto& arg : ty.typeArgs) {
        tys.emplace_back(arg.Ty());
    }
    return tys;
}

void InsertInstMapping(TypeManager& tyMgr, SubstPack& m, GenericsTy& genTy, DataTy instTy)
{
    CJC_ASSERT(!genTy.isPlaceholder);
    if (m.u2i.count(&genTy) == 0) {
        m.u2i[&genTy] = tyMgr.AllocTyVar();
    }
    m.inst[StaticCast<TyVar*>(m.u2i[&genTy])].emplace(instTy);
}

TypeSubst GenerateTypeMappingByArgs(const std::vector<Ptr<Ty>>& srcArgs, const std::vector<Ptr<Ty>>& instantiateArgs)
{
    if (srcArgs.size() != instantiateArgs.size()) {
        return {};
    }
    TypeSubst typeMapping;
    for (size_t i = 0; i < srcArgs.size(); ++i) {
        if (auto genTy = DynamicCast<GenericsTy*>(srcArgs[i])) {
            typeMapping.emplace(genTy, instantiateArgs[i]);
        } else {
            if (srcArgs[i]->kind != instantiateArgs[i]->kind ||
                Ty::GetDeclPtrOfTy(srcArgs[i]) != Ty::GetDeclPtrOfTy(instantiateArgs[i])) {
                continue;
            }
            typeMapping.merge(
                GenerateTypeMappingByArgs(GetTypeArgTys(*srcArgs[i]), GetTypeArgTys(*instantiateArgs[i])));
        }
    }
    return typeMapping;
}

/**
 * Find mappings from partially instantiated ty args to fully instantiated ty args, recursively
 * e.g.
 * class C<T1, T2> {}
 * extend<R1, R2> C<R1, Array<R2>> {}
 * let c = C<String, Array<Int>>()
 *
 * for `C<Int, Array<Int>>` against the extension, the inputs are:
 * srcArgs: [R1, Array<R2>]
 * instantiateArgs: [String, Array<Int>]
 * The resulting maps:
 * u2i:  [R1 |-> R1', R2 |-> R2']
 * inst: [R1' |-> String, R2' |-> Int]
 */
void GenerateTypeMappingByArgs(
    TypeManager& tyMgr, SubstPack& m, const std::vector<Ptr<Ty>>& srcArgs, const std::vector<Ptr<Ty>>& instantiateArgs)
{
    if (srcArgs.size() != instantiateArgs.size()) {
        return;
    }
    TypeSubst typeMapping;
    for (size_t i = 0; i < srcArgs.size(); ++i) {
        if (auto genTy = DynamicCast<GenericsTy*>(srcArgs[i])) {
            InsertInstMapping(tyMgr, m, *genTy, instantiateArgs[i]);
        } else {
            if (srcArgs[i]->kind != instantiateArgs[i]->kind ||
                Ty::GetDeclPtrOfTy(srcArgs[i]) != Ty::GetDeclPtrOfTy(instantiateArgs[i])) {
                continue;
            }
            GenerateTypeMappingByArgs(tyMgr, m, GetTypeArgTys(*srcArgs[i]), GetTypeArgTys(*instantiateArgs[i]));
        }
    }
}
} // namespace

TypeSubst GenerateTypeMappingByTy(const Ptr<Ty> genericTy, const Ptr<Ty> instantTy)
{
    if (!genericTy || !instantTy) {
        return {};
    }
    if (!genericTy->IsGeneric() &&
        (genericTy->kind != instantTy->kind || Ty::GetDeclPtrOfTy(genericTy) != Ty::GetDeclPtrOfTy(instantTy))) {
        return {};
    }
    return GenerateTypeMappingByArgs({genericTy}, {instantTy});
}

/**
 * Generate type mapping **directly** from the decl to the given type args.
 * If the decl is an extension, then it's the mapping from the extended type to the type args.
 * It doesn't include mapping from the decl of the extended type to the extension, or mapping
 * for the entire inheritance chain.
 * See `GenerateTypeMappingByArgs` for an example.
 */
void GenerateTypeMapping(TypeManager& tyMgr, SubstPack& m, const Decl& decl, const std::vector<Ptr<Ty>>& typeArgs)
{
    auto generic = decl.GetGeneric();
    if (!generic) {
        return;
    }
    if (decl.astKind == ASTKind::EXTEND_DECL) {
        auto extendedTypeArgs = StaticCast<ExtendDecl>(decl).extendedType->GetTy()->TyArgs();
        GenerateTypeMappingByArgs(tyMgr, m, extendedTypeArgs, typeArgs);
        return;
    }
    if (generic->typeParameters.size() == typeArgs.size()) {
        for (size_t i = 0; i < typeArgs.size(); ++i) {
            if (generic->typeParameters[i]->GetTy().IsCorrect() && Ty::IsTyCorrect(typeArgs[i])) {
                auto genTy = StaticCast<TyVar>(generic->typeParameters[i]->DataTy());
                InsertInstMapping(tyMgr, m, *genTy, typeArgs[i]);
            }
        }
    }
}

/**
 * Given an extend whose generic parameter's mapping to instantiated types already exists in `m`,
 * generated type mapping from the original type decl to this extend, using the inst ty vars for the extendedType.
 *
 * E.g, given:
 * class A<T> {}
 * extend<R> A<Option<R>> {}
 *
 * Will newly generate:
 * u2i: [T |-> T']
 * inst: [T' |-> Option<R'>]
 */
void RelayMappingFromExtendToExtended(TypeManager& tyMgr, SubstPack& m, const ExtendDecl& decl)
{
    auto target = decl.extendedType->GetTarget();
    if (!target) {
        return;
    }
    auto originalTypeArgs = GetRealTarget(target)->GetTy()->TyArgs();
    auto extendedTypeArgsInst = decl.extendedType->GetTy()->TyArgs();
    for (auto& ty : extendedTypeArgsInst) {
        ty = tyMgr.GetInstantiatedTy(ty, m.u2i);
    }
    GenerateTypeMappingByArgs(tyMgr, m, originalTypeArgs, extendedTypeArgsInst);
}

TypeSubst GenerateTypeMapping(const Decl& decl, const std::vector<Ptr<Ty>>& typeArgs)
{
    TypeSubst substituteMapping;
    auto generic = decl.GetGeneric();
    if (!generic) {
        return substituteMapping;
    }
    if (decl.astKind == ASTKind::EXTEND_DECL) {
        auto extendedTypeArgs = StaticCast<ExtendDecl>(decl).extendedType->GetTy()->TyArgs();
        return GenerateTypeMappingByArgs(extendedTypeArgs, typeArgs);
    }
    if (generic->typeParameters.size() == typeArgs.size()) {
        for (size_t i = 0; i < typeArgs.size(); ++i) {
            if (generic->typeParameters[i]->GetTy().IsCorrect() && Ty::IsTyCorrect(typeArgs[i])) {
                // could be used by instantiated decl, therefore need to check
                if (auto declGenParam = DynamicCast<TyVar>(generic->typeParameters[i]->DataTy())) {
                    substituteMapping[declGenParam] = typeArgs[i];
                }
            }
        }
    }
    return substituteMapping;
}

TypeSubst InverseMapping(const TypeSubst& typeMapping)
{
    TypeSubst inversed;
    for (auto [from, to] : typeMapping) {
        if (auto genTo = DynamicCast<TyVar*>(to)) {
            inversed.emplace(genTo, from);
        }
    }
    return inversed;
}

void MergeTypeSubstToMultiTypeSubst(MultiTypeSubst& mts, const TypeSubst& typeMapping)
{
    for (auto it : typeMapping) {
        mts[it.first].emplace(it.second);
    }
}

void MergeMultiTypeSubsts(MultiTypeSubst& target, const MultiTypeSubst& src)
{
    for (auto it : src) {
        if (it.second.empty()) {
            continue;
        }
        target[it.first].merge(it.second);
    }
}

bool HaveCyclicSubstitution(TypeManager& tyMgr, const TypeSubst& typeMapping)
{
    auto keys = Utils::GetKeys(typeMapping);
    for (auto tyVar : keys) {
        auto mapping = typeMapping;
        auto target = mapping[tyVar];
        (void)mapping.erase(tyVar);
        auto substitutedTy = tyMgr.GetInstantiatedTy(target, mapping);
        // eg: {X->Y, Y->E<X>} will generate 'X->E<X>' which may cause infinite substitution.
        bool recursived = substitutedTy != tyVar && substitutedTy->Contains(tyVar);
        if (recursived) {
            return true;
        }
    }
    return false;
}
} // namespace Cangjie::TypeCheckUtil
