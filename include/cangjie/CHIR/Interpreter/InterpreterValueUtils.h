// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares interpreter values.
 */

#ifndef CANGJIE_CHIR_INTERRETER_INTERPREVERVALUEUTILS_H
#define CANGJIE_CHIR_INTERRETER_INTERPREVERVALUEUTILS_H

#include "cangjie/CHIR/Interpreter/InterpreterValue.h"
#include "cangjie/CHIR/Interpreter/BCHIR.h"

namespace Cangjie::CHIR::Interpreter {

struct IValUtils {
    template <class T, class... Types> inline static constexpr T& Get(IVal& v)
    {
        return std::get<T, Types...>(v);
    }
    template <class T, class... Types> inline static constexpr T&& Get(IVal&& v)
    {
        return std::get<T, Types...>(std::move(v));
    }
    template <class T, class... Types> inline static constexpr const T& Get(const IVal& v)
    {
        return std::get<T, Types...>(v);
    }
    template <class T, class... Types> inline static constexpr std::add_pointer_t<T> GetIf(IVal* pv) noexcept
    {
        return std::get_if<T, Types...>(pv);
    }
    template <class T, class... Types>
    inline static constexpr std::add_pointer_t<const T> GetIf(const IVal* pv) noexcept
    {
        return std::get_if<T, Types...>(pv);
    }

    /** @brief Print Interpreter values */
    static void Printer(const IVal& v, std::ostream& os = std::cout);

    /** @brief Transform value to a string
     *
     * This function is not used by the interpreter directly, but it's
     * necessary when debugging the interpreter runtime
     */
    static std::string ToString(const IVal& v);

    /** @brief Create an interpreter primitive value with content given in the argument */
    template <typename T, typename K> static T PrimitiveValue(K value)
    {
        if constexpr (std::is_same<T, IInt8>::value) {
            return PrimitiveInt8(value);
        } else if constexpr (std::is_same<T, IInt16>::value) {
            return PrimitiveInt16(value);
        } else if constexpr (std::is_same<T, IInt32>::value) {
            return PrimitiveInt32(value);
        } else if constexpr (std::is_same<T, IInt64>::value) {
            return PrimitiveInt64(value);
        } else if constexpr (std::is_same<T, IIntNat>::value) {
            return PrimitiveIntNat(value);
        } else if constexpr (std::is_same<T, IUInt8>::value) {
            return PrimitiveUInt8(value);
        } else if constexpr (std::is_same<T, IUInt16>::value) {
            return PrimitiveUInt16(value);
        } else if constexpr (std::is_same<T, IUInt32>::value) {
            return PrimitiveUInt32(value);
        } else if constexpr (std::is_same<T, IUInt64>::value) {
            return PrimitiveUInt64(value);
        } else if constexpr (std::is_same<T, IUIntNat>::value) {
            return PrimitiveUIntNat(value);
        } else if constexpr (std::is_same<T, IFloat16>::value) {
            return PrimitiveFloat16(value);
        } else if constexpr (std::is_same<T, IFloat32>::value) {
            return PrimitiveFloat32(value);
        } else if constexpr (std::is_same<T, IFloat64>::value) {
            return PrimitiveFloat64(value);
        } else if constexpr (std::is_same<T, IBool>::value) {
            return PrimitiveBool(value);
        } else if constexpr (std::is_same<T, IRune>::value) {
            return PrimitiveRune(value);
        } else {
            CJC_ABORT();
        }
    }

    /** @brief Create an interpreter primitive values of some Type with the content given in the argument */
    template <typename K> static IVal PrimitiveOfType(CHIR::Type& ty, K value)
    {
        switch (ty.GetTypeKind()) {
            case CHIR::Type::TypeKind::TYPE_UINT8:
                return PrimitiveValue<IUInt8, uint8_t>(value);
            case CHIR::Type::TypeKind::TYPE_UINT16:
                return PrimitiveValue<IUInt16, uint16_t>(value);
            case CHIR::Type::TypeKind::TYPE_UINT32:
                return PrimitiveValue<IUInt32, uint32_t>(value);
            case CHIR::Type::TypeKind::TYPE_ENUM:
            case CHIR::Type::TypeKind::TYPE_UINT64:
                return PrimitiveValue<IUInt64, uint64_t>(value);
            case CHIR::Type::TypeKind::TYPE_UINT_NATIVE:
                return PrimitiveValue<IUIntNat, size_t>(value);
            case CHIR::Type::TypeKind::TYPE_INT8:
                return PrimitiveValue<IInt8, int8_t>(value);
            case CHIR::Type::TypeKind::TYPE_INT16:
                return PrimitiveValue<IInt16, int16_t>(value);
            case CHIR::Type::TypeKind::TYPE_INT32:
                return PrimitiveValue<IInt32, int32_t>(value);
            case CHIR::Type::TypeKind::TYPE_INT64:
                return PrimitiveValue<IInt64, int64_t>(value);
            case CHIR::Type::TypeKind::TYPE_INT_NATIVE:
#if (defined(__x86_64__) || defined(__aarch64__))
                return PrimitiveValue<IIntNat, int64_t>(value);
#else
                return PrimitiveValue<IIntNat, int32_t>(value);
#endif
            case CHIR::Type::TypeKind::TYPE_FLOAT16:
                return PrimitiveValue<IFloat16, float>(value);
            case CHIR::Type::TypeKind::TYPE_FLOAT32:
                return PrimitiveValue<IFloat32, float>(value);
            case CHIR::Type::TypeKind::TYPE_FLOAT64:
                return PrimitiveValue<IFloat64, double>(value);
            default: {
                CJC_ABORT();
            }
        }
        return INullptr();
    }

