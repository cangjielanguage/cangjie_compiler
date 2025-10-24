// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements parser-related diagnostics for cangjie-native Objective-C FFI
 */

#include "../../ParserImpl.h"
#include "OCFFIParserImpl.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Basic/DiagnosticEngine.h"

using namespace Cangjie;
using namespace AST;

void OCFFIParserImpl::DiagObjCMirrorCannotBeSealed(const Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_cannot_be_sealed, node);
}

void OCFFIParserImpl::DiagObjCMirrorCannotHaveFinalizer(const Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_cannot_have_finalizer, node);
}

void OCFFIParserImpl::DiagObjCMirrorMethodMustHaveForeignName(const Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_method_must_have_foreign_name, node);
}

void OCFFIParserImpl::DiagObjCMirrorCtorMustHaveForeignName(const Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_ctor_must_have_foreign_name, node);
}

void OCFFIParserImpl::DiagObjCMirrorCannotHavePrivateMember(const Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_cannot_have_private_member, node);
}

void OCFFIParserImpl::DiagObjCMirrorCannotHaveStaticInit(const Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_cannot_have_static_init, node);
}

void OCFFIParserImpl::DiagObjCMirrorCannotHaveConstMember(const Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_cannot_have_const_member, node);
}

void OCFFIParserImpl::DiagObjCMirrorFieldCannotHaveInitializer(const AST::Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_field_cannot_have_initializer, node);
}

void OCFFIParserImpl::DiagObjCMirrorCannotHavePrimaryCtor(const AST::Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_cannot_have_primary_ctor, node);
}

void OCFFIParserImpl::DiagObjCMirrorFieldCannotBeStatic(const AST::Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_field_cannot_be_static, node);
}

void OCFFIParserImpl::DiagObjCImplCannotBeGeneric(const Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_impl_cannot_be_generic, node);
}

void OCFFIParserImpl::DiagObjCInitFuncMustBeStatic(const Node& node) const
{
    CJC_ASSERT(!node.TestAttr(Attribute::STATIC));
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_init_method_must_be_static, node);
}

void OCFFIParserImpl::DiagObjCInitFuncMustBeInMirrorClass(const FuncDecl& fd) const
{
    CJC_ASSERT(!fd.outerDecl || !fd.outerDecl->TestAttr(Attribute::OBJ_C_MIRROR)
        || fd.outerDecl->astKind != ASTKind::CLASS_DECL);

    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_init_method_must_be_in_mirror_class, fd);
}

void OCFFIParserImpl::DiagObjCImplCannotBeOpen(const Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_impl_cannot_be_open, node);
}

void OCFFIParserImpl::DiagObjCImplCannotBeInterface(const Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_impl_cannot_be_interface, node);
}

void OCFFIParserImpl::DiagObjCImplCannotBeAbstract(const Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_impl_cannot_be_abstract, node);
}

void OCFFIParserImpl::DiagObjCImplCannotBeSealed(const Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_impl_cannot_be_sealed, node);
}

void OCFFIParserImpl::DiagObjCImplCannotHaveStaticInit(const Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_impl_cannot_have_static_init, node);
}

void OCFFIParserImpl::DiagObjCMirrorFuncCannotBeForeign(const FuncDecl& decl) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_func_cannot_be_foreign, decl, decl.identifier.Val());
}

void OCFFIParserImpl::DiagObjCMirrorFuncCannotBeC(const FuncDecl& decl) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_func_cannot_be_c, decl, decl.identifier.Val());
}

void OCFFIParserImpl::DiagObjCMirrorFuncCannotBeGeneric(const FuncDecl& decl) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_func_cannot_be_generic, decl, decl.identifier.Val());
}

void OCFFIParserImpl::DiagObjCMirrorFuncCannotHaveBody(const FuncDecl& decl) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_func_cannot_have_body, decl, decl.identifier.Val());
}

void OCFFIParserImpl::DiagObjCMirrorFuncMustHaveExplicitType(const FuncDecl& decl) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_func_must_have_explicit_type, decl, decl.identifier.Val());
}

void OCFFIParserImpl::DiagObjCMirrorFuncCannotBeConst(const FuncDecl& decl) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_func_cannot_be_const, decl, decl.identifier.Val());
}

void OCFFIParserImpl::DiagObjCMirrorFuncMustBeTopLevel(const FuncDecl& decl) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_func_must_be_top_level, decl, decl.identifier.Val());
}
