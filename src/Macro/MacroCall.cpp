// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/*
 * @file
 *
 * This file implements the MacroCall.
 */
#include "cangjie/Utils/ConstantsUtils.h"
#include "cangjie/Macro/TokenSerialization.h"
#include "cangjie/Basic/DiagnosticEmitter.h"

#include "cangjie/Macro/MacroCall.h"

namespace Cangjie {
MacroCall::MacroCall(Ptr<AST::Node> node): node(node), begin(node->begin), end(node->end)
{
    if (node->astKind == AST::ASTKind::MACRO_EXPAND_EXPR) {
        kind = MacroKind::EXPR_KIND;
        auto macExpr = RawStaticCast<AST::MacroExpandExpr*>(node);
        invocation = &(macExpr->invocation);
        modifiers = &macExpr->modifiers;
    } else if (node->astKind == AST::ASTKind::MACRO_EXPAND_DECL) {
        kind = MacroKind::DECL_KIND;
        auto macDecl = RawStaticCast<AST::MacroExpandDecl*>(node);
        invocation = &(macDecl->invocation);
        modifiers = &macDecl->modifiers;
    } else if (node->astKind == AST::ASTKind::MACRO_EXPAND_PARAM) {
        kind = MacroKind::PARAM_KIND;
        auto macDecl = RawStaticCast<AST::MacroExpandParam*>(node);
        invocation = &(macDecl->invocation);
        modifiers = &macDecl->modifiers;
    } else {
        kind = MacroKind::UNINITIALIZED;
        Errorln("Illegal initialization of MacroCall");
    }
}

std::vector<OwnedPtr<AST::Annotation>> MacroCall::GetAnnotations() const
{
    std::vector<OwnedPtr<AST::Annotation>> ret;
    if (kind == MacroKind::UNINITIALIZED) {
        return ret;
    }
    if (kind == MacroKind::EXPR_KIND) {
        auto macExpr = RawStaticCast<AST::MacroExpandExpr*>(node);
        ret = std::move(macExpr->annotations);
    } else if (kind == MacroKind::DECL_KIND) {
        auto macDecl = RawStaticCast<AST::MacroExpandDecl*>(node);
        ret = std::move(macDecl->annotations);
    } else {
        auto macDecl = RawStaticCast<AST::MacroExpandParam*>(node);
        ret = std::move(macDecl->annotations);
    }
    return ret;
}

extern "C" {
/*
 * Recursively searches for the target parent node of the current macroCall node.
 */
bool MacroCall::TraverseParentMacroNode(MacroCall* mcNode, const std::string& parentName)
{
    if (!mcNode) {
        return false;
    }
    if (mcNode && !mcNode->parentMacroCall) {
        return false;
    }
    if (mcNode->parentMacroCall->GetFullName() == parentName) {
        return true;
    }
    return TraverseParentMacroNode(mcNode->parentMacroCall, parentName);
}

/*
 * Check whether the target node exists.
 */
bool MacroCall::CheckParentContext(const char* parentStr, bool report)
{
    std::string parentName(parentStr);
    bool success{false};
    if (this->parentNames.empty()) {
        success = TraverseParentMacroNode(this, parentName);
    } else {
        // For child process in lsp.
        for (auto& parent : parentNames) {
            if (parent == parentName) {
                success = true;
                break;
            }
        }
    }
    if (!success && report) {
        if (this->ci) {
            (void)this->ci->diag.Diagnose(
                this->GetBeginPos(), DiagKind::macro_assert_parent_context_failed, this->GetFullName(), parentName);
        } else {
            // Save parent name if assert failed, and nodify macrocall result with parent name to main process.
            this->assertParents.emplace_back(parentName);
        }
    }
    return success;
}


void MacroCall::DiagReport(const int level, const Range range, const char *message, const char* hint) const
{
    DiagKindRefactor diagKind;
    if (level == static_cast<int>(DiagSeverity::DS_ERROR)) {
        diagKind = DiagKindRefactor::parse_diag_error;
    } else if (level == static_cast<int>(DiagSeverity::DS_WARNING)) {
        diagKind = DiagKindRefactor::parse_diag_warning;
    } else {
        return;
    }

    auto builder = this->ci->diag.DiagnoseRefactor(diagKind, range, message);
    builder.AddMainHintArguments(hint);
}

/*
 * Record the information sent by the inner macro.
 */
void MacroCall::SetItemMacroContext(char* key, void* value, uint8_t type)
{
    (void)this->recordMacroInfo.emplace_back(key);

    void* valueP = value;
    if (static_cast<ItemKind>(type) == ItemKind::TKS) {
        auto tokens = TokenSerialization::GetTokensFromBytes(reinterpret_cast<uint8_t*>(value));
        valueP = TokenSerialization::GetTokensBytesWithHead(tokens);
        free(value);
    }
    (void)this->recordMacroInfo.emplace_back(valueP);

    uint8_t* typeP = (uint8_t*)malloc(sizeof(uint8_t));
    if (typeP == nullptr) {
        Errorln("Macro With Context Memory Allocation Failed.");
        (void)this->recordMacroInfo.emplace_back(typeP);
        return;
    }
    *typeP = type;
    (void)this->recordMacroInfo.emplace_back(typeP);
}

void MacroCall::TraverseMacroNode(
    MacroCall* macroNode, const std::string& childName, std::vector<std::vector<void*>>& macroInfos)
{
    if (!macroNode) {
        return;
    }
    if (!macroNode->childMessages.empty()) {
        // For child process in lsp.
        for (auto& cm : macroNode->childMessages) {
            if (cm.childName != childName) {
                continue;
            }
            std::vector<void*> itemInfo;
            for (auto& it : cm.items) {
                itemInfo.emplace_back(it.key.data());
                if (it.kind == ItemKind::STRING) {
                    itemInfo.emplace_back(it.sValue.data()); // string value
                } else if (it.kind == ItemKind::INT) {
                    itemInfo.emplace_back(&it.iValue);       // int value
                } else if (it.kind == ItemKind::BOOL) {
                    itemInfo.emplace_back(&it.bValue);       // bool value
                } else if (it.kind == ItemKind::TKS) {
                    itemInfo.emplace_back(TokenSerialization::GetTokensBytesWithHead(it.tValue)); // tokens value
                }
                uint8_t* typeP = (uint8_t*)malloc(sizeof(uint8_t));
                if (typeP == nullptr) {
                    Errorln("Macro With Context Memory Allocation Failed.");
                    itemInfo.emplace_back(typeP);
                    continue;
                }
                *typeP = static_cast<uint8_t>(it.kind);
                itemInfo.emplace_back(typeP);
            }
            (void)macroInfos.emplace_back(itemInfo);
        }
        return;
    }
    if (macroNode->GetFullName() == childName) {
        (void)macroInfos.emplace_back(macroNode->recordMacroInfo);
    }
    for (MacroCall* child : macroNode->children) {
        TraverseMacroNode(child, childName, macroInfos);
    }
}

/*
 * Obtaining information from the child nodes:
 * For example,
 * @Outter(@Inner()... @Inner()... @Inner)
 * The data structure wil be created for 'Outter' as follows.
 * |  Inner  |  Inner  |  Inner  |
 *                |
 * |(k,v,t)..|(k,v,t)..|(k,v,t)..|
 */
void*** MacroCall::GetChildMessagesFromMacroContext(const char* childrenStr)
{
    this->macroInfoVec.clear();
    if (this->children.empty() && this->childMessages.empty()) {
        return nullptr;
    }
    std::string childName(childrenStr);
    this->TraverseMacroNode(this, childName, this->macroInfoVec);
    if (this->macroInfoVec.empty()) {
        return nullptr;
    }
    void*** rawPtr = static_cast<void***>(malloc((this->macroInfoVec.size() + 1) * sizeof(void**)));
    if (rawPtr == nullptr) {
        Errorln("Macro With Context Memory Allocation Failed.");
        return rawPtr;
    }
    void** vecPtr;
    for (size_t i = 0; i < this->macroInfoVec.size(); i++) {
        vecPtr = static_cast<void**>(malloc((this->macroInfoVec[i].size() + 1) * sizeof(void*)));
        if (vecPtr == nullptr) {
            Errorln("Macro With Context Memory Allocation Failed.");
            return rawPtr;
        }
        for (size_t j = 0; j <= this->macroInfoVec[i].size(); j++) {
            if (j == this->macroInfoVec[i].size()) {
                // Null pointer is required to identify different segments when interoperate with C.
                vecPtr[j] = nullptr;
                break;
            }
            vecPtr[j] = this->macroInfoVec[i][j];
        }
        rawPtr[i] = vecPtr;
    }
    rawPtr[this->macroInfoVec.size()] = nullptr;
    return rawPtr;
}
}

}