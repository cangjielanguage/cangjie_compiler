// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements typecheck apis for exprs.
 */

#include "TypeCheckerImpl.h"

#include "TypeCheckUtil.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;

namespace {
bool IsLocalDeclOutOfFuncBody(const Decl& decl, const FuncBody& curFuncBody)
{
    // Return false if the decl is member variable.
    if (decl.outerDecl && decl.outerDecl->IsNominalDecl()) {
        return false;
    }
    // Return true if the decl is not toplevel (which scope level is 0) and not in funcBody.
    return decl.scopeLevel > 0 && decl.scopeLevel < curFuncBody.scopeLevel;
}
} // namespace

void TypeChecker::TypeCheckerImpl::UpdateAnyTy()
{
    auto anyDecl = importManager.GetCoreDecl<InterfaceDecl>("Any");
    if (anyDecl && Ty::IsTyCorrect(anyDecl->ty)) {
        typeManager.SetSemaAnyTy(anyDecl->ty);
    }
}

void TypeChecker::TypeCheckerImpl::UpdateCTypeTy()
{
    if (!ci->invocation.globalOptions.implicitPrelude) {
        return;
    }
    auto ctypeDecl = importManager.GetCoreDecl<InterfaceDecl>(CTYPE_NAME);
    if (ctypeDecl && Ty::IsTyCorrect(ctypeDecl->ty)) {
        typeManager.SetSemaCTypeTy(ctypeDecl->ty);
    }
}

bool TypeChecker::TypeCheckerImpl::IsLegalAccessFromStaticFunc(
    const ASTContext& ctx, const RefExpr& re, const Decl& decl)
{
    auto curFuncBody = GetCurFuncBody(ctx, re.scopeName);
    // First make sure we are in a static function, and target is not constructor function.
    if (!curFuncBody || !curFuncBody->TestAttr(AST::Attribute::STATIC) || decl.TestAttr(AST::Attribute::STATIC) ||
        IsClassOrEnumConstructor(decl)) {
        return true;
    }
    // Check if the reference is a non-static member of current structure declaration.
    Symbol* symOfCurStruct = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, re.scopeName);
    CJC_NULLPTR_CHECK(symOfCurStruct);
    CJC_NULLPTR_CHECK(re.curFile);
    std::vector<Ptr<Decl>> decls;
    if (auto typeDecl = Ty::GetDeclPtrOfTy(symOfCurStruct->node->ty)) {
        decls = FieldLookup(ctx, typeDecl, decl.identifier, {.baseTy = typeDecl->ty, .file = re.curFile});
    } else {
        decls = ExtendFieldLookup(ctx, *re.curFile, symOfCurStruct->node->ty, decl.identifier);
    }
    auto diagForInvalidAccess = [this](const RefExpr& re, const Decl& decl, const FuncBody& curFuncBody) {
        if (curFuncBody.funcDecl) {
            std::string identifier =
                curFuncBody.funcDecl->TestAttr(AST::Attribute::COMPILER_ADD) && curFuncBody.funcDecl->ownerFunc
                ? curFuncBody.funcDecl->ownerFunc->identifier
                : curFuncBody.funcDecl->identifier;
            if (curFuncBody.funcDecl->propDecl) {
                // set or get in property
                std::string propDeclName = "*et";
                identifier = curFuncBody.funcDecl->identifier.Val().substr(identifier.size() - propDeclName.size());
            }
            diag.Diagnose(
                re, DiagKind::sema_static_function_cannot_access_non_static_member, decl.identifier.Val(), identifier);
        } else {
            diag.Diagnose(re, DiagKind::sema_static_lambdaExpr_cannot_access_non_static, decl.identifier.Val());
        }
    };
    for (auto it : decls) {
        if (it == &decl) {
            // This function guarantees the curFuncBody is not nullptr.
            diagForInvalidAccess(re, decl, *curFuncBody);
            return false;
        }
    }
    return true;
}

