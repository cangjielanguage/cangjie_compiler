// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Analysis/Utils.h"

#include <mutex>
#include <queue>

#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Utils.h"

namespace Cangjie::CHIR {
namespace {
static std::unordered_map<ClassType*, std::vector<ClassType*>> g_superClassMap = {};
static std::mutex g_mtx;
std::vector<ClassType*> GetAllFathers(ClassType* clsTy, CHIRBuilder* builder)
{
    std::unique_lock<std::mutex> lock(g_mtx);
    if (g_superClassMap.count(clsTy) != 0) {
        return g_superClassMap[clsTy];
    }
    std::queue<ClassType*> tys;
    std::vector<ClassType*> fathers;
    tys.push(clsTy);
    fathers.push_back(clsTy);

    while (!tys.empty()) {
        auto ty = tys.front();
        tys.pop();

        auto supers = ty->GetImplementedInterfaceTys(builder);
        std::for_each(supers.begin(), supers.end(), [&](auto super) {
            if (super) {
                tys.push(super);
                fathers.push_back(super);
            }
        });

        auto super = ty->GetSuperClassTy(builder);
        if (super) {
            tys.push(super);
            fathers.push_back(super);
        }
    }
    g_superClassMap.emplace(clsTy, fathers);
    return fathers;
}
} // namespace

bool IsUnsignedArithmetic(const BinaryExpression& expr)
{
    auto ty = expr.GetLHSOperand()->GetType();
    return ty->IsUnsignedInteger() || IsStructEnum(ty);
}

std::string GetRefName(size_t index)
{
    return "Ref" + std::to_string(index);
}

std::string GetObjName(size_t index)
{
    return "Obj" + std::to_string(index);
}

std::string GetObjChildName(std::string parentName, size_t fieldIdx)
{
    return parentName + "." + std::to_string(fieldIdx);
}

Cangjie::Position ToPosition(const DebugLocation& loc)
{
    return Cangjie::Position(loc.GetFileID(),
        static_cast<int>(loc.GetBeginPos().line), static_cast<int>(loc.GetBeginPos().column));
}

std::string ToPosInfo(const DebugLocation& loc, bool isPrintFileName)
{
    auto [line, column] = loc.GetBeginPos();
    auto fileName = isPrintFileName ? Cangjie::FileUtil::GetFileName(loc.GetAbsPath()) + "," : "";
    return " at [" + fileName + std::to_string(line) + "," + std::to_string(column) + "]";
}

std::optional<size_t> IsInitialisingMemberVar(const Func& func, const StoreElementRef& store)
{
    auto location = store.GetLocation();
    if (func.IsConstructor() && location->IsParameter()) {
        auto& paths = store.GetPath();
        // func->GetParam(0) is the `this` arugment.
        if (location == func.GetParam(0) && paths.size() == 1) {
            return paths[0];
        }
    }
    return std::nullopt;
}

const Lambda* IsApplyToLambda(const Expression* expr)
{
    CJC_NULLPTR_CHECK(expr);
    if (expr->GetExprKind() != ExprKind::APPLY && expr->GetExprKind() != ExprKind::APPLY_WITH_EXCEPTION) {
        return nullptr;
    }
    CJC_ASSERT(expr->GetNumOfOperands() > 0);
    if (!expr->GetOperand(0)->IsLocalVar()) {
        return nullptr;
    }
    auto callee = StaticCast<const LocalVar*>(expr->GetOperand(0))->GetExpr();
    if (callee->GetExprKind() != ExprKind::LAMBDA) {
        return nullptr;
    }
    return StaticCast<const Lambda*>(callee);
}

// Check if it is a getOrThrow Function
bool IsGetOrThrowFunction(const Expression& expr)
{
    static const FuncInfo GETORTHROWFUNCINFO{"getOrThrow", NOT_CARE, {NOT_CARE}, NOT_CARE, "std.core"};

    if (expr.GetExprKind() != ExprKind::APPLY) {
        return false;
    }
    auto apply = StaticCast<const Apply*>(&expr);
    auto callee = apply->GetCallee();
    if (!callee->IsFunc()) {
        return false;
    }
    return IsExpectedFunction(*VirtualCast<FuncBase*>(callee), GETORTHROWFUNCINFO);
}

ClassType* LeastCommonSuperClass(ClassType* ty1, ClassType* ty2, CHIRBuilder* builder)
{
    auto allFathers1 = GetAllFathers(ty1, builder);
    auto allFathers2 = GetAllFathers(ty2, builder);

    // In order to speed up efficiency of searching.
    std::unordered_set<ClassType*> allFathersSet;
    allFathersSet.insert(allFathers1.begin(), allFathers1.end());

    for (auto& ty : allFathers2) {
        if (allFathersSet.find(ty) != allFathersSet.end()) {
            return ty;
        }
    }
    return nullptr;
}

bool IsStructEnum(const Ptr<Type>& type)
{
    if (type->IsEnum()) {
        return !static_cast<EnumType*>(type.get())->GetEnumDef()->GetCtors().empty();
    }
    return false;
}

bool IsRefEnum(const Ptr<Type>& type)
{
    if (type->IsEnum()) {
        return static_cast<EnumType*>(type.get())->GetEnumDef()->GetCtors().empty();
    }
    return false;
}

std::unordered_set<Value*> GetLambdaCapturedVarsRecursively(const Lambda& lambda)
{
    std::unordered_set<Value*> allCapturedVars;
    std::unordered_set<const Lambda*> visited;

    std::function<void(const Lambda&, std::unordered_set<const Lambda*>&)> collectRecursively =
        [&allCapturedVars, &collectRecursively](const Lambda& lambda, std::unordered_set<const Lambda*>& visited) {
        visited.emplace(&lambda);
        auto visitAction = [&visited, &collectRecursively](Expression& expr) {
            if (expr.IsApply()) {
                auto callee = DynamicCast<LocalVar*>(StaticCast<Apply&>(expr).GetCallee());
                if (callee != nullptr && callee->GetExpr()->IsLambda()) {
                    auto child = StaticCast<const Lambda*>(callee->GetExpr());
                    if (visited.find(child) == visited.end()) {
                        collectRecursively(*child, visited);
                    }
                }
            } else if (expr.IsApplyWithException()) {
                auto callee = DynamicCast<LocalVar*>(StaticCast<ApplyWithException&>(expr).GetCallee());
                if (callee != nullptr && callee->GetExpr()->IsLambda()) {
                    auto child = StaticCast<const Lambda*>(callee->GetExpr());
                    if (visited.find(child) == visited.end()) {
                        collectRecursively(*child, visited);
                    }
                }
            } else if (expr.IsLambda()) {
                collectRecursively(StaticCast<const Lambda&>(expr), visited);
            }
            return VisitResult::CONTINUE;
        };
        Visitor::Visit(*lambda.GetBody(), visitAction);

        for (auto var : lambda.GetCapturedVariables()) {
            if (var->GetType()->IsRef()) {
                allCapturedVars.emplace(var);
            }
        }
    };
    collectRecursively(lambda, visited);
    
    return allCapturedVars;
}
}  // namespace Cangjie::CHIR