// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "JavaDesugarManager.h"

#include "NativeFFI/Java/JavaCodeGenerator/JavaSourceCodeGenerator.h"
#include "NativeFFI/Utils.h"
#include "Utils.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"

namespace Cangjie::Interop::Java {
using namespace Cangjie::Native::FFI;

namespace {
// Some helper functions.
inline Ptr<CallExpr> TryGetSuperCall(const FuncDecl& ctor)
{
    if (!ctor.funcBody) {
        return nullptr;
    }
    if (!ctor.funcBody->body) {
        return nullptr;
    }
    if (ctor.funcBody->body->body.empty()) {
        return nullptr;
    }
    auto firstNode = ctor.funcBody->body->body[0].get();
    if (auto callExpr = As<ASTKind::CALL_EXPR>(firstNode);
        callExpr && callExpr->callKind == CallKind::CALL_SUPER_FUNCTION) {
        return callExpr;
    }
    return nullptr;
}

inline Ptr<FuncTy> GetNativeFuncTy(const std::vector<OwnedPtr<FuncParam>>& params,
    const std::function<Ptr<Ty>(const FuncParam&)>& toJni, Ptr<Ty> jniRet, TypeManager& typeManager)
{
    std::vector<Ptr<Ty>> funcTyParams;
    for (auto& param : params) {
        funcTyParams.push_back(toJni(*param));
    }
    return typeManager.GetFunctionTy(funcTyParams, jniRet, {.isC = true});
}

size_t GetCtorId(const FuncDecl& ctor)
{
    auto decl = As<ASTKind::CLASS_LIKE_DECL>(ctor.outerDecl);
    CJC_NULLPTR_CHECK(decl);
    auto& members = decl->GetMemberDecls();
    for (size_t i = 0; i < members.size(); i++) {
        if (&ctor == members[i].get()) {
            return i;
        }
    }
    return 0;
}

// Check whether decl is instance member.
inline bool IsInstMember(const Decl& decl)
{
    return decl.IsMemberDecl() && !decl.TestAnyAttr(Attribute::STATIC, Attribute::CONSTRUCTOR);
}

// Check whether the decl is static member.
inline bool IsStaticMember(const Decl& decl)
{
    return decl.IsMemberDecl() && decl.TestAttr(Attribute::STATIC);
}

// Check whether the decl is private static member.
inline bool IsPrivateStaticMember(const Ptr<Decl> decl)
{
    return decl && IsStaticMember(*decl) && decl->TestAttr(Attribute::PRIVATE);
}

/**
 * Collecting parameters used by expressions and encoding the ids of using parameter.
 *
 * @param expr the expression to be checked
 * @param params the original parameters of constructor
 * @param usingParams the result of using params expr
 *
 * @return {usingThis, paramIds} whether "this" is used and the paramIds encoding string.
 */
std::pair<bool, std::string> CollectParams(
    Ptr<Expr> expr, const std::vector<OwnedPtr<FuncParam>>& params, std::vector<Ptr<FuncParam>>& usingParams)
{
    bool usingThis = false;
    std::set<Ptr<Decl>> refParams;
    Walker(expr, [&usingThis, &refParams](auto node) {
        auto ref = As<ASTKind::REF_EXPR>(node);
        if (!ref) {
            return VisitAction::WALK_CHILDREN;
        }
        auto target = ref->ref.target;
        CJC_NULLPTR_CHECK(target);
        // explicit and implicit using this
        if (ref->isThis || IsInstMember(*target)) {
            usingThis = true;
        } else if (target->astKind == ASTKind::FUNC_PARAM) {
            // using parameter
            refParams.insert(ref->ref.target);
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();

    std::vector<std::string> ids;
    for (size_t pi = 0; pi < params.size(); pi++) {
        if (refParams.find(params[pi].get()) != refParams.end()) {
            ids.push_back(std::to_string(pi));
            usingParams.push_back(params[pi].get());
        }
    }
    std::string paramIds = "";
    if (!ids.empty()) {
        const std::string psep = "P";
        paramIds = psep + Cangjie::Utils::JoinStrings(ids, psep);
    }
    return {usingThis, paramIds};
}

/**
 * Collecting using static refs including fields and funcs and check whether using private static members.
 *
 * @param expr the expression to be checked
 * @param staticRefs the result of using static ref exprs
 *
 * @return usingPrivate whether using private static members.
 */
bool CollectStaticRefs(Ptr<Expr> expr, std::vector<Ptr<RefExpr>>& staticRefs)
{
    bool usingPrivate = false;
    Walker(expr, [&usingPrivate, &staticRefs](auto node) {
        if (auto ref = As<ASTKind::REF_EXPR>(node)) {
            if (ref->desugarExpr) {
                return VisitAction::WALK_CHILDREN;
            }
            auto target = ref->ref.target;
            CJC_NULLPTR_CHECK(target);
            if (!IsStaticMember(*target)) {
                return VisitAction::SKIP_CHILDREN;
            }
            if (target->TestAttr(Attribute::PRIVATE)) {
                usingPrivate = true;
            }
            staticRefs.push_back(ref);
        } else if (auto ma = As<ASTKind::MEMBER_ACCESS>(node)) {
            if (IsPrivateStaticMember(ma->target)) {
                usingPrivate = true;
            }
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    return usingPrivate;
}
}

/**
 * Unwrap a refExpr of native func param into Cangjie type.
 * 1. Primitive Type
 * 2. JavaMirror/JavaImpl: jobject -> JavaEntity -> @JavaMirror class M / @JavaImpl class A
 *
 * For example:
 *     pa: jobject -> A(Java_CFFI_JavaEntityJobject(pa)): A
 */
OwnedPtr<Expr> JavaDesugarManager::UnwrapRefExpr(OwnedPtr<RefExpr> ref, Ptr<Ty> targetTy, const ClassLikeDecl& decl)
{
    // Do not wrap for primitive type.
    if (targetTy->IsPrimitive()) {
        return std::move(ref);
    }
    // Mirror or Impl or Option or other?
    // jobject -> JavaEntity
    auto entity = lib.CreateJavaEntityJobjectCall(std::move(ref));
    // JavaEntity -> Cangjie Type
    return lib.UnwrapJavaEntity(std::move(entity), targetTy, decl);
}

/**
 * Wrap the cangjie expr into native type.
 * 1. Primitive Type
 * 2. JavaMirror/JavaImpl:  @JavaMirror class M / @JavaImpl class A -> JavaEntity -> jobject
 *
 * For example: foo(m, a) with type M
 * withExceptionHandling(env, { =>
 *   let tmp = foo(m, a)
 *   return Java_CFFI_unwrapJavaEntityAsValue(tmp.javaref)
 * })
 */
OwnedPtr<Expr> JavaDesugarManager::WrapExprWithExceptionHandling(
    std::vector<OwnedPtr<Node>>&& nodes, OwnedPtr<Expr> expr, FuncParam& env, const ClassLikeDecl& decl)
{
    auto curFile = expr->curFile;
    auto retTy = expr->ty;
    auto res = CreateTmpVarDecl(nullptr, std::move(expr));
    OwnedPtr<Expr> ret = WithinFile(CreateRefExpr(*res), curFile);
    if (retTy->IsPrimitive()) {
        ret = WithinFile(CreateRefExpr(*res), curFile);
    } else {
        // Cangjie Type -> JavaEntity -> jobject
        ret = lib.UnwrapJavaEntity(lib.WrapJavaEntity(WithinFile(CreateRefExpr(*res), curFile)), retTy, decl, true);
    }
    nodes.push_back(std::move(res));
    nodes.push_back(std::move(ret));
    return lib.WrapExceptionHandling(WithinFile(CreateRefExpr(env), curFile),
        WrapReturningLambdaExpr(typeManager, std::move(nodes)));
}

OwnedPtr<FuncDecl> JavaDesugarManager::CreateNativeFunc(const std::string& funcName,
    std::vector<OwnedPtr<FuncParam>> params, Ptr<Ty> retTy, std::vector<OwnedPtr<Node>> nodes)
{
    auto jniRetTy = GetJNITy(retTy);
    auto funcTy = GetNativeFuncTy(params,
        [this](const FuncParam& node) { return GetJNITy(node.ty);}, jniRetTy, typeManager);
    auto block = CreateBlock(std::move(nodes), jniRetTy);
    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.push_back(CreateFuncParamList(std::move(params)));
    auto funcBody = CreateFuncBody(std::move(paramLists), CreateType(jniRetTy), std::move(block), funcTy);
    auto fdecl = CreateFuncDecl(funcName, std::move(funcBody), funcTy);
    fdecl->funcBody->funcDecl = fdecl.get();
    fdecl->EnableAttr(Attribute::GLOBAL, Attribute::PUBLIC, Attribute::C);
    return fdecl;
}

OwnedPtr<FuncDecl> JavaDesugarManager::GenerateNativeFunc4Argument(const FuncArg& arg,
    const std::vector<OwnedPtr<FuncParam>>& params, ClassLikeDecl& decl, size_t ctorId, size_t argId)
{
    std::vector<Ptr<FuncParam>> usingParams;
    auto [usingThis, paramIds] = CollectParams(arg.expr.get(), params, usingParams);
    if (usingThis) {
        // Report error for using this in super call.
        diag.DiagnoseRefactor(DiagKindRefactor::sema_java_interop_not_supported, arg, "using this in super call");
        return nullptr;
    }
    std::vector<Ptr<RefExpr>> staticRefs;
    auto usingPrivate = CollectStaticRefs(arg.expr.get(), staticRefs);
    if (usingPrivate) {
        // Report error for using private static member in super call.
        diag.DiagnoseRefactor(
            DiagKindRefactor::sema_java_interop_not_supported, arg, "using private static member in super call");
        return nullptr;
    }
    // The encoding rules of super call id: C{ctorId}A{argId}{usingParamIds: P{paramId}P{paramId}...}
    //     {ctorId} is the index of the constructor containing the super call in the class body.
    //     {argId} is the index of the func argument in super call.
    //     {usingParamIds} is the indexes of the func argument using parameters of the constructor.
    std::string id = "C" + std::to_string(ctorId) + "A" + std::to_string(argId);
    std::string funcName = GetJniSuperArgFuncName(decl, id) + paramIds;
    std::vector<OwnedPtr<FuncParam>> funcParams;
    funcParams.push_back(lib.CreateEnvFuncParam());
    // jobject or jclass
    funcParams.push_back(lib.CreateJClassOrJObjectFuncParam());
    // Body
    std::vector<OwnedPtr<Node>> nodes; // collect temp vars
    std::map<Ptr<Decl>, Ptr<VarDecl>> source2cloned;
    for (size_t pi = 0; pi < usingParams.size(); pi++) {
        // Create func param for native func
        auto funcParam = CreateFuncParam(
            usingParams[pi]->identifier.Val(), nullptr, nullptr, GetJNITy(usingParams[pi]->ty));
        auto ref = WithinFile(CreateRefExpr(*funcParam), decl.curFile);
        funcParams.push_back(std::move(funcParam));
        // Create temp var wrapper for using param: `let tv = unwrap(p0)`.
        OwnedPtr<Expr> initializer = UnwrapRefExpr(std::move(ref), usingParams[pi]->ty, decl);
        auto tv = CreateTmpVarDecl(nullptr, std::move(initializer));
        source2cloned[usingParams[pi]] = tv.get();
        nodes.push_back(std::move(tv));
    }
    for (auto sf : staticRefs) {
        // Create temp var for static member ref: `let tv = A.sf`.
        auto outerDecl = sf->ref.target->outerDecl;
        CJC_NULLPTR_CHECK(outerDecl);
        OwnedPtr<Expr> initializer = CreateMemberAccess(CreateRefExpr(*outerDecl), *sf->ref.target);
        auto tv = CreateTmpVarDecl(nullptr, std::move(initializer));
        source2cloned[sf->ref.target] = tv.get();
        nodes.push_back(std::move(tv));
    }
    // For `foo(x, y + z)` the parameters x, y and implicit static field z need be replaced by temp vars.
    // -----------------
    // let tmp0 = unwrap(x)
    // let tmp1 = unwrap(y)
    // let tmp2 = A.z
    // foo(tmp0, tmp1 + tmp2)
    auto replaceTarget = [&mp = std::as_const(source2cloned)](Node& source, Node& target) {
        if (auto srcRef = As<ASTKind::REF_EXPR>(&source)) {
            // replace ref by temp var
            auto it = mp.find(srcRef->ref.target);
            if (it != mp.end()) {
                auto dstRef = StaticAs<ASTKind::REF_EXPR>(&target);
                dstRef->ref.identifier = it->second->identifier;
                dstRef->ref.target = it->second;
            }
        } else if (auto call = As<ASTKind::CALL_EXPR>(&source)) {
            if (call->baseFunc->astKind != ASTKind::REF_EXPR) {
                return;
            }
            auto refExpr = As<ASTKind::REF_EXPR>(call->baseFunc);
            auto it = mp.find(refExpr->ref.target);
            // update call member access by call temp var
            if (it != mp.end()) {
                auto targetCall = As<ASTKind::CALL_EXPR>(&target);
                targetCall->resolvedFunction = nullptr;
                targetCall->callKind = CallKind::CALL_FUNCTION_PTR;
            }
        }
    };
    auto clonedExpr = ASTCloner::Clone(arg.expr.get(), replaceTarget);
    auto wrapper = WrapExprWithExceptionHandling(std::move(nodes), std::move(clonedExpr), *funcParams[0], decl);
    OwnedPtr<FuncDecl> nativeFn = CreateNativeFunc(
        funcName, std::move(funcParams), arg.expr->ty, Nodes(std::move(wrapper)));
    nativeFn->moduleName = decl.moduleName;
    nativeFn->fullPackageName = decl.fullPackageName;
    // update curFile info.
    AddCurFile(*nativeFn, decl.curFile);
    return nativeFn;
}

OwnedPtr<CallExpr> JavaDesugarManager::DesugarJavaImplSuperCall(const FuncDecl& ctor, Decl& jniEnvVar)
{
    auto& paramList = *ctor.funcBody->paramLists[0];
    auto decl = As<ASTKind::CLASS_LIKE_DECL>(ctor.outerDecl);
    // super call
    auto superCall = TryGetSuperCall(ctor);
    CJC_NULLPTR_CHECK(superCall);
    size_t ctorId = GetCtorId(ctor);
    auto needGen = [](const Expr& expr) {
        if (expr.astKind == ASTKind::LIT_CONST_EXPR) {
            return false;
        }
        if (expr.astKind != ASTKind::REF_EXPR) {
            return true;
        }
        auto refTarget = StaticAs<ASTKind::REF_EXPR>(&expr)->ref.target;
        CJC_NULLPTR_CHECK(refTarget);
        return refTarget->astKind != ASTKind::FUNC_PARAM;
    };
    auto& args = superCall->args;
    for (size_t index = 0; index < args.size(); index++) {
        // generate @C func on demand
        if (!needGen(*args[index]->expr)) {
            continue;
        }
        if (auto argFn = GenerateNativeFunc4Argument(*args[index], paramList.params, *decl, ctorId, index); argFn) {
             // record desugared function
            args[index]->expr->desugarExpr = CreateRefExpr(*argFn);
            generatedDecls.push_back(std::move(argFn));
        }
    }
    // Java_CFFI_newJavaObject_raw call
    return lib.CreateCFFINewJavaObjectCall(WithinFile(CreateRefExpr(jniEnvVar), ctor.curFile),
        utils.GetJavaClassNormalizeSignature(*decl->ty), paramList, false, *ctor.curFile);
}

void JavaDesugarManager::DesugarJavaImplConstructor(FuncDecl& ctor, FuncDecl& parentCtor)
{
    auto curFile = ctor.curFile;
    CJC_ASSERT(ctor.TestAttr(Attribute::CONSTRUCTOR));
    auto classLikeDecl = As<ASTKind::CLASS_LIKE_DECL>(ctor.outerDecl);
    CJC_NULLPTR_CHECK(classLikeDecl);
    ctor.constructorCall = ConstructorCall::SUPER;

    auto jniEnvCall = lib.CreateGetJniEnvCall(curFile);
    if (!jniEnvCall) {
        ctor.EnableAttr(Attribute::IS_BROKEN);
        return;
    }

    auto jniEnvPtrDecl = lib.GetJniEnvPtrDecl();
    if (!jniEnvPtrDecl) {
        ctor.EnableAttr(Attribute::IS_BROKEN);
        return;
    }

    ctor.EnableAttr(Attribute::UNSAFE);

    auto jniEnvVar = CreateTmpVarDecl(jniEnvPtrDecl->type, jniEnvCall);

    CJC_ASSERT(ctor.funcBody);
    CJC_ASSERT(ctor.funcBody->paramLists.size() == 1);

    if (auto newObjCall = DesugarJavaImplSuperCall(ctor, *jniEnvVar)) {
        auto jniEnvRef = WithinFile(CreateRefExpr(*jniEnvVar), curFile);
        if (auto newWeakRefCall = lib.CreateNewGlobalRefCall(std::move(jniEnvRef), std::move(newObjCall), true)) {
            std::vector<OwnedPtr<Node>> nodes;
            nodes.push_back(std::move(jniEnvVar));
            nodes.push_back(std::move(newWeakRefCall));
            auto lambdaCall = WrapReturningLambdaCall(typeManager, std::move(nodes));
            auto superCall = CreateSuperCall(*ctor.outerDecl, parentCtor, parentCtor.ty);

            superCall->args.insert(superCall->args.begin(), CreateFuncArg(std::move(lambdaCall)));

            if (!ctor.funcBody->body->body.empty()) {
                auto firstNode = ctor.funcBody->body->body[0].get();
                if (auto callExpr = As<ASTKind::CALL_EXPR>(firstNode);
                    callExpr && (callExpr->callKind == CallKind::CALL_SUPER_FUNCTION)) {
                        // This super call `callExpr` will be removed in `JavaSourceCodeGenerator`
                        callExpr->EnableAttr(Attribute::JAVA_MIRROR, Attribute::UNREACHABLE);
                }
            }

            auto putToRegistryCall = lib.CreatePutToRegistrySelfInitCall(
                lib.CreateGetJniEnvCall(curFile), CreateJavaRefCall(*classLikeDecl, curFile),
                CreateThisRef(Ptr(classLikeDecl), classLikeDecl->ty, curFile));
            ctor.funcBody->body->body.insert(ctor.funcBody->body->body.begin(), std::move(putToRegistryCall));
            // insert generated super call at the beginning of the constructor
            ctor.funcBody->body->body.insert(ctor.funcBody->body->body.begin(), std::move(superCall));
            /*
                We can't remove existing user-defined super call on this stage
                because it will be used for javacode generation
             */
        } else {
            ctor.EnableAttr(Attribute::IS_BROKEN);
        }
    } else {
        ctor.EnableAttr(Attribute::IS_BROKEN);
    }
}

OwnedPtr<FuncDecl> JavaDesugarManager::GenerateJavaImplConstructor(FuncDecl& sampleCtor, ClassLikeDecl& parent)
{
    auto ctor = ASTCloner::Clone(Ptr(&sampleCtor));

    // obj: Java_CFFI_JavaEntity
    auto javaEntityDecl = lib.GetJavaEntityDecl();
    if (!javaEntityDecl) {
        return nullptr;
    }

    auto entityParam = CreateFuncParam(
        JAVA_IMPL_ENTITY_ARG_NAME_IN_GENERATED_CTOR,
        CreateRefType(*javaEntityDecl), nullptr, javaEntityDecl->ty);

    auto entityParamRef = WithinFile(CreateRefExpr(*entityParam), sampleCtor.curFile);
    ctor->funcBody->paramLists[0]->params.insert(ctor->funcBody->paramLists[0]->params.begin(), std::move(entityParam));

    Ptr<FuncDecl> parentCtor = GetGeneratedJavaMirrorConstructor(parent);
    CJC_ASSERT(parentCtor);
    CJC_ASSERT(parentCtor->funcBody->paramLists[0]->params.size() == 1); // Java_CFFI_JavaEntity

    std::vector<Ptr<Ty>> paramTys;
    paramTys.push_back(javaEntityDecl->ty);
    for (auto paramTy : StaticCast<FuncTy*>(sampleCtor.ty.get())->paramTys) {
        paramTys.push_back(paramTy);
    }
    auto ctorTy = typeManager.GetFunctionTy(paramTys, StaticCast<FuncTy*>(sampleCtor.ty.get())->retTy);

    auto superCall = CreateSuperCall(*sampleCtor.outerDecl, *parentCtor, parentCtor->ty);
    superCall->args.push_back(CreateFuncArg(std::move(entityParamRef)));

    auto& block = ctor->funcBody->body;

    block->body.erase(std::remove_if(block->body.begin(), block->body.end(),
        [](auto& node) {
            if (auto call = As<ASTKind::CALL_EXPR>(node.get())) {
                return call->callKind == CallKind::CALL_SUPER_FUNCTION;
            }
            return false;
        }),
        block->body.end());

    block->body.insert(block->body.begin(), std::move(superCall));
    ctor->funcBody->ty = ctorTy;
    ctor->ty = ctorTy;
    ctor->funcBody->funcDecl = ctor.get();
    ctor->constructorCall = ConstructorCall::SUPER;
    ctor->DisableAttr(Attribute::PRIMARY_CONSTRUCTOR);
    ctor->EnableAttr(Attribute::JAVA_MIRROR_SUBTYPE);
    ctor->EnableAttr(Attribute::UNSAFE);
    return std::move(ctor);
}

OwnedPtr<PrimitiveType> JavaDesugarManager::CreateUnitType()
{
    auto type = MakeOwned<PrimitiveType>();
    type->str = "Unit";
    type->kind = TypeKind::TYPE_UNIT;
    type->ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    return type;
}

Ptr<Ty> JavaDesugarManager::GetJNITy(Ptr<Ty> ty)
{
    static auto jobjectTy = lib.GetJobjectTy();
    static auto jlongTy = lib.GetJlongTy();

    if (!ty) {
        return nullptr;
    }
    if (ty->IsCoreOptionType()) {
        return jobjectTy;
    }
    if (IsMirror(*ty) || IsImpl(*ty)) {
        return jobjectTy;
    }
    if (IsCJMapping(*ty)) {
        return jlongTy;
    }
    CJC_ASSERT(ty->IsBuiltin());
    return ty;
}

std::string JavaDesugarManager::GetJniMethodName(const FuncDecl& method)
{
    auto sampleJavaName = GetJavaMemberName(method);
    std::string fqname = GetJavaFQName(*(method.outerDecl));
    MangleJNIName(fqname);
    auto mangledFuncName = GetMangledMethodName(mangler, method.funcBody->paramLists[0]->params, sampleJavaName);
    MangleJNIName(mangledFuncName);

    return "Java_" + fqname + "_" + mangledFuncName;
}

std::string JavaDesugarManager::GetJniMethodNameForProp(const PropDecl& propDecl, bool isSet) const
{
    std::string varDecl = GetJavaMemberName(propDecl);
    std::string varDeclSuffix = varDecl;
    varDeclSuffix[0] = static_cast<char>(toupper(varDeclSuffix[0]));
    std::string fqname = GetJavaFQName(*(propDecl.outerDecl));
    MangleJNIName(fqname);
    return "Java_" + fqname + (isSet ? "_set" : "_get") + varDeclSuffix + "Impl";
    ;
}

inline std::string JavaDesugarManager::GetJniSuperArgFuncName(const ClassLikeDecl& outer, const std::string& id) const
{
    std::string fqname = GetJavaFQName(outer);
    MangleJNIName(fqname);
    return "Java_" + fqname + "_super" + id;
}

std::string JavaDesugarManager::GetJniInitCjObjectFuncName(const FuncDecl& ctor, bool isGeneratedCtor)
{
    std::string fqname = GetJavaFQName(*(ctor.outerDecl));
    MangleJNIName(fqname);
    auto mangledFuncName = GetMangledJniInitCjObjectFuncName(mangler, ctor.funcBody->paramLists[0]->params,
                                                             isGeneratedCtor);
    MangleJNIName(mangledFuncName);

    if (auto enumDecl = As<ASTKind::ENUM_DECL>(ctor.outerDecl)) {
        mangledFuncName = ctor.identifier + mangledFuncName;
    }

    return "Java_" + fqname + "_" + mangledFuncName;
}

std::string JavaDesugarManager::GetJniInitCjObjectFuncNameForVarDecl(const AST::VarDecl& ctor) const
{
    std::string fqname = GetJavaFQName(*(ctor.outerDecl));
    MangleJNIName(fqname);
    auto mangledFuncName = ctor.identifier.Val();
    MangleJNIName(mangledFuncName);
    return "Java_" + fqname + "_" + mangledFuncName + "initCJObject";
}

std::string JavaDesugarManager::GetJniDeleteCjObjectFuncName(const Decl& decl) const
{
    std::string fqname = GetJavaFQName(decl);
    MangleJNIName(fqname);

    return "Java_" + fqname + "_deleteCJObject";
}

OwnedPtr<Decl> JavaDesugarManager::GenerateNativeDeleteCjObjectFunc(ClassLikeDecl& javaImpl, VarDecl& javaWeakRefField)
{
    auto curFile = javaImpl.curFile;
    CJC_NULLPTR_CHECK(javaImpl.curFile);
    std::vector<OwnedPtr<FuncParam>> params;
    FuncParam* jniEnvPtrParam = nullptr;
    OwnedPtr<Expr> selfParamRef;
    GenerateFuncParamsForNativeDeleteCjObject(javaImpl, params, jniEnvPtrParam, selfParamRef);
    /* lambda:
        { objToDelete: <type> =>
            return objToDelete.javaref
        }
    */

    std::vector<OwnedPtr<FuncParam>> lambdaParams;
    lambdaParams.push_back(CreateFuncParam("objToDelete", CreateRefType(javaImpl), nullptr, javaImpl.ty));
    std::vector<OwnedPtr<FuncParamList>> lambdaParamLists;
    lambdaParamLists.push_back(CreateFuncParamList(std::move(lambdaParams)));

    auto retExpr = CreateReturnExpr(CreateMemberAccess(
        WithinFile(CreateRefExpr(*lambdaParamLists[0]->params[0]), curFile), javaWeakRefField));
    retExpr->ty = TypeManager::GetNothingTy();

    auto javaEntityDecl = lib.GetJavaEntityDecl();
    if (!javaEntityDecl) {
        return nullptr;
    }
    auto javaEntityType = CreateRefType(*javaEntityDecl);

    auto lambda = CreateLambdaExpr(CreateFuncBody(
        std::move(lambdaParamLists), std::move(javaEntityType), CreateBlock({}, javaEntityDecl->ty), javaEntityDecl->ty
    ));

    retExpr->refFuncBody = lambda->funcBody.get();
    lambda->funcBody->body->body.push_back(std::move(retExpr));
    lambda->curFile = curFile;

    std::vector<Ptr<Ty>> lambdaParamTys;
    lambdaParamTys.push_back(javaImpl.ty);
    lambda->ty = typeManager.GetFunctionTy(lambdaParamTys, javaEntityDecl->ty);

    auto deleteCjObjCall = lib.CreateDeleteCJObjectCall(
        WithinFile(CreateRefExpr(*jniEnvPtrParam), curFile), std::move(selfParamRef), std::move(lambda), javaImpl.ty);
    auto wrappedNodesLambda = WrapReturningLambdaExpr(typeManager, Nodes(std::move(deleteCjObjCall)));
    Ptr<Ty> unitTy = typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT).get();
    auto funcName = GetJniDeleteCjObjectFuncName(javaImpl);
    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.push_back(CreateFuncParamList(std::move(params)));

    return GenerateNativeFuncDeclBylambda(javaImpl, wrappedNodesLambda, paramLists, *jniEnvPtrParam, unitTy, funcName);
}

void JavaDesugarManager::DesugarJavaImpl(ClassDecl& jimpl)
{
    auto parentMirror = jimpl.GetSuperClassDecl();
    CJC_NULLPTR_CHECK(parentMirror);
    CJC_ASSERT(IsMirror(*parentMirror));

    for (auto& member : jimpl.GetMemberDecls()) {
        if (member->TestAttr(Attribute::IS_BROKEN) || member->astKind == ASTKind::PRIMARY_CTOR_DECL) {
            continue;
        }

        Walker(member, [this, &jimpl](auto node) {
            auto call = As<ASTKind::CALL_EXPR>(node);
            if (!call) {
                return VisitAction::WALK_CHILDREN;
            }
            auto ma = As<ASTKind::MEMBER_ACCESS>(call->baseFunc.get());
            if (!ma || !ma->baseExpr) {
                return VisitAction::WALK_CHILDREN;
            }
            auto ref = As<ASTKind::REF_EXPR>(ma->baseExpr);
            if (!ref || !ref->isSuper) {
                return VisitAction::WALK_CHILDREN;
            }

            DesugarSuperMethodCall(*call, *StaticAs<ASTKind::CLASS_DECL>(&jimpl));
            return VisitAction::WALK_CHILDREN;
        }).Walk();

        if (auto fd = As<ASTKind::FUNC_DECL>(member.get())) {
            if (!fd->TestAttr(Attribute::CONSTRUCTOR) || IsGeneratedJavaImplConstructor(*fd)) {
                continue;
            }

            DesugarJavaImplConstructor(*fd, *GetGeneratedJavaMirrorConstructor(*parentMirror));
            continue;
        }
    }
}

void JavaDesugarManager::DesugarInJavaImpls(File& file)
{
    for (auto& decl : file.decls) {
        if (auto cdecl = As<ASTKind::CLASS_DECL>(decl.get())) {
            if (cdecl->TestAttr(Attribute::IS_BROKEN) || !IsImpl(*cdecl)) {
                continue;
            }
            DesugarJavaImpl(*cdecl);

            if (JavaSourceCodeGenerator::IsDeclAppropriateForGeneration(*cdecl)) {
                const std::string fileJ = cdecl->identifier.Val() + ".java";
                auto codegen = JavaSourceCodeGenerator(cdecl, mangler, javaCodeGenPath, fileJ,
                    GetCangjieLibName(outputLibPath, cdecl->GetFullPackageName()));
                codegen.Generate();
            }
        }
    }
}

OwnedPtr<Expr> JavaDesugarManager::CreateIsInstanceCall(Ptr<VarDecl> jObjectVar, Ptr<Ty> classTy, Ptr<File> curFile)
{
    auto isInstanceOfDecl = lib.GetIsInstanceOf();

    auto jniEnvCall = lib.CreateGetJniEnvCall(curFile);

    auto javaRefExpr = CreateJavaRefCall(WithinFile(CreateRefExpr(*jObjectVar), curFile));

    auto nameLit = CreateLitConstExpr(
        LitConstKind::STRING, utils.GetJavaTypeSignature(*classTy),
        isInstanceOfDecl->funcBody->paramLists[0]->params[2]->ty);

    return CreateCall(isInstanceOfDecl, curFile, std::move(jniEnvCall), std::move(javaRefExpr), std::move(nameLit));
}

void JavaDesugarManager::DesugarIsExpression(IsExpr& ie)
{
    auto curFile = ie.curFile;
    CJC_NULLPTR_CHECK(curFile);
    static const auto BOOL_TY = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);

    CJC_ASSERT(!ie.desugarExpr);

    auto castTy = ie.isType->ty;
    auto jObjectDecl = utils.GetJObjectDecl();
    CJC_ASSERT(jObjectDecl);

    // match (x)
    std::vector<OwnedPtr<MatchCase>> matchCases;

    // case x : JObject => IsInstance(..)
    auto jObjVarPattern = WithinFile(CreateVarPattern(V_COMPILER, jObjectDecl->ty), curFile);
    jObjVarPattern->varDecl->curFile = curFile;
    auto isInstanceCall = CreateIsInstanceCall(jObjVarPattern->varDecl, castTy, curFile);
    auto jObjectType = CreateType(jObjectDecl->ty);
    auto typePattern = CreateTypePattern(std::move(jObjVarPattern), std::move(jObjectType), *ie.leftExpr);
    matchCases.emplace_back(CreateMatchCase(std::move(typePattern), std::move(isInstanceCall)));

    // case _ => false
    auto falseLit = CreateLitConstExpr(LitConstKind::BOOL, "false", BOOL_TY);
    matchCases.emplace_back(CreateMatchCase(MakeOwned<WildcardPattern>(), std::move(falseLit)));

    ie.desugarExpr = WithinFile(CreateMatchExpr(std::move(ie.leftExpr), std::move(matchCases), BOOL_TY), curFile);
}

OwnedPtr<Expr> JavaDesugarManager::CreateJObjectCast(Ptr<VarDecl> jObjectVar, Ptr<ClassLikeDecl> castDecl,
    Ptr<File> curFile)
{
    auto castTy = castDecl->ty;

    // match (IsInstance(obj.javaref, ...))
    auto isInstanceCall = CreateIsInstanceCall(jObjectVar, castTy, curFile);

    auto javarefExpr = CreateJavaRefCall(WithinFile(CreateRefExpr(*jObjectVar), curFile));

    // cast true => ...
    OwnedPtr<Expr> trueBranch;
    if (castDecl->TestAttr(Attribute::JAVA_MIRROR)) {
        // Some(T(obj.javaref))
        auto ctorCall = CreateMirrorConstructorCall(importManager, std::move(javarefExpr), castTy);
        trueBranch = utils.CreateOptionSomeCall(std::move(ctorCall), castTy);
    } else {
        // getFromRegistry(..)
        CJC_ASSERT(castDecl->TestAttr(Attribute::JAVA_MIRROR_SUBTYPE));
        bool retAsOption = true;
        trueBranch = lib.CreateGetFromRegistryByEntityCall(lib.CreateGetJniEnvCall(curFile), std::move(javarefExpr),
            castTy, retAsOption);
    }

    // case false => None
    OwnedPtr<Expr> falseBranch = utils.CreateOptionNoneRef(castTy);

    return CreateBoolMatch(
        std::move(isInstanceCall), std::move(trueBranch), std::move(falseBranch), utils.GetOptionTy(castTy));
}

void JavaDesugarManager::DesugarAsExpression(AsExpr& ae)
{
    auto curFile = ae.curFile;
    CJC_NULLPTR_CHECK(curFile);
    CJC_ASSERT(!ae.desugarExpr);

    auto castTy = ae.asType->ty;
    auto castDecl = DynamicCast<ClassLikeDecl>(Ty::GetDeclOfTy(castTy));
    auto jObjectDecl = utils.GetJObjectDecl();

    auto castResultTy = utils.GetOptionTy(castTy);

    // match (obj)
    std::vector<OwnedPtr<MatchCase>> typeMatchCases;

    // case obj : JObject => match ...
    auto jObjVarPattern = WithinFile(CreateVarPattern(V_COMPILER, jObjectDecl->ty), curFile);
    jObjVarPattern->varDecl->curFile = curFile;
    auto jObjectType = CreateType(jObjectDecl->ty);
    auto isInstanceMatch = CreateJObjectCast(jObjVarPattern->varDecl, castDecl, curFile);
    auto typePattern = CreateTypePattern(std::move(jObjVarPattern), std::move(jObjectType), *ae.leftExpr);
    typeMatchCases.emplace_back(CreateMatchCase(std::move(typePattern), std::move(isInstanceMatch)));

    // case _ => None
    auto noneRef = utils.CreateOptionNoneRef(castTy);
    typeMatchCases.emplace_back(CreateMatchCase(MakeOwned<WildcardPattern>(), std::move(noneRef)));
    ae.desugarExpr = WithinFile(
        CreateMatchExpr(std::move(ae.leftExpr), std::move(typeMatchCases), castResultTy),
        curFile);
}

namespace {

bool ShouldDesugarTypecheck(Ptr<Type> type, Ptr<Expr> expr)
{
    // When obj is not class or interface or type is not of Java class
    // then will be desugared as regular as
    auto castDecl = DynamicCast<ClassLikeDecl>(Ty::GetDeclOfTy(type->ty));
    auto objDecl = DynamicCast<ClassLikeDecl>(Ty::GetDeclOfTy(expr->ty));
    if (!objDecl || !castDecl ||
        !castDecl->TestAnyAttr(Attribute::JAVA_MIRROR_SUBTYPE, Attribute::JAVA_MIRROR)) {
        return false;
    }

    return true;
}

std::vector<Ptr<TypePattern>> CollectTypePatternsWithJavaClass(Ptr<Pattern> pat)
{
    std::vector<Ptr<TypePattern>> res;

    Walker(pat, [&res](auto node) {
        CJC_ASSERT(node);

        if (auto tpat = As<ASTKind::TYPE_PATTERN>(node.get())) {
            auto decl = Ty::GetDeclOfTy(tpat->type->ty);
            // Saving for all Java classes, except JObject which is JAVA_MIRROR and
            // not JAVA_MIRROR_SUBTYPE
            if (decl && decl->TestAttr(Attribute::JAVA_MIRROR_SUBTYPE)) {
                res.push_back(tpat);
            }
        }

        return VisitAction::WALK_CHILDREN;
    }).Walk();

    return res;
}

std::vector<Ptr<TypePattern>> CollectTypePatternsWithJavaClass(const std::vector<OwnedPtr<Pattern>>& patterns)
{
    std::vector<Ptr<TypePattern>> res;

    for (auto& pat : patterns) {
        auto pats = CollectTypePatternsWithJavaClass(pat.get());
        std::move(pats.begin(), pats.end(), std::back_inserter(res));
    }

    return res;
}

OwnedPtr<VarPattern> CreateTmpVarPattern(Ptr<Ty> ty)
{
    auto var = CreateTmpVarDecl();
    auto varPat = MakeOwned<VarPattern>();
    var->parentPattern = varPat;
    var->ty = ty;
    varPat->ty = ty;
    varPat->varDecl = std::move(var);
    return varPat;
}

} // namespace

void JavaDesugarManager::DesugarMatchCase(MatchCase& matchCase)
{
    auto jObjectDecl = utils.GetJObjectDecl();

    std::vector<Ptr<TypePattern>> typePatterns = CollectTypePatternsWithJavaClass(matchCase.patterns);

    if (typePatterns.empty()) {
        return;
    }

    std::vector<OwnedPtr<Expr>> isInstanceGuards;
    std::vector<std::tuple<Ptr<VarDecl>, Ptr<Ty>>> patternVars;
    for (auto pat : typePatterns) {
        if (DynamicCast<WildcardPattern>(pat->pattern.get())) {
            pat->pattern = CreateTmpVarPattern(pat->type->ty);
            pat->pattern->curFile = matchCase.curFile;
        }

        auto varPat = DynamicCast<VarPattern>(pat->pattern.get());
        // Pattern under type pattern is always either wildcard or var
        CJC_ASSERT(varPat);
        auto originalTy = varPat->ty;

        pat->type = CreateType(jObjectDecl->ty);
        varPat->ty = jObjectDecl->ty;
        varPat->varDecl->ty = jObjectDecl->ty;

        patternVars.emplace_back(varPat->varDecl, originalTy);
        isInstanceGuards.emplace_back(CreateIsInstanceCall(varPat->varDecl, originalTy, matchCase.curFile));
    }

    OwnedPtr<Expr> guard;
    if (!matchCase.patternGuard) {
        guard = std::move(isInstanceGuards.back());
        isInstanceGuards.pop_back();
    } else {
        OwnedPtr<Block> guardVarsBlock = CastAndSubstituteVars(*matchCase.patternGuard, patternVars);
        guard = ASTCloner::Clone(matchCase.patternGuard.get());
        guardVarsBlock->body.emplace_back(std::move(matchCase.patternGuard));
        guardVarsBlock->ty = guard->ty;
        guard->desugarExpr = std::move(guardVarsBlock);
    }

    while (!isInstanceGuards.empty()) {
        guard = CreateBinaryExpr(std::move(isInstanceGuards.back()), std::move(guard), TokenKind::AND);
        isInstanceGuards.pop_back();
    }

    matchCase.patternGuard = std::move(guard);

    auto bodyVarsBlock = CastAndSubstituteVars(*matchCase.exprOrDecls, patternVars);
    bodyVarsBlock->ty = matchCase.exprOrDecls->ty;
    std::move(matchCase.exprOrDecls->body.begin(), matchCase.exprOrDecls->body.end(),
        std::back_inserter(bodyVarsBlock->body));
    matchCase.exprOrDecls = std::move(bodyVarsBlock);
}

OwnedPtr<Block> JavaDesugarManager::CastAndSubstituteVars(
    Expr& expr, const std::vector<std::tuple<Ptr<VarDecl>, Ptr<Ty>>>& patternVars)
{
    auto curFile = expr.curFile;
    auto varsBlock = WithinFile(MakeOwned<Block>(), curFile);
    std::unordered_map<Ptr<Decl>, Ptr<Decl>> varsMapping;
    for (auto [varDecl, castTy] : patternVars) {
        auto castDecl = DynamicCast<ClassLikeDecl>(Ty::GetDeclOfTy(castTy));
        CJC_ASSERT(castDecl);

        auto javarefExpr = CreateJavaRefCall(WithinFile(CreateRefExpr(*varDecl), curFile));
        OwnedPtr<Expr> initializer = lib.UnwrapJavaEntity(std::move(javarefExpr), castDecl->ty, *castDecl);
        auto castedVar = WithinFile(CreateTmpVarDecl(CreateType(castDecl->ty), std::move(initializer)), curFile);
        varsMapping[varDecl] = castedVar;
        varsBlock->body.emplace_back(std::move(castedVar));
    }

    Walker(&expr, [&varsMapping](Ptr<Node> node) {
        CJC_ASSERT(node);

        if (auto refExpr = DynamicCast<RefExpr>(node.get())) {
            auto target = refExpr->ref.target;
            if (auto castedVarIt = varsMapping.find(target); castedVarIt != varsMapping.end()) {
                refExpr->ref.identifier = castedVarIt->second->identifier;
                refExpr->ref.target = castedVarIt->second;
            }
        }

        return VisitAction::WALK_CHILDREN;
    }).Walk();

    return varsBlock;
}


void JavaDesugarManager::DesugarLetPattern(LetPatternDestructor& letPat)
{
    std::vector<Ptr<TypePattern>> typePatterns = CollectTypePatternsWithJavaClass(letPat.patterns);

    if (typePatterns.empty()) {
        return;
    }

    for (auto typePat : typePatterns) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_java_interop_not_supported,
                              *typePat, "let type patterns with JavaImpl or JavaMirror types");
    }
}

void JavaDesugarManager::DesugarTypechecks(File& file)
{
    Walker(&file, [this](auto node) {
        CJC_ASSERT(node);

        if (auto ie = As<ASTKind::IS_EXPR>(node.get())) {
            if (!ShouldDesugarTypecheck(ie->isType, ie->leftExpr)) {
                return VisitAction::WALK_CHILDREN;
            }

            DesugarIsExpression(*ie);
        } else if (auto ae = As<ASTKind::AS_EXPR>(node.get())) {
            if (!ShouldDesugarTypecheck(ae->asType, ae->leftExpr)) {
                return VisitAction::WALK_CHILDREN;
            }

            DesugarAsExpression(*ae);
        } else if (auto mc = As<ASTKind::MATCH_CASE>(node.get())) {
            DesugarMatchCase(*mc);
        } else if (auto lpd = As<ASTKind::LET_PATTERN_DESTRUCTOR>(node.get())) {
            DesugarLetPattern(*lpd);
        }

        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

void JavaDesugarManager::DesugarSuperMethodCall(CallExpr& call, ClassDecl& impl)
{
    CJC_ASSERT(call.baseFunc && call.baseFunc->astKind == ASTKind::MEMBER_ACCESS);
    auto& ma = *StaticAs<ASTKind::MEMBER_ACCESS>(call.baseFunc.get());
    CJC_ASSERT(ma.baseExpr && ma.baseExpr->astKind == ASTKind::REF_EXPR);
    auto& ref = *StaticAs<ASTKind::REF_EXPR>(ma.baseExpr.get().get());
    CJC_ASSERT(ref.isSuper);
    CJC_ASSERT(call.resolvedFunction && call.resolvedFunction->outerDecl);
    auto& outerDecl = *call.resolvedFunction->outerDecl;
    CJC_ASSERT(IsMirror(outerDecl));
    auto parent = As<ASTKind::CLASS_DECL>(&outerDecl);
    auto curFile = call.curFile;

    std::vector<OwnedPtr<Expr>> args;

    for (auto& arg : call.args) {
        auto desugaredArg = lib.WrapJavaEntity(ASTCloner::Clone(arg->expr.get()));
        args.emplace_back(std::move(desugaredArg));
    }

    auto desugaredCall = lib.CreateCallMethodCall(
        lib.CreateGetJniEnvCall(curFile), CreateJavaRefCall(impl, curFile),
        MemberJNISignature(utils, *call.resolvedFunction, parent), std::move(args), *curFile, false);
    desugaredCall->desugarArgs = std::nullopt;

    call.desugarExpr = call.ty->IsUnit()
        ? std::move(desugaredCall)
        : lib.UnwrapJavaEntity(std::move(desugaredCall), call.ty, impl);
}

void JavaDesugarManager::GenerateInJavaImpl(AST::ClassDecl* classDecl)
{
    CJC_ASSERT(classDecl && IsImpl(*classDecl));
    auto& jimpl = *classDecl;
    auto parentMirror = jimpl.GetSuperClassDecl();
    CJC_NULLPTR_CHECK(parentMirror);

    std::vector<OwnedPtr<FuncDecl>> generatedCtors;

    for (auto& member : jimpl.GetMemberDecls()) {
        if (member->TestAnyAttr(Attribute::IS_BROKEN, Attribute::PRIVATE)) {
            // private members are not callable from java
            continue;
        }
        if (auto fd = As<ASTKind::FUNC_DECL>(member.get())) {
            if (fd->TestAttr(Attribute::CONSTRUCTOR)) {
                generatedCtors.push_back(GenerateJavaImplConstructor(*fd, *parentMirror));
                continue;
            } else {
                generatedDecls.push_back(GenerateNativeMethod(*fd, jimpl));
            }
        }
    }

    if (!generatedCtors.empty()) {
        auto& javaWeakRefField = *GetJavaRefField(*parentMirror);
        generatedDecls.push_back(GenerateNativeDeleteCjObjectFunc(jimpl, javaWeakRefField));

        for (auto& generatedCtor : generatedCtors) {
            generatedDecls.push_back(GenerateNativeInitCjObjectFunc(*generatedCtor, true));
            if (auto cd = As<ASTKind::CLASS_DECL>(&jimpl)) {
                cd->body->decls.push_back(std::move(generatedCtor));
            }
        }
    }
}

void JavaDesugarManager::GenerateInJavaImpls(File& file)
{
    for (auto& decl : file.decls) {
        auto astDecl = As<ASTKind::DECL>(decl.get());
        if (astDecl && astDecl->TestAttr(Attribute::IS_BROKEN)) {
            continue;
        }
        auto classDecl = As<ASTKind::CLASS_DECL>(decl.get());
        if (classDecl && IsImpl(*classDecl)) {
            GenerateInJavaImpl(classDecl);
        }
    }
}

} // namespace Cangjie::Interop::Java
