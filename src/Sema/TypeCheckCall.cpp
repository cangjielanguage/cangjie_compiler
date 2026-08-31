// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements typecheck apis for function CallExpr.
 */

#include "TypeCheckerImpl.h"

#include <algorithm>
#include <tuple>

#include "CJMP/MPTypeCheckerImpl.h"
#include "Desugar/DesugarInTypeCheck.h"
#include "DiagSuppressor.h"
#include "Diags.h"
#include "ExtraScopes.h"
#include "JoinAndMeet.h"
#include "LocalTypeArgumentSynthesis.h"
#include "TypeCheckUtil.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/RecoverDesugar.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/Macros.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace Sema;
using namespace TypeCheckUtil;
using namespace AST;

namespace {
ModalTy GetInoutTargetTy(const OwnedPtr<FuncArg>& arg)
{
    if (arg->expr->GetTy().IsCorrect()) {
        return arg->expr->GetTy();
    }
    CJC_ASSERT(arg->GetTy()->IsPointer() && arg->GetTy()->typeArgs.size() == 1);
    return arg->GetTy()->typeArgs[0];
}

// Parser guarantees CallExpr's args not contains nullptr.
// NOTE: 'arg.name' may be named for trailing closure, so we need using calculated 'argName'.
std::optional<size_t> CheckNameArgInParams(DiagnosticEngine& diag, const FuncArg& arg, const std::string& argName,
    const FuncParamList& list, const std::vector<bool>& marks, const FuncDecl& fd)
{
    // Size of list.params and marks are guaranteed by caller that will not out of bound here.
    // Traverse the parameters, find the parameter has same identifier with the named argument.
    for (size_t j = 0; j < list.params.size(); ++j) {
        CJC_NULLPTR_CHECK(list.params[j]);
        if (list.params[j]->identifier != argName) {
            continue;
        }
        // Named argument must passed to named parameter.
        if (!list.params[j]->isNamedParam) {
            diag.Diagnose(arg, DiagKind::sema_invalid_named_arguments, argName);
            return {};
        }
        if (marks[j]) {
            // When mark[j] is true, current arg's type has been added to argsTy already. A parameter
            // cannot be assigned two values. So return false.
            diag.Diagnose(arg, DiagKind::sema_multiple_named_argument, argName);
            return {};
        }
        return {j};
    }
    // Legacy CString(am: ptr) calls use arbitrary labels; accept any named arg for the single-param ctor.
    if (IsCStringConstructor(fd) && list.params.size() == 1) {
        if (marks[0]) {
            diag.Diagnose(arg, DiagKind::sema_multiple_named_argument, argName);
            return {};
        }
        return {0};
    }
    diag.Diagnose(arg, DiagKind::sema_unknown_named_argument, argName);
    return {};
}

void SortCallArgumentByParamOrder(const FuncDecl& fd, CallExpr& ce, std::vector<Ptr<AST::FuncArg>>& args)
{
    bool namedArgFound = false;
    size_t pos = 0;
    for (auto& arg : ce.args) {
        CJC_NULLPTR_CHECK(arg->expr);
        arg->expr->SetTy(arg->withInout ? GetInoutTargetTy(arg) : arg->GetTy());
        auto argName = GetArgName(fd, *arg);
        if (argName.empty()) {
            if (namedArgFound) {
                // Positional argument can not appear after named argument.
                break;
            }
            if (pos < args.size()) {
                args[pos] = arg.get();
            } else {
                // For varArg func.
                args.push_back(arg.get());
            }
            ++pos;
            continue;
        }
        namedArgFound = true;
        // Find the parameters whose name is the same as arguments. Then get the correct position where the
        // argument's type should be.
        bool bound = false;
        for (size_t j = 0; j < fd.funcBody->paramLists[0]->params.size(); ++j) {
            if (fd.funcBody->paramLists[0]->params[j]->identifier == argName) {
                args[j] = arg.get();
                bound = true;
                break;
            }
        }
        if (!bound && IsCStringConstructor(fd) && fd.funcBody->paramLists[0]->params.size() == 1) {
            args[0] = arg.get();
        }
    }
}

/**
 * Update callExpr's baseExpr's target and binding named funcArg with funcParam for LSP.
 * NOTE: This function should only be used if the 'fd' matches the callExpr.
 */
void UpdateCallTargetsForLSP(const CallExpr& ce, FuncDecl& fd)
{
    // Set callExpr's baseExpr decl (Caller guarantees only RefExpr or MemberAccess is possible here).
    ReplaceTarget(ce.baseFunc.get(), &fd);
    // Caller guarantees.
    CJC_ASSERT(fd.funcBody && !fd.funcBody->paramLists.empty());

    std::unordered_map<std::string, Ptr<FuncParam>> namedParams;
    for (auto& param : fd.funcBody->paramLists[0]->params) {
        if (param->isNamedParam) {
            namedParams.emplace(param->identifier, param.get());
        }
    }
    for (auto& arg : ce.args) {
        if (!arg->name.Empty() && namedParams.count(arg->name) != 0) {
            ReplaceTarget(arg.get(), namedParams[arg->name]);
        }
    }
}

// Because two candidate types of different PrimitiveTy means argument is ideal type,
// change primitive arg type to ideal type.
void ProcessParamToArg(TypeManager& tyMgr, ArgumentTypeUnit& atu)
{
    size_t argSize = atu.argTys.size(); // atu.argTys and atu.tysInArgOrder has same length.
    for (size_t t = 0; t < argSize; ++t) {
        auto& argTy = atu.argTys[t];
        auto& paramTy = atu.tysInArgOrder[t];
        CJC_ASSERT(argTy && paramTy);
        paramTy = tyMgr.InstOf(paramTy);
        if (argTy->IsInteger() && paramTy->IsNumeric()) {
            argTy = {TypeManager::GetPrimitiveTy(TypeKind::TYPE_IDEAL_INT)};
        } else if (argTy->IsFloating() && paramTy->IsNumeric()) {
            argTy = {TypeManager::GetPrimitiveTy(TypeKind::TYPE_IDEAL_FLOAT)};
        }
    }
}

// Caller guarantees call's baseFunc, and baseFunc's node members not null.
bool IsSuperCall(const CallExpr& ce)
{
    if (ce.baseFunc->astKind == ASTKind::REF_EXPR) {
        auto re = StaticAs<ASTKind::REF_EXPR>(ce.baseFunc.get());
        return re->isSuper;
    } else if (ce.baseFunc->astKind == ASTKind::MEMBER_ACCESS) {
        auto ma = StaticAs<ASTKind::MEMBER_ACCESS>(ce.baseFunc.get());
        if (ma->baseExpr == nullptr || ma->baseExpr->astKind != ASTKind::REF_EXPR) {
            return false;
        }
        auto reBase = StaticAs<ASTKind::REF_EXPR>(ma->baseExpr.get());
        return reBase->isSuper;
    }
    return false;
}

bool IsPossibleEnumConstructor(const std::vector<Ptr<FuncDecl>>& candidates, const Expr& baseExpr)
{
    if (baseExpr.astKind != ASTKind::REF_EXPR) {
        return false;
    }
    auto& re = static_cast<const RefExpr&>(baseExpr);
    if (!candidates.empty() && Is<EnumDecl>(candidates.front()->outerDecl)) {
        for (auto& target : re.ref.targets) {
            if (target->GetTy() == nullptr || target->TyKind() != TypeKind::TYPE_FUNC) {
                return true;
            }
        }
    }
    return false;
}

bool IsInGenericStructDecl(const FuncDecl& fd)
{
    return fd.outerDecl && fd.outerDecl->IsNominalDecl() && fd.outerDecl->generic;
}

// Get the generic types in candidate.
std::vector<ModalTy> GetAllGenericTys(const Expr& expr, const FuncDecl& fd)
{
    std::vector<ModalTy> result;
    std::vector<Ptr<Generic>> generics;
    if (fd.funcBody->generic) {
        // Get the candidate's generic types.
        generics.push_back(fd.funcBody->generic.get());
    }
    if (IsInGenericStructDecl(fd)) {
        // 1. Collect if the callee is memberAccess of decl's static member.
        // 2. Collect if candidate is constructor of struct/class/enum.
        bool isStaticMemberAccess = (expr.astKind == ASTKind::MEMBER_ACCESS) && fd.TestAttr(Attribute::STATIC);
        if (isStaticMemberAccess || IsClassOrEnumConstructor(fd)) {
            generics.push_back(fd.outerDecl->generic.get());
        }
    }
    for (auto& generic : generics) {
        for (auto& it : generic->typeParameters) {
            result.push_back(it->GetTy());
        }
    }
    return result;
}

/** Calculate enum target's scope level when using. */
SUPPRESS_WARNING("-Wunused-function")
int64_t CalculateEnumCtorScopeLevel(const ASTContext& ctx, const CallExpr& ce, const FuncDecl& fd)
{
    if (fd.TestAttr(Attribute::IMPORTED)) {
        return 0; // Imported enum constructor is treated as lifted to toplevel.
    }
    // If current context inside enum decl, return origin scope level.
    if (IsNode1ScopeVisibleForNode2(fd, ce)) {
        return static_cast<int64_t>(fd.scopeLevel);
    }
    // Else if current context inside enum decl's extend decl, return origin scope level.
    Symbol* symOfCurStruct = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, ce.symbol->scopeName);
    if (symOfCurStruct && symOfCurStruct->node && symOfCurStruct->node->astKind == ASTKind::EXTEND_DECL) {
        auto ed = RawStaticCast<ExtendDecl*>(symOfCurStruct->node);
        if (ed->extendedType && fd.outerDecl && Ty::GetDeclPtrOfTy(ed->extendedType->GetTy()) == fd.outerDecl) {
            return static_cast<int64_t>(fd.scopeLevel);
        }
    }
    // Otherwise return global scope level.
    return 0;
}
UNSUPPRESS_WARNING()

// Check whether given i types can passed to j types (T_i <: T_j).
bool CompareParamTys(TypeManager& tyMgr, const std::vector<ModalTy>& iTys, const std::vector<ModalTy>& jTys)
{
    if (iTys.size() != jTys.size()) {
        return false;
    }
    size_t argSize = iTys.size();
    for (size_t t = 0; t < argSize; ++t) {
        if (iTys[t]->IsNumeric() && jTys[t]->IsNumeric()) {
            ComparisonRes res = CompareIntAndFloat(*iTys[t], *jTys[t]);
            if (res == ComparisonRes::GT) {
                return false;
            } else {
                continue;
            }
        }
        if (!tyMgr.IsSubtype(iTys[t], jTys[t])) {
            return false;
        }
    }
    return true;
}

enum class OverloadCmp {
    BETTER,
    WORSE,
    EQUAL
};

// Check whether arg of i can be passed as arg of j (T_i <: T_j), or to say i is better than j for ctor call.
OverloadCmp CompareThisParamTy(
    TypeManager& tyMgr, const FuncDecl& i, const FuncDecl& j, const CallExpr& ce, ModalTy target)
{
    if (!tyMgr.HasThisParam(i) || !tyMgr.HasThisParam(j)) {
        return OverloadCmp::EQUAL;
    }
    auto iThis = tyMgr.GetThisParamTy(i);
    auto jThis = tyMgr.GetThisParamTy(j);
    if (tyMgr.ImplementsCopyInterface(iThis.Ty()) && tyMgr.ImplementsCopyInterface(jThis.Ty())) {
        return OverloadCmp::EQUAL;
    }
    // ctor call has special rules for modal, because the modal can be deduced.
    // in case let c: C@XXX = C @m(), use @m mode only. This rule is checked outside and not considered here.
    // in case let c: C@local? = C(), @local? is better, if no @local? is found, the other modals are equally good.
    // in case let c: C = C(), only @~local is accepted.
    // in case let c = C(), any mode is accepted, but prefer @~local for compatibility.
    if (ce.callKind == CallKind::CALL_OBJECT_CREATION || ce.callKind == CallKind::CALL_STRUCT_CREATION) {
        if (!ce.modal.HasLocal() && !Ty::IsTyCorrect(target)) {
            return OverloadCmp::EQUAL;
        }
        if (Ty::IsTyCorrect(target)) {
            ModalInfo refModal = ce.modal.HasLocal() ? ce.modal.ToModalInfo() : target.Mode();
            if (iThis.Mode() == refModal && jThis.Mode() != refModal) {
                return OverloadCmp::BETTER;
            }
            if (iThis.Mode() != refModal && jThis.Mode() == refModal) {
                return OverloadCmp::WORSE;
            }
            return OverloadCmp::EQUAL;
        }
    }
    if (iThis.Mode().IsSubModal(jThis.Mode())) {
        return OverloadCmp::BETTER;
    }
    return iThis.Mode() == jThis.Mode() ? OverloadCmp::EQUAL : OverloadCmp::WORSE;
}

std::vector<SubstPack> ResolveTypeMappings(
    TypeManager& tyMgr, const std::vector<SubstPack>& typeMappings, const FuncDecl& fd)
{
    CJC_ASSERT(fd.GetTy() && fd.TyKind() == TypeKind::TYPE_FUNC);
    auto mappingSize = typeMappings.size();
    std::vector<bool> matchMark(mappingSize, true); // When getting false, the type mapping is excluded.
    for (size_t i = 0; i < mappingSize; ++i) {
        if (!matchMark[i]) {
            continue;
        }
        for (size_t j = i + 1; j < mappingSize; ++j) {
            if (!matchMark[j]) {
                continue;
            }
            auto iTy = DynamicCast<FuncTy>(tyMgr.ApplySubstPack(fd.GetTy(), typeMappings[i]).Ty());
            auto jTy = DynamicCast<FuncTy>(tyMgr.ApplySubstPack(fd.GetTy(), typeMappings[j]).Ty());
            if (iTy == jTy) {
                matchMark[j] = false;
                continue;
            }
            CJC_NULLPTR_CHECK(iTy);
            CJC_NULLPTR_CHECK(jTy);
            auto iToJ = CompareParamTys(tyMgr, iTy->paramTys, jTy->paramTys);
            auto jToI = CompareParamTys(tyMgr, jTy->paramTys, iTy->paramTys);
            if (iToJ && !jToI) {
                matchMark[j] = false;
            } else if (!iToJ && jToI) {
                matchMark[i] = false;
                break;
            }
        }
    }
    std::vector<SubstPack> resMappings;
    for (size_t i = 0; i < matchMark.size(); ++i) {
        if (matchMark[i]) {
            resMappings.push_back(typeMappings[i]);
        }
    }
    return resMappings;
}

void DiagnoseForMultiMapping(DiagnosticEngine& diag, const CallExpr& ce, const FuncDecl& fd,
    const std::vector<SubstPack>& typeMappings, const std::vector<SubstPack>& resMappings)
{
    // Do not know how to skip the whole type checking process at some earlier stage for function calls. Patch here.
    bool shouldDiag = std::none_of(ce.args.cbegin(), ce.args.cend(), [](const OwnedPtr<FuncArg>& farg) {
        return farg && farg->expr && farg->expr->GetTy() && farg->expr->GetTy()->IsInvalid();
    });
    if (!shouldDiag) {
        return;
    }
    if (resMappings.empty()) {
        diag.Diagnose(ce, DiagKind::sema_parameters_and_arguments_mismatch);
    } else if (resMappings.size() > typeMappings.size()) {
        diag.Diagnose(ce, DiagKind::sema_generic_ambiguous_method_match_in_upper_bounds, fd.identifier.Val());
    } else {
        diag.Diagnose(ce, DiagKind::sema_ambiguous_arg_type);
    }
}

bool IsFuncDeclPossibleVariadic(const FuncDecl& fd)
{
    // enum constructors cannot be variadic.
    if (fd.TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
        return false;
    }
    // Operator overloading except for `()`, `[]` cannot be variadic.
    if (fd.identifier != "()" && fd.identifier != "[]" && IsFieldOperator(fd.identifier)) {
        return false;
    }
    CJC_ASSERT(fd.funcBody && !fd.funcBody->paramLists.empty());
    auto& params = fd.funcBody->paramLists.front()->params;
    auto iter = std::find_if(params.crbegin(), params.crend(), [](auto& param) {
        CJC_NULLPTR_CHECK(param);
        return !param->isNamedParam;
    });
    return iter != params.crend() && (*iter)->GetTy().IsCorrect() && (*iter)->GetTy()->IsStructArray();
}

size_t GetPositionalParamSize(const FuncDecl& fd)
{
    CJC_ASSERT(fd.funcBody && !fd.funcBody->paramLists.empty());
    auto& params = fd.funcBody->paramLists.front()->params;
    size_t namedSize = 0;
    for (auto iter = params.crbegin(); iter < params.crend(); ++iter) {
        CJC_NULLPTR_CHECK(*iter);
        if ((*iter)->isNamedParam) {
            namedSize++;
        } else {
            break;
        }
    }
    return params.size() - namedSize;
}

size_t GetInitializedlNameParamSize(const FuncDecl& fd)
{
    CJC_ASSERT(fd.funcBody && !fd.funcBody->paramLists.empty());
    auto& params = fd.funcBody->paramLists.front()->params;
    size_t result = 0;
    for (auto iter = params.crbegin(); iter < params.crend(); ++iter) {
        CJC_NULLPTR_CHECK(*iter);
        if ((*iter)->isNamedParam) {
            result += (*iter)->TestAttr(Attribute::HAS_INITIAL);
        } else {
            break;
        }
    }
    return result;
}

size_t GetPositionalArgSize(const CallExpr& ce)
{
    size_t count = 0;
    for (auto& arg : ce.args) {
        if (!arg->name.Empty()) {
            break;
        }
        count++;
    }
    return count;
}

// Caller guarantees the fd is not a function with variable length argument.
bool HasSamePositionalArgsSize(const FuncDecl& fd, const CallExpr& ce)
{
    // Legacy CString(am: ptr) uses an arbitrary label for the single pointer argument.
    if (IsCStringConstructor(fd) && ce.args.size() == 1 && !ce.args[0]->name.Empty()) {
        return GetPositionalParamSize(fd) == 1;
    }
    size_t positionalParamSize = GetPositionalParamSize(fd);
    size_t positionalArgSize = GetPositionalArgSize(ce);
    if (!ce.args.empty() && ce.args.back()->TestAttr(Attribute::IMPLICIT_ADD) &&
        !fd.funcBody->paramLists[0]->params.empty() &&
        fd.funcBody->paramLists[0]->params.back()->isNamedParam && positionalArgSize == ce.args.size()) {
        // For trailing closure argument, if the original func param is named and no other named argument existed,
        // it should be treated as named argument.
        --positionalArgSize;
    }
    return positionalParamSize == positionalArgSize;
}

bool IsCallSourceExprVariadic(const CallExpr& ce)
{
    // A desugarred variadic function call cannot be variadic.
    return ce.sourceExpr && ce.sourceExpr->astKind == ASTKind::CALL_EXPR &&
        StaticAs<ASTKind::CALL_EXPR>(ce.sourceExpr)->callKind == CallKind::CALL_VARIADIC_FUNCTION;
}

// Let `v` be the size of the varargs in `ce`, we have `positionalArgSize = positionalParamSize - 1 + v`.
// If it is compatible, then `v >= 0`, i.e. `positionalArgSize + 1 - positionalParamSize >= 0`.
// This is the so called "variadic inequality".
bool AreFuncDeclAndCallExprFullfillVariadicInequality(const FuncDecl& fd, const CallExpr& ce)
{
    size_t positionalArgSize = GetPositionalArgSize(ce);
    size_t positionalParamSize = GetPositionalParamSize(fd);
    return positionalArgSize + 1 >= positionalParamSize;
}

bool IsPossibleVariadicFunction(const FuncDecl& fd, const CallExpr& ce)
{
    return !IsCallSourceExprVariadic(ce) && IsFuncDeclPossibleVariadic(fd) &&
        AreFuncDeclAndCallExprFullfillVariadicInequality(fd, ce);
}

bool IsPossibleVariadicFunction(const std::vector<Ptr<FuncDecl>>& candidates, const CallExpr& ce)
{
    return Utils::In(candidates, [&ce](Ptr<const FuncDecl> fd) {
        return fd != nullptr && !IsCallSourceExprVariadic(ce) && IsFuncDeclPossibleVariadic(*fd) &&
            AreFuncDeclAndCallExprFullfillVariadicInequality(*fd, ce);
    });
}

// This function is used to check whether a function typed expression could be variadic.
bool IsPossibleVariadicFunction(const FuncTy& funcTy, const CallExpr& ce)
{
    // `hasVariableLenArg` indicates the `funcTy` is a variadic C FFI function,
    // it cannot be a Cangjie variadic function at the same time.
    if (funcTy.hasVariableLenArg || funcTy.paramTys.empty() || IsCallSourceExprVariadic(ce) ||
        GetPositionalArgSize(ce) + 1 < funcTy.paramTys.size()) {
        return false;
    }
    return Ty::IsTyCorrect(funcTy.paramTys.back()) && funcTy.paramTys.back()->IsStructArray();
}

inline ModalTy GetRealNonOptionTy(ModalTy ty)
{
    if (ty->IsCoreOptionType()) {
        return GetRealNonOptionTy(ty->typeArgs[0]);
    }
    return ty;
}

