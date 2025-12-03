// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Analysis/TypeAnalysis.h"
#include "cangjie/CHIR/Utils/Utils.h"

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

namespace {
Type* GetRealGenericRetType(const Apply& apply, Type& genericType, const Func* calleeFunc, CHIRBuilder& builder)
{
    auto instTypes = apply.GetInstantiatedTypeArgs();
    auto genericTypes = calleeFunc->GetGenericTypeParams();
    CJC_ASSERT(instTypes.size() == genericTypes.size());
    std::unordered_map<const GenericType*, Type*> genericMap;
    for (size_t i = 0; i < instTypes.size(); ++i) {
        genericMap[genericTypes[i]] = instTypes[i];
    }
    if (calleeFunc->GetParentCustomTypeDef() != nullptr) {
        auto def = calleeFunc->GetParentCustomTypeDef();
        auto thisType = apply.GetThisType()->StripAllRefs();
        if (thisType->IsClassOrStruct()) {
            auto customType = StaticCast<CustomType*>(thisType);
            auto defGenericParams = def->GetGenericTypeParams();
            auto genericArgs = customType->GetGenericArgs();
            CJC_ASSERT(defGenericParams.size() == genericArgs.size());
            for (size_t i = 0; i < customType->GetGenericArgs().size(); ++i) {
                genericMap[defGenericParams[i]] = genericArgs[i];
            }
        }
    }
    return ReplaceRawGenericArgType(genericType, genericMap, builder);
}
}

CHIRBuilder* TypeValue::builder{nullptr};
template <> const std::string Analysis<TypeDomain>::name = "type-analysis";
template <> const std::optional<unsigned> Analysis<TypeDomain>::blockLimit = 300;
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
    const Func* func, CHIRBuilder& builder, bool isDebug, const DevirtualizationInfo& devirtInfo)
    : ValueAnalysis(func, builder, isDebug), realRetTyMap(devirtInfo.GetReturnTypeMap()),
      constMemberTypeMap(devirtInfo.GetConstMemberMap())
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
    auto resultType = expression->GetResult()->GetType();
    if (isDebug && !resultType->IsRef() && !resultType->IsGeneric()) {
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
        HandleDefaultExpr(state, apply);
        return;
    }

    Func* calleeFunc = DynamicCast<Func*>(callee);
    auto it = realRetTyMap.find(calleeFunc);
    if (it == realRetTyMap.end()) {
        HandleDefaultExpr(state, apply);
        return;
    }
    auto realRetTy = it->second;
    if (realRetTy->IsGenericRelated()) {
        realRetTy = GetRealGenericRetType(*apply, *realRetTy, calleeFunc, builder);
        if (realRetTy == nullptr) {
            // cannot get real generic return type, return.
            return;
        }
    }
    return UpdateDefaultValue(state, apply->GetResult(), refObj, realRetTy);
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

template <class MemberAccess>
static Type* GetInnerTypeFromMemberAccess(const MemberAccess& ma, CHIRBuilder& builder)
{
    const auto& path = ma.GetPath();
    auto locationType = ma.GetOperands()[0]->GetType();
    for (size_t i = 0; i < path.size() - 1; ++i) {
        auto index = path[i];
        locationType = GetFieldOfType(*locationType, index, builder);
    }
    return locationType;
}

static const CustomTypeDef* CheckMemberDefineInWhichParent(CustomType& type, size_t index)
{
    auto def = type.GetCustomTypeDef();
    auto memberInfo = def->GetInstanceVar(index);
    return memberInfo.outerDef == nullptr ? def : memberInfo.outerDef;
}

