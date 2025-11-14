// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements typecheck apis for reference exprs.
 */

#include "TypeCheckerImpl.h"

#include "Diags.h"
#include "ScopeManager.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Symbol.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Basic/Position.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Modules/ModulesUtils.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace Sema;
using namespace AST;
using namespace TypeCheckUtil;

namespace {
inline bool CheckTargetTypeArgs(Ptr<const Decl> const decl, size_t numTypeArgs)
{
    Ptr<Generic> generic = decl ? decl->GetGeneric() : nullptr;
    return generic != nullptr && generic->typeParameters.size() == numTypeArgs;
}

inline bool CheckForQuestFuncRetType(DiagnosticEngine& diag, Decl& target, const Expr& expr, bool hasTargetTy)
{
    if (hasTargetTy) {
        return true; // Check after the return type later in 'ChkCallExpr' or 'ChkRefExpr'.
    }
    if (Ty::IsTyCorrect(target.ty) && target.ty->HasQuestTy() && target.astKind == ASTKind::FUNC_DECL) {
        DiagUnableToInferReturnType(diag, *StaticAs<ASTKind::FUNC_DECL>(&target), expr);
        return false;
    }
    return true;
}
} // namespace

void TypeChecker::TypeCheckerImpl::CheckThisOrSuper(const ASTContext& ctx, RefExpr& re)
{
    re.ty = TypeManager::GetInvalidTy();
    auto sym = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, re.scopeName);
    if (!sym || !Is<InheritableDecl>(sym->node)) {
        diag.Diagnose(re, DiagKind::sema_this_super_use_error_outside_class, re.ref.identifier.Val());
        return;
    }
    auto decl = RawStaticCast<InheritableDecl*>(sym->node);
    // Set this/super's type and target first. Will not diagnose.
    if (re.isThis) {
        re.ty = InferTypeOfThis(re, *decl);
    } else {
        re.ty = InferTypeOfSuper(re, *decl);
    }
    // Legality of using this/super will be checked after typecheck in 'CheckLegalityOfReference'.
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::InferTypeOfThis(RefExpr& re, InheritableDecl& objDecl)
{
    Ptr<InheritableDecl> outerDecl = &objDecl;
    auto typeDecl = outerDecl;
    // Handle special case: this in ExtendDecl: if extended types is built-in type, directly return extended Type.
    if (auto ed = AST::As<ASTKind::EXTEND_DECL>(typeDecl); ed && ed->extendedType && ed->extendedType->ty) {
        if (ed->extendedType->ty->IsBuiltin()) {
            return ed->extendedType->ty;
        }
        // Real type decl is decl of extened type.
        auto realTypeDecl = Ty::GetDeclPtrOfTy(ed->extendedType->ty);
        if (!Is<InheritableDecl>(realTypeDecl)) {
            return TypeManager::GetInvalidTy();
        }
        typeDecl = RawStaticCast<InheritableDecl*>(realTypeDecl);
    }
    ReplaceTarget(&re, typeDecl);

    // all this reference are checked as This type if possible (i.e. it is instance of class decl).
    auto ret = ReplaceWithGenericTyInInheritableDecl(outerDecl->ty, *outerDecl, *typeDecl);
    if (auto cd = DynamicCast<ClassTy>(ret)) {
        ret = typeManager.GetClassThisTy(*cd->decl, cd->typeArgs);
    }
    return ret;
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::InferTypeOfSuper(RefExpr& re, const InheritableDecl& objDecl)
{
    // Super can only used in classDecl, if not in class decl, super's ty is invalid.
    if (objDecl.astKind != ASTKind::CLASS_DECL) {
        return TypeManager::GetInvalidTy();
    }
    auto cd = RawStaticCast<const ClassDecl*>(&objDecl);
    if (auto classTy = DynamicCast<ClassTy*>(cd->ty); classTy) {
        auto superClassTy = classTy->GetSuperClassTy();
        if (superClassTy) {
            CJC_NULLPTR_CHECK(superClassTy->declPtr);
            ReplaceTarget(&re, superClassTy->declPtr);
            return ReplaceWithGenericTyInInheritableDecl(superClassTy->declPtr->ty, objDecl, *superClassTy->declPtr);
        }
    }
    return TypeManager::GetInvalidTy();
}

Ptr<Ty> TypeChecker::TypeCheckerImpl::ReplaceWithGenericTyInInheritableDecl(
    Ptr<Ty> ty, const Decl& outerDecl, const InheritableDecl& id)
{
    if (!ty || !ty->HasGeneric()) {
        return ty;
    }
    // Builds the generic mapping between typeArguments of the derived struc decl and the inherited decl,
    // such as: class decl & it's super class decl, the extend decl and the inheritable decl.
    // e.g.
    // class A<T> { let item: T }
    // extend A<T> { }
    // extend has an implicit generic, the above is in fact `extend<T1> A<T1> { }`
    // if we have
    // extend<T1> A<T1> {
    //     func foo(): T1 { return this.item }
    // }
    // The type of this.item is T, which is not consistent with the return type T1, but T1 is equivalent to T here.
    // So we need to replace the T's to T1's.
    // currentType is the A<T> (AST node) in class B<T> <: A<T>, or extend A<T>
    // targetGeneric is the <T> (AST node) at the declaration of A<T>, e.g. class A<T>
    Ptr<Ty> currentTy = outerDecl.ty;
    MultiTypeSubst typeMapping;
    if (currentTy && id.ty) {
        typeMapping = promotion.GetPromoteTypeMapping(*currentTy, *id.ty);
    }
    if (typeMapping.empty()) {
        return ty;
    }
    return typeManager.GetBestInstantiatedTy(ty, typeMapping);
}

void TypeChecker::TypeCheckerImpl::CheckUsageOfThis(const ASTContext& ctx, const RefExpr& re) const
{
    auto outerDecl = GetCurInheritableDecl(ctx, re.scopeName);
    if (outerDecl == nullptr) {
        return;
    }
    // 'curFuncBody' can belong to FuncDecl, LambdaExpr, MacroDecl.
    auto curFuncBody = GetCurFuncBody(ctx, re.scopeName);
    if (curFuncBody && curFuncBody->TestAttr(Attribute::STATIC)) {
        diag.Diagnose(re, DiagKind::sema_static_members_cannot_call_members, re.ref.identifier.Val());
    }
    // Any use of 'this()' call in interface is invalid.
    if (Is<InterfaceDecl>(outerDecl) && re.callOrPattern) {
        diag.Diagnose(re, DiagKind::sema_illegal_this_in_interface);
    }
    auto outMostFuncSymbol = ScopeManager::GetOutMostSymbol(ctx, SymbolKind::FUNC_LIKE, re.scopeName);
    Ptr<FuncDecl> outMostFunc = outMostFuncSymbol ? DynamicCast<FuncDecl*>(outMostFuncSymbol->node) : nullptr;
    bool insideConstructor = outMostFunc && IsInstanceConstructor(*outMostFunc);
    // When 'outMostFunc' is funcDecl, 'curFuncBody' must have valid value.
    if (outMostFunc) {
        // 'this' cannot be captured by lambda or internal function in a mut function.
        bool lambdaOrNestedFuncInMutFunc =
            outMostFunc->TestAttr(Attribute::MUT) && curFuncBody && (curFuncBody->funcDecl != outMostFunc);
        if (lambdaOrNestedFuncInMutFunc) {
            diag.Diagnose(re, DiagKind::sema_capture_this_or_instance_field_in_func, re.ref.identifier.Val(),
                "mutable function '" + outMostFunc->identifier + "'");
        }
        // 'this' cannot be used as an expression in a mut function, constructor of inheritable class or finalizer.
        bool referenceNotAllowed = re.isAlone &&
            (outMostFunc->TestAttr(Attribute::MUT) || outMostFunc->IsFinalizer() ||
                (insideConstructor && IsInheritableClass(*outerDecl)));
        if (referenceNotAllowed) {
            std::string info = outMostFunc->TestAttr(Attribute::MUT)
                ? "mutable function '" + outMostFunc->identifier + "'"
                : (outMostFunc->IsFinalizer() ? "finalizer"
                                              : std::string("constructor of ") +
                              (outerDecl->TestAttr(Attribute::OPEN) ? "open" : "abstract") + " class");
            diag.Diagnose(re, DiagKind::sema_use_this_as_an_expression_in_func, info);
        }
    }
    // Check usage of this in 'struct' decl except call like 'this()'.
    // Member variables this.xx cannot be accessed in nested function/lambda of 'struct' constructor
    bool referenceInStruct = outerDecl->astKind == ASTKind::STRUCT_DECL && !re.callOrPattern;
    if (referenceInStruct) {
        bool notInConstructor =
            curFuncBody && (!curFuncBody->funcDecl || !curFuncBody->funcDecl->TestAttr(Attribute::CONSTRUCTOR));
        if (notInConstructor && insideConstructor) {
            diag.Diagnose(re, DiagKind::sema_illegal_capture_this, "struct");
        }
        bool inInvalidCtx = !curFuncBody || (outMostFuncSymbol && outMostFuncSymbol->astKind == ASTKind::LAMBDA_EXPR);
        if (inInvalidCtx) {
            diag.Diagnose(re, DiagKind::sema_illegal_this_outside_struct_constructor);
        }
    }
    bool referenceInInitializer = !curFuncBody && !ctx.currentCheckingNodes.empty();
    if (referenceInInitializer) {
        CheckThisOrSuperInInitializer(*ctx.currentCheckingNodes.top(), re);
    }
    CheckInvalidRefInCFunc(ctx, re);
}

void TypeChecker::TypeCheckerImpl::CheckUsageOfSuper(const ASTContext& ctx, const RefExpr& re) const
{
    auto outerDecl = GetCurInheritableDecl(ctx, re.scopeName);
    if (outerDecl == nullptr) {
        return;
    }
    auto curFuncBody = GetCurFuncBody(ctx, re.scopeName);
    if (curFuncBody && curFuncBody->TestAttr(Attribute::STATIC)) {
        diag.Diagnose(re, DiagKind::sema_static_members_cannot_call_members, re.ref.identifier.Val());
    }
    // Any use of 'super' call in interface is invalid.
    if (Is<InterfaceDecl>(outerDecl)) {
        diag.Diagnose(re, DiagKind::sema_use_super_in_interface);
    }
    // 'super' is not allowed in extend decl or other non-class decl.
    if (outerDecl->astKind == ASTKind::EXTEND_DECL) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_extend_use_super, re);
    } else if (outerDecl->astKind != ASTKind::CLASS_DECL) {
        diag.Diagnose(re, DiagKind::sema_super_use_error_inside_non_class);
    } else {
        // It's illegal to use 'super' individually, e.g: var a = super
        if (re.isAlone) {
            diag.Diagnose(re, DiagKind::sema_illegal_super_alone);
        }
    }
    bool referenceInInitializer = !curFuncBody && !ctx.currentCheckingNodes.empty();
    if (referenceInInitializer) {
        CheckThisOrSuperInInitializer(*ctx.currentCheckingNodes.top(), re);
    }
    CheckInvalidRefInCFunc(ctx, re);
}

