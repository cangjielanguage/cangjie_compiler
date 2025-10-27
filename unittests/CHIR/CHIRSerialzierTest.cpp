// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <iostream>
#include <string>

#include <flatbuffers/PackageFormat_generated.h>

#include "gtest/gtest.h"

#include "cangjie/Basic/Print.h"
#include "cangjie/CHIR/CHIR.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/IntrinsicKind.h"
#include "cangjie/CHIR/Serializer/CHIRSerializer.h"
#include "cangjie/CHIR/Type/CustomTypeDef.h"
#include "cangjie/CHIR/Type/Type.h"

using namespace Cangjie::CHIR;

class CHIRSerialzierTest : public ::testing::Test {
protected:
    void SetUp() override
    {
    }
};

PackageFormat::CHIRTypeKind Serialize(const Type::TypeKind& kind)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::Type;
    auto ret = CHIRTypeKind_INVALID;
    switch (kind) {
        case Type::TypeKind::TYPE_INVALID:
            ret = CHIRTypeKind_INVALID;
            break;
        // integer
        case Type::TypeKind::TYPE_INT8:
            ret = CHIRTypeKind_INT8;
            break;
        case Type::TypeKind::TYPE_INT16:
            ret = CHIRTypeKind_INT16;
            break;
        case Type::TypeKind::TYPE_INT32:
            ret = CHIRTypeKind_INT32;
            break;
        case Type::TypeKind::TYPE_INT64:
            ret = CHIRTypeKind_INT64;
            break;
        case Type::TypeKind::TYPE_INT_NATIVE:
            ret = CHIRTypeKind_INT_NATIVE;
            break;
        // unsigned integer
        case Type::TypeKind::TYPE_UINT8:
            ret = CHIRTypeKind_UINT8;
            break;
        case Type::TypeKind::TYPE_UINT16:
            ret = CHIRTypeKind_UINT16;
            break;
        case Type::TypeKind::TYPE_UINT32:
            ret = CHIRTypeKind_UINT32;
            break;
        case Type::TypeKind::TYPE_UINT64:
            ret = CHIRTypeKind_UINT64;
            break;
        case Type::TypeKind::TYPE_UINT_NATIVE:
            ret = CHIRTypeKind_UINT_NATIVE;
            break;
        // float
        case Type::TypeKind::TYPE_FLOAT16:
            ret = CHIRTypeKind_FLOAT16;
            break;
        case Type::TypeKind::TYPE_FLOAT32:
            ret = CHIRTypeKind_FLOAT32;
            break;
        case Type::TypeKind::TYPE_FLOAT64:
            ret = CHIRTypeKind_FLOAT64;
            break;
        // other primitive type
        case Type::TypeKind::TYPE_RUNE:
            ret = CHIRTypeKind_RUNE;
            break;
        case Type::TypeKind::TYPE_BOOLEAN:
            ret = CHIRTypeKind_BOOLEAN;
            break;
        case Type::TypeKind::TYPE_UNIT:
            ret = CHIRTypeKind_UNIT;
            break;
        case Type::TypeKind::TYPE_NOTHING:
            ret = CHIRTypeKind_NOTHING;
            break;
        // Void type
        case Type::TypeKind::TYPE_VOID:
            ret = CHIRTypeKind_VOID;
            break;
        // composite type
        case Type::TypeKind::TYPE_TUPLE:
            ret = CHIRTypeKind_TUPLE;
            break;
        case Type::TypeKind::TYPE_STRUCT:
            ret = CHIRTypeKind_STRUCT;
            break;
        case Type::TypeKind::TYPE_ENUM:
            ret = CHIRTypeKind_ENUM;
            break;
        case Type::TypeKind::TYPE_FUNC:
            ret = CHIRTypeKind_FUNC;
            break;
        case Type::TypeKind::TYPE_CLASS:
            ret = CHIRTypeKind_CLASS;
            break;
        // Built-in array related type
        case Type::TypeKind::TYPE_RAWARRAY:
            ret = CHIRTypeKind_RAWARRAY;
            break;
        case Type::TypeKind::TYPE_VARRAY:
            ret = CHIRTypeKind_VARRAY;
            break;
        // Built-in CFFI related type
        case Type::TypeKind::TYPE_CPOINTER:
            ret = CHIRTypeKind_C_POINTER;
            break;
        case Type::TypeKind::TYPE_CSTRING:
            ret = CHIRTypeKind_C_STRING;
            break;
        // Generic type
        case Type::TypeKind::TYPE_GENERIC:
            ret = CHIRTypeKind_GENERIC;
            break;
        // Referece to an value with abritray type
        case Type::TypeKind::TYPE_REFTYPE:
            ret = CHIRTypeKind_REFTYPE;
            break;
        // Built-in box type
        case Type::TypeKind::TYPE_BOXTYPE:
            ret = CHIRTypeKind_BOXTYPE;
            break;
        case Type::TypeKind::TYPE_THIS:
            ret = CHIRTypeKind_THIS;
            break;
        case Type::TypeKind::MAX_TYPE_KIND:
            CJC_ABORT();
            break;
            // no defalut here, due to we need use compiler to check all value be handled.
    }
    return ret;
}

PackageFormat::SourceExpr Serialize(const SourceExpr& kind)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::SourceExpr;
    auto ret = SourceExpr_IF_EXPR;
    switch (kind) {
        case SourceExpr::IF_EXPR:
            ret = SourceExpr_IF_EXPR;
            break;
        case SourceExpr::WHILE_EXPR:
            ret = SourceExpr_WHILE_EXPR;
            break;
        case SourceExpr::DO_WHILE_EXPR:
            ret = SourceExpr_DO_WHILE_EXPR;
            break;
        case SourceExpr::MATCH_EXPR:
            ret = SourceExpr_MATCH_EXPR;
            break;
        case SourceExpr::IF_LET_OR_WHILE_LET:
            ret = SourceExpr_IF_LET_OR_WHILE_LET;
            break;
        case SourceExpr::QUEST:
            ret = SourceExpr_QUEST;
            break;
        case SourceExpr::BINARY:
            ret = SourceExpr_BINARY;
            break;
        case SourceExpr::FOR_IN_EXPR:
            ret = SourceExpr_FOR_IN_EXPR;
            break;
        case SourceExpr::OTHER:
            ret = SourceExpr_OTHER;
            break;
    }
    return ret;
}

PackageFormat::Linkage Serialize(const Cangjie::Linkage& kind)
{
    using namespace PackageFormat;
    using Cangjie::Linkage;
    auto ret = Linkage_WEAK_ODR;
    switch (kind) {
        case Linkage::WEAK_ODR:
            ret = Linkage_WEAK_ODR;
            break;
        case Linkage::EXTERNAL:
            ret = Linkage_EXTERNAL;
            break;
        case Linkage::INTERNAL:
            ret = Linkage_INTERNAL;
            break;
        case Linkage::LINKONCE_ODR:
            ret = Linkage_LINKONCE_ODR;
            break;
        case Linkage::EXTERNAL_WEAK:
            ret = Linkage_EXTERNAL_WEAK;
            break;
    }
    return ret;
}

PackageFormat::SkipKind Serialize(const Cangjie::CHIR::SkipKind& kind)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::SkipKind;
    auto ret = SkipKind_NO_SKIP;
    switch (kind) {
        case SkipKind::NO_SKIP:
            ret = SkipKind_NO_SKIP;
            break;
        case SkipKind::SKIP_DCE_WARNING:
            ret = SkipKind_SKIP_DCE_WARNING;
            break;
        case SkipKind::SKIP_FORIN_EXIT:
            ret = SkipKind_SKIP_FORIN_EXIT;
            break;
        case SkipKind::SKIP_VIC:
            ret = SkipKind_SKIP_VIC;
            break;
    }
    return ret;
}

PackageFormat::OverflowStrategy Serialize(const Cangjie::OverflowStrategy& kind)
{
    using namespace PackageFormat;
    using Cangjie::OverflowStrategy;
    auto ret = OverflowStrategy_NA;
    switch (kind) {
        case OverflowStrategy::NA:
            ret = OverflowStrategy_NA;
            break;
        case OverflowStrategy::CHECKED:
            ret = OverflowStrategy_CHECKED;
            break;
        case OverflowStrategy::WRAPPING:
            ret = OverflowStrategy_WRAPPING;
            break;
        case OverflowStrategy::THROWING:
            ret = OverflowStrategy_THROWING;
            break;
        case OverflowStrategy::SATURATING:
            ret = OverflowStrategy_SATURATING;
            break;
        case OverflowStrategy::OVERFLOW_STRATEGY_END:
            CJC_ABORT();
            break;
    }
    return ret;
}

PackageFormat::ValueKind Serialize(const Value::ValueKind& kind)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::Value;
    auto ret = ValueKind_LITERAL;
    switch (kind) {
        case Value::ValueKind::KIND_LITERAL:
            ret = ValueKind_LITERAL;
            break;
        case Value::ValueKind::KIND_GLOBALVAR:
            ret = ValueKind_GLOBALVAR;
            break;
        case Value::ValueKind::KIND_PARAMETER:
            ret = ValueKind_PARAMETER;
            break;
        case Value::ValueKind::KIND_IMP_FUNC:
            ret = ValueKind_IMPORTED_FUNC;
            break;
        case Value::ValueKind::KIND_IMP_VAR:
            ret = ValueKind_IMPORTED_VAR;
            break;
        case Value::ValueKind::KIND_LOCALVAR:
            ret = ValueKind_LOCALVAR;
            break;
        case Value::ValueKind::KIND_FUNC:
            ret = ValueKind_FUNC;
            break;
        case Value::ValueKind::KIND_BLOCK:
            ret = ValueKind_BLOCK;
            break;
        case Value::ValueKind::KIND_BLOCK_GROUP:
            ret = ValueKind_BLOCK_GROUP;
            break;
    }
    return ret;
}

