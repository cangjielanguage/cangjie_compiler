// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements diagnostics for type checker.
 */

#include "Diags.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "TypeCheckUtil.h"

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;

namespace {
std::string GetParamsType(const std::vector<Ptr<Ty>>& typeList)
{
    std::string result = "(";
    for (auto& ty : typeList) {
        std::string tyName = ty != nullptr ? ty->String() : "UnknownType";
        result += tyName + ", ";
    }
    if (!typeList.empty()) {
        result.erase(result.size() - 2, 2); // Erase last extra ", " which length is 2.
    }
    result += ")";
    return result;
}

void DiagWrongNumberOfArgumentsCommon(
    DiagnosticEngine& diag, const CallExpr& ce, const std::vector<Ptr<Ty>>& paramTys, Ptr<const FuncDecl> fd)
{
    auto expectedNum = paramTys.size();
    auto foundNum = ce.args.size();
    CJC_ASSERT(expectedNum != foundNum);
    std::string symptom;
    if (expectedNum > foundNum) {
        symptom = expectedNum - foundNum == 1 ? "missing argument" : "missing arguments";
    } else {
        symptom = foundNum - expectedNum == 1 ? "extra argument given" : "extra arguments given";
    }
    std::string lst = GetParamsType(paramTys);
    std::string expected = std::to_string(expectedNum) + (expectedNum == 1 ? " argument" : " arguments");
    // If the position of "()" exists, using paren positions' range, otherwise using the range of 'ce' begin to end.
    bool parenPosExist = !ce.leftParenPos.IsZero() && !ce.rightParenPos.IsZero();
    auto beginPos = parenPosExist ? ce.leftParenPos : ce.begin;
    // For trailing lambda, the last argument is after 'rightParenPos', using the end of lambda as the range end.
    bool lastArgAfterParen = !ce.args.empty() && ce.args.back() && ce.args.back()->end > ce.rightParenPos;
    auto endPos = parenPosExist ? (lastArgAfterParen ? ce.args.back()->end : ce.rightParenPos + 1) : ce.end;
    auto builder = diag.DiagnoseRefactor(
        DiagKindRefactor::sema_wrong_number_of_arguments, ce, MakeRange(beginPos, endPos), symptom, lst);
    builder.AddMainHintArguments(expected, std::to_string(foundNum));
    if (fd != nullptr && fd->ShouldDiagnose()) {
        builder.AddNote(*fd, MakeRange(fd->identifier), "found candidate");
    } else if (fd == nullptr) {
        Ptr<const Decl> target = ce.baseFunc->GetTarget();
        if (target == nullptr) {
            return;
        } else if (auto vd = DynamicCast<const VarDecl*>(target); vd && vd->ShouldDiagnose()) {
            builder.AddNote(*vd, MakeRange(vd->identifier), "found candidate");
        }
    }
}

std::set<size_t> GetRestNamedParams(const std::vector<OwnedPtr<FuncParam>>& params, size_t offset)
{
    std::set<size_t> uncheckedNamedParams{};
    for (size_t i = offset; i < params.size(); ++i) {
        if (params[i]->isNamedParam) {
            uncheckedNamedParams.insert(i);
        }
    }
    return uncheckedNamedParams;
}

std::set<std::string> GetRestArgNames(
    const FuncDecl& fd, const std::vector<OwnedPtr<Cangjie::AST::FuncArg>>& args, size_t offset)
{
    std::set<std::string> uncheckedArgNames{};
    for (size_t i = offset; i < args.size(); ++i) {
        CJC_NULLPTR_CHECK(args[i]);
        std::string name = GetArgName(fd, *args[i]);
        if (!name.empty()) {
            uncheckedArgNames.emplace(name);
        }
    }
    return uncheckedArgNames;
}
} // namespace