    static ITuple CreateCPointer(size_t ptr)
    {
        return ITuple{{IValUtils::PrimitiveValue<IUIntNat>(ptr)}};
    }

    /**
     * @brief Creates an array representation of a string, as used on Cangjie's core
     */
    static IArray StringToArray(const std::string& str)
    {
        auto array = IArray();
        array.content.reserve(str.size() + 1);
        array.content.emplace_back(IValUtils::PrimitiveValue<IInt64>(static_cast<int64_t>(str.size())));
        for (auto& c : str) {
            array.content.emplace_back(IValUtils::PrimitiveValue<IUInt8>(static_cast<const uint8_t>(c)));
        }
        return array;
    }

    /** @brief returns the number of bits of an IVal */
    template <typename T> static size_t SizeOf()
    {
        if constexpr (std::is_same<T, IInt8>::value || std::is_same<T, IUInt8>::value) {
            return sizeof(uint8_t) * CHAR_BIT;
        } else if constexpr (std::is_same<T, IInt16>::value || std::is_same<T, IUInt16>::value) {
            return sizeof(uint16_t) * CHAR_BIT;
        } else if constexpr (std::is_same<T, IInt32>::value || std::is_same<T, IUInt32>::value) {
            return sizeof(uint32_t) * CHAR_BIT;
        } else if constexpr (std::is_same<T, IInt64>::value || std::is_same<T, IUInt64>::value) {
            return sizeof(uint64_t) * CHAR_BIT;
        } else if constexpr (std::is_same<T, IIntNat>::value || std::is_same<T, IUIntNat>::value) {
            return sizeof(size_t) * CHAR_BIT;
        } else {
            CJC_ABORT();
        }
    }

private:

    /** @brief Auxiliary printing function */
    static void PrintVector(const std::vector<IVal>& vec, std::ostream& os);
    /** @brief Auxiliary printing function */
    static void PrintNonNumeric(const IVal& v, std::ostream& os);

    inline static IInt8 PrimitiveInt8(int8_t value)
    {
        return IInt8{value};
    }

    inline static IInt16 PrimitiveInt16(int16_t value)
    {
        return IInt16{value};
    }

    inline static IInt32 PrimitiveInt32(int32_t value)
    {
        return IInt32{value};
    }

    inline static IInt64 PrimitiveInt64(int64_t value)
    {
        return IInt64{value};
    }

#if (defined(__x86_64__) || defined(__aarch64__))
    inline static IIntNat PrimitiveIntNat(int64_t value)
#else
    inline static IIntNat PrimitiveIntNat(int32_t value)
#endif
    {
        return IIntNat{value};
    }

    inline static IUInt8 PrimitiveUInt8(uint8_t value)
    {
        return IUInt8{value};
    }

    inline static IUInt16 PrimitiveUInt16(uint16_t value)
    {
        return IUInt16{value};
    }

    inline static IUInt32 PrimitiveUInt32(uint32_t value)
    {
        return IUInt32{value};
    }

    inline static IUInt64 PrimitiveUInt64(uint64_t value)
    {
        return IUInt64{value};
    }

    inline static IUIntNat PrimitiveUIntNat(size_t value)
    {
        return IUIntNat{value};
    }

    inline static IFloat16 PrimitiveFloat16(float value)
    {
        return IFloat16{value};
    }

    inline static IFloat32 PrimitiveFloat32(float value)
    {
        return IFloat32{value};
    }

    inline static IFloat64 PrimitiveFloat64(double value)
    {
        return IFloat64{value};
    }

    inline static IBool PrimitiveBool(bool value)
    {
        return IBool{value};
    }

    inline static IRune PrimitiveRune(char32_t value)
    {
        return IRune{value};
    }
};

} // namespace Cangjie::CHIR::Interpreter

#endif // CANGJIE_CHIR_INTERRETER_INTERPREVERVALUE_UTILS_H