PackageFormat::ConstantValueKind Serialize(const ConstantValueKind& kind)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::ConstantValueKind;
    auto ret = ConstantValueKind_BOOL;
    switch (kind) {
        case ConstantValueKind::KIND_BOOL:
            ret = ConstantValueKind_BOOL;
            break;
        case ConstantValueKind::KIND_RUNE:
            ret = ConstantValueKind_RUNE;
            break;
        case ConstantValueKind::KIND_INT:
            ret = ConstantValueKind_INT;
            break;
        case ConstantValueKind::KIND_FLOAT:
            ret = ConstantValueKind_FLOAT;
            break;
        case ConstantValueKind::KIND_STRING:
            ret = ConstantValueKind_STRING;
            break;
        case ConstantValueKind::KIND_UNIT:
            ret = ConstantValueKind_UNIT;
            break;
        case ConstantValueKind::KIND_NULL:
            ret = ConstantValueKind_NULL;
            break;
        case ConstantValueKind::KIND_FUNC:
            ret = ConstantValueKind_FUNC;
            break;
    }
    return ret;
}

PackageFormat::FuncKind Serialize(const FuncKind& kind)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::FuncKind;
    auto ret = FuncKind_DEFAULT;
    switch (kind) {
        case FuncKind::DEFAULT:
            ret = FuncKind_DEFAULT;
            break;
        case FuncKind::GETTER:
            ret = FuncKind_GETTER;
            break;
        case FuncKind::SETTER:
            ret = FuncKind_SETTER;
            break;
        case FuncKind::LAMBDA:
            ret = FuncKind_LAMBDA;
            break;
        case FuncKind::CLASS_CONSTRUCTOR:
            ret = FuncKind_CLASS_CONSTRUCTOR;
            break;
        case FuncKind::PRIMAL_CLASS_CONSTRUCTOR:
            ret = FuncKind_PRIMAL_CLASS_CONSTRUCTOR;
            break;
        case FuncKind::STRUCT_CONSTRUCTOR:
            ret = FuncKind_STRUCT_CONSTRUCTOR;
            break;
        case FuncKind::PRIMAL_STRUCT_CONSTRUCTOR:
            ret = FuncKind_PRIMAL_STRUCT_CONSTRUCTOR;
            break;
        case FuncKind::GLOBALVAR_INIT:
            ret = FuncKind_GLOBALVAR_INIT;
            break;
        case FuncKind::FINALIZER:
            ret = FuncKind_FINALIZER;
            break;
        case FuncKind::MAIN_ENTRY:
            ret = FuncKind_MAIN_ENTRY;
            break;
        case FuncKind::ANNOFACTORY_FUNC:
            ret = FuncKind_ANNOFACTORY_FUNC;
            break;
        case FuncKind::MACRO_FUNC:
            ret = FuncKind_MACRO_FUNC;
            break;
        case FuncKind::DEFAULT_PARAMETER_FUNC:
            ret = FuncKind_DEFAULT_PARAMETER_FUNC;
            break;
        case FuncKind::INSTANCEVAR_INIT:
            ret = FuncKind_INSTANCEVAR_INIT;
            break;
        case FuncKind::FUNCKIND_END:
            CJC_ABORT();
            break;
    }
    return ret;
}

PackageFormat::CustomDefKind Serialize(const CustomDefKind& kind)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::CustomDefKind;
    auto ret = CustomDefKind_STRUCT;
    switch (kind) {
        case CustomDefKind::TYPE_STRUCT:
            ret = CustomDefKind_STRUCT;
            break;
        case CustomDefKind::TYPE_ENUM:
            ret = CustomDefKind_ENUM;
            break;
        case CustomDefKind::TYPE_CLASS:
            ret = CustomDefKind_CLASS;
            break;
        case CustomDefKind::TYPE_EXTEND:
            ret = CustomDefKind_EXTEND;
            break;
    }
    return ret;
}

PackageFormat::CHIRExprKind Serialize(const ExprKind& kind)
{
    using namespace PackageFormat;
    auto ret = CHIRExprKind_INVALID;
    switch (kind) {
        case ExprKind::INVALID:
            ret = CHIRExprKind_INVALID;
            break;
        case ExprKind::GOTO:
            ret = CHIRExprKind_GOTO;
            break;
        case ExprKind::BRANCH:
            ret = CHIRExprKind_BRANCH;
            break;
        case ExprKind::MULTIBRANCH:
            ret = CHIRExprKind_MULTIBRANCH;
            break;
        case ExprKind::EXIT:
            ret = CHIRExprKind_EXIT;
            break;
        case ExprKind::APPLY_WITH_EXCEPTION:
            ret = CHIRExprKind_APPLY_WITH_EXCEPTION;
            break;
        case ExprKind::INVOKE_WITH_EXCEPTION:
            ret = CHIRExprKind_INVOKE_WITH_EXCEPTION;
            break;
        case ExprKind::INVOKESTATIC_WITH_EXCEPTION:
            ret = CHIRExprKind_INVOKESTATIC_WITH_EXCEPTION;
            break;
        case ExprKind::RAISE_EXCEPTION:
            ret = CHIRExprKind_RAISE_EXCEPTION;
            break;
        case ExprKind::INT_OP_WITH_EXCEPTION:
            ret = CHIRExprKind_INT_OP_WITH_EXCEPTION;
            break;
        case ExprKind::SPAWN_WITH_EXCEPTION:
            ret = CHIRExprKind_SPAWN_WITH_EXCEPTION;
            break;
        case ExprKind::TYPECAST_WITH_EXCEPTION:
            ret = CHIRExprKind_TYPECAST_WITH_EXCEPTION;
            break;
        case ExprKind::INTRINSIC_WITH_EXCEPTION:
            ret = CHIRExprKind_INTRINSIC_WITH_EXCEPTION;
            break;
        case ExprKind::ALLOCATE_WITH_EXCEPTION:
            ret = CHIRExprKind_ALLOCATE_WITH_EXCEPTION;
            break;
        case ExprKind::RAW_ARRAY_ALLOCATE_WITH_EXCEPTION:
            ret = CHIRExprKind_RAW_ARRAY_ALLOCATE_WITH_EXCEPTION;
            break;
        case ExprKind::NEG:
            ret = CHIRExprKind_NEG;
            break;
        case ExprKind::NOT:
            ret = CHIRExprKind_NOT;
            break;
        case ExprKind::BITNOT:
            ret = CHIRExprKind_BITNOT;
            break;
        case ExprKind::ADD:
            ret = CHIRExprKind_ADD;
            break;
        case ExprKind::SUB:
            ret = CHIRExprKind_SUB;
            break;
        case ExprKind::MUL:
            ret = CHIRExprKind_MUL;
            break;
        case ExprKind::DIV:
            ret = CHIRExprKind_DIV;
            break;
        case ExprKind::MOD:
            ret = CHIRExprKind_MOD;
            break;
        case ExprKind::EXP:
            ret = CHIRExprKind_EXP;
            break;
        case ExprKind::LSHIFT:
            ret = CHIRExprKind_LSHIFT;
            break;
        case ExprKind::RSHIFT:
            ret = CHIRExprKind_RSHIFT;
            break;
        case ExprKind::BITAND:
            ret = CHIRExprKind_BITAND;
            break;
        case ExprKind::BITOR:
            ret = CHIRExprKind_BITOR;
            break;
        case ExprKind::BITXOR:
            ret = CHIRExprKind_BITXOR;
            break;
        case ExprKind::LT:
            ret = CHIRExprKind_LT;
            break;
        case ExprKind::GT:
            ret = CHIRExprKind_GT;
            break;
        case ExprKind::LE:
            ret = CHIRExprKind_LE;
            break;
        case ExprKind::GE:
            ret = CHIRExprKind_GE;
            break;
        case ExprKind::EQUAL:
            ret = CHIRExprKind_EQUAL;
            break;
        case ExprKind::NOTEQUAL:
            ret = CHIRExprKind_NOTEQUAL;
            break;
        case ExprKind::AND:
            ret = CHIRExprKind_AND;
            break;
        case ExprKind::OR:
            ret = CHIRExprKind_OR;
            break;
        case ExprKind::ALLOCATE:
            ret = CHIRExprKind_ALLOCATE;
            break;
        case ExprKind::LOAD:
            ret = CHIRExprKind_LOAD;
            break;
        case ExprKind::STORE:
            ret = CHIRExprKind_STORE;
            break;
        case ExprKind::GET_ELEMENT_REF:
            ret = CHIRExprKind_GET_ELEMENT_REF;
            break;
        case ExprKind::GET_ELEMENT_BY_NAME:
            ret = CHIRExprKind_GET_ELEMENT_BY_NAME;
            break;
        case ExprKind::STORE_ELEMENT_REF:
            ret = CHIRExprKind_STORE_ELEMENT_REF;
            break;
        case ExprKind::STORE_ELEMENT_BY_NAME:
            ret = CHIRExprKind_STORE_ELEMENT_BY_NAME;
            break;
        case ExprKind::IF:
            ret = CHIRExprKind_IF;
            break;
        case ExprKind::LOOP:
            ret = CHIRExprKind_LOOP;
            break;
        case ExprKind::FORIN_RANGE:
            ret = CHIRExprKind_FORIN_RANGE;
            break;
        case ExprKind::FORIN_ITER:
            ret = CHIRExprKind_FORIN_ITER;
            break;
        case ExprKind::FORIN_CLOSED_RANGE:
            ret = CHIRExprKind_FORIN_CLOSED_RANGE;
            break;
        case ExprKind::LAMBDA:
            ret = CHIRExprKind_LAMBDA;
            break;
        case ExprKind::CONSTANT:
            ret = CHIRExprKind_CONSTANT;
            break;
        case ExprKind::DEBUGEXPR:
            ret = CHIRExprKind_DEBUGEXPR;
            break;
        case ExprKind::TUPLE:
            ret = CHIRExprKind_TUPLE;
            break;
        case ExprKind::FIELD:
            ret = CHIRExprKind_FIELD;
            break;
        case ExprKind::FIELD_BY_NAME:
            ret = CHIRExprKind_FIELD_BY_NAME;
            break;
        case ExprKind::APPLY:
            ret = CHIRExprKind_APPLY;
            break;
        case ExprKind::INVOKE:
            ret = CHIRExprKind_INVOKE;
            break;
        case ExprKind::INVOKESTATIC:
            ret = CHIRExprKind_INVOKE_STATIC;
            break;
        case ExprKind::INSTANCEOF:
            ret = CHIRExprKind_INSTANCEOF;
            break;
        case ExprKind::TYPECAST:
            ret = CHIRExprKind_TYPECAST;
            break;
        case ExprKind::GET_EXCEPTION:
            ret = CHIRExprKind_GET_EXCEPTION;
            break;
        case ExprKind::RAW_ARRAY_ALLOCATE:
            ret = CHIRExprKind_RAW_ARRAY_ALLOCATE;
            break;
        case ExprKind::RAW_ARRAY_LITERAL_INIT:
            ret = CHIRExprKind_RAW_ARRAY_LITERAL_INIT;
            break;
        case ExprKind::RAW_ARRAY_INIT_BY_VALUE:
            ret = CHIRExprKind_RAW_ARRAY_INIT_BY_VALUE;
            break;
        case ExprKind::VARRAY:
            ret = CHIRExprKind_VARRAY;
            break;
        case ExprKind::VARRAY_BUILDER:
            ret = CHIRExprKind_VARRAY_BUILDER;
            break;
        case ExprKind::INTRINSIC:
            ret = CHIRExprKind_INTRINSIC;
            break;
        case ExprKind::SPAWN:
            ret = CHIRExprKind_SPAWN;
            break;
        case ExprKind::GET_INSTANTIATE_VALUE:
            ret = CHIRExprKind_GET_INSTANTIATE_VALUE;
            break;
        case ExprKind::BOX:
            ret = CHIRExprKind_BOX;
            break;
        case ExprKind::UNBOX:
            ret = CHIRExprKind_UNBOX;
            break;
        case ExprKind::TRANSFORM_TO_GENERIC:
            ret = CHIRExprKind_TRANSFORM_TO_GENERIC;
            break;
        case ExprKind::TRANSFORM_TO_CONCRETE:
            ret = CHIRExprKind_TRANSFORM_TO_CONCRETE;
            break;
        case ExprKind::UNBOX_TO_REF:
            ret = CHIRExprKind_UNBOX_TO_REF;
            break;
        case ExprKind::GET_RTTI:
            ret = CHIRExprKind_GET_RTTI;
            break;
        case ExprKind::GET_RTTI_STATIC:
            ret = CHIRExprKind_GET_RTTI_STATIC;
            break;
        case ExprKind::MAX_EXPR_KINDS:
            CJC_ABORT();
            break;
            // no defalut here, due to we need use compiler to check all value be handled.
    }
    return ret;
}

