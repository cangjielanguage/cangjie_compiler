// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements generate forward class pipeline for CJMapping open class which is used in Objctive-C side.
 */

#include "Handlers.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "NativeFFI/Utils.h"
#include "TypeCheckUtil.h"
#include "cangjie/AST/ASTCasting.h"
#include "cangjie/AST/Create.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;
using namespace Cangjie::Native::FFI;
using namespace Cangjie::TypeCheckUtil;

namespace {
constexpr auto IMPL_SUFFIX = "Impl";
constexpr auto MTX_IDENT = "mtx";
constexpr auto OVERRIDE_MASK_IDENT = "overrideMask";
constexpr auto OBJ_IDENT = "obj";
constexpr auto CJ_FINALIZABLE_IDENT = "cjFinalizable";
constexpr auto OBJC_FOR_PURE_CJ_IDENT = "objcObjForPureCJ";
constexpr auto OBJC_OBJ_IDENT = "objcObj";
constexpr auto AUTO_RELEASE_IDENT = "objcObjAutorelease";
constexpr auto INIT_WITH_REG_ID_SEL = "initWithRegistryId:";
constexpr auto REINIT_WITH_REG_ID_SEL = "reinitWithRegistryId:";
constexpr auto INIT_IDENT = "init";
constexpr auto FINALIZER_IDENT = "~init";

inline std::vector<OwnedPtr<FuncParamList>> CreateParamLists(std::vector<OwnedPtr<FuncParam>>&& params)
{
    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.emplace_back(CreateFuncParamList(std::move(params)));
    return paramLists;
}

inline const std::vector<OwnedPtr<FuncParam>>& GetParams(const FuncDecl& fn)
{
    auto& fnBody = fn.funcBody;
    CJC_NULLPTR_CHECK(fnBody);
    auto& paramLists = fnBody->paramLists;
    CJC_ASSERT(paramLists.size() == 1);
    return paramLists[0]->params;
}

inline OwnedPtr<VarDecl> CreateVar(
    const std::string& name, Ptr<Ty> ty, bool isVar, OwnedPtr<Expr> initializer = nullptr)
{
    auto ret = MakeOwned<VarDecl>();
    ret->identifier = name;
    ret->ty = ty;
    ret->isVar = isVar;
    ret->EnableAttr(Attribute::COMPILER_ADD, Attribute::IMPLICIT_ADD, Attribute::NO_REFLECT_INFO);
    if (initializer) {
        ret->initializer = std::move(initializer);
    }
    ret->toBeCompiled = true;
    return ret;
}

inline OwnedPtr<FuncDecl> CreateFunc(const std::string& name, Ptr<FuncTy> fnTy,
    std::vector<OwnedPtr<FuncParam>>&& params, std::vector<OwnedPtr<Node>>&& nodes)
{
    auto retTy = fnTy->retTy;
    auto ty = nodes.empty() ? retTy : nodes.back()->ty;
    auto body = CreateBlock(std::move(nodes), ty);
    auto fnBody = CreateFuncBody(CreateParamLists(std::move(params)), CreateType(retTy), std::move(body), retTy);
    auto fn = CreateFuncDecl(name, std::move(fnBody), fnTy);
    fn->funcBody->funcDecl = fn.get();
    return fn;
}

template <typename... Args> inline OwnedPtr<Block> CreateBlock(bool isSafe, OwnedPtr<Args>&&... args)
{
    auto nodes = Nodes(std::forward<OwnedPtr<Args>>(args)...);
    CJC_ASSERT(!nodes.empty());
    auto ty = nodes.back()->ty;
    auto block = CreateBlock(std::move(nodes), ty);
    if (isSafe) {
        block->EnableAttr(Attribute::UNSAFE);
    }
    return std::move(block);
}

inline OwnedPtr<ParenExpr> CreateParenExpr(OwnedPtr<Expr> expr)
{
    auto parenExpr = MakeOwnedNode<ParenExpr>();
    parenExpr->ty = expr->ty;
    parenExpr->expr = std::move(expr);
    return std::move(parenExpr);
}

template <typename... Args>
inline OwnedPtr<CallExpr> CreateMemCall(OwnedPtr<Expr> base, Ptr<FuncDecl> fd, OwnedPtr<Args>&&... args)
{
    std::vector<OwnedPtr<FuncArg>> funcArgs;
    (funcArgs.push_back(CreateFuncArg(std::forward<OwnedPtr<Args>>(args))), ...);
    auto funcTy = Cangjie::StaticCast<FuncTy*>(fd->ty);
    return CreateCallExpr(CreateMemberAccess(std::move(base), *fd), std::move(funcArgs), fd, funcTy->retTy,
        CallKind::CALL_DECLARED_FUNCTION);
}

inline void CreateParamsAndArgs(const std::vector<OwnedPtr<FuncParam>>& origParams,
    std::vector<OwnedPtr<FuncParam>>& params, CallExpr& call, std::function<Ptr<Ty>(Ptr<Ty>)> tyMap,
    std::function<OwnedPtr<Expr>(OwnedPtr<Expr>)> exprMap)
{
    std::transform(
        origParams.begin(), origParams.end(), std::back_inserter(params), [&call, &tyMap, &exprMap](auto& p) {
            auto ty = tyMap(p->ty);
            auto np = CreateFuncParam(p->identifier.Val(), CreateType(ty), nullptr, ty);
            call.args.emplace_back(CreateFuncArg(exprMap(CreateRefExpr(*np))));
            return std::move(np);
        });
}

inline void CreateParamsAndArgs(
    const std::vector<OwnedPtr<FuncParam>>& origParams, std::vector<OwnedPtr<FuncParam>>& params, CallExpr& call)
{
    CreateParamsAndArgs(
        origParams, params, call, [](Ptr<Ty> ty) { return ty; }, [](OwnedPtr<Expr> e) { return e; });
}

inline std::vector<Ptr<Ty>> CollectTys(const std::vector<OwnedPtr<FuncParam>>& params)
{
    std::vector<Ptr<Ty>> paramTys;
    std::transform(params.begin(), params.end(), std::back_inserter(paramTys), [](auto& p) { return p->ty; });
    return paramTys;
}

inline void UpdateWrapperAttr(FuncDecl& fn)
{
    fn.EnableAttr(Attribute::C, Attribute::GLOBAL, Attribute::PUBLIC, Attribute::NO_MANGLE);
}

// Collect open member functions.
void CollectOpenMemberFuncs(ClassDecl& decl, std::vector<Ptr<FuncDecl>>& openMemberFuncs)
{
    for (auto& member : decl.GetMemberDeclPtrs()) {
        if (member->IsOpen()) {
            openMemberFuncs.emplace_back(Cangjie::DynamicCast<FuncDecl*>(member));
        }
    }
    // For stable order.
    std::sort(openMemberFuncs.begin(), openMemberFuncs.end(),
        [](Ptr<FuncDecl>& a, Ptr<FuncDecl>& b) { return a->ty->String() > b->ty->String(); });
}
} // namespace

