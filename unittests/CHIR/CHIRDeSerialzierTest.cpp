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
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/IntrinsicKind.h"
#include "cangjie/CHIR/Serializer/CHIRDeserializer.h"
#include "cangjie/CHIR/Type/CustomTypeDef.h"
#include "cangjie/CHIR/Type/Type.h"

using namespace Cangjie::CHIR;

class CHIRDeSerialzierTest : public ::testing::Test {
protected:
    void SetUp() override
    {
    }
};

Type::TypeKind DeSerialize(const PackageFormat::CHIRTypeKind& kind)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::Type;
    auto ret = Type::TypeKind::TYPE_INVALID;
    switch (kind) {
        case CHIRTypeKind_INVALID:
            ret = Type::TypeKind::TYPE_INVALID;
            break;
            // integer
        case CHIRTypeKind_INT8:
            ret = Type::TypeKind::TYPE_INT8;
            break;
        case CHIRTypeKind_INT16:
            ret = Type::TypeKind::TYPE_INT16;
            break;
        case CHIRTypeKind_INT32:
            ret = Type::TypeKind::TYPE_INT32;
            break;
        case CHIRTypeKind_INT64:
            ret = Type::TypeKind::TYPE_INT64;
            break;
        case CHIRTypeKind_INT_NATIVE:
            ret = Type::TypeKind::TYPE_INT_NATIVE;
            break;
            // unsigned integer
        case CHIRTypeKind_UINT8:
            ret = Type::TypeKind::TYPE_UINT8;
            break;
        case CHIRTypeKind_UINT16:
            ret = Type::TypeKind::TYPE_UINT16;
            break;
        case CHIRTypeKind_UINT32:
            ret = Type::TypeKind::TYPE_UINT32;
            break;
        case CHIRTypeKind_UINT64:
            ret = Type::TypeKind::TYPE_UINT64;
            break;
        case CHIRTypeKind_UINT_NATIVE:
            ret = Type::TypeKind::TYPE_UINT_NATIVE;
            break;
            // float
        case CHIRTypeKind_FLOAT16:
            ret = Type::TypeKind::TYPE_FLOAT16;
            break;
        case CHIRTypeKind_FLOAT32:
            ret = Type::TypeKind::TYPE_FLOAT32;
            break;
        case CHIRTypeKind_FLOAT64:
            ret = Type::TypeKind::TYPE_FLOAT64;
            break;
            // other primitive type
        case CHIRTypeKind_RUNE:
            ret = Type::TypeKind::TYPE_RUNE;
            break;
        case CHIRTypeKind_BOOLEAN:
            ret = Type::TypeKind::TYPE_BOOLEAN;
            break;
        case CHIRTypeKind_UNIT:
            ret = Type::TypeKind::TYPE_UNIT;
            break;
        case CHIRTypeKind_NOTHING:
            ret = Type::TypeKind::TYPE_NOTHING;
            break;
            // Void type
        case CHIRTypeKind_VOID:
            ret = Type::TypeKind::TYPE_VOID;
            break;
            // composite type
        case CHIRTypeKind_TUPLE:
            ret = Type::TypeKind::TYPE_TUPLE;
            break;
        case CHIRTypeKind_STRUCT:
            ret = Type::TypeKind::TYPE_STRUCT;
            break;
        case CHIRTypeKind_ENUM:
            ret = Type::TypeKind::TYPE_ENUM;
            break;
        case CHIRTypeKind_FUNC:
            ret = Type::TypeKind::TYPE_FUNC;
            break;
        case CHIRTypeKind_CLASS:
            ret = Type::TypeKind::TYPE_CLASS;
            break;
            // Built-in array related type
        case CHIRTypeKind_RAWARRAY:
            ret = Type::TypeKind::TYPE_RAWARRAY;
            break;
        case CHIRTypeKind_VARRAY:
            ret = Type::TypeKind::TYPE_VARRAY;
            break;
            // Built-in CFFI related type
        case CHIRTypeKind_C_POINTER:
            ret = Type::TypeKind::TYPE_CPOINTER;
            break;
        case CHIRTypeKind_C_STRING:
            ret = Type::TypeKind::TYPE_CSTRING;
            break;
            // Generic type
        case CHIRTypeKind_GENERIC:
            ret = Type::TypeKind::TYPE_GENERIC;
            break;
            // Referece to an value with abritray type
        case CHIRTypeKind_REFTYPE:
            ret = Type::TypeKind::TYPE_REFTYPE;
            break;
            // Built-in box type
        case CHIRTypeKind_BOXTYPE:
            ret = Type::TypeKind::TYPE_BOXTYPE;
            break;
        case CHIRTypeKind_THIS:
            ret = Type::TypeKind::TYPE_THIS;
            break;
            // no defalut here, due to we need use compiler to check all value be handled.
    }
    return ret;
}

SourceExpr DeSerialize(const PackageFormat::SourceExpr& kind)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::SourceExpr;
    auto ret = SourceExpr::IF_EXPR;
    switch (kind) {
        case SourceExpr_IF_EXPR:
            ret = SourceExpr::IF_EXPR;
            break;
        case SourceExpr_WHILE_EXPR:
            ret = SourceExpr::WHILE_EXPR;
            break;
        case SourceExpr_DO_WHILE_EXPR:
            ret = SourceExpr::DO_WHILE_EXPR;
            break;
        case SourceExpr_MATCH_EXPR:
            ret = SourceExpr::MATCH_EXPR;
            break;
        case SourceExpr_IF_LET_OR_WHILE_LET:
            ret = SourceExpr::IF_LET_OR_WHILE_LET;
            break;
        case SourceExpr_QUEST:
            ret = SourceExpr::QUEST;
            break;
        case SourceExpr_BINARY:
            ret = SourceExpr::BINARY;
            break;
        case SourceExpr_FOR_IN_EXPR:
            ret = SourceExpr::FOR_IN_EXPR;
            break;
        case SourceExpr_OTHER:
            ret = SourceExpr::OTHER;
            break;
    }
    return ret;
}

Cangjie::Linkage DeSerialize(const PackageFormat::Linkage& kind)
{
    using namespace PackageFormat;
    using Cangjie::Linkage;
    auto ret = Cangjie::Linkage::WEAK_ODR;
    switch (kind) {
        case Linkage_WEAK_ODR:
            ret = Cangjie::Linkage::WEAK_ODR;
            break;
        case Linkage_EXTERNAL:
            ret = Cangjie::Linkage::EXTERNAL;
            break;
        case Linkage_INTERNAL:
            ret = Cangjie::Linkage::INTERNAL;
            break;
        case Linkage_LINKONCE_ODR:
            ret = Cangjie::Linkage::LINKONCE_ODR;
            break;
        case Linkage_EXTERNAL_WEAK:
            ret = Cangjie::Linkage::EXTERNAL_WEAK;
            break;
    }
    return ret;
}

Cangjie::CHIR::SkipKind DeSerialize(const PackageFormat::SkipKind& kind)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::SkipKind;
    auto ret = SkipKind::NO_SKIP;
    switch (kind) {
        case SkipKind_NO_SKIP:
            ret = SkipKind::NO_SKIP;
            break;
        case SkipKind_SKIP_DCE_WARNING:
            ret = SkipKind::SKIP_DCE_WARNING;
            break;
        case SkipKind_SKIP_FORIN_EXIT:
            ret = SkipKind::SKIP_FORIN_EXIT;
            break;
        case SkipKind_SKIP_VIC:
            ret = SkipKind::SKIP_VIC;
            break;
    }
    return ret;
}

Cangjie::OverflowStrategy DeSerialize(const PackageFormat::OverflowStrategy& kind)
{
    using namespace PackageFormat;
    using Cangjie::OverflowStrategy;
    auto ret = OverflowStrategy::NA;
    switch (kind) {
        case OverflowStrategy_NA:
            ret = OverflowStrategy::NA;
            break;
        case OverflowStrategy_CHECKED:
            ret = OverflowStrategy::CHECKED;
            break;
        case OverflowStrategy_WRAPPING:
            ret = OverflowStrategy::WRAPPING;
            break;
        case OverflowStrategy_THROWING:
            ret = OverflowStrategy::THROWING;
            break;
        case OverflowStrategy_SATURATING:
            ret = OverflowStrategy::SATURATING;
            break;
    }
    return ret;
}

Value::ValueKind DeSerialize(const PackageFormat::ValueKind& kind)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::Value;
    auto ret = Value::ValueKind::KIND_LITERAL;
    switch (kind) {
        case ValueKind_LITERAL:
            ret = Value::ValueKind::KIND_LITERAL;
            break;
        case ValueKind_GLOBALVAR:
            ret = Value::ValueKind::KIND_GLOBALVAR;
            break;
        case ValueKind_PARAMETER:
            ret = Value::ValueKind::KIND_PARAMETER;
            break;
        case ValueKind_IMPORTED_FUNC:
            ret = Value::ValueKind::KIND_IMP_FUNC;
            break;
        case ValueKind_IMPORTED_VAR:
            ret = Value::ValueKind::KIND_IMP_VAR;
            break;
        case ValueKind_LOCALVAR:
            ret = Value::ValueKind::KIND_LOCALVAR;
            break;
        case ValueKind_FUNC:
            ret = Value::ValueKind::KIND_FUNC;
            break;
        case ValueKind_BLOCK:
            ret = Value::ValueKind::KIND_BLOCK;
            break;
        case ValueKind_BLOCK_GROUP:
            ret = Value::ValueKind::KIND_BLOCK_GROUP;
            break;
    }
    return ret;
}

