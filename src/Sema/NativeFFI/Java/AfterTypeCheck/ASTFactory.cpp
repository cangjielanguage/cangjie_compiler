// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "ASTFactory.h"
#include "NativeFFI/Java/AfterTypeCheck/AfterTypeCheckContext.h"
#include "NativeFFI/Java/AfterTypeCheck/InteropLibBridge.h"
#include "NativeFFI/Java/AfterTypeCheck/JniBridge.h"
#include "NativeFFI/Java/AfterTypeCheck/Utils.h"
#include "NativeFFI/Java/CachingApi/JFieldIdCache.h"
#include "NativeFFI/Java/JavaMemberSignature.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie::Native::FFI::Java {
using namespace AST;

namespace {

// a: VArray<T, $X> -> inout(a) of type T
OwnedPtr<FuncArg> CreateInoutVArrayArg(OwnedPtr<Expr> expr, TypeManager& typeManager)
{
    CJC_ASSERT(expr->TyKind() == TypeKind::TYPE_VARRAY);

    auto arg = CreateFuncArg(std::move(expr));
    auto* varrayTy = StaticCast<VArrayTy>(arg->DataTy());
    CJC_ASSERT_WITH_MSG(!varrayTy->typeArgs.empty(), "VArray must be generic");
    arg->withInout = true;
    arg->SetTy({typeManager.GetPointerTy(varrayTy->TyArg(0))});
    return arg;
}
} // namespace

ASTFactory::ASTFactory(
    TypeManager& typeManager, InteropLibBridge& ilib, JniBridge& jni,
    JClassCache& jclassCache, JMethodIdCache& jmethodIdCache, JFieldIdCache& jfieldIdCache)
    : typeManager(typeManager), ilib(ilib), jni(jni),
    jclassCache(jclassCache), jmethodIdCache(jmethodIdCache), jfieldIdCache(jfieldIdCache)
{
}

OwnedPtr<Block> ASTFactory::CreateNewJavaObjectCall(AfterTypeCheckContext& ctx, Ptr<Expr> jniEnvPtr,
    JavaMemberSignature constructor, std::vector<OwnedPtr<Expr>> args) const
{
    CJC_NULLPTR_CHECK(jniEnvPtr->curFile);
    auto curFile = jniEnvPtr->curFile;

    auto block = CreateBlock({}, ilib.GetJavaEntityTy());
    block->curFile = curFile;

    auto jclass = jclassCache.CreateJClassAccess(ctx, constructor.GetClassSignature(), jniEnvPtr);
    auto jmethod = jmethodIdCache.CreateJMethodIdAccess(ctx, constructor, ASTCloner::Clone(jclass.get()), jniEnvPtr);

    // 1. let jniCallRes: jobject | local java reference
    auto& callResVar = StaticCast<VarDecl&>(*block->body.emplace_back(CreateTmpVarDecl(nullptr, nullptr)));
    callResVar.curFile = curFile;

    if (args.empty()) {
        // call without arguments, no need to pass empty value.
        // 2. jniCallRes = (*jniEnv)->NewObject(jclass, jmethod)
        callResVar.initializer = jni.interface.CreateNewObjectCall(jniEnvPtr,
            ASTCloner::Clone(jclass.get()), ASTCloner::Clone(jmethod.get()));
    } else {
        // call with arguments - arguments are constructed as JValue-s within VArray and passed as in-out value into JNI
        // 0. var jniArgs: VArray<jvalue, $x> = [args...]
        auto& jniArgs = StaticCast<VarDecl&>(**block->body.emplace(block->body.begin(),
            CreateTmpVArrayVarDecl(ConvertToJValues(std::move(args)), *curFile)));

        // 2. jniCallRes = (*jniEnv)->NewObjectA(jclass, jmethod, inout jniArgs)
        callResVar.initializer = jni.interface.CreateNewObjectACall(jniEnvPtr,
            CreateFuncArg(ASTCloner::Clone(jclass.get())),
            CreateFuncArg(ASTCloner::Clone(jmethod.get())),
            CreateInoutVArrayArg(WithinFile(CreateRefExpr(jniArgs), curFile), typeManager));
    }
    callResVar.SetTy(callResVar.initializer->GetTy());

    // 4. handlePendingException()
    block->body.push_back(ilib.CreateJNIHandlePendingExceptionCall(jniEnvPtr));
    // 5. let $gref = Java_CFFI_JavaEntity(SwapLocalWithGlobalReference(jniCallRes)) | global java reference
    auto& gref = StaticCast<VarDecl&>(*block->body.emplace_back(CreateTmpVarDecl(nullptr, nullptr)));
    gref.curFile = curFile;
    gref.initializer = ilib.CreateJavaEntityJobjectCall(ilib.CreateSwapLocalWithGlobalRefCall(
        ASTCloner::Clone(jniEnvPtr),
        WithinFile(CreateRefExpr(callResVar), curFile)));
    gref.SetTy(gref.initializer->GetTy());

    // 6. return $gref
    block->body.push_back(WithinFile(CreateRefExpr(gref), curFile));
    return block;
}

