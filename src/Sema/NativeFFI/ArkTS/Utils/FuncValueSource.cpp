// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the ArkTSFunc<F> use-site rules (B4b): the explicit
 * construction `ArkTSFunc<F>(f)` applies the function-type constraints with
 * their dedicated diagnostics and is lowered at the use site onto the
 * carrier's JSValue wrapping constructor through the generated `(ctx, info)`
 * lambda, and the direct call of a carrier value is refused with its
 * dedicated diagnostic.
 */

#include "FuncValueSource.h"

#include "../FunctionSource.h"

#include <string>
#include <unordered_map>
#include <utility>

#include "TypeCheckerImpl.h"
#include "Collector.h"
#include "DiagSuppressor.h"
#include "TypeCheckUtil.h"
#include "../BeforeBodyTypeCheck/PlaceholderBinding.h"
#include "ReferenceMembers.h"
#include "TypeMapper.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Parse/Parser.h"

using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI::ArkTS;

namespace Cangjie::Native::FFI::ArkTS {
namespace {
// The written spelling of `F` for the dedicated messages: the carrier's first
// type argument when it resolved to something printable, else the carrier
// spelling. Never the semantic name of the whole carrier: the user must see
// the argument they wrote.
std::string FuncCarrierArgumentSpelling(Ptr<Ty> carrierTy)
{
    if (Ty::IsTyCorrect(carrierTy) && !carrierTy->typeArgs.empty()) {
        auto fTy = carrierTy->typeArgs.front().Ty();
        if (Ty::IsTyCorrect(fTy)) {
            return Ty::ToString(fTy);
        }
    }
    return Ty::ToString(carrierTy);
}
} // namespace

// Route one refused carrier position to its dedicated diagnostic. The
// package pin inside the reason helper keeps every same-named user type on
// the generic unsupported-type routing; the position node is the written type
// when the caller has one.
bool DiagnoseArkTSFuncCarrierPosition(DiagnosticEngine& diag, Node& position, Ptr<Ty> ty)
{
    switch (ArkTSFuncCarrierRejectReason(ty)) {
        case ArkTSFuncCarrierReject::NOT_A_FUNCTION:
            diag.DiagnoseRefactor(
                DiagKindRefactor::sema_arkts_func_type_argument_invalid, position, FuncCarrierArgumentSpelling(ty));
            return true;
        case ArkTSFuncCarrierReject::UNSUPPORTED_MEMBER:
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_func_type_argument_unsupported, position,
                FuncCarrierArgumentSpelling(ty));
            return true;
        case ArkTSFuncCarrierReject::OUTER_GENERIC:
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_func_outer_generic_not_allowed, position,
                FuncCarrierArgumentSpelling(ty));
            return true;
        case ArkTSFuncCarrierReject::NONE:
            break;
    }
    return false;
}

