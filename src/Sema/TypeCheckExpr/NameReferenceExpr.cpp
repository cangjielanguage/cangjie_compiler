// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "DiagSuppressor.h"
#include "Diags.h"
#include "EnumSugarChecker.h"
#include "EnumSugarTargetsFinder.h"
#include "LocalTypeArgumentSynthesis.h"
#include "TypeCheckUtil.h"
#include "ExtraScopes.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/Modules/ModulesUtils.h"
#include "cangjie/Macro/TestEntryConstructor.h"

using namespace Cangjie;
using namespace Sema;
using namespace TypeCheckUtil;
using namespace Meta;

namespace {
bool IsEnumNeedSynthesis(Expr& expr, const Decl& target)
{
    if (!target.TestAttr(AST::Attribute::ENUM_CONSTRUCTOR) || !target.GetGeneric()) {
        return false;
    }
    Ptr<Expr> ref = &expr;
    if (auto ma = DynamicCast<MemberAccess*>(&expr); ma) {
        ref = ma->baseExpr.get();
    }
    return ref && NeedFurtherInstantiation(ref->GetTypeArgs());
}

bool CheckInferrableEnumReference(DiagnosticEngine& diag, TypeManager& tyMgr, Expr& expr, Ty& target)
{
    auto res = EnumSugarTargetsFinder::RefineTargetTy(tyMgr, &target, expr.GetTarget());
    if (!res.has_value()) {
        diag.Diagnose(expr, DiagKind::sema_generic_type_without_type_argument);
        return false;
    }
    auto targetTy = *res;
    if (target.IsInterface()) {
        auto candiTys = Promotion(tyMgr).Downgrade(*targetTy, target);
        if (candiTys.empty()) {
            DiagUnableToInferExpr(diag, expr);
            return false;
        }
        targetTy = *candiTys.begin();
    }
    expr.ty = targetTy;
    Ptr<Expr> ref = &expr;
    if (auto ma = DynamicCast<MemberAccess*>(&expr); ma) {
        ref = ma->baseExpr.get();
        ref->ty = targetTy; // Type of enum base is same with the expression's ty.
    }
    if (auto reference = DynamicCast<AST::NameReferenceExpr*>(ref)) {
        reference->instTys.clear();
        for (auto it : targetTy->typeArgs) {
            (void)reference->instTys.emplace_back(it);
        }
    }
    return true;
}

bool CheckNonFunctionReference(DiagnosticEngine& diag, TypeManager& tyMgr, Ty& target, Expr& refNode)
{
    auto refTarget = refNode.GetTarget();
    bool checkEnumCtor = refTarget && refTarget->TestAttr(AST::Attribute::ENUM_CONSTRUCTOR);
    if (checkEnumCtor && IsEnumNeedSynthesis(refNode, *refTarget)) {
        // Check and update type for enum target without typeArgument.
        return CheckInferrableEnumReference(diag, tyMgr, refNode, target);
    }

    bool isWellTyped = tyMgr.IsSubtype(refNode.ty, &target);
    if (!isWellTyped) {
        DiagMismatchedTypes(diag, refNode, target);
        refNode.ty = TypeManager::GetInvalidTy();
    }
    return isWellTyped;
}

ASTKind GetTargetsSameASTKind(const std::vector<Ptr<Decl>>& targets)
{
    ASTKind ret = ASTKind::INVALID_DECL;
    for (auto& it : targets) {
        CJC_NULLPTR_CHECK(it);
        if (ret == ASTKind::INVALID_DECL && it->astKind != ASTKind::INVALID_DECL) {
            ret = it->astKind;
        }
        if (ret == it->astKind) {
            continue;
        } else {
            return ASTKind::INVALID_DECL;
        }
    }
    return ret;
}

ASTKind GetTargetsSameASTKind(std::unordered_map<Ptr<Ty>, std::vector<Ptr<Decl>>>& allTargets)
{
    ASTKind ret = ASTKind::INVALID_DECL;
    for (auto& it : allTargets) {
        auto kind = GetTargetsSameASTKind(it.second);
        if (kind == ASTKind::INVALID_DECL || (ret != ASTKind::INVALID_DECL && ret != kind)) {
            return ASTKind::INVALID_DECL;
        }
        ret = kind;
    }
    return ret;
}

std::unordered_map<Ptr<Decl>, std::unordered_set<Ptr<Ty>>> GetUpperBoundTargetsWithGivenKind(
    const std::unordered_map<Ptr<Ty>, std::vector<Ptr<Decl>>>& allTargets, const std::unordered_set<ASTKind>& kinds)
{
    std::unordered_map<Ptr<Decl>, std::unordered_set<Ptr<Ty>>> results;
    for (auto [ty, targets] : allTargets) {
        for (auto target : targets) {
            if (Utils::In(target->astKind, kinds)) {
                results[target].emplace(ty);
            }
        }
    }
    return results;
}

bool IsCloserToImpl(const Decl& src, const Decl& target)
{
    // If the current decl is not abstract and previous is abstract, replace previous one.
    bool updateAbstract = src.TestAttr(Attribute::ABSTRACT) && !target.TestAttr(Attribute::ABSTRACT);
    // If the current decl is not in interface, replace previous one.
    bool updateNonInterface = !src.TestAttr(Attribute::ABSTRACT) && !target.TestAttr(Attribute::ABSTRACT) &&
        target.outerDecl && target.outerDecl->astKind != ASTKind::INTERFACE_DECL;
    return updateAbstract || updateNonInterface;
}

std::map<Ptr<Ty>, Ptr<Decl>>::const_iterator FoundSameSignatureMember(
    TypeManager& tyMgr, const Decl& decl, FuncTy& funcTy, std::map<Ptr<Ty>, Ptr<Decl>>& methodSigs)
{
    auto curTy = &funcTy;
    std::map<Ptr<Ty>, Ptr<Decl>>::const_iterator found = methodSigs.find(curTy);
    if (found != methodSigs.cend()) {
        return found;
    }
    if (!decl.TestAttr(Attribute::GENERIC)) {
        return methodSigs.cend();
    }
    std::unordered_set<Ptr<Decl>> decls;
    for (auto method : std::as_const(methodSigs)) {
        if (method.second->TestAttr(Attribute::GENERIC)) {
            decls.emplace(method.second);
        }
    }
    for (auto it : decls) {
        // Substitute generic types for generic function.
        TypeSubst typeMapping = tyMgr.GenerateGenericMappingFromGeneric(decl, *it);
        // Checking whether substituted function signature is existed in map.
        curTy = StaticCast<FuncTy*>(tyMgr.GetInstantiatedTy(&funcTy, typeMapping));
        found = methodSigs.find(curTy);
        if (found != methodSigs.cend()) {
            return found;
        }
    }
    return methodSigs.cend();
}

FuncSig2Decl::const_iterator FoundSameSignatureMember(
    TypeManager& tyMgr, const Decl& decl, FuncTy& funcTy, FuncSig2Decl& methodSigs)
{
    auto keyPair = std::make_pair(decl.identifier, funcTy.paramTys);
    FuncSig2Decl::const_iterator found = methodSigs.find(keyPair);
    if (found != methodSigs.cend()) {
        return found;
    }
    if (!decl.TestAttr(Attribute::GENERIC)) {
        return methodSigs.cend();
    }
    std::unordered_set<Ptr<Decl>> decls;
    for (auto method : std::as_const(methodSigs)) {
        if (method.second->TestAttr(Attribute::GENERIC)) {
            decls.emplace(method.second);
        }
    }
    for (auto it : decls) {
        // Substitute generic types for generic function.
        TypeSubst typeMapping = tyMgr.GenerateGenericMappingFromGeneric(decl, *it);
        // Checking whether substituted function signature is existed in map.
        auto instTy = StaticCast<FuncTy*>(tyMgr.GetInstantiatedTy(&funcTy, typeMapping));
        keyPair = std::make_pair(decl.identifier, instTy->paramTys);
        found = methodSigs.find(keyPair);
        if (found != methodSigs.cend()) {
            return found;
        }
    }
    return methodSigs.cend();
}

std::vector<Ptr<Decl>> MergeFuncTargetsInUpperBounds(TypeManager& tyMgr, const MemberAccess& ma)
{
    // We need to check upperbound members in a fixed order.
    OrderedDeclSet upperDecls;
    for (auto it : ma.foundUpperBoundMap) {
        upperDecls.emplace(it.first);
    }
    // Functions found in upperbounds which have same signature must have only one valid implementation.
    // So, classify functions by function signature.
    std::unordered_set<Ptr<Decl>> targets;
    FuncSig2Decl methodSigs;
    for (auto decl : upperDecls) {
        CJC_NULLPTR_CHECK(decl);
        if (decl->astKind != ASTKind::FUNC_DECL) {
            continue;
        }
        MultiTypeSubst mts;
        tyMgr.GenerateTypeMappingForUpperBounds(mts, ma, *decl);
        auto tys = tyMgr.GetInstantiatedTys(decl->ty, mts);
        for (auto ty : tys) {
            auto funcTy = DynamicCast<FuncTy*>(ty);
            if (!Ty::IsTyCorrect(funcTy)) {
                continue;
            }
            const auto found = FoundSameSignatureMember(tyMgr, *decl, *funcTy, methodSigs);
            if (found == methodSigs.cend()) {
                methodSigs.emplace(std::make_pair(decl->identifier, funcTy->paramTys), RawStaticCast<FuncDecl*>(decl));
            } else if (IsCloserToImpl(*found->second, *decl)) {
                // If the decl is generic, the paramsTys in the map key should also be updated,
                // so, just erase found result and emplace new result here.
                methodSigs.erase(found);
                methodSigs.emplace(std::make_pair(decl->identifier, funcTy->paramTys), RawStaticCast<FuncDecl*>(decl));
            }
        }
    }
    for (auto method : std::as_const(methodSigs)) {
        targets.emplace(method.second);
    }
    return Utils::SetToVec<Ptr<Decl>>(targets);
}

std::vector<Ptr<Decl>> MergeFuncTargetsInSum(TypeManager& tyMgr, const MemberAccess& ma)
{
    // We need to check upperbound members in a fixed order.
    OrderedDeclSet upperDecls;
    for (auto it : ma.foundUpperBoundMap) {
        upperDecls.emplace(it.first);
    }
    // Functions found in upperbounds which have same signature must have only one valid implementation.
    // So, classify functions by function signature.
    std::unordered_set<Ptr<Decl>> targets;
    std::map<Ptr<Ty>, Ptr<Decl>> methodSigs;
    for (auto decl : upperDecls) {
        CJC_NULLPTR_CHECK(decl);
        if (decl->astKind != ASTKind::FUNC_DECL) {
            continue;
        }
        MultiTypeSubst mts;
        tyMgr.GenerateTypeMappingForUpperBounds(mts, ma, *decl);
        auto tys = tyMgr.GetInstantiatedTys(decl->ty, mts);
        for (auto ty : tys) {
            auto funcTy = DynamicCast<FuncTy*>(ty);
            if (!Ty::IsTyCorrect(funcTy)) {
                continue;
            }
            const auto found = FoundSameSignatureMember(tyMgr, *decl, *funcTy, methodSigs);
            if (found == methodSigs.cend()) {
                methodSigs.emplace(funcTy, RawStaticCast<FuncDecl*>(decl));
            } else if (IsCloserToImpl(*found->second, *decl)) {
                // If the decl is generic, the paramsTys in the map key should also be updated,
                // so, just erase found result and emplace new result here.
                methodSigs.erase(found);
                methodSigs.emplace(funcTy, RawStaticCast<FuncDecl*>(decl));
            }
        }
    }
    for (auto method : std::as_const(methodSigs)) {
        targets.emplace(method.second);
    }
    return Utils::SetToVec<Ptr<Decl>>(targets);
}

void DiagForGenericParamMemberNotFound(DiagnosticEngine& diag, const MemberAccess& ma, const GenericParamDecl& gpd)
{
    if (Ty::IsTyCorrect(gpd.ty) && gpd.ty->IsGeneric() && RawStaticCast<GenericsTy*>(gpd.ty)->isUpperBoundLegal) {
        diag.Diagnose(*ma.baseExpr, DiagKind::sema_invalid_field_expose_access, ma.field.Val(),
            "exposed generic parameter", gpd.identifier.Val());
    }
}
} // namespace