OwnedPtr<Block> ASTFactory::CreateNewJavaObjectCall(
    AfterTypeCheckContext& ctx, File& curFile, JavaMemberSignature constructor, std::vector<OwnedPtr<Expr>> args) const
{
    return WithLocalJniEnvPtr(curFile, [&](Ptr<Expr> jniEnvPtr) {
        return CreateNewJavaObjectCall(ctx,
            ASTCloner::Clone(jniEnvPtr), constructor, std::move(args));
    });
}

OwnedPtr<Block> ASTFactory::CreateNonvirtualJavaMethodCall(AfterTypeCheckContext& ctx, Ptr<Expr> jniEnvPtr,
    Ptr<Expr> jobjectInstance, JavaMemberSignature method, Ty& retTy, std::vector<OwnedPtr<Expr>> args) const
{
    CJC_NULLPTR_CHECK(jobjectInstance);
    CJC_ASSERT(!method.IsStatic());
    return CreateJavaMethodCall(ctx, jniEnvPtr, jobjectInstance, method, retTy, std::move(args),
        /* isVirtual = */ false);
}

OwnedPtr<Block> ASTFactory::CreateNonvirtualJavaMethodCall(AfterTypeCheckContext& ctx, File& curFile,
    Ptr<Expr> jobjectInstance, JavaMemberSignature method, Ty& retTy, std::vector<OwnedPtr<Expr>> args) const
{
    CJC_NULLPTR_CHECK(jobjectInstance);
    CJC_ASSERT(!method.IsStatic());
    return CreateJavaMethodCall(ctx, curFile, jobjectInstance, method, retTy, std::move(args),
        /* isVirtual = */ false);
}

OwnedPtr<Block> ASTFactory::CreateStaticJavaMethodCall(AfterTypeCheckContext& ctx, Ptr<Expr> jniEnvPtr,
    JavaMemberSignature method, Ty& retTy, std::vector<OwnedPtr<Expr>> args) const
{
    CJC_ASSERT(method.IsStatic());
    return CreateJavaMethodCall(ctx, jniEnvPtr, nullptr, method, retTy, std::move(args),
        /* isVirtual = */ false);
}

OwnedPtr<Block> ASTFactory::CreateStaticJavaMethodCall(AfterTypeCheckContext& ctx, File& curFile,
    JavaMemberSignature method, Ty& retTy, std::vector<OwnedPtr<Expr>> args) const
{
    CJC_ASSERT(method.IsStatic());
    return CreateJavaMethodCall(ctx, curFile, nullptr, method, retTy, std::move(args),
        /* isVirtual = */ false);
}

OwnedPtr<Block> ASTFactory::CreateVirtualJavaMethodCall(AfterTypeCheckContext& ctx, Ptr<Expr> jniEnvPtr,
    Ptr<Expr> jobjectInstance, JavaMemberSignature method, Ty& retTy, std::vector<OwnedPtr<Expr>> args) const
{
    CJC_NULLPTR_CHECK(jobjectInstance);
    CJC_ASSERT(!method.IsStatic());
    return CreateJavaMethodCall(ctx, jniEnvPtr, jobjectInstance, method, retTy, std::move(args),
        /* isVirtual = */ true);
}

