// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Desugar functions used after typecheck step.
 */

#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "Desugar/AfterTypeCheck.h"

#include "AutoBoxing.h"
#include "ExtraScopes.h"
#include "NativeFFI/Java/AfterTypeCheck/JavaInteropManager.h"
#include "NativeFFI/ObjC/AfterTypeCheck/Desugar.h"
#include "TypeCheckUtil.h"
#include "TypeCheckerImpl.h"

#include "cangjie/AST/ASTCasting.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Driver/StdlibMap.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/Utils.h"
#include "cangjie/Utils/ProfileRecorder.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;
using namespace Sema::Desugar::AfterTypeCheck;

namespace {
OwnedPtr<TypeConvExpr> CreateConvInt64(VarDecl& vd, ModalTy ty)
{
    auto convType = MakeOwnedNode<PrimitiveType>();
    convType->kind = TypeKind::TYPE_INT64;
    convType->SetTy(ty);
    auto numConv = MakeOwnedNode<TypeConvExpr>();
    numConv->type = std::move(convType);
    numConv->expr = CreateRefExpr(vd);
    numConv->expr->SetTy(ty);
    numConv->SetTy(ty);
    return numConv;
}

OwnedPtr<FuncDecl> CreateMainInvokeDecl(OwnedPtr<FuncBody>&& funcBody, const FuncDecl& mainFunc, ModalTy funcTy)
{
    auto mainInvokeFunc = MakeOwnedNode<FuncDecl>();
    funcBody->funcDecl = mainInvokeFunc.get();
    mainInvokeFunc->toBeCompiled = mainFunc.toBeCompiled;
    mainInvokeFunc->SetTy(funcTy);
    mainInvokeFunc->identifier = MAIN_INVOKE;
    mainInvokeFunc->funcBody = std::move(funcBody);
    mainInvokeFunc->moduleName = mainFunc.moduleName;
    mainInvokeFunc->fullPackageName = mainFunc.fullPackageName;
    mainInvokeFunc->EnableAttr(Attribute::GLOBAL);
    AddCurFile(*mainInvokeFunc, mainFunc.curFile);
    if (mainFunc.TestAttr(Attribute::INCRE_COMPILE)) {
        mainInvokeFunc->EnableAttr(Attribute::INCRE_COMPILE);
    }
    return mainInvokeFunc;
}

/**
 * Non-primitive type property compound assignment will be desugared for operator overloading first.
 * eg: a.b += c where b is Non-primitive type is desugared to a.b = a.b.+(c)
 * And the 'a.b' part of 'a.b.+' should already have 'mapExpr'.
 * Above action is done before desugar of property accessing.
 * So we add 'mapExpr' to the base of property accessing for compound assignment of property in this function.
 */
void TrySetMappedExprForProp(Expr& expr, Ptr<Expr> mapExpr)
{
    if (expr.astKind != ASTKind::CALL_EXPR) {
        return;
    }
    auto ce = StaticAs<ASTKind::CALL_EXPR>(&expr);
    // Checking for 'mapExpr' on the 'a.b' of 'a.b.+(c)'.
    if (auto ma = DynamicCast<MemberAccess*>(ce->baseFunc.get()); ma && ma->baseExpr && ma->baseExpr->mapExpr) {
        // Adding 'mapExpr' on the 'a' of 'a.b'.
        if (auto baseMa = DynamicCast<MemberAccess*>(ma->baseExpr.get()); baseMa && baseMa->baseExpr) {
            baseMa->baseExpr->mapExpr = mapExpr;
            ma->baseExpr->mapExpr = nullptr; // Reset old 'mapExpr'
        }
    }
}

std::vector<OwnedPtr<FuncArg>> GetFuncArgsForDesugaredPropDecl(
    TypeManager& tyMgr, Expr& expr, ModalTy propTy, Ptr<Expr> mapExpr, FuncDecl& getFunc)
{
    std::vector<OwnedPtr<FuncArg>> args;
    TokenKind op = TokenKind::ADD;
    OwnedPtr<Expr> rightExpr;
    if (expr.astKind == ASTKind::ASSIGN_EXPR) {
        auto& ae = static_cast<AssignExpr&>(expr);
        auto argExpr = ASTCloner::Clone(ae.rightExpr.get());
        // Compound Assignment Operator
        if (ae.isCompound) {
            if (COMPOUND_ASSIGN_EXPR_MAP.find(ae.op) != COMPOUND_ASSIGN_EXPR_MAP.end()) {
                op = COMPOUND_ASSIGN_EXPR_MAP.at(ae.op);
            }
            rightExpr = std::move(argExpr);
        } else {
            // If the assignment is not compund, try to set 'mapExpr' on the right part and return it as argument.
            TrySetMappedExprForProp(*argExpr, mapExpr);
            args.emplace_back(CreateFuncArg(std::move(argExpr)));
            return args;
        }
    } else if (expr.astKind == ASTKind::INC_OR_DEC_EXPR) {
        // IncOrDecExpr only support primitive types, so this will never have operator overloading.
        auto& ide = static_cast<IncOrDecExpr&>(expr);
        op = ide.op == TokenKind::INCR ? TokenKind::ADD : TokenKind::SUB;
        rightExpr = CreateLitConstExpr(LitConstKind::INTEGER, "1", propTy);
    } else {
        // Caller guarantees the expr's kind only be ASSIGN_EXPR or INC_OR_DEC_EXPR.
        CJC_ABORT();
    }
    // Create argument part as "expr.propGet() + value".
    // NOTE: the expression here must not have operator overloading with binary operator
    // since that case has already desugared during typechecking.
    // For memberAccess case, the baseExpr of memberAccess should be mapped to one expression to avoid side effect.
    auto leftBaseExpr = mapExpr ? CreateMemberAccess(ASTCloner::Clone(mapExpr), getFunc)
                                : OwnedPtr<Expr>(CreateRefExpr(getFunc).release());
    // 'getFunc' may be generic type, which need to be update to real used type.
    leftBaseExpr->SetTy(ModalTy{tyMgr.GetFunctionTy({}, propTy)});
    auto isMemberAccessSuperCall = false;
    if (auto ma = DynamicCast<MemberAccess*>(leftBaseExpr.get()); ma) {
        CJC_NULLPTR_CHECK(ma->baseExpr);
        if (auto ref = DynamicCast<RefExpr*>(ma->baseExpr.get()); ref) {
            isMemberAccessSuperCall = ref->isSuper;
        }
        if (auto target = ma->baseExpr->GetTarget(); !target || !target->IsNominalDecl()) {
            ma->baseExpr->mapExpr = mapExpr;
        }
    }
    CopyBasicInfo(&expr, leftBaseExpr.get());
    auto leftExpr = CreateCallExpr(std::move(leftBaseExpr), {});
    leftExpr->callKind = isMemberAccessSuperCall ? CallKind::CALL_SUPER_FUNCTION : CallKind::CALL_DECLARED_FUNCTION;
    leftExpr->resolvedFunction = &getFunc;
    leftExpr->SetTy(propTy);
    auto binaryExpr = CreateBinaryExpr(std::move(leftExpr), std::move(rightExpr), op);
    CopyBasicInfo(&expr, binaryExpr.get());
    args.emplace_back(CreateFuncArg(std::move(binaryExpr)));
    return args;
}

OwnedPtr<Expr> CreateSetterBaseExpr(
    TypeManager& tyMgr, Expr& subExpr, FuncDecl& setFunc, Ptr<Expr>& basePtr, bool& isMemberAccessSuperCall)
{
    auto baseExpr = subExpr.astKind == ASTKind::MEMBER_ACCESS
        ? CreateMemberAccess(ASTCloner::Clone(RawStaticCast<MemberAccess*>(&subExpr)->baseExpr.get()), setFunc)
        : OwnedPtr<Expr>(CreateRefExpr(setFunc).release());
    baseExpr->SetTy({tyMgr.GetFunctionTy({subExpr.GetTy()}, {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)})});
    CopyBasicInfo(&subExpr, baseExpr.get());
    if (auto ma = DynamicCast<MemberAccess*>(baseExpr.get())) {
        CJC_NULLPTR_CHECK(ma->baseExpr);
        basePtr = ma->baseExpr.get();
        if (auto ref = DynamicCast<RefExpr*>(basePtr); ref) {
            isMemberAccessSuperCall = ref->isSuper;
        }
        if (auto target = basePtr->GetTarget(); !target || !target->IsNominalDecl()) {
            ma->baseExpr->mapExpr = basePtr;
        }
    }
    return baseExpr;
}

