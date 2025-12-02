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

#include "Diags.h"
#include "BuiltInOperatorUtil.h"
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
    // Skip enum constructor insertion when common enum has platform match.
    // During platform compilation, platform constructors take priority over common ones.
    if (ed.IsCommonMatchedWithPlatform()) {
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
    // NOTE: Property decl's getter/setter function should be ignored from 'redef' checking.
    auto redefModifier = TypeCheckUtil::FindModifier(fd, TokenKind::REDEF);
    auto staticModifier = TypeCheckUtil::FindModifier(fd, TokenKind::STATIC);
    if (redefModifier && staticModifier == nullptr && fd.propDecl == nullptr) {
        diag.Diagnose(*redefModifier, DiagKind::sema_redef_modify_static_func, "function");
    }

    fd.funcBody->funcDecl = &fd;
    (void)CheckFuncBody(ctx, *fd.funcBody);
    if (Ty::IsTyCorrect(fd.ty) && fd.ty->HasQuestTy()) {
        CJC_ASSERT(fd.ty->IsFunc());
        // NOTE: Error's for synthesized quest ty must be reported in 'CheckBodyRetType',
        // otherwise it means funcBody contains broken nodes.
        // Update return type to invalid, keep 'fd''s type in funcTy format.
        fd.ty = typeManager.GetFunctionTy(RawStaticCast<FuncTy*>(fd.ty)->paramTys, TypeManager::GetInvalidTy());
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
    if (fd.funcBody->retType && fd.funcBody->retType->ty) {
        auto retTy = fd.funcBody->retType->ty;
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
            bool isArrayTy = fp->ty && fp->ty->IsStructArray() && !fp->ty->typeArgs.empty();
            bool isArrayStringTy = isArrayTy && fp->ty->typeArgs[0] && fp->ty->typeArgs[0]->IsString();
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
    auto funcTy = DynamicCast<FuncTy*>(fd.ty);
    if (!Ty::IsTyCorrect(funcTy) || fd.op == TokenKind::ILLEGAL || fd.op == TokenKind::LPAREN) {
        return;
    }
    Ptr<Ty> baseTy = (fd.outerDecl != nullptr) ? fd.outerDecl->ty : nullptr;
    if (fd.op == TokenKind::LSQUARE) {
        return HandIndexOperatorOverload(fd, *funcTy);
    }
    const std::vector<Ptr<Ty>>& paramTys = funcTy->paramTys;
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
    if (invalidNamedParams.empty() && funcTy.retTy != TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)) {
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
        if (IsGlobalOrMember(vd) && Ty::IsInitialTy(vd.ty)) {
            DiagUnableToInferDecl(diag, vd);
            vd.ty = TypeManager::GetInvalidTy();
        }
        return;
    }
    // Mark the declaration is checked.
    vd.EnableAttr(Attribute::IS_CHECK_VISITED);

    SynchronizeTypeAndInitializer(ctx, vd);
    if (vd.initializer != nullptr && Ty::IsInitialTy(vd.initializer->ty)) {
        // Already generate diagnostics before, quit here.
        return;
    }
    CheckStaticVarAccessNonStatic(vd);
}

void TypeChecker::TypeCheckerImpl::CheckVarWithPatternDecl(ASTContext& ctx, VarWithPatternDecl& vpd)
{
    if (vpd.TestAttr(Attribute::IS_CHECK_VISITED)) {
        // Unable to infer mutually recursive top level variables.
        if (vpd.TestAttr(Attribute::GLOBAL) && Ty::IsInitialTy(vpd.ty)) {
            DiagUnableToInferDecl(diag, vpd);
            vpd.ty = TypeManager::GetInvalidTy();
        }
        return;
    }
    // Mark the current declaration checked.
    vpd.EnableAttr(Attribute::IS_CHECK_VISITED);
    SynchronizeTypeAndInitializer(ctx, vpd);
    if ((vpd.initializer != nullptr && !Ty::IsTyCorrect(vpd.initializer->ty)) || !vpd.irrefutablePattern) {
        // Already generate diagnostics before, quit here.
        return;
    }
    if (Ty::IsTyCorrect(vpd.ty) && !ChkPattern(ctx, *vpd.ty, *vpd.irrefutablePattern, false)) {
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

template <typename T> void TypeChecker::TypeCheckerImpl::SynchronizeTypeAndInitializer(ASTContext& ctx, T& vd)
{
    if (vd.type != nullptr && vd.initializer == nullptr) {
        Synthesize(ctx, vd.type.get());
        vd.ty = vd.type->ty;
    } else if (vd.type != nullptr && vd.initializer != nullptr) {
        // Always set vd.ty to the one the user gives.
        Synthesize(ctx, vd.type.get());
        if (AST::Ty::IsTyCorrect(vd.type->ty)) {
            // User has defined vardecl's type. Should not be set to invalid even if initializer is incompatible.
            vd.ty = vd.type->ty;
            if (vd.type->ty->IsRune() && IsSingleRuneStringLiteral(*vd.initializer)) {
                vd.initializer->ty = vd.type->ty;
            } else if (vd.type->ty->kind == TypeKind::TYPE_UINT8 && IsSingleByteStringLiteral(*vd.initializer)) {
                vd.initializer->ty = vd.type->ty;
                ChkLitConstExprRange(StaticCast<LitConstExpr&>(*vd.initializer));
            } else {
                bool isWellTyped = Check(ctx, vd.type->ty, vd.initializer.get());
                // Unset 'checked' attribute for local variables when there exists any error.
                if (!isWellTyped && !IsGlobalOrMember(vd)) {
                    vd.DisableAttr(Attribute::IS_CHECK_VISITED);
                }
            }
        } else {
            // VarDecl's user defined type is invalid, should not update 'vd.ty' with initializer's type.
            vd.ty = TypeManager::GetInvalidTy();
            (void)Synthesize(ctx, vd.initializer.get());
        }
        // Should not report here. Any error should be reported during 'Check' or 'Synthesize' step.
    } else if (vd.type == nullptr && vd.initializer != nullptr) {
        bool isWellTyped = SynthesizeAndReplaceIdealTy(ctx, *vd.initializer);
        // Unset 'checked' attribute for local variables when there exists any error.
        if (!isWellTyped && !IsGlobalOrMember(vd)) {
            vd.DisableAttr(Attribute::IS_CHECK_VISITED);
        }
        // Set VarDecl's type by its initializer's type.
        // when the initializer is 'this', the vd's ty will be inferred to the corresponding ClassTy (NOT
        // ClassThisTy)
        if (auto ctt = DynamicCast<AST::ClassThisTy*>(vd.initializer->ty); ctt && ctt->decl) {
            vd.ty = typeManager.GetClassTy(*ctt->decl, ctt->typeArgs);
        } else if (auto fty = DynamicCast<AST::FuncTy*>(vd.initializer->ty);
                   fty && fty->isC && fty->hasVariableLenArg) {
            vd.ty = TypeManager::GetInvalidTy();
            bool shouldDiag = vd.ShouldDiagnose() && !CanSkipDiag(*vd.initializer);
            if (shouldDiag) {
                diag.Diagnose(*vd.initializer, DiagKind::sema_cfunc_var_cannot_have_var_param);
            }
        } else {
            vd.ty = vd.initializer->ty;
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
        pd.ty = TypeManager::GetInvalidTy();
    }
    CJC_NULLPTR_CHECK(pd.type);
    (void)Synthesize(ctx, pd.type.get());
    pd.ty = pd.type->ty;
    for (auto& pmd : pd.getters) {
        if (pmd->funcBody && !pmd->funcBody->paramLists.empty()) {
            if (!pmd->funcBody->paramLists[0]->params.empty()) {
                (void)diag.Diagnose(*pmd, DiagKind::sema_cannot_have_parameter, "getter");
            }
            if (pmd->funcBody->paramLists.size() > 1) {
                (void)diag.Diagnose(*pmd, DiagKind::sema_cannot_currying, "getter");
            }
        }
        (void)Synthesize(ctx, pmd.get());
    }
    for (auto& pmd : pd.setters) {
        if (pmd->funcBody && pmd->funcBody->paramLists.size() > 1) {
            (void)diag.Diagnose(*pmd, DiagKind::sema_cannot_currying, "setter");
        }
        (void)Synthesize(ctx, pmd.get());
    }
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
    auto syms = GetSymsByASTKind(ctx, ASTKind::ENUM_DECL);
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
        (void)Synthesize(ctx, interfaceType.get());
        if (auto id = AST::As<ASTKind::INTERFACE_DECL>(Ty::GetDeclPtrOfTy(interfaceType->ty)); id) {
            CheckSealedInheritance(ed, *interfaceType);
            (void)id->subDecls.insert(&ed);
        } else {
            // Can only implement interface. Set type to invalid (avoid invalid reference).
            interfaceType->ty = TypeManager::GetInvalidTy();
            (void)diag.Diagnose(ed, DiagKind::sema_type_implement_non_interface, "enum", ed.identifier.Val());
        }
    }
    // Check constructors.
    for (auto& it : ed.constructors) {
        CJC_NULLPTR_CHECK(it);
        CheckAnnotations(ctx, *it);
        Walker(it.get(), [this, &ctx](Ptr<Node> node) {
            if (auto type = DynamicCast<Type*>(node); type) {
                Synthesize(ctx, type);
                return VisitAction::SKIP_CHILDREN;
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
    // Check each function.
    for (auto& it : ed.members) {
        if (it->astKind == ASTKind::FUNC_DECL) {
            auto fd = StaticAs<ASTKind::FUNC_DECL>(it.get());
            (void)Synthesize(ctx, fd);
        } else if (it->astKind == ASTKind::PROP_DECL) {
            auto pd = StaticAs<ASTKind::PROP_DECL>(it.get());
            (void)Synthesize(ctx, pd);
            if (pd->isVar) {
                auto mutDecl = TypeCheckUtil::FindModifier(*pd, TokenKind::MUT);
                CJC_ASSERT(mutDecl);
                if (mutDecl) {
                    (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_immutable_type_illegal_property, *mutDecl);
                }
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::SetEnumEleTy(Decl& constructor)
{
    // Imported enum decl's constructor may have valid type.
    if (!constructor.TestAttr(Attribute::ENUM_CONSTRUCTOR) || Ty::IsTyCorrect(constructor.ty)) {
        return;
    }
    if (constructor.astKind == ASTKind::VAR_DECL) {
        if (constructor.outerDecl != nullptr) {
            constructor.ty = constructor.outerDecl->ty;
        }
    } else if (constructor.astKind == ASTKind::FUNC_DECL) {
        SetEnumEleTyHandleFuncDecl(*StaticAs<ASTKind::FUNC_DECL>(&constructor));
    } else {
        (void)diag.Diagnose(constructor, DiagKind::sema_invalid_constructor_in_enum);
        constructor.ty = TypeManager::GetInvalidTy();
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
        if (it->ty && Ty::IsCStructType(*it->ty)) {
            auto structTy = RawStaticCast<StructTy*>(it->ty);
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
        funcDecl.ty = TypeManager::GetInvalidTy();
        return;
    }
    // EnumDecl's func constructor must be 'CtorName(Type1, Type2...)'.
    // The target and type of parameters should be checked in 'ResolveName' stage.
    std::vector<Ptr<Ty>> paramTys;
    for (auto& param : funcDecl.funcBody->paramLists[0]->params) {
        if (!param->type) {
            continue;
        }
        auto ty = param->type->ty;
        param->ty = ty;
        paramTys.emplace_back(ty);
    }
    auto ctorTy = typeManager.GetFunctionTy(paramTys, funcDecl.outerDecl->ty);
    funcDecl.funcBody->ty = ctorTy;
    funcDecl.ty = ctorTy;
    funcDecl.funcBody->retType = MakeOwned<RefType>();
    funcDecl.funcBody->retType->ty = ctorTy->retTy;
    funcDecl.funcBody->retType->begin = funcDecl.begin;
    funcDecl.funcBody->retType->end = funcDecl.end;
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
        (void)Synthesize(ctx, interfaceType.get());
        if (auto id = AST::As<ASTKind::INTERFACE_DECL>(Ty::GetDeclPtrOfTy(interfaceType->ty)); id) {
            CheckSealedInheritance(sd, *interfaceType);
            (void)id->subDecls.insert(&sd);
        } else {
            // Can only implement interface. Set type to invalid (avoid invalid reference).
            interfaceType->ty = TypeManager::GetInvalidTy();
            (void)diag.Diagnose(sd, DiagKind::sema_type_implement_non_interface, "struct", sd.identifier.Val());
        }
    }
    if (sd.ty && Ty::IsCStructType(*sd.ty)) {
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
    if (sd.TestAnyAttr(Attribute::JAVA_CJ_MAPPING)) {
        CheckJavaInteropLibImport(sd);
        if (sd.generic) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_cjmapping_struct_generic_not_supported,
                MakeRange(sd.generic->leftAnglePos, sd.generic->rightAnglePos), std::string{sd.identifier});
        }
        if (!sd.inheritedTypes.empty()) {
            diag.DiagnoseRefactor(
                DiagKindRefactor::sema_cjmapping_struct_inheritance_interface_not_supported, MakeRange(sd.identifier));
        }
    }
}

void TypeChecker::TypeCheckerImpl::GetRevTypeMapping(
    std::vector<Ptr<Ty>>& params, std::vector<Ptr<Ty>>& args, MultiTypeSubst& revTyMap)
{
    if (args.size() != params.size()) {
        return;
    }
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == nullptr) {
            continue;
        } else if (args[i]->kind == TypeKind::TYPE_GENERICS) {
            revTyMap[RawStaticCast<GenericsTy*>(args[i])].emplace(params[i]);
        } else if (auto decl = Ty::GetDeclPtrOfTy(args[i]); decl && Ty::IsTyCorrect(decl->ty)) {
            GetRevTypeMapping(decl->ty->typeArgs, args[i]->typeArgs, revTyMap);
        }
    }
}

TypeSubst TypeChecker::TypeCheckerImpl::GetGenericTysToInstTysMapping(Ty& genericTy, Ty& instTy) const
{
    TypeSubst map;
    if (genericTy.typeArgs.size() > instTy.typeArgs.size()) {
        return map;
    }
    for (size_t i = 0; i < genericTy.typeArgs.size(); ++i) {
        map.emplace(StaticCast<GenericsTy*>(genericTy.typeArgs[i]), instTy.typeArgs[i]);
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
std::unordered_set<Ptr<Ty>> GetTyArgsRecursive(Ty& ty)
{
    std::unordered_set<Ptr<Ty>> tyArgSet;
    if (!Ty::IsTyCorrect(&ty)) {
        return tyArgSet;
    }
    for (auto& arg : ty.typeArgs) {
        tyArgSet.insert(arg);
        tyArgSet.merge(GetTyArgsRecursive(*arg));
    }
    return tyArgSet;
}
} // namespace

std::vector<Ptr<Ty>> TypeChecker::TypeCheckerImpl::GetUnusedTysInTypeAlias(const TypeAliasDecl& tad) const
{
    std::vector<Ptr<Ty>> diffs;
    if (!tad.generic || !tad.type || Ty::IsInitialTy(tad.type->ty)) {
        return diffs;
    }
    std::vector<Ptr<Ty>> declTys; // Use vector to keep defined order.
    for (auto& param : tad.generic->typeParameters) {
        declTys.emplace_back(param->ty);
    }
    auto usedTys = GetTyArgsRecursive(*tad.type->ty);
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
    if (!tad.type || !Ty::IsTyCorrect(tad.type->ty)) {
        return;
    }
    CheckTypeAliasAccess(tad);
    // Check type parameters which are not used
    if (tad.generic) {
        std::vector<Ptr<Ty>> diffs = GetUnusedTysInTypeAlias(tad);
        if (!diffs.empty()) {
            diag.Diagnose(tad, DiagKind::typealias_unused_type_parameters, Ty::GetTypesToStr(diffs, ","));
        }
    }
    // NOTE: for incremental compile, the type may be marked as 'IS_CHECK_VISITED'.
    if (!tad.type->TestAttr(Attribute::IS_CHECK_VISITED)) {
        CheckReferenceTypeLegality(ctx, *tad.type);
    }
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::SynFuncParam(ASTContext& ctx, FuncParam& fp)
{
    if (!fp.type && !Ty::IsTyCorrect(fp.ty)) {
        return TypeManager::GetInvalidTy();
    }
    if (fp.type) {
        (void)Synthesize(ctx, fp.type.get());
        if (!Ty::IsTyCorrect(fp.type->ty)) {
            (void)Synthesize(ctx, fp.assignment.get()); // If fp has assignment, synthesize to report error.
            return TypeManager::GetInvalidTy();
        }
        fp.ty = fp.type->ty;
    }
    if (fp.assignment) {
        if (!Check(ctx, fp.ty, fp.assignment.get())) {
            return TypeManager::GetInvalidTy();
        }
        (void)Synthesize(ctx, fp.desugarDecl.get());
    }
    return fp.ty;
}

bool TypeChecker::TypeCheckerImpl::ChkFuncParam(ASTContext& ctx, Ty& target, FuncParam& fp)
{
    if (fp.ty && fp.ty == &target) {
        return true;
    }
    if (fp.type) {
        (void)Synthesize(ctx, fp.type.get());
        if (!Ty::IsTyCorrect(fp.type->ty)) {
            return false;
        }
        fp.ty = fp.type->ty;
        // Check type compatibility.
        if (!typeManager.IsSubtype(&target, fp.type->ty)) {
            DiagMismatchedTypes(diag, fp, target);
            return false;
        }
        if (fp.assignment) {
            if (!Check(ctx, fp.ty, fp.assignment.get())) {
                return false;
            }
            (void)Synthesize(ctx, fp.desugarDecl.get());
        }
    } else {
        fp.ty = &target;
    }
    return true;
}
