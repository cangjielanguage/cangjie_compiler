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
#include "Desugar/AfterTypeCheck.h"
#include "NativeFFI/ObjC/Utils/Common.h"
#include "NativeFFI/Utils.h"
#include "TypeCheckUtil.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/SafePointer.h"
#include "cangjie/AST/ASTCasting.h"

using namespace Cangjie::AST;
using namespace Cangjie::TypeCheckUtil;
using namespace Cangjie::Interop::ObjC;
using namespace Cangjie::Native::FFI;

namespace {

constexpr auto VALUE_IDENT = "value";
constexpr auto INIT_IDENT = "init";
constexpr auto FINALIZER_IDENT = "~init";

} // namespace

OwnedPtr<Expr> ASTFactory::CreateNativeHandleExpr(OwnedPtr<Expr> entity)
{
    CJC_ASSERT(typeMapper.IsObjCMirror(*entity->ty) || typeMapper.IsObjCImpl(*entity->ty)|| typeMapper.IsSyntheticWrapper(*entity->ty));
    auto entityTy = StaticCast<ClassLikeTy>(entity->ty);
    auto curFile = entity->curFile;
    auto getter = GetNativeHandleGetter(*entityTy->commonDecl);
    auto getterMemberAccess = WithinFile(CreateMemberAccess(std::move(entity), *getter), curFile);
    auto getterCall = CreateCallExpr(std::move(getterMemberAccess), {}, getter, StaticCast<FuncTy>(getter->ty)->retTy,
        CallKind::CALL_DECLARED_FUNCTION);
    return WithinFile(std::move(getterCall), curFile);
}

OwnedPtr<Expr> ASTFactory::CreateNativeHandleExpr(ClassLikeTy& ty, Ptr<File> curFile)
{
    CJC_ASSERT(typeMapper.IsObjCMirror(ty) || typeMapper.IsObjCImpl(ty) || typeMapper.IsSyntheticWrapper(ty));
    auto thisRef = CreateThisRef(ty.commonDecl, &ty, curFile);

    return CreateNativeHandleExpr(std::move(thisRef));
}

OwnedPtr<Expr> ASTFactory::CreateNativeHandleExpr(ClassLikeDecl& decl, bool isStatic, Ptr<File> curFile)
{
    if (!isStatic) {
        auto& ty = *StaticCast<ClassLikeTy>(decl.ty);

        return CreateNativeHandleExpr(ty, curFile);
    }

    // Static case
    auto& ty = *StaticCast<ClassTy>(decl.ty);
    return CreateGetClassCall(ty, curFile);
}

OwnedPtr<Expr> ASTFactory::UnwrapEntity(OwnedPtr<Expr> expr)
{
    if (typeMapper.IsObjCMirror(*expr->ty) || typeMapper.IsObjCImpl(*expr->ty)) {
        return CreateNativeHandleExpr(std::move(expr));
    }

    if (typeMapper.IsObjCCJMapping(*expr->ty)) {
        return CreatePutToRegistryCall(std::move(expr));
    }

    if (expr->ty->IsCoreOptionType()) {
        auto innerTy = expr->ty->typeArgs[0];
        if (typeMapper.IsValidObjCMirror(*innerTy) || typeMapper.IsObjCImpl(*innerTy)) {
            return UnwrapObjCMirrorOption(std::move(expr), innerTy);
        }
    }
    if (typeMapper.IsObjCPointer(*expr->ty)) {
        CJC_ASSERT(expr->ty->typeArgs.size() == 1);
        auto elementType = expr->ty->typeArgs[0];
        auto field = bridge.GetObjCPointerPointerField();
        auto fieldRef = CreateRefExpr(*field, *expr);
        return CreateUnsafePointerCast(
            CreateMemberAccess(std::move(expr), *field),
            typeMapper.Cj2CType(elementType)
        );
    }
    if (typeMapper.IsObjCFunc(*expr->ty)) {
        CJC_ASSERT(expr->ty->typeArgs.size() == 1);
        auto mappedTy = typeMapper.Cj2CType(expr->ty);
        auto getter = bridge.GetObjCFuncFPointerAccessor();
        CJC_ASSERT(mappedTy->IsPointer());
        return CreateUnsafePointerCast(
            CreateMemberCall(std::move(expr), getter),
            mappedTy->typeArgs[0]
        );
    }
    if (typeMapper.IsObjCBlock(*expr->ty)) {
        CJC_ASSERT(expr->ty->typeArgs.size() == 1);
        auto mappedTy = typeMapper.Cj2CType(expr->ty);
        auto abiPtr = bridge.GetObjCBlockAbiPointerAccessor();
        CJC_ASSERT(mappedTy->IsPointer());
        return CreateUnsafePointerCast(
            CreateMemberCall(std::move(expr), abiPtr),
            mappedTy->typeArgs[0]
        );
    }

    CJC_ASSERT(expr->ty->IsPrimitive() || expr->ty->IsPointer() || Ty::IsCStructType(*expr->ty));
    return expr;
}

OwnedPtr<Expr> ASTFactory::WrapEntity(OwnedPtr<Expr> expr, Ty& wrapTy)
{
    if (typeMapper.IsValidObjCMirror(wrapTy)) {
        CJC_ASSERT(expr->ty->IsPointer());
        auto classLikeTy = StaticCast<ClassLikeTy>(&wrapTy);
        auto mirror = As<ASTKind::CLASS_DECL>(classLikeTy->commonDecl);
        if (!mirror) {
            mirror = GetSyntheticWrapper(importManager, *classLikeTy->commonDecl);
            CJC_NULLPTR_CHECK(mirror);
        }

        auto ctor = GetGeneratedBaseCtor(*mirror);
        return CreateCallExpr(CreateRefExpr(*ctor), Nodes<FuncArg>(CreateFuncArg(std::move(expr))), ctor, classLikeTy,
            CallKind::CALL_OBJECT_CREATION);
    }

    if (typeMapper.IsObjCImpl(wrapTy)) {
        CJC_ASSERT(expr->ty->IsPointer());
        auto& classTy = *StaticCast<ClassTy>(&wrapTy);
        auto& impl = *classTy.commonDecl;
        return CreateGetFromRegistryByNativeHandleCall(std::move(expr), CreateRefType(impl));
    }

    if (typeMapper.IsObjCPointer(wrapTy)) {
        CJC_ASSERT(expr->ty->IsPointer());
        CJC_ASSERT(wrapTy.typeArgs.size() == 1);
        auto ctor = bridge.GetObjCPointerConstructor();
        CJC_ASSERT(ctor);
        auto ctorRef = CreateRefExpr(*ctor, *expr);
        ctorRef->instTys.push_back(wrapTy.typeArgs[0]);
        ctorRef->typeArguments.push_back(CreateType(wrapTy.typeArgs[0]));
        auto unitPtrExpr = CreateUnsafePointerCast(
            std::move(expr),
            typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT));
        return CreateCallExpr(std::move(ctorRef), Nodes<FuncArg>(CreateFuncArg(std::move(unitPtrExpr))), ctor, &wrapTy,
            CallKind::CALL_STRUCT_CREATION);
    }

    if (typeMapper.IsObjCFunc(wrapTy)) {
        CJC_ASSERT(expr->ty->IsPointer());
        CJC_ASSERT(wrapTy.typeArgs.size() == 1);
        auto ctor = bridge.GetObjCFuncConstructor();
        CJC_ASSERT(ctor);
        auto ctorRef = CreateRefExpr(*ctor, *expr);
        ctorRef->instTys.push_back(wrapTy.typeArgs[0]);
        ctorRef->typeArguments.push_back(CreateType(wrapTy.typeArgs[0]));
        auto unitPtrExpr = CreateUnsafePointerCast(
            std::move(expr),
            typeManager.GetFunctionTy(
                {},
                typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT),
                {.isC = true}
            ));
        return CreateCallExpr(std::move(ctorRef), Nodes<FuncArg>(CreateFuncArg(std::move(unitPtrExpr))), ctor, &wrapTy,
            CallKind::CALL_STRUCT_CREATION);
    }

    if (typeMapper.IsObjCBlock(wrapTy)) {
        CJC_ASSERT(expr->ty->IsPointer());
        CJC_ASSERT(wrapTy.typeArgs.size() == 1);
        auto ctor = bridge.GetObjCBlockConstructor();
        CJC_ASSERT(ctor);
        auto ctorRef = CreateRefExpr(*ctor, *expr);
        ctorRef->instTys.push_back(wrapTy.typeArgs[0]);
        ctorRef->typeArguments.push_back(CreateType(wrapTy.typeArgs[0]));
        return CreateCallExpr(std::move(ctorRef), Nodes<FuncArg>(CreateFuncArg(std::move(expr))), ctor, &wrapTy,
            CallKind::CALL_OBJECT_CREATION);
    }

    if (typeMapper.IsObjCCJMapping(wrapTy)) {
        if (auto decl = Ty::GetDeclOfTy<InheritableDecl>(&wrapTy)) {
            return CreateGetFromRegistryByIdCall(std::move(expr), CreateRefType(*decl));
        }
        CJC_ABORT(); // other CJMapping is not supported
    }

    if (wrapTy.IsCoreOptionType()) {
        if (auto classALTy = DynamicCast<ClassLikeTy>(wrapTy.typeArgs[0])) {
            if (auto decl = classALTy->commonDecl;
                decl && decl->TestAnyAttr(Attribute::OBJ_C_MIRROR, Attribute::OBJ_C_MIRROR_SUBTYPE)) {
                    return WrapObjCMirrorOption(expr, decl, expr->curFile);
            }
        }
    }

    CJC_ASSERT(expr->ty->IsPrimitive() || Ty::IsCStructType(*expr->ty));
    CJC_ASSERT(wrapTy.IsPrimitive() || Ty::IsCStructType(wrapTy));
    return expr;
}

