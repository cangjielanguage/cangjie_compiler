// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements desugaring of ObjCPointer struct accessors
 */

#include "Handlers.h"
#include "NativeFFI/ObjC/Utils/ASTQuery.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;
using namespace Cangjie::Native::FFI;
using namespace Cangjie;

namespace {

constexpr auto WRITE_POINTER_INTRINSIC = "writePointer";
constexpr auto OBJCPOINTER_READ_METHOD = "read";
constexpr auto OBJCPOINTER_WRITE_METHOD = "write";

OwnedPtr<Expr> CreateZero(Ptr<Ty> ty)
{
    return CreateLitConstExpr(LitConstKind::INTEGER, "0", {ty});
}

/**
 * if T is CType
 * PTR.read() -> readPointer<T>(CPointer<T>(PTR.ptr))
 *
 * if T is itself ObjCPointer<U>
 * PTR.read() -> ObjCPointer<U>(readPointer<CPointer<Unit>>(CPointer<CPointer<Unit>>(PTR.ptr)))
 *
 * if T is @ObjCImpl/@ObjCMirror class
 * PTR.read() -> T(readPointer<NativeHandle>(CPointer<NativeHandle>(PTR.ptr)))
 *
 * if T is @ObjCImpl/@ObjCMirror interface -> (not implemented yet)
 * PTR.read() -> T$WrapperClass(readPointer<NativeHandle>(CPointer<NativeHandle>(PTR.ptr)))
 *
 * All these cases can be unified as
 * WRAP(readPointer<NATIVE_C_TYPE>(CPointer<NATIVE_C_TYPE>(PTR.ptr)))
 * where NATIVE_C_TYPE is the C type representation of T
 *       WRAP(expr) is the construction procedure of T from NATIVE_C_TYPE
*/
void HandleObjCPointerRead(InteropContext& ctx, CallExpr& callExpr)
{
    auto ma = As<ASTKind::MEMBER_ACCESS>(callExpr.baseFunc);
    CJC_NULLPTR_CHECK(ma);
    auto receiver = ASTCloner::Clone<Expr>(ma->baseExpr);
    CJC_NULLPTR_CHECK(receiver);
    auto elementType = receiver->DataTy()->TyArg(0);
    auto rawCType = ctx.typeMapper.Cj2CType(elementType);
    auto ptrFieldDecl = ctx.bridge.GetObjCPointerPointerField();
    CJC_ASSERT(ptrFieldDecl);

    auto ptrExpr =
        ctx.factory.CreateUnsafePointerCast(CreateMemberAccess(std::move(receiver), *ptrFieldDecl), rawCType);
    CopyBasicInfo(&callExpr, ptrExpr);
    auto call = CreateReadPointerCall(ctx.importManager, ctx.typeManager, std::move(ptrExpr));
    // We use objc_retain here, because it's not a return value of any method
    auto wrappedCall = ctx.factory.WrapEntity(std::move(call), *elementType, Retain::RETAINED);
    ctx.factory.SetDesugarExpr(&callExpr, std::move(wrappedCall));
}

/**
 * if T is CType
 * PTR.write(V) -> writePointer<T>(CPointer<T>(PTR.ptr), V)
 *
 * if T is itself ObjCPointer<U>
 * PTR.write(V) -> writePointer<CPointer<Unit>>(CPointer<CPointer<Unit>>(PTR.ptr), V.ptr)
 *
 * if T is @ObjCImpl/@ObjCMirror class
 * PTR.write(V) -> writePointer<NativeHandle>(CPointer<NativeHandle>(PTR.ptr), V.$obj)
 *
 * if T is @ObjCImpl/@ObjCMirror interface -> (not implemented yet)
 * PTR.write(V) -> writePointer<NativeHandle>(CPointer<NativeHandle>(PTR.ptr)), V.$getObj())
 *
 * All these cases can be unified as
 * writePointer<NATIVE_C_TYPE>(CPointer<NATIVE_C_TYPE>(PTR.ptr), UNWRAP(V))
 * where NATIVE_C_TYPE is the C type representation of T
 *       UNWRAP(expr) is the extraction procedure of NATIVE_C_TYPE from T
*/
void HandleObjCPointerWrite(InteropContext& ctx, CallExpr& callExpr)
{
    auto ma = As<ASTKind::MEMBER_ACCESS>(callExpr.baseFunc);
    CJC_NULLPTR_CHECK(ma);
    auto receiver = ASTCloner::Clone<Expr>(ma->baseExpr);
    CJC_ASSERT(callExpr.args.size() == 1);
    auto valueArg = ASTCloner::Clone<Expr>(callExpr.args[0]->expr);
    CJC_ASSERT_WITH_MSG(!receiver->DataTy()->typeArgs.empty(), "ObjCPointer typeArgs is empty");
    auto elementType = receiver->DataTy()->TyArg(0);
    auto rawCType = ctx.typeMapper.Cj2CType(elementType);
    Ptr<Ty> pointerType = ctx.typeManager.GetPointerTy(rawCType);
    auto ptrFieldDecl = ctx.bridge.GetObjCPointerPointerField();
    CJC_ASSERT(ptrFieldDecl);
    Ptr<Ty> int64Type = ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_INT64);
    Ptr<Ty> unitType = ctx.typeManager.GetPrimitiveTy(TypeKind::TYPE_UNIT);