void TypeChecker::TypeCheckerImpl::DiagMemberAccessNotFound(const MemberAccess& ma)
{
    if (IsFieldOperator(ma.field)) {
        return; // Do not report error for operator overload access.
    }
    if (ci->invocation.globalOptions.compileTestsOnly &&
        TestEntryConstructor::IsTestRegistrationFunction(ma.target)
    ) {
        /*
         * Allow loading not accessible test registration functions,
         * especially from other packages, to be able to execute tests from other packages
         */
        return;
    }
    Ptr<const Expr> baseExpr = ma.baseExpr.get();
    CJC_NULLPTR_CHECK(baseExpr);
    if (!Ty::IsTyCorrect(baseExpr->ty)) {
        return; // Do not report error for baseExpr with invlaid type.
    }
    auto getMemberRange = [&ma]() { return ma.field.ZeroPos() ? MakeRange(ma.begin, ma.end) : MakeRange(ma.field); };
    if (ma.isExposedAccess) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_not_found_from_generic_upper_bounds, ma, getMemberRange(),
            ma.field.Val(), baseExpr->ty->name);
    } else if (baseExpr->ty->IsNominal()) {
        std::string kind = baseExpr->ty->Ty::String();
        kind[0] = static_cast<char>(std::tolower(kind[0]));
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_not_member_of, ma, getMemberRange(), ma.field, kind,
            baseExpr->ty->name + baseExpr->ty->PrintTypeArgs());
        RecommendImportForMemberAccess(typeManager, importManager, ma, &builder);
    } else if (ma.ShouldDiagnose(true)) {
        auto builder =
            diag.DiagnoseRefactor(DiagKindRefactor::sema_undeclared_identifier, ma, getMemberRange(), ma.field);
        RecommendImportForMemberAccess(typeManager, importManager, ma, &builder);
    }
}

