// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "CJMP/MPTypeCheckerImpl.h"
#include "DiagSuppressor.h"
#include "Diags.h"
#include "EnumSugarChecker.h"
#include "EnumSugarTargetsFinder.h"
#include "ExtraScopes.h"
#include "LocalTypeArgumentSynthesis.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Macro/TestEntryConstructor.h"
#include "cangjie/Modules/ModulesUtils.h"

using namespace Cangjie;
using namespace Sema;
using namespace TypeCheckUtil;
using namespace Meta;

namespace {
std::unordered_map<Ptr<Decl>, std::unordered_set<Ptr<Ty>>> UpperBoundModalSetsToDataTy(
    const std::unordered_map<Ptr<Decl>, std::unordered_set<ModalTy>>& src)
{
    std::unordered_map<Ptr<Decl>, std::unordered_set<Ptr<Ty>>> out;
    for (const auto& e : src) {
        std::unordered_set<Ptr<Ty>> tys;
        for (const auto& m : e.second) {
            tys.insert(m.Ty());
        }
        out.emplace(e.first, std::move(tys));
    }
    return out;
}

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

bool CheckEnumCtorModal(ModalTy target, Expr& expr)
{
    // refNode is enum ctor, target ty is the enum with any local, it is good if
    // 1) either refNode's modal is subtype of target's modal (which is checked later)
    // 2) or refNode does not specify modal. in this case, convert refNode's modal to target's modal
    // the following two if's are both of 2)
    if (Is<RefExpr>(&expr)) {
        expr.SetTy(expr.GetTy().With(target.Mode()));
        return true;
    } else if (auto ma = DynamicCast<MemberAccess>(&expr); ma && ma->baseExpr && ma->baseExpr->GetTy().IsCorrect()) {
        expr.SetTy(expr.GetTy().With(ma->baseExpr->TyMode()));
        return true;
    }
    return false;
}

bool CheckInferrableEnumReference(DiagnosticEngine& diag, TypeManager& tyMgr, Expr& expr, ModalTy target)
{
    auto res = EnumSugarTargetsFinder::RefineTargetTy(tyMgr, target, expr.GetTarget());
    if (!res.has_value()) {
        diag.Diagnose(expr, DiagKind::sema_generic_type_without_type_argument);
        return false;
    }
    auto targetTy = *res;
    if (target->IsInterface()) {
        auto candiTys = Promotion(tyMgr).Downgrade(targetTy, target);
        if (candiTys.empty()) {
            DiagUnableToInferExpr(diag, expr);
            return false;
        }
        targetTy = *candiTys.begin();
    }
    expr.SetTy(targetTy);
    Ptr<Expr> ref = &expr;
    if (auto ma = DynamicCast<MemberAccess*>(&expr); ma) {
        ref = ma->baseExpr.get();
        ref->SetTy(targetTy); // Type of enum base is same with the expression's ty.
    }
    if (auto reference = DynamicCast<AST::NameReferenceExpr*>(ref)) {
        reference->instTys.clear();
        for (auto it : targetTy->typeArgs) {
            (void)reference->instTys.emplace_back(it.Ty());
        }
    }
    return true;
}

bool CheckNonFunctionReference(DiagnosticEngine& diag, TypeManager& tyMgr, ModalTy target, Expr& refNode)
{
    auto refTarget = refNode.GetTarget();
    bool checkEnumCtor = refTarget && refTarget->TestAttr(AST::Attribute::ENUM_CONSTRUCTOR);
    if (checkEnumCtor && IsEnumNeedSynthesis(refNode, *refTarget)) {
        // Check and update type for enum target without typeArgument.
        return CheckInferrableEnumReference(diag, tyMgr, refNode, target);
    }

    bool isWellTyped = true;
    if (checkEnumCtor) {
        ModalTy targetTyWith = target.With({});
        ModalTy foundTyWith = refNode.GetTy().With({});
        isWellTyped = tyMgr.IsSubtype(foundTyWith, targetTyWith) && CheckEnumCtorModal(target, refNode);
    } else {
        isWellTyped = tyMgr.IsSubtype(refNode.GetTy(), target);
    }
    if (!isWellTyped) {
        DiagMismatchedTypes(diag, refNode, target);
        refNode.SetTy({TypeManager::GetInvalidTy()});
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

ASTKind GetTargetsSameASTKind(std::unordered_map<ModalTy, std::vector<Ptr<Decl>>>& allTargets)
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

std::unordered_map<Ptr<Decl>, std::unordered_set<ModalTy>> GetUpperBoundTargetsWithGivenKind(
    const std::unordered_map<ModalTy, std::vector<Ptr<Decl>>>& allTargets, const std::unordered_set<ASTKind>& kinds)
{
    std::unordered_map<Ptr<Decl>, std::unordered_set<ModalTy>> results;
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

FuncSig2Decl::const_iterator FoundSameSignatureMember(
    TypeManager& tyMgr, const Decl& decl, std::optional<ModalInfo> thisMode, FuncTy& funcTy, FuncSig2Decl& methodSigs)
{
    FuncSig keyPair{decl.identifier, thisMode, funcTy.paramTys};
    auto found = methodSigs.find(keyPair);
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
        // Checking whether substituted function signature exists in map.
        auto instTy = StaticCast<FuncTy>(tyMgr.GetInstantiatedTy(&funcTy, typeMapping));
        std::optional<ModalInfo> declThisMode{};
        if (auto fd = DynamicCast<FuncDecl>(it); fd && tyMgr.HasThisParam(*fd)) {
            declThisMode = GetThisParamModal(*fd);
        }
        keyPair = {decl.identifier, declThisMode, instTy->paramTys};
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
        std::optional<ModalInfo> thisMode{};
        if (auto fd = StaticCast<FuncDecl>(decl); tyMgr.HasThisParam(*fd)) {
            thisMode = GetThisParamModal(*fd);
        }
        auto tys = tyMgr.GetInstantiatedTys(decl->DataTy(), mts);
        for (auto ty : tys) {
            auto funcTy = DynamicCast<FuncTy>(ty);
            if (!Ty::IsTyCorrect(funcTy)) {
                continue;
            }
            const auto found = FoundSameSignatureMember(tyMgr, *decl, thisMode, *funcTy, methodSigs);
            if (found == methodSigs.cend()) {
                methodSigs.emplace(FuncSig{decl->identifier, thisMode, funcTy->paramTys}, StaticCast<FuncDecl>(decl));
            } else if (IsCloserToImpl(*found->second, *decl)) {
                // If the decl is generic, the paramsTys in the map key should also be updated,
                // so, just erase found result and emplace new result here.
                methodSigs.erase(found);
                methodSigs.emplace(FuncSig{decl->identifier, thisMode, funcTy->paramTys}, StaticCast<FuncDecl>(decl));
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
    FuncSig2Decl methodSigs;
    for (auto decl : upperDecls) {
        CJC_NULLPTR_CHECK(decl);
        if (decl->astKind != ASTKind::FUNC_DECL) {
            continue;
        }
        MultiTypeSubst mts;
        tyMgr.GenerateTypeMappingForUpperBounds(mts, ma, *decl);
        std::optional<ModalInfo> thisMode{};
        if (auto fd = StaticCast<FuncDecl>(decl); tyMgr.HasThisParam(*fd)) {
            thisMode = GetThisParamModal(*fd);
        }
        auto tys = tyMgr.GetInstantiatedTys(decl->DataTy(), mts);
        for (auto ty : tys) {
            auto funcTy = DynamicCast<FuncTy>(ty);
            if (!Ty::IsTyCorrect(funcTy)) {
                continue;
            }
            auto found = FoundSameSignatureMember(tyMgr, *decl, thisMode, *funcTy, methodSigs);
            if (found == methodSigs.cend()) {
                methodSigs.emplace(FuncSig{decl->identifier, thisMode, funcTy->paramTys}, StaticCast<FuncDecl>(decl));
            } else if (IsCloserToImpl(*found->second, *decl)) {
                // If the decl is generic, the paramsTys in the map key should also be updated,
                // so, just erase found result and emplace new result here.
                methodSigs.erase(found);
                methodSigs.emplace(FuncSig{decl->identifier, thisMode, funcTy->paramTys}, StaticCast<FuncDecl>(decl));
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
    if (gpd.GetTy().IsCorrect() && gpd.GetTy()->IsGeneric() &&
        RawStaticCast<GenericsTy*>(gpd.DataTy())->isUpperBoundLegal) {
        diag.Diagnose(*ma.baseExpr, DiagKind::sema_invalid_field_expose_access, ma.field.Val(),
            "exposed generic parameter", gpd.identifier.Val());
    }
}

/// Get local modal of the receiver when \p nre is used as a function reference.
ModalInfo GetFunRefCaptureModal(const ASTContext& ctx, const NameReferenceExpr& nre)
{
    if (auto ma = DynamicCast<MemberAccess>(&nre)) {
        if (auto targetOfBase = ma->baseExpr->GetTarget();
            targetOfBase && targetOfBase->IsTypeDecl() && !IsThisOrSuper(*ma->baseExpr)) {
            return {};
        }
        return ma->baseExpr->TyMode();
    }
    if (auto re = DynamicCast<RefExpr>(&nre)) {
        if (re->isSuper || re->isThis) {
            return re->TyMode();
        }
        return GetCurThisModal(ctx, re->scopeName);
    }
    return {};
}
} // namespace

void TypeChecker::TypeCheckerImpl::DiagLocalFullFunRefCapture(
    const ASTContext& ctx, const NameReferenceExpr& nre, const std::string& capturedName) const
{
    diag.DiagnoseRefactor(DiagKindRefactor::sema_bad_capture_local, nre, GetFunRefCaptureModal(ctx, nre).ToString(),
        capturedName, "member function", "reference");
}

/// True when a standalone function reference captures an @local! receiver
/// Returns true when capturing a @local! fun ref.
bool TypeChecker::TypeCheckerImpl::IsCapturingLocalFullInFunRef(
    const ASTContext& ctx, const NameReferenceExpr& nre, const FuncDecl& fd)
{
    if (!nre.isAlone || nre.callOrPattern || !TypeManager::HasThisParam(fd)) {
        return false;
    }
    return GetFunRefCaptureModal(ctx, nre).local == Mode::FULL;
}

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
    if (!baseExpr->GetTy().IsCorrect()) {
        return; // Do not report error for baseExpr with invlaid type.
    }
    auto getMemberRange = [&ma]() { return ma.field.ZeroPos() ? MakeRange(ma.begin, ma.end) : MakeRange(ma.field); };
    if (ma.isExposedAccess) {
        (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_not_found_from_generic_upper_bounds, ma, getMemberRange(),
            ma.field.Val(), baseExpr->GetTy()->name);
    } else if (baseExpr->GetTy()->IsNominal()) {
        std::string kind = baseExpr->GetTy()->Ty::String();
        kind[0] = static_cast<char>(std::tolower(kind[0]));
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_not_member_of, ma, getMemberRange(), ma.field, kind,
            baseExpr->GetTy()->name + baseExpr->GetTy()->PrintTypeArgs());
        RecommendImportForMemberAccess(typeManager, importManager, ma, &builder);
    } else if (ma.ShouldDiagnose(true)) {
        auto builder =
            diag.DiagnoseRefactor(DiagKindRefactor::sema_undeclared_identifier, ma, getMemberRange(), ma.field);
        RecommendImportForMemberAccess(typeManager, importManager, ma, &builder);
    }
}

bool TypeChecker::TypeCheckerImpl::CheckThisTypeForFunRef(
    const ASTContext& ctx, const FuncDecl& fd, const NameReferenceExpr& refNode)
{
    if (!typeManager.HasThisParam(fd)) {
        return true;
    }
    auto fdThisTy = typeManager.GetThisParamTy(fd);
    // check for modal compatibility, because can only access a member func with compatible modal
    if (auto re = DynamicCast<RefExpr>(&refNode)) {
        if (Is<BuiltInDecl>(fd.outerDecl) || Is<InheritableDecl>(fd.outerDecl)) {
            // implicit this. access
            auto thisMode = GetThisParamTyInScope(ctx, re->scopeName);
            if (!thisMode.Mode().IsSubModal(fdThisTy.Mode())) {
                return false;
            }
        }
    }
    if (auto ma = DynamicCast<MemberAccess>(&refNode)) {
        if (!ma->baseExpr->TyMode().IsSubModal(fdThisTy.Mode())) {
            return false;
        }
    }
    return true;
}

void TypeChecker::TypeCheckerImpl::ResolveFunRefOverload(const ASTContext& ctx, NameReferenceExpr& refNode,
    std::vector<std::tuple<Ptr<AST::FuncDecl>, AST::ModalTy, TypeSubst>>& candidates)
{
    // resolve only by this type, which is the same as in ResolveOverload
    if (candidates.empty()) {
        return;
    }
    auto thisArgTy = GetReceiverTy(ctx, refNode);
    auto argMode = thisArgTy.Mode();
    // Classify this-param candidates by their modal relation to the receiver.
    bool hasExact{false};
    bool hasSubmode{false};
    bool hasThisParam{false};
    for (const auto& can : candidates) {
        auto fd = std::get<0>(can);
        if (!typeManager.HasThisParam(*fd)) {
            continue;
        }
        hasThisParam = true;
        auto paramMode = typeManager.GetThisParamTy(*fd).Mode();
        if (argMode == paramMode) {
            hasExact = true;
        }
        if (argMode.IsSubModal(paramMode)) {
            hasSubmode = true;
        }
    }
    // not instance method ref, return
    if (!hasThisParam) {
        return;
    }
    if (!hasExact && !hasSubmode) {
        // only consider copy type cast when even sub modes do not match any overload, keep all in this case.
        // otherwise go through normal filter by this mode
        if (typeManager.ImplementsCopyInterface(thisArgTy.Ty())) {
            return;
        }
        candidates.clear();
        return;
    }

    for (auto it = candidates.begin(); it != candidates.end();) {
        auto fd = std::get<0>(*it);
        if (!typeManager.HasThisParam(*fd)) {
            ++it;
            continue;
        }
        auto paramMode = typeManager.GetThisParamTy(*fd).Mode();
        if (hasExact ? argMode != paramMode : !argMode.IsSubModal(paramMode)) {
            it = candidates.erase(it);
        } else {
            ++it;
        }
    }
}

bool TypeChecker::TypeCheckerImpl::ChkRefExpr(ASTContext& ctx, ModalTy target, NameReferenceExpr& refNode)
{
    Synthesize({ctx, SynPos::EXPR_ARG}, &refNode);
    auto targets = GetFuncTargets(refNode);
    // None function target check.
    if (targets.empty()) {
        if (Is<PropDecl>(refNode.GetTarget())) {
            if (!typeManager.IsSubtype(refNode.GetTy(), target)) {
                DiagMismatchedTypes(diag, refNode, target);
                refNode.SetTy({TypeManager::GetInvalidTy()});
                return false;
            }
            return true;
        }
        return CheckNonFunctionReference(diag, typeManager, target, refNode);
    }
    // Overloading check.
    CJC_ASSERT(refNode.IsReferenceExpr());
    RemoveDuplicateElements(targets);
    // Add for cjmp
    mpImpl->RemoveCommonCandidatesIfHasSpecific(targets);
    auto [genericIgnored, candidates] = CollectValidFuncTys(ctx, targets, refNode, DynamicCast<FuncTy>(target.Ty()));
    // filter @local! func ref captures
    if (refNode.isAlone && !candidates.empty() && std::get<0>(candidates[0])->astKind == ASTKind::FUNC_DECL) {
        decltype(candidates) valid;
        for (auto& can : candidates) {
            if (!IsCapturingLocalFullInFunRef(ctx, refNode, *std::get<0>(can))) {
                valid.push_back(can);
            }
        }
        if (valid.empty()) {
            DiagLocalFullFunRefCapture(ctx, refNode, "this");
            refNode.SetTy({TypeManager::GetInvalidTy()});
            return false;
        }
        std::swap(candidates, valid);
    }
    ResolveFunRefOverload(ctx, refNode, candidates);

    TypeSubst resultMapping;
    Ptr<FuncDecl> matchedFd = nullptr;
    int matched{0};
    ModalTy validCandidateTy{};
    for (auto [fd, fdTy, mapping] : candidates) {
        if (!typeManager.IsSubtype(fdTy, target) || !CheckThisTypeForFunRef(ctx, *fd, refNode)) {
            if (!validCandidateTy && Ty::IsTyCorrect(fdTy) && !StaticCast<FuncTy>(fdTy.Ty())->retTy->IsQuest()) {
                validCandidateTy = fdTy;
            }
            continue;
        }
        ++matched;
        ReplaceTarget(&refNode, fd);
        matchedFd = fd;
        refNode.SetTy(fdTy);
        resultMapping = mapping;
    }
    refNode.SetTy(TypeManager::GetNonNullTy(refNode.GetTy()));
    if (refNode.GetTy()->IsQuest()) {
        refNode.SetTy({TypeManager::GetInvalidTy()});
    }
    if (matched > 1) {
        diag.Diagnose(refNode, DiagKind::sema_ambiguous_func_ref, targets[0]->identifier.Val());
    } else if (matched == 0) {
        if (refNode.GetTy().IsCorrect() && refNode.GetTy()->HasQuestTy()) {
            CJC_ASSERT(refNode.GetTarget());
            DiagUnableToInferReturnType(diag, *targets[0], refNode);
        } else if (genericIgnored) {
            diag.Diagnose(refNode, DiagKind::sema_generic_type_without_type_argument);
        } else if (validCandidateTy) {
            DiagMismatchedTypesWithFoundTy(diag, refNode, target, validCandidateTy);
        } else {
            diag.Diagnose(refNode, DiagKind::sema_no_match_function_declaration_for_ref, targets[0]->identifier.Val());
        }
        if (targets.size() > 1) {
            ReplaceTarget(&refNode, nullptr);
        }
    } else {
        if (IsGenericUpperBoundCall(refNode, *matchedFd)) {
            auto& ref = static_cast<NameReferenceExpr&>(refNode);
            ref.matchedParentTy = typeManager.GetInstantiatedTy(matchedFd->outerDecl->GetTy(), resultMapping);
        }
        InstantiateReferenceType(ctx, refNode, resultMapping);
    }
    return !candidates.empty();
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
        auto targetTy = Synthesize({ctx, SynPos::EXPR_ARG}, declToSyn);
        if (auto fd = DynamicCast<FuncDecl*>(declToSyn); fd && targetTy->HasQuestTy()) {
            DiagUnableToInferReturnType(diag, *fd, nre);
            return false;
        }
    }
    return true;
}

void TypeChecker::TypeCheckerImpl::InferRefExpr(ASTContext& ctx, RefExpr& re)
{
    if (re.ref.target && re.GetTy().IsCorrect() && !re.ref.target->GetTy()->IsPlaceholder()) {
        return; // If the target is already existed and type is valid, we can exit early.
    }
    bool isWellTyped = true;
    for (auto& type : re.typeArguments) {
        isWellTyped = Synthesize({ctx, SynPos::EXPR_ARG}, type.get()).IsCorrect() && isWellTyped;
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
                re.SetTy({TypeManager::GetInvalidTy()});
            }
            return;
        }
        targets = res.second;
    }

    if (!FilterAndCheckTargetsOfRef(ctx, re, targets) || !isWellTyped) {
        re.SetTy({TypeManager::GetInvalidTy()});
        return;
    }
    if (re.isAlone && !re.callOrPattern && !ctx.HasTargetTy(&re) && !targets.empty() && IsAllFuncDecl(targets)) {
        std::vector<Ptr<Decl>> kept;
        kept.reserve(targets.size());
        for (auto target : targets) {
            if (IsCapturingLocalFullInFunRef(ctx, re, *StaticCast<FuncDecl>(target))) {
                continue;
            }
            kept.emplace_back(target);
        }
        if (kept.empty()) {
            // All overloads capture a @local! receiver: no usable target, report and bail.
            DiagLocalFullFunRefCapture(ctx, re, targets.front()->identifier.Val());
            re.SetTy({TypeManager::GetInvalidTy()});
            return;
        }
        std::swap(kept, targets);
    }
    // 'targets' must not empty.
    auto decl = GetAccessibleDecl(ctx, re, targets);
    if (!decl) {
        // indeterminately pick one, overload resolution is done later
        decl = targets.front();
    }
    ModifyTargetOfRef(re, decl, targets);
    // Legality of using refExpr will be checked after typecheck in 'CheckLegalityOfReference'.
    CJC_ASSERT(re.GetTarget()); // 're.GetTarget()' should be set in 'ModifyTargetOfRef'.

    if (auto target = re.ref.target; target->IsBuiltIn()) {
        if (auto cfunc = StaticCast<BuiltInDecl>(target); cfunc->type == BuiltInType::CFUNC) {
            InferCFuncExpr(ctx, re);
            return;
        }
    }
    // If refExpr is base of call, decide real target & type in function call checking.
    // If refExpr is overloaded function reference with target type, decide real target & type in 'ChkRefExpr'.
    if (IsAllFuncDecl(targets) && targets.size() > 1 && (re.callOrPattern || ctx.HasTargetTy(&re))) {
        re.SetTy({TypeManager::GetQuestTy()});
        return;
    }
    if (!SynTargetOnUsed(ctx, re, *re.ref.target)) {
        re.SetTy({TypeManager::GetInvalidTy()});
        return;
    }
    if (Is<PropDecl>(re.GetTarget())) {
        if (auto res = ResolvePropOverload(ctx, re, targets)) {
            // For setter context (LEFT_VALUE), use the setter's parameter type so
            // downstream assignment checks the RHS against the prop's value type,
            // not Unit (the setter call's return type).
            auto isGetter = !re.TestAttr(Attribute::LEFT_VALUE);
            auto acc = GetUsableAccessorForProperty(*res, isGetter);
            if (acc) {
                if (!isGetter) {
                    CheckAssignToImmutProp(re);
                }
                re.SetTy(typeManager.SubstituteTypeAliasInTy(typeManager.GetAccessorTargetTy(*acc)));
            } else {
                re.SetTy({TypeManager::GetInvalidTy()});
                return;
            }
        } else {
            re.SetTy({TypeManager::GetInvalidTy()});
            return;
        }
    } else {
        re.SetTy(typeManager.SubstituteTypeAliasInTy(re.GetTarget()->GetTy()));
    }
    if (!decl->IsFunc() || re.isAlone) {
        // Only check non-function or non-call target. Functions will be check after function overload resolution.
        InstantiateReferenceType(ctx, re);
        // Member var (implicit this access): use current this modal if not demode.
        if (decl->astKind == ASTKind::VAR_DECL && decl->IsMemberDecl() &&
            !HasModifier(decl->modifiers, TokenKind::DEMODE) &&
            !decl->TestAnyAttr(Attribute::STATIC, Attribute::ENUM_CONSTRUCTOR)) {
            re.SetTy(re.GetTy().With(GetCurThisModal(ctx, re.scopeName)));
        }
    }
    if (Ty::IsInitialTy(re.DataTy())) {
        re.SetTy({TypeManager::GetInvalidTy()});
    }
}

/// For ma, return baseExpr's ty; for re, if it is implicit this access, return current this ty; otherwise return its ty
ModalTy TypeChecker::TypeCheckerImpl::GetReceiverTy(const ASTContext& ctx, const NameReferenceExpr& re) const
{
    if (auto ma = DynamicCast<MemberAccess>(&re)) {
        if (ma->baseExpr) {
            return ma->baseExpr->GetTy();
        }
    }
    if (auto re2 = DynamicCast<RefExpr>(&re)) {
        if (re2->isThis || re2->isSuper) {
            return re2->GetTy();
        }
        // implicit this access, get current this ty.
        auto target = re2->ref.target;
        if (target && target->IsMemberDecl()) {
            return GetThisParamTyInScope(ctx, re2->scopeName);
        }
    }
    return re.GetTy();
}

Ptr<PropDecl> TypeChecker::TypeCheckerImpl::ResolvePropOverload(
    const ASTContext& ctx, NameReferenceExpr& re, const std::vector<Ptr<Decl>>& targets)
{
    // Gather candidate PropDecls from the looked-up targets.
    std::vector<PropDecl*> props;
    for (auto& t : targets) {
        if (auto pd = DynamicCast<PropDecl>(t)) {
            props.push_back(pd);
        }
    }
    if (props.empty()) {
        return nullptr;
    }
    auto receiver = GetReceiverTy(ctx, re);
    std::vector<PropDecl*> callable;
    for (auto target : targets) {
        // this mode of receiver type of prop is the mode of the prop's mode.
        // Only in this case, the mode of copy type matters.
        auto pd = StaticCast<PropDecl>(target);
        auto ty = pd->GetTy();
        if (receiver.Mode().IsSubModal(ty.Mode())) {
            callable.push_back(pd);
        }
    }

    // remove the props that are strictly worse than any other. The complexity is O(n).
    // don't check callable when receiver is copy type because they always match.
    if (!callable.empty() && !typeManager.ImplementsCopyInterface(receiver.Ty())) {
        for (size_t i{0}; i + 1 < callable.size(); ++i) {
            size_t j{i + 1};
            while (j < callable.size()) {
                if (callable[i]->TyMode().IsSubModal(callable[j]->TyMode())) {
                    callable.erase(callable.begin() + static_cast<ssize_t>(j));
                } else if (callable[j]->TyMode().IsSubModal(callable[i]->TyMode())) {
                    callable.erase(callable.begin() + static_cast<ssize_t>(i));
                    break; // j is always i + 1, so no need to increment i again
                } else {
                    ++j;
                }
            }
        }
    }

    if (callable.empty()) {
        auto db = diag.Diagnose(re, DiagKind::sema_no_match_prop_accessor_for_call, props.front()->identifier.Val());
        for (auto pd : props) {
            db.AddNote(*pd, DiagKind::sema_found_candidate_decl);
        }
        if (auto ma = DynamicCast<MemberAccess>(&re)) {
            ma->targets.clear();
        }
        return nullptr;
    }

    re.SetTarget(callable[0]);
    // only when the receiver is copy type, we can have such diag.
    if (callable.size() > 1) {
        auto db = diag.Diagnose(re, DiagKind::sema_ambiguous_match, callable.front()->identifier.Val());
        for (auto pd : callable) {
            db.AddNote(*pd, DiagKind::sema_found_candidate_decl);
        }
        return nullptr;
    }
    CheckAssignToImmutProp(re);
    return callable[0];
}

void TypeChecker::TypeCheckerImpl::CheckAssignToImmutProp(NameReferenceExpr& re)
{
    auto pd = DynamicCast<PropDecl>(re.GetTarget());
    if (pd && re.TestAttr(Attribute::LEFT_VALUE) && !pd->isVar) {
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_cannot_assign_to_immutable, re);
        if (!pd->identifier.ZeroPos()) {
            builder.AddNote(
                *pd, MakeRange(pd->identifier), DeclKindToString(*pd) + " '" + pd->identifier + "' is immutable");
            re.SetTy({TypeManager::GetInvalidTy()});
            return;
        }
    }
}

void TypeChecker::TypeCheckerImpl::InferCFuncExpr(ASTContext& ctx, NameReferenceExpr& nre)
{
    // infer type arguments of CFunc is currently disallowed
    if (nre.typeArguments.size() != 1) {
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_generic_argument_no_match, nre, MakeRange(nre.GetBegin(), nre.GetEnd()));
        nre.SetTy({TypeManager::GetInvalidTy()});
        return;
    }
    auto funcType = DynamicCast<FuncType>(&*nre.typeArguments[0]);
    if (!funcType) {
        diag.Diagnose(nre.typeArguments[0]->GetBegin(), nre.typeArguments[0]->GetEnd(), DiagKind::sema_cfunc_type);
        nre.SetTy({TypeManager::GetInvalidTy()});
        return;
    }

    // synthesise type
    std::vector<ModalTy> paramTys;
    for (size_t i{0}; i < funcType->paramTypes.size(); ++i) {
        auto& param = funcType->paramTypes[i];
        param->SetTy(GetTyFromASTType(ctx, &*param));
        paramTys.push_back(param->GetTy());
    }
    funcType->retType->SetTy(GetTyFromASTType(ctx, funcType->retType.get()));
    ModalTy retTy = funcType->retType->GetTy();
    auto resTy = typeManager.GetFunctionTy(std::move(paramTys), retTy, {.isC = true});
    funcType->SetTy({resTy});
    nre.SetTy({resTy});
}

void TypeChecker::TypeCheckerImpl::TryInitializeBaseSum(ASTContext& ctx, MemberAccess& ma)
{
    if (!ma.baseExpr->GetTy()->IsPlaceholder()) {
        return;
    }
    auto tv = RawStaticCast<GenericsTy*>(ma.baseExpr->DataTy());
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
    TryEnforceCandidate(*tv, ctx.Mem2Decls(sig), typeManager, {sig});
}

void TypeChecker::TypeCheckerImpl::InferMemberAccess(ASTContext& ctx, MemberAccess& ma)
{
    if (ma.target && ma.GetTy().IsCorrect()) {
        return; // If the target already exists and type is valid, we can exit early.
    }
    bool isWellTyped = true;
    for (auto& type : ma.typeArguments) {
        isWellTyped = Synthesize({ctx, SynPos::NONE}, type.get()).IsCorrect() && isWellTyped;
    }
    Ptr<Expr> baseExpr = ma.baseExpr.get();
    if (!baseExpr || !isWellTyped) {
        ma.SetTy({TypeManager::GetInvalidTy()});
        return;
    }
    SetIsNotAlone(*ma.baseExpr);
    auto targetOfBase = GetBaseDeclInMemberAccess(ctx, ma);
    TryInitializeBaseSum(ctx, ma);
    // baseExpr is Synthesized by GetBaseDeclInMemberAccess. Refactor later.
    // Whether current is access member by type alias of primitive types.
    bool isPrimitiveTypeAlias = targetOfBase && targetOfBase->astKind == ASTKind::TYPE_ALIAS_DECL &&
        baseExpr->GetTy() && baseExpr->GetTy()->IsPrimitive();
    // Whether current is access member by real/generic type or package name.
    bool isStaticAccessByName =
        targetOfBase && !IsThisOrSuper(*baseExpr) && (targetOfBase->IsTypeDecl() || Is<PackageDecl>(targetOfBase));
    bool isBuiltInStaticAccess = targetOfBase && targetOfBase->IsBuiltIn();
    bool isPartialPackagePath =
        !targetOfBase && Ty::IsInitialTy(ma.baseExpr->DataTy()) && (ma.isAlone || ma.callOrPattern);
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
    if (auto builtin = DynamicCast<BuiltInDecl>(ma.target); builtin && builtin->type == BuiltInType::CFUNC) {
        InferCFuncExpr(ctx, ma);
        return;
    }
    // Legality of using memberAccess will be checked after typecheck in 'CheckLegalityOfReference'.
    // If memberAccess is base of call, decide real target & type in function call checking.
    // If memberAccess is overloaded function reference with target type, decide real target & type in 'ChkRefExpr'.
    bool checkedLater =
        IsAllFuncDecl(ma.targets) && ma.targets.size() > 1 && (ma.callOrPattern || ctx.HasTargetTy(&ma));
    if (checkedLater) {
        ma.SetTy({TypeManager::GetQuestTy()});
        return;
    }
    if (!SynTargetOnUsed(ctx, ma, *ma.target)) {
        ma.SetTy({TypeManager::GetInvalidTy()});
        return;
    }
    if (Is<PropDecl>(ma.target)) {
        ma.SetTy(typeManager.SubstituteTypeAliasInTy(ma.target->GetTy()));
        CheckAssignToImmutProp(ma);
    } else if (Is<VarDecl>(ma.target)) {
        // use baseExpr ty unless demode.
        if (!ma.target->TestAttr(Attribute::STATIC) &&
            !HasModifier(ma.target->modifiers, TokenKind::DEMODE)) {
            ma.SetTy(ma.target->GetTy().With(ma.baseExpr->TyMode()));
        } else {
            ma.SetTy(typeManager.SubstituteTypeAliasInTy(ma.target->GetTy()));
        }
    } else {
        ma.SetTy(typeManager.SubstituteTypeAliasInTy(ma.target->GetTy()));
    }
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
    SynthesizeWithNegCache({ctx, SynPos::EXPR_ARG}, baseExpr);
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
        ma.SetTy({TypeManager::GetInvalidTy()});
        return;
    }
    ModalTy elemTy = typeArgs[0]->GetTy();
    ma.baseExpr->SetTy(ModalTy{typeManager.GetArrayTy(elemTy.Ty(), 1)}.With(elemTy.Mode()));
    auto targets = ExtendFieldLookup(ctx, *ma.curFile, ma.baseExpr->DataTy(), ma.field);
    if (!FilterAndCheckTargetsOfNameAccess(ctx, ma, targets)) {
        return;
    }
    auto target = GetAccessibleDecl(ctx, ma, targets);
    ReplaceTarget(&ma, target ? target : targets[0]);
    AddFuncTargetsForMemberAccess(ma, targets);
}