void GenerateFwdClass::HandleImpl(InteropContext& ctx)
{
    this->pctx = &ctx;
    for (auto& decl : ctx.cjMappings) {
        if (auto cls = DynamicCast<ClassDecl>(decl); cls && cls->IsOpen()) {
            curFile = decl->curFile;
            auto fwd = GenerateFwdClassDecl(*cls);
            ctx.fwdClassMap.emplace(decl, fwd.get());
            GenerateDeleteCJObject();
            GenerateLockOrUnlockCJObject(true);
            GenerateLockOrUnlockCJObject(false);
            PutIntoContext(std::move(fwd));
            Clear();
        }
    }
}

OwnedPtr<ClassDecl> GenerateFwdClass::GenerateFwdClassDecl(ClassDecl& decl)
{
    auto fwd = MakeOwned<ClassDecl>();
    fwd->identifier = decl.identifier.Val() + OBJ_C_FWD_CLASS_SUFFIX;
    fwd->fullPackageName = decl.fullPackageName;
    fwd->moduleName = decl.moduleName;
    fwd->curFile = decl.curFile;
    fwd->inheritedTypes.emplace_back(CreateRefType(decl));
    fwd->ty = pctx->typeManager.GetClassTy(*fwd, {});
    auto classTy = DynamicCast<ClassTy*>(decl.ty);
    CJC_ASSERT(classTy);
    classTy->directSubtypes.insert(fwd->ty);
    fwd->EnableAttr(Attribute::PUBLIC, Attribute::COMPILER_ADD);
    fwd->body = MakeOwned<ClassBody>();
    fwdClass = fwd.get();
    // Generate fields
    GenerateObjVar();
    GenerateOverrideMaskVar();
    GenerateCjFinalizableVar();
    GenerateMtxVar();
    // static objcObj
    GenerateObjcObjForPureCJ(decl);
    // init funcs
    GenerateCtors(decl);
    // private funcs
    GenerateObjcObj4FwdClass();
    GenerateAutoreleased4FwdClass();
    // member funcs
    GenerateMemberFuncsAndWrappers(decl);
    // ~init
    GenerateFinalizer4FwdClass();
    return fwd;
}