template <typename TMemberAccess>
void TypeAnalysis::HandleMemeberAccess(TypeDomain& state, const TMemberAccess* memberAccess) const
{
    auto locationType = GetInnerTypeFromMemberAccess(*memberAccess, builder)->StripAllRefs();
    if (!locationType->IsClassOrStruct()) {
        return HandleDefaultExpr(state, memberAccess);
    }
    auto path = memberAccess->GetPath();
    auto index = path[path.size() - 1];
    auto customType = StaticCast<CustomType*>(locationType);
    // get member define in which parent.
    auto def = CheckMemberDefineInWhichParent(*customType, index);
    auto it = constMemberTypeMap.find(def);
    if (it == constMemberTypeMap.end()) {
        return HandleDefaultExpr(state, memberAccess);
    }
    auto it2 = it->second.find(index);
    if (it2 == it->second.end()) {
        return HandleDefaultExpr(state, memberAccess);
    }
    auto geneircSubType = it2->second->StripAllRefs();
    // member define:
    //   class XX<T> {
    //     let member: I<T> = CA<T>()
    //     ..
    //   }
    // user of XX: getElementRef or field:
    //     let x = XX<Int64>()
    //     x.member.func()
    //       => type of x.member will be a inst parent type I<Int64>. To Get CA<Int64>, we need inst sub type from
    //          generic sub type CA<T> and inst parent type I<Int64>.
    auto ResultInstParentType = memberAccess->GetResult()->GetType()->StripAllRefs();
    if(!ResultInstParentType->IsClass()) {
        return HandleDefaultExpr(state, memberAccess);
    }
    auto instanceType =
        GetInstSubType(*geneircSubType, *static_cast<ClassType*>(ResultInstParentType), builder);
    AbstractObject* refObj;
    if constexpr (std::is_same_v<GetElementRef, TMemberAccess>) {
        refObj = state.GetTwoLevelRefAndSetToTop(memberAccess->GetResult(), memberAccess);
    } else {
        // else is field
        refObj = state.GetReferencedObjAndSetToTop(memberAccess->GetResult(), memberAccess);
    }
    UpdateDefaultValue(state, memberAccess->GetResult(), refObj, instanceType);
}

void TypeAnalysis::PreHandleGetElementRefExpr(TypeDomain& state, const GetElementRef* getElemRef)
{
    HandleMemeberAccess(state, getElemRef);
}

void TypeAnalysis::PreHandleFieldExpr(TypeDomain& state, const Field* field)
{
    HandleMemeberAccess(state, field);
}

void TypeAnalysis::HandleDefaultExpr(TypeDomain& state, const Expression* expr) const
{
    auto refObj = PreHandleDefaultExpr(state, expr);
    UpdateDefaultValue(state, expr->GetResult(), refObj, nullptr);
}

Value* TypeAnalysis::PreHandleDefaultExpr(TypeDomain& state, const Expression* expr) const
{
    auto result = expr->GetResult();
    auto resType = result->GetType();
    if (!resType->IsRef()) {
        state.SetToTopOrTopRef(result, false);
        return nullptr;
    }
    auto firstRefBaseType = StaticCast<RefType*>(resType)->GetBaseType();
    if (!firstRefBaseType->IsRef()) {
        auto refObj = state.GetReferencedObjAndSetToTop(result, expr);
        return refObj;
    }
    auto refObj = state.GetTwoLevelRefAndSetToTop(result, expr);
    return refObj;
}

void TypeAnalysis::UpdateDefaultValue(
    TypeDomain& state, Value* value, Value* refObj, Type* relType) const
{
    if (value == nullptr) {
        return;
    }
    auto dest = refObj ? refObj : value;
    auto type = relType == nullptr ? value->GetType()->StripAllRefs() : relType;
    if (type->IsBox()) {
        type = StaticCast<BoxType*>(type)->GetBaseType();
    }
    if (type->IsPrimitive() || type->IsStruct() || type->IsEnum()) {
        state.Update(dest, std::make_unique<TypeValue>(DevirtualTyKind::EXACTLY, type));
    }
    if (type->IsClass()) {
        auto classDef = StaticCast<ClassType*>(type)->GetClassDef();
        auto kind =
            !classDef->IsInterface() && !classDef->TestAttr(Attribute::VIRTUAL) && !classDef->IsAbstract() ?
            DevirtualTyKind::EXACTLY : DevirtualTyKind::SUBTYPE_OF;
        state.Update(dest, std::make_unique<TypeValue>(kind, type));
    }
}

void TypeAnalysis::HandleFuncParam(TypeDomain& state, Parameter* param, Value* refObj)
{
    if (!refObj) {
        return;
    }
    UpdateDefaultValue(state, param, refObj, nullptr);
}
}  // namespace Cangjie::CHIR