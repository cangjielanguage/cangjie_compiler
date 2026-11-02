// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 *  file implements functions to mark extend box attribute for ast.
 */
#include "ExtendBoxMarker.h"

#include "TypeCheckUtil.h"

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace AST;

TypeManager* ExtendBoxMarker::typeManager = nullptr;
std::mutex ExtendBoxMarker::mtx;

std::function<VisitAction(Ptr<Node>)> ExtendBoxMarker::GetMarkExtendBoxFunc(TypeManager& typeMgr)
{
    typeManager = &typeMgr;
    std::function<VisitAction(Ptr<Node>)> markerFunc = [](auto node) -> VisitAction {
        CJC_ASSERT(node);
        switch (node->astKind) {
            case ASTKind::VAR_DECL:
                return MarkBoxPointHandleVarDecl(*RawStaticCast<VarDecl*>(node));
            case ASTKind::ASSIGN_EXPR:
                return MarkBoxPointHandleAssignExpr(*RawStaticCast<AssignExpr*>(node));
            case ASTKind::CALL_EXPR:
                return MarkBoxPointHandleCallExpr(*RawStaticCast<CallExpr*>(node));
            case ASTKind::IF_EXPR:
                return MarkBoxPointHandleIfExpr(*RawStaticCast<IfExpr*>(node));
            case ASTKind::RETURN_EXPR:
                return MarkBoxPointHandleReturnExpr(*RawStaticCast<ReturnExpr*>(node));
            case ASTKind::MATCH_EXPR:
                return MarkBoxPointHandleMatchExpr(*RawStaticCast<MatchExpr*>(node));
            case ASTKind::TRY_EXPR:
                return MarkBoxPointHandleTryExpr(*RawStaticCast<TryExpr*>(node));
            case ASTKind::ARRAY_EXPR:
                return MarkBoxPointHandleArrayExpr(*RawStaticCast<ArrayExpr*>(node));
            case ASTKind::ARRAY_LIT:
                return MarkBoxPointHandleArrayLit(*RawStaticCast<ArrayLit*>(node));
            case ASTKind::TUPLE_LIT:
                return MarkBoxPointHandleTupleLit(*RawStaticCast<TupleLit*>(node));
            case ASTKind::WHILE_EXPR:
                return MarkBoxPointHandleWhileExpr(StaticCast<WhileExpr>(*node));
            default:
                return VisitAction::WALK_CHILDREN;
        }
    };
    return markerFunc;
}

bool ExtendBoxMarker::NeedAutoBox(DataTy child, DataTy interface, bool isUpcast)
{
    CJC_ASSERT(typeManager);
    CJC_NULLPTR_CHECK(child);
    CJC_NULLPTR_CHECK(interface);
    auto target = interface;
    // If the 'target' has more option box than child, then get it's typeArg for box checking.
    // NOTE: The 'while' should only be entered during instantiation checking.
    //       After instantiation, the option box is happened before extend box.
    //       So this 'while' should not be entered during extend box 'AutoBoxing' step.
    while (TypeCheckUtil::CountOptionNestedLevel(child) < TypeCheckUtil::CountOptionNestedLevel(target)) {
        target = target->typeArgs[0].Ty();
    }
    bool isExtended = target && typeManager->HasExtensionRelation(*child, *target);
    if (isExtended && isUpcast) {
        typeManager->RecordUsedExtend(*child, *target);
    }
    return isExtended;
}

void ExtendBoxMarker::CheckBlockNeedBox(const Block& block, Ty& ty, Node& nodeToCheck)
{
    auto lastExpr = block.GetLastExprOrDecl();
    DataTy lastTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT);
    if (auto expr = DynamicCast<Expr*>(lastExpr); expr) {
        lastTy = expr->DataTy();
    }
    if (NeedAutoBox(lastTy, &ty)) {
        nodeToCheck.EnableAttr(Attribute::NEED_AUTO_BOX);
    }
}

