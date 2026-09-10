// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements typecheck apis for decls.
 */

#include "TypeCheckerImpl.h"

#include <algorithm>
#include <functional>

#include "BuiltInOperatorUtil.h"
#include "Diags.h"
#include "SearchSymbol.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/Position.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"

using namespace Cangjie;
using namespace AST;
using namespace Sema;
using namespace TypeCheckUtil;

namespace {
void InsertEnumConstructors(ASTContext& ctx, const EnumDecl& ed, bool enableMacroInLsp)
{
    // Skip enum constructor insertion when common enum has specific match.
    // During specific compilation, specific constructors take priority over common ones.
    if (ed.IsCommonMatchedWithSpecific()) {
        return;
    }
    for (auto& ctor : ed.constructors) {
        CJC_NULLPTR_CHECK(ctor);
        if (ctor->astKind == ASTKind::VAR_DECL) {
            ctx.InsertEnumConstructor(ctor->identifier, 0, *ctor, enableMacroInLsp);
        } else if (ctor->astKind == ASTKind::FUNC_DECL) {
            auto& fd = StaticCast<FuncDecl&>(*ctor);
            CJC_ASSERT(fd.funcBody && fd.funcBody->paramLists.size() == 1 && fd.funcBody->paramLists.front());
            ctx.InsertEnumConstructor(
                fd.identifier, fd.funcBody->paramLists.front()->params.size(), fd, enableMacroInLsp);
        }
    }
}

inline void DiagUnableToInferDecl(DiagnosticEngine& diag, const Decl& decl)
{
    diag.DiagnoseRefactor(DiagKindRefactor::sema_unable_to_infer_decl, MakeRangeForDeclIdentifier(decl));
}
} // namespace

void TypeChecker::TypeCheckerImpl::CheckFuncDecl(ASTContext& ctx, FuncDecl& fd)
{
    CJC_NULLPTR_CHECK(fd.funcBody);
    if (fd.TestAttr(Attribute::IS_CHECK_VISITED)) {
        return;
    }
    fd.EnableAttr(Attribute::IS_CHECK_VISITED);
    if (!IsGlobalOrMember(fd) && fd.modal.Local() == ASTMode::FULL) {
        DiagInvalidLocalFuncType(fd);
        fd.SetTy({TypeManager::GetInvalidTy()});
        return;
    }
    // NOTE: Property decl's getter/setter function should be ignored from 'redef' checking.
    auto redefModifier = TypeCheckUtil::FindModifier(fd, TokenKind::REDEF);
    auto staticModifier = TypeCheckUtil::FindModifier(fd, TokenKind::STATIC);
    if (redefModifier && staticModifier == nullptr && fd.propDecl == nullptr) {
        diag.Diagnose(*redefModifier, DiagKind::sema_redef_modify_static_func, "function");
    }

    fd.funcBody->funcDecl = &fd;
    (void)CheckFuncBody(ctx, *fd.funcBody);
    if (fd.GetTy().IsCorrect() && fd.GetTy()->HasQuestTy()) {
        CJC_ASSERT(fd.GetTy()->IsFunc());
        // NOTE: Error's for synthesized quest ty must be reported in 'CheckBodyRetType',
        // otherwise it means funcBody contains broken nodes.
        // Update return type to invalid, keep 'fd''s type in funcTy format.
        fd.SetTy({
            typeManager.GetFunctionTy(DynamicCast<FuncTy*>(fd.DataTy())->paramTys, {TypeManager::GetInvalidTy()})});
    }
    if (fd.GetTy().IsCorrect()) {
        if (fd.modal.HasLocal()) {
            fd.SetTy(fd.GetTy().With(fd.modal.ToModalInfo()));
        }
        if (!IsGlobalOrMember(fd) && !fd.TestAttr(Attribute::COMPILER_ADD)) {
            CheckLocalCaptures(ctx, fd);
        }
    }
    if (fd.propDecl) {
        CheckPropMethodTargetType(fd);
    }
    // NOTE: 'fd''s type should only be updated inside 'CheckFuncBody' not here.
    if (fd.TestAttr(AST::Attribute::MAIN_ENTRY)) {
        CheckEntryFunc(fd);
    } else if (fd.TestAttr(Attribute::OPERATOR)) {
        CheckOperatorOverloadFunc(fd);
    }
}