// OptionType
Ptr<Ty> ASTFactory::GetOptionTy(Ptr<Ty> ty)
{
    return typeManager.GetEnumTy(*GetOptionDecl(), {ty});
}

Ptr<EnumDecl> ASTFactory::GetOptionDecl()
{
    static auto decl = importManager.GetCoreDecl<EnumDecl>(STD_LIB_OPTION);
    return decl;
}

Ptr<Decl> ASTFactory::GetOptionSomeDecl()
{
    static auto someDecl = Sema::Desugar::AfterTypeCheck::LookupEnumMember(GetOptionDecl(), OPTION_VALUE_CTOR);
    return someDecl;
}

Ptr<Decl> ASTFactory::GetOptionNoneDecl()
{
    static auto noneDecl = Sema::Desugar::AfterTypeCheck::LookupEnumMember(GetOptionDecl(), OPTION_NONE_CTOR);
    return noneDecl;
}

OwnedPtr<Expr> ASTFactory::CreateOptionSomeRef(Ptr<Ty> ty)
{
    auto someDeclRef = CreateRefExpr(*GetOptionSomeDecl());
    auto optionActualTy = GetOptionTy(ty);
    someDeclRef->ty = typeManager.GetFunctionTy({ty}, optionActualTy);
    return someDeclRef;
}

OwnedPtr<Expr> ASTFactory::CreateOptionNoneRef(Ptr<Ty> ty)
{
    auto noneDeclRef = CreateRefExpr(*GetOptionNoneDecl());
    auto optionActualTy = GetOptionTy(ty);
    noneDeclRef->ty = optionActualTy;
    return noneDeclRef;
}

OwnedPtr<Expr> ASTFactory::CreateOptionSomeCall(OwnedPtr<Expr> expr, Ptr<Ty> ty)
{
    std::vector<OwnedPtr<FuncArg>> someDeclCallArgs {};
    someDeclCallArgs.emplace_back(CreateFuncArg(std::move(expr)));
    auto someDeclCall = CreateCallExpr(CreateOptionSomeRef(ty), std::move(someDeclCallArgs));
    someDeclCall->ty = GetOptionTy(ty);
    someDeclCall->resolvedFunction = As<ASTKind::FUNC_DECL>(GetOptionSomeDecl());
    someDeclCall->callKind = CallKind::CALL_DECLARED_FUNCTION;
    return someDeclCall;
}

OwnedPtr<Expr> ASTFactory::CreateOptionMatch(
    OwnedPtr<Expr> selector,
    std::function<OwnedPtr<Expr>(VarDecl&)> someBranch,
    std::function<OwnedPtr<Expr>()> noneBranch,
    Ptr<Ty> ty)
{
    auto curFile = selector->curFile;
    CJC_NULLPTR_CHECK(curFile);

    auto& optTy = *selector->ty;
    CJC_ASSERT(optTy.IsCoreOptionType());
    auto optArgTy = optTy.typeArgs[0];

    auto vp = CreateVarPattern(Cangjie::V_COMPILER, optArgTy);
    vp->curFile = curFile;
    vp->varDecl->curFile = curFile;
    auto& someArgVar = *vp->varDecl;

    auto somePattern = MakeOwnedNode<EnumPattern>();
    somePattern->ty = selector->ty;
    somePattern->constructor = CreateOptionSomeRef(optArgTy);
    somePattern->patterns.emplace_back(std::move(vp));
    somePattern->curFile = curFile;
    auto caseSome = CreateMatchCase(std::move(somePattern), someBranch(someArgVar));

    auto nonePattern = MakeOwnedNode<EnumPattern>();
    nonePattern->constructor = CreateOptionNoneRef(optArgTy);
    nonePattern->ty = nonePattern->constructor->ty;
    nonePattern->curFile = curFile;
    auto caseNone = CreateMatchCase(std::move(nonePattern), noneBranch());

    return WithinFile(CreateMatchExpr(
        std::move(selector),
        Nodes<MatchCase>(std::move(caseSome), std::move(caseNone)), ty), curFile);
}


Ptr<Ty> ASTFactory::GetObjCTy()
{
    return typeManager.GetPointerTy(typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT));
}

OwnedPtr<Expr> ASTFactory::CreateObjCobjectNull()
{
    auto pointerExpr = MakeOwnedNode<PointerExpr>();
    pointerExpr->type = MakeOwnedNode<Type>();
    pointerExpr->type->ty = GetObjCTy();
    pointerExpr->ty = pointerExpr->type->ty;
    return pointerExpr;
}

/*
    UNWRAP [OPTION -> HANDLE]
    match(entity) {
        None -> CPointer()
        Some(t) -> Unwrap(t)
    }
*/
OwnedPtr<Expr> ASTFactory::UnwrapObjCMirrorOption(
    OwnedPtr<Expr> entity, Ptr<Ty> ty)
{
    auto curFile = entity->curFile;
    CJC_NULLPTR_CHECK(curFile);
    auto actualTy = ty;
    return CreateOptionMatch(std::move(entity),
        [this, curFile](VarDecl& e) {
            auto unwrapped = UnwrapEntity(WithinFile(CreateRefExpr(e), curFile));
            return unwrapped;
        },
        [this]() {
            // CPointer<Unit>()
            return CreateObjCobjectNull();
        }, typeMapper.Cj2CType(actualTy));
}
/*
    WRAP [HADNLE -> OPTION]
    match (handle.isNull) {
        case false => Some(T(handle)) | Some(getFromRegistry(handle))
        case true => None
    }
 */
OwnedPtr<Expr> ASTFactory::WrapObjCMirrorOption(
    const Ptr<Expr> entity, Ptr<ClassLikeDecl> mirror, const Ptr<File> curFile)
{
    //CJC_ASSERT(ty->IsCoreOptionType());
    // auto curFile = entity->curFile;
    // CJC_NULLPTR_CHECK(curFile);
    //CJC_ASSERT(IsMirror(*decl) || IsImpl(*decl));
    std::vector<OwnedPtr<Node>> nodes;
    auto baseTy = typeManager.GetPointerTy(typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT));
    auto tmpVar = CreateTmpVarDecl(CreateType(baseTy), entity);
    CopyBasicInfo(tmpVar->initializer.get(), tmpVar.get());
    tmpVar->begin = entity->begin;
    tmpVar->curFile = curFile;
    auto objcrefExpr = CreateRefExpr(*tmpVar);

    auto castTy = mirror->ty;
    // case true => None
    OwnedPtr<Expr> trueBranch = CreateOptionNoneRef(castTy);
    // case false => wrap($tmp, T)
    OwnedPtr<Expr> falseBranch =  WrapEntity(std::move(objcrefExpr), *castTy);

    auto isInstanceCall = WithinFile(CreateGetObjcEntityOrNullCall(*tmpVar, curFile), curFile);
    auto boolMatch = CreateBoolMatch(
        std::move(isInstanceCall), std::move(trueBranch), std::move(falseBranch), GetOptionTy(castTy));
    nodes.push_back(std::move(tmpVar));
    nodes.push_back(std::move(boolMatch));
    return WrapReturningLambdaCall(typeManager, std::move(nodes));
}

