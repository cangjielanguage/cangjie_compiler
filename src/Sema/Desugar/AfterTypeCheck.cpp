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

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;
using namespace Sema::Desugar::AfterTypeCheck;

namespace {
OwnedPtr<TypeConvExpr> CreateConvInt64(VarDecl& vd, Ty& ty)
{
    auto convType = MakeOwnedNode<PrimitiveType>();
    convType->kind = TypeKind::TYPE_INT64;
    convType->ty = &ty;
    auto numConv = MakeOwnedNode<TypeConvExpr>();
    numConv->type = std::move(convType);
    numConv->expr = CreateRefExpr(vd);
    numConv->expr->ty = &ty;
    numConv->ty = &ty;
    return numConv;
}

OwnedPtr<FuncDecl> CreateMainInvokeDecl(OwnedPtr<FuncBody>&& funcBody, const FuncDecl& mainFunc, Ty& funcTy)
{
    auto mainInvokeFunc = MakeOwnedNode<FuncDecl>();
    funcBody->funcDecl = mainInvokeFunc.get();
    mainInvokeFunc->toBeCompiled = mainFunc.toBeCompiled;
    mainInvokeFunc->ty = &funcTy;
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
    TypeManager& tyMgr, Expr& expr, Ty& propTy, Ptr<Expr> mapExpr, FuncDecl& getFunc)
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
        rightExpr = CreateLitConstExpr(LitConstKind::INTEGER, "1", &propTy);
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
    leftBaseExpr->ty = tyMgr.GetFunctionTy({}, &propTy);
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
    leftExpr->ty = &propTy;
    auto binaryExpr = CreateBinaryExpr(std::move(leftExpr), std::move(rightExpr), op);
    CopyBasicInfo(&expr, binaryExpr.get());
    args.emplace_back(CreateFuncArg(std::move(binaryExpr)));
    return args;
}

/**
 * Desugar of propDecl get is happened after sema well-typed.
 */