namespace Cangjie::Sema {
Range MakeRangeForDeclIdentifier(const Decl& decl)
{
    // Optimize range for `init`.
    if (decl.astKind == ASTKind::FUNC_DECL && decl.TestAttr(Attribute::CONSTRUCTOR)) {
        CJC_NULLPTR_CHECK(decl.outerDecl);
        const Decl& outerDecl = *decl.outerDecl;
        if (decl.identifier.ZeroPos()) {
            // Compiler added default `init`.
            return MakeRange(outerDecl.identifier);
        } else if (decl.TestAttr(Attribute::PRIMARY_CONSTRUCTOR)) {
            // Primary constructor's user visible identifier is the type name.
            return MakeRange(decl.identifier.Begin(), outerDecl.identifier);
        } else {
            return MakeRange(decl.identifier);
        }
    } else if (auto ed = DynamicCast<const ExtendDecl*>(&decl); ed && ed->extendedType) {
        // Recheck for decls from common part, we use the position of the decl due to no other available positions.
        if (ed->TestAttr(AST::Attribute::FROM_COMMON_PART)) {
            return MakeRange(ed->begin, ed->end);
        }
        return MakeRange(ed->extendedType->begin, ed->extendedType->end);
    } else if (auto vpd = DynamicCast<const VarWithPatternDecl*>(&decl); vpd && vpd->irrefutablePattern) {
        return MakeRange(vpd->irrefutablePattern->begin, vpd->irrefutablePattern->end);
    }
    if (decl.identifier.ZeroPos()) {
        return MakeRange(decl.begin, decl.end);
    } else if (auto fd = DynamicCast<const FuncDecl*>(&decl); fd && (fd->isGetter || fd->isSetter)) {
        return MakeRange(decl.identifier.Begin(), decl.identifier.Begin() + 3); // `get` and `set` are 3 letter words
    } else {
        return MakeRange(decl.identifier);
    }
}

void DiagRedefinitionWithFoundNode(DiagnosticEngine& diag, const Decl& current, const Decl& previous)
{
    if (current.TestAttr(Attribute::IS_BROKEN)) {
        return;
    }
    CJC_ASSERT(current.identifier == previous.identifier);
    const Decl* declUp = &previous;
    const Decl* declDown = &current;
    auto rangeUp = MakeRangeForDeclIdentifier(previous);
    auto rangeDown = MakeRangeForDeclIdentifier(current);
    // NOTE: when one is private global, we need report private decl conflict against non-private version.
    if (rangeUp.begin > rangeDown.begin || (previous.TestAttr(Attribute::GLOBAL, Attribute::PRIVATE) &&
        current.curFile != previous.curFile)) {
        const Decl* tmpDecl = declUp;
        declUp = declDown;
        declDown = tmpDecl;
        auto tmpRange = rangeUp;
        rangeUp = rangeDown;
        rangeDown = tmpRange;
    }
    auto builder = diag.DiagnoseRefactor(
        DiagKindRefactor::sema_redefinition, *declDown, rangeDown, declDown->identifier);
    builder.AddNote(*declUp, rangeUp, "'" + declDown->identifier + "' is previously declared here");
}

void DiagOverloadConflict(DiagnosticEngine& diag, const std::vector<Ptr<FuncDecl>>& sameSigFuncs)
{
    // If the 'sameSigFuncs' has size 1, then the function is conflicting with imported declaration,
    // and the error for imported declaration will be generated later.
    CJC_ASSERT(sameSigFuncs.size() >= 1);
    auto baseFd = sameSigFuncs.front();
    auto getIdentifier =
        [](auto fd) { return fd->identifierForLsp.empty() ? fd->identifier.Val() : fd->identifierForLsp; };
    auto identifier = getIdentifier(baseFd);
    std::string kind = "function";
    if (baseFd->TestAttr(Attribute::MACRO_FUNC)) {
        kind = "macro";
    } else if (IsClassOrEnumConstructor(*baseFd)) {
        kind = "constructor";
    } else if (IsStaticInitializer(*baseFd)) {
        kind = "static constructor";
    }
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_overload_conflicts, *baseFd,
        MakeRange(baseFd->identifier.Begin(), identifier), kind, identifier);
    auto hasConstraints = [](auto fd) {
        auto generic = fd->GetGeneric();
        return generic && !generic->genericConstraints.empty();
    };
    if (std::any_of(sameSigFuncs.begin(), sameSigFuncs.end(), hasConstraints)) {
        builder.AddNote("generic constraints are not involved in the overloading");
    }
    for (auto it = sameSigFuncs.begin() + 1; it != sameSigFuncs.end(); ++it) {
        auto fd = *it;
        builder.AddNote(*fd, MakeRange(fd->identifier.Begin(), getIdentifier(fd)), "conflict with the declaration");
    }
}