void TypeChecker::TypeCheckerImpl::InferStaticAccess(const ASTContext& ctx, MemberAccess& ma, Decl& targetOfBaseExpr)
{
    ma.SetTy({TypeManager::GetInvalidTy()}); // Ty will be set to valid if non-error happens.
    // Caller guarantees current is access member by real/generic type or package name.
    std::vector<Ptr<Decl>> targets;
    Ptr<Expr> baseExpr = ma.baseExpr.get();
    CJC_ASSERT(baseExpr);
    // Case for access member by real type.
    if (targetOfBaseExpr.astKind != ASTKind::GENERIC_PARAM_DECL) {
        // In this case, targetOfBaseExpr is ClassDecl, InterfaceDecl, StructDecl, EnumDecl or PackageDecl.
        CJC_NULLPTR_CHECK(ma.curFile);
        targets = FieldLookup(ctx, &targetOfBaseExpr, ma.field,
            {baseExpr->GetTy(), ma.curFile, true, true, ma.TestAttr(AST::Attribute::LEFT_VALUE)});
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
        auto genericTy = DynamicCast<GenericsTy>(baseExpr->DataTy());
        if (!genericTy) {
            return; // When the target of base is generic param and 'ty' is not 'GenericsTy', errors happened before.
        }
        // 'GetMemberAccessExposedTarget' will put found targets into 'ma.targets' and report error if not found.
        auto target = GetMemberAccessExposedTarget(ctx, ma, *genericTy, true);
        if (!target) {
            return;
        }
        if (target->astKind == ASTKind::PROP_DECL && ma.targets.size() > 1) {
            // Overloaded props: mirror non-generic InferInstanceAccess -- modal subset filtering via
            // ResolvePropOverload, then accessor selection via GetUsableAccessorForProperty. Only runs
            // when there is more than one candidate; static props cannot overload (sema_static_prop_overload
            // is reported by PreCheck) so a single static prop stays on the original simple path below.
            if (auto res = ResolvePropOverload(ctx, ma, ma.targets)) {
                auto isGetter = !ma.TestAttr(Attribute::LEFT_VALUE);
                auto acc = GetUsableAccessorForProperty(*res, isGetter);
                if (!acc) {
                    diag.Diagnose(ma, DiagKind::sema_no_match_prop_accessor_for_call, res->identifier.Val());
                    return;
                }
                target = acc->propDecl;
                ReplaceTarget(&ma, target);
                targets = {target};
            } else {
                return;
            }
        } else {
            ReplaceTarget(&ma, target);
            if (target->astKind == ASTKind::FUNC_DECL) {
                targets = std::vector<Ptr<Decl>>(ma.targets.begin(), ma.targets.end());
            } else {
                targets = {target};
            }
        }
    }
    CheckAssignToImmutProp(ma);
}

