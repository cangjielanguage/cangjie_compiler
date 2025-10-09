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

void OCFFIParserImpl::DiagObjCMirrorMemberMustHaveForeignName(const Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_objc_mirror_member_must_have_foreign_name, node);
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
