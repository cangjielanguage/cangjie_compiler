// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_UTILS_H
#define CANGJIE_CHIR_ANALYSIS_UTILS_H

#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/CHIR/Analysis/Analysis.h"
#include "cangjie/CHIR/IR/DebugLocation.h"
#include "cangjie/CHIR/IR/Type/Type.h"

#include <functional>
#include <unordered_map>

namespace Cangjie::CHIR {
/**
 * @brief join template functions.
 * @tparam K map key, normally CHIR value.
 * @tparam V map value, normally domain.
 * @param lhs left side to join.
 * @param rhs right side to join.
 * @param action join function of different domian.
 * @return flag if changed after join.
 */
template <typename K, typename V>
bool MapJoinTemplate(
    std::unordered_map<K, V>& lhs, const std::unordered_map<K, V>& rhs, std::function<bool(K, V&, const V&)> action)
{
    bool changed = false;
    for (auto& [k2, v2] : rhs) {
        if (auto it = lhs.find(k2); it != lhs.end()) {
            auto& v1 = it->second;
            changed |= action(k2, v1, v2);
        } else {
            lhs.emplace(k2, v2);
            changed = true;
        }
    }
    return changed;
}

/**
 * @brief map join interface
 * @tparam T map key, normally CHIR value.
 * @tparam Domain map value, normally domain.
 * @param lhs left side to join.
 * @param rhs right side to join.
 * @return flag if changed after join.
 */
template <typename T, typename Domain, typename = std::enable_if_t<std::is_base_of_v<AbstractDomain<Domain>, Domain>>>
bool MapJoin(std::unordered_map<T, Domain>& lhs, const std::unordered_map<T, Domain>& rhs)
{
    const auto action = [](T, Domain& v1, const Domain& v2) -> bool { return v1.Join(v2); };
    return MapJoinTemplate<T, Domain>(lhs, rhs, action);
}

/**
 * @brief join a vector domain
 * @tparam Domain domain to join
 * @param lhs left side to join.
 * @param rhs right side to join.
 * @return flag if changed after join.
 */
template <typename Domain, typename = std::enable_if_t<std::is_base_of_v<AbstractDomain<Domain>, Domain>>>
bool VectorJoin(std::vector<Domain>& lhs, const std::vector<Domain>& rhs)
{
    CJC_ASSERT(lhs.size() == rhs.size());
    bool changed = false;
    for (size_t i = 0; i < lhs.size(); ++i) {
        changed |= lhs[i].Join(rhs[i]);
    }
    return changed;
}

/// get ref name from index
std::string GetRefName(size_t index);

/// get object name from index
std::string GetObjName(size_t index);

/// get child object name from index and parent name.
std::string GetObjChildName(std::string parentName, size_t fieldIdx);

/**
 * @brief from cangjie position to range.
 * @param loc location to change.
 * @return cangjie range change to.
 */
template <typename T> Cangjie::Range ToRange(const T& loc)
{
    auto begin = loc.GetBeginPos();
    auto end = loc.GetEndPos();
    Cangjie::Position beginPos = Cangjie::Position(
        loc.GetFileID(), static_cast<int>(begin.line), static_cast<int>(begin.column));
    Cangjie::Position endPos = Cangjie::Position(
        loc.GetFileID(), static_cast<int>(end.line), static_cast<int>(end.column));
    return MakeRange(beginPos, endPos);
}

/**
 * @brief from cangjie position to range with non zero.
 * @param loc location to change.
 * @return cangjie range change to.
 */
template <typename T> std::pair<bool, Cangjie::Range> ToRangeIfNotZero(const T& loc)
{
    auto begin = loc.GetBeginPos();
    auto end = loc.GetEndPos();
    Cangjie::Position beginPos(loc.GetFileID(), static_cast<int>(begin.line), static_cast<int>(begin.column));
    Cangjie::Position endPos(loc.GetFileID(), static_cast<int>(end.line), static_cast<int>(end.column));
    if (!beginPos.IsZero() && !endPos.IsZero()) {
        return std::make_pair(true, MakeRange(beginPos, endPos));
    }
    return std::make_pair(false, MakeRange(beginPos, endPos));
}

/**
 * @brief from CHIR location to cangjie position.
 * @param loc CHIR location input.
 * @return cangjie position change to.
 */
Cangjie::Position ToPosition(const DebugLocation& loc);

/**
 * @brief from CHIR location to format print.
 * @param loc CHIR location.
 * @param isPrintFileName flag whether print file name.
 * @return print result of location.
 */
std::string ToPosInfo(const DebugLocation& loc, bool isPrintFileName = false);

/**
 * @brief cut from long type value to small type with high bits cut off.
 * @tparam T type to cut.
 * @param srcNum number input to cut.
 * @param destTypeKind destination type.
 * @return value after cut.
 */
template <typename T> T CutOffHighBits(T srcNum, Type::TypeKind destTypeKind)
{
    switch (destTypeKind) {
        case Type::TypeKind::TYPE_UINT8:
            return static_cast<T>(static_cast<uint8_t>(srcNum));
        case Type::TypeKind::TYPE_UINT16:
            return static_cast<T>(static_cast<uint16_t>(srcNum));
        case Type::TypeKind::TYPE_UINT32:
            return static_cast<T>(static_cast<uint32_t>(srcNum));
        case Type::TypeKind::TYPE_UINT64:
            return static_cast<T>(static_cast<uint64_t>(srcNum));
        case Type::TypeKind::TYPE_UINT_NATIVE:
            return static_cast<T>(static_cast<size_t>(srcNum));
        case Type::TypeKind::TYPE_INT8:
            return static_cast<T>(static_cast<int8_t>(srcNum));
        case Type::TypeKind::TYPE_INT16:
            return static_cast<T>(static_cast<int16_t>(srcNum));
        case Type::TypeKind::TYPE_INT32:
            return static_cast<T>(static_cast<int32_t>(srcNum));
        case Type::TypeKind::TYPE_INT64:
            return static_cast<T>(static_cast<int64_t>(srcNum));
        case Type::TypeKind::TYPE_INT_NATIVE:
            return static_cast<T>(static_cast<ssize_t>(srcNum));
        case Type::TypeKind::TYPE_FLOAT32:
            return static_cast<T>(static_cast<float>(srcNum));
        case Type::TypeKind::TYPE_FLOAT64:
            return static_cast<T>(static_cast<double>(srcNum));
        default:
            CJC_ABORT();
            return static_cast<T>(0);
    }
}

/**
 * @brief check if member var is initialized.
 * @param func function to check,
 * @param store store expression.
 * @return path of member var.
 */
std::optional<size_t> IsInitialisingMemberVar(const Func& func, const StoreElementRef& store);

/**
 * @brief This function will check if an expression is an Apply expression, and if it's callee
 * is a result of a Lambda expression. If it's, it will return the lambda; otherwise a nullptr.
 */
const Lambda* IsApplyToLambda(const Expression* expr);

/// This function will check if the function is getOrThrow function.
bool IsGetOrThrowFunction(const Expression& expr);

/**
 * @brief find least common supper class of two input classes.
 * @param ty1 first class.
 * @param ty2 second class.
 * @param builder CHIR builder for collecting info in CHIR.
 * @return least common supper class.
 */
ClassType* LeastCommonSuperClass(ClassType* ty1, ClassType* ty2, CHIRBuilder* builder);

/// check if type is enum with struct body.
bool IsStructEnum(const Ptr<Type>& type);

/// check if type is enum without struct body.
bool IsRefEnum(const Ptr<Type>& type);

/// check if expression is arithmetic of unsigned value.
bool IsUnsignedArithmetic(const BinaryExpression& expr);

/// get CHIR func value from apply.
Func* TryGetInstanceVarInitFromApply(const Expression& expr);

/**
 * @brief Get mutable variables that captured by lambda recursively.
 * if there is lambda define or lambda calling, we will step in and collect child lambda's captured mutable variables
 * @param lambda the root lambda.
 * @return captured mutable variables.
 */
std::unordered_set<Value*> GetLambdaCapturedVarsRecursively(const Lambda& lambda);
} // namespace Cangjie::CHIR

#endif