namespace {
template <typename T> bool AreAllPropDecls(T& results)
{
    for (auto it : results) {
        if (it->astKind != AST::ASTKind::PROP_DECL) {
            return false;
        }
    }
    return !results.empty();
}
}

void TypeChecker::TypeCheckerImpl::InferInstanceAccess(const ASTContext& ctx, MemberAccess& ma)
{
    // In this case, targetOfBaseExpr is an object.
    Ptr<Expr> baseExpr = ma.baseExpr.get();
    if (!baseExpr || Ty::IsInitialTy(baseExpr->DataTy())) {
        return; // 'baseExpr' may be a part of package name such as 'pkga' in 'package pkga.pkgb.pkgc'.
    }
    ma.SetTy({TypeManager::GetInvalidTy()}); // Ty will be set to valid if non-error happens.
    Ptr<Decl> target = GetObjMemberAccessTarget(ctx, ma, baseExpr->GetTy());
    if (!target) {
        return;
    }
    if (AreAllPropDecls(ma.targets)) {
        if (auto res = ResolvePropOverload(ctx, ma, ma.targets)) {
            target = res;
            auto isGetter = !ma.TestAttr(Attribute::LEFT_VALUE);
            auto acc = GetUsableAccessorForProperty(*res, isGetter);
            if (!acc) {
                // resolution succeed but accessor doesn't exist, the error should be report elsewhere (e.g. assign to
                // immutable prop), don't report again.
                ma.target = nullptr;
                return;
            }
            ma.target = acc->propDecl;
        } else {
            ma.target = nullptr;
            return;
        }
    } else {
        // if some are prop's some are not, the error should be reported later. we just pick one determinately
        ReplaceTarget(&ma, target);
    }
    if (target->IsFunc() && ma.isAlone && !ma.callOrPattern && !ctx.HasTargetTy(&ma)) {
        std::vector<Ptr<Decl>> valid;
        for (auto t : ma.targets) {
            if (!IsCapturingLocalFullInFunRef(ctx, ma, *StaticCast<FuncDecl>(t))) {
                valid.push_back(t);
            }
        }
        if (!ma.targets.empty() && valid.empty()) {
            DiagLocalFullFunRefCapture(ctx, ma, ma.field.Val());
            ma.target = {};
            ma.targets.clear();
            return;
        }
        std::swap(valid, ma.targets);
    }
}

