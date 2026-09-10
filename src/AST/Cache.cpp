// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements type check cache utils.
 */

#include "cangjie/AST/Cache.h"
#include "cangjie/AST/ASTCasting.h"

using namespace Cangjie;
using namespace AST;

namespace Cangjie::AST {
TargetCache CollectTargets(const Node& node)
{
    if (auto farg = DynamicCast<const FuncArg*>(&node)) {
        return CollectTargets(*farg->expr);
    }
    if (auto target1 = node.GetTarget()) {
        if (auto ma = DynamicCast<const MemberAccess*>(&node);
               ma && ma->baseExpr && ma->baseExpr->IsReferenceExpr()) {
            auto target2 = ma->baseExpr->GetTarget();
            return std::make_pair(target1, target2);
        } else {
            return std::make_pair(target1, nullptr);
        }
    }
    return std::make_pair(nullptr, nullptr);
}

void RestoreTargets(Node& node, const TargetCache& targets)
{
    if (auto farg = DynamicCast<const FuncArg*>(&node)) {
        RestoreTargets(*farg->expr, targets);
    }
    node.SetTarget(targets.first);
    if (auto ma = DynamicCast<const MemberAccess*>(&node);
           ma && ma->baseExpr && ma->baseExpr->IsReferenceExpr()) {
        ma->baseExpr->SetTarget(targets.second);
    }
}

bool CacheKey::operator==(const CacheKey& b) const
{
    return target == b.target && isDesugared == b.isDesugared && diagKey == b.diagKey;
}

bool MemSig::operator==(const MemSig& b) const
{
    return id == b.id && isVarOrProp == b.isVarOrProp && arity == b.arity && genArity == b.genArity;
}
} // namespace Cangjie::AST
