// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "InheritanceChecker.h"

#include "NativeFFI/Java/AfterTypeCheck/Utils.h"
#include "cangjie/AST/Clone.h"

namespace Cangjie::Interop::Java {

namespace {

std::string GetAnnoValue(Ptr<Annotation> anno)
{
    CJC_ASSERT(anno);
    CJC_ASSERT(anno->args.size() == 1);

    auto litExpr = DynamicCast<LitConstExpr>(anno->args[0]->expr.get());
    CJC_ASSERT(litExpr);
    return litExpr->stringValue;
}

void DiagConflictingForeignName(
    DiagnosticEngine& diag, const Decl& declWithAnno, const Decl& otherDecl, const Decl& checkingDecl)
{
    auto anno = GetForeignNameAnnotation(declWithAnno);
    CJC_ASSERT(anno);

    auto builder = [&diag, &anno, &declWithAnno]() {
        if (!anno->TestAttr(Attribute::COMPILER_ADD)) {
            auto declWithAnnoRange = MakeRange(anno->GetBegin(), declWithAnno.identifier.End());
            return diag.DiagnoseRefactor(DiagKindRefactor::sema_foreign_name_conflicting_annotation, declWithAnno,
                declWithAnnoRange, declWithAnno.identifier, anno->identifier, anno->identifier);
        } else {
            return diag.DiagnoseRefactor(DiagKindRefactor::sema_foreign_name_conflicting_derived_annotation,
                declWithAnno, MakeRange(declWithAnno.identifier), declWithAnno.identifier, anno->identifier, GetAnnoValue(anno));
        }
    }();

    auto otherAnno = GetForeignNameAnnotation(otherDecl);
    if (otherAnno && !otherAnno->TestAttr(Attribute::COMPILER_ADD)) {
        auto otherDeclRange = MakeRange(otherAnno->GetBegin(), otherDecl.identifier.End());
        builder.AddNote(
            otherDecl, otherDeclRange, "Other declaration '" + otherDecl.identifier + "' has a different @ForeignName");
    } else if (otherAnno) {
        builder.AddNote(otherDecl, MakeRange(otherDecl.identifier),
            "Other declaration '" + otherDecl.identifier + "' has a different derived @ForeignName '" +
                GetAnnoValue(otherAnno) + "'");
    } else {
        auto otherDeclRange = MakeRange(otherDecl.identifier);
        builder.AddNote(
            otherDecl, otherDeclRange, "Other declaration '" + otherDecl.identifier + "' doesn't have a @ForeignName");
    }

    builder.AddNote(checkingDecl, MakeRange(checkingDecl.identifier),
        "While checking declaration '" + checkingDecl.identifier + "'");
}

bool NeedCheck(const MemberSignature& parent, const MemberSignature& child)
{
    if (child.decl->outerDecl->TestAttr(Attribute::IMPORTED)) {
        return false;
    }
    if (!parent.decl->IsFuncOrProp()) {
        return false;
    }
    CJC_ASSERT(child.decl->IsFuncOrProp());

    if (!parent.decl->outerDecl->TestAnyAttr(Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE)) {
        return false;
    }
    if (!child.decl->outerDecl->TestAnyAttr(Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE)) {
        // @JavaMirror anottation might be missing here, will report it later
        return false;
    }
    if (parent.decl->outerDecl == child.decl->outerDecl) {
        return false;
    }

    return true;
}

} // namespace

void CheckForeignName(DiagnosticEngine& diag, TypeManager& typeManager, const MemberSignature& parent,
    const MemberSignature& child, const Decl& checkingDecl)
{
    if (!NeedCheck(parent, child)) {
        return;
    }

    auto childAnno = GetForeignNameAnnotation(*child.decl);
    auto parentAnno = GetForeignNameAnnotation(*parent.decl);
    if (!childAnno && !parentAnno) {
        return;
    }

    if (!typeManager.IsSubtype(child.structTy, parent.structTy)) {
        if (!childAnno && parentAnno) {
            DiagConflictingForeignName(diag, *parent.decl, *child.decl, checkingDecl);
        } else if (!parentAnno && childAnno) {
            DiagConflictingForeignName(diag, *child.decl, *parent.decl, checkingDecl);
        } else if (GetAnnoValue(childAnno) != GetAnnoValue(parentAnno)) {
            DiagConflictingForeignName(diag, *parent.decl, *child.decl, checkingDecl);
        }
        return;
    }

    if (childAnno && !childAnno->TestAttr(Attribute::COMPILER_ADD)) {
        auto range = MakeRange(childAnno->GetBegin(), child.decl->identifier.End());
        diag.DiagnoseRefactor(DiagKindRefactor::sema_foreign_name_appeared_in_child, *child.decl, range, childAnno->identifier);
    } else if (childAnno && !parentAnno) {
        DiagConflictingForeignName(diag, *child.decl, *parent.decl, checkingDecl);
    } else if (!childAnno && parentAnno && child.replaceOther) {
        // NOTE: When replaceOther is true, then this method is overriding some other parent one
        // And if there is no ForeignName, then that parent also hadn't ForeignName. But current
        // parent do have it
        DiagConflictingForeignName(diag, *parent.decl, *child.decl, checkingDecl);
    } else if (!childAnno && parentAnno) {
        auto clonedAnno = ASTCloner::Clone(parentAnno);
        clonedAnno->EnableAttr(Attribute::COMPILER_ADD);
        CopyBasicInfo(child.decl, clonedAnno.get());
        child.decl->annotations.emplace_back(std::move(clonedAnno));
    } else if (GetAnnoValue(childAnno) != GetAnnoValue(parentAnno)) {
        DiagConflictingForeignName(diag, *parent.decl, *child.decl, checkingDecl);
    }
}

} // namespace Cangjie::Interop::Java
