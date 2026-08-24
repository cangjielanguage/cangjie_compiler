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
#include "TypeMapper.h"
#include "cangjie/AST/AttributePack.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/ConstantsUtils.h"
#include "cangjie/Utils/SafePointer.h"
#include "cangjie/AST/ASTCasting.h"

using namespace Cangjie::AST;
using namespace Cangjie::TypeCheckUtil;
using namespace Cangjie::Interop::ObjC;
using namespace Cangjie::Native::FFI;

namespace {

constexpr auto VALUE_IDENT = "value";
constexpr auto FINALIZER_IDENT = "~init";
constexpr auto GET_POINTER_ADDRESS_INTRINSIC = "getPointerAddress";

} // namespace

std::vector<OwnedPtr<FuncParamList>> ASTFactory::CreateParamLists(std::vector<OwnedPtr<FuncParam>>&& params)
{
    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.emplace_back(CreateFuncParamList(std::move(params)));
    return paramLists;
}

std::vector<OwnedPtr<FuncParam>>& ASTFactory::GetParams(const FuncDecl& fn)
{
    auto& fnBody = fn.funcBody;
    CJC_NULLPTR_CHECK(fnBody);
    auto& paramLists = fnBody->paramLists;
    CJC_ASSERT(paramLists.size() == 1);
    return paramLists[0]->params;
}

OwnedPtr<VarDecl> ASTFactory::CreateVar(const std::string& name, ModalTy ty, bool isVar, OwnedPtr<Expr> initializer)
{
    auto ret = MakeOwned<VarDecl>();
    ret->identifier = name;
    ret->SetTy(ty);
    ret->isVar = isVar;
    ret->EnableAttr(Attribute::COMPILER_ADD, Attribute::IMPLICIT_ADD, Attribute::NO_REFLECT_INFO);
    if (initializer) {
        ret->initializer = std::move(initializer);
    }
    ret->toBeCompiled = true;
    return ret;
}

OwnedPtr<FuncDecl> ASTFactory::CreateFunc(const std::string& name, Ptr<FuncTy> fnTy,
    std::vector<OwnedPtr<FuncParam>>&& params, std::vector<OwnedPtr<Node>>&& nodes)
{
    auto retTy = fnTy->retTy;
    auto ty = nodes.empty() ? retTy : nodes.back()->GetTy();
    auto body = Cangjie::AST::CreateBlock(std::move(nodes), ty);
    auto fnBody = Cangjie::AST::CreateFuncBody(
        CreateParamLists(std::move(params)), CreateType(retTy.Ty()), std::move(body), retTy);
    auto fn = Cangjie::AST::CreateFuncDecl(name, std::move(fnBody), {fnTy});
    fn->funcBody->funcDecl = fn.get();
    return fn;
}

OwnedPtr<ParenExpr> ASTFactory::CreateParenExpr(OwnedPtr<Expr> expr)
{
    auto parenExpr = MakeOwnedNode<ParenExpr>();
    parenExpr->SetTy(expr->GetTy());
    parenExpr->expr = std::move(expr);
    return std::move(parenExpr);
}

OwnedPtr<Expr> ASTFactory::CreateNativeHandleExpr(OwnedPtr<Expr> entity)
{
    CJC_ASSERT(typeMapper.IsObjCMirror(*entity->GetTy()) || typeMapper.IsObjCImpl(*entity->GetTy()) ||
        typeMapper.IsSyntheticWrapper(*entity->GetTy()));
    auto entityTy = StaticCast<ClassLikeTy>(entity->DataTy());
    auto curFile = entity->curFile;
    auto getter = GetNativeHandleGetter(*entityTy->commonDecl);
    auto getterMemberAccess = WithinFile(CreateMemberAccess(std::move(entity), *getter), curFile);
    auto getterCall = CreateCallExpr(std::move(getterMemberAccess), {}, getter,
        StaticCast<FuncTy>(getter->DataTy())->retTy, CallKind::CALL_DECLARED_FUNCTION);
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
        auto& ty = *StaticCast<ClassLikeTy>(decl.DataTy());

        return CreateNativeHandleExpr(ty, curFile);
    }

    return CreateGetClassCall(decl, curFile);
}

OwnedPtr<Expr> ASTFactory::UnwrapEntity(OwnedPtr<Expr> expr)
{
    if (typeMapper.IsObjCMirror(*expr->GetTy()) || typeMapper.IsObjCImpl(*expr->GetTy())) {
        return CreateNativeHandleExpr(std::move(expr));
    }

    if (expr->GetTy()->IsCoreOptionType()) {
        auto innerTy = expr->DataTy()->TyArg(0);
        if (typeMapper.IsValidObjCMirror(*innerTy) || typeMapper.IsObjCImpl(*innerTy)) {
            return UnwrapObjCMirrorOption(std::move(expr), innerTy);
        }
    }
    if (typeMapper.IsObjCPointer(*expr->GetTy())) {
        CJC_ASSERT(expr->DataTy()->TyArgs().size() == 1);
        auto elementType = expr->DataTy()->TyArg(0);
        auto field = bridge.GetObjCPointerPointerField();
        auto fieldRef = CreateRefExpr(*field, *expr);
        return CreateUnsafePointerCast(
            CreateMemberAccess(std::move(expr), *field),
            typeMapper.Cj2CType(elementType)
        );
    }
    if (typeMapper.IsObjCFunc(*expr->GetTy())) {
        CJC_ASSERT(expr->DataTy()->TyArgs().size() == 1);
        auto mappedTy = typeMapper.Cj2CType(expr->DataTy());
        auto getter = bridge.GetObjCFuncFPointerAccessor();
        CJC_ASSERT(mappedTy->IsPointer());
        return CreateUnsafePointerCast(CreateMemberCall(std::move(expr), getter), mappedTy->TyArg(0));
    }
    if (typeMapper.IsObjCBlock(*expr->GetTy())) {
        CJC_ASSERT(expr->DataTy()->TyArgs().size() == 1);
        auto mappedTy = typeMapper.Cj2CType(expr->DataTy());
        auto abiPtr = bridge.GetObjCBlockAbiPointerAccessor();
        CJC_ASSERT(mappedTy->IsPointer());
        return CreateUnsafePointerCast(CreateMemberCall(std::move(expr), abiPtr), mappedTy->TyArg(0));
    }

    CJC_ASSERT(expr->GetTy()->IsPrimitive() || expr->GetTy()->IsPointer() || expr->GetTy()->IsCStructType() ||
        expr->GetTy()->IsCFunc() || expr->GetTy()->IsCString());
    return expr;
}

OwnedPtr<Expr> ASTFactory::WrapEntity(OwnedPtr<Expr> expr, Ty& wrapTy, Retain withRetain)
{
    if (typeMapper.IsObjCMirror(wrapTy)) {
        CJC_ASSERT(expr->GetTy()->IsPointer());
        auto classLikeTy = StaticCast<ClassLikeTy>(&wrapTy);
        auto mirror = As<ASTKind::CLASS_DECL>(classLikeTy->commonDecl);
        if (!mirror) {
            mirror = GetSyntheticWrapper(importManager, *classLikeTy->commonDecl);
            CJC_NULLPTR_CHECK(mirror);
        }

        auto ctor = GetGeneratedBaseCtor(*mirror);
        switch (withRetain) {
            case Retain::RETAINED:
                expr = CreateObjCRetainCall(std::move(expr));
                break;
            case Retain::RETAIN_AUTORELEASED_RETURN_VALUE:
                expr = CreateObjCRetainAutoreleasedReturnValueCall(std::move(expr));
                break;
            case Retain::UNRETAINED:
            default:
                break;
        }
        return CreateCallExpr(CreateRefExpr(*ctor), Nodes<FuncArg>(CreateFuncArg(std::move(expr))), ctor, {classLikeTy},
            CallKind::CALL_OBJECT_CREATION);
    }

    if (typeMapper.IsObjCImpl(wrapTy)) {
        CJC_ASSERT(expr->DataTy()->IsPointer());
        auto& classTy = *StaticCast<ClassTy>(&wrapTy);
        auto& impl = *classTy.commonDecl;
        return CreateGetFromRegistryByNativeHandleCall(std::move(expr), CreateRefType(impl));
    }

    if (typeMapper.IsObjCPointer(wrapTy)) {
        CJC_ASSERT(expr->DataTy()->IsPointer());
        CJC_ASSERT(wrapTy.TyArgs().size() == 1);
        auto ctor = bridge.GetObjCPointerConstructor();
        CJC_ASSERT(ctor);
        auto ctorRef = CreateRefExpr(*ctor, *expr);
        ctorRef->instTys.push_back(wrapTy.TyArg(0));
        ctorRef->typeArguments.push_back(CreateType(wrapTy.TyArg(0)));
        auto unitPtrExpr = CreateUnsafePointerCast(
            std::move(expr),
            typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT));
        return CreateCallExpr(std::move(ctorRef), Nodes<FuncArg>(CreateFuncArg(std::move(unitPtrExpr))), ctor,
            {&wrapTy}, CallKind::CALL_STRUCT_CREATION);
    }

    if (typeMapper.IsObjCFunc(wrapTy)) {
        CJC_ASSERT(expr->DataTy()->IsPointer());
        CJC_ASSERT(wrapTy.TyArgs().size() == 1);
        auto ctor = bridge.GetObjCFuncConstructor();
        CJC_ASSERT(ctor);
        auto ctorRef = CreateRefExpr(*ctor, *expr);
        ctorRef->instTys.push_back(wrapTy.TyArg(0));
        ctorRef->typeArguments.push_back(CreateType(wrapTy.TyArg(0)));
        auto unitPtrExpr = CreateUnsafePointerCast(std::move(expr),
            typeManager.GetFunctionTy({}, {typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT)}, {.isC = true}));
        return CreateCallExpr(std::move(ctorRef), Nodes<FuncArg>(CreateFuncArg(std::move(unitPtrExpr))), ctor,
            {&wrapTy}, CallKind::CALL_STRUCT_CREATION);
    }

    if (typeMapper.IsObjCBlock(wrapTy)) {
        CJC_ASSERT(expr->DataTy()->IsPointer());
        CJC_ASSERT(wrapTy.TyArgs().size() == 1);
        auto ctor = bridge.GetObjCBlockConstructorFromObjC();
        CJC_ASSERT(ctor);
        auto ctorRef = CreateRefExpr(*ctor, *expr);
        ctorRef->instTys.push_back(wrapTy.TyArg(0));
        ctorRef->typeArguments.push_back(CreateType(wrapTy.TyArg(0)));
        return CreateCallExpr(std::move(ctorRef), Nodes<FuncArg>(CreateFuncArg(std::move(expr))), ctor, {&wrapTy},
            CallKind::CALL_OBJECT_CREATION);
    }

    if (wrapTy.IsCoreOptionType()) {
        if (auto classALTy = DynamicCast<ClassLikeTy>(wrapTy.TyArg(0))) {
            if (auto decl = classALTy->commonDecl;
                decl && decl->TestAnyAttr(Attribute::OBJ_C_MIRROR, Attribute::OBJ_C_MIRROR_SUBTYPE)) {
                    return WrapObjCMirrorOption(expr, decl, expr->curFile, withRetain);
            }
        }
    }

    CJC_ASSERT(expr->GetTy()->IsPrimitive() || expr->GetTy()->IsCStructType() || expr->GetTy()->IsCFunc() ||
        expr->GetTy()->IsCString());
    CJC_ASSERT(wrapTy.IsPrimitive() || wrapTy.IsCStructType() || wrapTy.IsCFunc() || wrapTy.IsCString());
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
    auto someFuncTy = typeManager.GetFunctionTy({{ty}}, {optionActualTy});
    someDeclRef->SetTy({someFuncTy});
    return someDeclRef;
}

