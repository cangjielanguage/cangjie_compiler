// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "DesugarJavaImplSuperConstructorCall.h"
#include "NativeFFI/Utils.h"
#include "Utils.h"
#include "InteropLibBridge.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/SafePointer.h"
#include <cstddef>
#include <iterator>
#include <utility>

namespace Cangjie::Native::FFI::Java {
using namespace Cangjie::Interop::Java;

namespace {

Ptr<CallExpr> TryGetSuperCall(const FuncDecl& ctor)
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
    CJC_ASSERT_WITH_MSG(!ctor.funcBody->body->body.empty(), "body must have at least one node");
    auto firstNode = ctor.funcBody->body->body[0].get();
    if (auto callExpr = As<ASTKind::CALL_EXPR>(firstNode);
        callExpr && callExpr->callKind == CallKind::CALL_SUPER_FUNCTION) {
        return callExpr;
    }
    return nullptr;
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

} // namespace

std::string DesugarJavaImplSuperConstructorCall::GetJniSuperArgFuncName(
    const ClassLikeDecl& outer, const std::string& id) const
{
    std::string fqname = GetJavaFQName(outer);
    return jni.GetJavaNativeFunctionName(fqname, "super" + id);
}

/**
 * Unwrap a refExpr of native func param into Cangjie type.
 * 1. Primitive Type
 * 2. JavaMirror/JavaImpl: jobject -> JavaEntity -> @JavaMirror class M / @JavaImpl class A
 *
 * For example:
 *     pa: jobject -> A(Java_CFFI_JavaEntityJobject(pa)): A
 */
OwnedPtr<Expr> DesugarJavaImplSuperConstructorCall::UnwrapRefExpr(OwnedPtr<RefExpr> ref, Ptr<Ty> targetTy,
    const ClassLikeDecl& decl) const
{
    // Do not wrap for primitive type.
    if (targetTy->IsPrimitive()) {
        return std::move(ref);
    }
    // Mirror or Impl or Option or other?
    // jobject -> JavaEntity
    auto entity = ilib.CreateJavaEntityJobjectCall(std::move(ref));
    // JavaEntity -> Cangjie Type
    return ilib.UnwrapJavaEntity(std::move(entity), targetTy, decl);
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
OwnedPtr<Expr> DesugarJavaImplSuperConstructorCall::WrapExprWithExceptionHandling(
    std::vector<OwnedPtr<Node>>&& nodes, OwnedPtr<Expr> expr, FuncParam& env, const ClassLikeDecl& decl) const
{
    auto curFile = expr->curFile;
    auto retTy = expr->GetTy();
    auto res = CreateTmpVarDecl(nullptr, std::move(expr));
    OwnedPtr<Expr> ret = WithinFile(CreateRefExpr(*res), curFile);
    if (retTy->IsPrimitive()) {
        ret = WithinFile(CreateRefExpr(*res), curFile);
    } else {
        // Cangjie Type -> JavaEntity -> jobject
        ret = ilib.UnwrapJavaEntity(
            ilib.WrapJavaEntity(WithinFile(CreateRefExpr(*res), curFile)), retTy.Ty(), decl, true);
    }
    nodes.push_back(std::move(res));
    nodes.push_back(std::move(ret));
    return ilib.WrapExceptionHandling(
        WithinFile(CreateRefExpr(env), curFile), WrapReturningLambdaExpr(typeManager, std::move(nodes)));
}

OwnedPtr<FuncDecl> DesugarJavaImplSuperConstructorCall::CreateMemberFunc4Argument(const FuncArg& arg,
    const std::vector<OwnedPtr<FuncParam>>& params, ClassLikeDecl& refWrapper, size_t ctorId, size_t argId) const
{
    std::vector<Ptr<FuncParam>> usingParams;
    auto [usingThis, paramIds] = CollectParams(arg.expr.get(), params, usingParams);
    if (usingThis) {
        // Report error for using this in super call.
        diag.DiagnoseRefactor(DiagKindRefactor::sema_java_interop_not_supported, arg, "using this in super call");
        return nullptr;
    }
    std::vector<Ptr<RefExpr>> staticRefs;

    // The encoding rules of super call id: C{ctorId}A{argId}{usingParamIds: P{paramId}P{paramId}...}
    //     {ctorId} is the index of the constructor containing the super call in the class body.
    //     {argId} is the index of the func argument in super call.
    //     {usingParamIds} is the indexes of the func argument using parameters of the constructor.
    std::string funcName = "C" + std::to_string(ctorId) + "A" + std::to_string(argId) + paramIds;
    std::vector<OwnedPtr<FuncParam>> funcParams;
    // Body
    std::map<Ptr<Decl>, Ptr<FuncParam>> source2cloned;
    for (size_t pi = 0; pi < usingParams.size(); pi++) {
        // Create func param for native func & pass it to target function call
        auto funcParam = CreateFuncParam(usingParams[pi]->identifier.Val(), nullptr, nullptr, usingParams[pi]->GetTy());
        auto ref = WithinFile(CreateRefExpr(*funcParam), refWrapper.curFile);
        source2cloned[usingParams[pi]] = funcParam.get();
        funcParams.push_back(std::move(funcParam));
    }

    // For `foo(x, y + z)` the parameters x, y and implicit static field z need be passed through.
    // -----------------
    // foo(x, y + z)
    auto replaceTarget = [&mp = std::as_const(source2cloned)](Node& source, Node& target) {
        if (auto srcRef = As<ASTKind::REF_EXPR>(&source)) {
            // replace ref by parameter reference
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
            // update call member access by call parameter. -- Does it really update anything?
            if (it != mp.end()) {
                auto targetCall = As<ASTKind::CALL_EXPR>(&target);
                targetCall->resolvedFunction = nullptr;
                targetCall->callKind = CallKind::CALL_FUNCTION_PTR;
            }
        }
    };
    auto clonedExpr = ASTCloner::Clone(arg.expr.get(), replaceTarget);
    CJC_NULLPTR_CHECK(refWrapper.curFile);

    std::vector<ModalTy> funcTyParams;
    for (auto& param : funcParams) {
        funcTyParams.push_back(param->GetTy());
    }

    OwnedPtr<FuncDecl> memberFn = CreateFuncDecl(funcName, MakeOwned<FuncBody>());
    auto& funcBody = *memberFn->funcBody;
    funcBody.SetTy(arg.expr->GetTy());
    memberFn->SetTy(ModalTy{typeManager.GetFunctionTy(funcTyParams, arg.expr->GetTy())});
    funcBody.funcDecl = memberFn;
    funcBody.body = MakeOwned<Block>();
    auto& body = *funcBody.body;
    body.SetTy(arg.expr->GetTy());
    memberFn->outerDecl = &refWrapper;
    funcBody.parentClassLike = &refWrapper;
    memberFn->fullPackageName = refWrapper.fullPackageName;
    memberFn->moduleName = refWrapper.moduleName;
    memberFn->curFile = refWrapper.curFile;
    memberFn->EnableAttr(Attribute::INTERNAL, Attribute::IN_CLASSLIKE, Attribute::INITIALIZED,
        Attribute::STATIC, Attribute::IS_CHECK_VISITED);

    auto& paramList = *funcBody.paramLists.emplace_back(MakeOwned<FuncParamList>());
    std::move(funcParams.begin(), funcParams.end(), std::back_inserter(paramList.params));
    body.body.emplace_back(std::move(clonedExpr));

    return memberFn;
}

OwnedPtr<FuncDecl> DesugarJavaImplSuperConstructorCall::CreateNativeFunc4Argument(FuncDecl& memberFunc,
    ClassLikeDecl& refWrapper) const
{
    // The encoding rules of super call id: C{ctorId}A{argId}{usingParamIds: P{paramId}P{paramId}...}
    //     {ctorId} is the index of the constructor containing the super call in the class body.
    //     {argId} is the index of the func argument in super call.
    //     {usingParamIds} is the indexes of the func argument using parameters of the constructor.
    std::string funcName = GetJniSuperArgFuncName(refWrapper, memberFunc.identifier);
    std::vector<OwnedPtr<FuncParam>> funcParams;
    auto& jniEnvParam = *funcParams.emplace_back(jni.CreateJniEnvParam());
    funcParams.push_back(jni.CreateJniJobjectOrJclassParam());
    // Body
    auto argTy = StaticCast<FuncTy&>(*memberFunc.GetTy()).retTy;

    auto proxyCall = WithinFile(CreateCallExpr(
        CreateMemberAccess(WithinFile(CreateRefExpr(*memberFunc.outerDecl), memberFunc.curFile), memberFunc),
        {}, &memberFunc, argTy, CallKind::CALL_DECLARED_FUNCTION), memberFunc.curFile);

    for (auto& memberParam : memberFunc.funcBody->paramLists[0]->params) {
        auto& jniParamTy = jni.ConvertCangjieToJniTy(*memberParam->GetTy());
        auto funcParam =
            CreateFuncParam(memberParam->identifier.Val(), nullptr, nullptr, ModalTy{Ptr<Ty>(&jniParamTy)});
        auto ref = WithinFile(CreateRefExpr(*funcParam), memberFunc.curFile);
        proxyCall->args.push_back(CreateFuncArg(UnwrapRefExpr(std::move(ref), memberParam->GetTy().Ty(), refWrapper)));
        funcParams.push_back(std::move(funcParam));
    }
    auto wrapper = WrapExprWithExceptionHandling({}, std::move(proxyCall), jniEnvParam, refWrapper);
    CJC_NULLPTR_CHECK(refWrapper.curFile);
    OwnedPtr<FuncDecl> nativeFn = utils.CreateNativeFunc(funcName,
        std::move(funcParams),
        &jni.ConvertCangjieToJniTy(*argTy),
        Nodes(std::move(wrapper)),
        *refWrapper.curFile,
        refWrapper.moduleName,
        refWrapper.fullPackageName);

    return nativeFn;
}

void DesugarJavaImplSuperConstructorCall::DesugarSuperConstructorCall(AfterTypeCheckContext& ctx,
    const FuncDecl& ctor) const
{
    CJC_ASSERT_WITH_MSG(!ctor.funcBody->paramLists.empty(), "paramLists cannot be empty");
    auto& paramList = *ctor.funcBody->paramLists[0];
    auto decl = StaticAs<ASTKind::CLASS_LIKE_DECL>(ctor.outerDecl);
    // super call
    auto superCall = TryGetSuperCall(ctor);
    if (!superCall) {
        return;
    }
    size_t ctorId = GetCtorId(ctor); // TODO: replace with non-positional mangling
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
            ctx.CacheJavaImplSuperCtorArgNativeFunc(ctor, nullptr);
            continue;
        }

        if (auto memberFunc = CreateMemberFunc4Argument(*args[index], paramList.params, *decl, ctorId, index)) {
            if (auto argFn = CreateNativeFunc4Argument(*memberFunc, *decl)) {
                // record desugared function
                ctx.CacheJavaImplSuperCtorArgNativeFunc(ctor, argFn);
                ctx.AddGeneratedDecl(std::move(argFn));
                decl->GetMemberDecls().push_back(std::move(memberFunc));
            } else {
                decl->EnableAttr(Attribute::IS_BROKEN, Attribute::HAS_BROKEN);
            }
        } else {
            decl->EnableAttr(Attribute::IS_BROKEN, Attribute::HAS_BROKEN);
        }
    }

    ctx.CacheJavaImplSuperCtorCall(ctor, ASTCloner::Clone(superCall));
    // Remove original user-defined constructor.
    // At `GenerateInJavaImplReferenceWrapper` stage, other super-call with java reference parameter is inserted.
    ctor.funcBody->body->body.erase(ctor.funcBody->body->body.begin());
}

DesugarJavaImplSuperConstructorCall::DesugarJavaImplSuperConstructorCall(
    TypeManager& typeManager, InteropLibBridge& ilib, JniBridge& jni, DiagnosticEngine& diag,
    Interop::Java::Utils& utils) : typeManager(typeManager), ilib(ilib), jni(jni), diag(diag), utils(utils)
{
}

void DesugarJavaImplSuperConstructorCall::Process(AfterTypeCheckContext& ctx)
{
    for (auto& refWrapper : ctx.GetJavaImplReferenceWrappers()) {
        for (auto& ctor : ctx.GetJavaImplUserDefinedConstructors(*refWrapper)) {
            DesugarSuperConstructorCall(ctx, *ctor);
        }
    }
}

} // namespace Cangjie::Native::FFI::Java