/**
 * Generate @C warpper
 *
 * func CJImpl_ObjC_glue_A_Fwd_initCJObject(obj: NativeObjCId, overrideMask: UInt64) {
 *     putToRegistry(A_fwd(obj, overrideMask, ...))
 * }
 */
void GenerateFwdClass::GenerateInitCJObject(AST::FuncDecl& ctor, AST::FuncDecl& fwdCtor)
{
    CJC_ASSERT(fwdClass && objVar && maskVar);
    auto objParam = CreateFuncParam(OBJ_IDENT, CreateType(objVar->ty), nullptr, objVar->ty);
    auto maskParam = CreateFuncParam(OVERRIDE_MASK_IDENT, CreateType(maskVar->ty), nullptr, maskVar->ty);
    // create: cjA = A_fwd(obj, mask, ...)
    auto args = Nodes<FuncArg>(CreateFuncArg(CreateRefExpr(*objParam)), CreateFuncArg(CreateRefExpr(*maskParam)));
    auto ctorCall =
        CreateCallExpr(CreateRefExpr(fwdCtor), std::move(args), &fwdCtor, fwdClass->ty, CallKind::CALL_OBJECT_CREATION);
    auto params = Nodes<FuncParam>(std::move(objParam), std::move(maskParam));
    // Mapping type: Cangjie -> ObjC (Native)
    CreateParamsAndArgs(
        GetParams(ctor), params, *ctorCall, [](Ptr<Ty> ty) { return ty; }, [](OwnedPtr<Expr> e) { return e; });
    // create: putToRegistry(cjA)
    auto putCall = pctx->factory.CreatePutToRegistryCall(std::move(ctorCall));
    auto wrapperName = pctx->nameGenerator.GenerateInitCjObjectName(fwdCtor);
    auto wrapperFnTy = pctx->typeManager.GetFunctionTy(CollectTys(params), putCall->ty, {.isC = true});
    auto fn = CreateFunc(wrapperName, wrapperFnTy, std::move(params), Nodes(std::move(putCall)));
    UpdateWrapperAttr(*fn);
    PutIntoContext(std::move(fn));
}

/**
 * Generate @C warpper
 * func CJImpl_ObjC_glue_AFwd_deleteCJObject(registryId: RegistryId) {
 *     let fwd = getFromRegistryById<A_fwd>(registryId)
 *     fwd.cjFinalizable = true
 *     removeFromRegistry(registryId)
 *     fwd.mtx.unlock()
 * }
 */
void GenerateFwdClass::GenerateDeleteCJObject()
{
    CJC_ASSERT(fwdClass && curFile && cjFinalizableVar && mtxVar);

    auto registryIdTy = pctx->bridge.GetRegistryIdTy();
    auto param = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, registryIdTy);

    auto initializer = pctx->factory.CreateGetFromRegistryByIdCall(CreateRefExpr(*param), CreateType(fwdClass->ty));
    const auto fwdVarName = "fwd";
    auto fwdVar = CreateVar(fwdVarName, fwdClass->ty, false, std::move(initializer));
    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    // create: fwd.cjFinalizable = true
    auto trueLit = CreateLitConstExpr(LitConstKind::BOOL, "true", cjFinalizableVar->ty);
    auto cjFinalizableMA = CreateMemberAccess(CreateRefExpr(*fwdVar), *cjFinalizableVar);
    auto assign = CreateAssignExpr(std::move(cjFinalizableMA), std::move(trueLit), unitTy);
    // create: removeFromRegistry()
    auto remove = CreateCall(pctx->bridge.GetRemoveFromRegistryDecl(), curFile, CreateRefExpr(*param));
    // create: fwd.mtx.unlock()
    auto unlock = CreateMemCall(CreateMemberAccess(CreateRefExpr(*fwdVar), *mtxVar), pctx->bridge.GetMutexUnlock());

    auto fnTy = pctx->typeManager.GetFunctionTy({registryIdTy}, unitTy, {.isC = true});
    auto fnName = pctx->nameGenerator.GenerateDeleteCjObjectName(*fwdClass);
    auto fn = CreateFunc(fnName, fnTy, Nodes<FuncParam>(std::move(param)),
        Nodes(std::move(fwdVar), std::move(assign), std::move(remove), std::move(unlock)));
    UpdateWrapperAttr(*fn);
    PutIntoContext(std::move(fn));
}