VisitAction ExtendBoxMarker::MarkBoxPointHandleArrayExpr(ArrayExpr& ae)
{
    CJC_ASSERT(typeManager);
    bool secondIsFunc = ae.args.size() == 2 && ae.args[1]->GetTy().IsCorrect() && ae.args[1]->GetTy()->IsFunc();
    bool ignored = !ae.GetTy().IsCorrect() || ae.args.empty() || secondIsFunc;
    if (ignored) {
        return VisitAction::WALK_CHILDREN;
    }
    // For 'VArray<...>(repeat: T)' constructor may need auto box.
    if (ae.isValueArray) {
        if (NeedAutoBox(ae.args[0]->DataTy(), typeManager->GetTypeArgs(*ae.GetTy())[0])) {
            ae.EnableAttr(Attribute::NEED_AUTO_BOX);
        }
        return VisitAction::WALK_CHILDREN;
    }
    // For 'RawArray(size, item: T)' constructor may need auto box.
    if (ae.args.size() > 1) {
        if (NeedAutoBox(ae.args[1]->DataTy(), typeManager->GetTypeArgs(*ae.GetTy())[0])) {
            ae.EnableAttr(Attribute::NEED_AUTO_BOX);
        }
        return VisitAction::WALK_CHILDREN;
    }
    // For 'RawArray(Collection)' constructor may need auto box.
    DataTy argTy = ae.args[0]->DataTy();
    if (!Ty::IsTyCorrect(argTy)) {
        return VisitAction::WALK_CHILDREN;
    }
    auto iface = DynamicCast<InterfaceTy*>(argTy.get());
    if (!iface || !iface->decl || iface->decl->identifier != "Collection") {
        return VisitAction::WALK_CHILDREN;
    }
    Ptr<InterfaceDecl> collDecl = iface->GetGenericTy() ? iface->GetGenericTy()->decl : iface->decl;
    DataTy eleTy = typeManager->GetTypeArgs(*ae.GetTy())[0];
    DataTy expectedColl = typeManager->GetInterfaceTy(*collDecl, {eleTy});
    if (NeedAutoBox(argTy, expectedColl)) {
        ae.EnableAttr(Attribute::NEED_AUTO_BOX);
    }
    return VisitAction::WALK_CHILDREN;
}

/* In TryExpr:
 * If the context does explicitly require a certain type, the tryBlock and
 * each of the catchBlocks (if present) are required to be a subtype of the required type.
 * If the context does not explicitly require a certain type, the tryBlock and
 * all catchBlocks(if present) are required to have their least common super type,
 * which is also the type of the entire tryExpr.
 * When the entire tryExpr is an Interface type, and one of the tryBlock or catchBlocks is
 * an type which extend the Interface, or non-class type which implements the Interface,
 * we need to box it.
 * */
VisitAction ExtendBoxMarker::MarkBoxPointHandleTryExpr(TryExpr& te)
{
    if (!te.GetTy().IsCorrect()) {
        return VisitAction::WALK_CHILDREN;
    }
    if (te.tryBlock) {
        CheckBlockNeedBox(*te.tryBlock, *te.GetTy(), te);
    }

    for (auto& cb : te.catchBlocks) {
        CJC_NULLPTR_CHECK(cb);
        CheckBlockNeedBox(*cb, *te.GetTy(), te);
    }
    return VisitAction::WALK_CHILDREN;
}

bool ExtendBoxMarker::IsTypePatternNeedBox(Ptr<Pattern> pattern, Ty& selectorTy)
{
    if (selectorTy.IsNothing() || pattern == nullptr) {
        return false;
    }
    // NOTE: we need to collect all boxed types, so DO NOT interrupt loop early.
    bool boxOrUnbox = false;
    switch (pattern->astKind) {
        case ASTKind::TYPE_PATTERN: {
            auto typePattern = RawStaticCast<TypePattern*>(pattern);
            CJC_ASSERT(typePattern->type && typePattern->GetTy() && typePattern->type->GetTy() == typePattern->GetTy());
            // Downcast or Upcast.
            bool cond = NeedAutoBox(typePattern->DataTy(), &selectorTy, false) ||
                NeedAutoBox(&selectorTy, typePattern->DataTy());
            boxOrUnbox = cond || MustUnboxDownCast(selectorTy, *typePattern->GetTy());
            break;
        }
        case ASTKind::TUPLE_PATTERN: {
            auto tuplePattern = StaticCast<TuplePattern*>(pattern);
            auto tupleTy = StaticCast<TupleTy*>(&selectorTy);
            for (size_t i = 0; i < tuplePattern->patterns.size(); i++) {
                CJC_ASSERT(tupleTy->typeArgs[i]);
                bool cond = IsTypePatternNeedBox(tuplePattern->patterns[i].get(), *tupleTy->typeArgs[i]);
                boxOrUnbox = boxOrUnbox || cond;
            }
            break;
        }
        case ASTKind::ENUM_PATTERN: {
            auto enumPattern = StaticCast<EnumPattern*>(pattern);
            CJC_ASSERT(enumPattern->constructor && enumPattern->constructor->GetTy());
            auto constructorTy = DynamicCast<FuncTy>(enumPattern->constructor->DataTy());
            if (!constructorTy) { // Enum pattern may without param.
                break;
            }
            for (size_t i = 0; i < enumPattern->patterns.size(); i++) {
                auto paramTy = constructorTy->paramTys[i];
                CJC_ASSERT(paramTy);
                bool cond = IsTypePatternNeedBox(enumPattern->patterns[i].get(), *paramTy);
                boxOrUnbox = boxOrUnbox || cond;
            }
            break;
        }
        default:
            break;
    }
    return boxOrUnbox;
}