void DesugarGetForPropDecl(TypeManager& tyMgr, Expr& expr)
{
    // 1. Do not desugar for expression which already has desugared result.
    // 2. Do not desugar as property get for left value.
    // 3. Do not desugar for unreadchable node.
    // 4. Do not desugar for non-reference node.
    bool ignored = expr.desugarExpr || !Ty::IsTyCorrect(expr.ty) || expr.TestAttr(Attribute::LEFT_VALUE) ||
        expr.TestAttr(Attribute::UNREACHABLE) || !expr.IsReferenceExpr();
    if (ignored) {
        return;
    }
    auto target = expr.GetTarget();
    if (!target || target->astKind != ASTKind::PROP_DECL) {
        return;
    }
    auto propDecl = StaticAs<ASTKind::PROP_DECL>(target);
    auto getFunc = GetUsedMemberDecl(*propDecl, true);
    if (getFunc->astKind != ASTKind::FUNC_DECL) {
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
    baseExpr->ty = tyMgr.GetFunctionTy({}, expr.ty);
    CopyBasicInfo(&expr, baseExpr.get());
    auto lastCallExpr = CreateCallExpr(std::move(baseExpr), {});
    lastCallExpr->callKind = isMemberAccessSuperCall ? CallKind::CALL_SUPER_FUNCTION : CallKind::CALL_DECLARED_FUNCTION;
    lastCallExpr->resolvedFunction = StaticCast<FuncDecl*>(getFunc);
    lastCallExpr->ty = expr.ty;
    CopyBasicInfo(&expr, lastCallExpr.get());
    AddCurFile(*lastCallExpr, expr.curFile);
    expr.desugarExpr = std::move(lastCallExpr);
}

/**
 * Desugar of propDecl set is happened after sema well-typed.
 */
void DesugarSetForPropDecl(TypeManager& tyMgr, Expr& expr)
{
    if (expr.desugarExpr || !Ty::IsTyCorrect(expr.ty)) {
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
    if (!subExpr || !Ty::IsTyCorrect(subExpr->ty)) {
        return;
    }
    auto propDecl = DynamicCast<PropDecl*>(subExpr->GetTarget());
    if (propDecl == nullptr || !propDecl->isVar) {
        return;
    }
    auto [getFunc, setFunc] = GetUsableGetterSetterForProperty(*propDecl);
    if (!getFunc || !setFunc) {
        return;
    }
    // Original 'subExpr' should not be checked for further.
    subExpr->EnableAttr(Attribute::UNREACHABLE);

    // Since current desugar happens inside sema stage, we cannot use 'move' to create new node.
    auto baseExpr = subExpr->astKind == ASTKind::MEMBER_ACCESS
        ? CreateMemberAccess(ASTCloner::Clone(RawStaticCast<MemberAccess*>(subExpr)->baseExpr.get()), *setFunc)
        : OwnedPtr<Expr>(CreateRefExpr(*setFunc).release());
    // 'setFunc' may be generic type, which need to be update to real used type.
    baseExpr->ty = tyMgr.GetFunctionTy({subExpr->ty}, TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT));
    CopyBasicInfo(subExpr, baseExpr.get());
    Ptr<Expr> basePtr = nullptr;
    // For refExpr case, 'a += b' is desugared to aSet(aGet() + b), there is no side effect to be handle.
    // For memberAccess case, the baseExpr of memberAccess should be mapped to one expression to avoid side effect.
    // eg: a.b += c ===> a.bSet(a.bGet() + c)
    // the 'a' part should only be generated once. This is guaranteed by setting both 'a' 's 'mapExpr' to same 'a',
    // then CHIR will only generate 'a' once.
    bool isMemberAccessSuperCall = false;
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
    auto args = GetFuncArgsForDesugaredPropDecl(tyMgr, expr, *subExpr->ty, basePtr, *getFunc);
    auto lastCallExpr = CreateCallExpr(std::move(baseExpr), std::move(args));
    CopyBasicInfo(subExpr, lastCallExpr.get());
    lastCallExpr->callKind = isMemberAccessSuperCall ? CallKind::CALL_SUPER_FUNCTION : CallKind::CALL_DECLARED_FUNCTION;
    lastCallExpr->resolvedFunction = setFunc;
    // Setter of propDecl must be unit type.
    lastCallExpr->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    AddCurFile(*lastCallExpr, expr.curFile);
    if (isCompound) {
        lastCallExpr->EnableAttr(Attribute::SIDE_EFFECT);
    }
    expr.desugarExpr = std::move(lastCallExpr);
}
} // namespace

void TypeChecker::PerformDesugarAfterSema(std::vector<Ptr<Package>>& pkgs) const
{
    impl->PerformDesugarAfterSema(pkgs);
}