void TypeChecker::TypeCheckerImpl::CheckThisOrSuperInInitializer(const Node& node, const RefExpr& re) const
{
    if (node.astKind == ASTKind::VAR_DECL) {
        if (node.TestAttr(Attribute::STATIC)) {
            diag.Diagnose(
                re, DiagKind::sema_this_or_super_not_allowed_to_initialize_static_member, re.ref.identifier.Val());
        } else if (re.isSuper || (re.isThis && re.isAlone)) {
            diag.Diagnose(
                re, DiagKind::sema_this_or_super_not_allowed_to_initialize_non_static_member, re.ref.identifier.Val());
        }
    }
}

bool TypeChecker::TypeCheckerImpl::IsRefTypeArgSizeValid(const Expr& expr, std::vector<Ptr<Decl>>& targets)
{
    auto typeArgs = expr.GetTypeArgs();
    if (typeArgs.empty()) {
        return true; // Ignored for empty typeArguments.
    }
    bool isValid;
    // If re has type arguments, the number of type parameters of the target should match re.
    size_t numTypeArgs = typeArgs.size();
    // If typeArguments is already substitutied for typealias expression, here need to check with real target.
    bool typeAliasSubstituted = typeArgs[0]->TestAttr(Attribute::COMPILER_ADD) && !targets.empty() &&
        targets[0]->astKind == ASTKind::TYPE_ALIAS_DECL;
    if (typeAliasSubstituted) {
        isValid = CheckTargetTypeArgs(TypeCheckUtil::GetRealTarget(targets[0]), numTypeArgs);
    } else {
        for (auto it = targets.begin(); it != targets.end();) {
            (!CheckTargetTypeArgs(*it, numTypeArgs)) ? (it = targets.erase(it)) : (++it);
        }
        isValid = !targets.empty();
    }

    if (!isValid) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_generic_argument_no_match, expr);
    }
    return isValid;
}

void TypeChecker::TypeCheckerImpl::FilterCandidatesForRef(
    const ASTContext& ctx, const RefExpr& re, std::vector<Ptr<Decl>>& targets)
{
    auto curFuncBody = GetCurFuncBody(ctx, re.scopeName);
    // Filter macro func bcz macro can NOT be referred.
    Utils::EraseIf(targets, [curFuncBody](auto it) -> bool {
        if (!it) {
            return false; // Need keep null for re-export case. At least one valid target before null.
        }
        return (!curFuncBody && it->TestAttr(Attribute::MACRO_FUNC)) ||
            (curFuncBody && !curFuncBody->TestAttr(Attribute::MACRO_INVOKE_BODY) &&
                it->TestAttr(Attribute::MACRO_FUNC));
    });
    auto sym = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, re.scopeName);
    if (sym && Is<InheritableDecl>(sym->node)) {
        FilterTargetsInExtend(re, sym->node->ty, targets);
    }

    // Filter incompatible and shadowed function if all candidates are functions.
    if (IsAllFuncDecl(targets)) {
        std::vector<Ptr<FuncDecl>> funcs;
        std::for_each(
            targets.begin(), targets.end(), [&funcs](auto fd) { funcs.emplace_back(RawStaticCast<FuncDecl*>(fd)); });
        FilterShadowedFunc(funcs);
        targets.clear();
        targets.insert(targets.begin(), funcs.begin(), funcs.end());
    }
}

