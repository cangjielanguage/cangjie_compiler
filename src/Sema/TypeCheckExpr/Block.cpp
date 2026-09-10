// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "TypeCheckerImpl.h"

#include "DiagSuppressor.h"
#include "Diags.h"

using namespace Cangjie;
using namespace Sema;

bool TypeChecker::TypeCheckerImpl::SynthesizeAndReplaceIdealTy(const CheckerContext& ctx, Node& node)
{
    // Call `Synthesize` on declares containing invalid types may return valid types.
    // Therefore, we need to know if there are any errors during the inference process.
    auto ds = DiagSuppressor(diag);
    bool valid = Synthesize(ctx, &node).IsCorrect() && !ds.HasError();
    // Keep ideal literal types pending for the implicit-return expression of a lambda that is a
    // direct argument of a generic call, so generic type argument inference can unify it against
    // the expected contextual type (e.g. `TypeTest({=> 0})` where the expected type is
    // `TypeTest<() -> Int32>`). Sub-expressions that themselves carry the contextual return type
    // (binary operands, if/tuple/paren bodies) are handled by their own synthesis functions.
    if (valid && !(ctx.SynthPos() == SynPos::IMPLICIT_RETURN && ctx.Ctx().inFuncArgLambdaBody > 0)) {
        valid = ReplaceIdealTy(node);
    } else if (valid && node.GetTy().IsCorrect()) {
        // A literal in the implicit-return position of a func-arg lambda also keeps its *modal*
        // pending (IDEAL), mirroring IDEAL_INT/IDEAL_FLOAT for the data type: the literal's modal
        // cannot be inferred from context yet (the expected type is established later, during
        // generic type argument inference). Marking it IDEAL lets the solver unify it against the
        // expected contextual modal, and it defaults back to ~local (NOT) if left unresolved.
        // Only the bare literal itself gets IDEAL; composite expressions (if/tuple/paren) inherit
        // the modal of their literal operands through their own JoinMode, which treats IDEAL as
        // transparent. Constructor calls are not handled here: their modal is determined by the
        // selected init's `this` specifier (spec: only a matching init can construct C @m), not the
        // "any modal is valid" semantics of literals, so IDEAL does not apply.
        if (node.astKind == ASTKind::LIT_CONST_EXPR && !typeManager.ImplementsCopyInterface(node.DataTy())) {
            node.SetTy(node.GetTy().With(Mode::IDEAL));
        }
    }
    ds.ReportDiag();
    return valid;
}

ModalTy TypeChecker::TypeCheckerImpl::SynBlock(const CheckerContext& ctx, Block& b)
{
    if (b.body.empty()) {
        b.SetTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
    } else {
        bool existInvalid = false;
        for (size_t i = 0; i < b.body.size(); i++) {
            auto& node = b.body[i];
            auto exprPos = i == b.body.size() - 1 && ctx.SynthPos() != SynPos::UNUSED ? SynPos::IMPLICIT_RETURN : SynPos::UNUSED;
            existInvalid = !SynthesizeAndReplaceIdealTy(ctx.With(exprPos), *node) || existInvalid;
        }
        Ptr<Node> lastNode = b.body[b.body.size() - 1].get();
        CJC_ASSERT(lastNode != nullptr);
        if (existInvalid) {
            b.SetTy({TypeManager::GetInvalidTy()});
        } else if (lastNode->IsDecl()) {
            b.SetTy({TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)});
        } else {
            b.SetTy(lastNode->GetTy());
        }
    }
    return b.GetTy();
}

bool TypeChecker::TypeCheckerImpl::ChkBlock(ASTContext& ctx, ModalTy target, Block& b)
{
    ModalTy unitTy = {TypeManager::GetPrimitiveTy(TypeKind::TYPE_UNIT)};
    if (b.body.empty()) {
        b.SetTy(unitTy);
        // NOTE: This function may return false, the caller should handle diagnostics.
        // Only unsafe block is allowed to exist on its own, and needs to diagnose here.
        auto ret = typeManager.IsSubtype(b.GetTy(), target);
        if (!ret && b.TestAttr(Attribute::UNSAFE)) {
            DiagSemaMismatchedTypes(diag, b, target.String(), b.GetTy().String());
        }
        return ret;
    }
    // Synthesize the first N - 1 nodes.
    bool isWellTyped = true;
    for (size_t i = 0; i < b.body.size() - 1; i++) {
        CJC_ASSERT(b.body[i]);
        isWellTyped = SynthesizeAndReplaceIdealTy({ctx, SynPos::UNUSED}, *b.body[i]) && isWellTyped;
    }
    Ptr<Node> lastNode = b.body[b.body.size() - 1].get();
    CJC_ASSERT(lastNode != nullptr);
    // If lastNode is compiler added return, just check inner expr.
    if (!b.TestAttr(AST::Attribute::COMPILER_ADD) && lastNode->TestAttr(AST::Attribute::COMPILER_ADD) &&
        lastNode->astKind == ASTKind::RETURN_EXPR) {
        lastNode = StaticCast<ReturnExpr>(lastNode)->expr.get();
    }
    if (lastNode->IsDecl()) {
        bool typeMatched = typeManager.IsSubtype(unitTy, target);
        isWellTyped = SynthesizeAndReplaceIdealTy({ctx, SynPos::IMPLICIT_RETURN}, *lastNode) && typeMatched && isWellTyped;
        if (isWellTyped) {
            b.SetTy(unitTy);
            return true;
        } else {
            b.SetTy({TypeManager::GetInvalidTy()});
            if (!typeMatched) {
                DiagMismatchedTypesWithFoundTy(
                    diag, *lastNode, target, unitTy, "definitions and declarations are always of type 'Unit'");
            }
            return false;
        }
    } else {
        isWellTyped = Check(ctx, target, lastNode) && isWellTyped;
        if (isWellTyped) {
            b.SetTy(lastNode->GetTy());
            return true;
        }
        b.SetTy({TypeManager::GetInvalidTy()});
        return false;
    }
}