void DesugarCompoundSetForPropDecl(TypeManager& tyMgr, Expr& expr, Expr& subExpr, PropDecl& propDecl)
{
    auto [getFunc, setFunc] = GetUsableGetterSetterForProperty(propDecl);
    if (!getFunc || !setFunc) {
        return;
    }
    subExpr.EnableAttr(Attribute::UNREACHABLE);
    Ptr<Expr> basePtr = nullptr;
    bool isMemberAccessSuperCall = false;
    auto baseExpr = CreateSetterBaseExpr(tyMgr, subExpr, *setFunc, basePtr, isMemberAccessSuperCall);
    auto args = GetFuncArgsForDesugaredPropDecl(tyMgr, expr, subExpr.GetTy(), basePtr, *getFunc);
    auto lastCallExpr = CreateCallExpr(std::move(baseExpr), std::move(args));
    CopyBasicInfo(&subExpr, lastCallExpr.get());
    lastCallExpr->callKind = isMemberAccessSuperCall ? CallKind::CALL_SUPER_FUNCTION : CallKind::CALL_DECLARED_FUNCTION;
    lastCallExpr->resolvedFunction = setFunc;
    lastCallExpr->SetTy(ModalTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
    AddCurFile(*lastCallExpr, expr.curFile);
    lastCallExpr->EnableAttr(Attribute::SIDE_EFFECT);
    expr.desugarExpr = std::move(lastCallExpr);
}

void DesugarSimpleSetForPropDecl(TypeManager& tyMgr, AssignExpr& expr, PropDecl& propDecl)
{
    CJC_NULLPTR_CHECK(expr.leftValue);
    auto& subExpr = *expr.leftValue;
    auto setFunc = GetUsableAccessorForProperty(propDecl, false);
    if (!setFunc) {
        return;
    }
    subExpr.EnableAttr(Attribute::UNREACHABLE);
    Ptr<Expr> basePtr = nullptr;
    bool isMemberAccessSuperCall = false;
    auto baseExpr = CreateSetterBaseExpr(tyMgr, subExpr, *setFunc, basePtr, isMemberAccessSuperCall);
    auto argExpr = ASTCloner::Clone(StaticCast<AssignExpr&>(expr).rightExpr.get());
    TrySetMappedExprForProp(*argExpr, basePtr);
    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(argExpr)));
    auto lastCallExpr = CreateCallExpr(std::move(baseExpr), std::move(args));
    CopyBasicInfo(&subExpr, lastCallExpr.get());
    lastCallExpr->callKind = isMemberAccessSuperCall ? CallKind::CALL_SUPER_FUNCTION : CallKind::CALL_DECLARED_FUNCTION;
    lastCallExpr->resolvedFunction = setFunc;
    lastCallExpr->SetTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
    AddCurFile(*lastCallExpr, expr.curFile);
    expr.desugarExpr = std::move(lastCallExpr);
}
} // namespace