void TypeChecker::TypeCheckerImpl::CheckStaticVarAccessNonStatic(const VarDecl& vd)
{
    // Only check for static variable.
    if (!vd.TestAttr(Attribute::STATIC)) {
        return;
    }
    Walker walkDecl(vd.initializer.get(), [this, &vd](Ptr<Node> node) -> VisitAction {
        if (auto re = DynamicCast<RefExpr*>(node); re) {
            auto target = re->GetTarget();
            if (!target || target->astKind == ASTKind::FUNC_DECL) {
                return VisitAction::SKIP_CHILDREN;
            }
            // If vardecl is static, the initializer cannot contain non-static member.
            bool invalidAccess = !re->TestAttr(Attribute::COMPILER_ADD) && target != nullptr &&
                !target->IsStaticOrGlobal() && !target->IsTypeDecl() && !target->TestAttr(Attribute::CONSTRUCTOR) &&
                !target->TestAttr(Attribute::ENUM_CONSTRUCTOR) && target->outerDecl;
            if (invalidAccess) {
                diag.Diagnose(
                    vd, DiagKind::sema_static_variable_cannot_access_non_static_member, re->ref.identifier.Val());
            }
            return VisitAction::WALK_CHILDREN;
        } else if (node->astKind == ASTKind::MEMBER_ACCESS || node->astKind == ASTKind::FUNC_BODY) {
            return VisitAction::SKIP_CHILDREN;
        } else {
            return VisitAction::WALK_CHILDREN;
        }
    });
    walkDecl.Walk();
}

void TypeChecker::TypeCheckerImpl::CheckEntryFunc(FuncDecl& fd)
{
    bool invalid = !fd.curFile || !fd.curFile->curPackage;
    if (invalid || !fd.TestAttr(Attribute::GLOBAL)) {
        return; // Do not need diagnose.
    }
    if (fd.curFile->isCommon) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_common_package_has_main, fd);
    }
    if (!mainFunctionMap.empty() &&
        (mainFunctionMap.find(fd.curFile) == mainFunctionMap.end() ||
            mainFunctionMap[fd.curFile].find(&fd) == mainFunctionMap[fd.curFile].end())) {
        (void)diag.Diagnose(fd, DiagKind::sema_redefinition_entry);
    }
    if (fd.funcBody->retType && fd.funcBody->retType->GetTy()) {
        auto retTy = fd.funcBody->retType->GetTy();
        if (Ty::IsTyCorrect(retTy) && !(retTy->IsInteger() || retTy->IsUnit())) {
            (void)diag.Diagnose(fd, DiagKind::sema_unexpected_return_type_for_entry);
        }
    }
    bool noParamList = !fd.funcBody || fd.funcBody->paramLists.empty();
    if (noParamList) {
        return; // Invalid main function, error messages should be reported before.
    }
    bool invalidParamTy = std::any_of(fd.funcBody->paramLists[0]->params.begin(),
        fd.funcBody->paramLists[0]->params.end(), [](const OwnedPtr<FuncParam>& fp) {
            bool isArrayTy = fp->GetTy() && fp->GetTy()->IsStructArray() && !fp->GetTy()->typeArgs.empty();
            bool isArrayStringTy = isArrayTy && fp->GetTy()->typeArgs[0] && fp->GetTy()->typeArgs[0]->IsString();
            if (isArrayStringTy) {
                return false;
            }
            return true;
        });
    if (invalidParamTy || fd.funcBody->paramLists[0]->params.size() > 1) {
        (void)diag.Diagnose(fd, DiagKind::sema_unexpected_param_for_entry);
    }
    (void)mainFunctionMap[fd.curFile].emplace(&fd);
}

void TypeChecker::TypeCheckerImpl::CheckOperatorOverloadFunc(const FuncDecl& fd)
{
    Ptr<FuncTy> funcTy = DynamicCast<FuncTy*>(fd.DataTy());
    if (!Ty::IsTyCorrect(Ptr<Ty>(funcTy)) || fd.op == TokenKind::ILLEGAL || fd.op == TokenKind::LPAREN) {
        return;
    }
    ModalTy baseTy = (fd.outerDecl != nullptr) ? fd.outerDecl->GetTy() : ModalTy{};
    if (fd.op == TokenKind::LSQUARE) {
        return HandIndexOperatorOverload(fd, *funcTy);
    }
    const std::vector<ModalTy>& paramTys = funcTy->paramTys;
    switch (paramTys.size()) {
        case 0: { // If the size of paramsTy is 0.
            // Unary operators.
            if (IsUnaryOperator(fd.op)) {
                if (baseTy && IsBuiltinUnaryExpr(fd.op, *baseTy)) {
                    (void)diag.Diagnose(fd, DiagKind::sema_operator_overload_built_in_unary_operator,
                        fd.identifier.Val(), baseTy->String());
                }
            } else {
                // If fd.op is not a unary operator.
                diag.Diagnose(fd, DiagKind::sema_operator_overload_invalid_num_parameter, fd.identifier.Val());
            }
            break;
        }
        case 1: { // If the size of paramsTy is 1.
            // Binary operators.
            // Allow 'Intrinsic' function to overload.
            if (!IsBinaryOperator(fd.op)) { // If fd.op is not a binary operator.
                (void)diag.Diagnose(fd, DiagKind::sema_operator_overload_invalid_num_parameter, fd.identifier.Val());
            } else if (fd.fullPackageName != CORE_PACKAGE_NAME && baseTy && paramTys[0] &&
                IsBuiltinBinaryExpr(fd.op, *baseTy, *paramTys[0])) {
                (void)diag.Diagnose(fd, DiagKind::sema_operator_overload_built_in_binary_operator, fd.identifier.Val(),
                    baseTy->String(), paramTys[0]->String());
            }
            break;
        }
        default:
            (void)diag.Diagnose(fd, DiagKind::sema_operator_overload_invalid_num_parameter, fd.identifier.Val());
    }
}