/**
 * Generate @C warpper
 * func CJImpl_ObjC_glue_A_Fwd_lock/unlockCJObject(registryId: RegistryId) {
 *     getFromRegistryById<A_fwd>(registryId).mtx.lock()
 * }
 */
void GenerateFwdClass::GenerateLockOrUnlockCJObject(bool isLock)
{
    CJC_ASSERT(fwdClass && curFile && cjFinalizableVar && mtxVar);
    auto registryIdTy = pctx->bridge.GetRegistryIdTy();
    auto param = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, registryIdTy);
    auto fwdCall = pctx->factory.CreateGetFromRegistryByIdCall(CreateRefExpr(*param), CreateType(fwdClass->ty));
    // create: {}.mtx.lock() or {}.mtx.unlock()
    auto lockMem = isLock ? pctx->bridge.GetMutexLock() : pctx->bridge.GetMutexUnlock();
    auto call = CreateMemCall(CreateMemberAccess(std::move(fwdCall), *mtxVar), lockMem);
    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    auto fnTy = pctx->typeManager.GetFunctionTy({registryIdTy}, unitTy, {.isC = true});
    auto fnName = isLock ? pctx->nameGenerator.GenerateLockCjObjectName(*fwdClass)
                         : pctx->nameGenerator.GenerateUnlockCjObjectName(*fwdClass);
    auto fn = CreateFunc(fnName, fnTy, Nodes<FuncParam>(std::move(param)), Nodes(std::move(call)));
    UpdateWrapperAttr(*fn);
    PutIntoContext(std::move(fn));
}

/**
 * Generate @C warpper
 * func CJImpl_ObjC_glue_A_Fwd_foo(registryId: RegistryId, ...) {
 *     getFromRegistryById<A_fwd>(registryId).fooImpl(...)
 * }
 */
void GenerateFwdClass::GenerateFwdMemberFuncWrapper(FuncDecl& decl, FuncDecl& impl)
{
    CJC_ASSERT(fwdClass);
    auto registryIdTy = pctx->bridge.GetRegistryIdTy();
    auto param = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, registryIdTy);
    auto fwdCall = pctx->factory.CreateGetFromRegistryByIdCall(CreateRefExpr(*param), CreateType(fwdClass->ty));

    // create: cjA = A_fwd(registryId, ...)
    auto implCall = CreateMemCall(std::move(fwdCall), &impl);
    auto params = Nodes<FuncParam>(std::move(param));
    // Mapping: wrap params
    CreateParamsAndArgs(GetParams(impl), params, *implCall);
    auto fnName = pctx->nameGenerator.GenerateMethodWrapperName(decl);
    auto wrapperFnTy = pctx->typeManager.GetFunctionTy(CollectTys(params), implCall->ty, {.isC = true});
    auto fn = CreateFunc(fnName, wrapperFnTy, std::move(params), Nodes(std::move(implCall)));
    UpdateWrapperAttr(*fn);
    PutIntoContext(std::move(fn));
}

void GenerateFwdClass::GenerateCtors(ClassDecl& decl)
{
    for (auto& member : decl.GetMemberDecls()) {
        // if need mapping
        if (!member->TestAttr(Attribute::CONSTRUCTOR)) {
            continue;
        }
        auto fn = DynamicCast<FuncDecl*>(member.get());
        CJC_NULLPTR_CHECK(fn);
        GenerateConstructor4FwdClass(*fn);
    }
}

void GenerateFwdClass::GenerateMemberFuncsAndWrappers(ClassDecl& decl)
{
    std::vector<Ptr<FuncDecl>> openMembers;
    // collect open members
    CollectOpenMemberFuncs(decl, openMembers);
    // open member func
    for (size_t i = 0; i < openMembers.size(); i++) {
        auto fn = openMembers[i];
        auto impl = GenerateImplMethod4FwdClass(*fn);
        GenerateOverrideMethod4FwdClass(*fn, impl, i);
    }
}