    auto writePointerFunc = ctx.importManager.GetCoreDecl<FuncDecl>(WRITE_POINTER_INTRINSIC);
    CJC_NULLPTR_CHECK(writePointerFunc);
    auto writePointerRef = CreateRefExpr(*writePointerFunc, callExpr);
    writePointerRef->instTys.push_back(rawCType);
    writePointerRef->SetTy(
        {ctx.typeManager.GetFunctionTy(std::vector<ModalTy>{{pointerType}, {int64Type}, {rawCType}}, {unitType})});

    auto ptrExpr =
        ctx.factory.CreateUnsafePointerCast(CreateMemberAccess(std::move(receiver), *ptrFieldDecl), rawCType);
    CopyBasicInfo(&callExpr, ptrExpr);
    auto callArgs = std::vector<OwnedPtr<FuncArg>> {};
    callArgs.emplace_back(CreateFuncArg(std::move(ptrExpr)));
    callArgs.emplace_back(CreateFuncArg(CreateZero(int64Type)));
    callArgs.emplace_back(CreateFuncArg(ctx.factory.UnwrapEntity(std::move(valueArg))));
    auto call = WithinFile(CreateCallExpr(
        std::move(writePointerRef),
        std::move(callArgs),
        writePointerFunc,
        unitType,
        CallKind::CALL_INTRINSIC_FUNCTION), callExpr.curFile);
    ctx.factory.SetDesugarExpr(&callExpr, std::move(call));
}
}

void RewriteObjCPointerAccess::HandleImpl(InteropContext& ctx)
{
    for (auto& file : ctx.pkg.files) {
        Walker(file, Walker::GetNextWalkerID(), [&file, &ctx](auto node) {
            if (!node->IsSamePackage(*file->curPackage)) {
                return VisitAction::WALK_CHILDREN;
            }
            Ptr<CallExpr> callExpr = As<ASTKind::CALL_EXPR>(node);
            if (!callExpr) {
                return VisitAction::WALK_CHILDREN;
            }

            auto funcDecl = callExpr->resolvedFunction;
            if (!funcDecl || !funcDecl->outerDecl || !IsObjCPointer(*funcDecl->outerDecl)) {
                return VisitAction::WALK_CHILDREN;
            }

            if (funcDecl->identifier == OBJCPOINTER_READ_METHOD && callExpr->args.size() == 0) {
                CJC_ASSERT(IsObjCCompatible(*callExpr->GetTy()));
                HandleObjCPointerRead(ctx, *callExpr);
                return VisitAction::WALK_CHILDREN;
            }

            if (funcDecl->identifier == OBJCPOINTER_WRITE_METHOD && callExpr->args.size() == 1) {
                CJC_ASSERT(IsObjCCompatible(*callExpr->args[0]->GetTy()));
                HandleObjCPointerWrite(ctx, *callExpr);
                return VisitAction::WALK_CHILDREN;
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
    }
}