bool TypeChecker::TypeCheckerImpl::ChkRefExpr(ASTContext& ctx, Ty& target, NameReferenceExpr& refNode)
{
    (void)Synthesize(ctx, &refNode);
    auto targets = GetFuncTargets(refNode);
    // None function target check.
    if (targets.empty()) {
        return CheckNonFunctionReference(diag, typeManager, target, refNode);
    }
    // Overloading check.
    uint32_t matched = 0;
    CJC_ASSERT(refNode.IsReferenceExpr());
    RemoveDuplicateElements(targets);
    auto [genericIgnored, candidates] = CollectValidFuncTys(ctx, targets, refNode, DynamicCast<FuncTy*>(&target));
    TypeSubst resultMapping;
    Ptr<FuncDecl> matchedFd = nullptr;
    Ptr<Ty> validCandidateTy = nullptr;
    for (auto [fd, fdTy, mapping] : candidates) {
        if (!typeManager.IsSubtype(fdTy, &target)) {
            if (!validCandidateTy && Ty::IsTyCorrect(fdTy) && !StaticCast<FuncTy>(fdTy)->retTy->IsQuest()) {
                validCandidateTy = fdTy;
            }
            continue;
        }
        if (matched != 0) {
            matched++;
            break;
        }
        matched++;
        ReplaceTarget(&refNode, fd);
        std::tie(matchedFd, refNode.ty, resultMapping) = {fd, fdTy, mapping};
    }
    refNode.ty = TypeManager::GetNonNullTy(refNode.ty);
    if (refNode.ty->IsQuest()) {
        refNode.ty = TypeManager::GetInvalidTy();
    }
    if (matched > 1) {
        diag.Diagnose(refNode, DiagKind::sema_ambiguous_func_ref, targets[0]->identifier.Val());
    } else if (matched == 0) {
        if (Ty::IsTyCorrect(refNode.ty) && refNode.ty->HasQuestTy()) {
            CJC_ASSERT(refNode.GetTarget());
            DiagUnableToInferReturnType(diag, *targets[0], refNode);
        } else if (genericIgnored) {
            diag.Diagnose(refNode, DiagKind::sema_generic_type_without_type_argument);
        } else if (validCandidateTy) {
            DiagMismatchedTypesWithFoundTy(diag, refNode, target, *validCandidateTy);
        } else {
            diag.Diagnose(refNode, DiagKind::sema_no_match_function_declaration_for_ref, targets[0]->identifier.Val());
        }
        if (targets.size() > 1) {
            ReplaceTarget(&refNode, nullptr); // Only clear target when there are more than one candidate.
        }
    } else {
        if (IsGenericUpperBoundCall(refNode, *matchedFd)) {
            // Previous check guarantees 'refNode' is 'RefExpr' or 'MemberAccess' which must be 'NameReferenceExpr'.
            auto& ref = static_cast<NameReferenceExpr&>(refNode);
            ref.matchedParentTy = typeManager.GetInstantiatedTy(matchedFd->outerDecl->ty, resultMapping);
        }
        InstantiateReferenceType(ctx, refNode, resultMapping);
    }
    return matched != 0;
}

bool TypeChecker::TypeCheckerImpl::SynTargetOnUsed(ASTContext& ctx, const NameReferenceExpr& nre, Decl& target)
{
    // Type decls are no need to be checked again recursively, because the ty is already set at PreCheck stage.
    if (NeedSynOnUsed(target)) {
        Ptr<Decl> declToSyn = &target;
        if (auto vd = DynamicCast<VarDecl*>(&target)) {
            auto& vda = ctx.GetOuterVarDeclAbstract(*vd);
            declToSyn = vda.TestAttr(Attribute::GLOBAL) ? &vda : vd;
        }
        auto targetTy = Synthesize(ctx, declToSyn);
        if (auto fd = DynamicCast<FuncDecl*>(declToSyn); fd && targetTy->HasQuestTy()) {
            DiagUnableToInferReturnType(diag, *fd, nre);
            return false;
        }
    }
    return true;
}