ConstantValueKind DeSerialize(const PackageFormat::ConstantValueKind& kind)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::ConstantValueKind;
    auto ret = ConstantValueKind::KIND_BOOL;
    switch (kind) {
        case ConstantValueKind_BOOL:
            ret = ConstantValueKind::KIND_BOOL;
            break;
        case ConstantValueKind_RUNE:
            ret = ConstantValueKind::KIND_RUNE;
            break;
        case ConstantValueKind_INT:
            ret = ConstantValueKind::KIND_INT;
            break;
        case ConstantValueKind_FLOAT:
            ret = ConstantValueKind::KIND_FLOAT;
            break;
        case ConstantValueKind_STRING:
            ret = ConstantValueKind::KIND_STRING;
            break;
        case ConstantValueKind_UNIT:
            ret = ConstantValueKind::KIND_UNIT;
            break;
        case ConstantValueKind_NULL:
            ret = ConstantValueKind::KIND_NULL;
            break;
        case ConstantValueKind_FUNC:
            ret = ConstantValueKind::KIND_FUNC;
            break;
    }
    return ret;
}

FuncKind DeSerialize(const PackageFormat::FuncKind& kind)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::FuncKind;
    auto ret = FuncKind::DEFAULT;
    switch (kind) {
        case FuncKind_DEFAULT:
            ret = FuncKind::DEFAULT;
            break;
        case FuncKind_GETTER:
            ret = FuncKind::GETTER;
            break;
        case FuncKind_SETTER:
            ret = FuncKind::SETTER;
            break;
        case FuncKind_LAMBDA:
            ret = FuncKind::LAMBDA;
            break;
        case FuncKind_CLASS_CONSTRUCTOR:
            ret = FuncKind::CLASS_CONSTRUCTOR;
            break;
        case FuncKind_PRIMAL_CLASS_CONSTRUCTOR:
            ret = FuncKind::PRIMAL_CLASS_CONSTRUCTOR;
            break;
        case FuncKind_STRUCT_CONSTRUCTOR:
            ret = FuncKind::STRUCT_CONSTRUCTOR;
            break;
        case FuncKind_PRIMAL_STRUCT_CONSTRUCTOR:
            ret = FuncKind::PRIMAL_STRUCT_CONSTRUCTOR;
            break;
        case FuncKind_GLOBALVAR_INIT:
            ret = FuncKind::GLOBALVAR_INIT;
            break;
        case FuncKind_FINALIZER:
            ret = FuncKind::FINALIZER;
            break;
        case FuncKind_MAIN_ENTRY:
            ret = FuncKind::MAIN_ENTRY;
            break;
        case FuncKind_ANNOFACTORY_FUNC:
            ret = FuncKind::ANNOFACTORY_FUNC;
            break;
        case FuncKind_MACRO_FUNC:
            ret = FuncKind::MACRO_FUNC;
            break;
        case FuncKind_DEFAULT_PARAMETER_FUNC:
            ret = FuncKind::DEFAULT_PARAMETER_FUNC;
            break;
        case FuncKind_INSTANCEVAR_INIT:
            ret = FuncKind::INSTANCEVAR_INIT;
            break;
    }
    return ret;
}

CustomDefKind DeSerialize(const PackageFormat::CustomDefKind& kind)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::CustomDefKind;
    auto ret = CustomDefKind::TYPE_STRUCT;
    switch (kind) {
        case CustomDefKind_STRUCT:
            ret = CustomDefKind::TYPE_STRUCT;
            break;
        case CustomDefKind_ENUM:
            ret = CustomDefKind::TYPE_ENUM;
            break;
        case CustomDefKind_CLASS:
            ret = CustomDefKind::TYPE_CLASS;
            break;
        case CustomDefKind_EXTEND:
            ret = CustomDefKind::TYPE_EXTEND;
            break;
    }
    return ret;
}

ExprKind DeSerialize(const PackageFormat::CHIRExprKind& kind)
{
    using namespace PackageFormat;
    auto ret = ExprKind::INVALID;
    switch (kind) {
        case CHIRExprKind_INVALID:
            ret = ExprKind::INVALID;
            break;
        // Terminator
        case CHIRExprKind_GOTO:
            ret = ExprKind::GOTO;
            break;
        case CHIRExprKind_BRANCH:
            ret = ExprKind::BRANCH;
            break;
        case CHIRExprKind_MULTIBRANCH:
            ret = ExprKind::MULTIBRANCH;
            break;
        case CHIRExprKind_EXIT:
            ret = ExprKind::EXIT;
            break;
        case CHIRExprKind_APPLY_WITH_EXCEPTION:
            ret = ExprKind::APPLY_WITH_EXCEPTION;
            break;
        case CHIRExprKind_INVOKE_WITH_EXCEPTION:
            ret = ExprKind::INVOKE_WITH_EXCEPTION;
            break;
        case CHIRExprKind_INVOKESTATIC_WITH_EXCEPTION:
            ret = ExprKind::INVOKESTATIC_WITH_EXCEPTION;
            break;
        case CHIRExprKind_RAISE_EXCEPTION:
            ret = ExprKind::RAISE_EXCEPTION;
            break;
        case CHIRExprKind_INT_OP_WITH_EXCEPTION:
            ret = ExprKind::INT_OP_WITH_EXCEPTION;
            break;
        case CHIRExprKind_SPAWN_WITH_EXCEPTION:
            ret = ExprKind::SPAWN_WITH_EXCEPTION;
            break;
        case CHIRExprKind_TYPECAST_WITH_EXCEPTION:
            ret = ExprKind::TYPECAST_WITH_EXCEPTION;
            break;
        case CHIRExprKind_INTRINSIC_WITH_EXCEPTION:
            ret = ExprKind::INTRINSIC_WITH_EXCEPTION;
            break;
        case CHIRExprKind_ALLOCATE_WITH_EXCEPTION:
            ret = ExprKind::ALLOCATE_WITH_EXCEPTION;
            break;
        case CHIRExprKind_RAW_ARRAY_ALLOCATE_WITH_EXCEPTION:
            ret = ExprKind::RAW_ARRAY_ALLOCATE_WITH_EXCEPTION;
            break;
            // Unary
        case CHIRExprKind_NEG:
            ret = ExprKind::NEG;
            break;
        case CHIRExprKind_NOT:
            ret = ExprKind::NOT;
            break;
        case CHIRExprKind_BITNOT:
            ret = ExprKind::BITNOT;
            break;
            // Binary
        case CHIRExprKind_ADD:
            ret = ExprKind::ADD;
            break;
        case CHIRExprKind_SUB:
            ret = ExprKind::SUB;
            break;
        case CHIRExprKind_MUL:
            ret = ExprKind::MUL;
            break;
        case CHIRExprKind_DIV:
            ret = ExprKind::DIV;
            break;
        case CHIRExprKind_MOD:
            ret = ExprKind::MOD;
            break;
        case CHIRExprKind_EXP:
            ret = ExprKind::EXP;
            break;
        case CHIRExprKind_LSHIFT:
            ret = ExprKind::LSHIFT;
            break;
        case CHIRExprKind_RSHIFT:
            ret = ExprKind::RSHIFT;
            break;
        case CHIRExprKind_BITAND:
            ret = ExprKind::BITAND;
            break;
        case CHIRExprKind_BITOR:
            ret = ExprKind::BITOR;
            break;
        case CHIRExprKind_BITXOR:
            ret = ExprKind::BITXOR;
            break;
        case CHIRExprKind_LT:
            ret = ExprKind::LT;
            break;
        case CHIRExprKind_GT:
            ret = ExprKind::GT;
            break;
        case CHIRExprKind_LE:
            ret = ExprKind::LE;
            break;
        case CHIRExprKind_GE:
            ret = ExprKind::GE;
            break;
        case CHIRExprKind_EQUAL:
            ret = ExprKind::EQUAL;
            break;
        case CHIRExprKind_NOTEQUAL:
            ret = ExprKind::NOTEQUAL;
            break;
        case CHIRExprKind_AND:
            ret = ExprKind::AND;
            break;
        case CHIRExprKind_OR:
            ret = ExprKind::OR;
            break;
            // Memory
        case CHIRExprKind_ALLOCATE:
            ret = ExprKind::ALLOCATE;
            break;
        case CHIRExprKind_LOAD:
            ret = ExprKind::LOAD;
            break;
        case CHIRExprKind_STORE:
            ret = ExprKind::STORE;
            break;
        case CHIRExprKind_GET_ELEMENT_REF:
            ret = ExprKind::GET_ELEMENT_REF;
            break;
        case CHIRExprKind_GET_ELEMENT_BY_NAME:
            ret = ExprKind::GET_ELEMENT_BY_NAME;
            break;
        case CHIRExprKind_STORE_ELEMENT_REF:
            ret = ExprKind::STORE_ELEMENT_REF;
            break;
        case CHIRExprKind_STORE_ELEMENT_BY_NAME:
            ret = ExprKind::STORE_ELEMENT_BY_NAME;
            break;
            // Complext
        case CHIRExprKind_IF:
            ret = ExprKind::IF;
            break;
        case CHIRExprKind_LOOP:
            ret = ExprKind::LOOP;
            break;
        case CHIRExprKind_FORIN_RANGE:
            ret = ExprKind::FORIN_RANGE;
            break;
        case CHIRExprKind_FORIN_ITER:
            ret = ExprKind::FORIN_ITER;
            break;
        case CHIRExprKind_FORIN_CLOSED_RANGE:
            ret = ExprKind::FORIN_CLOSED_RANGE;
            break;
        case CHIRExprKind_LAMBDA:
            ret = ExprKind::LAMBDA;
            break;
            // Others
        case CHIRExprKind_CONSTANT:
            ret = ExprKind::CONSTANT;
            break;
        case CHIRExprKind_DEBUGEXPR:
            ret = ExprKind::DEBUGEXPR;
            break;
        case CHIRExprKind_TUPLE:
            ret = ExprKind::TUPLE;
            break;
        case CHIRExprKind_FIELD:
            ret = ExprKind::FIELD;
            break;
        case CHIRExprKind_FIELD_BY_NAME:
            ret = ExprKind::FIELD_BY_NAME;
            break;
        case CHIRExprKind_APPLY:
            ret = ExprKind::APPLY;
            break;
        case CHIRExprKind_INVOKE:
            ret = ExprKind::INVOKE;
            break;
        case CHIRExprKind_INVOKE_STATIC:
            ret = ExprKind::INVOKESTATIC;
            break;
        case CHIRExprKind_INSTANCEOF:
            ret = ExprKind::INSTANCEOF;
            break;
        case CHIRExprKind_TYPECAST:
            ret = ExprKind::TYPECAST;
            break;
        case CHIRExprKind_GET_EXCEPTION:
            ret = ExprKind::GET_EXCEPTION;
            break;
        case CHIRExprKind_RAW_ARRAY_ALLOCATE:
            ret = ExprKind::RAW_ARRAY_ALLOCATE;
            break;
        case CHIRExprKind_RAW_ARRAY_LITERAL_INIT:
            ret = ExprKind::RAW_ARRAY_LITERAL_INIT;
            break;
        case CHIRExprKind_RAW_ARRAY_INIT_BY_VALUE:
            ret = ExprKind::RAW_ARRAY_INIT_BY_VALUE;
            break;
        case CHIRExprKind_VARRAY:
            ret = ExprKind::VARRAY;
            break;
        case CHIRExprKind_VARRAY_BUILDER:
            ret = ExprKind::VARRAY_BUILDER;
            break;
        case CHIRExprKind_INTRINSIC:
            ret = ExprKind::INTRINSIC;
            break;
        case CHIRExprKind_SPAWN:
            ret = ExprKind::SPAWN;
            break;
        case CHIRExprKind_GET_INSTANTIATE_VALUE:
            ret = ExprKind::GET_INSTANTIATE_VALUE;
            break;
        case CHIRExprKind_BOX:
            ret = ExprKind::BOX;
            break;
        case CHIRExprKind_UNBOX:
            ret = ExprKind::UNBOX;
            break;
        case CHIRExprKind_TRANSFORM_TO_GENERIC:
            ret = ExprKind::TRANSFORM_TO_GENERIC;
            break;
        case CHIRExprKind_TRANSFORM_TO_CONCRETE:
            ret = ExprKind::TRANSFORM_TO_CONCRETE;
            break;
        case CHIRExprKind_UNBOX_TO_REF:
            ret = ExprKind::UNBOX_TO_REF;
            break;
        case CHIRExprKind_GET_RTTI:
            ret = ExprKind::GET_RTTI;
            break;
        case CHIRExprKind_GET_RTTI_STATIC:
            ret = ExprKind::GET_RTTI_STATIC;
            break;
    }
    return ret;
}