void TypeChecker::TypeCheckerImpl::ParsePackageConfigFile(Ptr<AST::Package>& pkg, InteropCJPackageConfigReader packagesFullConfig)
{
    std::string currentPackageName = pkg->fullPackageName;
    pkg->interopCJApiStrategy = packagesFullConfig.GetApiStrategy(currentPackageName);
    pkg->interopCJGenericTypeStrategy = packagesFullConfig.GetGenericTypeStrategy(currentPackageName);
    if (auto currentPackageConfig = packagesFullConfig.GetPackage(currentPackageName)) {
        pkg->interopCJIncludedApis = currentPackageConfig->interopCJIncludedApis;
        pkg->interopCJExcludedApis = currentPackageConfig->interopCJExcludedApis;
        pkg->allowedInteropCJGenericInstantiations = currentPackageConfig->allowedInteropCJGenericInstantiations;
        for (auto& file : pkg->files) {
            for (auto & decl : file->decls) {
                // Following the symbol exposure strategy "Full" requirement, and included_apis is empty
                if (pkg->interopCJApiStrategy == InteropCJStrategy::FULL &&
                    pkg->interopCJIncludedApis.size() == 0) {
                    if (decl->symbol) {
                        decl->symbol->isNeedExposedToInterop = true;
                    }
                    // For each memberDecl in structDecl
                    if (auto structDecl = As<ASTKind::STRUCT_DECL>(decl.get())) {
                        for (auto& member : structDecl->GetMemberDecls()) {
                            if (member->symbol) {
                                member->symbol->isNeedExposedToInterop = true;
                            }
                        }
                    }
                }
                // For exposed symbol
                if (pkg->interopCJApiStrategy == InteropCJStrategy::NONE) {
                    bool isMemberExposed = false;
                    for (const auto& element : pkg->interopCJIncludedApis) {
                        if (decl->symbol) {
                            if (element == decl->symbol->name) {
                                decl->symbol->isNeedExposedToInterop = true;
                            }
                            if (auto structDecl = As<ASTKind::STRUCT_DECL>(decl.get())) {
                                for (auto& member : structDecl->GetMemberDecls()) {
                                    if (member->symbol &&
                                        ((!decl->symbol->isNeedExposedToInterop &&
                                             element ==
                                                 (decl->symbol->name + "." + member->symbol->name)) ||
                                            decl->symbol->isNeedExposedToInterop)) {
                                        member->symbol->isNeedExposedToInterop = true;
                                        isMemberExposed = true;
                                    }
                                    // For default constructor function exposed because of part of
                                    // memberfunction need exposed.
                                    if (isMemberExposed && member->TestAttr(Attribute::CONSTRUCTOR) &&
                                        !member->symbol->isNeedExposedToInterop) {
                                        member->symbol->isNeedExposedToInterop = true;
                                    }
                                }
                            }
                        }
                    }
                    if (isMemberExposed && !decl->symbol->isNeedExposedToInterop) {
                        decl->symbol->isNeedExposedToInterop = true;
                    }
                }
                // For hiddened symbol
                if (pkg->interopCJApiStrategy == InteropCJStrategy::FULL) {
                    for (const auto& element : pkg->interopCJExcludedApis) {
                        if (decl->symbol) {
                            if (element == decl->symbol->name) {
                                decl->symbol->isNeedExposedToInterop = false;
                            }
                            if (auto structDecl = As<ASTKind::STRUCT_DECL>(decl.get())) {
                                for (auto& member : structDecl->GetMemberDecls()) {
                                    if (member->symbol &&
                                        (!decl->symbol->isNeedExposedToInterop &&
                                            (element ==
                                                (decl->symbol->name + "." + member->symbol->name)))) {
                                        member->symbol->isNeedExposedToInterop = false;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    } else {
        // Packages not configured int the configuration file will be set using the default configuration information.
        if (pkg->interopCJApiStrategy == InteropCJStrategy::FULL) {
            for (auto& file : pkg->files) {
                for (auto& decl : file->decls) {
                    if (decl->symbol) {
                        decl->symbol->isNeedExposedToInterop = true;
                    }
                }
            }
        }
    }
    for (auto& file : pkg->files) {
        for (auto& decl : file->decls) {
            auto structDecl = As<ASTKind::STRUCT_DECL>(decl.get());
            /*
                For member decl in includeApis and inside STRUCT_DECL, but STRUCT_DECL not in includeApis, need Warning &
                insert STRUCT_DECL in includeApis.
            */
            if (structDecl && !structDecl->symbol->isNeedExposedToInterop) {
                for (auto& member : structDecl->GetMemberDecls()) {
                    if (member->symbol && member->symbol->isNeedExposedToInterop) {
                        structDecl->symbol->isNeedExposedToInterop = true;
                        std::cerr << "Warning: " << structDecl->symbol->name << " is not config to exposed but "
                                  << structDecl->symbol->name << "." << member->symbol->name
                                  << " is config to exposed" << std::endl;
                    }
                }
            } else {
                /*
                  The STRUCT_DECL is configured to includeApis, but in the source code, structs with non-public
                  modifiers are prohibited from exposing symbols for interoperability, and users are notified
                  via a Warning.
                */
                if (structDecl && structDecl->TestAnyAttr(Attribute::IS_BROKEN, Attribute::PRIVATE, Attribute::PROTECTED)) {
                    structDecl->symbol->isNeedExposedToInterop = false;
                    std::cerr << "Warning: " << structDecl->symbol->name << " is config to exposed but it not "
                              << "public symbol, so it is hiddened." << std::endl;
                }
            }
        }
    }
}

/** @p pkgs is set of source packages, desugar only needs to be done on source package. */
void TypeChecker::TypeCheckerImpl::PerformDesugarAfterSema(std::vector<Ptr<AST::Package>>& pkgs)
{
    TyVarScope ts(typeManager);
    InteropCJPackageConfigReader packagesFullConfig;
    // parse package config toml file.
    if (ci->invocation.globalOptions.enableInteropCJMapping &&
        ci->invocation.globalOptions.interopCJPackageConfigPath != "./" &&
        !packagesFullConfig.Parse(ci->invocation.globalOptions.interopCJPackageConfigPath)) {
        std::cerr << "Failed to parse package config file" << std::endl;
    }
    // validate parser result.
    if (ci->invocation.globalOptions.enableInteropCJMapping && !packagesFullConfig.Validate()) {
        std::cerr << "Package config validation failed" << std::endl;
    }
    for (auto& pkg : pkgs) {
        // Store and transmit information to the cjinterop stage.
        if (ci->invocation.globalOptions.enableInteropCJMapping &&
            ci->invocation.globalOptions.interopCJPackageConfigPath != "./") {
            pkg->isInteropCJPackageConfig = true;
            ParsePackageConfigFile(pkg, packagesFullConfig);
        }
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
    auto funcTy = DynamicCast<FuncTy*>(mainFunc->ty);
    // Do not generate mainInvoke if any of the following condition satisfies:
    // 1. target is a library, not an executable (-c or --output-type=staticlib or --output-type=dylib)
    // 2. main function is in imported package. (can be remove when strategy of main importation is updated).
    // 3. current process is in incremental compiling strategy
    // 4. main function is invalid.
    // 5. when compiling chir we only depend on whether we have main func or not
    bool outputCHIR = ci->invocation.globalOptions.outputMode == GlobalOptions::OutputMode::CHIR;
    if ((!ci->invocation.globalOptions.CompileExecutable() && !outputCHIR) ||
        packageHasMain->TestAttr(Attribute::IMPORTED) ||
        ci->compileStrategy->type == StrategyType::INCREMENTAL_COMPILE || !funcTy || !Ty::IsTyCorrect(funcTy->retTy) ||
        !mainFunc->funcBody || mainFunc->funcBody->paramLists.empty()) {
        return;
    }
    auto mainTy = funcTy->retTy;
    auto int64Ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64);
    auto mainInvokeTy = typeManager.GetFunctionTy(funcTy->paramTys, int64Ty);

    // Creat mainInvoke function: "func $mainInvoke(v:Array<String>)" or "func $mainInvoke()".
    OwnedPtr<FuncBody> funcBody = MakeOwnedNode<FuncBody>();
    funcBody->ty = mainInvokeTy;
    funcBody->body = MakeOwnedNode<Block>();
    funcBody->body->ty = mainInvokeTy;
    auto funcParamList = MakeOwnedNode<FuncParamList>();
    Ptr<VarDecl> argPtr = nullptr;
    if (!mainFunc->funcBody->paramLists[0]->params.empty()) {
        auto param = CreateFuncParam("v", ASTCloner::Clone(mainFunc->funcBody->paramLists[0]->params[0]->type.get()));
        argPtr = param.get();
        funcParamList->params.emplace_back(std::move(param));
    }
    funcBody->paramLists.push_back(std::move(funcParamList));

    // Creat "retVal = main(v) or main()"
    std::vector<OwnedPtr<FuncArg>> args;
    if (argPtr) {
        args.emplace_back(CreateFuncArg(CreateRefExpr(*argPtr), "", argPtr->ty));
    }
    auto callMain = CreateCallExpr(CreateRefExpr(*mainFunc), std::move(args), mainFunc, mainTy);
    auto retVal = CreateVarDecl("retVal", std::move(callMain));
    // Create "return Int64(retVal)" or "return 0".
    OwnedPtr<ReturnExpr> retExpr = mainTy->IsInteger()
        ? CreateReturnExpr(CreateConvInt64(*retVal, *int64Ty), funcBody.get())
        : CreateReturnExpr(CreateLitConstExpr(LitConstKind::INTEGER, "0", int64Ty), funcBody.get());
    retExpr->ty = TypeManager::GetNothingTy();
    funcBody->body->body.emplace_back(std::move(retVal));
    funcBody->body->body.emplace_back(std::move(retExpr));
    mainFunc->curFile->decls.emplace_back(CreateMainInvokeDecl(std::move(funcBody), *mainFunc, *mainInvokeTy));
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
                DesugarGetForPropDecl(typeManager, *StaticAs<ASTKind::EXPR>(node));
                break;
            case ASTKind::ASSIGN_EXPR:
            case ASTKind::INC_OR_DEC_EXPR:
                DesugarSetForPropDecl(typeManager, *StaticAs<ASTKind::EXPR>(node));
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
        ci->invocation.globalOptions.outputJavaGenDir, ci->invocation.globalOptions.output,
        ci->invocation.globalOptions.targetInteropLanguage);

    jim.CheckImplRedefinition(pkg);
    for (auto& file : pkg.files) {
        jim.CheckTypes(*file);
    }

    jim.DesugarPackage(pkg);
    Interop::ObjC::Desugar(Interop::ObjC::InteropContext(
        pkg, typeManager, importManager, diag, *ci->mangler, ci->invocation.globalOptions.output));

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
                if (lce->siExpr && Ty::IsTyCorrect(lce->ty)) {
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

Ptr<AST::Ty> TypeChecker::TypeCheckerImpl::SynthesizeWithoutRecover(ASTContext& ctx, Ptr<AST::Node> node)
{
    CJC_NULLPTR_CHECK(node);
    ctx.ClearTypeCheckCache(*node);    // ensure newly created nodes have no related cache
    ctx.SkipSynForCorrectTyRec(*node); // avoid possible recovery during synthesize for already checked node
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
    funcBody->body->ty = typeManager.GetAnyTy();

    auto anyDecl = Ty::GetDeclPtrOfTy<InheritableDecl>(typeManager.GetAnyTy());
    CJC_NULLPTR_CHECK(anyDecl);
    funcBody->retType = CreateRefType(*anyDecl);

    funcBody->ty = typeManager.GetFunctionTy({}, typeManager.GetAnyTy());

    auto fromTy = outerDecl.ty;
    auto funcParamList = MakeOwned<FuncParamList>();
    funcBody->paramLists.push_back(std::move(funcParamList));
    auto xRef = CreateRefExpr("this");
    xRef->ref.target = &outerDecl;
    xRef->ty = fromTy;
    xRef->EnableAttr(Attribute::NO_REFLECT_INFO);
    auto returnExpr = CreateReturnExpr(std::move(xRef), funcBody.get());
    returnExpr->ty = TypeManager::GetNothingTy();
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

void TypeChecker::TypeCheckerImpl::PerformToAnyInsertion(AST::Package& pkg)
{
    if (ci->invocation.globalOptions.disableInstantiation) {
        return;
    }
    auto addToAny = [this](const OwnedPtr<Decl>& decl) {
        if (decl->astKind == ASTKind::STRUCT_DECL && !decl->TestAttr(Attribute::GENERIC)) {
            auto sd = RawStaticCast<AST::StructDecl*>(decl.get());
            auto fd = CreateToAny(*sd);
            fd->EnableAttr(Attribute::IN_STRUCT);
            fd->funcBody->parentStruct = sd;
            fd->toBeCompiled = sd->toBeCompiled;
            (void)sd->body->decls.emplace_back(std::move(fd));
        }
    };
    IterateToplevelDecls(pkg, addToAny);
    (void)std::for_each(pkg.genericInstantiatedDecls.begin(), pkg.genericInstantiatedDecls.end(),
        [&addToAny](auto& it) { addToAny(it); });
}
#endif
