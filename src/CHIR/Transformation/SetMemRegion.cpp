// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/SetMemRegion.h"

#include "cangjie/CHIR/IR/Annotation.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/CHIR/Utils/Visitor/Visitor.h"

using namespace Cangjie::CHIR;

namespace {
std::vector<BlockGroup*> GetAllBlockGroups(const Lambda& start, BlockGroup& end)
{
    std::vector<BlockGroup*> res;
    auto current = start.GetBody();
    while (current != &end) {
        res.emplace_back(current);
        if (current->GetOwnerFunc()) {
            break;
        }
        current = current->GetOwnerExpression()->GetParentBlockGroup()->GetFuncOrLambdaBody();
    }
    res.emplace_back(&end);
    return res;
}
}

SetMemRegion::SetMemRegion(CHIRBuilder& builder)
    : builder(builder)
{
}

SetMemRegion::~SetMemRegion()
{
}

void SetMemRegion::RunOnPackage(const Package& package)
{
    for (auto& func : package.GetGlobalFuncsWithBody()) {
        RunOnFunc(*func);
    }
}

std::unordered_set<BlockGroup*> SetMemRegion::CollectFunctionRegionBlockGroup(const Function& func) const
{
    std::unordered_set<BlockGroup*> scopeNeedsRegion;
    Visitor::Visit(*func.GetBody(), [&scopeNeedsRegion](Expression& expr) -> VisitResult {
        if (Is<Exclave>(expr)) {
            return VisitResult::SKIP;
        } else if (Is<FuncCall>(expr)) {
            // 1. function call where the callee return type does not implement `Copyable` and has mode
            // `local!` or `local?`
            if (expr.GetResult()->GetType()->StripAllRefs()->IsLocalRegion()) {
                auto body = expr.GetFuncOrLambdaBody();
                CJC_NULLPTR_CHECK(body);
                scopeNeedsRegion.emplace(body);
            } else if (auto apply = DynamicCast<ApplyBase*>(&expr)) {
                // 2. constructor call where the `this` argument type does not implement `Copyable` and
                // has mode `local!` or `local?` (covers both `Apply` and its `TryApply` form)
                if (auto func = DynamicCast<Function*>(apply->GetCallee());
                    func && func->IsConstructor() && func->GetParentCustomTypeDef() != nullptr) {
                    auto thisType = func->GetParam(0)->GetType()->StripAllRefs();
                    if (thisType->IsLocalRegion()) {
                        auto body = expr.GetFuncOrLambdaBody();
                        CJC_NULLPTR_CHECK(body);
                        scopeNeedsRegion.emplace(body);
                    }
                }
            }
        } else if (Is<Lambda>(expr)) {
            // 3. lambda where the captured variables are not `Copyable` and have mode `local!` or `local?`
            auto lambda = StaticCast<Lambda*>(&expr);
            if (LambdaIsUnused(*lambda)) {
                return VisitResult::SKIP;
            }
            for (auto var : lambda->GetCapturedVariables()) {
                if (var->GetType()->StripAllRefs()->IsLocalRegion()) {
                    auto varBody = var->GetFuncOrLambdaBody();
                    CJC_NULLPTR_CHECK(varBody);
                    for (auto blockGroup : GetAllBlockGroups(*lambda, *varBody)) {
                        scopeNeedsRegion.emplace(blockGroup);
                    }
                }
            }
        } else if (Is<AllocateBase>(expr)) {
            // 4. allocate a non-trivial enum whose type has mode `local!` or `local?`
            auto resultType = expr.GetResult()->GetType()->StripAllRefs();
            if (resultType->IsLocalRegion() && resultType->IsEnum() &&
                !StaticCast<EnumType*>(resultType)->GetEnumDef()->IsAllCtorsTrivial()) {
                auto body = expr.GetFuncOrLambdaBody();
                CJC_NULLPTR_CHECK(body);
                scopeNeedsRegion.emplace(body);
            }
        } else if (Is<Constant>(expr)) {
            // 5. a literal whose type does not implement `Copyable` and has mode `local!`.
            if (expr.GetResult()->GetType()->StripAllRefs()->IsMustLocalRegion()) {
                auto body = expr.GetFuncOrLambdaBody();
                CJC_NULLPTR_CHECK(body);
                scopeNeedsRegion.emplace(body);
            }
        }
        return VisitResult::CONTINUE;
    });
    // A callee that returns `@local!` / `@local?` must allocate into the *caller's* active region.
    // Wrapping this function body in StartRegion/EndRegion would end the region before the return
    // value is consumed. Virtual wrappers may report Box<>& as GetReturnType(); use the raw method.
    auto retTy = func.GetReturnType();
    if (auto rawMethod = func.Get<WrappedRawMethod>()) {
        retTy = rawMethod->GetReturnType();
    }
    if (retTy->StripAllRefs()->IsLocalRegion()) {
        scopeNeedsRegion.erase(func.GetBody());
    }
    return scopeNeedsRegion;
}

