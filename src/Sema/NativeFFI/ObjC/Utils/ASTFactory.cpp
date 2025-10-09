// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements factory class for creating AST nodes.
 */

#include "ASTFactory.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "NativeFFI/Utils.h"
#include "TypeCheckUtil.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Types.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/SafePointer.h"

using namespace Cangjie::AST;
using namespace Cangjie::TypeCheckUtil;
using namespace Cangjie::Interop::ObjC;
using namespace Cangjie::Native::FFI;

namespace {

constexpr auto VALUE_IDENT = "value";
constexpr auto INIT_IDENT = "init";

} // namespace

OwnedPtr<Expr> ASTFactory::CreateNativeHandleExpr(OwnedPtr<Expr> entity)
{
    CJC_ASSERT(typeMapper.IsValidObjCMirror(*entity->ty) || typeMapper.IsObjCImpl(*entity->ty));
    auto curFile = entity->curFile;
    return WithinFile(CreateMemberAccess(std::move(entity), NATIVE_HANDLE_IDENT), curFile);
}

OwnedPtr<Expr> ASTFactory::CreateNativeHandleExpr(ClassLikeTy& ty, Ptr<File> curFile)
{
    CJC_ASSERT(typeMapper.IsValidObjCMirror(ty) || typeMapper.IsObjCImpl(ty));
    auto thisRef = CreateThisRef(ty.commonDecl, &ty, curFile);

    return CreateNativeHandleExpr(std::move(thisRef));
}

OwnedPtr<Expr> ASTFactory::CreateNativeHandleExpr(ClassLikeDecl& decl, bool isStatic, Ptr<File> curFile)
{
    auto& ty = *StaticCast<ClassLikeTy>(decl.ty);
    auto handle = isStatic ? CreateGetClassCall(ty, curFile)
                           : UnwrapEntity(Native::FFI::CreateThisRef(&decl, &ty, curFile));

    return handle;
}

OwnedPtr<Expr> ASTFactory::UnwrapEntity(OwnedPtr<Expr> expr)
{
    if (typeMapper.IsValidObjCMirror(*expr->ty) || typeMapper.IsObjCImpl(*expr->ty)) {
        return CreateNativeHandleExpr(std::move(expr));
    }

    if (expr->ty->IsCoreOptionType()) {
        CJC_ABORT(); // Option type is not supported
    }
    if (typeMapper.IsObjCPointer(*expr->ty)) {
        CJC_ASSERT(expr->ty->typeArgs.size() == 1);
        auto elementType = expr->ty->typeArgs[0];
        auto field = GetObjCPointerPointerField();
        auto fieldRef = CreateRefExpr(*field, *expr);
        return CreateUnsafePointerCast(
            CreateMemberAccess(std::move(expr), *field),
            typeMapper.Cj2CType(elementType)
        );
    }

    CJC_ASSERT(expr->ty->IsPrimitive());
    return expr;
}

OwnedPtr<Expr> ASTFactory::WrapEntity(OwnedPtr<Expr> expr, Ty& wrapTy)
{
    if (typeMapper.IsValidObjCMirror(wrapTy)) {
        CJC_ASSERT(expr->ty->IsPointer());
        auto classLikeTy = StaticCast<ClassLikeTy>(&wrapTy);
        auto mirror = As<ASTKind::CLASS_DECL>(classLikeTy->commonDecl);
        if (!mirror) {
            CJC_ABORT(); // mirror interface is not supported
        }

        auto ctor = GetGeneratedMirrorCtor(*mirror);
        return CreateCallExpr(CreateRefExpr(*ctor), Nodes<FuncArg>(CreateFuncArg(std::move(expr))), ctor, classLikeTy,
            CallKind::CALL_OBJECT_CREATION);
    }

    if (typeMapper.IsObjCImpl(wrapTy)) {
        CJC_ASSERT(expr->ty->IsPointer());
        auto& classLikeTy = *StaticCast<ClassLikeTy>(&wrapTy);
        auto& impl = *classLikeTy.commonDecl;
        return CreateGetFromRegistryByNativeHandleCall(std::move(expr), CreateRefType(impl));
    }

    if (typeMapper.IsObjCPointer(wrapTy)) {
        CJC_ASSERT(expr->ty->IsPointer());
        CJC_ASSERT(wrapTy.typeArgs.size() == 1);
        auto ctor = GetObjCPointerConstructor();
        CJC_ASSERT(ctor);
        auto ctorRef = CreateRefExpr(*ctor, *expr);
        ctorRef->typeArguments.emplace_back(CreateType(wrapTy.typeArgs[0]));
        auto unitPtrExpr = CreateUnsafePointerCast(
            std::move(expr),
            typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT));
        return CreateCallExpr(std::move(ctorRef), Nodes<FuncArg>(CreateFuncArg(std::move(unitPtrExpr))), ctor, &wrapTy,
            CallKind::CALL_STRUCT_CREATION);
    }

    if (wrapTy.IsCoreOptionType()) {
        CJC_ABORT(); // Option type is not supported
    }

    CJC_ASSERT(expr->ty->IsPrimitive());
    CJC_ASSERT(wrapTy.IsPrimitive());
    return expr;
}

OwnedPtr<VarDecl> ASTFactory::CreateNativeHandleField(ClassDecl& target)
{
    auto nativeHandleTy = bridge.GetNativeObjCIdTy();

    auto nativeHandleField = CreateVarDecl(NATIVE_HANDLE_IDENT, nullptr, CreateType(nativeHandleTy));
    nativeHandleField->ty = nativeHandleTy;
    nativeHandleField->EnableAttr(Attribute::PUBLIC);

    PutDeclToClassBody(*nativeHandleField, target);

    return nativeHandleField;
}

OwnedPtr<Expr> ASTFactory::CreateNativeHandleInit(FuncDecl& ctor)
{
    auto& impl = *As<ASTKind::CLASS_LIKE_DECL>(ctor.outerDecl);
    auto& implTy = *StaticCast<ClassLikeTy>(impl.ty);
    auto& param = *ctor.funcBody->paramLists[0]->params.back();

    auto lhs = CreateNativeHandleExpr(implTy, impl.curFile); // this.$obj
    auto rhs = CreateRefExpr(param);
    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    auto nativeObjHandleAssignExpr = CreateAssignExpr(std::move(lhs), std::move(rhs), unitTy);

    return nativeObjHandleAssignExpr;
}