std::vector<Ptr<FuncDecl>> SyntaxFilterCandidates(const CallExpr& ce, const std::vector<Ptr<FuncDecl>>& candidates)
{
    std::vector<Ptr<FuncDecl>> definitelyMismatched;
    std::vector<Ptr<FuncDecl>> mismatched;
    std::vector<Ptr<FuncDecl>> matched;
    // Filter the candidates by lambda arguments and associated parameter types.
    for (auto fd : candidates) {
        bool mismatch = false;
        bool definitelyMismatch = false;
        auto paramTys = GetParamTys(*fd);
        for (size_t i = 0; i < ce.args.size(); i++) {
            auto lamArg = DynamicCast<LambdaExpr>(ce.args[i]->expr.get());
            if (!lamArg) {
                continue;
            } else if (ce.args[i]->TestAttr(Attribute::IMPLICIT_ADD)) {
                // For trailing closure, checking the argument with last parameter type,
                // ignore the mismatched size of arguments.
                definitelyMismatch = paramTys.empty() || !paramTys.back()->IsFunc();
                break;
            } else if (ce.args[i]->name.Empty() && paramTys.size() <= i) {
                definitelyMismatch = true;
                break;
            }
            ModalTy paramTy{};
            if (ce.args[i]->name.Empty()) {
                paramTy = GetRealNonOptionTy(paramTys[i]);
            } else if (auto res = GetParamTyAccordingToArgName(*fd, ce.args[i]->name)) {
                paramTy = GetRealNonOptionTy(res.value().first);
            }
            // Do not use size of lambda's parameters as filter condition, since it may be better matched than others.
            if (!paramTy ||
                !Utils::In(paramTy->kind, {TypeKind::TYPE_FUNC, TypeKind::TYPE_ANY, TypeKind::TYPE_GENERICS})) {
                mismatch = true;
            }
        }
        definitelyMismatch
            ? definitelyMismatched.emplace_back(fd)
            : (mismatch ? mismatched.emplace_back(fd) : matched.emplace_back(fd));
    }
    // Should not return empty vector for future diagnoses.
    return matched.empty() ? (mismatched.empty() ? definitelyMismatched : mismatched) : matched;
}

bool ChkArgsOrder(DiagnosticEngine& diag, const CallExpr& ce)
{
    bool foundNamedArg = false;
    for (auto& arg : ce.args) {
        if (foundNamedArg) {
            if (arg->name.Empty()) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_unordered_arguments, *arg);
                return false;
            }
        } else if (!arg->name.Empty()) {
            foundNamedArg = true;
        }
    }
    return true;
}

std::map<size_t, std::vector<Ptr<FuncDecl>>> FuncDeclsGroupByFixedPositionalArity(
    const std::vector<Ptr<FuncDecl>>& candidates, const CallExpr& ce)
{
    std::map<size_t, std::vector<Ptr<FuncDecl>>> fixedPositionalArityToGroup;
    for (auto fd : candidates) {
        CJC_NULLPTR_CHECK(fd);
        if (!fd || !IsPossibleVariadicFunction(*fd, ce)) {
            continue;
        }
        size_t fixedPositionalArity = GetPositionalParamSize(*fd) - 1;
        const auto iter = fixedPositionalArityToGroup.find(fixedPositionalArity);
        if (iter != fixedPositionalArityToGroup.cend()) {
            iter->second.emplace_back(fd);
        } else {
            std::vector<Ptr<FuncDecl>> group = {fd};
            fixedPositionalArityToGroup.emplace(fixedPositionalArity, group);
        }
    }
    return fixedPositionalArityToGroup;
}

inline bool IsSearchTargetInMemberAccessUpperBound(const CallExpr& ce)
{
    if (auto ma = DynamicCast<MemberAccess*>(ce.baseFunc.get())) {
        return ma->isExposedAccess;
    }
    return false;
}

std::pair<bool, TypeSubst> CheckAndObtainTypeMappingBetweenInterfaceAndExtend(
    const ExtendDecl& ed, const InterfaceDecl& id)
{
    bool flag = false;
    TypeSubst typeMapping;
    for (auto& super : ed.inheritedTypes) {
        if (!super->GetTy()->IsInterface()) {
            continue;
        }
        auto superTy = RawStaticCast<InterfaceTy*>(super->DataTy());
        if (&id != Ty::GetDeclPtrOfTy(DataTy{superTy})) {
            continue;
        }
        typeMapping = TypeCheckUtil::GenerateTypeMapping(id, superTy->TyArgs());
        if (typeMapping.empty()) {
            continue; // Failed to generate typeMapping.
        }
        flag = true;
        break;
    }
    return {flag, typeMapping};
}

void RemoveNonAnnotationCandidates(std::vector<Ptr<FuncDecl>>& candidates)
{
    for (auto iter = candidates.begin(); iter != candidates.end();) {
        CJC_NULLPTR_CHECK(*iter);
        bool isCustomAnnotation = IsInstanceConstructor(**iter) && (*iter)->TestAttr(Attribute::IN_CLASSLIKE) &&
            (*iter)->outerDecl && (*iter)->outerDecl->TestAttr(Attribute::IS_ANNOTATION);
        if (isCustomAnnotation) {
            ++iter;
        } else {
            iter = candidates.erase(iter);
        }
    }
}

inline bool IsFunctionalType(ModalTy ty)
{
    if (auto genericTy = DynamicCast<GenericsTy*>(ty.Ty())) {
        return std::any_of(
            genericTy->upperBounds.cbegin(), genericTy->upperBounds.cend(), [](auto ub) { return ub->IsFunc(); });
    }
    return ty->IsFunc();
}

bool CheckUseTrailingClosureWithFunctionType(
    DiagnosticEngine& diag, FuncArg& arg, ModalTy originalTy, const FuncDecl& candidate)
{
    // Implicit added func argument is added by trailing closure.
    if (!arg.TestAttr(Attribute::IMPLICIT_ADD) || IsFunctionalType(originalTy)) {
        return true;
    }
    arg.SetTy({TypeManager::GetInvalidTy()});
    CJC_NULLPTR_CHECK(arg.expr);
    arg.expr->SetTy({TypeManager::GetInvalidTy()});
    std::string message =
        originalTy->IsGeneric() ? "generic type without upper bound of function type" : "non-function type";
    auto diagBuilder = diag.DiagnoseRefactor(
        DiagKindRefactor::sema_trailing_lambda_cannot_used_for_non_function, arg, message);
    diagBuilder.AddMainHintArguments(originalTy.String());
    diagBuilder.AddNote(candidate, MakeRangeForDeclIdentifier(candidate), "found candidate");
    return false;
}

/// For `let c = C()`: drop ctor overloads whose `this` is @local when an @~local ctor exists.
/// When it is not ctor call, or target exists, do not call this fun.
void FilterBetterThisModeCtorCall(TypeManager& tm,
    const std::vector<OwnedPtr<FunctionMatchingUnit>>& candidates, std::vector<bool>& targetMark)
{
    bool hasNotLocalThisCtor = false;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (!targetMark[i]) {
            continue;
        }
        if (tm.HasThisParam(candidates[i]->fd) && !tm.GetThisParamTy(candidates[i]->fd).IsLocalType()) {
            hasNotLocalThisCtor = true;
            break;
        }
    }
    if (!hasNotLocalThisCtor) {
        return;
    }
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (!targetMark[i] || !tm.HasThisParam(candidates[i]->fd)) {
            continue;
        }
        if (tm.GetThisParamTy(candidates[i]->fd).IsLocalType()) {
            targetMark[i] = false;
        }
    }
}

void FilterOverriden(
    TypeManager& tm, std::vector<OwnedPtr<FunctionMatchingUnit>>& candidates, std::vector<bool>& targetMark)
{
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (!candidates[i]->fd.outerDecl) {
            return;
        }
        auto instOuter1 = tm.ApplySubstPack(candidates[i]->fd.outerDecl->DataTy(), candidates[i]->typeMapping);
        if (!Ty::IsTyCorrect(instOuter1)) {
            return;
        }
        auto instFuncTy1 =
            StaticCast<FuncTy*>(tm.ApplySubstPack(candidates[i]->fd.DataTy(), candidates[i]->typeMapping));
        auto& funcBody1 = candidates[i]->fd.funcBody;
        for (size_t j = 0; j < candidates.size(); ++j) {
            if (!candidates[j]->fd.outerDecl || i == j) {
                continue;
            }
            auto instOuter2 = tm.ApplySubstPack(candidates[j]->fd.outerDecl->DataTy(), candidates[j]->typeMapping);
            // Do not handle overloaded candidates.-
            if (!Ty::IsTyCorrect(instOuter2) || instOuter1 == instOuter2) {
                continue;
            }
            auto instFuncTy2 =
                StaticCast<FuncTy*>(tm.ApplySubstPack(candidates[j]->fd.DataTy(), candidates[j]->typeMapping));
            auto& funcBody2 = candidates[j]->fd.funcBody;
            auto sameSig = instFuncTy1 == instFuncTy2;
            sameSig = sameSig && ((funcBody1->generic == nullptr) == (funcBody2->generic == nullptr));
            if (sameSig && funcBody1->generic) {
                sameSig = funcBody1->generic->typeParameters.size() == funcBody2->generic->typeParameters.size();
                auto mappingOf2Func = GenerateTypeMappingBetweenFuncs(tm, candidates[i]->fd, candidates[j]->fd);
                auto subFuncs = tm.GetInstantiatedTys(candidates[i]->fd.GetTy(), mappingOf2Func);
                sameSig = sameSig && Utils::In(candidates[j]->fd.GetTy(), subFuncs);
            }
            if (tm.IsSubtype(instOuter1, instOuter2) && sameSig) {
                targetMark[j] = false;
                break;
            }
        }
    }
}
} // namespace

bool TypeChecker::TypeCheckerImpl::CheckArgsWithParamName(const CallExpr& ce, const FuncDecl& fd)
{
    // Record the type of the default parameter first.
    std::vector<bool> hasTy;
    CJC_ASSERT(fd.funcBody && !fd.funcBody->paramLists.empty());
    for (auto& param : fd.funcBody->paramLists[0]->params) {
        hasTy.push_back(param->TestAttr(Attribute::HAS_INITIAL));
    }
    size_t funcDeclParamListLen = fd.funcBody->paramLists[0]->params.size();
    // Help to mark the arguments whose type have been checked as valid.
    std::vector<bool> marks(funcDeclParamListLen, false);
    // Help to record whether named argument has been appear.
    bool namedArgFound = false;
    size_t pos = 0;
    for (size_t i = 0; i < ce.args.size(); ++i) {
        CJC_NULLPTR_CHECK(ce.args[i]); // Parser guarantees CallExpr's args not contains nullptr.
        size_t index = 0;
        std::string argName = GetArgName(fd, *ce.args[i]);
        if (argName.empty()) {
            // Positional argument can not appear after named argument.
            if (namedArgFound) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_unordered_arguments, *ce.args[i]);
                return false;
            }
            // Named parameter must be passed with named argument.
            if (pos < funcDeclParamListLen && fd.funcBody->paramLists[0]->params[pos]->isNamedParam) {
                DiagNeedNamedArgument(diag, ce, fd, pos, i);
                return false;
            }
            index = pos;
            ++pos;
        } else {
            auto res = CheckNameArgInParams(diag, *ce.args[i], argName, *fd.funcBody->paramLists[0], marks, fd);
            if (res.has_value()) {
                index = res.value();
            } else {
                return false;
            }
            namedArgFound = true;
        }
        if (index < funcDeclParamListLen) {
            marks[index] = true;
            hasTy[index] = true;
        }
    }
    for (auto it : std::as_const(hasTy)) {
        if (it) {
            continue;
        }
        if (ce.sourceExpr == nullptr) {
            DiagWrongNumberOfArguments(diag, ce, fd);
        }
        return false;
    }
    return true;
}

// Caller guarantees call's baseFunc, and baseFunc's node members not null.
bool TypeChecker::TypeCheckerImpl::IsGenericCall(const ASTContext& ctx, const CallExpr& ce, const FuncDecl& fd) const
{
    // 1. If fd has generic types, return true.
    bool hasGeneric = fd.TestAttr(Attribute::GENERIC) || (fd.GetTy() && fd.GetTy()->HasGeneric());
    if (hasGeneric) {
        return true;
    }
    // 2. If the caller has type arguments, return true.
    if (!ce.baseFunc->GetTypeArgs().empty()) {
        return true;
    }
    // 3. Generic class's constructor may not be a generic function with type parameters. So we should check its outer
    // class when call type is super call.
    if (IsSuperCall(ce) && fd.funcBody->parentClassLike && fd.funcBody->parentClassLike->generic) {
        return true;
    }
    // 4. If the call's baseFunc is a member access, and the base's type has generic, return true;
    if (ce.baseFunc->astKind == ASTKind::MEMBER_ACCESS) {
        auto ma = StaticAs<ASTKind::MEMBER_ACCESS>(ce.baseFunc.get());
        auto baseType = ma->baseExpr ? ma->baseExpr->GetTy() : ModalTy{};
        if (!Ty::IsTyCorrect(baseType)) {
            return false;
        }
        auto decl = Ty::GetDeclPtrOfTy(baseType);
        // Check whether decl is generic
        if (decl && decl->TestAttr(Attribute::GENERIC) &&
            (decl->astKind != ASTKind::ENUM_DECL || fd.TestAttr(Attribute::ENUM_CONSTRUCTOR) ||
                fd.TestAttr(Attribute::STATIC))) {
            return true;
        }
        // 5. If the target function is a member of generic structure decl, and the base's extends/implements the
        // decl, return true.
        if (IsInGenericStructDecl(fd)) {
            return true;
        }
    }
    // 6. If current callExpr is in a classlike decl, and the target function is decl's super classlike, and the super
    // classlike is a generic decl.
    if (ce.baseFunc->astKind == ASTKind::REF_EXPR) {
        auto sym1 = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, ce.scopeName);
        auto sym2 = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, fd.scopeName);
        if (sym1 != sym2 && sym2 != nullptr && sym2->node->GetTy().IsCorrect() &&
            !sym2->node->GetTy()->typeArgs.empty()) {
            return true;
        }
    }
    return false;
}

bool TypeChecker::TypeCheckerImpl::IsInterfaceFuncWithSameSignature(
    const FunctionMatchingUnit& i, const FunctionMatchingUnit& j)
{
    // Check whether both candidates are abstract interface function with same signature.
    bool bothInInterface = i.fd.outerDecl && i.fd.outerDecl->astKind == ASTKind::INTERFACE_DECL && j.fd.outerDecl &&
        j.fd.outerDecl->astKind == ASTKind::INTERFACE_DECL && i.fd.TestAttr(Attribute::ABSTRACT) &&
        j.fd.TestAttr(Attribute::ABSTRACT);
    if (bothInInterface) {
        if (TypeManager::HasThisParam(i.fd) && TypeManager::HasThisParam(j.fd)) {
            if (GetThisParamModal(i.fd) != GetThisParamModal(j.fd)) {
                return false;
            }
        }
        auto iTy = typeManager.ApplySubstPack(i.fd.GetTy(), i.typeMapping);
        auto jTy = typeManager.ApplySubstPack(j.fd.GetTy(), j.typeMapping);
        return iTy == jTy;
    }
    return false;
}

bool TypeChecker::TypeCheckerImpl::CompareFuncCandidates(
    FunctionMatchingUnit& i, FunctionMatchingUnit& j, const CallExpr& ce, ModalTy target)
{
    size_t argSize = i.tysInArgOrder.size();
    // Only resolve for generic when candidate is generic related and its parameter's types contains generic type.
    bool isGeneric = GetCurrentGeneric(j.fd, ce) != nullptr;
    if (isGeneric && Utils::In(j.tysInArgOrder, [](auto ty) { return ty->HasGeneric(); })) {
        InstCtxScope ic(*this);
        ic.SetRefDeclSimple(j.fd, ce);
        ArgumentTypeUnit atu(i.tysInArgOrder, j.tysInArgOrder, {});
        ProcessParamToArg(typeManager, atu);
        auto argPack = LocTyArgSynArgPack{GetTyVarsToSolve(typeManager.GetInstMapping()), atu.argTys, atu.tysInArgOrder,
            {}, {TypeManager::GetInvalidTy()}, {TypeManager::GetInvalidTy()}, {}};
        auto optSubst = LocalTypeArgumentSynthesis(typeManager, argPack, {}).SynthesizeTypeArguments();
        if (!optSubst.has_value()) {
            return false;
        }
        for (size_t t = 0; t < argSize; ++t) { // Int64 has priority than float and other int.
            if (!i.tysInArgOrder[t].Mode().IsSubModal(j.tysInArgOrder[t].Mode()) &&
                !typeManager.ImplementsCopyInterface(i.tysInArgOrder[t].Ty())) {
                return false;
            }
            if (!i.tysInArgOrder[t]->IsNumeric() || !j.tysInArgOrder[t]->IsNumeric()) {
                continue;
            }
            ComparisonRes res = CompareIntAndFloat(*i.tysInArgOrder[t], *j.tysInArgOrder[t]);
            if (res == ComparisonRes::GT) {
                return false;
            }
        }
        return CompareThisParamTy(typeManager, i.fd, j.fd, ce, target) != OverloadCmp::WORSE;
    }
    if (i.fd.outerDecl && j.fd.outerDecl) {
        // Since interface allows multiple inheritance, and have default functions, we need to resolve shadow case.
        // When both candidates are member functions and they have the same size of parameters and function signatures,
        // resolve them by deciding whether one is implementing the other.
        bool sameNumberOfParams = j.fd.GetTy()->typeArgs.size() == i.fd.GetTy()->typeArgs.size();
        bool sameThisParam{true};
        if (typeManager.HasThisParam(i.fd) && typeManager.HasThisParam(j.fd)) {
            sameThisParam = GetThisParamModal(i.fd) == GetThisParamModal(j.fd);
        }
        if (sameNumberOfParams && sameThisParam &&
            typeManager.IsFuncParameterTypesIdentical(i.tysInArgOrder, j.tysInArgOrder)) {
            bool isJImplementable =
                j.fd.outerDecl->astKind == ASTKind::INTERFACE_DECL || j.fd.TestAttr(Attribute::ABSTRACT);
            bool isITheImplementation =
                i.fd.outerDecl->astKind != ASTKind::INTERFACE_DECL && !i.fd.TestAttr(Attribute::ABSTRACT);
            return isJImplementable && isITheImplementation;
        }
    }
    // Check whether T_i <: T_j
    return CompareParamTys(typeManager, i.tysInArgOrder, j.tysInArgOrder) &&
        CompareThisParamTy(typeManager, i.fd, j.fd, ce, target) != OverloadCmp::WORSE;
}

/// For non-ctor call, do these in such order.
/// 1) keep only exact this arg mode to this param.
/// 2) if candidates still non-empty, keep this arg mode that is submode of this param.
/// 3) if this arg mode is not exact/submode of any this param and is copy type, keep all.
/// All these happen before param type subtyping.
void TypeChecker::TypeCheckerImpl::FilterBetterThisModeNonCtorCall(const ASTContext& ctx, const CallExpr& ce,
    const std::vector<OwnedPtr<FunctionMatchingUnit>>& candidates, std::vector<bool>& targetMark) const
{
    auto baseFunc = DynamicCast<NameReferenceExpr>(ce.baseFunc.get());
    if (!baseFunc || candidates.empty()) {
        return;
    }
    auto thisArgTy = GetReceiverTy(ctx, *baseFunc);
    auto argMode = thisArgTy.Mode();

    // Classify this-param candidates by their modal relation to the receiver.
    bool hasExact{false};
    bool hasSubmode{false};
    bool hasThisParam{false};
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (!targetMark[i] || !typeManager.HasThisParam(candidates[i]->fd)) {
            continue;
        }
        hasThisParam = true;
        auto paramMode = typeManager.GetThisParamTy(candidates[i]->fd).Mode();
        if (argMode == paramMode) {
            hasExact = true;
        }
        if (argMode.IsSubModal(paramMode)) {
            hasSubmode = true;
        }
    }
    // not instance method call, return
    if (!hasThisParam) {
        return;
    }
    if (!hasExact && !hasSubmode) {
        // only consider copy type cast when even sub modes do not match any overload, keep all in this case.
        // otherwise go through normal filter by this mode
        if (typeManager.ImplementsCopyInterface(thisArgTy.Ty())) {
            return;
        }
        for (size_t i{0}; i < targetMark.size(); ++i) {
            targetMark[i] = false;
        }
        return;
    }

    for (size_t i = 0; i < candidates.size(); ++i) {
        if (!targetMark[i] || !typeManager.HasThisParam(candidates[i]->fd)) {
            continue;
        }
        auto paramMode = typeManager.GetThisParamTy(candidates[i]->fd).Mode();
        if (hasExact ? argMode != paramMode : !argMode.IsSubModal(paramMode)) {
            targetMark[i] = false;
        }
    }
}

std::vector<size_t> TypeChecker::TypeCheckerImpl::ResolveOverload(const ASTContext& ctx,
    std::vector<OwnedPtr<FunctionMatchingUnit>>& candidates, const CallExpr& ce, ModalTy target)
{
    auto targetNum = candidates.size();
    std::vector<bool> targetMark(targetNum, true); // When get false, the target is excluded.
    FilterOverriden(typeManager, candidates, targetMark);
    auto isCtorCall = (ce.callKind == CallKind::CALL_OBJECT_CREATION || ce.callKind == CallKind::CALL_STRUCT_CREATION);
    if (isCtorCall && !ce.modal.HasLocal() && !Ty::IsTyCorrect(target)) {
        FilterBetterThisModeCtorCall(typeManager, candidates, targetMark);
    }
    if (!isCtorCall) {
        FilterBetterThisModeNonCtorCall(ctx, ce, candidates, targetMark);
    }

    for (size_t i = 0; i < targetNum; ++i) {
        if (!targetMark[i]) {
            continue;
        }
        for (size_t j = i + 1; j < targetNum; ++j) {
            if (!targetMark[j]) {
                continue;
            }
            auto iToJ = CompareFuncCandidates(*candidates[i], *candidates[j], ce, target);
            auto jToI = CompareFuncCandidates(*candidates[j], *candidates[i], ce, target);
            if (iToJ && !jToI) {
                targetMark[j] = false;
            } else if (!iToJ && jToI) {
                targetMark[i] = false;
                break;
            }
            if (IsInterfaceFuncWithSameSignature(*candidates[i], *candidates[j])) {
                // When two interface candidates have same signature, disable one of them.
                targetMark[i] = false;
                break;
            }
        }
    }

    std::vector<size_t> results;
    for (size_t i = 0; i < targetMark.size(); ++i) {
        if (targetMark[i]) {
            results.push_back(i);
        }
    }
    return results;
}