bool TypeChecker::TypeCheckerImpl::FilterInvalidEnumTargets(
    const NameReferenceExpr& nre, std::vector<Ptr<Decl>>& targets)
{
    CJC_ASSERT(!targets.empty());
    auto candidates = targets;
    auto ma = DynamicCast<MemberAccess>(&nre);
    // Filter enum constructor which is not matched.
    Utils::EraseIf(targets, [&nre, &ma](auto target) {
        return target && target->TestAttr(Attribute::ENUM_CONSTRUCTOR) && target->IsFunc() && !nre.callOrPattern &&
            (!ma || !ma->isPattern);
    });
    if (targets.empty()) {
        auto diagBuilder = diag.DiagnoseRefactor(
            DiagKindRefactor::sema_enum_constructor_with_param_must_have_args, nre, candidates[0]->identifier);
        for (auto& it : candidates) {
            diagBuilder.AddNote(*it, MakeRangeForDeclIdentifier(*it), "found candidate");
        }
        return false;
    }
    return true;
}

bool TypeChecker::TypeCheckerImpl::FilterAndCheckTargetsOfRef(
    const ASTContext& ctx, RefExpr& re, std::vector<Ptr<Decl>>& targets)
{
    RemoveDuplicateElements(targets);
    FilterCandidatesForRef(ctx, re, targets);

    if (targets.empty()) {
        diag.Diagnose(re, DiagKind::sema_undeclared_identifier, re.ref.identifier.Val());
        return false;
    }
    if (targets.size() == 1 && targets[0]->astKind == ASTKind::PACKAGE_DECL && re.isAlone) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_cannot_ref_to_pkg_name, re);
        targets.clear();
        return false;
    }
    // If source decls are not all functions, it will shadow imported decls.
    // If source decls not existed or there are all functions,
    // we need report error when there are more than one imported decls which are not all functions.
    auto sourceEndIt = std::stable_partition(
        targets.begin(), targets.end(), [](auto decl) { return !decl->TestAttr(Attribute::IMPORTED); });
    if (std::all_of(targets.begin(), sourceEndIt, [](auto decl) { return decl->astKind == ASTKind::FUNC_DECL; }) &&
        std::distance(sourceEndIt, targets.end()) > 1 &&
        std::any_of(sourceEndIt, targets.end(), [](auto decl) { return decl->astKind != ASTKind::FUNC_DECL; })) {
        std::vector<Ptr<Decl>> imports(sourceEndIt, targets.end());
        if (!std::all_of(imports.begin(), imports.end(), [](auto decl) { return decl->IsMemberDecl(); })) {
            DiagAmbiguousUse(diag, re, re.ref.identifier.Val(), imports, importManager);
            return false;
        }
    }

    if (!IsRefTypeArgSizeValid(re, targets) || !FilterInvalidEnumTargets(re, targets)) {
        return false;
    }

    // RefExpr in flow expr or func argument will be synthesized first and then checked, so ignored here.
    bool hasTarget = ctx.HasTargetTy(&re) || re.isInFlowExpr;
    bool referenceNeedTypeInfer = !hasTarget && re.isAlone;
    if (referenceNeedTypeInfer) {
        return FilterTargetsForFuncReference(re, targets);
    }
    hasTarget = hasTarget || re.callOrPattern != nullptr;
    return !targets.empty() ? CheckForQuestFuncRetType(diag, *targets[0], re, hasTarget) : false;
}

bool TypeChecker::TypeCheckerImpl::FilterTargetsForFuncReference(const Expr& expr, std::vector<Ptr<Decl>>& targets)
{
    auto typeArgs = expr.GetTypeArgs();
    // Reference without target type and used alone must be inferred.
    if (typeArgs.empty() && !targets.empty()) {
        // If reference has no type arguments and no target type, the target should not have type parameters.
        Utils::EraseIf(targets, [](auto it) { return it && it->IsFunc() && it->GetGeneric() != nullptr; });
        if (targets.empty()) {
            DiagGenericFuncWithoutTypeArg(diag, expr);
            return false;
        }
    }

    // If this reference needs to be inferred and has multiple function targets, the target cannot be distinguished.
    if (targets.size() > 1 && IsAllFuncDecl(targets)) {
        DiagAmbiguousUse(diag, expr, targets.front()->identifier, targets, importManager);
        targets.clear();
        return false;
    }
    return true;
}

void TypeChecker::TypeCheckerImpl::RemoveTargetNotMeetExtendConstraint(
    const Ptr<Ty> baseTy, std::vector<Ptr<Decl>>& targets)
{
    Utils::EraseIf(targets, [this, &baseTy](auto target) -> bool {
        bool ignored = !target || !Ty::IsTyCorrect(baseTy) || !Is<InheritableDecl>(target->outerDecl) ||
            !Ty::IsTyCorrect(target->outerDecl->ty);
        if (ignored) {
            return false;
        }
        // Check whether target in extend decl or inherited interface decl is accessible from member base type.
        auto prTys = promotion.Promote(*baseTy, *target->outerDecl->ty);
        if (prTys.empty()) {
            // No promoted type existed means the 'baseTy' not fit constraint of inheriting target's type.
            return true;
        }
        Ptr<ExtendDecl> extend = nullptr;
        if (auto res = typeManager.GetExtendDeclByInterface(*baseTy, *target->outerDecl->ty)) {
            extend = *res;
        } else {
            extend = DynamicCast<ExtendDecl*>(target->outerDecl);
        }
        if (extend && extend->extendedType && extend->extendedType->ty) {
            prTys = promotion.Promote(*baseTy, *extend->extendedType->ty);
            auto promotedTy = prTys.empty() ? TypeManager::GetInvalidTy() : *prTys.begin();
            if (Ty::IsTyCorrect(promotedTy)) {
                return !typeManager.CheckGenericDeclInstantiation(extend, promotedTy->typeArgs);
            }
        }
        return false;
    });
}

bool TypeChecker::TypeCheckerImpl::FilterTargetsInExtend(
    const AST::NameReferenceExpr& nre, Ptr<Ty> baseTy, std::vector<Ptr<Decl>>& targets)
{
    if (auto ma = DynamicCast<MemberAccess*>(&nre)) {
        CJC_NULLPTR_CHECK(ma->baseExpr);
        auto target = ma->baseExpr->GetTarget();
        if (target && target->IsTypeDecl() && baseTy && !IsThisOrSuper(*ma->baseExpr)) {
            // If the baseExpr is typeDecl and the current memberAccess is the baseFunc of a call,
            // the typeArg is inferrable from callExpr, do not filter candidates with constraint for now.
            bool inferrable = !baseTy->typeArgs.empty() && ma->baseExpr->GetTypeArgs().empty() && ma->callOrPattern;
            if (inferrable) {
                return true;
            }
        }
    }
    RemoveTargetNotMeetExtendConstraint(baseTy, targets);
    return !targets.empty();
}