void TypeChecker::TypeCheckerImpl::SetCaptureKind(
    const ASTContext& ctx, const NameReferenceExpr& nre, FuncBody& curFuncBody) const
{
    auto decl = nre.GetTarget();
    CJC_NULLPTR_CHECK(decl);
    auto targetFB = GetCurFuncBody(ctx, decl->scopeName);
    bool isLocal = !decl->TestAttr(Attribute::GLOBAL) && !decl->outerDecl; // Not gloabl and not member variable.
    if (targetFB != nullptr || isLocal) { // Capture a decl in funcBody.
        if (targetFB == &curFuncBody) {
            return;
        }
        if (auto varDecl = DynamicCast<VarDecl*>(decl); varDecl) {
            varDecl->EnableAttr(AST::Attribute::IS_CAPTURE);
            if (varDecl->isVar) {
                curFuncBody.capturedVars.emplace(&nre);
                curFuncBody.captureKind = CaptureKind::CAPTURE_VAR;
            }
        } else if (Is<FuncDecl>(decl)) {
            decl->EnableAttr(AST::Attribute::IS_CAPTURE);
        }
        return;
    }
    // Capture a decl in toplevel block.
    if (IsLocalDeclOutOfFuncBody(*decl, curFuncBody)) {
        if (auto varDecl = DynamicCast<VarDecl*>(decl); varDecl) {
            if (varDecl->isVar) {
                curFuncBody.captureKind = CaptureKind::CAPTURE_VAR;
            }
            if (!varDecl->TestAttr(AST::Attribute::GLOBAL) && !varDecl->TestAttr(AST::Attribute::IS_CAPTURE) &&
                varDecl->outerDecl == nullptr) {
                varDecl->EnableAttr(AST::Attribute::IS_CAPTURE);
            }
        } else if (Is<FuncDecl>(decl) && !decl->TestAttr(AST::Attribute::GLOBAL) && decl->outerDecl == nullptr) {
            decl->EnableAttr(AST::Attribute::IS_CAPTURE);
        }
    }
}

void TypeChecker::TypeCheckerImpl::CanTargetOfRefBeCaptured(
    const ASTContext& ctx, const NameReferenceExpr& nre, const Decl& decl, const FuncBody& curFuncBody) const
{
    CanTargetOfRefBeCapturedCaseNominalDecl(ctx, nre, decl, curFuncBody);
    CanTargetOfRefBeCapturedCaseMutFunc(ctx, nre, decl, curFuncBody);
}

void TypeChecker::TypeCheckerImpl::CanTargetOfRefBeCapturedCaseNominalDecl(
    const ASTContext& ctx, const NameReferenceExpr& nre, const Decl& decl, const FuncBody& curFuncBody) const
{
    auto funcSrc = ScopeManager::GetOutMostSymbol(ctx, SymbolKind::FUNC, nre.scopeName);
    if (!funcSrc) {
        return;
    }
    CJC_NULLPTR_CHECK(funcSrc->node);
    auto fd = StaticCast<FuncDecl*>(funcSrc->node);
    if (GetCurFuncBody(ctx, decl.scopeName)) {
        return;
    }
    // Member variables cannot be accessed in nested function/lambda of struct constructor.
    if (!curFuncBody.funcDecl || !curFuncBody.funcDecl->TestAttr(Attribute::CONSTRUCTOR)) {
        if (decl.TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::STATIC) || !fd->TestAttr(Attribute::CONSTRUCTOR)) {
            return;
        }
        CJC_NULLPTR_CHECK(fd->outerDecl);
        bool needCheck = decl.TestAttr(Attribute::IN_STRUCT) || (fd->outerDecl->astKind == ASTKind::CLASS_DECL &&
            (fd->outerDecl->TestAnyAttr(Attribute::ABSTRACT, Attribute::OPEN)));
        if (needCheck && fd->outerDecl == decl.outerDecl) {
            diag.Diagnose(nre, DiagKind::sema_illegal_capture_this,
                decl.TestAttr(Attribute::IN_STRUCT) ? "struct" : "inheritable class");
        }
    }
}