void TypeChecker::TypeCheckerImpl::InstantiatePartOfTheGenericParameters(
    std::vector<OwnedPtr<FunctionMatchingUnit>>& candidates)
{
    for (auto& candidate : candidates) {
        if (candidate->typeMapping.u2i.empty()) {
            // If the typeMapping is empty, current candidate has no generic type parameter. No need to do
            // instantiation.
            continue;
        }
        if (IsClassOrEnumConstructor(candidate->fd)) {
            continue;
        }
        SubstPack outerDeclTypeMapping = candidate->typeMapping;
        CJC_NULLPTR_CHECK(candidate->fd.funcBody);
        auto generic = candidate->fd.funcBody->generic.get();
        if (generic) {
            // Delete the generic types that introduced from current candidate. Only type parameters introduced by the
            // outer decls are retained.
            for (auto& it : generic->typeParameters) {
                outerDeclTypeMapping.u2i.erase(StaticCast<GenericsTy*>(it->DataTy()));
            }
            if (outerDeclTypeMapping.u2i.empty()) {
                // If the candidate does not introduce type parameters from the outer layer, there is no need to
                // instantiate the parameters.
                continue;
            }
        }
        // Do instantiation.
        for (auto& it : candidate->tysInArgOrder) {
            it = typeManager.ApplySubstPack(it, outerDeclTypeMapping);
        }
    }
}

// When the baseExpr of other expr is a memberAccess,
// we could build the type mapping by the memberAccess's baseExpr's type.
void TypeChecker::TypeCheckerImpl::GenerateTypeMappingForBaseExpr(const Expr& baseExpr, MultiTypeSubst& typeMapping)
{
    if (baseExpr.astKind != ASTKind::MEMBER_ACCESS) {
        return;
    }
    auto& ma = static_cast<const MemberAccess&>(baseExpr);
    if (!ma.baseExpr->GetTy().IsCorrect()) {
        return;
    }
    CJC_ASSERT(!ma.baseExpr->GetTy()->HasIntersectionTy());
    if (IsThisOrSuper(*ma.baseExpr)) {
        typeManager.GenerateGenericMapping(typeMapping, *ma.baseExpr->GetTy());
        return;
    }
    auto baseDecl = GetRealTarget(ma.baseExpr.get(), ma.baseExpr->GetTarget());
    if (baseDecl && baseDecl->astKind == ASTKind::PACKAGE_DECL) {
        return;
    }
    auto maTarget = ma.GetTarget();
    // NOTE: member access of enum constructor is also considered as type decl's member access for inference.
    bool typeDeclMemberAccess = baseDecl && maTarget && maTarget->outerDecl &&
        (baseDecl->IsTypeDecl() || baseDecl->TestAttr(Attribute::ENUM_CONSTRUCTOR));
    if (typeDeclMemberAccess) {
        CJC_NULLPTR_CHECK(maTarget->outerDecl->GetTy());
        auto promoteMapping = promotion.GetPromoteTypeMapping(ma.baseExpr->DataTy(), maTarget->outerDecl->DataTy());
        auto baseTypeArgs = ma.baseExpr->GetTypeArgs();
        std::unordered_set<ModalTy> definedTys;
        std::for_each(
            baseTypeArgs.begin(), baseTypeArgs.end(), [&definedTys](auto type) { definedTys.emplace(type->GetTy()); });
        auto genericTys = GetAllGenericTys(baseDecl->DataTy());
        for (auto it = promoteMapping.begin(); it != promoteMapping.end(); ++it) {
            // If mapped 'ty' is existed in 'baseDecl' generic types
            // and is not found in user defined type args, remove it from mapping.
            Utils::EraseIf(it->second,
                [&genericTys, &definedTys](auto ty) { return genericTys.count(ty) != 0 && definedTys.count(ty) == 0; });
        }
        MergeMultiTypeSubsts(typeMapping, promoteMapping);
    } else {
        typeManager.GenerateGenericMapping(typeMapping, *ma.baseExpr->GetTy());
    }
}

// NOTE: all sema tys of 'Type' & 'TypeDecl' are set in 'PreCheck' step.
TypeSubst TypeChecker::TypeCheckerImpl::GenerateTypeMappingByTyArgs(
    const std::vector<Ptr<Type>>& typeArgs, const Generic& generic) const
{
    TypeSubst typeMapping;
    // Add the mapping when we can get the type parameters and type arguments.
    auto typeArgsSize = typeArgs.size();
    // Get the type arguments size and compare it with type parameters' size. They should be equal.
    if (generic.typeParameters.size() != typeArgsSize) {
        return typeMapping;
    }
    // If callExpr gives type arguments and fd has type parameters as well, generate the typeMapping directly.
    for (size_t i = 0; i < typeArgsSize; ++i) {
        if (!generic.typeParameters[i]) {
            continue;
        }
        ModalTy typeArgTy = {TypeManager::GetInvalidTy()};
        if (auto type = typeArgs[i]; type) {
            if (type->GetTy() && type->GetTy()->HasIntersectionTy()) {
                continue;
            }
            typeArgTy = type->GetTy();
        }
        if (generic.typeParameters[i]->GetTy().IsCorrect() && Ty::IsTyCorrect(typeArgTy)) {
            typeMapping.emplace(DynamicCast<GenericsTy>(generic.typeParameters[i]->DataTy()), typeArgTy.Ty());
        }
    }
    return typeMapping;
}

namespace {
// apply all indirect substituions in inst map,
// so that equivalent substituions will appear the same
void ReduceSubstPack(SubstPack& maps, TypeManager& tyMgr)
{
    for (auto& [tv, insts] : maps.inst) {
        std::set<DataTy> instsOneStep = insts;
        std::set<DataTy> instsFinal;
        for (auto ty : instsOneStep) {
            instsFinal.merge(tyMgr.GetInstantiatedTys(ty, maps.inst));
        }
        insts = instsFinal;
    }
}

void FilterAndReduceSubstPacks(std::vector<SubstPack>& mapsList, TypeManager& tyMgr)
{
    std::vector<SubstPack> filtered;
    for (auto& maps : mapsList) {
        if (!HasTyVarsToSolve(maps)) {
            filtered.push_back(maps);
        }
    }
    for (auto& maps : filtered) {
        ReduceSubstPack(maps, tyMgr);
    }
    mapsList = filtered;
}
}

void TypeChecker::TypeCheckerImpl::FilterTypeMappings(
    const Expr& expr, FuncDecl& fd, std::vector<MultiTypeSubst>& typeMappings)
{
    // Check all type parameter included in typeMapping to ensure that each generic has a instantiation type, otherwise
    // this target is illegal.
    auto genericParams = StaticToTyVars(GetAllGenericTys(expr, fd));
    for (auto curTyParam : genericParams) {
        for (auto it = typeMappings.begin(); it != typeMappings.end();) {
            auto mapping = *it;
            if (curTyParam->IsGeneric() && mapping.find(curTyParam) == mapping.end()) {
                it = typeMappings.erase(it);
            } else {
                ++it;
            }
        }
    }
    // Reduce type mapping to direct mapping for used generic sema types.
    auto tyVars = ::GetAllGenericTys(fd.DataTy());
    CJC_ASSERT(fd.funcBody);
    if (fd.funcBody->generic) {
        // Since 'func test<T>(a: Int64):Unit' function type can be used without type parameters' sema ty,
        // we need to add type parameters' sema ty manually to ensure keeping type parameters' mapping.
        for (auto& it : fd.funcBody->generic->typeParameters) {
            tyVars.emplace(it->DataTy());
        }
    }
    // If candidate returns 'This' type, need to keep type mapping for current decl.
    if (IsFuncReturnThisType(fd)) {
        auto declOfThisType = GetDeclOfThisType(expr);
        if (declOfThisType) {
            tyVars.merge(::GetAllGenericTys(declOfThisType->DataTy()));
        }
    } else if (auto ma = DynamicCast<const MemberAccess*>(&expr); ma && ma->baseExpr) {
        // 1. Store upperBounds' parent generic ty, if current is generic upper bound function call.
        // 2. If function contains quest ty, also collect typeMapping of outerDecl for possible generic ty.
        if (fd.outerDecl && (IsGenericUpperBoundCall(*ma, fd) || fd.GetTy()->HasQuestTy())) {
            tyVars.merge(::GetAllGenericTys(fd.outerDecl->DataTy()));
        }
        auto baseTarget = TypeCheckUtil::GetRealTarget(ma->baseExpr->GetTarget());
        // When type of baseExpr should be inferred, we need to keep tyVars. Such as A.add() or pkg.A.add().
        bool isNameAccessNeedInfer = baseTarget && baseTarget->IsTypeDecl() && ma->baseExpr->GetTypeArgs().empty();
        if (isNameAccessNeedInfer) {
            tyVars.merge(::GetAllGenericTys(baseTarget->DataTy()));
        }
    }
    // Erase context type vars from the type mapping, the context tyVar should be substituted.
    // eg: class A<T, K> {  func test() { A<String, T>(); }}; The 'T' should not be considered as substitutable.
    for (auto& mapping : typeMappings) {
        mapping = ReduceMultiTypeSubst(typeManager, StaticToTyVars(tyVars), mapping);
    }
    if (typeMappings.empty()) {
        // Should always leave at least one empty element in the 'typeMappings'.
        typeMappings.emplace_back(MultiTypeSubst{});
    }
}

bool TypeChecker::TypeCheckerImpl::CheckThisParamCompatible(
    ASTContext& ctx, const FuncDecl& fd, const CallExpr& ce, ModalTy target)
{
    if (fd.TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
        return true;
    }
    if (TypeManager::HasThisParam(fd)) {
        auto callArgTy = GetThisArgTy(ctx, ce);
        auto fdParamTy = typeManager.GetThisParamTy(fd);
        if (typeManager.ImplementsCopyInterface(callArgTy.Ty())) {
            return true;
        }
        bool good;
        ModalTy expect;
        ModalTy found;
        if (ce.callKind == CallKind::CALL_OBJECT_CREATION || ce.callKind == CallKind::CALL_STRUCT_CREATION) {
            found = fdParamTy;
            if (ce.modal.HasLocal()) {
                auto ceModal = ce.modal.ToModalInfo();
                // Call-site modal (e.g. C()@local!) selects ctor; not the declared target type.
                expect = fdParamTy.With(ceModal);
                good = found.Mode() == ceModal;
            } else if (target) {
                expect = target;
                good = found.Mode().IsSubModal(expect.Mode());
            } else {
                expect = fdParamTy;
                good = true;
            }
        } else if (auto ref = DynamicCast<RefExpr>(&*ce.baseFunc); ref && (ref->isThis || ref->isSuper)) {
            // must call init or super with the same modal
            expect = callArgTy;
            found = fdParamTy;
            good = expect.Mode() == found.Mode();
        } else {
            // normal function call, sub modal is ok
            expect = fdParamTy;
            found = callArgTy;
            good = found.Mode().IsSubModal(expect.Mode());
        }
        if (!good) {
            if (ce.baseFunc->ShouldDiagnose() && !CanSkipDiag(*ce.baseFunc)) {
                DiagMismatchedTypesWithFoundTy(diag, *ce.baseFunc, expect, found);
            }
            return false;
        }
    }
    return true;
}

bool TypeChecker::TypeCheckerImpl::IsThisParamIdentical(const FuncDecl& fd1, const FuncDecl& fd2)
{
    bool hasThisParam1 = TypeManager::HasThisParam(fd1);
    bool hasThisParam2 = TypeManager::HasThisParam(fd2);
    if (hasThisParam1 != hasThisParam2) {
        return false;
    }
    if (!hasThisParam1) {
        return true;
    }
    auto thisParamTy1 = typeManager.GetThisParamTy(fd1);
    auto thisParamTy2 = typeManager.GetThisParamTy(fd2);
    if (typeManager.ImplementsCopyInterface(thisParamTy1.Ty()) &&
        typeManager.ImplementsCopyInterface(thisParamTy2.Ty())) {
        return true;
    }
    return thisParamTy1.Mode().IsSubModal(thisParamTy2.Mode()) && thisParamTy2.Mode().IsSubModal(thisParamTy1.Mode());
}

bool TypeChecker::TypeCheckerImpl::CheckGenericCallCompatible(
    ASTContext& ctx, FunctionCandidate& candidate, SubstPack& typeMapping, ModalTy targetRet)
{
    auto& ce = candidate.ce;
    auto& fd = candidate.fd;
    // Generate the type mapping first. The constraints should be checked in the process. If the mapping of a
    // generic type is not unique, the generation fails and the function is not matched. True is returned if the
    // mapping meets the constraints.
    auto typeMappings = GenerateTypeMappingForCall(ctx, candidate, targetRet);
    FilterAndReduceSubstPacks(typeMappings, typeManager);
    if (typeMappings.empty()) {
        return false;
    }
    CJC_ASSERT(ce.baseFunc);
    // After generating type mapping, check the compatibility of arguments and parameters.
    std::vector<ModalTy> paramTysInArgOrder = GetParamTysInArgsOrder(typeManager, ce, fd);
    if (paramTysInArgOrder.size() != ce.args.size()) {
        return false;
    }
    if (fd.TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
        auto targetMode = InferEnumCtorMode(ce, targetRet);
        for (size_t i{0}; i < paramTysInArgOrder.size(); ++i) {
            paramTysInArgOrder[i] = paramTysInArgOrder[i].With(targetMode);
        }
    }
    std::vector<SubstPack> resMappings;
    for (const auto& mts : typeMappings) {
        std::set<DataTy> usedTys = {fd.DataTy()};
        usedTys.merge(GetGenericParamsForCall(ce, fd));
        auto mappings = ExpandMultiTypeSubst(mts, usedTys);
        resMappings.insert(resMappings.end(), mappings.begin(), mappings.end());
    }
    size_t matchedCnt = 0;
    for (auto mapping = resMappings.begin(); mapping != resMappings.end();) {
        auto ds = DiagSuppressor(diag);
        bool matched = true;
        if (!CheckThisParamCompatible(ctx, fd, ce, targetRet)) {
            ce.SetTy({TypeManager::GetInvalidTy()});
            matched = false;
        }
        size_t currentCnt = 0;
        for (size_t i = 0; i < paramTysInArgOrder.size(); ++i) {
            auto paramTy = paramTysInArgOrder[i];
            if (paramTy && paramTy->HasGeneric()) {
                // If the parameter has generic type, get the instantiation type.
                paramTy = typeManager.ApplySubstPack(paramTy, *mapping);
            }
            if (!CheckWithCache(ctx, paramTy, ce.args[i].get())) {
                matched = false;
                break;
            }
            if (!CheckUseTrailingClosureWithFunctionType(diag, *ce.args[i], paramTysInArgOrder[i], fd)) {
                matched = false;
                break;
            }
            currentCnt++;
        }
        matchedCnt = currentCnt > matchedCnt ? currentCnt : matchedCnt;
        if (matched || resMappings.size() == 1) {
            ds.ReportDiag(); // Report when matched or current is last candidate.
        }
        mapping = matched ? mapping + 1 : resMappings.erase(mapping);
    }
    candidate.stat.matchedArgs = static_cast<uint32_t>(matchedCnt);
    resMappings = ResolveTypeMappings(typeManager, resMappings, fd);
    if (resMappings.size() == 1) {
        typeMapping = resMappings.front();
        return CheckCandidateConstrains(ce, fd, typeMapping);
    }
    DiagnoseForMultiMapping(diag, ce, fd, typeMappings, resMappings);
    return false;
}

bool TypeChecker::TypeCheckerImpl::CheckAndGetMappingForTypeDecl(
    const Expr& baseExpr, const Decl& typeDecl, const Decl& targetDecl, const SubstPack& typeMapping)
{
    // Generate mapping from 'structDecl' generics to 'typeDecl' generics.
    // eg: open class I1<F, H>{ static func f3(a: F, b: H) {} }
    //     class I2<K> <: I1<Int64, K> {}
    //     I2.f3(1, Int32(1))
    // typeMapping will contains F->Int64, H->Int32.
    // But we need further mapping from K -> H, to get final I2's type I2<Int32>
    // Also for mapping extend ty to type decl ty.
    MultiTypeSubst mts;
    MultiTypeSubst mapping;
    if (baseExpr.GetTy() && targetDecl.GetTy()) {
        mapping = promotion.GetPromoteTypeMapping(baseExpr.DataTy(), targetDecl.DataTy());
    }
    for (auto it : std::as_const(mapping)) {
        if (!it.first->IsGeneric()) {
            continue;
        }
        for (auto ty : it.second) {
            if (ty->IsGeneric()) {
                mts[RawStaticCast<GenericsTy*>(ty)].emplace(typeManager.ApplySubstPack(it.first, typeMapping));
            }
        }
    }
    // Diagnose for inconsistent types, eg:
    // open class I1<F, H> { static func f3(a: F, b: K) {} }
    // class I2<K> <: I1<K, K> {}
    // I2.f3(Int64(1), Int32(1))
    // Should report error since we got F->Int64, H->Int32 and K->F, K->H. The type substitution does not consistent.
    bool valid = true;
    std::string diagMessage;
    for (auto it : std::as_const(mts)) {
        if (it.second.size() > 1) {
            diagMessage += (diagMessage.empty() ? "" : std::string(", ")) + "'" + it.first->String() + "' to '" +
                Ty::GetTypesToStr(it.second, "', '") + "'";
            valid = false;
        }
    }
    if (!valid) {
        auto diagBuilder = diag.DiagnoseRefactor(DiagKindRefactor::sema_generic_type_inconsistent, baseExpr,
            MakeRange(baseExpr.begin, baseExpr.end), typeDecl.GetTy().String());
        diagBuilder.AddNote(diagMessage);
    }
    return valid;
}

bool TypeChecker::TypeCheckerImpl::CheckCandidateConstrains(
    const CallExpr& ce, const FuncDecl& fd, const SubstPack& typeMapping)
{
    auto userDefinedTypeArgs = ce.baseFunc->GetTypeArgs();
    if (!NeedFurtherInstantiation(userDefinedTypeArgs) && fd.TestAttr(Attribute::GENERIC) &&
        !CheckGenericDeclInstantiation(&fd, userDefinedTypeArgs, *ce.baseFunc)) {
        return false;
    }
    bool ignored = !fd.outerDecl || !fd.outerDecl->IsNominalDecl() || !fd.outerDecl->GetTy().IsCorrect();
    if (ignored) {
        return true;
    }
    Ptr<Decl> structDecl = fd.outerDecl;
    if (auto ma = DynamicCast<MemberAccess*>(ce.baseFunc.get());
        ma && ma->baseExpr && ma->baseExpr->GetTy().IsCorrect()) {
        ModalTy baseTy = ma->baseExpr->GetTy();
        if (auto res = typeManager.GetExtendDeclByInterface(*baseTy, *structDecl->GetTy())) {
            structDecl = *res;
        }
        if (!structDecl->GetGeneric()) {
            return true;
        }
        auto typeDecl = Ty::GetDeclPtrOfTy<InheritableDecl>(baseTy);
        auto typeArgs = ma->baseExpr->GetTypeArgs();
        if (!NeedFurtherInstantiation(typeArgs) && structDecl == typeDecl) {
            // If function's outerDecl and maBase's type decl is same, check constrains with user defined typeArgs.
            return CheckGenericDeclInstantiation(structDecl, typeArgs, *ma->baseExpr);
        }
        bool needCheckTypeInconsistency = typeDecl && typeDecl->TestAttr(Attribute::GENERIC) && typeDecl != structDecl;
        if (needCheckTypeInconsistency) {
            if (!CheckAndGetMappingForTypeDecl(*ma->baseExpr, *typeDecl, *structDecl, typeMapping)) {
                return false;
            }
        }
        // If function is in extend decl, we need to replace any generic ty in type decl to generic ty in extend.
        if (structDecl->astKind == ASTKind::EXTEND_DECL && typeDecl) {
            baseTy = ReplaceWithGenericTyInInheritableDecl(baseTy, *structDecl, *typeDecl);
        }
        auto prRes = promotion.Promote(baseTy, structDecl->DataTy());
        if (prRes.empty()) {
            return true;
        }
        auto instTy = typeManager.ApplySubstPack(*prRes.begin(), typeMapping);
        return Ty::IsTyCorrect(instTy) && CheckGenericDeclInstantiation(structDecl, instTy->typeArgs, *ma->baseExpr);
    } else if (IsClassOrEnumConstructor(fd)) {
        // If typeArgs are fully instantiated, check with decl constraint.
        if (!NeedFurtherInstantiation(userDefinedTypeArgs)) {
            return CheckGenericDeclInstantiation(structDecl, userDefinedTypeArgs, *ce.baseFunc);
        }
    }
    return true;
}

