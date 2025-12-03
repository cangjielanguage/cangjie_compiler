// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Checker/TypeCastCheck.h"

#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/Expression/Expression.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/Utils/TaskQueue.h"

namespace Cangjie::CHIR::NativeFFI {
TypeCastCheck::TypeCastCheck(DiagAdapter& diag) : diag(diag)
{
}

void TypeCastCheck::RunOnPackage(const Package& package, size_t threadNum)
{
    std::vector<Func*> funcs;
    for (auto func : package.GetGlobalFuncs()) {
        if (!func->TestAttr(Attribute::UNSAFE)) {
            funcs.emplace_back(func);
        }
    }

    if (funcs.empty()) {
        return;
    }
    if (threadNum == 1) {
        for (auto func : funcs) {
            RunOnFunc(*func);
        }
    } else {
        Utils::TaskQueue taskQueue(threadNum);
        for (auto func : funcs) {
            taskQueue.AddTask<void>([this, func]() { return RunOnFunc(*func); });
        }
        taskQueue.RunAndWaitForAllTasksCompleted();
    }
}

namespace {
bool IsNativeFIIType(Type& type)
{
    auto valueType = type.StripAllRefs();
    if (!valueType->IsClass()) {
        return false;
    }

    auto classType = StaticCast<ClassType>(valueType);
    auto classDef = classType->GetClassDef();
    return classDef->TestAttr(Attribute::JAVA_IMPL) || classDef->TestAttr(Attribute::JAVA_MIRROR);
}

std::string GetClassName(Type& type)
{
    auto valueType = type.StripAllRefs();
    CJC_ASSERT(valueType->IsClass());
    return StaticCast<ClassType>(valueType)->GetClassDef()->GetSrcCodeIdentifier();
}

std::optional<std::tuple<Type*, Type*>> TryExtractTypeCast(Expression& expr)
{
    if (expr.GetExprKind() == ExprKind::TYPECAST) {
        auto& typeCastExpr = StaticCast<TypeCast>(expr);
        return {{typeCastExpr.GetSourceTy(), typeCastExpr.GetTargetTy()}};
    }

    if (expr.GetExprKind() == ExprKind::TYPECAST_WITH_EXCEPTION) {
        auto& typeCastExpr = StaticCast<TypeCastWithException>(expr);
        return {{typeCastExpr.GetSourceTy(), typeCastExpr.GetTargetTy()}};
    }

    return std::nullopt;
}
} // namespace

void TypeCastCheck::RunOnFunc(const Func& func)
{
    std::function<VisitResult(Expression&)> visitor = [this, &func, &visitor](Expression& expr) {
        if (expr.IsLambda()) {
            Visitor::Visit(*StaticCast<Lambda>(expr).GetBody(), visitor);
            return VisitResult::CONTINUE;
        }

        auto typeCast = TryExtractTypeCast(expr);
        if (!typeCast) {
            return VisitResult::CONTINUE;
        }

        auto [sourceTy, targetTy] = *typeCast;
        if (!IsNativeFIIType(*sourceTy)) {
            return VisitResult::CONTINUE;
        }

        if (!IsNativeFIIType(*targetTy)) {
            auto [hasDebugPos, debugPos] = GetDebugPos(expr);
            if (!hasDebugPos) {
                debugPos = ToRange(func.GetDebugLocation());
            }

            CJC_ASSERT(sourceTy->GetTypeKind());
            auto builder = diag.DiagnoseRefactor(DiagKindRefactor::chir_native_ffi_java_illegal_type_cast, debugPos,
                GetClassName(*sourceTy), GetClassName(*targetTy));
        }

        return VisitResult::CONTINUE;
    };
    Visitor::Visit(func, visitor);
}
} // namespace Cangjie::CHIR::NativeFFI