void TypeChecker::TypeCheckerImpl::CanTargetOfRefBeCapturedCaseMutFunc(
    const ASTContext& ctx, const NameReferenceExpr& nre, const Decl& decl, const FuncBody& curFuncBody) const
{
    auto funcSrc = ScopeManager::GetOutMostSymbol(ctx, SymbolKind::FUNC, nre.scopeName);
    if (funcSrc && Is<FuncDecl>(funcSrc->node)) {
        auto fd = RawStaticCast<FuncDecl*>(funcSrc->node);
        // the fields of the instance cannot be captured by lambda or internal function in a mut function.
        if (fd->TestAttr(AST::Attribute::MUT) && curFuncBody.funcDecl != fd &&
            decl.TestAttr(AST::Attribute::IN_STRUCT) && decl.astKind == ASTKind::VAR_DECL) {
            diag.Diagnose(nre, DiagKind::sema_capture_this_or_instance_field_in_func, decl.identifier.Val(),
                "mutable function '" + fd->identifier + "'");
        }
        if (fd->IsFinalizer() &&
            decl.TestAnyAttr(AST::Attribute::IN_CLASSLIKE, AST::Attribute::IN_EXTEND) &&
            !decl.TestAttr(AST::Attribute::CONSTRUCTOR) &&
            (decl.astKind == ASTKind::FUNC_DECL || decl.astKind == ASTKind::PROP_DECL) &&
            !decl.TestAttr(AST::Attribute::STATIC)) {
            diag.Diagnose(
                nre, DiagKind::sema_capture_this_or_instance_field_in_func, decl.identifier.Val(), "finalizer");
        }
    }
}

// Immutable function cannot access mutable function.
void TypeChecker::TypeCheckerImpl::CheckImmutableFuncAccessMutableFunc(
    const Position& pos, const Node& srcNode, const Decl& destNode, bool isLeftStructValue) const
{
    // Only need to check mutable accessing for struct 's var property.
    bool accessMutableTarget = (destNode.astKind == ASTKind::FUNC_DECL && destNode.TestAttr(AST::Attribute::MUT)) ||
        (destNode.astKind == ASTKind::PROP_DECL && isLeftStructValue && static_cast<const PropDecl&>(destNode).isVar);
    bool bothInstance = !srcNode.TestAttr(AST::Attribute::STATIC) && !destNode.TestAttr(AST::Attribute::STATIC);
    if (auto fdSrc = DynamicCast<const FuncDecl*>(&srcNode); fdSrc && !fdSrc->TestAttr(AST::Attribute::MUT) &&
        fdSrc->outerDecl != nullptr && bothInstance && !fdSrc->TestAttr(AST::Attribute::CONSTRUCTOR) &&
        !fdSrc->TestAttr(AST::Attribute::PRIMARY_CONSTRUCTOR) && accessMutableTarget) {
        std::string srcName = fdSrc->isGetter ? "get" : fdSrc->identifier.Val();
        std::string dstName = destNode.astKind == ASTKind::PROP_DECL ? "set" : destNode.identifier.Val();
        diag.Diagnose(srcNode, pos, DiagKind::sema_immutable_function_cannot_access_mutable_function, srcName, dstName);
    }
}

void TypeChecker::TypeCheckerImpl::CheckForbiddenFuncReferenceAccess(
    const Position& pos, const FuncDecl& fd, const Decl& decl) const
{
    if (!fd.outerDecl || !decl.outerDecl || !decl.IsFuncOrProp() ||
        decl.TestAnyAttr(Attribute::CONSTRUCTOR, Attribute::STATIC)) {
        return; // Only check for instance function and property, except constructor and static member.
    }
    // Calling any member function or property in constructor of an inheritable class is forbidden.
    auto inOpenClassCtor = fd.outerDecl->astKind == ASTKind::CLASS_DECL && fd.TestAttr(AST::Attribute::CONSTRUCTOR) &&
        fd.outerDecl->TestAnyAttr(AST::Attribute::OPEN, AST::Attribute::ABSTRACT);
    bool useMemberInCtor = inOpenClassCtor && decl.TestAnyAttr(AST::Attribute::IN_CLASSLIKE, AST::Attribute::IN_EXTEND);
    if (useMemberInCtor) {
        diag.Diagnose(fd, pos, DiagKind::sema_illegal_member_used_in_open_constructor, DeclKindToString(decl),
            decl.identifier.Val(), fd.outerDecl->identifier.Val());
    }
    // Finalizer is only allowed in class.
    // spec rule: this.xx, super.xx or current member (function or property) is forbidden in class finalizer.
    if (fd.IsFinalizer() && typeManager.IsSubtype(fd.outerDecl->ty, decl.outerDecl->ty)) {
        std::string type = decl.astKind == ASTKind::PROP_DECL ? "property" : "function";
        diag.DiagnoseRefactor(DiagKindRefactor::sema_instance_func_cannot_be_used_in_finalizer, pos, type);
    }
}