ModalTy TypeChecker::TypeCheckerImpl::GetThisArgTy(const ASTContext& ctx, const CallExpr& ce)
{
    if (auto ma = DynamicCast<MemberAccess>(&*ce.baseFunc)) {
        return ma->baseExpr->GetTy();
    }
    if (ce.callKind == CallKind::CALL_OBJECT_CREATION || ce.callKind == CallKind::CALL_STRUCT_CREATION) {
        if (auto funcTy = DynamicCast<FuncTy>(TypeCheckUtil::GetRealTarget(ce.baseFunc->GetTarget())->DataTy())) {
            return funcTy->retTy;
        }
        // baseFunc is constructor, its ty would be the created object's type
        return TypeCheckUtil::GetRealTarget(ce.baseFunc->GetTarget())->GetTy();
    }
    if (auto ref = DynamicCast<RefExpr>(&*ce.baseFunc)) {
        return GetThisParamTyInScope(ctx, ref->scopeName);
    }
    CJC_ABORT_WITH_MSG("call not a member access or ref expr");
    return {};
}

ModalTy TypeChecker::TypeCheckerImpl::GetThisParamTyInScope(const ASTContext& ctx, const std::string& scopeName) const
{
    // get outer func ty (must be instance method so GetThisParamTy is valid)
    if (auto fn = ScopeManager::GetCurSatisfiedSymbolUntilTopLevel(ctx, scopeName, [](const Symbol& sym) {
        auto node = sym.node;
        if (auto func = DynamicCast<FuncDecl>(node)) {
            return Is<InheritableDecl>(func->outerDecl) && !func->TestAttr(Attribute::STATIC);
        }
        return false;
    })) {
        auto fd = StaticCast<FuncDecl>(fn->node);
        return typeManager.GetThisParamTy(*fd);
    }
    // not inside func, then inside member var init, use type @~local
    if (auto sym = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, scopeName)) {
        return sym->node->GetTy();
    }
    CJC_ABORT_WITH_MSG("no this param found in scope");
    return {};
}

/// 1) if ce has mode, use it
/// 2) else if \ref target present, use it
/// 3) else if at least one arg is non copy type, use common mode of all non copy type args
/// 4) otherwise use common mode of all args
ModalInfo TypeChecker::TypeCheckerImpl::InferEnumCtorMode(const CallExpr& ce, ModalTy target) const
{
    if (ce.modal) {
        return ce.modal.ToModalInfo();
    }
    if (target.IsCorrect()) {
        return target.Mode();
    }
    std::vector<bool> nonCopy(ce.args.size(), false);
    bool hasNonCopy{false};
    for (size_t i{0}; i < ce.args.size(); ++i) {
        nonCopy[i] = typeManager.ImplementsCopyInterface(ce.args[i]->DataTy());
        hasNonCopy = nonCopy[i] | hasNonCopy;
    }
    std::set<ModalTy> tys;
    if (hasNonCopy) {
        for (size_t i{0}; i < ce.args.size(); ++i) {
            if (nonCopy[i]) {
                tys.insert(ce.args[i]->GetTy());
            }
        }
    } else {
        for (size_t i{0}; i < ce.args.size(); ++i) {
            tys.insert(ce.args[i]->GetTy());
        }
    }
    return JoinAndMeet::JoinMode(typeManager, tys);
}

bool TypeChecker::TypeCheckerImpl::CheckCallCompatible(ASTContext& ctx, FunctionCandidate& candidate, ModalTy target)
{
    auto& ce = candidate.ce;
    auto& fd = candidate.fd;
    std::vector<ModalTy> paramTysInArgOrder = GetParamTysInArgsOrder(typeManager, ce, fd);
    if (paramTysInArgOrder.size() != ce.args.size()) {
        return false;
    }
    if (fd.TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
        auto targetMode = InferEnumCtorMode(ce, target);
        for (size_t i{0}; i < paramTysInArgOrder.size(); ++i) {
            paramTysInArgOrder[i] = paramTysInArgOrder[i].With(targetMode);
        }
    }
    if (!CheckThisParamCompatible(ctx, fd, ce, target)) {
        ce.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
    for (size_t i = 0; i < paramTysInArgOrder.size(); ++i) {
        ce.args[i]->Clear();
        if (!CheckWithCache(ctx, paramTysInArgOrder[i], ce.args[i].get())) {
            if (ce.args[i]->ShouldDiagnose() && !CanSkipDiag(*ce.args[i])) {
                DiagMismatchedTypes(diag, *ce.args[i], paramTysInArgOrder[i]);
            }
            return false;
        }
        if (!CheckUseTrailingClosureWithFunctionType(diag, *ce.args[i], paramTysInArgOrder[i], fd)) {
            return false;
        }
        if (fd.TestAttr(Attribute::C) && ce.args[i]->GetTy() && ce.args[i]->GetTy()->IsUnit()) {
            diag.Diagnose(*ce.args[i], DiagKind::sema_unit_cannot_as_cfunc_arg);
            return false;
        }
        candidate.stat.matchedArgs++;
        if (fd.TestAttr(Attribute::FOREIGN) && fd.hasVariableLenArg && i >= fd.funcBody->paramLists[0]->params.size()) {
            continue;
        }
        if (auto st = DynamicCast<StructTy>(ce.args[i]->DataTy());
            st && st->decl->TestAttr(Attribute::C) && st != paramTysInArgOrder[i].Ty()) {
            diag.Diagnose(*ce.args[i], DiagKind::sema_cstruct_cannot_autobox, paramTysInArgOrder[i]->String());
            return false;
        }
    }
    return true;
}

void TypeChecker::TypeCheckerImpl::FillEnumTypeArgumentsTy(
    const Decl& ctorDecl, const SubstPack& typeMapping, MemberAccess& ma)
{
    auto ed = As<ASTKind::ENUM_DECL>(ctorDecl.outerDecl);
    if (!ed || !ed->generic) {
        return;
    }
    if (auto ref = DynamicCast<NameReferenceExpr*>(ma.baseExpr.get())) {
        if (ref->typeArguments.empty()) {
            ref->instTys.clear();
        }
        if (ref->instTys.empty()) {
            for (auto& typeParam : ed->generic->typeParameters) {
                (void)ref->instTys.emplace_back(
                    typeManager.ApplySubstPack(StaticCast<GenericsTy*>(typeParam->DataTy()), typeMapping));
            }
        }
    }
    ma.baseExpr->SetTy(typeManager.ApplySubstPack(ed->GetTy(), typeMapping));
    auto baseTypeArgs = ma.baseExpr->GetTypeArgs();
    bool enumCallNoTyArgs = ma.typeArguments.empty() && baseTypeArgs.empty();
    if (enumCallNoTyArgs) {
        ma.SetTy(typeManager.ApplySubstPack(ma.GetTy(), typeMapping));
        if (ma.baseExpr->GetTy().IsCorrect()) {
            ma.SetTy(ma.GetTy().With(ma.baseExpr->TyMode()));
        }
    }
}

void TypeChecker::TypeCheckerImpl::FillTypeArgumentsTy(const FuncDecl& fd, const CallExpr& ce, SubstPack& typeMapping)
{
    auto generic = GetCurrentGeneric(fd, ce);
    if (auto ref = DynamicCast<NameReferenceExpr*>(ce.baseFunc.get()); ref && generic) {
        if (ref->typeArguments.empty()) {
            ref->instTys.clear();
        }
        if (ref->instTys.empty()) {
            for (auto& typeParam : generic->typeParameters) {
                // Store the instantiate type. this is more convenient for later instantiation.
                (void)ref->instTys.emplace_back(
                    typeManager.ApplySubstPack(StaticCast<GenericsTy*>(typeParam->DataTy()), typeMapping));
            }
        }
    }

    auto ma = As<ASTKind::MEMBER_ACCESS>(ce.baseFunc.get());
    // Return here for non-member access expression OR not structure's member decl.
    if (!ma || !fd.outerDecl) {
        return;
    }

    if (fd.TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
        FillEnumTypeArgumentsTy(fd, typeMapping, *ma);
        return;
    }

    /* for cases like
        enum A<T>{
            a
            public func b(x:T){0}
        }
        let v = A.a.b(1) // <-- inference here
        */
    if (auto ma2 = DynamicCast<MemberAccess*>(ma->baseExpr.get());
        ma2 && ma2->target && ma2->target->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
        FillEnumTypeArgumentsTy(*ma2->target, typeMapping, *ma2);
    }

    auto baseDecl = TypeCheckUtil::GetRealTarget(ma->baseExpr->GetTarget());
    auto ref = DynamicCast<NameReferenceExpr*>(ma->baseExpr.get());
    // A expr which has 'baseDecl' must also be 'NameReferenceExpr'.
    auto baseGeneric = baseDecl ? baseDecl->GetGeneric() : nullptr;
    bool noNeedArguments = !ref || !baseDecl || !baseGeneric;
    if (noNeedArguments) {
        return;
    }
    // Check whether is accessing member using generic type name without typeArguments.
    // NOTE: It is possible to access a member of generic type A<T> with non-generic type name B that B <: A<Type>.
    bool typeAccessOmitTypeArgs =
        (fd.TestAttr(Attribute::STATIC) || baseDecl->TestAttr(Attribute::ENUM_CONSTRUCTOR)) && ref->instTys.empty();
    if (!typeAccessOmitTypeArgs) {
        return;
    }
    if (baseDecl != fd.outerDecl) {
        // Generate mapping from 'fd.outerDecl' generics to real maBase's generics.
        // 1. eg: open class I1<F, H>{ static func f3(a: F, b: H) {} }
        //     class I2<K> <: I1<Int64, K> {}
        //     I2.f3(1, Int32(1))
        // typeMapping will contains F->Int64, H->Int32.
        // But we need further mapping from K -> H, to get final I2's type I2<Int32>
        // Also for mapping extend ty to type decl ty.
        // 2. public enum E<T> {
        //      EE
        //      func test(a: T) {}
        //    }
        //    EE.test(1) <- T of E<T> should be able to inferred.
        // This part may not be necessary. Could be treated as an unsolved ty var in the future.
        TypeSubst mapping;
        auto outerDeclTy = typeManager.GetInstantiatedTy(fd.outerDecl->DataTy(), typeMapping.u2i);
        if (baseDecl->GetTy() && fd.outerDecl->GetTy()) {
            mapping = MultiTypeSubstToTypeSubst(promotion.GetDowngradeTypeMapping(baseDecl->DataTy(), outerDeclTy));
        }
        typeManager.PackMapping(typeMapping, mapping);
    }
    // If memberAccess's base expression has no explicit type arguments, we should  add instantiated ty to it.
    for (auto& typeParameter : baseGeneric->typeParameters) {
        (void)ref->instTys.emplace_back(
            typeManager.ApplySubstPack(StaticCast<GenericsTy*>(typeParameter->DataTy()), typeMapping));
    }
    // Update the ma's base expression's ty.
    ma->baseExpr->SetTy(typeManager.ApplySubstPack(ma->baseExpr->GetTy(), typeMapping));
}

// Caller guarantees fmu.fd not null and has function type.
std::vector<Ptr<FuncDecl>> TypeChecker::TypeCheckerImpl::UpdateFuncGenericType(
    ASTContext& ctx, FunctionMatchingUnit& fmu, CallExpr& ce)
{
    auto funcTy = RawStaticCast<FuncTy*>(fmu.fd.DataTy());
    ReplaceIdealTypeInSubstPack(fmu.typeMapping);
    // Caller guarantees 'desugarArgs' has value.
    if (!ce.desugarArgs.has_value()) {
        return {};
    }
    for (size_t i = 0; i < ce.desugarArgs.value().size(); ++i) {
        auto& arg = ce.desugarArgs.value()[i];
        if (!arg->expr || !arg->expr->GetTy()) {
            continue;
        }
        if (arg->expr->GetTy()->HasIdealTy()) {
            return {};
        }
    }
    // The callExpr's type should be the instantiated type, not generic type.
    if (auto ref = DynamicCast<NameReferenceExpr*>(ce.baseFunc.get())) {
        ref->matchedParentTy = typeManager.ApplySubstPack(ref->matchedParentTy, fmu.typeMapping);
    }
    if (auto ma = DynamicCast<MemberAccess*>(ce.baseFunc.get()); ma && ma->matchedParentTy) {
        // 'matchedParentTy' may be generic ty that can be promoted further.
        if (ma->matchedParentTy->HasGeneric() && !ma->baseExpr->GetTy()->IsGeneric()) {
            auto prTys = promotion.Promote(ma->baseExpr->GetTy(), ma->matchedParentTy);
            auto updatedTy = prTys.empty() ? ModalTy{TypeManager::GetInvalidTy()} : *prTys.begin();
            if (Ty::IsTyCorrect(updatedTy)) {
                ma->matchedParentTy = updatedTy;
            }
        }
    }
    ce.baseFunc->SetTy(typeManager.ApplySubstPack(funcTy, fmu.typeMapping));
    auto rawTy = GetCallTy(ctx, ce, fmu.fd);
    ce.SetTy(typeManager.ApplySubstPack(rawTy, fmu.typeMapping));
    FillTypeArgumentsTy(fmu.fd, ce, fmu.typeMapping);
    return {&fmu.fd};
}

// Convert the type IDEAL_INT to INT64 and IDEAL_FLOAT to FLOAT64 in typeMapping.
void TypeChecker::TypeCheckerImpl::ReplaceIdealTypeInSubstPack(SubstPack& maps)
{
    for (auto& [tv, tys] : maps.inst) {
        auto tys0 = tys;
        tys.clear();
        for (auto& ty0 : tys0) { // std::set always has const iterator, can't replace in-place
            ModalTy ty1{ty0};
            ty1 = typeManager.ReplaceIdealTy(ty1);
            tys.insert(ty1.Ty());
        }
    }
}

OwnedPtr<FunctionMatchingUnit> TypeChecker::TypeCheckerImpl::CheckCandidate(
    ASTContext& ctx, FunctionCandidate& candidate, ModalTy targetRet, SubstPack& typeMapping)
{
    auto& ce = candidate.ce;
    auto& fd = candidate.fd;
    bool wrongNormalArgSize = !fd.hasVariableLenArg &&
        fd.funcBody->paramLists.front()->params.size() != ce.args.size() && !HasSamePositionalArgsSize(fd, ce);
    if (wrongNormalArgSize) {
        DiagWrongNumberOfArguments(diag, ce, fd);
        return nullptr;
    }
    candidate.stat.argNameValid = CheckArgsWithParamName(ce, fd);
    std::vector<ModalTy> paramTyInArgOrder = GetParamTysInArgsOrder(typeManager, ce, fd);
    if (paramTyInArgOrder.size() != ce.args.size()) {
        return nullptr;
    }
    // Check whether the target function's params type is compatible with arguments.
    if (IsGenericCall(ctx, candidate.ce, candidate.fd)) {
        if (!CheckGenericCallCompatible(ctx, candidate, typeMapping, targetRet)) {
            return nullptr;
        }
    } else if (!CheckCallCompatible(ctx, candidate, targetRet)) {
        return nullptr;
    }
    if (NeedSynOnUsed(fd)) {
        Synthesize({ctx, SynPos::NONE}, &fd);
    }
    if (!fd.GetTy().IsCorrect() || !fd.GetTy()->IsFunc()) {
        return nullptr;
    }
    if (ce.callKind == CallKind::CALL_SUPER_FUNCTION) {
        auto thisModal = GetThisArgTy(ctx, ce).Mode();
        auto superModal = GetThisParamModal(fd);
        // can only call super() with same @local
        if (Is<RefExpr>(ce.baseFunc) && thisModal != superModal) {
            return nullptr;
        }
        // call to super.foo() can use any compatible modal
        if (Is<MemberAccess>(ce.baseFunc) && !thisModal.IsSubModal(superModal)) {
            return nullptr;
        }
    }
    if (targetRet) {
        // Check if the function's return type is the subtype of the target type that the callExpr should be.
        auto retTy = GetCallTy(ctx, candidate.ce, fd);
        DynamicBindingThisType(*ce.baseFunc, fd, typeMapping);
        auto instRet = typeManager.ApplySubstPack(retTy, typeMapping);
        if (fd.TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
            if (ce.modal.HasLocal()) {
                instRet = instRet.With(ce.modal.ToModalInfo());
            } else {
                instRet = instRet.With(targetRet.Mode());
            }
        }
        // a created object can be converted to any modal
        ModalMatchMode modalMode;
        if (ce.modal.HasLocal()) {
            auto ceModal = ce.modal.ToModalInfo();
            // let c: C @local! = C()@local! — exact; let c: C @local? = C()@local! — @local! <: @local?
            modalMode = targetRet.Mode() == ceModal ? ModalMatchMode::EXACT : ModalMatchMode::SUBTYPE;
        } else {
            modalMode = ModalMatchMode::SUBTYPE;
        }
        // Should delete ideal type in the future.
        if (!instRet->HasIdealTy() && !typeManager.IsSubtype(instRet, targetRet, true, true, modalMode)) {
            if (!ce.sourceExpr) {
                DiagMismatchedTypesWithFoundTy(diag, ce, targetRet, instRet);
            }
            return nullptr;
        }
    } else if (ce.callKind == CallKind::CALL_OBJECT_CREATION || ce.callKind == CallKind::CALL_STRUCT_CREATION) {
        // no target type, let c = C()@local!, use exact mode
        if (ce.modal.HasLocal()) {
            auto ceModal = ce.modal.ToModalInfo();
            auto thisParamTy = typeManager.GetThisParamTy(fd);
            if (thisParamTy.Mode() != ceModal && !typeManager.ImplementsCopyInterface(thisParamTy.Ty())) {
                DiagSemaMismatchedTypes(diag, ce, thisParamTy->String(), thisParamTy.With(ceModal)->String());
                return nullptr;
            }
        }
        // otherwise it is let c = C(), any mode is accepted, no filter required here.
    }
    if (fd.TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
        auto targetMode = InferEnumCtorMode(ce, targetRet);
        for (size_t i{0}; i < paramTyInArgOrder.size(); ++i) {
            paramTyInArgOrder[i] = paramTyInArgOrder[i].With(targetMode);
        }
    }
    return candidate.stat.argNameValid ? MakeOwned<FunctionMatchingUnit>(fd, paramTyInArgOrder, typeMapping) : nullptr;
}

std::vector<Ptr<FuncDecl>> TypeChecker::TypeCheckerImpl::ReorderCallArgument(
    ASTContext& ctx, FunctionMatchingUnit& fmu, CallExpr& ce)
{
    std::vector<Ptr<FuncArg>> args;
    auto defaultArgs = std::vector<OwnedPtr<FuncArg>>();
    // Record the default parameters' type first. Non-default parameters' type is set nullptr firstly.
    for (auto& param : fmu.fd.funcBody->paramLists[0]->params) {
        if (param->TestAttr(Attribute::HAS_INITIAL)) {
            auto arg = CreateFuncArgForOptional(*param);
            if (!fmu.fd.TestAttr(Attribute::TOOL_ADD)) {
                arg->EnableAttr(Attribute::HAS_INITIAL);
            } else if (arg->GetTy().IsCorrect() && !arg->expr->GetTy().IsCorrect()) {
                // When default parameters are created by cjogen, the arg->expr type info is required,
                // but in Cangjie is used only as a placeholder.
                auto ret = Check(ctx, arg->GetTy(), arg->expr.get());
                CJC_ASSERT(ret);
            }
            SpreadInstantiationTy(*arg, fmu.typeMapping);
            args.push_back(arg.get());
            defaultArgs.push_back(std::move(arg));
        } else {
            args.push_back(nullptr);
        }
    }
    SortCallArgumentByParamOrder(fmu.fd, ce, args);
    ce.desugarArgs = args;
    // could be re-checked, must release after references in desugarArgs have changed
    ce.defaultArgs = std::move(defaultArgs);
    defaultArgs.clear();

    // Caller guarantees fd not null and type must be function type.
    auto funcTy = RawStaticCast<FuncTy*>(fmu.fd.DataTy());
    // When the funcDecl is a generic function, we should do instantiation and assign the real type to callExpr.
    if (IsGenericCall(ctx, ce, fmu.fd)) {
        return UpdateFuncGenericType(ctx, fmu, ce);
    }

    // Infer the ideal type, if infer fails, return a nullptr and there is no match function for the callExpr.
    for (size_t i = 0; i < funcTy->paramTys.size(); ++i) {
        auto& arg = ce.desugarArgs.value()[i];
        if (arg->TestAttr(Attribute::HAS_INITIAL)) {
            continue;
        }
        if (arg->GetTy() && arg->GetTy()->IsIdeal()) {
            return {};
        }
    }
    ce.baseFunc->SetTy({funcTy, fmu.fd.TyMode()});
    ce.SetTy(GetCallTy(ctx, ce, fmu.fd));
    return {&fmu.fd};
}

/// Get return type from a function call. This translates the returned This ty to its underlying type if necessary.
/// Otherwise it returns the original function return type.
ModalTy TypeChecker::TypeCheckerImpl::GetCallTy(ASTContext& ctx, const CallExpr& ce, const FuncDecl& target) const
{
    auto targetRetTy = StaticCast<FuncTy>(target.DataTy())->retTy;
    if (!Is<ClassThisTy>(targetRetTy.Ty())) {
        return targetRetTy;
    }
    if (auto ma = DynamicCast<MemberAccess>(&*ce.baseFunc)) {
        // SomeClass.StaticFunc() -> SomeClass
        if (target.TestAttr(Attribute::STATIC)) {
            return target.outerDecl->GetTy();
        }
        // instance.Func() -> instance type
        return ma->baseExpr->GetTy();
    }
    ModalInfo objectModal = GetThisParamModal(target);
    if (ce.modal.HasLocal()) {
        auto ceModal = ce.modal.ToModalInfo();
        if (TypeManager::HasThisParam(target) && GetThisParamModal(target) == ceModal) {
            objectModal = ceModal;
        }
    }
    auto sym = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, ce.scopeName);
    CJC_ASSERT(sym->node);
    if (auto classTy = DynamicCast<ClassTy>(sym->node->DataTy())) {
        return {typeManager.GetClassThisTy(*classTy->decl, classTy->TyArgs()), objectModal};
    }
    return sym->node->GetTy().With(objectModal);
}

