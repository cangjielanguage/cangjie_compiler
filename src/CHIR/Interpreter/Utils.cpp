// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements some utility functions for interpreter module.
 */

#include "cangjie/CHIR/Interpreter/Utils.h"

#include <securec.h>
#include "cangjie/CHIR/Interpreter/InterpreterValueUtils.h"

using namespace Cangjie::CHIR;
using namespace Cangjie::CHIR::Interpreter;

OpCode Interpreter::PrimitiveTypeKind2OpCode(Type::TypeKind kind)
{
    switch (kind) {
        case Type::TypeKind::TYPE_INT64:
            return OpCode::INT64;
        case Type::TypeKind::TYPE_INT32:
            return OpCode::INT32;
        case Type::TypeKind::TYPE_INT16:
            return OpCode::INT16;
        case Type::TypeKind::TYPE_INT8:
            return OpCode::INT8;
        case Type::TypeKind::TYPE_INT_NATIVE:
            return OpCode::INTNAT;
        case Type::TypeKind::TYPE_UINT64:
            return OpCode::UINT64;
        case Type::TypeKind::TYPE_UINT32:
            return OpCode::UINT32;
        case Type::TypeKind::TYPE_UINT16:
            return OpCode::UINT16;
        case Type::TypeKind::TYPE_UINT8:
            return OpCode::UINT8;
        case Type::TypeKind::TYPE_UINT_NATIVE:
            return OpCode::UINTNAT;
        case Type::TypeKind::TYPE_FLOAT64:
            return OpCode::FLOAT64;
        case Type::TypeKind::TYPE_FLOAT32:
            return OpCode::FLOAT32;
        case Type::TypeKind::TYPE_FLOAT16:
            return OpCode::FLOAT16;
        default:
            CJC_ASSERT(false);
            return OpCode::INVALID;
    }
}

OpCode Interpreter::UnExprKind2OpCode(Cangjie::CHIR::ExprKind exprKind)
{
    switch (exprKind) {
        case ExprKind::NEG:
            return OpCode::UN_NEG;
        case ExprKind::NOT:
            return OpCode::UN_NOT;
        case ExprKind::BITNOT:
            return OpCode::UN_BITNOT;
        default: {
            CJC_ASSERT(false);
            return OpCode::INVALID;
        }
    }
}

OpCode Interpreter::BinExprKind2OpCode(Cangjie::CHIR::ExprKind exprKind)
{
    switch (exprKind) {
        case ExprKind::ADD:
            return OpCode::BIN_ADD;
        case ExprKind::SUB:
            return OpCode::BIN_SUB;
        case ExprKind::MUL:
            return OpCode::BIN_MUL;
        case ExprKind::DIV:
            return OpCode::BIN_DIV;
        case ExprKind::MOD:
            return OpCode::BIN_MOD;
        case ExprKind::LSHIFT:
            return OpCode::BIN_LSHIFT;
        case ExprKind::RSHIFT:
            return OpCode::BIN_RSHIFT;
        case ExprKind::BITAND:
            return OpCode::BIN_BITAND;
        case ExprKind::BITOR:
            return OpCode::BIN_BITOR;
        case ExprKind::BITXOR:
            return OpCode::BIN_BITXOR;
        case ExprKind::LT:
            return OpCode::BIN_LT;
        case ExprKind::GT:
            return OpCode::BIN_GT;
        case ExprKind::LE:
            return OpCode::BIN_LE;
        case ExprKind::GE:
            return OpCode::BIN_GE;
        case ExprKind::EQUAL:
            return OpCode::BIN_EQUAL;
        case ExprKind::NOTEQUAL:
            return OpCode::BIN_NOTEQ;
        case ExprKind::EXP:
            return OpCode::BIN_EXP;
        case ExprKind::AND:
        case ExprKind::OR:
            // should have already been desugared at this point
            // missing break/return intended
        default: {
            CJC_ASSERT(false);
            return OpCode::INVALID;
        }
    }
}

OpCode Interpreter::BinExprKindWitException2OpCode(Cangjie::CHIR::ExprKind exprKind)
{
    switch (exprKind) {
        case ExprKind::ADD:
            return OpCode::BIN_ADD_EXC;
        case ExprKind::SUB:
            return OpCode::BIN_SUB_EXC;
        case ExprKind::MUL:
            return OpCode::BIN_MUL_EXC;
        case ExprKind::DIV:
            return OpCode::BIN_DIV_EXC;
        case ExprKind::MOD:
            return OpCode::BIN_MOD_EXC;
        case ExprKind::EXP:
            return OpCode::BIN_EXP_EXC;
        case ExprKind::LSHIFT:
            return OpCode::BIN_LSHIFT_EXC;
        case ExprKind::RSHIFT:
            return OpCode::BIN_RSHIFT_EXC;
        case ExprKind::NEG:
            return OpCode::UN_NEG_EXC;
        default: {
            CJC_ASSERT(false);
            return OpCode::INVALID;
        }
    }
}