OwnedPtr<FuncDecl> ASTFactory::CreateInitCjObject(const ClassDecl& target, FuncDecl& ctor)
{
    auto curFile = ctor.curFile;
    auto registryIdTy = bridge.GetRegistryIdTy();

    auto wrapperParamList = MakeOwned<FuncParamList>();
    auto& wrapperParams = wrapperParamList->params;
    auto& ctorParams = ctor.funcBody->paramLists[0]->params;
    std::transform(ctorParams.begin(), ctorParams.end(), std::back_inserter(wrapperParams), [this](auto& p) {
        return CreateFuncParam(p->identifier.GetRawText(), nullptr, nullptr, typeMapper.Cj2CType(p->ty));
    });
    auto objParamRef = CreateRefExpr(*wrapperParams[0]);

    std::vector<Ptr<Ty>> wrapperParamTys;
    std::transform(
        wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys), [](auto& p) { return p->ty; });

    auto wrapperTy = typeManager.GetFunctionTy(wrapperParamTys, registryIdTy, {.isC = true});

    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    std::vector<OwnedPtr<FuncArg>> ctorCallArgs;
    ctorCallArgs.emplace_back(CreateFuncArg(std::move(objParamRef)));

    // skip first param, as it is needed only for restore @ObjCImpl instance.
    for (size_t argIdx = 1; argIdx < ctorParams.size(); ++argIdx) {
        auto& wrapperParam = wrapperParams[argIdx];

        auto paramRef = WithinFile(CreateRefExpr(*wrapperParam), curFile);
        OwnedPtr<FuncArg> ctorCallArg = CreateFuncArg(WrapEntity(std::move(paramRef), *ctorParams[argIdx]->ty));
        ctorCallArgs.emplace_back(std::move(ctorCallArg));
    }

    auto ctorCall = CreateCallExpr(
        CreateRefExpr(ctor), std::move(ctorCallArgs), Ptr(&ctor), target.ty, CallKind::CALL_OBJECT_CREATION);
    auto putToRegistryCall = CreatePutToRegistryCall(std::move(ctorCall));

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), CreateType(registryIdTy),
        CreateBlock(Nodes<Node>(std::move(putToRegistryCall)), registryIdTy), wrapperTy);

    auto wrapperName = nameGenerator.GenerateInitCjObjectName(ctor);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), wrapperTy);
    wrapper->EnableAttr(Attribute::C, Attribute::GLOBAL, Attribute::PUBLIC, Attribute::NO_MANGLE);
    wrapper->funcBody->funcDecl = wrapper.get();
    PutDeclToFile(*wrapper, *ctor.curFile);

    return wrapper;
}

OwnedPtr<FuncDecl> ASTFactory::CreateDeleteCjObject(ClassDecl& target)
{
    auto registryIdTy = bridge.GetRegistryIdTy();

    auto param = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, registryIdTy);
    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    auto paramRef = CreateRefExpr(*param);

    std::vector<Ptr<Ty>> funcParamTys;
    funcParamTys.emplace_back(param->ty);
    auto funcTy = typeManager.GetFunctionTy(std::move(funcParamTys), unitTy, {.isC = true});

    std::vector<OwnedPtr<FuncParam>> funcParams;
    funcParams.emplace_back(std::move(param));
    auto paramList = CreateFuncParamList(std::move(funcParams));

    std::vector<OwnedPtr<Node>> funcNodes;
    auto getFromRegistryCall =
        CreateGetFromRegistryByIdCall(ASTCloner::Clone(paramRef.get()), CreateRefType(target));
    auto removeFromRegistryCall = CreateRemoveFromRegistryCall(std::move(paramRef));

    auto objTmpVarDecl = CreateTmpVarDecl(CreateRefType(target), std::move(getFromRegistryCall));
    auto nativeHandleExpr = CreateMemberAccess(CreateRefExpr(*objTmpVarDecl), NATIVE_HANDLE_IDENT);
    auto releaseCall = CreateObjCRuntimeReleaseCall(std::move(nativeHandleExpr));

    funcNodes.emplace_back(std::move(objTmpVarDecl));
    funcNodes.emplace_back(std::move(removeFromRegistryCall));
    funcNodes.emplace_back(std::move(releaseCall));

    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.emplace_back(std::move(paramList));

    auto funcBody = CreateFuncBody(
        std::move(paramLists),
        Native::FFI::CreateUnitType(target.curFile),
        CreateBlock(std::move(funcNodes), unitTy),
        funcTy);

    auto funcName = nameGenerator.GenerateDeleteCjObjectName(target);

    auto ret = CreateFuncDecl(funcName, std::move(funcBody), funcTy);
    ret->EnableAttr(Attribute::C, Attribute::GLOBAL, Attribute::PUBLIC, Attribute::NO_MANGLE);
    ret->funcBody->funcDecl = ret.get();
    PutDeclToFile(*ret, *target.curFile);

    return ret;
}