// Mirror(handle)
OwnedPtr<Expr> ASTFactory::CreateMirrorConstructorCall(OwnedPtr<Expr> entity, Ptr<Ty> mirrorTy)
{
    auto curFile = entity->curFile;
    auto classLikeTy = DynamicCast<ClassLikeTy*>(mirrorTy.get());
    if (!classLikeTy) {
        return nullptr;
    }
    if (auto decl = classLikeTy->commonDecl; decl && decl->TestAttr(Attribute::OBJ_C_MIRROR)) {
        Ptr<ClassDecl> cld;
        if (decl->astKind == ASTKind::CLASS_DECL) {
          cld = StaticAs<ASTKind::CLASS_DECL>(decl);
        } else {
          cld = GetSyntheticWrapper(importManager, *decl);
        }
        CJC_NULLPTR_CHECK(cld);
        auto mirrorCtor = GetGeneratedBaseCtor(*cld);
        CJC_NULLPTR_CHECK(mirrorCtor);

        auto ctorCall = CreateCall(mirrorCtor, curFile, std::move(entity));
        if (ctorCall) {
            ctorCall->callKind = CallKind::CALL_OBJECT_CREATION;
            ctorCall->ty = mirrorTy;
        }
        return ctorCall;
    }
    CJC_ABORT();
    return nullptr;
}

// handle.$obj.isNull()
// tmp = handle.$obj
// return tmp.isNull()
OwnedPtr<Expr> ASTFactory::CreateGetObjcEntityOrNullCall(VarDecl &entity, Ptr<File> file)
{
    // CPointer<Unit>()
    auto baseTy = typeManager.GetPointerTy(typeManager.GetInvalidTy());
    auto extendTy = typeManager.GetTyForExtendMap(*baseTy);
    auto extends = typeManager.GetBuiltinTyExtends(*extendTy);
    CJC_ASSERT(extends.size() != 0);
    auto members = (extends.begin())->get();
    Ptr<Decl> isNullMember;
    for (auto m : members->GetMemberDeclPtrs()) {
        if (m->identifier.GetRawText() == "isNull") {
            isNullMember = m;
            break;
        }
    }
    CJC_ASSERT(isNullMember);

    auto isNullAccessMa = CreateMemberAccess(CreateRefExpr(entity), *isNullMember);
    auto isNullAccess = WithinFile(std::move(isNullAccessMa), file);
    isNullAccess->ty = bridge.GetNativeObjCIdTy();
    isNullAccess->begin = entity.begin;
    isNullAccess->curFile = entity.curFile;

    auto resultCall = CreateCallExpr(std::move(isNullAccess), {},
        nullptr, typeManager.GetBoolTy(), CallKind::CALL_DECLARED_FUNCTION);
    auto result = WithinFile(std::move(resultCall), file);
    result->ty = typeManager.GetBoolTy();
    return result;
}

OwnedPtr<VarDecl> ASTFactory::CreateNativeHandleField(ClassDecl& target)
{
    auto nativeHandleTy = bridge.GetNativeObjCIdTy();

    auto nativeHandleField = CreateVarDecl(NATIVE_HANDLE_IDENT, nullptr, CreateType(nativeHandleTy));
    nativeHandleField->ty = nativeHandleTy;
    // mark it initialized because sema initialization analysis is run before
    // objc desugaring
    nativeHandleField->EnableAttr(Attribute::PUBLIC, Attribute::INITIALIZED);

    PutDeclToClassLikeBody(*nativeHandleField, target);

    return nativeHandleField;
}

OwnedPtr<FuncDecl> ASTFactory::CreateInitCjObject(const Decl& target, FuncDecl& ctor, bool generateForOneWayMapping)
{
    auto curFile = ctor.curFile;
    auto registryIdTy = bridge.GetRegistryIdTy();

    auto wrapperParamList = MakeOwned<FuncParamList>();
    auto& wrapperParams = wrapperParamList->params;
    auto& ctorParams = ctor.funcBody->paramLists[0]->params;
    std::transform(ctorParams.begin(), ctorParams.end(), std::back_inserter(wrapperParams),
        [this](auto& p) { return CreateFuncParam(p->identifier.Val(), nullptr, nullptr, typeMapper.Cj2CType(p->ty)); });

    std::vector<Ptr<Ty>> wrapperParamTys;
    std::transform(
        wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys), [](auto& p) { return p->ty; });

    auto wrapperTy = typeManager.GetFunctionTy(wrapperParamTys, registryIdTy, {.isC = true});

    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    std::vector<OwnedPtr<FuncArg>> ctorCallArgs;
    size_t argIdx = 0;
    if (!generateForOneWayMapping) {
        auto objParamRef = CreateRefExpr(*wrapperParams[0]);
        ctorCallArgs.emplace_back(CreateFuncArg(std::move(objParamRef)));
        // skip first param, as it is needed only for restore @ObjCImpl instance.
        argIdx = 1;
    }
    while (argIdx < ctorParams.size()) {
        auto& wrapperParam = wrapperParams[argIdx];
        auto paramRef = WithinFile(CreateRefExpr(*wrapperParam), curFile);
        OwnedPtr<FuncArg> ctorCallArg = CreateFuncArg(WrapEntity(std::move(paramRef), *ctorParams[argIdx]->ty));
        ctorCallArgs.emplace_back(std::move(ctorCallArg));
        ++argIdx;
    }

    auto ctorCall = CreateCallExpr(
        CreateRefExpr(ctor), std::move(ctorCallArgs), Ptr(&ctor), target.ty, CallKind::CALL_OBJECT_CREATION);
    auto putToRegistryCall = CreatePutToRegistryCall(std::move(ctorCall));

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), CreateType(registryIdTy),
        CreateBlock(Nodes<Node>(std::move(putToRegistryCall)), registryIdTy), wrapperTy);

    auto wrapperName = nameGenerator.GenerateInitCjObjectName(ctor);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), wrapperTy);
    wrapper->moduleName = ctor.moduleName;
    wrapper->fullPackageName = ctor.fullPackageName;
    wrapper->EnableAttr(Attribute::C, Attribute::GLOBAL, Attribute::PUBLIC, Attribute::NO_MANGLE);
    wrapper->funcBody->funcDecl = wrapper.get();
    PutDeclToFile(*wrapper, *ctor.curFile);

    return wrapper;
}

OwnedPtr<FuncDecl> ASTFactory::CreateDeleteCjObject(Decl& target, bool generateForOneWayMapping)
{
    auto registryIdTy = bridge.GetRegistryIdTy();
    auto param = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, registryIdTy);
    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    auto paramRef = CreateRefExpr(*param);
    auto paramRefTmp = paramRef.get();

    std::vector<Ptr<Ty>> funcParamTys;
    funcParamTys.emplace_back(param->ty);
    auto funcTy = typeManager.GetFunctionTy(std::move(funcParamTys), unitTy, {.isC = true});

    auto removeFromRegistryCall = CreateRemoveFromRegistryCall(std::move(paramRef));

    std::vector<OwnedPtr<Node>> funcNodes;
    if (!generateForOneWayMapping) {
        auto getFromRegistryCall = CreateGetFromRegistryByIdCall(
            ASTCloner::Clone(paramRefTmp), CreateRefType(static_cast<InheritableDecl&>(target)));
        auto objTmpVarDecl =
            CreateTmpVarDecl(CreateRefType(static_cast<InheritableDecl&>(target)), std::move(getFromRegistryCall));
        auto nativeHandleExpr = CreateMemberAccess(CreateRefExpr(*objTmpVarDecl), NATIVE_HANDLE_IDENT);
        auto releaseCall = CreateObjCReleaseCall(std::move(nativeHandleExpr));

        funcNodes.emplace_back(std::move(objTmpVarDecl));
        funcNodes.emplace_back(std::move(removeFromRegistryCall));
        funcNodes.emplace_back(std::move(releaseCall));
    } else {
        funcNodes.emplace_back(std::move(removeFromRegistryCall));
    }

    std::vector<OwnedPtr<FuncParam>> funcParams;
    funcParams.emplace_back(std::move(param));
    auto paramList = CreateFuncParamList(std::move(funcParams));
    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.emplace_back(std::move(paramList));

    auto funcBody = CreateFuncBody(std::move(paramLists), Native::FFI::CreateUnitType(target.curFile),
        CreateBlock(std::move(funcNodes), unitTy), funcTy);

    auto funcName = nameGenerator.GenerateDeleteCjObjectName(target);

    auto ret = CreateFuncDecl(funcName, std::move(funcBody), funcTy);
    ret->moduleName = target.moduleName;
    ret->fullPackageName = target.fullPackageName;
    ret->EnableAttr(Attribute::C, Attribute::GLOBAL, Attribute::PUBLIC, Attribute::NO_MANGLE);
    ret->funcBody->funcDecl = ret.get();
    PutDeclToFile(*ret, *target.curFile);

    return ret;
}

