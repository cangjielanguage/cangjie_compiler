// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements extend type check.
 */
#include "TypeCheckUtil.h"
#include "TypeCheckerImpl.h"

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"

using namespace Cangjie;
using namespace TypeCheckUtil;
using namespace AST;

void TypeChecker::TypeCheckerImpl::CheckExtendGenerics(const ExtendDecl& ed)
{
    if (!ed.generic || !Ty::IsTyCorrect(ed.extendedType->ty) || !ed.extendedType->ty->IsExtendable()) {
        return;
    }
    auto usedGenericTys = GetAllGenericTys(ed.extendedType->ty);
    std::vector<std::string> unusedTypes;
    for (auto& it : ed.generic->typeParameters) {
        if (usedGenericTys.count(it->ty) == 0) {
            unusedTypes.emplace_back(it->identifier);
        }
    }
    if (!unusedTypes.empty()) {
        std::string typeStr = " '" + Utils::JoinStrings(unusedTypes, "', '") + "'";
        if (unusedTypes.size() > 1) {
            typeStr = "s" + typeStr; // add suffix 's' for plural.
        }
        diag.DiagnoseRefactor(DiagKindRefactor::sema_extend_generic_must_be_used, *ed.extendedType, typeStr);
    }
}

void TypeChecker::TypeCheckerImpl::CheckExtendedTypeValidity(const Type& extendedType)
{
    if (!Ty::IsTyCorrect(extendedType.ty) || extendedType.ty->IsExtendable()) {
        return;
    }
    // All other types are not allowed to be extended.
    diag.DiagnoseRefactor(DiagKindRefactor::sema_illegal_extended_type, extendedType, extendedType.ty->String());
}

/**
 * Check procedure:
 * 1. Find same interface declaration for the extension. Follow case 'A<T>' will be found.
 * 2. Check whether the default implementation does not depend on external generic parameters is included.
 *
 * interface A<T> {
 *     func foo(): Unit {}
 * }
 * class B {}
 * extend B <: A<Int64> {
 *     // func foo(): Unit {}
 * }
 * extend B <: A<String> {
 *     // func foo(): Unit {} // same signature, should be diag.
 * }
 */