IntrinsicKind DeSerialize(const PackageFormat::IntrinsicKind& kind)
{
    using namespace PackageFormat;
    auto ret = NOT_INTRINSIC;
    switch (kind) {
        case IntrinsicKind_NOT_INTRINSIC:
            ret = NOT_INTRINSIC;
            break;
        case IntrinsicKind_NOT_IMPLEMENTED:
            ret = NOT_IMPLEMENTED;
            break;
        case IntrinsicKind_ARRAY_INIT:
            ret = ARRAY_INIT;
            break;
        // CORE
        case IntrinsicKind_SIZE_OF:
            ret = SIZE_OF;
            break;
        case IntrinsicKind_ALIGN_OF:
            ret = ALIGN_OF;
            break;
        case IntrinsicKind_ARRAY_ACQUIRE_RAW_DATA:
            ret = ARRAY_ACQUIRE_RAW_DATA;
            break;
        case IntrinsicKind_ARRAY_RELEASE_RAW_DATA:
            ret = ARRAY_RELEASE_RAW_DATA;
            break;
        case IntrinsicKind_ARRAY_BUILT_IN_COPY_TO:
            ret = ARRAY_BUILT_IN_COPY_TO;
            break;
        case IntrinsicKind_ARRAY_GET:
            ret = ARRAY_GET;
            break;
        case IntrinsicKind_ARRAY_SET:
            ret = ARRAY_SET;
            break;
        case IntrinsicKind_ARRAY_GET_UNCHECKED:
            ret = ARRAY_GET_UNCHECKED;
            break;
        case IntrinsicKind_ARRAY_GET_REF_UNCHECKED:
            ret = ARRAY_GET_REF_UNCHECKED;
            break;
        case IntrinsicKind_ARRAY_SET_UNCHECKED:
            ret = ARRAY_SET_UNCHECKED;
            break;
        case IntrinsicKind_ARRAY_SIZE:
            ret = ARRAY_SIZE;
            break;
        case IntrinsicKind_ARRAY_CLONE:
            ret = ARRAY_CLONE;
            break;
        case IntrinsicKind_ARRAY_SLICE_INIT:
            ret = ARRAY_SLICE_INIT;
            break;
        case IntrinsicKind_ARRAY_SLICE:
            ret = ARRAY_SLICE;
            break;
        case IntrinsicKind_ARRAY_SLICE_RAWARRAY:
            ret = ARRAY_SLICE_RAWARRAY;
            break;
        case IntrinsicKind_ARRAY_SLICE_START:
            ret = ARRAY_SLICE_START;
            break;
        case IntrinsicKind_ARRAY_SLICE_SIZE:
            ret = ARRAY_SLICE_SIZE;
            break;
        case IntrinsicKind_ARRAY_SLICE_GET_ELEMENT:
            ret = ARRAY_SLICE_GET_ELEMENT;
            break;
        case IntrinsicKind_ARRAY_SLICE_GET_ELEMENT_UNCHECKED:
            ret = ARRAY_SLICE_GET_ELEMENT_UNCHECKED;
            break;
        case IntrinsicKind_ARRAY_SLICE_SET_ELEMENT:
            ret = ARRAY_SLICE_SET_ELEMENT;
            break;
        case IntrinsicKind_ARRAY_SLICE_SET_ELEMENT_UNCHECKED:
            ret = ARRAY_SLICE_SET_ELEMENT_UNCHECKED;
            break;
        case IntrinsicKind_FILL_IN_STACK_TRACE:
            ret = FILL_IN_STACK_TRACE;
            break;
        case IntrinsicKind_DECODE_STACK_TRACE:
            ret = DECODE_STACK_TRACE;
            break;
        case IntrinsicKind_DUMP_CURRENT_THREAD_INFO:
            ret = DUMP_CURRENT_THREAD_INFO;
            break;
        case IntrinsicKind_DUMP_ALL_THREADS_INFO:
            ret = DUMP_ALL_THREADS_INFO;
            break;
        case IntrinsicKind_CHR:
            ret = CHR;
            break;
        case IntrinsicKind_ORD:
            ret = ORD;
            break;
        case IntrinsicKind_CPOINTER_GET_POINTER_ADDRESS:
            ret = CPOINTER_GET_POINTER_ADDRESS;
            break;
        case IntrinsicKind_CPOINTER_INIT0:
            ret = CPOINTER_INIT0;
            break; // CPointer constructor with no arguments
        case IntrinsicKind_CPOINTER_INIT1:
            ret = CPOINTER_INIT1;
            break; // CPointer constructor with one argument
        case IntrinsicKind_CPOINTER_READ:
            ret = CPOINTER_READ;
            break;
        case IntrinsicKind_CPOINTER_WRITE:
            ret = CPOINTER_WRITE;
            break;
        case IntrinsicKind_CPOINTER_ADD:
            ret = CPOINTER_ADD;
            break;
        case IntrinsicKind_CSTRING_INIT:
            ret = CSTRING_INIT;
            break;
        case IntrinsicKind_CSTRING_CONVERT_CSTR_TO_PTR:
            ret = CSTRING_CONVERT_CSTR_TO_PTR;
            break;
        case IntrinsicKind_INOUT_PARAM:
            ret = INOUT_PARAM;
            break;
        case IntrinsicKind_REGISTER_WATCHED_OBJECT:
            ret = REGISTER_WATCHED_OBJECT;
            break;
        case IntrinsicKind_OBJECT_REFEQ:
            ret = OBJECT_REFEQ;
            break;
        case IntrinsicKind_RAW_ARRAY_REFEQ: // cjnative only
            ret = RAW_ARRAY_REFEQ;
            break;
        case IntrinsicKind_OBJECT_ZERO_VALUE:
            ret = OBJECT_ZERO_VALUE;
            break;
        case IntrinsicKind_INVOKE_GC:
            ret = INVOKE_GC;
            break;
        case IntrinsicKind_SET_GC_THRESHOLD:
            ret = SET_GC_THRESHOLD;
            break;
        case IntrinsicKind_DUMP_CJ_HEAP_DATA:
            ret = DUMP_CJ_HEAP_DATA;
            break;
        case IntrinsicKind_GET_GC_COUNT:
            ret = GET_GC_COUNT;
            break;
        case IntrinsicKind_GET_GC_TIME_US:
            ret = GET_GC_TIME_US;
            break;
        case IntrinsicKind_GET_GC_FREED_SIZE:
            ret = GET_GC_FREED_SIZE;
            break;
        case IntrinsicKind_START_CJ_CPU_PROFILING:
            ret = START_CJ_CPU_PROFILING;
            break;
        case IntrinsicKind_STOP_CJ_CPU_PROFILING:
            ret = STOP_CJ_CPU_PROFILING;
            break;
        case IntrinsicKind_BLACK_BOX:
            ret = BLACK_BOX;
            break;
        case IntrinsicKind_GET_MAX_HEAP_SIZE:
            ret = GET_MAX_HEAP_SIZE;
            break;
        case IntrinsicKind_GET_ALLOCATE_HEAP_SIZE:
            ret = GET_ALLOCATE_HEAP_SIZE;
            break;
        case IntrinsicKind_GET_REAL_HEAP_SIZE:
            ret = GET_REAL_HEAP_SIZE;
            break;
        case IntrinsicKind_GET_THREAD_NUMBER:
            ret = GET_THREAD_NUMBER;
            break;
        case IntrinsicKind_GET_BLOCKING_THREAD_NUMBER:
            ret = GET_BLOCKING_THREAD_NUMBER;
            break;
        case IntrinsicKind_GET_NATIVE_THREAD_NUMBER:
            ret = GET_NATIVE_THREAD_NUMBER;
            break;
        case IntrinsicKind_VARRAY_SET:
            ret = VARRAY_SET;
            break;
        case IntrinsicKind_VARRAY_GET:
            ret = VARRAY_GET;
            break;
        // About Future
        case IntrinsicKind_FUTURE_INIT:
            ret = FUTURE_INIT;
            break;
        case IntrinsicKind_FUTURE_IS_COMPLETE: // cjnative only
            ret = FUTURE_IS_COMPLETE;
            break;
        case IntrinsicKind_FUTURE_WAIT: // cjnative only
            ret = FUTURE_WAIT;
            break;
        case IntrinsicKind_FUTURE_NOTIFYALL: // cjnative only
            ret = FUTURE_NOTIFYALL;
            break;
        case IntrinsicKind_IS_THREAD_OBJECT_INITED:
            ret = IS_THREAD_OBJECT_INITED;
            break;
        case IntrinsicKind_GET_THREAD_OBJECT:
            ret = GET_THREAD_OBJECT;
            break;
        case IntrinsicKind_SET_THREAD_OBJECT:
            ret = SET_THREAD_OBJECT;
            break;
        case IntrinsicKind_OVERFLOW_CHECKED_ADD:
            ret = OVERFLOW_CHECKED_ADD;
            break;
        case IntrinsicKind_OVERFLOW_CHECKED_SUB:
            ret = OVERFLOW_CHECKED_SUB;
            break;
        case IntrinsicKind_OVERFLOW_CHECKED_MUL:
            ret = OVERFLOW_CHECKED_MUL;
            break;
        case IntrinsicKind_OVERFLOW_CHECKED_DIV:
            ret = OVERFLOW_CHECKED_DIV;
            break;
        case IntrinsicKind_OVERFLOW_CHECKED_MOD:
            ret = OVERFLOW_CHECKED_MOD;
            break;
        case IntrinsicKind_OVERFLOW_CHECKED_POW:
            ret = OVERFLOW_CHECKED_POW;
            break;
        case IntrinsicKind_OVERFLOW_CHECKED_INC:
            ret = OVERFLOW_CHECKED_INC;
            break;
        case IntrinsicKind_OVERFLOW_CHECKED_DEC:
            ret = OVERFLOW_CHECKED_DEC;
            break;
        case IntrinsicKind_OVERFLOW_CHECKED_NEG:
            ret = OVERFLOW_CHECKED_NEG;
            break;
        case IntrinsicKind_OVERFLOW_THROWING_ADD:
            ret = OVERFLOW_THROWING_ADD;
            break;
        case IntrinsicKind_OVERFLOW_THROWING_SUB:
            ret = OVERFLOW_THROWING_SUB;
            break;
        case IntrinsicKind_OVERFLOW_THROWING_MUL:
            ret = OVERFLOW_THROWING_MUL;
            break;
        case IntrinsicKind_OVERFLOW_THROWING_DIV:
            ret = OVERFLOW_THROWING_DIV;
            break;
        case IntrinsicKind_OVERFLOW_THROWING_MOD:
            ret = OVERFLOW_THROWING_MOD;
            break;
        case IntrinsicKind_OVERFLOW_THROWING_POW:
            ret = OVERFLOW_THROWING_POW;
            break;
        case IntrinsicKind_OVERFLOW_THROWING_INC:
            ret = OVERFLOW_THROWING_INC;
            break;
        case IntrinsicKind_OVERFLOW_THROWING_DEC:
            ret = OVERFLOW_THROWING_DEC;
            break;
        case IntrinsicKind_OVERFLOW_THROWING_NEG:
            ret = OVERFLOW_THROWING_NEG;
            break;
        case IntrinsicKind_OVERFLOW_SATURATING_ADD:
            ret = OVERFLOW_SATURATING_ADD;
            break;
        case IntrinsicKind_OVERFLOW_SATURATING_SUB:
            ret = OVERFLOW_SATURATING_SUB;
            break;
        case IntrinsicKind_OVERFLOW_SATURATING_MUL:
            ret = OVERFLOW_SATURATING_MUL;
            break;
        case IntrinsicKind_OVERFLOW_SATURATING_DIV:
            ret = OVERFLOW_SATURATING_DIV;
            break;
        case IntrinsicKind_OVERFLOW_SATURATING_MOD:
            ret = OVERFLOW_SATURATING_MOD;
            break;
        case IntrinsicKind_OVERFLOW_SATURATING_POW:
            ret = OVERFLOW_SATURATING_POW;
            break;
        case IntrinsicKind_OVERFLOW_SATURATING_INC:
            ret = OVERFLOW_SATURATING_INC;
            break;
        case IntrinsicKind_OVERFLOW_SATURATING_DEC:
            ret = OVERFLOW_SATURATING_DEC;
            break;
        case IntrinsicKind_OVERFLOW_SATURATING_NEG:
            ret = OVERFLOW_SATURATING_NEG;
            break;
        case IntrinsicKind_OVERFLOW_WRAPPING_ADD:
            ret = OVERFLOW_WRAPPING_ADD;
            break;
        case IntrinsicKind_OVERFLOW_WRAPPING_SUB:
            ret = OVERFLOW_WRAPPING_SUB;
            break;
        case IntrinsicKind_OVERFLOW_WRAPPING_MUL:
            ret = OVERFLOW_WRAPPING_MUL;
            break;
        case IntrinsicKind_OVERFLOW_WRAPPING_DIV:
            ret = OVERFLOW_WRAPPING_DIV;
            break;
        case IntrinsicKind_OVERFLOW_WRAPPING_MOD:
            ret = OVERFLOW_WRAPPING_MOD;
            break;
        case IntrinsicKind_OVERFLOW_WRAPPING_POW:
            ret = OVERFLOW_WRAPPING_POW;
            break;
        case IntrinsicKind_OVERFLOW_WRAPPING_INC:
            ret = OVERFLOW_WRAPPING_INC;
            break;
        case IntrinsicKind_OVERFLOW_WRAPPING_DEC:
            ret = OVERFLOW_WRAPPING_DEC;
            break;
        case IntrinsicKind_OVERFLOW_WRAPPING_NEG:
            ret = OVERFLOW_WRAPPING_NEG;
            break;
        // llvm vector instructions for string optimization
        case IntrinsicKind_VECTOR_COMPARE_32: // cjnative only
            ret = VECTOR_COMPARE_32;
            break;
        case IntrinsicKind_VECTOR_INDEX_BYTE_32: // cjnative only
            ret = VECTOR_INDEX_BYTE_32;
            break;
        case IntrinsicKind_CJ_CORE_CAN_USE_SIMD: // cjnative only
            ret = CJ_CORE_CAN_USE_SIMD;
            break;
        case IntrinsicKind_CJ_TLS_DYN_SET_SESSION_CALLBACK:
            ret = CJ_TLS_DYN_SET_SESSION_CALLBACK;
            break;
        case IntrinsicKind_CJ_TLS_DYN_SSL_INIT:
            ret = CJ_TLS_DYN_SSL_INIT;
            break;

        // ============================ cjnative only start =================
        case IntrinsicKind_REFLECTION_INTRINSIC_START_FLAG:
            ret = REFLECTION_INTRINSIC_START_FLAG;
            break;
        case IntrinsicKind_IS_INTERFACE:
            ret = IS_INTERFACE;
            break;
        case IntrinsicKind_IS_CLASS:
            ret = IS_CLASS;
            break;
        case IntrinsicKind_IS_PRIMITIVE:
            ret = IS_PRIMITIVE;
            break;
        case IntrinsicKind_IS_STRUCT:
            ret = IS_STRUCT;
            break;
        case IntrinsicKind_IS_GENERIC:
            ret = IS_GENERIC;
            break;
        case IntrinsicKind_GET_OR_CREATE_TYPEINFO_FOR_REFLECT:
            ret = GET_OR_CREATE_TYPEINFO_FOR_REFLECT;
            break;
        case IntrinsicKind_GET_TYPETEMPLATE:
            ret = GET_TYPETEMPLATE;
            break;
        case IntrinsicKind_CHECK_METHOD_ACTUAL_ARGS:
            ret = CHECK_METHOD_ACTUAL_ARGS;
            break;
        case IntrinsicKind_METHOD_ENTRYPOINT_IS_NULL:
            ret = METHOD_ENTRYPOINT_IS_NULL;
            break;
        case IntrinsicKind_IS_RELECT_UNSUPPORTED_TYPE:
            ret = IS_RELECT_UNSUPPORTED_TYPE;
            break;
        case IntrinsicKind_GET_TYPE_FOR_ANY:
            ret = GET_TYPE_FOR_ANY;
            break;
        case IntrinsicKind_GET_TYPE_BY_MANGLED_NAME:
            ret = GET_TYPE_BY_MANGLED_NAME;
            break;
        case IntrinsicKind_GET_TYPE_NAME:
            ret = GET_TYPE_NAME;
            break;
        case IntrinsicKind_GET_TYPE_BY_QUALIFIED_NAME:
            ret = GET_TYPE_BY_QUALIFIED_NAME;
            break;
        case IntrinsicKind_GET_TYPE_QUALIFIED_NAME_LENGTH:
            ret = GET_TYPE_QUALIFIED_NAME_LENGTH;
            break;
        case IntrinsicKind_GET_TYPE_QUALIFIED_NAME:
            ret = GET_TYPE_QUALIFIED_NAME;
            break;
        case IntrinsicKind_GET_NUM_OF_INTERFACE:
            ret = GET_NUM_OF_INTERFACE;
            break;
        case IntrinsicKind_GET_INTERFACE:
            ret = GET_INTERFACE;
            break;
        case IntrinsicKind_IS_SUBTYPE:
            ret = IS_SUBTYPE;
            break;
        case IntrinsicKind_GET_TYPE_INFO_MODIFIER:
            ret = GET_TYPE_INFO_MODIFIER;
            break;
        case IntrinsicKind_GET_TYPE_INFO_ANNOTATIONS:
            ret = GET_TYPE_INFO_ANNOTATIONS;
            break;
        case IntrinsicKind_GET_OBJ_CLASS:
            ret = GET_OBJ_CLASS;
            break;
        case IntrinsicKind_GET_SUPER_TYPE_INFO:
            ret = GET_SUPER_TYPE_INFO;
            break;

        case IntrinsicKind_GET_NUM_OF_INSTANCE_METHOD_INFOS:
            ret = GET_NUM_OF_INSTANCE_METHOD_INFOS;
            break;
        case IntrinsicKind_GET_INSTANCE_METHOD_INFO:
            ret = GET_INSTANCE_METHOD_INFO;
            break;
        case IntrinsicKind_GET_NUM_OF_STATIC_METHOD_INFOS:
            ret = GET_NUM_OF_STATIC_METHOD_INFOS;
            break;
        case IntrinsicKind_GET_STATIC_METHOD_INFO:
            ret = GET_STATIC_METHOD_INFO;
            break;
        case IntrinsicKind_GET_METHOD_NAME:
            ret = GET_METHOD_NAME;
            break;
        case IntrinsicKind_GET_METHOD_RETURN_TYPE:
            ret = GET_METHOD_RETURN_TYPE;
            break;
        case IntrinsicKind_GET_METHOD_MODIFIER:
            ret = GET_METHOD_MODIFIER;
            break;
        case IntrinsicKind_GET_METHOD_ANNOTATIONS:
            ret = GET_METHOD_ANNOTATIONS;
            break;
        case IntrinsicKind_APPLY_CJ_METHOD:
            ret = APPLY_CJ_METHOD;
            break;
        case IntrinsicKind_APPLY_CJ_STATIC_METHOD:
            ret = APPLY_CJ_STATIC_METHOD;
            break;
        case IntrinsicKind_APPLY_CJ_GENERIC_METHOD:
            ret = APPLY_CJ_GENERIC_METHOD;
            break;
        case IntrinsicKind_APPLY_CJ_GENERIC_STATIC_METHOD:
            ret = APPLY_CJ_GENERIC_STATIC_METHOD;
            break;
        case IntrinsicKind_GET_NUM_OF_ACTUAL_PARAMETERS:
            ret = GET_NUM_OF_ACTUAL_PARAMETERS;
            break;
        case IntrinsicKind_GET_NUM_OF_GENERIC_PARAMETERS:
            ret = GET_NUM_OF_GENERIC_PARAMETERS;
            break;
        case IntrinsicKind_GET_ACTUAL_PARAMETER_INFO:
            ret = GET_ACTUAL_PARAMETER_INFO;
            break;
        case IntrinsicKind_GET_GENERIC_PARAMETER_INFO:
            ret = GET_GENERIC_PARAMETER_INFO;
            break;
        case IntrinsicKind_GET_NUM_OF_INSTANCE_FIELD_INFOS:
            ret = GET_NUM_OF_INSTANCE_FIELD_INFOS;
            break;
        case IntrinsicKind_GET_INSTANCE_FIELD_INFO:
            ret = GET_INSTANCE_FIELD_INFO;
            break;
        case IntrinsicKind_GET_NUM_OF_STATIC_FIELD_INFOS:
            ret = GET_NUM_OF_STATIC_FIELD_INFOS;
            break;
        case IntrinsicKind_GET_STATIC_FIELD_INFO:
            ret = GET_STATIC_FIELD_INFO;
            break;
        case IntrinsicKind_GET_STATIC_FIELD_NAME:
            ret = GET_STATIC_FIELD_NAME;
            break;
        case IntrinsicKind_GET_STATIC_FIELD_TYPE:
            ret = GET_STATIC_FIELD_TYPE;
            break;
        case IntrinsicKind_GET_STATIC_FIELD_ANNOTATIONS:
            ret = GET_STATIC_FIELD_ANNOTATIONS;
            break;
        case IntrinsicKind_GET_FIELD_NAME:
            ret = GET_FIELD_NAME;
            break;
        case IntrinsicKind_GET_FIELD_TYPE:
            ret = GET_FIELD_TYPE;
            break;
        case IntrinsicKind_GET_FIELD_ANNOTATIONS:
            ret = GET_FIELD_ANNOTATIONS;
            break;
        case IntrinsicKind_GET_FIELD_MODIFIER:
            ret = GET_FIELD_MODIFIER;
            break;
        case IntrinsicKind_GET_STATIC_FIELD_MODIFIER:
            ret = GET_STATIC_FIELD_MODIFIER;
            break;
        case IntrinsicKind_GET_FIELD_VALUE:
            ret = GET_FIELD_VALUE;
            break;
        case IntrinsicKind_SET_FIELD_VALUE:
            ret = SET_FIELD_VALUE;
            break;
        case IntrinsicKind_GET_STATIC_FIELD_VALUE:
            ret = GET_STATIC_FIELD_VALUE;
            break;
        case IntrinsicKind_SET_STATIC_FIELD_VALUE:
            ret = SET_STATIC_FIELD_VALUE;
            break;
        case IntrinsicKind_GET_FIELD_DECLARING_TYPE:
            ret = GET_FIELD_DECLARING_TYPE;
            break;
        case IntrinsicKind_GET_PARAMETER_INDEX:
            ret = GET_PARAMETER_INDEX;
            break;
        case IntrinsicKind_GET_PARAMETER_NAME:
            ret = GET_PARAMETER_NAME;
            break;
        case IntrinsicKind_GET_PARAMETER_TYPE:
            ret = GET_PARAMETER_TYPE;
            break;
        case IntrinsicKind_GET_PARAMETER_ANNOTATIONS:
            ret = GET_PARAMETER_ANNOTATIONS;
            break;
        case IntrinsicKind_GET_RELATED_PACKAGE_INF:
            ret = GET_RELATED_PACKAGE_INF;
            break;
        case IntrinsicKind_GET_PACKAGE_NAME:
            ret = GET_PACKAGE_NAME;
            break;
        case IntrinsicKind_GET_PACKAGE_NUM_OF_TYPE_INFOS:
            ret = GET_PACKAGE_NUM_OF_TYPE_INFOS;
            break;
        case IntrinsicKind_GET_PACKAGE_TYPE_INFO:
            ret = GET_PACKAGE_TYPE_INFO;
            break;
        case IntrinsicKind_GET_PACKAGE_NUM_OF_GLOBAL_METHODS:
            ret = GET_PACKAGE_NUM_OF_GLOBAL_METHODS;
            break;
        case IntrinsicKind_GET_PACKAGE_GLOBAL_METHOD_INFO:
            ret = GET_PACKAGE_GLOBAL_METHOD_INFO;
            break;
        case IntrinsicKind_GET_PACKAGE_NUM_OF_GLOBAL_FIELD_INFOS:
            ret = GET_PACKAGE_NUM_OF_GLOBAL_FIELD_INFOS;
            break;
        case IntrinsicKind_GET_PACKAGE_GLOBAL_FIELD_INFO:
            ret = GET_PACKAGE_GLOBAL_FIELD_INFO;
            break;
        case IntrinsicKind_LOAD_PACKAGE:
            ret = LOAD_PACKAGE;
            break;
        case IntrinsicKind_GET_PACKAGE_BY_QUALIFIEDNAME:
            ret = GET_PACKAGE_BY_QUALIFIEDNAME;
            break;
        case IntrinsicKind_GET_PACKAGE_VERSION:
            ret = GET_PACKAGE_VERSION;
            break;
        case IntrinsicKind_GET_SUB_PACKAGES:
            ret = GET_SUB_PACKAGES;
            break;
        case IntrinsicKind_REFLECTION_INTRINSIC_END_FLAG:
            ret = REFLECTION_INTRINSIC_END_FLAG;
            break;
        // ============================ cjnative only end =================
        case IntrinsicKind_SLEEP:
            ret = SLEEP;
            break;
        case IntrinsicKind_SOURCE_FILE:
            ret = SOURCE_FILE;
            break;
        case IntrinsicKind_SOURCE_LINE:
            ret = SOURCE_LINE;
            break;
        case IntrinsicKind_IDENTITY_HASHCODE:
            ret = IDENTITY_HASHCODE;
            break;
        case IntrinsicKind_IDENTITY_HASHCODE_FOR_ARRAY:
            ret = IDENTITY_HASHCODE_FOR_ARRAY;
            break;
        // ============================ cjnative only start =================
        // SYNC
        case IntrinsicKind_ATOMIC_LOAD:
            ret = ATOMIC_LOAD;
            break;
        case IntrinsicKind_ATOMIC_STORE:
            ret = ATOMIC_STORE;
            break;
        case IntrinsicKind_ATOMIC_SWAP:
            ret = ATOMIC_SWAP;
            break;
        case IntrinsicKind_ATOMIC_COMPARE_AND_SWAP:
            ret = ATOMIC_COMPARE_AND_SWAP;
            break;
        case IntrinsicKind_ATOMIC_FETCH_ADD:
            ret = ATOMIC_FETCH_ADD;
            break;
        case IntrinsicKind_ATOMIC_FETCH_SUB:
            ret = ATOMIC_FETCH_SUB;
            break;
        case IntrinsicKind_ATOMIC_FETCH_AND:
            ret = ATOMIC_FETCH_AND;
            break;
        case IntrinsicKind_ATOMIC_FETCH_OR:
            ret = ATOMIC_FETCH_OR;
            break;
        case IntrinsicKind_ATOMIC_FETCH_XOR:
            ret = ATOMIC_FETCH_XOR;
            break;
        case IntrinsicKind_MUTEX_INIT:
            ret = MUTEX_INIT;
            break;
        case IntrinsicKind_CJ_MUTEX_LOCK:
            ret = CJ_MUTEX_LOCK;
            break;
        case IntrinsicKind_MUTEX_TRY_LOCK:
            ret = MUTEX_TRY_LOCK;
            break;
        case IntrinsicKind_MUTEX_CHECK_STATUS:
            ret = MUTEX_CHECK_STATUS;
            break;
        case IntrinsicKind_MUTEX_UNLOCK:
            ret = MUTEX_UNLOCK;
            break;
        case IntrinsicKind_WAITQUEUE_INIT:
            ret = WAITQUEUE_INIT;
            break;
        case IntrinsicKind_MONITOR_INIT:
            ret = MONITOR_INIT;
            break;
        case IntrinsicKind_MOITIOR_WAIT:
            ret = MOITIOR_WAIT;
            break;
        case IntrinsicKind_MOITIOR_NOTIFY:
            ret = MOITIOR_NOTIFY;
            break;
        case IntrinsicKind_MOITIOR_NOTIFY_ALL:
            ret = MOITIOR_NOTIFY_ALL;
            break;
        case IntrinsicKind_MULTICONDITION_WAIT:
            ret = MULTICONDITION_WAIT;
            break;
        case IntrinsicKind_MULTICONDITION_NOTIFY:
            ret = MULTICONDITION_NOTIFY;
            break;
        case IntrinsicKind_MULTICONDITION_NOTIFY_ALL:
            ret = MULTICONDITION_NOTIFY_ALL;
            break;
        case IntrinsicKind_CROSS_ACCESS_BARRIER:
            ret = CROSS_ACCESS_BARRIER;
            break;
        case IntrinsicKind_CREATE_EXPORT_HANDLE:
            ret = CREATE_EXPORT_HANDLE;
            break;
        case IntrinsicKind_GET_EXPORTED_REF:
            ret = GET_EXPORTED_REF;
            break;
        case IntrinsicKind_REMOVE_EXPORTED_REF:
            ret = REMOVE_EXPORTED_REF;
            break;
        // ============================ cjnative only end =================
        // Syscall
        // AST lib FFI
        case IntrinsicKind_FFI_CJ_AST_LEX:
            ret = FFI_CJ_AST_LEX;
            break;
        case IntrinsicKind_FFI_CJ_AST_PARSEEXPR:
            ret = FFI_CJ_AST_PARSEEXPR;
            break;
        case IntrinsicKind_FFI_CJ_AST_PARSEDECL:
            ret = FFI_CJ_AST_PARSEDECL;
            break;
        case IntrinsicKind_FFI_CJ_AST_PARSE_PROPMEMBERDECL:
            ret = FFI_CJ_AST_PARSE_PROPMEMBERDECL;
            break;
        case IntrinsicKind_FFI_CJ_AST_PARSE_PRICONSTRUCTOR:
            ret = FFI_CJ_AST_PARSE_PRICONSTRUCTOR;
            break;
        case IntrinsicKind_FFI_CJ_AST_PARSE_PATTERN:
            ret = FFI_CJ_AST_PARSE_PATTERN;
            break;
        case IntrinsicKind_FFI_CJ_AST_PARSE_TYPE:
            ret = FFI_CJ_AST_PARSE_TYPE;
            break;
        case IntrinsicKind_FFI_CJ_AST_PARSETOPLEVEL:
            ret = FFI_CJ_AST_PARSETOPLEVEL;
            break;
        case IntrinsicKind_FFI_CJ_AST_DIAGREPORT:
            ret = FFI_CJ_AST_DIAGREPORT;
            break;
        // Macro With Context FFI
        case IntrinsicKind_FFI_CJ_PARENT_CONTEXT:
            ret = FFI_CJ_PARENT_CONTEXT;
            break;
        case IntrinsicKind_FFI_CJ_MACRO_ITEM_INFO:
            ret = FFI_CJ_MACRO_ITEM_INFO;
            break;
        case IntrinsicKind_FFI_CJ_GET_CHILD_MESSAGES:
            ret = FFI_CJ_GET_CHILD_MESSAGES;
            break;
        case IntrinsicKind_FFI_CJ_CHECK_ADD_SPACE:
            ret = FFI_CJ_CHECK_ADD_SPACE;
            break;
        // CodeGen
        case IntrinsicKind_CG_UNSAFE_BEGIN:
            ret = CG_UNSAFE_BEGIN;
            break;
        case IntrinsicKind_CG_UNSAFE_END:
            ret = CG_UNSAFE_END;
            break;
        // C FFI funcs
        case IntrinsicKind_STRLEN:
            ret = STRLEN;
            break;
        case IntrinsicKind_MEMCPY_S:
            ret = MEMCPY_S;
            break;
        case IntrinsicKind_MEMSET_S:
            ret = MEMSET_S;
            break;
        case IntrinsicKind_FREE:
            ret = FREE;
            break;
        case IntrinsicKind_MALLOC:
            ret = MALLOC;
            break;
        case IntrinsicKind_STRCMP:
            ret = STRCMP;
            break;
        case IntrinsicKind_MEMCMP:
            ret = MEMCMP;
            break;
        case IntrinsicKind_STRNCMP:
            ret = STRNCMP;
            break;
        case IntrinsicKind_STRCASECMP:
            ret = STRCASECMP;
            break;
        // The interpreter is using these for cjnative backend as well
        case IntrinsicKind_ATOMIC_INT8_LOAD:
            ret = ATOMIC_INT8_LOAD;
            break;
        case IntrinsicKind_ATOMIC_INT8_STORE:
            ret = ATOMIC_INT8_STORE;
            break;
        case IntrinsicKind_ATOMIC_INT8_SWAP:
            ret = ATOMIC_INT8_SWAP;
            break;
        case IntrinsicKind_ATOMIC_INT8_CAS:
            ret = ATOMIC_INT8_CAS;
            break;
        case IntrinsicKind_ATOMIC_INT8_FETCH_ADD:
            ret = ATOMIC_INT8_FETCH_ADD;
            break;
        case IntrinsicKind_ATOMIC_INT8_FETCH_SUB:
            ret = ATOMIC_INT8_FETCH_SUB;
            break;
        case IntrinsicKind_ATOMIC_INT8_FETCH_AND:
            ret = ATOMIC_INT8_FETCH_AND;
            break;
        case IntrinsicKind_ATOMIC_INT8_FETCH_OR:
            ret = ATOMIC_INT8_FETCH_OR;
            break;
        case IntrinsicKind_ATOMIC_INT8_FETCH_XOR:
            ret = ATOMIC_INT8_FETCH_XOR;
            break;
        case IntrinsicKind_ATOMIC_INT16_LOAD:
            ret = ATOMIC_INT16_LOAD;
            break;
        case IntrinsicKind_ATOMIC_INT16_STORE:
            ret = ATOMIC_INT16_STORE;
            break;
        case IntrinsicKind_ATOMIC_INT16_SWAP:
            ret = ATOMIC_INT16_SWAP;
            break;
        case IntrinsicKind_ATOMIC_INT16_CAS:
            ret = ATOMIC_INT16_CAS;
            break;
        case IntrinsicKind_ATOMIC_INT16_FETCH_ADD:
            ret = ATOMIC_INT16_FETCH_ADD;
            break;
        case IntrinsicKind_ATOMIC_INT16_FETCH_SUB:
            ret = ATOMIC_INT16_FETCH_SUB;
            break;
        case IntrinsicKind_ATOMIC_INT16_FETCH_AND:
            ret = ATOMIC_INT16_FETCH_AND;
            break;
        case IntrinsicKind_ATOMIC_INT16_FETCH_OR:
            ret = ATOMIC_INT16_FETCH_OR;
            break;
        case IntrinsicKind_ATOMIC_INT16_FETCH_XOR:
            ret = ATOMIC_INT16_FETCH_XOR;
            break;
        case IntrinsicKind_ATOMIC_INT32_LOAD:
            ret = ATOMIC_INT32_LOAD;
            break;
        case IntrinsicKind_ATOMIC_INT32_STORE:
            ret = ATOMIC_INT32_STORE;
            break;
        case IntrinsicKind_ATOMIC_INT32_SWAP:
            ret = ATOMIC_INT32_SWAP;
            break;
        case IntrinsicKind_ATOMIC_INT32_CAS:
            ret = ATOMIC_INT32_CAS;
            break;
        case IntrinsicKind_ATOMIC_INT32_FETCH_ADD:
            ret = ATOMIC_INT32_FETCH_ADD;
            break;
        case IntrinsicKind_ATOMIC_INT32_FETCH_SUB:
            ret = ATOMIC_INT32_FETCH_SUB;
            break;
        case IntrinsicKind_ATOMIC_INT32_FETCH_AND:
            ret = ATOMIC_INT32_FETCH_AND;
            break;
        case IntrinsicKind_ATOMIC_INT32_FETCH_OR:
            ret = ATOMIC_INT32_FETCH_OR;
            break;
        case IntrinsicKind_ATOMIC_INT32_FETCH_XOR:
            ret = ATOMIC_INT32_FETCH_XOR;
            break;
        case IntrinsicKind_ATOMIC_INT64_LOAD:
            ret = ATOMIC_INT64_LOAD;
            break;
        case IntrinsicKind_ATOMIC_INT64_STORE:
            ret = ATOMIC_INT64_STORE;
            break;
        case IntrinsicKind_ATOMIC_INT64_SWAP:
            ret = ATOMIC_INT64_SWAP;
            break;
        case IntrinsicKind_ATOMIC_INT64_CAS:
            ret = ATOMIC_INT64_CAS;
            break;
        case IntrinsicKind_ATOMIC_INT64_FETCH_ADD:
            ret = ATOMIC_INT64_FETCH_ADD;
            break;
        case IntrinsicKind_ATOMIC_INT64_FETCH_SUB:
            ret = ATOMIC_INT64_FETCH_SUB;
            break;
        case IntrinsicKind_ATOMIC_INT64_FETCH_AND:
            ret = ATOMIC_INT64_FETCH_AND;
            break;
        case IntrinsicKind_ATOMIC_INT64_FETCH_OR:
            ret = ATOMIC_INT64_FETCH_OR;
            break;
        case IntrinsicKind_ATOMIC_INT64_FETCH_XOR:
            ret = ATOMIC_INT64_FETCH_XOR;
            break;
        case IntrinsicKind_ATOMIC_UINT8_LOAD:
            ret = ATOMIC_UINT8_LOAD;
            break;
        case IntrinsicKind_ATOMIC_UINT8_STORE:
            ret = ATOMIC_UINT8_STORE;
            break;
        case IntrinsicKind_ATOMIC_UINT8_SWAP:
            ret = ATOMIC_UINT8_SWAP;
            break;
        case IntrinsicKind_ATOMIC_UINT8_CAS:
            ret = ATOMIC_UINT8_CAS;
            break;
        case IntrinsicKind_ATOMIC_UINT8_FETCH_ADD:
            ret = ATOMIC_UINT8_FETCH_ADD;
            break;
        case IntrinsicKind_ATOMIC_UINT8_FETCH_SUB:
            ret = ATOMIC_UINT8_FETCH_SUB;
            break;
        case IntrinsicKind_ATOMIC_UINT8_FETCH_AND:
            ret = ATOMIC_UINT8_FETCH_AND;
            break;
        case IntrinsicKind_ATOMIC_UINT8_FETCH_OR:
            ret = ATOMIC_UINT8_FETCH_OR;
            break;
        case IntrinsicKind_ATOMIC_UINT8_FETCH_XOR:
            ret = ATOMIC_UINT8_FETCH_XOR;
            break;
        case IntrinsicKind_ATOMIC_UINT16_LOAD:
            ret = ATOMIC_UINT16_LOAD;
            break;
        case IntrinsicKind_ATOMIC_UINT16_STORE:
            ret = ATOMIC_UINT16_STORE;
            break;
        case IntrinsicKind_ATOMIC_UINT16_SWAP:
            ret = ATOMIC_UINT16_SWAP;
            break;
        case IntrinsicKind_ATOMIC_UINT16_CAS:
            ret = ATOMIC_UINT16_CAS;
            break;
        case IntrinsicKind_ATOMIC_UINT16_FETCH_ADD:
            ret = ATOMIC_UINT16_FETCH_ADD;
            break;
        case IntrinsicKind_ATOMIC_UINT16_FETCH_SUB:
            ret = ATOMIC_UINT16_FETCH_SUB;
            break;
        case IntrinsicKind_ATOMIC_UINT16_FETCH_AND:
            ret = ATOMIC_UINT16_FETCH_AND;
            break;
        case IntrinsicKind_ATOMIC_UINT16_FETCH_OR:
            ret = ATOMIC_UINT16_FETCH_OR;
            break;
        case IntrinsicKind_ATOMIC_UINT16_FETCH_XOR:
            ret = ATOMIC_UINT16_FETCH_XOR;
            break;
        case IntrinsicKind_ATOMIC_UINT32_LOAD:
            ret = ATOMIC_UINT32_LOAD;
            break;
        case IntrinsicKind_ATOMIC_UINT32_STORE:
            ret = ATOMIC_UINT32_STORE;
            break;
        case IntrinsicKind_ATOMIC_UINT32_SWAP:
            ret = ATOMIC_UINT32_SWAP;
            break;
        case IntrinsicKind_ATOMIC_UINT32_CAS:
            ret = ATOMIC_UINT32_CAS;
            break;
        case IntrinsicKind_ATOMIC_UINT32_FETCH_ADD:
            ret = ATOMIC_UINT32_FETCH_ADD;
            break;
        case IntrinsicKind_ATOMIC_UINT32_FETCH_SUB:
            ret = ATOMIC_UINT32_FETCH_SUB;
            break;
        case IntrinsicKind_ATOMIC_UINT32_FETCH_AND:
            ret = ATOMIC_UINT32_FETCH_AND;
            break;
        case IntrinsicKind_ATOMIC_UINT32_FETCH_OR:
            ret = ATOMIC_UINT32_FETCH_OR;
            break;
        case IntrinsicKind_ATOMIC_UINT32_FETCH_XOR:
            ret = ATOMIC_UINT32_FETCH_XOR;
            break;
        case IntrinsicKind_ATOMIC_UINT64_LOAD:
            ret = ATOMIC_UINT64_LOAD;
            break;
        case IntrinsicKind_ATOMIC_UINT64_STORE:
            ret = ATOMIC_UINT64_STORE;
            break;
        case IntrinsicKind_ATOMIC_UINT64_SWAP:
            ret = ATOMIC_UINT64_SWAP;
            break;
        case IntrinsicKind_ATOMIC_UINT64_CAS:
            ret = ATOMIC_UINT64_CAS;
            break;
        case IntrinsicKind_ATOMIC_UINT64_FETCH_ADD:
            ret = ATOMIC_UINT64_FETCH_ADD;
            break;
        case IntrinsicKind_ATOMIC_UINT64_FETCH_SUB:
            ret = ATOMIC_UINT64_FETCH_SUB;
            break;
        case IntrinsicKind_ATOMIC_UINT64_FETCH_AND:
            ret = ATOMIC_UINT64_FETCH_AND;
            break;
        case IntrinsicKind_ATOMIC_UINT64_FETCH_OR:
            ret = ATOMIC_UINT64_FETCH_OR;
            break;
        case IntrinsicKind_ATOMIC_UINT64_FETCH_XOR:
            ret = ATOMIC_UINT64_FETCH_XOR;
            break;
        case IntrinsicKind_ATOMIC_BOOL_LOAD:
            ret = ATOMIC_BOOL_LOAD;
            break;
        case IntrinsicKind_ATOMIC_BOOL_STORE:
            ret = ATOMIC_BOOL_STORE;
            break;
        case IntrinsicKind_ATOMIC_BOOL_SWAP:
            ret = ATOMIC_BOOL_SWAP;
            break;
        case IntrinsicKind_ATOMIC_BOOL_CAS:
            ret = ATOMIC_BOOL_CAS;
            break;
        case IntrinsicKind_ATOMIC_REFERENCEBASE_LOAD:
            ret = ATOMIC_REFERENCEBASE_LOAD;
            break;
        case IntrinsicKind_ATOMIC_REFERENCEBASE_STORE:
            ret = ATOMIC_REFERENCEBASE_STORE;
            break;
        case IntrinsicKind_ATOMIC_REFERENCEBASE_SWAP:
            ret = ATOMIC_REFERENCEBASE_SWAP;
            break;
        case IntrinsicKind_ATOMIC_REFERENCEBASE_CAS:
            ret = ATOMIC_REFERENCEBASE_CAS;
            break;
        case IntrinsicKind_ATOMIC_OPTIONREFERENCE_LOAD:
            ret = ATOMIC_OPTIONREFERENCE_LOAD;
            break;
        case IntrinsicKind_ATOMIC_OPTIONREFERENCE_STORE:
            ret = ATOMIC_OPTIONREFERENCE_STORE;
            break;
        case IntrinsicKind_ATOMIC_OPTIONREFERENCE_SWAP:
            ret = ATOMIC_OPTIONREFERENCE_SWAP;
            break;
        case IntrinsicKind_ATOMIC_OPTIONREFERENCE_CAS:
            ret = ATOMIC_OPTIONREFERENCE_CAS;
            break;
        // CHIR: Exception intrinsic
        case IntrinsicKind_BEGIN_CATCH:
            ret = BEGIN_CATCH;
            break;
        // CHIR: Math intrinsic
        case IntrinsicKind_ABS:
            ret = ABS;
            break;
        case IntrinsicKind_FABS:
            ret = FABS;
            break;
        case IntrinsicKind_FLOOR:
            ret = FLOOR;
            break;
        case IntrinsicKind_CEIL:
            ret = CEIL;
            break;
        case IntrinsicKind_TRUNC:
            ret = TRUNC;
            break;
        case IntrinsicKind_SIN:
            ret = SIN;
            break;
        case IntrinsicKind_COS:
            ret = COS;
            break;
        case IntrinsicKind_EXP:
            ret = EXP;
            break;
        case IntrinsicKind_EXP2:
            ret = EXP2;
            break;
        case IntrinsicKind_LOG:
            ret = LOG;
            break;
        case IntrinsicKind_LOG2:
            ret = LOG2;
            break;
        case IntrinsicKind_LOG10:
            ret = LOG10;
            break;
        case IntrinsicKind_SQRT:
            ret = SQRT;
            break;
        case IntrinsicKind_ROUND:
            ret = ROUND;
            break;
        case IntrinsicKind_POW:
            ret = POW;
            break;
        case IntrinsicKind_POWI:
            ret = POWI;
            break;
        case IntrinsicKind_BIT_CAST:
            ret = BIT_CAST;
            break;
        // preinitialize intrinsic
        case IntrinsicKind_PREINITIALIZE:
            ret = PREINITIALIZE;
            break;
        // Box cast intrinsic
        case IntrinsicKind_OBJECT_AS:
            ret = OBJECT_AS;
            break;
        case IntrinsicKind_IS_NULL:
            ret = IS_NULL;
            break;
        case IntrinsicKind_GET_TYPE_FOR_TYPE_PARAMETER:
            ret = GET_TYPE_FOR_TYPE_PARAMETER;
            break;
        case IntrinsicKind_IS_SUBTYPE_TYPES:
            ret = IS_SUBTYPE_TYPES;
            break;
    }
    return ret;
}