void ExtendBoxMarker::MarkBoxPointHandleCondition(Expr& e)
{
    // record outermost condition to mark as box
    auto& target = e;
    std::stack<Expr*> st;
    st.push(&e);
    while (!st.empty()) {
        auto expr = st.top();
        st.pop();
        if (!expr || !expr->GetTy().IsCorrect()) {
            continue;
        }
        if (auto let = DynamicCast<LetPatternDestructor>(expr)) {
            if (!let->initializer || !let->initializer->GetTy().IsCorrect()) {
                continue;
            }
            for (auto& pat : std::as_const(let->patterns)) {
                if (IsTypePatternNeedBox(pat.get(), *let->initializer->GetTy())) {
                    target.EnableAttr(Attribute::NEED_AUTO_BOX);
                    pat->EnableAttr(Attribute::NEED_AUTO_BOX);
                }
            }
        }
        if (auto par = DynamicCast<ParenExpr>(expr)) {
            st.push(&*par->expr);
        }
        if (auto bin = DynamicCast<BinaryExpr>(expr); bin && (bin->op == TokenKind::AND || bin->op == TokenKind::OR)) {
            st.push(&*bin->leftExpr);
            st.push(&*bin->rightExpr);
        }
    }
}

VisitAction ExtendBoxMarker::MarkBoxPointHandleMatchExpr(MatchExpr& me)
{
    if (!me.GetTy().IsCorrect()) {
        return VisitAction::SKIP_CHILDREN;
    }
    // NOTE: we also need to collect all boxed types, so do not interrupt loop early.
    for (auto& matchCase : me.matchCases) {
        // Primary constructors and their desugared components (such as default params) are not type checked and do
        // not have a correct semantic type field.
        if (!me.selector || !me.selector->GetTy().IsCorrect()) {
            continue;
        }
        for (auto& pattern : matchCase->patterns) {
            if (IsTypePatternNeedBox(pattern.get(), *me.selector->GetTy())) {
                // It's possible that childs have different box type, so does not break after match.
                // auto box or unbox.
                me.EnableAttr(Attribute::NEED_AUTO_BOX);
            }
        }

        if (matchCase->exprOrDecls) {
            CheckBlockNeedBox(*matchCase->exprOrDecls, *me.GetTy(), me);
        }
    }
    for (auto& matchCase : me.matchCaseOthers) {
        if (matchCase->exprOrDecls) {
            CheckBlockNeedBox(*matchCase->exprOrDecls, *me.GetTy(), me);
        }
    }
    return VisitAction::WALK_CHILDREN;
}

VisitAction ExtendBoxMarker::MarkBoxPointHandleArrayLit(ArrayLit& lit)
{
    if (lit.GetTy().IsCorrect() && lit.GetTy()->typeArgs.size() == 1) {
        for (auto& child : lit.children) {
            // It's possible that child 0 and child 1 needs different box type, so does not break after match.
            if (child->GetTy() && NeedAutoBox(child->DataTy(), lit.GetTy()->TyArgs()[0])) {
                lit.EnableAttr(Attribute::NEED_AUTO_BOX);
            }
        }
    }
    return VisitAction::WALK_CHILDREN;
}

VisitAction ExtendBoxMarker::MarkBoxPointHandleReturnExpr(ReturnExpr& re)
{
    if (re.expr && re.refFuncBody && re.refFuncBody->GetTy() && re.refFuncBody->TyKind() == TypeKind::TYPE_FUNC) {
        auto funcTy = RawStaticCast<FuncTy*>(re.refFuncBody->DataTy());
        if (NeedAutoBox(re.expr->DataTy(), funcTy->retTy.Ty())) {
            re.EnableAttr(Attribute::NEED_AUTO_BOX);
        }
    }
    return VisitAction::WALK_CHILDREN;
}

