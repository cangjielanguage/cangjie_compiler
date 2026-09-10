// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements type casting for CHIR related nodes.
 */

#ifndef CANGJIE_UTILS_CHIR_CASTING_H
#define CANGJIE_UTILS_CHIR_CASTING_H

#include <type_traits>

#include "cangjie/CHIR/IR/Value/Value.h"
#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/IR/Type/CustomTypeDef.h"
#include "cangjie/CHIR/IR/Type/EnumDef.h"
#include "cangjie/CHIR/IR/Type/ExtendDef.h"
#include "cangjie/CHIR/IR/Type/StructDef.h"
#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/Utils/CastingTemplate.h"

namespace Cangjie {
// Type.h
DEFINE_NODE_TYPE_KIND(CHIR::RuneType, CHIR::Type::TypeKind::TYPE_RUNE);
DEFINE_NODE_TYPE_KIND(CHIR::BooleanType, CHIR::Type::TypeKind::TYPE_BOOLEAN);
DEFINE_NODE_TYPE_KIND(CHIR::UnitType, CHIR::Type::TypeKind::TYPE_UNIT);
DEFINE_NODE_TYPE_KIND(CHIR::NothingType, CHIR::Type::TypeKind::TYPE_NOTHING);
DEFINE_NODE_TYPE_KIND(CHIR::VoidType, CHIR::Type::TypeKind::TYPE_VOID);
DEFINE_NODE_TYPE_KIND(CHIR::TupleType, CHIR::Type::TypeKind::TYPE_TUPLE);
DEFINE_NODE_TYPE_KIND(CHIR::StructType, CHIR::Type::TypeKind::TYPE_STRUCT);
DEFINE_NODE_TYPE_KIND(CHIR::EnumType, CHIR::Type::TypeKind::TYPE_ENUM);
DEFINE_NODE_TYPE_KIND(CHIR::FuncType, CHIR::Type::TypeKind::TYPE_FUNC);
DEFINE_NODE_TYPE_KIND(CHIR::ClassType, CHIR::Type::TypeKind::TYPE_CLASS);
DEFINE_NODE_TYPE_KIND(CHIR::RawArrayType, CHIR::Type::TypeKind::TYPE_RAWARRAY);
DEFINE_NODE_TYPE_KIND(CHIR::VArrayType, CHIR::Type::TypeKind::TYPE_VARRAY);
DEFINE_NODE_TYPE_KIND(CHIR::CPointerType, CHIR::Type::TypeKind::TYPE_CPOINTER);
DEFINE_NODE_TYPE_KIND(CHIR::CStringType, CHIR::Type::TypeKind::TYPE_CSTRING);
DEFINE_NODE_TYPE_KIND(CHIR::GenericType, CHIR::Type::TypeKind::TYPE_GENERIC);
DEFINE_NODE_TYPE_KIND(CHIR::RefType, CHIR::Type::TypeKind::TYPE_REFTYPE);
DEFINE_NODE_TYPE_KIND(CHIR::BoxType, CHIR::Type::TypeKind::TYPE_BOXTYPE);
DEFINE_NODE_TYPE_KIND(CHIR::ThisType, CHIR::Type::TypeKind::TYPE_THIS);
// Expression.h
DEFINE_NODE_TYPE_KIND(CHIR::Constant, CHIR::ExprKind::CONSTANT);
DEFINE_NODE_TYPE_KIND(CHIR::Allocate, CHIR::ExprKind::ALLOCATE);
DEFINE_NODE_TYPE_KIND(CHIR::Load, CHIR::ExprKind::LOAD);
DEFINE_NODE_TYPE_KIND(CHIR::Store, CHIR::ExprKind::STORE);
DEFINE_NODE_TYPE_KIND(CHIR::GetElementRef, CHIR::ExprKind::GET_ELEMENT_REF);
DEFINE_NODE_TYPE_KIND(CHIR::GetElementByName, CHIR::ExprKind::GET_ELEMENT_BY_NAME);
DEFINE_NODE_TYPE_KIND(CHIR::StoreElementRef, CHIR::ExprKind::STORE_ELEMENT_REF);
DEFINE_NODE_TYPE_KIND(CHIR::StoreElementByName, CHIR::ExprKind::STORE_ELEMENT_BY_NAME);
DEFINE_NODE_TYPE_KIND(CHIR::Apply, CHIR::ExprKind::APPLY);
DEFINE_NODE_TYPE_KIND(CHIR::Invoke, CHIR::ExprKind::INVOKE);
DEFINE_NODE_TYPE_KIND(CHIR::InvokeStatic, CHIR::ExprKind::INVOKESTATIC);
DEFINE_NODE_TYPE_KIND(CHIR::ClassStaticCast, CHIR::ExprKind::CLASS_STATIC_CAST);
DEFINE_NODE_TYPE_KIND(CHIR::NumericCast, CHIR::ExprKind::NUMERIC_CAST);
DEFINE_NODE_TYPE_KIND(CHIR::TryNumericCast, CHIR::ExprKind::TRY_NUMERIC_CAST);
DEFINE_NODE_TYPE_KIND(CHIR::InstanceOf, CHIR::ExprKind::INSTANCEOF);
DEFINE_NODE_TYPE_KIND(CHIR::GoTo, CHIR::ExprKind::GOTO);
DEFINE_NODE_TYPE_KIND(CHIR::Branch, CHIR::ExprKind::BRANCH);
DEFINE_NODE_TYPE_KIND(CHIR::MultiBranch, CHIR::ExprKind::MULTIBRANCH);
DEFINE_NODE_TYPE_KIND(CHIR::Exclave, CHIR::ExprKind::EXCLAVE);
DEFINE_NODE_TYPE_KIND(CHIR::Exit, CHIR::ExprKind::EXIT);
DEFINE_NODE_TYPE_KIND(CHIR::RaiseException, CHIR::ExprKind::RAISE_EXCEPTION);
DEFINE_NODE_TYPE_KIND(CHIR::TryApply, CHIR::ExprKind::TRY_APPLY);
DEFINE_NODE_TYPE_KIND(CHIR::TryInvoke, CHIR::ExprKind::TRY_INVOKE);
DEFINE_NODE_TYPE_KIND(CHIR::TryInvokeStatic, CHIR::ExprKind::TRY_INVOKESTATIC);
DEFINE_NODE_TYPE_KIND(CHIR::TryUnaryExpression, CHIR::ExprKind::TRY_NEG);
DEFINE_NODE_TYPE_KIND(CHIR::TryIntrinsic, CHIR::ExprKind::TRY_INTRINSIC);
DEFINE_NODE_TYPE_KIND(CHIR::TryAllocate, CHIR::ExprKind::TRY_ALLOCATE);
DEFINE_NODE_TYPE_KIND(CHIR::TryRawArrayAllocate, CHIR::ExprKind::TRY_RAW_ARRAY_ALLOCATE);
DEFINE_NODE_TYPE_KIND(CHIR::TrySpawn, CHIR::ExprKind::TRY_SPAWN);
DEFINE_NODE_TYPE_KIND(CHIR::Tuple, CHIR::ExprKind::TUPLE);
DEFINE_NODE_TYPE_KIND(CHIR::Field, CHIR::ExprKind::FIELD);
DEFINE_NODE_TYPE_KIND(CHIR::FieldByName, CHIR::ExprKind::FIELD_BY_NAME);
DEFINE_NODE_TYPE_KIND(CHIR::RawArrayAllocate, CHIR::ExprKind::RAW_ARRAY_ALLOCATE);
DEFINE_NODE_TYPE_KIND(CHIR::RawArrayLiteralInit, CHIR::ExprKind::RAW_ARRAY_LITERAL_INIT);
DEFINE_NODE_TYPE_KIND(CHIR::RawArrayInitByValue, CHIR::ExprKind::RAW_ARRAY_INIT_BY_VALUE);
DEFINE_NODE_TYPE_KIND(CHIR::VArray, CHIR::ExprKind::VARRAY);
DEFINE_NODE_TYPE_KIND(CHIR::VArrayBuilder, CHIR::ExprKind::VARRAY_BUILDER);
DEFINE_NODE_TYPE_KIND(CHIR::GetException, CHIR::ExprKind::GET_EXCEPTION);
DEFINE_NODE_TYPE_KIND(CHIR::Intrinsic, CHIR::ExprKind::INTRINSIC);
DEFINE_NODE_TYPE_KIND(CHIR::ForInRange, CHIR::ExprKind::FORIN_RANGE);
DEFINE_NODE_TYPE_KIND(CHIR::ForInIter, CHIR::ExprKind::FORIN_ITER);
DEFINE_NODE_TYPE_KIND(CHIR::ForInClosedRange, CHIR::ExprKind::FORIN_CLOSED_RANGE);
DEFINE_NODE_TYPE_KIND(CHIR::Lambda, CHIR::ExprKind::LAMBDA);
DEFINE_NODE_TYPE_KIND(CHIR::Debug, CHIR::ExprKind::DEBUGEXPR);
DEFINE_NODE_TYPE_KIND(CHIR::Spawn, CHIR::ExprKind::SPAWN);
DEFINE_NODE_TYPE_KIND(CHIR::GetInstantiateValue, CHIR::ExprKind::GET_INSTANTIATE_VALUE);
DEFINE_NODE_TYPE_KIND(CHIR::Box, CHIR::ExprKind::BOX);
DEFINE_NODE_TYPE_KIND(CHIR::UnBoxToValue, CHIR::ExprKind::UNBOX_TO_VALUE);
DEFINE_NODE_TYPE_KIND(CHIR::GetRTTI, CHIR::ExprKind::GET_RTTI);
DEFINE_NODE_TYPE_KIND(CHIR::GetRTTIStatic, CHIR::ExprKind::GET_RTTI_STATIC);
DEFINE_NODE_TYPE_KIND(CHIR::StartRegion, CHIR::ExprKind::START_REGION);
DEFINE_NODE_TYPE_KIND(CHIR::EndRegion, CHIR::ExprKind::END_REGION);

// CustomTypeDef.h: class, enum, and struct.
DEFINE_NODE_TYPE_KIND(CHIR::ClassDef, CHIR::CustomDefKind::TYPE_CLASS);
DEFINE_NODE_TYPE_KIND(CHIR::EnumDef, CHIR::CustomDefKind::TYPE_ENUM);
DEFINE_NODE_TYPE_KIND(CHIR::StructDef, CHIR::CustomDefKind::TYPE_STRUCT);
DEFINE_NODE_TYPE_KIND(CHIR::ExtendDef, CHIR::CustomDefKind::TYPE_EXTEND);

// Defined the mono type checking method for CHIRNode.
template <> struct TypeAs<CHIR::Function> {
    static bool IsInstanceOf(const CHIR::Value& value)
    {
        return value.IsFunc();
    }
};

template <> struct TypeAs<CHIR::Block> {
    static bool IsInstanceOf(const CHIR::Value& value)
    {
        return value.IsBlock();
    }
};

template <> struct TypeAs<CHIR::BlockGroup> {
    static bool IsInstanceOf(const CHIR::Value& value)
    {
        return value.IsBlockGroup();
    }
};

template <> struct TypeAs<CHIR::LiteralValue> {
    static bool IsInstanceOf(const CHIR::Value& value)
    {
        return value.IsLiteral();
    }
};

template <> struct TypeAs<CHIR::BoolLiteral> {
    static bool IsInstanceOf(const CHIR::Value& value)
    {
        return value.IsLiteral() && static_cast<const CHIR::LiteralValue&>(value).IsBoolLiteral();
    }
};

template <> struct TypeAs<CHIR::RuneLiteral> {
    static bool IsInstanceOf(const CHIR::Value& value)
    {
        return value.IsLiteral() && static_cast<const CHIR::LiteralValue&>(value).IsRuneLiteral();
    }
};

template <> struct TypeAs<CHIR::StringLiteral> {
    static bool IsInstanceOf(const CHIR::Value& value)
    {
        return value.IsLiteral() && static_cast<const CHIR::LiteralValue&>(value).IsStringLiteral();
    }
};

template <> struct TypeAs<CHIR::IntLiteral> {
    static bool IsInstanceOf(const CHIR::Value& value)
    {
        return value.IsLiteral() && static_cast<const CHIR::LiteralValue&>(value).IsIntLiteral();
    }
};

template <> struct TypeAs<CHIR::FloatLiteral> {
    static bool IsInstanceOf(const CHIR::Value& value)
    {
        return value.IsLiteral() && static_cast<const CHIR::LiteralValue&>(value).IsFloatLiteral();
    }
};

template <> struct TypeAs<CHIR::UnitLiteral> {
    static bool IsInstanceOf(const CHIR::Value& value)
    {
        return value.IsLiteral() && static_cast<const CHIR::LiteralValue&>(value).IsUnitLiteral();
    }
};

template <> struct TypeAs<CHIR::NullLiteral> {
    static bool IsInstanceOf(const CHIR::Value& value)
    {
        return value.IsLiteral() && static_cast<const CHIR::LiteralValue&>(value).IsNullLiteral();
    }
};

template <> struct TypeAs<CHIR::LocalVar> {
    static bool IsInstanceOf(const CHIR::Value& value)
    {
        return value.IsLocalVar();
    }
};

template <> struct TypeAs<CHIR::Parameter> {
    static bool IsInstanceOf(const CHIR::Value& value)
    {
        return value.IsParameter();
    }
};

template <> struct TypeAs<CHIR::GlobalVar> {
    static bool IsInstanceOf(const CHIR::Value& value)
    {
        return value.IsGlobalVar();
    }
};

template <typename To> struct TypeAs<To, std::enable_if_t<std::is_base_of_v<CHIR::Type, To>>> {
    static inline bool IsInstanceOf(const CHIR::Type& node)
    {
        return node.GetTypeKind() == NodeType<To>::kind;
    }
};

template <> struct TypeAs<CHIR::BuiltinType> {
    static bool IsInstanceOf(const CHIR::Type& node)
    {
        return node.IsBuiltinType();
    }
};

template <> struct TypeAs<CHIR::CustomType> {
    static inline bool IsInstanceOf(const CHIR::Type& node)
    {
        return node.GetTypeKind() == CHIR::Type::TypeKind::TYPE_CLASS ||
            node.GetTypeKind() == CHIR::Type::TypeKind::TYPE_STRUCT ||
            node.GetTypeKind() == CHIR::Type::TypeKind::TYPE_ENUM;
    }
};

template <> struct TypeAs<CHIR::NumericType> {
    static inline bool IsInstanceOf(const CHIR::Type& node)
    {
        return node.GetTypeKind() >= CHIR::Type::TypeKind::TYPE_INT8 &&
            node.GetTypeKind() <= CHIR::Type::TypeKind::TYPE_FLOAT64;
    }
};

template <> struct TypeAs<CHIR::FloatType> {
    static inline bool IsInstanceOf(const CHIR::Type& node)
    {
        return node.GetTypeKind() >= CHIR::Type::TypeKind::TYPE_FLOAT16 &&
            node.GetTypeKind() <= CHIR::Type::TypeKind::TYPE_FLOAT64;
    }
};

template <> struct TypeAs<CHIR::IntType> {
    static inline bool IsInstanceOf(const CHIR::Type& node)
    {
        return node.GetTypeKind() >= CHIR::Type::TypeKind::TYPE_INT8 &&
            node.GetTypeKind() <= CHIR::Type::TypeKind::TYPE_UINT_NATIVE;
    }
};

template <typename To>
using ExprImplSeparately = std::enable_if_t<std::is_base_of_v<CHIR::Expression, To> &&
    ShouldInstantiate<To, CHIR::UnaryExpression, CHIR::BinaryExpression, CHIR::SpawnBase,
        CHIR::AllocateBase, CHIR::RawArrayAllocateBase, CHIR::TypeCast, CHIR::NumericCastBase,
        CHIR::UnaryExpressionBase, CHIR::BinaryExpressionBase, CHIR::TryBinaryExpression,
        CHIR::FuncCall, CHIR::ApplyBase, CHIR::DynamicDispatch, CHIR::InvokeBase,
        CHIR::InvokeStaticBase, CHIR::IntrinsicBase>::value>;

template <typename To> struct TypeAs<To, ExprImplSeparately<To>> {
    static inline bool IsInstanceOf(const CHIR::Expression& node)
    {
        return node.GetExprKind() == NodeType<To>::kind;
    }
};

template <> struct TypeAs<CHIR::TypeCast> {
    static inline bool IsInstanceOf(const CHIR::Expression& node)
    {
        auto kind = node.GetExprKind();
        return kind == CHIR::ExprKind::BOX || kind == CHIR::ExprKind::UNBOX_TO_VALUE ||
            kind == CHIR::ExprKind::UNBOX_TO_REF || kind == CHIR::ExprKind::CLASS_STATIC_CAST ||
            kind == CHIR::ExprKind::NUMERIC_CAST || kind == CHIR::ExprKind::TRY_NUMERIC_CAST ||
            kind == CHIR::ExprKind::CAST_TO_CONCRETE || kind == CHIR::ExprKind::CAST_TO_GENERIC;
    }
};

template <> struct TypeAs<CHIR::NumericCastBase> {
    static inline bool IsInstanceOf(const CHIR::Expression& node)
    {
        return node.GetExprKind() == CHIR::ExprKind::NUMERIC_CAST ||
            node.GetExprKind() == CHIR::ExprKind::TRY_NUMERIC_CAST;
    }
};

template <> struct TypeAs<CHIR::ApplyBase> {
    static inline bool IsInstanceOf(const CHIR::Expression& node)
    {
        return node.GetExprKind() == CHIR::ExprKind::APPLY ||
            node.GetExprKind() == CHIR::ExprKind::TRY_APPLY;
    }
};

template <> struct TypeAs<CHIR::InvokeBase> {
    static inline bool IsInstanceOf(const CHIR::Expression& node)
    {
        return node.GetExprKind() == CHIR::ExprKind::INVOKE ||
            node.GetExprKind() == CHIR::ExprKind::TRY_INVOKE;
    }
};

template <> struct TypeAs<CHIR::InvokeStaticBase> {
    static inline bool IsInstanceOf(const CHIR::Expression& node)
    {
        return node.GetExprKind() == CHIR::ExprKind::INVOKESTATIC ||
            node.GetExprKind() == CHIR::ExprKind::TRY_INVOKESTATIC;
    }
};

template <> struct TypeAs<CHIR::DynamicDispatch> {
    static inline bool IsInstanceOf(const CHIR::Expression& node)
    {
        return TypeAs<CHIR::InvokeBase>::IsInstanceOf(node) ||
            TypeAs<CHIR::InvokeStaticBase>::IsInstanceOf(node);
    }
};

template <> struct TypeAs<CHIR::IntrinsicBase> {
    static inline bool IsInstanceOf(const CHIR::Expression& node)
    {
        return node.GetExprKind() == CHIR::ExprKind::INTRINSIC ||
            node.GetExprKind() == CHIR::ExprKind::TRY_INTRINSIC;
    }
};

template <> struct TypeAs<CHIR::FuncCall> {
    static inline bool IsInstanceOf(const CHIR::Expression& node)
    {
        return TypeAs<CHIR::ApplyBase>::IsInstanceOf(node) ||
            TypeAs<CHIR::DynamicDispatch>::IsInstanceOf(node) ||
            TypeAs<CHIR::IntrinsicBase>::IsInstanceOf(node);
    }
};

template <> struct TypeAs<CHIR::SpawnBase> {
    static inline bool IsInstanceOf(const CHIR::Expression& node)
    {
        return node.GetExprKind() == CHIR::ExprKind::SPAWN ||
            node.GetExprKind() == CHIR::ExprKind::TRY_SPAWN;
    }
};

template <> struct TypeAs<CHIR::AllocateBase> {
    static inline bool IsInstanceOf(const CHIR::Expression& node)
    {
        return node.GetExprKind() == CHIR::ExprKind::ALLOCATE ||
            node.GetExprKind() == CHIR::ExprKind::TRY_ALLOCATE;
    }
};

template <> struct TypeAs<CHIR::RawArrayAllocateBase> {
    static inline bool IsInstanceOf(const CHIR::Expression& node)
    {
        return node.GetExprKind() == CHIR::ExprKind::RAW_ARRAY_ALLOCATE ||
            node.GetExprKind() == CHIR::ExprKind::TRY_RAW_ARRAY_ALLOCATE;
    }
};

template <> struct TypeAs<CHIR::UnaryExpression> {
    static inline bool IsInstanceOf(const CHIR::Expression& node)
    {
        auto kind = node.GetExprKind();
        return kind >= CHIR::ExprKind::NEG && kind <= CHIR::ExprKind::BITNOT;
    }
};

template <> struct TypeAs<CHIR::BinaryExpression> {
    static inline bool IsInstanceOf(const CHIR::Expression& node)
    {
        auto kind = node.GetExprKind();
        return kind >= CHIR::ExprKind::ADD && kind <= CHIR::ExprKind::OR;
    }
};

template <> struct TypeAs<CHIR::TryBinaryExpression> {
    static inline bool IsInstanceOf(const CHIR::Expression& node)
    {
        auto kind = node.GetExprKind();
        return kind >= CHIR::ExprKind::TRY_ADD && kind <= CHIR::ExprKind::TRY_RSHIFT;
    }
};

template <> struct TypeAs<CHIR::UnaryExpressionBase> {
    static inline bool IsInstanceOf(const CHIR::Expression& node)
    {
        return TypeAs<CHIR::UnaryExpression>::IsInstanceOf(node) ||
            node.GetExprKind() == CHIR::ExprKind::TRY_NEG;
    }
};

template <> struct TypeAs<CHIR::BinaryExpressionBase> {
    static inline bool IsInstanceOf(const CHIR::Expression& node)
    {
        return TypeAs<CHIR::BinaryExpression>::IsInstanceOf(node) ||
            TypeAs<CHIR::TryBinaryExpression>::IsInstanceOf(node);
    }
};

template <> struct TypeAs<CHIR::ForIn> {
    static bool IsInstanceOf(const CHIR::Expression& node)
    {
        auto kind = node.GetExprKind();
        return kind >= CHIR::ExprKind::FORIN_RANGE && kind <= CHIR::ExprKind::FORIN_CLOSED_RANGE;
    }
};

template <typename To> struct TypeAs<To, std::enable_if_t<std::is_base_of_v<CHIR::CustomTypeDef, To>>> {
    static inline bool IsInstanceOf(const CHIR::CustomTypeDef& node)
    {
        return node.GetCustomKind() == NodeType<To>::kind;
    }
};
}
#endif