Package::AccessLevel DeSerialize(const PackageFormat::PackageAccessLevel& kind)
{
    using namespace PackageFormat;
    auto ret = Package::AccessLevel::INTERNAL;
    switch (kind) {
        case PackageAccessLevel_INVALID:
            ret = Package::AccessLevel::INVALID;
            break;
        case PackageAccessLevel_INTERNAL:
            ret = Package::AccessLevel::INTERNAL;
            break;
        case PackageAccessLevel_PROTECTED:
            ret = Package::AccessLevel::PROTECTED;
            break;
        case PackageAccessLevel_PUBLIC:
            ret = Package::AccessLevel::PUBLIC;
            break;
    }
    return ret;
}

ToCHIR::Phase DeSerialize(const PackageFormat::Phase& kind)
{
    using namespace PackageFormat;
    auto ret = ToCHIR::Phase::RAW;
    switch (kind) {
        case Phase_RAW:
            ret = ToCHIR::Phase::RAW;
            break;
        case Phase_OPT:
            ret = ToCHIR::Phase::OPT;
            break;
        case Phase_PLUGIN:
            ret = ToCHIR::Phase::PLUGIN;
            break;
        case Phase_ANALYSIS_FOR_CJLINT:
            ret = ToCHIR::Phase::ANALYSIS_FOR_CJLINT;
            break;
    }
    return ret;
}