// Collect valid function types (non-generic function types & instantiated function types),
// and erase invalid targets from 'funcs' vector. Making candidateTys' order corresponding to 'funcs'.
// return true if there is not instantiated candidate exist during collection.
// NOTE: we only synthesize candidate function when following condition fits:
//  1. when the 'targetTy' is given, the function's parameter types should be subtype of the 'targetTy'.
//  2. the function is imported so it will not have circular dependency when synthesizing.
//  3. the function does not have user written return type.
// NOTE: return type is pair of (ignoreGeneric, (funcDecl, modalTy, typeMapping))
TypeChecker::TypeCheckerImpl::FuncTyPair TypeChecker::TypeCheckerImpl::CollectValidFuncTys(
    ASTContext& ctx, std::vector<Ptr<FuncDecl>>& funcs, Expr& expr, Ptr<FuncTy> targetTy)
{
    FilterShadowedFunc(funcs); // Filter invalid and shadowed function.
    std::vector<std::tuple<Ptr<AST::FuncDecl>, ModalTy, TypeSubst>> candidates;
    auto typeArgs = expr.GetTypeArgs();
    bool genericIgnored = false;
    auto ds = DiagSuppressor(diag);
    for (auto fd : funcs) {
        if (fd == nullptr || !fd->GetTy().IsCorrect() || !fd->GetTy()->IsFunc()) {
            continue;
        }
        SubstPack mts;
        // Instantiate for all candidate target generic functions.
        if (auto generic = fd->GetGeneric()) {
            // If re has no type arguments, generic candidates should be ignored.
            genericIgnored = genericIgnored || typeArgs.empty();
            if (typeArgs.empty() || !CheckGenericDeclInstantiation(fd, typeArgs, expr)) {
                continue;
            }
            typeManager.PackMapping(mts, GenerateTypeMappingByTyArgs(typeArgs, *generic));
        }
        ReplaceTarget(&expr, fd); // Update target for typeMapping generation.
        // Instantiate ty if call base is member access and member base is an instantiated node.
        MergeSubstPack(mts, GenerateGenericTypeMapping(ctx, expr));
        // Zip is just a compatibility patch. Change all following to use SubstPack in the future.
        std::vector<MultiTypeSubst> typeMappings{typeManager.ZipSubstPack(mts)};
        // Filter typeMapping with generic parameters, it will keep the size of 'typeMappings' to be at least one.
        FilterTypeMappings(expr, *fd, typeMappings);
        CJC_ASSERT(!typeMappings.empty());

        std::set<DataTy> usedTys = {fd->DataTy()};
        usedTys.merge(GetGenericParamsForDecl(*fd));
        std::set<TypeSubst> resMappings = ExpandMultiTypeSubst(typeManager, typeMappings[0], usedTys);

        auto thisCd = DynamicCast<ClassDecl*>(GetDeclOfThisType(expr));
        bool hasThisRet = thisCd && thisCd->GetTy().IsCorrect() && IsFuncReturnThisType(*fd);
        auto genericParams = GetAllGenericTys(expr, *fd);
        // Process for all possible type mappings.
        for (auto& mapping : resMappings) {
            // If generic typeParameter not empty but mapping is empty, set 'genericIgnored' true;
            // NOTE: As long as any one of mapping is empty, it means there is no valid mapping candidate.
            if (!genericParams.empty() && mapping.empty()) {
                genericIgnored = true;
                break;
            }
            auto instTy = typeManager.GetInstantiatedTy(fd->GetTy(), mapping);
            // If parameters' types already incompatible, ignore current candidate.
            if (targetTy && !typeManager.IsFuncParametersSubtype(*RawStaticCast<FuncTy*>(instTy.Ty()), *targetTy)) {
                continue;
            }
            if (NeedSynOnUsed(*fd) && Synthesize({ctx, SynPos::NONE}, fd) &&
                (!fd->GetTy().IsCorrect() || fd->GetTy()->HasQuestTy())) {
                DiagUnableToInferReturnType(diag, *fd, expr);
                break; // If the function's type still contains quest type, ignore current candidate.
            }
            // Re-instantiated function's type after function has been synthesize.
            instTy = typeManager.GetInstantiatedTy(fd->GetTy(), mapping);
            CJC_ASSERT(instTy->IsFunc());
            // Process dynamic binding this type.
            if (hasThisRet) {
                // Since 'CollectValidFuncTys' is only used for reference node, we do not need to keep 'This' ty here.
                auto realThisTy = typeManager.GetInstantiatedTy(thisCd->GetTy(), mapping);
                instTy = {typeManager.GetFunctionTy(RawStaticCast<FuncTy*>(instTy.Ty())->paramTys, realThisTy)};
            }
            candidates.emplace_back(std::make_tuple(fd, instTy, mapping));
        }
    }
    return {genericIgnored, candidates};
}

bool TypeChecker::TypeCheckerImpl::HasBaseOfPlaceholderTy(ASTContext& ctx, Ptr<Node> n)
{
    if (!n || typeManager.GetUnsolvedTyVars().empty()) {
        return false;
    }

    if (auto ma = DynamicCast<MemberAccess*>(n)) {
        SetIsNotAlone(*ma->baseExpr);
        if (HasBaseOfPlaceholderTy(ctx, ma->baseExpr)) {
            return true;
        }
    }

    bool wasOK = n->GetTy().IsCorrect();
    if (Ty::IsInitialTy(n->DataTy())) {
        DiagSuppressor ds(diag);
        ctx.targetTypeMap[n] = {TypeManager::GetQuestTy()};
        SynthesizeWithCache({ctx, SynPos::EXPR_ARG}, n);
        ctx.targetTypeMap[n] = {};
    }
    if (n->GetTy()->IsPlaceholder()) {
        return true;
    }
    if (!wasOK) {
        n->Clear();
    }
    return false;
}

std::vector<std::set<ModalTy>> TypeChecker::TypeCheckerImpl::GetArgTyPossibilities(ASTContext& ctx, CallExpr& ce)
{
    std::vector<std::set<ModalTy>> ceArgs;
    for (auto& arg : ce.args) {
        std::vector<ModalTy> argCandidates;
        if (!arg->expr) {
            continue;
        }
        // Check non-reference expressions type later with function parameter type as upper-bound.
        if (!arg->expr->IsReferenceExpr() || HasBaseOfPlaceholderTy(ctx, arg->expr)) {
            argCandidates.emplace_back(nullptr);
            ceArgs.emplace_back(Utils::VecToSet(argCandidates));
            continue;
        }
        // Set dummy target type for synthesize target, this will only used for skip filter function,
        // since ambiguous target of reference is acceptable as func arg, which is knonw from existence of target Ty.
        // Avoid error report by 'Synthesize' in current and later steps.
        ctx.targetTypeMap[arg->expr.get()] = {TypeManager::GetQuestTy()};
        SynthesizeWithCache({ctx, SynPos::EXPR_ARG}, arg.get());
        ctx.targetTypeMap[arg->expr.get()] = {};
        // Get all possible definition for each function argument.
        auto targets = GetFuncTargets(*arg->expr);
        // Only refExpr/memberAccess base with function target may have different types for a call,
        // So record and skip function check for other cases.
        if (!arg->GetTy().IsCorrect() || targets.empty()) {
            argCandidates.emplace_back(arg->GetTy());
            ceArgs.emplace_back(Utils::VecToSet(argCandidates));
            continue;
        }
        auto [genericIgnored, candidates] = CollectValidFuncTys(ctx, targets, *arg->expr);
        for (auto [_, ty, __] : candidates) {
            argCandidates.emplace_back(ty);
        }
        if (argCandidates.empty()) {
            if (genericIgnored) {
                diag.Diagnose(*arg, DiagKind::sema_generic_type_without_type_argument);
                return {};
            }
            argCandidates.emplace_back(arg->GetTy());
        }
        ceArgs.emplace_back(Utils::VecToSet(argCandidates));
    }
    return ceArgs;
}

std::vector<std::vector<ModalTy>> TypeChecker::TypeCheckerImpl::GetArgsCombination(ASTContext& ctx, CallExpr& ce)
{
    std::vector<std::set<ModalTy>> ceArgs = GetArgTyPossibilities(ctx, ce);
    if (ceArgs.empty()) {
        return {};
    }
    std::vector<std::vector<ModalTy>> combinations;
    std::function<void(std::vector<ModalTy>&)> fillCombination;
    size_t numArgs = ceArgs.size();
    // Combine every arguments possibilities.
    fillCombination = [&fillCombination, &ceArgs, &numArgs, &combinations](std::vector<ModalTy>& argSet) {
        size_t cur = argSet.size();
        for (auto& it : ceArgs[cur]) {
            argSet.emplace_back(it);
            if (cur + 1 == numArgs) {
                combinations.emplace_back(argSet);
            } else {
                fillCombination(argSet);
            }
            argSet.pop_back();
        }
    };
    std::vector<ModalTy> argSet;
    fillCombination(argSet);
    return combinations;
}

std::vector<Ptr<FuncDecl>> TypeChecker::TypeCheckerImpl::CheckFunctionMatch(
    ASTContext& ctx, FunctionCandidate& candidate, ModalTy target, SubstPack& typeMapping)
{
    auto matched = CheckCandidate(ctx, candidate, target, typeMapping);
    if (!matched) {
        return {};
    }
    if (candidate.fd.outerDecl && candidate.fd.outerDecl->astKind == ASTKind::INTERFACE_DECL) {
        if (auto ref = DynamicCast<NameReferenceExpr*>(candidate.ce.baseFunc.get())) {
            ref->matchedParentTy = candidate.fd.outerDecl->DataTy();
        }
    }
    // Infer the arguments' type again, to update ideal type and reference's targets which may overload.
    ReInferCallArgs(ctx, candidate.ce, *matched, target);
    return ReorderCallArgument(ctx, *matched, candidate.ce);
}

void TypeChecker::TypeCheckerImpl::RemoveShadowedFunc(
    const FuncDecl& fd, int64_t currentLevel, int64_t targetLevel, std::vector<Ptr<FuncDecl>>& funcs)
{
    if (targetLevel >= currentLevel) {
        return;
    }
    auto paramTys = GetParamTys(fd);
    auto isFuncSignatureSame = [this, &fd, &paramTys](auto& target) -> bool {
        if (fd.TestAttr(Attribute::GENERIC) != target->TestAttr(Attribute::GENERIC)) {
            return false;
        }
        auto targetParamTys = GetParamTys(*target);
        bool thisParamMatch{true};
        if (TypeManager::HasThisParam(*target) && TypeManager::HasThisParam(fd)) {
            thisParamMatch = typeManager.CheckTypeCompatibility(typeManager.GetThisParamTy(*target),
                typeManager.GetThisParamTy(fd), false) == TypeCompatibility::IDENTICAL;
        }
        return typeManager.IsFuncParameterTypesIdentical(targetParamTys, paramTys) && thisParamMatch;
    };
    funcs.erase(std::remove_if(funcs.begin(), funcs.end(), isFuncSignatureSame), funcs.end());
}

void TypeChecker::TypeCheckerImpl::FilterShadowedFunc(std::vector<Ptr<FuncDecl>>& candidates)
{
    auto calScopeLevel = [](auto fd) { // Caller guarantees fd not null.
        // Imported gloabl function's scope level is -1.
        return fd->TestAttr(Attribute::IMPORTED) && fd->TestAttr(Attribute::GLOBAL)
            ? -1
            : (fd->TestAttr(Attribute::ENUM_CONSTRUCTOR) ? 0 : static_cast<int64_t>(fd->scopeLevel));
    };
    std::unordered_map<int64_t, std::vector<Ptr<FuncDecl>>> levelMap;
    auto checkPriority = [&levelMap, &calScopeLevel](auto& fd) {
        if (fd == nullptr) {
            return;
        }
        int64_t scopeLevel = calScopeLevel(fd);
        auto found = levelMap.find(scopeLevel);
        if (found == levelMap.end()) {
            std::vector<Ptr<FuncDecl>> set{fd};
            levelMap.emplace(std::make_pair(scopeLevel, set));
        } else {
            found->second.emplace_back(fd);
        }
    };
    std::for_each(candidates.begin(), candidates.end(), checkPriority);
    for (auto& [i, fdSet] : levelMap) {
        int64_t level = i;
        // For every function decl in each scope level,
        // check and remove functions in outer scopes which has same function signature.
        for (auto& fd : fdSet) {
            for (auto& it : levelMap) {
                auto scopeLevel = it.first;
                auto& funcs = it.second;
                RemoveShadowedFunc(*fd, level, scopeLevel, funcs);
            }
        }
    }
    // Remove shadowed function from candidates in place.
    // Need keep null for re-export case. At least one valid target before null.
    Utils::EraseIf(
        candidates, [&levelMap, &calScopeLevel](auto fd) { return fd && !Utils::In(fd, levelMap[calScopeLevel(fd)]); });
}

void TypeChecker::TypeCheckerImpl::FilterExtendImplAbstractFunc(std::vector<Ptr<FuncDecl>>& candidates)
{
    std::set<Ptr<FuncDecl>> implementedOrOverridenFuncs;
    for (auto& fd1 : candidates) {
        if (!(fd1->TestAttr(Attribute::ABSTRACT) && fd1->GetTy().IsCorrect() && fd1->outerDecl &&
                fd1->outerDecl->astKind == ASTKind::INTERFACE_DECL)) {
            continue;
        }
        auto id = StaticAs<ASTKind::INTERFACE_DECL>(fd1->outerDecl);
        for (auto it2 = candidates.begin(); it2 != candidates.end(); ++it2) {
            auto fd2 = *it2;
            if (!(fd2->GetTy().IsCorrect() && fd2->outerDecl && fd2->outerDecl->astKind == ASTKind::EXTEND_DECL)) {
                continue;
            }
            auto ed = StaticAs<ASTKind::EXTEND_DECL>(fd2->outerDecl);
            auto [flag, typeMapping] = CheckAndObtainTypeMappingBetweenInterfaceAndExtend(*ed, *id);
            if (!flag) {
                continue;
            }
            auto params1 = StaticCast<FuncTy*>(typeManager.GetInstantiatedTy(fd1->DataTy(), typeMapping))->paramTys;
            auto params2 = GetParamTys(*fd2);
            bool sameThisParam{true};
            if (TypeManager::HasThisParam(*fd1) && TypeManager::HasThisParam(*fd2)) {
                sameThisParam = GetThisParamModal(*fd1) == GetThisParamModal(*fd2);
            }
            if (typeManager.IsFuncParameterTypesIdentical(params1, params2) && sameThisParam &&
                typeManager.IsSubtype(fd2->GetTy(), fd1->GetTy())) {
                implementedOrOverridenFuncs.insert(fd1);
            }
        }
    }
    if (implementedOrOverridenFuncs.empty()) {
        return;
    }
    for (auto it = candidates.begin(); it != candidates.end();) {
        if (implementedOrOverridenFuncs.find(*it) != implementedOrOverridenFuncs.end()) {
            it = candidates.erase(it);
        } else {
            ++it;
        }
    }
}

void TypeChecker::TypeCheckerImpl::FilterIncompatibleCandidatesForCall(
    const CallExpr& ce, std::vector<Ptr<FuncDecl>>& candidates)
{
    if (candidates.empty()) {
        return;
    }
    Ptr<FuncDecl> badFd = candidates.front();
    // Param lists are decided before type synthesis which can be used to filter candidates earlier.
    auto notMatch = [this, &badFd, &ce](const Ptr<FuncDecl> fd) {
        CJC_ASSERT(fd && fd->funcBody && !fd->funcBody->paramLists.empty());
        // If the function is variadic function, this function is always valid with arg size.
        if (fd->hasVariableLenArg) {
            return false;
        }
        if (IsPossibleVariadicFunction(*fd, ce)) {
            return false;
        }
        auto ds = DiagSuppressor(diag);
        if (HasSamePositionalArgsSize(*fd, ce) && CheckArgsWithParamName(ce, *fd)) {
            return false;
        }
        badFd = fd;
        return true;
    };
    // Filter by lambda and trailing closure first.
    candidates = SyntaxFilterCandidates(ce, candidates);
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), notMatch), candidates.end());
    if (!candidates.empty()) {
        return;
    }

    size_t initializedParamSize = GetInitializedlNameParamSize(*badFd);
    size_t paramsSize = badFd->funcBody->paramLists.front()->params.size();
    if ((ce.args.size() >= paramsSize - initializedParamSize) && (ce.args.size() <= paramsSize)) {
        CheckArgsWithParamName(ce, *badFd);
    } else {
        DiagWrongNumberOfArguments(diag, ce, *badFd);
    }
}

std::vector<Ptr<FuncDecl>> TypeChecker::TypeCheckerImpl::FilterCandidates(
    const ASTContext& ctx, const std::vector<Ptr<FuncDecl>>& inCandidates, const CallExpr& ce)
{
    if (inCandidates.empty()) {
        return inCandidates;
    }
    std::vector<Ptr<FuncDecl>> candidates = inCandidates;
    // The trait function of 'This' type cannot be called.
    auto isInvalidAndInvisible = [this, &ctx, &ce](Ptr<const FuncDecl> fd) {
        bool invalid = !fd || !fd->funcBody || fd->funcBody->paramLists.empty();
        if (invalid) {
            return true;
        }
        // Check candidate's accessibility, filter invisible candidate.
        Symbol* sym = ScopeManager::GetCurSymbolByKind(SymbolKind::STRUCT, ctx, ce.baseFunc->scopeName);
        return !IsLegalAccess(sym, *fd, *ce.baseFunc, importManager, typeManager);
    };
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), isInvalidAndInvisible), candidates.end());

    FilterIncompatibleCandidatesForCall(ce, candidates);

    auto paramInvalid = [](Ptr<FuncDecl> fd) {
        auto funcTy = DynamicCast<FuncTy>(fd->DataTy());
        // 'IsTyCorrect' check will confirm all typeArgs are valid.
        if (!Ty::IsTyCorrect(funcTy)) {
            return true;
        }
        if (funcTy->paramTys.size() != fd->funcBody->paramLists[0]->params.size()) {
            return true;
        }
        return false;
    };
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), paramInvalid), candidates.end());
    FilterShadowedFunc(candidates);
    FilterExtendImplAbstractFunc(candidates);
    return candidates;
}

std::vector<Ptr<FuncDecl>> TypeChecker::TypeCheckerImpl::FilterCandidatesWithReExport(
    const ASTContext& ctx, const std::vector<Ptr<FuncDecl>>& inCandidates, const CallExpr& ce)
{
    if (inCandidates.empty()) {
        return {};
    }
    auto first = inCandidates.begin();
    while (first != inCandidates.end()) {
        if (*first == nullptr) {
            break;
        }
        ++first;
    }
    if (first == inCandidates.end()) {
        return FilterCandidates(ctx, inCandidates, ce);
    }
    std::vector<Ptr<FuncDecl>> reExportFuncTargets;
    std::vector<Ptr<FuncDecl>> importedFuncTargets;
    std::copy(inCandidates.begin(), first, std::back_inserter(reExportFuncTargets));
    std::copy(++first, inCandidates.end(), std::back_inserter(importedFuncTargets));
    importedFuncTargets = FilterCandidates(ctx, importedFuncTargets, ce);
    reExportFuncTargets = FilterCandidates(ctx, reExportFuncTargets, ce);
    if (!(importedFuncTargets.empty() && reExportFuncTargets.empty())) {
        std::copy(importedFuncTargets.begin(), importedFuncTargets.end(), std::back_inserter(reExportFuncTargets));
        return reExportFuncTargets;
    }
    if (importedFuncTargets.empty()) {
        return reExportFuncTargets;
    }
    return importedFuncTargets;
}

