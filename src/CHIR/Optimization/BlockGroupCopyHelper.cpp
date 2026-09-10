// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file instantiate functions for CHIR pass Function Inline and Devirtualization
 */

#include "cangjie/CHIR/Optimization/BlockGroupCopyHelper.h"

#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/CHIR/Utils/Visitor/Visitor.h"
#include "cangjie/CHIR/IR/Type/ExtendDef.h"
#include "cangjie/CHIR/IR/Type/PrivateTypeConverter.h"

namespace Cangjie::CHIR {
std::pair<std::vector<Block*>, LocalVar*> BlockGroupCopyHelper::CloneBlockGroup(
    const BlockGroup& oldBG, BlockGroup& newBG)
{
    std::vector<Block*> newBlocks;
    for (auto block : oldBG.GetBlocks()) {
        newBlocks.emplace_back(block->Clone(builder, newBG));
    }
    InstBlockGroup(newBlocks);
    // including local var and param
    std::unordered_map<Value*, Value*> valueMap;
    std::unordered_set<Expression*> newDebugs;
    CollectValueMap(oldBG, newBlocks, valueMap, newDebugs);
    ReplaceExprOperands(newBlocks, valueMap);
    for (auto dbg : newDebugs) {
        dbg->RemoveSelfFromBlock();
    }

    LocalVar* newBlockGroupRetValue = nullptr;
    LocalVar* oldFuncRetValue = nullptr;
    if (auto func = oldBG.GetOwnerFunc()) {
        oldFuncRetValue = func->GetReturnValue();
    } else if (auto lambda = DynamicCast<Lambda*>(oldBG.GetOwnerExpression())) {
        oldFuncRetValue = lambda->GetReturnValue();
    }
    // some functions don't have return value, such as `init`
    if (oldFuncRetValue != nullptr) {
        CJC_ASSERT(oldFuncRetValue->IsLocalVar());
        auto it = valueMap.find(oldFuncRetValue);
        CJC_ASSERT(it != valueMap.end());
        newBlockGroupRetValue = StaticCast<LocalVar*>(it->second);
    }
    return {newBlocks, newBlockGroupRetValue};
}

void BlockGroupCopyHelper::CollectValueMap(const Lambda& oldLambda, const Lambda& newLambda,
    std::unordered_map<Value*, Value*>& valueMap, std::unordered_set<Expression*>& newDebugs)
{
    auto oldParams = oldLambda.GetParams();
    auto newParams = newLambda.GetParams();
    CJC_ASSERT(oldParams.size() == newParams.size());
    for (size_t i = 0; i < oldParams.size(); ++i) {
        valueMap.emplace(oldParams[i], newParams[i]);
    }
    auto newBlocks = newLambda.GetBody()->GetBlocks();
    CollectValueMap(*oldLambda.GetBody(), newBlocks, valueMap, newDebugs);
}

void BlockGroupCopyHelper::CollectValueMap(Block& oldBlk, Block& newBlk,
    std::unordered_map<Value*, Value*>& valueMap, std::unordered_set<Expression*>& newDebugs)
{
    auto oldExprs = oldBlk.GetExpressions();
    auto newExprs = newBlk.GetExpressions();
    valueMap.emplace(&oldBlk, &newBlk);
    CJC_ASSERT(oldExprs.size() == newExprs.size());
    for (size_t i = 0; i < oldExprs.size(); ++i) {
        auto oldExpr = oldExprs[i];
        auto newExpr = newExprs[i];
        CJC_ASSERT(oldExpr->GetExprKind() == newExpr->GetExprKind());
        if (oldExpr->GetResult() != nullptr) {
            CJC_ASSERT(newExpr->GetResult() != nullptr);
            valueMap.emplace(oldExpr->GetResult(), newExpr->GetResult());
        }
        if (oldExpr->GetExprKind() == ExprKind::DEBUGEXPR) {
            newDebugs.emplace(newExpr);
        } else if (oldExpr->GetExprKind() == ExprKind::LAMBDA) {
            CollectValueMap(*StaticCast<Lambda*>(oldExpr), *StaticCast<Lambda*>(newExpr), valueMap, newDebugs);
        } else if (oldExpr->GetExprKind() == ExprKind::EXCLAVE) {
            auto newBlocks = StaticCast<Exclave*>(newExpr)->GetBody()->GetBlocks();
            CollectValueMap(*StaticCast<Exclave*>(oldExpr)->GetBody(), newBlocks, valueMap, newDebugs);
        }
    }
}

void BlockGroupCopyHelper::CollectValueMap(const BlockGroup& oldBG, std::vector<Block*>& newBlocks,
    std::unordered_map<Value*, Value*>& valueMap, std::unordered_set<Expression*>& newDebugs)
{
    auto oldBlocks = oldBG.GetBlocks();
    CJC_ASSERT(oldBlocks.size() == newBlocks.size());
    for (size_t i = 0; i < oldBlocks.size(); ++i) {
        CollectValueMap(*oldBlocks[i], *newBlocks[i], valueMap, newDebugs);
    }
}

void BlockGroupCopyHelper::ReplaceExprOperands(
    std::vector<Block*>& blocks, const std::unordered_map<Value*, Value*>& valueMap)
{
    auto preVisit = [&valueMap](Expression& e) {
        // note: a hack here, remove later
        if (e.GetExprKind() == ExprKind::DEBUGEXPR) {
            return VisitResult::CONTINUE;
        }
        auto operands = e.GetOperands();
        for (size_t i = 0; i < operands.size(); ++i) {
            auto op = operands[i];
            auto it = valueMap.find(op);
            if (it == valueMap.end()) {
                continue;
            }
            e.ReplaceOperand(i, it->second);
        }
        return VisitResult::CONTINUE;
    };
    for (auto block : blocks) {
        Visitor::Visit(*block, preVisit);
    }
}

void BlockGroupCopyHelper::GetInstMapFromApply(const Apply& apply, const Function* newBodyOuterFunction)
{
    if (apply.GetCallee()->IsLocalVar()) {
        auto lambda = DynamicCast<Lambda*>(StaticCast<LocalVar*>(apply.GetCallee())->GetExpr());
        CJC_NULLPTR_CHECK(lambda);
        // get inst map from function
        size_t index = 0;
        for (auto& genericType : lambda->GetGenericTypeParams()) {
            instMap.emplace(genericType, apply.GetInstantiatedTypeArgs()[index]);
            ++index;
        }
        thisType = builder.GetType<ThisType>();
    } else {
        auto func = StaticCast<Function*>(apply.GetCallee());
        auto customDef = func->GetParentCustomTypeDef();
        if (customDef && customDef->IsGenericDef()) {
            // 1. get customType where function in.
            auto instParentCustomType = apply.GetInstParentCustomTyOfCallee(builder);
            if (instParentCustomType == nullptr) {
                instParentCustomType =
                    customDef->IsExtend() ? StaticCast<ExtendDef*>(customDef)->GetExtendedType() : customDef->GetType();
            }
            instParentCustomType = instParentCustomType->StripAllRefs();
            // 2. get inst map from custom type
            if (auto exDef = DynamicCast<const ExtendDef*>(customDef)) {
                auto newMap = exDef->GetExtendedType()->CalculateGenericTyMapping(*instParentCustomType);
                CJC_ASSERT(newMap.first);
                instMap.merge(newMap.second);
            } else {
                instMap = GetInstMapFromCurDefToCurType(StaticCast<CustomType&>(*instParentCustomType));
            }
        }
        // 3. get inst map from function
        size_t index = 0;
        for (auto& genericType : func->GetGenericTypeParams()) {
            instMap.emplace(genericType, apply.GetInstantiatedTypeArgs()[index]);
            ++index;
        }
        // 4. set this type if needed
        auto outerDef = newBodyOuterFunction ? newBodyOuterFunction->GetParentCustomTypeDef()
            : apply.GetTopLevelFunc()->GetParentCustomTypeDef();
        if (customDef && outerDef && outerDef == customDef) {
            // keep this type if same custom type between callee and caller
            thisType = builder.GetType<ThisType>();
        } else {
            thisType = apply.GetThisType();
        }
    }
}

void BlockGroupCopyHelper::InstBlockGroup(std::vector<Block*>& blocks)
{
    GenericTypeConvertor gConverter(instMap, builder);
    ConvertTypeFunc convertFunc = [&gConverter, this](Type& type) {
        auto res = ReplaceThisTypeToConcreteType(type, *thisType, builder);
        if (res != &type) {
            return res;
        } else {
            return gConverter.ConvertToInstantiatedType(type);
        }
    };
    PrivateTypeConverterNoInvokeOriginal converter(convertFunc, builder);
    auto preVisit = [&converter](Expression& e) {
        converter.VisitExpr(e);
        return VisitResult::CONTINUE;
    };
    for (auto block : blocks) {
        Visitor::Visit(*block, preVisit);
    }
}

void FixCastProblemAfterInst(std::vector<Block*>& blocks, CHIRBuilder& builder)
{
    auto preVisit = [&builder](Expression& e) {
        if (e.GetExprKind() == ExprKind::INSTANCEOF) {
            // fix instanceOf problem
            auto instance = StaticCast<InstanceOf*>(&e);
            auto objType = instance->GetObject()->GetType();
            if (objType->IsGenericRelated() ||
                (objType->IsRef() && StaticCast<RefType*>(objType)->GetBaseType()->IsClass()) ||
                instance->GetType()->IsGenericRelated()) {
                return VisitResult::CONTINUE;
            }
            if (objType->IsEqualOrSubTypeOf(*instance->GetType(), builder)) {
                auto trueExpr =
                    builder.CreateConstantExpression<BoolLiteral>(builder.GetBoolTy(), e.GetParentBlock(), true);
                e.ReplaceWith(*trueExpr);
            } else {
                auto falseExpr =
                    builder.CreateConstantExpression<BoolLiteral>(builder.GetBoolTy(), e.GetParentBlock(), false);
                e.ReplaceWith(*falseExpr);
            }
        } else if (e.GetExprKind() == ExprKind::CAST_TO_CONCRETE) {
            // change transformToConcrete to box/unbox/typecast
            auto& cast = StaticCast<CastToConcrete&>(e);
            if (!cast.GetSourceType()->IsGenericRelated()) {
                auto newCast = TypeCastOrBoxIfNeeded(
                    *cast.GetSourceValue(), *cast.GetTargetType(), builder, *e.GetParentBlock(), e.GetDebugLocation());
                if (newCast == cast.GetSourceValue()) {
                    for (auto user : e.GetResult()->GetUsers()) {
                        user->ReplaceOperand(e.GetResult(), cast.GetSourceValue());
                    }
                    e.RemoveSelfFromBlock();
                    return VisitResult::CONTINUE;
                }
                for (auto user : e.GetResult()->GetUsers()) {
                    user->ReplaceOperand(e.GetResult(), newCast);
                }
                StaticCast<LocalVar*>(newCast)->GetExpr()->MoveBefore(&e);
                e.RemoveSelfFromBlock();
            }
        } else if (e.GetExprKind() == ExprKind::CAST_TO_GENERIC) {
            // change CastToGeneric to box/unbox/typecast
            auto cast = StaticCast<CastToGeneric*>(&e);
            if (!cast->GetTargetType()->IsGenericRelated()) {
                auto newCast =
                    TypeCastOrBoxIfNeeded(*cast->GetSourceValue(), *cast->GetTargetType(), builder, *e.GetParentBlock(),
                        e.GetDebugLocation());
                if (newCast == cast->GetSourceValue()) {
                    for (auto user : e.GetResult()->GetUsers()) {
                        user->ReplaceOperand(e.GetResult(), cast->GetSourceValue());
                    }
                    e.RemoveSelfFromBlock();
                    return VisitResult::CONTINUE;
                }
                for (auto user : e.GetResult()->GetUsers()) {
                    user->ReplaceOperand(e.GetResult(), newCast);
                }
                StaticCast<LocalVar*>(newCast)->GetExpr()->MoveBefore(&e);
                e.RemoveSelfFromBlock();
            }
        } else if (e.GetExprKind() == ExprKind::CLASS_STATIC_CAST) {
            /* change typecast to unbox/box
             * func foo<T>(a: T) {
             *   let b: CA<T> = TypeCast(a, CA<T>)   // typecast to CA<T>
             * }
             *
             * when foo inline to other function and inst to int64 type, the typecast would change to:
             *   (a: Int64)
             *   let b: CA<Int64> = Box(a, CA<Int64>) */
            auto cast = StaticCast<ClassStaticCast*>(&e);
            if (cast->GetSourceValue()->GetType()->IsGenericRelated()) {
                return VisitResult::CONTINUE;
            }
            auto newCastRes =
                TypeCastOrBoxIfNeeded(*cast->GetSourceValue(), *e.GetResult()->GetType(), builder, *e.GetParentBlock(),
                    e.GetDebugLocation());
            if (newCastRes == cast->GetSourceValue()) {
                return VisitResult::CONTINUE;
            }
            auto newCast = StaticCast<LocalVar*>(newCastRes);
            if (newCast->GetExpr()->GetExprKind() == ExprKind::CLASS_STATIC_CAST) {
                newCast->GetExpr()->RemoveSelfFromBlock();
                return VisitResult::CONTINUE;
            }
            for (auto user : e.GetResult()->GetUsers()) {
                user->ReplaceOperand(e.GetResult(), newCast);
            }
            newCast->GetExpr()->MoveBefore(&e);
            e.RemoveSelfFromBlock();
        }
        return VisitResult::CONTINUE;
    };
    for (auto block : blocks) {
        Visitor::Visit(*block, preVisit);
    }
}
}  // namespace Cangjie::CHIR
