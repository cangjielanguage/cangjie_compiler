// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements utility class that process inheritance of structured types.
 */

#include "OCStructInheritanceCheckerImpl.h"
#include "../../Utils.h"
#include "InheritanceChecker/StructInheritanceChecker.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Basic/DiagnosticEngine.h"

using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI;

namespace Cangjie::Interop::ObjC {

namespace {

std::string GetAnnoValue(Ptr<Annotation> anno)
{
    CJC_ASSERT(anno);
    CJC_ASSERT(anno->args.size() == 1);

    auto litExpr = DynamicCast<LitConstExpr>(anno->args[0]->expr.get());
    CJC_ASSERT(litExpr);
    return litExpr->stringValue;
}

void DiagConflictingAnnotation(
    DiagnosticEngine& diag, const Decl& declWithAnno, const Decl& otherDecl, const Decl& checkingDecl, AnnotationKind annotationKind)
{
    auto anno = GetAnnotation(declWithAnno, annotationKind);
    CJC_ASSERT(anno);

    auto builder = [&diag, &anno, &declWithAnno]() {
        if (!anno->TestAttr(Attribute::COMPILER_ADD)) {
            auto declWithAnnoRange = MakeRange(anno->GetBegin(), declWithAnno.identifier.End());
            return diag.DiagnoseRefactor(DiagKindRefactor::sema_foreign_name_conflicting_annotation, declWithAnno,
                declWithAnnoRange, declWithAnno.identifier, anno->identifier);
        } else {
            return diag.DiagnoseRefactor(DiagKindRefactor::sema_foreign_name_conflicting_derived_annotation,
                declWithAnno, MakeRange(declWithAnno.identifier), declWithAnno.identifier, anno->identifier, GetAnnoValue(anno));
        }
    }();

    auto otherAnno = GetForeignNameAnnotation(otherDecl);
    if (otherAnno && !otherAnno->TestAttr(Attribute::COMPILER_ADD)) {
        auto otherDeclRange = MakeRange(otherAnno->GetBegin(), otherDecl.identifier.End());
        builder.AddNote(
            otherDecl, otherDeclRange, "Other declaration '" + otherDecl.identifier + "' has a different @" + anno->identifier);
    } else if (otherAnno) {
        builder.AddNote(otherDecl, MakeRange(otherDecl.identifier),
            "Other declaration '" + otherDecl.identifier + "' has a different derived @" + anno->identifier + " '" +
                GetAnnoValue(otherAnno) + "'");
    } else {
        auto otherDeclRange = MakeRange(otherDecl.identifier);
        builder.AddNote(
            otherDecl, otherDeclRange, "Other declaration '" + otherDecl.identifier + "' doesn't have a @" + anno->identifier);
    }

    builder.AddNote(checkingDecl, MakeRange(checkingDecl.identifier),
        "While checking declaration '" + checkingDecl.identifier + "'");
}

bool NeedCheckForeignName(const MemberSignature& parent, const MemberSignature& child)
{
    if (child.decl->outerDecl->TestAttr(Attribute::IMPORTED)) {
        return false;
    }
    if (!parent.decl->IsFuncOrProp()) {
        return false;
    }
    CJC_ASSERT(child.decl->IsFuncOrProp());

    if (!parent.decl->outerDecl->TestAnyAttr(Attribute::OBJ_C_MIRROR, Attribute::OBJ_C_MIRROR_SUBTYPE)) {
        return false;
    }
    if (!child.decl->outerDecl->TestAnyAttr(Attribute::OBJ_C_MIRROR, Attribute::OBJ_C_MIRROR_SUBTYPE)) {
        // @ObjCMirror anottation might be missing here, will report it later
        return false;
    }
    if (parent.decl->outerDecl == child.decl->outerDecl) {
        return false;
    }

    return true;
}

} // namespace

void CheckAnnotation(DiagnosticEngine& diag, TypeManager& typeManager, AnnotationKind annotationKind,
    const MemberSignature& parent, const MemberSignature& child, const Decl& checkingDecl)
{
    if (!NeedCheckForeignName(parent, child)) {
        return;
    }

    auto childAnno = GetAnnotation(*child.decl, annotationKind);
    auto parentAnno = GetAnnotation(*parent.decl, annotationKind);
    if (!childAnno && !parentAnno) {
        return;
    }

    if (!typeManager.IsSubtype(child.structTy, parent.structTy)) {
        if (!childAnno && parentAnno) {
            DiagConflictingAnnotation(diag, *parent.decl, *child.decl, checkingDecl, annotationKind);
        } else if (!parentAnno && childAnno) {
            DiagConflictingAnnotation(diag, *child.decl, *parent.decl, checkingDecl, annotationKind);
        } else if (GetAnnoValue(childAnno) != GetAnnoValue(parentAnno)) {
            DiagConflictingAnnotation(diag, *parent.decl, *child.decl, checkingDecl, annotationKind);
        }
        return;
    }

    if (childAnno && !childAnno->TestAttr(Attribute::COMPILER_ADD)) {
        auto range = MakeRange(childAnno->GetBegin(), child.decl->identifier.End());
        diag.DiagnoseRefactor(DiagKindRefactor::sema_foreign_name_appeared_in_child, *child.decl, range, childAnno->identifier);
    } else if (childAnno && !parentAnno) {
        DiagConflictingAnnotation(diag, *child.decl, *parent.decl, checkingDecl, annotationKind);
    } else if (!childAnno && parentAnno && child.replaceOther) {
        // NOTE: When replaceOther is true, then this method is overriding some other parent one
        // And if there is no ForeignName, then that parent also hadn't ForeignName. But current
        // parent do have it
        DiagConflictingAnnotation(diag, *parent.decl, *child.decl, checkingDecl, annotationKind);
    } else if (!childAnno && parentAnno) {
        auto clonedAnno = ASTCloner::Clone(parentAnno);
        clonedAnno->EnableAttr(Attribute::COMPILER_ADD);
        CopyBasicInfo(child.decl, clonedAnno.get());
        child.decl->annotations.emplace_back(std::move(clonedAnno));
    } else if (GetAnnoValue(childAnno) != GetAnnoValue(parentAnno)) {
        DiagConflictingAnnotation(diag, *parent.decl, *child.decl, checkingDecl, annotationKind);
    }
}

void CheckForeignAnnotations(DiagnosticEngine& diag, TypeManager& typeManager, const MemberSignature& parent,
    const MemberSignature& child, const Decl& checkingDecl)
{
    CheckAnnotation(diag, typeManager, AnnotationKind::FOREIGN_NAME, parent, child, checkingDecl);
    CheckAnnotation(diag, typeManager, AnnotationKind::FOREIGN_GETTER_NAME, parent, child, checkingDecl);
    CheckAnnotation(diag, typeManager, AnnotationKind::FOREIGN_SETTER_NAME, parent, child, checkingDecl);
}

namespace {

/**
 * @brief Generates a synthetic function stub based on an existing function declaration.
 *
 * This function creates a clone of the provided function declaration (fd),
 * replaces its outerDecl to synthetic class, and then inserts the
 * modified function declaration into the specified synthetic class declaration.
 *
 * @param synthetic The class declaration where the cloned function stub will be inserted.
 * @param fd The original function declaration that will be cloned and modified.
 */
void GenerateSyntheticClassFuncStub(ClassDecl& synthetic, FuncDecl& fd)
{
    OwnedPtr<FuncDecl> funcStub = ASTCloner::Clone(Ptr(&fd));

    // remove foreign anno from cloned func decl
    for (auto it = funcStub->annotations.begin(); it != funcStub->annotations.end(); ++it) {
        if ((*it)->kind == AnnotationKind::FOREIGN_NAME) {
            funcStub->annotations.erase(it);
            break;
        }
    }

    funcStub->outerDecl = Ptr(&synthetic);
    synthetic.body->decls.emplace_back(std::move(funcStub));
}

void GenerateSyntheticClassPropStub([[maybe_unused]] ClassDecl& synthetic, [[maybe_unused]] PropDecl& pd)
{
    auto propStub = ASTCloner::Clone(Ptr(&pd));

    // remove foreign anno from cloned func decl
    for (auto it = propStub->annotations.begin(); it != propStub->annotations.end(); ++it) {
        if ((*it)->kind == AnnotationKind::FOREIGN_NAME) {
            propStub->annotations.erase(it);
            break;
        }
    }

    propStub->outerDecl = Ptr(&synthetic);
    synthetic.body->decls.emplace_back(std::move(propStub));
}

void GenerateSyntheticClassAbstractMemberImplStubs(ClassDecl& synthetic, const MemberMap& members)
{
    for (const auto& idMemberSignature : members) {
        const auto& signature = idMemberSignature.second;

        // only abstract functions must be inside synthetic class
        if (!signature.decl->TestAttr(Attribute::ABSTRACT)) {
            continue;
        }

        switch (signature.decl->astKind) {
            case ASTKind::FUNC_DECL:
                GenerateSyntheticClassFuncStub(synthetic, *StaticAs<ASTKind::FUNC_DECL>(signature.decl));
                break;
            case ASTKind::PROP_DECL:
                GenerateSyntheticClassPropStub(synthetic, *StaticAs<ASTKind::PROP_DECL>(signature.decl));
                break;
            default:
                continue;
        }
    }
}

} // namespace

void GenerateSyntheticClassMemberStubs(
    ClassDecl& synthetic, const MemberMap& interfaceMembers, const MemberMap& instanceMembers)
{
    GenerateSyntheticClassAbstractMemberImplStubs(synthetic, interfaceMembers);
    GenerateSyntheticClassAbstractMemberImplStubs(synthetic, instanceMembers);
}

} // namespace Cangjie::Interop::ObjC