void TypeChecker::TypeCheckerImpl::InferRefExpr(ASTContext& ctx, RefExpr& re)
{
    if (re.ref.target && Ty::IsTyCorrect(re.ty) && !re.ref.target->ty->IsPlaceholder()) {
        return; // If the target is already existed and type is valid, we can exit early.
    }
    bool isWellTyped = true;
    for (auto& type : re.typeArguments) {
        isWellTyped = Ty::IsTyCorrect(Synthesize(ctx, type.get())) && isWellTyped;
    }
    if (re.isThis || re.isSuper) {
        CheckThisOrSuper(ctx, re);
        return;
    }
    bool isCustomAnnotation = false;
    if (auto ce = DynamicCast<CallExpr*>(re.callOrPattern); ce && ce->callKind == CallKind::CALL_ANNOTATION) {
        isCustomAnnotation = true;
    }
    auto targets = Lookup(ctx, re.ref.identifier, isCustomAnnotation ? TOPLEVEL_SCOPE_NAME : re.scopeName, re,
        re.TestAttr(AST::Attribute::LEFT_VALUE));
    if (!isCustomAnnotation && !re.TestAttr(Attribute::MACRO_INVOKE_BODY) &&
        std::all_of(targets.cbegin(), targets.cend(), [](auto target) {
            CJC_NULLPTR_CHECK(target);
            return target->TestAttr(Attribute::MACRO_FUNC);
        })) {
        auto enumSugarChecker = std::make_unique<EnumSugarChecker>(*this, ctx, re);
        auto res = enumSugarChecker->Resolve();
        if (res.first) {
            if (res.second.empty()) {
                re.ty = TypeManager::GetInvalidTy();
            }
            return;
        }
        targets = res.second;
    }

    if (!FilterAndCheckTargetsOfRef(ctx, re, targets) || !isWellTyped) {
        re.ty = TypeManager::GetInvalidTy();
        return;
    }
    // 'targets' must not empty.
    auto decl = GetAccessibleDecl(ctx, re, targets);
    if (!decl) {
        decl = targets.front();
    }
    ModifyTargetOfRef(re, decl, targets);
    // Legality of using refExpr will be checked after typecheck in 'CheckLegalityOfReference'.
    CJC_ASSERT(re.ref.target); // 're.ref.target' should be set in 'ModifyTargetOfRef'.

    if (auto target = re.ref.target; target->IsBuiltIn()) {
        if (auto cfunc = StaticCast<BuiltInDecl>(target); cfunc->type == BuiltInType::CFUNC) {
            return InferCFuncExpr(ctx, re);
        }
    }
    // If refExpr is base of call, decide real target & type in function call checking.
    // If refExpr is overloaded function reference with target type, decide real target & type in 'ChkRefExpr'.
    if (IsAllFuncDecl(targets) && targets.size() > 1 && (re.callOrPattern || ctx.HasTargetTy(&re))) {
        re.ty = TypeManager::GetQuestTy();
        return;
    }
    if (!SynTargetOnUsed(ctx, re, *re.ref.target)) {
        re.ty = TypeManager::GetInvalidTy();
        return;
    }
    re.ty = SubstituteTypeAliasInTy(*re.ref.target->ty);
    if (!decl->IsFunc() || re.isAlone) {
        // Only check non-function or non-call target. Functions will be check after function overload resolution.
        InstantiateReferenceType(ctx, re);
    }
    if (Ty::IsInitialTy(re.ty)) {
        re.ty = TypeManager::GetInvalidTy();
    }
}

void TypeChecker::TypeCheckerImpl::InferCFuncExpr(ASTContext& ctx, RefExpr& re)
{
    // check type arguments
    // infer type arguments of CFunc is currently disallowed
    if (re.typeArguments.size() != 1) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_generic_argument_no_match, re, MakeRange(re.GetBegin(), re.GetEnd()));
        re.ty = TypeManager::GetInvalidTy();
        return;
    }
    auto funcType = DynamicCast<FuncType>(&*re.typeArguments[0]);
    if (!funcType) {
        diag.Diagnose(re.typeArguments[0]->GetBegin(), re.typeArguments[0]->GetEnd(), DiagKind::sema_cfunc_type);
        re.ty = TypeManager::GetInvalidTy();
        return;
    }

    // synthesise type
    std::vector<Ptr<Ty>> paramTys;
    for (size_t i{0}; i < funcType->paramTypes.size(); ++i) {
        auto& param = funcType->paramTypes[i];
        paramTys.push_back(param->ty = GetTyFromASTType(ctx, &*param));
    }
    Ptr<Ty> retTy = funcType->retType->ty = GetTyFromASTType(ctx, funcType->retType.get());
    auto resTy = typeManager.GetFunctionTy(std::move(paramTys), retTy, {.isC = true});
    re.ty = resTy;
}

void TypeChecker::TypeCheckerImpl::TryInitializeBaseSum(ASTContext& ctx, MemberAccess& ma)
{
    if (!ma.baseExpr->ty->IsPlaceholder()) {
        return;
    }
    auto tv = RawStaticCast<GenericsTy*>(ma.baseExpr->ty);
    auto& sum = typeManager.constraints[tv].sum;
    if (sum.size() != 1 || !(*sum.begin())->IsAny()) {
        return;
    }
    MemSig sig;
    if (auto ce = DynamicCast<CallExpr*>(ma.callOrPattern)) {
        sig = MemSig{ma.field, false, ce->args.size(), ma.typeArguments.size()};
    } else {
        sig = MemSig{ma.field, true};
    }
    TryEnforceCandidate(*tv, ctx.Mem2Decls(sig), typeManager);
}

void TypeChecker::TypeCheckerImpl::InferMemberAccess(ASTContext& ctx, MemberAccess& ma)
{
    if (ma.target && Ty::IsTyCorrect(ma.ty)) {
        return; // If the target is already existed and type is valid, we can exit early.
    }
    bool isWellTyped = true;
    for (auto& type : ma.typeArguments) {
        isWellTyped = Ty::IsTyCorrect(Synthesize(ctx, type.get())) && isWellTyped;
    }
    Ptr<Expr> baseExpr = ma.baseExpr.get();
    if (!baseExpr || !isWellTyped) {
        ma.ty = TypeManager::GetInvalidTy();
        return;
    }
    SetIsNotAlone(*ma.baseExpr);
    auto targetOfBase = GetBaseDeclInMemberAccess(ctx, ma);
    TryInitializeBaseSum(ctx, ma);
    // baseExpr is Synthesized by GetBaseDeclInMemberAccess. Refactor later.
    // Whether current is access member by type alias of primitive types.
    bool isPrimitiveTypeAlias = targetOfBase && targetOfBase->astKind == ASTKind::TYPE_ALIAS_DECL && baseExpr->ty &&
        baseExpr->ty->IsPrimitive();
    // Whether current is access member by real/generic type or package name.
    bool isStaticAccessByName =
        targetOfBase && !IsThisOrSuper(*baseExpr) && (targetOfBase->IsTypeDecl() || Is<PackageDecl>(targetOfBase));
    bool isBuiltInStaticAccess = targetOfBase && targetOfBase->IsBuiltIn();
    bool isPartialPackagePath = !targetOfBase && Ty::IsInitialTy(ma.baseExpr->ty) && (ma.isAlone || ma.callOrPattern);
    if (isBuiltInStaticAccess) {
        InferBuiltInStaticAccess(ctx, ma, *RawStaticCast<BuiltInDecl*>(targetOfBase));
    } else if (baseExpr->astKind == ASTKind::PRIMITIVE_TYPE_EXPR || isPrimitiveTypeAlias) {
        CheckExtendField(ctx, ma);
    } else if (isStaticAccessByName) {
        CJC_ASSERT(targetOfBase);
        InferStaticAccess(ctx, ma, *targetOfBase);
    } else if (isPartialPackagePath) {
        auto range = ma.field.ZeroPos() ? MakeRange(ma.begin, ma.end) : MakeRange(ma.field);
        (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_undeclared_identifier, ma, range, ma.field);
    } else {
        InferInstanceAccess(ctx, ma);
    }
    if (!ma.target) {
        return;
    }
    // Legality of using memberAccess will be checked after typecheck in 'CheckLegalityOfReference'.
    // If memberAccess is base of call, decide real target & type in function call checking.
    // If memberAccess is overloaded function reference with target type, decide real target & type in 'ChkRefExpr'.
    bool checkedLater =
        IsAllFuncDecl(ma.targets) && ma.targets.size() > 1 && (ma.callOrPattern || ctx.HasTargetTy(&ma));
    if (checkedLater) {
        ma.ty = TypeManager::GetQuestTy();
        return;
    }
    if (!SynTargetOnUsed(ctx, ma, *ma.target)) {
        ma.ty = TypeManager::GetInvalidTy();
        return;
    }
    ma.ty = SubstituteTypeAliasInTy(*ma.target->ty);
    // Only instantiate ty for non-function or non-call. Function's will be done after overload resolution.
    if (!ma.target->IsFunc() || ma.isAlone) {
        InstantiateReferenceType(ctx, ma);
    }
}