/// Resolve a generic upper-bound member access when every matched member across all upper bounds is a function or a
/// property -- i.e. a method/property call on a generic-typed base expression such as `a.member` where `a: U` and
/// `U <: Bar2<U>`.
/// Returns the representative target Decl (first by source position among the filtered candidates), or nullptr when
/// no candidate survives filtering.
/// Side effects on `ma`.
///  - ma.targets: refilled with surviving candidates.
///  - ma.foundUpperBoundMap: populated to map each surviving candidate Decl to the set of upper-bound ModalTys it came
///    from (used by sum/placeholder filtering and to pick matchedParentTy).
///  - ma.matchedParentTy: set to the upper-bound type that the representative target was found under.
///  - Diagnostics: may be emitted by the Filter*AndCheck* helpers (ambiguity, no-match, etc.).
Ptr<Decl> TypeChecker::TypeCheckerImpl::CheckUpperBoundTargetsCaseFuncCall(
    const ASTContext& ctx, MemberAccess& ma, const std::unordered_map<ModalTy, std::vector<Ptr<Decl>>>& allTargets)
{
    ma.targets.clear(); // Need clear before insertion.
    ma.foundUpperBoundMap =
        UpperBoundModalSetsToDataTy(GetUpperBoundTargetsWithGivenKind(allTargets, {ASTKind::FUNC_DECL}));
    std::vector<Ptr<Decl>> fdTargets = ma.baseExpr->GetTy()->IsPlaceholder()
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
        ma.matchedParentTy = {*ma.foundUpperBoundMap[fdTargets[0]].begin()};
        return fdTargets[0];
    } else {
        return nullptr;
    }
}

