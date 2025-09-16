// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/ArrayListConstStartOpt.h"

#include "cangjie/CHIR/Analysis/Analysis.h"
#include "cangjie/CHIR/Analysis/ConstAnalysis.h"
#include "cangjie/CHIR/Utils.h"

namespace Cangjie::CHIR {
static const std::vector<FuncInfo> ARRAY_FUNC_INLINE_WHITE_LIST = {
    FuncInfo("getUnchecked", "Array", {NOT_CARE}, ANY_TYPE, "std.core"),
    FuncInfo("setUnchecked", "Array", {NOT_CARE}, ANY_TYPE, "std.core"),
};

static const std::vector<FuncInfo> ARRAYLIST_FUNC_LIST = {
    FuncInfo("[]", "ArrayList", {NOT_CARE}, ANY_TYPE, "std.collection"),
    FuncInfo("==", "ArrayList", {NOT_CARE}, ANY_TYPE, "std.collection"),
};

static const std::vector<FuncInfo> ARRAYLIST_ITERATOR_FUNC_LIST = {
    FuncInfo("next", "ArrayListIterator", {NOT_CARE}, ANY_TYPE, "std.collection"),
};

namespace {
bool InWhiteList(const Func& func, const std::vector<FuncInfo>& whiteList)
{
    for (auto element : whiteList) {
        if (IsExpectedFunction(func, element)) {
            return true;
        }
    }
    return false;
}
} // namespace

const OptEffectCHIRMap& ArrayListConstStartOpt::GetEffectMap() const
{
    return effectMap;
}

bool ArrayListConstStartOpt::CheckNeedRewrite(const Apply& apply) const
{
    if (!apply.GetCallee()->IsFuncWithBody()) {
        return false;
    }
    auto callee = DynamicCast<Func*>(apply.GetCallee());
    CJC_NULLPTR_CHECK(callee);
    return InWhiteList(*callee, ARRAY_FUNC_INLINE_WHITE_LIST);
}

bool ArrayListConstStartOpt::IsStartAddIndexExpression(const Field& field, bool isIteratorFunc) const
{
    // After inline, CHIR will be like:
    // %0: Class-_CNac9ArrayListIlE<Int64>&
    // ...
    // %39: Struct-_CNat5ArrayIlE<Int64>& =GetElementRef(%0, 0)
    // %40: Struct-_CNat5ArrayIlE<Int64> = Load(%39)
    // ...
    // %58: Int64 = Field(40%, 1)
    // %59: Int64 = Add(%58, %1) //%58 is start and %1 is index

    // check the Field expression is or not the expected:
    // 1.returnTy is TYPE_INT64
    auto resultTy = field.GetResult()->GetType()->GetTypeKind();
    if (resultTy != Type::TypeKind::TYPE_INT64) {
        return false;
    }

    // 2.the first index must be 1 cause the index of start in struct Array is 1
    if (field.GetPath().at(0) != 1) {
        return false;
    }

    // 3.check the Field expr operand is the array member of class arrayList
    // e.g. get %40 experssion and check ExprKind
    auto expr = StaticCast<LocalVar*>(field.GetBase())->GetExpr();
    if (expr->GetExprKind() != ExprKind::LOAD) {
        return false;
    }

    // e.g. get %39 experssion and check ExprKind
    expr = StaticCast<LocalVar*>(expr->GetOperand(0))->GetExpr();
    if (expr->GetExprKind() != ExprKind::GET_ELEMENT_REF) {
        return false;
    }
    auto getElementRef = StaticCast<GetElementRef*>(expr);
    // the index of member Array myData of Class ArrayList is 0
    if (getElementRef->GetPath().at(0) != 0) {
        return false;
    }

    auto location = getElementRef->GetLocation();
    if (!isIteratorFunc && location->IsParameter()) {
        auto param = StaticCast<Parameter*>(location);
        // the index 0 or 1 parameter of ArrayList Func is class ArrayList
        if (param == field.GetTopLevelFunc()->GetParam(0) ||
            param == field.GetTopLevelFunc()->GetParam(1)) {
            return true;
            }
    }

    // ArrayListIterator func need check more expression:
    // After inline,ArrayListIterator func CHIR will be like:
    // %0: Class-_CNac17ArrayListIteratorIlE<Int64>&
    // ...
    // %27: Class-_CNac9ArrayListIlE<Int64>&& = GetElementRef(%0, 1)
    // %28: Class-_CNac9ArrayListIlE<Int64>& = Load(%27)
    // %29: Struct-_CNat5ArrayIlE<Int64>& = GetElementRef(%28, 0)
    // %30: Struct-_CNat5Array0IlE<Int64> = Load(%29)
    // ...
    // %58: Int64 = Field(30%, 1)
    // %59: Int64 = Add(%58, %1) //%58 is start and %1 is index
    if (isIteratorFunc) {
        expr = StaticCast<LocalVar*>(location)->GetExpr();
        if (expr->GetExprKind() != ExprKind::LOAD) {
            return false;
        }
        expr = StaticCast<LocalVar*>(expr->GetOperand(0))->GetExpr();
        if (expr->GetExprKind() != ExprKind::GET_ELEMENT_REF) {
            return false;
        }
        getElementRef = StaticCast<GetElementRef*>(expr);
        // the index of member ArrayList myData of Class ArrayListIterator is 1
        if (getElementRef->GetPath().at(0) != 1) {
            return false;
        }

        location = getElementRef->GetLocation();
        if (!location->IsParameter()) {
            return false;
        }
        // the index 0 or 1 parameter of ArrayListIterator Func is class ArrayListIterator
        return StaticCast<Parameter*>(location) == field.GetTopLevelFunc()->GetParam(0) ||
            StaticCast<Parameter*>(location) == field.GetTopLevelFunc()->GetParam(1);
    }
    return false;
}

void ArrayListConstStartOpt::RewriteStartWithConstZero(Expression& oldExpr) const
{
    auto oldExprResult = oldExpr.GetResult();
    auto oldExprParent = oldExpr.GetParentBlock();
    Ptr<LiteralValue> literalValueZero = builder.CreateLiteralValue<IntLiteral>(builder.GetInt64Ty(), 0UL);
    auto newExpr = builder.CreateExpression<Constant>(oldExprResult->GetType(), literalValueZero, oldExprParent);
    newExpr->SetDebugLocation(oldExpr.GetDebugLocation());

    oldExpr.ReplaceWith(*newExpr);
    oldExprResult->ReplaceWith(*newExpr->GetResult(), newExpr->GetParentBlockGroup());

    if (opts.chirDebugOptimizer) {
        std::string message = "[ArrayListConstStartOpt] The " +
            ExprKindMgr::Instance()->GetKindName(static_cast<size_t>(oldExpr.GetExprKind())) +
            ToPosInfo(oldExpr.GetDebugLocation()) + " has been rewrited to a constant\n";
        std::cout << message;
    }
}

void ArrayListConstStartOpt::RunOnPackage(const Ptr<const Package>& package)
{
    for (auto func : package->GetGlobalFuncs()) {
        bool isArrayListIteratorFunc = InWhiteList(*func, ARRAYLIST_ITERATOR_FUNC_LIST);
        // only inline array func into arrayList and arrayListIterator func
        if (!isArrayListIteratorFunc && !InWhiteList(*func, ARRAYLIST_FUNC_LIST)) {
            continue;
        }

        auto postVisit = [this](Expression& e) {
            if (e.GetExprKind() != ExprKind::APPLY) {
                return VisitResult::CONTINUE;
            }

            auto& apply = StaticCast<Apply&>(e);
            if (CheckNeedRewrite(apply)) {
                pass.DoFunctionInline(apply, optPassName);
                MergeEffectMap(pass.GetEffectMap(), effectMap);
            }

            return VisitResult::CONTINUE;
        };
        Visitor::Visit(
            *func, [](Expression&) { return VisitResult::CONTINUE; }, postVisit);

        // remove start opt
        auto removeStartVisit = [this, isArrayListIteratorFunc](Expression& e) {
            if (e.GetExprKind() != ExprKind::FIELD) {
                return VisitResult::CONTINUE;
            }

            auto& field = StaticCast<Field&>(e);
            // find the Field expression related to the member start of myData in ArrayList
            if (IsStartAddIndexExpression(field, isArrayListIteratorFunc)) {
                // change this Field expression to Constant(0)
                RewriteStartWithConstZero(e);
            }

            return VisitResult::CONTINUE;
        };
        Visitor::Visit(
            *func, [](Expression&) { return VisitResult::CONTINUE; }, removeStartVisit);
    }
}
}  // namespace Cangjie::CHIR2