void GenerateFwdClass::GenerateObjVar()
{
    auto ty = pctx->bridge.GetNativeObjCIdTy();
    auto var = CreateVar(OBJ_IDENT, ty, false);
    var->EnableAttr(Attribute::PRIVATE);
    objVar = var.get();
    PutIntoFwdClass(std::move(var));
}

void GenerateFwdClass::GenerateOverrideMaskVar()
{
    auto ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UINT64);
    auto var = CreateVar(OVERRIDE_MASK_IDENT, ty, false);
    var->EnableAttr(Attribute::PRIVATE);
    maskVar = var.get();
    PutIntoFwdClass(std::move(var));
}

void GenerateFwdClass::GenerateCjFinalizableVar()
{
    auto ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    auto initializer = CreateLitConstExpr(LitConstKind::BOOL, "false", ty);
    auto var = CreateVar(CJ_FINALIZABLE_IDENT, ty, true, std::move(initializer));
    var->EnableAttr(Attribute::INTERNAL, Attribute::HAS_INITED_FIELD);
    cjFinalizableVar = var.get();
    PutIntoFwdClass(std::move(var));
}

void GenerateFwdClass::GenerateMtxVar()
{
    auto ty = pctx->bridge.GetForwarderMutexTy();
    auto ctor = pctx->bridge.GetMutexConstructor();
    CJC_ASSERT(ctor);
    auto initializer = CreateCallExpr(CreateRefExpr(*ctor), {}, ctor, ty, CallKind::CALL_OBJECT_CREATION);
    auto var = CreateVar(MTX_IDENT, ty, false, std::move(initializer));
    var->EnableAttr(Attribute::INTERNAL, Attribute::HAS_INITED_FIELD);
    mtxVar = var.get();
    PutIntoFwdClass(std::move(var));
}

/**
 * Create global func for pure cangjie object create objective-c object.
 *
 * static func objcObjForPureCJ(cjA: A): NativeObjCId {
 *     return unsafe {
 *         CFunc<(NativeObjCId, NativeObjCSel, RegistryId) -> NativeObjCId>(
 *             objCMsgSend())(alloc("A"), registerName("initWithRegistryId:"), putToRegistry(cjA))
 *     }
 * }
 *
 */
void GenerateFwdClass::GenerateObjcObjForPureCJ(ClassDecl& decl)
{
    CJC_ASSERT(objVar);
    const auto paramId = "cjObj";
    auto param = CreateFuncParam(paramId, CreateType(decl.ty), nullptr, decl.ty);
    // alloc("A")
    auto nativeHandle = pctx->factory.CreateAllocCall(decl, decl.curFile);
    auto unsafeBlock = RegCjObjAndInitObjcObj(std::move(nativeHandle), CreateRefExpr(*param));
    auto fnTy = pctx->typeManager.GetFunctionTy({decl.ty}, objVar->ty);
    auto fn =
        CreateFunc(OBJC_FOR_PURE_CJ_IDENT, fnTy, Nodes<FuncParam>(std::move(param)), Nodes(std::move(unsafeBlock)));
    fn->EnableAttr(Attribute::PRIVATE, Attribute::STATIC);
    objcObj4PureCJFunc = fn.get();
    PutIntoFwdClass(std::move(fn));
}

/**
 * Create forward class constructor
 *
 * init(obj: NativeObjCId, overrideMask: UInt64, ...) {
 *     super(...)
 *     this.obj = obj
 *     this.overrideMask = overrideMask
 * }
 */
void GenerateFwdClass::GenerateConstructor4FwdClass(FuncDecl& decl)
{
    CJC_ASSERT(fwdClass && objVar && maskVar);
    auto fnTy = DynamicCast<FuncTy*>(decl.ty);
    auto superClassDecl = DynamicCast<ClassDecl*>(decl.outerDecl);
    CJC_NULLPTR_CHECK(superClassDecl);
    auto objParam = CreateFuncParam(OBJ_IDENT, CreateType(objVar->ty), nullptr, objVar->ty);
    auto maskParam = CreateFuncParam(OVERRIDE_MASK_IDENT, CreateType(maskVar->ty), nullptr, maskVar->ty);
    // assign exprs
    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    auto assignObj = CreateAssignExpr(CreateMemberAccessOfFwdClass(*objVar), CreateRefExpr(*objParam), unitTy);
    auto assignMask = CreateAssignExpr(CreateMemberAccessOfFwdClass(*maskVar), CreateRefExpr(*maskParam), unitTy);
    auto params = Nodes<FuncParam>(std::move(objParam), std::move(maskParam));
    // super call
    auto superCall = CreateSuperCall(*superClassDecl, decl, fnTy);
    CreateParamsAndArgs(GetParams(decl), params, *superCall);
    auto ctorTy = pctx->typeManager.GetFunctionTy(CollectTys(params), fwdClass->ty);
    auto ctor = CreateFunc(INIT_IDENT, ctorTy, std::move(params),
        Nodes(std::move(superCall), std::move(assignObj), std::move(assignMask)));
    ctor->EnableAttr(Attribute::PUBLIC, Attribute::CONSTRUCTOR);
    Ptr<FuncDecl> fwdClassCtor = ctor.get();
    PutIntoFwdClass(std::move(ctor));
    // Genrate wrapper
    GenerateInitCJObject(decl, *fwdClassCtor);
}