PackageFormat::IntrinsicKind Serialize(const IntrinsicKind& kind)
{
    using namespace PackageFormat;
    auto ret = IntrinsicKind_NOT_INTRINSIC;
    switch (kind) {
        case NOT_INTRINSIC:
            ret = IntrinsicKind_NOT_INTRINSIC;
            break;
        case NOT_IMPLEMENTED:
            ret = IntrinsicKind_NOT_IMPLEMENTED;
            break;
        case ARRAY_INIT:
            ret = IntrinsicKind_ARRAY_INIT;
            break;
        case SIZE_OF:
            ret = IntrinsicKind_SIZE_OF;
            break;
        case ALIGN_OF:
            ret = IntrinsicKind_ALIGN_OF;
            break;
        case ARRAY_ACQUIRE_RAW_DATA:
            ret = IntrinsicKind_ARRAY_ACQUIRE_RAW_DATA;
            break;
        case ARRAY_RELEASE_RAW_DATA:
            ret = IntrinsicKind_ARRAY_RELEASE_RAW_DATA;
            break;
        case ARRAY_BUILT_IN_COPY_TO:
            ret = IntrinsicKind_ARRAY_BUILT_IN_COPY_TO;
            break;
        case ARRAY_GET:
            ret = IntrinsicKind_ARRAY_GET;
            break;
        case ARRAY_SET:
            ret = IntrinsicKind_ARRAY_SET;
            break;
        case ARRAY_GET_UNCHECKED:
            ret = IntrinsicKind_ARRAY_GET_UNCHECKED;
            break;
        case ARRAY_GET_REF_UNCHECKED:
            ret = IntrinsicKind_ARRAY_GET_REF_UNCHECKED;
            break;
        case ARRAY_SET_UNCHECKED:
            ret = IntrinsicKind_ARRAY_SET_UNCHECKED;
            break;
        case ARRAY_SIZE:
            ret = IntrinsicKind_ARRAY_SIZE;
            break;
        case ARRAY_CLONE:
            ret = IntrinsicKind_ARRAY_CLONE;
            break;
        case ARRAY_SLICE_INIT:
            ret = IntrinsicKind_ARRAY_SLICE_INIT;
            break;
        case ARRAY_SLICE:
            ret = IntrinsicKind_ARRAY_SLICE;
            break;
        case ARRAY_SLICE_RAWARRAY:
            ret = IntrinsicKind_ARRAY_SLICE_RAWARRAY;
            break;
        case ARRAY_SLICE_START:
            ret = IntrinsicKind_ARRAY_SLICE_START;
            break;
        case ARRAY_SLICE_SIZE:
            ret = IntrinsicKind_ARRAY_SLICE_SIZE;
            break;
        case ARRAY_SLICE_GET_ELEMENT:
            ret = IntrinsicKind_ARRAY_SLICE_GET_ELEMENT;
            break;
        case ARRAY_SLICE_GET_ELEMENT_UNCHECKED:
            ret = IntrinsicKind_ARRAY_SLICE_GET_ELEMENT_UNCHECKED;
            break;
        case ARRAY_SLICE_SET_ELEMENT:
            ret = IntrinsicKind_ARRAY_SLICE_SET_ELEMENT;
            break;
        case ARRAY_SLICE_SET_ELEMENT_UNCHECKED:
            ret = IntrinsicKind_ARRAY_SLICE_SET_ELEMENT_UNCHECKED;
            break;
        case FILL_IN_STACK_TRACE:
            ret = IntrinsicKind_FILL_IN_STACK_TRACE;
            break;
        case DECODE_STACK_TRACE:
            ret = IntrinsicKind_DECODE_STACK_TRACE;
            break;
        case DUMP_CURRENT_THREAD_INFO:
            ret = IntrinsicKind_DUMP_CURRENT_THREAD_INFO;
            break;
        case DUMP_ALL_THREADS_INFO:
            ret = IntrinsicKind_DUMP_ALL_THREADS_INFO;
            break;
        case CHR:
            ret = IntrinsicKind_CHR;
            break;
        case ORD:
            ret = IntrinsicKind_ORD;
            break;
        case CPOINTER_GET_POINTER_ADDRESS:
            ret = IntrinsicKind_CPOINTER_GET_POINTER_ADDRESS;
            break;
        case CPOINTER_INIT0:
            ret = IntrinsicKind_CPOINTER_INIT0;
            break;
        case CPOINTER_INIT1:
            ret = IntrinsicKind_CPOINTER_INIT1;
            break;
        case CPOINTER_READ:
            ret = IntrinsicKind_CPOINTER_READ;
            break;
        case CPOINTER_WRITE:
            ret = IntrinsicKind_CPOINTER_WRITE;
            break;
        case CPOINTER_ADD:
            ret = IntrinsicKind_CPOINTER_ADD;
            break;
        case CSTRING_INIT:
            ret = IntrinsicKind_CSTRING_INIT;
            break;
        case CSTRING_CONVERT_CSTR_TO_PTR:
            ret = IntrinsicKind_CSTRING_CONVERT_CSTR_TO_PTR;
            break;
        case INOUT_PARAM:
            ret = IntrinsicKind_INOUT_PARAM;
            break;
        case REGISTER_WATCHED_OBJECT:
            ret = IntrinsicKind_REGISTER_WATCHED_OBJECT;
            break;
        case OBJECT_REFEQ:
            ret = IntrinsicKind_OBJECT_REFEQ;
            break;
        case RAW_ARRAY_REFEQ:
            ret = IntrinsicKind_RAW_ARRAY_REFEQ;
            break;
        case OBJECT_ZERO_VALUE:
            ret = IntrinsicKind_OBJECT_ZERO_VALUE;
            break;
        case INVOKE_GC:
            ret = IntrinsicKind_INVOKE_GC;
            break;
        case SET_GC_THRESHOLD:
            ret = IntrinsicKind_SET_GC_THRESHOLD;
            break;
        case DUMP_CJ_HEAP_DATA:
            ret = IntrinsicKind_DUMP_CJ_HEAP_DATA;
            break;
        case GET_GC_COUNT:
            ret = IntrinsicKind_GET_GC_COUNT;
            break;
        case GET_GC_TIME_US:
            ret = IntrinsicKind_GET_GC_TIME_US;
            break;
        case GET_GC_FREED_SIZE:
            ret = IntrinsicKind_GET_GC_FREED_SIZE;
            break;
        case START_CJ_CPU_PROFILING:
            ret = IntrinsicKind_START_CJ_CPU_PROFILING;
            break;
        case STOP_CJ_CPU_PROFILING:
            ret = IntrinsicKind_STOP_CJ_CPU_PROFILING;
            break;
        case BLACK_BOX:
            ret = IntrinsicKind_BLACK_BOX;
            break;
        case GET_MAX_HEAP_SIZE:
            ret = IntrinsicKind_GET_MAX_HEAP_SIZE;
            break;
        case GET_ALLOCATE_HEAP_SIZE:
            ret = IntrinsicKind_GET_ALLOCATE_HEAP_SIZE;
            break;
        case GET_REAL_HEAP_SIZE:
            ret = IntrinsicKind_GET_REAL_HEAP_SIZE;
            break;
        case GET_THREAD_NUMBER:
            ret = IntrinsicKind_GET_THREAD_NUMBER;
            break;
        case GET_BLOCKING_THREAD_NUMBER:
            ret = IntrinsicKind_GET_BLOCKING_THREAD_NUMBER;
            break;
        case GET_NATIVE_THREAD_NUMBER:
            ret = IntrinsicKind_GET_NATIVE_THREAD_NUMBER;
            break;
        case VARRAY_SET:
            ret = IntrinsicKind_VARRAY_SET;
            break;
        case VARRAY_GET:
            ret = IntrinsicKind_VARRAY_GET;
            break;
        case FUTURE_INIT:
            ret = IntrinsicKind_FUTURE_INIT;
            break;
        case FUTURE_IS_COMPLETE:
            ret = IntrinsicKind_FUTURE_IS_COMPLETE;
            break;
        case FUTURE_WAIT:
            ret = IntrinsicKind_FUTURE_WAIT;
            break;
        case FUTURE_NOTIFYALL:
            ret = IntrinsicKind_FUTURE_NOTIFYALL;
            break;
        case IS_THREAD_OBJECT_INITED:
            ret = IntrinsicKind_IS_THREAD_OBJECT_INITED;
            break;
        case GET_THREAD_OBJECT:
            ret = IntrinsicKind_GET_THREAD_OBJECT;
            break;
        case SET_THREAD_OBJECT:
            ret = IntrinsicKind_SET_THREAD_OBJECT;
            break;
        case OVERFLOW_CHECKED_ADD:
            ret = IntrinsicKind_OVERFLOW_CHECKED_ADD;
            break;
        case OVERFLOW_CHECKED_SUB:
            ret = IntrinsicKind_OVERFLOW_CHECKED_SUB;
            break;
        case OVERFLOW_CHECKED_MUL:
            ret = IntrinsicKind_OVERFLOW_CHECKED_MUL;
            break;
        case OVERFLOW_CHECKED_DIV:
            ret = IntrinsicKind_OVERFLOW_CHECKED_DIV;
            break;
        case OVERFLOW_CHECKED_MOD:
            ret = IntrinsicKind_OVERFLOW_CHECKED_MOD;
            break;
        case OVERFLOW_CHECKED_POW:
            ret = IntrinsicKind_OVERFLOW_CHECKED_POW;
            break;
        case OVERFLOW_CHECKED_INC:
            ret = IntrinsicKind_OVERFLOW_CHECKED_INC;
            break;
        case OVERFLOW_CHECKED_DEC:
            ret = IntrinsicKind_OVERFLOW_CHECKED_DEC;
            break;
        case OVERFLOW_CHECKED_NEG:
            ret = IntrinsicKind_OVERFLOW_CHECKED_NEG;
            break;
        case OVERFLOW_THROWING_ADD:
            ret = IntrinsicKind_OVERFLOW_THROWING_ADD;
            break;
        case OVERFLOW_THROWING_SUB:
            ret = IntrinsicKind_OVERFLOW_THROWING_SUB;
            break;
        case OVERFLOW_THROWING_MUL:
            ret = IntrinsicKind_OVERFLOW_THROWING_MUL;
            break;
        case OVERFLOW_THROWING_DIV:
            ret = IntrinsicKind_OVERFLOW_THROWING_DIV;
            break;
        case OVERFLOW_THROWING_MOD:
            ret = IntrinsicKind_OVERFLOW_THROWING_MOD;
            break;
        case OVERFLOW_THROWING_POW:
            ret = IntrinsicKind_OVERFLOW_THROWING_POW;
            break;
        case OVERFLOW_THROWING_INC:
            ret = IntrinsicKind_OVERFLOW_THROWING_INC;
            break;
        case OVERFLOW_THROWING_DEC:
            ret = IntrinsicKind_OVERFLOW_THROWING_DEC;
            break;
        case OVERFLOW_THROWING_NEG:
            ret = IntrinsicKind_OVERFLOW_THROWING_NEG;
            break;
        case OVERFLOW_SATURATING_ADD:
            ret = IntrinsicKind_OVERFLOW_SATURATING_ADD;
            break;
        case OVERFLOW_SATURATING_SUB:
            ret = IntrinsicKind_OVERFLOW_SATURATING_SUB;
            break;
        case OVERFLOW_SATURATING_MUL:
            ret = IntrinsicKind_OVERFLOW_SATURATING_MUL;
            break;
        case OVERFLOW_SATURATING_DIV:
            ret = IntrinsicKind_OVERFLOW_SATURATING_DIV;
            break;
        case OVERFLOW_SATURATING_MOD:
            ret = IntrinsicKind_OVERFLOW_SATURATING_MOD;
            break;
        case OVERFLOW_SATURATING_POW:
            ret = IntrinsicKind_OVERFLOW_SATURATING_POW;
            break;
        case OVERFLOW_SATURATING_INC:
            ret = IntrinsicKind_OVERFLOW_SATURATING_INC;
            break;
        case OVERFLOW_SATURATING_DEC:
            ret = IntrinsicKind_OVERFLOW_SATURATING_DEC;
            break;
        case OVERFLOW_SATURATING_NEG:
            ret = IntrinsicKind_OVERFLOW_SATURATING_NEG;
            break;
        case OVERFLOW_WRAPPING_ADD:
            ret = IntrinsicKind_OVERFLOW_WRAPPING_ADD;
            break;
        case OVERFLOW_WRAPPING_SUB:
            ret = IntrinsicKind_OVERFLOW_WRAPPING_SUB;
            break;
        case OVERFLOW_WRAPPING_MUL:
            ret = IntrinsicKind_OVERFLOW_WRAPPING_MUL;
            break;
        case OVERFLOW_WRAPPING_DIV:
            ret = IntrinsicKind_OVERFLOW_WRAPPING_DIV;
            break;
        case OVERFLOW_WRAPPING_MOD:
            ret = IntrinsicKind_OVERFLOW_WRAPPING_MOD;
            break;
        case OVERFLOW_WRAPPING_POW:
            ret = IntrinsicKind_OVERFLOW_WRAPPING_POW;
            break;
        case OVERFLOW_WRAPPING_INC:
            ret = IntrinsicKind_OVERFLOW_WRAPPING_INC;
            break;
        case OVERFLOW_WRAPPING_DEC:
            ret = IntrinsicKind_OVERFLOW_WRAPPING_DEC;
            break;
        case OVERFLOW_WRAPPING_NEG:
            ret = IntrinsicKind_OVERFLOW_WRAPPING_NEG;
            break;
        case VECTOR_COMPARE_32:
            ret = IntrinsicKind_VECTOR_COMPARE_32;
            break;
        case VECTOR_INDEX_BYTE_32:
            ret = IntrinsicKind_VECTOR_INDEX_BYTE_32;
            break;
        case CJ_CORE_CAN_USE_SIMD:
            ret = IntrinsicKind_CJ_CORE_CAN_USE_SIMD;
            break;
        case CJ_TLS_DYN_SET_SESSION_CALLBACK:
            ret = IntrinsicKind_CJ_TLS_DYN_SET_SESSION_CALLBACK;
            break;
        case CJ_TLS_DYN_SSL_INIT:
            ret = IntrinsicKind_CJ_TLS_DYN_SSL_INIT;
            break;
        case REFLECTION_INTRINSIC_START_FLAG:
            ret = IntrinsicKind_REFLECTION_INTRINSIC_START_FLAG;
            break;
        case IS_INTERFACE:
            ret = IntrinsicKind_IS_INTERFACE;
            break;
        case IS_CLASS:
            ret = IntrinsicKind_IS_CLASS;
            break;
        case IS_PRIMITIVE:
            ret = IntrinsicKind_IS_PRIMITIVE;
            break;
        case IS_STRUCT:
            ret = IntrinsicKind_IS_STRUCT;
            break;
        case IS_GENERIC:
            ret = IntrinsicKind_IS_GENERIC;
            break;
        case GET_OR_CREATE_TYPEINFO_FOR_REFLECT:
            ret = IntrinsicKind_GET_OR_CREATE_TYPEINFO_FOR_REFLECT;
            break;
        case GET_TYPETEMPLATE:
            ret = IntrinsicKind_GET_TYPETEMPLATE;
            break;
        case CHECK_METHOD_ACTUAL_ARGS:
            ret = IntrinsicKind_CHECK_METHOD_ACTUAL_ARGS;
            break;
        case METHOD_ENTRYPOINT_IS_NULL:
            ret = IntrinsicKind_METHOD_ENTRYPOINT_IS_NULL;
            break;
        case IS_RELECT_UNSUPPORTED_TYPE:
            ret = IntrinsicKind_IS_RELECT_UNSUPPORTED_TYPE;
            break;
        case GET_TYPE_FOR_ANY:
            ret = IntrinsicKind_GET_TYPE_FOR_ANY;
            break;
        case GET_TYPE_BY_MANGLED_NAME:
            ret = IntrinsicKind_GET_TYPE_BY_MANGLED_NAME;
            break;
        case GET_TYPE_NAME:
            ret = IntrinsicKind_GET_TYPE_NAME;
            break;
        case GET_TYPE_BY_QUALIFIED_NAME:
            ret = IntrinsicKind_GET_TYPE_BY_QUALIFIED_NAME;
            break;
        case GET_TYPE_QUALIFIED_NAME_LENGTH:
            ret = IntrinsicKind_GET_TYPE_QUALIFIED_NAME_LENGTH;
            break;
        case GET_TYPE_QUALIFIED_NAME:
            ret = IntrinsicKind_GET_TYPE_QUALIFIED_NAME;
            break;
        case GET_NUM_OF_INTERFACE:
            ret = IntrinsicKind_GET_NUM_OF_INTERFACE;
            break;
        case GET_INTERFACE:
            ret = IntrinsicKind_GET_INTERFACE;
            break;
        case IS_SUBTYPE:
            ret = IntrinsicKind_IS_SUBTYPE;
            break;
        case GET_TYPE_INFO_MODIFIER:
            ret = IntrinsicKind_GET_TYPE_INFO_MODIFIER;
            break;
        case GET_TYPE_INFO_ANNOTATIONS:
            ret = IntrinsicKind_GET_TYPE_INFO_ANNOTATIONS;
            break;
        case GET_OBJ_CLASS:
            ret = IntrinsicKind_GET_OBJ_CLASS;
            break;
        case GET_SUPER_TYPE_INFO:
            ret = IntrinsicKind_GET_SUPER_TYPE_INFO;
            break;
        case GET_NUM_OF_INSTANCE_METHOD_INFOS:
            ret = IntrinsicKind_GET_NUM_OF_INSTANCE_METHOD_INFOS;
            break;
        case GET_INSTANCE_METHOD_INFO:
            ret = IntrinsicKind_GET_INSTANCE_METHOD_INFO;
            break;
        case GET_NUM_OF_STATIC_METHOD_INFOS:
            ret = IntrinsicKind_GET_NUM_OF_STATIC_METHOD_INFOS;
            break;
        case GET_STATIC_METHOD_INFO:
            ret = IntrinsicKind_GET_STATIC_METHOD_INFO;
            break;
        case GET_METHOD_NAME:
            ret = IntrinsicKind_GET_METHOD_NAME;
            break;
        case GET_METHOD_RETURN_TYPE:
            ret = IntrinsicKind_GET_METHOD_RETURN_TYPE;
            break;
        case GET_METHOD_MODIFIER:
            ret = IntrinsicKind_GET_METHOD_MODIFIER;
            break;
        case GET_METHOD_ANNOTATIONS:
            ret = IntrinsicKind_GET_METHOD_ANNOTATIONS;
            break;
        case APPLY_CJ_METHOD:
            ret = IntrinsicKind_APPLY_CJ_METHOD;
            break;
        case APPLY_CJ_STATIC_METHOD:
            ret = IntrinsicKind_APPLY_CJ_STATIC_METHOD;
            break;
        case APPLY_CJ_GENERIC_METHOD:
            ret = IntrinsicKind_APPLY_CJ_GENERIC_METHOD;
            break;
        case APPLY_CJ_GENERIC_STATIC_METHOD:
            ret = IntrinsicKind_APPLY_CJ_GENERIC_STATIC_METHOD;
            break;
        case GET_NUM_OF_ACTUAL_PARAMETERS:
            ret = IntrinsicKind_GET_NUM_OF_ACTUAL_PARAMETERS;
            break;
        case GET_NUM_OF_GENERIC_PARAMETERS:
            ret = IntrinsicKind_GET_NUM_OF_GENERIC_PARAMETERS;
            break;
        case GET_ACTUAL_PARAMETER_INFO:
            ret = IntrinsicKind_GET_ACTUAL_PARAMETER_INFO;
            break;
        case GET_GENERIC_PARAMETER_INFO:
            ret = IntrinsicKind_GET_GENERIC_PARAMETER_INFO;
            break;
        case GET_NUM_OF_INSTANCE_FIELD_INFOS:
            ret = IntrinsicKind_GET_NUM_OF_INSTANCE_FIELD_INFOS;
            break;
        case GET_INSTANCE_FIELD_INFO:
            ret = IntrinsicKind_GET_INSTANCE_FIELD_INFO;
            break;
        case GET_NUM_OF_STATIC_FIELD_INFOS:
            ret = IntrinsicKind_GET_NUM_OF_STATIC_FIELD_INFOS;
            break;
        case GET_STATIC_FIELD_INFO:
            ret = IntrinsicKind_GET_STATIC_FIELD_INFO;
            break;
        case GET_STATIC_FIELD_NAME:
            ret = IntrinsicKind_GET_STATIC_FIELD_NAME;
            break;
        case GET_STATIC_FIELD_TYPE:
            ret = IntrinsicKind_GET_STATIC_FIELD_TYPE;
            break;
        case GET_STATIC_FIELD_ANNOTATIONS:
            ret = IntrinsicKind_GET_STATIC_FIELD_ANNOTATIONS;
            break;
        case GET_FIELD_NAME:
            ret = IntrinsicKind_GET_FIELD_NAME;
            break;
        case GET_FIELD_TYPE:
            ret = IntrinsicKind_GET_FIELD_TYPE;
            break;
        case GET_FIELD_ANNOTATIONS:
            ret = IntrinsicKind_GET_FIELD_ANNOTATIONS;
            break;
        case GET_FIELD_MODIFIER:
            ret = IntrinsicKind_GET_FIELD_MODIFIER;
            break;
        case GET_STATIC_FIELD_MODIFIER:
            ret = IntrinsicKind_GET_STATIC_FIELD_MODIFIER;
            break;
        case GET_FIELD_VALUE:
            ret = IntrinsicKind_GET_FIELD_VALUE;
            break;
        case SET_FIELD_VALUE:
            ret = IntrinsicKind_SET_FIELD_VALUE;
            break;
        case GET_STATIC_FIELD_VALUE:
            ret = IntrinsicKind_GET_STATIC_FIELD_VALUE;
            break;
        case SET_STATIC_FIELD_VALUE:
            ret = IntrinsicKind_SET_STATIC_FIELD_VALUE;
            break;
        case GET_FIELD_DECLARING_TYPE:
            ret = IntrinsicKind_GET_FIELD_DECLARING_TYPE;
            break;
        case GET_PARAMETER_INDEX:
            ret = IntrinsicKind_GET_PARAMETER_INDEX;
            break;
        case GET_PARAMETER_NAME:
            ret = IntrinsicKind_GET_PARAMETER_NAME;
            break;
        case GET_PARAMETER_TYPE:
            ret = IntrinsicKind_GET_PARAMETER_TYPE;
            break;
        case GET_PARAMETER_ANNOTATIONS:
            ret = IntrinsicKind_GET_PARAMETER_ANNOTATIONS;
            break;
        case GET_RELATED_PACKAGE_INF:
            ret = IntrinsicKind_GET_RELATED_PACKAGE_INF;
            break;
        case GET_PACKAGE_NAME:
            ret = IntrinsicKind_GET_PACKAGE_NAME;
            break;
        case GET_PACKAGE_NUM_OF_TYPE_INFOS:
            ret = IntrinsicKind_GET_PACKAGE_NUM_OF_TYPE_INFOS;
            break;
        case GET_PACKAGE_TYPE_INFO:
            ret = IntrinsicKind_GET_PACKAGE_TYPE_INFO;
            break;
        case GET_PACKAGE_NUM_OF_GLOBAL_METHODS:
            ret = IntrinsicKind_GET_PACKAGE_NUM_OF_GLOBAL_METHODS;
            break;
        case GET_PACKAGE_GLOBAL_METHOD_INFO:
            ret = IntrinsicKind_GET_PACKAGE_GLOBAL_METHOD_INFO;
            break;
        case GET_PACKAGE_NUM_OF_GLOBAL_FIELD_INFOS:
            ret = IntrinsicKind_GET_PACKAGE_NUM_OF_GLOBAL_FIELD_INFOS;
            break;
        case GET_PACKAGE_GLOBAL_FIELD_INFO:
            ret = IntrinsicKind_GET_PACKAGE_GLOBAL_FIELD_INFO;
            break;
        case LOAD_PACKAGE:
            ret = IntrinsicKind_LOAD_PACKAGE;
            break;
        case GET_PACKAGE_BY_QUALIFIEDNAME:
            ret = IntrinsicKind_GET_PACKAGE_BY_QUALIFIEDNAME;
            break;
        case GET_PACKAGE_VERSION:
            ret = IntrinsicKind_GET_PACKAGE_VERSION;
            break;
        case GET_SUB_PACKAGES:
            ret = IntrinsicKind_GET_SUB_PACKAGES;
            break;
        case REFLECTION_INTRINSIC_END_FLAG:
            ret = IntrinsicKind_REFLECTION_INTRINSIC_END_FLAG;
            break;
        case SLEEP:
            ret = IntrinsicKind_SLEEP;
            break;
        case SOURCE_FILE:
            ret = IntrinsicKind_SOURCE_FILE;
            break;
        case SOURCE_LINE:
            ret = IntrinsicKind_SOURCE_LINE;
            break;
        case IDENTITY_HASHCODE:
            ret = IntrinsicKind_IDENTITY_HASHCODE;
            break;
        case IDENTITY_HASHCODE_FOR_ARRAY:
            ret = IntrinsicKind_IDENTITY_HASHCODE_FOR_ARRAY;
            break;
        case ATOMIC_LOAD:
            ret = IntrinsicKind_ATOMIC_LOAD;
            break;
        case ATOMIC_STORE:
            ret = IntrinsicKind_ATOMIC_STORE;
            break;
        case ATOMIC_SWAP:
            ret = IntrinsicKind_ATOMIC_SWAP;
            break;
        case ATOMIC_COMPARE_AND_SWAP:
            ret = IntrinsicKind_ATOMIC_COMPARE_AND_SWAP;
            break;
        case ATOMIC_FETCH_ADD:
            ret = IntrinsicKind_ATOMIC_FETCH_ADD;
            break;
        case ATOMIC_FETCH_SUB:
            ret = IntrinsicKind_ATOMIC_FETCH_SUB;
            break;
        case ATOMIC_FETCH_AND:
            ret = IntrinsicKind_ATOMIC_FETCH_AND;
            break;
        case ATOMIC_FETCH_OR:
            ret = IntrinsicKind_ATOMIC_FETCH_OR;
            break;
        case ATOMIC_FETCH_XOR:
            ret = IntrinsicKind_ATOMIC_FETCH_XOR;
            break;
        case MUTEX_INIT:
            ret = IntrinsicKind_MUTEX_INIT;
            break;
        case CJ_MUTEX_LOCK:
            ret = IntrinsicKind_CJ_MUTEX_LOCK;
            break;
        case MUTEX_TRY_LOCK:
            ret = IntrinsicKind_MUTEX_TRY_LOCK;
            break;
        case MUTEX_CHECK_STATUS:
            ret = IntrinsicKind_MUTEX_CHECK_STATUS;
            break;
        case MUTEX_UNLOCK:
            ret = IntrinsicKind_MUTEX_UNLOCK;
            break;
        case WAITQUEUE_INIT:
            ret = IntrinsicKind_WAITQUEUE_INIT;
            break;
        case MONITOR_INIT:
            ret = IntrinsicKind_MONITOR_INIT;
            break;
        case MOITIOR_WAIT:
            ret = IntrinsicKind_MOITIOR_WAIT;
            break;
        case MOITIOR_NOTIFY:
            ret = IntrinsicKind_MOITIOR_NOTIFY;
            break;
        case MOITIOR_NOTIFY_ALL:
            ret = IntrinsicKind_MOITIOR_NOTIFY_ALL;
            break;
        case MULTICONDITION_WAIT:
            ret = IntrinsicKind_MULTICONDITION_WAIT;
            break;
        case MULTICONDITION_NOTIFY:
            ret = IntrinsicKind_MULTICONDITION_NOTIFY;
            break;
        case MULTICONDITION_NOTIFY_ALL:
            ret = IntrinsicKind_MULTICONDITION_NOTIFY_ALL;
            break;
        case CROSS_ACCESS_BARRIER:
            ret = IntrinsicKind_CROSS_ACCESS_BARRIER;
            break;
        case CREATE_EXPORT_HANDLE:
            ret = IntrinsicKind_CREATE_EXPORT_HANDLE;
            break;
        case GET_EXPORTED_REF:
            ret = IntrinsicKind_GET_EXPORTED_REF;
            break;
        case REMOVE_EXPORTED_REF:
            ret = IntrinsicKind_REMOVE_EXPORTED_REF;
            break;
        case FFI_CJ_AST_LEX:
            ret = IntrinsicKind_FFI_CJ_AST_LEX;
            break;
        case FFI_CJ_AST_PARSEEXPR:
            ret = IntrinsicKind_FFI_CJ_AST_PARSEEXPR;
            break;
        case FFI_CJ_AST_PARSEDECL:
            ret = IntrinsicKind_FFI_CJ_AST_PARSEDECL;
            break;
        case FFI_CJ_AST_PARSE_PROPMEMBERDECL:
            ret = IntrinsicKind_FFI_CJ_AST_PARSE_PROPMEMBERDECL;
            break;
        case FFI_CJ_AST_PARSE_PRICONSTRUCTOR:
            ret = IntrinsicKind_FFI_CJ_AST_PARSE_PRICONSTRUCTOR;
            break;
        case FFI_CJ_AST_PARSE_PATTERN:
            ret = IntrinsicKind_FFI_CJ_AST_PARSE_PATTERN;
            break;
        case FFI_CJ_AST_PARSE_TYPE:
            ret = IntrinsicKind_FFI_CJ_AST_PARSE_TYPE;
            break;
        case FFI_CJ_AST_PARSETOPLEVEL:
            ret = IntrinsicKind_FFI_CJ_AST_PARSETOPLEVEL;
            break;
        case FFI_CJ_AST_DIAGREPORT:
            ret = IntrinsicKind_FFI_CJ_AST_DIAGREPORT;
            break;
        case FFI_CJ_PARENT_CONTEXT:
            ret = IntrinsicKind_FFI_CJ_PARENT_CONTEXT;
            break;
        case FFI_CJ_MACRO_ITEM_INFO:
            ret = IntrinsicKind_FFI_CJ_MACRO_ITEM_INFO;
            break;
        case FFI_CJ_GET_CHILD_MESSAGES:
            ret = IntrinsicKind_FFI_CJ_GET_CHILD_MESSAGES;
            break;
        case FFI_CJ_CHECK_ADD_SPACE:
            ret = IntrinsicKind_FFI_CJ_CHECK_ADD_SPACE;
            break;
        case CG_UNSAFE_BEGIN:
            ret = IntrinsicKind_CG_UNSAFE_BEGIN;
            break;
        case CG_UNSAFE_END:
            ret = IntrinsicKind_CG_UNSAFE_END;
            break;
        case STRLEN:
            ret = IntrinsicKind_STRLEN;
            break;
        case MEMCPY_S:
            ret = IntrinsicKind_MEMCPY_S;
            break;
        case MEMSET_S:
            ret = IntrinsicKind_MEMSET_S;
            break;
        case FREE:
            ret = IntrinsicKind_FREE;
            break;
        case MALLOC:
            ret = IntrinsicKind_MALLOC;
            break;
        case STRCMP:
            ret = IntrinsicKind_STRCMP;
            break;
        case MEMCMP:
            ret = IntrinsicKind_MEMCMP;
            break;
        case STRNCMP:
            ret = IntrinsicKind_STRNCMP;
            break;
        case STRCASECMP:
            ret = IntrinsicKind_STRCASECMP;
            break;
        case ATOMIC_INT8_LOAD:
            ret = IntrinsicKind_ATOMIC_INT8_LOAD;
            break;
        case ATOMIC_INT8_STORE:
            ret = IntrinsicKind_ATOMIC_INT8_STORE;
            break;
        case ATOMIC_INT8_SWAP:
            ret = IntrinsicKind_ATOMIC_INT8_SWAP;
            break;
        case ATOMIC_INT8_CAS:
            ret = IntrinsicKind_ATOMIC_INT8_CAS;
            break;
        case ATOMIC_INT8_FETCH_ADD:
            ret = IntrinsicKind_ATOMIC_INT8_FETCH_ADD;
            break;
        case ATOMIC_INT8_FETCH_SUB:
            ret = IntrinsicKind_ATOMIC_INT8_FETCH_SUB;
            break;
        case ATOMIC_INT8_FETCH_AND:
            ret = IntrinsicKind_ATOMIC_INT8_FETCH_AND;
            break;
        case ATOMIC_INT8_FETCH_OR:
            ret = IntrinsicKind_ATOMIC_INT8_FETCH_OR;
            break;
        case ATOMIC_INT8_FETCH_XOR:
            ret = IntrinsicKind_ATOMIC_INT8_FETCH_XOR;
            break;
        case ATOMIC_INT16_LOAD:
            ret = IntrinsicKind_ATOMIC_INT16_LOAD;
            break;
        case ATOMIC_INT16_STORE:
            ret = IntrinsicKind_ATOMIC_INT16_STORE;
            break;
        case ATOMIC_INT16_SWAP:
            ret = IntrinsicKind_ATOMIC_INT16_SWAP;
            break;
        case ATOMIC_INT16_CAS:
            ret = IntrinsicKind_ATOMIC_INT16_CAS;
            break;
        case ATOMIC_INT16_FETCH_ADD:
            ret = IntrinsicKind_ATOMIC_INT16_FETCH_ADD;
            break;
        case ATOMIC_INT16_FETCH_SUB:
            ret = IntrinsicKind_ATOMIC_INT16_FETCH_SUB;
            break;
        case ATOMIC_INT16_FETCH_AND:
            ret = IntrinsicKind_ATOMIC_INT16_FETCH_AND;
            break;
        case ATOMIC_INT16_FETCH_OR:
            ret = IntrinsicKind_ATOMIC_INT16_FETCH_OR;
            break;
        case ATOMIC_INT16_FETCH_XOR:
            ret = IntrinsicKind_ATOMIC_INT16_FETCH_XOR;
            break;
        case ATOMIC_INT32_LOAD:
            ret = IntrinsicKind_ATOMIC_INT32_LOAD;
            break;
        case ATOMIC_INT32_STORE:
            ret = IntrinsicKind_ATOMIC_INT32_STORE;
            break;
        case ATOMIC_INT32_SWAP:
            ret = IntrinsicKind_ATOMIC_INT32_SWAP;
            break;
        case ATOMIC_INT32_CAS:
            ret = IntrinsicKind_ATOMIC_INT32_CAS;
            break;
        case ATOMIC_INT32_FETCH_ADD:
            ret = IntrinsicKind_ATOMIC_INT32_FETCH_ADD;
            break;
        case ATOMIC_INT32_FETCH_SUB:
            ret = IntrinsicKind_ATOMIC_INT32_FETCH_SUB;
            break;
        case ATOMIC_INT32_FETCH_AND:
            ret = IntrinsicKind_ATOMIC_INT32_FETCH_AND;
            break;
        case ATOMIC_INT32_FETCH_OR:
            ret = IntrinsicKind_ATOMIC_INT32_FETCH_OR;
            break;
        case ATOMIC_INT32_FETCH_XOR:
            ret = IntrinsicKind_ATOMIC_INT32_FETCH_XOR;
            break;
        case ATOMIC_INT64_LOAD:
            ret = IntrinsicKind_ATOMIC_INT64_LOAD;
            break;
        case ATOMIC_INT64_STORE:
            ret = IntrinsicKind_ATOMIC_INT64_STORE;
            break;
        case ATOMIC_INT64_SWAP:
            ret = IntrinsicKind_ATOMIC_INT64_SWAP;
            break;
        case ATOMIC_INT64_CAS:
            ret = IntrinsicKind_ATOMIC_INT64_CAS;
            break;
        case ATOMIC_INT64_FETCH_ADD:
            ret = IntrinsicKind_ATOMIC_INT64_FETCH_ADD;
            break;
        case ATOMIC_INT64_FETCH_SUB:
            ret = IntrinsicKind_ATOMIC_INT64_FETCH_SUB;
            break;
        case ATOMIC_INT64_FETCH_AND:
            ret = IntrinsicKind_ATOMIC_INT64_FETCH_AND;
            break;
        case ATOMIC_INT64_FETCH_OR:
            ret = IntrinsicKind_ATOMIC_INT64_FETCH_OR;
            break;
        case ATOMIC_INT64_FETCH_XOR:
            ret = IntrinsicKind_ATOMIC_INT64_FETCH_XOR;
            break;
        case ATOMIC_UINT8_LOAD:
            ret = IntrinsicKind_ATOMIC_UINT8_LOAD;
            break;
        case ATOMIC_UINT8_STORE:
            ret = IntrinsicKind_ATOMIC_UINT8_STORE;
            break;
        case ATOMIC_UINT8_SWAP:
            ret = IntrinsicKind_ATOMIC_UINT8_SWAP;
            break;
        case ATOMIC_UINT8_CAS:
            ret = IntrinsicKind_ATOMIC_UINT8_CAS;
            break;
        case ATOMIC_UINT8_FETCH_ADD:
            ret = IntrinsicKind_ATOMIC_UINT8_FETCH_ADD;
            break;
        case ATOMIC_UINT8_FETCH_SUB:
            ret = IntrinsicKind_ATOMIC_UINT8_FETCH_SUB;
            break;
        case ATOMIC_UINT8_FETCH_AND:
            ret = IntrinsicKind_ATOMIC_UINT8_FETCH_AND;
            break;
        case ATOMIC_UINT8_FETCH_OR:
            ret = IntrinsicKind_ATOMIC_UINT8_FETCH_OR;
            break;
        case ATOMIC_UINT8_FETCH_XOR:
            ret = IntrinsicKind_ATOMIC_UINT8_FETCH_XOR;
            break;
        case ATOMIC_UINT16_LOAD:
            ret = IntrinsicKind_ATOMIC_UINT16_LOAD;
            break;
        case ATOMIC_UINT16_STORE:
            ret = IntrinsicKind_ATOMIC_UINT16_STORE;
            break;
        case ATOMIC_UINT16_SWAP:
            ret = IntrinsicKind_ATOMIC_UINT16_SWAP;
            break;
        case ATOMIC_UINT16_CAS:
            ret = IntrinsicKind_ATOMIC_UINT16_CAS;
            break;
        case ATOMIC_UINT16_FETCH_ADD:
            ret = IntrinsicKind_ATOMIC_UINT16_FETCH_ADD;
            break;
        case ATOMIC_UINT16_FETCH_SUB:
            ret = IntrinsicKind_ATOMIC_UINT16_FETCH_SUB;
            break;
        case ATOMIC_UINT16_FETCH_AND:
            ret = IntrinsicKind_ATOMIC_UINT16_FETCH_AND;
            break;
        case ATOMIC_UINT16_FETCH_OR:
            ret = IntrinsicKind_ATOMIC_UINT16_FETCH_OR;
            break;
        case ATOMIC_UINT16_FETCH_XOR:
            ret = IntrinsicKind_ATOMIC_UINT16_FETCH_XOR;
            break;
        case ATOMIC_UINT32_LOAD:
            ret = IntrinsicKind_ATOMIC_UINT32_LOAD;
            break;
        case ATOMIC_UINT32_STORE:
            ret = IntrinsicKind_ATOMIC_UINT32_STORE;
            break;
        case ATOMIC_UINT32_SWAP:
            ret = IntrinsicKind_ATOMIC_UINT32_SWAP;
            break;
        case ATOMIC_UINT32_CAS:
            ret = IntrinsicKind_ATOMIC_UINT32_CAS;
            break;
        case ATOMIC_UINT32_FETCH_ADD:
            ret = IntrinsicKind_ATOMIC_UINT32_FETCH_ADD;
            break;
        case ATOMIC_UINT32_FETCH_SUB:
            ret = IntrinsicKind_ATOMIC_UINT32_FETCH_SUB;
            break;
        case ATOMIC_UINT32_FETCH_AND:
            ret = IntrinsicKind_ATOMIC_UINT32_FETCH_AND;
            break;
        case ATOMIC_UINT32_FETCH_OR:
            ret = IntrinsicKind_ATOMIC_UINT32_FETCH_OR;
            break;
        case ATOMIC_UINT32_FETCH_XOR:
            ret = IntrinsicKind_ATOMIC_UINT32_FETCH_XOR;
            break;
        case ATOMIC_UINT64_LOAD:
            ret = IntrinsicKind_ATOMIC_UINT64_LOAD;
            break;
        case ATOMIC_UINT64_STORE:
            ret = IntrinsicKind_ATOMIC_UINT64_STORE;
            break;
        case ATOMIC_UINT64_SWAP:
            ret = IntrinsicKind_ATOMIC_UINT64_SWAP;
            break;
        case ATOMIC_UINT64_CAS:
            ret = IntrinsicKind_ATOMIC_UINT64_CAS;
            break;
        case ATOMIC_UINT64_FETCH_ADD:
            ret = IntrinsicKind_ATOMIC_UINT64_FETCH_ADD;
            break;
        case ATOMIC_UINT64_FETCH_SUB:
            ret = IntrinsicKind_ATOMIC_UINT64_FETCH_SUB;
            break;
        case ATOMIC_UINT64_FETCH_AND:
            ret = IntrinsicKind_ATOMIC_UINT64_FETCH_AND;
            break;
        case ATOMIC_UINT64_FETCH_OR:
            ret = IntrinsicKind_ATOMIC_UINT64_FETCH_OR;
            break;
        case ATOMIC_UINT64_FETCH_XOR:
            ret = IntrinsicKind_ATOMIC_UINT64_FETCH_XOR;
            break;
        case ATOMIC_BOOL_LOAD:
            ret = IntrinsicKind_ATOMIC_BOOL_LOAD;
            break;
        case ATOMIC_BOOL_STORE:
            ret = IntrinsicKind_ATOMIC_BOOL_STORE;
            break;
        case ATOMIC_BOOL_SWAP:
            ret = IntrinsicKind_ATOMIC_BOOL_SWAP;
            break;
        case ATOMIC_BOOL_CAS:
            ret = IntrinsicKind_ATOMIC_BOOL_CAS;
            break;
        case ATOMIC_REFERENCEBASE_LOAD:
            ret = IntrinsicKind_ATOMIC_REFERENCEBASE_LOAD;
            break;
        case ATOMIC_REFERENCEBASE_STORE:
            ret = IntrinsicKind_ATOMIC_REFERENCEBASE_STORE;
            break;
        case ATOMIC_REFERENCEBASE_SWAP:
            ret = IntrinsicKind_ATOMIC_REFERENCEBASE_SWAP;
            break;
        case ATOMIC_REFERENCEBASE_CAS:
            ret = IntrinsicKind_ATOMIC_REFERENCEBASE_CAS;
            break;
        case ATOMIC_OPTIONREFERENCE_LOAD:
            ret = IntrinsicKind_ATOMIC_OPTIONREFERENCE_LOAD;
            break;
        case ATOMIC_OPTIONREFERENCE_STORE:
            ret = IntrinsicKind_ATOMIC_OPTIONREFERENCE_STORE;
            break;
        case ATOMIC_OPTIONREFERENCE_SWAP:
            ret = IntrinsicKind_ATOMIC_OPTIONREFERENCE_SWAP;
            break;
        case ATOMIC_OPTIONREFERENCE_CAS:
            ret = IntrinsicKind_ATOMIC_OPTIONREFERENCE_CAS;
            break;
        case BEGIN_CATCH:
            ret = IntrinsicKind_BEGIN_CATCH;
            break;
        case ABS:
            ret = IntrinsicKind_ABS;
            break;
        case FABS:
            ret = IntrinsicKind_FABS;
            break;
        case FLOOR:
            ret = IntrinsicKind_FLOOR;
            break;
        case CEIL:
            ret = IntrinsicKind_CEIL;
            break;
        case TRUNC:
            ret = IntrinsicKind_TRUNC;
            break;
        case SIN:
            ret = IntrinsicKind_SIN;
            break;
        case COS:
            ret = IntrinsicKind_COS;
            break;
        case EXP:
            ret = IntrinsicKind_EXP;
            break;
        case EXP2:
            ret = IntrinsicKind_EXP2;
            break;
        case LOG:
            ret = IntrinsicKind_LOG;
            break;
        case LOG2:
            ret = IntrinsicKind_LOG2;
            break;
        case LOG10:
            ret = IntrinsicKind_LOG10;
            break;
        case SQRT:
            ret = IntrinsicKind_SQRT;
            break;
        case ROUND:
            ret = IntrinsicKind_ROUND;
            break;
        case POW:
            ret = IntrinsicKind_POW;
            break;
        case POWI:
            ret = IntrinsicKind_POWI;
            break;
        case BIT_CAST:
            ret = IntrinsicKind_BIT_CAST;
            break;
        case PREINITIALIZE:
            ret = IntrinsicKind_PREINITIALIZE;
            break;
        case OBJECT_AS:
            ret = IntrinsicKind_OBJECT_AS;
            break;
        case IS_NULL:
            ret = IntrinsicKind_IS_NULL;
            break;
        case GET_TYPE_FOR_TYPE_PARAMETER:
            ret = IntrinsicKind_GET_TYPE_FOR_TYPE_PARAMETER;
            break;
        case IS_SUBTYPE_TYPES:
            ret = IntrinsicKind_IS_SUBTYPE_TYPES;
            break;
            // no defalut here, due to we need use compiler to check all value be handled.
    }
    return ret;
}

