// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 * This file implements parse and check CJMP Decl.
 */

#include "ParserImpl.h"
#include "cangjie/AST/Utils.h"

using namespace Cangjie;
using namespace AST;

namespace {
// Check whether the decl has body or initializer.
bool HasDefault(const AST::Decl& decl)
{
    // Check on match CJMP decl for platform member.
    if (decl.TestAttr(Attribute::PLATFORM)) {
        return true;
    }
    switch (decl.astKind) {
        case ASTKind::FUNC_DECL: {
            auto& funcDecl = StaticCast<AST::FuncDecl&>(decl);
            if (funcDecl.funcBody->body) {
                return true;
            }
            break;
        }
        case ASTKind::PRIMARY_CTOR_DECL: {
            auto& pcDecl = StaticCast<AST::PrimaryCtorDecl&>(decl);
            if (pcDecl.funcBody->body) {
                return true;
            }
            break;
        }
        case ASTKind::PROP_DECL: {
            auto& propDecl = StaticCast<AST::PropDecl&>(decl);
            if (!propDecl.getters.empty() || !propDecl.setters.empty()) {
                return true;
            }
            break;
        }
        case ASTKind::VAR_DECL: {
            auto& varDecl = StaticCast<AST::VarDecl&>(decl);
            if (varDecl.initializer) {
                return true;
            }
            break;
        }
        default: ;
    }
    return false;
}

void SetCJMPAttrs(AST::Decl& decl)
{
    if (!decl.TestAttr(Attribute::COMMON)) {
        return;
    }
    if (HasDefault(decl)) {
        decl.EnableAttr(Attribute::COMMON_WITH_DEFAULT);
    }
}

// For cjmp diag error info.
const std::unordered_map<ASTKind, std::string> KIND_TO_STR = {
    {ASTKind::VAR_DECL, "variable"},
    {ASTKind::VAR_WITH_PATTERN_DECL, "variable"},
    {ASTKind::PRIMARY_CTOR_DECL, "primary constructor"},
    {ASTKind::FUNC_DECL, "function"},
    {ASTKind::CLASS_DECL, "class"},
    {ASTKind::INTERFACE_DECL, "interface"},
    {ASTKind::STRUCT_DECL, "struct"},
    {ASTKind::ENUM_DECL, "enum"},
    {ASTKind::PROP_DECL, "property"},
    {ASTKind::EXTEND_DECL, "extend"},
    {ASTKind::ENUM_PATTERN, "enum"},
    {ASTKind::TUPLE_PATTERN, "tuple"},
    {ASTKind::WILDCARD_PATTERN, "wildcard"},
    {ASTKind::FUNC_PARAM, "parameter"},
    {ASTKind::TYPE_ALIAS_DECL, "type"}
};

std::string GetDiagKind(const AST::Node& node)
{
    // init constructor
    if (node.astKind == ASTKind::FUNC_DECL && node.TestAttr(Attribute::CONSTRUCTOR)) {
        return "constructor";
    }
    auto it = KIND_TO_STR.find(node.astKind);
    if (it != KIND_TO_STR.end()) {
        return it->second;
    }
    CJC_ASSERT(false);
    return "unknown decl";
}
}  // namespace

void MPParserImpl::SetCompileOptions(const GlobalOptions& opts)
{
    this->compileCommon = (opts.outputMode == GlobalOptions::OutputMode::CHIR);
    this->compilePlatform = (opts.commonPartCjo != std::nullopt);
}

bool MPParserImpl::CheckCJMPModifiers(const std::set<AST::Modifier> &modifiers) const
{
    auto currentFile = ref->currentFile;
    if (ref->HasModifier(modifiers, TokenKind::PLATFORM)) {
        if (!compilePlatform) {
            ref->diag.DiagnoseRefactor(DiagKindRefactor::parse_platform_in_non_platform_file, *currentFile);
            return false;
        }
        if (currentFile->isCommon) {
            ref->diag.DiagnoseRefactor(DiagKindRefactor::parse_common_and_platform_in_the_same_file, *currentFile);
            return false;
        }
        if (currentFile->package != nullptr) {
            currentFile->package->hasPlatform = true;
        }
        currentFile->isPlatform = true;
    }
    if (ref->HasModifier(modifiers, TokenKind::COMMON)) {
        if (!compileCommon) {
            ref->diag.DiagnoseRefactor(DiagKindRefactor::parse_common_in_non_common_file, *currentFile);
            return false;
        }
        if (currentFile->isPlatform) {
            ref->diag.DiagnoseRefactor(DiagKindRefactor::parse_common_and_platform_in_the_same_file, *currentFile);
            return false;
        }
        if (currentFile->package != nullptr) {
            currentFile->package->hasCommon = true;
        }
        currentFile->isCommon = true;
    }
    return true;
}