/// Upper-bound case for PropDecl targets. Like CheckUpperBoundTargetsCaseFuncCall but for properties: collect
/// candidates across upper bounds, dedup same-data-type decls via IsCloserToImpl, refill ma.targets with
/// PropDecl candidates (as Ptr<Decl>, NOT cast to FuncDecl), and set ma.matchedParentTy. Modal subset filtering
/// and accessor selection are deferred to the downstream InferStaticAccess generic branch, mirroring the
/// non-generic InferInstanceAccess path.
Ptr<Decl> TypeChecker::TypeCheckerImpl::CheckUpperBoundTargetsCaseProp(
    const ASTContext& ctx, MemberAccess& ma, const std::unordered_map<ModalTy, std::vector<Ptr<Decl>>>& allTargets)
{
    ma.targets.clear(); // Need clear before insertion.
    ma.foundUpperBoundMap =
        UpperBoundModalSetsToDataTy(GetUpperBoundTargetsWithGivenKind(allTargets, {ASTKind::PROP_DECL}));
    std::vector<Ptr<Decl>> tempTargets;
    // Check upper-bound members in a fixed order. Same (dataTy, modal) duplicates are already
    // rejected by PreCheck's CheckPropRedefinitionInGroup, so we will not encounter them here.
    // Different-modal same-dataTy props are LEGITIMATE overloads -- dedup by (dataTy, modal)
    // together, not by dataTy alone (otherwise we'd collapse valid prop overloads).
    OrderedDeclSet upperDecls;
    for (auto it : ma.foundUpperBoundMap) {
        upperDecls.emplace(it.first);
    }
    // Decls that share BOTH data type and modal must have only one valid implementation across upper
    // bounds; pick the closer-to-impl one (mirrors CheckUpperBoundTargetsCaseOthers but extended with
    // modal equality so distinct prop overloads survive).
    for (auto it : upperDecls) {
        auto found = std::find_if(tempTargets.begin(), tempTargets.end(),
            [this, &it](auto decl) {
                return typeManager.IsTyEqual(decl->GetTy(), it->GetTy()) &&
                    decl->TyMode() == it->TyMode();
            });
        if (found == tempTargets.end()) {
            tempTargets.emplace_back(it);
        } else if (IsCloserToImpl(*(*found), *it)) {
            *found = it;
        }
    }
    if (auto genTy = DynamicCast<GenericsTy>(ma.baseExpr->DataTy()); genTy && genTy->isPlaceholder) {
        FilterSumUpperbound(ctx, ma, *genTy, tempTargets, allTargets);
    }
    if (tempTargets.empty()) {
        diag.Diagnose(ma, ma.field.Begin(), DiagKind::sema_generic_no_member_match_in_upper_bounds);
        return nullptr;
    }
    // Dispatch the filter step the same way as CheckUpperBoundTargetsCaseFuncCall:
    // - GENERIC_PARAM_DECL base (static access via generic type) -> FilterAndCheckTargetsOfNameAccess
    // - otherwise (instance access) -> FilterAndGetTargetsOfObjAccess
    // Both helpers already correctly handle PROP_DECL by pushing Ptr<Decl> into ma.targets (no FuncDecl cast).
    auto target = ma.baseExpr->GetTarget();
    if (target && target->astKind == ASTKind::GENERIC_PARAM_DECL) {
        FilterAndCheckTargetsOfNameAccess(ctx, ma, tempTargets);
    } else {
        FilterAndGetTargetsOfObjAccess(ctx, ma, tempTargets);
    }
    if (tempTargets.empty()) {
        // Filter helpers have already reported a diagnostic.
        return nullptr;
    }
    std::sort(tempTargets.begin(), tempTargets.end(), CompNodeByPos);
    ma.matchedParentTy = {*ma.foundUpperBoundMap[tempTargets[0]].begin()};
    return tempTargets[0];
}

