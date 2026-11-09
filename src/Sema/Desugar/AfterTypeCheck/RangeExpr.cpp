// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Desugar/AfterTypeCheck.h"

#include "TypeCheckUtil.h"

#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"

using namespace Cangjie;
using namespace AST;
using namespace TypeCheckUtil;

namespace {
std::vector<OwnedPtr<FuncArg>> CreateRangeExprArgs(const RangeExpr& re)
{
    std::vector<OwnedPtr<FuncArg>> args;
    if (re.startExpr != nullptr) {
        args.push_back(CreateFuncArg(ASTCloner::Clone(re.startExpr.get())));
    } else {
        // If startExpr does not exist, set LitConst "0" as default value.
        auto startExpr = CreateLitConstExpr(LitConstKind::INTEGER, "0", re.GetTy()->typeArgs[0]);
        args.push_back(CreateFuncArg(std::move(startExpr)));
    }
    if (re.stopExpr != nullptr) {
        args.push_back(CreateFuncArg(ASTCloner::Clone(re.stopExpr.get())));
    } else {
        // If stopExpr does not exist, set LitConst "0" as default value.
        auto stopExpr = CreateLitConstExpr(
            LitConstKind::INTEGER, std::to_string(std::numeric_limits<int64_t>::max()), re.GetTy()->typeArgs[0]);
        args.push_back(CreateFuncArg(std::move(stopExpr)));
    }
    if (re.stepExpr != nullptr) {
        args.push_back(CreateFuncArg(ASTCloner::Clone(re.stepExpr.get())));
    } else {
        // If stepExpr does not exist, set LitConst "1" as default value.
        auto stepExpr =
            CreateLitConstExpr(LitConstKind::INTEGER, "1", {TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT64)});
        args.push_back(CreateFuncArg(std::move(stepExpr)));
    }
    std::string hasStart = re.startExpr ? "true" : "false";
    std::string hasStop = re.stopExpr ? "true" : "false";
    std::string isClosed = re.isClosed ? "true" : "false";
    auto hasStartExpr =
        CreateLitConstExpr(LitConstKind::BOOL, hasStart, {TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)});
    auto hasStopExpr =
        CreateLitConstExpr(LitConstKind::BOOL, hasStop, {TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)});
    auto isClosedExpr =
        CreateLitConstExpr(LitConstKind::BOOL, isClosed, {TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN)});
    args.push_back(CreateFuncArg(std::move(hasStartExpr)));
    args.push_back(CreateFuncArg(std::move(hasStopExpr)));
    args.push_back(CreateFuncArg(std::move(isClosedExpr)));
    return args;
}
} // namespace

namespace Cangjie::Sema::Desugar::AfterTypeCheck {
void DesugarRangeExpr(TypeManager& typeManager, RangeExpr& re)
{
    // Desugar of RangeExpr is done after typeCheck.
    if (!re.decl) {
        // RangeExpr in for-in expr does not have decl set.
        return;
    }
    CJC_NULLPTR_CHECK(re.decl->generic);
    CJC_NULLPTR_CHECK(re.GetTy());
    if (re.desugarExpr) {
        return;
    }
    if (re.GetTy()->typeArgs.empty() || re.GetTy()->typeArgs.size() != re.decl->generic->typeParameters.size() ||
        !re.decl->generic->typeParameters[0]->GetTy().IsCorrect()) {
        return;
    }
    auto rangeFunc = CreateRefExpr(re.decl->identifier);
    CopyBasicInfo(&re, rangeFunc.get());
    (void)rangeFunc->instTys.emplace_back(re.GetTy()->typeArgs[0].Ty());
    TypeSubst typeMapping;
    typeMapping.emplace(
        StaticCast<GenericsTy*>(re.decl->generic->typeParameters[0]->DataTy()), re.GetTy()->typeArgs[0].Ty());

    std::vector<OwnedPtr<FuncArg>> args = CreateRangeExprArgs(re);
    auto ce = CreateCallExpr(std::move(rangeFunc), std::move(args));
    ce->SetTy(re.GetTy());
    for (auto& initFn : re.decl->body->decls) {
        if (auto fd = AST::As<ASTKind::FUNC_DECL>(initFn.get()); fd && IsInstanceConstructor(*fd)) {
            if (auto refExpr = AST::As<ASTKind::REF_EXPR>(ce->baseFunc.get()); refExpr) {
                ReplaceTarget(refExpr, fd, false);
                CJC_NULLPTR_CHECK(fd->GetTy());
                refExpr->SetTy(typeManager.GetInstantiatedTy(fd->GetTy(), typeMapping));
                ce->resolvedFunction = fd;
                ce->callKind = CallKind::CALL_OBJECT_CREATION;
            }
            break;
        }
    }
    CopyBasicInfo(&re, ce.get());
    AddCurFile(*ce, re.curFile);
    re.desugarExpr = std::move(ce);
}
} // namespace Cangjie::Sema::Desugar::AfterTypeCheck