OwnedPtr<Expr> ASTFactory::CreateOptionNoneRef(Ptr<Ty> ty)
{
    auto noneDeclRef = CreateRefExpr(*GetOptionNoneDecl());
    auto optionActualTy = GetOptionTy(ty);
    noneDeclRef->SetTy({optionActualTy});
    return noneDeclRef;
}

OwnedPtr<Expr> ASTFactory::CreateOptionSomeCall(OwnedPtr<Expr> expr, Ptr<Ty> ty)
{
    std::vector<OwnedPtr<FuncArg>> someDeclCallArgs {};
    someDeclCallArgs.emplace_back(CreateFuncArg(std::move(expr)));
    auto someDeclCall = CreateCallExpr(CreateOptionSomeRef(ty), std::move(someDeclCallArgs));
    someDeclCall->SetTy(GetOptionTy(ty));
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

    auto& optTy = *selector->GetTy();
    CJC_ASSERT(optTy.IsCoreOptionType());
    auto optArgTy{optTy.TyArg(0)};

    auto vp = CreateVarPattern(Cangjie::V_COMPILER, optArgTy);
    vp->curFile = curFile;
    vp->varDecl->curFile = curFile;
    auto& someArgVar = *vp->varDecl;

    auto somePattern = MakeOwnedNode<EnumPattern>();
    somePattern->SetTy(selector->GetTy());
    somePattern->constructor = CreateOptionSomeRef(optArgTy);
    somePattern->patterns.emplace_back(std::move(vp));
    somePattern->curFile = curFile;
    auto caseSome = CreateMatchCase(std::move(somePattern), someBranch(someArgVar));

    auto nonePattern = MakeOwnedNode<EnumPattern>();
    nonePattern->constructor = CreateOptionNoneRef(optArgTy);
    nonePattern->SetTy(nonePattern->constructor->GetTy());
    nonePattern->curFile = curFile;
    auto caseNone = CreateMatchCase(std::move(nonePattern), noneBranch());

    return WithinFile(
        CreateMatchExpr(std::move(selector), Nodes<MatchCase>(std::move(caseSome), std::move(caseNone)), {ty}),
        curFile);
}


Ptr<Ty> ASTFactory::GetObjCTy()
{
    return typeManager.GetPointerTy(typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT));
}

OwnedPtr<Expr> ASTFactory::CreateObjCobjectNull()
{
    auto pointerExpr = MakeOwnedNode<PointerExpr>();
    pointerExpr->type = MakeOwnedNode<Type>();
    pointerExpr->type->SetTy(GetObjCTy());
    pointerExpr->SetTy(pointerExpr->type->GetTy());
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
    {
    match (handle.isNull()) {
        case false => Some(T(handle)) | Some(getFromRegistry(handle))
        case true => None
    }
    }()
 */
OwnedPtr<Expr> ASTFactory::WrapObjCMirrorOption(
    const Ptr<Expr> entity, Ptr<ClassLikeDecl> mirror, const Ptr<File> curFile, Retain withRetain)
{
    std::vector<OwnedPtr<Node>> nodes;
    auto baseTy = typeManager.GetPointerTy(typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT));
    auto tmpVar = CreateTmpVarDecl(CreateType({baseTy}), entity);
    CopyBasicInfo(tmpVar->initializer.get(), tmpVar.get());
    tmpVar->begin = entity->begin;
    tmpVar->curFile = curFile;
    auto objcrefExpr = WithinFile(CreateRefExpr(*tmpVar), curFile);

    auto castTy = mirror->GetTy();
    // case true => None
    OwnedPtr<Expr> trueBranch = CreateOptionNoneRef(castTy.Ty());
    // case false => wrap($tmp, T)
    OwnedPtr<Expr> falseBranch = WrapEntity(std::move(objcrefExpr), *castTy, withRetain);

    auto isInstanceCall = WithinFile(CreateGetObjcEntityOrNullCall(*tmpVar, curFile), curFile);
    auto boolMatch = CreateBoolMatch(
        std::move(isInstanceCall), std::move(trueBranch), std::move(falseBranch), GetOptionTy(castTy.Ty()));
    boolMatch->begin = entity->begin;
    boolMatch->end = entity->end;
    nodes.push_back(std::move(tmpVar));
    nodes.push_back(std::move(boolMatch));
    return WrapReturningLambdaCall(typeManager, std::move(nodes));
}

OwnedPtr<Expr> ASTFactory::CreateOptionalMethodGuard(
    OwnedPtr<Expr> msgSend, OwnedPtr<Expr> cls, const std::string& selector, const Ptr<File> curFile)
{
    std::vector<OwnedPtr<Node>> nodes;
    auto baseTy = msgSend->GetTy();
    auto selectorCall = CreateGetCachedSelectorAccess(selector, curFile);
    auto isRespondToSelectorCall = CreateObjCRespondsToSelectorCall(std::move(cls), std::move(selectorCall), curFile);

    // case true => return msgSend(...)
    OwnedPtr<Expr> trueBranch = std::move(msgSend);

    // case false => throw Exception(...)
    OwnedPtr<Expr> falseBranch = WithinFile(CreateThrowOptionalMethodUnimplemented(*curFile), curFile);

    auto boolMatch =
        CreateBoolMatch(std::move(isRespondToSelectorCall), std::move(trueBranch), std::move(falseBranch), baseTy.Ty());

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
            ctorCall->SetTy({mirrorTy});
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
    static auto getPointerAddressFunc = importManager.GetCoreDecl<FuncDecl>(
        GET_POINTER_ADDRESS_INTRINSIC);
    CJC_NULLPTR_CHECK(getPointerAddressFunc);
    auto readPointerRef = WithinFile(CreateRefExpr(*getPointerAddressFunc), file);
    auto uintNativeTy = typeManager.GetPrimitiveTy(TypeKind::TYPE_UINT_NATIVE);
    CJC_ASSERT(entity.GetTy()->IsPointer());
    readPointerRef->instTys.push_back(entity.GetTy()->typeArgs.front().Ty());
    readPointerRef->SetTy({typeManager.GetFunctionTy(std::vector{entity.GetTy()}, {uintNativeTy})});
    std::vector<OwnedPtr<FuncArg>> callArgs;
    callArgs.push_back(CreateFuncArg(WithinFile(CreateRefExpr(entity), file)));
    auto getPointerAddressCall = CreateCallExpr(std::move(readPointerRef), std::move(callArgs), getPointerAddressFunc,
        {uintNativeTy}, CallKind::CALL_INTRINSIC_FUNCTION);

    auto result = CreateBinaryExpr(std::move(getPointerAddressCall),
        CreateLitConstExpr(LitConstKind::INTEGER, "0", {uintNativeTy}), TokenKind::EQUAL);
    // CreateBinaryExpr sets type to type of left or right operand, which is incorrect for ==
    result->SetTy({typeManager.GetBoolTy()});
    return result;
}

OwnedPtr<VarDecl> ASTFactory::CreateNativeHandleField(ClassDecl& target)
{
    auto nativeHandleTy = bridge.GetNativeObjCIdTy();

    auto nativeHandleField = CreateVarDecl(NATIVE_HANDLE_IDENT, nullptr, CreateType(nativeHandleTy));
    nativeHandleField->SetTy({nativeHandleTy});
    // mark it initialized because sema initialization analysis is run before
    // objc desugaring
    nativeHandleField->EnableAttr(Attribute::PUBLIC, Attribute::INITIALIZED);

    PutDeclToClassLikeBody(*nativeHandleField, target);

    return nativeHandleField;
}

OwnedPtr<FuncDecl> ASTFactory::CreateGetObjCClassDecl(ClassLikeDecl& target)
{
    auto nativeObjCClassTy = bridge.GetNativeObjCClassTy();

    auto curFile = target.curFile;
    auto paramList = MakeOwned<FuncParamList>();
    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.push_back(std::move(paramList));
    auto getClassCall = CreateGetCachedClassAccess(*StaticCast<ClassLikeTy>(target.DataTy()), curFile);
    auto wrapperFuncTy = typeManager.GetFunctionTy({}, {nativeObjCClassTy});
    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), CreateType(nativeObjCClassTy),
        CreateBlock({}, {nativeObjCClassTy}), {wrapperFuncTy});

    auto classDecl = CreateFuncDecl(GET_OBJ_C_CLASS_IDENT, std::move(wrapperBody), {wrapperFuncTy});
    classDecl->moduleName = target.moduleName;
    classDecl->fullPackageName = target.fullPackageName;
    classDecl->EnableAttr(Attribute::PUBLIC, Attribute::STATIC, Attribute::IS_CHECK_VISITED);
    classDecl->funcBody->funcDecl = classDecl.get();
    classDecl->linkage = Linkage::EXTERNAL;
    classDecl->funcBody->parentClassLike = &target;
    PutDeclToClassLikeBody(*classDecl, target);

    return classDecl;
}

OwnedPtr<FuncDecl> ASTFactory::CreateGetObjCClass(ClassLikeDecl& target)
{
    auto curFile = target.curFile;
    auto getClassCall = CreateGetCachedClassAccess(*StaticCast<ClassTy>(target.DataTy()), curFile);
    auto classDecl = CreateGetObjCClassDecl(target);
    classDecl->funcBody->body = CreateBlock(Nodes<Node>(std::move(getClassCall)));
    return classDecl;
}

OwnedPtr<FuncDecl> ASTFactory::CreateInitCjObject(Decl& target, FuncDecl& ctor, bool generateForOneWayMapping)
{
    auto curFile = ctor.curFile;
    auto registryIdTy = bridge.GetRegistryIdTy();

    std::unordered_map<std::string, Ptr<Ty>> actualTyArgMap;
    std::vector<ModalTy> funcTyParams;
    std::vector<OwnedPtr<Type>> actualPrimitiveType;

    auto wrapperParamList = MakeOwned<FuncParamList>();
    auto& wrapperParams = wrapperParamList->params;
    auto& ctorParams = ctor.funcBody->paramLists[0]->params;
    std::transform(
        ctorParams.begin(),
        ctorParams.end(),
        std::back_inserter(wrapperParams),
        [this, &actualTyArgMap](const auto& p) {
            std::string paramName = p->identifier.Val();
            // default ty
            Ptr<Ty> finalTy = p->DataTy();
            // genericTy->actualTy
            auto it = actualTyArgMap.find(p->DataTy()->name);
            if (it != actualTyArgMap.end()) {
                finalTy = it->second;
            }
            CJC_NULLPTR_CHECK(finalTy);
            Ptr<Ty> cType = this->typeMapper.Cj2CType(finalTy);
            CJC_NULLPTR_CHECK(cType);
            return CreateFuncParam(paramName, nullptr, nullptr, {cType});
        }
    );

    std::vector<ModalTy> wrapperParamTys;
    std::transform(wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys),
        [](auto& p) { return p->GetTy(); });

    auto wrapperTy = typeManager.GetFunctionTy(wrapperParamTys, {registryIdTy}, {.isC = true});

    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    std::vector<OwnedPtr<FuncArg>> ctorCallArgs;
    size_t argIdx = 0;
    if (!generateForOneWayMapping) {
        CJC_ASSERT_WITH_MSG(!wrapperParams.empty(), "wrapperParams should not be empty");
        auto objParamRef = CreateRefExpr(*wrapperParams[0]);
        ctorCallArgs.emplace_back(CreateFuncArg(std::move(objParamRef)));
        // skip first param, as it is needed only for restore @ObjCImpl instance.
        argIdx = 1;
    }
    while (argIdx < ctorParams.size()) {
        Ptr<Ty> finalTy = ctorParams[argIdx]->DataTy();
        // genericTy->actualTy
        auto it = actualTyArgMap.find(finalTy->name);
        if (it != actualTyArgMap.end()) {
            finalTy = it->second;
        }
        auto& wrapperParam = wrapperParams[argIdx];
        auto paramRef = WithinFile(CreateRefExpr(*wrapperParam), curFile);
        OwnedPtr<FuncArg> ctorCallArg = CreateFuncArg(WrapEntity(std::move(paramRef), *finalTy));
        ctorCallArgs.emplace_back(std::move(ctorCallArg));
        ++argIdx;
    }

    OwnedPtr<CallExpr> ctorCall;
    if (target.DataTy()->HasGeneric()) {
        auto instantiationRefExpr = CreateRefExpr(ctor);
        auto instantTy = GetInstantyForGenericTy(target, actualTyArgMap, typeManager);
        auto retTy = StaticCast<FuncTy*>(ctor.DataTy())->retTy;
        Ptr<FuncTy> funcTy;
        if (retTy.Ty()->HasGeneric()) {
            funcTy = typeManager.GetFunctionTy(funcTyParams, {instantTy}, {.isC = true});
        } else {
            funcTy = typeManager.GetFunctionTy(funcTyParams, retTy, {.isC = true});
        }
        instantiationRefExpr->typeArguments = std::move(actualPrimitiveType);
        instantiationRefExpr->SetTy({funcTy});
        ctorCall = CreateCallExpr(std::move(instantiationRefExpr), std::move(ctorCallArgs), Ptr(&ctor), {instantTy},
            CallKind::CALL_OBJECT_CREATION);
    } else {
        ctorCall = CreateCallExpr(
            CreateRefExpr(ctor), std::move(ctorCallArgs), Ptr(&ctor), target.GetTy(), CallKind::CALL_OBJECT_CREATION);
    }
    auto putToRegistryCall = CreatePutToRegistryCall(std::move(ctorCall));

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), CreateType(registryIdTy),
        CreateBlock(Nodes<Node>(std::move(putToRegistryCall)), {registryIdTy}), {wrapperTy});

    auto wrapperName = nameGenerator.GenerateInitCjObjectName(ctor);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), {wrapperTy});
    wrapper->moduleName = ctor.moduleName;
    wrapper->fullPackageName = ctor.fullPackageName;
    wrapper->EnableAttr(Attribute::C, Attribute::GLOBAL, Attribute::PUBLIC, Attribute::NO_MANGLE);
    wrapper->funcBody->funcDecl = wrapper.get();
    PutDeclToFile(*wrapper, *ctor.curFile);

    return wrapper;
}


