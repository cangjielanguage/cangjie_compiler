// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Walker related classes.
 */

#include "cangjie/AST/Walker.h"

#include <string>

#include "cangjie/AST/Match.h"
#include "cangjie/Basic/Match.h"

using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Meta;
namespace Cangjie::AST {
template <class NodeT> std::atomic_uint WalkerT<NodeT>::nextWalkerID = 1;
template <class NodeT> unsigned WalkerT<NodeT>::GetNextWalkerID()
{
    // All instantiate of WalkerT must share same counter.
    if (WalkerT<Node>::nextWalkerID == 0) {
        WalkerT<Node>::nextWalkerID++;
    }
    return WalkerT<Node>::nextWalkerID++;
}
template class WalkerT<Node>;
template class WalkerT<const Node>;
} // namespace Cangjie::AST
template VisitAction Walker::Walk(Ptr<Node> curNode) const;
template VisitAction ConstWalker::Walk(Ptr<const Node> curNode) const;
template <class NodeT>
VisitAction WalkerT<NodeT>::Walk(Ptr<NodeT> curNode) const
{
    if (!curNode) {
        return VisitAction::WALK_CHILDREN;
    }
    if (curNode->astKind != ASTKind::MODIFIER && curNode->visitedByWalkerID == ID) {
        // If already visited.
        // Modifiers are usually stored in a std::set<Modifier>.
        return VisitAction::WALK_CHILDREN;
    }
    curNode->visitedByWalkerID = ID;
    VisitAction action = VisitAction::WALK_CHILDREN;
    if (VisitPre) {
        // If VisitPost function is given, call it first.
        action = VisitPre(curNode);
    }

    if (action == VisitAction::STOP_NOW) {
        return action;
    }
    if (action == VisitAction::WALK_CHILDREN) {
        if (Is<Expr>(curNode)) {
            auto expr = StaticAs<ASTKind::EXPR>(curNode);
            if (Walk(expr->desugarExpr.get()) == VisitAction::STOP_NOW) {
                return VisitAction::STOP_NOW;
            }
        } else if (Is<Decl>(curNode)) {
            auto decl = StaticCast<Decl*>(curNode);
            for (auto& it : decl->annotations) {
                if (Walk(it.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
            }
            if (Walk(decl->annotationsArray.get()) == VisitAction::STOP_NOW) {
                return VisitAction::STOP_NOW;
            }
        }
        switch (curNode->astKind) {
            case ASTKind::PACKAGE: {
                auto package = StaticAs<ASTKind::PACKAGE>(curNode);
                for (auto& instantiatedDecl : package->genericInstantiatedDecls) {
                    if (Walk(instantiatedDecl.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                for (auto& it : package->files) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                // Source imported decls also should be walked.
                for (auto& srcFunc : package->srcImportedNonGenericDecls) {
                    if (Walk(srcFunc) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::FILE: {
                auto file = StaticAs<ASTKind::FILE>(curNode);
                if (Walk(file->package.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                for (auto& it : file->imports) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                for (auto& decl : file->exportedInternalDecls) {
                    if (Walk(decl.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                for (auto& it : file->decls) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::PRIMARY_CTOR_DECL: {
                auto pcd = StaticAs<ASTKind::PRIMARY_CTOR_DECL>(curNode);
                for (auto modifier : pcd->modifiers) {
                    if (Walk(&modifier) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                if (Walk(pcd->funcBody.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::MACRO_DECL: {
                auto md = StaticAs<ASTKind::MACRO_DECL>(curNode);
                for (auto modifier : md->modifiers) {
                    if (Walk(&modifier) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                if (!md->desugarDecl) {
                    if (Walk(md->funcBody.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                } else {
                    if (Walk(md->desugarDecl.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::MAIN_DECL: {
                auto md = StaticAs<ASTKind::MAIN_DECL>(curNode);
                if (md->desugarDecl) {
                    if (Walk(md->desugarDecl.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                } else {
                    if (Walk(md->funcBody.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::FUNC_DECL: {
                auto fd = StaticAs<ASTKind::FUNC_DECL>(curNode);
                for (auto modifier : fd->modifiers) {
                    if (Walk(&modifier) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                if (Walk(fd->funcBody.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::FUNC_BODY: {
                auto fb = StaticAs<ASTKind::FUNC_BODY>(curNode);
                if (Walk(fb->generic.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                for (auto& paramList : fb->paramLists) {
                    if (Walk(paramList.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                if (Walk(fb->retType.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(fb->body.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::FUNC_PARAM_LIST: {
                auto fpl = StaticAs<ASTKind::FUNC_PARAM_LIST>(curNode);
                for (auto& param : fpl->params) {
                    if (Walk(param.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::FUNC_PARAM: {
                auto fp = StaticAs<ASTKind::FUNC_PARAM>(curNode);
                if (Walk(fp->type.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(fp->assignment.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(fp->desugarDecl.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::MACRO_EXPAND_PARAM: {
                auto mep = StaticAs<ASTKind::MACRO_EXPAND_PARAM>(curNode);
                if (Walk(mep->invocation.decl.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::PROP_DECL: {
                auto pd = StaticAs<ASTKind::PROP_DECL>(curNode);
                for (auto modifier : pd->modifiers) {
                    if (Walk(&modifier) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                if (Walk(pd->type.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                for (auto& it : pd->getters) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                for (auto& it : pd->setters) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::VAR_WITH_PATTERN_DECL: {
                auto vpd = StaticAs<ASTKind::VAR_WITH_PATTERN_DECL>(curNode);
                if (Walk(vpd->irrefutablePattern.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(vpd->type.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(vpd->initializer.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::VAR_DECL: {
                auto vd = StaticAs<ASTKind::VAR_DECL>(curNode);
                for (auto modifier : vd->modifiers) {
                    if (Walk(&modifier) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                if (Walk(vd->type.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(vd->initializer.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::TYPE_ALIAS_DECL: {
                auto ta = StaticAs<ASTKind::TYPE_ALIAS_DECL>(curNode);
                if (Walk(ta->generic.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(ta->type.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::CLASS_DECL: {
                auto cd = StaticAs<ASTKind::CLASS_DECL>(curNode);
                for (auto modifier : cd->modifiers) {
                    if (Walk(&modifier) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                if (Walk(cd->generic.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                for (auto& refType : cd->inheritedTypes) {
                    if (Walk(refType.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                if (Walk(cd->body.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::INTERFACE_DECL: {
                auto id = StaticAs<ASTKind::INTERFACE_DECL>(curNode);
                for (auto modifier : id->modifiers) {
                    if (Walk(&modifier) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                if (Walk(id->generic.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                for (auto& it : id->inheritedTypes) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                if (Walk(id->body.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::ENUM_DECL: {
                auto ed = StaticAs<ASTKind::ENUM_DECL>(curNode);
                if (Walk(ed->generic.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                for (auto& it : ed->inheritedTypes) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                for (auto& it : ed->constructors) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                for (auto& it : ed->members) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::STRUCT_DECL: {
                auto sd = StaticAs<ASTKind::STRUCT_DECL>(curNode);
                for (auto modifier : sd->modifiers) {
                    if (Walk(&modifier) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                if (Walk(sd->generic.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                for (auto& it : sd->inheritedTypes) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                if (Walk(sd->body.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::STRUCT_BODY: {
                auto rb = StaticAs<ASTKind::STRUCT_BODY>(curNode);
                for (auto& it : rb->decls) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::EXTEND_DECL: {
                auto ed = StaticAs<ASTKind::EXTEND_DECL>(curNode);
                if (Walk(ed->extendedType.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                for (auto& it : ed->inheritedTypes) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                if (Walk(ed->generic.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                for (auto& it : ed->members) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::CLASS_BODY: {
                auto cb = StaticAs<ASTKind::CLASS_BODY>(curNode);
                for (auto& it : cb->decls) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::INTERFACE_BODY: {
                auto ib = StaticAs<ASTKind::INTERFACE_BODY>(curNode);
                for (auto& it : ib->decls) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::MACRO_EXPAND_DECL: {
                auto med = StaticAs<ASTKind::MACRO_EXPAND_DECL>(curNode);
                if (Walk(med->invocation.decl.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::MACRO_EXPAND_EXPR: {
                auto mee = StaticAs<ASTKind::MACRO_EXPAND_EXPR>(curNode);
                if (Walk(mee->invocation.decl.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::IF_EXPR: {
                auto ie = StaticAs<ASTKind::IF_EXPR>(curNode);
                if (Walk(ie->condExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(ie->thenBody.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (ie->hasElse) {
                    if (Walk(ie->elseBody.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::LET_PATTERN_DESTRUCTOR: {
                auto lpd = StaticAs<ASTKind::LET_PATTERN_DESTRUCTOR>(curNode);
                for (auto& p : lpd->patterns) {
                    if (Walk(p.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                if (Walk(lpd->initializer.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::MATCH_CASE: {
                auto mc = StaticAs<ASTKind::MATCH_CASE>(curNode);
                for (auto& it : mc->patterns) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                if (Walk(mc->patternGuard.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(mc->exprOrDecls.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::MATCH_CASE_OTHER: {
                auto mco = StaticAs<ASTKind::MATCH_CASE_OTHER>(curNode);
                if (Walk(mco->matchExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(mco->exprOrDecls.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::MATCH_EXPR: {
                auto me = StaticAs<ASTKind::MATCH_EXPR>(curNode);
                if (me->matchMode) {
                    if (Walk(me->selector.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                    for (auto& it : me->matchCases) {
                        if (Walk(it.get()) == VisitAction::STOP_NOW) {
                            return VisitAction::STOP_NOW;
                        }
                    }
                } else {
                    for (auto& it : me->matchCaseOthers) {
                        if (Walk(it.get()) == VisitAction::STOP_NOW) {
                            return VisitAction::STOP_NOW;
                        }
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::TRY_EXPR: {
                auto te = StaticAs<ASTKind::TRY_EXPR>(curNode);
                for (auto& it : te->resourceSpec) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                if (Walk(te->tryBlock.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                for (uint32_t cnt = 0; cnt < te->catchPatterns.size(); ++cnt) {
                    if (Walk(te->catchPatterns[cnt].get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                for (uint32_t cnt = 0; cnt < te->catchBlocks.size(); ++cnt) {
                    if (Walk(te->catchBlocks[cnt].get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                if (Walk(te->finallyBlock.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::THROW_EXPR: {
                auto te = StaticAs<ASTKind::THROW_EXPR>(curNode);
                if (Walk(te->expr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::RETURN_EXPR: {
                auto re = StaticAs<ASTKind::RETURN_EXPR>(curNode);
                if (!re->desugarExpr) {
                    if (Walk(re->expr.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::FOR_IN_EXPR: {
                auto fie = StaticAs<ASTKind::FOR_IN_EXPR>(curNode);
                if (Walk(fie->pattern.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(fie->inExpression.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(fie->patternGuard.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(fie->body.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::WHILE_EXPR: {
                auto we = StaticAs<ASTKind::WHILE_EXPR>(curNode);
                if (Walk(we->condExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(we->body.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::DO_WHILE_EXPR: {
                auto dwe = StaticAs<ASTKind::DO_WHILE_EXPR>(curNode);
                if (Walk(dwe->body.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(dwe->condExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::ASSIGN_EXPR: {
                auto ae = StaticAs<ASTKind::ASSIGN_EXPR>(curNode);
                if (Walk(ae->leftValue.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(ae->rightExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::INC_OR_DEC_EXPR: {
                auto expr = StaticAs<ASTKind::INC_OR_DEC_EXPR>(curNode);
                if (Walk(expr->expr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::UNARY_EXPR: {
                auto ue = StaticAs<ASTKind::UNARY_EXPR>(curNode);
                if (Walk(ue->expr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::BINARY_EXPR: {
                auto be = StaticAs<ASTKind::BINARY_EXPR>(curNode);
                if (Walk(be->leftExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(be->rightExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::RANGE_EXPR: {
                auto re = StaticAs<ASTKind::RANGE_EXPR>(curNode);
                if (Walk(re->startExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(re->stopExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(re->stepExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::SUBSCRIPT_EXPR: {
                auto se = StaticAs<ASTKind::SUBSCRIPT_EXPR>(curNode);
                if (Walk(se->baseExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                for (auto& it : se->indexExprs) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::MEMBER_ACCESS: {
                auto ma = StaticAs<ASTKind::MEMBER_ACCESS>(curNode);
                if (Walk(ma->baseExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                for (auto& it : ma->typeArguments) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::FUNC_ARG: {
                auto fa = StaticAs<ASTKind::FUNC_ARG>(curNode);
                if (Walk(fa->expr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::CALL_EXPR: {
                auto ce = StaticAs<ASTKind::CALL_EXPR>(curNode);
                if (Walk(ce->baseFunc.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (ce->desugarArgs.has_value()) {
                    for (auto& it : ce->desugarArgs.value()) {
                        if (Walk(it) == VisitAction::STOP_NOW) {
                            return VisitAction::STOP_NOW;
                        }
                    }
                } else { // 'desugarArgs' contains 'ce->args'.
                    for (auto& it : ce->args) {
                        if (Walk(it.get()) == VisitAction::STOP_NOW) {
                            return VisitAction::STOP_NOW;
                        }
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::PAREN_EXPR: {
                auto pe = StaticAs<ASTKind::PAREN_EXPR>(curNode);
                if (Walk(pe->expr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::LAMBDA_EXPR: {
                auto le = StaticAs<ASTKind::LAMBDA_EXPR>(curNode);
                if (Walk(le->funcBody.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::TRAIL_CLOSURE_EXPR: {
                auto tce = StaticAs<ASTKind::TRAIL_CLOSURE_EXPR>(curNode);
                if (Walk(tce->expr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(tce->lambda.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::LIT_CONST_EXPR: {
                auto lce = StaticAs<ASTKind::LIT_CONST_EXPR>(curNode);
                if (Walk(lce->ref.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (!lce->desugarExpr && Walk(lce->siExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::STR_INTERPOLATION_EXPR: {
                auto sie = StaticAs<ASTKind::STR_INTERPOLATION_EXPR>(curNode);
                for (auto& it : sie->strPartExprs) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::INTERPOLATION_EXPR: {
                auto ie = StaticAs<ASTKind::INTERPOLATION_EXPR>(curNode);
                if (Walk(ie->block.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::ARRAY_LIT: {
                auto al = StaticAs<ASTKind::ARRAY_LIT>(curNode);
                for (auto& it : al->children) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::ARRAY_EXPR: {
                auto asl = StaticAs<ASTKind::ARRAY_EXPR>(curNode);
                if (Walk(asl->type.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                for (auto& it : asl->args) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::POINTER_EXPR: {
                auto ptrExpr = StaticAs<ASTKind::POINTER_EXPR>(curNode);
                if (Walk(ptrExpr->type.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(ptrExpr->arg.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::TUPLE_LIT: {
                auto tl = StaticAs<ASTKind::TUPLE_LIT>(curNode);
                for (auto& it : tl->children) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::TYPE_CONV_EXPR: {
                auto expr = StaticAs<ASTKind::TYPE_CONV_EXPR>(curNode);
                if (Walk(expr->type.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(expr->expr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::REF_EXPR: {
                auto re = StaticAs<ASTKind::REF_EXPR>(curNode);
                for (auto& it : re->typeArguments) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::IF_AVAILABLE_EXPR: {
                auto ie = StaticCast<IfAvailableExpr>(curNode);
                if (Walk(ie->GetArg()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(ie->GetLambda1()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(ie->GetLambda2()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::BLOCK: {
                auto block = StaticAs<ASTKind::BLOCK>(curNode);
                for (auto& it : block->body) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::REF_TYPE: {
                auto rt = StaticAs<ASTKind::REF_TYPE>(curNode);
                for (auto& typeArg : rt->typeArguments) {
                    if (Walk(typeArg.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::QUALIFIED_TYPE: {
                auto qt = StaticAs<ASTKind::QUALIFIED_TYPE>(curNode);
                if (Walk(qt->baseType.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                for (auto& ta : qt->typeArguments) {
                    if (Walk(ta.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::OPTION_TYPE: {
                auto ot = StaticAs<ASTKind::OPTION_TYPE>(curNode);
                if (Walk(ot->desugarType.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(ot->componentType.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::CONSTANT_TYPE: {
                auto ct = StaticAs<ASTKind::CONSTANT_TYPE>(curNode);
                if (Walk(ct->constantExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::VARRAY_TYPE: {
                auto vt = StaticAs<ASTKind::VARRAY_TYPE>(curNode);
                if (Walk(vt->typeArgument.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(vt->constantType.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::PAREN_TYPE: {
                auto pt = StaticAs<ASTKind::PAREN_TYPE>(curNode);
                if (Walk(pt->type.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::FUNC_TYPE: {
                auto ft = StaticAs<ASTKind::FUNC_TYPE>(curNode);
                for (auto& paramType : ft->paramTypes) {
                    if (Walk(paramType.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                if (Walk(ft->retType.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::TUPLE_TYPE: {
                auto tt = StaticAs<ASTKind::TUPLE_TYPE>(curNode);
                for (auto& it : tt->fieldTypes) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::GENERIC_CONSTRAINT: {
                auto gc = StaticAs<ASTKind::GENERIC_CONSTRAINT>(curNode);
                if (Walk(gc->type.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                for (auto& upperBound : gc->upperBounds) {
                    if (Walk(upperBound.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::CONST_PATTERN: {
                auto cp = StaticAs<ASTKind::CONST_PATTERN>(curNode);
                if (Walk(cp->literal.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(cp->operatorCallExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::VAR_PATTERN: {
                auto vp = StaticAs<ASTKind::VAR_PATTERN>(curNode);
                if (Walk(vp->varDecl.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::TUPLE_PATTERN: {
                auto tp = StaticAs<ASTKind::TUPLE_PATTERN>(curNode);
                for (auto& pattern : tp->patterns) {
                    if (Walk(pattern.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::TYPE_PATTERN: {
                auto tp = StaticAs<ASTKind::TYPE_PATTERN>(curNode);
                if (Walk(tp->pattern.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(tp->type.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::ENUM_PATTERN: {
                auto ep = StaticAs<ASTKind::ENUM_PATTERN>(curNode);
                if (Walk(ep->constructor.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                for (auto& pattern : ep->patterns) {
                    if (Walk(pattern.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::VAR_OR_ENUM_PATTERN: {
                auto vep = StaticAs<ASTKind::VAR_OR_ENUM_PATTERN>(curNode);
                if (Walk(vep->pattern.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::EXCEPT_TYPE_PATTERN: {
                auto& exceptPattern = *StaticCast<ExceptTypePattern*>(curNode);
                if (Walk(exceptPattern.pattern.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                for (auto& i : exceptPattern.types) {
                    if (Walk(i.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::ANNOTATION: {
                auto anno = StaticAs<ASTKind::ANNOTATION>(curNode);
                if (Walk(anno->baseExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                for (auto& it : anno->args) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::SPAWN_EXPR: {
                auto se = StaticAs<ASTKind::SPAWN_EXPR>(curNode);
                if (se->arg && Walk(se->arg.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(se->task.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (se->futureObj) {
                    if (Walk(se->futureObj.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::SYNCHRONIZED_EXPR: {
                auto se = StaticAs<ASTKind::SYNCHRONIZED_EXPR>(curNode);
                // Notes: Seems that other part still needs information of se->mutex after desugar,
                // which seems weird. If simply break when se->desugar is not null, there are test
                // cases which fail.
                if (Walk(se->mutex.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                // If se is not desugared yet, we should be able to collect se->body.
                if (!se->desugarExpr && Walk(se->body.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::QUOTE_EXPR: {
                auto qe = StaticAs<ASTKind::QUOTE_EXPR>(curNode);
                for (auto& it : qe->exprs) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::IS_EXPR: {
                auto ie = StaticAs<ASTKind::IS_EXPR>(curNode);
                if (Walk(ie->leftExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(ie->isType.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::AS_EXPR: {
                auto ae = StaticAs<ASTKind::AS_EXPR>(curNode);
                if (Walk(ae->leftExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                if (Walk(ae->asType.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::BUILTIN_DECL: {
                auto bid = StaticAs<ASTKind::BUILTIN_DECL>(curNode);
                if (Walk(bid->generic.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::GENERIC: {
                auto generic = StaticAs<ASTKind::GENERIC>(curNode);
                for (auto& it : generic->typeParameters) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                for (auto& it : generic->genericConstraints) {
                    if (Walk(it.get()) == VisitAction::STOP_NOW) {
                        return VisitAction::STOP_NOW;
                    }
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::OPTIONAL_CHAIN_EXPR: {
                auto oce = StaticAs<ASTKind::OPTIONAL_CHAIN_EXPR>(curNode);
                // Only walk child when the optional chain is not desugared.
                if (!oce->desugarExpr && Walk(oce->expr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            case ASTKind::OPTIONAL_EXPR: {
                auto oe = StaticAs<ASTKind::OPTIONAL_EXPR>(curNode);
                if (Walk(oe->baseExpr.get()) == VisitAction::STOP_NOW) {
                    return VisitAction::STOP_NOW;
                }
                action = VisitAction::WALK_CHILDREN;
                break;
            }
            default: {
                action = VisitAction::WALK_CHILDREN;
            }
        }
    }
    // If VisitPost function is given, it will be called after children being visited.
    // The final action is a combine of visitPre, Walk(children) and visitPost.
    if (VisitPost) {
        auto optionalAction = VisitPost(curNode);
        if (optionalAction != VisitAction::KEEP_DECISION) {
            action = optionalAction;
        }
    }
    CJC_ASSERT(action != VisitAction::KEEP_DECISION);
    return action;
}
