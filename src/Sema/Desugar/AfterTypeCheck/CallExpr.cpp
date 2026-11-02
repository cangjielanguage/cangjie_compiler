// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Desugar/AfterTypeCheck.h"

#include "TypeCheckUtil.h"
#include "TypeCheckerImpl.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Utils.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;
using namespace Sema::Desugar::AfterTypeCheck;

void TypeChecker::TypeCheckerImpl::DesugarTokenCallExpr(ASTContext& ctx, CallExpr& ce)
{
    if (!ce.GetTy().IsCorrect() || ce.desugarExpr != nullptr || ce.sugarKind == Expr::SugarKind::TOKEN_CALL) {
        return;
    }
    if (!ce.baseFunc || ce.baseFunc->astKind != ASTKind::REF_EXPR) {
        return;
    }
    auto re = StaticCast<RefExpr>(ce.baseFunc.get());
    if (re->ref.identifier != "Token" || ce.args.empty() || ce.args.size() > G_TOKEN_ARG_NUM ||
        re->ref.targets.empty() || re->ref.targets[0]->fullPackageName != "std.ast") {
        // If Token() args num is greater than 2, the token already has a position.
        return;
    }
    std::vector<OwnedPtr<FuncArg>> args;
    auto uint32Ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_UINT32);
    auto int32Ty = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT32);
    auto fileID = CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(ce.begin.fileID), ModalTy{uint32Ty});
    // the `sugarKind` is also cloned, to prevent infinite loop in Walker
    // COMPILE_ADD attribute is not sufficent in this scenario
    auto newCe = CreateCallExpr(
        std::move(ce.baseFunc), std::move(ce.args), std::move(ce.resolvedFunction), ce.GetTy(), ce.callKind);
    newCe->sugarKind = Expr::SugarKind::TOKEN_CALL;
    args.emplace_back(CreateFuncArg(std::move(newCe)));
    args.emplace_back(CreateFuncArg(std::move(fileID)));
    auto line = CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(ce.begin.line), ModalTy{int32Ty});
    args.emplace_back(CreateFuncArg(std::move(line)));
    auto column = CreateLitConstExpr(LitConstKind::INTEGER, std::to_string(ce.begin.column), ModalTy{int32Ty});
    args.emplace_back(CreateFuncArg(std::move(column)));
    auto refreshExpr = CreateRefExprInAST("refreshPos");
    refreshExpr->begin = ce.begin;
    refreshExpr->end = ce.end;
    auto callExpr = CreateCallExpr(std::move(refreshExpr), std::move(args));
    CopyBasicInfo(&ce, callExpr.get());
    AddCurFile(*callExpr, ce.curFile);
    auto updateTy = SynthesizeWithoutRecover({ctx, SynPos::EXPR_ARG}, callExpr.get()); // Need syn to desugar.
    CJC_ASSERT(updateTy && updateTy->kind != TypeKind::TYPE_INVALID);
    ce.desugarExpr = std::move(callExpr);
}

namespace Cangjie::Sema::Desugar::AfterTypeCheck {
void DesugarComparableIntrinsic(AST::CallExpr& expr, TokenKind op)
{
    CJC_ASSERT(expr.desugarExpr == nullptr && expr.args.size() == 2); // compare intrinsic has exactly 2 args
    auto binExpr = CreateBinaryExpr(std::move(expr.args[0]->expr), std::move(expr.args[1]->expr), op);
    binExpr->SetTy(ModalTy{TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)});
    CopyBasicInfo(&expr, binExpr);
    expr.desugarExpr = std::move(binExpr);
}

void DesugarIntrinsicCallExpr(AST::CallExpr& expr)
{
    // Check whether the call is an intrinsic function call.
    if (expr.callKind != AST::CallKind::CALL_INTRINSIC_FUNCTION) {
        return;
    }

    auto target = expr.baseFunc->GetTarget();
    CJC_NULLPTR_CHECK(target);
    
    // Obtains the packageName where the intrinsic function definition is located.
    std::string packageName{};
    if (target->genericDecl) {
        packageName = target->genericDecl->fullPackageName;
    } else if (target->outerDecl && target->outerDecl->genericDecl) {
        packageName = target->outerDecl->genericDecl->fullPackageName;
    } else {
        packageName = target->fullPackageName;
    }
    
    // Find tokenkind type of the intrinsic function by package name and function name of the intrinsic function.
    std::string identfifier = target->identifier;
    TokenKind op = TokenKind::ILLEGAL;
    auto it = semaPackageMap.find(packageName);
    if (it == semaPackageMap.end() || it->second.find(identfifier) == it->second.end()) {
        return;
    }

    op = it->second.at(identfifier);
    CJC_ASSERT(op != TokenKind::ILLEGAL);

    // Desugar the intrinsic function call expr. eg: Float64Less(x, y) => x < y.
    DesugarComparableIntrinsic(expr, op);
}
} // namespace Cangjie::Sema::Desugar::AfterTypeCheck