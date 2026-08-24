// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the helper functions used for incremental semantic checking.
 */

#ifndef CANGJIE_SEMA_INCREMENTAL_UTILS_H
#define CANGJIE_SEMA_INCREMENTAL_UTILS_H

#include <unordered_set>

#include "cangjie/AST/Node.h"
#include "cangjie/IncrementalCompilation/CompilationCache.h"

namespace Cangjie::Sema {
void MarkIncrementalCheckForCtor(const std::unordered_set<Ptr<AST::Decl>>& declsToBeReCompiled);
std::unordered_set<Ptr<const AST::StructTy>> CollectChangedStructTypes(
    const AST::Package& pkg, const std::unordered_set<Ptr<AST::Decl>>& declsToBeReCompiled);
void HandleCtorForIncr(const AST::Package& pkg, std::map<std::string, Ptr<AST::Decl>>& mangledName2DeclMap,
    SemanticInfo& usageCache);
std::string GetTypeRawMangleName(AST::ModalTy ty);
void CollectCompilerAddedDeclUsage(const AST::Package& pkg, SemanticInfo& usageCache);
void CollectRemovedMangles(const std::string& removed, SemanticInfo& semaInfo,
    std::unordered_set<std::string>& removedMangles);
void CollectRemovedManglesForReCompile(
    const AST::Decl& changed, SemanticInfo& semaInfo, std::unordered_set<std::string>& removedMangles);
} // namespace Cangjie::Sema

#endif