void DiagMismatchedTypesWithFoundTy(DiagnosticEngine& diag, const Node& node, const std::string& expected,
    const std::string& found, const std::string& note)
{
    if (!node.ShouldDiagnose(true)) {
        return;
    }
    CJC_ASSERT(expected != found);
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_mismatched_types, node);
    builder.AddMainHintArguments(expected, found);
    if (!note.empty()) {
        builder.AddNote(note);
    }
}

void DiagMismatchedTypesWithFoundTy(
    DiagnosticEngine& diag, const Node& node, const Ty& expected, const Ty& found, const std::string& note)
{
    if (!node.ShouldDiagnose(true)) {
        return;
    }
    CJC_ASSERT(Ty::IsTyCorrect(&expected));
    CJC_ASSERT(Ty::IsTyCorrect(&found));
    std::string expectedStr = expected.String();
    std::string foundStr = found.String();
    if (expectedStr == foundStr) {
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_mismatched_types, node);
        Ptr<Decl> expectedDecl = Ty::GetDeclPtrOfTy(&expected);
        if (expectedDecl) {
            expectedStr += "' in '" + expectedDecl->fullPackageName;
            builder.AddNote(*expectedDecl, MakeRangeForDeclIdentifier(*expectedDecl), "'" + expectedStr + "'");
        }
        Ptr<Decl> foundDecl = Ty::GetDeclPtrOfTy(&found);
        if (foundDecl) {
            foundStr += "' in '" + foundDecl->fullPackageName;
            builder.AddNote(*foundDecl, MakeRangeForDeclIdentifier(*foundDecl), "'" + foundStr + "'");
        }
        builder.AddMainHintArguments(expectedStr, foundStr);
        if (!note.empty()) {
            builder.AddNote(note);
        }
    } else {
        DiagMismatchedTypesWithFoundTy(diag, node, expectedStr, foundStr, note);
    }
}

void DiagMismatchedTypes(DiagnosticEngine& diag, const Node& node, const Ty& type, const std::string& note)
{
    if (!Ty::IsTyCorrect(node.ty)) {
        return; // Should have been diagnosed before.
    }
    DiagMismatchedTypesWithFoundTy(diag, node, type, *node.ty, note);
}

void DiagMismatchedTypes(DiagnosticEngine& diag, const Node& node, const Node& type, const std::string& because)
{
    if (!Ty::IsTyCorrect(node.ty)) {
        return; // Should have been diagnosed before.
    }
    CJC_ASSERT(Ty::IsTyCorrect(type.ty));
    if (type.ShouldDiagnose() && !because.empty()) {
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_mismatched_types_because, node);
        builder.AddMainHintArguments(type.ty->String(), node.ty->String());
        builder.AddHint(type, type.ty->String(), because);
    } else {
        DiagMismatchedTypesWithFoundTy(diag, node, *type.ty, *node.ty);
    }
}

void DiagUnableToInferReturnType(DiagnosticEngine& diag, const FuncDecl& fd)
{
    if (Ty::IsTyCorrect(fd.ty)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_unable_to_infer_return_type, fd, MakeRange(fd.identifier));
    }
}

void DiagUnableToInferReturnType(DiagnosticEngine& diag, const FuncDecl& fd, const Expr& expr)
{
    if (Ty::IsTyCorrect(fd.ty)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_unable_to_infer_return_type, fd, MakeRange(fd.identifier))
            .AddNote(expr, MakeRange(expr.begin, expr.end), "with recursive usage from");
    }
}

void DiagUnableToInferReturnType(DiagnosticEngine& diag, const FuncBody& fb)
{
    if (fb.funcDecl == nullptr) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_unable_to_infer_return_type, fb);
    } else {
        DiagUnableToInferReturnType(diag, *fb.funcDecl);
    }
}

void DiagWrongNumberOfArguments(DiagnosticEngine& diag, const CallExpr& ce, const std::vector<Ptr<Ty>>& paramTys)
{
    if (!ce.ShouldDiagnose(true)) {
        return;
    }
    DiagWrongNumberOfArgumentsCommon(diag, ce, paramTys, nullptr);
}

void DiagWrongNumberOfArguments(DiagnosticEngine& diag, const CallExpr& ce, const FuncDecl& fd)
{
    if (!ce.ShouldDiagnose(true)) {
        return;
    }
    CJC_NULLPTR_CHECK(fd.funcBody);
    CJC_ASSERT(!fd.funcBody->paramLists.empty());
    CJC_NULLPTR_CHECK(fd.funcBody->paramLists.front());
    auto& params = fd.funcBody->paramLists.front()->params;
    std::vector<Ptr<Ty>> paramTys;
    std::transform(params.cbegin(), params.cend(), std::back_inserter(paramTys),
        [](auto& param) { return param->type == nullptr ? param->ty : param->type->ty; });
    DiagWrongNumberOfArgumentsCommon(diag, ce, paramTys, &fd);
}