OwnedPtr<FuncDecl> ASTFactory::CreateInitCjObjectReturningObjCSelf(const Decl& target, FuncDecl& ctor) {
    auto curFile = ctor.curFile;
    auto nativeObjCIdTy = bridge.GetNativeObjCIdTy();

    auto wrapperParamList = MakeOwned<FuncParamList>();
    auto& wrapperParams = wrapperParamList->params;
    auto& ctorParams = ctor.funcBody->paramLists[0]->params;
    std::transform(ctorParams.begin(), ctorParams.end(), std::back_inserter(wrapperParams), [this](auto& p) {
        return CreateFuncParam(p->identifier.Val(), nullptr, nullptr, {typeMapper.Cj2CType(p->DataTy())});
    });

    std::vector<ModalTy> wrapperParamTys;
    std::transform(wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys),
        [](auto& p) { return p->GetTy(); });

    auto wrapperTy = typeManager.GetFunctionTy(wrapperParamTys, {nativeObjCIdTy}, {.isC = true});

    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    std::vector<OwnedPtr<FuncArg>> ctorCallArgs;
    auto objCSelf = CreateRefExpr(*wrapperParams[0]);
    ctorCallArgs.emplace_back(CreateFuncArg(std::move(objCSelf)));
    size_t argIdx = 1;
    while (argIdx < ctorParams.size()) {
        auto& wrapperParam = wrapperParams[argIdx];
        auto paramRef = WithinFile(CreateRefExpr(*wrapperParam), curFile);
        auto ctorCallArg = CreateFuncArg(WrapEntity(std::move(paramRef), *ctorParams[argIdx]->DataTy()));
        ctorCallArgs.emplace_back(std::move(ctorCallArg));
        ++argIdx;
    }

    auto ctorCall = WithinFile(CreateCallExpr(CreateRefExpr(ctor), std::move(ctorCallArgs), Ptr(&ctor), target.GetTy(),
                                              CallKind::CALL_OBJECT_CREATION),
        curFile);

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), nullptr,
        CreateBlock(Nodes<Node>(UnwrapEntity(std::move(ctorCall))), {nativeObjCIdTy}), {wrapperTy});

    auto wrapperName = nameGenerator.GenerateInitCjObjectName(ctor);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), {wrapperTy});
    wrapper->moduleName = ctor.moduleName;
    wrapper->fullPackageName = ctor.fullPackageName;
    wrapper->EnableAttr(Attribute::C, Attribute::GLOBAL, Attribute::PUBLIC, Attribute::NO_MANGLE);
    wrapper->funcBody->funcDecl = wrapper.get();
    PutDeclToFile(*wrapper, *ctor.curFile);

    return wrapper;
}

OwnedPtr<FuncDecl> ASTFactory::CreateInitCjObjectForEnumNoParams(AST::EnumDecl& target, AST::VarDecl& ctor)
{
    auto curFile = ctor.curFile;
    CJC_NULLPTR_CHECK(curFile);
    auto registryIdTy = bridge.GetRegistryIdTy();

    auto wrapperParamList = MakeOwned<FuncParamList>();
    std::vector<ModalTy> wrapperParamTys;
    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    // createMemRef
    auto enumRef = WithinFile(CreateRefExpr(target), curFile);
    auto ctorCall = CreateMemberAccess(std::move(enumRef), ctor.identifier);
    auto putToRegistryCall = CreatePutToRegistryCall(std::move(ctorCall));

    auto wrapperName = nameGenerator.GenerateInitCjObjectName(ctor);
    auto wrapperTy = typeManager.GetFunctionTy(wrapperParamTys, {registryIdTy}, {.isC = true});
    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), CreateType(registryIdTy),
        CreateBlock(Nodes<Node>(std::move(putToRegistryCall)), {registryIdTy}), {wrapperTy});

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), {wrapperTy});
    wrapper->moduleName = ctor.moduleName;
    wrapper->fullPackageName = ctor.fullPackageName;
    wrapper->EnableAttr(Attribute::C, Attribute::GLOBAL, Attribute::PUBLIC, Attribute::NO_MANGLE);
    wrapper->funcBody->funcDecl = wrapper.get();
    PutDeclToFile(*wrapper, *ctor.curFile);

    return wrapper;
}