/**
 * Desugar of propDecl get is happened after sema well-typed.
 */
void TypeChecker::TypeCheckerImpl::DesugarGetForPropDecl(Expr& expr)
{
    // 1. Do not desugar for expression which already has desugared result.
    // 2. Do not desugar as property get for left value.
    // 3. Do not desugar for unreadchable node.
    // 4. Do not desugar for non-reference node.
    bool ignored = expr.desugarExpr || !expr.GetTy().IsCorrect() || expr.TestAttr(Attribute::LEFT_VALUE) ||
        expr.TestAttr(Attribute::UNREACHABLE) || !expr.IsReferenceExpr();
    if (ignored) {
        return;
    }
    auto target = expr.GetTarget();
    if (!target || target->astKind != ASTKind::PROP_DECL) {
        return;
    }
    auto propDecl = StaticCast<PropDecl>(target);
    auto getFunc = GetUsableAccessorForProperty(*propDecl, true);
    if (!getFunc) {
        return;
    }
    OwnedPtr<Expr> baseExpr;
    bool isMemberAccessSuperCall = false;
    if (auto ma = DynamicCast<MemberAccess*>(&expr); ma) {
        if (!ma->baseExpr) {
            return; // Current node may be desugared by other process.
        }
        if (auto ref = DynamicCast<RefExpr*>(ma->baseExpr.get()); ref) {
            isMemberAccessSuperCall = ref->isSuper;
        }
        baseExpr = CreateMemberAccess(ASTCloner::Clone(static_cast<MemberAccess&>(expr).baseExpr.get()), *getFunc);
    } else {
        baseExpr = CreateRefExpr(*getFunc);
    }
    // 'getFunc' may be generic type, which need to be update to real used type.
    baseExpr->SetTy({typeManager.GetFunctionTy({}, expr.GetTy())});
    CopyBasicInfo(&expr, baseExpr.get());
    auto lastCallExpr = CreateCallExpr(std::move(baseExpr), {});
    lastCallExpr->callKind = isMemberAccessSuperCall ? CallKind::CALL_SUPER_FUNCTION : CallKind::CALL_DECLARED_FUNCTION;
    lastCallExpr->resolvedFunction = StaticCast<FuncDecl*>(getFunc);
    lastCallExpr->SetTy(expr.GetTy());
    CopyBasicInfo(&expr, lastCallExpr.get());
    AddCurFile(*lastCallExpr, expr.curFile);
    expr.desugarExpr = std::move(lastCallExpr);
}