OwnedPtr<FuncDecl> ASTFactory::CreateMethodWrapper(FuncDecl& method)
{
    auto registryIdTy = bridge.GetRegistryIdTy();

    auto registryIdParam = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, registryIdTy);
    auto registryIdParamRef = CreateRefExpr(*registryIdParam);
    auto outerDecl = StaticAs<ASTKind::CLASS_DECL>(method.outerDecl);

    auto wrapperParamList = MakeOwned<FuncParamList>();
    auto& wrapperParams = wrapperParamList->params;
    wrapperParams.emplace_back(std::move(registryIdParam));

    auto& originParams = method.funcBody->paramLists[0]->params;
    std::transform(originParams.begin(), originParams.end(), std::back_inserter(wrapperParams), [this](auto& p) {
        auto convertedParamTy = typeMapper.Cj2CType(p->ty);
        return CreateFuncParam(p->identifier.GetRawText(), CreateType(convertedParamTy), nullptr, convertedParamTy);
    });

    std::vector<Ptr<Ty>> wrapperParamTys;
    std::transform(
        wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys), [](auto& p) { return p->ty; });

    auto wrapperTy =
        typeManager.GetFunctionTy(wrapperParamTys, typeMapper.Cj2CType(method.funcBody->retType->ty), {.isC = true});

    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    std::vector<OwnedPtr<Node>> wrapperNodes;
    auto getFromRegistryCall =
        CreateGetFromRegistryByIdCall(std::move(registryIdParamRef), CreateRefType(*outerDecl));

    auto objTmpVarDecl = CreateTmpVarDecl(CreateRefType(*outerDecl), std::move(getFromRegistryCall));
    auto methodExpr = CreateMemberAccess(CreateRefExpr(*objTmpVarDecl), method);
    methodExpr->curFile = method.curFile;
    methodExpr->begin = method.GetBegin();
    methodExpr->end = method.GetEnd();

    std::vector<OwnedPtr<FuncArg>> methodArgs;
    // skip first param, as it is needed only for restore @ObjCImpl instance.
    for (size_t i = 1; i < wrapperParams.size(); ++i) {
        auto wrapperParam = wrapperParams[i].get();
        auto originParam = originParams[i - 1].get();

        auto paramRef = CreateRefExpr(*wrapperParam);
        auto wrappedParamRef = WrapEntity(std::move(paramRef), *originParam->ty);
        auto arg = CreateFuncArg(std::move(wrappedParamRef), wrapperParam->identifier, originParam->ty);
        methodArgs.emplace_back(std::move(arg));
    }

    auto methodCall = CreateCallExpr(std::move(methodExpr), std::move(methodArgs), Ptr(&method),
        method.funcBody->retType->ty, CallKind::CALL_DECLARED_FUNCTION);

    wrapperNodes.emplace_back(std::move(objTmpVarDecl));
    wrapperNodes.emplace_back(UnwrapEntity(std::move(methodCall)));

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), CreateType(wrapperTy->retTy),
        CreateBlock(std::move(wrapperNodes), wrapperTy->retTy), wrapperTy);

    auto wrapperName = nameGenerator.GenerateMethodWrapperName(method);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), wrapperTy);
    wrapper->EnableAttr(Attribute::C, Attribute::GLOBAL, Attribute::PUBLIC, Attribute::NO_MANGLE);
    wrapper->funcBody->funcDecl = wrapper.get();

    PutDeclToFile(*wrapper, *method.curFile);

    return wrapper;
}

OwnedPtr<FuncDecl> ASTFactory::CreateGetterWrapper(PropDecl& prop)
{
    auto registryIdTy = bridge.GetRegistryIdTy();

    auto registryIdParam = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, registryIdTy);
    auto registryIdParamRef = CreateRefExpr(*registryIdParam);
    auto outerDecl = StaticAs<ASTKind::CLASS_DECL>(prop.outerDecl);

    auto wrapperParamList = MakeOwned<FuncParamList>();
    auto& wrapperParams = wrapperParamList->params;
    wrapperParams.emplace_back(std::move(registryIdParam));

    std::vector<Ptr<Ty>> wrapperParamTys;
    std::transform(
        wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys), [](auto& p) { return p->ty; });

    auto wrapperTy = typeManager.GetFunctionTy(wrapperParamTys, typeMapper.Cj2CType(prop.ty), {.isC = true});

    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    std::vector<OwnedPtr<Node>> wrapperNodes;
    auto getFromRegistryCall =
        CreateGetFromRegistryByIdCall(std::move(registryIdParamRef), CreateRefType(*outerDecl));

    auto objTmpVarDecl = CreateTmpVarDecl(CreateRefType(*outerDecl), std::move(getFromRegistryCall));
    // Not sure if accessing the first getter is good
    auto propGetterExpr = CreateMemberAccess(CreateRefExpr(*objTmpVarDecl), *prop.getters[0].get());
    propGetterExpr->curFile = prop.curFile;
    propGetterExpr->begin = prop.GetBegin();
    propGetterExpr->end = prop.GetEnd();

    auto propGetterCall = CreateCallExpr(
        std::move(propGetterExpr), {}, Ptr(prop.getters[0].get()), prop.ty, CallKind::CALL_DECLARED_FUNCTION);

    wrapperNodes.emplace_back(std::move(objTmpVarDecl));
    wrapperNodes.emplace_back(UnwrapEntity(std::move(propGetterCall)));

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), ASTCloner::Clone(prop.type.get()),
        CreateBlock(std::move(wrapperNodes), wrapperTy->retTy), wrapperTy);

    auto wrapperName = nameGenerator.GeneratePropGetterWrapperName(prop);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), wrapperTy);
    wrapper->EnableAttr(Attribute::C, Attribute::GLOBAL, Attribute::PUBLIC, Attribute::NO_MANGLE);
    wrapper->funcBody->funcDecl = wrapper.get();

    PutDeclToFile(*wrapper, *prop.curFile);

    return wrapper;
}

