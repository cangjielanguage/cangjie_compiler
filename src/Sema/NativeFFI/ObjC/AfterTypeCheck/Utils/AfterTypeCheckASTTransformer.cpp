// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements AST transformations for AfterTypeCheck ObjC interop.
 */

#include "AfterTypeCheckASTTransformer.h"
#include "NativeFFI/ObjC/Utils/ASTQuery.h"
#include "NativeFFI/Utils.h"
#include "TypeCheckUtil.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include <algorithm>
#include <iterator>
#include <vector>

namespace Cangjie::Interop::ObjC {

using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI;

namespace {
Ptr<Ty> FuncTyOf(TypeManager& typeManager, const std::vector<OwnedPtr<FuncParam>>& params, ModalTy retTy)
{
    std::vector<ModalTy> paramTys;
    paramTys.reserve(params.size());
    std::transform(
        params.begin(), params.end(), std::back_inserter(paramTys), [](auto&& param) { return param->GetTy(); });
    return typeManager.GetFunctionTy(paramTys, retTy);
}

OwnedPtr<CallExpr> CreateAllocCall(const InteropLibBridge& objCLib, OwnedPtr<Expr> className)
{
    auto curFile = className->curFile;
    return CreateCall(objCLib.GetAllocDecl(), curFile, std::move(className));
}
} // namespace

AfterTypeCheckASTTransformer::AfterTypeCheckASTTransformer(
    ImportManager& importManager, TypeManager& typeManager, DiagnosticEngine& diag, ASTInserter& astInserter) noexcept
    : typeManager(typeManager), astInserter(astInserter), objCLib(importManager, diag)
{
}

void AfterTypeCheckASTTransformer::TransformToObjCImplRegCompanionField(VarDecl& vd, ClassDecl& regComp) const noexcept
{
    vd.identifier = REGISTRY_COMPANION_FIELD_IDENT;
    vd.SetTy(regComp.GetTy());
    vd.EnableAttr(Attribute::PRIVATE, Attribute::INITIALIZED, Attribute::NO_REFLECT_INFO);
}

void AfterTypeCheckASTTransformer::TransformToObjCImplStaticProxyFunc(
    FuncDecl& proxy, FuncDecl& origin, ClassDecl& regCompanion) const noexcept
{
    CJC_ASSERT_WITH_MSG(origin.TestAttr(Attribute::STATIC), "expected static function to be proxied");
    CJC_ASSERT_WITH_MSG(!origin.funcBody->paramLists.empty(), "expected at least one param list");

    proxy.EnableAttr(Attribute::OBJ_C_EXPORT_MOVED_MEMBER_PROXY);

    auto& params = proxy.funcBody->paramLists[0]->params;
    std::vector<OwnedPtr<FuncArg>> originCallArgs;
    std::transform(params.begin(), params.end(), std::back_inserter(originCallArgs),
        [](auto&& param) { return CreateFuncArg(CreateRefExpr(*param)); });

    auto regCompanionRef = CreateRefExpr(regCompanion);
    auto originMemberAccess = CreateMemberAccess(std::move(regCompanionRef), origin);
    auto originCall = CreateCallExpr(std::move(originMemberAccess), std::move(originCallArgs), Ptr(&origin),
        StaticCast<FuncTy>(origin.DataTy())->retTy, CallKind::CALL_DECLARED_FUNCTION);

    std::vector<OwnedPtr<Node>> proxyBodyNodes;
    proxyBodyNodes.push_back(std::move(originCall));
    proxy.funcBody->body->body = std::move(proxyBodyNodes);
}

void AfterTypeCheckASTTransformer::TransformToObjCImplProxyProp(
    PropDecl& pd, VarDecl& vd, Decl& receiver) const noexcept
{
    pd.identifier = vd.identifier;
    pd.SetTy(vd.GetTy());
    pd.CloneAttrs(vd);
    pd.EnableAttr(Attribute::COMPILER_ADD);
    // Set before the accessors are filled in below, which clone the property's attributes.
    pd.EnableAttr(Attribute::OBJ_C_EXPORT_MOVED_MEMBER_PROXY);
    pd.isVar = vd.isVar;
    pd.modifiers.insert(vd.modifiers.begin(), vd.modifiers.end());
    for (auto& anno : vd.annotations) {
        pd.annotations.emplace_back(ASTCloner::Clone(anno.get()));
    }
    if (pd.getters.empty()) {
        auto paramLists = Nodes<FuncParamList>(CreateFuncParamList(std::vector<OwnedPtr<FuncParam>>{}));
        // Transform function will set the identifier
        auto getter = CreateFuncDecl("", CreateFuncBody(std::move(paramLists), nullptr, CreateBlock({})));
        TransformToProxyPropGetter(*getter, vd, receiver, pd);
        astInserter.InsertGetterInto(pd, std::move(getter), InsertMode::RECURSIVE);
    } else {
        TransformToProxyPropGetter(*pd.getters[0], vd, receiver, pd);
    }

    if (vd.isVar) {
        pd.EnableAttr(Attribute::MUT);
        Modifier mut = Modifier(TokenKind::MUT, vd.begin);
        mut.curFile = vd.curFile;
        pd.modifiers.insert(std::move(mut));

        if (pd.setters.empty()) {
            auto paramLists = Nodes<FuncParamList>(CreateFuncParamList(std::vector<OwnedPtr<FuncParam>>{}));
            // Transform function will set the identifier
            auto setter = CreateFuncDecl("", CreateFuncBody(std::move(paramLists), nullptr, CreateBlock({})));
            TransformToProxyPropSetter(*setter, vd, receiver, pd);
            astInserter.InsertSetterInto(pd, std::move(setter), InsertMode::RECURSIVE);
        } else {
            TransformToProxyPropSetter(*pd.setters[0], vd, receiver, pd);
        }
    }
}

void AfterTypeCheckASTTransformer::TransformToProxyPropGetter(
    FuncDecl& getter, VarDecl& origin, Decl& receiver, PropDecl& pd) const noexcept
{
    getter.funcBody->body->body.clear();
    getter.funcBody->body->body.push_back(CreateMemberAccess(CreateRefExpr(receiver), origin));
    getter.funcBody->body->SetTy(origin.GetTy());

    getter.identifier = "$" + pd.identifier + "get";
    auto getterTy = typeManager.GetFunctionTy({}, pd.GetTy());
    getter.SetTy({getterTy});
    getter.funcBody->SetTy({getterTy});

    getter.CloneAttrs(pd);
    getter.DisableAttr(Attribute::MUT);
    getter.EnableAttr(Attribute::COMPILER_ADD);
}

void AfterTypeCheckASTTransformer::TransformToProxyPropSetter(
    FuncDecl& setter, VarDecl& origin, Decl& receiver, PropDecl& pd) const noexcept
{
    auto setterParam = CreateFuncParam("value", nullptr, nullptr, origin.GetTy());
    auto setterParamRef = CreateRefExpr(*setterParam);

    CJC_ASSERT_WITH_MSG(!setter.funcBody->paramLists.empty(), "expected at least one param list");
    auto& setterParams = setter.funcBody->paramLists[0]->params;
    setterParams.clear();
    setterParams.push_back(std::move(setterParam));

    static auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    auto setterTy = FuncTyOf(typeManager, setterParams, {unitTy});

    auto lhs = CreateMemberAccess(CreateRefExpr(receiver), origin);
    auto assignment = CreateAssignExpr(std::move(lhs), std::move(setterParamRef), {unitTy});
    setter.funcBody->body->body.clear();
    setter.funcBody->body->body.push_back(std::move(assignment));
    setter.funcBody->body->SetTy({unitTy});

    setter.identifier = "$" + pd.identifier + "set";
    setter.SetTy(setterTy);
    setter.funcBody->SetTy(setterTy);

    setter.CloneAttrs(pd);
    setter.DisableAttr(Attribute::MUT);
    setter.EnableAttr(Attribute::COMPILER_ADD);
}

void AfterTypeCheckASTTransformer::AppendNativeObjCIdMarkerIfNeeded(CallExpr& call, Ptr<File> curFile) const noexcept
{
    auto ctor = call.resolvedFunction;
    CJC_NULLPTR_CHECK(ctor);
    CJC_ASSERT_WITH_MSG(ctor->TestAttr(Attribute::CONSTRUCTOR), "expected a ctor call");
    if (!HasNativeObjCIdMarkerParam(*ctor)) {
        return;
    }

    call.args.push_back(CreateFuncArg(WithinFile(CreateRefExpr(*objCLib.GetNativeObjCIdMarkerInstance()), curFile)));
}

void AfterTypeCheckASTTransformer::TransformToObjCImplBaseCtorDecl(FuncDecl& ctor, ClassDecl& impl) const noexcept
{
    auto handleParam = CreateFuncParam(NATIVE_HANDLE_IDENT, nullptr, nullptr, objCLib.GetNativeObjCIdTy());
    auto regIdParam = CreateFuncParam(REGISTRY_ID_IDENT, nullptr, nullptr, objCLib.GetRegistryIdTy());
    // `NativeObjCId` is an alias of `CPointer<Unit>` and `RegistryId` of `Int64`, both ObjC-compatible types a
    // user may take, so without the marker this ctor would clash with a user-written `init(CPointer<Unit>, Int64)`.
    auto markerParam = CreateFuncParam(NATIVE_HANDLE_MARKER, nullptr, nullptr, objCLib.GetNativeObjCIdMarkerTy());

    CJC_ASSERT_WITH_MSG(!ctor.funcBody->paramLists.empty(), "expected at least one param list");
    auto& ctorParams = ctor.funcBody->paramLists[0]->params;
    ctorParams.clear();
    ctorParams.push_back(std::move(handleParam));
    ctorParams.push_back(std::move(regIdParam));
    ctorParams.push_back(std::move(markerParam));

    auto ctorTy = FuncTyOf(typeManager, ctorParams, impl.GetTy());

    ctor.identifier = INIT_IDENT;
    ctor.SetTy(ctorTy);
    ctor.EnableAttr(Attribute::PUBLIC, Attribute::CONSTRUCTOR);

    ctor.funcBody->SetTy(ctorTy);
    ctor.funcBody->parentClassLike = &impl;
    ctor.funcBody->body->SetTy(impl.GetTy());
}

void AfterTypeCheckASTTransformer::TransformToObjCImplBaseCtorBody(
    FuncBody& ctorBody, ClassDecl& impl, ClassDecl& regComp) const noexcept
{
    CJC_ASSERT_WITH_MSG(!ctorBody.paramLists.empty(), "expected at least one param list");
    auto& params = ctorBody.paramLists[0]->params;
    CJC_ASSERT_WITH_MSG(params.size() > 1, "expected at least 2 params");
    auto curFile = impl.curFile;
    auto handleParamRef = CreateRefExpr(*params[0]);
    auto regIdParamRef = CreateRefExpr(*params[1]);
    auto superClass = impl.GetSuperClassDecl();
    ctorBody.body->body.clear();
    if (superClass->IsObjCExport()) {
        auto superCtor = GetObjCImplBaseCtor(*superClass);
        CJC_ASSERT_WITH_MSG(superCtor, "expected base ctor in the exported '@ObjC' class");
        auto superCtorCall = CreateSuperCall(*superClass, *superCtor, superCtor->DataTy());
        superCtorCall->args.push_back(CreateFuncArg(std::move(handleParamRef)));
        superCtorCall->args.push_back(CreateFuncArg(ASTCloner::Clone(regIdParamRef.get())));
        superCtorCall->curFile = curFile;
        AppendNativeObjCIdMarkerIfNeeded(*superCtorCall, curFile);
        ctorBody.body->body.push_back(std::move(superCtorCall));
    } else {
        CJC_ASSERT_WITH_MSG(superClass->IsObjCForeign(), "expected super class to be @ObjCMirror");
        auto superCtor = GetObjCMirrorBaseCtor(*superClass);
        CJC_ASSERT_WITH_MSG(superCtor, "expected base ctor in the @ObjCMirror decl");
        auto superCtorCall = CreateSuperCall(*superClass, *superCtor, superCtor->DataTy());
        superCtorCall->args.push_back(CreateFuncArg(std::move(handleParamRef)));
        superCtorCall->curFile = curFile;
        AppendNativeObjCIdMarkerIfNeeded(*superCtorCall, curFile);
        ctorBody.body->body.push_back(std::move(superCtorCall));
    }

    auto getFromRegistryByIdDecl = objCLib.GetGetFromRegistryByIdDecl();
    auto getFromRegistryByIdExpr = CreateRefExpr(*getFromRegistryByIdDecl);

    std::vector<OwnedPtr<FuncArg>> getFromRegistryByIdArgs;
    getFromRegistryByIdArgs.emplace_back(CreateFuncArg(std::move(regIdParamRef)));

    getFromRegistryByIdExpr->instTys.emplace_back(regComp.DataTy());
    getFromRegistryByIdExpr->SetTy(typeManager.GetInstantiatedTy(getFromRegistryByIdDecl->GetTy(),
        TypeCheckUtil::GenerateTypeMapping(*getFromRegistryByIdDecl, getFromRegistryByIdExpr->instTys)));
    getFromRegistryByIdExpr->typeArguments.push_back(CreateRefType(regComp));
    auto getFromRegistryIdCall = CreateCallExpr(std::move(getFromRegistryByIdExpr), std::move(getFromRegistryByIdArgs),
        getFromRegistryByIdDecl, regComp.GetTy(), CallKind::CALL_DECLARED_FUNCTION);

    auto regCompField = GetObjCImplRegCompanionField(impl);
    auto lhs = CreateMemberAccess(CreateThisRef(&impl, impl.GetTy(), impl.curFile), *regCompField);
    static auto unitTy = typeManager.GetPrimitiveTy(AST::TypeKind::TYPE_UNIT);
    auto assignment = CreateAssignExpr(std::move(lhs), std::move(getFromRegistryIdCall), {unitTy});

    ctorBody.body->body.push_back(std::move(assignment));
}

void AfterTypeCheckASTTransformer::TransformToObjCImplRegCompBaseCtorBody(
    FuncBody& ctorBody, ClassDecl& regComp) const noexcept
{
    auto curFile = regComp.curFile;
    CJC_ASSERT_WITH_MSG(!ctorBody.paramLists.empty(), "expected at least one param list");
    auto& params = ctorBody.paramLists[0]->params;
    CJC_ASSERT_WITH_MSG(!params.empty(), "expected at least 1 param");
    auto handleParamRef = CreateRefExpr(*params[0]);
    if (auto superClass = GetObjCImplRegistryCompanionSuperClass(regComp)) {
        auto superCtor = GetObjCImplRegCompanionBaseCtor(*superClass);
        CJC_ASSERT_WITH_MSG(superCtor, "expected base ctor in the @ObjCImpl registry companion class");
        auto superCtorCall = CreateSuperCall(*superClass, *superCtor, superCtor->DataTy());
        superCtorCall->args.push_back(CreateFuncArg(std::move(handleParamRef)));
        superCtorCall->curFile = curFile;
        ctorBody.body->body.push_back(std::move(superCtorCall));
        return;
    }

    auto putToRegistryDecl = objCLib.GetPutToRegistryDecl();
    auto setRegistryIdDecl = objCLib.GetSetRegistryIdDecl();

    auto putToRegistryCall = CreateCall(putToRegistryDecl, curFile, CreateThisRef(&regComp, regComp.GetTy(), curFile));
    auto setRegistryIdCall =
        CreateCall(setRegistryIdDecl, curFile, std::move(handleParamRef), std::move(putToRegistryCall));

    ctorBody.body->body.push_back(std::move(setRegistryIdCall));
}

void AfterTypeCheckASTTransformer::TransformToObjCImplRegDataCtorDecl(
    FuncDecl& ctor, ClassDecl& impl, ClassDecl& regComp) const noexcept
{
    auto nativeObjCIdTy = objCLib.GetNativeObjCIdTy();
    auto tupleTy = typeManager.GetTupleTy({nativeObjCIdTy, regComp.DataTy()});
    auto regDataParam = CreateFuncParam(std::string(REGISTRY_DATA_PARAM_IDENT), nullptr, nullptr, {tupleTy});

    CJC_ASSERT_WITH_MSG(!ctor.funcBody->paramLists.empty(), "expected at least one param list");
    auto& params = ctor.funcBody->paramLists[0]->params;
    params.insert(params.begin(), std::move(regDataParam));

    auto ctorTy = FuncTyOf(typeManager, params, impl.GetTy());

    ctor.identifier = INIT_IDENT;
    ctor.SetTy(ctorTy);
    ctor.funcBody->SetTy(ctorTy);
}

void AfterTypeCheckASTTransformer::TransformToObjCImplUserCtorBody(FuncBody& ctorBody, FuncDecl& regDataCtor,
    ClassDecl& impl, ClassDecl& regComp, OwnedPtr<Expr> objCClassRef) const noexcept
{
    auto curFile = ctorBody.curFile;

    auto handleParam = CreateFuncParam(NATIVE_HANDLE_IDENT, nullptr, nullptr, objCLib.GetNativeObjCIdTy());
    auto handleParamRef = CreateRefExpr(*handleParam);

    auto regCompCtor = GetObjCImplRegCompanionBaseCtor(regComp);
    CJC_ASSERT_WITH_MSG(regCompCtor, "expected a base ctor in the registry companion class");
    auto regCompCtorCall = CreateCall(regCompCtor, curFile, ASTCloner::Clone(handleParamRef.get()));
    auto tupleTy = typeManager.GetTupleTy({objCLib.GetNativeObjCIdTy(), regComp.DataTy()});

    std::vector<OwnedPtr<Expr>> tupleElements;
    tupleElements.push_back(std::move(handleParamRef));
    tupleElements.push_back(std::move(regCompCtorCall));
    auto tupleLit = CreateTupleLit(std::move(tupleElements), {tupleTy});

    std::vector<OwnedPtr<FuncParam>> lambdaParams;
    lambdaParams.push_back(std::move(handleParam));

    std::vector<OwnedPtr<Node>> lambdaBodyNodes;
    lambdaBodyNodes.push_back(std::move(tupleLit));

    auto lambdaExpr =
        WithinFile(WrapReturningLambdaExpr(typeManager, std::move(lambdaBodyNodes), std::move(lambdaParams)), curFile);

    auto allocCall = CreateAllocCall(objCLib, WithinFile(std::move(objCClassRef), curFile));
    std::vector<OwnedPtr<FuncArg>> lambdaCallArgs;
    lambdaCallArgs.push_back(CreateFuncArg(std::move(allocCall)));
    auto lambdaCall = CreateCallExpr(std::move(lambdaExpr), std::move(lambdaCallArgs), nullptr, {tupleTy});

    CJC_ASSERT_WITH_MSG(!ctorBody.paramLists.empty(), "expected at least one param list");
    auto& userParams = ctorBody.paramLists[0]->params;

    std::vector<OwnedPtr<FuncArg>> thisCallArgs;
    thisCallArgs.push_back(CreateFuncArg(std::move(lambdaCall)));

    for (auto& param : userParams) {
        thisCallArgs.push_back(CreateFuncArg(WithinFile(CreateRefExpr(*param), curFile)));
    }

    auto thisCall = CreateThisCall(impl, regDataCtor, regDataCtor.DataTy(), curFile, std::move(thisCallArgs));
    ctorBody.body->body.clear();
    ctorBody.body->body.push_back(std::move(thisCall));
}

} // namespace Cangjie::Interop::ObjC
