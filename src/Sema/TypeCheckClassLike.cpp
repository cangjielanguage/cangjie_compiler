// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements typecheck apis for class and interface.
 */

#include "TypeCheckerImpl.h"

#include <algorithm>
#include <functional>
#include <iterator>

#include "Diags.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Sema/TestManager.h"

using namespace Cangjie;
using namespace AST;
using namespace Sema;
using namespace TypeCheckUtil;

namespace {
void CheckAnnotationHasConstInit(DiagnosticEngine& diag, const ClassDecl& cd)
{
    CJC_NULLPTR_CHECK(cd.body);
    if (!Utils::In(cd.body->decls, [](const auto& decl) {
            CJC_NULLPTR_CHECK(decl);
            return IsInstanceConstructor(*decl) && decl->IsConst();
        })) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_annotation_no_const_init, MakeRangeForDeclIdentifier(cd));
    }
}
} // namespace

void TypeChecker::TypeCheckerImpl::CheckSealedInheritance(const Decl& child, const Type& parent)
{
    Ptr<const Decl> target = parent.GetTarget();
    if (target == nullptr) {
        return;
    }
    if (target->TestAttr(Attribute::SEALED)) {
        if (target->TestAttr(Attribute::IMPORTED) && !child.TestAttr(Attribute::IMPORTED)) {
            // Parent is imported, and child is defined in current package.
            DiagCannotInheritSealed(diag, child, parent);
        }
        // Otherwise, both parent and child are imported,
        // which is orphan extension and the error is reported by `CheckExtendOrphanRule`.
        if (target->TestAttr(Attribute::PLATFORM) && !child.TestAttr(Attribute::FROM_COMMON_PART)) {
            // Parent is imported, and child is defined in current package.
            DiagCannotInheritSealed(diag, child, parent, true);
        }
    }
}

/**
 * Check some constraints for decls that inherit `ThreadContext` from package `core`.
 * eg: Should not inherit or implement `ThreadContext` if decls are not within the whitelist.
 */
void TypeChecker::TypeCheckerImpl::CheckThreadContextInheritance(const Decl& decl, const Type& parent)
{
    static std::unordered_map<std::string, std::string> whitelist = {
        {"ThreadContext", "std.core"},
        {"MainThreadContext", "ohos.base"}
    };
    // Manage all derived type of `ThreadContext` or `SchedulerNativeHandle` which from package `core` with whitelist.
    auto target = parent.GetTarget();
    if (target == nullptr || target->fullPackageName != "std.core" || target->identifier != "ThreadContext") {
        return;
    }
    // Decls cannot be modified with 'open' when inherit 'ThreadContext'
    if (decl.TestAttr(Attribute::OPEN)) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_inherit_thread_context_not_open, decl, decl.identifier);
        return;
    }
    auto found = whitelist.find(decl.identifier);
    if (found != whitelist.end() && found->second == decl.curFile->curPackage->fullPackageName) {
        return;
    }
    (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_inherit_thread_context_invalid, decl, decl.identifier);
}

void TypeChecker::TypeCheckerImpl::CheckClassDecl(ASTContext& ctx, ClassDecl& cd)
{
    if (cd.TestAttr(Attribute::IS_CHECK_VISITED)) {
        return;
    }
    // Mark the current class declaration is checked.
    cd.EnableAttr(Attribute::IS_CHECK_VISITED);
    if (!cd.TestAttr(Attribute::PUBLIC)) {
        for (auto& ann : cd.annotations) {
            CJC_NULLPTR_CHECK(ann);
            if (ann->kind == AnnotationKind::ANNOTATION) {
                (void)diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_annotation_non_public, MakeRangeForDeclIdentifier(cd));
                break;
            }
        }
    }
    if (cd.TestAttr(Attribute::IS_ANNOTATION)) {
        CheckAnnotationHasConstInit(diag, cd);
    }
    // Do type check for all implemented interfaces.
    for (auto& it : cd.inheritedTypes) {
        CJC_NULLPTR_CHECK(it);
        Synthesize(ctx, it.get());
        if (auto rt = DynamicCast<RefType*>(it.get()); rt && rt->ref.target) {
            CheckSealedInheritance(cd, *rt);
            CheckThreadContextInheritance(cd, *rt);
        }
    }
    TypeCheckCompositeBody(ctx, cd, cd.body->decls);
    CheckRecursiveConstructorCall(cd.body->decls);
    if (cd.TestAnyAttr(Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE)) {
        CheckJavaInteropLibImport(cd);
    }
    if (cd.TestAnyAttr(Attribute::OBJ_C_MIRROR, Attribute::OBJ_C_MIRROR_SUBTYPE)) {
        CheckObjCInteropLibImport(cd);
    }
}