/**
 * Create forward class objcObj
 *
 * func objcObj(): NativeObjCId {
 *     mtx.lock()
 *     try {
 *         if (cjFinalizable) { // backOnTrack also known as TransitionIII
 *             cjFinalizable = false
 *             unsafe { CFunc<(NativeObjCId, NativeObjCSel, RegistryId) -> NativeObjCId>(objCMsgSend())(
 *                 obj, registerName("reinitWithRegistryId:"), putToRegistry(this)) }
 *         } else {
 *             unsafe { objCRetain(obj) }
 *         }
 *     } finally {
 *         mtx.unlock()
 *     }
 * }
 *
 */
void GenerateFwdClass::GenerateObjcObj4FwdClass()
{
    CJC_ASSERT(curFile && objVar && mtxVar && cjFinalizableVar);
    // create: assign
    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    auto initializer = CreateLitConstExpr(LitConstKind::BOOL, "false", cjFinalizableVar->ty);
    auto assign = CreateAssignExpr(CreateMemberAccessOfFwdClass(*cjFinalizableVar), std::move(initializer), unitTy);
    // create: unsafe call initWithRegistryId
    auto unsafeBlock = RegCjObjAndInitObjcObj(CreateRefExpr(*objVar), CreateThisOfFwdClass(), true);
    auto retainCall = CreateCall(pctx->bridge.GetObjCRetainDecl(), curFile, CreateRefExpr(*objVar));
    // if (cjFinalizable) {} else {}
    auto ifExpr =
        CreateIfExpr(CreateRefExpr(*cjFinalizableVar), CreateBlock(false, std::move(assign), std::move(unsafeBlock)),
            CreateBlock(false, CreateBlock(true, std::move(retainCall))), objVar->ty);
    // try {} finally {}
    auto tryExpr = MakeOwnedNode<TryExpr>();
    tryExpr->ty = ifExpr->ty;
    tryExpr->tryBlock = CreateBlock(false, std::move(ifExpr));
    tryExpr->finallyBlock = CreateBlock(false, CreateMemCall(CreateRefExpr(*mtxVar), pctx->bridge.GetMutexUnlock()));
    auto fnTy = pctx->typeManager.GetFunctionTy({}, objVar->ty);
    auto fn = CreateFunc(OBJC_OBJ_IDENT, fnTy, {},
        Nodes(CreateMemCall(CreateRefExpr(*mtxVar), pctx->bridge.GetMutexLock()), std::move(tryExpr)));
    fn->EnableAttr(Attribute::PRIVATE);
    objcObjFunc = fn.get();
    PutIntoFwdClass(std::move(fn));
}

/**
 * Create forward class objcObj
 * func objcObjAutoreleased(): NativeObjCId {
 *     unsafe { objCAutorelease(objcObj()) }
 * }
 */
void GenerateFwdClass::GenerateAutoreleased4FwdClass()
{
    CJC_ASSERT(curFile && objcObjFunc);
    auto objcAutoReleaseDecl = pctx->bridge.GetObjCAutoReleaseDecl();
    auto autoCall = CreateCall(objcAutoReleaseDecl, curFile, CreateCall(objcObjFunc, curFile));
    auto fnTy = pctx->typeManager.GetFunctionTy({}, objVar->ty);
    auto fn = CreateFunc(AUTO_RELEASE_IDENT, fnTy, {}, Nodes(CreateBlock(true, std::move(autoCall))));
    fn->EnableAttr(Attribute::PRIVATE);
    objcAutoReleaseFunc = fn.get();
    PutIntoFwdClass(std::move(fn));
}