void TypeChecker::TypeCheckerImpl::MarkAndCheckRefExprVarCaptureStatus(
    const ASTContext& ctx, const NameReferenceExpr& nre) const
{
    auto target = nre.GetTarget();
    if (!target || target->IsTypeDecl()) {
        return;
    }
    auto curFuncBody = GetCurFuncBody(ctx, nre.scopeName);
    if (!curFuncBody) {
        return;
    }
    // Global or static variable decl is not treated as capture.
    bool canBeCaptured = !target->TestAnyAttr(Attribute::STATIC, Attribute::GLOBAL);
    if (canBeCaptured) {
        CanTargetOfRefBeCaptured(ctx, nre, *target, *curFuncBody);
        SetCaptureKind(ctx, nre, *curFuncBody);
    }
    if (nre.astKind == ASTKind::REF_EXPR) {
        CheckWarningOfCaptureVariable(ctx, StaticCast<const RefExpr&>(nre));
    }
}

Ptr<Decl> TypeChecker::TypeCheckerImpl::GetRealTarget(Ptr<Expr> const node, Ptr<Decl> const target)
{
    std::vector<Ptr<Decl>> targets = {target};
    HandleAlias(node, targets);
    return targets[0];
}

void TypeChecker::TypeCheckerImpl::SubstituteTypeForTypeAliasTypeMapping(
    const TypeAliasDecl& tad, const std::vector<Ptr<AST::Ty>>& typeArgs, TypeSubst& typeMapping) const
{
    if (!tad.generic || tad.generic->typeParameters.size() != typeArgs.size()) {
        return;
    }
    auto argsNum = tad.generic->typeParameters.size();
    for (auto& it : typeMapping) {
        for (size_t i = 0; i < argsNum; ++i) {
            if (it.second == tad.generic->typeParameters[i]->ty) {
                it.second = typeArgs[i];
                break;
            }
        }
    }
}

/**
 * Typealias's may be used recursively, so generate a typeMapping from used typealias decl to inner most typealias.
 *   type A<T0> = T0*T0
 *   type A1<T1> = A<T1>
 * Generate T0->T1.
 *   type B<T> = A<Rune>
 *   type C = B<Int64>
 * Generate T0->Rune for B and C.
 */
TypeSubst TypeChecker::TypeCheckerImpl::GenerateTypeMappingForTypeAliasDecl(const TypeAliasDecl& tad) const
{
    std::unordered_set<Ptr<const TypeAliasDecl>> visited;
    return GenerateTypeMappingForTypeAliasDeclVisit(tad, visited);
}