OwnedPtr<Block> ASTFactory::CreateVirtualJavaMethodCall(AfterTypeCheckContext& ctx, File& curFile,
    Ptr<Expr> jobjectInstance, JavaMemberSignature method, Ty& retTy, std::vector<OwnedPtr<Expr>> args) const
{
    CJC_NULLPTR_CHECK(jobjectInstance);
    CJC_ASSERT(!method.IsStatic());
    return CreateJavaMethodCall(ctx, curFile, jobjectInstance, method, retTy, std::move(args),
        /* isVirtual = */ true);
}

OwnedPtr<Block> ASTFactory::CreateJavaMethodCall(AfterTypeCheckContext& ctx, Ptr<Expr> jniEnvPtr,
    Ptr<Expr> jobjectInstance, JavaMemberSignature method, Ty& retTy, std::vector<OwnedPtr<Expr>> args,
    bool isVirtual) const
{
    auto curFile = jniEnvPtr->curFile;
    CJC_NULLPTR_CHECK(curFile);
    CJC_ASSERT(method.IsMethod());
    CJC_ASSERT(method.IsStatic() || jobjectInstance); // instance cannot be null if method is non-static.

    auto block = CreateBlock({}, {&retTy});
    block->curFile = curFile;

    static auto convertJniValueToJavaCompatible = [this](OwnedPtr<Expr> jniValue,
        Ty& expectedTy, Ptr<Expr> jniEnvPtr, Block& block) {
            auto ret = std::move(jniValue);
            if (IsMirror(expectedTy) || IsImpl(expectedTy) || expectedTy.IsCoreOptionType()) {
                // local java reference -> global java reference
                CJC_ASSERT(ret->GetTy()->IsPointer());
                auto& gref = StaticCast<VarDecl&>(*block.body.emplace_back(CreateTmpVarDecl(nullptr, nullptr)));
                gref.curFile = ret->curFile;
                gref.initializer = ilib.CreateSwapLocalWithGlobalRefCall(
                    ASTCloner::Clone(jniEnvPtr),
                    std::move(ret));
                gref.SetTy(gref.initializer->GetTy());
                ret = WithinFile(CreateRefExpr(gref), gref.curFile);
            }
            if (!expectedTy.IsPrimitive()) {
                ret = ilib.CreateJavaEntityJobjectCall(std::move(ret));
            }
            return ilib.ConvertJavaResultToCJ(std::move(ret), &expectedTy);
    };

    auto jclass = jclassCache.CreateJClassAccess(ctx, method.GetClassSignature(), jniEnvPtr);
    auto jmethod = jmethodIdCache.CreateJMethodIdAccess(ctx, method, ASTCloner::Clone(jclass.get()), jniEnvPtr);

    // 1. let callRes: retTy
    auto& callResVar = StaticCast<VarDecl&>(*block->body.emplace_back(CreateTmpVarDecl(nullptr, nullptr)));
    callResVar.curFile = curFile;
    if (args.empty()) {
        // 2. jniCallRes = Java_CFFI_JavaEntity((*jniEnv)->CallMethod(jclass/jobject, jmethod))
        auto jniMethodCall = method.IsStatic()
        ? jni.CreateStaticJavaMethodCall(retTy, jniEnvPtr, jclass, jmethod)
        : isVirtual
            ? jni.CreateVirtualInstanceJavaMethodCall(retTy, jniEnvPtr, jobjectInstance, jmethod)
            : jni.CreateNonvirtualJavaMethodCall(retTy, jniEnvPtr, jobjectInstance, jclass, jmethod);

        callResVar.initializer = std::move(jniMethodCall);
    } else {
        auto createInoutVArrayArg = [this, &curFile](VarDecl& varrayArg) {
            return CreateInoutVArrayArg(WithinFile(CreateRefExpr(varrayArg), curFile), typeManager);
        };
        // 0. var jniArgs: VArray<jvalue, $x> = [args...]
        auto& jniArgs = StaticCast<VarDecl&>(**block->body.emplace(block->body.begin(),
            CreateTmpVArrayVarDecl(ConvertToJValues(std::move(args)), *curFile)));

        // 2. jniCallRes = (*jniEnv)->CallMethod(jclass/jobject, jmethod, inout jniArgs)
        auto jniMethodCall = method.IsStatic()
        ? jni.CreateStaticJavaMethodCall(retTy, jniEnvPtr, jclass, jmethod, createInoutVArrayArg(jniArgs))
        : isVirtual
            ? jni.CreateVirtualInstanceJavaMethodCall(retTy, jniEnvPtr, jobjectInstance, jmethod,
                createInoutVArrayArg(jniArgs))
            : jni.CreateNonvirtualJavaMethodCall(retTy, jniEnvPtr, jobjectInstance, jclass, jmethod,
                createInoutVArrayArg(jniArgs));

        callResVar.initializer = std::move(jniMethodCall);
    }
    callResVar.SetTy(callResVar.initializer->GetTy());
    // 3. handlePendingException()
    block->body.push_back(ilib.CreateJNIHandlePendingExceptionCall(jniEnvPtr));
    // 4. return unwrapped jniCallRes
    block->body.push_back(convertJniValueToJavaCompatible(
        WithinFile(CreateRefExpr(callResVar), curFile),
        retTy, jniEnvPtr, *block));
    return block;
}