/**
 * Desugar of propDecl set is happened after sema well-typed.
 */
void TypeChecker::TypeCheckerImpl::DesugarSetForPropDecl(Expr& expr)
{
    if (expr.desugarExpr || !expr.GetTy().IsCorrect()) {
        return;
    }
    Ptr<Expr> subExpr = nullptr;
    bool isCompound = false;
    if (expr.astKind == AST::ASTKind::INC_OR_DEC_EXPR) {
        subExpr = static_cast<const IncOrDecExpr&>(expr).expr.get();
        isCompound = true;
    } else if (expr.astKind == AST::ASTKind::ASSIGN_EXPR) {
        subExpr = static_cast<const AssignExpr&>(expr).leftValue.get();
        // If desugar of compound assignExpr was performed during typecheck,
        // we need to move 'SIDE_EFFECT' from assignExpr to new desugared 'CallExpr'.
        // eg: a.v += "s" ====>  a.v = a.v + "s" ===> a.v$set(a.v$get().+("s"))
        isCompound = static_cast<const AssignExpr&>(expr).isCompound || expr.TestAttr(Attribute::SIDE_EFFECT);
        expr.DisableAttr(Attribute::SIDE_EFFECT);
    }
    if (!subExpr || !subExpr->GetTy().IsCorrect()) {
        return;
    }
    auto propDecl = DynamicCast<PropDecl*>(subExpr->GetTarget());
    if (propDecl == nullptr || !propDecl->isVar) {
        return;
    }
    if (isCompound) {
        DesugarCompoundSetForPropDecl(typeManager, expr, *subExpr, *propDecl);
        return;
    }
    DesugarSimpleSetForPropDecl(typeManager, StaticCast<AssignExpr&>(expr), *propDecl);
}

void UpdatePosForMacroExpandedNodes(const std::vector<Ptr<AST::Package>>& pkgs)
{
    auto visit = [](Ptr<AST::Node> curNode) -> AST::VisitAction {
        if (!curNode->curMacroCall) {
            return AST::VisitAction::WALK_CHILDREN;
        }
        curNode->begin = curNode->GetDebugPos(curNode->begin);
        curNode->end = curNode->GetDebugPos(curNode->end);
        return AST::VisitAction::WALK_CHILDREN;
    };
    for (auto pkg : pkgs) {
        AST::Walker walker(pkg, visit);
        walker.Walk();
    }
}

void TypeChecker::PerformDesugarAfterSema(std::vector<Ptr<Package>>& pkgs) const
{
    impl->PerformDesugarAfterSema(pkgs);
}

