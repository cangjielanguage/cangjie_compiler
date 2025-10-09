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
        (Peek().kind == TokenKind::PLATFORM);
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

    const auto& defRules = GetModifierRulesByDefKind(defKind);
    if (defRules.find(scopeKind) == defRules.end()) {
        return {};
    }

    if (modifiers.empty()) {
        return (scopeKind == ScopeKind::CLASS_BODY || scopeKind == ScopeKind::INTERFACE_BODY)
            ? std::set<AST::Attribute>{AST::Attribute::IN_CLASSLIKE}
            : std::set<AST::Attribute>{};
    }

    auto modifiersVec = SortModifierByPos(modifiers);
    if (defRules.at(scopeKind).empty()) {
        DiagIllegalModifierInScope(**modifiersVec.begin());
        return {};
    }
    auto scopeRules = defRules.at(scopeKind);
    std::unordered_map<TokenKind, std::vector<TokenKind>> scopeWarningRules;
    const auto& rules = GetModifierWarningRulesByDefKind(defKind);
    if (auto foundRules = rules.find(scopeKind); foundRules != rules.end()) {
        scopeWarningRules = foundRules->second;
    }
    if (!mpImpl->CheckCJMPModifiers(modifiers)) {
        return {};
    }
    return GetModifierAttrs(scopeKind, scopeRules, scopeWarningRules, modifiersVec);
}

std::set<AST::Attribute> ParserImpl::GetModifierAttrs(const ScopeKind& scopeKind,
    std::unordered_map<TokenKind, std::vector<TokenKind>>& scopeRules,
    const std::unordered_map<TokenKind, std::vector<TokenKind>>& scopeWarningRules,
    const std::vector<Ptr<const AST::Modifier>>& modifiersVec)
{
    // Store the modifiers TokenKind that has been traversed.
    std::vector<Ptr<const AST::Modifier>> traversedModifiers;
    std::set<AST::Attribute> attrs;
    std::vector<Ptr<const AST::Modifier>> validModifiers;

    // All error reporting forms in modifiers should be unified. Currently, a variety of error reporting is adopted for
    // compatible use cases, and adjustments will be made in the future.
    for (auto& it : modifiersVec) {
        // Check allowing modifiers
        if (scopeRules.find(it->modifier) == scopeRules.end()) {
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
        for (auto& cm : scopeRules[it->modifier]) {
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
    ReportModifierWarning(scopeKind, scopeWarningRules, validModifiers);
    return attrs;
}

void ParserImpl::ReportModifierWarning([[maybe_unused]] ScopeKind scopeKind,
    const std::unordered_map<TokenKind, std::vector<TokenKind>>& scopeWarningRules,
    const std::vector<Ptr<const AST::Modifier>>& modifiers)
{
    for (auto rule : scopeWarningRules) {
        auto iter = std::find_if(
            modifiers.begin(), modifiers.end(), [&rule](auto& mod) { return mod->modifier == rule.first; });
        if (iter == modifiers.end()) {
            continue;
        }
        for (auto higherMod : rule.second) {
            // Do not report scope dependent redundant modifier warning for macro expansion.
            if (higherMod == TokenKind::ILLEGAL && !diag.ignoreScopeCheck) {
                DiagRedundantModifiers(**iter);
                break;
            }
            auto found = std::find_if(modifiers.begin(), modifiers.end(),
                [&higherMod](auto& mod) { return mod->modifier == higherMod; });
            if (found != modifiers.end()) {
                DiagRedundantModifiers(**iter, **found);
                break;
            }
        }
    }
}
