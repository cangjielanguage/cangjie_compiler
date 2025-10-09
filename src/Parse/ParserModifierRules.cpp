// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares static maps of trustlist modifiers for definitions in different scope kind. SCOPE_MODIFIER_RULES
 * contains modifier trustlists defined under different scope kinds. The trustlist contains all allowed modifiers of the
 * current scope kind and modifiers that conflict with the modifier. XXX_YYY_MODIFIERS naming contains three parts. XXX
 * means scope kind. YYY means definition kind.
 */

#include "ParserImpl.h"

#include "cangjie/Parse/ParseModifiersRules.h"

namespace {
using namespace Cangjie;
// TokenKind to Attribute Map.
static const std::unordered_map<TokenKind, AST::Attribute> TK2ATTRMAP = {
    {TokenKind::STATIC, AST::Attribute::STATIC},
    {TokenKind::PUBLIC, AST::Attribute::PUBLIC},
    {TokenKind::PRIVATE, AST::Attribute::PRIVATE},
    {TokenKind::INTERNAL, AST::Attribute::INTERNAL},
    {TokenKind::PROTECTED, AST::Attribute::PROTECTED},
    {TokenKind::COMMON, AST::Attribute::COMMON},
    {TokenKind::PLATFORM, AST::Attribute::PLATFORM},
    {TokenKind::OVERRIDE, AST::Attribute::OVERRIDE},
    {TokenKind::REDEF, AST::Attribute::REDEF},
    {TokenKind::ABSTRACT, AST::Attribute::ABSTRACT},
    {TokenKind::SEALED, AST::Attribute::SEALED},
    {TokenKind::OPEN, AST::Attribute::OPEN},
    {TokenKind::FOREIGN, AST::Attribute::FOREIGN},
    {TokenKind::UNSAFE, AST::Attribute::UNSAFE},
    {TokenKind::MUT, AST::Attribute::MUT},
    {TokenKind::OPERATOR, AST::Attribute::OPERATOR},
};
} // namespace

