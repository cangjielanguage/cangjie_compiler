// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements validating the used nodes.
 */

#include <stack>
#include "cangjie/AST/ASTTypeValidator.h"

namespace Cangjie::AST {
class ASTTypeValidator {
public:
    ~ASTTypeValidator() = default;
    explicit ASTTypeValidator(DiagnosticEngine& diag) : id(Walker::GetNextWalkerID()), diag(diag)
    {
        (void)validRanges.emplace(RawStaticCast<const Node*>(nullptr), MakeRange(DEFAULT_POSITION, DEFAULT_POSITION));
    }
    VisitAction PreVisitor(const Node& node)
    {
        auto range = MakeRange(node.begin, node.end);
        if (!range.HasZero()) {
            (void)validRanges.emplace(&node, std::move(range));
        }
        auto [action, needCheck] = GetNodeValidatingStrategy(node);
        if (auto expr = DynamicCast<const Expr*>(&node); expr && expr->desugarExpr) {
            auto preVisit = [this](auto node) { return PreVisitor(*node); };
            auto postVisit = [this](auto node) { return PostVisitor(*node); };
            Walker(expr->desugarExpr.get(), id, preVisit, postVisit).Walk();
            action = VisitAction::SKIP_CHILDREN;
        }
        if (action == VisitAction::SKIP_CHILDREN) {
            needCheck = false;
        }
        (void)checkStatus.emplace(&node, needCheck);
        // Dot not check the children of type node (may be typealias substitution result).
        return Is<Type>(node) ? VisitAction::SKIP_CHILDREN : action;
    }

    VisitAction PostVisitor(const Node& node)
    {
        if (!checkStatus[&node]) {
            (void)checkStatus.erase(&node);
            return VisitAction::WALK_CHILDREN;
        }
        if (node.TestAttr(Attribute::FROM_COMMON_PART)) {
            return VisitAction::SKIP_CHILDREN;
        }
        if (node.TestAttr(Attribute::COMMON) && node.TestAttr(Attribute::IMPORTED)) {
            return VisitAction::SKIP_CHILDREN;
        }
        // Target can be ignored that:
        // 1. the target is type node's target.
        // 2. the target has ignored astKinds.
        auto curTarget = node.GetTarget();
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
        bool valid = !curTarget || Is<Type>(node) || Utils::In(curTarget->astKind, ignoreKinds) ||
            Ty::IsTyCorrect(curTarget->ty);
        CJC_ASSERT(valid);

        valid = valid && Ty::IsTyCorrect(node.ty) && !node.ty->HasIdealTy() && !node.ty->HasQuestTy();
#endif
        CJC_ASSERT(valid);

        if (auto fb = DynamicCast<const FuncBody*>(&node); fb) {
            Ptr<Decl> target = nullptr;
            if (fb->parentClassLike) {
                target = fb->parentClassLike;
            } else if (fb->parentStruct) {
                target = fb->parentStruct;
            } else if (fb->parentEnum) {
                target = fb->parentEnum;
            }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
            valid = valid && (!target || Ty::IsTyCorrect(target->ty));
#endif
            CJC_ASSERT(valid);
        }
        auto [preNode, range] = validRanges.top(); // Since 'validRanges' has default value, it must not be empty.
        if (!valid) {
            auto builder = diag.DiagnoseRefactor(DiagKindRefactor::sema_invalid_node_after_check, node, range);
            builder.AddNote("please report this to Cangjie team and include the project");
            return VisitAction::STOP_NOW;
        }
        if (&node == preNode) {
            validRanges.pop();
        }
        return VisitAction::WALK_CHILDREN;
    };
    unsigned id;

private:
    // Get node visiting strategy for validation step.
    static std::pair<VisitAction, bool> GetNodeValidatingStrategy(const Node& node)
    {
        auto action = VisitAction::WALK_CHILDREN;
        bool needCheck = true;
        // Ignore for node which:
        // 1. the node is added as initial arg.
        // 2. the node is generic.
        // 3. the node is marked as 'INCRE_COMPILE' which type is loaded from cache.
        // 4. the node's kind is existed in ignore map.
        if (node.TestAttr(Attribute::GENERIC) || Utils::In(node.astKind, ignoreKinds) ||
            (node.astKind != ASTKind::PACKAGE && node.TestAttr(Attribute::INCRE_COMPILE)) ||
            (node.astKind == ASTKind::FUNC_ARG && node.TestAttr(Attribute::HAS_INITIAL))) {
            action = VisitAction::SKIP_CHILDREN;
        } else if (Utils::In(node.astKind, noneTyKinds)) {
            // Do not check nodes that should not have sema ty.
            needCheck = false;
        } else if (Is<Type>(node) && node.ty->IsGeneric()) {
            // For type node, if type itself is generic T, ignored.
            action = VisitAction::SKIP_CHILDREN;
        } else if (auto target = node.GetTarget(); target && target->astKind == ASTKind::PACKAGE_DECL) {
            // Ignore children when target is package decl.
            action = VisitAction::SKIP_CHILDREN;
        }
        return {action, needCheck};
    }

    DiagnosticEngine& diag;
    std::stack<std::pair<Ptr<const Node>, Range>> validRanges;
    std::unordered_map<Ptr<const Node>, bool> checkStatus;

    // Node kinds who's type and children can be ignored.
    inline const static std::unordered_set<ASTKind> ignoreKinds {
        ASTKind::GENERIC_PARAM_DECL, ASTKind::GENERIC_CONSTRAINT,
        ASTKind::PRIMARY_CTOR_DECL, ASTKind::TYPE_ALIAS_DECL, ASTKind::BUILTIN_DECL, ASTKind::MODIFIER,
        ASTKind::ANNOTATION, ASTKind::PACKAGE_SPEC, ASTKind::IMPORT_SPEC, ASTKind::WILDCARD_PATTERN, ASTKind::GENERIC,
        ASTKind::MACRO_EXPAND_DECL, ASTKind::MACRO_EXPAND_EXPR, ASTKind::MACRO_EXPAND_PARAM};

    // Node kinds who's sema type can be ignored.
    inline const static std::unordered_set<ASTKind> noneTyKinds{
        ASTKind::PACKAGE,
        ASTKind::FILE,
        ASTKind::PRIMARY_CTOR_DECL,
        ASTKind::MACRO_DECL,
        ASTKind::MAIN_DECL,
        ASTKind::CLASS_BODY,
        ASTKind::INTERFACE_BODY,
        ASTKind::STRUCT_BODY,
        ASTKind::DUMMY_BODY,
        ASTKind::RETURN_EXPR,
        ASTKind::LET_PATTERN_DESTRUCTOR,
        ASTKind::FUNC_PARAM_LIST,
    };
};

void ValidateUsedNodes(DiagnosticEngine& diagEngine, Package& pkg)
{
    ASTTypeValidator validator(diagEngine);
    auto preVisit = [&validator](auto node) { return validator.PreVisitor(*node); };
    auto postVisit = [&validator](auto node) { return validator.PostVisitor(*node); };
    Walker(&pkg, validator.id, preVisit, postVisit).Walk();
}
} // namespace Cangjie::AST
