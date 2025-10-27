// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements constant analysis.
 */

#include "cangjie/CHIR/Analysis/ConstAnalysis.h"

#include <cmath>

namespace Cangjie::CHIR {
ConstValue::ConstValue(ConstKind kind) : kind(kind)
{
}

ConstValue::~ConstValue()
{
}

ConstValue::ConstKind ConstValue::GetConstKind() const
{
    return kind;
}

std::optional<std::unique_ptr<ConstValue>> ConstBoolVal::Join(const ConstValue& rhs) const
{
    if (rhs.GetConstKind() == ConstKind::BOOL && this->val == StaticCast<const ConstBoolVal&>(rhs).val) {
        return std::nullopt;
    }
    return nullptr;
}

std::string ConstBoolVal::ToString() const
{
    return val ? "true" : "false";
}

std::unique_ptr<ConstValue> ConstBoolVal::Clone() const
{
    return std::make_unique<ConstBoolVal>(val);
}

bool ConstBoolVal::GetVal() const
{
    return val;
}

std::optional<std::unique_ptr<ConstValue>> ConstRuneVal::Join(const ConstValue& rhs) const
{
    if (rhs.GetConstKind() == ConstKind::RUNE && this->val == StaticCast<const ConstRuneVal&>(rhs).val) {
        return std::nullopt;
    }
    return nullptr;
}

std::string ConstRuneVal::ToString() const
{
    return std::to_string(val);
}

std::unique_ptr<ConstValue> ConstRuneVal::Clone() const
{
    return std::make_unique<ConstRuneVal>(val);
}

char32_t ConstRuneVal::GetVal() const
{
    return val;
}

std::optional<std::unique_ptr<ConstValue>> ConstStrVal::Join(const ConstValue& rhs) const
{
    if (rhs.GetConstKind() == ConstKind::STRING && this->val == StaticCast<const ConstStrVal&>(rhs).val) {
        return std::nullopt;
    }
    return nullptr;
}

std::string ConstStrVal::ToString() const
{
    return val;
}

std::unique_ptr<ConstValue> ConstStrVal::Clone() const
{
    return std::make_unique<ConstStrVal>(val);
}

std::string ConstStrVal::GetVal() const
{
    return val;
}

std::optional<std::unique_ptr<ConstValue>> ConstUIntVal::Join(const ConstValue& rhs) const
{
    if (rhs.GetConstKind() == ConstKind::UINT && this->val == StaticCast<const ConstUIntVal&>(rhs).val) {
        return std::nullopt;
    }
    return nullptr;
}

std::string ConstUIntVal::ToString() const
{
    return std::to_string(val);
}

std::unique_ptr<ConstValue> ConstUIntVal::Clone() const
{
    return std::make_unique<ConstUIntVal>(val);
}

uint64_t ConstUIntVal::GetVal() const
{
    return val;
}

std::optional<std::unique_ptr<ConstValue>> ConstIntVal::Join(const ConstValue& rhs) const
{
    if (rhs.GetConstKind() == ConstKind::INT && this->val == StaticCast<const ConstIntVal&>(rhs).val) {
        return std::nullopt;
    }
    return nullptr;
}

std::string ConstIntVal::ToString() const
{
    return std::to_string(val);
}

std::unique_ptr<ConstValue> ConstIntVal::Clone() const
{
    return std::make_unique<ConstIntVal>(val);
}

int64_t ConstIntVal::GetVal() const
{
    return val;
}

std::optional<std::unique_ptr<ConstValue>> ConstFloatVal::Join(const ConstValue& rhs) const
{
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#endif
    // this float equal is intentional
    if (rhs.GetConstKind() == ConstKind::FLOAT && this->val == StaticCast<const ConstFloatVal&>(rhs).val) {
#if defined(__clang__)
#pragma clang diagnostic pop
#else
#pragma GCC diagnostic pop
#endif
        return std::nullopt;
    }
    return nullptr;
}

std::string ConstFloatVal::ToString() const
{
    return std::to_string(val);
}

std::unique_ptr<ConstValue> ConstFloatVal::Clone() const
{
    return std::make_unique<ConstFloatVal>(val);
}

double ConstFloatVal::GetVal() const
{
    return val;
}

template <> const std::string Analysis<ConstDomain>::name = "const-analysis";
template <> const std::optional<unsigned> Analysis<ConstDomain>::blockLimit = std::nullopt;
template <> ConstDomain::ChildrenMap ValueAnalysis<ConstValueDomain>::globalChildrenMap{};
template <> ConstDomain::AllocatedRefMap ValueAnalysis<ConstValueDomain>::globalAllocatedRefMap{};
template <> ConstDomain::AllocatedObjMap ValueAnalysis<ConstValueDomain>::globalAllocatedObjMap{};
template <> std::vector<std::unique_ptr<Ref>> ValueAnalysis<ConstValueDomain>::globalRefPool{};
template <> std::vector<std::unique_ptr<AbstractObject>> ValueAnalysis<ConstValueDomain>::globalAbsObjPool{};
template <>
ConstDomain ValueAnalysis<ConstValueDomain>::globalState{&globalChildrenMap, &globalAllocatedRefMap,
    nullptr, &globalAllocatedObjMap, &globalRefPool, &globalAbsObjPool};

template <> const std::string Analysis<ConstPoolDomain>::name = "const-analysis";
template <> const std::optional<unsigned> Analysis<ConstPoolDomain>::blockLimit = std::nullopt;
template <> ConstPoolDomain::ChildrenMap ValueAnalysis<ConstValueDomain, ConstActivePool>::globalChildrenMap{};
template <> ConstPoolDomain::AllocatedRefMap ValueAnalysis<ConstValueDomain, ConstActivePool>::globalAllocatedRefMap{};
template <> ConstPoolDomain::AllocatedObjMap ValueAnalysis<ConstValueDomain, ConstActivePool>::globalAllocatedObjMap{};
template <> std::vector<std::unique_ptr<Ref>> ValueAnalysis<ConstValueDomain, ConstActivePool>::globalRefPool{};
template <> std::vector<std::unique_ptr<AbstractObject>> ValueAnalysis<ConstValueDomain, ConstActivePool>::globalAbsObjPool{};
template <>
ConstPoolDomain ValueAnalysis<ConstValueDomain, ConstActivePool>::globalState{&globalChildrenMap,
    &globalAllocatedRefMap, nullptr, &globalAllocatedObjMap, &globalRefPool, &globalAbsObjPool};

template <> bool IsTrackedGV<ValueDomain<ConstValue>>(const GlobalVar& gv)
{
    auto baseTyKind = StaticCast<RefType*>(gv.GetType())->GetBaseType()->GetTypeKind();
    return (baseTyKind >= Type::TYPE_INT8 && baseTyKind <= Type::TYPE_UNIT) || baseTyKind == Type::TYPE_TUPLE ||
        baseTyKind == Type::TYPE_STRUCT || baseTyKind == Type::TYPE_ENUM;
}

template <>
ValueDomain<ConstValue> HandleNonNullLiteralValue<ValueDomain<ConstValue>>(const LiteralValue* literalValue)
{
    if (literalValue->IsBoolLiteral()) {
        return ValueDomain<ConstValue>(
            std::make_unique<ConstBoolVal>(StaticCast<BoolLiteral*>(literalValue)->GetVal()));
    } else if (literalValue->IsFloatLiteral()) {
        // There is no proper types to represent `Float16` in Cangjie.
        if (literalValue->GetType()->GetTypeKind() != Type::TypeKind::TYPE_FLOAT16) {
            return ValueDomain<ConstValue>(
                std::make_unique<ConstFloatVal>(StaticCast<FloatLiteral*>(literalValue)->GetVal()));
        } else {
            return ValueDomain<ConstValue>(/* isTop = */true);
        }
    } else if (literalValue->IsIntLiteral()) {
        if (auto intTy = StaticCast<IntType*>(literalValue->GetType()); intTy->IsSigned()) {
            return ValueDomain<ConstValue>(
                std::make_unique<ConstIntVal>(StaticCast<IntLiteral*>(literalValue)->GetSignedVal()));
        } else {
            return ValueDomain<ConstValue>(
                std::make_unique<ConstUIntVal>(StaticCast<IntLiteral*>(literalValue)->GetUnsignedVal()));
        }
    } else if (literalValue->IsRuneLiteral()) {
        return ValueDomain<ConstValue>(
            std::make_unique<ConstRuneVal>(StaticCast<RuneLiteral*>(literalValue)->GetVal()));
    } else if (literalValue->IsStringLiteral()) {
        return ValueDomain<ConstValue>(
                std::make_unique<ConstStrVal>(StaticCast<StringLiteral*>(literalValue)->GetVal()));
    } else if (literalValue->IsUnitLiteral()) {
        return ValueDomain<ConstValue>(/* isTop = */true);
    } else {
        InternalError("Unsupported const val kind");
        return ValueDomain<ConstValue>(/* isTop = */true);
    }
}
}  // namespace Cangjie::CHIR