using FuncCompT = std::function<bool(Ptr<FuncDecl> const, Ptr<FuncDecl> const)>;
std::vector<Ptr<FuncDecl>> TypeChecker::TypeCheckerImpl::GetOrderedCandidates(const ASTContext& ctx, const CallExpr& ce,
    std::vector<Ptr<FuncDecl>>& candidates, std::unordered_map<Ptr<FuncDecl>, int64_t>& fdScopeLevelMap) const
{
    std::vector<Ptr<FuncDecl>> reExportAndCurrentPkgFuncTargets;
    std::vector<Ptr<FuncDecl>> importedFuncTargets;
    for (Ptr<FuncDecl> funcDecl : candidates) {
        CJC_NULLPTR_CHECK(funcDecl);
        if (funcDecl->isReExportedOrRenamed) {
            reExportAndCurrentPkgFuncTargets.emplace_back(funcDecl);
        } else {
            importedFuncTargets.emplace_back(funcDecl);
        }
    }
    // Enum constructors are treated as global decls.
    // Define a compare function to help sorting scope in descending order.
    bool reExport = true;
    FuncCompT scopeLevelCompare = [&ctx, &ce, &reExport, &fdScopeLevelMap](auto fd1, auto fd2) -> bool {
        auto scopeCal = [&ctx, &ce, &reExport](const FuncDecl& fd) -> int64_t {
            // Imported toplevel function's scope level is treated as lowest.
            // Member functions except enum constructor should be treated as original scope level.
            if (!fd.IsSamePackage(ce) && fd.TestAttr(Attribute::GLOBAL) &&
                (!fd.outerDecl || fd.TestAttr(Attribute::ENUM_CONSTRUCTOR))) {
                return reExport ? 0 : -1;
            }
            if (fd.TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
                return CalculateEnumCtorScopeLevel(ctx, ce, fd);
            }
            return static_cast<int64_t>(fd.scopeLevel);
        };
        int64_t scopeLevel1 = scopeCal(*fd1);
        fdScopeLevelMap[fd1] = scopeLevel1;
        int64_t scopeLevel2 = scopeCal(*fd2);
        fdScopeLevelMap[fd2] = scopeLevel2;
        // Greater scope level, smaller file id, smaller position.
        if (scopeLevel1 != scopeLevel2) {
            return scopeLevel1 > scopeLevel2;
        } else if (fd1->begin.fileID != fd2->begin.fileID) {
            return fd1->begin.fileID < fd2->begin.fileID;
        } else {
            return fd1->begin < fd2->begin;
        }
    };
    // Sort the candidates according to the scope level. This ensures that the check starts with the maximum scope
    // level.
    size_t reExportSize = reExportAndCurrentPkgFuncTargets.size();
    std::sort(reExportAndCurrentPkgFuncTargets.begin(), reExportAndCurrentPkgFuncTargets.end(), scopeLevelCompare);
    reExport = false;
    if (importedFuncTargets.size() != 1) {
        std::sort(importedFuncTargets.begin(), importedFuncTargets.end(), scopeLevelCompare);
    } else {
        fdScopeLevelMap[importedFuncTargets[0]] =
            importedFuncTargets[0]->TestAttr(Attribute::ENUM_CONSTRUCTOR) ? 0 : -1;
    }
    std::copy(
        importedFuncTargets.begin(), importedFuncTargets.end(), std::back_inserter(reExportAndCurrentPkgFuncTargets));
    FuncCompT cmpScope = [&fdScopeLevelMap](auto fd1, auto fd2) { return fdScopeLevelMap[fd1] > fdScopeLevelMap[fd2]; };
    std::inplace_merge(reExportAndCurrentPkgFuncTargets.begin(),
        std::next(reExportAndCurrentPkgFuncTargets.begin(), static_cast<long>(reExportSize)),
        reExportAndCurrentPkgFuncTargets.end(), cmpScope);
    return reExportAndCurrentPkgFuncTargets;
}

std::vector<Ptr<FuncDecl>> TypeChecker::TypeCheckerImpl::MatchFunctionForCall(
    ASTContext& ctx, std::vector<Ptr<FuncDecl>>& candidates, CallExpr& ce, ModalTy target, SubstPack& typeMapping)
{
    TyVarScope ts(typeManager);
    std::vector<std::vector<ModalTy>> argCombinations = GetArgsCombination(ctx, ce);
    if (!ce.args.empty() && argCombinations.empty()) {
        return {};
    }
    auto filterSum = [this, &ce, target](std::vector<Ptr<FuncDecl>> ret) {
        if (auto ma = DynamicCast<MemberAccess*>(ce.baseFunc.get())) {
            if (ma->baseExpr->GetTy()->IsPlaceholder() && ret.size() == 1) {
                FilterSumUpperbound(*ma, *RawStaticCast<GenericsTy*>(ma->baseExpr->DataTy()), *ret[0]);
            }
        }
        // After a class ctor match, infer the call's modal from the contextual target type when the call site
        // does not specify one (e.g. let c: C@local? = C() gives C() the @local? modal). Skip when the call
        // already carries an explicit modal (e.g. C @local!()).
        if (Ty::IsTyCorrect(ce.GetTy()) && Ty::IsTyCorrect(target) &&
            (ce.callKind == CallKind::CALL_OBJECT_CREATION || ce.callKind == CallKind::CALL_STRUCT_CREATION) &&
            !ce.modal.HasLocal() && !ce.GetTy().IsLocalType()) {
            ce.SetTy(ce.GetTy().With(target.Mode()));
        }
        return ret;
    };
    if (candidates.size() == 1) {
        FunctionCandidate candidate{*candidates[0], ce, argCombinations};
        return filterSum(CheckFunctionMatch(ctx, candidate, target, typeMapping));
    }

    std::unordered_map<Ptr<FuncDecl>, int64_t> fdScopeLevelMap;
    auto orderedCandidates = GetOrderedCandidates(ctx, ce, candidates, fdScopeLevelMap);
    // Record the index of compatible target in targets and its coherent argument type pattern.
    std::vector<OwnedPtr<FunctionMatchingUnit>> legals;
    std::vector<FunctionMatchingUnit> illegals;
    // Record the scopeLevel.
    int64_t preScopeLevel = fdScopeLevelMap[orderedCandidates[0]];
    // set checkpoint for resetting side effects
    auto cs = PData::CommitScope(typeManager.constraints);
    // Loop through each candidate, check whether the arguments and parameters are compatible, and find out whether the
    // candidate function can be called. We only check the compatible candidates in the largest scopeLevel.
    for (size_t i = 0; i < orderedCandidates.size(); ++i) {
        auto ds = DiagSuppressor(diag);
        auto curScopeLevel = fdScopeLevelMap[orderedCandidates[i]];
        if (curScopeLevel != preScopeLevel && !legals.empty()) {
            // We only check the candidates in largest scope level.
            break;
        }
        FunctionCandidate candidate{*orderedCandidates[i], ce, argCombinations};
        auto ret = CheckCandidate(ctx, candidate, target, typeMapping);
        // Check result & If current function declaration is already matched, do not add again.
        if (!ret || (!legals.empty() && legals.back()->id == static_cast<int64_t>(i))) {
            illegals.emplace_back(*orderedCandidates[i], std::vector<ModalTy>(), SubstPack());
            illegals.back().diags = std::make_pair(ds.GetSuppressedDiag(), candidate.stat);
            PData::Reset(typeManager.constraints);
            continue;
        }
        ret->ver = PData::Stash<Constraint, CstVersionID>(typeManager.constraints);
        ret->diags = std::make_pair(ds.GetSuppressedDiag(), candidate.stat);
        ret->id = static_cast<int64_t>(i);
        preScopeLevel = curScopeLevel;
        legals.emplace_back(std::move(ret));
    }
    return filterSum(CheckMatchResult(ctx, ce, legals, illegals, target));
}

void TypeChecker::TypeCheckerImpl::ReInferCallArgs(
    ASTContext& ctx, const CallExpr& ce, const FunctionMatchingUnit& legal, ModalTy target)
{
    for (size_t i = 0; i < legal.tysInArgOrder.size(); ++i) {
        auto instTy = typeManager.ApplySubstPack(legal.tysInArgOrder[i], legal.typeMapping);
        if (legal.fd.TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
            if (ce.modal.HasLocal()) {
                instTy = instTy.With(ce.modal.ToModalInfo());
            } else if (target) {
                instTy = instTy.With(target.Mode());
            }
        }
        // Since the call has been checked, return value can be ignored.
        (void)CheckWithEffectiveCache(ctx, instTy, ce.args[i].get(), false);
    }
}

void TypeChecker::TypeCheckerImpl::RecoverCallArgs(
    ASTContext& ctx, const CallExpr& ce, const std::vector<ModalTy>& argsTys)
{
    // Recover arguments' types if current check failed but previous check passed (argsTys not empty).
    if (argsTys.empty() || argsTys.size() != ce.args.size()) {
        return;
    }
    if (ce.resolvedFunction) {
        UpdateCallTargetsForLSP(ce, *ce.resolvedFunction);
    }
    for (size_t i = 0; i < argsTys.size(); ++i) {
        (void)CheckWithEffectiveCache(ctx, argsTys[i], ce.args[i].get(), false);
    }
}

namespace {
/**
 * Choose the reasonable set of diagnoses to report again when their is not matching function.
 * @param ce The callExpr need to be resolved.
 * @param diagnoses The vector of diagnostics of all function candidates and their count of matched arguments.
 */
void CheckEmptyMatchResult(DiagnosticEngine& diag, CallExpr& ce,
    std::vector<FunctionMatchingUnit>& illegals)
{
    if (illegals.empty()) {
        return;
    }
    // Get diagnoses which have most matching arguments.
    std::vector<std::vector<Diagnostic>> diags;
    uint32_t maxMatched = 0;
    // Used to mark whether collected diagnoses context has valid argument names.
    bool hasValid = false;
    for (size_t i = 0; i < illegals.size(); ++i) {
        auto [diagInfo, stat] = illegals[i].diags;
        if (stat.matchedArgs > maxMatched) {
            maxMatched = stat.matchedArgs;
            diags.clear();
            hasValid = stat.argNameValid;
            diags.emplace_back(diagInfo);
        } else if (stat.matchedArgs == maxMatched) {
            // If argument valid status changed from invalid to valid, drop all collected diagnoses with invalid args.
            if (stat.argNameValid && !hasValid) {
                hasValid = true;
                diags.clear();
            }
            // Only collect diagnoses when the current status is the same with the stored status.
            if (stat.argNameValid == hasValid) {
                diags.emplace_back(diagInfo);
            }
        }
    }
    // Choose a set of diagnoses that contains error happens outside callExpr to ensure throwing the useful diagnoses.
    auto hasCallIrrelatedDiag = [&ce](const std::vector<Diagnostic>& diags) {
        return std::any_of(diags.begin(), diags.end(), [&ce](const auto& info) {
            return info.start != ce.begin && std::all_of(ce.args.begin(), ce.args.end(), [&info](const auto& arg) {
                // Check 'start' for old diag, 'mainHint.range.begin' for refactored diag.
                auto pos = info.start.IsZero() ? info.mainHint.range.begin : info.start;
                return !pos.IsZero() && pos != arg->begin;
            });
        });
    };
    size_t idx = 0;
    for (size_t i = 0; i < diags.size(); ++i) {
        if (hasCallIrrelatedDiag(diags[i])) {
            idx = i;
            break;
        }
    }
    std::for_each(diags[idx].begin(), diags[idx].end(), [&diag](auto info) { diag.Diagnose(info); });
}
} // namespace

std::vector<Ptr<FuncDecl>> TypeChecker::TypeCheckerImpl::CheckMatchResult(ASTContext& ctx, CallExpr& ce,
    std::vector<OwnedPtr<FunctionMatchingUnit>>& legals, std::vector<FunctionMatchingUnit>& illegals, ModalTy target)
{
    // No matching function.
    if (legals.empty()) {
        CheckEmptyMatchResult(diag, ce, illegals);
        return {};
    }
    // Only one legal target.
    uint64_t id;
    if (legals.size() == 1) {
        id = 0;
    } else {
        std::vector<Ptr<FuncDecl>> result;
        std::for_each(legals.begin(), legals.end(), [&result](auto& it) { result.emplace_back(&it->fd); });
        auto hasEnum = std::find_if(
            result.begin(), result.end(), [](auto& fd) { return fd->TestAttr(Attribute::ENUM_CONSTRUCTOR); });
        if (hasEnum != result.end()) {
            return result; // Find at least one enum candidates in all.
        }
        InstantiatePartOfTheGenericParameters(legals);
        auto indexRes = ResolveOverload(ctx, legals, ce, target);
        if (indexRes.size() == 1) {
            id = indexRes[0];
        } else {
            return (indexRes.size() > 1) ? result : std::vector<Ptr<FuncDecl>>();
        }
    }
    // Check and report suppressed diagnoses for matched function call.
    bool funcHasError = false;
    PData::Apply(typeManager.constraints, legals[id]->ver);
    for (const Diagnostic& funcDiag : legals[id]->diags.first) {
        if (funcDiag.diagSeverity == DiagSeverity::DS_ERROR) {
            funcHasError = true;
        }
        diag.Diagnose(funcDiag);
    }
    if (funcHasError) {
        return {};
    }
    if (legals[id]->fd.outerDecl && legals[id]->fd.outerDecl->astKind == ASTKind::INTERFACE_DECL) {
        if (auto ref = DynamicCast<NameReferenceExpr*>(ce.baseFunc.get())) {
            ref->matchedParentTy = legals[id]->fd.outerDecl->DataTy();
        }
    }
    // Infer the arguments' type again, to update ideal type and reference's targets which may overload.
    ReInferCallArgs(ctx, ce, *legals[id], target);
    auto ret = ReorderCallArgument(ctx, *legals[id], ce);
    return ret;
}

static std::optional<std::vector<ModalTy>> CheckCallArgs(
    DiagnosticEngine& diag, TypeManager& tyMgr, CallExpr& ce, FuncTy& funcTy)
{
    auto paramTys = funcTy.paramTys;
    if (funcTy.hasVariableLenArg && ce.args.size() > funcTy.paramTys.size()) {
        size_t varargsSize = ce.args.size() - funcTy.paramTys.size();
        for (size_t i = 0; i < varargsSize; i++) {
            paramTys.push_back(funcTy.isC ? tyMgr.GetCTypeTy() : tyMgr.GetAnyTy());
        }
    }
    if (paramTys.size() != ce.args.size()) {
        DiagWrongNumberOfArguments(diag, ce, paramTys);
        return std::nullopt;
    }
    for (auto& arg : ce.args) {
        if (!arg->name.Empty()) {
            diag.Diagnose(*arg, DiagKind::sema_unsupport_named_argument);
        }
    }
    return paramTys;
}

bool TypeChecker::TypeCheckerImpl::CheckFuncPtrCall(ASTContext& ctx, ModalTy target, CallExpr& ce, FuncTy& funcTy)
{
    // no need to check the arguments of CFunc call again
    if (IsValidCFuncConstructorCall(ce)) {
        return true;
    }
    CJC_NULLPTR_CHECK(ce.baseFunc);
    auto baseTarget = ce.baseFunc->GetTarget();
    if (baseTarget && baseTarget->astKind == ASTKind::TYPE_ALIAS_DECL) {
        auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_invalid_called_object, *ce.baseFunc);
        builder.AddNote("cannot call a type alias for a function type");
        return false;
    }
    auto paramTysOption = CheckCallArgs(diag, typeManager, ce, funcTy);
    if (!paramTysOption.has_value()) {
        return false;
    }
    auto paramTys = paramTysOption.value();
    SubstPack typeMapping;
    if (funcTy.HasGeneric()) {
        // Generate the type mapping for generic call first.
        typeMapping = GenerateGenericTypeMapping(ctx, *ce.baseFunc);
    }
    for (size_t i = 0; i < paramTys.size(); ++i) {
        // If is not generic call, type instantiation will have no effect.
        auto param = typeManager.ApplySubstPack(paramTys[i], typeMapping);
        if (!param) {
            return false;
        }
        if (!Check(ctx, param, ce.args[i].get())) {
            return false;
        }
    }
    // Infer the ideal type. Previous success indicates ce.args' member not null.
    for (size_t i = 0; i < paramTys.size(); ++i) {
        auto param = typeManager.ApplySubstPack(paramTys[i], typeMapping);
        if (!ce.args[i]->expr || !ce.args[i]->GetTy()->HasIdealTy()) {
            continue;
        }
        (void)Check(ctx, param, ce.args[i]->expr.get());
        if (ce.args[i]->expr->GetTy()->HasIdealTy()) {
            if (ce.ShouldDiagnose()) {
                diag.Diagnose(ce, DiagKind::sema_parameters_and_arguments_mismatch);
            }
            return false;
        }
        ce.args[i]->SetTy(ce.args[i]->expr->GetTy());
    }
    ce.SetTy(typeManager.ApplySubstPack(funcTy.retTy, typeMapping));
    if (target && !typeManager.IsSubtype(ce.GetTy(), target)) {
        DiagMismatchedTypes(diag, ce, target);
        return false;
    } else {
        ce.callKind = CallKind::CALL_FUNCTION_PTR; // Only set 'callKind' when check succeed (allow re-enter).
        return true;
    }
}

ModalTy TypeChecker::TypeCheckerImpl::SynCallExpr(ASTContext& ctx, CallExpr& ce)
{
    if (!ChkCallExpr(ctx, {}, ce)) {
        return {TypeManager::GetInvalidTy()};
    }
    return ce.GetTy();
}

ModalTy TypeChecker::TypeCheckerImpl::SynTrailingClosure(ASTContext& ctx, TrailingClosureExpr& tc)
{
    // TrailingClosureExpr will be desugared if ast node is valid.
    if (tc.desugarExpr) {
        tc.SetTy(Synthesize({ctx, SynPos::EXPR_ARG}, tc.desugarExpr.get()));
    } else {
        tc.SetTy({TypeManager::GetInvalidTy()});
    }
    return tc.GetTy();
}

bool TypeChecker::TypeCheckerImpl::ChkTrailingClosureExpr(ASTContext& ctx, ModalTy target, TrailingClosureExpr& tc)
{
    // TrailingClosureExpr will be desugared if ast node is valid.
    if (tc.desugarExpr) {
        if (Check(ctx, target, tc.desugarExpr.get())) {
            tc.SetTy(tc.desugarExpr->GetTy());
            return true;
        }
        tc.desugarExpr->SetTy({TypeManager::GetInvalidTy()});
    }
    tc.SetTy({TypeManager::GetInvalidTy()});
    return false;
}

static void RemoveMismatchedThisModeForCtorCall(const CallExpr& ce, std::vector<Ptr<FuncDecl>>& candidates)
{
    if (ce.modal.Empty()) {
        return;
    }
    /* When call ctor with explicit mode, remove ctor of mismatching mode. This is to avoid such err msg.
    error: mismatched types
     ==> nolocalinit.cj:6:13:
      |
    6 |     let d = Dog() @local!
      |             ^^^ expected 'Class-Dog @local!', found 'Class-Dog'
      |

    1 error generated, 1 error printed.
    */
    for (auto it = candidates.begin(); it != candidates.end();) {
        if (!IsInstanceConstructor(**it) && !(*it)->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
            ++it;
            continue;
        }
        if (!TypeManager::HasThisParam(**it)) {
            ++it;
            continue;
        }
        if (auto mode = TypeManager::GetThisParamMode(**it); mode != ce.modal.ToModalInfo()) {
            it = candidates.erase(it);
        } else {
            ++it;
        }
    }
}

bool TypeChecker::TypeCheckerImpl::GetCallBaseCandidates(
    const ASTContext& ctx, const CallExpr& ce, Expr& expr, Ptr<Decl>& target, std::vector<Ptr<FuncDecl>>& candidates)
{
    target = GetRealTarget(&expr, expr.GetTarget());
    if (target->TestAttr(Attribute::COMMON) && target->specificImplementation) {
        target = target->specificImplementation;
    }
    CJC_NULLPTR_CHECK(target);
    // only CPointer and VArray use special builtin decl checks
    if (IsBuiltinTypeAlias(*target) || target->TyKind() == TypeKind::TYPE_VARRAY ||
        target->TyKind() == TypeKind::TYPE_POINTER) {
        return false;
    }
    bool isFunctionCall = true;
    if (IsInstanceConstructor(*target) && target->outerDecl) {
        target = target->outerDecl;
    }
    if (target->IsStructOrClassDecl() || target->TyKind() == TypeKind::TYPE_ARRAY ||
        target->TyKind() == TypeKind::TYPE_CSTRING) {
        // Get constructors for current decl.
        for (auto& member : target->GetMemberDeclPtrs()) {
            CJC_NULLPTR_CHECK(member);
            bool originalCtor =
                IsInstanceConstructor(*member) && member->astKind == ASTKind::FUNC_DECL && member->identifier == "init";
            if (originalCtor) {
                (void)candidates.emplace_back(StaticCast<FuncDecl*>(member));
            }
        }
        isFunctionCall = false;
    } else {
        candidates = GetFuncTargets(expr);
    }
    RemoveDuplicateElements(candidates);
    if (ce.callKind == CallKind::CALL_ANNOTATION) {
        RemoveNonAnnotationCandidates(candidates);
    }
    RemoveMismatchedThisModeForCtorCall(ce, candidates);
    // Add for cjmp
    mpImpl->RemoveCommonCandidatesIfHasSpecific(candidates);
    auto ds = DiagSuppressor(diag);
    candidates = FilterCandidatesWithReExport(ctx, candidates, ce);
    if (expr.astKind == ASTKind::REF_EXPR && candidates.empty() && target->astKind == ASTKind::PACKAGE_DECL) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_cannot_ref_to_pkg_name, expr);
    }
    if (ds.HasError()) {
        ds.ReportDiag();
        return false;
    }
    ds.ReportDiag(); // Report warnings.
    if (!candidates.empty() && isFunctionCall) {
        target = candidates[0];
    }
    return true;
}

bool TypeChecker::TypeCheckerImpl::CheckRefConstructor(const ASTContext& ctx, const CallExpr& ce, const RefExpr& re)
{
    // Check if init() and super() is invoked outside constructor
    if (re.isSuper || re.isThis) {
        auto curFuncBody = GetCurFuncBody(ctx, re.scopeName);
        bool insideCtor{false};
        if (curFuncBody && curFuncBody->funcDecl && curFuncBody->funcDecl->TestAttr(Attribute::CONSTRUCTOR)) {
            insideCtor = true;
        }
        if (!insideCtor) {
            diag.Diagnose(ce, DiagKind::sema_invalid_this_call_outside_ctor, re.ref.identifier.Val());
            return false;
        }
    }
    return true;
}