TypeSubst TypeChecker::TypeCheckerImpl::GenerateTypeMappingForTypeAliasDeclVisit(
    const TypeAliasDecl& tad, std::unordered_set<Ptr<const TypeAliasDecl>>& visited) const
{
    TypeSubst typeMapping;
    if (!tad.type) {
        return typeMapping;
    }
    if (visited.count(&tad) > 0) {
        return typeMapping;
    } else {
        visited.emplace(&tad);
    }
    auto target = tad.type->GetTarget();
    if (!target || !Ty::IsTyCorrect(tad.type->ty)) {
        return typeMapping;
    }
    if (target->astKind != ASTKind::TYPE_ALIAS_DECL) {
        // For target which is not typealias decl, generate typeMapping from used genericTy to itself.
        for (auto ty : tad.type->ty->typeArgs) {
            if (ty->IsGeneric()) {
                typeMapping[StaticCast<GenericsTy*>(ty)] = ty;
            }
        }
        return typeMapping;
    }

    auto targetTad = RawStaticCast<TypeAliasDecl*>(target);
    typeMapping = GenerateTypeMappingForTypeAliasDeclVisit(*targetTad, visited);
    // Get used typeArguments of current typealias decl.
    // eg. have 'type A1<T> = Type<Rune, T>' & 'type B1<X> = A1<X>'
    //     current tad is B1, target is A1. We need to collect X here.
    //  or have 'type A2<T, K> = Type<Rune, T>' & 'type B2<X> = A2<Int64, X>'
    //     current tad is B2, target is A1. We need to collect 'Int64 & X' here.
    std::vector<Ptr<AST::Ty>> typeArgs;
    for (auto& it : tad.type->GetTypeArgs()) {
        typeArgs.push_back(it->ty);
    }
    SubstituteTypeForTypeAliasTypeMapping(*targetTad, typeArgs, typeMapping);
    return typeMapping;
}

void TypeChecker::TypeCheckerImpl::HandleAlias(Ptr<Expr> expr, std::vector<Ptr<Decl>>& targets)
{
    for (auto& target : targets) {
        if (!target || target->astKind != ASTKind::TYPE_ALIAS_DECL) {
            continue;
        }
        auto aliasDecl = StaticCast<TypeAliasDecl*>(target);
        if (aliasDecl->type == nullptr) {
            continue;
        }
        auto innerTypeAliasTarget = GetLastTypeAliasTarget(*aliasDecl);
        if (auto realTarget = innerTypeAliasTarget->type->GetTarget(); realTarget) {
            target = realTarget;
            if (auto ref = DynamicCast<NameReferenceExpr*>(expr)) {
                auto wasEmpty = ref->typeArguments.empty();
                auto typeMapping = GenerateTypeMappingForTypeAliasUse(*aliasDecl, *ref);
                SubstituteTypeArguments(*innerTypeAliasTarget, ref->typeArguments, typeMapping);
                // Try to insert new typeArguments to ref's instTys.
                UpdateInstTysWithTypeArgs(*ref);
                if (wasEmpty && !ref->typeArguments.empty()) {
                    ref->compilerAddedTyArgs = true;
                }
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckWarningOfCaptureVariable(const ASTContext& ctx, const RefExpr& re) const
{
    auto target = re.GetTarget();
    if (!Is<VarDecl>(target) || Is<FuncParam>(target)) {
        return;
    }
    auto funcSym = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, re.scopeName);
    while (target != nullptr && funcSym != nullptr && funcSym->scopeLevel > target->scopeLevel) {
        auto funcScope = funcSym->scopeName.substr(0, funcSym->scopeName.find('_'));
        // If has another a same name decl in inter scope, should report a warning.
        auto decls = ctx.GetDeclsByName({target->identifier, funcScope});
        for (auto decl : decls) {
            if (!Is<VarDecl>(decl) || decl->identifier != target->identifier) {
                continue;
            }
            diag.Diagnose(
                re, DiagKind::sema_capture_has_shadow_variable, target->identifier.Val(), target->begin, decl->begin);
            return;
        }
        funcSym = ScopeManager::GetCurSymbolByKind(SymbolKind::FUNC_LIKE, ctx, funcSym->scopeName);
    }
}

bool TypeChecker::TypeCheckerImpl::CheckOptionBox(Ty& target, Ty& ty)
{
    if (typeManager.IsSubtype(&ty, &target)) {
        return true;
    }
    if (!Ty::IsTyCorrect(&target) || !target.IsCoreOptionType()) {
        return false;
    }
    if (typeManager.IsTyEqual(&ty, &target)) {
        return true;
    }
    auto curTarget = &target;
    while (Ty::IsTyCorrect(curTarget) && curTarget->IsCoreOptionType()) {
        CJC_ASSERT(curTarget->typeArgs.size() == 1);
        curTarget = curTarget->typeArgs[0];
        if (typeManager.IsTyEqual(&ty, curTarget)) {
            return true;
        }
    }
    return false;
}