OwnedPtr<FuncDecl> ASTFactory::CreateMethodWrapper(FuncDecl& method)
{
    auto outerDecl = static_cast<InheritableDecl*>(method.outerDecl.get());
    CJC_NULLPTR_CHECK(outerDecl);

    auto wrapperParamList = MakeOwned<FuncParamList>();
    auto& wrapperParams = wrapperParamList->params;

    OwnedPtr<VarDecl> objTmpVarDecl;
    OwnedPtr<MemberAccess> methodExpr;
    if (method.TestAttr(Attribute::STATIC)) {
        methodExpr = CreateMemberAccess(WithinFile(CreateRefExpr(*method.outerDecl), method.curFile), method);
    } else {
        auto registryIdTy = bridge.GetRegistryIdTy();
        auto registryIdParam = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, registryIdTy);
        auto registryIdParamRef = CreateRefExpr(*registryIdParam);
        wrapperParams.emplace_back(std::move(registryIdParam));
        auto getFromRegistryCall =
            CreateGetFromRegistryByIdCall(std::move(registryIdParamRef), CreateRefType(*outerDecl));
        objTmpVarDecl = CreateTmpVarDecl(CreateRefType(*outerDecl), std::move(getFromRegistryCall));
        methodExpr = CreateMemberAccess(CreateRefExpr(*objTmpVarDecl), method);
    }

    auto& originParams = method.funcBody->paramLists[0]->params;
    std::transform(originParams.begin(), originParams.end(), std::back_inserter(wrapperParams), [this](auto& p) {
        auto convertedParamTy = typeMapper.Cj2CType(p->ty);
        return CreateFuncParam(p->identifier.GetRawText() + (typeMapper.IsObjCCJMapping(*(p->ty)) ? "Id" : ""),
            CreateType(convertedParamTy), nullptr, convertedParamTy);
    });

    std::vector<Ptr<Ty>> wrapperParamTys;
    std::transform(
        wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys), [](auto& p) { return p->ty; });

    auto& retTy = method.funcBody->retType->ty;
    auto retWrapperTy = typeManager.GetFunctionTy(wrapperParamTys, typeMapper.Cj2CType(retTy), {.isC = true});

    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    methodExpr->curFile = method.curFile;
    methodExpr->begin = method.GetBegin();
    methodExpr->end = method.GetEnd();

    std::vector<OwnedPtr<FuncArg>> methodArgs;
    // check if need to skip first param, as it is needed only for restore @ObjCImpl instance in non-static method.
    size_t index = method.TestAttr(Attribute::STATIC) ? 0 : 1;
    for (size_t i = index; i < wrapperParams.size(); ++i) {
        auto wrapperParam = wrapperParams[i].get();
        auto originParam = originParams[i - index].get();

        auto paramRef = CreateRefExpr(*wrapperParam);
        auto wrappedParamRef = WrapEntity(std::move(paramRef), *originParam->ty);
        auto arg = CreateFuncArg(std::move(wrappedParamRef), wrapperParam->identifier, originParam->ty);
        methodArgs.emplace_back(std::move(arg));
    }

    auto methodCall = CreateCallExpr(std::move(methodExpr), std::move(methodArgs), Ptr(&method),
        method.funcBody->retType->ty, CallKind::CALL_DECLARED_FUNCTION);

    std::vector<OwnedPtr<Node>> wrapperNodes;
    if (!method.TestAttr(Attribute::STATIC)) {
        wrapperNodes.emplace_back(std::move(objTmpVarDecl));
    }
    wrapperNodes.emplace_back(UnwrapEntity(std::move(methodCall)));

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), CreateType(retWrapperTy->retTy),
        CreateBlock(std::move(wrapperNodes), retWrapperTy->retTy), retWrapperTy);

    auto wrapperName = nameGenerator.GenerateMethodWrapperName(method);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), retWrapperTy);
    wrapper->moduleName = method.moduleName;
    wrapper->fullPackageName = method.fullPackageName;
    wrapper->EnableAttr(Attribute::C, Attribute::GLOBAL, Attribute::PUBLIC, Attribute::NO_MANGLE);
    wrapper->funcBody->funcDecl = wrapper.get();

    PutDeclToFile(*wrapper, *method.curFile);

    return wrapper;
}

OwnedPtr<FuncDecl> ASTFactory::CreateGetterWrapper(PropDecl& prop)
{
    auto outerDecl = static_cast<InheritableDecl*>(prop.outerDecl.get());
    CJC_NULLPTR_CHECK(outerDecl);

    auto wrapperParamList = MakeOwned<FuncParamList>();
    auto& wrapperParams = wrapperParamList->params;

    OwnedPtr<VarDecl> objTmpVarDecl;
    OwnedPtr<MemberAccess> propGetterExpr;
    if (prop.TestAttr(Attribute::STATIC)) {
        propGetterExpr =
            CreateMemberAccess(WithinFile(CreateRefExpr(*outerDecl), prop.curFile), *prop.getters[0].get());
    } else {
        auto registryIdTy = bridge.GetRegistryIdTy();
        auto registryIdParam = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, registryIdTy);
        auto registryIdParamRef = CreateRefExpr(*registryIdParam);
        wrapperParams.emplace_back(std::move(registryIdParam));
        auto getFromRegistryCall =
            CreateGetFromRegistryByIdCall(std::move(registryIdParamRef), CreateRefType(*outerDecl));
        objTmpVarDecl = CreateTmpVarDecl(CreateRefType(*outerDecl), std::move(getFromRegistryCall));
        // Not sure if accessing the first getter is good
        propGetterExpr = CreateMemberAccess(CreateRefExpr(*objTmpVarDecl), *prop.getters[0].get());
    }
    propGetterExpr->curFile = prop.curFile;
    propGetterExpr->begin = prop.GetBegin();
    propGetterExpr->end = prop.GetEnd();
    auto propGetterCall = CreateCallExpr(
        std::move(propGetterExpr), {}, Ptr(prop.getters[0].get()), prop.ty, CallKind::CALL_DECLARED_FUNCTION);

    std::vector<Ptr<Ty>> wrapperParamTys;
    std::transform(
        wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys), [](auto& p) { return p->ty; });

    auto wrapperTy = typeManager.GetFunctionTy(wrapperParamTys, typeMapper.Cj2CType(prop.ty), {.isC = true});

    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    std::vector<OwnedPtr<Node>> wrapperNodes;
    if (!prop.TestAttr(Attribute::STATIC)) {
        wrapperNodes.emplace_back(std::move(objTmpVarDecl));
    }
    wrapperNodes.emplace_back(UnwrapEntity(std::move(propGetterCall)));

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), CreateType(wrapperTy->retTy),
        CreateBlock(std::move(wrapperNodes), wrapperTy->retTy), wrapperTy);

    auto wrapperName = nameGenerator.GeneratePropGetterWrapperName(prop);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), wrapperTy);
    wrapper->moduleName = prop.moduleName;
    wrapper->fullPackageName = prop.fullPackageName;
    wrapper->EnableAttr(Attribute::C, Attribute::GLOBAL, Attribute::PUBLIC, Attribute::NO_MANGLE);
    wrapper->funcBody->funcDecl = wrapper.get();

    PutDeclToFile(*wrapper, *prop.curFile);

    return wrapper;
}