TEST_F(CHIRDeSerialzierTest, TypeKindEnum)
{
    using Cangjie::CHIR::Type;
    auto enumBegin = PackageFormat::CHIRTypeKind_MIN;
    auto enumEnd = PackageFormat::CHIRTypeKind_MAX;

    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(Type::TypeKind(static_cast<PackageFormat::CHIRTypeKind>(i)),
            DeSerialize(static_cast<PackageFormat::CHIRTypeKind>(i)));
    }
}

TEST_F(CHIRDeSerialzierTest, SourceExprEnum)
{
    using Cangjie::CHIR::SourceExpr;
    auto enumBegin = PackageFormat::SourceExpr_MIN;
    auto enumEnd = PackageFormat::SourceExpr_MAX;

    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(SourceExpr(static_cast<PackageFormat::SourceExpr>(i)),
            DeSerialize(static_cast<PackageFormat::SourceExpr>(i)));
    }
}

TEST_F(CHIRDeSerialzierTest, LinkageEnum)
{
    using Cangjie::Linkage;
    auto enumBegin = PackageFormat::Linkage_MIN;
    auto enumEnd = PackageFormat::Linkage_MAX;

    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(Cangjie::Linkage(static_cast<PackageFormat::Linkage>(i)),
            DeSerialize(static_cast<PackageFormat::Linkage>(i)));
    }
}