void TypeChecker::TypeCheckerImpl::HandIndexOperatorOverload(const FuncDecl& fd, const FuncTy& funcTy)
{
    CJC_ASSERT(!fd.funcBody->paramLists.empty());
    const auto& params = fd.funcBody->paramLists[0]->params;
    if (params.empty()) {
        (void)diag.Diagnose(fd, DiagKind::sema_operator_overload_invalid_num_parameter, fd.identifier.Val());
        return;
    }
    // Index operator overload function can have many parameters but at most one named parameter.
    auto lastIndex = params.size() - 1;
    if (!params[lastIndex]->isNamedParam) { // It is getter.
        return;
    }
    std::vector<std::reference_wrapper<const FuncParam>> invalidNamedParams;
    for (auto& param : params) {
        if (param->isNamedParam && param->identifier != "value") {
            (void)invalidNamedParams.emplace_back(*param);
        }
    }
    if (!invalidNamedParams.empty()) {
        auto& firstParam = invalidNamedParams.front();
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_invalid_subscript_assign_parameter, firstParam,
            MakeRange(firstParam.get().identifier));
        for (auto iter = invalidNamedParams.cbegin() + 1; iter != invalidNamedParams.cend(); ++iter) {
            builder.AddHint(MakeRange(iter->get().identifier));
        }
    }
    if (params.size() <= 1) {
        (void)diag.DiagnoseRefactor(
            DiagKindRefactor::sema_invalid_subscript_assign_parameter_num, fd, MakeRange(fd.identifier));
        return;
    }
    if (invalidNamedParams.empty() && !funcTy.retTy->IsUnit()) {
        auto range = MakeRange(fd.identifier);
        if (fd.funcBody && fd.funcBody->retType && !fd.funcBody->retType->begin.IsZero()) {
            range = MakeRange(fd.funcBody->retType->begin, fd.funcBody->retType->end);
        }
        (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_invalid_subscript_assign_return, fd, range);
    }
}

void TypeChecker::TypeCheckerImpl::CheckVarDecl(ASTContext& ctx, VarDecl& vd)
{
    if (vd.TestAttr(Attribute::IS_CHECK_VISITED)) {
        // Unable to infer mutually recursive variables.
        if (IsGlobalOrMember(vd) && Ty::IsInitialTy(vd.DataTy())) {
            DiagUnableToInferDecl(diag, vd);
            vd.SetTy({TypeManager::GetInvalidTy()});
        }
        return;
    }
    // Mark the declaration is checked.
    vd.EnableAttr(Attribute::IS_CHECK_VISITED);

    SynchronizeTypeAndInitializer({ctx, SynPos::NONE}, vd);
    if (vd.initializer != nullptr && Ty::IsInitialTy(vd.initializer->DataTy())) {
        // Already generate diagnostics before, quit here.
        return;
    }
    CheckStaticVarAccessNonStatic(vd);
}

void TypeChecker::TypeCheckerImpl::CheckVarWithPatternDecl(ASTContext& ctx, VarWithPatternDecl& vpd)
{
    if (vpd.TestAttr(Attribute::IS_CHECK_VISITED)) {
        // Unable to infer mutually recursive top level variables.
        if (vpd.TestAttr(Attribute::GLOBAL) && Ty::IsInitialTy(vpd.DataTy())) {
            DiagUnableToInferDecl(diag, vpd);
            vpd.SetTy({TypeManager::GetInvalidTy()});
        }
        return;
    }
    // Mark the current declaration checked.
    vpd.EnableAttr(Attribute::IS_CHECK_VISITED);
    SynchronizeTypeAndInitializer({ctx, SynPos::NONE}, vpd);
    if ((vpd.initializer != nullptr && !vpd.initializer->GetTy().IsCorrect()) || !vpd.irrefutablePattern) {
        // Already generate diagnostics before, quit here.
        return;
    }
    if (vpd.GetTy().IsCorrect() &&
        !ChkPattern(ctx, vpd.GetTy(), *vpd.irrefutablePattern, false, vpd.initializer.get())) {
        diag.Diagnose(*vpd.irrefutablePattern, DiagKind::sema_mismatched_type_for_pattern_in_vardecl);
    }
    if (!IsIrrefutablePattern(*vpd.irrefutablePattern)) {
        diag.Diagnose(*vpd.irrefutablePattern, DiagKind::sema_pattern_can_not_be_assigned);
    }
    // Set VarDecl's initializer of VarPattern in TuplePattern by VarWithPatternDecl's initializer.
    auto tp = DynamicCast<TuplePattern>(vpd.irrefutablePattern.get());
    auto tl = DynamicCast<TupleLit>(vpd.initializer.get());
    if (vpd.isConst && tp && tl) {
        if (tp->patterns.size() != tl->children.size()) {
            return;
        }
        for (size_t i = 0; i < tp->patterns.size(); ++i) {
            if (auto vp = DynamicCast<VarPattern>(tp->patterns[i].get()); vp && vp->varDecl) {
                vp->varDecl->initializer = ASTCloner::Clone(tl->children[i].get());
            }
        }
    }
}