VisitAction ExtendBoxMarker::MarkBoxPointHandleIfExpr(IfExpr& ie)
{
    if (ie.GetTy().IsCorrect() && (ie.condExpr && ie.condExpr->GetTy().IsCorrect())) {
        MarkBoxPointHandleCondition(*ie.condExpr);
    }
    if (ie.GetTy().IsCorrect() && !ie.GetTy()->IsUnitOrNothing()) {
        if (ie.thenBody) {
            CheckBlockNeedBox(*ie.thenBody, *ie.GetTy(), ie);
        }
        if (!ie.hasElse || !ie.elseBody) {
            return VisitAction::WALK_CHILDREN;
        }
        if (auto block = DynamicCast<Block*>(ie.elseBody.get()); block) {
            CheckBlockNeedBox(*block, *ie.GetTy(), ie);
        }
    }
    return VisitAction::WALK_CHILDREN;
}

VisitAction ExtendBoxMarker::MarkBoxPointHandleWhileExpr(const AST::WhileExpr& we)
{
    if (we.GetTy().IsCorrect() && we.condExpr && we.condExpr->GetTy().IsCorrect()) {
        MarkBoxPointHandleCondition(*we.condExpr);
    }
    return VisitAction::WALK_CHILDREN;
}

VisitAction ExtendBoxMarker::MarkBoxPointHandleCallExpr(CallExpr& ce)
{
    if (!ce.baseFunc || !ce.baseFunc->GetTy() || ce.baseFunc->TyKind() != TypeKind::TYPE_FUNC) {
        return VisitAction::WALK_CHILDREN;
    }
    auto funcTy = RawStaticCast<FuncTy*>(ce.baseFunc->DataTy());
    unsigned count = 0;
    auto callCheck = [&count, &funcTy, &ce](auto begin, auto end) {
        for (auto it = begin; it != end; ++it) {
            if (count >= funcTy->paramTys.size()) {
                break;
            }
            auto& paramTy = funcTy->paramTys[count];
            // It's possible that childs have different box type, so does not break after match.
            if ((*it)->expr && NeedAutoBox((*it)->expr->DataTy(), paramTy.Ty())) {
                ce.EnableAttr(Attribute::NEED_AUTO_BOX);
            }
            count = count + 1;
        }
    };
    if (ce.desugarArgs.has_value()) {
        callCheck(ce.desugarArgs->begin(), ce.desugarArgs->end());
    } else {
        callCheck(ce.args.begin(), ce.args.end());
    }
    return VisitAction::WALK_CHILDREN;
}

VisitAction ExtendBoxMarker::MarkBoxPointHandleAssignExpr(AssignExpr& ae)
{
    // Desugared assign expression will be skipped.
    if (!ae.desugarExpr && ae.rightExpr && ae.leftValue &&
        NeedAutoBox(ae.rightExpr->DataTy(), ae.leftValue->DataTy())) {
        ae.EnableAttr(Attribute::NEED_AUTO_BOX);
    }
    return VisitAction::WALK_CHILDREN;
}

VisitAction ExtendBoxMarker::MarkBoxPointHandleVarDecl(VarDecl& vd)
{
    if (vd.initializer && NeedAutoBox(vd.initializer->DataTy(), vd.DataTy())) {
        vd.EnableAttr(Attribute::NEED_AUTO_BOX);
    }
    return VisitAction::WALK_CHILDREN;
}

VisitAction ExtendBoxMarker::MarkBoxPointHandleTupleLit(TupleLit& tl)
{ // Tuple literal allows element been boxed.
    auto tupleTy = DynamicCast<TupleTy*>(tl.DataTy());
    if (tupleTy == nullptr) {
        return VisitAction::WALK_CHILDREN;
    }
    auto typeArgs = tupleTy->TyArgs();
    for (size_t i = 0; i < typeArgs.size(); ++i) {
        if (NeedAutoBox(tl.children[i]->DataTy(), typeArgs[i])) {
            tl.EnableAttr(Attribute::NEED_AUTO_BOX);
        }
    }
    return VisitAction::WALK_CHILDREN;
}
