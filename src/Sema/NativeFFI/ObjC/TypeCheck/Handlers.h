// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares typecheck handlers for Objective-C mirror declarations.
 */

#ifndef CANGJIE_SEMA_OBJ_C_TYPECHECK_HANDLERS
#define CANGJIE_SEMA_OBJ_C_TYPECHECK_HANDLERS

#include "NativeFFI/ObjC/Utils/Handler.h"
#include "Context.h"

namespace Cangjie::Interop::ObjC {

class CheckInterface : public Handler<CheckInterface, TypeCheckContext> {
public:
    void HandleImpl(TypeCheckContext& ctx);
};

class CheckAbstractClass : public Handler<CheckAbstractClass, TypeCheckContext> {
public:
    void HandleImpl(TypeCheckContext& ctx);
};

class CheckInheritanceInterface : public Handler<CheckInheritanceInterface, TypeCheckContext> {
public:
    void HandleImpl(TypeCheckContext& ctx);
};

class CheckMultipleInherit : public Handler<CheckMultipleInherit, TypeCheckContext> {
public:
    void HandleImpl(TypeCheckContext& ctx);
};

class CheckMirrorSubtypeAttr : public Handler<CheckMirrorSubtypeAttr, TypeCheckContext> {
public:
    void HandleImpl(TypeCheckContext& ctx);
};

class CheckMirrorInheritMirror : public Handler<CheckMirrorInheritMirror, TypeCheckContext> {
public:
    void HandleImpl(TypeCheckContext& ctx);
};

class CheckImplInheritMirror : public Handler<CheckImplInheritMirror, TypeCheckContext> {
public:
    void HandleImpl(TypeCheckContext& ctx);
};

class CheckGeneric : public Handler<CheckGeneric, TypeCheckContext> {
public:
void HandleImpl(TypeCheckContext& ctx);
};

class CheckInitMethod : public Handler<CheckInitMethod, TypeCheckContext> {
public:
    void HandleImpl(TypeCheckContext& ctx);
};

class CheckMemberTypes : public Handler<CheckMemberTypes, TypeCheckContext> {
public:
    explicit CheckMemberTypes(InteropType interopType) : interopType(interopType)
    {
    }
    void HandleImpl(TypeCheckContext& ctx);

private:
    void CheckFuncTypes(AST::FuncDecl& fd, TypeCheckContext& ctx);
    void CheckFuncParamTypes(AST::FuncDecl& fd, TypeCheckContext& ctx);
    void CheckFuncRetType(AST::FuncDecl& fd, TypeCheckContext& ctx);

    void CheckPropTypes(AST::PropDecl& pd, TypeCheckContext& ctx);
    void CheckVarTypes(AST::VarDecl& vd, TypeCheckContext& ctx);
    std::string GetDeclInteropName();

    InteropType interopType;
};

class CheckTopLevelFuncTypes : public Handler<CheckTopLevelFuncTypes, TypeCheckContext> {
public:
    void HandleImpl(TypeCheckContext& ctx);
};

} // namespace Cangjie::Interop::ObjC

#endif // CANGJIE_SEMA_OBJ_C_TYPECHECK_HANDLERS