void TypeChecker::TypeCheckerImpl::CheckExtendDupDefImplByDiffTypArgs(
    const std::set<Ptr<ExtendDecl>, CmpNodeByPos>& extendDecls, const ExtendDecl& extend)
{
    std::set<Ptr<Decl>> directlyImplementedInterfaces;
    for (auto e : extendDecls) {
        if (e == &extend) {
            continue;
        }
        for (auto& inhert : e->inheritedTypes) {
            auto inhertDecl = Ty::GetDeclPtrOfTy(inhert->ty);
            directlyImplementedInterfaces.emplace(inhertDecl);
        }
    }
    for (auto& inhert : extend.inheritedTypes) {
        auto inhertDecl = Ty::GetDeclPtrOfTy(inhert->ty);
        auto found = directlyImplementedInterfaces.find(inhertDecl);
        if (found != directlyImplementedInterfaces.end()) {
            CheckDefImplWithoutOutsideGeneric(*inhertDecl, extend);
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckDefImplWithoutOutsideGeneric(Decl& inhertDecl, const ExtendDecl& extend)
{
    auto id = DynamicCast<InterfaceDecl>(&inhertDecl);
    if (!id || !id->generic || id->generic->typeParameters.empty()) {
        return;
    }
    std::vector<Ptr<Ty>> outersideGenericParamTys;
    for (auto& tp : id->generic->typeParameters) {
        outersideGenericParamTys.emplace_back(tp->ty);
    }
    for (auto& member : id->GetMemberDecls()) {
        if (!member->TestAttr(Attribute::DEFAULT) || !Ty::IsTyCorrect(member->ty)) {
            continue;
        }
        auto usedGenericTypeParamTys = member->ty->GetGenericTyArgs();
        bool usedOutersideGeneric = false;
        for (auto& outer : outersideGenericParamTys) {
            if (Utils::In(outer, usedGenericTypeParamTys)) {
                usedOutersideGeneric = true;
                break;
            }
        }
        if (!usedOutersideGeneric) {
            std::string typeName = "extend " + extend.ty->String();
            auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_extend_member_cannot_shadow,
                MakeRange(member->identifier), member->identifier.Val(), typeName);
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckExtendInterfaces(
    Ty& ty, const std::set<Ptr<ExtendDecl>, CmpNodeByPos>& extendDecls)
{
    std::unordered_set<Ptr<InterfaceTy>> inheritInterfaces;
    if (auto extendedDecl = Ty::GetDeclPtrOfTy<InheritableDecl>(&ty); extendedDecl) {
        std::vector<Ptr<Ty>> allSuperTys{};
        for (const auto& inheritedType : extendedDecl->inheritedTypes) {
            if (inheritedType->ty) {
                auto superTys = typeManager.GetAllSuperTys(*inheritedType->ty);
                allSuperTys.insert(allSuperTys.end(), superTys.begin(), superTys.end());
            }
        }
        for (const auto& superTy : allSuperTys) {
            if (superTy && superTy->IsInterface()) {
                inheritInterfaces.emplace(RawStaticCast<InterfaceTy*>(superTy));
            }
        }
    }
    std::unordered_map<Ptr<InterfaceTy>, std::set<Ptr<Type>, CmpNodeByPos>> extendInterfaces;
    for (auto& extendDecl : extendDecls) { // Check all extend decls of it.first type.
        if (!extendDecl->extendedType) {
            continue;
        }
        MultiTypeSubst typeMapping;
        if (extendDecl->extendedType->ty) {
            typeMapping = promotion.GetPromoteTypeMapping(ty, *extendDecl->extendedType->ty);
        }
        // Check all implemented interfaces of this extend decl.
        for (const auto& interface : extendDecl->inheritedTypes) {
            auto instantiateTy = typeManager.GetBestInstantiatedTy(interface->ty, typeMapping);
            if (!instantiateTy->IsInterface()) {
                continue;
            }
            auto interfaceTy = RawStaticCast<InterfaceTy*>(instantiateTy);
            extendInterfaces[interfaceTy].insert(interface.get());
            if (interfaceTy->decl && Utils::In(interfaceTy->decl->identifier.Val(), {std::string("Any"), CTYPE_NAME}) &&
                interfaceTy->decl->fullPackageName == CORE_PACKAGE_NAME) {
                diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_interface_is_not_extendable, *interface, interfaceTy->decl->identifier);
            }
        }
    }
    std::set<Ptr<InterfaceTy>, CmpTyByName> keys;
    std::for_each(extendInterfaces.begin(), extendInterfaces.end(), [&keys](auto& it) { keys.emplace(it.first); });
    for (auto interfaceTy : keys) {
        // Duplicated with extended type or duplicated with other extends.
        auto& typeSet = extendInterfaces[interfaceTy];
        if (inheritInterfaces.count(interfaceTy) > 0 || typeSet.size() > 1) {
            // When multiple interfaces are implemented repeatedly, only diagnose at the last implementation position.
            // The pre-process ensures that the size of typeSet is at least 1.
            CJC_ASSERT(!typeSet.empty());
            const auto type = *typeSet.crbegin();
            if (!type->TestAttr(Attribute::IMPORTED)) {
                diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_extend_duplicate_interface, *type, interfaceTy->String(), ty.String());
            }
        }
    }
}

// Do check interface implementation of the special extended version.
// eg: Line 1: interface I<T> {}
//     Line 2: class A<K> {}
//     Line 3: extend A<String> <: I<String> {}
//     Line 4: extend<T> A<T> <: I<T> {}
//     They might get the same instantiation version: 'A<String> <: I<String>', this is illeage in spec.
void TypeChecker::TypeCheckerImpl::CheckSpecializationExtend(const InheritableDecl& extendedDecl,
    const ExtendDecl& extendDecl, const std::set<Ptr<ExtendDecl>, CmpNodeByPos> otherExtendDecls)
{
    // Only check specialized versions of generic types. Only Line 3 will be checked.
    if (extendedDecl.ty && !extendedDecl.ty->HasGeneric()) {
        return;
    }
    // Get typemapping from 'class A<K>'(in Line 2) to 'extend A<String>'(in Line 3), it should return K |-> String.
    auto orig2specMapping = GenerateTypeMapping(extendedDecl, extendDecl.extendedType->ty->typeArgs);

    // Check if type definition is 'class A<T> <: I<T>'. The super interface will be instantiated as 'I<String>'.
    CheckSpecializationExtendDupImstantation(*extendedDecl.ty, extendDecl, extendedDecl, orig2specMapping, true);

    // Check with other generic extend declarations.
    for (auto ed : otherExtendDecls) {
        // Exclude all non-generic declaration.
        if (!ed->ty || !ed->ty->HasGeneric()) {
            continue;
        }
        // Get typemapping from 'class A<K>'(in Line 2) to 'extend<T> A<T>'(in line 4), it should return K |-> T.
        auto orig2genericMapping = GenerateTypeMapping(extendedDecl, ed->extendedType->ty->typeArgs);
        TypeSubst instantMapping;
        bool needCheck = true;
        for (auto typeArg : extendedDecl.ty->typeArgs) {
            auto tyArgGen = StaticCast<GenericsTy*>(typeArg);
            auto mapping = GenerateTypeMappingByTy(orig2genericMapping[tyArgGen], orig2specMapping[tyArgGen]);
            for (auto m : mapping) {
                auto found = instantMapping.emplace(m);
                // If the same generic T maps to different types, the two extends must not conflict.
                // if 'T |-> TypeA' and 'T |-> TypeB', 'extend A<TypeA, TypeB>' and 'extend A<T, T>' must not conflict.
                if (!found.second && found.first->second != m.second) {
                    needCheck = false;
                    break;
                }
            }
        }
        if (needCheck) {
            CheckSpecializationExtendDupImstantation(*extendedDecl.ty, extendDecl, *ed, instantMapping);
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckSpecializationExtendDupImstantation(const Ty& extendedDeclTy,
    const ExtendDecl& compareExtend, const InheritableDecl& beComparedDecl, const TypeSubst& instantMapping,
    const bool checkParent)
{
    // 'compareExtend' is Specialized Version, 'beComparedDecl' is Generic Version.
    for (auto& compareSuperInterface : compareExtend.inheritedTypes) {
        if (!compareSuperInterface || compareSuperInterface->ty->HasGeneric() ||
            compareSuperInterface->ty->typeArgs.empty()) {
            continue;
        }
        std::unordered_set<Ptr<Ty>> allSuperTys{};
        for (const auto& inheritedType : beComparedDecl.inheritedTypes) {
            if (!inheritedType->ty) {
                continue;
            }
            if (checkParent) {
                allSuperTys.merge(typeManager.GetAllSuperTys(*inheritedType->ty));
            } else {
                allSuperTys.emplace(inheritedType->ty);
            }
        }
        for (auto& beComparedSuperInterfaceTy : allSuperTys) {
            if (!beComparedSuperInterfaceTy || beComparedSuperInterfaceTy->kind != TypeKind::TYPE_INTERFACE ||
                !beComparedSuperInterfaceTy->HasGeneric()) {
                continue;
            }
            auto instantTy = typeManager.GetInstantiatedTy(beComparedSuperInterfaceTy, instantMapping);
            if (instantTy != compareSuperInterface->ty) {
                continue;
            }
            auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_extend_duplicate_interface,
                *compareSuperInterface, compareSuperInterface->ty->String(), extendedDeclTy.String());
            builder.AddNote(MakeRange(beComparedDecl.begin, beComparedDecl.end),
                "it may be an instantiated version of the following declaration");
        }
    }
}

void TypeChecker::TypeCheckerImpl::PreCheckAllExtendInterface()
{
    // Check extend decls of primitive types in stable order.
    std::set<Ptr<Ty>, CmpTyByName> keys;
    std::for_each(typeManager.builtinTyToExtendMap.begin(), typeManager.builtinTyToExtendMap.end(),
        [&keys](auto& it) { keys.emplace(it.first); });
    for (auto ty : keys) {
        CJC_ASSERT(ty);
        auto& extends = typeManager.builtinTyToExtendMap[ty];
        std::set<Ptr<ExtendDecl>, CmpNodeByPos> ordered(extends.begin(), extends.end());
        CheckExtendInterfaces(*ty, ordered);
        // The CPointer type is a generic type and can be extended by users. It need to be checked for duplicate
        // implementations of the specialized version.
        if (ty->IsPointer()) {
            auto extendDeclInCore = std::find_if(ordered.begin(), ordered.end(),
                [](const Ptr<ExtendDecl> e) { return e->fullPackageName == "std.core"; });
            for (auto ed : ordered) {
                if (extendDeclInCore == ordered.end() || ed == *extendDeclInCore) {
                    continue;
                }
                CheckSpecializationExtend(**extendDeclInCore, *ed, ordered);
            }
        }
    }
    // Check extend decls of class/struct/enum in stable order.
    auto extendedDecls = typeManager.GetAllExtendedDecls();
    std::set<Ptr<const InheritableDecl>, CmpNodeByPos> declKeys(extendedDecls.begin(), extendedDecls.end());
    for (auto decl : declKeys) {
        CJC_ASSERT(decl->ty);
        auto extends = typeManager.GetDeclExtends(*decl);
        std::set<Ptr<ExtendDecl>, CmpNodeByPos> ordered(extends.begin(), extends.end());
        CheckExtendInterfaces(*decl->ty, ordered);
        for (auto ed : ordered) {
            CheckSpecializationExtend(*decl, *ed, ordered);
        }
    }
}

void TypeChecker::TypeCheckerImpl::PreCheckExtend(ASTContext& ctx, ExtendDecl& ed)
{
    // Need put after TypeAlias circle check.
    if (ed.extendedType) {
        auto decl = Ty::GetDeclOfTy(ed.extendedType->ty);
        if (decl && decl->TestAttr(Attribute::C)) {
            diag.DiagnoseRefactor(
                DiagKindRefactor::sema_c_type_cannot_extend_interface, *ed.extendedType.get(), decl->identifier);
        }
        ed.ty = ed.extendedType->ty;
    }
    // Check implemented interfaces.
    for (auto it = ed.inheritedTypes.begin(); it != ed.inheritedTypes.end();) {
        if (!Ty::IsTyCorrect((*it)->ty) || (*it)->ty->kind != TypeKind::TYPE_INTERFACE) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_extend_not_interface, *(*it));
            ctx.DeleteInvertedIndexes(it->get());
            it->reset();
            it = ed.inheritedTypes.erase(it);
        } else {
            ++it;
        }
    }
    SetExtendExternalAttr(ctx, ed);
    CheckExtendedTypeValidity(*ed.extendedType);
}

namespace {
inline bool IsExtendedASTKind(const Type& ty)
{
    return ty.astKind == ASTKind::REF_TYPE || ty.astKind == ASTKind::QUALIFIED_TYPE ||
        ty.astKind == ASTKind::PRIMITIVE_TYPE || ty.astKind == ASTKind::OPTION_TYPE;
}

void UpdateExtendMap(TypeManager& typeManager, const std::unordered_set<Ptr<AST::ExtendDecl>>& extends)
{
    // Ensure two maps are cleared correctly: `builtinTyToExtendMap` and `declToExtendMap`.
    // For multi package compilation case: clear once for all the packages.
    // For single package or file compilation case: clear right before invocation of this function is OK.
    for (const auto& extendDecl : extends) {
        CJC_NULLPTR_CHECK(extendDecl);
        if (extendDecl->isInMacroCall || !extendDecl->extendedType ||
            (!extendDecl->TestAttr(Attribute::IMPORTED) && extendDecl->TestAttr(Attribute::FROM_COMMON_PART) &&
                extendDecl->TestAttr(Attribute::COMMON))) {
            // The extendDecl in macrocall is only for lsp, and does not need to be updated.
            continue;
        }
        auto extendTy = extendDecl->extendedType->ty;
        if (!Ty::IsTyCorrect(extendTy) || !extendTy->IsExtendable() || !IsExtendedASTKind(*extendDecl->extendedType)) {
            continue;
        }
        for (auto& interfaceType : extendDecl->inheritedTypes) {
            CJC_NULLPTR_CHECK(interfaceType);
            if (Ty::IsTyCorrect(interfaceType->ty) && interfaceType->ty->IsClassLike()) {
                StaticCast<ClassLikeTy&>(*interfaceType->ty).directSubtypes.emplace(extendTy);
            }
        }
        // extendTy is a built-in type
        if (extendTy->IsBuiltin()) {
            extendTy = typeManager.GetTyForExtendMap(*extendTy);
            if (typeManager.builtinTyToExtendMap.find(extendTy) == typeManager.builtinTyToExtendMap.end()) {
                typeManager.builtinTyToExtendMap[extendTy] = {};
            }
            typeManager.builtinTyToExtendMap[extendTy].emplace(extendDecl);
            continue;
        }
        // extendTy is not a built-in type
        auto decl = Ty::GetDeclPtrOfTy<InheritableDecl>(extendTy);
        if (decl == nullptr) {
            continue;
        }
        if (typeManager.declToExtendMap.find(decl) == typeManager.declToExtendMap.end()) {
            typeManager.declToExtendMap[decl] = {};
        }
        typeManager.declToExtendMap[decl].emplace(extendDecl);
        for (auto& interfaceType : extendDecl->inheritedTypes) {
            auto id = Ty::GetDeclPtrOfTy(interfaceType->ty);
            if (id == nullptr) {
                continue;
            }
            auto interface = RawStaticCast<InterfaceDecl*>(id);
            interface->subDecls.emplace(decl);
        }
    }
}
} // namespace

void TypeChecker::TypeCheckerImpl::BuildExtendMap(ASTContext& ctx)
{
    std::unordered_set<Ptr<ExtendDecl>> allExtends;
    // Collect current package's source extends.
    auto syms = GetSymsByASTKind(ctx, ASTKind::EXTEND_DECL, Sort::posAsc);
    for (auto& sym : syms) {
        if (auto ed = AST::As<ASTKind::EXTEND_DECL>(sym->node); ed) {
            allExtends.emplace(ed);
            PreCheckExtend(ctx, *ed);
        }
    }
    UpdateExtendMap(typeManager, allExtends);
}

void TypeChecker::TypeCheckerImpl::BuildImportedExtendMap()
{
    std::unordered_set<Ptr<ExtendDecl>> allExtends;
    // Collect all imported extends.
    auto emplaceExtend = [&allExtends](const OwnedPtr<Decl>& decl) {
        if (auto ed = DynamicCast<ExtendDecl*>(decl.get())) {
            allExtends.emplace(ed);
        }
    };
    // Must collect all packages' extend, even invisible macro packages.
    auto pkgs = importManager.GetAllImportedPackages(true);
    for (auto& pkg : pkgs) {
        if (pkg->srcPackage->TestAttr(Attribute::IMPORTED)) {
            IterateToplevelDecls(*pkg->srcPackage, emplaceExtend);
        }
    }
    UpdateExtendMap(typeManager, allExtends);
}

void TypeChecker::TypeCheckerImpl::CheckExtendRules(const ASTContext& ctx)
{
    if (ctx.curPackage->TestAttr(Attribute::IMPORTED)) {
        return; // OrphanRule and generic consistency check can be ignored for imported package.
    }
    std::vector<Symbol*> syms = GetSymsByASTKind(ctx, ASTKind::EXTEND_DECL, Sort::posAsc);
    for (auto& sym : syms) {
        auto extendDecl = As<ASTKind::EXTEND_DECL>(sym->node);
        bool invalid =
            extendDecl == nullptr || !extendDecl->extendedType || !Ty::IsTyCorrect(extendDecl->extendedType->ty);
        if (invalid) {
            continue;
        }
        CheckExtendOrphanRule(ctx, *extendDecl);
        CheckExtendGenerics(*extendDecl);
    }
}

void TypeChecker::TypeCheckerImpl::CheckImmutExtendInhertMutSuper(const Type& inheritedType, const ExtendDecl& ed)
{
    if (!Ty::IsTyCorrect(ed.ty) || !ed.ty->IsImmutableType()) {
        return;
    }
    Ptr<Decl> target = inheritedType.GetTarget();
    if (!target || target->astKind != ASTKind::INTERFACE_DECL || target->TestAttr(Attribute::IN_REFERENCE_CYCLE)) {
        return;
    }
    auto id = StaticCast<InterfaceDecl*>(target);
    for (auto& member : id->GetMemberDecls()) {
        if (member->astKind == ASTKind::PROP_DECL && member->TestAttr(Attribute::MUT)) {
            auto builder =
                diag.DiagnoseRefactor(DiagKindRefactor::sema_interface_is_not_extendable, ed, id->identifier);
            builder.AddNote("interface '" + id->identifier + "' contains mutable member '" + member->identifier + "'");
            return;
        }
    }
    for (auto& it : id->inheritedTypes) {
        CheckImmutExtendInhertMutSuper(*it, ed);
    }
}

void TypeChecker::TypeCheckerImpl::CheckExtendDecl(ASTContext& ctx, ExtendDecl& ed)
{
    if (ed.TestAttr(Attribute::IS_CHECK_VISITED)) {
        return;
    }
    ed.EnableAttr(Attribute::IS_CHECK_VISITED);

    // Do type legality check for extend type.
    CJC_NULLPTR_CHECK(ed.extendedType);
    Synthesize(ctx, ed.extendedType.get());
    Ptr<Decl> extendedDecl = ed.extendedType->GetTarget();
    // Check implemented interfaces.
    for (auto it = ed.inheritedTypes.begin(); it != ed.inheritedTypes.end();) {
        if (!Ty::IsTyCorrect(Synthesize(ctx, it->get()))) {
            ctx.DeleteInvertedIndexes(it->get());
            it->reset();
            it = ed.inheritedTypes.erase(it);
        } else {
            if (extendedDecl) {
                CheckSealedInheritance(*extendedDecl, **it);
                CheckThreadContextInheritance(*extendedDecl, **it);
            }
            CheckImmutExtendInhertMutSuper(**it, ed);
            ++it;
        }
    }
    for (auto& m : ed.members) {
        Synthesize(ctx, m.get());
        if (ed.ty && ed.ty->IsImmutableType()) {
            if (auto fd = DynamicCast<FuncDecl*>(m.get()); fd && !ed.ty->IsEnum() && IsIndexAssignmentOperator(*fd)) {
                auto fdIdRange = MakeRange(fd->identifier);
                diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_immutable_type_extend_assignment_index_operator, *fd, fdIdRange);
            }
            if (auto pd = DynamicCast<PropDecl*>(m.get()); pd && pd->isVar) {
                auto mutDecl = TypeCheckUtil::FindModifier(*pd, TokenKind::MUT);
                CJC_ASSERT(mutDecl);
                if (mutDecl) {
                    (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_immutable_type_illegal_property, *mutDecl);
                }
            }
        }
    }
    if (auto inhertDecl = DynamicCast<InheritableDecl>(extendedDecl)) {
        auto extends = typeManager.GetDeclExtends(*inhertDecl);
        std::set<Ptr<ExtendDecl>, CmpNodeByPos> ordered(extends.begin(), extends.end());
        CheckExtendDupDefImplByDiffTypArgs(ordered, ed);
    }
}

void TypeChecker::TypeCheckerImpl::CheckExtendOrphanRule(const ASTContext& ctx, ExtendDecl& ed)
{
    CJC_ASSERT(ed.extendedType && ed.extendedType->ty);
    // Collect interfaces which has already been extended in other packages.
    auto extendedTypeTarget = Ty::GetDeclPtrOfTy<InheritableDecl>(ed.extendedType->ty);
    std::unordered_set<Ptr<Ty>> otherPackageExtendInterfaceTy{};
    // 1. collect direct inherited interfaces of type decl.
    if (extendedTypeTarget && extendedTypeTarget->ty) {
        auto iTys = typeManager.GetAllSuperTys(*extendedTypeTarget->ty, {}, false);
        otherPackageExtendInterfaceTy.insert(iTys.begin(), iTys.end());
    }
    // 2. collect direct and indirect extend decls.
    auto extends = extendedTypeTarget ? CollectAllRelatedExtends(typeManager, *extendedTypeTarget)
                                      : typeManager.GetAllExtendsByTy(*ed.extendedType->ty);
    for (const auto& extend : extends) {
        // Replace other extended interfaces with the current extended interface generics to ensure that the interface
        // generics are the same when ty is compared. If it cannot be replaced, an empty typeMapping is generated.
        TypeSubst typeMapping = InverseMapping(GenerateTypeMapping(ed, extend->ty->typeArgs));
        if (extend->fullPackageName != ctx.fullPackageName) {
            for (const auto& inheritedType : extend->inheritedTypes) {
                if (!inheritedType->ty) {
                    continue;
                }
                for (auto superTy : typeManager.GetAllSuperTys(*inheritedType->ty)) {
                    otherPackageExtendInterfaceTy.insert(typeManager.GetInstantiatedTy(superTy, typeMapping));
                }
            }
        }
    }
    // Check whether extended type is imported.
    bool isImportedExtendedType = (extendedTypeTarget && extendedTypeTarget->fullPackageName != ctx.fullPackageName) ||
        ed.extendedType->ty->IsBuiltin();
    // Check whether all extended interfaces are imported.
    std::set<Ptr<Ty>, CmpTyByName> externalDecls = {};
    for (const auto& inheritedType : ed.inheritedTypes) {
        if (!inheritedType->ty) {
            continue;
        }
        auto allSuperTys = typeManager.GetAllSuperTys(*inheritedType->ty);
        for (auto& ty : allSuperTys) {
            auto decl = Ty::GetDeclPtrOfTy(ty);
            if (decl && decl->fullPackageName != ctx.fullPackageName && otherPackageExtendInterfaceTy.count(ty) == 0) {
                externalDecls.insert(ty);
            }
        }
    }
    // Report errors.
    if (isImportedExtendedType && !externalDecls.empty()) {
        std::string extendedType =
            extendedTypeTarget ? extendedTypeTarget->identifier.Val() : ed.extendedType->ty->String();
        DiagnosticBuilder diagnose = diag.DiagnoseRefactor(DiagKindRefactor::sema_type_cannot_extend_imported_interface,
            *ed.extendedType, extendedTypeTarget ? "imported" : "primitive", extendedType);
        diagnose.AddNote("used external interface: " + Ty::GetTypesToStr(externalDecls, " "));
    }
}

void TypeChecker::TypeCheckerImpl::SetExtendExternalAttr(const ASTContext& ctx, ExtendDecl& ed)
{
    // Check import and export.
    Ptr<Decl> extendedTypeTarget = Ty::GetDeclPtrOfTy(ed.extendedType->ty);
    if (!extendedTypeTarget) {
        extendedTypeTarget = TypeCheckUtil::GetRealTarget(ed.extendedType->GetTarget());
    }
    if ((extendedTypeTarget == nullptr || extendedTypeTarget->TestAttr(Attribute::IMPORTED)) &&
        ed.inheritedTypes.empty()) {
        // direct extend of built-in type or imported type is visible in defined package but not exported.
        // So do not add attribute for this kind of extend.
        return;
    }
    // If extension and extended type declare in same package, extension's external attr decided on both extended type
    // and generic upperbound type.
    // If not, extension's external attr decided on both of inhert interface and generic upperbound type.
    // NOTE: 'public' was added for decl with 'sealed' in 'DeclAttributeChecker'.
    auto accessLevel = extendedTypeTarget == nullptr ? AccessLevel::PUBLIC : GetAccessLevel(*extendedTypeTarget);
    if (ed.generic) {
        for (auto& tp : ed.generic->genericConstraints) {
            for (auto& up : tp->upperBounds) {
                Ptr<Decl> decl = up->GetTarget();
                if (decl == nullptr) {
                    continue;
                }
                auto upperboundAccessLevel = GetAccessLevel(*decl);
                if (upperboundAccessLevel < accessLevel) {
                    accessLevel = upperboundAccessLevel;
                }
            }
        }
    }
    if ((extendedTypeTarget != nullptr && extendedTypeTarget->fullPackageName != ctx.fullPackageName) ||
        (extendedTypeTarget == nullptr && "std.core" != ctx.fullPackageName)) {
        auto interfaceAccessLevelFinal = AccessLevel::PRIVATE;
        // Different package. Extension visible as same as max of all inherited type.
        for (auto& interface : ed.inheritedTypes) {
            Ptr<Decl> decl = interface->GetTarget();
            if (decl == nullptr) {
                return;
            }
            auto interfaceAccessLevel = GetAccessLevel(*decl);
            if (interfaceAccessLevelFinal < interfaceAccessLevel) {
                interfaceAccessLevelFinal = interfaceAccessLevel;
            }
        }
        accessLevel = accessLevel < interfaceAccessLevelFinal ? accessLevel : interfaceAccessLevelFinal;
    }
    ed.EnableAttr(GetAttrByAccessLevel(accessLevel));
}

void TypeChecker::TypeCheckerImpl::CheckExtendField(const ASTContext& ctx, MemberAccess& ma)
{
    ma.ty = TypeManager::GetInvalidTy(); // Ty will be set to valid if non-error happens.
    CJC_NULLPTR_CHECK(ma.curFile);
    std::vector<Ptr<Decl>> targets = ExtendFieldLookup(ctx, *ma.curFile, ma.baseExpr->ty, ma.field);
    if (!FilterAndCheckTargetsOfNameAccess(ctx, ma, targets)) {
        return;
    }
    auto target = GetAccessibleDecl(ctx, ma, targets);
    ReplaceTarget(&ma, target ? target : targets[0]);
    AddFuncTargetsForMemberAccess(ma, targets);
}