OwnedPtr<Block> ASTFactory::CreateJavaMethodCall(AfterTypeCheckContext& ctx, File& curFile, Ptr<Expr> jobjectInstance,
    JavaMemberSignature method, Ty& retTy, std::vector<OwnedPtr<Expr>> args, bool isVirtual) const
{
    return WithLocalJniEnvPtr(curFile, [&](Ptr<Expr> jniEnvPtr) {
        return CreateJavaMethodCall(ctx,
            ASTCloner::Clone(jniEnvPtr), jobjectInstance, method, retTy, std::move(args), isVirtual);
    });
}

OwnedPtr<AST::Block> ASTFactory::CreateInstanceJavaFieldGetCall(AfterTypeCheckContext& ctx, AST::File& curFile,
    Ptr<AST::Expr> jobjectInstance, JavaMemberSignature field, AST::Ty& ty) const
{
    return CreateJavaFieldGetCall(ctx, curFile, jobjectInstance, field, ty);
}

OwnedPtr<AST::Block> ASTFactory::CreateStaticJavaFieldGetCall(
    AfterTypeCheckContext& ctx, AST::File& curFile, JavaMemberSignature field, AST::Ty& ty) const
{
    return CreateJavaFieldGetCall(ctx, curFile, nullptr, field, ty);
}

OwnedPtr<AST::Block> ASTFactory::CreateInstanceJavaFieldSetCall(AfterTypeCheckContext& ctx, AST::File& curFile,
    Ptr<AST::Expr> jobjectInstance, JavaMemberSignature field, AST::Ty& ty, OwnedPtr<AST::Expr> value) const
{
    return CreateJavaFieldSetCall(ctx, curFile, jobjectInstance, field, ty, std::move(value));
}

OwnedPtr<AST::Block> ASTFactory::CreateStaticJavaFieldSetCall(AfterTypeCheckContext& ctx, AST::File& curFile,
    JavaMemberSignature field, AST::Ty& ty, OwnedPtr<AST::Expr> value) const
{
    return CreateJavaFieldSetCall(ctx, curFile, nullptr, field, ty, std::move(value));
}