void DiagGenericFuncWithoutTypeArg(DiagnosticEngine& diag, const Expr& expr)
{
    if (!expr.ShouldDiagnose()) {
        return;
    }
    auto range = MakeRange(expr.begin, expr.end);
    if (auto ma = DynamicCast<const MemberAccess*>(&expr); ma) {
        range = MakeRange(ma->field);
    }
    auto name = expr.symbol == nullptr ? "" : " '" + expr.symbol->name + "'";
    diag.DiagnoseRefactor(DiagKindRefactor::sema_generic_func_without_type_arg, expr, range, name);
}

void DiagStaticAndNonStaticOverload(DiagnosticEngine& diag, const FuncDecl& fd, const FuncDecl& firstNonStatic)
{
    if (!fd.ShouldDiagnose()) {
        return;
    }
    auto builder = diag.DiagnoseRefactor(
        DiagKindRefactor::sema_static_function_overload_conflicts, fd, MakeRange(fd.identifier), fd.identifier);
    builder.AddNote(firstNonStatic, MakeRange(firstNonStatic.identifier), "non-static function is here");
}

void DiagImmutableAccessMutableFunc(DiagnosticEngine& diag, const MemberAccess& outerMa, const MemberAccess& ma)
{
    CJC_NULLPTR_CHECK(ma.baseExpr);
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_immutable_access_mutable_func, *ma.baseExpr);
    auto mutFuncPos = outerMa.GetFieldPos();
    if (!mutFuncPos.IsZero() && !IsFieldOperator(ma.field)) {
        builder.AddHint(MakeRange(mutFuncPos, outerMa.field), "is a mutable function");
    }
    if (auto vd = DynamicCast<const VarDecl*>(ma.baseExpr->GetTarget())) {
        if (!vd->isVar && !vd->identifier.ZeroPos()) {
            auto letOrConst = vd->IsConst() ? "const" : "let";
            builder.AddNote(*vd, MakeRange(vd->identifier),
                "'" + vd->identifier + "' is a variable declared with '" + letOrConst + "'");
        } else if (auto pd = DynamicCast<const PropDecl*>(vd);
                   pd && pd->getters.size() == 1 && pd->getters.front() && !pd->getters.front()->begin.IsZero()) {
            auto& get = *pd->getters.front();
            builder.AddNote(get, MakeRangeForDeclIdentifier(get), "property getter returns immutable value");
        }
    }
}

// `ae` can be `AssignExpr` or `IncOrDecExpr`.
void DiagCannotAssignToImmutable(DiagnosticEngine& diag, const Expr& ae, const Expr& perpetrator)
{
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_cannot_assign_to_immutable, ae);
    auto target = perpetrator.GetTarget();
    if (target != nullptr && !target->identifier.ZeroPos()) {
        builder.AddNote(*target, MakeRange(target->identifier),
            DeclKindToString(*target) + " '" + target->identifier + "' is immutable");
    } else if (perpetrator.astKind == ASTKind::CALL_EXPR) {
        builder.AddNote(
            perpetrator, MakeRange(perpetrator.begin, perpetrator.end), "function call returns immutable value");
    }
}

void DiagCannotOverride(DiagnosticEngine& diag, const Decl& child, const Decl& parent)
{
    std::string kind = DeclKindToString(child);
    std::string name = child.identifier;
    auto childRange = child.identifier.ZeroPos() ? MakeRange(child.begin, child.end) : MakeRange(child.identifier);
    auto parentRange =
        parent.identifier.ZeroPos() ? MakeRange(parent.begin, parent.end) : MakeRange(parent.identifier.Begin(), name);
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_cannot_override, child, childRange, kind, name);
    builder.AddNote(parent, parentRange, "the parent " + kind + " is not modified by 'open'");
}

void DiagCannotHaveDefaultParam(DiagnosticEngine& diag, const FuncDecl& fd, const FuncParam& fp)
{
    if (fp.TestAttr(Attribute::IS_BROKEN)) {
        return;
    }
    std::string funcType;
    if (fd.op != TokenKind::ILLEGAL) {
        funcType = "operator overloading";
    } else if (fd.TestAttr(Attribute::OPEN)) {
        funcType = "'open'";
    } else {
        funcType = "abstract";
    }
    (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_cannot_have_default_param, fp, funcType);
}

