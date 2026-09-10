// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements Modifiers check apis.
 */

#include "ParserImpl.h"

#include "cangjie/AST/Utils.h"
#include "cangjie/Parse/ParseModifiersRules.h"

using namespace Cangjie;

bool ParserImpl::SeeingModifier()
{
    return (Peek().kind >= TokenKind::STATIC && Peek().kind <= TokenKind::OPERATOR) ||
        (Peek().kind == TokenKind::CONST) || (Peek().kind == TokenKind::COMMON) ||
        (Peek().kind == TokenKind::SPECIFIC) || (Peek().kind == TokenKind::EXCLAVE) ||
        (Peek().kind == TokenKind::DEMODE);
}

void ParserImpl::SetDeclBeginPos(AST::Decl& decl) const
{
    if (decl.modifiers.empty()) {
        return;
    }
    auto modVec = SortModifierByPos(decl.modifiers);
    if (inForeignBlock) {
        auto size = modVec.size();
        for (size_t i = 0; i < size; i++) {
            if (modVec[i]->modifier != TokenKind::FOREIGN) {
                continue;
            }
            modVec.erase(modVec.begin(), modVec.begin() + static_cast<long>(i) + 1);
            break;
        }
    }
    if (modVec.empty()) {
        return;
    }
    decl.begin = modVec[0]->begin;
}

// Make sure there is no repeat modifier in modifiers.
std::set<AST::Attribute> ParserImpl::CheckDeclModifiers(const std::set<AST::Modifier>& modifiers, ScopeKind scopeKind,
    DefKind defKind)
{
    // For libast, ignore all scope info.
    if (scopeKind == ScopeKind::UNKNOWN_SCOPE) {
        std::set<AST::Attribute> attrs;
        std::for_each(modifiers.begin(), modifiers.end(), [&](auto& modi) {
            if (auto attr = GetAttributeByModifier(modi.modifier)) {
                attrs.insert(attr.value());
            }
        });
        return attrs;
    }

    if (!HasScopeRules(defKind, scopeKind)) {
        return {};
    }

    if (modifiers.empty()) {
        return (scopeKind == ScopeKind::CLASS_BODY || scopeKind == ScopeKind::INTERFACE_BODY)
            ? std::set<AST::Attribute>{AST::Attribute::IN_CLASSLIKE}
            : std::set<AST::Attribute>{};
    }

    auto modifiersVec = SortModifierByPos(modifiers);
    if (IsScopeRulesEmpty(defKind, scopeKind)) {
        DiagIllegalModifierInScope(**modifiersVec.begin());
        return {};
    }
    if (!mpImpl->CheckCJMPModifiers(modifiers)) {
        return {};
    }
    return GetModifierAttrs(defKind, scopeKind, modifiersVec);
}

std::set<AST::Attribute> ParserImpl::GetModifierAttrs(
    DefKind defKind, ScopeKind scopeKind, const std::vector<Ptr<const AST::Modifier>>& modifiersVec)
{
    // Store the modifiers TokenKind that has been traversed.
    std::vector<Ptr<const AST::Modifier>> traversedModifiers;
    std::set<AST::Attribute> attrs;
    std::vector<Ptr<const AST::Modifier>> validModifiers;

    // All error reporting forms in modifiers should be unified. Currently, a variety of error reporting is adopted for
    // compatible use cases, and adjustments will be made in the future.
    for (auto& it : modifiersVec) {
        // Check allowing modifiers
        if (!IsModifierAllowed(defKind, scopeKind, it->modifier)) {
            // Ignore modifiers check when Parsing from libast.
            if (diag.ignoreScopeCheck) {
                continue;
            }
            DiagIllegalModifierInScope(*it);
            if (auto attr = GetAttributeByModifier(it->modifier)) {
                attrs.insert(attr.value());
            }
            continue;
        }
        bool hasCollision = false;
        // Check conflict modifiers.
        auto conflicts = GetConflictingModifiers(defKind, scopeKind, it->modifier);
        for (size_t i = 0; i < conflicts.size; ++i) {
            TokenKind cm = conflicts.data[i];
            auto iter = std::find_if(traversedModifiers.begin(), traversedModifiers.end(),
                [&cm](auto& preMod) { return preMod->modifier == cm; });
            if (iter == traversedModifiers.end()) {
                continue;
            }
            DiagConflictedModifier(**iter, *it);
            hasCollision = true;
            // Remove conflicted modifiers from valid list which used for report warning.
            Utils::EraseIf(validModifiers, [&cm](auto mod) { return mod->modifier == cm; });
        }
        traversedModifiers.emplace_back(it);
        if (!hasCollision) {
            validModifiers.emplace_back(it);
        }
        if (auto attr = GetAttributeByModifier(it->modifier)) {
            attrs.insert(attr.value());
        }
    }
    ReportModifierWarning(defKind, scopeKind, validModifiers);
    return attrs;
}

void ParserImpl::ReportModifierWarning(
    DefKind defKind, ScopeKind scopeKind, const std::vector<Ptr<const AST::Modifier>>& modifiers)
{
    if (!HasWarningRules(defKind, scopeKind)) {
        return;
    }
    for (auto& mod : modifiers) {
        auto conflicts = GetWarningConflicts(defKind, scopeKind, mod->modifier);
        if (!conflicts.data) {
            continue;
        }
        for (size_t i = 0; i < conflicts.size; ++i) {
            TokenKind higherMod = conflicts.data[i];
            // Do not report scope dependent redundant modifier warning for macro expansion.
            if (higherMod == TokenKind::ILLEGAL && !diag.ignoreScopeCheck) {
                DiagRedundantModifiers(*mod);
                break;
            }
            auto found = std::find_if(
                modifiers.begin(), modifiers.end(), [&higherMod](auto& m) { return m->modifier == higherMod; });
            if (found != modifiers.end()) {
                DiagRedundantModifiers(*mod, **found);
                break;
            }
        }
    }
}