bool TypeChecker::TypeCheckerImpl::FilterAndCheckTargetsOfNameAccess(
    const ASTContext& ctx, const MemberAccess& ma, std::vector<Ptr<Decl>>& targets)
{
    if (targets.empty()) {
        DiagMemberAccessNotFound(ma);
        return false;
    }
    // Erase members which cannot accessed by package name or type name.
    Utils::EraseIf(targets, [](auto target) {
        return target && !target->TestAttr(Attribute::ENUM_CONSTRUCTOR) && !target->TestAttr(Attribute::STATIC) &&
            !target->TestAttr(Attribute::GLOBAL);
    });
    if (targets.empty()) {
        diag.Diagnose(ma, DiagKind::sema_illegal_access_non_static_member, ma.field.Val());
        return false;
    }
    RemoveDuplicateElements(targets);
    if (!FilterTargetsInExtend(ma, ma.baseExpr->ty, targets)) {
        DiagMemberAccessNotFound(ma);
        return false;
    } else if (!IsRefTypeArgSizeValid(ma, targets) || !FilterInvalidEnumTargets(ma, targets)) {
        return false;
    }

    if (targets.empty()) {
        DiagMemberAccessNotFound(ma);
        return false;
    }

    // MemberAccess in func argument will be synthesized first and then checked, so ignored here.
    bool referenceNeedTypeInfer =
        ((!ctx.HasTargetTy(&ma) && (ma.isAlone || !ma.callOrPattern)) ||
            (targets[0]->astKind != ASTKind::FUNC_DECL && !targets[0]->TestAttr(Attribute::ENUM_CONSTRUCTOR))) &&
        !ma.isInFlowExpr && !ma.isPattern;
    if (referenceNeedTypeInfer) {
        CJC_ASSERT(ma.baseExpr);
        // If type argument is not given in base and can't be inferred, report error.
        auto targetOfBase = ma.baseExpr->GetTarget();
        bool noTypeArgs = NeedFurtherInstantiation(ma.baseExpr->GetTypeArgs());
        bool genericBaseWithoutTypeArg =
            targetOfBase && targetOfBase->IsTypeDecl() && targetOfBase->GetGeneric() && noTypeArgs;
        if (genericBaseWithoutTypeArg) {
            diag.Diagnose(*ma.baseExpr, DiagKind::sema_generic_type_without_type_argument);
            return false;
        }
        return FilterTargetsForFuncReference(ma, targets);
    }
    bool hasTarget = ctx.HasTargetTy(&ma) || ma.isInFlowExpr || ma.callOrPattern != nullptr;
    return !targets.empty() ? CheckForQuestFuncRetType(diag, *targets[0], ma, hasTarget) : false;
}

Ptr<Decl> TypeChecker::TypeCheckerImpl::FilterAndGetTargetsOfObjAccess(
    const ASTContext& ctx, MemberAccess& ma, std::vector<Ptr<Decl>>& targets)
{
    if (targets.empty()) {
        DiagMemberAccessNotFound(ma);
        return nullptr;
    }
    CJC_NULLPTR_CHECK(ma.baseExpr);
    if (!FilterTargetsInExtend(ma, ma.baseExpr->ty, targets)) {
        DiagMemberAccessNotFound(ma);
        return nullptr;
    }
    for (auto it = targets.begin(); it != targets.end();) {
        // Objects cannot be used to access static members.
        auto decl = *it;
        if (decl && decl->TestAttr(Attribute::STATIC)) {
            it = targets.erase(it);
        } else {
            if (decl && decl->astKind == ASTKind::FUNC_DECL) {
                ma.targets.push_back(StaticAs<ASTKind::FUNC_DECL>(decl));
            }
            ++it;
        }
    }
    if (targets.empty()) {
        diag.Diagnose(ma, DiagKind::sema_object_cannot_access_static_member, ma.field.Val());
    }
    bool hasTarget = ctx.HasTargetTy(&ma) || ma.isInFlowExpr;
    bool referenceNeedTypeInfer = !hasTarget && (ma.isAlone || !ma.callOrPattern);
    if (referenceNeedTypeInfer) {
        if (targets.empty()) {
            return nullptr;
        }
        auto accessibleDecls = GetAccessibleDecls(ctx, ma, targets);
        // If there are accessible decls, checking for function reference resovling.
        if (!accessibleDecls.empty() && !FilterTargetsForFuncReference(ma, accessibleDecls)) {
            return nullptr;
        }
        return accessibleDecls.empty() ? targets[0] : accessibleDecls[0];
    }
    auto target = GetAccessibleDecl(ctx, ma, targets);
    if (!target) {
        if (targets.empty()) {
            return nullptr;
        }
        target = targets[0];
    }
    hasTarget = hasTarget || ma.callOrPattern != nullptr;
    return target && CheckForQuestFuncRetType(diag, *target, ma, hasTarget) ? target : nullptr;
}

void TypeChecker::TypeCheckerImpl::InstantiateReferenceType(
    const ASTContext& ctx, NameReferenceExpr& expr, const TypeSubst& instantiateMap)
{
    if (expr.compilerAddedTyArgs) {
        expr.typeArguments.clear();
    }
    auto target = expr.GetTarget();
    if (!expr.GetTypeArgs().empty()) {
        // For partial generic typealias case.
        if (target && target->astKind != ASTKind::TYPE_ALIAS_DECL) {
            UpdateInstTysWithTypeArgs(expr);
        }
        CheckGenericExpr(expr);
    }
    SubstPack typeMapping;
    if (instantiateMap.empty()) {
        typeMapping = GenerateGenericTypeMapping(ctx, expr);
        auto tys = typeManager.ApplySubstPackNonUniq(expr.ty, typeMapping, true);
        bool cannotInfer = tys.size() != 1 && Is<FuncDecl*>(target) && !ctx.HasTargetTy(&expr);
        if (cannotInfer) {
            diag.Diagnose(expr, DiagKind::sema_ambiguous_func_ref, target->identifier.Val());
        }
        expr.ty = *tys.begin();
    } else {
        typeManager.PackMapping(typeMapping, instantiateMap);
        expr.ty = typeManager.GetInstantiatedTy(expr.ty, instantiateMap);
    }
    if (!Ty::IsTyCorrect(expr.ty)) {
        expr.ty = TypeManager::GetInvalidTy();
    } else if (auto fd = DynamicCast<FuncDecl*>(target); fd) {
        DynamicBindingThisType(expr, *fd, typeMapping);
    }
}