void DiagCannotInheritSealed(DiagnosticEngine& diag, const Decl& child, const Type& sealed, const bool& isCommon)
{
    Ptr<const Decl> target = sealed.GetTarget();
    CJC_NULLPTR_CHECK(target);
    bool isImplement = child.astKind == ASTKind::CLASS_DECL && target->astKind == ASTKind::INTERFACE_DECL;
    auto range = MakeRange(child.identifier);
    if (child.astKind == ASTKind::EXTEND_DECL) {
        const ExtendDecl& ed = StaticCast<const ExtendDecl&>(child);
        if (ed.extendedType) {
            Ptr<const Decl> extendedDecl = ed.extendedType->GetTarget();
            if (extendedDecl && extendedDecl->astKind != ASTKind::INTERFACE_DECL) {
                isImplement = true;
            }
            range = MakeRange(ed.extendedType->begin, ed.extendedType->end);
        }
    }
    const std::string inheritOrImplement = isImplement ? "implement" : "inherit";
    const std::string importedOrCommon = isCommon ? "common-defined" : "imported";
    const std::string classOrInterface = target->astKind == ASTKind::CLASS_DECL ? "class" : "interface";
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_cannot_inherit_sealed,
        range, inheritOrImplement, importedOrCommon, classOrInterface, target->identifier);
    const std::string declaredPos = isCommon ? "common package part" : ("package '" + target->fullPackageName + "'");
    builder.AddHint(sealed, "sealed " + classOrInterface + " declared in " + declaredPos);
}

void DiagPackageMemberNotFound(
    DiagnosticEngine& diag, const ImportManager& importManager, const MemberAccess& ma, const PackageDecl& pd)
{
    auto range = ma.field.ZeroPos() ? MakeRange(ma.begin, ma.end) : MakeRange(ma.field);
    auto decls = importManager.GetPackageMembersByName(*pd.srcPackage, ma.field);
    if (decls.empty()) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_not_member_of, ma, range, ma.field, "package", pd.identifier);
    } else {
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_member_not_imported, ma, range, pd.identifier + "." + ma.field);
    }
}

void DiagAmbiguousUse(
    DiagnosticEngine& diag, const Node& node, const std::string& name, std::vector<Ptr<Decl>>& targets,
    const ImportManager& importManager)
{
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_ambiguous_use, node, name);
    std::sort(targets.begin(), targets.end(), CompNodeByPos);
    CJC_ASSERT(targets.size() > 1);
    std::vector<Ptr<Decl>> importedTargets;
    for (auto target : targets) {
        CJC_NULLPTR_CHECK(target);
        if (target->TestAttr(Attribute::IMPORTED)) {
            importedTargets.emplace_back(target);
        } else {
            builder.AddNote(*target, MakeRangeForDeclIdentifier(*target), "found candidate");
        }
    }
    CJC_ASSERT(node.curFile && node.curFile->curPackage);
    AddDiagNotesForImportedDecls(
        builder, importManager.GetImportsOfDecl(node.curFile->curPackage->fullPackageName), importedTargets);
}

namespace {
std::string GetNoteMessageForMemberDecl(const Decl& target)
{
    std::string ret = "found ";
    CJC_NULLPTR_CHECK(target.outerDecl);
    if (target.outerDecl->astKind == ASTKind::EXTEND_DECL) {
        ret += "extended ";
    }
    ret += DeclKindToString(target) + " member of '" + target.outerDecl->ty->String() + "'";
    return ret;
}
} // namespace