template <typename T>
void TypeChecker::TypeCheckerImpl::SynchronizeTypeAndInitializer(const CheckerContext& ctx, T& vd)
{
    if (vd.type != nullptr && vd.initializer == nullptr) {
        Synthesize(ctx, vd.type.get());
        vd.SetTy(vd.type->GetTy());
    } else if (vd.type != nullptr && vd.initializer != nullptr) {
        // Always set vd.GetTy() to the one the user gives.
        Synthesize(ctx, vd.type.get());
        if (vd.type->GetTy().IsCorrect()) {
            // User has defined vardecl's type. Should not be set to invalid even if initializer is incompatible.
            vd.SetTy(vd.type->GetTy());
            if (vd.type->GetTy()->IsRune() && IsSingleRuneStringLiteral(*vd.initializer)) {
                vd.initializer->SetTy(vd.type->GetTy());
            } else if (vd.type->TyKind() == TypeKind::TYPE_UINT8 && IsSingleByteStringLiteral(*vd.initializer)) {
                vd.initializer->SetTy(vd.type->GetTy());
                ChkLitConstExprRange(StaticCast<LitConstExpr&>(*vd.initializer));
            } else {
                bool isWellTyped = Check(ctx.Ctx(), vd.type->GetTy(), vd.initializer.get());
                // Unset 'checked' attribute for local variables when there exists any error.
                if (!isWellTyped && !IsGlobalOrMember(vd)) {
                    vd.DisableAttr(Attribute::IS_CHECK_VISITED);
                }
            }
        } else {
            // VarDecl's user defined type is invalid, should not update 'vd.GetTy()' with initializer's type.
            vd.SetTy({TypeManager::GetInvalidTy()});
            Synthesize(ctx.With(SynPos::EXPR_ARG), vd.initializer.get());
        }
        // Should not report here. Any error should be reported during 'Check' or 'Synthesize' step.
    } else if (vd.type == nullptr && vd.initializer != nullptr) {
        bool isWellTyped = SynthesizeAndReplaceIdealTy(ctx.With(SynPos::EXPR_ARG), *vd.initializer);
        // Unset 'checked' attribute for local variables when there exists any error.
        if (!isWellTyped && !IsGlobalOrMember(vd)) {
            vd.DisableAttr(Attribute::IS_CHECK_VISITED);
        }
        // Set VarDecl's type by its initializer's type.
        // when the initializer is 'this', the vd's ty will be inferred to the corresponding ClassTy (NOT
        // ClassThisTy)
        if (auto ctt = DynamicCast<AST::ClassThisTy>(vd.initializer->DataTy()); ctt && ctt->decl) {
            vd.SetTy({typeManager.GetClassTy(*ctt->decl, ctt->TyArgs()), vd.initializer->TyMode()});
        } else if (auto fty = DynamicCast<AST::FuncTy>(vd.initializer->DataTy());
            fty && fty->isC && fty->hasVariableLenArg) {
            vd.SetTy({TypeManager::GetInvalidTy()});
            bool shouldDiag = vd.ShouldDiagnose() && !CanSkipDiag(*vd.initializer);
            if (shouldDiag) {
                diag.Diagnose(*vd.initializer, DiagKind::sema_cfunc_var_cannot_have_var_param);
            }
        } else {
            vd.SetTy(vd.initializer->GetTy());
        }
        // global var cannot have local type, but for copy type we can infer it to not local to pass the check.
        if (IsGlobalOrMember(vd) && vd.GetTy().IsCorrect() && typeManager.ImplementsCopyInterface(vd.DataTy())) {
            vd.SetTy(vd.GetTy().With(Mode::NOT));
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckPropDecl(ASTContext& ctx, PropDecl& pd)
{
    if (pd.TestAttr(Attribute::IS_CHECK_VISITED)) {
        return;
    }
    // Mark the current declaration is checked.
    pd.EnableAttr(Attribute::IS_CHECK_VISITED);
    auto redefModifier = TypeCheckUtil::FindModifier(pd, TokenKind::REDEF);
    auto staticModifier = TypeCheckUtil::FindModifier(pd, TokenKind::STATIC);
    if (redefModifier && staticModifier == nullptr) {
        (void)diag.Diagnose(*redefModifier, DiagKind::sema_redef_modify_static_func, "property");
        pd.SetTy({TypeManager::GetInvalidTy()});
    }
    CJC_NULLPTR_CHECK(pd.type);
    Synthesize({ctx, SynPos::NONE}, pd.type.get());
    pd.SetTy(pd.type->GetTy());

    for (auto& pmd : pd.getters) {
        if (pmd->funcBody && !pmd->funcBody->paramLists.empty()) {
            if (!pmd->funcBody->paramLists[0]->params.empty()) {
                (void)diag.Diagnose(*pmd, DiagKind::sema_cannot_have_parameter, "getter");
            }
            if (pmd->funcBody->paramLists.size() > 1) {
                (void)diag.Diagnose(*pmd, DiagKind::sema_cannot_currying, "getter");
            }
        }
        Synthesize({ctx, SynPos::NONE}, pmd.get());
    }
    for (auto& pmd : pd.setters) {
        if (pmd->funcBody && pmd->funcBody->paramLists.size() > 1) {
            diag.Diagnose(*pmd, DiagKind::sema_cannot_currying, "setter");
        }
        Synthesize({ctx, SynPos::NONE}, pmd.get());
    }
}

bool TypeChecker::TypeCheckerImpl::CheckPropMethodTargetType(const FuncDecl& fd)
{
    auto prop = fd.propDecl;
    if (fd.isSetter) {
        auto& params = fd.funcBody->paramLists[0];
        if (params->params.empty() || !params->params[0]->GetTy().IsCorrect()) {
            return true;
        }
        if (params->params[0]->DataTy() != prop->DataTy()) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_prop_method_target_type_mismatch, fd, prop->GetTy().String(),
                params->params[0]->GetTy().String());
            return false;
        }
        return true;
    }
    if (!fd.funcBody->retType || !fd.funcBody->retType->GetTy().IsCorrect()) {
        return true;
    }
    if (fd.funcBody->retType->DataTy() != prop->DataTy()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_prop_method_target_type_mismatch, fd, prop->GetTy().String(),
            fd.funcBody->retType->GetTy().String());
        return false;
    }
    return true;
}

