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
    bool ignore = Ty::IsInitialTy(decl.ty) || !decl.IsNominalDecl() || decl.astKind == ASTKind::EXTEND_DECL;
    if (ignore) {
        return;
    }
    if (!IsDeclCanRestoredForTy(decl)) {
        return;
    }
    switch (decl.ty->kind) {
        case TypeKind::TYPE_CLASS: {
            auto ty = RawStaticCast<ClassTy*>(decl.ty);
            ty->decl = StaticAs<ASTKind::CLASS_DECL>(&decl);
            ty->commonDecl = StaticAs<ASTKind::CLASS_DECL>(&decl);
            auto thisTy = typeManager.GetClassThisTy(*ty->declPtr, ty->typeArgs);
            thisTy->decl = StaticAs<ASTKind::CLASS_DECL>(&decl);
            thisTy->commonDecl = StaticAs<ASTKind::CLASS_DECL>(&decl);
            break;
        }
        case TypeKind::TYPE_INTERFACE: {
            auto ty = RawStaticCast<InterfaceTy*>(decl.ty);
            ty->decl = StaticAs<ASTKind::INTERFACE_DECL>(&decl);
            ty->commonDecl = StaticAs<ASTKind::INTERFACE_DECL>(&decl);
            break;
        }
        case TypeKind::TYPE_STRUCT: {
            auto ty = RawStaticCast<StructTy*>(decl.ty);
            ty->decl = StaticAs<ASTKind::STRUCT_DECL>(&decl);
            break;
        }
        case TypeKind::TYPE_ENUM: {
            auto ty = RawStaticCast<EnumTy*>(decl.ty);
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

void GIM::GenericInstantiationManagerImpl::CollectDeclMemberFuncs(
    Decl& decl, std::unordered_set<Ptr<FuncDecl>>& funcs) const
{
    for (auto& member : decl.GetMemberDecls()) {
        // Only collect non-private non-generic member which may implement interface member.
        bool ignored = member == nullptr || member->astKind == AST::ASTKind::VAR_DECL ||
            member->TestAnyAttr(Attribute::PRIVATE, Attribute::CONSTRUCTOR);
        if (ignored) {
            continue;
        }
        WorkForMembers(*member, [&funcs](auto& it) {
            if (it.astKind == AST::ASTKind::FUNC_DECL) {
                funcs.emplace(StaticCast<FuncDecl>(&it));
            }
        });
    }
}

std::unordered_set<Ptr<FuncDecl>> GIM::GenericInstantiationManagerImpl::MergeMemberFuncs(
    Ty& ty, Decl& decl, const std::unordered_set<Ptr<FuncDecl>>& inheritedMembers)
{
    std::unordered_set<Ptr<FuncDecl>> memberFuncs;
    CollectDeclMemberFuncs(decl, memberFuncs);
    std::unordered_set<Ptr<FuncDecl>> dropped;
    for (auto member : memberFuncs) {
        std::vector<Ptr<FuncDecl>> overloads;
        std::copy_if(inheritedMembers.begin(), inheritedMembers.end(), std::back_inserter(overloads),
            [member](auto it) { return it->identifier == member->identifier; });
        for (auto func : overloads) {
            // If current member func is implementation of inherited function, drop inherited function.
            // Otherwise store inherited function.
            if (IsImplementationFunc(ty, *func, *member)) {
                dropped.emplace(func);
            }
        }
    }
    // Add inherited members to current collection, except dropped functions.
    std::copy_if(inheritedMembers.begin(), inheritedMembers.end(), std::inserter(memberFuncs, memberFuncs.end()),
        [&dropped](auto decl) { return dropped.count(decl) == 0; });
    return memberFuncs;
}

std::unordered_set<Ptr<FuncDecl>> GIM::GenericInstantiationManagerImpl::CollectInheritedMembers(
    Ty& ty, Decl& decl)
{
    std::set<std::pair<Ptr<AST::Ty>, Ptr<AST::Decl>>> visited;
    return CollectInheritedMembersVisit(ty, decl, visited);
}

std::unordered_set<Ptr<FuncDecl>> GIM::GenericInstantiationManagerImpl::CollectInheritedMembersVisit(
    Ty& ty, Decl& decl, std::set<std::pair<Ptr<AST::Ty>, Ptr<AST::Decl>>>& visited)
{
    auto visitedKey = std::make_pair(&ty, &decl);
    if (visited.count(visitedKey) != 0) {
        // avoid repeatedly visiting same super-type when there are multiple paths
        return {};
    }
    visited.insert(visitedKey);
    std::unordered_set<Ptr<FuncDecl>> inheritedFuncs;
    // Decl is guaranteed as InheritableDecl.
    auto& inheritDecl = static_cast<InheritableDecl&>(decl);
    for (auto& type : inheritDecl.inheritedTypes) {
        Ptr<Decl> baseDecl = Ty::GetDeclPtrOfTy(type->ty);
        if (!baseDecl) {
            continue;
        }
        // If no inherited function exist, directly collection members,
        // Otherwise merge inherited functions with current type decl's member functions.
        // If two parents have functions with same signature, current decl must override them,
        // that we can choose any of them here.
        inheritedFuncs = inheritedFuncs.empty() ? CollectInheritedMembersVisit(*type->ty, *baseDecl, visited)
                                                : MergeMemberFuncs(*type->ty, *baseDecl, inheritedFuncs);
    }
    return MergeMemberFuncs(ty, decl, inheritedFuncs);
}

namespace {
std::set<Ptr<ExtendDecl>> GetAllRelatedExtendsByTy(TypeManager& typeManager, Ty& ty)
{
    auto baseDecl = Ty::GetDeclPtrOfTy<InheritableDecl>(&ty);
    if (baseDecl) {
        return CollectAllRelatedExtends(typeManager, *baseDecl);
    } else {
        return typeManager.GetAllExtendsByTy(ty);
    }
}
} // namespace

std::unordered_set<Ptr<FuncDecl>> GIM::GenericInstantiationManagerImpl::GetInheritedMemberFuncs(
    Ty& ty)
{
    std::unordered_set<Ptr<FuncDecl>> funcs;
    auto baseDecl = Ty::GetDeclPtrOfTy<InheritableDecl>(&ty);
    if (baseDecl) {
        // Collect inherited function if sema's InheritedMembers not done.
        funcs = CollectInheritedMembers(ty, *baseDecl);
    }
    // Collect func which declared with related extends.
    auto extendDecls = GetAllRelatedExtendsByTy(typeManager, ty);
    for (auto ed : extendDecls) {
        if (!ed || ed->TestAttr(Attribute::GENERIC_INSTANTIATED)) {
            continue;
        }
        CollectDeclMemberFuncs(*ed, funcs);
    }

    return funcs;
}

std::unordered_set<Ptr<InheritableDecl>> GIM::GenericInstantiationManagerImpl::GetInheritedInterfaces(
    Ty& ty)
{
    std::unordered_set<Ptr<InheritableDecl>> inheritableType;
    // Collect interfaces that are explict implemented by the 'decl'.
    auto collectInterfaces = [&inheritableType](const InheritableDecl& decl) {
        auto superITys = decl.GetSuperInterfaceTys();
        std::for_each(superITys.begin(), superITys.end(), [&inheritableType](auto ty) {
            if (auto id = DynamicCast<InterfaceDecl*>(Ty::GetDeclPtrOfTy(ty)); id) {
                inheritableType.insert(id);
            }
        });
    };
    std::function<void(InheritableDecl&)> collectInheritedInterfaces = [&collectInheritedInterfaces, &inheritableType,
                                                                           &collectInterfaces](InheritableDecl& decl) {
        if (auto cd = DynamicCast<ClassDecl*>(&decl); cd) {
            auto super = cd->GetSuperClassDecl();
            if (super) {
                (void)inheritableType.emplace(super); // Collect ineriable class decl.
                collectInheritedInterfaces(*super);
            }
        }
        collectInterfaces(decl);
    };

    // Collect inheritableType which declared with extend.
    std::set<Ptr<ExtendDecl>> extendDecls = GetAllRelatedExtendsByTy(typeManager, ty);
    for (auto& ed : extendDecls) {
        if (ed->TestAttr(Attribute::GENERIC_INSTANTIATED)) {
            continue;
        }
        collectInterfaces(*RawStaticCast<InheritableDecl*>(ed));
    }

    // Collect inheritableType which declare in class, struct, enum (interface decl is not extendable).
    auto decl = Ty::GetDeclPtrOfTy(&ty);
    if (auto inheritDecl = DynamicCast<InheritableDecl*>(decl); inheritDecl) {
        collectInheritedInterfaces(*inheritDecl);
    }

    // Collect inheritableType which declare in super.
    std::unordered_set<Ptr<Ty>> allInterfaceTys;
    for (auto i : inheritableType) {
        if (i->ty) {
            allInterfaceTys.merge(typeManager.GetAllSuperTys(*i->ty));
        }
    }
    allInterfaceTys.erase(&ty);
    for (auto iTy : allInterfaceTys) {
        if (auto id = DynamicCast<InterfaceDecl*>(Ty::GetDeclPtrOfTy(iTy)); id) {
            inheritableType.insert(id);
        }
    }
    return inheritableType;
}

void GIM::GenericInstantiationManagerImpl::MapFuncWithDecl(Ty& ty, FuncDecl& interfaceFunc, const FuncDecl& target)
{
    CJC_ASSERT(target.outerDecl);

    auto decls =
        GetRealIndexingMembers(target.outerDecl->GetMemberDecls(), target.outerDecl->TestAttr(Attribute::GENERIC));
    auto realDecl = target.propDecl ? RawStaticCast<const Decl*>(target.propDecl) : &target;
    for (auto it = decls.begin(); it != decls.end(); ++it) {
        if (*it == realDecl) {
            auto keyPair = std::make_pair(&ty, &interfaceFunc);
            auto valuePair = std::make_pair(target.outerDecl, std::distance(decls.begin(), it));
            abstractFuncToDeclMap[keyPair].emplace(valuePair);
            break;
        }
    }
}

MultiTypeSubst GIM::GenericInstantiationManagerImpl::GetTypeMapping(Ptr<Ty>& baseTy, Ty& interfaceTy)
{
    MultiTypeSubst typeMapping;
    if (!baseTy) {
        return typeMapping;
    }
    typeMapping = promotion.GetPromoteTypeMapping(*baseTy, interfaceTy);
    if (!typeMapping.empty()) {
        return typeMapping;
    }
    // For a generic type which extends interface,
    // only sema type created in extend decl can generate typeMapping with the extended interface type.
    std::set<Ptr<ExtendDecl>> extendDecls = GetAllRelatedExtendsByTy(typeManager, *baseTy);
    for (auto& ed : extendDecls) {
        if (ed->TestAttr(Attribute::GENERIC_INSTANTIATED)) {
            continue;
        }
        baseTy = ed->extendedType->ty;
        MultiTypeSubst prRes;
        if (baseTy) {
            prRes = promotion.GetPromoteTypeMapping(*baseTy, interfaceTy);
        }
        std::for_each(prRes.begin(), prRes.end(), [&typeMapping](auto it) { typeMapping[it.first].merge(it.second); });
    }
    return typeMapping;
}

/**
 * Check whether two functions which exist in given type's whole inheritance relationship has
 * implementation/override relation.
 * eg. interface I1 {
 *         func foo():Int64  // f1
 *     }
 *     class A {
 *         func foo():Int64{0} // f2
 *     }
 *     extend A <: I1{}
 * In this case, sema type of 'A', interface function 'f1' and function 'f2' are checked,
 * result will be 'f2' implements 'f1', function returns true.
 * Example also for generic declarations.
 */
bool GIM::GenericInstantiationManagerImpl::IsImplementationFunc(
    Ty& ty, const FuncDecl& interfaceFunc, const FuncDecl& fd)
{
    CJC_ASSERT(fd.outerDecl && interfaceFunc.outerDecl);
    // If function's static or generic status not equal, the will not have relation of implementation.
    bool noRelation = interfaceFunc.TestAttr(Attribute::STATIC) != fd.TestAttr(Attribute::STATIC) ||
        interfaceFunc.TestAttr(Attribute::GENERIC) != fd.TestAttr(Attribute::GENERIC);
    if (noRelation) {
        return false;
    }
    if (interfaceFunc.outerDecl->astKind == fd.outerDecl->astKind) {
        // If both functions are declared in same kind of type decls, directly return if 'fd' is abstract.
        if (fd.TestAttr(Attribute::ABSTRACT)) {
            return false;
        }
    } else {
        // If functions are declared in different kind of type decls, directly return if 'fd' is in interface.
        if (fd.outerDecl->astKind == ASTKind::INTERFACE_DECL) {
            return false;
        }
    }
    auto baseTy = Ptr(&ty);
    auto structDecl = RawStaticCast<InheritableDecl*>(fd.outerDecl);
    // Update base ty to extendDecl's ty when the extended type is current given 'ty'.
    // For primitive and builtin types, the declPtr is always nullptr that always update baseTy to extendDecl ty.
    // For user defined types only update baseTy when declPtr is same.
    bool shouldUpdateImplementTy = true;
    if (auto ed = DynamicCast<ExtendDecl*>(structDecl);
        ed && Ty::GetDeclPtrOfTy(ed->extendedType->ty) == Ty::GetDeclPtrOfTy(baseTy)) {
        baseTy = ed->extendedType->ty;
        shouldUpdateImplementTy = false;
    }
    // 1. Substitute interface func's type to baseTy. NOTE: May update baseTy's value.
    auto interfaceTy = interfaceFunc.outerDecl->ty;
    auto typeMappings = GetTypeMapping(baseTy, *interfaceTy);
    for (auto& type : structDecl->inheritedTypes) {
        // Eg: generate mapping for extend Int16 <: Number<Int32> and interface Number<T>.
        if (type && type->ty && typeManager.IsSubtype(Ty::GetGenericTyOfInsTy(*type->ty), interfaceTy)) {
            typeManager.GenerateGenericMapping(typeMappings, *type->ty);
            break;
        }
    }
    auto iFuncTy = interfaceFunc.ty;
    // 1.5 Mapping function generic from 'interfaceFunc' to 'fd'.
    // Previously checked 'interfaceFunc' and 'fd' have same generic status.
    if (fd.TestAttr(Attribute::GENERIC)) {
        TypeSubst typeMapping = typeManager.GenerateGenericMappingFromGeneric(interfaceFunc, fd);
        iFuncTy = typeManager.GetInstantiatedTy(iFuncTy, typeMapping);
    }
    // For 'interface I<T> { func foo(a: T) {} }; class C <: I<A> & I<B>',
    // function 'foo' may have multiple instantiation type.
    auto interfaceFuncTys = typeManager.GetInstantiatedTys(iFuncTy, typeMappings);

    // 2. Substitute implemented func's type to baseTy.
    auto implementedFuncTy = RawStaticCast<FuncTy*>(fd.ty);
    if (shouldUpdateImplementTy) {
        auto structTy = structDecl->ty;
        if (baseTy && structTy) {
            typeMappings = promotion.GetPromoteTypeMapping(*baseTy, *structTy);
        } else {
            typeMappings = {};
        }
        implementedFuncTy = RawStaticCast<FuncTy*>(typeManager.GetBestInstantiatedTy(fd.ty, typeMappings));
    }

    // 3. compare two func's signature. structure declaration mapping relation if signatures are same.
    // when function have multiple instantiated types, only need one of them passed.
    // Actual type will be decided in rearrange stage.
    return std::any_of(interfaceFuncTys.begin(), interfaceFuncTys.end(), [this, implementedFuncTy](auto ty) {
        return implementedFuncTy && ty &&
            typeManager.IsFuncParameterTypesIdentical(*implementedFuncTy, *RawStaticCast<FuncTy*>(ty));
    });
}

void GIM::GenericInstantiationManagerImpl::BuildAbstractFuncMapHelper(Ty& ty)
{
    std::unordered_set<Ptr<FuncDecl>> funcs = GetInheritedMemberFuncs(ty);
    std::unordered_set<Ptr<InheritableDecl>> interfaces = GetInheritedInterfaces(ty);

    auto dealWithFunDecl = [this, &funcs, &ty](FuncDecl& realMember) {
        // Currently not adaptable for property decl.
        for (auto& fd : funcs) {
            if (fd->identifier != realMember.identifier) {
                continue;
            }
            if (IsImplementationFunc(ty, realMember, *fd)) {
                MapFuncWithDecl(ty, realMember, *fd);
            }
        }
    };

    for (auto& id : interfaces) {
        for (auto realMember : GetInheritedMemberFuncs(*id->ty)) {
            dealWithFunDecl(*realMember);
        }
    }
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
    Utils::ProfileRecorder::Start("BuildAbstractFuncMap", "primitive types");
    abstractFuncToDeclMap.clear();
    // For primitive types.
    for (auto& it : typeManager.builtinTyToExtendMap) {
        BuildAbstractFuncMapHelper(*it.first);
    }
    Utils::ProfileRecorder::Stop("BuildAbstractFuncMap", "primitive types");
    Utils::ProfileRecorder::Start("BuildAbstractFuncMap", "class/struct/enum/interface type");
    // For all class/struct/enum/interface type.
    std::unordered_set<Ptr<Decl>> inheritableDecls;
    std::unordered_set<Ptr<Decl>> genericDecls;
    auto collectDecls = [&inheritableDecls, &genericDecls](const OwnedPtr<Decl>& decl) {
        if (!decl || !GetDeclTy(*decl) || !decl->IsNominalDecl()) {
            return;
        }
        if (decl->astKind != ASTKind::EXTEND_DECL) {
            inheritableDecls.emplace(decl.get());
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
    Utils::ProfileRecorder::Start("BuildAbstractFuncMap", "inheritableDecls");
    for (auto& decl : inheritableDecls) {
        BuildAbstractFuncMapHelper(*decl->ty);
    }
    Utils::ProfileRecorder::Stop("BuildAbstractFuncMap", "inheritableDecls");
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
    const AST::FuncDecl& genericDecl, const std::unordered_set<Ptr<AST::Decl>>& insFuncDecls, Generic2InsMap& result)
{
    result.emplace(&genericDecl, insFuncDecls);
    for (size_t i = 0; i < genericDecl.funcBody->paramLists[0]->params.size(); ++i) {
        auto& genericParam = genericDecl.funcBody->paramLists[0]->params[i];
        if (genericParam->desugarDecl == nullptr) {
            continue;
        }
        std::unordered_set<Ptr<AST::Decl>> insParamDecls;
        for (auto decl : insFuncDecls) {
            auto& insParam = StaticCast<AST::FuncDecl*>(decl)->funcBody->paramLists[0]->params[i];
            CJC_NULLPTR_CHECK(insParam->desugarDecl);
            insParamDecls.emplace(insParam->desugarDecl.get());
        }
        result.emplace(genericParam->desugarDecl.get(), insParamDecls);
    }
}

static void AppendGenericPropMap(
    const AST::PropDecl& propDecl, std::unordered_set<Ptr<AST::Decl>>& insPropDecls, Generic2InsMap& result)
{
    for (size_t i = 0; i < propDecl.getters.size(); ++i) {
        std::unordered_set<Ptr<AST::Decl>> insGetterDecls;
        auto genericGetter = propDecl.getters[i].get();
        for (auto insProp : insPropDecls) {
            insGetterDecls.emplace(StaticCast<AST::PropDecl*>(insProp)->getters[i].get());
        }
        AppendGenericFuncMap(*genericGetter, insGetterDecls, result);
    }
    for (size_t i = 0; i < propDecl.setters.size(); ++i) {
        std::unordered_set<Ptr<AST::Decl>> insSetterDecls;
        auto genericSetter = propDecl.setters[i].get();
        for (auto insProp : insPropDecls) {
            insSetterDecls.emplace(StaticCast<AST::PropDecl*>(insProp)->setters[i].get());
        }
        AppendGenericFuncMap(*genericSetter, insSetterDecls, result);
    }
}

void GIM::GenericInstantiationManagerImpl::AppendGenericMemberMap(const AST::Decl& genericDecl,
    const std::unordered_set<Ptr<AST::Decl>>& insNominalDecls, Generic2InsMap& result) const
{
    result.emplace(&genericDecl, insNominalDecls);
    for (auto& genericMember : genericDecl.GetMemberDecls()) {
        if (genericMember->astKind != AST::ASTKind::FUNC_DECL && genericMember->astKind != AST::ASTKind::PROP_DECL) {
            continue;
        }
        auto insMemberDecls = PartialInstantiation::GetInstantiatedDecl(*genericMember);
        if (insMemberDecls.empty()) {
            continue;
        }
        if (auto genericMemberFunc = DynamicCast<const AST::FuncDecl*>(genericMember.get()); genericMemberFunc) {
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
        } else if (mapIt.first->astKind == AST::ASTKind::FUNC_DECL) {
            AppendGenericFuncMap(*StaticCast<const AST::FuncDecl*>(mapIt.first), mapIt.second, result);
        }
    }
    return result;
}