namespace Cangjie {
std::optional<AST::Attribute> GetAttributeByModifier(TokenKind tokenKind)
{
    auto found = TK2ATTRMAP.find(tokenKind);
    return found == TK2ATTRMAP.end() ? std::optional<AST::Attribute>{} : std::optional<AST::Attribute>{found->second};
}

const static std::unordered_map<TokenKind, std::vector<TokenKind>> PACKAGE_MODIFIERS = {
    {TokenKind::PUBLIC, {TokenKind::PROTECTED, TokenKind::INTERNAL}},
    {TokenKind::PROTECTED, {TokenKind::PUBLIC, TokenKind::INTERNAL}},
    {TokenKind::INTERNAL, {TokenKind::PUBLIC, TokenKind::PROTECTED}},
};

#define ACCESSIBLE_MODIFIERS \
    {TokenKind::PUBLIC, {TokenKind::PROTECTED, TokenKind::INTERNAL, TokenKind::PRIVATE}}, \
    {TokenKind::PROTECTED, {TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PRIVATE}}, \
    {TokenKind::INTERNAL, {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::PRIVATE}}, \
    {TokenKind::PRIVATE, {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::INTERNAL}},

const static std::unordered_map<TokenKind, std::vector<TokenKind>> TOPLEVEL_COMMON_MODIFIERS = {
    ACCESSIBLE_MODIFIERS
};

#define ACCESSIBLE_MODIFIERS_TOPLEVEL                                                                            \
    {TokenKind::PUBLIC, {TokenKind::PROTECTED, TokenKind::INTERNAL, TokenKind::PRIVATE}},                        \
    {TokenKind::PROTECTED, {TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PRIVATE}},                        \
    {TokenKind::INTERNAL, {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::PRIVATE}},                        \
    {TokenKind::PRIVATE,                                                                                         \
        {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::INTERNAL, TokenKind::PLATFORM, TokenKind::COMMON}}, \
    {TokenKind::COMMON, {TokenKind::PLATFORM, TokenKind::PRIVATE}},                                              \
    {TokenKind::PLATFORM, {TokenKind::COMMON, TokenKind::PRIVATE}}

const static std::unordered_map<TokenKind, std::vector<TokenKind>> TOPLEVEL_ENUM_MODIFIERS = {
    ACCESSIBLE_MODIFIERS_TOPLEVEL
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> CLASS_BODY_VARIABLE_MODIFIERS = {
    ACCESSIBLE_MODIFIERS
    {TokenKind::STATIC, {}},
    {TokenKind::CONST, {TokenKind::COMMON, TokenKind::PLATFORM}},
    {TokenKind::COMMON, {TokenKind::PLATFORM, TokenKind::CONST}},
    {TokenKind::PLATFORM, {TokenKind::COMMON, TokenKind::CONST}},
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> TOPLEVEL_STRUCT_MODIFIERS = {
    ACCESSIBLE_MODIFIERS_TOPLEVEL
};

#define ACCESSIBLE_MODIFIERS_WITH_FOREIGN \
    {TokenKind::FOREIGN, {TokenKind::PUBLIC}}, \
    {TokenKind::PUBLIC, {TokenKind::FOREIGN, TokenKind::PROTECTED, TokenKind::INTERNAL, TokenKind::PRIVATE}}, \
    {TokenKind::PROTECTED, {TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PRIVATE, TokenKind::SEALED}}, \
    {TokenKind::INTERNAL, {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::PRIVATE, TokenKind::SEALED}}, \
    {TokenKind::PRIVATE, {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::INTERNAL, TokenKind::SEALED, \
        TokenKind::COMMON, TokenKind::PLATFORM}}, \
    {TokenKind::SEALED, {TokenKind::PROTECTED, TokenKind::INTERNAL, TokenKind::PRIVATE}},

const static std::unordered_map<TokenKind, std::vector<TokenKind>> TOPLEVEL_CLASS_MODIFIERS = {
    ACCESSIBLE_MODIFIERS_WITH_FOREIGN
    {TokenKind::ABSTRACT, {}},
    {TokenKind::OPEN, {}},
    {TokenKind::COMMON, {TokenKind::PLATFORM, TokenKind::PRIVATE}},
    {TokenKind::PLATFORM, {TokenKind::COMMON, TokenKind::PRIVATE}},
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> TOPLEVEL_INTERFACE_MODIFIERS = {
    ACCESSIBLE_MODIFIERS_WITH_FOREIGN
    {TokenKind::OPEN, {}},
    {TokenKind::COMMON, {TokenKind::PLATFORM, TokenKind::PRIVATE}},
    {TokenKind::PLATFORM, {TokenKind::COMMON, TokenKind::PRIVATE}},
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> TOPLEVEL_VARIABLE_MODIFIERS = {
    {TokenKind::FOREIGN,
        {TokenKind::UNSAFE, TokenKind::PUBLIC, TokenKind::CONST, TokenKind::PLATFORM, TokenKind::COMMON}},
    {TokenKind::PUBLIC, {TokenKind::FOREIGN, TokenKind::PROTECTED, TokenKind::INTERNAL, TokenKind::PRIVATE}},
    {TokenKind::PROTECTED, {TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PRIVATE}},
    {TokenKind::INTERNAL, {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::PRIVATE}},
    {TokenKind::PRIVATE,
        {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::INTERNAL, TokenKind::PLATFORM, TokenKind::COMMON}},
    {TokenKind::CONST, {TokenKind::FOREIGN, TokenKind::PLATFORM, TokenKind::COMMON}},
    {TokenKind::COMMON, {TokenKind::PLATFORM, TokenKind::PRIVATE, TokenKind::CONST, TokenKind::FOREIGN}},
    {TokenKind::PLATFORM, {TokenKind::COMMON, TokenKind::PRIVATE, TokenKind::CONST, TokenKind::FOREIGN}},
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> TOPLEVEL_FUNCDECL_MODIFIERS = {
    {TokenKind::FOREIGN,
        {TokenKind::UNSAFE, TokenKind::PUBLIC, TokenKind::CONST, TokenKind::INTERNAL, TokenKind::PROTECTED,
            TokenKind::PRIVATE, TokenKind::COMMON}},
    {TokenKind::PUBLIC, {TokenKind::FOREIGN, TokenKind::PROTECTED, TokenKind::INTERNAL, TokenKind::PRIVATE}},
    {TokenKind::PROTECTED, {TokenKind::FOREIGN, TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PRIVATE}},
    {TokenKind::INTERNAL, {TokenKind::FOREIGN, TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::PRIVATE}},
    {TokenKind::PRIVATE, {TokenKind::FOREIGN, TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::INTERNAL,
        TokenKind::COMMON}},
    {TokenKind::UNSAFE, {TokenKind::FOREIGN}},
    {TokenKind::CONST, {TokenKind::FOREIGN, TokenKind::COMMON, TokenKind::PLATFORM}},
    {TokenKind::COMMON, {TokenKind::PLATFORM, TokenKind::PRIVATE, TokenKind::FOREIGN, TokenKind::CONST}},
    {TokenKind::PLATFORM, {TokenKind::COMMON, TokenKind::PRIVATE, TokenKind::CONST}},
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> TOPLEVEL_MACRODECL_MODIFIERS = {
    ACCESSIBLE_MODIFIERS
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> TOPLEVEL_MAINDECL_MODIFIERS = {
    {TokenKind::UNSAFE, {}},
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> CLASS_BODY_FUNCDECL_MODIFIERS = {
    {TokenKind::PUBLIC, {TokenKind::PROTECTED, TokenKind::INTERNAL, TokenKind::PRIVATE}},
    {TokenKind::PROTECTED, {TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PRIVATE}},
    {TokenKind::PRIVATE, {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::INTERNAL, TokenKind::OPEN}},
    {TokenKind::INTERNAL, {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::PRIVATE, TokenKind::OPEN}},
    {TokenKind::STATIC, {TokenKind::OPEN, TokenKind::OVERRIDE, TokenKind::OPERATOR}},
    {TokenKind::REDEF, {TokenKind::OPEN, TokenKind::OVERRIDE, TokenKind::OPERATOR}},
    {TokenKind::OPEN, {TokenKind::STATIC, TokenKind::INTERNAL, TokenKind::PRIVATE, TokenKind::REDEF, TokenKind::CONST}},
    {TokenKind::OVERRIDE, {TokenKind::STATIC, TokenKind::REDEF}},
    {TokenKind::OPERATOR, {TokenKind::STATIC, TokenKind::REDEF}},
    {TokenKind::UNSAFE, {}},
    {TokenKind::CONST, {TokenKind::OPEN, TokenKind::COMMON, TokenKind::PLATFORM}},
    {TokenKind::COMMON, {TokenKind::PLATFORM, TokenKind::CONST}},
    {TokenKind::PLATFORM, {TokenKind::COMMON, TokenKind::CONST}},
    {TokenKind::ABSTRACT, {}}
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> INTERFACE_BODY_FUNCDECL_MODIFIERS = {
    {TokenKind::STATIC, {TokenKind::OPERATOR, TokenKind::MUT, TokenKind::OVERRIDE, TokenKind::OPEN}},
    {TokenKind::REDEF, {TokenKind::OPEN, TokenKind::OVERRIDE, TokenKind::OPERATOR}},
    {TokenKind::OPERATOR, {TokenKind::STATIC}},
    {TokenKind::MUT, {TokenKind::STATIC}},
    {TokenKind::UNSAFE, {}},
    {TokenKind::OVERRIDE, {TokenKind::STATIC, TokenKind::REDEF}},
    {TokenKind::OPEN, {TokenKind::STATIC, TokenKind::REDEF}},
    {TokenKind::CONST, {TokenKind::COMMON, TokenKind::PLATFORM}},
    {TokenKind::PUBLIC, {}},
    {TokenKind::COMMON, {TokenKind::PLATFORM, TokenKind::CONST}},
    {TokenKind::PLATFORM, {TokenKind::COMMON, TokenKind::CONST}}
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> STRUCT_BODY_FUNCDECL_MODIFIERS = {
    ACCESSIBLE_MODIFIERS
    {TokenKind::STATIC, {TokenKind::OPERATOR, TokenKind::MUT}},
    {TokenKind::OPERATOR, {TokenKind::STATIC}},
    {TokenKind::MUT, {TokenKind::STATIC, TokenKind::CONST}},
    {TokenKind::UNSAFE, {}},
    {TokenKind::OVERRIDE, {TokenKind::STATIC, TokenKind::REDEF}},
    {TokenKind::REDEF, {TokenKind::OPEN, TokenKind::OVERRIDE, TokenKind::OPERATOR}},
    {TokenKind::CONST, {TokenKind::MUT, TokenKind::COMMON, TokenKind::PLATFORM}},
    {TokenKind::COMMON, {TokenKind::PLATFORM, TokenKind::CONST}},
    {TokenKind::PLATFORM, {TokenKind::COMMON, TokenKind::CONST}},
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> ENUM_BODY_FUNCDECL_MODIFIERS = {
    ACCESSIBLE_MODIFIERS
    {TokenKind::STATIC, {TokenKind::OPERATOR}},
    {TokenKind::OPERATOR, {TokenKind::STATIC}},
    {TokenKind::UNSAFE, {}},
    {TokenKind::OVERRIDE, {TokenKind::STATIC, TokenKind::REDEF}},
    {TokenKind::REDEF, {TokenKind::OVERRIDE, TokenKind::OPERATOR}},
    {TokenKind::CONST, {TokenKind::COMMON, TokenKind::PLATFORM}},
    {TokenKind::COMMON, {TokenKind::PLATFORM, TokenKind::CONST}},
    {TokenKind::PLATFORM, {TokenKind::COMMON, TokenKind::CONST}}
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> FUNC_BODY_FUNCDECL_MODIFIERS = {
    {TokenKind::UNSAFE, {}},
    {TokenKind::CONST, {}},
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> STRUCT_BODY_VARIABLE_MODIFIERS = {
    ACCESSIBLE_MODIFIERS
    {TokenKind::STATIC, {}},
    {TokenKind::CONST, {}},
    {TokenKind::CONST, {TokenKind::COMMON, TokenKind::PLATFORM}},
    {TokenKind::COMMON, {TokenKind::PLATFORM, TokenKind::CONST}},
    {TokenKind::PLATFORM, {TokenKind::COMMON, TokenKind::CONST}}
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> EXTEND_BODY_FUNCDECL_MODIFIERS = {
    ACCESSIBLE_MODIFIERS
    {TokenKind::OPERATOR, {TokenKind::STATIC}},
    {TokenKind::MUT, {TokenKind::STATIC}},
    {TokenKind::STATIC, {TokenKind::MUT, TokenKind::OPERATOR}},
    {TokenKind::UNSAFE, {}},
    {TokenKind::CONST, {}},
    {TokenKind::COMMON, {TokenKind::PLATFORM}},
    {TokenKind::PLATFORM, {TokenKind::COMMON}}
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> CLASS_BODY_PROP_MODIFIERS = {
    {TokenKind::PUBLIC, {TokenKind::PROTECTED, TokenKind::INTERNAL, TokenKind::PRIVATE}},
    {TokenKind::PROTECTED, {TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PRIVATE}},
    {TokenKind::INTERNAL, {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::PRIVATE, TokenKind::OPEN}},
    {TokenKind::PRIVATE, {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::INTERNAL, TokenKind::OPEN}},
    {TokenKind::STATIC, {TokenKind::OPEN, TokenKind::OVERRIDE}},
    {TokenKind::REDEF, {TokenKind::OPEN, TokenKind::OVERRIDE}},
    {TokenKind::OPEN, {TokenKind::STATIC, TokenKind::INTERNAL, TokenKind::PRIVATE, TokenKind::REDEF}},
    {TokenKind::OVERRIDE, {TokenKind::STATIC, TokenKind::REDEF}},
    {TokenKind::MUT, {}},
    {TokenKind::COMMON, {TokenKind::PLATFORM}},
    {TokenKind::PLATFORM, {TokenKind::COMMON}},
    {TokenKind::ABSTRACT, {}},
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> INTERFACE_BODY_PROP_MODIFIERS = {
    {TokenKind::STATIC, {TokenKind::OPEN, TokenKind::OVERRIDE}},
    {TokenKind::REDEF, {TokenKind::OPEN, TokenKind::OVERRIDE}},
    {TokenKind::OVERRIDE, {TokenKind::STATIC, TokenKind::REDEF}},
    {TokenKind::OPEN, {TokenKind::STATIC, TokenKind::REDEF}},
    {TokenKind::MUT, {}},
    {TokenKind::PUBLIC, {}},
    {TokenKind::COMMON, {TokenKind::PLATFORM}},
    {TokenKind::PLATFORM, {TokenKind::COMMON}},
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> STRUCT_BODY_PROP_MODIFIERS = {
    ACCESSIBLE_MODIFIERS
    {TokenKind::STATIC, {TokenKind::OVERRIDE}},
    {TokenKind::OVERRIDE, {TokenKind::STATIC, TokenKind::REDEF}},
    {TokenKind::REDEF, {TokenKind::OVERRIDE}},
    {TokenKind::MUT, {}},
    {TokenKind::COMMON, {TokenKind::PLATFORM}},
    {TokenKind::PLATFORM, {TokenKind::COMMON}},
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> ENUM_BODY_PROP_MODIFIERS = {
    ACCESSIBLE_MODIFIERS
    {TokenKind::STATIC, {TokenKind::OVERRIDE}},
    {TokenKind::OVERRIDE, {TokenKind::STATIC, TokenKind::REDEF}},
    {TokenKind::REDEF, {TokenKind::OVERRIDE}},
    {TokenKind::COMMON, {TokenKind::PLATFORM}},
    {TokenKind::PLATFORM, {TokenKind::COMMON}},
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> EXTEND_BODY_PROP_MODIFIERS = {
    ACCESSIBLE_MODIFIERS
    {TokenKind::STATIC, {}},
    {TokenKind::MUT, {}},
    {TokenKind::COMMON, {TokenKind::PLATFORM}},
    {TokenKind::PLATFORM, {TokenKind::COMMON}},
};

// AGGREGATE means class and struct in Cangjie here.
#define AGGREGATE_BODY_INSTANCE_INIT_MODIFIERS \
    {TokenKind::PUBLIC, {TokenKind::PROTECTED, TokenKind::INTERNAL, TokenKind::PRIVATE, TokenKind::STATIC}}, \
    {TokenKind::PROTECTED, {TokenKind::PUBLIC, TokenKind::INTERNAL, TokenKind::PRIVATE, TokenKind::STATIC}}, \
    {TokenKind::INTERNAL, {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::PRIVATE, TokenKind::STATIC}}, \
    {TokenKind::PRIVATE, {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::INTERNAL, TokenKind::STATIC}}, \
    {TokenKind::CONST, {}}, \
    {TokenKind::COMMON, {TokenKind::PLATFORM}}, \
    {TokenKind::PLATFORM, {TokenKind::COMMON}},

#define AGGREGATE_BODY_INIT_MODIFIERS \
    AGGREGATE_BODY_INSTANCE_INIT_MODIFIERS \
    {TokenKind::STATIC, {TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::INTERNAL, TokenKind::PRIVATE}},

/**
 * SCOPE_MODIFIER_RULES contains modifier trustlists defined under different scope kinds. The trustlist contains all
 * allowed modifiers of the current scope kind and modifiers that conflict with the modifier.
 */
const static std::unordered_map<DefKind,
    std::unordered_map<ScopeKind, std::unordered_map<TokenKind, std::vector<TokenKind>>>>
    SCOPE_MODIFIER_RULES = {
        {DefKind::PACKAGE, {{ScopeKind::TOPLEVEL, PACKAGE_MODIFIERS}}},
        {
            DefKind::FUNC,
            {
                {ScopeKind::TOPLEVEL, TOPLEVEL_FUNCDECL_MODIFIERS},
                {ScopeKind::CLASS_BODY, CLASS_BODY_FUNCDECL_MODIFIERS},
                {ScopeKind::INTERFACE_BODY, INTERFACE_BODY_FUNCDECL_MODIFIERS},
                {ScopeKind::STRUCT_BODY, STRUCT_BODY_FUNCDECL_MODIFIERS},
                {ScopeKind::EXTEND_BODY, EXTEND_BODY_FUNCDECL_MODIFIERS},
                {ScopeKind::ENUM_BODY, ENUM_BODY_FUNCDECL_MODIFIERS},
                {ScopeKind::FUNC_BODY, FUNC_BODY_FUNCDECL_MODIFIERS},
            },
        },
        {DefKind::MACRO, {{ScopeKind::TOPLEVEL, TOPLEVEL_MACRODECL_MODIFIERS}}},
        {DefKind::CLASS, {{ScopeKind::TOPLEVEL, TOPLEVEL_CLASS_MODIFIERS}}},
        {DefKind::INTERFACE, {{ScopeKind::TOPLEVEL, TOPLEVEL_INTERFACE_MODIFIERS}}},
        {
            DefKind::VARIABLE,
            {
                {ScopeKind::TOPLEVEL, TOPLEVEL_VARIABLE_MODIFIERS},
                {ScopeKind::CLASS_BODY, CLASS_BODY_VARIABLE_MODIFIERS},
                {ScopeKind::STRUCT_BODY, STRUCT_BODY_VARIABLE_MODIFIERS},
                {ScopeKind::PRIMARY_CONSTRUCTOR_BODY_FOR_CLASS, {AGGREGATE_BODY_INSTANCE_INIT_MODIFIERS}},
                {ScopeKind::PRIMARY_CONSTRUCTOR_BODY_FOR_STRUCT, {AGGREGATE_BODY_INSTANCE_INIT_MODIFIERS}},
                {ScopeKind::FUNC_BODY, {{TokenKind::CONST, {}}}},
                {ScopeKind::MACRO_BODY, {{TokenKind::CONST, {}}}},
            },
        },
        {DefKind::STRUCT, {{ScopeKind::TOPLEVEL, TOPLEVEL_STRUCT_MODIFIERS}}},
        {DefKind::ENUM, {{ScopeKind::TOPLEVEL, TOPLEVEL_ENUM_MODIFIERS}}},
        {DefKind::TYPE, {{ScopeKind::TOPLEVEL, TOPLEVEL_COMMON_MODIFIERS}}},
        {DefKind::CONSTRUCTOR,
            {
                {ScopeKind::CLASS_BODY, {AGGREGATE_BODY_INIT_MODIFIERS}},
                {ScopeKind::STRUCT_BODY, {AGGREGATE_BODY_INIT_MODIFIERS}},
            }},
        {DefKind::PRIMARY_CONSTRUCTOR,
            {
                {ScopeKind::CLASS_BODY, {AGGREGATE_BODY_INSTANCE_INIT_MODIFIERS}},
                {ScopeKind::STRUCT_BODY, {AGGREGATE_BODY_INSTANCE_INIT_MODIFIERS}},
            }},
        {
            DefKind::PROPERTY,
            {
                {ScopeKind::CLASS_BODY, CLASS_BODY_PROP_MODIFIERS},
                {ScopeKind::INTERFACE_BODY, INTERFACE_BODY_PROP_MODIFIERS},
                {ScopeKind::STRUCT_BODY, STRUCT_BODY_PROP_MODIFIERS},
                {ScopeKind::EXTEND_BODY, EXTEND_BODY_PROP_MODIFIERS},
                {ScopeKind::ENUM_BODY, ENUM_BODY_PROP_MODIFIERS},
            },
        },
        {DefKind::MAIN, {{ScopeKind::TOPLEVEL, TOPLEVEL_MAINDECL_MODIFIERS}}},
        // Empty means it doesn't support any modifier.
        {DefKind::EXTEND, {{ScopeKind::TOPLEVEL, {}}}},
        {DefKind::FINALIZER, {{ScopeKind::CLASS_BODY, {}}}},
};

// key: token to be checked, report when any of the token inside map value existed.
const static std::unordered_map<TokenKind, std::vector<TokenKind>> TOPLEVEL_CLASS_WARNING_MODIFIERS = {
    {TokenKind::OPEN, {TokenKind::SEALED, TokenKind::ABSTRACT}},
    {TokenKind::PUBLIC, {TokenKind::SEALED}},
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> TOPLEVEL_INTERFACE_WARNING_MODIFIERS = {
    // NOTE: interface with open (without sealed) also should report warning.
    // 'ILLEGAL' used to indicate that always report warning when key token existed.
    {TokenKind::OPEN, {TokenKind::SEALED, TokenKind::ILLEGAL}},
    {TokenKind::PUBLIC, {TokenKind::SEALED}},
};

const static std::unordered_map<TokenKind, std::vector<TokenKind>> INTERFACE_MEMBER_WARNING_MODIFIERS = {
    {TokenKind::OPEN, {TokenKind::ILLEGAL}},
    {TokenKind::PUBLIC, {TokenKind::ILLEGAL}},
};

const static std::unordered_map<DefKind,
    std::unordered_map<ScopeKind, std::unordered_map<TokenKind, std::vector<TokenKind>>>>
    SCOPE_MODIFIER_WARNING_RULES = {
        {DefKind::FUNC, {{ScopeKind::INTERFACE_BODY, INTERFACE_MEMBER_WARNING_MODIFIERS}}},
        {DefKind::PROPERTY, {{ScopeKind::INTERFACE_BODY, INTERFACE_MEMBER_WARNING_MODIFIERS}}},
        {DefKind::CLASS, {{ScopeKind::TOPLEVEL, TOPLEVEL_CLASS_WARNING_MODIFIERS}}},
        {DefKind::INTERFACE, {{ScopeKind::TOPLEVEL, TOPLEVEL_INTERFACE_WARNING_MODIFIERS}}},
    };

const ModifierRules& GetModifierRulesByDefKind(DefKind defKind)
{
    CJC_ASSERT(SCOPE_MODIFIER_RULES.find(defKind) != SCOPE_MODIFIER_RULES.end());
    return SCOPE_MODIFIER_RULES.at(defKind);
}

const ModifierRules& GetModifierWarningRulesByDefKind(DefKind defKind)
{
    const static ModifierRules EMPTY;
    auto found = SCOPE_MODIFIER_WARNING_RULES.find(defKind);
    return found == SCOPE_MODIFIER_WARNING_RULES.end() ? EMPTY : found->second;
}

AST::Attribute ParserImpl::GetModifierAttr(const OwnedPtr<AST::Modifier>& modifier, AST::ASTKind kind) const
{
    if (modifier) {
        if (auto found = TK2ATTRMAP.find(modifier->modifier); found != TK2ATTRMAP.end()) {
            return found->second;
        }
    }
    // The default modifier of package statement is 'public'.
    if (kind == AST::ASTKind::PACKAGE_SPEC) {
        return AST::Attribute::PUBLIC;
    }
    // The default modifier of import statement is 'private'.
    if (kind == AST::ASTKind::IMPORT_SPEC) {
        return AST::Attribute::PRIVATE;
    }
    // Otherwise, the default modifier is 'internal'.
    return AST::Attribute::INTERNAL;
}
} // namespace Cangjie