/** @p pkgs is set of source packages, desugar only needs to be done on source package. */
void TypeChecker::TypeCheckerImpl::PerformDesugarAfterSema(std::vector<Ptr<AST::Package>>& pkgs)
{
    TyVarScope ts(typeManager);
    for (auto& pkg : pkgs) {
        PerformDesugarAfterTypeCheck(*ci->pkgCtxMap[pkg], *pkg);
        TryDesugarForCoalescing(*pkg);
        AutoBoxing autoBox(typeManager);
        autoBox.AddOptionBox(*pkg);
    }
    if (ci->invocation.globalOptions.enIncrementalCompilation) {
        ci->CacheSemaUsage(GetSemanticUsage(typeManager, pkgs));
    }
    GenerateMainInvoke();
    // Inline checking needs to process source package and imported packages which has source imported decls.
    CheckInlineFunctions(ci->GetPackages());
    // Since the inlined function will affect decls' visibility, analyze linkage after inline decl checking.
    for (auto& pkg : pkgs) {
        AnalyzeFunctionLinkage(*pkg);
    }
    // If compiled with the `-g` or '--coverage', files should be saved with absolute paths.
    // When compiling stdlib without options '--coverage', do not save file with abs path.
    // Then can not debugging stdlib with abs path.
    bool saveFileWithAbsPath =
        ci->invocation.globalOptions.enableCompileDebug || ci->invocation.globalOptions.enableCoverage;
    for (auto& pkg : pkgs) {
        bool saveAbsPath = STANDARD_LIBS.find(pkg->fullPackageName) != STANDARD_LIBS.end()
            ? ci->invocation.globalOptions.enableCoverage
            : saveFileWithAbsPath;
        importManager.ExportDeclsWithContent(saveAbsPath, *pkg);
    }
    // Update positions of macro-expanded nodes for user-facing locations.
    // Affects exception stack line numbers, coverage hit lines, and cjdb jump/source locations.
    if (ci->invocation.globalOptions.enableCompileDebug || ci->invocation.globalOptions.displayLineInfo) {
        UpdatePosForMacroExpandedNodes(pkgs);
    }
}

void TypeChecker::TypeCheckerImpl::GenerateMainInvoke()
{
    if (mainFunctionMap.size() != 1 || mainFunctionMap.begin()->second.size() != 1) {
        return;
    }
    // 'main' must be checked before insert to 'mainFunctionMap' in the function 'CheckEntryFunc'.
    // Get the package that contains main function. If the package is imported, get one from source packages.
    auto mainFunc = RawStaticCast<FuncDecl*>(*mainFunctionMap.begin()->second.begin());
    CJC_ASSERT(mainFunc && mainFunc->curFile && mainFunc->curFile->curPackage);
    auto packageHasMain = mainFunc->curFile->curPackage;
    auto funcTy = DynamicCast<FuncTy>(mainFunc->DataTy());
    // Do not generate mainInvoke if any of the following condition satisfies:
    // 1. target is a library, not an executable (-c or --output-type=staticlib or --output-type=dylib)
    // 2. main function is in imported package. (can be remove when strategy of main importation is updated).
    // 3. current process is in incremental compiling strategy
    // 4. main function is invalid.
    // 5. when compiling chir we only depend on whether we have main func or not
    // 6. output-type is obj and compile-target is not exe (in CompileExecutable)
    bool outputCHIR = ci->invocation.globalOptions.outputMode == GlobalOptions::OutputMode::CHIR;
    if ((!ci->invocation.globalOptions.CompileExecutable() && !outputCHIR) ||
        packageHasMain->TestAttr(Attribute::IMPORTED) ||
        ci->compileStrategy->type == StrategyType::INCREMENTAL_COMPILE || !funcTy || !Ty::IsTyCorrect(funcTy->retTy) ||
        !mainFunc->funcBody || mainFunc->funcBody->paramLists.empty()) {
        return;
    }
    auto mainTy = funcTy->retTy;
    auto int64Ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64);
    auto mainInvokeTy = typeManager.GetFunctionTy(funcTy->paramTys, {int64Ty});

    // Creat mainInvoke function: "func $mainInvoke(v:Array<String>)" or "func $mainInvoke()".
    OwnedPtr<FuncBody> funcBody = MakeOwnedNode<FuncBody>();
    funcBody->SetTy({mainInvokeTy});
    funcBody->body = MakeOwnedNode<Block>();
    funcBody->body->SetTy({mainInvokeTy});
    auto funcParamList = MakeOwnedNode<FuncParamList>();
    Ptr<VarDecl> argPtr = nullptr;
    if (!mainFunc->funcBody->paramLists[0]->params.empty()) {
        const auto& param = mainFunc->funcBody->paramLists[0]->params[0];
        auto newParam = CreateFuncParam("v", ASTCloner::Clone(param->type.get()), nullptr, param->GetTy());
        argPtr = newParam.get();
        funcParamList->params.emplace_back(std::move(newParam));
    }
    funcBody->paramLists.push_back(std::move(funcParamList));

    // Creat "retVal = main(v) or main()"
    std::vector<OwnedPtr<FuncArg>> args;
    if (argPtr) {
        args.emplace_back(CreateFuncArg(CreateRefExpr(*argPtr), "", argPtr->GetTy()));
    }
    auto callMain = CreateCallExpr(CreateRefExpr(*mainFunc), std::move(args), mainFunc, mainTy);
    auto retVal = CreateVarDecl("retVal", std::move(callMain));
    // Create "return Int64(retVal)" or "return 0".
    OwnedPtr<ReturnExpr> retExpr = mainTy->IsInteger()
        ? CreateReturnExpr(CreateConvInt64(*retVal, {int64Ty}), funcBody.get())
        : CreateReturnExpr(CreateLitConstExpr(LitConstKind::INTEGER, "0", ModalTy{int64Ty}), funcBody.get());
    retExpr->SetTy({TypeManager::GetNothingTy()});
    funcBody->body->body.emplace_back(std::move(retVal));
    funcBody->body->body.emplace_back(std::move(retExpr));
    mainFunc->curFile->decls.emplace_back(CreateMainInvokeDecl(std::move(funcBody), *mainFunc, {mainInvokeTy}));
}