OwnedPtr<FuncDecl> ASTFactory::CreateDeleteCjObject(Decl& target)
{
    auto registryIdTy = bridge.GetRegistryIdTy();
    auto param = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, {registryIdTy});
    auto unitTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    auto paramRef = CreateRefExpr(*param);
    auto paramRefTmp = paramRef.get();

    std::vector<ModalTy> funcParamTys;
    funcParamTys.emplace_back(param->GetTy());
    auto funcTy = typeManager.GetFunctionTy(std::move(funcParamTys), {unitTy}, {.isC = true});

    auto removeFromRegistryCall = CreateRemoveFromRegistryCall(std::move(paramRef));

    std::vector<OwnedPtr<Node>> funcNodes;
    auto getFromRegistryCall = CreateGetFromRegistryByIdCall(
        ASTCloner::Clone(paramRefTmp), CreateRefType(static_cast<InheritableDecl&>(target)));
    auto objTmpVarDecl =
        CreateTmpVarDecl(CreateRefType(static_cast<InheritableDecl&>(target)), std::move(getFromRegistryCall));
    auto nativeHandleExpr = CreateMemberAccess(CreateRefExpr(*objTmpVarDecl), NATIVE_HANDLE_IDENT);
    auto releaseCall = CreateObjCReleaseCall(std::move(nativeHandleExpr));

    funcNodes.emplace_back(std::move(objTmpVarDecl));
    funcNodes.emplace_back(std::move(removeFromRegistryCall));
    funcNodes.emplace_back(std::move(releaseCall));

    std::vector<OwnedPtr<FuncParam>> funcParams;
    funcParams.emplace_back(std::move(param));
    auto paramList = CreateFuncParamList(std::move(funcParams));
    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.emplace_back(std::move(paramList));

    auto funcBody = CreateFuncBody(std::move(paramLists), Native::FFI::CreateUnitType(target.curFile),
        CreateBlock(std::move(funcNodes), {unitTy}), {funcTy});

    auto funcName = nameGenerator.GenerateDeleteCjObjectName(target);

    auto ret = CreateFuncDecl(funcName, std::move(funcBody), {funcTy});
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

    // Alias the return Type node so each use reads the current Ptr<Ty> (same as former retType->ty ref).
    auto& methodRetType = *method.funcBody->retType;
    OwnedPtr<CallExpr> getFromRegistryCall;
    OwnedPtr<VarDecl> objTmpVarDecl;
    OwnedPtr<MemberAccess> methodExpr;
    std::unordered_map<std::string, Ptr<Ty>> actualTyArgMap;
    std::vector<ModalTy> funcTyParams;
    std::vector<OwnedPtr<Type>> actualPrimitiveType;
    auto instantTy = GetInstantyForGenericTy(*method.outerDecl, actualTyArgMap, typeManager);
    Ptr<Ty> retActualTy;
    if (methodRetType.DataTy()->IsGeneric()) {
        retActualTy = actualTyArgMap[methodRetType.DataTy()->name];
    } else {
        retActualTy = methodRetType.DataTy();
    }
    if (method.TestAttr(Attribute::STATIC)) {
        auto staticRefExpr = CreateRefExpr(*method.outerDecl);
        if (method.outerDecl->DataTy()->HasGeneric()) {
            staticRefExpr->typeArguments = std::move(actualPrimitiveType);
            staticRefExpr->SetTy({instantTy});
        }
        methodExpr = CreateMemberAccess(WithinFile(std::move(staticRefExpr), method.curFile), method);
    } else {
        auto registryIdTy = bridge.GetRegistryIdTy();
        auto registryIdParam = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, {registryIdTy});
        auto registryIdParamRef = CreateRefExpr(*registryIdParam);
        wrapperParams.emplace_back(std::move(registryIdParam));
        if (outerDecl->DataTy()->HasGeneric()) {
            auto registryTy = CreateRefType(*outerDecl, instantTy);
            registryTy->typeArguments = std::move(actualPrimitiveType);

            getFromRegistryCall =
                CreateGetFromRegistryByIdCall(std::move(registryIdParamRef), std::move(registryTy));
            objTmpVarDecl = CreateTmpVarDecl(CreateType(instantTy), std::move(getFromRegistryCall));
        } else {
            getFromRegistryCall =
                CreateGetFromRegistryByIdCall(std::move(registryIdParamRef), CreateRefType(*outerDecl));
            objTmpVarDecl = CreateTmpVarDecl(CreateRefType(*outerDecl), std::move(getFromRegistryCall));
        }

        // The mut function object does not allow assignment using the 'let' method and needs to use 'var' instead.
        if (method.TestAttr(Attribute::MUT)) {
            objTmpVarDecl->isVar = true;
        }

        methodExpr = CreateMemberAccess(CreateRefExpr(*objTmpVarDecl), method);
        if (!methodRetType.DataTy()->IsGeneric()) {
            methodExpr->SetTy({typeManager.GetFunctionTy(funcTyParams, methodRetType.GetTy())});
        } else {
            methodExpr->SetTy({typeManager.GetFunctionTy(funcTyParams, {retActualTy})});
        }
    }

    auto& originParams = method.funcBody->paramLists[0]->params;
    std::transform(
        originParams.begin(),
        originParams.end(),
        std::back_inserter(wrapperParams),
        [this, &actualTyArgMap](const auto& p) {
            std::string paramName = p->identifier.Val();
            // default ty
            Ptr<Ty> finalTy = p->DataTy();
            // genericTy->actualTy
            auto it = actualTyArgMap.find(p->DataTy()->name);
            if (it != actualTyArgMap.end()) {
                finalTy = it->second;
            }
            CJC_NULLPTR_CHECK(finalTy);
            auto convertedParamTy = typeMapper.Cj2CType(finalTy);
            CJC_NULLPTR_CHECK(convertedParamTy);
            return CreateFuncParam(p->identifier, CreateType(convertedParamTy), nullptr, convertedParamTy);
        }
    );

    std::vector<ModalTy> wrapperParamTys;
    std::transform(wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys),
        [](auto& p) { return p->GetTy(); });

    Ptr<FuncTy> retWrapperTy;
    if (methodRetType.DataTy()->HasGeneric()) {
        retWrapperTy = typeManager.GetFunctionTy(wrapperParamTys, {typeMapper.Cj2CType(retActualTy)}, {.isC = true});
    } else {
        retWrapperTy =
            typeManager.GetFunctionTy(wrapperParamTys, {typeMapper.Cj2CType(methodRetType.DataTy())}, {.isC = true});
    }

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

        Ptr<Ty> finalTy = originParam->DataTy();
        // genericTy->actualTy
        auto it = actualTyArgMap.find(finalTy->name);
        if (it != actualTyArgMap.end()) {
            finalTy = it->second;
        }
        auto paramRef = CreateRefExpr(*wrapperParam);
        auto wrappedParamRef = WrapEntity(WithinFile(std::move(paramRef), method.curFile), *finalTy);
        auto arg = CreateFuncArg(std::move(wrappedParamRef), wrapperParam->identifier, {finalTy});
        methodArgs.emplace_back(std::move(arg));
    }

    OwnedPtr<CallExpr> methodCall;
    if (outerDecl->DataTy()->HasGeneric()) {
        methodCall = CreateCallExpr(std::move(methodExpr), std::move(methodArgs), Ptr(&method), {retActualTy},
            CallKind::CALL_DECLARED_FUNCTION);
    } else {
        methodCall = CreateCallExpr(std::move(methodExpr), std::move(methodArgs), Ptr(&method), methodRetType.GetTy(),
            CallKind::CALL_DECLARED_FUNCTION);
    }

    std::vector<OwnedPtr<Node>> wrapperNodes;
    if (!method.TestAttr(Attribute::STATIC)) {
        wrapperNodes.emplace_back(std::move(objTmpVarDecl));
    }
    auto resTy = methodCall->GetTy();
    auto unwrappedMethodCall = UnwrapEntity(std::move(methodCall));
    if (typeMapper.IsObjCObjectType(*resTy)) {
        // We always return object values with retain count +1
        // ARC balances it with __bridge_transfer
        // It's done to prevent over release objects, e.g. if Cangjie GC triggers before control acquired by ARC and
        // @Mirror object is collected
        unwrappedMethodCall = CreateObjCRetainCall(std::move(unwrappedMethodCall));
    }
    wrapperNodes.emplace_back(std::move(unwrappedMethodCall));

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), CreateType(retWrapperTy->retTy.Ty()),
        CreateBlock(std::move(wrapperNodes), retWrapperTy->retTy), {retWrapperTy});

    auto wrapperName = nameGenerator.GenerateMethodWrapperName(method);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), {retWrapperTy});
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
        auto registryIdParam = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, {registryIdTy});
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
        std::move(propGetterExpr), {}, Ptr(prop.getters[0].get()), prop.GetTy(), CallKind::CALL_DECLARED_FUNCTION);

    std::vector<ModalTy> wrapperParamTys;
    std::transform(wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys),
        [](auto& p) { return p->GetTy(); });

    auto wrapperTy = typeManager.GetFunctionTy(wrapperParamTys, {typeMapper.Cj2CType(prop.DataTy())}, {.isC = true});

    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    std::vector<OwnedPtr<Node>> wrapperNodes;
    if (!prop.TestAttr(Attribute::STATIC)) {
        wrapperNodes.emplace_back(std::move(objTmpVarDecl));
    }
    auto unwrappedPropGetterCall = UnwrapEntity(std::move(propGetterCall));
    if (typeMapper.IsObjCObjectType(*prop.GetTy())) {
        // We always return object values with retain count +1
        // ARC balances it with __bridge_transfer
        // It's done to prevent over release objects, e.g. if Cangjie GC triggers before control acquired by ARC and
        // @Mirror object is collected
        unwrappedPropGetterCall = CreateObjCRetainCall(std::move(unwrappedPropGetterCall));
    }
    wrapperNodes.emplace_back(std::move(unwrappedPropGetterCall));

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), CreateType(wrapperTy->retTy.Ty()),
        CreateBlock(std::move(wrapperNodes), wrapperTy->retTy), {wrapperTy});

    auto wrapperName = nameGenerator.GeneratePropGetterWrapperName(prop);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), {wrapperTy});
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

    auto convertedPropTy = typeMapper.Cj2CType(prop.DataTy());
    auto setterParam = CreateFuncParam(VALUE_IDENT, CreateType(convertedPropTy), nullptr, {convertedPropTy});
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
        auto registryIdParam = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, {registryIdTy});
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
        auto wrappedParamRef = WrapEntity(std::move(paramRef), *originParam->DataTy());
        auto arg = CreateFuncArg(std::move(wrappedParamRef), wrapperParam->identifier, originParam->GetTy());
        propSetterArgs.emplace_back(std::move(arg));
    }

    std::vector<ModalTy> wrapperParamTys;
    std::transform(wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys),
        [](auto& p) { return p->GetTy(); });

    auto wrapperTy = typeManager.GetFunctionTy(wrapperParamTys, {unitTy}, {.isC = true});

    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    auto propSetterCall = CreateCallExpr(
        std::move(propSetterExpr), std::move(propSetterArgs), Ptr(&setter), {unitTy}, CallKind::CALL_DECLARED_FUNCTION);

    if (objTmpVarDecl != nullptr) {
        wrapperNodes.emplace_back(std::move(objTmpVarDecl));
    }
    wrapperNodes.emplace_back(std::move(propSetterCall));

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), CreateUnitType(prop.curFile),
        CreateBlock(std::move(wrapperNodes), wrapperTy->retTy), {wrapperTy});

    auto wrapperName = nameGenerator.GetPropSetterWrapperName(prop);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), {wrapperTy});
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

    Ptr<PrimitiveTy> genericActualTy;
    std::unordered_map<std::string, Ptr<Ty>> actualTyArgMap;
    std::vector<OwnedPtr<Type>> actualPrimitiveType;
    OwnedPtr<CallExpr> getFromRegistryCall;

    auto wrapperParamList = MakeOwned<FuncParamList>();
    auto& wrapperParams = wrapperParamList->params;

    OwnedPtr<VarDecl> objTmpVarDecl;
    OwnedPtr<MemberAccess> fieldExpr;
    if (field.TestAttr(Attribute::STATIC)) {
        fieldExpr = CreateMemberAccess(WithinFile(CreateRefExpr(*outerDecl), field.curFile), field);
    } else {
        auto registryIdTy = bridge.GetRegistryIdTy();
        auto registryIdParam = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, {registryIdTy});
        auto registryIdParamRef = CreateRefExpr(*registryIdParam);
        wrapperParams.emplace_back(std::move(registryIdParam));
        if (outerDecl->DataTy()->HasGeneric()) {
            auto instantTy = GetInstantyForGenericTy(*outerDecl, actualTyArgMap, typeManager);
            auto registryTy = CreateRefType(*outerDecl, instantTy);
            registryTy->typeArguments = std::move(actualPrimitiveType);
            auto refTy = ASTCloner::Clone<RefType>(registryTy);
            getFromRegistryCall =
                CreateGetFromRegistryByIdCall(std::move(registryIdParamRef), std::move(registryTy));
            objTmpVarDecl = CreateTmpVarDecl(std::move(refTy), std::move(getFromRegistryCall));
            fieldExpr = CreateMemberAccess(CreateRefExpr(*objTmpVarDecl), field);
            if (field.DataTy()->IsGeneric()) {
                fieldExpr->SetTy({genericActualTy});
            }
        } else {
            getFromRegistryCall =
                CreateGetFromRegistryByIdCall(std::move(registryIdParamRef), CreateRefType(*outerDecl));
            objTmpVarDecl = CreateTmpVarDecl(CreateRefType(*outerDecl), std::move(getFromRegistryCall));
            fieldExpr = CreateMemberAccess(CreateRefExpr(*objTmpVarDecl), field);
        }
    }
    fieldExpr->curFile = field.curFile;
    fieldExpr->begin = field.GetBegin();
    fieldExpr->end = field.GetEnd();

    std::vector<ModalTy> wrapperParamTys;
    std::transform(wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys),
        [](auto& p) { return p->GetTy(); });

    Ptr<Ty> fieldObjCTy = field.DataTy()->IsGeneric() ? genericActualTy : typeMapper.Cj2CType(field.DataTy());
    auto wrapperTy = typeManager.GetFunctionTy(wrapperParamTys, {fieldObjCTy}, {.isC = true});

    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    std::vector<OwnedPtr<Node>> wrapperNodes;
    if (!field.TestAttr(Attribute::STATIC)) {
        wrapperNodes.emplace_back(std::move(objTmpVarDecl));
    }
    auto unwrappedFieldExpr = UnwrapEntity(std::move(fieldExpr));
    if (typeMapper.IsObjCObjectType(*field.GetTy())) {
        // We always return object values with retain count +1
        // ARC balances it with __bridge_transfer
        // It's done to prevent over release objects, e.g. if Cangjie GC triggers before control acquired by ARC and
        // @Mirror object is collected
        unwrappedFieldExpr = CreateObjCRetainCall(std::move(unwrappedFieldExpr));
    }
    wrapperNodes.emplace_back(std::move(unwrappedFieldExpr));

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), CreateType(wrapperTy->retTy.Ty()),
        CreateBlock(std::move(wrapperNodes), wrapperTy->retTy), {wrapperTy});

    // Generate wrapper name from ORIGIN field, not a mirror one.
    auto wrapperName = nameGenerator.GetFieldGetterWrapperName(field);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), {wrapperTy});
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

    auto convertedFieldTy = typeMapper.Cj2CType(field.DataTy());
    auto setterParam = CreateFuncParam(VALUE_IDENT, CreateType(convertedFieldTy), nullptr, {convertedFieldTy});
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
        auto registryIdParam = CreateFuncParam(REGISTRY_ID_IDENT, CreateType(registryIdTy), nullptr, {registryIdTy});
        auto registryIdParamRef = CreateRefExpr(*registryIdParam);
        wrapperParams.emplace_back(std::move(registryIdParam));

        auto getFromRegistryCall = CreateGetFromRegistryByIdCall(std::move(registryIdParamRef), CreateRefType(*outerDecl));
        auto objTmpVarDecl = CreateTmpVarDecl(CreateRefType(*outerDecl), std::move(getFromRegistryCall));
        lhs = CreateMemberAccess(CreateRefExpr(*objTmpVarDecl), field);
        wrapperNodes.emplace_back(std::move(objTmpVarDecl));
    }
    auto assignFieldExpr =
        CreateAssignExpr(std::move(lhs), WrapEntity(std::move(setterParamRef), *field.DataTy()), {unitTy});
    assignFieldExpr->curFile = field.curFile;
    assignFieldExpr->begin = field.GetBegin();
    assignFieldExpr->end = field.GetEnd();
    wrapperNodes.emplace_back(std::move(assignFieldExpr));

    wrapperParams.emplace_back(std::move(setterParam));
    std::vector<ModalTy> wrapperParamTys;
    std::transform(wrapperParams.begin(), wrapperParams.end(), std::back_inserter(wrapperParamTys),
        [](auto& p) { return p->GetTy(); });

    auto wrapperTy = typeManager.GetFunctionTy(wrapperParamTys, {unitTy}, {.isC = true});

    std::vector<OwnedPtr<FuncParamList>> wrapperParamLists;
    wrapperParamLists.emplace_back(std::move(wrapperParamList));

    auto wrapperBody = CreateFuncBody(std::move(wrapperParamLists), CreateUnitType(field.curFile),
        CreateBlock(std::move(wrapperNodes), wrapperTy->retTy), {wrapperTy});

    // Generate wrapper name from ORIGIN field, not a mirror one.
    auto wrapperName = nameGenerator.GetFieldSetterWrapperName(field);

    auto wrapper = CreateFuncDecl(wrapperName, std::move(wrapperBody), {wrapperTy});
    wrapper->moduleName = field.moduleName;
    wrapper->fullPackageName = field.fullPackageName;
    wrapper->EnableAttr(Attribute::C, Attribute::GLOBAL, Attribute::PUBLIC, Attribute::NO_MANGLE);
    wrapper->funcBody->funcDecl = wrapper.get();

    PutDeclToFile(*wrapper, *field.curFile);

    return wrapper;
}