void AddDiagNotesForImportedDecls(DiagnosticBuilder& builder, const ImportManager::DeclImportsMap& declToImports,
    const std::vector<Ptr<Decl>>& targets)
{
    std::set<Ptr<const ImportSpec>, CmpNodeByPos> imports;
    std::set<std::string> implicitImports;
    for (auto target : targets) {
        CJC_NULLPTR_CHECK(target);
        if (!target->TestAttr(Attribute::GLOBAL) && target->astKind != ASTKind::PACKAGE_DECL) {
            // Imported non-global decl must be member decl.
            CJC_ASSERT(Is<InheritableDecl>(target->outerDecl));
            builder.AddNote(*target, MakeRangeForDeclIdentifier(*target), GetNoteMessageForMemberDecl(*target));
        }
        auto found = declToImports.find(target);
        if (found == declToImports.end()) {
            continue;
        }
        for (auto import : found->second) {
            if (import->TestAttr(Attribute::IMPLICIT_ADD)) {
                implicitImports.emplace(target->fullPackageName);
            } else {
                imports.emplace(import);
            }
        }
    }
    for (auto import : imports) {
        builder.AddNote(*import, MakeRange(import->begin, import->end), "found imported candidate");
    }
    for (auto implicitPkg : implicitImports) {
        builder.AddNote("found candidate from implicit imported pacakge '" + implicitPkg + "'");
    }
}

void DiagInvalidAssign(DiagnosticEngine& diag, const Node& node, const std::string& name)
{
    auto range = MakeRange(node.begin, node.end);
    (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_unqualified_left_value_assigned, node, range, name);
}

void DiagLowerAccessLevelTypesUse(DiagnosticEngine& diag, const Decl& outDecl,
    const std::vector<std::pair<Node&, Decl&>>& limitedDecls, const std::vector<Ptr<Decl>>& hintDecls)
{
    bool noHint = hintDecls.empty();
    if (limitedDecls.empty() && noHint) {
        return;
    }
    auto kind = noHint ? DiagKindRefactor::sema_accessibility : DiagKindRefactor::sema_accessibility_with_main_hint;
    auto range = MakeRangeForDeclIdentifier(outDecl);
    std::string lowerLevelStr = limitedDecls.size() + hintDecls.size() != 1
        ? "lower access level"
        : GetAccessLevelStr(limitedDecls.empty() ? *hintDecls.front().get() : limitedDecls.front().second, "'");
    auto builder = diag.DiagnoseRefactor(kind, outDecl, range, GetAccessLevelStr(outDecl), lowerLevelStr);

    if (!noHint && Ty::IsTyCorrect(outDecl.ty)) {
        builder.AddMainHintArguments("inferred type",
            outDecl.ty->IsFunc() ? Ty::ToString(StaticCast<FuncTy*>(outDecl.ty)->retTy) : Ty::ToString(outDecl.ty),
            lowerLevelStr);
        for (const auto& hintDecl : hintDecls) {
            CJC_ASSERT(hintDecl);
            auto inDeclRange = MakeRangeForDeclIdentifier(*hintDecl);
            builder.AddNote(*hintDecl, inDeclRange,
                "the " + GetAccessLevelStr(*hintDecl, "'") + " type is '" + Ty::ToString(hintDecl->ty) + "'");
        }
    }
    for (const auto& [node, decl] : limitedDecls) {
        auto usedVisibility = GetAccessLevelStr(decl, "'");
        if (!node.begin.IsZero() && !node.end.IsZero()) {
            auto typeRange = MakeRange(node.begin, node.end);
            builder.AddNote(
                node, typeRange, "type '" + Ty::ToString(node.ty) + "' contains " + usedVisibility + " type");
        }
        auto inDeclRange = MakeRangeForDeclIdentifier(decl);
        builder.AddNote(
            decl, inDeclRange, "the " + usedVisibility + " type is '" + Ty::ToString(decl.ty) + "'");
    }
}

void DiagPatternInternalTypesUse(DiagnosticEngine& diag, const std::vector<std::pair<Node&, Decl&>>& inDecls)
{
    for (const auto& inDeclPair : inDecls) {
        auto& node = inDeclPair.first;
        auto& used = inDeclPair.second;
        if (!node.begin.IsZero() && !node.end.IsZero()) {
            auto usedVisibility = GetAccessLevelStr(used, "'");
            auto range = MakeRange(node.begin, node.end);
            auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_accessibility_with_main_hint, node, range,
                GetAccessLevelStr(node), usedVisibility);
            builder.AddMainHintArguments("inferred type", Ty::ToString(node.ty), usedVisibility);
            auto inDeclRange = MakeRangeForDeclIdentifier(used);
            builder.AddNote(used, inDeclRange, "the " + usedVisibility + " type is '" + Ty::ToString(used.ty) + "'");
        }
    }
}