Ptr<Decl> TypeChecker::TypeCheckerImpl::CheckUpperBoundTargetsCaseOthers(
    const ASTContext& ctx, MemberAccess& ma, const std::unordered_map<ModalTy, std::vector<Ptr<Decl>>>& allTargets)
{
    ma.foundUpperBoundMap = UpperBoundModalSetsToDataTy(
        GetUpperBoundTargetsWithGivenKind(allTargets, {ASTKind::VAR_DECL}));
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
            [this, &it](auto decl) { return typeManager.IsTyEqual(decl->GetTy(), it->GetTy()); });
        if (found == tempTargets.end()) {
            tempTargets.emplace_back(it);
        } else if (IsCloserToImpl(*(*found), *it)) {
            *found = it;
        }
    }
    if (auto genTy = DynamicCast<GenericsTy>(ma.baseExpr->DataTy()); genTy && genTy->isPlaceholder) {
        FilterSumUpperbound(ctx, ma, *genTy, tempTargets, allTargets);
    }
    if (tempTargets.empty()) {
        diag.Diagnose(ma, ma.field.Begin(), DiagKind::sema_generic_no_member_match_in_upper_bounds);
        return nullptr;
    } else if (tempTargets.size() == 1 && ma.foundUpperBoundMap.find(tempTargets[0]) != ma.foundUpperBoundMap.end()) {
        // For non-function target, all types must be same, so just chose first one.
        ma.matchedParentTy = ModalTy{*ma.foundUpperBoundMap[tempTargets[0]].begin()};
        return As<ASTKind::DECL>(tempTargets[0]);
    } else {
        OrderedDeclSet candidates(tempTargets.cbegin(), tempTargets.cend());
        DiagAmbiguousUpperBoundTargets(diag, ma, candidates);
        return nullptr;
    }
}

