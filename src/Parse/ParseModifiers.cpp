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

namespace {
// The access-modifier (public/private/internal/protected) x 'foreign'
// conflict pair. On @ArkTS-annotated declarations the pair is skipped, so the
// stdlib can spell the preset declarations verbatim, e.g.
// '@ArkTS[foreignName: "Array"] public foreign class ArkTSArray<T>'.
// An @ArkTS foreign function relaxes every visibility except 'private': the
// design forbids a private foreign declaration while 'public foreign func' is
// the ArkTS spelling. Every other conflict pair keeps firing, with or without
// @ArkTS: 'public foreign class C {}' -> "'foreign' and 'public' modifiers
// conflict on class declaration"; the @ArkTS spelling above parses.
bool IsAccessForeignConflict(DefKind defKind, TokenKind lhs, TokenKind rhs)
{
    auto isAccessModifier = [](TokenKind kind) {
        return kind == TokenKind::PUBLIC || kind == TokenKind::PRIVATE || kind == TokenKind::INTERNAL ||
            kind == TokenKind::PROTECTED;
    };
    bool accessForeign = (lhs == TokenKind::FOREIGN && isAccessModifier(rhs)) ||
        (isAccessModifier(lhs) && rhs == TokenKind::FOREIGN);
    if (!accessForeign) {
        return false;
    }
    if (defKind == DefKind::FUNC) {
        return lhs != TokenKind::PRIVATE && rhs != TokenKind::PRIVATE;
    }
    return true;
}
} // namespace

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
    if (!foreignBlockModifiers.empty()) {
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
    DefKind defKind, bool arktsAnnotated)
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
    return GetModifierAttrs(defKind, scopeKind, modifiersVec, arktsAnnotated);
}

std::set<AST::Attribute> ParserImpl::GetModifierAttrs(DefKind defKind, ScopeKind scopeKind,
    const std::vector<Ptr<const AST::Modifier>>& modifiersVec, bool arktsAnnotated)
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
            // On @ArkTS-annotated declarations the access-modifier x FOREIGN
            // pair is skipped (except PRIVATE on a function) - only this pair,
            // so every other conflict (SEALED x access, CONST/UNSAFE x
            // FOREIGN, ...) still fires.
            if (arktsAnnotated && IsAccessForeignConflict(defKind, it->modifier, cm)) {
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