OwnedPtr<FuncDecl> ASTFactory::CreateSetterWrapper(PropDecl& prop)
{
    auto registryIdTy = bridge.GetRegistryIdTy();
    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);

    auto registryIdParam = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, registryIdTy);
    auto registryIdParamRef = CreateRefExpr(*registryIdParam);

    auto convertedPropTy = typeMapper.Cj2CType(prop.ty);
    auto setterParam = CreateFuncParam(VALUE_IDENT, CreateType(convertedPropTy), nullptr, convertedPropTy);
    auto setterParamRef = CreateRefExpr(*setterParam);

    auto outerDecl = StaticAs<ASTKind::CLASS_DECL>(prop.outerDecl);

    auto wrapperParamList = MakeOwned<FuncParamList>();
    auto& wrapperParams = wrapperParamList->params;
    wrapperParams.emplace_back(std::move(registryIdParam));
    wrapperParams.emplace_back(std::move(setterParam));

    std::vector<Ptr<Ty>> wrapperParamTys;
    std::transform(
        wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys), [](auto& p) { return p->ty; });

    auto wrapperTy = typeManager.GetFunctionTy(wrapperParamTys, unitTy, {.isC = true});

    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    std::vector<OwnedPtr<Node>> wrapperNodes;
    auto getFromRegistryCall =
        CreateGetFromRegistryByIdCall(std::move(registryIdParamRef), CreateRefType(*outerDecl));

    auto objTmpVarDecl = CreateTmpVarDecl(CreateRefType(*outerDecl), std::move(getFromRegistryCall));
    // Not sure if accessing the first setter is good
    auto& setter = *prop.setters[0].get();
    auto& originParams = setter.funcBody->paramLists[0]->params;
    auto propSetterExpr = CreateMemberAccess(CreateRefExpr(*objTmpVarDecl), setter);
    propSetterExpr->curFile = prop.curFile;
    propSetterExpr->begin = prop.GetBegin();
    propSetterExpr->end = prop.GetEnd();

    std::vector<OwnedPtr<FuncArg>> propSetterArgs;
    // skip first param, as it is needed only for restore @ObjCImpl instance.
    for (size_t i = 1; i < wrapperParams.size(); ++i) {
        auto wrapperParam = wrapperParams[i].get();
        auto originParam = originParams[i - 1].get();

        auto paramRef = CreateRefExpr(*wrapperParam);
        auto wrappedParamRef = WrapEntity(std::move(paramRef), *originParam->ty);
        auto arg = CreateFuncArg(std::move(wrappedParamRef), wrapperParam->identifier, originParam->ty);
        propSetterArgs.emplace_back(std::move(arg));
    }

    auto propSetterCall = CreateCallExpr(
        std::move(propSetterExpr), std::move(propSetterArgs), Ptr(&setter), unitTy, CallKind::CALL_DECLARED_FUNCTION);

    wrapperNodes.emplace_back(std::move(objTmpVarDecl));
    wrapperNodes.emplace_back(std::move(propSetterCall));

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), CreateUnitType(prop.curFile),
        CreateBlock(std::move(wrapperNodes), wrapperTy->retTy), wrapperTy);

    auto wrapperName = nameGenerator.GetPropSetterWrapperName(prop);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), wrapperTy);
    wrapper->EnableAttr(Attribute::C, Attribute::GLOBAL, Attribute::PUBLIC, Attribute::NO_MANGLE);
    wrapper->funcBody->funcDecl = wrapper.get();

    PutDeclToFile(*wrapper, *prop.curFile);

    return wrapper;
}

OwnedPtr<FuncDecl> ASTFactory::CreateGetterWrapper(VarDecl& field)
{
    auto registryIdTy = bridge.GetRegistryIdTy();

    auto registryIdParam = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, registryIdTy);
    auto registryIdParamRef = CreateRefExpr(*registryIdParam);
    auto outerDecl = StaticAs<ASTKind::CLASS_DECL>(field.outerDecl);

    auto wrapperParamList = MakeOwned<FuncParamList>();
    auto& wrapperParams = wrapperParamList->params;
    wrapperParams.emplace_back(std::move(registryIdParam));

    std::vector<Ptr<Ty>> wrapperParamTys;
    std::transform(
        wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys), [](auto& p) { return p->ty; });

    auto convertedFieldTy = typeMapper.Cj2CType(field.ty);
    auto wrapperTy = typeManager.GetFunctionTy(wrapperParamTys, convertedFieldTy, {.isC = true});

    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    std::vector<OwnedPtr<Node>> wrapperNodes;
    auto getFromRegistryCall =
        CreateGetFromRegistryByIdCall(std::move(registryIdParamRef), CreateRefType(*outerDecl));

    auto objTmpVarDecl = CreateTmpVarDecl(CreateRefType(*outerDecl), std::move(getFromRegistryCall));
    auto fieldExpr = CreateMemberAccess(CreateRefExpr(*objTmpVarDecl), field);
    fieldExpr->curFile = field.curFile;
    fieldExpr->begin = field.GetBegin();
    fieldExpr->end = field.GetEnd();

    wrapperNodes.emplace_back(std::move(objTmpVarDecl));
    wrapperNodes.emplace_back(UnwrapEntity(std::move(fieldExpr)));

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), ASTCloner::Clone(field.type.get()),
        CreateBlock(std::move(wrapperNodes), wrapperTy->retTy), wrapperTy);

    // Generate wrapper name from ORIGIN field, not a mirror one.
    auto wrapperName = nameGenerator.GetFieldGetterWrapperName(field);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), wrapperTy);
    wrapper->EnableAttr(Attribute::C, Attribute::GLOBAL, Attribute::PUBLIC, Attribute::NO_MANGLE);
    wrapper->funcBody->funcDecl = wrapper.get();

    PutDeclToFile(*wrapper, *field.curFile);

    return wrapper;
}

OwnedPtr<FuncDecl> ASTFactory::CreateSetterWrapper(VarDecl& field)
{
    auto registryIdTy = bridge.GetRegistryIdTy();
    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);

    auto registryIdParam = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, registryIdTy);
    auto registryIdParamRef = CreateRefExpr(*registryIdParam);
    auto convertedFieldTy = typeMapper.Cj2CType(field.ty);
    auto setterParam = CreateFuncParam(VALUE_IDENT, CreateType(convertedFieldTy), nullptr, convertedFieldTy);
    auto setterParamRef = CreateRefExpr(*setterParam);

    auto outerDecl = StaticAs<ASTKind::CLASS_DECL>(field.outerDecl);

    auto wrapperParamList = MakeOwned<FuncParamList>();
    auto& wrapperParams = wrapperParamList->params;
    wrapperParams.emplace_back(std::move(registryIdParam));
    wrapperParams.emplace_back(std::move(setterParam));

    std::vector<Ptr<Ty>> wrapperParamTys;
    std::transform(
        wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys), [](auto& p) { return p->ty; });

    auto wrapperTy = typeManager.GetFunctionTy(wrapperParamTys, unitTy, {.isC = true});

    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    std::vector<OwnedPtr<Node>> wrapperNodes;
    auto getFromRegistryCall =
        CreateGetFromRegistryByIdCall(std::move(registryIdParamRef), CreateRefType(*outerDecl));

    auto objTmpVarDecl = CreateTmpVarDecl(CreateRefType(*outerDecl), std::move(getFromRegistryCall));
    auto lhs = CreateMemberAccess(CreateRefExpr(*objTmpVarDecl), field);
    auto assignFieldExpr = CreateAssignExpr(std::move(lhs), WrapEntity(std::move(setterParamRef), *field.ty), unitTy);
    assignFieldExpr->curFile = field.curFile;
    assignFieldExpr->begin = field.GetBegin();
    assignFieldExpr->end = field.GetEnd();

    wrapperNodes.emplace_back(std::move(objTmpVarDecl));
    wrapperNodes.emplace_back(std::move(assignFieldExpr));

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), CreateUnitType(field.curFile),
        CreateBlock(std::move(wrapperNodes), wrapperTy->retTy), wrapperTy);

    // Generate wrapper name from ORIGIN field, not a mirror one.
    auto wrapperName = nameGenerator.GetFieldSetterWrapperName(field);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), wrapperTy);
    wrapper->EnableAttr(Attribute::C, Attribute::GLOBAL, Attribute::PUBLIC, Attribute::NO_MANGLE);
    wrapper->funcBody->funcDecl = wrapper.get();

    PutDeclToFile(*wrapper, *field.curFile);

    return wrapper;
}

