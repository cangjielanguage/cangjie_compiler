// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/SetMemRegion.h"

#include "cangjie/CHIR/IR/Annotation.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/IR/Value/Value.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/CHIR/Utils/Visitor/Visitor.h"

using namespace Cangjie;
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

bool IsInExclave(const Expression& expr)
{
    auto blockGroup = expr.GetParentBlockGroup();
    CJC_NULLPTR_CHECK(blockGroup);
    auto owner = blockGroup->GetOwnerExpression();
    if (owner == nullptr) {
        return false;
    }
    if (Cangjie::Is<Exclave>(owner)) {
        return true;
    }
    return IsInExclave(*owner);
}

bool AllReturnValuesInExclave(const LocalVar& result)
{
    if (!result.IsRetValue()) {
        return false;
    }
    for (auto user : result.GetUsers()) {
        auto store = Cangjie::DynamicCast<Store*>(user);
        if (store == nullptr || store->GetLocation() != &result) {
            continue;
        }
        // Nothing-typed stores sit on unreachable paths after Exclave/Return/Throw
        // (themselves typed Nothing). They are not real return values of the enum.
        if (store->GetValue()->GetType()->IsNothing()) {
            continue;
        }
        if (!IsInExclave(*store)) {
            return false;
        }
    }
    return true;
}

std::pair<std::vector<Parameter*>, Type*> GetParamValAndRetType(const Expression& expr)
{
    std::vector<Parameter*> params;
    Type* retType = nullptr;
    if (auto apply = DynamicCast<const ApplyBase*>(&expr)) {
        auto f = apply->GetCallee();
        if (auto func = DynamicCast<Function*>(f)) {
            if (auto generic = func->GetGenericDecl()) {
                func = generic;
            }
            params = func->GetParams();
            retType = func->GetReturnType();
        } else if (auto localVar = DynamicCast<LocalVar*>(f)) {
            if (auto lambda = DynamicCast<Lambda*>(localVar->GetExpr())) {
                if (auto generic = lambda->GetGenericDecl()) {
                    lambda = generic;
                }
                params = lambda->GetParams();
                retType = lambda->GetReturnType();
            }
        }
    } else if (auto invoke = DynamicCast<const DynamicDispatch*>(&expr)) {
        auto callee = StaticCast<Function*>(invoke->GetCallee());
        if (auto generic = callee->GetGenericDecl()) {
            callee = generic;
        }
        params = callee->GetParams();
        retType = callee->GetReturnType();
    }
    return {params, retType};
}