void TypeChecker::TypeCheckerImpl::BuildImportedEnumConstructorMap(ASTContext& ctx)
{
    CJC_NULLPTR_CHECK(ctx.curPackage);
    std::unordered_set<const EnumDecl*> visited;
    for (auto& file : ctx.curPackage->files) {
        for (auto& [_, decls] : importManager.GetImportedDecls(*file)) {
            for (auto decl : decls) {
                auto ed = DynamicCast<const EnumDecl*>(decl);
                if (!ed || visited.count(ed) != 0) {
                    continue;
                }
                InsertEnumConstructors(ctx, *ed, ci->invocation.globalOptions.enableMacroInLSP);
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::BuildEnumConstructorMap(ASTContext& ctx) const
{
    auto syms = SearchSymbol::GetSymsByASTKind(ctx, ASTKind::ENUM_DECL);
    for (auto sym : syms) {
        if (auto ed = DynamicCast<EnumDecl*>(sym->node)) {
            InsertEnumConstructors(ctx, *ed, ci->invocation.globalOptions.enableMacroInLSP);
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckEnumDecl(ASTContext& ctx, EnumDecl& ed)
{
    if (ed.TestAttr(Attribute::IS_CHECK_VISITED)) {
        return;
    }
    // Mark the current declaration is checked.
    ed.EnableAttr(Attribute::IS_CHECK_VISITED);

    // Do type check for all implemented interfaces.
    for (auto& interfaceType : ed.inheritedTypes) {
        Synthesize({ctx, SynPos::NONE}, interfaceType.get());
        if (auto id = AST::As<ASTKind::INTERFACE_DECL>(Ty::GetDeclPtrOfTy(interfaceType->GetTy())); id) {
            CheckSealedInheritance(ed, *interfaceType);
            (void)id->subDecls.insert(&ed);
        } else {
            // Can only implement interface. Set type to invalid (avoid invalid reference).
            interfaceType->SetTy({TypeManager::GetInvalidTy()});
            (void)diag.Diagnose(ed, DiagKind::sema_type_implement_non_interface, "enum", ed.identifier.Val());
        }
    }
    // Check constructors.
    for (auto& it : ed.constructors) {
        CJC_NULLPTR_CHECK(it);
        CheckAnnotations(ctx, *it);
        Walker(it.get(), [this, &ctx](Ptr<Node> node) {
            if (auto type = DynamicCast<Type*>(node); type) {
                Synthesize({ctx, SynPos::NONE}, type);
                return VisitAction::SKIP_CHILDREN;
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
    // Check each function.
    for (auto& it : ed.members) {
        if (it->astKind == ASTKind::FUNC_DECL) {
            auto fd = StaticAs<ASTKind::FUNC_DECL>(it.get());
            Synthesize({ctx, SynPos::NONE}, fd);
        } else if (it->astKind == ASTKind::PROP_DECL) {
            auto pd = StaticAs<ASTKind::PROP_DECL>(it.get());
            Synthesize({ctx, SynPos::NONE}, pd);
            if (pd->isVar) {
                auto mutDecl = TypeCheckUtil::FindModifier(*pd, TokenKind::MUT);
                CJC_ASSERT(mutDecl);
                if (mutDecl) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_immutable_type_illegal_property, *mutDecl);
                    pd->EnableAttr(Attribute::HAS_BROKEN);
                }
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::SetEnumEleTy(Decl& constructor)
{
    // Imported enum decl's constructor may have valid type.
    if (!constructor.TestAttr(Attribute::ENUM_CONSTRUCTOR) || constructor.GetTy().IsCorrect()) {
        return;
    }
    if (constructor.astKind == ASTKind::VAR_DECL) {
        if (constructor.outerDecl != nullptr) {
            constructor.SetTy(constructor.outerDecl->GetTy());
        }
    } else if (constructor.astKind == ASTKind::FUNC_DECL) {
        SetEnumEleTyHandleFuncDecl(*StaticAs<ASTKind::FUNC_DECL>(&constructor));
    } else {
        (void)diag.Diagnose(constructor, DiagKind::sema_invalid_constructor_in_enum);
        constructor.SetTy({TypeManager::GetInvalidTy()});
    }
}

void TypeChecker::TypeCheckerImpl::CheckEnumFuncDeclIsCStructParam(const FuncDecl& funcDecl)
{
    bool invalid = !funcDecl.funcBody || funcDecl.funcBody->paramLists.empty();
    if (invalid) {
        return;
    }
    for (auto& it : funcDecl.funcBody->paramLists[0]->params) {
        // String type is special, we should solve this after.
        if (it->DataTy() && it->DataTy()->IsCStructType()) {
            auto structTy = DynamicCast<StructTy*>(it->DataTy());
            CJC_ASSERT(structTy && structTy->declPtr);
            if (structTy->declPtr->identifier != "String" && funcDecl.outerDecl) {
                (void)diag.Diagnose(funcDecl, DiagKind::sema_enum_pattern_func_param_cty_error,
                    funcDecl.identifier.Val(), funcDecl.outerDecl->identifier.Val());
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::SetEnumEleTyHandleFuncDecl(FuncDecl& funcDecl)
{
    bool invalid = !funcDecl.outerDecl || !funcDecl.funcBody || funcDecl.funcBody->paramLists.empty();
    if (invalid) {
        funcDecl.SetTy({TypeManager::GetInvalidTy()});
        return;
    }
    // EnumDecl's func constructor must be 'CtorName(Type1, Type2...)'.
    // The target and type of parameters should be checked in 'ResolveName' stage.
    std::vector<ModalTy> paramTys;
    for (auto& param : funcDecl.funcBody->paramLists[0]->params) {
        if (!param->type) {
            continue;
        }
        auto ty = param->type->GetTy();
        param->SetTy(ty);
        paramTys.emplace_back(ty);
    }
    auto ctorTy = typeManager.GetFunctionTy(paramTys, funcDecl.outerDecl->GetTy());
    funcDecl.funcBody->SetTy({ctorTy});
    funcDecl.SetTy({ctorTy});
    funcDecl.funcBody->retType = MakeOwned<RefType>();
    funcDecl.funcBody->retType->SetTy(ctorTy->retTy);
    funcDecl.funcBody->retType->begin = funcDecl.begin;
    funcDecl.funcBody->retType->end = funcDecl.end;
    funcDecl.funcBody->retType->EnableAttr(Attribute::COMPILER_ADD);
    // Check c ffi type usage.
    if (funcDecl.outerDecl->TestAttr(Attribute::C)) {
        diag.Diagnose(funcDecl, DiagKind::sema_enum_pattern_func_cty_error, funcDecl.identifier.Val(),
            funcDecl.outerDecl->identifier.Val());
    } else {
        CheckEnumFuncDeclIsCStructParam(funcDecl);
    }
}

void TypeChecker::TypeCheckerImpl::CheckStructDecl(ASTContext& ctx, StructDecl& sd)
{
    if (sd.TestAttr(Attribute::IS_CHECK_VISITED)) {
        return;
    }
    // Mark the current declaration is checked.
    sd.EnableAttr(Attribute::IS_CHECK_VISITED);
    // Do type check for all implemented interfaces.
    for (auto& interfaceType : sd.inheritedTypes) {
        Synthesize({ctx, SynPos::NONE}, interfaceType.get());
        if (auto id = AST::As<ASTKind::INTERFACE_DECL>(Ty::GetDeclPtrOfTy(interfaceType->GetTy())); id) {
            CheckSealedInheritance(sd, *interfaceType);
            (void)id->subDecls.insert(&sd);
        } else {
            // Can only implement interface. Set type to invalid (avoid invalid reference).
            interfaceType->SetTy({TypeManager::GetInvalidTy()});
            (void)diag.Diagnose(sd, DiagKind::sema_type_implement_non_interface, "struct", sd.identifier.Val());
        }
    }
    if (sd.GetTy() && sd.GetTy()->IsCStructType()) {
        if (sd.generic) {
            (void)diag.Diagnose(
                *sd.generic, sd.generic->leftAnglePos, DiagKind::sema_cffi_cannot_have_type_param, "struct with @C");
        }
        if (!sd.inheritedTypes.empty()) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_cstruct_cannot_impl_interfaces, MakeRange(sd.identifier));
        }
    }
    CJC_NULLPTR_CHECK(sd.body);
    TypeCheckCompositeBody(ctx, sd, sd.body->decls);
    CheckRecursiveConstructorCall(sd.body->decls);
}

void TypeChecker::TypeCheckerImpl::GetRevTypeMapping(
    std::vector<ModalTy>& params, std::vector<ModalTy>& args, MultiTypeSubst& revTyMap)
{
    if (args.size() != params.size()) {
        return;
    }
    for (size_t i = 0; i < args.size(); ++i) {
        if (!args[i]) {
            continue;
        } else if (args[i].Kind() == TypeKind::TYPE_GENERICS) {
            revTyMap[StaticCast<GenericsTy*>(args[i].Ty())].emplace(params[i].Ty());
        } else if (auto decl = Ty::GetDeclPtrOfTy(args[i].Ty()); decl && decl->GetTy().IsCorrect()) {
            GetRevTypeMapping(decl->DataTy()->typeArgs, args[i]->typeArgs, revTyMap);
        }
    }
}

TypeSubst TypeChecker::TypeCheckerImpl::GetGenericTysToInstTysMapping(ModalTy genericTy, ModalTy instTy) const
{
    TypeSubst map;
    if (genericTy->typeArgs.size() > instTy->typeArgs.size()) {
        return map;
    }
    for (size_t i = 0; i < genericTy->typeArgs.size(); ++i) {
        map.emplace(StaticCast<GenericsTy*>(genericTy->typeArgs[i].Ty()), instTy->typeArgs[i].Ty());
    }
    return map;
}

void TypeChecker::TypeCheckerImpl::GetAllAssumptions(TyVarUB& source, TyVarUB& newMap)
{
    for (const auto& kv : std::as_const(source)) {
        if (newMap.find(kv.first) != newMap.cend()) {
            continue;
        } else {
            newMap.emplace(kv.first, kv.second);
        }
        for (const auto ty : kv.second) {
            auto decl = Ty::GetDeclPtrOfTy(ty);
            if (!decl) {
                continue;
            }
            auto genericDecl = decl->GetGeneric();
            if (!genericDecl) {
                continue;
            }
            auto n = genericDecl->assumptionCollection;
            GetAllAssumptions(n, newMap);
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckTypeAliasAccess(const TypeAliasDecl& tad)
{
    if (tad.TestAttr(Attribute::PRIVATE)) {
        return;
    }
    std::vector<Ptr<AST::Type>> typeArgs{};
    GetTypeArgsOfType(tad.type.get(), typeArgs);

    const AccessLevel tadLevel = GetAccessLevel(tad);
    const std::string tadLevelStr = GetAccessLevelStr(tad);
    for (auto type : typeArgs) {
        if (type->astKind != ASTKind::REF_TYPE) {
            continue;
        }
        auto rt = StaticAs<ASTKind::REF_TYPE>(type);
        Ptr<Decl> decl = rt->ref.target;
        if (!decl) {
            continue;
        }
        if (decl->astKind != ASTKind::GENERIC_PARAM_DECL && !IsCompatibleAccessLevel(tadLevel, GetAccessLevel(*decl))) {
            (void)diag.Diagnose(tad, DiagKind::sema_typealias_external_refer_internal, tadLevelStr,
                tad.identifier.Val(), GetAccessLevelStr(*decl), decl->identifier.Val());
        }
    }
}

namespace {
std::unordered_set<ModalTy> GetTyArgsRecursive(ModalTy ty)
{
    std::unordered_set<ModalTy> tyArgSet;
    if (!ty.IsCorrect()) {
        return tyArgSet;
    }
    for (auto& arg : ty->typeArgs) {
        tyArgSet.insert(arg);
        tyArgSet.merge(GetTyArgsRecursive(arg));
    }
    return tyArgSet;
}
} // namespace

std::vector<ModalTy> TypeChecker::TypeCheckerImpl::GetUnusedTysInTypeAlias(const TypeAliasDecl& tad) const
{
    std::vector<ModalTy> diffs;
    if (!tad.generic || !tad.type || Ty::IsInitialTy(tad.type->DataTy())) {
        return diffs;
    }
    std::vector<ModalTy> declTys; // Use vector to keep defined order.
    for (auto& param : tad.generic->typeParameters) {
        declTys.emplace_back(param->GetTy());
    }
    auto usedTys = GetTyArgsRecursive(tad.type->GetTy());
    for (auto ty : declTys) {
        if (usedTys.find(ty) == usedTys.end()) {
            diffs.emplace_back(ty);
        }
    }
    return diffs;
}

void TypeChecker::TypeCheckerImpl::CheckTypeAlias(ASTContext& ctx, TypeAliasDecl& tad)
{
    if (tad.TestAttr(Attribute::IS_CHECK_VISITED)) {
        return;
    }
    // Mark the current declaration is checked.
    tad.EnableAttr(Attribute::IS_CHECK_VISITED);
    if (!tad.type || !tad.type->GetTy().IsCorrect()) {
        return;
    }
    CheckTypeAliasAccess(tad);
    // Check type parameters which are not used
    if (tad.generic) {
        std::vector<ModalTy> diffs = GetUnusedTysInTypeAlias(tad);
        if (!diffs.empty()) {
            diag.Diagnose(tad, DiagKind::typealias_unused_type_parameters, Ty::GetModalTypesToStr(diffs, ","));
        }
    }
    // NOTE: for incremental compile, the type may be marked as 'IS_CHECK_VISITED'.
    if (!tad.type->TestAttr(Attribute::IS_CHECK_VISITED)) {
        CheckReferenceTypeLegality(ctx, *tad.type);
    }
}

ModalTy TypeChecker::TypeCheckerImpl::SynFuncParam(ASTContext& ctx, FuncParam& fp)
{
    if (!fp.type && !fp.GetTy().IsCorrect()) {
        return {TypeManager::GetInvalidTy()};
    }
    if (fp.type) {
        Synthesize({ctx, SynPos::NONE}, fp.type.get());
        if (!fp.type->GetTy().IsCorrect()) {
            Synthesize({ctx, SynPos::EXPR_ARG}, fp.assignment.get()); // If fp has assignment, synthesize to report error.
            return {TypeManager::GetInvalidTy()};
        }
        fp.SetTy(fp.type->GetTy());
    }
    if (fp.assignment) {
        if (!Check(ctx, fp.GetTy(), fp.assignment.get())) {
            return {TypeManager::GetInvalidTy()};
        }
        Synthesize({ctx, SynPos::EXPR_ARG}, fp.desugarDecl.get());
    }
    return fp.GetTy();
}

ModalTy TypeChecker::TypeCheckerImpl::SynThisParam(ASTContext& ctx, ThisParam& tp)
{
    auto cl = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, tp.scopeName);
    if (!cl || !cl->node->GetTy()) {
        tp.SetTy({TypeManager::GetInvalidTy()});
        return tp.GetTy();
    }
    auto modalInfo = tp.modal.ToModalInfo();
    ModalTy ty = cl->node->GetTy();
    switch (ty->kind) {
        case TypeKind::TYPE_CLASS: {
            auto classTy = StaticCast<ClassTy>(ty.Ty());
            tp.SetTy({typeManager.GetClassThisTy(*classTy->decl, classTy->TyArgs()), modalInfo});
            return tp.GetTy();
        }
        case TypeKind::TYPE_STRUCT: {
            auto structTy = StaticCast<StructTy>(ty.Ty());
            tp.SetTy({typeManager.GetStructTy(*structTy->declPtr, structTy->TyArgs()), modalInfo});
            return tp.GetTy();
        }
        case TypeKind::TYPE_ENUM: {
            auto enumTy = StaticCast<EnumTy>(ty.Ty());
            tp.SetTy({typeManager.GetEnumTy(*enumTy->declPtr, enumTy->TyArgs()), modalInfo});
            return tp.GetTy();
        }
        case TypeKind::TYPE_INTERFACE: {
            auto interfTy = StaticCast<InterfaceTy>(ty.Ty());
            tp.SetTy({typeManager.GetInterfaceTy(*interfTy->declPtr, interfTy->TyArgs()), modalInfo});
            return tp.GetTy();
        }
        default:
            tp.SetTy(ty.With(modalInfo));
            return tp.GetTy();
    }
}

bool TypeChecker::TypeCheckerImpl::ChkFuncParam(ASTContext& ctx, ModalTy target, FuncParam& fp)
{
    if (fp.GetTy() && fp.GetTy() == target) {
        return true;
    }
    if (fp.type) {
        Synthesize({ctx, SynPos::NONE}, fp.type.get());
        if (!fp.type->GetTy().IsCorrect()) {
            return false;
        }
        fp.SetTy(fp.type->GetTy());
        // Check type compatibility.
        if (!typeManager.IsSubtype(target, fp.type->GetTy())) {
            DiagMismatchedTypes(diag, fp, target);
            return false;
        }
        if (fp.assignment) {
            if (!Check(ctx, fp.GetTy(), fp.assignment.get())) {
                return false;
            }
            Synthesize({ctx, SynPos::NONE}, fp.desugarDecl.get());
        }
    } else {
        fp.SetTy(target);
    }
    return true;
}
