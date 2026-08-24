// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Implements GenericInstantiation related methods.
 */

#include "GenericInstantiationManagerImpl.h"

#include <functional>

#include "TypeCheckUtil.h"
#include "TypeCheckerImpl.h"
#include "ImplUtils.h"
#include "PartialInstantiation.h"
#include "OverrideFunctionResolver.h"

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeManager.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;

using GIM = GenericInstantiationManager;

void GIM::GenericInstantiationManagerImpl::ClearCache()
{
    structContext.clear();
    extendGenerated.clear();
    intersectionTyStatus.clear();
    declInstantiationByTypeMap.clear();
    instantiatedDeclsMap.clear();
    membersIndexMap.clear();
    skippedMemberOffsets.clear();
    OverrideFunctionResolver::ClearCache();
}

void GIM::GenericInstantiationManagerImpl::WalkImportedInstantiations(
    const std::function<void(Decl&)>& processFunc, const std::function<bool(Package&)>& skipChecker) const
{
    for (auto pd : importManager.GetAllImportedPackages(true)) {
        CJC_ASSERT(pd && pd->srcPackage);
        auto srcPkg = pd->srcPackage;
        if (skipChecker(*srcPkg)) {
            continue;
        }
        for (auto& decl : srcPkg->genericInstantiatedDecls) {
            CJC_NULLPTR_CHECK(decl);
            if (decl->genericDecl && importManager.IsMacroRelatedPackageName(decl->genericDecl->fullPackageName)) {
                continue;
            }
            processFunc(*decl);
        }
    }
}

void GIM::GenericInstantiationManagerImpl::RebuildGenericInstantiationManager()
{
    WalkImportedInstantiations(
        [this](Decl& decl) {
            // Do not building cache for extend decl when current is not incremental compilation.
            auto genericDecl = decl.genericDecl;
            if (!genericDecl || decl.astKind == ASTKind::EXTEND_DECL) {
                return;
            }
            GenericInfo genericInfo(genericDecl, BuildTypeMapping(decl));
            declInstantiationByTypeMap.emplace(genericInfo, &decl);
            instantiatedDeclsMap[genericDecl].emplace(&decl);
        },
        [this](auto& pkg) {
            // 1. When backend is llvm, do not rebuild cache.
            // 2. For other backend, filter out macro-related package.
            //    Ensure that macro-related packages are used only in macro expansion.
            // NOTE: the incremental compilation does not support macro package.
            return backend == Triple::BackendType::CJNATIVE ||
                importManager.IsMacroRelatedPackageName(pkg.fullPackageName);
        });
}

void GIM::GenericInstantiationManagerImpl::RestoreInstantiatedDeclTy() const
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    WalkImportedInstantiations([this](Decl& decl) { RestoreInstantiatedDeclTy(decl); },
        [this](auto& pkg) { return importManager.IsMacroRelatedPackageName(pkg.fullPackageName); });
#endif
}

/**
 * All sema types created in sema stage are only point to generic decls.
 * So after generic instantiation, we need to update sema ty's decl from generic decl to instantiated decl.
 * Since instantiated decls are not shared between irrelevant packages,
 * an instantiated decl can be restored to ty only if it's generated in relevant packages
 * or all of instantiated decls of same ty are generated in irrelevant package.
 */
