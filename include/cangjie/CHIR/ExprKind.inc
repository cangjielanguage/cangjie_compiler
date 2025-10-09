// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// Tips:
// 1. EXPRKIND(expression kind, exression name for human reading, Expression class)
// 2. Remember to modify CHIRCasting.h and PackageFormat.fbs after adding a new expression

#if defined(MAJORKIND) && defined(EXPRKIND)

MAJORKIND(TERMINATOR,
EXPRKIND(GOTO, "GoTo", GoTo)
EXPRKIND(BRANCH, "Branch", Branch)
EXPRKIND(MULTIBRANCH, "MultiBranch", MultiBranch)
EXPRKIND(EXIT, "Exit", Exit)
EXPRKIND(APPLY_WITH_EXCEPTION, "ApplyWithException", ApplyWithException)
EXPRKIND(INVOKE_WITH_EXCEPTION, "InvokeWithException", InvokeWithException)
EXPRKIND(INVOKESTATIC_WITH_EXCEPTION, "InvokeStaticWithException", InvokeStaticWithException)
EXPRKIND(RAISE_EXCEPTION, "RaiseException", RaiseException)
EXPRKIND(INT_OP_WITH_EXCEPTION, "IntOpWithException", IntOpWithException)
EXPRKIND(SPAWN_WITH_EXCEPTION, "SpawnWithException", SpawnWithException)
EXPRKIND(TYPECAST_WITH_EXCEPTION, "TypeCastWithException", TypeCastWithException)
EXPRKIND(INTRINSIC_WITH_EXCEPTION, "IntrinsicWithException", IntrinsicWithException)
EXPRKIND(ALLOCATE_WITH_EXCEPTION, "AllocateWithException", AllocateWithException)
EXPRKIND(RAW_ARRAY_ALLOCATE_WITH_EXCEPTION, "RawArrayAllocateWithException", RawArrayAllocateWithException)
)

MAJORKIND(UNARY_EXPR,
EXPRKIND(NEG, "Neg", UnaryExpression)
EXPRKIND(NOT, "Not", UnaryExpression)
EXPRKIND(BITNOT, "BitNot", UnaryExpression)
)

MAJORKIND(BINARY_EXPR,
EXPRKIND(ADD, "Add", BinaryExpression)
EXPRKIND(SUB, "Sub", BinaryExpression)
EXPRKIND(MUL, "Mul", BinaryExpression)
EXPRKIND(DIV, "Div", BinaryExpression)
EXPRKIND(MOD, "Mod", BinaryExpression)
EXPRKIND(EXP, "Exp", BinaryExpression)
EXPRKIND(LSHIFT, "LShift", BinaryExpression)
EXPRKIND(RSHIFT, "RShift", BinaryExpression)
EXPRKIND(BITAND, "BitAnd", BinaryExpression)
EXPRKIND(BITOR, "BitOr", BinaryExpression)
EXPRKIND(BITXOR, "BitXor", BinaryExpression)
EXPRKIND(LT, "LT", BinaryExpression)
EXPRKIND(GT, "GT", BinaryExpression)
EXPRKIND(LE, "LE", BinaryExpression)
EXPRKIND(GE, "GE", BinaryExpression)
EXPRKIND(EQUAL, "Equal", BinaryExpression)
EXPRKIND(NOTEQUAL, "NotEqual", BinaryExpression)
EXPRKIND(AND, "And", BinaryExpression)
EXPRKIND(OR, "Or", BinaryExpression)
)

MAJORKIND(MEMORY_EXPR,
EXPRKIND(ALLOCATE, "Allocate", Allocate)
EXPRKIND(LOAD, "Load", Load)
EXPRKIND(STORE, "Store", Store)
EXPRKIND(GET_ELEMENT_REF, "GetElementRef", GetElementRef)
EXPRKIND(GET_ELEMENT_BY_NAME, "GetElementByName", GetElementByName)
EXPRKIND(STORE_ELEMENT_REF, "StoreElementRef", StoreElementRef)
EXPRKIND(STORE_ELEMENT_BY_NAME, "StoreElementByName", StoreElementByName)
)

MAJORKIND(STRUCTURED_CTRL_FLOW_EXPR,
EXPRKIND(IF, "If", If)
EXPRKIND(LOOP, "Loop", Loop)
EXPRKIND(FORIN_RANGE, "ForInRange", ForInRange)
EXPRKIND(FORIN_ITER, "ForInIter", ForInIter)
EXPRKIND(FORIN_CLOSED_RANGE, "ForInClosedRange", ForInClosedRange)
EXPRKIND(LAMBDA, "Lambda", Lambda)
)

MAJORKIND(OTHERS,
EXPRKIND(CONSTANT, "Constant", Constant)
EXPRKIND(DEBUGEXPR, "Debug", Debug)
EXPRKIND(TUPLE, "Tuple", Tuple)
EXPRKIND(FIELD, "Field", Field)
EXPRKIND(FIELD_BY_NAME, "FieldByName", FieldByName)
EXPRKIND(APPLY, "Apply", Apply)
EXPRKIND(INVOKE, "Invoke", Invoke)
EXPRKIND(INVOKESTATIC, "InvokeStatic", InvokeStatic)
EXPRKIND(INSTANCEOF, "InstanceOf", InstanceOf)
EXPRKIND(TYPECAST, "TypeCast", TypeCast)
EXPRKIND(GET_EXCEPTION, "GetException", GetException)

EXPRKIND(RAW_ARRAY_ALLOCATE, "RawArrayAllocate", RawArrayAllocate)
EXPRKIND(RAW_ARRAY_LITERAL_INIT, "RawArrayLiteralInit", RawArrayLiteralInit)
EXPRKIND(RAW_ARRAY_INIT_BY_VALUE, "RawArrayInitByValue", RawArrayInitByValue)
EXPRKIND(VARRAY, "VArray", VArray)
EXPRKIND(VARRAY_BUILDER, "VArrayBuilder", VArrayBuilder)
EXPRKIND(INTRINSIC, "Intrinsic", Intrinsic)
EXPRKIND(SPAWN, "Spawn", Spawn)
EXPRKIND(GET_INSTANTIATE_VALUE, "GetInstantiateValue", GetInstantiateValue)

EXPRKIND(BOX, "Box", Box)
EXPRKIND(UNBOX, "UnBox", Unbox)
EXPRKIND(TRANSFORM_TO_GENERIC, "TransformToGeneric", TransformToGeneric)
EXPRKIND(TRANSFORM_TO_CONCRETE, "TransformToConcrete", TransformToConcrete)
EXPRKIND(UNBOX_TO_REF, "UnBoxToRef", UnBoxToRef)

EXPRKIND(GET_RTTI, "GetRTTI", GetRTTI)
EXPRKIND(GET_RTTI_STATIC, "GetRTTIStatic", GetRTTIStatic)
)

#endif