OwnedPtr<FuncDecl> ASTFactory::CreateSetterWrapper(PropDecl& prop)
{
    auto registryIdTy = bridge.GetRegistryIdTy();
    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);

    auto convertedPropTy = typeMapper.Cj2CType(prop.ty);
    auto setterParam = CreateFuncParam(VALUE_IDENT, CreateType(convertedPropTy), nullptr, convertedPropTy);
    auto setterParamRef = CreateRefExpr(*setterParam);

    auto outerDecl = static_cast<InheritableDecl*>(prop.outerDecl.get());
    CJC_NULLPTR_CHECK(outerDecl);

    auto wrapperParamList = MakeOwned<FuncParamList>();
    auto& wrapperParams = wrapperParamList->params;

    std::vector<OwnedPtr<Node>> wrapperNodes;

    // Not sure if accessing the first setter is good
    auto& setter = *prop.setters[0].get();
    auto& originParams = setter.funcBody->paramLists[0]->params;
    OwnedPtr<Expr> propSetterExpr;

    OwnedPtr<Decl> objTmpVarDecl;

    if (prop.TestAttr(Attribute::STATIC)) {
        wrapperParams.emplace_back(std::move(setterParam));
        propSetterExpr = CreateMemberAccess(WithinFile(CreateRefExpr(*outerDecl), prop.curFile), setter);
        propSetterExpr->curFile = prop.curFile;
        propSetterExpr->begin = prop.GetBegin();
        propSetterExpr->end = prop.GetEnd();
    } else {
        auto registryIdParam = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, registryIdTy);
        auto registryIdParamRef = CreateRefExpr(*registryIdParam);
        auto getFromRegistryCall =
            CreateGetFromRegistryByIdCall(std::move(registryIdParamRef), CreateRefType(*outerDecl));

        objTmpVarDecl = CreateTmpVarDecl(CreateRefType(*outerDecl), std::move(getFromRegistryCall));

        wrapperParams.emplace_back(std::move(registryIdParam));
        wrapperParams.emplace_back(std::move(setterParam));
        propSetterExpr = CreateMemberAccess(CreateRefExpr(*objTmpVarDecl), setter);
        propSetterExpr->curFile = prop.curFile;
        propSetterExpr->begin = prop.GetBegin();
        propSetterExpr->end = prop.GetEnd();
    }
    std::vector<OwnedPtr<FuncArg>> propSetterArgs;
    // setter should have only one parameter
    CJC_ASSERT(wrapperParams.size() <= 2);
    {
        auto wrapperParam = wrapperParams.back().get();
        auto originParam = originParams.back().get();

        auto paramRef = CreateRefExpr(*wrapperParam);
        auto wrappedParamRef = WrapEntity(std::move(paramRef), *originParam->ty);
        auto arg = CreateFuncArg(std::move(wrappedParamRef), wrapperParam->identifier, originParam->ty);
        propSetterArgs.emplace_back(std::move(arg));
    }

    std::vector<Ptr<Ty>> wrapperParamTys;
    std::transform(
        wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys), [](auto& p) { return p->ty; });

    auto wrapperTy = typeManager.GetFunctionTy(wrapperParamTys, unitTy, {.isC = true});

    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    auto propSetterCall = CreateCallExpr(
        std::move(propSetterExpr), std::move(propSetterArgs), Ptr(&setter), unitTy, CallKind::CALL_DECLARED_FUNCTION);

    if (objTmpVarDecl != nullptr) {
        wrapperNodes.emplace_back(std::move(objTmpVarDecl));
    }
    wrapperNodes.emplace_back(std::move(propSetterCall));

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), CreateUnitType(prop.curFile),
        CreateBlock(std::move(wrapperNodes), wrapperTy->retTy), wrapperTy);

    auto wrapperName = nameGenerator.GetPropSetterWrapperName(prop);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), wrapperTy);
    wrapper->moduleName = prop.moduleName;
    wrapper->fullPackageName = prop.fullPackageName;
    wrapper->EnableAttr(Attribute::C, Attribute::GLOBAL, Attribute::PUBLIC, Attribute::NO_MANGLE);
    wrapper->funcBody->funcDecl = wrapper.get();

    PutDeclToFile(*wrapper, *prop.curFile);

    return wrapper;
}

OwnedPtr<FuncDecl> ASTFactory::CreateGetterWrapper(VarDecl& field)
{
    auto outerDecl = static_cast<InheritableDecl*>(field.outerDecl.get());
    CJC_NULLPTR_CHECK(outerDecl);

    auto wrapperParamList = MakeOwned<FuncParamList>();
    auto& wrapperParams = wrapperParamList->params;

    OwnedPtr<VarDecl> objTmpVarDecl;
    OwnedPtr<MemberAccess> fieldExpr;
    if (field.TestAttr(Attribute::STATIC)) {
        fieldExpr = CreateMemberAccess(WithinFile(CreateRefExpr(*outerDecl), field.curFile), field);
    } else {
        auto registryIdTy = bridge.GetRegistryIdTy();
        auto registryIdParam = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, registryIdTy);
        auto registryIdParamRef = CreateRefExpr(*registryIdParam);
        wrapperParams.emplace_back(std::move(registryIdParam));
        auto getFromRegistryCall =
            CreateGetFromRegistryByIdCall(std::move(registryIdParamRef), CreateRefType(*outerDecl));
        objTmpVarDecl = CreateTmpVarDecl(CreateRefType(*outerDecl), std::move(getFromRegistryCall));
        fieldExpr = CreateMemberAccess(CreateRefExpr(*objTmpVarDecl), field);
    }
    fieldExpr->curFile = field.curFile;
    fieldExpr->begin = field.GetBegin();
    fieldExpr->end = field.GetEnd();

    std::vector<Ptr<Ty>> wrapperParamTys;
    std::transform(
        wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys), [](auto& p) { return p->ty; });

    auto wrapperTy = typeManager.GetFunctionTy(wrapperParamTys, typeMapper.Cj2CType(field.ty), {.isC = true});

    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    std::vector<OwnedPtr<Node>> wrapperNodes;
    if (!field.TestAttr(Attribute::STATIC)) {
        wrapperNodes.emplace_back(std::move(objTmpVarDecl));
    }
    wrapperNodes.emplace_back(UnwrapEntity(std::move(fieldExpr)));

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), CreateType(wrapperTy->retTy),
        CreateBlock(std::move(wrapperNodes), wrapperTy->retTy), wrapperTy);

    // Generate wrapper name from ORIGIN field, not a mirror one.
    auto wrapperName = nameGenerator.GetFieldGetterWrapperName(field);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), wrapperTy);
    wrapper->moduleName = field.moduleName;
    wrapper->fullPackageName = field.fullPackageName;
    wrapper->EnableAttr(Attribute::C, Attribute::GLOBAL, Attribute::PUBLIC, Attribute::NO_MANGLE);
    wrapper->funcBody->funcDecl = wrapper.get();

    PutDeclToFile(*wrapper, *field.curFile);

    return wrapper;
}

OwnedPtr<FuncDecl> ASTFactory::CreateSetterWrapper(VarDecl& field)
{
    auto registryIdTy = bridge.GetRegistryIdTy();
    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);

    auto convertedFieldTy = typeMapper.Cj2CType(field.ty);
    auto setterParam = CreateFuncParam(VALUE_IDENT, CreateType(convertedFieldTy), nullptr, convertedFieldTy);
    auto setterParamRef = CreateRefExpr(*setterParam);

    auto outerDecl = static_cast<InheritableDecl*>(field.outerDecl.get());
    CJC_NULLPTR_CHECK(outerDecl);

    auto wrapperParamList = MakeOwned<FuncParamList>();
    auto& wrapperParams = wrapperParamList->params;
    std::vector<OwnedPtr<Node>> wrapperNodes;
    OwnedPtr<MemberAccess> lhs;
    if (field.TestAttr(Attribute::STATIC)) {
        lhs = CreateMemberAccess(WithinFile(CreateRefExpr(*outerDecl), field.curFile), field);
    } else {
        auto registryIdParam = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, registryIdTy);
        auto registryIdParamRef = CreateRefExpr(*registryIdParam);
        wrapperParams.emplace_back(std::move(registryIdParam));

        auto getFromRegistryCall = CreateGetFromRegistryByIdCall(std::move(registryIdParamRef), CreateRefType(*outerDecl));
        auto objTmpVarDecl = CreateTmpVarDecl(CreateRefType(*outerDecl), std::move(getFromRegistryCall));
        lhs = CreateMemberAccess(CreateRefExpr(*objTmpVarDecl), field);
        wrapperNodes.emplace_back(std::move(objTmpVarDecl));
    }
    auto assignFieldExpr = CreateAssignExpr(std::move(lhs), WrapEntity(std::move(setterParamRef), *field.ty), unitTy);
    assignFieldExpr->curFile = field.curFile;
    assignFieldExpr->begin = field.GetBegin();
    assignFieldExpr->end = field.GetEnd();
    wrapperNodes.emplace_back(std::move(assignFieldExpr));

    wrapperParams.emplace_back(std::move(setterParam));
    std::vector<Ptr<Ty>> wrapperParamTys;
    std::transform(
        wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys), [](auto& p) { return p->ty; });

    auto wrapperTy = typeManager.GetFunctionTy(wrapperParamTys, unitTy, {.isC = true});

    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), CreateUnitType(field.curFile),
        CreateBlock(std::move(wrapperNodes), wrapperTy->retTy), wrapperTy);

    // Generate wrapper name from ORIGIN field, not a mirror one.
    auto wrapperName = nameGenerator.GetFieldSetterWrapperName(field);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), wrapperTy);
    wrapper->moduleName = field.moduleName;
    wrapper->fullPackageName = field.fullPackageName;
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

std::set<Ptr<FuncDecl>> ASTFactory::GetAllParentCtors(ClassDecl& target) const
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

OwnedPtr<FuncDecl> ASTFactory::CreateBaseCtorDecl(ClassDecl& target)
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

    PutDeclToClassLikeBody(*ctor, target);

    return ctor;
}