/**
 * Create forward class implementation method
 * func fooImpl(...) {
 *     super.foo(...)
 * }
 */
Ptr<FuncDecl> GenerateFwdClass::GenerateImplMethod4FwdClass(FuncDecl& decl)
{
    auto fnTy = DynamicCast<FuncTy*>(decl.ty);
    CJC_NULLPTR_CHECK(fnTy);
    auto superClassDecl = DynamicCast<ClassDecl*>(decl.outerDecl);
    // super.foo(...)
    auto superCall = CreateMemCall(CreateSuperRef(superClassDecl, superClassDecl->ty), &decl);
    std::vector<OwnedPtr<FuncParam>> params;
    CreateParamsAndArgs(GetParams(decl), params, *superCall);
    auto fn = CreateFunc(decl.identifier.Val() + IMPL_SUFFIX, fnTy, std::move(params), Nodes(std::move(superCall)));
    fn->EnableAttr(Attribute::INTERNAL);
    auto ret = fn.get();
    PutIntoFwdClass(std::move(fn));
    return ret;
}

/**
 * Create forward class override method
 * public/protected override func foo() {
 *     if ((overrideMask & (1 << 0)) != 0) {
 *        withAutoreleasePool<{ret}> { => unsafe {
 *             CFunc<(NativeObjCId, NativeObjCSel, ...) -> {ret}>(objCMsgSend())(
 *                 objcObjAutoreleased(), registerName("foo"), ...)
 *        }}
 *     } else {
 *         fooImpl()
 *     }
 * }
 */
void GenerateFwdClass::GenerateOverrideMethod4FwdClass(FuncDecl& decl, Ptr<FuncDecl> impl, size_t mid)
{
    auto fnTy = DynamicCast<FuncTy*>(decl.ty);
    CJC_NULLPTR_CHECK(fnTy);
    auto implCall = CreateMemberCall(CreateThisOfFwdClass(), impl);
    std::vector<OwnedPtr<FuncParam>> params;
    CreateParamsAndArgs(GetParams(decl), params, *implCall);
    auto cond = CreateMaskCond(mid);
    // if (cond) {} else {}
    auto ifExpr = CreateIfExpr(std::move(cond), CreateBlock(false, CreateAutoReleaseCall(decl, params)),
        CreateBlock(false, std::move(implCall)), fnTy->retTy);
    auto fn = CreateFunc(decl.identifier.Val(), fnTy, std::move(params), Nodes(std::move(ifExpr)));
    fn->EnableAttr(Attribute::PUBLIC, Attribute::OVERRIDE);
    auto fwdMem = fn.get();
    pctx->fwdOverrideTable.emplace(fwdMem, &decl);
    PutIntoFwdClass(std::move(fn));
    GenerateFwdMemberFuncWrapper(*fwdMem, *impl);
}

/**
 * Generate forward class finalizer
 * ~init() {
 *     unsafe { objCRelease(obj) }
 * }
 */
void GenerateFwdClass::GenerateFinalizer4FwdClass()
{
    CJC_ASSERT(curFile && objVar);
    auto call = CreateCall(pctx->bridge.GetObjCReleaseDecl(), curFile, CreateRefExpr(*objVar));
    auto fnTy = pctx->typeManager.GetFunctionTy({}, TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT));
    auto fn = CreateFunc(FINALIZER_IDENT, fnTy, {}, Nodes(CreateBlock(true, std::move(call))));
    fn->EnableAttr(Attribute::INTERNAL, Attribute::FINALIZER);
    PutIntoFwdClass(std::move(fn));
}

void GenerateFwdClass::PutIntoFwdClass(OwnedPtr<Decl> decl)
{
    CJC_ASSERT(fwdClass && fwdClass->body);
    decl->outerDecl = fwdClass;
    decl->fullPackageName = fwdClass->fullPackageName;
    decl->EnableAttr(Attribute::IN_CLASSLIKE);
    if (decl->astKind == ASTKind::FUNC_DECL) {
        auto& body = StaticCast<FuncDecl>(decl.get())->funcBody;
        CJC_NULLPTR_CHECK(body);
        body->parentClassLike = fwdClass;
    }
    fwdClass->body->decls.emplace_back(std::move(decl));
}