OwnedPtr<AST::Block> ASTFactory::CreateJavaFieldGetCall(AfterTypeCheckContext& ctx, Ptr<AST::Expr> jniEnvPtr,
    Ptr<Expr> jobjectInstance, JavaMemberSignature field, AST::Ty& ty) const
{
    auto curFile = jniEnvPtr->curFile;
    CJC_ASSERT(field.IsField());
    CJC_ASSERT(field.IsStatic() || jobjectInstance); // instance cannot be null if field is non-static.
    auto block = CreateBlock({}, {&ty});
    block->curFile = curFile;

    static auto convertJniValueToJavaCompatible = [this](OwnedPtr<Expr> jniValue,
        Ty& expectedTy, Ptr<Expr> jniEnvPtr, Block& block) {
        auto ret = std::move(jniValue);
        if (IsMirror(expectedTy) || IsImpl(expectedTy) || expectedTy.IsCoreOptionType()) {
            // local java reference -> global java reference
            CJC_ASSERT(ret->GetTy()->IsPointer());
            auto& gref = StaticCast<VarDecl&>(*block.body.emplace_back(CreateTmpVarDecl(nullptr, nullptr)));
            gref.curFile = ret->curFile;
            gref.initializer = ilib.CreateSwapLocalWithGlobalRefCall(
                ASTCloner::Clone(jniEnvPtr),
                std::move(ret));
            gref.SetTy(gref.initializer->GetTy());
            ret = WithinFile(CreateRefExpr(gref), gref.curFile);
        }
        if (!expectedTy.IsPrimitive()) {
            ret = ilib.CreateJavaEntityJobjectCall(std::move(ret));
        }
        return ilib.ConvertJavaResultToCJ(std::move(ret), &expectedTy);
    };

    auto jclass = jclassCache.CreateJClassAccess(ctx, field.GetClassSignature(), jniEnvPtr);
    auto jfield = jfieldIdCache.CreateJFieldIdAccess(ctx, field, ASTCloner::Clone(jclass.get()), jniEnvPtr);

    // 1. let callRes: retTy
    auto& callResVar = StaticCast<VarDecl&>(*block->body.emplace_back(CreateTmpVarDecl(nullptr, nullptr)));
    callResVar.curFile = curFile;
    auto jniFieldGetCall = field.IsStatic()
        ? jni.CreateGetStaticJavaFieldCall(ty, jniEnvPtr, jclass, jfield)
        : jni.CreateGetInstanceJavaFieldCall(ty, jniEnvPtr, jobjectInstance, jfield);

    callResVar.initializer = std::move(jniFieldGetCall);
    callResVar.SetTy(callResVar.initializer->GetTy());

    // 3. handlePendingException()
    block->body.push_back(ilib.CreateJNIHandlePendingExceptionCall(jniEnvPtr));
    // 4. return jniCallRes
    block->body.push_back(convertJniValueToJavaCompatible(
        WithinFile(CreateRefExpr(callResVar), curFile),
        ty, jniEnvPtr, *block));
    return block;
}

OwnedPtr<AST::Block> ASTFactory::CreateJavaFieldGetCall(AfterTypeCheckContext& ctx, AST::File& curFile,
    Ptr<Expr> jobjectInstance, JavaMemberSignature field, AST::Ty& ty) const
{
    return WithLocalJniEnvPtr(curFile, [&](Ptr<Expr> jniEnvPtr) {
        return CreateJavaFieldGetCall(ctx,
            ASTCloner::Clone(jniEnvPtr), jobjectInstance, field, ty);
    });
}