OwnedPtr<ThrowExpr> ASTFactory::CreateThrowOptionalMethodUnimplemented(File& file)
{
    auto exceptionDecl = bridge.GetObjCOptionalMethodUnimplementedExceptionDecl();
    CJC_NULLPTR_CHECK(exceptionDecl);
    return CreateThrowException(*exceptionDecl, {}, file, typeManager);
}

OwnedPtr<ThrowExpr> ASTFactory::CreateThrowUnreachableCodeExpr(File& file)
{
    auto exceptionDecl = bridge.GetObjCUnreachableCodeExceptionDecl();
    CJC_NULLPTR_CHECK(exceptionDecl);
    return CreateThrowException(*exceptionDecl, {}, file, typeManager);
}

OwnedPtr<ThrowExpr> ASTFactory::CreateObjCInitException(File& file, ClassLikeDecl& cls)
{
    auto exceptionDecl = bridge.GetObjCInitException();
    CJC_NULLPTR_CHECK(exceptionDecl);

    constexpr auto OBJC_INIT_EXCEPTION_MESSAGE_PART_1 = "Initialization error: expected ";
    constexpr auto OBJC_INIT_EXCEPTION_MESSAGE_PART_2 = ", got nil.";

    auto argTy = GetStringDecl(importManager).GetTy();
    auto className = nameGenerator.GetObjCDeclName(cls);
    auto arg = CreateLitConstExpr(LitConstKind::STRING,
        OBJC_INIT_EXCEPTION_MESSAGE_PART_1 + className + OBJC_INIT_EXCEPTION_MESSAGE_PART_2, argTy);
    std::vector<OwnedPtr<Expr>> args;
    args.emplace_back(std::move(arg));
    return CreateThrowException(*exceptionDecl, std::move(args), file, typeManager);
}

OwnedPtr<ThrowExpr> ASTFactory::CreateThrowStaticMethodCallOnInterfaceExpr(File& file)
{
    auto exceptionDecl = bridge.GetObjCStaticMethodCallOnIntefaceExceptionDecl();
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
    auto param = CreateFuncParam(NATIVE_HANDLE_IDENT, CreateType(nativeObjCIdTy), nullptr, {nativeObjCIdTy});

    std::vector<ModalTy> ctorFuncParamTys;
    ctorFuncParamTys.emplace_back(param->GetTy());
    auto ctorFuncTy = typeManager.GetFunctionTy(std::move(ctorFuncParamTys), target.GetTy());

    std::vector<OwnedPtr<FuncParam>> ctorParams;
    ctorParams.emplace_back(std::move(param));
    auto paramList = CreateFuncParamList(std::move(ctorParams));

    std::vector<OwnedPtr<Node>> ctorNodes;

    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.emplace_back(std::move(paramList));

    auto ctorFuncBody = CreateFuncBody(
        std::move(paramLists), CreateRefType(target), CreateBlock(std::move(ctorNodes), target.GetTy()), {ctorFuncTy});

    auto ctor = CreateFuncDecl(std::string(INIT_IDENT), std::move(ctorFuncBody), {ctorFuncTy});
    ctor->funcBody->funcDecl = ctor.get();
    ctor->constructorCall = ConstructorCall::NONE;
    ctor->funcBody->parentClassLike = &target;
    ctor->EnableAttr(Attribute::PUBLIC, Attribute::CONSTRUCTOR);

    PutDeclToClassLikeBody(*ctor, target);

    return ctor;
}

OwnedPtr<FuncDecl> ASTFactory::CreateImplCtor(FuncDecl& from)
{
    auto nativeHandleTy = bridge.GetNativeObjCIdTy();
    auto ctor = ASTCloner::Clone(Ptr(&from));
    auto curFile = ctor->curFile;

    auto& implCtorParams = ctor->funcBody->paramLists[0]->params;

    implCtorParams.insert(implCtorParams.begin(),
        WithinFile(
            CreateFuncParam(NATIVE_HANDLE_IDENT, CreateType(nativeHandleTy), nullptr, {nativeHandleTy}), curFile));

    std::vector<ModalTy> implCtorParamTys;
    std::transform(implCtorParams.begin(), implCtorParams.end(), std::back_inserter(implCtorParamTys),
        [](auto& p) { return p->type->GetTy(); });

    ctor->SetTy({typeManager.GetFunctionTy(implCtorParamTys, ctor->funcBody->retType->GetTy())});
    ctor->funcBody->SetTy(ctor->GetTy());
    ctor->funcBody->funcDecl = ctor.get();
    ctor->constructorCall = ConstructorCall::SUPER;

    return ctor;
}

bool ASTFactory::IsGeneratedMember(const Decl& decl) const
{
    return IsGeneratedNativeHandleField(decl) || IsGeneratedGetObjCClassFunction(decl) ||
        IsGeneratedHasInitedField(decl) || IsGeneratedCtor(decl) || IsGeneratedNativeHandleGetter(decl) ||
        IsObjCGeneratedMember(decl);
}

bool ASTFactory::IsGeneratedNativeHandleField(const Decl& decl) const
{
    return decl.identifier.Val() == NATIVE_HANDLE_IDENT;
}

bool ASTFactory::IsGeneratedGetObjCClassFunction(const Decl& decl) const
{
    return decl.identifier.Val() == GET_OBJ_C_CLASS_IDENT;
}

bool ASTFactory::IsGeneratedHasInitedField(const Decl& decl) const
{
    return decl.TestAttr(Attribute::HAS_INITED_FIELD);
}

bool ASTFactory::IsGeneratedBaseCtor(const Decl& decl) const
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

    if (params.size() != 1) {
        return false;
    }

    return params[0]->identifier == NATIVE_HANDLE_IDENT;
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
    CJC_ASSERT(TypeMapper::IsObjCMirror(*decl.GetTy()) || TypeMapper::IsObjCImpl(*decl.GetTy()) ||
        TypeMapper::IsSyntheticWrapper(*decl.GetTy()));

    if (TypeMapper::IsObjCImpl(decl)) {
        auto cd = StaticCast<ClassDecl>(&decl);
        auto super = cd->GetSuperClassDecl();
        CJC_NULLPTR_CHECK(super);
        return GetGeneratedBaseCtor(*super);
    }

    for (auto& member : decl.GetMemberDeclPtrs()) {
        if (auto fd = As<ASTKind::FUNC_DECL>(member); fd && IsGeneratedBaseCtor(*fd)) {
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
                if (fdParam->identifier != originParam->identifier || fdParam->GetTy() != originParam->GetTy()) {
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

OwnedPtr<CallExpr> ASTFactory::CreatePutToRegistryCall(OwnedPtr<Expr> expr)
{
    auto putToRegistryDecl = bridge.GetPutToRegistryDecl();
    auto putToRegistryExpr = CreateRefExpr(*putToRegistryDecl);

    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(expr)));

    return CreateCallExpr(std::move(putToRegistryExpr), std::move(args), putToRegistryDecl,
        putToRegistryDecl->funcBody->retType->GetTy(), CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<Expr> ASTFactory::CreateNativeLambdaForBlockType(Ty& ty, Ptr<File> curFile)
{
    const auto fty = DynamicCast<FuncTy>(&ty);
    CJC_NULLPTR_CHECK(fty);

    std::vector<ModalTy> cArgTys;
    cArgTys.emplace_back(typeManager.GetPointerTy(bridge.GetNativeBlockABIDecl()->DataTy()));
    for (const ModalTy& aty : fty->paramTys) {
        cArgTys.emplace_back(typeMapper.Cj2CType(aty.Ty()));
    }
    Ptr<Ty> cResTy = typeMapper.Cj2CType(fty->retTy.Ty());

    auto cFuncTy = typeManager.GetFunctionTy(std::move(cArgTys), {cResTy}, {.isC = true});
    std::vector<OwnedPtr<FuncParam>> lambdaParams;
    auto varIndex = 0;
    for (const ModalTy& m : cArgTys) {
        lambdaParams.push_back(CreateFuncParam("arg" + std::to_string(varIndex++), nullptr, nullptr, m));
    }
    auto getLambdaFromBlockDecl = bridge.GetObjCGetLambdaFromBlockDecl();
    auto getLambdaFromBlockRef = WithinFile(CreateRefExpr(*getLambdaFromBlockDecl), curFile);
    getLambdaFromBlockRef->instTys = { &ty };
    auto refFnTy = DynamicCast<FuncTy>(getLambdaFromBlockRef->DataTy());
    CJC_NULLPTR_CHECK(refFnTy);
    getLambdaFromBlockRef->SetTy({typeManager.GetFunctionTy(refFnTy->paramTys, {Ptr(&ty)})});
    std::vector<OwnedPtr<FuncArg>> getLambdaFromBlockArgs;
    getLambdaFromBlockArgs.push_back(CreateFuncArg(WithinFile(CreateRefExpr(*lambdaParams[0]), curFile)));
    auto cangjieFuncExpr = CreateCallExpr(std::move(getLambdaFromBlockRef), std::move(getLambdaFromBlockArgs),
        getLambdaFromBlockDecl, {Ptr(&ty)}, CallKind::CALL_DECLARED_FUNCTION);
    std::vector<OwnedPtr<FuncArg>> cangjieFuncArgs;
    for (size_t i = 1; i < lambdaParams.size(); ++i) {
        cangjieFuncArgs.push_back(CreateFuncArg(
            WrapEntity(WithinFile(CreateRefExpr(*lambdaParams[i]), curFile), *fty->paramTys[i - 1].Ty())));
    }
    auto resultCangjie = CreateCallExpr(
        std::move(cangjieFuncExpr), std::move(cangjieFuncArgs), nullptr, {cResTy}, CallKind::CALL_DECLARED_FUNCTION);
    std::vector<OwnedPtr<Node>> body;
    body.push_back(UnwrapEntity(std::move(resultCangjie)));
    auto lambda = WrapReturningLambdaExpr(typeManager, std::move(body), std::move(lambdaParams));
    lambda->SetTy({cFuncTy});

    return lambda;
}

OwnedPtr<Expr> ASTFactory::CreateObjCBlockFromLambdaCall(OwnedPtr<Expr> funcExpr)
{
    auto curFile = funcExpr->curFile;
    auto funcTy = DynamicCast<FuncTy>(funcExpr->DataTy());
    CJC_NULLPTR_CHECK(funcTy);
    auto creatorFunc = bridge.GetObjCStoreLambdaAsBlockDecl();
    auto cfuncLambda = CreateNativeLambdaForBlockType(*funcTy, curFile);
    std::vector<OwnedPtr<FuncArg>> creatorFuncArgs;
    creatorFuncArgs.push_back(CreateFuncArg(std::move(funcExpr), "", {typeManager.GetAnyTy()}));
    auto nativeAbiErasedFuncTy = typeManager.GetFunctionTy(
        {bridge.GetNativeBlockABIDecl()->GetTy()}, {typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT)}, {.isC = true});
    creatorFuncArgs.push_back(CreateFuncArg(
        CreateUnsafePointerCast(
            std::move(cfuncLambda), nativeAbiErasedFuncTy)));
    auto pointerToAbiTy = typeManager.GetPointerTy(bridge.GetCangjieBlockABIDecl()->DataTy());
    auto objcBlockDecl = bridge.GetObjCBlockDecl();
    CJC_NULLPTR_CHECK(objcBlockDecl);
    auto objcBlockTy =
        typeManager.GetInstantiatedTy(objcBlockDecl->GetTy(), GenerateTypeMapping(*objcBlockDecl, {funcTy}));

    auto nativeBlockExpr = CreateCallExpr(WithinFile(CreateRefExpr(*creatorFunc), curFile), std::move(creatorFuncArgs),
        creatorFunc, {pointerToAbiTy}, CallKind::CALL_DECLARED_FUNCTION);
    auto blockConstructor = bridge.GetObjCBlockConstructorFromCangjie();
    auto blockConstructorRef = WithinFile(CreateRefExpr(*blockConstructor), curFile);
    blockConstructorRef->instTys = {funcTy};
    blockConstructorRef->SetTy({typeManager.GetFunctionTy({{pointerToAbiTy}}, {objcBlockTy})});
    std::vector<OwnedPtr<FuncArg>> constructorArgs;
    constructorArgs.push_back(CreateFuncArg(std::move(nativeBlockExpr)));
    auto result = CreateCallExpr(std::move(blockConstructorRef), std::move(constructorArgs), blockConstructor,
        {objcBlockTy}, CallKind::CALL_OBJECT_CREATION);

    return result;
}

OwnedPtr<CallExpr> ASTFactory::CreateGetFromRegistryByNativeHandleCall(
    OwnedPtr<Expr> nativeHandle, OwnedPtr<Type> typeArg)
{
    CJC_ASSERT(nativeHandle->DataTy()->IsPointer());
    auto getFromRegistryByNativeHandleDecl = bridge.GetGetFromRegistryByNativeHandleDecl();
    auto getFromRegistryNativeHandleExpr = CreateRefExpr(*getFromRegistryByNativeHandleDecl);

    auto ty = typeArg->GetTy();
    CJC_ASSERT(TypeMapper::IsObjCImpl(*ty));
    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(nativeHandle)));

    getFromRegistryNativeHandleExpr->instTys.emplace_back(ty.Ty());
    getFromRegistryNativeHandleExpr->SetTy(typeManager.GetInstantiatedTy(getFromRegistryByNativeHandleDecl->GetTy(),
        GenerateTypeMapping(*getFromRegistryByNativeHandleDecl, getFromRegistryNativeHandleExpr->instTys)));
    getFromRegistryNativeHandleExpr->typeArguments.emplace_back(std::move(typeArg));

    return CreateCallExpr(std::move(getFromRegistryNativeHandleExpr), std::move(args),
        getFromRegistryByNativeHandleDecl, ty, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> ASTFactory::CreateGetFromRegistryByIdCall(OwnedPtr<Expr> registryId, OwnedPtr<Type> typeArg)
{
    auto getFromRegistryByIdDecl = bridge.GetGetFromRegistryByIdDecl();
    auto getFromRegistryByIdExpr = CreateRefExpr(*getFromRegistryByIdDecl);

    auto ty = typeArg->GetTy();
    CJC_ASSERT(TypeMapper::IsObjCImpl(*ty));

    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(registryId)));

    getFromRegistryByIdExpr->instTys.emplace_back(ty.Ty());
    getFromRegistryByIdExpr->SetTy(typeManager.GetInstantiatedTy(getFromRegistryByIdDecl->GetTy(),
        GenerateTypeMapping(*getFromRegistryByIdDecl, getFromRegistryByIdExpr->instTys)));
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
        removeFromRegistryDecl->funcBody->retType->GetTy(), CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<Expr> ASTFactory::CreateObjCReleaseCall(OwnedPtr<Expr> nativeHandle)
{
    auto releaseDecl = bridge.GetObjCReleaseDecl();
    auto releaseExpr = CreateRefExpr(*releaseDecl);

    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(nativeHandle)));
    return CreateCallExpr(std::move(releaseExpr), std::move(args), nullptr,
        {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)}, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<Expr> ASTFactory::CreateObjCIsKindOfClassCall(OwnedPtr<Expr> id, OwnedPtr<Expr> cls, Ptr<File> file)
{
    auto kindOfClassDecl = bridge.GetObjCIsKindOfClassDecl();
    auto kindOfClassExpr = CreateRefExpr(*kindOfClassDecl);

    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(id)));
    args.emplace_back(CreateFuncArg(std::move(cls)));
    return WithinFile(CreateCallExpr(std::move(kindOfClassExpr), std::move(args), kindOfClassDecl,
                          {typeManager.GetBoolTy()}, CallKind::CALL_DECLARED_FUNCTION),
        file);
}