OwnedPtr<RefExpr> GenerateFwdClass::CreateThisOfFwdClass()
{
    CJC_NULLPTR_CHECK(fwdClass);
    return CreateThisRef(fwdClass, fwdClass->ty, fwdClass->curFile);
}

OwnedPtr<MemberAccess> GenerateFwdClass::CreateMemberAccessOfFwdClass(Decl& decl)
{
    return CreateMemberAccess(CreateThisOfFwdClass(), decl);
}

OwnedPtr<Block> GenerateFwdClass::RegCjObjAndInitObjcObj(OwnedPtr<Expr> objcObj, OwnedPtr<Expr> cjObj, bool reinit)
{
    CJC_ASSERT(curFile && objVar);
    // create: putToRegistry(cjA)
    auto putCall = pctx->factory.CreatePutToRegistryCall(std::move(cjObj));
    // create: CFunc<...>(objCMsgSend())(alloc("A"), registerName("initWithRegistryId:"), tmp)
    objcObj->curFile = curFile;
    auto sel = reinit ? REINIT_WITH_REG_ID_SEL : INIT_WITH_REG_ID_SEL;
    auto call =
        pctx->factory.CreateObjCMsgSendCall(std::move(objcObj), sel, objVar->ty, Nodes<Expr>(std::move(putCall)));
    return CreateBlock(true, std::move(call));
}

OwnedPtr<Expr> GenerateFwdClass::CreateAutoReleaseCall(FuncDecl& decl, const std::vector<OwnedPtr<FuncParam>>& params)
{
    CJC_ASSERT(curFile && objcAutoReleaseFunc);
    auto fnTy = DynamicCast<FuncTy*>(decl.ty);
    CJC_NULLPTR_CHECK(fnTy);
    std::vector<OwnedPtr<Expr>> msgSendArgs;
    // the rule: primitive -> primitive.
    std::transform(params.begin(), params.end(), std::back_inserter(msgSendArgs),
        [this](auto& param) { return pctx->factory.UnwrapEntity(WithinFile(CreateRefExpr(*param), curFile)); });

    auto nativeHandle = CreateMemCall(CreateThisOfFwdClass(), objcAutoReleaseFunc);
    nativeHandle->curFile = curFile;
    // Need selector names
    auto sel = decl.identifier.Val() + std::string(params.size(), ':');
    auto nativeCall = pctx->factory.CreateObjCMsgSendCall(
        std::move(nativeHandle), sel, pctx->typeMapper.Cj2CType(fnTy->retTy), std::move(msgSendArgs));
    return pctx->factory.CreateAutoreleasePoolScope(fnTy->retTy, Nodes(std::move(nativeCall)));
}

OwnedPtr<Expr> GenerateFwdClass::CreateMaskCond(size_t mid)
{
    CJC_ASSERT(maskVar);
    auto boolTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    auto uint64Ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UINT64);
    auto int64Ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64);
    // (overrideMask & (1 << 0)) != 0
    auto oneLit = CreateLitConstExpr(LitConstKind::INTEGER, "1", uint64Ty);
    auto idLit = CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(mid), int64Ty);
    auto lshiftExpr = CreateBinaryExpr(std::move(oneLit), std::move(idLit), TokenKind::LSHIFT);
    auto bitAndExpr = CreateBinaryExpr(
        CreateMemberAccessOfFwdClass(*maskVar), CreateParenExpr(std::move(lshiftExpr)), TokenKind::BITAND);
    auto zeroLit = CreateLitConstExpr(LitConstKind::INTEGER, "0", uint64Ty);
    auto cond = CreateBinaryExpr(CreateParenExpr(std::move(bitAndExpr)), std::move(zeroLit), TokenKind::NOTEQ);
    cond->ty = boolTy;
    return std::move(cond);
}

void GenerateFwdClass::Clear()
{
    fwdClass = nullptr;
    objVar = nullptr;
    maskVar = nullptr;
    cjFinalizableVar = nullptr;
    mtxVar = nullptr;
    objcObj4PureCJFunc = nullptr;
    objcObjFunc = nullptr;
    objcAutoReleaseFunc = nullptr;
}

void GenerateFwdClass::PutIntoContext(OwnedPtr<AST::Decl> decl)
{
    CJC_ASSERT(pctx && curFile);
    // Update curFile
    AddCurFile(*decl, curFile);
    decl->fullPackageName = curFile->GetFullPackageName();
    pctx->genDecls.push_back(std::move(decl));
}