bool TypeChecker::TypeCheckerImpl::ChkCallBaseRefExpr(
    ASTContext& ctx, CallExpr& ce, Ptr<Decl>& target, std::vector<Ptr<FuncDecl>>& candidates)
{
    CJC_NULLPTR_CHECK(ce.baseFunc);
    CJC_ASSERT(ce.baseFunc->astKind == ASTKind::REF_EXPR);
    auto re = StaticAs<ASTKind::REF_EXPR>(ce.baseFunc.get());
    re->isAlone = false;
    re->callOrPattern = &ce;
    ctx.targetTypeMap[re] = ctx.targetTypeMap[&ce]; // For enum sugar type infer.
    Synthesize({ctx, SynPos::EXPR_ARG}, re);
    auto reRealTarget = GetRealTarget(re, re->GetTarget());
    if (re->GetTy()->IsCFunc() && reRealTarget && reRealTarget->IsBuiltIn()) {
        return false;
    } else if (!re->ref.target && re->ref.targets.empty()) {
        return false;
    } else {
        return CheckRefConstructor(ctx, ce, *re) && GetCallBaseCandidates(ctx, ce, *re, target, candidates);
    }
}

bool TypeChecker::TypeCheckerImpl::ChkCallBaseMemberAccess(
    ASTContext& ctx, CallExpr& ce, Ptr<Decl>& target, std::vector<Ptr<FuncDecl>>& candidates)
{
    auto ma = StaticAs<ASTKind::MEMBER_ACCESS>(ce.baseFunc.get());
    ma->callOrPattern = &ce;
    ma->isAlone = false;
    // in case base is enum, its type arg may be inferred from func call, so disable error report here
    ctx.targetTypeMap[ma->baseExpr] = {TypeManager::GetQuestTy()};
    Synthesize({ctx, SynPos::EXPR_ARG}, ma);
    ctx.targetTypeMap[ma->baseExpr] = {};
    auto maRealTarget = GetRealTarget(ma, ma->GetTarget());
    if (ma->GetTy() && ma->GetTy()->IsNothing()) {
        return true;
    } else if (ma->GetTy() && ma->GetTy()->IsCFunc() && maRealTarget && maRealTarget->IsBuiltIn()) {
        return false;
    } else if (!ma->target && ma->targets.empty()) {
        // 'varr' is VArray type, 'varr.size()' is illegitimate, 'size' of VArray is a property.
        if (Is<VArrayTy>(ma->baseExpr->DataTy()) && ma->field == "size") {
            diag.Diagnose(ce, DiagKind::sema_no_match_operator_function_call);
            ma->SetTy({TypeManager::GetInvalidTy()});
        }
        return false;
    } else {
        return GetCallBaseCandidates(ctx, ce, *ma, target, candidates);
    }
}

namespace {
bool CanReachFuncCallByQuestable(Node& root)
{
    bool canReachFuncCall = false;
    Walker(&root, [&canReachFuncCall](Ptr<Node> n) {
        if (Is<TrailingClosureExpr>(n) || Is<CallExpr>(n)) {
            canReachFuncCall = true;
            return VisitAction::STOP_NOW;
        }
        if (!n || !IsQuestableNode(*n)) {
            return VisitAction::SKIP_CHILDREN;
        } else {
            return VisitAction::WALK_CHILDREN;
        }
    }).Walk();
    return canReachFuncCall;
}
} // namespace

/*
 * Used to help type argument inference for curried function call,
 * where the args are continuously given in a call chain like f(1)(2)(3).
 * Expected return type for the entire call chain and the last argument's type
 * are piggybacked to targetForBase.
 * This procedure could be reached recursively if the baseFunc is yet another function call
 * but not the first one.
 *
 * For example:
 * Check(`f(1)(2)(3)`, Int64`)
 * -> Check(`f(1)(2)`, (Int64)->Int64)
 *    -> Check(`f(1)`, (Int64)->(Int64)->Int64)
 *
 * If the target type or any argument's type is unknown, it is replaced by QuestTy.
 * Since targetForBase is an overestimation with noCast set and possibly QuestTy,
 * it can only pass down questable nodes to ensure it's not misused as the ty of any Node.
 */
bool TypeChecker::TypeCheckerImpl::ChkCurryCallBase(ASTContext& ctx, CallExpr& ce, ModalTy targetRet)
{
    std::vector<ModalTy> paramTys;
    ModalTy retTy = targetRet ? targetRet : ModalTy{TypeManager::GetQuestTy()};
    {
        auto ds = DiagSuppressor(diag);
        for (auto& arg: ce.args) {
            if (Ty::IsInitialTy(arg->DataTy())) {
                SynthesizeWithCache({ctx, SynPos::EXPR_ARG}, arg);
            }
            if (arg->GetTy().IsCorrect()) {
                paramTys.push_back(arg->GetTy());
            } else {
                paramTys.push_back({TypeManager::GetQuestTy()});
            }
        }
    }
    auto targetForBase = typeManager.GetFunctionTy(paramTys, retTy, {false, false, false, true});
    return Check(ctx, {targetForBase}, ce.baseFunc.get());
}

/*
 * Constrain the return type of an immediately-invoked lambda base (`({=> ...}())`)
 * with the call's expected return type, so ideal literals in the lambda body can be
 * unified against the contextual type instead of degrading to Int64/Float64.
 * Returns true when the lambda is checked this way (caller skips the fallback
 * Synthesize); returns false when no target is available or the check fails.
 */
bool TypeChecker::TypeCheckerImpl::ChkImmediateLamCallBase(ASTContext& ctx, CallExpr& ce, AST::ModalTy targetRet)
{
    auto lam = DynamicCast<LambdaExpr>(ce.baseFunc.get());
    if (lam == nullptr || !targetRet || !Ty::IsTyCorrect(targetRet) || targetRet->IsQuest()) {
        return false;
    }
    auto paramTys = TypeCheckUtil::GetFuncBodyParamTys(*lam->funcBody);
    DataTy targetForBase = typeManager.GetFunctionTy(paramTys, targetRet, {false, false, false, true});
    auto ds = DiagSuppressor(diag);
    bool isWellTyped = Check(ctx, targetForBase, ce.baseFunc.get());
    ds.ReportDiag();
    if (!isWellTyped) {
        ce.baseFunc->Clear();
    }
    return isWellTyped;
}

bool TypeChecker::TypeCheckerImpl::ChkCallBaseExpr(
    ASTContext& ctx, CallExpr& ce, Ptr<Decl>& targetDecl, ModalTy targetRet, std::vector<Ptr<FuncDecl>>& candidates)
{
    auto setFuncArgsInvalidTy = [&ce]() {
        ce.SetTy({TypeManager::GetInvalidTy()});
        // Skip the check for function's arguments.
        std::for_each(ce.args.begin(), ce.args.end(), [](const OwnedPtr<FuncArg>& fa) {
            CJC_NULLPTR_CHECK(fa->expr);
            if (!fa->expr->GetTy().IsCorrect()) {
                fa->expr->SetTy({TypeManager::GetInvalidTy()});
            }
            fa->SetTy({TypeManager::GetInvalidTy()});
        });
    };
    if (ce.baseFunc->desugarExpr != nullptr && !Is<TrailingClosureExpr>(ce.baseFunc.get())) {
        if (!Synthesize({ctx, SynPos::EXPR_ARG}, ce.baseFunc.get()).IsCorrect()) {
            setFuncArgsInvalidTy();
            return false;
        }
        return true;
    }
    bool isWellTyped = true;
    auto cs = PData::CommitScope(typeManager.constraints);
    switch (ce.baseFunc->astKind) {
        case ASTKind::REF_EXPR:
            isWellTyped = ChkCallBaseRefExpr(ctx, ce, targetDecl, candidates);
            break;
        case ASTKind::MEMBER_ACCESS:
            isWellTyped = ChkCallBaseMemberAccess(ctx, ce, targetDecl, candidates);
            break;
        default:
            bool mayCurry = false;
            if (CanReachFuncCallByQuestable(*ce.baseFunc)) {
                auto ds = DiagSuppressor(diag);
                mayCurry = ChkCurryCallBase(ctx, ce, targetRet);
            }
            if (!mayCurry) {
                PData::Reset(typeManager.constraints);
                ce.baseFunc->Clear();
                // An immediately-invoked lambda argument has no contextual function-parameter
                // type; constrain its return type with the call's expected return type so ideal
                // literals in the body (e.g. `goo({=> return 0}())` expecting `Int32`) unify.
                if (ChkImmediateLamCallBase(ctx, ce, targetRet)) {
                    break;
                }
                Synthesize({ctx, SynPos::EXPR_ARG}, ce.baseFunc.get());
            }
            break;
    }
    if (!isWellTyped && !ce.baseFunc->GetTy().IsCorrect()) {
        setFuncArgsInvalidTy();
    }
    return isWellTyped;
}

void TypeChecker::TypeCheckerImpl::CheckMacroCall(ASTContext& ctx, Node& macroNode)
{
    if (macroNode.TestAttr(Attribute::IS_CHECK_VISITED)) {
        return;
    }
    // Mark the current macroNode is checked.
    macroNode.EnableAttr(Attribute::IS_CHECK_VISITED);
    // Note: The MacroCall Check here is designed to be used for IDE LSP, because normally macroCalls
    // do not exist after expansion. Now we do keep orginal macroCall AST, and put it under the
    // its current File Node(managed by unique_ptr) in case any toolchain(LSP, etc) wants to access
    // the original Nodes before macro expansion.
    // But the following implementation is out-of-date and should be rewritten. See issue 175.
    auto targetHandler = [&ctx, &macroNode, this](const auto& kind) -> void {
        auto me = static_cast<std::decay_t<decltype(kind)>*>(&macroNode);
        if (me->invocation.target != nullptr) {
            return;
        }
        // For lsp hover, No error is reported during semantic analysis of code in macrocall(through judging by
        // isInMacroCall field of the node which is diagnosed).
        if (ci->invocation.globalOptions.enableMacroInLSP && me->invocation.decl &&
            !me->invocation.decl->TestAttr(Attribute::IS_CHECK_VISITED)) {
            Synthesize({ctx, SynPos::NONE}, me->invocation.decl.get());
        }
        auto decls = importManager.GetImportedDeclsByName(*me->curFile, me->identifier);
        if (decls.empty()) {
            return;
        }
        Utils::EraseIf(decls, [](Ptr<const Decl> d) -> bool { return !d || !d->TestAttr(Attribute::MACRO_FUNC); });
        for (const auto& it : decls) {
            Ptr<FuncDecl> funcDecl{nullptr};
            if (it->astKind == ASTKind::MACRO_DECL) {
                auto macroDecl = RawStaticCast<MacroDecl*>(it);
                funcDecl = macroDecl->desugarDecl.get();
            } else {
                funcDecl = StaticAs<ASTKind::FUNC_DECL>(it);
            }
            if (!funcDecl || !funcDecl->funcBody) {
                continue;
            }
            if (funcDecl->funcBody && funcDecl->funcBody->paramLists.empty()) {
                continue;
            }
            auto isCommonMacroCall = !me->invocation.HasAttr();
            auto isCommonMacroDecl = funcDecl->funcBody->paramLists.front()->params.size() == MACRO_COMMON_ARGS;
            if (isCommonMacroCall == isCommonMacroDecl) {
                ReplaceTarget(me, funcDecl);
            }
        }
    };
    if (macroNode.astKind == ASTKind::MACRO_EXPAND_EXPR) {
        targetHandler(MacroExpandExpr());
    } else if (macroNode.astKind == ASTKind::MACRO_EXPAND_DECL) {
        targetHandler(MacroExpandDecl());
    } else if (macroNode.astKind == ASTKind::MACRO_EXPAND_PARAM) {
        targetHandler(MacroExpandParam());
    }
}

bool TypeChecker::TypeCheckerImpl::CheckCallKind(const CallExpr& ce, Ptr<Decl> decl, CallKind& type)
{
    if (!decl) {
        return false;
    }
    switch (decl->astKind) {
        case ASTKind::INTERFACE_DECL:
            diag.Diagnose(ce, DiagKind::sema_interface_can_not_be_instantiated, decl->identifier.Val());
            break;
        case ASTKind::CLASS_DECL:
        case ASTKind::STRUCT_DECL:
        case ASTKind::BUILTIN_DECL: {
            // Call of type name for constructor, eg. class A {} -> A(), struct R{} -> R().
            // Call of 'this()' is treated as normal declared function call.
            auto re = As<ASTKind::REF_EXPR>(ce.baseFunc.get());
            bool isSuper = re && re->isSuper;
            bool isThis = re && re->isThis;
            auto cd = As<ASTKind::CLASS_DECL>(decl);
            if (cd && cd->TestAttr(Attribute::ABSTRACT) && !isSuper && !isThis) {
                diag.Diagnose(ce, DiagKind::sema_abstract_class_can_not_be_instantiated, cd->identifier.Val());
                break;
            }
            if (isThis) {
                type = CallKind::CALL_DECLARED_FUNCTION;
            } else if (decl->astKind == ASTKind::CLASS_DECL) {
                type = isSuper ? CallKind::CALL_SUPER_FUNCTION : CallKind::CALL_OBJECT_CREATION;
            } else {
                type = CallKind::CALL_STRUCT_CREATION;
            }
            break;
        }
        case ASTKind::FUNC_DECL: {
            auto ma = As<ASTKind::MEMBER_ACCESS>(ce.baseFunc.get());
            auto reBase = ma ? As<ASTKind::REF_EXPR>(ma->baseExpr.get()) : nullptr;
            if (reBase && reBase->isSuper) {
                type = CallKind::CALL_SUPER_FUNCTION;
            } else {
                type = decl->TestAttr(Attribute::INTRINSIC) ? CallKind::CALL_INTRINSIC_FUNCTION
                                                            : CallKind::CALL_DECLARED_FUNCTION;
            }
            break;
        }
        default:
            return false;
    }
    return true;
}

DiagKind TypeChecker::TypeCheckerImpl::GetErrorKindForCall(const std::vector<Ptr<FuncDecl>>& candidatesBeforeCheck,
    const std::vector<Ptr<FuncDecl>>& candidatesAfterCheck, const CallExpr& ce) const
{
    bool hasEmpty = candidatesBeforeCheck.empty() || candidatesAfterCheck.empty();
    auto target = ce.baseFunc ? ce.baseFunc->GetTarget() : nullptr;
    if (hasEmpty) {
        if (IsSearchTargetInMemberAccessUpperBound(ce)) {
            return DiagKind::sema_generic_no_method_match_in_upper_bounds;
        } else if (target && target->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
            return DiagKind::sema_enum_constructor_type_not_match;
        } else if (ce.callKind == CallKind::CALL_OBJECT_CREATION || ce.callKind == CallKind::CALL_STRUCT_CREATION) {
            return DiagKind::sema_no_match_constructor;
        } else {
            return DiagKind::sema_no_match_function_declaration_for_call;
        }
    } else {
        if (IsSearchTargetInMemberAccessUpperBound(ce)) {
            return DiagKind::sema_generic_ambiguous_method_match_in_upper_bounds;
        } else if (target && target->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
            return DiagKind::sema_multiple_constructor_in_enum;
        } else if (ce.callKind == CallKind::CALL_OBJECT_CREATION || ce.callKind == CallKind::CALL_STRUCT_CREATION) {
            return DiagKind::sema_ambiguous_constructor_match;
        } else {
            return DiagKind::sema_ambiguous_match;
        }
    }
}

void TypeChecker::TypeCheckerImpl::DiagnoseForCall(const std::vector<Ptr<FuncDecl>>& candidatesBeforeCheck,
    const std::vector<Ptr<FuncDecl>>& candidatesAfterCheck, CallExpr& ce, const Decl& decl)
{
    // Ignore desugared and macro added callExpr.
    if (ce.sourceExpr || ce.baseFunc->TestAttr(Attribute::MACRO_INVOKE_BODY)) {
        return;
    }
    // If ce contains invalid type, diagnostics are reported before.
    bool invalid = !candidatesBeforeCheck.empty() && candidatesAfterCheck.empty() && ce.baseFunc->GetTy().IsCorrect() &&
        Utils::In(ce.args, [](const auto& arg) { return !arg->GetTy().IsCorrect(); });
    if (invalid) {
        return;
    }
    bool isSuperCreation = ce.callKind == CallKind::CALL_SUPER_FUNCTION && decl.astKind != ASTKind::FUNC_DECL;
    std::string identifier = isSuperCreation ? "super" : decl.identifier.Val();
    Position identifierPos{ce.begin};
    if (auto re = DynamicCast<RefExpr*>(ce.baseFunc.get()); re) {
        identifier = re->ref.identifier; // We should diagnose with alias rather than real name.
    } else if (auto memberAccess = DynamicCast<MemberAccess>(ce.baseFunc.get());
        memberAccess && !memberAccess->field.ZeroPos()) {
        identifierPos = memberAccess->field.Begin();
    }
    DiagKind errorKind = GetErrorKindForCall(candidatesBeforeCheck, candidatesAfterCheck, ce);
    if (candidatesBeforeCheck.empty()) {
        ce.callKind = CallKind::CALL_INVALID; // When 'candidatesBeforeCheck' is empty, reset call kind.
        auto builder = diag.Diagnose(ce, identifierPos, errorKind, identifier);
        if (auto ma = DynamicCast<MemberAccess*>(ce.baseFunc.get())) {
            RecommendImportForMemberAccess(typeManager, importManager, *ma, &builder);
        }
        return;
    } else if (candidatesBeforeCheck.size() == 1) {
        // Errors should be reported before, optimizing the only 'no matching' error here.
        return;
    }
    bool hasEmpty = candidatesBeforeCheck.empty() || candidatesAfterCheck.empty();
    std::vector<Ptr<FuncDecl>> candidates = hasEmpty ? candidatesBeforeCheck : candidatesAfterCheck;
    DiagKind noteKind = hasEmpty ? DiagKind::sema_found_possible_candidate_decl : DiagKind::sema_found_candidate_decl;
    // Print error.
    std::vector<Ptr<Decl>> importedTargets;
    DiagnosticBuilder diagInfo = diag.Diagnose(ce, identifierPos, errorKind, identifier);
    // Print all candidates.
    for (auto& it : candidates) {
        CJC_NULLPTR_CHECK(it);
        if (it->TestAttr(Attribute::IMPORTED)) {
            importedTargets.emplace_back(it);
        } else {
            diagInfo.AddNote(*it, it->identifier.Begin(), noteKind);
        }
    }
    AddDiagNotesForImportedDecls(
        diagInfo, importManager.GetImportsOfDecl(ce.curFile->curPackage->fullPackageName), importedTargets);
}

Ptr<Decl> TypeChecker::TypeCheckerImpl::GetDeclOfThisType(const Expr& expr) const
{
    Ptr<Decl> decl = nullptr;
    if (auto ma = DynamicCast<const MemberAccess*>(&expr); ma && ma->baseExpr) {
        if (auto ct = DynamicCast<ClassTy>(ma->baseExpr->DataTy()); ct) {
            decl = ct->declPtr;
        }
    } else if (auto re = DynamicCast<const RefExpr*>(&expr); re) {
        Ptr<Decl> baseFunc = re->GetTarget();
        if (auto vd = DynamicCast<VarDecl*>(baseFunc);
            vd && vd->initializer != nullptr && vd->initializer->astKind == ASTKind::MEMBER_ACCESS) {
            // get decl of This type from non normal call
            Ptr<MemberAccess> maInitializer = RawStaticCast<MemberAccess*>(vd->initializer.get());
            Ptr<Decl> baseTarget = maInitializer->baseExpr->GetTarget();
            if (baseTarget != nullptr && baseTarget->GetTy() && baseTarget->GetTy()->IsClass()) {
                decl = RawStaticCast<ClassTy*>(baseTarget->DataTy())->declPtr;
            }
        } else {
            // get decl of This type from normal call
            CJC_NULLPTR_CHECK(expr.curFile);
            auto ctx = ci->GetASTContextByPackage(expr.curFile->curPackage);
            CJC_NULLPTR_CHECK(ctx);
            auto outMostFunc = ScopeManager::GetOutMostSymbol(*ctx, SymbolKind::FUNC, expr.scopeName);
            if (outMostFunc && StaticCast<FuncDecl*>(outMostFunc->node)->funcBody->parentClassLike) {
                decl = RawStaticCast<FuncDecl*>(outMostFunc->node)->funcBody->parentClassLike;
            }
        }
    }
    return decl;
}

std::optional<ModalTy> TypeChecker::TypeCheckerImpl::DynamicBindingThisType(
    Expr& baseExpr, const FuncDecl& fd, const SubstPack& typeMapping)
{
    if (!IsFuncReturnThisType(fd)) {
        return {};
    }
    auto funcTy = DynamicCast<FuncTy*>(
        baseExpr.GetTy().IsCorrect() && baseExpr.GetTy()->IsFunc() ? baseExpr.DataTy() : fd.DataTy());
    if (funcTy == nullptr) {
        return {};
    }
    auto declOfThisType = GetDeclOfThisType(baseExpr);
    auto thisMode = funcTy->retTy.Mode();
    if (auto cd = DynamicCast<ClassDecl*>(declOfThisType); cd && cd->GetTy().IsCorrect()) {
        auto instTy = typeManager.ApplySubstPack(typeManager.GetClassThisTy(*cd, cd->GetTy()->TyArgs()), typeMapping);
        baseExpr.SetTy({typeManager.GetFunctionTy(funcTy->paramTys, instTy), thisMode});
        return baseExpr.GetTy();
    } else if (auto ma = DynamicCast<MemberAccess*>(&baseExpr); ma && ma->baseExpr) {
        // If the baseExpr's type is generic type, set the function call's return type as 'gty'.
        if (auto gty = DynamicCast<GenericsTy>(ma->baseExpr->DataTy()); gty) {
            baseExpr.SetTy({typeManager.GetFunctionTy(funcTy->paramTys, {gty, thisMode})});
            return baseExpr.GetTy();
        }
    }
    return {};
}