OwnedPtr<FuncDecl> ASTFactory::CreateImplCtor(ClassDecl& target, FuncDecl& from)
{
    auto nativeHandleTy = bridge.GetNativeObjCIdTy();
    auto ctor = ASTCloner::Clone(Ptr(&from));
    auto curFile = ctor->curFile;

    auto& implCtorParams = ctor->funcBody->paramLists[0]->params;

    implCtorParams.insert(implCtorParams.begin(),
        WithinFile(CreateFuncParam(NATIVE_HANDLE_IDENT, CreateType(nativeHandleTy), nullptr, nativeHandleTy), curFile));
    auto& nativeHandleParam = *implCtorParams.front();

    std::vector<Ptr<Ty>> implCtorParamTys;
    std::transform(implCtorParams.begin(), implCtorParams.end(), std::back_inserter(implCtorParamTys),
        [](auto& p) { return p->type->ty; });

    ctor->ty = typeManager.GetFunctionTy(implCtorParamTys, ctor->funcBody->retType->ty);
    ctor->funcBody->ty = ctor->ty;
    ctor->funcBody->funcDecl = ctor.get();
    ctor->constructorCall = ConstructorCall::SUPER;

    CJC_NULLPTR_CHECK(target.GetSuperClassDecl());
    auto parentCtor = GetGeneratedBaseCtor(*target.GetSuperClassDecl());
    auto superCall = WithinFile(CreateSuperCall(*target.GetSuperClassDecl(), *parentCtor, parentCtor->ty), curFile);
    superCall->args.emplace_back(CreateFuncArg(WithinFile(CreateRefExpr(nativeHandleParam), curFile)));

    auto& body = ctor->funcBody->body->body;
    body.erase(std::remove_if(body.begin(), body.end(), [](auto& node) {
        if (auto call = As<ASTKind::CALL_EXPR>(node.get())) {
            return IsSuperConstructorCall(*call);
        }
        return false;
        }), body.end());

    body.insert(body.begin(), std::move(superCall));

    return ctor;
}

bool ASTFactory::IsGeneratedMember(const Decl& decl) const
{
    return IsGeneratedNativeHandleField(decl) || IsGeneratedHasInitedField(decl) || IsGeneratedCtor(decl) ||
        IsGeneratedNativeHandleGetter(decl);
}

bool ASTFactory::IsGeneratedNativeHandleField(const Decl& decl) const
{
    return decl.identifier.Val() == NATIVE_HANDLE_IDENT;
}

bool ASTFactory::IsGeneratedHasInitedField(const Decl& decl) const
{
    return decl.TestAttr(Attribute::HAS_INITED_FIELD);
}

bool ASTFactory::IsGeneratedCtor(const Decl& decl) const
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

bool ASTFactory::IsGeneratedNativeHandleGetter(const Decl& decl) const
{
    return decl.identifier.Val() == NATIVE_HANDLE_GETTER_IDENT;
}

Ptr<FuncDecl> ASTFactory::GetGeneratedBaseCtor(Decl& decl)
{
    CJC_ASSERT(decl.astKind == ASTKind::CLASS_DECL);
    CJC_ASSERT(TypeMapper::IsObjCMirror(*decl.ty) || TypeMapper::IsObjCImpl(*decl.ty) ||
        TypeMapper::IsSyntheticWrapper(*decl.ty));

    for (auto& member : decl.GetMemberDeclPtrs()) {
        if (auto fd = As<ASTKind::FUNC_DECL>(member); fd && IsGeneratedCtor(*fd)) {
            return fd;
        }
    }

    CJC_ABORT();
    return nullptr;
}

