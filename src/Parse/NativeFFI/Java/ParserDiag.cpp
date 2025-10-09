// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements parser-related diagnostics for cangjie-native java FFI
 */

#include "../../ParserImpl.h"
#include "JFFIParserImpl.h"

using namespace Cangjie;
using namespace AST;

void JFFIParserImpl::DiagJavaMirrorCannotHaveFinalizer(const AST::Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_java_mirror_cannot_have_finalizer, node);
}

void JFFIParserImpl::DiagJavaMirrorCannotHavePrivateMember(const AST::Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_java_mirror_cannot_have_private_member, node);
}

void JFFIParserImpl::DiagJavaMirrorCannotHaveStaticInit(const AST::Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_java_mirror_cannot_have_static_init, node);
}

void JFFIParserImpl::DiagJavaMirrorCannotHaveConstMember(const AST::Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_java_mirror_cannot_have_const_member, node);
}

void JFFIParserImpl::DiagJavaImplCannotBeGeneric(const AST::Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_java_impl_cannot_be_generic, node);
}

void JFFIParserImpl::DiagJavaImplCannotBeAbstract(const AST::Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_java_impl_cannot_be_abstract, node);
}

void JFFIParserImpl::DiagJavaImplCannotBeSealed(const AST::Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_java_impl_cannot_be_sealed, node);
}

void JFFIParserImpl::DiagJavaMirrorCannotBeSealed(const AST::Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_java_mirror_cannot_be_sealed, node);
}

void JFFIParserImpl::DiagJavaImplCannotHaveStaticInit(const AST::Node& node) const
{
    p.ParseDiagnoseRefactor(DiagKindRefactor::parse_java_impl_cannot_have_static_init, node);
}