FunctionMatchingUnit* TypeChecker::TypeCheckerImpl::FindFuncWithMaxChildRetTy(
    const CallExpr& ce, std::vector<std::unique_ptr<FunctionMatchingUnit>>& candidates)
{
    if (candidates.empty()) {
        return nullptr;
    }
    FunctionMatchingUnit* maxChildFmu = candidates[0].get();
    for (size_t i = 1; i < candidates.size(); i++) {
        auto ty1 = typeManager.ApplySubstPack(maxChildFmu->fd.GetTy(), maxChildFmu->typeMapping);
        auto ty2 = typeManager.ApplySubstPack(candidates[i]->fd.GetTy(), candidates[i]->typeMapping);
        // Errors which cause the types to be invalid should have been reported before.
        if (ty1->IsInvalid() || ty2->IsInvalid()) {
            continue;
        }
        auto meteRes = JoinAndMeet(typeManager, {ty1, ty2}).MeetAsVisibleTy();
        ModalTy maxChildRetTy{};
        auto [optErrs, metTy] = JoinAndMeet::SetMetType(maxChildRetTy, meteRes);
        maxChildRetTy = metTy;
        if (optErrs) {
            (void)diag.Diagnose(ce, DiagKind::sema_diag_report_note_message, *optErrs);
        };
        if (maxChildRetTy->IsInvalid()) {
            return nullptr;
        } else if (maxChildRetTy == ty2 && ty1 != ty2) {
            maxChildFmu = candidates[i].get();
        } else {
            continue;
        }
    }
    return maxChildFmu;
}

namespace {
bool AllowsCallPostfixModal(const CallExpr& ce, Ptr<const Decl> decl)
{
    switch (ce.callKind) {
        case CallKind::CALL_OBJECT_CREATION:
        case CallKind::CALL_STRUCT_CREATION:
            return true;
        case CallKind::CALL_DECLARED_FUNCTION:
            return decl && decl->TestAttr(Attribute::ENUM_CONSTRUCTOR);
        default:
            return false;
    }
}

// only ctor call can have postfix modal
void CheckCallPostfixModal(DiagnosticEngine& diag, const CallExpr& ce, Ptr<const Decl> decl)
{
    if (!ce.modal.HasLocal() || AllowsCallPostfixModal(ce, decl)) {
        return;
    }
    diag.DiagnoseRefactor(DiagKindRefactor::sema_mode_on_non_constructor_call,
        MakeRange(ce.modal.LocalBegin(), ce.modal.LocalEnd()));
}
}

bool TypeChecker::TypeCheckerImpl::PostCheckCallExpr(
    const ASTContext& ctx, CallExpr& ce, FuncDecl& func, const SubstPack& typeMapping)
{
    CheckCallPostfixModal(diag, ce, &func);
    if (!func.TestAttr(Attribute::CONSTRUCTOR) && !CheckStaticCallNonStatic(ctx, ce, func)) {
        // Reset status for re-enter callCheck.
        ce.SetTy({TypeManager::GetInvalidTy()});
        ce.callKind = CallKind::CALL_INVALID;
        return false;
    }
    // Check whether the instantiated type arguments still has unimplemented static functions.
    if (func.TestAttr(Attribute::GENERIC)) {
        auto typeArgs = ce.baseFunc->GetTypeArgs();
        auto ref = DynamicCast<NameReferenceExpr*>(ce.baseFunc.get());
        // Currently, only name reference node can have typeArgument.
        auto isGenericLegal = !(typeArgs.empty() && ref && ref->instTys.empty()) &&
            CheckCallGenericDeclInstantiation(&func, typeArgs, *ce.baseFunc);
        if (!isGenericLegal) {
            ce.SetTy({TypeManager::GetInvalidTy()});
            ce.callKind = CallKind::CALL_INVALID;
            return false;
        }
    }
    ce.resolvedFunction = &func;
    ce.baseFunc->SetTarget(&func);
    UpdateCallTargetsForLSP(ce, func);
    DynamicBindingThisType(*ce.baseFunc, func, typeMapping);
    if (!func.TestAttr(Attribute::FOREIGN) && !func.TestAttr(Attribute::C)) {
        bool valid = true;
        for (auto& fa : ce.args) {
            if (fa->withInout) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_can_only_used_in_cfunc_calling, *fa);
                valid = false;
            }
        }
        if (!valid) {
            return false;
        }
    } else {
        // VArray must modify by 'inout' in CFunc call.
        auto fa = std::find_if(
            ce.args.begin(), ce.args.end(), [](auto& fa) { return Is<VArrayTy>(fa->DataTy()) && !fa->withInout; });
        if (fa != ce.args.end()) {
            (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_mismatch, *fa->get(), VARRAY_NAME);
            return false;
        }
    }
    if (func.GetTy()->HasQuestTy()) {
        DiagUnableToInferReturnType(diag, func, ce);
        ce.SetTy({TypeManager::GetInvalidTy()});
        ce.callKind = CallKind::CALL_INVALID;
        return false;
    }
    return true;
}

namespace {
Ptr<FuncTy> MakePlaceholderFuncTy(size_t arity, TypeManager& tyMgr)
{
    std::vector<ModalTy> paramTys;
    ModalTy retTy = {tyMgr.AllocTyVar()};
    for (size_t i = 0; i < arity; i++) {
        paramTys.push_back({tyMgr.AllocTyVar()});
    }
    return tyMgr.GetFunctionTy(paramTys, retTy);
}

Ptr<FuncTy> TryCastingToFuncTy(ModalTy ty, size_t arity, TypeManager& tyMgr)
{
    if (auto funcTy = DynamicCast<FuncTy*>(ty.Ty()); funcTy) {
        return funcTy;
    }
    if (auto genTy = DynamicCast<GenericsTy*>(ty.Ty()); genTy) {
        if (genTy->isPlaceholder) {
            if (auto placeholderFuncTy = tyMgr.ConstrainByCtor(*genTy, *MakePlaceholderFuncTy(arity, tyMgr))) {
                return StaticCast<FuncTy>(placeholderFuncTy.Ty());
            } else {
                return nullptr;
            }
        }
        auto allUpperBounds = genTy->upperBounds;
        for (auto& upperBound : allUpperBounds) {
            if (auto funcTy = DynamicCast<FuncTy*>(upperBound); funcTy) {
                return funcTy;
            }
        }
    }
    return nullptr;
};
}

bool TypeChecker::TypeCheckerImpl::CheckNonNormalCall(ASTContext& ctx, ModalTy target, CallExpr& ce)
{
    CJC_NULLPTR_CHECK(ce.baseFunc);
    bool res = false;
    if (auto funcTy = TryCastingToFuncTy(ce.baseFunc->GetTy(), ce.args.size(), typeManager)) {
        auto maybeVariadic = IsPossibleVariadicFunction(*funcTy, ce);
        std::vector<Diagnostic> diagnostics;
        if (maybeVariadic) {
            auto ds = DiagSuppressor(diag);
            res = CheckFuncPtrCall(ctx, target, ce, *funcTy);
            diagnostics = ds.GetSuppressedDiag();
        } else {
            res = CheckFuncPtrCall(ctx, target, ce, *funcTy);
        }
        if (!res && maybeVariadic) {
            DesugarVariadicCallExpr(ctx, ce, funcTy->paramTys.size() - 1);
            bool success = false;
            {
                auto ds = DiagSuppressor(diag);
                success = CheckFuncPtrCall(ctx, target, *StaticAs<ASTKind::CALL_EXPR>(ce.desugarExpr.get()), *funcTy);
                if (diagnostics.empty()) {
                    diagnostics = ds.GetSuppressedDiag();
                }
            }
            if (success) {
                ce.SetTy(ce.desugarExpr->GetTy());
                res = true;
            } else {
                RecoverFromVariadicCallExpr(ce);
                std::for_each(diagnostics.cbegin(), diagnostics.cend(), [this](auto info) { diag.Diagnose(info); });
                ce.SetTy({TypeManager::GetInvalidTy()});
                res = false;
            }
        }
        // VArray must modify by 'inout' in CFunc lambda call.
        if (funcTy->IsCFunc()) {
            auto fa = std::find_if(
                ce.args.begin(), ce.args.end(), [](auto& fa) { return Is<VArrayTy>(fa->DataTy()) && !fa->withInout; });
            if (fa != ce.args.end()) {
                (void)diag.DiagnoseRefactor(DiagKindRefactor::sema_inout_mismatch, *fa->get(), VARRAY_NAME);
                res = false;
            }
        }
        if (funcTy->isC && !ce.TestAttr(Attribute::UNSAFE) && !IsValidCFuncConstructorCall(ce)) {
            diag.Diagnose(ce, DiagKind::sema_unsafe_function_invoke_failed);
            res = false;
        }
    } else if (ChkFunctionCallExpr(ctx, target, ce)) {
        res = true;
    }
    if (!res) {
        ce.SetTy({TypeManager::GetInvalidTy()});
    }
    return res;
}

void TypeChecker::TypeCheckerImpl::PostProcessForLSP(CallExpr& ce, const std::vector<Ptr<FuncDecl>>& result) const
{
    if (result.size() > 1) {
        // According to demand from LSP, a target should be bind when conflict happens.
        // Caller guarantees no nullptr existing in the 'result'.
        UpdateCallTargetsForLSP(ce, *result[0]);
        ce.resolvedFunction = result[0];
    } else {
        ReplaceTarget(ce.baseFunc.get(), nullptr);
        ce.baseFunc->SetTy({TypeManager::GetInvalidTy()});
    }
}

// The type of CallExpr depends on the type of its baseExpr, which can be refExpr or memberAccess.
bool TypeChecker::TypeCheckerImpl::ChkCallExpr(ASTContext& ctx, ModalTy target, CallExpr& ce)
{
    if (ce.desugarExpr) {
        return ChkDesugarExprOfCallExpr(ctx, target, ce);
    }
    if (!ce.baseFunc) {
        return false;
    }

    bool prevCheckTrue =
        ce.callKind != CallKind::CALL_INVALID && ce.GetTy().IsCorrect() && typeManager.GetUnsolvedTyVars().empty();
    std::vector<ModalTy> argsTys;
    if (prevCheckTrue) {
        std::transform(
            ce.args.begin(), ce.args.end(), std::back_inserter(argsTys), [](auto& arg) { return arg->GetTy(); });
    }
    // Set type as not null before starting type check.
    ce.SetTy(TypeManager::GetNonNullTy(ce.GetTy()));

    // Step 1: Check call expression's base expression, get valid function candidates & decl.
    // Decl only be set when call base is valid RefExpr or MemberAccess.
    Ptr<Decl> decl{nullptr};
    std::vector<Ptr<FuncDecl>> candidates;
    if (!ChkCallBaseExpr(ctx, ce, decl, target, candidates)) {
        CheckCallPostfixModal(diag, ce, decl);
        // If no call base exist, expr may be array or pointer builtin api call.
        return ChkBuiltinCall(ctx, TypeManager::GetNonNullTy(target), ce);
    }
    if (!ce.baseFunc->GetTy().IsCorrect()) {
        return false;
    }
    if (ce.baseFunc->GetTy()->IsNothing()) {
        return SynArgsOfNothingBaseExpr(ctx, ce);
    }
    // Check ToTokens interface implementation.
    if (ce.needCheckToTokens) {
        CheckToTokensImpCallExpr(ce);
    }

    // Step 2: Check & set callKind.
    // Function pointer call, operator () function call and non-valid call base, return false.
    if (!CheckCallKind(ce, decl, ce.callKind)) {
        CheckCallPostfixModal(diag, ce, decl);
        return CheckNonNormalCall(ctx, target, ce);
    }
    if (ce.callKind == CallKind::CALL_INVALID) {
        return false;
    }
    if (candidates.empty()) {
        DiagnoseForCall(candidates, candidates, ce, *decl);
        return false;
    }
    bool maybeEnumOverloadOP = IsPossibleEnumConstructor(candidates, *ce.baseFunc);
    bool maybeVariadicFunction = IsPossibleVariadicFunction(candidates, ce);
    bool maybeEnumOrVariadic = maybeEnumOverloadOP || maybeVariadicFunction;
    // Do not diagnose when candidates may be enum constructor or operator().
    // Step 3: Check whether function candidates matched.
    SubstPack typeMapping;
    std::vector<Ptr<FuncDecl>> result;
    std::vector<Diagnostic> diagnostics;
    PData::CommitScope cs(typeManager.constraints);
    if (maybeEnumOrVariadic) {
        auto ds = DiagSuppressor(diag);
        result = MatchFunctionForCall(ctx, candidates, ce, target, typeMapping);
        diagnostics = ds.GetSuppressedDiag();
    } else {
        result = MatchFunctionForCall(ctx, candidates, ce, target, typeMapping);
    }
    ce.SetTy(TypeManager::GetNonNullTy(ce.GetTy()));
    if (result.size() == 1) {
        return PostCheckCallExpr(ctx, ce, *result[0], typeMapping);
    }
    PData::Reset(typeManager.constraints);
    // If candidates may be enum constructor or operator(), clear baseFunc's ty when constructor mismatched.
    ce.baseFunc->SetTy(maybeEnumOrVariadic ? ModalTy{TypeManager::GetInvalidTy()} : ce.baseFunc->GetTy());
    auto ret = (maybeEnumOverloadOP && ChkFunctionCallExpr(ctx, target, ce)) ||
        (maybeVariadicFunction && result.empty() && ChkVariadicCallExpr(ctx, target, ce, candidates, diagnostics));
    if (ret) {
        return true;
    }
    // If no matching or having multiple matching candidates, generate diagnosis.
    if (diagnostics.empty()) {
        DiagnoseForCall(candidates, result, ce, *decl);
    }
    // Recover arguments' types if current check failed but previous check passed.
    RecoverCallArgs(ctx, ce, argsTys);
    if (diag.GetDiagnoseStatus()) {
        PostProcessForLSP(ce, result);
    } else if (!prevCheckTrue) {
        ce.callKind = CallKind::CALL_INVALID;
    }
    return false;
}

bool TypeChecker::TypeCheckerImpl::ChkDesugarExprOfCallExpr(ASTContext& ctx, ModalTy target, CallExpr& ce)
{
    if (ce.desugarExpr->GetTy().IsCorrect()) {
        ce.SetTy(ce.desugarExpr->GetTy());
        return !target || typeManager.IsSubtype(ce.desugarExpr->GetTy(), target);
    }
    bool isWellTyped;
    // If `ce` is a builtin call and it has been cleared, we should recheck the `desugarExpr`.
    if (Ty::IsTyCorrect(target)) {
        isWellTyped = Check(ctx, target, ce.desugarExpr.get());
    } else {
        isWellTyped = Synthesize({ctx, SynPos::EXPR_ARG}, ce.desugarExpr.get()).IsCorrect();
    }
    ce.SetTy(ce.desugarExpr->GetTy());
    return isWellTyped && ce.GetTy().IsCorrect();
}

bool TypeChecker::TypeCheckerImpl::SynArgsOfNothingBaseExpr(ASTContext& ctx, CallExpr& ce)
{
    bool isWellTyped = true;
    std::for_each(ce.args.cbegin(), ce.args.cend(), [this, &ctx, &isWellTyped](auto& arg) {
        isWellTyped = isWellTyped && Synthesize({ctx, SynPos::EXPR_ARG}, arg.get()).IsCorrect();
        ReplaceIdealTy(*arg);
        ReplaceIdealTy(*arg->expr);
    });
    ce.SetTy(isWellTyped ? ModalTy{TypeManager::GetNothingTy()} : ModalTy{TypeManager::GetInvalidTy()});
    return isWellTyped;
}

bool TypeChecker::TypeCheckerImpl::ChkFunctionCallExpr(ASTContext& ctx, ModalTy target, CallExpr& ce)
{
    DesugarCallExpr(ctx, ce);
    auto desugarCallExpr = StaticAs<ASTKind::CALL_EXPR>(ce.desugarExpr.get());
    if (!ChkCallExpr(ctx, target, *desugarCallExpr)) {
        RecoverToCallExpr(ce);
        diag.Diagnose(ce, DiagKind::sema_no_match_operator_function_call);
        ce.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
    ce.callKind = desugarCallExpr->callKind;
    ce.SetTy(desugarCallExpr->GetTy());
    return true;
}

bool TypeChecker::TypeCheckerImpl::ChkVariadicCallExpr(ASTContext& ctx, ModalTy target, CallExpr& ce,
    const std::vector<Ptr<FuncDecl>>& candidates, std::vector<Diagnostic>& diagnostics)
{
    if (!ChkArgsOrder(diag, ce)) {
        return false;
    }
    std::vector<Ptr<FuncDecl>> results;
    std::map<size_t, std::vector<Ptr<FuncDecl>>> fixedPositionalArityToGroup =
        FuncDeclsGroupByFixedPositionalArity(candidates, ce);
    // The `CallExpr` is desugarred based on the fixed positional arity. We group the candidates based on
    // the fixed positional arity and use `MatchFunctionForCall` to try the desugarred call.
    for (auto& [fixedPositionalArity, group] : fixedPositionalArityToGroup) {
        DesugarVariadicCallExpr(ctx, ce, fixedPositionalArity);
        auto ds = DiagSuppressor(diag);
        SubstPack typeMapping;
        std::vector<Ptr<FuncDecl>> matchResults =
            MatchFunctionForCall(ctx, group, *StaticAs<ASTKind::CALL_EXPR>(ce.desugarExpr.get()), target, typeMapping);
        if (matchResults.size() == 1) {
            results.emplace_back(matchResults.front());
        }
        if (diagnostics.empty()) {
            // The first diagnostic will be reported if the variadic call checked failed.
            diagnostics = ds.GetSuppressedDiag();
        }
        RecoverFromVariadicCallExpr(ce);
    }
    if (results.size() == 1) {
        size_t fixedPositionalArity = GetPositionalParamSize(*results.front()) - 1;
        DesugarVariadicCallExpr(ctx, ce, fixedPositionalArity);
        SubstPack typeMapping;
        std::vector<Ptr<FuncDecl>> matchResults = MatchFunctionForCall(
            ctx, results, *StaticAs<ASTKind::CALL_EXPR>(ce.desugarExpr.get()), target, typeMapping);
        CJC_ASSERT(matchResults.size() == 1);
        bool ret = PostCheckCallExpr(
            ctx, *StaticAs<ASTKind::CALL_EXPR>(ce.desugarExpr.get()), *matchResults.front(), typeMapping);
        if (ret) {
            ce.SetTy(ce.desugarExpr->GetTy());
            ce.resolvedFunction = StaticAs<ASTKind::CALL_EXPR>(ce.desugarExpr.get())->resolvedFunction;
        } else {
            RecoverFromVariadicCallExpr(ce);
        }
        return ret;
    } else if (results.empty()) {
        // If no matching functions are found, report the first diagnostic.
        std::for_each(diagnostics.cbegin(), diagnostics.cend(), [this](auto info) { diag.Diagnose(info); });
    } else {
        // Multiple matching functions found, report ambiguous function call.
        Ptr<Decl> decl = results.front();
        CJC_NULLPTR_CHECK(decl);
        if (decl->TestAttr(Attribute::CONSTRUCTOR) && decl->outerDecl) { decl = decl->outerDecl; }
        DiagnoseForCall(candidates, results, ce, *decl);
    }
    ce.callKind = CallKind::CALL_INVALID;
    return false;
}

void TypeChecker::TypeCheckerImpl::CheckToTokensImpCallExpr(const CallExpr& ce)
{
    auto toTokensDecl = importManager.GetAstDecl<InterfaceDecl>("ToTokens");
    // ce has the form: a.obj.ToTokens()
    if (ce.astKind != ASTKind::CALL_EXPR || !toTokensDecl) {
        return;
    }
    auto ma = StaticAs<ASTKind::MEMBER_ACCESS>(ce.baseFunc.get());
    auto be = ma->baseExpr.get();
    if (be) {
        auto prTys = promotion.Promote(be->GetTy(), toTokensDecl->GetTy());
        auto ty = prTys.empty() ? ModalTy{TypeManager::GetInvalidTy()} : *prTys.begin();
        // must use @~local mode here
        if (ty != toTokensDecl->GetTy()) {
            diag.Diagnose(ce, DiagKind::sema_invalid_tokens_implementation, be->GetTy().String());
        }
    }
}

bool TypeChecker::TypeCheckerImpl::CheckStaticCallNonStatic(
    const ASTContext& ctx, const CallExpr& ce, const FuncDecl& result)
{
    // Return true if result is not a structure decl's method.
    if (!result.outerDecl || !result.outerDecl->IsNominalDecl()) {
        return true;
    }

    if (auto re = DynamicCast<RefExpr*>(ce.baseFunc.get()); re) {
        return IsLegalAccessFromStaticFunc(ctx, *re, result);
    }
    return true;
}

void TypeChecker::TypeCheckerImpl::SpreadInstantiationTy(Node& node, const SubstPack& typeMapping)
{
    if (typeMapping.u2i.empty() || !node.GetTy().IsCorrect()) {
        return;
    }
    Walker walkerExpr(&node, [this, &typeMapping](Ptr<Node> n) -> VisitAction {
        if (n->GetTy().IsCorrect()) {
            n->SetTy(typeManager.ApplySubstPack(n->GetTy(), typeMapping));
            if (auto ma = DynamicCast<MemberAccess>(n.get()); ma && ma->baseExpr && ma->baseExpr->GetTy().IsCorrect()) {
                auto target = DynamicCast<VarDecl>(ma->GetTarget());
                if (!target || !HasModifier(target->modifiers, TokenKind::DEMODE)) {
                    n->SetTy(n->GetTy().With(ma->baseExpr->TyMode()));
                }
            }
            return VisitAction::WALK_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    });
    walkerExpr.Walk();
}