Ptr<FuncDecl> ASTFactory::GetGeneratedImplCtor(const Decl& declArg, const FuncDecl& origin)
{
    CJC_ASSERT(origin.TestAttr(Attribute::CONSTRUCTOR));
    CJC_NULLPTR_CHECK(origin.funcBody);

    const auto& originParamLists = origin.funcBody->paramLists;
    CJC_ASSERT(!originParamLists.empty());

    // taking first param list probably is not the best idea
    const auto& originParams = originParamLists[0]->params;

    for (auto& member : declArg.GetMemberDeclPtrs()) {
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

void ASTFactory::PutDeclToClassLikeBody(Decl& decl, ClassLikeDecl& target)
{
    // use generics?
    switch (target.astKind) {
        case ASTKind::INTERFACE_DECL:
            PutDeclToInterfaceBody(decl, *StaticAs<ASTKind::INTERFACE_DECL>(Ptr(&target)));
            break;
        case ASTKind::CLASS_DECL:
            PutDeclToClassBody(decl, *StaticAs<ASTKind::CLASS_DECL>(Ptr(&target)));
            break;
        default:
            // Not supported
            CJC_ABORT();
    }
    decl.outerDecl = &target;
    decl.curFile = target.curFile;
    decl.fullPackageName = target.fullPackageName;
    decl.EnableAttr(Attribute::IN_CLASSLIKE);
}

void ASTFactory::PutDeclToClassBody(Decl& decl, ClassDecl& target)
{
    decl.begin = target.body->end;
    decl.end = target.body->end;
}

void ASTFactory::PutDeclToInterfaceBody(Decl& decl, InterfaceDecl& target)
{
    decl.begin = target.body->end;
    decl.end = target.body->end;
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
    CJC_ASSERT(TypeMapper::IsObjCImpl(*ty) || TypeMapper::IsObjCCJMapping(*ty));

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

OwnedPtr<Expr> ASTFactory::CreateObjCReleaseCall(OwnedPtr<Expr> nativeHandle)
{
    auto releaseDecl = bridge.GetObjCReleaseDecl();
    auto releaseExpr = CreateRefExpr(*releaseDecl);

    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(nativeHandle)));
    return CreateCallExpr(std::move(releaseExpr), std::move(args), nullptr,
        TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT), CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<Expr> ASTFactory::CreateWithMethodEnvScope(OwnedPtr<Expr> nativeHandle, Ptr<Ty> retTy,
    std::function<std::vector<OwnedPtr<Node>>(OwnedPtr<Expr>, OwnedPtr<Expr>)> bodyFactory)
{
    CJC_ASSERT(typeMapper.IsObjCCompatible(*retTy));

    Ptr<FuncDecl> withMethodEnvDecl;
    OwnedPtr<RefExpr> withMethodEnvRef;
    if (retTy->IsPrimitive() || typeMapper.IsObjCPointer(*retTy)) {
        withMethodEnvDecl = bridge.GetWithMethodEnvDecl();
        auto unwrappedTy = typeMapper.Cj2CType(retTy);
        withMethodEnvRef = CreateRefExpr(*withMethodEnvDecl);
        withMethodEnvRef->instTys.emplace_back(unwrappedTy);
        withMethodEnvRef->ty = typeManager.GetInstantiatedTy(
            withMethodEnvDecl->ty, GenerateTypeMapping(*withMethodEnvDecl, withMethodEnvRef->instTys));
        withMethodEnvRef->typeArguments.emplace_back(CreateType(unwrappedTy));
    } else {
        withMethodEnvDecl = bridge.GetWithMethodEnvObjDecl();
        withMethodEnvRef = CreateRefExpr(*withMethodEnvDecl);
    }
    auto receiverParam =
        WithinFile(CreateFuncParam("receiver", nullptr, nullptr, bridge.GetNativeObjCIdTy()), nativeHandle->curFile);
    auto receiverRef = WithinFile(CreateRefExpr(*receiverParam), nativeHandle->curFile);

    auto objCSuperParam = WithinFile(
        CreateFuncParam("objCSuper", nullptr, nullptr, bridge.GetNativeObjCSuperPtrTy()), nativeHandle->curFile);
    auto objCSuperRef = WithinFile(CreateRefExpr(*objCSuperParam), nativeHandle->curFile);

    auto actionParams = Nodes<FuncParam>(std::move(receiverParam), std::move(objCSuperParam));
    auto args = Nodes<FuncArg>(CreateFuncArg(std::move(nativeHandle)),
        CreateFuncArg(WrapReturningLambdaExpr(
            typeManager, bodyFactory(std::move(receiverRef), std::move(objCSuperRef)), std::move(actionParams))));

    auto realRetTy = StaticCast<FuncTy>(withMethodEnvRef->ty)->retTy;
    return CreateCallExpr(
        std::move(withMethodEnvRef), std::move(args), withMethodEnvDecl, realRetTy, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> ASTFactory::CreateObjCMsgSendCall(
    Ptr<FuncTy> ty, OwnedPtr<FuncType> funcType, std::vector<OwnedPtr<Expr>> funcArgs)
{
    auto msgSendDecl = bridge.GetObjCMsgSendDecl();
    auto msgSendExpr = CreateCallExpr(CreateRefExpr(*msgSendDecl), {}, nullptr, msgSendDecl->funcBody->retType->ty);
    auto retType = funcType->retType.get();

    auto cFuncDecl = importManager.GetCoreDecl<BuiltInDecl>(std::string(CFUNC_NAME));
    CJC_NULLPTR_CHECK(cFuncDecl);
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

OwnedPtr<CallExpr> ASTFactory::CreateObjCMsgSendCall(
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

    return CreateObjCMsgSendCall(fty, std::move(ft), std::move(args));
}

OwnedPtr<Expr> ASTFactory::CreateGetClassCall(ClassTy& ty, Ptr<File> curFile)
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

OwnedPtr<CallExpr> ASTFactory::CreateAllocCall(Decl& decl, Ptr<File> curFile)
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
    return CreateObjCMsgSendCall(
        std::move(nativeHandle), objcname, typeMapper.Cj2CType(StaticCast<FuncTy>(fd.ty)->retTy), std::move(rawArgs));
}

OwnedPtr<Expr> ASTFactory::CreateMethodCallViaMsgSend(FuncDecl& fd, OwnedPtr<Expr> handle)
{
    CJC_NULLPTR_CHECK(fd.outerDecl);
    CJC_ASSERT(TypeMapper::IsObjCCompatible(*fd.outerDecl->ty));
    CJC_NULLPTR_CHECK(fd.funcBody);
    CJC_ASSERT(!fd.funcBody->paramLists.empty());

    auto curFile = handle->curFile;
    auto& params = fd.funcBody->paramLists[0]->params;

    std::vector<OwnedPtr<Expr>> args;
    std::transform(params.begin(), params.end(), std::back_inserter(args), [this ,curFile](auto& param) {
        auto unwrapped = UnwrapEntity(WithinFile(CreateRefExpr(*param), curFile));
        return unwrapped;
    });

    return CreateMethodCallViaMsgSend(fd, std::move(handle), std::move(args));
}

OwnedPtr<Expr> ASTFactory::CreateAllocInitCall(FuncDecl& fd)
{
    // object instantiation is allowed only for:
    // - init constructor
    // - @ObjCInit method
    CJC_ASSERT(fd.TestAttr(Attribute::CONSTRUCTOR) || IsStaticInitMethod(fd));
    CJC_NULLPTR_CHECK(fd.outerDecl);
    CJC_ASSERT(fd.outerDecl->astKind == ASTKind::CLASS_DECL);
    auto& mirror = *StaticAs<ASTKind::CLASS_DECL>(fd.outerDecl);
    auto curFile = mirror.curFile;

    auto allocCall = CreateAllocCall(mirror, curFile);
    return CreateMethodCallViaMsgSend(fd, std::move(allocCall));
}

OwnedPtr<Expr> ASTFactory::CreatePropGetterCallViaMsgSend(PropDecl& pd, OwnedPtr<Expr> nativeHandle)
{
    auto objcname = nameGenerator.GetObjCDeclName(pd);
    return CreateObjCMsgSendCall(std::move(nativeHandle), objcname, typeMapper.Cj2CType(pd.ty), {});
}

OwnedPtr<Expr> ASTFactory::CreatePropSetterCallViaMsgSend(PropDecl& pd, OwnedPtr<Expr> nativeHandle, OwnedPtr<Expr> arg)
{
    auto objcname = nameGenerator.GetObjCDeclName(pd);
    std::transform(
        objcname.begin(), objcname.begin() + 1, objcname.begin(), [](unsigned char c) { return std::toupper(c); });
    objcname = "set" + objcname + ":";
    return CreateObjCMsgSendCall(
        std::move(nativeHandle), objcname, typeMapper.Cj2CType(pd.ty), Nodes<Expr>(std::move(arg)));
}

OwnedPtr<Expr> ASTFactory::CreateFuncCallViaOpaquePointer(
    OwnedPtr<Expr> ptr,
    Ptr<Ty> retTy,
    std::vector<OwnedPtr<Expr>> args)
{
    std::vector<Ptr<Ty>> argTys;
    for (auto& rawArg : args) {
        argTys.push_back(rawArg->ty);
    }
    auto ty = typeManager.GetFunctionTy(
        argTys,
        retTy,
        { .isC = true }
    );
    auto cFuncDecl = importManager.GetCoreDecl<BuiltInDecl>(std::string(CFUNC_NAME));
    auto cFuncRefExpr = CreateRefExpr(*cFuncDecl);

    cFuncRefExpr->ty = ty;
    cFuncRefExpr->instTys.emplace_back(ty);

    // CFunc<...>(ptr)

    auto cFuncCallExpr = CreateCallExpr(std::move(cFuncRefExpr), Nodes<FuncArg>(CreateFuncArg(std::move(ptr))),
        nullptr, ty, CallKind::CALL_FUNCTION_PTR);

    std::vector<OwnedPtr<FuncArg>> actualArgs;
    std::transform(args.begin(), args.end(), std::back_inserter(actualArgs),
        [](auto&& argExpr) { return CreateFuncArg(std::move(argExpr)); });

    // CFunc<...>(ptr)(...)
    return CreateCallExpr(
        std::move(cFuncCallExpr), std::move(actualArgs), nullptr, retTy, CallKind::CALL_FUNCTION_PTR);
}

OwnedPtr<Expr> ASTFactory::CreateAutoreleasePoolScope(Ptr<Ty> ty, std::vector<OwnedPtr<Node>> actions)
{
    CJC_ASSERT(typeMapper.IsObjCCompatible(*ty));
    CJC_ASSERT(!actions.empty());

    Ptr<FuncDecl> arpdecl;
    OwnedPtr<RefExpr> arpref;
    if (typeMapper.IsObjCObjectType(*ty) || typeMapper.IsObjCBlock(*ty)) {
        arpdecl = bridge.GetWithAutoreleasePoolObjDecl();
        arpref = CreateRefExpr(*arpdecl);
    } else {
        arpdecl = bridge.GetWithAutoreleasePoolDecl();
        auto unwrappedTy = typeMapper.Cj2CType(ty);
        arpref = CreateRefExpr(*arpdecl);
        arpref->instTys.emplace_back(unwrappedTy);
        arpref->ty = typeManager.GetInstantiatedTy(arpdecl->ty, GenerateTypeMapping(*arpdecl, arpref->instTys));
        arpref->typeArguments.emplace_back(CreateType(unwrappedTy));
    }

    auto args = Nodes<FuncArg>(CreateFuncArg(WrapReturningLambdaExpr(typeManager, std::move(actions))));

    auto retTy = StaticCast<FuncTy>(arpref->ty)->retTy;
    return CreateCallExpr(std::move(arpref), std::move(args), arpdecl, retTy, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> ASTFactory::CreateGetInstanceVariableCall(const PropDecl& field, OwnedPtr<Expr> nativeHandle)
{
    Ptr<FuncDecl> getInstVarDecl;
    OwnedPtr<RefExpr> getInstVarRef;
    if (field.ty->IsPrimitive()) {
        getInstVarDecl = bridge.GetGetInstanceVariableDecl();
        getInstVarRef = CreateRefExpr(*getInstVarDecl);

        getInstVarRef->instTys.emplace_back(field.ty);
        getInstVarRef->ty = typeManager.GetInstantiatedTy(
            getInstVarDecl->ty, GenerateTypeMapping(*getInstVarDecl, getInstVarRef->instTys));
        getInstVarRef->typeArguments.emplace_back(CreateType(field.ty));
    } else {
        getInstVarDecl = bridge.GetGetInstanceVariableObjDecl();
        getInstVarRef = CreateRefExpr(*getInstVarDecl);
    }

    auto objcname = nameGenerator.GetObjCDeclName(field);
    auto nameExpr =
        WithinFile(CreateLitConstExpr(LitConstKind::STRING, objcname, GetStringDecl(importManager).ty), field.curFile);

    auto args = Nodes<FuncArg>(CreateFuncArg(std::move(nativeHandle)), CreateFuncArg(std::move(nameExpr)));

    auto retTy = StaticCast<FuncTy>(getInstVarRef->ty)->retTy;
    return CreateCallExpr(
        std::move(getInstVarRef), std::move(args), getInstVarDecl, retTy, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> ASTFactory::CreateSetInstanceVariableCall(
    const PropDecl& field, OwnedPtr<Expr> nativeHandle, OwnedPtr<Expr> value)
{
    Ptr<FuncDecl> setInstVarDecl;
    OwnedPtr<RefExpr> setInstVarRef;
    if (field.ty->IsPrimitive()) {
        setInstVarDecl = bridge.GetSetInstanceVariableDecl();
        setInstVarRef = CreateRefExpr(*setInstVarDecl);

        setInstVarRef->instTys.emplace_back(field.ty);
        setInstVarRef->ty = typeManager.GetInstantiatedTy(
            setInstVarDecl->ty, GenerateTypeMapping(*setInstVarDecl, setInstVarRef->instTys));
        setInstVarRef->typeArguments.emplace_back(CreateType(field.ty));
    } else {
        setInstVarDecl = bridge.GetSetInstanceVariableObjDecl();
        setInstVarRef = CreateRefExpr(*setInstVarDecl);
    }

    auto objcname = nameGenerator.GetObjCDeclName(field);
    auto nameExpr =
        WithinFile(CreateLitConstExpr(LitConstKind::STRING, objcname, GetStringDecl(importManager).ty), field.curFile);

    auto args = Nodes<FuncArg>(
        CreateFuncArg(std::move(nativeHandle)), CreateFuncArg(std::move(nameExpr)), CreateFuncArg(std::move(value)));

    return CreateCallExpr(std::move(setInstVarRef), std::move(args), setInstVarDecl,
        TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT), CallKind::CALL_DECLARED_FUNCTION);
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

void ASTFactory::SetDesugarExpr(Ptr<Expr> original, OwnedPtr<Expr> desugared) {
    original->desugarExpr = std::move(desugared);
    original->desugarExpr->sourceExpr = original;
    CopyBasicInfo(original, original->desugarExpr);
    AddCurFile(*original->desugarExpr, original->curFile);
}

OwnedPtr<FuncDecl> ASTFactory::CreateFinalizer(ClassDecl& target)
{
    static auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    auto fbody = CreateFuncBody({}, nullptr, CreateBlock({}, unitTy), unitTy);
    fbody->paramLists.emplace_back(MakeOwned<FuncParamList>());
    auto nativeHandleExpr = CreateNativeHandleFieldExpr(target);
    auto releaseCall = CreateObjCReleaseCall(std::move(nativeHandleExpr));
    fbody->body->body.emplace_back(std::move(releaseCall));
    auto fd = CreateFuncDecl(FINALIZER_IDENT, std::move(fbody), typeManager.GetFunctionTy({}, unitTy));
    PutDeclToClassLikeBody(*fd, target);
    fd->EnableAttr(Attribute::PRIVATE, Attribute::FINALIZER);
    fd->linkage = Linkage::EXTERNAL;
    fd->funcBody->funcDecl = fd.get();

    return fd;
}

OwnedPtr<VarDecl> ASTFactory::CreateHasInitedField(ClassDecl& target)
{
    static auto boolTy = typeManager.GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    auto initializer = CreateLitConstExpr(LitConstKind::BOOL, "false", boolTy);
    auto ret = CreateVarDecl(HAS_INITED_IDENT, std::move(initializer));
    ret->isVar = true;
    PutDeclToClassLikeBody(*ret, target);
    ret->EnableAttr(Attribute::PRIVATE, Attribute::NO_REFLECT_INFO, Attribute::HAS_INITED_FIELD);

    return ret;
}

OwnedPtr<FuncDecl> ASTFactory::CreateNativeHandleGetterDecl(ClassLikeDecl& target)
{
    static auto nativeObjCIdTy = bridge.GetNativeObjCIdTy();
    std::vector<OwnedPtr<FuncParam>> getterParams;
    auto getterParamList = CreateFuncParamList(std::move(getterParams));
    auto getterFuncBody = CreateFuncBody(Nodes<FuncParamList>(std::move(getterParamList)), CreateType(nativeObjCIdTy),
        CreateBlock({}, nativeObjCIdTy), nativeObjCIdTy);
    auto getterDecl = CreateFuncDecl(
        NATIVE_HANDLE_GETTER_IDENT, std::move(getterFuncBody), typeManager.GetFunctionTy({}, nativeObjCIdTy));
    getterDecl->funcBody->funcDecl = getterDecl.get();
    getterDecl->funcBody->parentClassLike = &target;
    getterDecl->EnableAttr(Attribute::PUBLIC, Attribute::INITIALIZED, Attribute::IS_CHECK_VISITED);
    PutDeclToClassLikeBody(*getterDecl, target);

    return getterDecl;
}

OwnedPtr<Expr> ASTFactory::CreateNativeHandleFieldExpr(ClassDecl& target)
{
    auto nativeHandleDecl = GetNativeVarHandle(target);
    CJC_NULLPTR_CHECK(nativeHandleDecl);
    return CreateMemberAccess(CreateThisRef(&target, target.ty, target.curFile), *nativeHandleDecl);
}

OwnedPtr<Expr> ASTFactory::CreateMethodCallViaMsgSendSuper(
    FuncDecl& fd, OwnedPtr<Expr> receiver, OwnedPtr<Expr> objCSuper, std::vector<OwnedPtr<Expr>> rawArgs)
{
    auto objCName = nameGenerator.GetObjCDeclName(fd);
    return CreateObjCMsgSendSuperCall(std::move(receiver), std::move(objCSuper), objCName,
        typeMapper.Cj2CType(StaticCast<FuncTy>(fd.ty)->retTy), std::move(rawArgs));
}

OwnedPtr<Expr> ASTFactory::CreatePropGetterCallViaMsgSendSuper(
    PropDecl& pd, OwnedPtr<Expr> receiver, OwnedPtr<Expr> objCSuper)
{
    auto objCName = nameGenerator.GetObjCDeclName(pd);
    return CreateObjCMsgSendSuperCall(
        std::move(receiver), std::move(objCSuper), objCName, typeMapper.Cj2CType(pd.ty), {});
}

OwnedPtr<Expr> ASTFactory::CreatePropSetterCallViaMsgSendSuper(
    PropDecl& pd, OwnedPtr<Expr> receiver, OwnedPtr<Expr> objCSuper, OwnedPtr<Expr> value)
{
    auto objCName = nameGenerator.GetObjCDeclName(pd);
    std::transform(
        objCName.begin(), objCName.begin() + 1, objCName.begin(), [](unsigned char c) { return std::toupper(c); });
    objCName = "set" + objCName + ":";
    return CreateObjCMsgSendSuperCall(
        std::move(receiver), std::move(objCSuper), objCName, typeMapper.Cj2CType(pd.ty), Nodes<Expr>(std::move(value)));
}

OwnedPtr<CallExpr> ASTFactory::CreateObjCMsgSendSuperCall(OwnedPtr<Expr> receiver, OwnedPtr<Expr> objCSuper,
    const std::string& selector, Ptr<Ty> retTy, std::vector<OwnedPtr<Expr>> rawArgs)
{
    auto selCall = CreateRegisterNameCall(selector, receiver->curFile);

    auto ft = MakeOwned<FuncType>();
    ft->isC = true;
    ft->retType = CreateType(retTy);

    rawArgs.insert(rawArgs.begin(), ASTCloner::Clone(selCall.get()));
    rawArgs.insert(rawArgs.begin(), std::move(receiver));
    std::vector<Ptr<Ty>> paramTys;
    for (auto& param : rawArgs) {
        ft->paramTypes.emplace_back(CreateType(param->ty));
        paramTys.emplace_back(param->ty);
    }

    auto fty = typeManager.GetFunctionTy(paramTys, retTy, {.isC = true});
    ft->ty = fty;
    return CreateObjCMsgSendSuperCall(
        std::move(objCSuper), std::move(selCall), std::move(fty), std::move(ft), std::move(rawArgs));
}

OwnedPtr<CallExpr> ASTFactory::CreateObjCMsgSendSuperCall(OwnedPtr<Expr> objCSuper, OwnedPtr<Expr> sel, Ptr<FuncTy> ty,
    OwnedPtr<FuncType> funcType, std::vector<OwnedPtr<Expr>> funcArgs)
{
    auto msgSendSuperDecl = bridge.GetObjCMsgSendSuperDecl();
    // objCMsgSendSuper(objc_super*, SEL)
    auto msgSendSuperExpr = CreateCallExpr(CreateRefExpr(*msgSendSuperDecl),
        Nodes<FuncArg>(CreateFuncArg(std::move(objCSuper)), CreateFuncArg(std::move(sel))), nullptr,
        msgSendSuperDecl->funcBody->retType->ty, CallKind::CALL_DECLARED_FUNCTION);
    auto retType = funcType->retType.get();

    auto cFuncDecl = importManager.GetCoreDecl<BuiltInDecl>(std::string(CFUNC_NAME));
    CJC_NULLPTR_CHECK(cFuncDecl);
    auto cFuncRefExpr = CreateRefExpr(*cFuncDecl);

    cFuncRefExpr->ty = ty;
    cFuncRefExpr->typeArguments.emplace_back(std::move(funcType));

    // CFunc<...>(msgSendSuper)
    auto cFuncCallExpr = CreateCallExpr(std::move(cFuncRefExpr),
        Nodes<FuncArg>(CreateFuncArg(std::move(msgSendSuperExpr))), nullptr, ty, CallKind::CALL_FUNCTION_PTR);

    std::vector<OwnedPtr<FuncArg>> msgSendSuperCallArgs;
    std::transform(funcArgs.begin(), funcArgs.end(), std::back_inserter(msgSendSuperCallArgs),
        [](auto&& argExpr) { return CreateFuncArg(std::move(argExpr)); });

    // CFunc<...>(msgSendSuper)(...)
    return CreateCallExpr(
        std::move(cFuncCallExpr), std::move(msgSendSuperCallArgs), nullptr, retType->ty, CallKind::CALL_FUNCTION_PTR);
}