IVal Interpreter::ByteCodeToIval(const Bchir::Definition& def, const Bchir& bchir, Bchir& topBchir)
{
    switch (static_cast<OpCode>(def.Get(0))) {
        case OpCode::UINT8:
            return {IValUtils::PrimitiveValue<IUInt8>(static_cast<uint8_t>(def.Get(1)))};
        case OpCode::UINT16:
            return {IValUtils::PrimitiveValue<IUInt16>(static_cast<uint16_t>(def.Get(1)))};
        case OpCode::UINT32:
            return {IValUtils::PrimitiveValue<IUInt32>(static_cast<uint32_t>(def.Get(1)))};
        case OpCode::UINT64:
            return {IValUtils::PrimitiveValue<IUInt64>(static_cast<uint64_t>(def.Get8bytes(1)))};
        case OpCode::UINTNAT:
#if (defined(__x86_64__) || defined(__aarch64__))
            return {IValUtils::PrimitiveValue<IUIntNat>(static_cast<uint64_t>(def.Get8bytes(1)))};
#else
            return {IValUtils::PrimitiveValue<IUIntNat>(static_cast<uint32_t>(def.Get8bytes(1)))};
#endif
        case OpCode::INT8:
            return {IValUtils::PrimitiveValue<IInt8>(static_cast<int8_t>(def.Get(1)))};
        case OpCode::INT16:
            return {IValUtils::PrimitiveValue<IInt16>(static_cast<int16_t>(def.Get(1)))};
        case OpCode::INT32:
            return {IValUtils::PrimitiveValue<IInt32>(static_cast<int32_t>(def.Get(1)))};
        case OpCode::INT64:
            return {IValUtils::PrimitiveValue<IInt64>(static_cast<int64_t>(def.Get8bytes(1)))};
        case OpCode::INTNAT:
#if (defined(__x86_64__) || defined(__aarch64__))
            return {IValUtils::PrimitiveValue<IIntNat>(static_cast<int64_t>(def.Get8bytes(1)))};
#else
            return {IValUtils::PrimitiveValue<IIntNat>(static_cast<int32_t>(def.Get8bytes(1)))};
#endif
        case OpCode::FLOAT16:
            return {IValUtils::PrimitiveValue<IFloat16>(static_cast<float>(def.Get(1)))};
        case OpCode::FLOAT32:
            return {IValUtils::PrimitiveValue<IFloat32>(static_cast<float>(def.Get(1)))};
        case OpCode::FLOAT64: {
            auto tmp = def.Get8bytes(1);
            double d;
            auto ret = memcpy_s(&d, sizeof(double), &tmp, sizeof(double));
            if (ret != EOK) {
                CJC_ASSERT(false);
                return {INullptr()};
            }
            return {IValUtils::PrimitiveValue<IFloat64>(d)};
        }
        case OpCode::RUNE:
            return {IValUtils::PrimitiveValue<IRune>(def.Get(1))};
        case OpCode::BOOL:
            return {IValUtils::PrimitiveValue<IBool>(def.Get(1))};
        case OpCode::NULLPTR:
            return {INullptr()};
        case OpCode::STRING: {
            // string values in the interpreter need to match the definition in core
            /* OPTIMIZE:
            Instead of having a section of std::strings literals and converting them to match
            core library RawArray<UInt8> during interpretation, we can have a section of
            IArray strings, and during interpretation we just create an IPtr pointing to the
            corresponding IArray string. This is correct because strings are immutable in CJ.
            Also, the Unicode size of the string should be calculated on translation */
            auto strIdx = def.Get(1);
            auto& str = bchir.GetString(strIdx);
            auto array = topBchir.StoreStringArray(IValUtils::StringToArray(str));
            auto ptr = IPointer();
            ptr.content = array;

            auto tuple = ITuple{{ptr, IValUtils::PrimitiveValue<IUInt32>(uint32_t(0)),
                IValUtils::PrimitiveValue<IUInt32>(static_cast<uint32_t>(str.size()))}};
            return {tuple};
        }
        default:
            CJC_ASSERT(false);
            return {INullptr()};
    }
}
