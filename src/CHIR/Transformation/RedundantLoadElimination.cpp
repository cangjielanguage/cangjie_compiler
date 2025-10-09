// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/RedundantLoadElimination.h"

#include "cangjie/CHIR/Analysis/Engine.h"
#include "cangjie/CHIR/Analysis/ReachingDefinitionAnalysis.h"
#include "cangjie/CHIR/Analysis/Utils.h"

namespace Cangjie::CHIR {

RedundantLoadElimination::RedundantLoadElimination()
{
}

void RedundantLoadElimination::RunOnPackage(const Ptr<const Package>& package, bool isDebug) const
{
    for (auto func : package->GetGlobalFuncs()) {
        RunOnFunc(func, isDebug);
    }
}

static void ModifyApplyCalleeInfo(const LocalVar& loadResult, Value& storeValue)
{
    if (!storeValue.IsFunc()) {
        return;
    }
    auto funcBase = VirtualCast<FuncBase*>(&storeValue);
    if (!funcBase->IsMemberFunc() || !funcBase->TestAttr(Attribute::STATIC)) {
        return;
    }
    // If the direct value func call comes from a load,
    //   it is invalid if func is generic(must from a GetInstantiateValue), so this type is its parent directly.
    Type* thisType = funcBase->GetParentCustomTypeOrExtendedType();
    std::vector<Expression*> oldUsers = loadResult.GetUsers();
    for (auto user : oldUsers) {
        if (user->GetExprKind() == ExprKind::APPLY) {
            auto apply = StaticCast<Apply*>(user);
            apply->SetThisType(thisType);
        }
        if (user->GetExprKind() == ExprKind::APPLY_WITH_EXCEPTION) {
            auto apply = StaticCast<ApplyWithException*>(user);
            apply->SetThisType(thisType);
        }
    }
}

void RedundantLoadElimination::RunOnFunc(const Ptr<const Func>& func, bool isDebug) const
{
    if (func->TestAttr(Attribute::SKIP_ANALYSIS)) {
        return;
    }
    auto analysis = std::make_unique<ReachingDefinitionAnalysis>(func);
    auto engine = Engine<ReachingDefinitionDomain>(func, std::move(analysis));
    auto result = engine.IterateToFixpoint();
    CJC_NULLPTR_CHECK(result);

    const auto actionBeforeVisitExpr = [](const ReachingDefinitionDomain&, Expression*, size_t) {};

    std::unordered_map<Block*, std::vector<size_t>> toBeRemoved;
    const auto actionAfterVisitExpr = [func, isDebug, &toBeRemoved](
                                          const ReachingDefinitionDomain& state, Expression* expr, size_t index) {
        CJC_NULLPTR_CHECK(expr);
        if (expr->GetExprKind() != ExprKind::LOAD) {
            return;
        }
        auto load = StaticCast<Load*>(expr);
        if (auto def = state.CheckReachingDef(load->GetLocation()); def) {
            /*
             * adjust value call if load's result is a call
             * %1: () -> Int64& = Allocate(() -> Int64)
             * %2: Unit = Store(@StaticFunc, %1)  // static func in custom type
             * %3: () -> Int64 = Load(%1)
             * %8: Int64 = Apply(%3 : () -> Int64)  // if %3 is opt by RLE, this apply need adjustment
             */
            ModifyApplyCalleeInfo(*load->GetResult(), *def->GetValue());

            // replace load with its exact value
            load->GetResult()->ReplaceWith(*def->GetValue(), func->GetBody());
            toBeRemoved[load->GetParentBlock()].emplace_back(index);
            if (isDebug) {
                std::string message = "[RLE] The usages of the result of Load" +
                    ToPosInfo(load->GetDebugLocation()) + " have been replaced by the value" +
                    ToPosInfo(def->GetDebugLocation()) + "\n";
                std::cout << message;
            }
            return;
        }

        if (auto validLoad = state.CheckReachingLoadDef(load->GetLocation()); validLoad) {
            if (validLoad == load) {
                return;
            }
            load->GetResult()->ReplaceWith(*validLoad->GetResult(), func->GetBody());
            toBeRemoved[load->GetParentBlock()].emplace_back(index);
            if (isDebug) {
                std::string message = "[RLE] The usages of the result of Load" +
                    ToPosInfo(load->GetDebugLocation()) + " have been replaced by the value" +
                    ToPosInfo(validLoad->GetDebugLocation()) + "\n";
                std::cout << message;
            }
        }
    };

    const auto actionOnTerminator = [](const ReachingDefinitionDomain&, Terminator*, std::optional<Block*>) {};

    result->VisitWith(actionBeforeVisitExpr, actionAfterVisitExpr, actionOnTerminator);

    for (auto& [bb, indexes] : toBeRemoved) {
        std::vector<Expression*> exprs;
        for (auto i : indexes) {
            exprs.emplace_back(bb->GetExpressionByIdx(i));
        }
        for (auto e : exprs) {
            e->RemoveSelfFromBlock();
        }
    }
}
}
