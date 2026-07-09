// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

// Tips:
// 1. EXPRKIND(expression kind, exression name for human reading, Expression class)
// 2. Remember to modify CHIRCasting.h and StdxChirFormat.fbs after adding a new expression
// 3. When adding a new unary/binary expression (including TRY_*), also update
//    UNARY_EXPR_KIND_TABLE / BINARY_EXPR_KIND_TABLE in Expression.cpp, and the matching
//    UnaryExprKind / BinaryExprKind enumerators if needed

#if defined(MAJORKIND) && defined(EXPRKIND)

MAJORKIND(TERMINATOR,
EXPRKIND(GOTO, "GoTo", GoTo)
EXPRKIND(BRANCH, "Branch", Branch)
EXPRKIND(MULTIBRANCH, "MultiBranch", MultiBranch)
EXPRKIND(EXIT, "Exit", Exit)
EXPRKIND(TRY_APPLY, "TryApply", TryApply)
EXPRKIND(TRY_INVOKE, "TryInvoke", TryInvoke)
EXPRKIND(TRY_INVOKESTATIC, "TryInvokeStatic", TryInvokeStatic)
EXPRKIND(RAISE_EXCEPTION, "RaiseException", RaiseException)
EXPRKIND(TRY_NEG, "TryNeg", TryUnaryExpression)
EXPRKIND(TRY_ADD, "TryAdd", TryBinaryExpression)
EXPRKIND(TRY_SUB, "TrySub", TryBinaryExpression)
EXPRKIND(TRY_MUL, "TryMul", TryBinaryExpression)
EXPRKIND(TRY_DIV, "TryDiv", TryBinaryExpression)
EXPRKIND(TRY_MOD, "TryMod", TryBinaryExpression)
EXPRKIND(TRY_EXP, "TryExp", TryBinaryExpression)
EXPRKIND(TRY_LSHIFT, "TryLShift", TryBinaryExpression)
EXPRKIND(TRY_RSHIFT, "TryRShift", TryBinaryExpression)
EXPRKIND(TRY_SPAWN, "TrySpawn", TrySpawn)
EXPRKIND(TRY_NUMERIC_CAST, "TryNumericCast", TryNumericCast)
EXPRKIND(TRY_INTRINSIC, "TryIntrinsic", TryIntrinsic)
EXPRKIND(TRY_ALLOCATE, "TryAllocate", TryAllocate)
EXPRKIND(TRY_RAW_ARRAY_ALLOCATE, "TryRawArrayAllocate", TryRawArrayAllocate)
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
EXPRKIND(CLASS_STATIC_CAST, "ClassStaticCast", ClassStaticCast)
EXPRKIND(NUMERIC_CAST, "NumericCast", NumericCast)
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
EXPRKIND(UNBOX_TO_VALUE, "UnBoxToValue", UnBoxToValue)
EXPRKIND(CAST_TO_GENERIC, "CastToGeneric", CastToGeneric)
EXPRKIND(CAST_TO_CONCRETE, "CastToConcrete", CastToConcrete)
EXPRKIND(UNBOX_TO_REF, "UnBoxToRef", UnBoxToRef)

EXPRKIND(GET_RTTI, "GetRTTI", GetRTTI)
EXPRKIND(GET_RTTI_STATIC, "GetRTTIStatic", GetRTTIStatic)
)

#endif