OwnedPtr<AST::Block> ASTFactory::CreateJavaFieldSetCall(AfterTypeCheckContext& ctx, Ptr<AST::Expr> jniEnvPtr,
    Ptr<Expr> jobjectInstance, JavaMemberSignature field, AST::Ty& ty, OwnedPtr<AST::Expr> value) const
{
    CJC_ASSERT(field.IsField());
    CJC_ASSERT(field.IsStatic() || jobjectInstance); // instance cannot be null if field is non-static.
    auto curFile = jniEnvPtr->curFile;
    auto block = CreateBlock({}, {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
    block->curFile = curFile;

    auto jclass = jclassCache.CreateJClassAccess(ctx, field.GetClassSignature(), jniEnvPtr);
    auto jfield = jfieldIdCache.CreateJFieldIdAccess(ctx, field, ASTCloner::Clone(jclass.get()), jniEnvPtr);

    OwnedPtr<AST::Expr> jniValue;
    if (ty.IsPrimitive()) {
        jniValue = ilib.CreateJValueExpr(std::move(value));
    } else {
        jniValue = ilib.CreateJObjectExpr(std::move(value));
    }

    // 1. (*jniEnv)->SetField(jclass/jobject, jfield, value)
    auto jniFieldSetCall = field.IsStatic()
        ? jni.CreateSetStaticJavaFieldCall(ty, jniEnvPtr, jclass, jfield, std::move(jniValue))
        : jni.CreateSetInstanceJavaFieldCall(ty, jniEnvPtr, jobjectInstance, jfield, std::move(jniValue));

    block->body.push_back(std::move(jniFieldSetCall));

    // 2. handlePendingException()
    block->body.push_back(ilib.CreateJNIHandlePendingExceptionCall(jniEnvPtr));
    return block;
}

OwnedPtr<AST::Block> ASTFactory::CreateJavaFieldSetCall(AfterTypeCheckContext& ctx, AST::File& curFile,
    Ptr<Expr> jobjectInstance, JavaMemberSignature field, AST::Ty& ty, OwnedPtr<AST::Expr> value) const
{
    return WithLocalJniEnvPtr(curFile, [&](Ptr<Expr> jniEnvPtr) {
        return CreateJavaFieldSetCall(ctx,
            ASTCloner::Clone(jniEnvPtr), jobjectInstance, field, ty, std::move(value));
    });
}

OwnedPtr<VarDecl> ASTFactory::CreateTmpVArrayVarDecl(std::vector<OwnedPtr<Expr>> args, File& curFile) const
{
    auto jvalueTy = ilib.GetJValueTy();
    CJC_NULLPTR_CHECK(jvalueTy);
    auto varrayTy = typeManager.GetVArrayTy(*jvalueTy, static_cast<int64_t>(args.size()));
    auto argsArray = CreateArrayLit(std::move(args), {varrayTy});
    auto argsVar = CreateTmpVarDecl(nullptr, std::move(argsArray));
    argsVar->isVar = true;
    argsVar->SetTy(argsVar->initializer->GetTy());
    CopyBasicInfo(argsVar->initializer.get(), argsVar.get());
    argsVar->curFile = &curFile;
    return argsVar;
}

std::vector<OwnedPtr<Expr>> ASTFactory::ConvertToJValues(std::vector<OwnedPtr<Expr>> values) const
{
    for (auto& value : values) {
        value = ilib.CreateJValueExpr(std::move(value));
    }
    return values;
}

std::vector<OwnedPtr<Expr>> ASTFactory::CreateParamsUsage(const FuncParamList& paramList) const
{
    auto curFile = paramList.curFile;
    std::vector<OwnedPtr<Expr>> args;
    args.reserve(paramList.params.size());
    for (auto& param : paramList.params) {
        args.emplace_back(WithinFile(CreateRefExpr(*param), curFile));
    }
    return args;
}

std::vector<OwnedPtr<Expr>> ASTFactory::ExtractArgExprs(const std::vector<OwnedPtr<FuncArg>>& args) const
{
    std::vector<OwnedPtr<Expr>> argExprs;
    for (auto& arg : args) {
        auto argExpr = ASTCloner::Clone(arg->expr.get());
        CJC_NULLPTR_CHECK(argExpr);
        argExprs.push_back(std::move(argExpr));
    }
    return argExprs;
}

OwnedPtr<Block> ASTFactory::WithLocalJniEnvPtr(File& curFile,
    std::function<OwnedPtr<Block>(Ptr<Expr> jniEnvPtr)> builder) const
{
    static auto jniEnvPtrDecl = ilib.GetJniEnvPtrDecl();
    auto jniEnvVar = CreateTmpVarDecl(jniEnvPtrDecl->type, ilib.CreateGetJniEnvCall(&curFile));

    auto resBlock = builder(WithinFile(CreateRefExpr(*jniEnvVar), &curFile));

    resBlock->body.insert(resBlock->body.begin(), std::move(jniEnvVar));
    return resBlock;
}
}
