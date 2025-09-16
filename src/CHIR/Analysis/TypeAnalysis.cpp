// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Analysis/TypeAnalysis.h"
#include "cangjie/CHIR/Utils.h"

#include <queue>

namespace Cangjie::CHIR {

TypeValue::TypeValue(DevirtualTyKind kind, Type* baseLineType) : kind(kind), baseLineType(baseLineType)
{
}

std::optional<std::unique_ptr<TypeValue>> TypeValue::Join(const TypeValue& rhs) const
{
    auto rhsClassTy = StaticCast<const TypeValue&>(rhs).GetSpecificType();
    if (this->baseLineType != rhsClassTy) {
        if (!this->baseLineType->IsClass() || !rhsClassTy->IsClass()) {
            return nullptr;
        }
        auto fatherTy = LeastCommonSuperClass(
            StaticCast<ClassType*>(this->baseLineType), StaticCast<ClassType*>(rhsClassTy), builder);
        if (fatherTy) {
            return std::make_unique<TypeValue>(DevirtualTyKind::SUBTYPE_OF, fatherTy);
        } else {
            return nullptr;
        }
    } else {
        return std::nullopt;
    }
}

std::string TypeValue::ToString() const
{
    return "{ " + GetKindString(kind) + ", " + baseLineType->ToString() + " }";
}

std::unique_ptr<TypeValue> TypeValue::Clone() const
{
    return std::make_unique<TypeValue>(kind, baseLineType);
}

DevirtualTyKind TypeValue::GetTypeKind() const
{
    return kind;
}

Type* TypeValue::GetSpecificType() const
{
    return baseLineType;
}

void TypeValue::SetCHIRBuilder(CHIRBuilder* chirBuilder)
{
    builder = chirBuilder;
}

std::string TypeValue::GetKindString(DevirtualTyKind clsTyKind) const
{
    switch (clsTyKind) {
        case DevirtualTyKind::SUBTYPE_OF:
            return "SUBTYPE_OF";
        case DevirtualTyKind::EXACTLY:
            return "EXACTLY";
        default:
            return "Unknown";
    }
}

CHIRBuilder* TypeValue::builder{nullptr};
template <> const std::string Analysis<TypeDomain>::name = "type-analysis";
template <> const std::optional<unsigned> Analysis<TypeDomain>::blockLimit = std::nullopt;
template <> TypeDomain::ChildrenMap ValueAnalysis<TypeValueDomain>::globalChildrenMap{};
template <> TypeDomain::AllocatedRefMap ValueAnalysis<TypeValueDomain>::globalAllocatedRefMap{};
template <> TypeDomain::AllocatedObjMap ValueAnalysis<TypeValueDomain>::globalAllocatedObjMap{};
template <> std::vector<std::unique_ptr<Ref>> ValueAnalysis<TypeValueDomain>::globalRefPool{};
template <> std::vector<std::unique_ptr<AbstractObject>> ValueAnalysis<TypeValueDomain>::globalAbsObjPool{};
template <>
TypeDomain ValueAnalysis<TypeValueDomain>::globalState{&globalChildrenMap, &globalAllocatedRefMap,
    nullptr, &globalAllocatedObjMap, &globalRefPool, &globalAbsObjPool};

template <> bool IsTrackedGV<ValueDomain<TypeValue>>(const GlobalVar& gv)
{
    Type* baseTy = gv.GetType();
    while (baseTy->IsRef()) {
        auto ty = StaticCast<RefType*>(baseTy)->GetBaseType();
        baseTy = ty;
    }
    return baseTy->IsClass();
}

TypeAnalysis::TypeAnalysis(
    const Func* func, CHIRBuilder& builder, bool isDebug, const std::unordered_map<Func*, Type*>& realRetTyMap)
    : ValueAnalysis(func, builder, isDebug), realRetTyMap(realRetTyMap)
{
}

bool TypeAnalysis::CheckFuncHasInvoke(const BlockGroup& body)
{
    std::vector<Block*> blocks = body.GetBlocks();
    for (auto bb : blocks) {
        auto exprs = bb->GetNonTerminatorExpressions();
        for (size_t i = 0; i < exprs.size(); ++i) {
            if (exprs[i]->GetExprKind() == ExprKind::LAMBDA) {
                if (CheckFuncHasInvoke(*StaticCast<Lambda*>(exprs[i])->GetBody())) {
                    return true;
                }
            }
            if (exprs[i]->GetExprKind() == ExprKind::INVOKE) {
                return true;
            }
        }
    }
    return false;
}

bool TypeAnalysis::Filter(const Func& method)
{
    return CheckFuncHasInvoke(*method.GetBody());
}


void TypeAnalysis::PrintDebugMessage(const Expression* expr, const TypeValue* absVal) const
{
    std::string message = "The value of " + expr->GetResult()->GetIdentifier() + " = " + expr->ToString() +
        " has been set to " + absVal->ToString();
    std::cout << message << std::endl;
}

void TypeAnalysis::HandleNormalExpressionEffect(TypeDomain& state, const Expression* expression)
{
    switch (expression->GetExprKind()) {
        case ExprKind::TYPECAST:
            return HandleTypeCastExpr(state, StaticCast<const TypeCast*>(expression));
        case ExprKind::BOX:
            return HandleBoxExpr(state, StaticCast<const Box*>(expression));
        default: {
            auto res = expression->GetResult();
            auto domain = state.GetAbstractDomain(res);
            if ((!domain || domain->IsTop()) && (res->GetType()->IsPrimitive())) {
                state.Update(res, std::make_unique<TypeValue>(DevirtualTyKind::EXACTLY, res->GetType()));
            } else {
                state.TrySetToTopOrTopRef(res, res->GetType()->IsRef());
            }
        }
    }
    auto resutlType = expression->GetResult()->GetType();
    if (isDebug && !resutlType->IsRef() && !resutlType->IsGeneric()) {
        if (auto absVal = state.CheckAbstractValue(expression->GetResult()); absVal) {
            PrintDebugMessage(expression, absVal);
        }
    }
}

void TypeAnalysis::HandleAllocateExpr(TypeDomain& state, const Allocate* expression, Value* newObj)
{
    if (!newObj) {
        return;
    }

    auto baseTy = expression->GetType();
    if (baseTy->IsClass()) {
        state.Update(newObj, std::make_unique<TypeValue>(DevirtualTyKind::EXACTLY, StaticCast<ClassType*>(baseTy)));
    }
}

std::optional<Block*> TypeAnalysis::HandleTerminatorEffect(TypeDomain& state, const Terminator* terminator)
{
    switch (terminator->GetExprKind()) {
        // already handled by the framework
        // case ExprKind::ALLOCATE_WITH_EXCEPTION:
        // case ExprKind::RAW_ARRAY_ALLOCATE_WITH_EXCEPTION:
        // case ExprKind::RAW_ARRAY_LITERAL_ALLOCATE_WITH_EXCEPTION:
        // case ExprKind::APPLY_WITH_EXCEPTION:
        // case ExprKind::INVOKE_WITH_EXCEPTION:
        case ExprKind::TYPECAST_WITH_EXCEPTION:
            HandleTypeCastExpr(state, StaticCast<const TypeCastWithException*>(terminator));
            break;
        case ExprKind::GOTO:
        case ExprKind::EXIT:
        case ExprKind::BRANCH:
        case ExprKind::MULTIBRANCH:
        case ExprKind::INT_OP_WITH_EXCEPTION:
        case ExprKind::INTRINSIC_WITH_EXCEPTION:
        default: {
            auto dest = terminator->GetResult();
            if (dest) {
                state.SetToTopOrTopRef(dest, dest->GetType()->IsRef());
            }
            break;
        }
    }

    return std::nullopt;
}

void TypeAnalysis::HandleInvokeExpr(TypeDomain& state, const Invoke* invoke, Value* refObj)
{
    Type* resTy = invoke->GetResult()->GetType();
    while (resTy->IsRef()) {
        auto ty = StaticCast<RefType*>(resTy)->GetBaseType();
        resTy = ty;
    }
    if (!resTy->IsClass()) {
        return;
    }
    return state.Update(
        refObj, std::make_unique<TypeValue>(DevirtualTyKind::SUBTYPE_OF, StaticCast<ClassType*>(resTy)));
}

void TypeAnalysis::HandleApplyExpr(TypeDomain& state, const Apply* apply, Value* refObj)
{
    auto callee = apply->GetCallee();
    if (!callee->IsFuncWithBody()) {
        return;
    }

    Func* calleeFunc = DynamicCast<Func*>(callee);
    auto it = realRetTyMap.find(calleeFunc);
    if (it == realRetTyMap.end()) {
        return;
    }
    auto& relType = it->second;
    auto dest = refObj ? refObj : apply->GetResult();
    return state.Update(dest, std::make_unique<TypeValue>(DevirtualTyKind::SUBTYPE_OF, relType));
}

// =============== Transfer functions for TypeCast expression =============== //

void TypeAnalysis::HandleBoxExpr(TypeDomain& state, const Box* boxExpr) const
{
    auto result = boxExpr->GetResult();
    auto obj = state.GetReferencedObjAndSetToTop(result, boxExpr);
    state.Propagate(boxExpr->GetSourceValue(), obj);
}

template <typename TTypeCast> void TypeAnalysis::HandleTypeCastExpr(TypeDomain& state, const TTypeCast* typecast) const
{
    Type* srcTy = typecast->GetSourceTy();
    Type* tgtTy = typecast->GetTargetTy();
    while (srcTy->IsRef()) {
        auto ty1 = StaticCast<RefType*>(srcTy)->GetBaseType();
        srcTy = ty1;
    }

    while (tgtTy->IsRef()) {
        auto ty1 = StaticCast<RefType*>(tgtTy)->GetBaseType();
        tgtTy = ty1;
    }
    LocalVar* result = typecast->GetResult();
    // Set an initial state
    if (result->GetType()->IsRef()) {
        state.GetReferencedObjAndSetToTop(result, typecast);
    } else {
        state.SetToTopOrTopRef(result, false);
    }

    if (!srcTy->IsClass() || !tgtTy->IsClass()) {
        return;
    }

    // Check whether sourceTy is a subclass of targetTy.
    auto checkSubClass = [this](ClassType* sourceTy, ClassType* targetTy) {
        auto fatherTy = LeastCommonSuperClass(sourceTy, targetTy, &builder);
        // Return the sourceTy, only if least common super class is targetTy.
        if (fatherTy == targetTy) {
            return sourceTy;
        }
        return targetTy;
    };

    auto srcAbsVal = state.CheckAbstractObjectRefBy(typecast->GetSourceValue());
    if (!srcAbsVal) {
        return;
    }
    auto srcVal = state.CheckAbstractValue(srcAbsVal);
    if (!srcVal) {
        return;
    }

    if (srcVal->GetTypeKind() == DevirtualTyKind::EXACTLY) {
        return state.Propagate(typecast->GetSourceValue(), result);
    } else if (srcVal->GetTypeKind() == DevirtualTyKind::SUBTYPE_OF) {
        ClassType* srcClsTy = StaticCast<ClassType*>(srcTy);
        ClassType* tgtClsTy = StaticCast<ClassType*>(tgtTy);
        ClassType* resTy = checkSubClass(srcClsTy, tgtClsTy);
        if (srcVal->GetSpecificType() == resTy) {
            return state.Propagate(typecast->GetSourceValue(), result);
        } else {
            Value* resVal = result;
            if (result->GetType()->IsRef()) {
                resVal = state.CheckAbstractObjectRefBy(result);
            }
            return state.Update(resVal, std::make_unique<TypeValue>(DevirtualTyKind::SUBTYPE_OF, tgtClsTy));
        }
    }
}

void TypeAnalysis::PreHandleGetElementRefExpr(TypeDomain& state, const GetElementRef* getElemRef)
{
    HandleDefaultExpr(state, getElemRef);
}

void TypeAnalysis::HandleDefaultExpr(TypeDomain& state, const Expression* expr) const
{
    // only using result type to analyse state
    // must meet following conditions:
    // 1. expr do not have branch
    // 2. expr do not have more accurate state info
    // 3. keep state if expr is re-analyse
    auto result = expr->GetResult();
    auto resType = result->GetType();
    if (resType->IsPrimitive() || resType->IsStruct() || resType->IsEnum()) {
        state.SetToTopOrTopRef(result, false);
        state.Update(result, std::make_unique<TypeValue>(DevirtualTyKind::EXACTLY, resType));
        return;
    }
    if (!resType->IsRef()) {
        state.SetToTopOrTopRef(result, false);
        return;
    }
    auto firstRefBaseType = StaticCast<RefType*>(resType)->GetBaseType();
    if (firstRefBaseType->IsPrimitive() || firstRefBaseType->IsStruct() || firstRefBaseType->IsEnum()) {
        auto resVal = state.GetReferencedObjAndSetToTop(result, expr);
        state.Update(resVal, std::make_unique<TypeValue>(DevirtualTyKind::EXACTLY, firstRefBaseType));
        return;
    }
    if (firstRefBaseType->IsClass()) {
        auto classDef = StaticCast<ClassType*>(firstRefBaseType)->GetClassDef();
        auto kind =
            !classDef->IsInterface() && !classDef->TestAttr(Attribute::VIRTUAL) && !classDef->IsAbstract() ?
            DevirtualTyKind::EXACTLY : DevirtualTyKind::SUBTYPE_OF;
        auto resVal = state.GetReferencedObjAndSetToTop(result, expr);
        state.Update(resVal, std::make_unique<TypeValue>(kind, firstRefBaseType));
        return;
    }
    if (!firstRefBaseType->IsRef()) {
        state.GetReferencedObjAndSetToTop(result, expr);
        return;
    }
    auto secondRefBaseType = StaticCast<RefType*>(firstRefBaseType)->GetBaseType();
    auto resVal = state.GetTwoLevelRefAndSetToTop(result, expr);
    if (secondRefBaseType->IsClass()) {
        auto classDef = StaticCast<ClassType*>(secondRefBaseType)->GetClassDef();
        auto kind =
            !classDef->IsInterface() && !classDef->TestAttr(Attribute::VIRTUAL) && !classDef->IsAbstract() ?
            DevirtualTyKind::EXACTLY : DevirtualTyKind::SUBTYPE_OF;
        state.Update(resVal, std::make_unique<TypeValue>(kind, secondRefBaseType));
    }
}

void TypeAnalysis::HandleFuncParam(TypeDomain& state, const Parameter* param, Value* refObj)
{
    if (!refObj) {
        return;
    }

    auto baseTy = param->GetType()->StripAllRefs();
    if (baseTy->IsClass()) {
        state.Update(refObj, std::make_unique<TypeValue>(DevirtualTyKind::SUBTYPE_OF, StaticCast<ClassType*>(baseTy)));
    }
}
}  // namespace Cangjie::CHIR