void SetMemRegion::RunOnFunc(const Function& func)
{
    SetRegions(CollectFunctionRegionBlockGroup(func));
    FlattenExclave(*func.GetBody());
}

void SetMemRegion::SetRegions(const std::unordered_set<BlockGroup*>& bgsNeedRegion)
{
    if (bgsNeedRegion.empty()) {
        return;
    }
    for (auto bg : bgsNeedRegion) {
        // 1. set start region
        auto entryBlock = bg->GetEntryBlock();
        auto startRegion = builder.CreateExpression<StartRegion>(builder.GetUnitTy(), entryBlock);
        entryBlock->InsertExprIntoHead(*startRegion);

        // 2. set end region
        Visitor::Visit(*bg, [this](Expression& expr) -> VisitResult {
            // don't visit nested lambdas or exclaves
            // we will visit lambda's body later because `bgsNeedRegion` contains it
            if (Is<Lambda>(expr) || Is<Exclave>(expr)) {
                return VisitResult::SKIP;
            } else if (Is<Exit>(expr)) {
                auto parentBlock = expr.GetParentBlock();
                auto endRegion = builder.CreateExpression<EndRegion>(builder.GetUnitTy(), parentBlock);
                endRegion->MoveBefore(&expr);
                return VisitResult::CONTINUE;
            } else if (auto raise = DynamicCast<RaiseException*>(&expr)) {
                // Same-frame catch must keep the current local region alive (see catch.cj):
                // EndRegion before RaiseException(..., catchBlock) would free the region, then
                // post-catch `@local!` allocations fail with "without local object region".
                // Only end the region when unwinding out of this frame (no exception successor).
                if (raise->GetExceptionBlock() != nullptr) {
                    return VisitResult::CONTINUE;
                }
                auto parentBlock = expr.GetParentBlock();
                auto endRegion = builder.CreateExpression<EndRegion>(builder.GetUnitTy(), parentBlock);
                endRegion->MoveBefore(&expr);
                return VisitResult::CONTINUE;
            }
            return VisitResult::CONTINUE;
        });
    }
}

void SetMemRegion::FlattenExclave(BlockGroup& funcBody)
{
    auto preVisit = []([[maybe_unused]] Expression& e) -> VisitResult {
        return VisitResult::CONTINUE;
    };
    auto postVisit = [this](Expression& e) -> VisitResult {
        if (!Is<Exclave>(e)) {
            return VisitResult::CONTINUE;
        }
        auto exclave = StaticCast<Exclave*>(&e);
        // 1. move the sub blocks to the parent block group
        auto outerBG = exclave->GetParentBlockGroup();
        auto subEntryBlock = exclave->GetBody()->GetEntryBlock();
        for (auto subBlock : exclave->GetBody()->GetBlocks()) {
            subBlock->MoveTo(*outerBG);
        }
        // 2. replace the exclave terminator with a goto to the sub entry block
        auto parentBlock = exclave->GetParentBlock();
        auto gotoExpr = builder.CreateTerminator<GoTo>(subEntryBlock, parentBlock);
        exclave->ReplaceWith(*gotoExpr);
        return VisitResult::CONTINUE;
    };
    Visitor::Visit(funcBody, preVisit, postVisit);
}