// Perform desugar after typecheck before generic instantiation.
void TypeChecker::TypeCheckerImpl::DesugarForPropDecl(Node& pkg)
{
    Walker(&pkg, [this](Ptr<Node> node) -> VisitAction {
        if (node->TestAnyAttr(Attribute::HAS_BROKEN, Attribute::IS_BROKEN)) {
            return VisitAction::SKIP_CHILDREN;
        }
        switch (node->astKind) {
            case ASTKind::REF_EXPR:
            case ASTKind::MEMBER_ACCESS:
                DesugarGetForPropDecl(*StaticCast<Expr>(node));
                break;
            case ASTKind::ASSIGN_EXPR:
            case ASTKind::INC_OR_DEC_EXPR:
                DesugarSetForPropDecl(*StaticCast<Expr>(node));
                break;
            default:
                break;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

// Perform desugar after typecheck before generic instantiation.
void TypeChecker::TypeCheckerImpl::PerformDesugarAfterTypeCheck(ASTContext& ctx, Package& pkg)
{
    Interop::Java::JavaInteropManager jim(importManager, typeManager, diag, *ci->mangler,
        ci->invocation.globalOptions.outputJavaGenDir, ci->invocation.globalOptions.output);

    jim.CheckImplRedefinition(pkg);
    for (auto& file : pkg.files) {
        jim.CheckTypes(*file);
    }

    jim.DesugarPackage(pkg, structMemberMap, [this](auto& node) {
        DesugarForPropDecl(node);
    });
    Interop::ObjC::Desugar(Interop::ObjC::InteropContext(
        pkg, typeManager, importManager, diag, *ci->mangler,
        ci->invocation.globalOptions.output, ci->invocation.globalOptions.outputObjCGenDir,
        structMemberMap, ci->invocation.globalOptions.target.os));

    DesugarDeclsForPackage(pkg, ci->invocation.globalOptions.enableCoverage);
    std::function<VisitAction(Ptr<Node>)> preVisit = [this, &ctx](Ptr<Node> node) -> VisitAction {
        switch (node->astKind) {
            case ASTKind::FOR_IN_EXPR: {
                auto fie = StaticAs<ASTKind::FOR_IN_EXPR>(node);
                if (fie->inExpression) {
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
                    ReArrangeForInExpr(ctx, *fie);
#endif
                }
                break;
            }
            case ASTKind::LIT_CONST_EXPR: {
                auto lce = StaticAs<ASTKind::LIT_CONST_EXPR>(node);
                if (lce->siExpr && lce->GetTy().IsCorrect()) {
                    DesugarStrInterpolationExpr(ctx, *lce);
                }
                break;
            }
            case ASTKind::FUNC_PARAM:
                PostProcessFuncParam(*StaticAs<ASTKind::FUNC_PARAM>(node), ci->invocation.globalOptions);
                break;
            case ASTKind::RANGE_EXPR:
                DesugarRangeExpr(typeManager, *StaticAs<ASTKind::RANGE_EXPR>(node));
                break;
            case ASTKind::CALL_EXPR:
                DesugarIntrinsicCallExpr(*StaticAs<ASTKind::CALL_EXPR>(node));
                DesugarTokenCallExpr(ctx, *StaticAs<ASTKind::CALL_EXPR>(node));
                break;
            case ASTKind::BINARY_EXPR:
                DesugarBinaryExpr(StaticCast<BinaryExpr&>(*node));
                break;
            case ASTKind::IS_EXPR:
                DesugarIsExpr(typeManager, *StaticAs<ASTKind::IS_EXPR>(node));
                break;
            case ASTKind::AS_EXPR:
                DesugarAsExpr(typeManager, *StaticAs<ASTKind::AS_EXPR>(node));
                break;
            case ASTKind::ARRAY_LIT:
                AddArrayLitConstructor(*StaticAs<ASTKind::ARRAY_LIT>(node));
                break;
            case ASTKind::IF_EXPR:
                DesugarIfExpr(typeManager, *StaticAs<ASTKind::IF_EXPR>(node));
                break;
            case ASTKind::SPAWN_EXPR:
                DesugarSpawnExpr(ctx, *StaticAs<ASTKind::SPAWN_EXPR>(node));
                break;
            case ASTKind::TRY_EXPR:
                DesugarTryWithResourcesExpr(ctx, *StaticCast<TryExpr*>(node));
                DesugarTryToFrame(ctx, *StaticCast<TryExpr*>(node));
                break;
            case ASTKind::PERFORM_EXPR:
                DesugarPerform(ctx, *StaticCast<PerformExpr*>(node));
                break;
            case ASTKind::RESUME_EXPR:
                DesugarResume(ctx, *StaticCast<ResumeExpr*>(node));
                break;
            default:
                break;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker(&pkg, preVisit).Walk();
}

AST::ModalTy TypeChecker::TypeCheckerImpl::SynthesizeWithoutRecover(const CheckerContext& ctx, Ptr<AST::Node> node)
{
    CJC_NULLPTR_CHECK(node);
    ctx.Ctx().ClearTypeCheckCache(*node);    // ensure newly created nodes have no related cache
    ctx.Ctx().SkipSynForCorrectTyRec(*node); // avoid possible recovery during synthesize for already checked node
    auto ret = Synthesize(ctx, node);
    DesugarForPropDecl(*node); // seems that the above still aren't enough...
    return ret;
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
// public func toAny(): Any { return this }
OwnedPtr<AST::FuncDecl> TypeChecker::TypeCheckerImpl::CreateToAny(AST::Decl& outerDecl)
{
    OwnedPtr<FuncBody> funcBody = MakeOwned<FuncBody>();
    funcBody->body = MakeOwned<Block>();
    funcBody->body->SetTy(typeManager.GetAnyTy());

    auto anyDecl = Ty::GetDeclPtrOfTy<InheritableDecl>(typeManager.GetAnyTy());
    CJC_NULLPTR_CHECK(anyDecl);
    funcBody->retType = CreateRefType(*anyDecl);

    funcBody->SetTy({typeManager.GetFunctionTy({}, {typeManager.GetAnyTy()})});

    auto fromTy = outerDecl.GetTy();
    auto funcParamList = MakeOwned<FuncParamList>();
    funcBody->paramLists.push_back(std::move(funcParamList));
    auto xRef = CreateRefExpr("this");
    xRef->ref.target = &outerDecl;
    xRef->SetTy(fromTy);
    xRef->EnableAttr(Attribute::NO_REFLECT_INFO);
    auto returnExpr = CreateReturnExpr(std::move(xRef), funcBody.get());
    returnExpr->SetTy({TypeManager::GetNothingTy()});
    funcBody->body->body.push_back(std::move(returnExpr));

    auto toAnyFunc = CreateFuncDecl(std::string(TO_ANY), std::move(funcBody));
    toAnyFunc->fullPackageName = outerDecl.fullPackageName;
    toAnyFunc->EnableAttr(Attribute::COMPILER_ADD, Attribute::PUBLIC);
    toAnyFunc->outerDecl = &outerDecl;
    if (outerDecl.TestAttr(Attribute::GENERIC_INSTANTIATED)) {
        toAnyFunc->linkage = Linkage::INTERNAL;
    } else {
        toAnyFunc->linkage = Linkage::WEAK_ODR;
    }
    toAnyFunc->doNotExport = true;

    AddCurFile(*toAnyFunc, outerDecl.curFile);

    return toAnyFunc;
}
#endif