bool CalleeParamOrReturnTypeHasNonCopyable(const Expression& expr)
{
    auto [params, retType] = GetParamValAndRetType(expr);
    if (retType == nullptr) {
        return false;
    }
    for (auto param : params) {
        if (param->GetType()->StripAllRefs()->IsMustLocalRegion()) {
            return true;
        }
        if (param->GetType()->StripAllRefs()->IsMaybeLocalRegion() && param->GetParamDftValFunc() != nullptr) {
            return true;
        }
    }
    if (retType->StripAllRefs()->IsLocalRegion()) {
        return true;
    }
    return false;
}
} // namespace

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
                return VisitResult::CONTINUE;
            }
            // 2. constructor call where the `this` argument type does not implement `Copyable` and
            // has mode `local!` or `local?` (covers both `Apply` and its `TryApply` form)
            if (auto apply = DynamicCast<ApplyBase*>(&expr)) {
                if (auto func = DynamicCast<Function*>(apply->GetCallee());
                    func && func->IsConstructor() && func->GetParentCustomTypeDef() != nullptr) {
                    auto thisType = func->GetParam(0)->GetType()->StripAllRefs();
                    if (thisType->IsLocalRegion()) {
                        auto body = expr.GetFuncOrLambdaBody();
                        CJC_NULLPTR_CHECK(body);
                        scopeNeedsRegion.emplace(body);
                        return VisitResult::CONTINUE;
                    }
                }
            }
            // 3. func call whose callee declaration (follow genericDecl when present) has a Parameter that
            // is `local!`, or `local?` with a default value, or whose return type IsLocalRegion — even if
            // the call-site instantiation is Copyable (e.g. T @local? instantiated to Int64)
            if (CalleeParamOrReturnTypeHasNonCopyable(expr)) {
                auto body = expr.GetFuncOrLambdaBody();
                CJC_NULLPTR_CHECK(body);
                scopeNeedsRegion.emplace(body);
                return VisitResult::CONTINUE;
            }
        } else if (auto lambda = DynamicCast<Lambda*>(&expr)) {
            // 4. lambda where the captured variables are not `Copyable` and have mode `local!` or `local?`
            for (auto var : lambda->GetCapturedVariables()) {
                if (var->GetType()->StripAllRefs()->IsLocalRegion()) {
                    auto varBody = var->GetFuncOrLambdaBody();
                    CJC_NULLPTR_CHECK(varBody);
                    for (auto blockGroup : GetAllBlockGroups(*lambda, *varBody)) {
                        scopeNeedsRegion.emplace(blockGroup);
                    }
                }
            }
            // 5. lambda where the function type is `local!` or `local?`.
            // Although a lambda's type cannot be `local!`, still use IsLocalRegion here.
            if (lambda->GetFuncType()->IsLocalRegion()) {
                auto body = expr.GetResult()->GetFuncOrLambdaBody();
                CJC_NULLPTR_CHECK(body);
                scopeNeedsRegion.emplace(body);
            }
        } else if (Is<AllocateBase>(expr) || Is<Tuple>(expr)) {
            // 6. allocate (Allocate or Tuple expression) a non-trivial enum whose type has mode `local!` or `local?`
            auto resultType = expr.GetResult()->GetType()->StripAllRefs();
            if (resultType->IsLocalRegion() && resultType->IsEnum() &&
                !StaticCast<EnumType*>(resultType)->GetEnumDef()->IsAllCtorsTrivial() &&
                !AllReturnValuesInExclave(*expr.GetResult())) {
                auto body = expr.GetFuncOrLambdaBody();
                CJC_NULLPTR_CHECK(body);
                scopeNeedsRegion.emplace(body);
            }
        } else if (Is<Constant>(expr)) {
            // 7. a literal whose type does not implement `Copyable` and has mode `local!`.
            if (expr.GetResult()->GetType()->StripAllRefs()->IsMustLocalRegion()) {
                auto body = expr.GetFuncOrLambdaBody();
                CJC_NULLPTR_CHECK(body);
                scopeNeedsRegion.emplace(body);
            }
        }
        return VisitResult::CONTINUE;
    });
    if (func.TestAttr(Attribute::EXCLAVE)) {
        scopeNeedsRegion.erase(func.GetBody());
    }
    return scopeNeedsRegion;
}

void SetMemRegion::RunOnFunc(Function& func)
{
    // Wrappers created by WrapVirtualFunc and WrapMutFunc only box/unbox and forward. Do not
    // collect regions for them: a local Apply result (e.g. Array @local?) would otherwise open a
    // region here, and EndRegion would reclaim the reference-backed payload before the Box is
    // returned to the caller. FlattenExclave still runs, because a wrapper copies the raw
    // method's attributes and so may carry EXCLAVE, which this pass must always clear.
    if (func.Get<WrappedRawMethod>() == nullptr) {
        SetRegions(CollectFunctionRegionBlockGroup(func));
    }
    FlattenExclave(func);
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
            // don't visit nested lambdas
            // we will visit lambda's body later because `bgsNeedRegion` contains it
            if (Is<Lambda>(expr)) {
                return VisitResult::SKIP;
            } else if (Is<Exit>(expr) || Is<Exclave>(expr)) {
                auto parentBlock = expr.GetParentBlock();
                auto endRegion = builder.CreateExpression<EndRegion>(builder.GetUnitTy(), parentBlock);
                endRegion->MoveBefore(&expr);
                if (Is<Exclave>(expr)) {
                    return VisitResult::SKIP;
                }
            }
            return VisitResult::CONTINUE;
        });
    }
}

void SetMemRegion::FlattenExclave(Function& func)
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
    Visitor::Visit(*func.GetBody(), preVisit, postVisit);
    func.DisableAttr(Attribute::EXCLAVE);
}