OwnedPtr<ThrowExpr> ASTFactory::CreateThrowUnreachableCodeExpr(File& file)
{
    auto exceptionDecl = bridge.GetObjCUnreachableCodeExceptionDecl();
    CJC_NULLPTR_CHECK(exceptionDecl);
    return CreateThrowException(*exceptionDecl, {}, file, typeManager);
}

std::set<Ptr<FuncDecl>> ASTFactory::GetAllParentCtors(ClassDecl& target)
{
    std::set<Ptr<FuncDecl>> result = {};
    for (auto& it : target.GetAllSuperDecls()) {
        for (OwnedPtr<Decl>& declPtr : it->GetMemberDecls()) {
            if (IsGeneratedMember(*declPtr.get())) {
                continue;
            }

            if (!declPtr->TestAttr(Attribute::CONSTRUCTOR)) {
                continue;
            }

            if (declPtr->astKind != ASTKind::FUNC_DECL) {
                // skip primary ctor, as it is desugared to init already
                continue;
            }

            auto funcDecl = StaticAs<ASTKind::FUNC_DECL>(declPtr.get());
            if (!funcDecl->funcBody) {
                continue;
            }

            result.insert(funcDecl);
        }
    }
    return result;
}

OwnedPtr<FuncDecl> ASTFactory::CreateMirrorCtorDecl(ClassDecl& target)
{
    auto nativeObjCIdTy = bridge.GetNativeObjCIdTy();
    auto param = CreateFuncParam(NATIVE_HANDLE_IDENT, CreateType(nativeObjCIdTy), nullptr, nativeObjCIdTy);

    std::vector<Ptr<Ty>> ctorFuncParamTys;
    ctorFuncParamTys.emplace_back(param->ty);
    auto ctorFuncTy = typeManager.GetFunctionTy(std::move(ctorFuncParamTys), target.ty);

    std::vector<OwnedPtr<FuncParam>> ctorParams;
    ctorParams.emplace_back(std::move(param));
    auto paramList = CreateFuncParamList(std::move(ctorParams));

    std::vector<OwnedPtr<Node>> ctorNodes;

    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.emplace_back(std::move(paramList));

    auto ctorFuncBody = CreateFuncBody(
        std::move(paramLists), CreateRefType(target), CreateBlock(std::move(ctorNodes), target.ty), ctorFuncTy);

    auto ctor = CreateFuncDecl(INIT_IDENT, std::move(ctorFuncBody), ctorFuncTy);
    ctor->funcBody->funcDecl = ctor.get();
    ctor->constructorCall = ConstructorCall::NONE;
    ctor->funcBody->parentClassLike = &target;
    ctor->EnableAttr(Attribute::PUBLIC, Attribute::CONSTRUCTOR);

    PutDeclToClassBody(*ctor, target);

    return ctor;
}

OwnedPtr<FuncDecl> ASTFactory::CreateImplCtor(ClassDecl& impl, FuncDecl& from)
{
    auto nativeHandleTy = bridge.GetNativeObjCIdTy();
    auto ctor = ASTCloner::Clone(Ptr(&from));

    auto& implCtorParams = ctor->funcBody->paramLists[0]->params;

    implCtorParams.insert(implCtorParams.begin(),
        CreateFuncParam(NATIVE_HANDLE_IDENT, CreateType(nativeHandleTy), nullptr, nativeHandleTy));
    auto& nativeHandleParam = *implCtorParams.front();

    std::vector<Ptr<Ty>> implCtorParamTys;
    std::transform(implCtorParams.begin(), implCtorParams.end(), std::back_inserter(implCtorParamTys),
        [](auto& p) { return p->type->ty; });

    ctor->ty = typeManager.GetFunctionTy(implCtorParamTys, ctor->funcBody->retType->ty);
    ctor->funcBody->ty = ctor->ty;
    ctor->funcBody->funcDecl = ctor.get();
    ctor->constructorCall = ConstructorCall::SUPER;

    CJC_NULLPTR_CHECK(impl.GetSuperClassDecl());
    auto parentCtor = GetGeneratedMirrorCtor(*impl.GetSuperClassDecl());
    auto superCall = CreateSuperCall(*impl.GetSuperClassDecl(), *parentCtor, parentCtor->ty);
    superCall->args.emplace_back(CreateFuncArg(CreateRefExpr(nativeHandleParam)));

    auto& body = ctor->funcBody->body->body;
    body.erase(std::remove_if(body.begin(), body.end(), [](auto& node) {
        if (auto call = As<ASTKind::CALL_EXPR>(node.get())) {
            return call->callKind == CallKind::CALL_SUPER_FUNCTION;
        }
        return false;
        }), body.end());

    body.insert(body.begin(), std::move(superCall));

    return ctor;
}

bool ASTFactory::IsGeneratedMember(const Decl& decl)
{
    return IsGeneratedNativeHandleField(decl) || IsGeneratedCtor(decl);
}

bool ASTFactory::IsGeneratedNativeHandleField(const Decl& decl)
{
    return decl.identifier.Val() == NATIVE_HANDLE_IDENT;
}

bool ASTFactory::IsGeneratedCtor(const Decl& decl)
{
    auto fd = DynamicCast<const FuncDecl*>(&decl);
    if (!fd || !fd->TestAttr(Attribute::CONSTRUCTOR) || !fd->funcBody) {
        return false;
    }

    auto& paramLists = fd->funcBody->paramLists;

    if (paramLists.empty()) {
        return false;
    }

    // taking first param list probably is not the best idea
    auto& params = paramLists[0]->params;

    if (params.empty()) {
        return false;
    }

    return params[0]->identifier == NATIVE_HANDLE_IDENT;
}