TEST_F(CHIRDeSerialzierTest, SkipKindEnum)
{
    using Cangjie::CHIR::SkipKind;
    auto enumBegin = PackageFormat::SkipKind_MIN;
    auto enumEnd = PackageFormat::SkipKind_MAX;

    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(
            SkipKind(static_cast<PackageFormat::SkipKind>(i)), DeSerialize(static_cast<PackageFormat::SkipKind>(i)));
    }
}

TEST_F(CHIRDeSerialzierTest, OverflowStrategyEnum)
{
    using Cangjie::OverflowStrategy;
    auto enumBegin = PackageFormat::OverflowStrategy_MIN;
    auto enumEnd = PackageFormat::OverflowStrategy_MAX;

    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(OverflowStrategy(static_cast<PackageFormat::OverflowStrategy>(i)),
            DeSerialize(static_cast<PackageFormat::OverflowStrategy>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRDeSerialzierTest, ValueKindEnum)
{
    using Cangjie::CHIR::Value;
    auto enumBegin = PackageFormat::ValueKind_MIN;
    auto enumEnd = PackageFormat::ValueKind_MAX;

    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(Value::ValueKind(static_cast<PackageFormat::ValueKind>(i)),
            DeSerialize(static_cast<PackageFormat::ValueKind>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRDeSerialzierTest, ConstantValueKindEnum)
{
    using Cangjie::CHIR::ConstantValueKind;
    auto enumBegin = PackageFormat::ConstantValueKind_MIN;
    auto enumEnd = PackageFormat::ConstantValueKind_MAX;

    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(ConstantValueKind(static_cast<PackageFormat::ConstantValueKind>(i)),
            DeSerialize(static_cast<PackageFormat::ConstantValueKind>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRDeSerialzierTest, FuncKindEnum)
{
    using namespace PackageFormat;
    using Cangjie::CHIR::FuncKind;
    auto enumBegin = FuncKind_MIN;
    auto enumEnd = FuncKind_MAX;

    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(
            FuncKind(static_cast<PackageFormat::FuncKind>(i)), DeSerialize(static_cast<PackageFormat::FuncKind>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRDeSerialzierTest, CustomDefKindEnum)
{
    using Cangjie::CHIR::CustomDefKind;
    auto enumBegin = PackageFormat::CustomDefKind_MIN;
    auto enumEnd = PackageFormat::CustomDefKind_MAX;

    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(CustomDefKind(static_cast<PackageFormat::CustomDefKind>(i)),
            DeSerialize(static_cast<PackageFormat::CustomDefKind>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRDeSerialzierTest, ExprKindEnum)
{
    using namespace PackageFormat;
    auto enumBegin = PackageFormat::CHIRExprKind_MIN;
    auto enumEnd = PackageFormat::CHIRExprKind_MAX;

    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(ExprKind(static_cast<PackageFormat::CHIRExprKind>(i)),
            DeSerialize(static_cast<PackageFormat::CHIRExprKind>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRDeSerialzierTest, IntrinsicKindEnum)
{
    using namespace PackageFormat;
    auto enumBegin = PackageFormat::IntrinsicKind_MIN;
    auto enumEnd = PackageFormat::IntrinsicKind_MAX;

    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(Cangjie::CHIR::IntrinsicKind(static_cast<PackageFormat::IntrinsicKind>(i)),
            DeSerialize(static_cast<PackageFormat::IntrinsicKind>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRDeSerialzierTest, PackageAccessLevelEnum)
{
    using namespace PackageFormat;
    auto enumBegin = PackageFormat::PackageAccessLevel_MIN;
    auto enumEnd = PackageFormat::PackageAccessLevel_MAX;

    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(Package::AccessLevel(static_cast<PackageFormat::PackageAccessLevel>(i)),
            DeSerialize(static_cast<PackageFormat::PackageAccessLevel>(i)))
            << "cur i: " << i;
    }
}

TEST_F(CHIRDeSerialzierTest, Phase)
{
    using namespace PackageFormat;
    auto enumBegin = PackageFormat::Phase_MIN;
    auto enumEnd = PackageFormat::Phase_MAX;

    for (size_t i = static_cast<size_t>(enumBegin); i <= static_cast<size_t>(enumEnd); i++) {
        EXPECT_EQ(
            ToCHIR::Phase(static_cast<PackageFormat::Phase>(i)), DeSerialize(static_cast<PackageFormat::Phase>(i)))
            << "cur i: " << i;
    }
}
