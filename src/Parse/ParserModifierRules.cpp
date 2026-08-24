// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares static maps of trustlist modifiers for definitions in different scope kind.
 * SCOPE_MODIFIER_RULES contains modifier trustlists defined under different scope kinds. The trustlist contains all
 * allowed modifiers of the current scope kind and modifiers that conflict with the modifier. XXX_YYY_MODIFIERS naming
 * contains three parts. XXX means scope kind. YYY means definition kind.
 */
#include "ParserImpl.h"

#include "cangjie/AST/Node.h"
#include "cangjie/Parse/ParseModifiersRules.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))

namespace {
using namespace Cangjie;

struct ConflictRule {
    TokenKind modifier;
    const TokenKind* conflicts;
    size_t conflictCount;
};

// declare conflict rule with an array literal with its size, represented by var args
#define CR(tk, ...) {tk, (const TokenKind[]){__VA_ARGS__}, sizeof((const TokenKind[]){__VA_ARGS__}) / sizeof(TokenKind)}
#define CR_EMPTY(tk) {tk, nullptr, 0}
// declare conflict rule with named array with its size
#define CR_ARR(tk, arr) {tk, arr, ARRAY_SIZE(arr)}

const TokenKind ACCESS_PUBLIC[] = {TokenKind::PRIVATE, TokenKind::INTERNAL, TokenKind::PROTECTED};
const TokenKind ACCESS_PRIVATE[] = {TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PROTECTED};
const TokenKind ACCESS_INTERNAL[] = {TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::PROTECTED};
const TokenKind ACCESS_PROTECTED[] = {TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::INTERNAL};
const TokenKind COMMON_SPECIFIC[] = {TokenKind::SPECIFIC};
const TokenKind SPECIFIC_COMMON[] = {TokenKind::COMMON};
const TokenKind OVERRIDE_STATIC_REDEF[] = {TokenKind::STATIC, TokenKind::REDEF};
const TokenKind MUT_STATIC[] = {TokenKind::STATIC};
const TokenKind OPERATOR_STATIC[] = {TokenKind::STATIC};
const TokenKind OPEN_STATIC_REDEF[] = {TokenKind::STATIC, TokenKind::REDEF};
const TokenKind REDEF_OVERRIDE_OPEN_OPERATOR[] = {TokenKind::OVERRIDE, TokenKind::OPEN, TokenKind::OPERATOR};
const TokenKind STATIC_OVERRIDE[] = {TokenKind::OVERRIDE};
const TokenKind OVERRIDE_STATIC_REDEF2[] = {TokenKind::STATIC, TokenKind::REDEF};
const TokenKind REDEF_OVERRIDE[] = {TokenKind::OVERRIDE};
const TokenKind STATIC_OVERRIDE_OPEN[] = {TokenKind::OVERRIDE, TokenKind::OPEN};
const TokenKind REDEF_OVERRIDE_OPEN[] = {TokenKind::OVERRIDE, TokenKind::OPEN};
const TokenKind PUBLIC_SEALED[] = {TokenKind::SEALED};
const TokenKind ILLEGAL_ONLY[] = {TokenKind::ILLEGAL};

const ConflictRule PACKAGE_MODIFIERS[] = {
    CR(TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PROTECTED),
    CR(TokenKind::INTERNAL, TokenKind::PUBLIC, TokenKind::PROTECTED),
    CR(TokenKind::PROTECTED, TokenKind::PUBLIC, TokenKind::INTERNAL),
};

const ConflictRule TOPLEVEL_COMMON_MODIFIERS[] = {
    CR_ARR(TokenKind::PUBLIC, ACCESS_PUBLIC),
    CR_ARR(TokenKind::PRIVATE, ACCESS_PRIVATE),
    CR_ARR(TokenKind::INTERNAL, ACCESS_INTERNAL),
    CR_ARR(TokenKind::PROTECTED, ACCESS_PROTECTED),
};

const ConflictRule TOPLEVEL_ENUM_MODIFIERS[] = {
    CR_ARR(TokenKind::PUBLIC, ACCESS_PUBLIC),
    CR(TokenKind::PRIVATE, TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PROTECTED, TokenKind::COMMON,
        TokenKind::SPECIFIC),
    CR_ARR(TokenKind::INTERNAL, ACCESS_INTERNAL),
    CR_ARR(TokenKind::PROTECTED, ACCESS_PROTECTED),
    CR(TokenKind::COMMON, TokenKind::PRIVATE, TokenKind::SPECIFIC),
    CR(TokenKind::SPECIFIC, TokenKind::PRIVATE, TokenKind::COMMON),
};

const ConflictRule CLASS_BODY_VARIABLE_MODIFIERS[] = {
    CR_EMPTY(TokenKind::CONST),
    CR(TokenKind::STATIC, TokenKind::DEMODE),
    CR_ARR(TokenKind::PUBLIC, ACCESS_PUBLIC),
    CR_ARR(TokenKind::PRIVATE, ACCESS_PRIVATE),
    CR_ARR(TokenKind::INTERNAL, ACCESS_INTERNAL),
    CR_ARR(TokenKind::PROTECTED, ACCESS_PROTECTED),
    CR_ARR(TokenKind::COMMON, COMMON_SPECIFIC),
    CR_ARR(TokenKind::SPECIFIC, SPECIFIC_COMMON),
    CR(TokenKind::DEMODE, TokenKind::STATIC),
};

const ConflictRule TOPLEVEL_STRUCT_MODIFIERS[] = {
    CR_ARR(TokenKind::PUBLIC, ACCESS_PUBLIC),
    CR(TokenKind::PRIVATE, TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PROTECTED, TokenKind::COMMON,
        TokenKind::SPECIFIC),
    CR_ARR(TokenKind::INTERNAL, ACCESS_INTERNAL),
    CR_ARR(TokenKind::PROTECTED, ACCESS_PROTECTED),
    CR(TokenKind::COMMON, TokenKind::PRIVATE, TokenKind::SPECIFIC),
    CR(TokenKind::SPECIFIC, TokenKind::PRIVATE, TokenKind::COMMON),
};

const ConflictRule TOPLEVEL_CLASS_MODIFIERS[] = {
    CR(TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::INTERNAL, TokenKind::PROTECTED, TokenKind::FOREIGN),
    CR(TokenKind::PRIVATE, TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PROTECTED, TokenKind::SEALED,
        TokenKind::COMMON, TokenKind::SPECIFIC),
    CR(TokenKind::INTERNAL, TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::PROTECTED, TokenKind::SEALED),
    CR(TokenKind::PROTECTED, TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::INTERNAL, TokenKind::SEALED),
    CR_EMPTY(TokenKind::ABSTRACT),
    CR(TokenKind::SEALED, TokenKind::PRIVATE, TokenKind::INTERNAL, TokenKind::PROTECTED),
    CR_EMPTY(TokenKind::OPEN),
    CR(TokenKind::FOREIGN, TokenKind::PUBLIC),
    CR(TokenKind::COMMON, TokenKind::PRIVATE, TokenKind::SPECIFIC),
    CR(TokenKind::SPECIFIC, TokenKind::PRIVATE, TokenKind::COMMON),
};

const ConflictRule TOPLEVEL_INTERFACE_MODIFIERS[] = {
    CR(TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::INTERNAL, TokenKind::PROTECTED, TokenKind::FOREIGN),
    CR(TokenKind::PRIVATE, TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PROTECTED, TokenKind::SEALED,
        TokenKind::COMMON, TokenKind::SPECIFIC),
    CR(TokenKind::INTERNAL, TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::PROTECTED, TokenKind::SEALED),
    CR(TokenKind::PROTECTED, TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::INTERNAL, TokenKind::SEALED),
    CR(TokenKind::SEALED, TokenKind::PRIVATE, TokenKind::INTERNAL, TokenKind::PROTECTED),
    CR_EMPTY(TokenKind::OPEN),
    CR(TokenKind::FOREIGN, TokenKind::PUBLIC),
    CR(TokenKind::COMMON, TokenKind::PRIVATE, TokenKind::SPECIFIC),
    CR(TokenKind::SPECIFIC, TokenKind::PRIVATE, TokenKind::COMMON),
};

const ConflictRule TOPLEVEL_VARIABLE_MODIFIERS[] = {
    CR(TokenKind::CONST, TokenKind::FOREIGN),
    CR(TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::INTERNAL, TokenKind::PROTECTED, TokenKind::FOREIGN),
    CR(TokenKind::PRIVATE, TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PROTECTED, TokenKind::COMMON,
        TokenKind::SPECIFIC),
    CR_ARR(TokenKind::INTERNAL, ACCESS_INTERNAL),
    CR_ARR(TokenKind::PROTECTED, ACCESS_PROTECTED),
    CR(TokenKind::FOREIGN, TokenKind::CONST, TokenKind::PUBLIC, TokenKind::UNSAFE, TokenKind::COMMON,
        TokenKind::SPECIFIC),
    CR(TokenKind::COMMON, TokenKind::PRIVATE, TokenKind::FOREIGN, TokenKind::SPECIFIC),
    CR(TokenKind::SPECIFIC, TokenKind::PRIVATE, TokenKind::FOREIGN, TokenKind::COMMON),
};

const ConflictRule TOPLEVEL_FUNCDECL_MODIFIERS[] = {
    CR(TokenKind::CONST, TokenKind::FOREIGN),
    CR(TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::INTERNAL, TokenKind::PROTECTED, TokenKind::FOREIGN),
    CR(TokenKind::PRIVATE, TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PROTECTED, TokenKind::FOREIGN,
        TokenKind::COMMON),
    CR(TokenKind::INTERNAL, TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::PROTECTED, TokenKind::FOREIGN),
    CR(TokenKind::PROTECTED, TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::INTERNAL, TokenKind::FOREIGN),
    CR(TokenKind::FOREIGN, TokenKind::CONST, TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::INTERNAL,
        TokenKind::PROTECTED, TokenKind::UNSAFE, TokenKind::COMMON, TokenKind::EXCLAVE),
    CR(TokenKind::UNSAFE, TokenKind::FOREIGN),
    CR(TokenKind::COMMON, TokenKind::PRIVATE, TokenKind::FOREIGN, TokenKind::SPECIFIC),
    CR(TokenKind::SPECIFIC, TokenKind::PRIVATE, TokenKind::COMMON),
    CR(TokenKind::EXCLAVE, TokenKind::FOREIGN, TokenKind::ABSTRACT),
};

const ConflictRule TOPLEVEL_MACRODECL_MODIFIERS[] = {
    CR_ARR(TokenKind::PUBLIC, ACCESS_PUBLIC),
    CR_ARR(TokenKind::PRIVATE, ACCESS_PRIVATE),
    CR_ARR(TokenKind::INTERNAL, ACCESS_INTERNAL),
    CR_ARR(TokenKind::PROTECTED, ACCESS_PROTECTED),
};

const ConflictRule TOPLEVEL_MAINDECL_MODIFIERS[] = {
    CR_EMPTY(TokenKind::UNSAFE),
};

const ConflictRule CLASS_BODY_FUNCDECL_MODIFIERS[] = {
    CR(TokenKind::CONST, TokenKind::OPEN),
    CR(TokenKind::STATIC, TokenKind::OVERRIDE, TokenKind::OPEN, TokenKind::OPERATOR),
    CR_ARR(TokenKind::PUBLIC, ACCESS_PUBLIC),
    CR(TokenKind::PRIVATE, TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PROTECTED, TokenKind::OPEN),
    CR(TokenKind::INTERNAL, TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::PROTECTED, TokenKind::OPEN),
    CR_ARR(TokenKind::PROTECTED, ACCESS_PROTECTED),
    CR_ARR(TokenKind::OVERRIDE, OVERRIDE_STATIC_REDEF),
    CR_ARR(TokenKind::REDEF, REDEF_OVERRIDE_OPEN_OPERATOR),
    CR(TokenKind::ABSTRACT, TokenKind::EXCLAVE),
    CR(TokenKind::OPEN, TokenKind::CONST, TokenKind::STATIC, TokenKind::PRIVATE, TokenKind::INTERNAL, TokenKind::REDEF),
    CR_EMPTY(TokenKind::UNSAFE),
    CR_ARR(TokenKind::OPERATOR, OVERRIDE_STATIC_REDEF),
    CR_ARR(TokenKind::COMMON, COMMON_SPECIFIC),
    CR_ARR(TokenKind::SPECIFIC, SPECIFIC_COMMON),
    CR(TokenKind::EXCLAVE, TokenKind::ABSTRACT, TokenKind::FOREIGN),
};

const ConflictRule INTERFACE_BODY_FUNCDECL_MODIFIERS[] = {
    CR_EMPTY(TokenKind::CONST),
    CR(TokenKind::STATIC, TokenKind::OVERRIDE, TokenKind::OPEN, TokenKind::MUT, TokenKind::OPERATOR),
    CR_EMPTY(TokenKind::PUBLIC),
    CR_ARR(TokenKind::OVERRIDE, OVERRIDE_STATIC_REDEF),
    CR_ARR(TokenKind::REDEF, REDEF_OVERRIDE_OPEN_OPERATOR),
    CR_ARR(TokenKind::OPEN, OPEN_STATIC_REDEF),
    CR_ARR(TokenKind::MUT, MUT_STATIC),
    CR_EMPTY(TokenKind::UNSAFE),
    CR_ARR(TokenKind::OPERATOR, OPERATOR_STATIC),
    CR_ARR(TokenKind::COMMON, COMMON_SPECIFIC),
    CR_ARR(TokenKind::SPECIFIC, SPECIFIC_COMMON),
    CR(TokenKind::EXCLAVE, TokenKind::ABSTRACT),
};

const ConflictRule STRUCT_BODY_FUNCDECL_MODIFIERS[] = {
    CR(TokenKind::CONST, TokenKind::MUT),
    CR(TokenKind::STATIC, TokenKind::MUT, TokenKind::OPERATOR),
    CR_ARR(TokenKind::PUBLIC, ACCESS_PUBLIC),
    CR_ARR(TokenKind::PRIVATE, ACCESS_PRIVATE),
    CR_ARR(TokenKind::INTERNAL, ACCESS_INTERNAL),
    CR_ARR(TokenKind::PROTECTED, ACCESS_PROTECTED),
    CR_ARR(TokenKind::OVERRIDE, OVERRIDE_STATIC_REDEF),
    CR_ARR(TokenKind::REDEF, REDEF_OVERRIDE_OPEN_OPERATOR),
    CR(TokenKind::MUT, TokenKind::CONST, TokenKind::STATIC),
    CR_EMPTY(TokenKind::UNSAFE),
    CR_ARR(TokenKind::OPERATOR, OPERATOR_STATIC),
    CR_ARR(TokenKind::COMMON, COMMON_SPECIFIC),
    CR_ARR(TokenKind::SPECIFIC, SPECIFIC_COMMON),
    CR_EMPTY(TokenKind::EXCLAVE),
};

const ConflictRule ENUM_BODY_FUNCDECL_MODIFIERS[] = {
    CR_EMPTY(TokenKind::CONST),
    CR(TokenKind::STATIC, TokenKind::OPERATOR),
    CR_ARR(TokenKind::PUBLIC, ACCESS_PUBLIC),
    CR_ARR(TokenKind::PRIVATE, ACCESS_PRIVATE),
    CR_ARR(TokenKind::INTERNAL, ACCESS_INTERNAL),
    CR_ARR(TokenKind::PROTECTED, ACCESS_PROTECTED),
    CR_ARR(TokenKind::OVERRIDE, OVERRIDE_STATIC_REDEF),
    CR(TokenKind::REDEF, TokenKind::OVERRIDE, TokenKind::OPERATOR),
    CR_EMPTY(TokenKind::UNSAFE),
    CR(TokenKind::OPERATOR, TokenKind::STATIC),
    CR_ARR(TokenKind::COMMON, COMMON_SPECIFIC),
    CR_ARR(TokenKind::SPECIFIC, SPECIFIC_COMMON),
};

const ConflictRule FUNC_BODY_FUNCDECL_MODIFIERS[] = {
    CR_EMPTY(TokenKind::CONST),
    CR_EMPTY(TokenKind::UNSAFE),
    CR_EMPTY(TokenKind::EXCLAVE),
};

const ConflictRule STRUCT_BODY_VARIABLE_MODIFIERS[] = {
    CR_EMPTY(TokenKind::CONST),
    CR(TokenKind::STATIC, TokenKind::DEMODE),
    CR_ARR(TokenKind::PUBLIC, ACCESS_PUBLIC),
    CR_ARR(TokenKind::PRIVATE, ACCESS_PRIVATE),
    CR_ARR(TokenKind::INTERNAL, ACCESS_INTERNAL),
    CR_ARR(TokenKind::PROTECTED, ACCESS_PROTECTED),
    CR_ARR(TokenKind::COMMON, COMMON_SPECIFIC),
    CR_ARR(TokenKind::SPECIFIC, SPECIFIC_COMMON),
    CR(TokenKind::DEMODE, TokenKind::STATIC),
};

const ConflictRule EXTEND_BODY_FUNCDECL_MODIFIERS[] = {
    CR_EMPTY(TokenKind::CONST),
    CR(TokenKind::STATIC, TokenKind::MUT, TokenKind::OPERATOR),
    CR_ARR(TokenKind::PUBLIC, ACCESS_PUBLIC),
    CR_ARR(TokenKind::PRIVATE, ACCESS_PRIVATE),
    CR_ARR(TokenKind::INTERNAL, ACCESS_INTERNAL),
    CR_ARR(TokenKind::PROTECTED, ACCESS_PROTECTED),
    CR_ARR(TokenKind::MUT, MUT_STATIC),
    CR_EMPTY(TokenKind::UNSAFE),
    CR_ARR(TokenKind::OPERATOR, OPERATOR_STATIC),
    CR_ARR(TokenKind::COMMON, COMMON_SPECIFIC),
    CR_ARR(TokenKind::SPECIFIC, SPECIFIC_COMMON),
    CR_EMPTY(TokenKind::EXCLAVE),
};

const ConflictRule CLASS_BODY_PROP_MODIFIERS[] = {
    CR_ARR(TokenKind::STATIC, STATIC_OVERRIDE_OPEN),
    CR_ARR(TokenKind::PUBLIC, ACCESS_PUBLIC),
    CR(TokenKind::PRIVATE, TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PROTECTED, TokenKind::OPEN),
    CR(TokenKind::INTERNAL, TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::PROTECTED, TokenKind::OPEN),
    CR_ARR(TokenKind::PROTECTED, ACCESS_PROTECTED),
    CR_ARR(TokenKind::OVERRIDE, OVERRIDE_STATIC_REDEF),
    CR_ARR(TokenKind::REDEF, REDEF_OVERRIDE_OPEN),
    CR_EMPTY(TokenKind::ABSTRACT),
    CR(TokenKind::OPEN, TokenKind::STATIC, TokenKind::PRIVATE, TokenKind::INTERNAL, TokenKind::REDEF),
    CR_EMPTY(TokenKind::MUT),
    CR_ARR(TokenKind::COMMON, COMMON_SPECIFIC),
    CR_ARR(TokenKind::SPECIFIC, SPECIFIC_COMMON),
};

const ConflictRule INTERFACE_BODY_PROP_MODIFIERS[] = {
    CR_ARR(TokenKind::STATIC, STATIC_OVERRIDE_OPEN),
    CR_EMPTY(TokenKind::PUBLIC),
    CR_ARR(TokenKind::OVERRIDE, OVERRIDE_STATIC_REDEF),
    CR_ARR(TokenKind::REDEF, REDEF_OVERRIDE_OPEN),
    CR_ARR(TokenKind::OPEN, OPEN_STATIC_REDEF),
    CR_EMPTY(TokenKind::MUT),
    CR_ARR(TokenKind::COMMON, COMMON_SPECIFIC),
    CR_ARR(TokenKind::SPECIFIC, SPECIFIC_COMMON),
};

const ConflictRule STRUCT_BODY_PROP_MODIFIERS[] = {
    CR_ARR(TokenKind::STATIC, STATIC_OVERRIDE),
    CR_ARR(TokenKind::PUBLIC, ACCESS_PUBLIC),
    CR_ARR(TokenKind::PRIVATE, ACCESS_PRIVATE),
    CR_ARR(TokenKind::INTERNAL, ACCESS_INTERNAL),
    CR_ARR(TokenKind::PROTECTED, ACCESS_PROTECTED),
    CR_ARR(TokenKind::OVERRIDE, OVERRIDE_STATIC_REDEF2),
    CR_ARR(TokenKind::REDEF, REDEF_OVERRIDE),
    CR_EMPTY(TokenKind::MUT),
    CR_ARR(TokenKind::COMMON, COMMON_SPECIFIC),
    CR_ARR(TokenKind::SPECIFIC, SPECIFIC_COMMON),
};

const ConflictRule ENUM_BODY_PROP_MODIFIERS[] = {
    CR_ARR(TokenKind::STATIC, STATIC_OVERRIDE),
    CR_ARR(TokenKind::PUBLIC, ACCESS_PUBLIC),
    CR_ARR(TokenKind::PRIVATE, ACCESS_PRIVATE),
    CR_ARR(TokenKind::INTERNAL, ACCESS_INTERNAL),
    CR_ARR(TokenKind::PROTECTED, ACCESS_PROTECTED),
    CR_ARR(TokenKind::OVERRIDE, OVERRIDE_STATIC_REDEF2),
    CR_ARR(TokenKind::REDEF, REDEF_OVERRIDE),
    CR_ARR(TokenKind::COMMON, COMMON_SPECIFIC),
    CR_ARR(TokenKind::SPECIFIC, SPECIFIC_COMMON),
};

const ConflictRule EXTEND_BODY_PROP_MODIFIERS[] = {
    CR_EMPTY(TokenKind::STATIC),
    CR_ARR(TokenKind::PUBLIC, ACCESS_PUBLIC),
    CR_ARR(TokenKind::PRIVATE, ACCESS_PRIVATE),
    CR_ARR(TokenKind::INTERNAL, ACCESS_INTERNAL),
    CR_ARR(TokenKind::PROTECTED, ACCESS_PROTECTED),
    CR_EMPTY(TokenKind::MUT),
    CR_ARR(TokenKind::COMMON, COMMON_SPECIFIC),
    CR_ARR(TokenKind::SPECIFIC, SPECIFIC_COMMON),
};

// AGGREGATE means class and struct in Cangjie here.
const TokenKind AGG_PUBLIC[] = {TokenKind::STATIC, TokenKind::PRIVATE, TokenKind::INTERNAL, TokenKind::PROTECTED};
const TokenKind AGG_PRIVATE[] = {TokenKind::STATIC, TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PROTECTED};
const TokenKind AGG_INTERNAL[] = {TokenKind::STATIC, TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::PROTECTED};
const TokenKind AGG_PROTECTED[] = {TokenKind::STATIC, TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::INTERNAL};

const ConflictRule AGGREGATE_BODY_INSTANCE_INIT_MODIFIERS[] = {
    CR_EMPTY(TokenKind::CONST),
    CR_EMPTY(TokenKind::EXCLAVE),
    CR_ARR(TokenKind::PUBLIC, AGG_PUBLIC),
    CR_ARR(TokenKind::PRIVATE, AGG_PRIVATE),
    CR_ARR(TokenKind::INTERNAL, AGG_INTERNAL),
    CR_ARR(TokenKind::PROTECTED, AGG_PROTECTED),
};

const ConflictRule AGGREGATE_BODY_INIT_MODIFIERS[] = {
    CR_EMPTY(TokenKind::CONST),
    CR_EMPTY(TokenKind::EXCLAVE),
    CR(TokenKind::STATIC, TokenKind::PUBLIC, TokenKind::PRIVATE, TokenKind::INTERNAL, TokenKind::PROTECTED),
    CR_ARR(TokenKind::PUBLIC, AGG_PUBLIC),
    CR_ARR(TokenKind::PRIVATE, AGG_PRIVATE),
    CR_ARR(TokenKind::INTERNAL, AGG_INTERNAL),
    CR_ARR(TokenKind::PROTECTED, AGG_PROTECTED),
    CR_ARR(TokenKind::COMMON, COMMON_SPECIFIC),
    CR_ARR(TokenKind::SPECIFIC, SPECIFIC_COMMON),
};

const ConflictRule FUNC_BODY_VARIABLE_MODIFIERS[] = {
    CR_EMPTY(TokenKind::CONST),
};

// key: token to be checked, report when any of the token inside map value existed.
// For OPEN, check SEALED before ABSTRACT (priority order, not TokenKind order)
const ConflictRule TOPLEVEL_CLASS_WARNING_MODIFIERS[] = {
    CR_ARR(TokenKind::PUBLIC, PUBLIC_SEALED),
    CR(TokenKind::OPEN, TokenKind::SEALED, TokenKind::ABSTRACT),
};

// NOTE: interface with open (without sealed) also should report warning.
// 'ILLEGAL' used to indicate that always report warning when key token existed.
// For OPEN, check SEALED first; ILLEGAL is fallback when no other modifier matches
const ConflictRule TOPLEVEL_INTERFACE_WARNING_MODIFIERS[] = {
    CR_ARR(TokenKind::PUBLIC, PUBLIC_SEALED),
    CR(TokenKind::OPEN, TokenKind::SEALED, TokenKind::ILLEGAL),
};

const ConflictRule INTERFACE_MEMBER_WARNING_MODIFIERS[] = {
    CR_ARR(TokenKind::PUBLIC, ILLEGAL_ONLY),
    CR_ARR(TokenKind::OPEN, ILLEGAL_ONLY),
};

struct ScopeRuleEntry {
    ScopeKind scope;
    const ConflictRule* rules;
    size_t count;
};

// get size of array
#define SCOPE_ENTRY(sk, arr) {sk, arr, ARRAY_SIZE(arr)}
#define SCOPE_ENTRY_EMPTY(sk) {sk, nullptr, 0}

struct DefRuleEntry {
    DefKind def;
    const ScopeRuleEntry* scopes;
    size_t count;
};

// create a literal array with varargs, and add its size
#define DEF_SCOPES(...)                                                                                                \
    (const ScopeRuleEntry[]){__VA_ARGS__}, sizeof((const ScopeRuleEntry[]){__VA_ARGS__}) / sizeof(ScopeRuleEntry)

const DefRuleEntry SCOPE_MODIFIER_RULES[] = {
    {DefKind::FUNC,
        DEF_SCOPES(SCOPE_ENTRY(ScopeKind::TOPLEVEL, TOPLEVEL_FUNCDECL_MODIFIERS),
            SCOPE_ENTRY(ScopeKind::CLASS_BODY, CLASS_BODY_FUNCDECL_MODIFIERS),
            SCOPE_ENTRY(ScopeKind::INTERFACE_BODY, INTERFACE_BODY_FUNCDECL_MODIFIERS),
            SCOPE_ENTRY(ScopeKind::STRUCT_BODY, STRUCT_BODY_FUNCDECL_MODIFIERS),
            SCOPE_ENTRY(ScopeKind::EXTEND_BODY, EXTEND_BODY_FUNCDECL_MODIFIERS),
            SCOPE_ENTRY(ScopeKind::FUNC_BODY, FUNC_BODY_FUNCDECL_MODIFIERS),
            SCOPE_ENTRY(ScopeKind::ENUM_BODY, ENUM_BODY_FUNCDECL_MODIFIERS))},
    {DefKind::MAIN, DEF_SCOPES(SCOPE_ENTRY(ScopeKind::TOPLEVEL, TOPLEVEL_MAINDECL_MODIFIERS))},
    {DefKind::MACRO, DEF_SCOPES(SCOPE_ENTRY(ScopeKind::TOPLEVEL, TOPLEVEL_MACRODECL_MODIFIERS))},
    {DefKind::CLASS, DEF_SCOPES(SCOPE_ENTRY(ScopeKind::TOPLEVEL, TOPLEVEL_CLASS_MODIFIERS))},
    {DefKind::INTERFACE, DEF_SCOPES(SCOPE_ENTRY(ScopeKind::TOPLEVEL, TOPLEVEL_INTERFACE_MODIFIERS))},
    {DefKind::VARIABLE,
        DEF_SCOPES(SCOPE_ENTRY(ScopeKind::TOPLEVEL, TOPLEVEL_VARIABLE_MODIFIERS),
            SCOPE_ENTRY(ScopeKind::CLASS_BODY, CLASS_BODY_VARIABLE_MODIFIERS),
            SCOPE_ENTRY(ScopeKind::STRUCT_BODY, STRUCT_BODY_VARIABLE_MODIFIERS),
            SCOPE_ENTRY(ScopeKind::FUNC_BODY, FUNC_BODY_VARIABLE_MODIFIERS),
            SCOPE_ENTRY(ScopeKind::MACRO_BODY, FUNC_BODY_VARIABLE_MODIFIERS),
            SCOPE_ENTRY(ScopeKind::PRIMARY_CONSTRUCTOR_BODY_FOR_CLASS, AGGREGATE_BODY_INSTANCE_INIT_MODIFIERS),
            SCOPE_ENTRY(ScopeKind::PRIMARY_CONSTRUCTOR_BODY_FOR_STRUCT, AGGREGATE_BODY_INSTANCE_INIT_MODIFIERS))},
    {DefKind::ENUM, DEF_SCOPES(SCOPE_ENTRY(ScopeKind::TOPLEVEL, TOPLEVEL_ENUM_MODIFIERS))},
    {DefKind::STRUCT, DEF_SCOPES(SCOPE_ENTRY(ScopeKind::TOPLEVEL, TOPLEVEL_STRUCT_MODIFIERS))},
    // Empty means it doesn't support any modifier.
    {DefKind::EXTEND, DEF_SCOPES(SCOPE_ENTRY_EMPTY(ScopeKind::TOPLEVEL))},
    {DefKind::TYPE, DEF_SCOPES(SCOPE_ENTRY(ScopeKind::TOPLEVEL, TOPLEVEL_COMMON_MODIFIERS))},
    {DefKind::CONSTRUCTOR,
        DEF_SCOPES(SCOPE_ENTRY(ScopeKind::CLASS_BODY, AGGREGATE_BODY_INIT_MODIFIERS),
            SCOPE_ENTRY(ScopeKind::STRUCT_BODY, AGGREGATE_BODY_INIT_MODIFIERS))},
    {DefKind::PRIMARY_CONSTRUCTOR,
        DEF_SCOPES(SCOPE_ENTRY(ScopeKind::CLASS_BODY, AGGREGATE_BODY_INSTANCE_INIT_MODIFIERS),
            SCOPE_ENTRY(ScopeKind::STRUCT_BODY, AGGREGATE_BODY_INSTANCE_INIT_MODIFIERS))},
    {DefKind::PROPERTY,
        DEF_SCOPES(SCOPE_ENTRY(ScopeKind::CLASS_BODY, CLASS_BODY_PROP_MODIFIERS),
            SCOPE_ENTRY(ScopeKind::INTERFACE_BODY, INTERFACE_BODY_PROP_MODIFIERS),
            SCOPE_ENTRY(ScopeKind::STRUCT_BODY, STRUCT_BODY_PROP_MODIFIERS),
            SCOPE_ENTRY(ScopeKind::EXTEND_BODY, EXTEND_BODY_PROP_MODIFIERS),
            SCOPE_ENTRY(ScopeKind::ENUM_BODY, ENUM_BODY_PROP_MODIFIERS))},
    // Empty means it doesn't support any modifier.
    {DefKind::FINALIZER, DEF_SCOPES(SCOPE_ENTRY_EMPTY(ScopeKind::CLASS_BODY))},
    {DefKind::PACKAGE, DEF_SCOPES(SCOPE_ENTRY(ScopeKind::TOPLEVEL, PACKAGE_MODIFIERS))},
};

const DefRuleEntry SCOPE_MODIFIER_WARNING_MODIFIERS[] = {
    {DefKind::FUNC, DEF_SCOPES(SCOPE_ENTRY(ScopeKind::INTERFACE_BODY, INTERFACE_MEMBER_WARNING_MODIFIERS))},
    {DefKind::CLASS, DEF_SCOPES(SCOPE_ENTRY(ScopeKind::TOPLEVEL, TOPLEVEL_CLASS_WARNING_MODIFIERS))},
    {DefKind::INTERFACE, DEF_SCOPES(SCOPE_ENTRY(ScopeKind::TOPLEVEL, TOPLEVEL_INTERFACE_WARNING_MODIFIERS))},
    {DefKind::PROPERTY, DEF_SCOPES(SCOPE_ENTRY(ScopeKind::INTERFACE_BODY, INTERFACE_MEMBER_WARNING_MODIFIERS))},
};

// use binary search when data set is large
constexpr size_t BINARY_SEARCH_THRESHOLD = 12;

const ScopeRuleEntry* FindScopeInDef(const ScopeRuleEntry* scopes, size_t count, ScopeKind scopeKind)
{
    if (count >= BINARY_SEARCH_THRESHOLD) {
        int lo = 0, hi = static_cast<int>(count) - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (scopes[mid].scope == scopeKind) {
                return &scopes[mid];
            }
            if (static_cast<int>(scopes[mid].scope) < static_cast<int>(scopeKind)) {
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        return nullptr;
    }
    // linear search for small data set
    for (size_t i = 0; i < count; ++i) {
        if (scopes[i].scope == scopeKind) {
            return &scopes[i];
        }
    }
    return nullptr;
}

const ScopeRuleEntry* FindScopeEntry(const DefRuleEntry* defs, size_t defCount, DefKind defKind, ScopeKind scopeKind)
{
    const DefRuleEntry* defEntry = nullptr;
    if (defCount >= BINARY_SEARCH_THRESHOLD) {
        int lo = 0, hi = static_cast<int>(defCount) - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (defs[mid].def == defKind) {
                defEntry = &defs[mid];
                break;
            }
            if (static_cast<int>(defs[mid].def) < static_cast<int>(defKind)) {
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
    } else {
        for (size_t i = 0; i < defCount; ++i) {
            if (defs[i].def == defKind) {
                defEntry = &defs[i];
                break;
            }
        }
    }
    if (!defEntry) {
        return nullptr;
    }
    return FindScopeInDef(defEntry->scopes, defEntry->count, scopeKind);
}

const ConflictRule* FindConflictRule(const ConflictRule* rules, size_t count, TokenKind modifier)
{
    if (count >= BINARY_SEARCH_THRESHOLD) {
        int lo = 0, hi = static_cast<int>(count) - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (rules[mid].modifier == modifier) {
                return &rules[mid];
            }
            if (static_cast<int>(rules[mid].modifier) < static_cast<int>(modifier)) {
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        return nullptr;
    }
    for (size_t i = 0; i < count; ++i) {
        if (rules[i].modifier == modifier) {
            return &rules[i];
        }
    }
    return nullptr;
}
} // namespace

namespace Cangjie {

std::optional<AST::Attribute> GetAttributeByModifier(TokenKind tokenKind)
{
    if (tokenKind == TokenKind::COMMON) {
        return AST::Attribute::COMMON;
    }
    if (tokenKind == TokenKind::SPECIFIC) {
        return AST::Attribute::SPECIFIC;
    }
    if (tokenKind == TokenKind::DEMODE || tokenKind == TokenKind::INOUT) {
        return std::nullopt;
    }

    // use array index for consecutive tokens
    static const AST::Attribute ATTR_TABLE[] = {
        AST::Attribute::STATIC,
        AST::Attribute::PUBLIC,
        AST::Attribute::PRIVATE,
        AST::Attribute::INTERNAL,
        AST::Attribute::PROTECTED,
        AST::Attribute::OVERRIDE,
        AST::Attribute::REDEF,
        AST::Attribute::ABSTRACT,
        AST::Attribute::SEALED,
        AST::Attribute::OPEN,
        AST::Attribute::FOREIGN,
        AST::Attribute::STATIC, // placeholder for INOUT
        AST::Attribute::MUT,
        AST::Attribute::UNSAFE,
        AST::Attribute::OPERATOR,
    };
    constexpr int TABLE_SIZE = static_cast<int>(TokenKind::OPERATOR) - static_cast<int>(TokenKind::STATIC) + 1;

    int idx = static_cast<int>(tokenKind) - static_cast<int>(TokenKind::STATIC);
    if (idx >= 0 && idx < TABLE_SIZE) {
        return ATTR_TABLE[idx];
    }
    return std::nullopt;
}

bool HasScopeRules(DefKind defKind, ScopeKind scopeKind)
{
    return FindScopeEntry(SCOPE_MODIFIER_RULES, ARRAY_SIZE(SCOPE_MODIFIER_RULES), defKind, scopeKind) != nullptr;
}

bool IsScopeRulesEmpty(DefKind defKind, ScopeKind scopeKind)
{
    auto entry = FindScopeEntry(SCOPE_MODIFIER_RULES, ARRAY_SIZE(SCOPE_MODIFIER_RULES), defKind, scopeKind);
    return entry == nullptr || entry->count == 0;
}

bool IsModifierAllowed(DefKind defKind, ScopeKind scopeKind, TokenKind modifier)
{
    auto entry = FindScopeEntry(SCOPE_MODIFIER_RULES, ARRAY_SIZE(SCOPE_MODIFIER_RULES), defKind, scopeKind);
    if (!entry || entry->count == 0) {
        return false;
    }
    return FindConflictRule(entry->rules, entry->count, modifier) != nullptr;
}

ConflictArray GetConflictingModifiers(DefKind defKind, ScopeKind scopeKind, TokenKind modifier)
{
    auto entry = FindScopeEntry(SCOPE_MODIFIER_RULES, ARRAY_SIZE(SCOPE_MODIFIER_RULES), defKind, scopeKind);
    if (!entry) {
        return {};
    }
    auto rule = FindConflictRule(entry->rules, entry->count, modifier);
    if (!rule) {
        return {};
    }
    return {rule->conflicts, rule->conflictCount};
}

bool HasWarningRules(DefKind defKind, ScopeKind scopeKind)
{
    return FindScopeEntry(SCOPE_MODIFIER_WARNING_MODIFIERS, ARRAY_SIZE(SCOPE_MODIFIER_WARNING_MODIFIERS), defKind,
               scopeKind) != nullptr;
}

ConflictArray GetWarningConflicts(DefKind defKind, ScopeKind scopeKind, TokenKind modifier)
{
    auto entry = FindScopeEntry(
        SCOPE_MODIFIER_WARNING_MODIFIERS, ARRAY_SIZE(SCOPE_MODIFIER_WARNING_MODIFIERS), defKind, scopeKind);
    if (!entry) {
        return {};
    }
    auto rule = FindConflictRule(entry->rules, entry->count, modifier);
    if (!rule) {
        return {};
    }
    return {rule->conflicts, rule->conflictCount};
}

AST::Attribute ParserImpl::GetModifierAttr(const OwnedPtr<AST::Modifier>& modifier, AST::ASTKind kind) const
{
    if (modifier) {
        if (auto attr = GetAttributeByModifier(modifier->modifier)) {
            return *attr;
        }
    }
    if (kind == AST::ASTKind::PACKAGE_SPEC) {
        return AST::Attribute::PUBLIC;
    }
    if (kind == AST::ASTKind::IMPORT_SPEC) {
        return AST::Attribute::PRIVATE;
    }
    return AST::Attribute::INTERNAL;
}

} // namespace Cangjie