void TypeChecker::TypeCheckerImpl::FilterSumUpperbound(const ASTContext& ctx, AST::MemberAccess& ma,
    AST::GenericsTy& tv, std::vector<Ptr<AST::Decl>>& targets,
    const std::unordered_map<ModalTy, std::vector<Ptr<Decl>>>& allTargets)
{
    auto& m = ctx.targetTypeMap;
    ModalTy tgtTy{};
    if (m.count(&ma) > 0) {
        tgtTy = m.at(&ma);
    }
    if (tgtTy && tgtTy->HasQuestTy()) {
        tgtTy = ModalTy{};
    }
    if (tgtTy) {
        std::map<Ptr<Decl>, ModalTy> decl2Ub;
        for (auto& [ub, decls] : allTargets) {
            for (auto d : decls) {
                decl2Ub[d] = ub;
            }
        }
        std::vector<Ptr<AST::Decl>> filteredTargets;
        ModalTy validTy{};
        for (auto d : targets) {
            auto ub = decl2Ub[d];
            // ub should never be filtered out
            if (!d->outerDecl || typeManager.constraints[&tv].ubs.count(ub) > 0) {
                filteredTargets.push_back(d);
                continue; // only filter out ub and targets from sum
            }
            InstCtxScope ic(*this);
            ic.SetRefDecl(*d->outerDecl, ub);
            auto instTy = typeManager.InstOf(d->GetTy());
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
        ma.SetTy({TypeManager::GetInvalidTy()});
        DiagMismatchedTypesWithFoundTy(diag, ma, tgtTy, ma.GetTy());
    }
}

bool TypeChecker::TypeCheckerImpl::FilterSumUpperbound(AST::MemberAccess& ma, AST::GenericsTy& tv, const AST::Decl& d)
{
    CJC_ASSERT(tv.isPlaceholder);
    auto hasMemOfTargetSig = [this, &ma, &d](ModalTy ty) -> bool {
        for (auto& [d2, ub] : ma.foundUpperBoundMap) {
            if (ub.count(ty.Ty()) > 0 && typeManager.IsTyEqual(d.GetTy(), d2->GetTy())) {
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
        if (!typeManager.IsTyEqual(ModalTy{&tv}, eq)) {
            return false;
        }
    }
    return true;
}

Ptr<Decl> TypeChecker::TypeCheckerImpl::GetIdealTypeFuncTargetFromExtend(
    const ASTContext& ctx, MemberAccess& ma, const ModalTy baseExprTy)
{
    CJC_NULLPTR_CHECK(ma.curFile);
    std::unordered_set<Ptr<Decl>> candidates;
    std::vector<ModalTy> targetTys;
    for (auto& tyKind : GetIdealTypesByKind(baseExprTy->kind)) {
        auto targets = ExtendFieldLookup(ctx, *ma.curFile, TypeManager::GetPrimitiveTy(tyKind), ma.field);
        for (auto& target : targets) {
            if (!target->TestAttr(AST::Attribute::STATIC)) {
                candidates.insert(target);
                targetTys.push_back(ModalTy{TypeManager::GetPrimitiveTy(tyKind)});
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
            target = GetObjMemberAccessTarget(ctx, ma, ma.baseExpr->GetTy());
        }
        if (Ty::IsTyCorrect(ma.baseExpr->GetTy()) && target == nullptr) {
            std::string tyStr = Ty::GetModalTypesToStr(targetTys, " ");
            diag.DiagnoseRefactor(DiagKindRefactor::sema_ambiguous_match_primitive_extend, ma, ma.field, tyStr);
        }
        return target;
    } else if (candidates.size() == 1) {
        ma.baseExpr->SetTy(targetTys.back());
        ma.target = *candidates.begin();
        if (ma.target->astKind == ASTKind::FUNC_DECL || ma.target->astKind == ASTKind::PROP_DECL) {
            ma.targets.push_back(ma.target);
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
    std::unordered_map<ModalTy, std::vector<Ptr<Decl>>> allTargets;
    for (auto& upperBound : allUpperBounds) {
        if (!Ty::IsTyCorrect(upperBound)) {
            continue;
        }
        auto targets = GetUpperBoundTargets(ctx, ma, ModalTy{upperBound}, isStaticAccess);
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
    }
    if (kind == ASTKind::PROP_DECL) {
        return CheckUpperBoundTargetsCaseProp(ctx, ma, allTargets);
    }
    return CheckUpperBoundTargetsCaseOthers(ctx, ma, allTargets);
}

Ptr<Decl> TypeChecker::TypeCheckerImpl::GetObjMemberAccessTarget(
    const ASTContext& ctx, MemberAccess& ma, ModalTy baseExprTy)
{
    // If the member access is 'this.xxx' and in original type decl, the members in extend should be ignored.
    auto re = DynamicCast<RefExpr>(ma.baseExpr.get());
    auto outerDecl = GetCurInheritableDecl(ctx, ma.scopeName);
    bool searchExtend = !re || !re->isThis || !outerDecl || outerDecl->astKind == ASTKind::EXTEND_DECL;
    CJC_NULLPTR_CHECK(ma.curFile);
    return match(*baseExprTy)(
        [this, &ctx, &ma, searchExtend](ClassTy& classTy) {
            LookupInfo info{{&classTy}, ma.curFile, true, searchExtend, ma.TestAttr(AST::Attribute::LEFT_VALUE)};
            auto targets = FieldLookup(ctx, classTy.declPtr, ma.field, info);
            return FilterAndGetTargetsOfObjAccess(ctx, ma, targets);
        },
        [this, &ctx, &ma](InterfaceTy& interfaceTy) {
            auto targets = FieldLookup(ctx, interfaceTy.declPtr, ma.field, {{&interfaceTy}, ma.curFile});
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
                ma.SetTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64)});
                auto literalExpr = CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(varrayTy.size), ma.GetTy());
                ma.desugarExpr = std::move(literalExpr);
                ma.desugarExpr->SetTy(ma.GetTy());
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
                auto maybeSol = typeManager.TryGreedySubst(ModalTy{&genericsTy});
                if (maybeSol && !maybeSol->IsPlaceholder()) {
                    ma.baseExpr->SetTy(maybeSol);
                    return GetObjMemberAccessTarget(ctx, ma, maybeSol);
                }
                auto& cst = typeManager.constraints[&genericsTy];
                genericsTy.upperBounds.clear();
                for (const auto& ub : cst.ubs) {
                    genericsTy.upperBounds.insert(ub.Ty());
                }
                for (const auto& s : cst.sum) {
                    genericsTy.upperBounds.insert(s.Ty());
                }
            }
            // Diagnose inside callee.
            return GetMemberAccessExposedTarget(ctx, ma, genericsTy, false);
        },
        [this, &ctx, &ma, &baseExprTy]() {
            if (baseExprTy->IsIdeal()) {
                return GetIdealTypeFuncTargetFromExtend(ctx, ma, baseExprTy);
            }
            auto targets = ExtendFieldLookup(ctx, *ma.curFile, baseExprTy.Ty(), ma.field);
            return FilterAndGetTargetsOfObjAccess(ctx, ma, targets);
        });
}

std::vector<Ptr<Decl>> TypeChecker::TypeCheckerImpl::GetUpperBoundTargets(
    const ASTContext& ctx, const MemberAccess& ma, ModalTy baseExprTy, const bool isStaticAccess)
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
    return match(*baseExprTy.Ty())(
        [this, &ctx, &ma, &filterTargets](ClassTy& classTy) {
            auto targets = FieldLookup(ctx, classTy.decl, ma.field, {ModalTy{&classTy}, ma.curFile});
            filterTargets(targets);
            return targets;
        },
        [this, &ctx, &ma, &filterTargets](InterfaceTy& interfaceTy) {
            auto targets = FieldLookup(ctx, interfaceTy.decl, ma.field, {ModalTy{&interfaceTy}, ma.curFile});
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
        bool isThisOrSuper = re->GetTy() && (re->isThis || re->isSuper);
        if (re->isThis) {
            bool accessStructLeftValue =
                ma.TestAttr(AST::Attribute::LEFT_VALUE) && re->GetTy() && re->GetTy()->IsStruct();
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