PackageFormat::PackageAccessLevel Serialize(const Package::AccessLevel& kind)
{
    using namespace PackageFormat;
    auto ret = PackageAccessLevel_INVALID;
    switch (kind) {
        case Package::AccessLevel::INVALID:
            ret = PackageAccessLevel_INVALID;
            break;
        case Package::AccessLevel::INTERNAL:
            ret = PackageAccessLevel_INTERNAL;
            break;
        case Package::AccessLevel::PROTECTED:
            ret = PackageAccessLevel_PROTECTED;
            break;
        case Package::AccessLevel::PUBLIC:
            ret = PackageAccessLevel_PUBLIC;
            break;
    }
    return ret;
}

PackageFormat::Phase Serialize(const Cangjie::CHIR::ToCHIR::Phase& kind)
{
    using namespace PackageFormat;
    auto ret = Phase_RAW;
    switch (kind) {
        case ToCHIR::Phase::RAW:
            ret = Phase_RAW;
            break;
        case ToCHIR::Phase::OPT:
            ret = Phase_OPT;
            break;
        case ToCHIR::Phase::PLUGIN:
            ret = Phase_PLUGIN;
            break;
        case ToCHIR::Phase::ANALYSIS_FOR_CJLINT:
            ret = Phase_ANALYSIS_FOR_CJLINT;
            break;
    }
    return ret;
}
TEST_F(CHIRSerialzierTest, TypeKindEnum)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::Type;
    auto enumBegin = Type::TypeKind::TYPE_INVALID;
    auto enumEnd = Type::TypeKind::TYPE_THIS; // make sure this is max one we defined exclude pseudo-value MAX_TYPE_KIND
    EXPECT_EQ(static_cast<size_t>(enumBegin), 0);
    EXPECT_EQ(static_cast<size_t>(enumEnd) + 1, static_cast<size_t>(Cangjie::CHIR::Type::TypeKind::MAX_TYPE_KIND));
    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(
            PackageFormat::CHIRTypeKind(static_cast<Type::TypeKind>(i)), Serialize(static_cast<Type::TypeKind>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRSerialzierTest, SourceExprEnum)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::SourceExpr;
    SourceExpr enumBegin = SourceExpr::IF_EXPR;
    SourceExpr enumEnd = SourceExpr::OTHER; // make sure this is max one we defined
    EXPECT_EQ(static_cast<size_t>(enumBegin), 0);
    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(PackageFormat::SourceExpr(static_cast<SourceExpr>(i)), Serialize(static_cast<SourceExpr>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRSerialzierTest, LinkageEnum)
{
    using namespace PackageFormat;
    using Cangjie::Linkage;
    Linkage enumBegin = Linkage::WEAK_ODR;
    Linkage enumEnd = Linkage::EXTERNAL_WEAK; // make sure this is max one we defined
    EXPECT_EQ(static_cast<size_t>(enumBegin), 0);
    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(PackageFormat::Linkage(static_cast<Linkage>(i)), Serialize(static_cast<Linkage>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRSerialzierTest, SkipKindEnum)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::SkipKind;
    SkipKind enumBegin = SkipKind::NO_SKIP;
    SkipKind enumEnd = SkipKind::SKIP_VIC; // make sure this is max one we defined
    EXPECT_EQ(static_cast<size_t>(enumBegin), 0);
    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(PackageFormat::SkipKind(static_cast<SkipKind>(i)), Serialize(static_cast<SkipKind>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRSerialzierTest, OverflowStrategyEnum)
{
    using namespace PackageFormat;
    using Cangjie::OverflowStrategy;
    OverflowStrategy enumBegin = OverflowStrategy::NA;
    OverflowStrategy enumEnd = OverflowStrategy::SATURATING; // make sure this is max one we defined
    EXPECT_EQ(static_cast<size_t>(enumBegin), 0);
    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(PackageFormat::OverflowStrategy(static_cast<OverflowStrategy>(i)),
            Serialize(static_cast<OverflowStrategy>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRSerialzierTest, ValueKindEnum)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::Value;
    Value::ValueKind enumBegin = Value::ValueKind::KIND_LITERAL;
    Value::ValueKind enumEnd = Value::ValueKind::KIND_BLOCK_GROUP; // make sure this is max one we defined
    EXPECT_EQ(static_cast<size_t>(enumBegin), 0);
    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(
            PackageFormat::ValueKind(static_cast<Value::ValueKind>(i)), Serialize(static_cast<Value::ValueKind>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRSerialzierTest, ConstantValueKindEnum)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::ConstantValueKind;
    ConstantValueKind enumBegin = ConstantValueKind::KIND_BOOL;
    ConstantValueKind enumEnd = ConstantValueKind::KIND_FUNC; // make sure this is max one we defined
    EXPECT_EQ(static_cast<size_t>(enumBegin), 0);
    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(PackageFormat::ConstantValueKind(static_cast<ConstantValueKind>(i)),
            Serialize(static_cast<ConstantValueKind>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRSerialzierTest, FuncKindEnum)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::FuncKind;
    FuncKind enumBegin = FuncKind::DEFAULT;
    FuncKind enumEnd =
        FuncKind::INSTANCEVAR_INIT; // make sure this is max one we defined exclude pseudo-value FUNCKIND_END
    EXPECT_EQ(static_cast<size_t>(enumBegin), 0);
    EXPECT_EQ(static_cast<size_t>(enumEnd) + 1, static_cast<size_t>(FuncKind::FUNCKIND_END));
    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(PackageFormat::FuncKind(static_cast<FuncKind>(i)), Serialize(static_cast<FuncKind>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRSerialzierTest, CustomDefKindEnum)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::CustomDefKind;
    CustomDefKind enumBegin = CustomDefKind::TYPE_STRUCT;
    CustomDefKind enumEnd = CustomDefKind::TYPE_EXTEND; // make sure this is max one we defined
    EXPECT_EQ(static_cast<size_t>(enumBegin), 0);

    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(PackageFormat::CustomDefKind(static_cast<CustomDefKind>(i)), Serialize(static_cast<CustomDefKind>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRSerialzierTest, ExprKindEnum)
{
    using namespace PackageFormat;
    ExprKind enumBegin = ExprKind::INVALID;
    ExprKind enumEnd =
        ExprKind::GET_RTTI_STATIC; // make sure this is max one we defined exclude pseudo-value MAX_EXPR_KINDS
    EXPECT_EQ(static_cast<size_t>(enumBegin), 0);
    EXPECT_EQ(static_cast<size_t>(enumEnd) + 1, static_cast<size_t>(ExprKind::MAX_EXPR_KINDS));
    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(PackageFormat::CHIRExprKind(static_cast<ExprKind>(i)), Serialize(static_cast<ExprKind>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRSerialzierTest, IntrinsicKindEnum)
{
    using namespace PackageFormat;
    Cangjie::CHIR::IntrinsicKind enumBegin = NOT_INTRINSIC;
    Cangjie::CHIR::IntrinsicKind enumEnd = IS_SUBTYPE_TYPES; // make sure this is max one we defined

    EXPECT_EQ(static_cast<size_t>(enumBegin), 0);
    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(PackageFormat::IntrinsicKind(static_cast<Cangjie::CHIR::IntrinsicKind>(i)),
            Serialize(static_cast<Cangjie::CHIR::IntrinsicKind>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRSerialzierTest, PackageAccessLevelEnum)
{
    using namespace PackageFormat;
    Package::AccessLevel enumBegin = Package::AccessLevel::INVALID;
    Package::AccessLevel enumEnd = Package::AccessLevel::PUBLIC; // make sure this is max one we defined

    EXPECT_EQ(static_cast<size_t>(enumBegin), 0);
    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(PackageFormat::PackageAccessLevel(static_cast<Package::AccessLevel>(i)),
            Serialize(static_cast<Package::AccessLevel>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRSerialzierTest, Phase)
{
    using namespace PackageFormat;
    ToCHIR::Phase enumBegin = ToCHIR::Phase::PHASE_MIN;
    ToCHIR::Phase enumEnd = ToCHIR::Phase::PHASE_MAX; // make sure this is max one we defined

    EXPECT_EQ(static_cast<size_t>(enumBegin), 0);
    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(PackageFormat::Phase(static_cast<ToCHIR::Phase>(i)), Serialize(static_cast<ToCHIR::Phase>(i)))
            << "cur i: " << i;
    }
}