void TypeChecker::TypeCheckerImpl::CheckAndAddSubDecls(
    const Type& type, ClassDecl& cd, bool& hasSuperClass, int& superClassLikeNum) const
{
    auto target = Ty::IsTyCorrect(type.ty) ? Ty::GetDeclPtrOfTy(type.ty) : type.GetTarget();
    if (auto id = AST::As<ASTKind::INTERFACE_DECL>(target); id) {
        id->subDecls.insert(&cd);
    } else if (auto superClass = AST::As<ASTKind::CLASS_DECL>(target); superClass) {
        if (hasSuperClass) {
            diag.Diagnose(type, DiagKind::sema_illegal_multi_inheritance, cd.identifier.Val());
        } else {
            if (superClassLikeNum != 0) {
                diag.Diagnose(type, DiagKind::sema_superclass_must_be_placed_at_first, superClass->identifier.Val());
            }
        }
        // Check at the first.
        superClass->subDecls.insert(&cd);
        if ((!superClass->TestAttr(Attribute::ABSTRACT) && !superClass->TestAttr(Attribute::OPEN)) ||
            TestManager::IsDeclOpenToMock(*superClass)
        ) {
            diag.Diagnose(type, DiagKind::sema_non_inheritable_super_class, superClass->identifier.Val());
        }
        hasSuperClass = true;
    } else if (auto tad = AST::As<ASTKind::TYPE_ALIAS_DECL>(target); tad) {
        CheckAndAddSubDecls(*tad->type, cd, hasSuperClass, superClassLikeNum);
    } else {
        // Can only implement class or interface.
        diag.Diagnose(type, DiagKind::sema_class_inherit_non_class_nor_interface, cd.identifier.Val());
        return;
    }
    superClassLikeNum++;
}

bool TypeChecker::TypeCheckerImpl::HasSuperClass(ClassDecl& cd) const
{
    int count = 0;
    bool hasSuperClass = false;
    for (auto& it : cd.inheritedTypes) {
        CheckAndAddSubDecls(*it, cd, hasSuperClass, count);
    }
    return hasSuperClass;
}

void TypeChecker::TypeCheckerImpl::AddObjectSuperClass(ASTContext& ctx, ClassDecl& cd)
{
    auto tmp = MakeOwned<RefType>();
    tmp->curFile = cd.curFile;
    tmp->ref.identifier = OBJECT_NAME;
    tmp->EnableAttr(Attribute::COMPILER_ADD);
    if (ctx.fullPackageName == CORE_PACKAGE_NAME && cd.identifier == OBJECT_NAME) {
        return; // Do not add 'Object' for itself.
    } else if (auto objectDecl = importManager.GetCoreDecl(OBJECT_NAME)) {
        tmp->ref.target = objectDecl;
        tmp->ty = tmp->ref.target->ty;
        cd.inheritedTypes.insert(cd.inheritedTypes.begin(), std::move(tmp));
    } else {
        ctx.diag.Diagnose(cd, DiagKind::sema_no_core_object);
    }
}

bool TypeChecker::TypeCheckerImpl::AddJObjectSuperClassJavaInterop(ASTContext& ctx, ClassDecl& cd)
{
    using namespace Interop::Java;
    if (ctx.fullPackageName == INTEROP_JAVA_LANG_PACKAGE && cd.identifier == INTEROP_JOBJECT_NAME) {
        return false;
    }
    if (auto objectDecl = importManager.GetImportedDecl(
        INTEROP_JAVA_LANG_PACKAGE, INTEROP_JOBJECT_NAME)) {
        CJC_ASSERT(objectDecl->astKind == ASTKind::CLASS_DECL);
        auto tmp = MakeOwned<RefType>();
        tmp->EnableAttr(AST::Attribute::COMPILER_ADD);
        tmp->curFile = cd.curFile;
        tmp->ref.identifier = SrcIdentifier{INTEROP_JOBJECT_NAME};
        tmp->ref.target = objectDecl;
        tmp->ty = tmp->ref.target->ty;
        cd.inheritedTypes.insert(cd.inheritedTypes.begin(), std::move(tmp));
        cd.EnableAttr(Attribute::JAVA_MIRROR_SUBTYPE);
    } else {
        ctx.diag.DiagnoseRefactor(DiagKindRefactor::sema_member_not_imported, cd.identifier.Begin(),
                                  INTEROP_JAVA_LANG_PACKAGE + "." + INTEROP_JOBJECT_NAME);
    }
    return true;
}

void TypeChecker::TypeCheckerImpl::CheckInterfaceDecl(ASTContext& ctx, InterfaceDecl& id)
{
    if (id.TestAttr(Attribute::IS_CHECK_VISITED)) {
        return;
    }
    // Mark the current declaration is checked.
    id.EnableAttr(Attribute::IS_CHECK_VISITED);
    // Do type check for all implemented interfaces.
    for (auto& interfaceType : id.inheritedTypes) {
        Synthesize(ctx, interfaceType.get());
        if (auto it = DynamicCast<InterfaceTy*>(interfaceType->ty); it) {
            it->decl->subDecls.insert(&id);
            CheckSealedInheritance(id, *interfaceType);
            CheckThreadContextInheritance(id, *interfaceType);
        } else {
            // Can only implement interface.
            diag.Diagnose(id, DiagKind::sema_interface_inherit_non_interface, id.identifier.Val());
        }
    }
    TypeCheckCompositeBody(ctx, id, id.body->decls);
    if (id.TestAnyAttr(Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE)) {
        CheckJavaInteropLibImport(id);
    }
    if (id.TestAnyAttr(Attribute::OBJ_C_MIRROR, Attribute::OBJ_C_MIRROR_SUBTYPE)) {
        CheckObjCInteropLibImport(id);
    }
}