Ptr<FuncDecl> ASTFactory::GetGeneratedMirrorCtor(Decl& decl)
{
    CJC_ASSERT(decl.astKind == ASTKind::CLASS_DECL);
    CJC_ASSERT(TypeMapper::IsValidObjCMirror(*decl.ty));

    auto& classDecl = *StaticAs<ASTKind::CLASS_DECL>(&decl);
    for (auto& member : classDecl.GetMemberDeclPtrs()) {
        if (auto fd = As<ASTKind::FUNC_DECL>(member); fd && IsGeneratedCtor(*fd)) {
            return fd;
        }
    }

    CJC_ABORT();
    return nullptr;
}

Ptr<FuncDecl> ASTFactory::GetGeneratedImplCtor(const ClassDecl& impl, const FuncDecl& origin)
{
    CJC_ASSERT(origin.TestAttr(Attribute::CONSTRUCTOR));
    CJC_NULLPTR_CHECK(origin.funcBody);

    const auto& originParamLists = origin.funcBody->paramLists;
    CJC_ASSERT(!originParamLists.empty());

    // taking first param list probably is not the best idea
    const auto& originParams = originParamLists[0]->params;

    for (auto& member: impl.GetMemberDeclPtrs()) {
        if (auto fd = As<ASTKind::FUNC_DECL>(member); fd && IsGeneratedCtor(*fd)) {
            CJC_NULLPTR_CHECK(fd->funcBody);

            const auto& fdParamLists = fd->funcBody->paramLists;
            CJC_ASSERT(!fdParamLists.empty());

            // taking first param list probably is not the best idea
            const auto& fdParams = fdParamLists[0]->params;

            if (originParams.size() + 1 != fdParams.size()) {
                continue;
            }

            auto matched = true;
            // assume that first fd param if $obj: NativeObjCId
            for (size_t i = 1; i < fdParams.size(); ++i) {
                const auto fdParam = fdParams[i].get();
                const auto originParam = originParams[i - 1].get();
                if (fdParam->identifier != originParam->identifier || fdParam->ty != originParam->ty) {
                    matched = false;
                    break;
                }
            }

            if (matched) {
                return fd;
            }
        }
    }

    return Ptr<FuncDecl>();
}

void ASTFactory::PutDeclToClassBody(Decl& decl, ClassDecl& target)
{
    decl.begin = target.body->end;
    decl.end = target.body->end;
    decl.outerDecl = &target;
    decl.curFile = target.curFile;
    decl.EnableAttr(Attribute::IN_CLASSLIKE);
}

void ASTFactory::PutDeclToFile(Decl& decl, File& target)
{
    decl.curFile = &target;
    decl.begin = target.end;
    decl.end = target.end;
}

OwnedPtr<CallExpr> ASTFactory::CreatePutToRegistryCall(OwnedPtr<Expr> nativeHandle)
{
    auto putToRegistryDecl = bridge.GetPutToRegistryDecl();
    auto putToRegistryExpr = CreateRefExpr(*putToRegistryDecl);

    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(nativeHandle)));

    return CreateCallExpr(std::move(putToRegistryExpr), std::move(args), putToRegistryDecl,
        putToRegistryDecl->funcBody->retType->ty, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> ASTFactory::CreateGetFromRegistryByNativeHandleCall(
    OwnedPtr<Expr> nativeHandle, OwnedPtr<Type> typeArg)
{
    CJC_ASSERT(nativeHandle->ty->IsPointer());
    auto getFromRegistryByNativeHandleDecl = bridge.GetGetFromRegistryByNativeHandleDecl();
    auto getFromRegistryNativeHandleExpr = CreateRefExpr(*getFromRegistryByNativeHandleDecl);

    auto ty = typeArg->ty;
    CJC_ASSERT(TypeMapper::IsObjCImpl(*ty));
    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(nativeHandle)));

    getFromRegistryNativeHandleExpr->instTys.emplace_back(ty);
    getFromRegistryNativeHandleExpr->ty = typeManager.GetInstantiatedTy(getFromRegistryByNativeHandleDecl->ty,
        GenerateTypeMapping(*getFromRegistryByNativeHandleDecl, getFromRegistryNativeHandleExpr->instTys));
    getFromRegistryNativeHandleExpr->typeArguments.emplace_back(std::move(typeArg));

    return CreateCallExpr(std::move(getFromRegistryNativeHandleExpr), std::move(args),
        getFromRegistryByNativeHandleDecl, ty, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> ASTFactory::CreateGetFromRegistryByIdCall(OwnedPtr<Expr> registryId, OwnedPtr<Type> typeArg)
{
    auto getFromRegistryByIdDecl = bridge.GetGetFromRegistryByIdDecl();
    auto getFromRegistryByIdExpr = CreateRefExpr(*getFromRegistryByIdDecl);

    auto ty = typeArg->ty;
    CJC_ASSERT(TypeMapper::IsObjCImpl(*ty));

    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(registryId)));

    getFromRegistryByIdExpr->instTys.emplace_back(ty);
    getFromRegistryByIdExpr->ty = typeManager.GetInstantiatedTy(
        getFromRegistryByIdDecl->ty, GenerateTypeMapping(*getFromRegistryByIdDecl, getFromRegistryByIdExpr->instTys));
    getFromRegistryByIdExpr->typeArguments.emplace_back(std::move(typeArg));

    return CreateCallExpr(std::move(getFromRegistryByIdExpr), std::move(args), getFromRegistryByIdDecl, ty,
        CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> ASTFactory::CreateRemoveFromRegistryCall(OwnedPtr<Expr> registryId)
{
    auto removeFromRegistryDecl = bridge.GetRemoveFromRegistryDecl();
    auto removeFromRegistryExpr = CreateRefExpr(*removeFromRegistryDecl);

    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(registryId)));

    return CreateCallExpr(std::move(removeFromRegistryExpr), std::move(args), removeFromRegistryDecl,
        removeFromRegistryDecl->funcBody->retType->ty, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> ASTFactory::CreateObjCRuntimeReleaseCall(OwnedPtr<Expr> nativeHandle)
{
    auto releaseExpr = bridge.CreateObjCRuntimeReleaseExpr();

    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(nativeHandle)));
    return CreateCallExpr(std::move(releaseExpr), std::move(args), nullptr,
        TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT), CallKind::CALL_FUNCTION_PTR);
}