OwnedPtr<Expr> ASTFactory::CreateObjCConformsToProtocolCall(OwnedPtr<Expr> id, OwnedPtr<Expr> cls, Ptr<File> file)
{
    auto conformsToProtocolDecl = bridge.GetObjCConformsToProtocolDecl();
    auto conformsToProtocolExpr = CreateRefExpr(*conformsToProtocolDecl);

    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(id)));
    args.emplace_back(CreateFuncArg(std::move(cls)));
    return WithinFile(CreateCallExpr(std::move(conformsToProtocolExpr), std::move(args), conformsToProtocolDecl,
                          {typeManager.GetBoolTy()}, CallKind::CALL_DECLARED_FUNCTION),
        file);
}

OwnedPtr<Expr> ASTFactory::CreateObjCRespondsToSelectorCall(OwnedPtr<Expr> cls, OwnedPtr<Expr> sel, Ptr<File> file)
{
    auto responseToSelDecl = bridge.GetObjCRespondsToSelectorDecl();
    auto responseToSelExpr = CreateRefExpr(*responseToSelDecl);

    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(cls)));
    args.emplace_back(CreateFuncArg(std::move(sel)));
    return WithinFile(CreateCallExpr(std::move(responseToSelExpr), std::move(args), responseToSelDecl,
                          {typeManager.GetBoolTy()}, CallKind::CALL_DECLARED_FUNCTION),
        file);
}

OwnedPtr<Expr> ASTFactory::CreateGetSuperClassExpr(OwnedPtr<Expr> objCSuper, Ptr<File> file) {
    auto getSuperClassDecl = bridge.GetGetSuperClassDecl();
    auto getSuperClassExpr = CreateRefExpr(*getSuperClassDecl);

    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(objCSuper)));
    return WithinFile(CreateCallExpr(std::move(getSuperClassExpr), std::move(args), getSuperClassDecl,
                          {bridge.GetNativeObjCClassTy()}, CallKind::CALL_DECLARED_FUNCTION),
        file);
}