void MPParserImpl::CheckCJMPDecl(AST::Decl& decl) const
{
    if (!compileCommon && !compilePlatform) {
        return;
    }
    if (!CheckCJMPModifiersOf(decl)) {
        return;
    }
    // Enable COMMON_WITH_DEFAULT attr for func/constructor/var
    SetCJMPAttrs(decl);
    // Check sema rules
    if (decl.astKind == ASTKind::INTERFACE_DECL) {
        // Check that the member of platform interface must have the body
        CheckPlatformInterface(StaticCast<AST::InterfaceDecl&>(decl));
    } else if (decl.astKind == ASTKind::PRIMARY_CTOR_DECL) {
        auto& fn = StaticCast<AST::PrimaryCtorDecl&>(decl);
        CheckCJMPFuncParams(fn, fn.funcBody.get());
    } else if (decl.astKind == ASTKind::FUNC_DECL) {
        auto& fn = StaticCast<AST::FuncDecl&>(decl);
        CheckCJMPFuncParams(fn, fn.funcBody.get());
    }
}

bool MPParserImpl::HasCJMPModifiers(const AST::Modifier& modifier) const
{
    if (!compileCommon && !compilePlatform) {
        return false;
    }
    return (modifier.modifier == TokenKind::COMMON || modifier.modifier == TokenKind::PLATFORM);
}

bool MPParserImpl::CheckCJMPModifiersOf(const AST::Decl& decl) const
{
    if (decl.IsCommonOrPlatform()) {
        auto kind = decl.TestAttr(Attribute::COMMON) ? "common" : "platform";
        // tuple, enum, _ pattern
        if (decl.astKind == ASTKind::VAR_WITH_PATTERN_DECL && decl.TestAttr(Attribute::COMMON)) {
            auto& varDecl = StaticCast<AST::VarWithPatternDecl&>(decl);
            auto& pattern = *(varDecl.irrefutablePattern);
            ref->diag.DiagnoseRefactor(DiagKindRefactor::parse_cjmp_pattern_decl, pattern, GetDiagKind(pattern), kind);
            return false;
        }
        // static init
        if (IsStaticInitializer(decl)) {
            ref->diag.DiagnoseRefactor(DiagKindRefactor::parse_cjmp_static_init, decl, kind);
            return false;
        }
    }
    bool ret = true;
    // Check whether modifiers are same between members and outer decl.
    for (auto &member : decl.GetMemberDeclPtrs()) {
        ret = CheckCJMPModifiersBetween(*member, decl) && ret;
    }
    return ret;
}

/**
 * Checks whether the cjmp modifiers of two declarations are same.
 *
 * @param inner The inner declaration, member or member param decl.
 * @param outer The outer declaration, nominal decl or primary constructor.
 * @return True if the modifiers are same.
 *
 */
bool MPParserImpl::CheckCJMPModifiersBetween(const AST::Decl& inner, const AST::Decl& outer) const
{
    auto p0 = GetDiagKind(inner) + " " + inner.identifier.Val();
    if (inner.TestAttr(Attribute::COMMON) && !outer.TestAttr(Attribute::COMMON)) {
        DiagOuterDeclMissMatch(inner, p0, "common", GetDiagKind(outer), "common");
        return false;
    }
    if (inner.TestAttr(Attribute::PLATFORM) && !outer.TestAttr(Attribute::PLATFORM)) {
        DiagOuterDeclMissMatch(inner, p0, "platform", GetDiagKind(outer), "platform");
        return false;
    }
    return true;
}

void MPParserImpl::CheckCJMPFuncParams(AST::Decl& decl, const Ptr<AST::FuncBody> funcBody) const
{
    if (!funcBody || funcBody->paramLists.size() != 1) {
        return;
    }
    auto& params = funcBody->paramLists[0]->params;
    for (size_t index = 0; index < params.size(); index++) {
        if (params[index]->assignment && decl.TestAttr(Attribute::PLATFORM)) {
            ref->diag.DiagnoseRefactor(DiagKindRefactor::parse_platform_function_parameter_cannot_have_default_value,
                *params[index], GetDiagKind(decl));
            decl.EnableAttr(Attribute::IS_BROKEN);
        }
        CheckCJMPModifiersBetween(*params[index], decl);
    }
}

void MPParserImpl::CheckPlatformInterface(const AST::InterfaceDecl& decl) const
{
    if (!decl.TestAttr(Attribute::PLATFORM)) {
        return;
    }
    // Check that the general member of platform interface must have the body
    for (auto& member : decl.GetMemberDeclPtrs()) {
        if (!HasDefault(*member)) {
            ref->diag.DiagnoseRefactor(DiagKindRefactor::parse_platform_member_must_have_implementation,
                *member, member->identifier.Val(), decl.identifier.Val());
        }
    }
}

void MPParserImpl::DiagOuterDeclMissMatch(const AST::Node& node,
    const std::string& p0, const std::string& p1, const std::string& p2, const std::string& p3) const
{
    ref->diag.DiagnoseRefactor(DiagKindRefactor::parse_cjmp_outdecl_miss_match, node, p0, p1, p2, p3);
}