OwnedPtr<CallExpr> ASTFactory::CreateObjCRuntimeMsgSendCall(
    Ptr<FuncTy> ty, OwnedPtr<FuncType> funcType, std::vector<OwnedPtr<Expr>> funcArgs)
{
    auto msgSendExpr = bridge.CreateObjCRuntimeMsgSendExpr();
    auto retType = funcType->retType.get();

    auto cFuncDecl = importManager.GetCoreDecl<BuiltInDecl>(std::string(CFUNC_NAME));
    auto cFuncRefExpr = CreateRefExpr(*cFuncDecl);

    cFuncRefExpr->ty = ty;
    cFuncRefExpr->typeArguments.emplace_back(std::move(funcType));

    // CFunc<...>(msgSend)

    auto cFuncCallExpr = CreateCallExpr(std::move(cFuncRefExpr), Nodes<FuncArg>(CreateFuncArg(std::move(msgSendExpr))),
        nullptr, ty, CallKind::CALL_FUNCTION_PTR);

    std::vector<OwnedPtr<FuncArg>> msgSendCallArgs;
    std::transform(funcArgs.begin(), funcArgs.end(), std::back_inserter(msgSendCallArgs),
        [](auto&& argExpr) { return CreateFuncArg(std::move(argExpr)); });

    // CFunc<...>(msgSend)(...)
    return CreateCallExpr(
        std::move(cFuncCallExpr), std::move(msgSendCallArgs), nullptr, retType->ty, CallKind::CALL_FUNCTION_PTR);
}

OwnedPtr<CallExpr> ASTFactory::CreateObjCRuntimeMsgSendCall(
    OwnedPtr<Expr> nativeHandle, const std::string& selector, Ptr<Ty> retTy, std::vector<OwnedPtr<Expr>> args)
{
    auto selectorCall = CreateRegisterNameCall(selector, nativeHandle->curFile);

    auto ft = MakeOwned<FuncType>();
    ft->isC = true;
    ft->retType = CreateType(retTy);

    args.insert(args.begin(), std::move(selectorCall));
    args.insert(args.begin(), std::move(nativeHandle));
    std::vector<Ptr<Ty>> paramTys;
    for (auto& param : args) {
        ft->paramTypes.emplace_back(CreateType(param->ty));
        paramTys.emplace_back(param->ty);
    }

    auto fty = typeManager.GetFunctionTy(paramTys, retTy, {.isC = true});
    ft->ty = fty;

    return CreateObjCRuntimeMsgSendCall(fty, std::move(ft), std::move(args));
}

OwnedPtr<Expr> ASTFactory::CreateGetClassCall(ClassLikeTy& ty, Ptr<File> curFile)
{
    auto getClassFuncDecl = bridge.GetGetClassDecl();
    auto getClassExpr = CreateRefExpr(*getClassFuncDecl);

    auto cnameAsLit = CreateLitConstExpr(LitConstKind::STRING, ty.name, GetStringDecl(importManager).ty);
    return CreateCall(getClassFuncDecl, curFile, std::move(cnameAsLit));
}

OwnedPtr<CallExpr> ASTFactory::CreateRegisterNameCall(OwnedPtr<Expr> selectorExpr)
{
    auto registerNameDecl = bridge.GetRegisterNameDecl();
    auto registerNameExpr = CreateRefExpr(*registerNameDecl);

    auto curFile = selectorExpr->curFile;
    return CreateCall(registerNameDecl, curFile, std::move(selectorExpr));
}

OwnedPtr<CallExpr> ASTFactory::CreateRegisterNameCall(const std::string& selector, Ptr<File> curFile)
{
    auto strTy = GetStringDecl(importManager).ty;
    auto selectorAsLit = CreateLitConstExpr(LitConstKind::STRING, selector, strTy);
    selectorAsLit->curFile = curFile;

    return CreateRegisterNameCall(std::move(selectorAsLit));
}

OwnedPtr<CallExpr> ASTFactory::CreateAllocCall(OwnedPtr<Expr> className)
{
    auto allocDecl = bridge.GetAllocDecl();
    auto allocExpr = CreateRefExpr(*allocDecl);

    auto curFile = className->curFile;
    return CreateCall(allocDecl, curFile, std::move(className));
}

OwnedPtr<CallExpr> ASTFactory::CreateAllocCall(ClassDecl& decl, Ptr<File> curFile)
{
    auto objcname = nameGenerator.GetObjCDeclName(decl);
    auto classNameExpr =
        WithinFile(CreateLitConstExpr(LitConstKind::STRING, objcname, GetStringDecl(importManager).ty), curFile);
    return CreateAllocCall(std::move(classNameExpr));
}

OwnedPtr<Expr> ASTFactory::CreateMethodCallViaMsgSend(
    FuncDecl& fd, OwnedPtr<Expr> nativeHandle, std::vector<OwnedPtr<Expr>> rawArgs)
{
    auto objcname = nameGenerator.GetObjCDeclName(fd);
    return CreateObjCRuntimeMsgSendCall(
        std::move(nativeHandle), objcname, typeMapper.Cj2CType(StaticCast<FuncTy>(fd.ty)->retTy), std::move(rawArgs));
}

OwnedPtr<Expr> ASTFactory::CreatePropGetterCallViaMsgSend(PropDecl& pd, OwnedPtr<Expr> nativeHandle)
{
    auto objcname = nameGenerator.GetObjCDeclName(pd);
    return CreateObjCRuntimeMsgSendCall(std::move(nativeHandle), objcname, typeMapper.Cj2CType(pd.ty), {});
}

OwnedPtr<Expr> ASTFactory::CreatePropSetterCallViaMsgSend(PropDecl& pd, OwnedPtr<Expr> nativeHandle, OwnedPtr<Expr> arg)
{
    auto objcname = nameGenerator.GetObjCDeclName(pd);
    std::transform(
        objcname.begin(), objcname.begin() + 1, objcname.begin(), [](unsigned char c) { return std::toupper(c); });
    objcname = "set" + objcname + ":";
    return CreateObjCRuntimeMsgSendCall(
        std::move(nativeHandle), objcname, typeMapper.Cj2CType(pd.ty), Nodes<Expr>(std::move(arg)));
}