namespace {
// The spliced callee is the one placeholder that never names a declaration:
// the walker finds the generated call and replaces its callee with the moved
// user function value (an AST value, never spliced text).
void SpliceArkTSFuncValue(Node& root, OwnedPtr<Expr>& userFunc, CallExpr*& splicedAt)
{
    Walker(&root, [&userFunc, &splicedAt](Ptr<Node> node) {
        auto call = DynamicCast<CallExpr*>(node);
        if (call == nullptr || splicedAt != nullptr || call->baseFunc == nullptr) {
            return VisitAction::WALK_CHILDREN;
        }
        auto callee = DynamicCast<RefExpr*>(call->baseFunc.get());
        if (callee != nullptr && callee->ref.identifier == ARKTS_FUNC_SPLICE_CALLEE) {
            call->baseFunc = std::move(userFunc);
            splicedAt = call;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

// Bind one classRefs slot recorded by the emitter. A RefType keeps the
// lowering contract (identifier swap plus the pending map for the support
// merge); a RefExpr is the value-position wrapping-constructor call and
// follows the function-wrapper rebinding, keeping the written type arguments
// for the checker's own instantiation pass when the declaration is generic.
void BindArkTSFuncSlot(Node& node, const std::unordered_map<std::string, Ptr<Decl>>& classRefs, Ptr<File> file)
{
    if (auto refType = DynamicCast<RefType*>(&node)) {
        std::unordered_map<Ptr<const Node>, Ptr<Decl>> pending;
        BindArkTSRefTypePlaceholder(*refType, classRefs, pending);
        return;
    }
    auto ref = DynamicCast<RefExpr*>(&node);
    if (ref == nullptr) {
        return;
    }
    auto found = classRefs.find(ref->ref.identifier.Val());
    if (found == classRefs.end()) {
        return;
    }
    ref->ref.identifier = found->second->identifier;
    ref->ref.target = found->second;
    ref->ref.targets = {found->second};
    if (ref->typeArguments.empty()) {
        ref->SetTy(found->second->GetTy());
    }
    ref->curFile = file;
}

// The owner declaration the generated subtree is reported under: the function
// body being checked. A missing owner (a global initializer or a top-level
// expression) skips the support pinning: the injected imports still resolve
// the names, only the speculative re-checks keep them pinned in the common
// (function body) case.
Ptr<Decl> FindConstructionOwner(ASTContext& ctx, CallExpr& ce)
{
    auto body = TypeCheckUtil::GetCurFuncBody(ctx, ce.scopeName);
    if (body == nullptr || body->funcDecl == nullptr) {
        return nullptr;
    }
    return body->funcDecl;
}

// The written shape of one explicit construction: the callee is a reference
// carrying one type argument (`F`) and is literally named `ArkTSFunc`; a
// same-named user class resolves elsewhere and keeps its ordinary meaning.
// Only the written spelling gates the construction syntax: this hook runs at
// the call-check entry, before the base check has assigned `callOrPattern`
// and resolved the reference, so the caller's speculative resolution would
// otherwise run without the call context the ordinary resolution relies on
// and answer a generic enum constructor sugar (`Some<T>(x)`) with a premature
// undeclared-identifier error.
RefExpr* ArkTSFuncConstructionCallee(CallExpr& ce)
{
    auto re = DynamicCast<RefExpr*>(ce.baseFunc.get());
    if (re == nullptr || re->typeArguments.size() != 1) {
        return nullptr;
    }
    if (re->ref.identifier.Val() != ARKTS_FUNC_CARRIER) {
        return nullptr;
    }
    return re;
}

} // namespace
} // namespace Cangjie::Native::FFI::ArkTS

// Parse the compiler-owned probe declaration and detach its return expression:
// `func __generated() { return ArkTSFunc<F>(..., ()) }`.
// The parse runs with diagnostics disabled and their capture discarded: this check sits inside the enclosing body
// check, whose suppressor would otherwise capture the parser's speculative `<` trial diagnostics (the generic-args
// probe for every `identifier <` comparison in the generated guards), because a diagnostic built while diagnostics
// are disabled is stored without ever reaching the parser's transaction rollback. The text is compiler-owned, so a
// genuine parse failure surfaces structurally: the probe below does not have the expected single-return shape and
// the construction is refused without a diagnostic of its own. Returns nullptr when the generated text or the parsed
// probe is not usable.
OwnedPtr<Expr> TypeChecker::TypeCheckerImpl::ParseArkTSFuncConstructionProbe(const ArkTSTypeDesc& desc,
    CallExpr& ce, std::unordered_map<std::string, Ptr<Decl>>& classRefs)
{
    auto code = ArkTSFuncConstructionSource(desc, "__cj_arkts_fc_", classRefs);
    if (code.empty()) {
        return nullptr;
    }
    bool parseWellFormed = false;
    OwnedPtr<Decl> generated;
    Ptr<FuncDecl> probe{nullptr};
    {
        DiagSuppressor parseDiags(diag);
        Parser parser(code, diag, ci->GetSourceManager(), ce.begin);
        parser.SetCurFile(ce.curFile);
        generated = parser.ParseDecl(ScopeKind::TOPLEVEL);
        probe = DynamicCast<FuncDecl*>(generated.get());
        parseWellFormed = probe != nullptr && !probe->TestAttr(Attribute::IS_BROKEN);
    }
    if (!parseWellFormed || probe == nullptr || probe->funcBody == nullptr || probe->funcBody->body == nullptr ||
        probe->funcBody->body->body.size() != 1) {
        return nullptr;
    }
    auto returned = DynamicCast<ReturnExpr*>(probe->funcBody->body->body.front().get());
    if (returned == nullptr || returned->expr == nullptr) {
        return nullptr;
    }
    return std::move(returned->expr);
}

// Lower one explicit construction: generate the glue, parse it, rebind its placeholders and splice positions, and hand
// the replacement to the checker as the call's desugaring.
bool TypeChecker::TypeCheckerImpl::LowerArkTSFuncConstruction(
    ASTContext& ctx, ModalTy target, CallExpr& ce, const ArkTSTypeDesc& desc)
{
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto replacement = ParseArkTSFuncConstructionProbe(desc, ce, classRefs);
    if (replacement == nullptr) {
        ce.SetTy({TypeManager::GetInvalidTy()});
        ce.callKind = CallKind::CALL_INVALID;
        return true;
    }
    // Positions, the file and the scope of every generated node are the call's; the classRefs slots bind now, before
    // the checker sees the subtree. The replacement root keeps the call's own range already. The user's own resolved
    // class reference (instantiated `ArkTSFunc<F>` with the written type argument) becomes the generated constructor
    // call's base, so the candidate match starts from exactly the state a written call reaches.
    Walker(replacement.get(), [&](Ptr<Node> node) {
        node->curFile = ce.curFile;
        node->begin = ce.begin;
        node->end = ce.end;
        node->scopeName = ce.scopeName;
        BindArkTSFuncSlot(*node, classRefs, ce.curFile);
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    if (auto glueCall = DynamicCast<CallExpr*>(replacement.get()); glueCall != nullptr) {
        auto userBase = DynamicCast<RefExpr*>(ce.baseFunc.get());
        auto glueBase = DynamicCast<NameReferenceExpr*>(glueCall->baseFunc.get());
        if (userBase != nullptr && glueBase != nullptr && !userBase->typeArguments.empty()) {
            glueCall->baseFunc = ASTCloner::Clone<Expr>(Ptr<Expr>(ce.baseFunc.get()));
        }
    }
    // Support names (the context entry, the conversion exception, BigInt and the carrier spelling) resolve through
    // the file's compiler-injected imports and are pinned for the speculative re-checks.
    auto owner = FindConstructionOwner(ctx, ce);
    if (owner != nullptr && !BindArkTSSupportReferences(*owner, *replacement, importManager, diag,
        arktsSupportRefs, owner->identifier.Val())) {
        ce.SetTy({TypeManager::GetInvalidTy()});
        ce.callKind = CallKind::CALL_INVALID;
        return true;
    }
    // Move the user function value in place of the spliced callee. The original argument slot is consumed (the desugar
    // contract moves the user expressions into the replacement, never clones them).
    OwnedPtr<Expr> userFunc = std::move(ce.args.front()->expr);
    CallExpr* splicedAt = nullptr;
    SpliceArkTSFuncValue(*replacement, userFunc, splicedAt);
    if (splicedAt == nullptr) {
        ce.SetTy({TypeManager::GetInvalidTy()});
        ce.callKind = CallKind::CALL_INVALID;
        return true;
    }
    ce.args.clear();
    CollectArkTSFuncReplacementSymbols(ctx, ce, *replacement);
    ResolveArkTSReplacementTypes(ctx, *replacement);
    ce.desugarExpr = std::move(replacement);
    return ChkDesugarExprOfCallExpr(ctx, target, ce);
}

// Collect the inserted subtree into the scope the call sits in (the enclosing body was already collected; without this
// the generated lambda's parameters and locals have no symbol entry).
void TypeChecker::TypeCheckerImpl::CollectArkTSFuncReplacementSymbols(
    ASTContext& ctx, CallExpr& ce, Expr& replacement)
{
    const auto oldScope = ctx.currentScopeName;
    const auto oldLevel = ctx.currentScopeLevel;
    ctx.currentScopeName = ce.scopeName;
    ctx.currentScopeLevel = ce.scopeLevel;
    Collector collector(scopeManager);
    auto previousSymbol = ctx.symbolTable.empty() ? ctx.symbolTable.end() : std::prev(ctx.symbolTable.end());
    collector.BuildSymbolTable(ctx, &replacement, ci->buildTrie);
    auto firstGenerated =
        previousSymbol == ctx.symbolTable.end() ? ctx.symbolTable.begin() : std::next(previousSymbol);
    for (auto it = firstGenerated; it != ctx.symbolTable.end(); ++it) {
        if (auto generatedDecl = DynamicCast<Decl*>((*it)->node); generatedDecl != nullptr) {
            ctx.AddDeclName(
                {(*it)->name, ScopeManagerApi::GetScopeNameWithoutTail((*it)->scopeName)}, *generatedDecl);
        }
    }
    ctx.currentScopeName = oldScope;
    ctx.currentScopeLevel = oldLevel;
}

// The replacement is generated while the enclosing body is already being checked: PreCheckUsage's ResolveNames has
// run and no pass will resolve the written types inside it afterwards. The collection above has created the string
// literals' Struct-String references (Collector::CollectLitConstExpr is the only place that builds them), so now every
// generated type position is resolved here with the same entry ResolveNames uses; the resolved clones under the
// generated constructor call keep their types.
void TypeChecker::TypeCheckerImpl::ResolveArkTSReplacementTypes(ASTContext& ctx, Expr& replacement)
{
    Walker(&replacement, [&ctx, this](Ptr<Node> node) {
        switch (node->astKind) {
            case ASTKind::REF_TYPE:
            case ASTKind::QUALIFIED_TYPE:
            case ASTKind::VARRAY_TYPE:
            case ASTKind::TUPLE_TYPE:
            case ASTKind::PAREN_TYPE:
            case ASTKind::FUNC_TYPE:
            case ASTKind::OPTION_TYPE:
            case ASTKind::PRIMITIVE_TYPE: {
                SetTypeTy(ctx, *StaticAs<ASTKind::TYPE>(node));
                return VisitAction::WALK_CHILDREN;
            }
            default:
                return VisitAction::WALK_CHILDREN;
        }
    }).Walk();
}

// The silent speculative base resolution of a candidate carrier construction:
// it mirrors the ordinary base check — the call is the reference's enclosing
// call, so the enum sugar and overload paths see the argument count like
// `ChkCallBaseRefExpr` would leave it — but no diagnostic may escape: one
// emitted here would precede the ordinary path's own verdict and turn into a
// user-visible error before a success. The call context (`callOrPattern`) is
// set for the attempt and put back afterwards; the reference keeps whatever
// resolution the attempt produced, and the caller restores the written state
// when the attempt did not resolve cleanly.
bool TypeChecker::TypeCheckerImpl::SynthesizeArkTSFuncCalleeOnce(ASTContext& ctx, CallExpr& ce, RefExpr& re)
{
    const auto savedCallOrPattern = re.callOrPattern;
    bool resolvedCleanly = false;
    {
        re.callOrPattern = &ce;
        DiagSuppressor suppressor(diag);
        (void)Synthesize({ctx, SynPos::EXPR_ARG}, ce.baseFunc.get());
        resolvedCleanly = !suppressor.HasError();
    }
    re.callOrPattern = savedCallOrPattern;
    return resolvedCleanly;
}

// Whether the resolved target of a candidate carrier construction is the ArkTS
// preset carrier class: the package pin applies to the resolved declaration,
// so a same-named user class keeps its ordinary meaning and this hook never
// touches it.
bool TypeChecker::TypeCheckerImpl::ResolvedToArkTSFuncCarrier(CallExpr& ce, RefExpr& re)
{
    auto callee = GetRealTarget(ce.baseFunc.get(), re.GetTarget());
    return callee != nullptr && callee->astKind == ASTKind::CLASS_DECL && IsArkTSPresetFunc(*callee);
}

// One accepted construction: the carrier's constraint refusals win over
// everything else — the dedicated diagnostics replace the generic
// unsupported-type message (design 4.2.1 e3/e4) — then the argument must be a
// function value of exactly F (the ordinary argument check keeps its mismatch
// diagnostics on the user node) and the construction is lowered at the use
// site. Returns the call's own well-typedness.
bool TypeChecker::TypeCheckerImpl::ApplyArkTSFuncConstruction(
    ASTContext& ctx, ModalTy target, CallExpr& ce, RefExpr& re, Ptr<Ty> carrierTy)
{
    auto desc = ArkTSClassifyType(carrierTy, &importManager);
    if (desc.kind != ArkTSTypeDesc::Kind::ARKTS_FUNC) {
        (void)DiagnoseArkTSFuncCarrierPosition(diag, *re.typeArguments.front(), carrierTy);
        ce.SetTy({TypeManager::GetInvalidTy()});
        ce.callKind = CallKind::CALL_INVALID;
        return true;
    }
    if (!Check(ctx, ModalTy{desc.funcTy}, ce.args.front()->expr.get())) {
        ce.SetTy({TypeManager::GetInvalidTy()});
        ce.callKind = CallKind::CALL_INVALID;
        return true;
    }
    return LowerArkTSFuncConstruction(ctx, target, ce, desc);
}

// Recognition, constraint checks and use-site rewrite of one explicit construction. Returns true when the call WAS the
// carrier constructor shape (the preset class name called with one type argument and one positional argument); the
// result is then the call's own well-typedness. Everything else returns false and continues the normal call check.
// The bodyless init(f: F) is removed at preset preparation, so a call that reaches the ordinary path never resolves
// to a bodyless target.
bool TypeChecker::TypeCheckerImpl::ChkArkTSFuncConstruction(ASTContext& ctx, ModalTy target, CallExpr& ce)
{
    auto re = ArkTSFuncConstructionCallee(ce);
    if (re == nullptr) {
        return false;
    }
    // Resolve the base reference before judging it (this hook runs at the call-check entry, before the base check
    // would have). The resolution is speculative: it stays silent, and on any failure the reference keeps exactly
    // the state it had here, so the ordinary base check redoes it with the call context in place and reports
    // whatever it finds.
    const auto savedTarget = re->ref.target;
    const auto savedTargets = re->ref.targets;
    const auto savedInstTys = re->instTys;
    const auto savedTy = re->GetTy();
    auto restoreRefState = [&re, &savedTarget, &savedTargets, &savedInstTys, &savedTy]() {
        re->ref.target = savedTarget;
        re->ref.targets = savedTargets;
        re->instTys = savedInstTys;
        re->SetTy(savedTy);
    };
    if (re->GetTarget() == nullptr || !Ty::IsTyCorrect(re->DataTy())) {
        if (!SynthesizeArkTSFuncCalleeOnce(ctx, ce, *re) || re->GetTarget() == nullptr
            || !Ty::IsTyCorrect(re->DataTy())) {
            restoreRefState();
            return false;
        }
    }
    if (!ResolvedToArkTSFuncCarrier(ce, *re)) {
        restoreRefState();
        return false;
    }
    // Every call naming the preset class is carrier construction syntax. The wrong shape (named argument — the stale
    // `init(f: F)` spelling — or no argument at all) is refused here; the generated two-argument calls
    // (`ArkTSFunc<F>(handle, ())`, from the wrapper's from-JS position) stay on the ordinary constructor path and
    // never reach this branch.
    if (ce.args.empty() || (ce.args.size() == 1 && !ce.args.front()->name.Empty())) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_func_construction_shape_invalid, ce);
        ce.SetTy({TypeManager::GetInvalidTy()});
        ce.callKind = CallKind::CALL_INVALID;
        return true;
    }
    if (ce.args.size() != 1) {
        restoreRefState();
        return false;
    }
    auto carrierTy = re->DataTy();
    if (!Ty::IsTyCorrect(carrierTy)) {
        restoreRefState();
        return false;
    }
    return ApplyArkTSFuncConstruction(ctx, target, ce, *re, carrierTy);
}

// The direct-call refusal: `cb(args)` where cb carries an ArkTS function reference is refused in this release
// (the per-signature call lowering is a later phase), with a dedicated diagnostic and no codegen path reached.
// The desugared `cb.()` shape arrives here after the base lookup found no call operator;
// the carrier type is resolved by now.
bool TypeChecker::TypeCheckerImpl::RefuseArkTSFuncValueCall(CallExpr& ce)
{
    auto ma = DynamicCast<MemberAccess*>(ce.baseFunc.get());
    if (ma == nullptr || ma->baseExpr == nullptr || ma->field.Val() != "()") {
        return false;
    }
    auto carrier = DynamicCast<ClassTy*>(ma->baseExpr->DataTy());
    if (carrier == nullptr || carrier->decl == nullptr || !IsArkTSPresetFunc(*carrier->decl)) {
        return false;
    }
    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_func_direct_call_not_allowed, ce, ma->baseExpr->ToString());
    ce.SetTy({TypeManager::GetInvalidTy()});
    ce.callKind = CallKind::CALL_INVALID;
    arkTSFuncValueCallRefused = true;
    return true;
}