void GIM::GenericInstantiationManagerImpl::RestoreInstantiatedDeclTy(Decl& decl) const
{
    bool ignore = Ty::IsInitialTy(decl.DataTy()) || !decl.IsNominalDecl() || decl.astKind == ASTKind::EXTEND_DECL;
    if (ignore) {
        return;
    }
    if (!IsDeclCanRestoredForTy(decl)) {
        return;
    }
    switch (decl.TyKind()) {
        case TypeKind::TYPE_CLASS: {
            auto ty = RawStaticCast<ClassTy*>(decl.DataTy());
            ty->decl = StaticAs<ASTKind::CLASS_DECL>(&decl);
            ty->commonDecl = StaticAs<ASTKind::CLASS_DECL>(&decl);
            auto thisTy = typeManager.GetClassThisTy(*ty->declPtr, ty->TyArgs());
            thisTy->decl = StaticAs<ASTKind::CLASS_DECL>(&decl);
            thisTy->commonDecl = StaticAs<ASTKind::CLASS_DECL>(&decl);
            break;
        }
        case TypeKind::TYPE_INTERFACE: {
            auto ty = RawStaticCast<InterfaceTy*>(decl.DataTy().get());
            ty->decl = StaticAs<ASTKind::INTERFACE_DECL>(&decl);
            ty->commonDecl = StaticAs<ASTKind::INTERFACE_DECL>(&decl);
            break;
        }
        case TypeKind::TYPE_STRUCT: {
            auto ty = RawStaticCast<StructTy*>(decl.DataTy().get());
            ty->decl = StaticAs<ASTKind::STRUCT_DECL>(&decl);
            break;
        }
        case TypeKind::TYPE_ENUM: {
            auto ty = RawStaticCast<EnumTy*>(decl.DataTy().get());
            ty->decl = StaticAs<ASTKind::ENUM_DECL>(&decl);
            break;
        }
        default:
            break;
    }
}

bool GIM::GenericInstantiationManagerImpl::IsDeclCanRestoredForTy(const Decl& decl) const
{
    if (!curPkg) {
        return true;
    }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (decl.fullPackageName == curPkg->fullPackageName) {
        return true; // If decl is in current package, the 'decl' can be set as ty's decl.
    }
    // If there is an instantiated version in current package, the 'decl' cannot be set as ty's decl.
    GenericInfo genericInfo(decl.genericDecl, BuildTypeMapping(decl));
    auto decls = declInstantiationByTypeMap.equal_range(genericInfo);
    for (auto it = decls.first; it != decls.second; ++it) {
        auto instantiatedDecl = it->second;
        if (instantiatedDecl->fullPackageName == curPkg->fullPackageName) {
            return false;
        }
    }
    return true;
#endif
}

TypeSubst GIM::GenericInstantiationManagerImpl::BuildTypeMapping(
    const Decl& instantiatedDecl) const
{
    auto genericDecl = instantiatedDecl.genericDecl;
    if (genericDecl == nullptr) {
        return {};
    }
    TypeSubst typeMapping = typeManager.GenerateGenericMappingFromGeneric(*genericDecl, instantiatedDecl);
    return typeMapping;
}

/**
 * Build abstract function map for all type which inherited interface.
 * This map helps target rearrange of interface call in generic function which has interface upper bound.
 * eg. func test<T>(ins: T) where T <: I {
 *         ins.interfaceFunc
 *     }
 * target of ins.interfaceFunc, must be rearranged to ins.instanceFunc if:
 *   1. interfaceFunc is a static interface function.
 *   2. type 'T' extends interface 'I'
 */
void GIM::GenericInstantiationManagerImpl::BuildAbstractFuncMap()
{
    Utils::ProfileRecorder::Start("BuildAbstractFuncMap", "class/struct/enum/interface type");
    // For all class/struct/enum/interface type.
    std::unordered_set<Ptr<Decl>> genericDecls;
    auto collectDecls = [&genericDecls](const OwnedPtr<Decl>& decl) {
        if (!decl || !GetDeclTy(*decl) || !decl->IsNominalDecl()) {
            return;
        }
        if (decl->generic) {
            genericDecls.emplace(decl.get());
        }
    };
    auto pkgs = importManager.GetAllImportedPackages();
    for (auto& pkg : pkgs) {
        CJC_NULLPTR_CHECK(pkg->srcPackage);
        IterateToplevelDecls(*pkg->srcPackage, collectDecls);
    }
    Utils::ProfileRecorder::Stop("BuildAbstractFuncMap", "class/struct/enum/interface type");
    Utils::ProfileRecorder recorder("BuildAbstractFuncMap", " Build index");
    // Build index for members of generic decl.
    for (auto decl : genericDecls) {
        size_t i = 0;
        for (auto& member : decl->GetMemberDecls()) {
            // NOTE: Ignore primaryCtorDecl from indexing map.
            //       The primaryCtorDecl will be removed from instantiated version.
            if (member->astKind == ASTKind::PRIMARY_CTOR_DECL) {
                continue;
            }
            membersIndexMap.emplace(member.get(), i++);
        }
        if (decl->astKind != ASTKind::ENUM_DECL) {
            continue;
        }
        auto ed = RawStaticCast<EnumDecl*>(decl);
        for (size_t idx = 0; idx < ed->constructors.size(); ++idx) {
            membersIndexMap.emplace(ed->constructors[idx].get(), idx);
        }
    }
}