Ptr<Decl> TypeChecker::TypeCheckerImpl::GetBaseDeclInMemberAccess(ASTContext& ctx, const MemberAccess& ma)
{
    if (ma.baseExpr == nullptr) {
        return nullptr;
    }
    Ptr<Expr> baseExpr = ma.baseExpr.get();
    // Synthesize baseExpr's ty, baseExpr maybe another MemberAccess like a.b or RefExpr a.
    // Could be from desugaring a binary expr, need to avoid exponential repetitive check with cache
    SynthesizeWithNegCache(ctx, baseExpr);
    return GetRealTarget(baseExpr, baseExpr->GetTarget());
}

void TypeChecker::TypeCheckerImpl::InferBuiltInStaticAccess(
    const ASTContext& ctx, MemberAccess& ma, const BuiltInDecl& bid)
{
    if (bid.IsType(BuiltInType::ARRAY)) {
        InferArrayStaticAccess(ctx, ma);
    } else if (bid.IsType(BuiltInType::POINTER) || bid.IsType(BuiltInType::CSTRING)) {
        CheckExtendField(ctx, ma);
    }
    // Leave for other builtin composite type.
}

void TypeChecker::TypeCheckerImpl::InferArrayStaticAccess(const ASTContext& ctx, MemberAccess& ma)
{
    CJC_ASSERT(ma.baseExpr);
    CJC_NULLPTR_CHECK(ma.curFile);
    auto typeArgs = ma.baseExpr->GetTypeArgs();
    if (typeArgs.empty()) {
        diag.Diagnose(ma, DiagKind::sema_generic_type_without_type_argument);
        ma.ty = TypeManager::GetInvalidTy();
        return;
    }
    ma.baseExpr->ty = typeManager.GetArrayTy(typeArgs[0]->ty, 1);
    auto targets = ExtendFieldLookup(ctx, *ma.curFile, ma.baseExpr->ty, ma.field);
    if (!FilterAndCheckTargetsOfNameAccess(ctx, ma, targets)) {
        return;
    }
    auto target = GetAccessibleDecl(ctx, ma, targets);
    ReplaceTarget(&ma, target ? target : targets[0]);
    AddFuncTargetsForMemberAccess(ma, targets);
}

void TypeChecker::TypeCheckerImpl::InferStaticAccess(const ASTContext& ctx, MemberAccess& ma, Decl& targetOfBaseExpr)
{
    ma.ty = TypeManager::GetInvalidTy(); // Ty will be set to valid if non-error happens.
    // Caller guarantees current is access member by real/generic type or package name.
    std::vector<Ptr<Decl>> targets;
    Ptr<Expr> baseExpr = ma.baseExpr.get();
    CJC_ASSERT(baseExpr);
    // Case for access member by real type.
    if (targetOfBaseExpr.astKind != ASTKind::GENERIC_PARAM_DECL) {
        // In this case, targetOfBaseExpr is ClassDecl, InterfaceDecl, StructDecl, EnumDecl or PackageDecl.
        CJC_NULLPTR_CHECK(ma.curFile);
        targets = FieldLookup(ctx, &targetOfBaseExpr, ma.field,
            {baseExpr->ty, ma.curFile, true, true, ma.TestAttr(AST::Attribute::LEFT_VALUE)});
        // remove macro decl when it is not a @ call
        bool hasRemovedMacros{!targets.empty()};
        bool isCustomAnnotation = false;
        if (auto ce = DynamicCast<CallExpr*>(ma.callOrPattern); ce && ce->callKind == CallKind::CALL_ANNOTATION) {
            isCustomAnnotation = true;
        }
        if (!isCustomAnnotation && Is<PackageDecl>(targetOfBaseExpr) &&
            !ma.TestAttr(Attribute::MACRO_INVOKE_BODY)) {
            targets.erase(std::remove_if(targets.begin(), targets.end(), [](auto target) {
                return target->TestAttr(Attribute::MACRO_FUNC);
                }), targets.end());
            if (!targets.empty()) {
                hasRemovedMacros = false;
            }
        }
        if (targets.empty()) {
            if (targetOfBaseExpr.astKind == ASTKind::PACKAGE_DECL) {
                if (hasRemovedMacros) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_undeclared_identifier, MakeRange(ma.field.Begin(),
                        ma.field.End()), ma.field.Val());
                } else {
                    DiagPackageMemberNotFound(diag, importManager, ma, StaticCast<PackageDecl&>(targetOfBaseExpr));
                }
            } else {
                DiagMemberAccessNotFound(ma);
            }
            return;
        }
        if (!FilterAndCheckTargetsOfNameAccess(ctx, ma, targets)) {
            return;
        }
        // targets is guaranteed to be no empty after the check FilterAndCheckTargetsOfNameAccess is invoked.
        auto target = GetAccessibleDecl(ctx, ma, targets);
        ReplaceTarget(&ma, target ? target : targets[0]);
        AddFuncTargetsForMemberAccess(ma, targets);
    } else { // Case for access member by generic type.
        auto genericTy = DynamicCast<GenericsTy*>(baseExpr->ty);
        if (!genericTy) {
            return; // When the target of base is generic param and 'ty' is not 'GenericsTy', errors happened before.
        }
        // 'GetMemberAccessExposedTarget' will put found targets into 'ma.targets' and report error if not found.
        auto target = GetMemberAccessExposedTarget(ctx, ma, *genericTy, true);
        if (!target) {
            return;
        }
        ReplaceTarget(&ma, target);
        if (target && target->astKind == ASTKind::FUNC_DECL) {
            targets = std::vector<Ptr<Decl>>(ma.targets.begin(), ma.targets.end());
        }
        if (target && (target->astKind == ASTKind::VAR_DECL || target->astKind == ASTKind::PROP_DECL)) {
            targets = {target};
        }
    }
}