OwnedPtr<Expr> ASTFactory::CreateWithObjCSuperScope(OwnedPtr<Expr> nativeHandle, ClassDecl& outerDecl, Ptr<Ty> retTy,
    std::function<std::vector<OwnedPtr<Node>>(OwnedPtr<Expr>, OwnedPtr<Expr>)> bodyFactory)
{
    CJC_ASSERT(typeMapper.IsObjCCompatible(*retTy));

    auto withObjCSuperDecl = bridge.GetWithObjCSuperDecl();
    auto unwrappedTy = typeMapper.Cj2CType(retTy);
    auto withObjCSuperRef = CreateRefExpr(*withObjCSuperDecl);
    withObjCSuperRef->instTys.emplace_back(unwrappedTy);
    withObjCSuperRef->SetTy(typeManager.GetInstantiatedTy(
        withObjCSuperDecl->GetTy(), GenerateTypeMapping(*withObjCSuperDecl, withObjCSuperRef->instTys)));
    withObjCSuperRef->typeArguments.emplace_back(CreateType(unwrappedTy));
    auto receiverParam =
        WithinFile(CreateFuncParam("receiver", nullptr, nullptr, {bridge.GetNativeObjCIdTy()}), nativeHandle->curFile);
    auto receiverRef = WithinFile(CreateRefExpr(*receiverParam), nativeHandle->curFile);

    auto objCSuperParam = WithinFile(
        CreateFuncParam("objCSuper", nullptr, nullptr, {bridge.GetNativeObjCSuperPtrTy()}), nativeHandle->curFile);
    auto objCSuperRef = WithinFile(CreateRefExpr(*objCSuperParam), nativeHandle->curFile);

    auto actionParams = Nodes<FuncParam>(std::move(receiverParam), std::move(objCSuperParam));
    auto objcname = nameGenerator.GetObjCDeclName(outerDecl);
    auto classNameExpr = CreateLitConstExpr(LitConstKind::STRING, objcname, GetStringDecl(importManager).GetTy());
    auto args = Nodes<FuncArg>(CreateFuncArg(std::move(nativeHandle)),
        CreateFuncArg(std::move(classNameExpr)),
        CreateFuncArg(WrapReturningLambdaExpr(
            typeManager, bodyFactory(std::move(receiverRef), std::move(objCSuperRef)), std::move(actionParams))));

    auto withObjCSuperFnTy = DynamicCast<FuncTy>(withObjCSuperRef->DataTy());
    CJC_NULLPTR_CHECK(withObjCSuperFnTy);
    ModalTy realRetTy = withObjCSuperFnTy->retTy;
    return CreateCallExpr(
        std::move(withObjCSuperRef), std::move(args), withObjCSuperDecl, realRetTy, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> ASTFactory::CreateObjCMsgSendCall(
    Ptr<FuncTy> ty, OwnedPtr<FuncType> funcType, std::vector<OwnedPtr<Expr>> funcArgs)
{
    auto msgSendDecl = bridge.GetObjCMsgSendDecl();
    auto msgSendExpr = CreateCallExpr(CreateRefExpr(*msgSendDecl), {}, nullptr, msgSendDecl->funcBody->retType->GetTy(),
        CallKind::CALL_DECLARED_FUNCTION);
    auto retType = funcType->retType.get();

    auto cFuncDecl = importManager.GetCoreDecl<BuiltInDecl>(std::string(CFUNC_NAME));
    CJC_NULLPTR_CHECK(cFuncDecl);
    auto cFuncRefExpr = CreateRefExpr(*cFuncDecl);

    cFuncRefExpr->SetTy({ty});
    cFuncRefExpr->typeArguments.emplace_back(std::move(funcType));

    // CFunc<...>(msgSend)

    auto cFuncCallExpr = CreateCallExpr(std::move(cFuncRefExpr), Nodes<FuncArg>(CreateFuncArg(std::move(msgSendExpr))),
        nullptr, {ty}, CallKind::CALL_FUNCTION_PTR);

    std::vector<OwnedPtr<FuncArg>> msgSendCallArgs;
    std::transform(funcArgs.begin(), funcArgs.end(), std::back_inserter(msgSendCallArgs),
        [](auto&& argExpr) { return CreateFuncArg(std::move(argExpr)); });

    // CFunc<...>(msgSend)(...)
    return CreateCallExpr(
        std::move(cFuncCallExpr), std::move(msgSendCallArgs), nullptr, retType->GetTy(), CallKind::CALL_FUNCTION_PTR);
}

OwnedPtr<CallExpr> ASTFactory::CreateObjCMsgSendCall(
    OwnedPtr<Expr> nativeHandle, const std::string& selector, Ptr<Ty> retTy, std::vector<OwnedPtr<Expr>> args)
{
    auto selectorCall = CreateGetCachedSelectorAccess(selector, nativeHandle->curFile);

    auto ft = MakeOwned<FuncType>();
    ft->retType = CreateType(retTy);

    args.insert(args.begin(), std::move(selectorCall));
    args.insert(args.begin(), std::move(nativeHandle));
    std::vector<ModalTy> paramTys;
    for (auto& param : args) {
        ft->paramTypes.emplace_back(CreateType(param->DataTy()));
        paramTys.emplace_back(param->GetTy());
    }

    auto fty = typeManager.GetFunctionTy(paramTys, {retTy}, {.isC = true});
    ft->SetTy({fty});

    return CreateObjCMsgSendCall(fty, std::move(ft), std::move(args));
}

OwnedPtr<Expr> ASTFactory::CreateGetClassCall(const ClassLikeDecl& cls, Ptr<File> curFile)
{
    auto cnameFunc = As<ASTKind::FUNC_DECL>(FindMirrorMember(GET_OBJ_C_CLASS_IDENT, cls));
    auto cnameRefExpr = CreateRefExpr(*cnameFunc);
    auto cnameCall = CreateCall(cnameFunc, curFile);
    return cnameCall;
}

OwnedPtr<Expr> ASTFactory::CreateGetClassCall(ClassLikeTy& ty, Ptr<File> curFile)
{
    auto getClassFuncDecl = bridge.GetGetClassDecl();
    auto getClassExpr = CreateRefExpr(*getClassFuncDecl);

    auto cnameAsLit = CreateLitConstExpr(LitConstKind::STRING, ty.name, GetStringDecl(importManager).GetTy());
    return CreateCall(getClassFuncDecl, curFile, std::move(cnameAsLit));
}

OwnedPtr<Expr> ASTFactory::CreateGetClassCall(std::string& className, Ptr<File> curFile)
{
    auto getClassFuncDecl = bridge.GetGetClassDecl();
    auto getClassExpr = CreateRefExpr(*getClassFuncDecl);

    auto cnameAsLit = CreateLitConstExpr(LitConstKind::STRING, className, GetStringDecl(importManager).GetTy());
    return CreateCall(getClassFuncDecl, curFile, std::move(cnameAsLit));
}

OwnedPtr<Expr> ASTFactory::CreateGetProtoCall(std::string& protoName, Ptr<File> curFile)
{
    auto getClassFuncDecl = bridge.GetGetProtoDecl();
    auto getClassExpr = CreateRefExpr(*getClassFuncDecl);

    auto cnameAsLit = CreateLitConstExpr(LitConstKind::STRING, protoName, GetStringDecl(importManager).GetTy());
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
    auto strTy = GetStringDecl(importManager).GetTy();
    auto selectorAsLit = CreateLitConstExpr(LitConstKind::STRING, selector, strTy);
    selectorAsLit->curFile = curFile;

    return CreateRegisterNameCall(std::move(selectorAsLit));
}

OwnedPtr<CallExpr> ASTFactory::CreateAllocCall(OwnedPtr<Expr> nativeClassExpr)
{
    auto allocDecl = bridge.GetAllocDecl();
    auto allocExpr = CreateRefExpr(*allocDecl);

    auto curFile = nativeClassExpr->curFile;
    return CreateCall(allocDecl, curFile, std::move(nativeClassExpr));
}

OwnedPtr<CallExpr> ASTFactory::CreateAllocCall(Decl& decl, Ptr<File> curFile)
{
    auto objcname = nameGenerator.GetObjCDeclName(decl);
    return CreateAllocCall(WithinFile(CreateGetCachedClassAccess(objcname, curFile), curFile));
}

OwnedPtr<Expr> ASTFactory::CreateMethodCallViaMsgSend(
    FuncDecl& fd, OwnedPtr<Expr> nativeHandle, std::vector<OwnedPtr<Expr>> rawArgs)
{
    auto objcname = nameGenerator.GetObjCDeclName(fd);
    auto fdFnTy = DynamicCast<FuncTy>(fd.DataTy());
    CJC_NULLPTR_CHECK(fdFnTy);
    return CreateObjCMsgSendCall(
        std::move(nativeHandle), objcname, typeMapper.Cj2CType(fdFnTy->retTy.Ty()), std::move(rawArgs));
}

OwnedPtr<Expr> ASTFactory::CreateMethodCallViaMsgSend(FuncDecl& fd, OwnedPtr<Expr> handle)
{
    CJC_NULLPTR_CHECK(fd.outerDecl);
    CJC_ASSERT(TypeMapper::IsObjCCompatible(*fd.outerDecl->GetTy()));
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
    auto objcname = nameGenerator.GetObjCGetterName(pd);
    return CreateObjCMsgSendCall(std::move(nativeHandle), objcname, typeMapper.Cj2CType(pd.DataTy()), {});
}

OwnedPtr<Expr> ASTFactory::CreatePropSetterCallViaMsgSend(PropDecl& pd, OwnedPtr<Expr> nativeHandle, OwnedPtr<Expr> arg)
{
    auto objcname = nameGenerator.GetObjCSetterName(pd);
    return CreateObjCMsgSendCall(
        std::move(nativeHandle), objcname, typeMapper.Cj2CType(pd.DataTy()), Nodes<Expr>(std::move(arg)));
}

OwnedPtr<Expr> ASTFactory::CreateFuncCallViaOpaquePointer(
    OwnedPtr<Expr> ptr,
    Ptr<Ty> retTy,
    std::vector<OwnedPtr<Expr>> args)
{
    std::vector<ModalTy> argTys;
    for (auto& rawArg : args) {
        argTys.push_back(rawArg->GetTy());
    }
    auto ty = typeManager.GetFunctionTy(std::move(argTys), {retTy}, {.isC = true});
    auto cFuncDecl = importManager.GetCoreDecl<BuiltInDecl>(std::string(CFUNC_NAME));
    CJC_NULLPTR_CHECK(cFuncDecl);
    auto cFuncRefExpr = CreateRefExpr(*cFuncDecl);

    cFuncRefExpr->SetTy({ty});
    cFuncRefExpr->instTys.emplace_back(ty);

    // CFunc<...>(ptr)

    auto cFuncCallExpr = CreateCallExpr(std::move(cFuncRefExpr), Nodes<FuncArg>(CreateFuncArg(std::move(ptr))), nullptr,
        {ty}, CallKind::CALL_FUNCTION_PTR);

    std::vector<OwnedPtr<FuncArg>> actualArgs;
    std::transform(args.begin(), args.end(), std::back_inserter(actualArgs),
        [](auto&& argExpr) { return CreateFuncArg(std::move(argExpr)); });

    // CFunc<...>(ptr)(...)
    return CreateCallExpr(
        std::move(cFuncCallExpr), std::move(actualArgs), nullptr, {retTy}, CallKind::CALL_FUNCTION_PTR);
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
        arpref->SetTy(typeManager.GetInstantiatedTy(arpdecl->GetTy(), GenerateTypeMapping(*arpdecl, arpref->instTys)));
        arpref->typeArguments.emplace_back(CreateType(unwrappedTy));
    }

    auto args = Nodes<FuncArg>(CreateFuncArg(WrapReturningLambdaExpr(typeManager, std::move(actions))));

    auto arpFnTy = DynamicCast<FuncTy>(arpref->DataTy());
    CJC_NULLPTR_CHECK(arpFnTy);
    ModalTy retTy = arpFnTy->retTy;
    return CreateCallExpr(std::move(arpref), std::move(args), arpdecl, retTy, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<CallExpr> ASTFactory::CreateGetInstanceVariableCall(const PropDecl& field, OwnedPtr<Expr> nativeHandle)
{
    Ptr<FuncDecl> getInstVarDecl;
    OwnedPtr<RefExpr> getInstVarRef;
    auto curFile = nativeHandle->curFile;
    if (typeMapper.IsObjCObjectType(*field.DataTy())) {
        getInstVarDecl = bridge.GetGetInstanceVariableObjDecl();
        getInstVarRef = CreateRefExpr(*getInstVarDecl);
    } else {
        getInstVarDecl = bridge.GetGetInstanceVariableDecl();
        getInstVarRef = CreateRefExpr(*getInstVarDecl);

        getInstVarRef->instTys.emplace_back(typeMapper.Cj2CType(field.DataTy()));
        getInstVarRef->SetTy(typeManager.GetInstantiatedTy(
            getInstVarDecl->GetTy(), GenerateTypeMapping(*getInstVarDecl, getInstVarRef->instTys)));
    }

    auto objcname = nameGenerator.GetObjCDeclName(field);
    auto nameExpr =
        WithinFile(CreateLitConstExpr(LitConstKind::STRING, objcname, GetStringDecl(importManager).GetTy()), curFile);

    auto args = Nodes<FuncArg>(CreateFuncArg(std::move(nativeHandle)), CreateFuncArg(std::move(nameExpr)));

    auto getInstVarFnTy = DynamicCast<FuncTy>(getInstVarRef->DataTy());
    CJC_NULLPTR_CHECK(getInstVarFnTy);
    ModalTy retTy = getInstVarFnTy->retTy;
    return WithinFile(CreateCallExpr(std::move(getInstVarRef), std::move(args), getInstVarDecl, retTy,
                          CallKind::CALL_DECLARED_FUNCTION),
        curFile);
}

OwnedPtr<CallExpr> ASTFactory::CreateSetInstanceVariableCall(
    const PropDecl& field, OwnedPtr<Expr> nativeHandle, OwnedPtr<Expr> value)
{
    Ptr<FuncDecl> setInstVarDecl;
    OwnedPtr<RefExpr> setInstVarRef;
    auto curFile = nativeHandle->curFile;
    if (typeMapper.IsObjCObjectType(*field.DataTy())) {
        setInstVarDecl = bridge.GetSetInstanceVariableObjDecl();
        setInstVarRef = CreateRefExpr(*setInstVarDecl);
    } else {
        setInstVarDecl = bridge.GetSetInstanceVariableDecl();
        setInstVarRef = CreateRefExpr(*setInstVarDecl);

        setInstVarRef->instTys.emplace_back(typeMapper.Cj2CType(field.DataTy()));
        setInstVarRef->SetTy(typeManager.GetInstantiatedTy(
            setInstVarDecl->GetTy(), GenerateTypeMapping(*setInstVarDecl, setInstVarRef->instTys)));
    }

    auto objcname = nameGenerator.GetObjCDeclName(field);
    auto nameExpr =
        WithinFile(CreateLitConstExpr(LitConstKind::STRING, objcname, GetStringDecl(importManager).GetTy()), curFile);

    auto args = Nodes<FuncArg>(
        CreateFuncArg(std::move(nativeHandle)), CreateFuncArg(std::move(nameExpr)), CreateFuncArg(std::move(value)));

    return WithinFile(CreateCallExpr(std::move(setInstVarRef), std::move(args), setInstVarDecl,
                          {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)}, CallKind::CALL_DECLARED_FUNCTION),
        curFile);
}

OwnedPtr<Expr> ASTFactory::CreateUnsafePointerCast(OwnedPtr<Expr> expr, Ptr<Ty> elementType)
{
    CJC_ASSERT(expr->DataTy()->IsPointer() || expr->DataTy()->IsCFunc());
    CJC_ASSERT(elementType->IsMetCType());
    auto ptrExpr = MakeOwned<PointerExpr>();
    auto pointerType = typeManager.GetPointerTy(elementType);
    CopyBasicInfo(expr, ptrExpr);
    ptrExpr->arg = CreateFuncArg(std::move(expr));
    ptrExpr->SetTy({pointerType});
    ptrExpr->type = CreateType(ptrExpr->DataTy());
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
    auto finalizerFnTy = typeManager.GetFunctionTy({}, {unitTy});
    auto fbody = CreateFuncBody({}, nullptr, CreateBlock({}, {unitTy}), {finalizerFnTy});
    fbody->paramLists.emplace_back(MakeOwned<FuncParamList>());
    auto nativeHandleExpr = CreateNativeHandleFieldExpr(target);
    auto releaseCall = CreateObjCReleaseCall(std::move(nativeHandleExpr));
    fbody->body->body.emplace_back(std::move(releaseCall));
    auto fd = CreateFuncDecl(FINALIZER_IDENT, std::move(fbody), {finalizerFnTy});
    PutDeclToClassLikeBody(*fd, target);
    fd->EnableAttr(Attribute::PRIVATE, Attribute::FINALIZER);
    fd->linkage = Linkage::EXTERNAL;
    fd->funcBody->funcDecl = fd.get();

    return fd;
}

OwnedPtr<VarDecl> ASTFactory::CreateHasInitedField(ClassDecl& target)
{
    static auto boolTy = typeManager.GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    auto initializer = CreateLitConstExpr(LitConstKind::BOOL, "false", {boolTy});
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
    auto getterFnTy = typeManager.GetFunctionTy({}, {nativeObjCIdTy});
    auto getterFuncBody = CreateFuncBody(Nodes<FuncParamList>(std::move(getterParamList)), CreateType(nativeObjCIdTy),
        CreateBlock({}, {nativeObjCIdTy}), {getterFnTy});
    auto getterDecl = CreateFuncDecl(NATIVE_HANDLE_GETTER_IDENT, std::move(getterFuncBody), {getterFnTy});
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
    return CreateMemberAccess(CreateThisRef(&target, target.DataTy(), target.curFile), *nativeHandleDecl);
}

OwnedPtr<Expr> ASTFactory::CreateMethodCallViaMsgSendSuper(
    FuncDecl& fd, OwnedPtr<Expr> receiver, OwnedPtr<Expr> objCSuper, std::vector<OwnedPtr<Expr>> rawArgs)
{
    auto objCName = nameGenerator.GetObjCDeclName(fd);
    auto fdFnTy = DynamicCast<FuncTy>(fd.DataTy());
    CJC_NULLPTR_CHECK(fdFnTy);
    return CreateObjCMsgSendSuperCall(std::move(receiver), std::move(objCSuper), objCName,
        typeMapper.Cj2CType(fdFnTy->retTy.Ty()), std::move(rawArgs));
}

OwnedPtr<Expr> ASTFactory::CreatePropGetterCallViaMsgSendSuper(
    PropDecl& pd, OwnedPtr<Expr> receiver, OwnedPtr<Expr> objCSuper)
{
    auto objCName = nameGenerator.GetObjCDeclName(pd);
    return CreateObjCMsgSendSuperCall(
        std::move(receiver), std::move(objCSuper), objCName, typeMapper.Cj2CType(pd.DataTy()), {});
}

OwnedPtr<Expr> ASTFactory::CreatePropSetterCallViaMsgSendSuper(
    PropDecl& pd, OwnedPtr<Expr> receiver, OwnedPtr<Expr> objCSuper, OwnedPtr<Expr> value)
{
    auto objCName = nameGenerator.GetObjCDeclName(pd);
    std::transform(
        objCName.begin(), objCName.begin() + 1, objCName.begin(), [](unsigned char c) { return std::toupper(c); });
    objCName = "set" + objCName + ":";
    return CreateObjCMsgSendSuperCall(std::move(receiver), std::move(objCSuper), objCName,
        typeMapper.Cj2CType(pd.DataTy()), Nodes<Expr>(std::move(value)));
}

OwnedPtr<CallExpr> ASTFactory::CreateObjCMsgSendSuperCall(OwnedPtr<Expr> receiver, OwnedPtr<Expr> objCSuper,
    const std::string& selector, Ptr<Ty> retTy, std::vector<OwnedPtr<Expr>> rawArgs)
{
    auto selCall = CreateGetCachedSelectorAccess(selector, receiver->curFile);

    auto ft = MakeOwned<FuncType>();
    ft->retType = CreateType(retTy);

    rawArgs.insert(rawArgs.begin(), ASTCloner::Clone(selCall.get()));
    rawArgs.insert(rawArgs.begin(), std::move(receiver));
    std::vector<ModalTy> paramTys;
    for (auto& param : rawArgs) {
        ft->paramTypes.emplace_back(CreateType(param->DataTy()));
        paramTys.emplace_back(param->GetTy());
    }

    auto fty = typeManager.GetFunctionTy(paramTys, {retTy}, {.isC = true});
    ft->SetTy({fty});
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
        msgSendSuperDecl->funcBody->retType->GetTy(), CallKind::CALL_DECLARED_FUNCTION);
    auto retType = funcType->retType.get();

    auto cFuncDecl = importManager.GetCoreDecl<BuiltInDecl>(std::string(CFUNC_NAME));
    CJC_NULLPTR_CHECK(cFuncDecl);
    auto cFuncRefExpr = CreateRefExpr(*cFuncDecl);

    cFuncRefExpr->SetTy({ty});
    cFuncRefExpr->typeArguments.emplace_back(std::move(funcType));

    // CFunc<...>(msgSendSuper)
    auto cFuncCallExpr = CreateCallExpr(std::move(cFuncRefExpr),
        Nodes<FuncArg>(CreateFuncArg(std::move(msgSendSuperExpr))), nullptr, {ty}, CallKind::CALL_FUNCTION_PTR);

    std::vector<OwnedPtr<FuncArg>> msgSendSuperCallArgs;
    std::transform(funcArgs.begin(), funcArgs.end(), std::back_inserter(msgSendSuperCallArgs),
        [](auto&& argExpr) { return CreateFuncArg(std::move(argExpr)); });

    // CFunc<...>(msgSendSuper)(...)
    return CreateCallExpr(std::move(cFuncCallExpr), std::move(msgSendSuperCallArgs), nullptr, retType->GetTy(),
        CallKind::CALL_FUNCTION_PTR);
}

OwnedPtr<Expr> ASTFactory::CreateObjectGetClassCall(OwnedPtr<Expr> id, Ptr<File> curFile)
{
    auto objectGetClassDecl = bridge.GetObjectGetClassDecl();
    auto objectGetClassExpr = CreateRefExpr(*objectGetClassDecl);

    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(id)));
    auto objectGetClassCallExpr = CreateCallExpr(std::move(objectGetClassExpr), std::move(args), objectGetClassDecl,
        {typeManager.GetBoolTy()}, CallKind::CALL_DECLARED_FUNCTION);
    return WithinFile(std::move(objectGetClassCallExpr), curFile);
}