static void AppendGenericFuncMap(
    const FuncDecl& genericDecl, const std::unordered_set<Ptr<Decl>>& insFuncDecls, Generic2InsMap& result)
{
    result.emplace(&genericDecl, insFuncDecls);
    for (size_t i = 0; i < genericDecl.funcBody->paramLists[0]->params.size(); ++i) {
        auto& genericParam = genericDecl.funcBody->paramLists[0]->params[i];
        if (genericParam->desugarDecl == nullptr) {
            continue;
        }
        std::unordered_set<Ptr<Decl>> insParamDecls;
        for (auto decl : insFuncDecls) {
            auto& insParam = StaticCast<FuncDecl*>(decl)->funcBody->paramLists[0]->params[i];
            CJC_NULLPTR_CHECK(insParam->desugarDecl);
            insParamDecls.emplace(insParam->desugarDecl.get());
        }
        result.emplace(genericParam->desugarDecl.get(), insParamDecls);
    }
}

static void AppendGenericPropMap(
    const PropDecl& propDecl, std::unordered_set<Ptr<Decl>>& insPropDecls, Generic2InsMap& result)
{
    for (size_t i = 0; i < propDecl.getters.size(); ++i) {
        std::unordered_set<Ptr<Decl>> insGetterDecls;
        auto genericGetter = propDecl.getters[i].get();
        for (auto insProp : insPropDecls) {
            auto instPopDecl = StaticCast<PropDecl*>(insProp);
            CJC_ASSERT(propDecl.getters.size() == instPopDecl->getters.size());
            insGetterDecls.emplace(instPopDecl->getters[i].get());
        }
        AppendGenericFuncMap(*genericGetter, insGetterDecls, result);
    }
    for (size_t i = 0; i < propDecl.setters.size(); ++i) {
        std::unordered_set<Ptr<Decl>> insSetterDecls;
        auto genericSetter = propDecl.setters[i].get();
        for (auto insProp : insPropDecls) {
            auto instPopDecl = StaticCast<PropDecl*>(insProp);
            CJC_ASSERT(propDecl.setters.size() == instPopDecl->setters.size());
            insSetterDecls.emplace(instPopDecl->setters[i].get());
        }
        AppendGenericFuncMap(*genericSetter, insSetterDecls, result);
    }
}

void GIM::GenericInstantiationManagerImpl::AppendGenericMemberMap(
    const Decl& genericDecl, const std::unordered_set<Ptr<Decl>>& insNominalDecls, Generic2InsMap& result) const
{
    result.emplace(&genericDecl, insNominalDecls);
    for (auto& genericMember : genericDecl.GetMemberDecls()) {
        if (genericMember->astKind != ASTKind::FUNC_DECL && genericMember->astKind != ASTKind::PROP_DECL) {
            continue;
        }
        auto insMemberDecls = PartialInstantiation::GetInstantiatedDecl(*genericMember);
        if (insMemberDecls.empty()) {
            continue;
        }
        if (auto genericMemberFunc = DynamicCast<const FuncDecl*>(genericMember.get()); genericMemberFunc) {
            AppendGenericFuncMap(*genericMemberFunc, insMemberDecls, result);
        } else {
            AppendGenericPropMap(*StaticCast<const PropDecl*>(genericMember.get()), insMemberDecls, result);
        }
    }
}

Generic2InsMap GIM::GenericInstantiationManagerImpl::GetAllGenericToInsDecls() const
{
    Generic2InsMap result;
    for (auto& mapIt : instantiatedDeclsMap) {
        if (mapIt.first->IsNominalDecl()) {
            AppendGenericMemberMap(*mapIt.first, mapIt.second, result);
        } else if (mapIt.first->astKind == ASTKind::FUNC_DECL) {
            AppendGenericFuncMap(*StaticCast<const FuncDecl*>(mapIt.first), mapIt.second, result);
        }
    }
    return result;
}