void TypeChecker::TypeCheckerImpl::InferInstanceAccess(const ASTContext& ctx, MemberAccess& ma)
{
    // In this case, targetOfBaseExpr is an object.
    Ptr<Expr> baseExpr = ma.baseExpr.get();
    if (!baseExpr || Ty::IsInitialTy(baseExpr->ty)) {
        return; // 'baseExpr' may be a part of package name such as 'pkga' in 'package pkga.pkgb.pkgc'.
    }
    ma.ty = TypeManager::GetInvalidTy(); // Ty will be set to valid if non-error happens.
    Ptr<Decl> target = GetObjMemberAccessTarget(ctx, ma, *baseExpr->ty);
    if (!target) {
        return;
    }
    ReplaceTarget(&ma, target);
}

Ptr<Decl> TypeChecker::TypeCheckerImpl::CheckUpperBoundTargetsCaseFuncCall(
    const ASTContext& ctx, MemberAccess& ma, const std::unordered_map<Ptr<Ty>, std::vector<Ptr<Decl>>>& allTargets)
{
    ma.targets.clear(); // Need clear before insertion.
    ma.foundUpperBoundMap = GetUpperBoundTargetsWithGivenKind(allTargets, {ASTKind::FUNC_DECL});
    std::vector<Ptr<Decl>> fdTargets = ma.baseExpr->ty->IsPlaceholder()
        ? MergeFuncTargetsInSum(typeManager, ma)
        : MergeFuncTargetsInUpperBounds(typeManager, ma);
    std::sort(fdTargets.begin(), fdTargets.end(), CompNodeByPos);
    auto target = ma.baseExpr->GetTarget();
    if (target && target->astKind == ASTKind::GENERIC_PARAM_DECL) {
        FilterAndCheckTargetsOfNameAccess(ctx, ma, fdTargets);
    } else {
        FilterAndGetTargetsOfObjAccess(ctx, ma, fdTargets);
    }
    // The final target will be determined in match function period.
    if (!fdTargets.empty()) {
        std::for_each(fdTargets.begin(), fdTargets.end(),
            [&ma](auto it) { ma.targets.emplace_back(RawStaticCast<FuncDecl*>(it)); });
        ma.matchedParentTy = *ma.foundUpperBoundMap[fdTargets[0]].begin();
        return fdTargets[0];
    } else {
        return nullptr;
    }
}

Ptr<Decl> TypeChecker::TypeCheckerImpl::CheckUpperBoundTargetsCaseOthers(const ASTContext& ctx,
    MemberAccess& ma, const std::unordered_map<Ptr<Ty>, std::vector<Ptr<Decl>>>& allTargets)
{
    ma.foundUpperBoundMap = GetUpperBoundTargetsWithGivenKind(allTargets, {ASTKind::VAR_DECL, ASTKind::PROP_DECL});
    std::vector<Ptr<Decl>> tempTargets;
    // We need to check upperbound members in a fixed order.
    OrderedDeclSet upperDecls;
    for (auto it : ma.foundUpperBoundMap) {
        upperDecls.emplace(it.first);
    }
    for (auto it : upperDecls) {
        // Decls found in upperbounds which have same type must have only one valid implementation.
        // So, classify decls by type.
        auto found = std::find_if(tempTargets.begin(), tempTargets.end(),
            [this, &it](auto decl) { return typeManager.IsTyEqual(decl->ty, it->ty); });
        if (found == tempTargets.end()) {
            tempTargets.emplace_back(it);
        } else if (IsCloserToImpl(*(*found), *it)) {
            *found = it;
        }
    }
    if (auto genTy = DynamicCast<GenericsTy*>(ma.baseExpr->ty); genTy && genTy->isPlaceholder) {
        FilterSumUpperbound(ctx, ma, *genTy, tempTargets, allTargets);
    }
    if (tempTargets.empty()) {
        diag.Diagnose(ma, ma.field.Begin(), DiagKind::sema_generic_no_member_match_in_upper_bounds);
        return nullptr;
    } else if (tempTargets.size() == 1 && ma.foundUpperBoundMap.find(tempTargets[0]) != ma.foundUpperBoundMap.end()) {
        // For non-function target, all types must be same, so just chose first one.
        ma.matchedParentTy = *ma.foundUpperBoundMap[tempTargets[0]].begin();
        return As<ASTKind::DECL>(tempTargets[0]);
    } else {
        OrderedDeclSet candidates(tempTargets.cbegin(), tempTargets.cend());
        DiagAmbiguousUpperBoundTargets(diag, ma, candidates);
        return nullptr;
    }
}

void TypeChecker::TypeCheckerImpl::FilterSumUpperbound(const ASTContext& ctx, AST::MemberAccess& ma,
    AST::GenericsTy& tv, std::vector<Ptr<AST::Decl>>& targets,
    const std::unordered_map<Ptr<Ty>, std::vector<Ptr<Decl>>>& allTargets)
{
    auto& m = ctx.targetTypeMap;
    auto tgtTy = m.count(&ma) > 0 ? m.at(&ma) : nullptr;
    if (tgtTy && tgtTy->HasQuestTy()) {
        tgtTy = nullptr;
    }
    if (tgtTy) {
        std::map<Ptr<Decl>, Ptr<Ty>> decl2Ub;
        for (auto& [ub, decls] : allTargets) {
            for (auto d : decls) {
                decl2Ub[d] = ub;
            }
        }
        std::vector<Ptr<AST::Decl>> filteredTargets;
        Ptr<Ty> validTy = nullptr;
        for (auto d : targets) {
            auto ub = decl2Ub[d];
            // ub should never be filtered out
            if (!d->outerDecl || typeManager.constraints[&tv].ubs.count(ub) > 0) {
                filteredTargets.push_back(d);
                continue; // only filter out ub and targets from sum
            }
            InstCtxScope ic(*this);
            ic.SetRefDecl(*d->outerDecl, ub);
            auto instTy = typeManager.InstOf(d->ty);
            if (typeManager.IsSubtype(instTy, tgtTy)) {
                if (validTy && instTy != validTy) {
                    return; // can't disambiguate anyway, just return
                }
                filteredTargets.push_back(d);
                validTy = instTy;
            }
        }
        targets = filteredTargets;
    }
    if (targets.size() == 1 && !FilterSumUpperbound(ma, tv, *targets[0])) {
        ma.ty = TypeManager::GetInvalidTy();
        DiagMismatchedTypesWithFoundTy(diag, ma, *tgtTy, *ma.ty);
    }
}