OwnedPtr<Expr> ASTFactory::CreateAutoreleasePoolScope(Ptr<Ty> ty, std::vector<OwnedPtr<Node>> actions)
{
    CJC_ASSERT(typeMapper.IsObjCCompatible(*ty));
    CJC_ASSERT(!actions.empty());

    Ptr<FuncDecl> arpdecl;
    OwnedPtr<RefExpr> arpref;
    if (ty->IsPrimitive() || typeMapper.IsObjCPointer(*ty)) {
        arpdecl = bridge.GetWithAutoreleasePoolDecl();
        auto unwrappedTy = typeMapper.Cj2CType(ty);
        arpref = CreateRefExpr(*arpdecl);
        arpref->instTys.emplace_back(unwrappedTy);
        arpref->ty = typeManager.GetInstantiatedTy(arpdecl->ty, GenerateTypeMapping(*arpdecl, arpref->instTys));
        arpref->typeArguments.emplace_back(CreateType(unwrappedTy));
    } else {
        arpdecl = bridge.GetWithAutoreleasePoolObjDecl();
        arpref = CreateRefExpr(*arpdecl);
    }

    auto args = Nodes<FuncArg>(CreateFuncArg(WrapReturningLambdaExpr(typeManager, std::move(actions))));

    auto retTy = StaticCast<FuncTy>(arpref->ty)->retTy;
    return CreateCallExpr(std::move(arpref), std::move(args), arpdecl, retTy, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> ASTFactory::CreateGetInstanceVariableCall(
    const AST::PropDecl& field, OwnedPtr<Expr> nativeHandle)
{
    Ptr<FuncDecl> getInstVarDecl;
    OwnedPtr<RefExpr> getInstVarRef;
    if (field.ty->IsPrimitive()) {
        getInstVarDecl = bridge.GetGetInstanceVariableDecl();
        getInstVarRef = CreateRefExpr(*getInstVarDecl);

        getInstVarRef->instTys.emplace_back(field.ty);
        getInstVarRef->ty = typeManager.GetInstantiatedTy(
            getInstVarDecl->ty,
            GenerateTypeMapping(*getInstVarDecl, getInstVarRef->instTys)
        );
        getInstVarRef->typeArguments.emplace_back(CreateType(field.ty));
    } else {
        getInstVarDecl = bridge.GetGetInstanceVariableObjDecl();
        getInstVarRef = CreateRefExpr(*getInstVarDecl);
    }

    auto objcname = nameGenerator.GetObjCDeclName(field);
    auto nameExpr = WithinFile(
        CreateLitConstExpr(
            LitConstKind::STRING,
            objcname,
            GetStringDecl(importManager).ty
        ),
        field.curFile);

    auto args = Nodes<FuncArg>(
        CreateFuncArg(std::move(nativeHandle)),
        CreateFuncArg(std::move(nameExpr))
    );

    auto retTy = StaticCast<FuncTy>(getInstVarRef->ty)->retTy;
    return CreateCallExpr(
        std::move(getInstVarRef),
        std::move(args),
        getInstVarDecl,
        retTy,
        CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> ASTFactory::CreateObjCRuntimeSetInstanceVariableCall(
    const PropDecl& field, OwnedPtr<Expr> nativeHandle, OwnedPtr<Expr> value)
{
    Ptr<FuncDecl> setInstVarDecl;
    OwnedPtr<RefExpr> setInstVarRef;
    if (field.ty->IsPrimitive()) {
        setInstVarDecl = bridge.GetSetInstanceVariableDecl();
        setInstVarRef = CreateRefExpr(*setInstVarDecl);

        setInstVarRef->instTys.emplace_back(field.ty);
        setInstVarRef->ty = typeManager.GetInstantiatedTy(
            setInstVarDecl->ty,
            GenerateTypeMapping(*setInstVarDecl, setInstVarRef->instTys)
        );
        setInstVarRef->typeArguments.emplace_back(CreateType(field.ty));
    } else {
        setInstVarDecl = bridge.GetSetInstanceVariableObjDecl();
        setInstVarRef = CreateRefExpr(*setInstVarDecl);
    }

    auto objcname = nameGenerator.GetObjCDeclName(field);
    auto nameExpr = WithinFile(
        CreateLitConstExpr(LitConstKind::STRING,
            objcname,
            GetStringDecl(importManager).ty
        ),
        field.curFile);

    auto args = Nodes<FuncArg>(
        CreateFuncArg(std::move(nativeHandle)),
        CreateFuncArg(std::move(nameExpr)),
        CreateFuncArg(std::move(value))
    );

    return CreateCallExpr(
        std::move(setInstVarRef),
        std::move(args),
        setInstVarDecl,
        TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT),
        CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<Expr> ASTFactory::CreateUnsafePointerCast(OwnedPtr<Expr> expr, Ptr<Ty> elementType)
{
    CJC_ASSERT(expr->ty->IsPointer());
    CJC_ASSERT(Ty::IsMetCType(*elementType));
    auto ptrExpr = MakeOwned<PointerExpr>();
    auto pointerType = typeManager.GetPointerTy(elementType);
    CopyBasicInfo(expr, ptrExpr);
    ptrExpr->arg = CreateFuncArg(std::move(expr));
    ptrExpr->ty = pointerType;
    ptrExpr->type = CreateType(ptrExpr->ty);
    ptrExpr->EnableAttr(Attribute::COMPILER_ADD);
    return ptrExpr;
}

Ptr<FuncDecl> ASTFactory::GetObjCPointerConstructor()
{
    Ptr<FuncDecl> result = nullptr;
    auto outer = bridge.GetObjCPointerDecl();
    for (auto& member : outer->body->decls) {
        if (auto funcDecl = DynamicCast<FuncDecl*>(member.get())) {
            if (funcDecl->TestAttr(Attribute::CONSTRUCTOR)
                && funcDecl->funcBody
                && funcDecl->funcBody->paramLists[0]
                && funcDecl->funcBody->paramLists[0]->params.size() == 1) {
                result = funcDecl;
            }
        }
    }
    return result;
}

Ptr<VarDecl> ASTFactory::GetObjCPointerPointerField()
{
    Ptr<VarDecl> result = nullptr;
    auto outer = bridge.GetObjCPointerDecl();
    for (auto& member : outer->body->decls) {
        if (auto fieldDecl = DynamicCast<VarDecl*>(member.get())) {
            if (fieldDecl->ty->IsPointer()) {
                result = fieldDecl;
            }
        }
    }
    return result;
}
