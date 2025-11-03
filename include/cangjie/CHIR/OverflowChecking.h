// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_OVERFLOWCHECKING_H
#define CANGJIE_CHIR_OVERFLOWCHECKING_H

// DO NOT remove this include. Required for builds for Windows and newer clang
#include <limits>

#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Type/Type.h"

namespace Cangjie::CHIR {

class OverflowChecker {
public:
    /**
     * @brief Check if overflow after operation of x and y, Note: x and y themselves do not exceed the type range.
     *
     * @param typeKind The type kind of the operation.
     * @param exprKind The expression kind of the operation.
     * @param x The first operand.
     * @param y The second operand.
     * @param strategy The overflow strategy to be used.
     * @param res The result of the operation.
     * @return True if an overflow occurs, false otherwise.
     */
    static bool IsIntOverflow(const Type::TypeKind& typeKind, const ExprKind& exprKind, int64_t x, int64_t y,
        const OverflowStrategy& strategy, int64_t* res);

    /**
     * @brief Check if overflow after operation of x and y, Note: x and y themselves do not exceed the type range.
     *
     * @param typeKind The type kind of the operation.
     * @param exprKind The expression kind of the operation.
     * @param x The first operand.
     * @param y The second operand.
     * @param strategy The overflow strategy to be used.
     * @param res The result of the operation.
     * @return True if an overflow occurs, false otherwise.
     */
    static bool IsUIntOverflow(const Type::TypeKind& typeKind, const ExprKind& exprKind, uint64_t x, uint64_t y,
        const OverflowStrategy& strategy, uint64_t* res);

    /**
     * @brief Given two int/uint x and y, check if the operation is overflow
     * and stores the result in res according to the overflow strategy.
     *
     * @tparam T The data type of the operands.
     * @param x The first operand.
     * @param y The second operand.
     * @param kind The expression kind of the operation.
     * @param strategy The overflow strategy to be used.
     * @param res The result of the operation.
     * @return True if an overflow occurs, false otherwise.
     */
    template <typename T> static bool IsOverflow(T x, T y, ExprKind kind, const OverflowStrategy& strategy, T* res);

    /**
     * @brief Checks for overflow during an exponential operation.
     *
     * @param x The base of the exponentiation.
     * @param y The exponent.
     * @param strategy The overflow strategy to be used.
     * @param res The result of the operation.
     * @return True if an overflow occurs, false otherwise.
     */
    static bool IsExpOverflow(int64_t x, uint64_t y, OverflowStrategy strategy, int64_t* res);

    /**
     * @brief Checks for overflow when typecasting an integer to another type.
     *
     * @tparam T The source data type.
     * @tparam K The target data type.
     * @param x The value to be typecast.
     * @param res The result of the typecast.
     * @param strategy The overflow strategy to be used.
     * @return True if an overflow occurs, false otherwise.
     */
    template <typename T, typename K> static bool IsTypecastOverflowForInt(T x, K* res, OverflowStrategy strategy)
    {
        CJC_NULLPTR_CHECK(res);
        CJC_ASSERT(!(std::is_same<T, double>::value) && !(std::is_same<T, float>::value));
        bool isOverflow = false;
        if (std::is_signed<T>::value && !std::is_signed<K>::value && x < 0) {
            isOverflow = true;
        } else if (!std::is_signed<T>::value && std::is_signed<K>::value) {
            isOverflow = x > static_cast<T>(std::numeric_limits<K>::max());
        } else {
            using LargerType = typename std::conditional<sizeof(T) <= sizeof(K), K, T>::type;
            constexpr auto tmax = static_cast<LargerType>(std::numeric_limits<T>::max());
            constexpr auto kmax = static_cast<LargerType>(std::numeric_limits<K>::max());
            if constexpr (tmax > kmax) {
                isOverflow = x > static_cast<T>(std::numeric_limits<K>::max()) ||
                    x < static_cast<T>(std::numeric_limits<K>::min());
            } else {
                isOverflow = static_cast<K>(x) > std::numeric_limits<K>::max() ||
                    static_cast<K>(x) < std::numeric_limits<K>::min();
            }
        }
        if (isOverflow && strategy == OverflowStrategy::SATURATING) {
            if (x < 0) {
                *res = std::numeric_limits<K>::min();
            } else {
                *res = std::numeric_limits<K>::max();
            }
        } else {
            *res = (K)x;
        }
        return isOverflow;
    }