bool TypeChecker::TypeCheckerImpl::FilterSumUpperbound(AST::MemberAccess& ma, AST::GenericsTy& tv, const AST::Decl& d)
{
    CJC_ASSERT(tv.isPlaceholder);
    auto hasMemOfTargetSig = [this, &ma, &d](Ptr<Ty> ty)->bool {
        for (auto& [d2, ub] : ma.foundUpperBoundMap) {
            if (ub.count(ty) > 0 && typeManager.IsTyEqual(d.ty, d2->ty)) {
                return true;
            }
        }
        return false;
    };
    auto& cst = typeManager.constraints[&tv];
    auto& sum = cst.sum;
    for (auto it = sum.begin(); it != sum.end();) {
        if (!hasMemOfTargetSig(*it)) {
            it = sum.erase(it);
        } else {
            it++;
        }
    }
    if (sum.size() == 1 && !(*sum.begin())->IsAny()) {
        auto eq = *sum.begin();
        if (cst.eq.size() == 0) {
            cst.eq.insert(eq);
        }
        if (!typeManager.IsTyEqual(&tv, eq)) {
            return false;
        }
    }
    return true;
}

Ptr<Decl> TypeChecker::TypeCheckerImpl::GetIdealTypeFuncTargetFromExtend(
    const ASTContext& ctx, MemberAccess& ma, const Ty& baseExprTy)
{
    CJC_NULLPTR_CHECK(ma.curFile);
    std::unordered_set<Ptr<Decl>> candidates;
    std::vector<Ptr<Ty>> targetTys;
    for (auto& tyKind : GetIdealTypesByKind(baseExprTy.kind)) {
        auto targets = ExtendFieldLookup(ctx, *ma.curFile, TypeManager::GetPrimitiveTy(tyKind), ma.field);
        for (auto& target : targets) {
            if (!target->TestAttr(AST::Attribute::STATIC)) {
                candidates.insert(target);
                targetTys.push_back(TypeManager::GetPrimitiveTy(tyKind));
            }
        }
    }
    if (candidates.size() > 1) {
        CJC_ASSERT(ma.baseExpr);
        ReplaceIdealTy(*ma.baseExpr);
        if (auto lce = DynamicCast<LitConstExpr*>(ma.baseExpr.get())) {
            InitializeLitConstValue(*lce);
        }
        Ptr<Decl> target = nullptr;
        {
            auto disDiag = DiagSuppressor(diag);
            target = GetObjMemberAccessTarget(ctx, ma, *ma.baseExpr->ty);
        }
        if (Ty::IsTyCorrect(ma.baseExpr->ty) && target == nullptr) {
            std::string tyStr = Ty::GetTypesToStr(targetTys, " ");
            diag.DiagnoseRefactor(DiagKindRefactor::sema_ambiguous_match_primitive_extend, ma, ma.field, tyStr);
        }
        return target;
    } else if (candidates.size() == 1) {
        ma.baseExpr->ty = targetTys.back();
        ma.target = *candidates.begin();
        if (ma.target->astKind == ASTKind::FUNC_DECL) {
            ma.targets.push_back(StaticAs<ASTKind::FUNC_DECL>(ma.target));
        }
        return ma.target;
    } else {
        DiagMemberAccessNotFound(ma);
        return nullptr;
    }
}

Ptr<Decl> TypeChecker::TypeCheckerImpl::GetMemberAccessExposedTarget(
    const ASTContext& ctx, MemberAccess& ma, const GenericsTy& genericsTy, bool isStaticAccess)
{
    ma.isExposedAccess = true;
    if (!genericsTy.isUpperBoundLegal) {
        return nullptr; // If not legal, errors should be reported before.
    }
    auto allUpperBounds = genericsTy.upperBounds;
    std::unordered_map<Ptr<Ty>, std::vector<Ptr<Decl>>> allTargets;
    for (auto& upperBound : allUpperBounds) {
        if (!Ty::IsTyCorrect(upperBound)) {
            continue;
        }
        auto targets = GetUpperBoundTargets(ctx, ma, *upperBound, isStaticAccess);
        if (!targets.empty()) {
            allTargets.emplace(std::make_pair(upperBound, targets));
        }
    }
    if (allTargets.empty()) {
        isStaticAccess ? DiagForGenericParamMemberNotFound(diag, ma, *genericsTy.decl)
                       : DiagMemberAccessNotFound(ma);
        return nullptr;
    }
    // Since members of user defined type cannot have same name, the found targets must have same astKind.
    // If not same, report error.
    auto kind = GetTargetsSameASTKind(allTargets);
    if (kind == ASTKind::INVALID_DECL) {
        OrderedDeclSet candidates;
        for (auto it : allTargets) {
            candidates.insert(it.second.begin(), it.second.end());
        }
        DiagAmbiguousUpperBoundTargets(diag, ma, candidates);
        return nullptr;
    }
    // Will report error if return nullptr.
    if (kind == ASTKind::FUNC_DECL) {
        return CheckUpperBoundTargetsCaseFuncCall(ctx, ma, allTargets);
    } else {
        return CheckUpperBoundTargetsCaseOthers(ctx, ma, allTargets);
    }
}