OwnedPtr<Expr> ASTFactory::CreateConvertToNSStringCall(OwnedPtr<Expr> id, ClassDecl& classDecl, Ptr<File> curFile)
{
    auto convertDecl = bridge.GetConvertToNSStringDecl();
    auto convertExpr = CreateRefExpr(*convertDecl);

    std::vector<OwnedPtr<FuncArg>> args;
    args.emplace_back(CreateFuncArg(std::move(id)));

    auto nativeObjCIdTy = bridge.GetNativeObjCIdTy();
    auto convertCallExpr = CreateCallExpr(
        std::move(convertExpr), std::move(args), convertDecl, {nativeObjCIdTy}, CallKind::CALL_DECLARED_FUNCTION);

    std::vector<OwnedPtr<FuncArg>> ctorArgs;
    ctorArgs.emplace_back(CreateFuncArg(std::move(convertCallExpr)));

    auto realTarget = GetGeneratedBaseCtor(classDecl);
    return CreateThisCall(classDecl, *realTarget, realTarget->DataTy(), curFile, std::move(ctorArgs));
}

OwnedPtr<Expr> ASTFactory::CreateDescriptionAsStringCall(OwnedPtr<Expr> id)
{
    auto convertDecl = bridge.GetDescriptionAsStringDecl();
    return CreateCall(std::move(convertDecl), convertDecl->curFile, std::move(id));
}

OwnedPtr<Expr> ASTFactory::CreateObjCRetainCall(OwnedPtr<Expr> id)
{
    auto curFile = id->curFile;
    auto objCRetainDecl = bridge.GetObjCRetainDecl();
    return CreateCall(std::move(objCRetainDecl), curFile, std::move(id));
}

OwnedPtr<Expr> ASTFactory::CreateObjCRetainAutoreleasedReturnValueCall(OwnedPtr<Expr> id)
{
    auto curFile = id->curFile;
    auto objCRetainAutoreleasedReturnValueDecl = bridge.GetObjCRetainAutoreleasedReturnValue();
    return CreateCall(std::move(objCRetainAutoreleasedReturnValueDecl), curFile, std::move(id));
}

OwnedPtr<Expr> ASTFactory::CreateGetCachedSelectorAccess(std::string selector, Ptr<File> curFile)
{
    auto nativeObjCSelTy = bridge.GetNativeObjCSelDecl()->type->GetTy();
    auto&& cachedSelectors = declarationCache.cachedSelectorDecls;
    if (auto it = cachedSelectors.find(selector); it != std::end(cachedSelectors)) {
        auto& varDecl = *it->second;
        std::vector<OwnedPtr<Node>> ifNull;
        ifNull.push_back(CreateAssignExpr(WithinFile(CreateRefExpr(varDecl), curFile),
            CreateRegisterNameCall(selector, curFile), {typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT)}));
        ifNull.push_back(WithinFile(CreateRefExpr(varDecl), curFile));

        std::vector<OwnedPtr<Node>> ifNotNull;
        ifNotNull.push_back(WithinFile(CreateRefExpr(varDecl), curFile));
        return CreateIfExpr(
            CreateGetObjcEntityOrNullCall(varDecl, curFile),
            CreateBlock(std::move(ifNull), nativeObjCSelTy),
            CreateBlock(std::move(ifNotNull), nativeObjCSelTy),
            nativeObjCSelTy);
    } else {
        std::string selectorVarName = "objcSelector$";
        // selector names may contain : and $ symbols,
        // so we conservatively fix them
        for (auto ch : selector) {
            if (ch == '$') {
                selectorVarName += "$DOLLAR$";
            } else if (ch == ':') {
                selectorVarName += "$$";
            } else {
                selectorVarName += ch;
            }
        }
        auto null = MakeOwned<PointerExpr>();
        null->SetTy(nativeObjCSelTy);
        auto varDecl = CreateVar(selectorVarName, nativeObjCSelTy, true, std::move(null));
        varDecl->EnableAttr(Attribute::INTERNAL, Attribute::GLOBAL, Attribute::INITIALIZED);
        PutDeclToFile(*varDecl, *curFile);
        declarationCache.cachedSelectorDecls[selector] = std::move(varDecl);
        return CreateGetCachedSelectorAccess(selector, curFile);
    }
}

OwnedPtr<Expr> ASTFactory::CreateGetCachedClassAccess(std::string className, Ptr<File> curFile)
{
    auto nativeObjCClassTy = bridge.GetNativeObjCClassTy();
    auto&& cachedClasses = declarationCache.cachedClassDecls;
    if (auto it = cachedClasses.find(className); it != std::end(cachedClasses)) {
        auto& varDecl = *it->second;
        std::vector<OwnedPtr<Node>> ifNull;
        ifNull.push_back(CreateAssignExpr(WithinFile(CreateRefExpr(varDecl), curFile),
            CreateGetClassCall(className, curFile), {typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT)}));
        ifNull.push_back(WithinFile(CreateRefExpr(varDecl), curFile));

        std::vector<OwnedPtr<Node>> ifNotNull;
        ifNotNull.push_back(WithinFile(CreateRefExpr(varDecl), curFile));
        return CreateIfExpr(
            CreateGetObjcEntityOrNullCall(varDecl, curFile),
            CreateBlock(std::move(ifNull), nativeObjCClassTy),
            CreateBlock(std::move(ifNotNull), nativeObjCClassTy),
            nativeObjCClassTy);
    } else {
        // unlike selectors, classes are required to be valid identifiers
        std::string classVarName = "objcClass$" + className;
        auto null = MakeOwned<PointerExpr>();
        null->SetTy(nativeObjCClassTy);
        auto varDecl = CreateVar(classVarName, nativeObjCClassTy, true, std::move(null));
        varDecl->EnableAttr(Attribute::INTERNAL, Attribute::GLOBAL, Attribute::INITIALIZED);
        PutDeclToFile(*varDecl, *curFile);
        declarationCache.cachedClassDecls[className] = std::move(varDecl);
        return CreateGetCachedClassAccess(className, curFile);
    }
}

OwnedPtr<Expr> ASTFactory::CreateGetCachedClassAccess(ClassLikeTy& ty, Ptr<File> curFile)
{
    std::string name = nameGenerator.GetObjCDeclName(*Ty::GetDeclOfTy(&ty));
    return CreateGetCachedClassAccess(name, curFile);
}