void TypeChecker::TypeCheckerImpl::CheckAccessLegalityOfRefExpr(const ASTContext& ctx, RefExpr& re)
{
    // Get target from re.ref.target, if re.ref.target is null but re.ref.targets not empty, get first of targets.
    auto decl = (!re.ref.target && !re.ref.targets.empty()) ? re.ref.targets[0] : re.ref.target;
    if (!decl || decl->astKind == ASTKind::PACKAGE_DECL) {
        return;
    }
    if (auto fd = DynamicCast<FuncDecl*>(decl); fd && fd->propDecl) {
        decl = fd->propDecl; // Property was desugar to get/set, resest checking decl to propDecl.
    }
    auto funcSrc = ScopeManager::GetOutMostSymbol(ctx, SymbolKind::FUNC, re.scopeName);
    if (funcSrc && Is<FuncDecl>(funcSrc->node)) {
        auto outerFd = RawStaticCast<FuncDecl*>(funcSrc->node);
        // Checking with propDecl's funcDecl.
        bool accessStructLeftValue =
            re.TestAttr(Attribute::LEFT_VALUE) && outerFd->funcBody && outerFd->funcBody->parentStruct;
        CheckImmutableFuncAccessMutableFunc(re.begin, *outerFd, *decl, accessStructLeftValue);
        // Should use original decl here.
        CheckForbiddenFuncReferenceAccess(re.begin, *outerFd, *decl);
    }
    if (decl->IsFunc()) {
        if (re.isAlone) {
            (void)CheckFuncAccessControl(ctx, re, re.ref.targets);
            // The unsafe function can only be called rather than as name reference
            if (decl->TestAttr(Attribute::UNSAFE) && !re.isInFlowExpr) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_unsafe_func_can_only_be_called, re);
            }
        }
    } else {
        (void)CheckNonFuncAccessControl(ctx, re, *decl);
    }
    // We check if a static function will access non-static variables or non-static functions.
    // Note that, if we have multiple candidates here, we will not be able to implement the
    // check. In that case, we will leave the check to the place where we resolve the overload
    // issue.
    // NOTE: some desugared expressions only have 're.ref.target' with no targets, so checking with <= 1.
    if (re.ref.targets.size() <= 1) {
        (void)IsLegalAccessFromStaticFunc(ctx, re, *decl);
    }
    // When isAlone is false, refExpr is part of a rvalue expression, which can be callExpr or memberAccess (the
    // value of isAlone is set when checking these nodes). Since a composite type cannot appear alone as an rvalue,
    // it needs to be checked when isAlone is true. For example:  let a = A is wrong when A is a class. The flag
    // isAlone is true in this case;
    if (re.isAlone && decl->IsTypeDecl()) {
        diag.Diagnose(re, DiagKind::sema_ref_not_be_type, re.ref.identifier.Val())
            .AddNote(*decl, DiagKind::sema_found_candidate_decl);
    }
    CheckInvalidRefInCFunc(ctx, re, *decl);
}

void TypeChecker::TypeCheckerImpl::CheckAccessLegalityOfMemberAccess(const ASTContext& ctx, MemberAccess& ma)
{
    // Get target from ma.target, if ma.target is null but ma.targets not empty, get first of targets.
    auto decl = (!ma.target && !ma.targets.empty()) ? ma.targets[0] : ma.target;
    if (!decl || decl->astKind == ASTKind::PACKAGE_DECL || !ma.baseExpr) {
        return;
    }
    if (auto fd = DynamicCast<FuncDecl*>(decl); fd && fd->propDecl) {
        decl = fd->propDecl; // Property was desugar to get/set, resest checking decl to propDecl.
    }
    if (decl->IsFunc()) {
        if (ma.isAlone) {
            std::vector<Ptr<Decl>> targets{ma.target};
            (void)CheckFuncAccessControl(ctx, ma, targets);
            // The unsafe function can only be called rather than as name reference
            if (decl->TestAttr(Attribute::UNSAFE) && !ma.isInFlowExpr) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_unsafe_func_can_only_be_called, ma);
            }
        }
    } else {
        (void)CheckNonFuncAccessControl(ctx, ma, *decl);
    }
    auto targetOfBase = ma.baseExpr->GetTarget();
    // Whether current is access member by real/generic type.
    bool isStaticAccessByName = targetOfBase && targetOfBase->IsTypeDecl() && !IsThisOrSuper(*ma.baseExpr);
    if (decl->IsTypeDecl() || decl->TestAnyAttr(Attribute::GLOBAL, Attribute::STATIC) || isStaticAccessByName) {
        CheckStaticMemberAccessLegality(ma, *decl);
    } else {
        CheckInstanceMemberAccessLegality(ctx, ma, *decl);
    }
}

void TypeChecker::TypeCheckerImpl::CheckStaticMemberAccessLegality(const MemberAccess& ma, const Decl& target)
{
    auto targetOfBase = ma.baseExpr->GetTarget();
    if (targetOfBase) {
        IsNamespaceMemberAccessLegal(ma, *targetOfBase, target);
    }

    // When isAlone is false, reference is part of a rvalue expression, which can be callExpr or memberAccess (the
    // value of isAlone is set when checking these nodes). Since a composite type cannot appear alone as an rvalue,
    // it needs to be checked when isAlone is true. For example:  let a = A is wrong when A is a class. The flag
    // isAlone is true in this case;
    if (ma.isAlone && target.IsTypeDecl()) {
        diag.Diagnose(ma, DiagKind::sema_ref_not_be_type, ma.field.Val())
            .AddNote(target, DiagKind::sema_found_candidate_decl);
    }
}

void TypeChecker::TypeCheckerImpl::IsNamespaceMemberAccessLegal(
    const MemberAccess& ma, AST::Decl& targetOfBaseDecl, const Decl& target)
{
    auto realTarget = TypeCheckUtil::GetRealTarget(&targetOfBaseDecl);
    // Check whether the accessed member is external if is accessed from another package.
    if (targetOfBaseDecl.astKind == ASTKind::PACKAGE_DECL && target.TestAttr(Attribute::GLOBAL)) {
        // Only external target can be accessed by other packages.
        std::string packageName = ma.curFile && ma.curFile->curPackage ? ma.curFile->curPackage->fullPackageName : "";
        std::string targetPackage = target.fullPackageName;
        auto relation = Modules::GetPackageRelation(packageName, targetPackage);
        if (!Modules::IsVisible(target, relation)) {
            diag.Diagnose(ma, DiagKind::sema_package_internal_decl_obtain_illegal, GetAccessLevelStr(target),
                ma.field.Val(), targetPackage);
        }
    } else if (!ma.isPattern && ma.isAlone && ma.baseExpr->ty && ma.baseExpr->ty->kind == TypeKind::TYPE_ENUM &&
        target.astKind == ASTKind::FUNC_DECL && target.TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
        DiagMemberAccessNotFound(ma);
    } else if (realTarget->IsNominalDecl() && !realTarget->TestAttr(Attribute::FOREIGN) &&
        target.TestAttr(Attribute::ABSTRACT)) {
        auto fieldRange = MakeRange(ma.field);
        std::string strType = target.astKind == ASTKind::FUNC_DECL ? "function" : "property";
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_interface_call_with_unimplemented_call, ma, fieldRange, strType, ma.field.Val());
    }
    // Handle generic enum var field like: TimeUnit<Int32>.century.
    if (target.TestAttr(Attribute::ENUM_CONSTRUCTOR) && !ma.typeArguments.empty()) {
        // TimeUnit.century<Int32> is not allowed.
        diag.Diagnose(ma, DiagKind::sema_invalid_type_param_of_enum_member_access, ma.field.Val(),
            targetOfBaseDecl.identifier.Val());
    }
}

void TypeChecker::TypeCheckerImpl::CheckInstanceMemberAccessLegality(
    const ASTContext& ctx, const MemberAccess& ma, const Decl& target)
{
    CheckForbiddenMemberAccess(ctx, ma, target);
    CheckLetInstanceAccessMutableFunc(ctx, ma, target);

    // Check for 'super.func()' calling abstract function.
    auto refExpr = As<ASTKind::REF_EXPR>(ma.baseExpr.get());
    bool isTargetAbstract = refExpr && refExpr->isSuper && target.TestAttr(Attribute::ABSTRACT);
    if (isTargetAbstract) {
        diag.Diagnose(ma, DiagKind::sema_abstract_method_cannot_be_accessed_directly, target.identifier.Val());
    }
}