Ptr<Decl> TypeChecker::TypeCheckerImpl::GetObjMemberAccessTarget(
    const ASTContext& ctx, MemberAccess& ma, Ty& baseExprTy)
{
    // If the member access is 'this.xxx' and in original type decl, the members in extend should be ignored.
    auto re = DynamicCast<RefExpr*>(ma.baseExpr.get());
    auto outerDecl = GetCurInheritableDecl(ctx, ma.scopeName);
    bool searchExtend = !re || !re->isThis || !outerDecl || outerDecl->astKind == ASTKind::EXTEND_DECL;
    CJC_NULLPTR_CHECK(ma.curFile);
    return match(baseExprTy)(
        [this, &ctx, &ma, searchExtend](ClassTy& classTy) {
            LookupInfo info{&classTy, ma.curFile, true, searchExtend, ma.TestAttr(AST::Attribute::LEFT_VALUE)};
            auto targets = FieldLookup(ctx, classTy.declPtr, ma.field, info);
            return FilterAndGetTargetsOfObjAccess(ctx, ma, targets);
        },
        [this, &ctx, &ma](InterfaceTy& interfaceTy) {
            auto targets = FieldLookup(ctx, interfaceTy.declPtr, ma.field, {&interfaceTy, ma.curFile});
            return FilterAndGetTargetsOfObjAccess(ctx, ma, targets);
        },
        [this, &ctx, &ma, searchExtend](const StructTy& structTy) {
            auto targets = FieldLookup(
                ctx, structTy.declPtr, ma.field, {.file = ma.curFile, .lookupExtend = searchExtend});
            return FilterAndGetTargetsOfObjAccess(ctx, ma, targets);
        },
        [this, &ctx, &ma, searchExtend](const EnumTy& enumTy) {
            auto targets = FieldLookup(
                ctx, enumTy.declPtr, ma.field, {.file = ma.curFile, .lookupExtend = searchExtend});
            return FilterAndGetTargetsOfObjAccess(ctx, ma, targets);
        },
        [this, &ctx, &ma](ArrayTy& arrayTy) {
            auto targets = ExtendFieldLookup(ctx, *ma.curFile, &arrayTy, ma.field);
            return FilterAndGetTargetsOfObjAccess(ctx, ma, targets);
        },
        [this, &ma](const VArrayTy& varrayTy) {
            // 'size' is the only member of VArray.
            if (ma.field == "size") {
                ma.ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64);
                auto literalExpr = CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(varrayTy.size), ma.ty);
                ma.desugarExpr = std::move(literalExpr);
                ma.desugarExpr->ty = ma.ty;
            } else {
                DiagMemberAccessNotFound(ma);
            }
            return Ptr<Decl>();
        },
        [this, &ctx, &ma](PointerTy& pointerTy) {
            auto targets = ExtendFieldLookup(ctx, *ma.curFile, &pointerTy, ma.field);
            return FilterAndGetTargetsOfObjAccess(ctx, ma, targets);
        },
        [this, &ctx, &ma](GenericsTy& genericsTy) {
            if (genericsTy.isPlaceholder) {
                auto maybeSol = typeManager.TryGreedySubst(&genericsTy);
                if (!maybeSol->IsPlaceholder()) {
                    ma.baseExpr->ty = maybeSol;
                    return GetObjMemberAccessTarget(ctx, ma, *maybeSol);
                }
                auto& cst = typeManager.constraints[&genericsTy];
                genericsTy.upperBounds.clear();
                genericsTy.upperBounds.insert(cst.ubs.begin(), cst.ubs.end());
                genericsTy.upperBounds.insert(cst.sum.begin(), cst.sum.end());
            }
            // Diagnose inside callee.
            return GetMemberAccessExposedTarget(ctx, ma, genericsTy, false);
        },
        [this, &ctx, &ma, &baseExprTy]() {
            if (baseExprTy.IsIdeal()) {
                return GetIdealTypeFuncTargetFromExtend(ctx, ma, baseExprTy);
            }
            auto targets = ExtendFieldLookup(ctx, *ma.curFile, &baseExprTy, ma.field);
            return FilterAndGetTargetsOfObjAccess(ctx, ma, targets);
        });
}

std::vector<Ptr<Decl>> TypeChecker::TypeCheckerImpl::GetUpperBoundTargets(
    const ASTContext& ctx, const MemberAccess& ma, Ty& baseExprTy, const bool isStaticAccess)
{
    CJC_NULLPTR_CHECK(ma.curFile);
    auto filterTargets = [&isStaticAccess](std::vector<Ptr<Decl>>& targets) -> void {
        for (auto it = targets.begin(); it != targets.end();) {
            // Objects cannot be used to access static members.
            if ((!isStaticAccess && (*it)->TestAttr(AST::Attribute::STATIC)) ||
                (isStaticAccess && !(*it)->TestAttr(AST::Attribute::STATIC))) {
                it = targets.erase(it);
            } else {
                ++it;
            }
        }
    };
    return match(baseExprTy)(
        [this, &ctx, &ma, &filterTargets](ClassTy& classTy) {
            auto targets = FieldLookup(ctx, classTy.decl, ma.field, {&classTy, ma.curFile});
            filterTargets(targets);
            return targets;
        },
        [this, &ctx, &ma, &filterTargets](InterfaceTy& interfaceTy) {
            auto targets = FieldLookup(ctx, interfaceTy.decl, ma.field, {&interfaceTy, ma.curFile});
            filterTargets(targets);
            return targets;
        },
        [this, &ctx, &ma, &filterTargets](const StructTy& structTy) {
            auto targets = FieldLookup(ctx, structTy.decl, ma.field, {.file = ma.curFile});
            filterTargets(targets);
            return targets;
        },
        [this, &ctx, &ma, &filterTargets](const EnumTy& enumTy) {
            auto targets = FieldLookup(ctx, enumTy.declPtr, ma.field, {.file = ma.curFile});
            filterTargets(targets);
            return targets;
        },
        [this, &ctx, &ma, &filterTargets](ArrayTy& arrayTy) {
            auto targets = ExtendFieldLookup(ctx, *ma.curFile, &arrayTy, ma.field);
            filterTargets(targets);
            return targets;
        },
        [this, &ctx, &ma, &filterTargets](PrimitiveTy& ty) {
            auto targets = ExtendFieldLookup(ctx, *ma.curFile, &ty, ma.field);
            filterTargets(targets);
            return targets;
        },
        []() { return std::vector<Ptr<Decl>>(); });
}

void TypeChecker::TypeCheckerImpl::CheckForbiddenMemberAccess(
    const ASTContext& ctx, const MemberAccess& ma, const Decl& target) const
{
    auto funcSrc = ScopeManager::GetOutMostSymbol(ctx, SymbolKind::FUNC, ma.scopeName);
    if (auto re = DynamicCast<RefExpr*>(ma.baseExpr.get()); re && funcSrc) {
        bool isThisOrSuper = re->ty && (re->isThis || re->isSuper);
        if (re->isThis) {
            bool accessStructLeftValue = ma.TestAttr(AST::Attribute::LEFT_VALUE) && re->ty && re->ty->IsStruct();
            CheckImmutableFuncAccessMutableFunc(ma.begin, *funcSrc->node, target, accessStructLeftValue);
        }
        auto funcDecl = StaticCast<FuncDecl*>(funcSrc->node);
        if (isThisOrSuper) {
            CheckForbiddenFuncReferenceAccess(ma.field.ZeroPos() ? ma.begin : ma.field.Begin(), *funcDecl, target);
        }
    }
    // NOT allowed to call enum constructor by object instance.
    if (target.TestAttr(AST::Attribute::ENUM_CONSTRUCTOR)) {
        ctx.diag.Diagnose(ma, DiagKind::sema_invalid_enum_member_access);
    }
    // Object member access does not allow type arguments appeared after var decl field.
    if (target.astKind == ASTKind::VAR_DECL && !ma.typeArguments.empty() && ma.isAlone) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_generic_argument_no_match, ma);
    }
}