void DiagAmbiguousUpperBoundTargets(DiagnosticEngine& diag, const MemberAccess& ma,
    const OrderedDeclSet& targets)
{
    auto diagBuilder = diag.DiagnoseRefactor(DiagKindRefactor::sema_ambiguous_use, ma, ma.field);
    for (auto it : targets) {
        CJC_NULLPTR_CHECK(it);
        CJC_ASSERT(Is<InheritableDecl>(it->outerDecl));
        std::string message = GetNoteMessageForMemberDecl(*it);
        diagBuilder.AddNote(*it, MakeRangeForDeclIdentifier(*it), message);
    }
}

void DiagUseClosureCaptureVarAlone(DiagnosticEngine& diag, const Expr& expr)
{
    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_use_func_capture_var_alone, expr,
        expr.astKind == ASTKind::LAMBDA_EXPR ? "lambda" : "function");
    Ptr<FuncBody> fb = nullptr;
    if (auto le = DynamicCast<const LambdaExpr*>(&expr)) {
        fb = le->funcBody.get();
    } else if (auto re = DynamicCast<const RefExpr*>(&expr)) {
        fb = StaticCast<FuncDecl*>(re->GetTarget())->funcBody.get();
    }
    CJC_NULLPTR_CHECK(fb);
    std::set<Ptr<const NameReferenceExpr>, CmpNodeByPos> capturedVars(
        fb->capturedVars.cbegin(), fb->capturedVars.cend());
    for (auto varRe : capturedVars) {
        CJC_ASSERT(varRe && varRe->GetTarget());
        auto& vd = *varRe->GetTarget();
        builder.AddNote(*varRe, MakeRange(varRe->begin, varRe->end), "'" + vd.identifier + "' is mutable");
    }
}

void DiagNeedNamedArgument(
    DiagnosticEngine& diag, const CallExpr& ce, const FuncDecl& fd, size_t paramPos, size_t argPos)
{
    CJC_ASSERT(argPos < ce.args.size());
    if (argPos >= ce.args.size()) {
        return;
    }
    CJC_ASSERT(fd.funcBody && !fd.funcBody->paramLists.empty());
    std::set<size_t> restParams = GetRestNamedParams(fd.funcBody->paramLists[0]->params, paramPos);
    std::set<std::string> restArgNames = GetRestArgNames(fd, ce.args, argPos + 1);
    std::string expectedPrefix;
    bool first = true;
    for (auto n : restParams) {
        if (restArgNames.count(fd.funcBody->paramLists[0]->params[n]->identifier) == 1) {
            continue;
        }
        if (fd.funcBody->paramLists[0]->params[n]->identifier.Empty()) {
            continue;
        }
        expectedPrefix +=
            (first ? "'" : " or '") + fd.funcBody->paramLists[0]->params[n]->identifier.GetRawText() + ":'";
        first = false;
    }
    diag.DiagnoseRefactor(DiagKindRefactor::sema_need_named_argument, *ce.args[argPos], expectedPrefix);
}
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
void DiagForStaticVariableDependsGeneric(DiagnosticEngine& diag, const Node& node, const std::set<Ptr<Ty>>& targetTys)
{
    if (targetTys.empty()) {
        return;
    }
    for (auto id : targetTys) {
        if (!id) {
            continue;
        }
        auto builder =
            diag.DiagnoseRefactor(DiagKindRefactor::sema_static_variable_use_generic_parameter, node, id->String());
        if (auto gt = DynamicCast<GenericsTy>(id); gt && gt->decl && !gt->decl->begin.IsZero()) {
            builder.AddNote(*gt->decl, "generic argument declaration here");
        }
    }
};
#endif
void RecommendImportForMemberAccess(TypeManager& typeManager, const ImportManager& importManager,
    const MemberAccess& ma, const Ptr<DiagnosticBuilder> builder)
{
    if (ma.baseExpr == nullptr || !Ty::IsTyCorrect(ma.baseExpr->ty)) {
        return;
    }

    CJC_ASSERT(ma.curFile && ma.curFile->curPackage);
    std::vector<std::pair<Ptr<Decl>, Ptr<InterfaceDecl>>> recommendations;
    auto extends = typeManager.GetAllExtendsByTy(*ma.baseExpr->ty);
    for (auto& ed : extends) {
        for (auto& decl : ed->members) {
            if (decl->identifier == ma.field && ma.curFile) {
                importManager.IsExtendMemberAccessible(*ma.curFile, *decl, *ma.baseExpr->ty, builder);
            }
        }
    }
}
} // namespace Cangjie::Sema