void TypeChecker::TypeCheckerImpl::CheckLegalityOfReference(ASTContext& ctx, Node& node)
{
    unsigned id = Walker::GetNextWalkerID();
    std::function<VisitAction(Ptr<Node>)> postVisit = [this, &ctx](Ptr<Node> node) -> VisitAction {
        if (node->astKind == ASTKind::VAR_DECL) {
            ctx.currentCheckingNodes.pop();
        }
        if (auto ma = DynamicCast<MemberAccess*>(node); ma) {
            // Needs to check memberAccess's child node first, so added to post visit.
            CheckAccessLegalityOfMemberAccess(ctx, *ma);
        }
        return VisitAction::WALK_CHILDREN;
    };
    std::function<VisitAction(Ptr<Node>)> preVisit = [this, &ctx, &preVisit, &postVisit, id](
                                                         Ptr<Node> node) -> VisitAction {
        if (node->astKind == ASTKind::PRIMARY_CTOR_DECL) {
            return VisitAction::SKIP_CHILDREN;
        } else if (node->astKind == ASTKind::ANNOTATION) {
            return VisitAction::SKIP_CHILDREN;
        } else if (node->astKind == ASTKind::VAR_DECL) {
            ctx.currentCheckingNodes.push(node);
        }
        // If current node is desugared func argument, ignore the checking.
        if (node->astKind == ASTKind::FUNC_ARG && node->TestAttr(Attribute::HAS_INITIAL)) {
            return VisitAction::SKIP_CHILDREN;
        }
        if (auto expr = DynamicCast<Expr*>(node); expr) {
            if (expr->desugarExpr) {
                // Only check desugared nodes.
                Walker(expr->desugarExpr.get(), id, preVisit, postVisit).Walk();
                return VisitAction::SKIP_CHILDREN;
            } else if (auto ref = DynamicCast<NameReferenceExpr*>(expr); ref && !ref->instTys.empty()) {
                CheckInstTypeCompleteness(ctx, *ref);
            }
        }
        if (auto re = DynamicCast<RefExpr*>(node)) {
            if (re->isThis) {
                CheckUsageOfThis(ctx, *re);
            } else if (re->isSuper) {
                CheckUsageOfSuper(ctx, *re);
            } else {
                CheckAccessLegalityOfRefExpr(ctx, *re);
            }
        } else if (auto fa = DynamicCast<FuncArg>(node); fa && fa->withInout) {
            CheckMutationInStruct(ctx, *fa->expr);
        } else if (auto ae = DynamicCast<AssignExpr>(node)) {
            CheckMutationInStruct(ctx, *ae->leftValue);
        } else if (auto ide = DynamicCast<IncOrDecExpr>(node)) {
            CheckMutationInStruct(ctx, *ide->expr);
        } else if (auto fd = DynamicCast<FuncDecl>(node); fd && IsInstanceConstructor(*fd)) {
            CheckMemberAccessInCtorParamOrCtorArg(ctx, *fd);
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(&node, id, preVisit, postVisit).Walk();
}

std::optional<std::string> TypeChecker::TypeCheckerImpl::GetDiagnoseKindOfFuncDecl(
    const Ptr<Node> usage,
    const Ptr<Decl> target
) const
{
    if (target->TestAnyAttr(Attribute::MACRO_FUNC)) {
        // deprecation of macroses checked in MACRO_EXPAND stage
        return std::nullopt;
    }

    if (target->TestAnyAttr(Attribute::CONSTRUCTOR)) {
        return "constructor";
    }

    auto funcDecl = DynamicCast<const FuncDecl*>(target);
    if (!funcDecl) {
        InternalError("decl with ASTKind::FuncDecl can not be casted to FuncDecl.");
        return std::nullopt;
    }

    if (target->TestAnyAttr(Attribute::ENUM_CONSTRUCTOR)) {
        if (usage->astKind == ASTKind::MEMBER_ACCESS) {
            auto ma = DynamicCast<MemberAccess*>(usage);
            if (ma && ma->isPattern) {
                // enum constructor used as pattern matching
                // according to spec this case is not reported
                return std::nullopt;
            }
        }

        if (funcDecl->funcBody && funcDecl->funcBody->parentEnum) {
            std::string enumName = funcDecl->funcBody->parentEnum->identifier.GetRawText();

            return "enum '" + enumName + "' constructor";
        } else {
            InternalError("it's enum constructor with no parent class like decl:" +
                funcDecl->identifier.GetRawText());
        }
    } else if (funcDecl->isSetter) {
        return "property setter of";
    } else if (funcDecl->op != TokenKind::ILLEGAL) {
        return "operator";
    } else {
        return "function";
    }

    return std::nullopt;
}

std::string TypeChecker::TypeCheckerImpl::GetDiagnoseKindOfVarDecl(const Ptr<Decl> target) const
{
    if (target->TestAttr(AST::Attribute::ENUM_CONSTRUCTOR)) {
        if (auto vd = StaticCast<VarDecl*>(target); vd) {
            if (vd->outerDecl) {
                return "enum '" + vd->outerDecl->identifier.GetRawText() + "' constructor";
            } else {
                return "enum constructor";
            }
        }
    }
    return "variable";
}

std::optional<std::string> TypeChecker::TypeCheckerImpl::GetKindfOfDeprecatedDeclaration(
    const Ptr<Decl> target, const Ptr<Node> usage) const
{
    switch (target->astKind) {
        case ASTKind::CLASS_DECL:
        case ASTKind::INTERFACE_DECL:
        case ASTKind::STRUCT_DECL:
        case ASTKind::ENUM_DECL:
        case ASTKind::PROP_DECL:
        case ASTKind::MACRO_DECL:
        case ASTKind::TYPE_ALIAS_DECL: {
            return DeclKindToString(*target);
        }
        case ASTKind::VAR_DECL: {
            return GetDiagnoseKindOfVarDecl(target);
        }
        case ASTKind::FUNC_DECL: {
            auto fdKind = GetDiagnoseKindOfFuncDecl(usage, target);
            if (fdKind.has_value()) {
                return fdKind.value();
            }
            return std::nullopt;
        }
        case ASTKind::FUNC_PARAM: {
            if (target->TestAttr(Attribute::HAS_INITIAL)) {
                return "function parameter";
            }
            break;
        }
        default: {
            InternalError("Unhanled @Deprecated declaration of kind " + ASTKIND_TO_STRING_MAP[target->astKind]);
            break;
        }
    }
    return std::nullopt;
}

bool TypeChecker::TypeCheckerImpl::ShouldSkipDeprecationDiagnostic(const Ptr<Decl> target, bool strict)
{
    if (strictDeprecatedContext && target->IsSamePackage(*strictDeprecatedContext)) {
        return true;
    }

    if (!strict && deprecatedContext && target->IsSamePackage(*deprecatedContext)) {
        return true;
    }

    return false;
}

void TypeChecker::TypeCheckerImpl::DiagnoseDeprecatedUsage(
    const Ptr<Node> usage,
    const Ptr<Decl> target,
    const std::string& nameOfDiagnose
)
{
    std::string name = nameOfDiagnose != ""
        ? nameOfDiagnose
        : target->identifier.GetRawText();

    std::optional<std::string> kind = GetKindfOfDeprecatedDeclaration(target, usage);
    if (!kind.has_value()) {
        return;
    }

    Ptr<Annotation> deprecated;
    for (auto& annotation : target->annotations) {
        if (annotation->kind == AnnotationKind::DEPRECATED) {
            deprecated = annotation.get();
            break;
        }
    }
    InternalError("@Deprecated annotation not found in target for DiagnoseDeprecatedUsage" && deprecated);

    std::string message = "";
    std::string since = ".";
    bool strict = false;

    AST::ExtractArgumentsOfDeprecatedAnno(deprecated, message, since, strict);

    auto range = MakeRange(usage->begin, usage->end);
    if (usage->astKind == ASTKind::MEMBER_ACCESS) {
        if (auto ma = StaticCast<MemberAccess*>(usage); ma) {
            auto begin = ma->field.Begin();
            if (!begin.IsZero()) {
                range.begin = begin;
            }
            auto end = ma->field.End();
            if (!end.IsZero()) {
                range.end = end;
            }
        }
    }

    auto diagnoseKind = strict
        ? DiagKindRefactor::sema_deprecated_error
        : DiagKindRefactor::sema_deprecated_warning;

    if (kind.value() == "") {
        InternalError("Kind of deprecated declaration diagnostic must be defined.");
    }
    if (ShouldSkipDeprecationDiagnostic(target, strict)) {
        return;
    }
    diag.DiagnoseRefactor(diagnoseKind, *usage, range, kind.value(), name, since, message);
}

void TypeChecker::TypeCheckerImpl::CheckUsageOfDeprecatedParameters(
    const Ptr<Node> usage
)
{
    auto ce = DynamicCast<const CallExpr*>(usage);
    if (!ce) {
        return;
    }

    auto fd = ce->resolvedFunction;
    if (!fd) {
        return;
    }

    if (!fd->funcBody || fd->funcBody->paramLists.empty()) {
        return;
    }

    if (!ce->desugarArgs.has_value()) {
        return;
    }

    for (size_t i = 0; i < fd->funcBody->paramLists.size(); i++) {
        auto& params = fd->funcBody->paramLists[i]->params;
        auto& args = ce->desugarArgs.value();

        if (params.size() != args.size()) {
            continue;
        }

        for (size_t j = 0; j < args.size(); j++) {
            auto& param = params[j];
            auto& arg = args[j];

            if (param->HasAnno(AnnotationKind::DEPRECATED) &&
                !arg->TestAnyAttr(Attribute::COMPILER_ADD)) {
                DiagnoseDeprecatedUsage(arg, param);
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckUsageOfDeprecatedNominative(
    const Ptr<Node> usage,
    const Ptr<Decl> target
)
{
    if (auto constructor = DynamicCast<FuncDecl*>(target); constructor) {
        auto funcBody = constructor->funcBody.get();
        if (funcBody) {
            Ptr<Decl> parentDecl;

            if (auto pc = funcBody->parentClassLike; pc != nullptr) {
                parentDecl = pc;
            } else if (auto ps = funcBody->parentStruct; ps != nullptr) {
                parentDecl = ps;
            } else if (auto pe = funcBody->parentEnum; pe != nullptr) {
                parentDecl = pe;
            }

            if (parentDecl != nullptr && parentDecl->HasAnno(AnnotationKind::DEPRECATED)) {
                DiagnoseDeprecatedUsage(usage, parentDecl);
            } else if (auto refExpr = DynamicCast<RefExpr*>(usage);
                       refExpr != nullptr &&
                       refExpr->aliasTarget != nullptr &&
                       refExpr->aliasTarget->HasAnno(AnnotationKind::DEPRECATED)) {
                // unlike the previous case, it's the typealias is deprecated
                // so we are pointing to it instead
                DiagnoseDeprecatedUsage(usage, refExpr->aliasTarget);
            }
        }
    }

    if (target->TestAttr(AST::Attribute::ENUM_CONSTRUCTOR)) {
        auto vd = DynamicCast<VarDecl*>(target);
        if (!vd) {
            return;
        }
        if (!vd->outerDecl) {
            return;
        }

        auto pe = vd->outerDecl;
        if (!pe || !pe->HasAnno(AnnotationKind::DEPRECATED)) {
            return;
        }
        DiagnoseDeprecatedUsage(usage, pe);
    }
}

void TypeChecker::TypeCheckerImpl::CheckOverridingOrRedefiningOfDeprecated(
    const Ptr<Decl> overridden,
    const Ptr<Decl> overriding,
    const std::string& declType
)
{
    if (!overridden || !overridden->HasAnno(AnnotationKind::DEPRECATED)) {
        return;
    }

    auto isOverridenStrict = IsDeprecatedStrict(overridden);
    if (overriding->HasAnno(AnnotationKind::DEPRECATED)) {
        if (isOverridenStrict && !IsDeprecatedStrict(overriding)) {
            auto diagnoseKind = DiagKindRefactor::sema_deprecation_weakening;

            diag.DiagnoseRefactor(diagnoseKind, *overriding);
        }
    } else {
        bool isRedefined = overridden->TestAnyAttr(Attribute::STATIC);
        DiagKindRefactor diagnoseKind;
        if (isRedefined) {
            diagnoseKind = isOverridenStrict
                ? DiagKindRefactor::sema_deprecation_redef_error
                : DiagKindRefactor::sema_deprecation_redef_warning;
        } else {
            diagnoseKind = isOverridenStrict
                ? DiagKindRefactor::sema_deprecation_override_error
                : DiagKindRefactor::sema_deprecation_override_warning;
        }

        auto name = declType == "setter"
            ? "set"
            : overriding->identifier.GetRawText();

        diag.DiagnoseRefactor(
            diagnoseKind,
            *overriding,
            declType,
            name);
    }
}

void TypeChecker::TypeCheckerImpl::CheckOverridingOrRedefinitionOfDeprecatedFunction(
    const Ptr<ClassDecl> cd,
    const Ptr<Decl> member
)
{
    if (!Is<FuncDecl*>(member) || member->TestAttr(Attribute::GENERIC)) {
        return;
    }
    auto funcOverride = StaticCast<FuncDecl*>(member.get());

    for (auto& inheritedType : cd->inheritedTypes) {
        auto inheritedDecl = Ty::GetDeclPtrOfTy(inheritedType->ty);
        if (!inheritedDecl) {
            continue;
        }

        for (auto& memberDecl : inheritedDecl->GetMemberDecls()) {
            if (auto baseFuncDecl = As<ASTKind::FUNC_DECL>(memberDecl); baseFuncDecl) {
                if (Is<FuncTy*>(baseFuncDecl->ty) &&
                    Is<FuncTy*>(funcOverride->ty) &&
                    typeManager.IsFuncDeclSubType(*funcOverride, *baseFuncDecl)
                ) {
                    CheckOverridingOrRedefiningOfDeprecated(
                        baseFuncDecl,
                        funcOverride,
                        "function");
                }
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckOverridingOrRedefinitionOfDeprecatedProperty(
    const Ptr<ClassDecl> cd,
    const Ptr<Decl> member
)
{
    auto propOverride = DynamicCast<PropDecl*>(member.get());
    if (!propOverride) {
        return;
    }

    // Checking overriding/redefinition of deprecated property
    auto getterOverride = GetUsableGetterForProperty(*propOverride);
    auto setterOverride = propOverride->isVar ? GetUsableSetterForProperty(*propOverride) : nullptr;
    for (auto& inheritedType : cd->inheritedTypes) {
        auto inheritedDecl = Ty::GetDeclPtrOfTy(inheritedType->ty);
        if (!inheritedDecl) {
            continue;
        }

        for (auto& memberDecl : inheritedDecl->GetMemberDecls()) {
            if (auto basePropDecl = As<ASTKind::PROP_DECL>(memberDecl); basePropDecl) {
                if (basePropDecl->TestAttr(Attribute::GENERIC)) {
                    return;
                }
                auto propGetter = GetUsableGetterForProperty(*basePropDecl);
                if (propGetter && getterOverride && Is<FuncTy*>(getterOverride->ty) && Is<FuncTy*>(propGetter->ty) &&
                    typeManager.IsFuncDeclSubType(*getterOverride, *propGetter)) {
                    CheckOverridingOrRedefiningOfDeprecated(
                        basePropDecl,
                        propOverride,
                        "property");
                }
                auto propSetter = basePropDecl->isVar ? GetUsableSetterForProperty(*basePropDecl) : nullptr;
                if (propSetter && setterOverride &&
                    Is<FuncTy*>(setterOverride->ty) &&
                    Is<FuncTy*>(propSetter->ty) &&
                    typeManager.IsFuncDeclSubType(*setterOverride, *propSetter)
                ) {
                    CheckOverridingOrRedefiningOfDeprecated(
                        propSetter,
                        setterOverride,
                        "setter");
                }
            }
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckOverridingOrRedefinitionOfDeprecated(
    const Ptr<ClassDecl> cd
)
{
    for (auto& member : cd->body->decls) {
        CheckOverridingOrRedefinitionOfDeprecatedFunction(cd, member);
        CheckOverridingOrRedefinitionOfDeprecatedProperty(cd, member);
    }
}

void TypeChecker::TypeCheckerImpl::CheckUsageOfDeprecatedSetter(
    const Ptr<Node> usage
)
{
    auto ae = DynamicCast<AssignExpr*>(usage);
    if (!ae || !ae->leftValue) {
        return;
    }

    if (auto leftPartTarget = ae->leftValue->GetTarget(); leftPartTarget) {
        if (leftPartTarget->astKind != ASTKind::PROP_DECL) {
            return;
        }

        auto pd = StaticCast<PropDecl*>(leftPartTarget);
        if (!pd || !pd->isVar) {
            return;
        }

        auto setter = GetUsableSetterForProperty(*pd);
        if (setter && setter->HasAnno(AnnotationKind::DEPRECATED)) {
            DiagnoseDeprecatedUsage(
                ae->leftValue,
                setter,
                pd->identifier.GetRawText()
            );
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckDeprecationLevelOnInheritors(
    const Ptr<ClassLikeDecl> classLike
)
{
    auto isParentStrict = IsDeprecatedStrict(classLike);
    OrderedDeclSet orderedSubDecl;
    orderedSubDecl.insert(classLike->subDecls.begin(), classLike->subDecls.end());
    for (auto sub : orderedSubDecl) {
        if (sub->HasAnno(AnnotationKind::DEPRECATED)) {
            if (isParentStrict && !IsDeprecatedStrict(sub)) {
                auto diagnoseKind = DiagKindRefactor::sema_deprecation_weakening;

                diag.DiagnoseRefactor(diagnoseKind, *sub);
            }
        } else {
            auto diagnoseKind = isParentStrict
                ? DiagKindRefactor::sema_deprecation_override_error
                : DiagKindRefactor::sema_deprecation_override_warning;

            diag.DiagnoseRefactor(
                diagnoseKind,
                *sub,
                DeclKindToString(*sub),
                sub->identifier.GetRawText()
            );
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckUsageOfDeprecatedWithTarget(
    const Ptr<Node> usage,
    const Ptr<Decl> target
)
{
    if (target->HasAnno(AnnotationKind::DEPRECATED)) {
        if (target->astKind == ASTKind::FUNC_PARAM) {
            // They are checked in CheckUsageOfDeprecatedParameters
            // Here skipping due to usage of function paremeters
            // inside block must not be diagnosed
            return;
        }
        if (target->astKind == ASTKind::VAR_DECL && usage->TestAnyAttr(Attribute::COMPILER_ADD)) {
            // Skip compiler generated assignment of member
            // desugared from let/var param of function
            return;
        }

        DiagnoseDeprecatedUsage(usage, target);
        return;
    }

    if (IsClassOrEnumConstructor(*target)) {
        if (usage->astKind == ASTKind::MEMBER_ACCESS) {
            // qualified enum constructor `En.EnumConstr`
            // e.g. skip reporting `EnumConstr` because diasgnostic for `En` is enough
            return;
        }
        if (!usage->TestAnyAttr(Attribute::COMPILER_ADD)) {
            CheckUsageOfDeprecatedNominative(usage, target);
        }
    }

    if (auto alias = As<ASTKind::TYPE_ALIAS_DECL>(target); alias) {
        auto aliased = Ty::GetDeclOfTy(alias->type->ty);
        if (!aliased) {
            return;
        }

        if (aliased->HasAnno(AnnotationKind::DEPRECATED)) {
            DiagnoseDeprecatedUsage(usage, aliased);
        }
    }
}

void TypeChecker::TypeCheckerImpl::CheckUsageOfDeprecated(
    Node& node
)
{
    auto enterVisitor = [this](const Ptr<Node> usage) {
        if (usage->astKind == ASTKind::ENUM_PATTERN) {
            return VisitAction::SKIP_CHILDREN;
        }
        if (usage->IsDecl()) {
            auto decl = StaticCast<Decl>(usage);
            if (decl && decl->HasAnno(AnnotationKind::DEPRECATED)) {
                if (IsDeprecatedStrict(decl) && !strictDeprecatedContext) {
                    strictDeprecatedContext = usage;
                } else if (!deprecatedContext) {
                    deprecatedContext = usage;
                }
            }
            if (auto cd = As<ASTKind::CLASS_DECL>(usage); cd && cd->body) {
                CheckOverridingOrRedefinitionOfDeprecated(cd);
            }
            if (decl && decl->HasAnno(AnnotationKind::DEPRECATED)) {
                if (auto cl = DynamicCast<ClassLikeDecl*>(decl); cl) {
                    CheckDeprecationLevelOnInheritors(cl);
                }
            }
            return VisitAction::WALK_CHILDREN;
        }

        if (usage->astKind == ASTKind::CALL_EXPR) {
            CheckUsageOfDeprecatedParameters(usage);
        } else if (usage->astKind == ASTKind::ASSIGN_EXPR) {
            CheckUsageOfDeprecatedSetter(usage);
        }

        if (auto target = usage->GetTarget(); target && target != usage) {
            CheckUsageOfDeprecatedWithTarget(usage, target);
        }

        return VisitAction::WALK_CHILDREN;
    };

    auto exitVisitor = [this](const Ptr<Node> usage) {
        if (deprecatedContext == usage) {
            deprecatedContext = nullptr;
        }

        if (strictDeprecatedContext == usage) {
            strictDeprecatedContext = nullptr;
        }

        return VisitAction::KEEP_DECISION;
    };

    Walker(&node, enterVisitor, exitVisitor).Walk();
}