    /**
     * @brief Checks for overflow after an addition operation.
     *
     * @tparam T The data type of the operands.
     * @param x The first operand.
     * @param y The second operand.
     * @param strategy The overflow strategy to be used.
     * @param res The result of the addition.
     * @return True if an overflow occurs, false otherwise.
     */
    template <typename T> static bool IsOverflowAfterAdd(T x, T y, OverflowStrategy strategy, T* res)
    {
        CJC_NULLPTR_CHECK(res);
        bool isOverflow = __builtin_add_overflow(x, y, res);
        if (isOverflow && strategy == OverflowStrategy::SATURATING) {
            if (*res > x) {
                *res = std::numeric_limits<T>::min();
            } else {
                *res = std::numeric_limits<T>::max();
            }
        }
        return isOverflow;
    }

    /**
     * @brief Checks for overflow after a subtraction operation.
     *
     * @tparam T The data type of the operands.
     * @param x The first operand.
     * @param y The second operand.
     * @param strategy The overflow strategy to be used.
     * @param res The result of the subtraction.
     * @return True if an overflow occurs, false otherwise.
     */
    template <typename T> static bool IsOverflowAfterSub(T x, T y, OverflowStrategy strategy, T* res)
    {
        CJC_NULLPTR_CHECK(res);
        bool isOverflow = __builtin_sub_overflow(x, y, res);
        if (isOverflow && strategy == OverflowStrategy::SATURATING) {
            if (*res > x) {
                *res = std::numeric_limits<T>::min();
            } else {
                *res = std::numeric_limits<T>::max();
            }
        }
        return isOverflow;
    }

    /**
     * @brief Checks for overflow after a multiplication operation.
     *
     * @tparam T The data type of the operands.
     * @param x The first operand.
     * @param y The second operand.
     * @param strategy The overflow strategy to be used.
     * @param res The result of the multiplication.
     * @return True if an overflow occurs, false otherwise.
     */
    template <typename T> static bool IsOverflowAfterMul(T x, T y, OverflowStrategy strategy, T* res)
    {
        CJC_NULLPTR_CHECK(res);
        bool isOverflow = __builtin_mul_overflow(x, y, res);
        if (isOverflow && strategy == OverflowStrategy::SATURATING) {
            if (x < 0 && y < 0) {
                *res = std::numeric_limits<T>::max();
            } else if (x >= 0 && y >= 0) {
                *res = std::numeric_limits<T>::max();
            } else {
                *res = std::numeric_limits<T>::min();
            }
        }
        return isOverflow;
    }

    /**
     * @brief Checks for overflow after a division operation.
     *
     * @tparam T The data type of the operands.
     * @param x The first operand.
     * @param y The second operand.
     * @param strategy The overflow strategy to be used.
     * @param res The result of the division.
     * @return True if an overflow occurs, false otherwise.
     */
    template <typename T> static bool IsOverflowAfterDiv(T x, T y, OverflowStrategy strategy, T* res)
    {
        CJC_NULLPTR_CHECK(res);
        bool isOverflow = false;
        if (std::is_signed<T>::value) {
            isOverflow = x == std::numeric_limits<T>::min() && y == -1;
            if (isOverflow && strategy == OverflowStrategy::WRAPPING) {
                *res = std::numeric_limits<T>::min();
                return isOverflow;
            } else if (isOverflow && strategy == OverflowStrategy::SATURATING) {
                *res = std::numeric_limits<T>::max();
                return isOverflow;
            } else if (isOverflow) {
                return isOverflow;
            }
        }
        CJC_ASSERT(y != 0);
        *res = x / y;
        return isOverflow;
    }

    /**
     * @brief Checks for overflow after a modulus operation.
     *
     * @tparam T The data type of the operands.
     * @param x The first operand.
     * @param y The second operand.
     * @param res The result of the modulus operation.
     * @return True if an overflow occurs, false otherwise.
     */
    template <typename T> static bool IsOverflowAfterMod(T x, T y, T* res)
    {
        CJC_NULLPTR_CHECK(res);
        if (std::is_signed<T>::value) {
            if (x == std::numeric_limits<T>::min() && y == -1) {
                *res = 0;
                // CJ decided that this does not cause overflow
                return true;
            }
        }
        CJC_ASSERT(y != 0);
        *res = x % y;
        return false;
    }
};
} // namespace Cangjie::CHIR

#endif // CANGJIE_CHIR_OVERFLOWCHECKING_H
