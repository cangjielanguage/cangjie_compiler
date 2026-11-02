// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "DesugarJArray.h"
#include "Utils.h"
#include "NativeFFI/Utils.h"

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/SafePointer.h"
#include "ExclusiveScope.h"

namespace Cangjie::Native::FFI::Java {

namespace {
using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Cangjie::Interop::Java;

bool IsJArrayConstructor(const FuncDecl& constr)
{
    bool isConstructor = constr.TestAttr(Attribute::CONSTRUCTOR);
    bool isParentJArray = constr.outerDecl && IsJArray(*constr.outerDecl);

    return isConstructor && isParentJArray;
}

bool IsSizeConstructor(const FuncDecl& constr)
{
    return IsJArrayConstructor(constr) && AreParamTypeKindsValid(constr, {TypeKind::TYPE_INT32});
}

bool IsSizeJNITypeConstructor(const FuncDecl& constr, const ImportManager& importManager)
{
    static auto strTy = GetStringDecl(importManager).GetTy();
    CJC_NULLPTR_CHECK(strTy);
    auto isSecondParamStr = [&constr] { return constr.funcBody->paramLists[0]->params[1]->GetTy() == strTy; };

    return IsJArrayConstructor(constr) &&
        AreParamTypeKindsValid(constr, {TypeKind::TYPE_INT32, TypeKind::TYPE_STRUCT}) && isSecondParamStr() &&
        constr.TestAttr(Attribute::COMPILER_ADD);
}

} // namespace

void DesugarJArray::GenerateJniTypeConstructor(AfterTypeCheckContext& ctx, ClassDecl& jarray) const
{
    CJC_ASSERT_WITH_MSG(IsJArray(jarray), "Expected JArray decl");

    OwnedPtr<FuncDecl> newConstr;

    for (auto& member : jarray.GetMemberDecls()) {
        if (const auto constr = DynamicCast<FuncDecl*>(member.get()); constr && IsSizeConstructor(*constr)) {
            newConstr = ASTCloner::Clone(Ptr(constr));
            InsertJniTypeParamIntoConstructor(*newConstr);
            InsertConstructorBody(ctx, *newConstr);
            break;
        }
    }

    jarray.GetMemberDecls().emplace_back(std::move(newConstr));
}

void DesugarJArray::InsertOriginalSizeConstructorBody(FuncDecl& constr) const
{
    if (IsSizeConstructor(constr)) {
        auto exceptionCall = CreateThrowExceptionCall(importManager, typeManager,
            "Internal error: unhandled call to JArray constructor", constr.curFile);
        constr.funcBody->body->body.emplace_back(std::move(exceptionCall));
    }
}

void DesugarJArray::TransformConstructorCallsToPassJNIParam(File& file) const
{
    Walker(&file, Walker::GetNextWalkerID(), [this, &file](auto node) {
        CJC_ASSERT_WITH_MSG(file.curPackage, "file's curPackage is nullptr");
        if (!node->IsSamePackage(*file.curPackage)) { // if node is imported
            return VisitAction::WALK_CHILDREN;
        }

        Ptr<CallExpr> callExpr = As<ASTKind::CALL_EXPR>(node);
        if (!callExpr) {
            return VisitAction::WALK_CHILDREN;
        }

        auto funcDecl = callExpr->resolvedFunction;
        if (!funcDecl || !IsSizeConstructor(*funcDecl)) {
            return VisitAction::WALK_CHILDREN;
        }

        // here funcDecl is size constructor of JArray

        static auto jniTypeConstr = FindSizeJNITypeConstructorFromInnerDecl(*funcDecl);
        CJC_NULLPTR_CHECK(jniTypeConstr);

        auto newCallExpr = ASTCloner::Clone(callExpr);
        newCallExpr->resolvedFunction = jniTypeConstr;

        ModalTy jarrayElementType;
        { // update newCallExpr->baseFunc
            // constructor call of java.lang.JArray expected to be RefExpr
            CJC_ASSERT(newCallExpr->baseFunc->astKind == ASTKind::REF_EXPR);
            auto base = StaticAs<ASTKind::REF_EXPR>(newCallExpr->baseFunc.get());
            CJC_ASSERT_WITH_MSG(base->instTys.size() == 1, "JArray type must have exactly one generic argument");
            jarrayElementType = base->instTys.front();
            base->ref.target = jniTypeConstr;
            base->SetTy(jniTypeConstr->GetTy());
        }

        CJC_ASSERT_WITH_MSG(newCallExpr->args.size() == 1, "expected to be init(length: Int32)");
        { // add jniType param in new constructor call
            auto jniType = ilib.SelectJSigByTypeKind(jarrayElementType->kind, jarrayElementType.Ty());
            auto fa = CreateFuncArg(std::move(jniType));
            newCallExpr->args.emplace_back(std::move(fa));
        }

        // update desugarArgs as it has value of cloned init(length: Int32)
        newCallExpr->desugarArgs = [&] {
            std::vector<Ptr<FuncArg>> argPtrs;
            for (auto& arg : newCallExpr->args) {
                argPtrs.emplace_back(arg.get());
            }
            return argPtrs;
        }();
        callExpr->desugarExpr = std::move(newCallExpr);

        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

void DesugarJArray::InsertJniTypeParamIntoConstructor(FuncDecl& constr) const
{
    CJC_ASSERT_WITH_MSG(IsJArray(*constr.outerDecl), "Expected JArray decl");
    CJC_ASSERT_WITH_MSG(constr.TestAttr(Attribute::CONSTRUCTOR), "'constr' argument expected to be constructor");

    auto strTy = GetStringDecl(importManager).GetTy();

    { // add new func param
        auto jniTypeParam = CreateFuncParam("$jniType", CreateType(strTy.Ty()), nullptr, strTy);
        constr.funcBody->paramLists[0]->params.emplace_back(std::move(jniTypeParam));
    }

    { // add new param ty in constr.ty->paramTys
        std::vector<ModalTy> ctorFuncParamTys;
        CJC_ASSERT(constr.GetTy()->kind == TypeKind::TYPE_FUNC);
        auto fty = StaticCast<FuncTy>(constr.DataTy());
        ctorFuncParamTys = std::move(fty->paramTys);
        ctorFuncParamTys.push_back(strTy);
        constr.SetTy(ModalTy{typeManager.GetFunctionTy(std::move(ctorFuncParamTys), constr.outerDecl->GetTy())});
    }
}

void DesugarJArray::InsertConstructorBody(AfterTypeCheckContext& ctx, FuncDecl& constr) const
{
    CJC_ASSERT_WITH_MSG(IsJArray(*constr.outerDecl), "Expected JArray decl");

    auto jniEnvPtrDecl = ilib.GetJniEnvPtrDecl();

    if (!jniEnvPtrDecl) {
        constr.EnableAttr(Attribute::IS_BROKEN);
        return;
    }

    auto jniEnvVar = CreateTmpVarDecl(jniEnvPtrDecl->type, ilib.CreateGetJniEnvCall(constr.curFile));

    // SwapLocalWithGlobalReference(Java_CFFI_newJavaArray($jniEnvPtr, $jniType, length).asJobject())
    auto newObjectCall = ilib.CreateSwapLocalWithGlobalRefCall(
        WithinFile(CreateRefExpr(*jniEnvVar), constr.curFile),
        ilib.CreateAsJniJobjectCall(
            ilib.CreateCFFINewJavaArrayCall(
                WithinFile(CreateRefExpr(*jniEnvVar), constr.curFile),
                *constr.funcBody->paramLists[0])));

    // Java_CFFI_JavaEntity($newObjectCall)
    auto javaref = ilib.CreateJavaEntityJobjectCall(std::move(newObjectCall));
    auto scopeBody = CreateBlock(Nodes(std::move(jniEnvVar), std::move(javaref)), ilib.GetJavaEntityTy());
    scopeBody->curFile = constr.curFile;
    auto exclusiveScopeCall = WrapWithExclusiveScope(ctx, std::move(scopeBody), ilib.GetJavaEntityTy());
    if (!exclusiveScopeCall) {
        constr.EnableAttr(Attribute::IS_BROKEN);
        return;
    }

    constr.constructorCall = ConstructorCall::OTHER_INIT;

    // JArray expected to be ClassLikeDecl
    CJC_ASSERT(constr.outerDecl->IsClassLikeDecl());
    auto jarray = StaticAs<ASTKind::CLASS_LIKE_DECL>(constr.outerDecl);
    static auto generatedJavaRefInitConstr = GetJavaMirrorWrappingConstructor(*jarray);
    auto thisCall = CreateThisCall(
        *constr.outerDecl, *generatedJavaRefInitConstr, generatedJavaRefInitConstr->DataTy(), constr.curFile);
    thisCall->args.push_back(CreateFuncArg(std::move(exclusiveScopeCall)));
    constr.funcBody->body->body.clear();
    constr.funcBody->body->body.push_back(std::move(thisCall));
}

Ptr<FuncDecl> DesugarJArray::FindSizeJNITypeConstructor(ClassDecl& jarray) const
{
    CJC_ASSERT_WITH_MSG(IsJArray(jarray), "Expected JArray decl");

    for (auto& member : jarray.GetMemberDecls()) {
        if (auto fd = DynamicCast<FuncDecl*>(&*member)) {
            if (IsSizeJNITypeConstructor(*fd, importManager)) {
                return fd;
            }
        }
    }

    return nullptr;
}

Ptr<FuncDecl> DesugarJArray::FindSizeJNITypeConstructorFromInnerDecl(const Decl& decl) const
{
    CJC_ASSERT(decl.outerDecl->astKind == ASTKind::CLASS_DECL); // expected to be java.lang.JArray
    auto jarray = As<ASTKind::CLASS_DECL>(decl.outerDecl);
    return FindSizeJNITypeConstructor(*jarray);
}

void DesugarJArray::ReplaceCallsWithArrayJavaEntityGet(File& file) const
{
    Walker(&file, Walker::GetNextWalkerID(), [this, &file](auto node) {
        if (!node->IsSamePackage(*file.curPackage)) {
            return VisitAction::WALK_CHILDREN;
        }

        Ptr<CallExpr> callExpr = As<ASTKind::CALL_EXPR>(node);
        if (!callExpr) {
            return VisitAction::WALK_CHILDREN;
        }

        auto funcDecl = callExpr->resolvedFunction;
        if (!funcDecl || !funcDecl->outerDecl || !IsJArray(*funcDecl->outerDecl) ||
            funcDecl->identifier != "[]" || callExpr->args.size() != 1
        ) {
            return VisitAction::WALK_CHILDREN;
        }

        auto ma = As<ASTKind::MEMBER_ACCESS>(callExpr->baseFunc);
        CJC_ASSERT_WITH_MSG(!ma->baseExpr->GetTy()->typeArgs.empty(), "JArray type must be generic");
        auto arrayElementType = ma->baseExpr->GetTy()->typeArgs[0];
        if (!arrayElementType->IsClass() && !arrayElementType->IsCoreOptionType() && !arrayElementType->IsString()) {
            return VisitAction::WALK_CHILDREN;
        }

        static auto arrayJavaEntityGetDecl = ilib.FindArrayJavaEntityGetDecl(
            *As<ASTKind::CLASS_DECL>(funcDecl->outerDecl));
        CJC_ASSERT(arrayJavaEntityGetDecl);
        auto newCallExpr = ASTCloner::Clone(callExpr);
        newCallExpr->resolvedFunction = arrayJavaEntityGetDecl;
        auto base = As<ASTKind::MEMBER_ACCESS>(newCallExpr->baseFunc);
        base->target = arrayJavaEntityGetDecl;
        base->SetTy(arrayJavaEntityGetDecl->GetTy());
        if (IsMirror(*arrayElementType)  || IsImpl(*arrayElementType) || arrayElementType->IsCoreOptionType()) {
            newCallExpr = ilib.CreateAsJniJobjectCall(std::move(newCallExpr));
            newCallExpr = ilib.CreateSwapLocalWithGlobalRefCall(ilib.CreateGetJniEnvCall(&file),
                std::move(newCallExpr));
            newCallExpr = ilib.CreateJavaEntityJobjectCall(std::move(newCallExpr));
        }
        callExpr->desugarExpr =
            ilib.UnwrapJavaEntity(std::move(newCallExpr), arrayElementType.Ty(), funcDecl->outerDecl);
        callExpr->desugarArgs = std::nullopt;

        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

void DesugarJArray::ReplaceCallsWithArrayJavaEntitySet(File& file) const
{
    Walker(&file, Walker::GetNextWalkerID(), [this, &file](auto node) {
        if (!node->IsSamePackage(*file.curPackage)) {
            return VisitAction::WALK_CHILDREN;
        }

        Ptr<CallExpr> callExpr = As<ASTKind::CALL_EXPR>(node);
        if (!callExpr) {
            return VisitAction::WALK_CHILDREN;
        }

        auto funcDecl = callExpr->resolvedFunction;
        if (!funcDecl || !funcDecl->outerDecl || !IsJArray(*funcDecl->outerDecl) ||
            funcDecl->identifier != "[]" || callExpr->args.size() != 2
        ) {
            return VisitAction::WALK_CHILDREN;
        }

        auto ma = As<ASTKind::MEMBER_ACCESS>(callExpr->baseFunc);
        CJC_ASSERT_WITH_MSG(!ma->baseExpr->GetTy()->typeArgs.empty(), "JArray type must be generic");
        auto arrayElementType = ma->baseExpr->GetTy()->typeArgs[0];
        if (!arrayElementType->IsClass() && !arrayElementType->IsCoreOptionType() && !arrayElementType->IsString()) {
            return VisitAction::WALK_CHILDREN;
        }

        static auto arrayJavaEntitySetDecl = ilib.FindArrayJavaEntitySetDecl(
            *As<ASTKind::CLASS_DECL>(funcDecl->outerDecl));
        CJC_ASSERT(arrayJavaEntitySetDecl);
        auto newCallExpr = ASTCloner::Clone(callExpr);
        newCallExpr->resolvedFunction = arrayJavaEntitySetDecl;

        newCallExpr->args[1] = ASTCloner::Clone(newCallExpr->args[1].get());
        newCallExpr->args[1]->expr = ilib.WrapJavaEntity(std::move(newCallExpr->args[1]->expr));
        newCallExpr->desugarArgs = std::nullopt;
        callExpr->desugarArgs = std::nullopt;
        auto base = As<ASTKind::MEMBER_ACCESS>(newCallExpr->baseFunc);
        base->target = arrayJavaEntitySetDecl;
        base->SetTy(arrayJavaEntitySetDecl->GetTy());
        callExpr->args[1] = ASTCloner::Clone(newCallExpr->args[1].get());

        callExpr->baseFunc = ASTCloner::Clone(newCallExpr->baseFunc.get());
        callExpr->resolvedFunction = newCallExpr->resolvedFunction;
        callExpr->desugarExpr = std::move(newCallExpr);

        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

DesugarJArray::DesugarJArray(
    TypeManager& typeManager,
    const ImportManager& importManager,
    InteropLibBridge& ilib) : typeManager(typeManager), importManager(importManager), ilib(ilib)
{
}

void DesugarJArray::Process(AfterTypeCheckContext& ctx)
{
    for (auto& mirror : ctx.GetJavaMirrors()) {
        if (!IsJArray(*mirror)) {
            continue;
        }

        auto& jArray = *StaticAs<ASTKind::CLASS_DECL>(mirror);
        for (auto member : jArray.GetMemberDeclPtrs()) {
            auto fd = As<ASTKind::FUNC_DECL>(member);
            if (!fd) {
                continue;
            }
            InsertOriginalSizeConstructorBody(*fd);
        }
        GenerateJniTypeConstructor(ctx, jArray);
    }

    for (auto& file : ctx.pkg.files) {
        ReplaceCallsWithArrayJavaEntityGet(*file);
        ReplaceCallsWithArrayJavaEntitySet(*file);
        TransformConstructorCallsToPassJNIParam(*file);
    }
}

}
