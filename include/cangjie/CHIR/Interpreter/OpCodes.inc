// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file lists bytecode interpreter operation codes.
 */

// OPCODE(op code id, op code value for human reading, number of additional bytecode arguments, handles exception)
#ifdef OPCODE
// Enviroment accessors
OPCODE(GVAR, "GVAR", 1, false) // global variable
OPCODE(LVAR, "LVAR", 1, false) // local variable
// Enviroment Setters
OPCODE(GVAR_SET, "GVAR_SET", 1, false) // global variable
OPCODE(LVAR_SET, "LVAR_SET", 1, false) // local variable
// Memory related
OPCODE(ALLOCATE_CLASS, "ALLOCATE_CLASS", 2, false) // id, fields
OPCODE(ALLOCATE_CLASS_EXC, "ALLOCATE_CLASS_EXC", 3, true) // id, fields, jump index for exception
OPCODE(ALLOCATE_STRUCT, "ALLOCATE_STRUCT", 1, false) // number of fields
OPCODE(ALLOCATE_STRUCT_EXC, "ALLOCATE_STRUCT_EXC", 2, true) // fields, jump index for exception
OPCODE(ALLOCATE_RAW_ARRAY, "ALLOCATE_RAW_ARRAY", 0, false)
OPCODE(ALLOCATE_RAW_ARRAY_EXC, "ALLOCATE_RAW_ARRAY_EXC", 1, true)
OPCODE(ALLOCATE_RAW_ARRAY_LITERAL, "ALLOCATE_RAW_ARRAY_LITERAL", 1, false) // array size
OPCODE(ALLOCATE_RAW_ARRAY_LITERAL_EXC, "ALLOCATE_RAW_ARRAY_LITERAL_EXC", 2, true) // array size
OPCODE(RAW_ARRAY_LITERAL_INIT, "RAW_ARRAY_LITERAL_INIT", 1, false) // array size
OPCODE(RAW_ARRAY_INIT_BY_VALUE, "RAW_ARRAY_INIT_BY_VALUE", 0, false)
OPCODE(ALLOCATE, "ALLOCATE", 0, false)
OPCODE(ALLOCATE_EXC, "ALLOCATE_EXC", 1, true) // jump index for exception
OPCODE(FRAME, "FRAME", 1, false)
// Constructors
OPCODE(UINT8, "UINT8", 1, false)
OPCODE(UINT16, "UINT16", 1, false)
OPCODE(UINT32, "UINT32", 1, false)
OPCODE(UINT64, "UINT64", 2, false) // need 2 cells to store uint64_t value
OPCODE(UINTNAT, "UINTNAT", 2, false) // need 2 cells to store uint native value in a 64-bit machine
OPCODE(INT8, "INT8", 1, false)
OPCODE(INT16, "INT16", 1, false)
OPCODE(INT32, "INT32", 1, false)
OPCODE(INT64, "INT64", 2, false) // need 2 cells to store int64_t value
OPCODE(INTNAT, "INTNAT", 2, false) // need 2 cells to store int native value in a 64-bit machine
OPCODE(FLOAT16, "FLOAT16", 1, false)
OPCODE(FLOAT32, "FLOAT32", 1, false)
OPCODE(FLOAT64, "FLOAT64", 2, false) // need 2 cells to store double value
OPCODE(RUNE, "RUNE", 1, false)
OPCODE(BOOL, "BOOL", 1, false)
OPCODE(UNIT, "UNIT", 0, false)
OPCODE(NULLPTR, "NULLPTR", 0, false)
OPCODE(STRING, "STRING", 1, false)
OPCODE(TUPLE, "TUPLE", 1, false)
OPCODE(ARRAY, "ARRAY", 1, false)
OPCODE(VARRAY, "VARRAY", 1, false)         // size
OPCODE(VARRAY_BY_VALUE, "VARRAY_BY_VALUE", 0, false)
OPCODE(VARRAY_GET, "VARRAY_GET", 1, false) // path size
OPCODE(FUNC, "FUNC", 1, false)
// Terminators
OPCODE(RETURN, "RETURN", 0, false) // return to call site
OPCODE(JUMP, "JUMP", 1, false)   // Jump to subsequent index
OPCODE(BRANCH, "BRANCH", 2, false) // Branch
// Interpreter specific
OPCODE(EXIT, "EXIT", 0, false)
OPCODE(STORE, "STORE", 0, false)  // same as ASG but does not produce result
OPCODE(DROP, "DROP", 0, false)   // Drop operation from argument stack
OPCODE(GETREF, "GETREF", 1, false) // path size + variable sized path
OPCODE(STOREINREF, "STOREINREF", 1, false) // path size + variable sized path
// Active Operations
// Unary operations
OPCODE(UN_NEG, "UN_NEG", 2, false)  // type kind, overflow strategy
OPCODE(UN_NEG_EXC, "UN_NEG_EXC", 3, true)  // type kind, overflow strategy, jump index for exception
OPCODE(UN_NOT, "UN_NOT", 2, false)
OPCODE(UN_BITNOT, "UN_BITNOT", 2, false)
OPCODE(UN_INC, "UN_INC", 2, false)
OPCODE(UN_DEC, "UN_DEC", 2, false)
// Binary operations
OPCODE(BIN_ADD, "BIN_ADD", 2, false) // type kind, overflow strategy
OPCODE(BIN_SUB, "BIN_SUB", 2, false)
OPCODE(BIN_MUL, "BIN_MUL", 2, false)
OPCODE(BIN_DIV, "BIN_DIV", 2, false)
OPCODE(BIN_MOD, "BIN_MOD", 2, false)
OPCODE(BIN_EXP, "BIN_EXP", 2, false)
OPCODE(BIN_ADD_EXC, "BIN_ADD_EXC", 3, true) // type kind, overflow strategy, jump index for exception
OPCODE(BIN_SUB_EXC, "BIN_SUB_EXC", 3, true)
OPCODE(BIN_MUL_EXC, "BIN_MUL_EXC", 3, true)
OPCODE(BIN_DIV_EXC, "BIN_DIV_EXC", 3, true)
OPCODE(BIN_MOD_EXC, "BIN_MOD_EXC", 3, true)
OPCODE(BIN_EXP_EXC, "BIN_EXP_EXC", 3, true)
OPCODE(BIN_LT, "BIN_LT", 2, false)
OPCODE(BIN_GT, "BIN_GT", 2, false)
OPCODE(BIN_LE, "BIN_LE", 2, false)
OPCODE(BIN_GE, "BIN_GE", 2, false)
OPCODE(BIN_NOTEQ, "BIN_NOTEQ", 2, false)
OPCODE(BIN_EQUAL, "BIN_EQUAL", 2, false)
OPCODE(BIN_BITAND, "BIN_BITAND", 2, false)
OPCODE(BIN_BITOR, "BIN_BITOR", 2, false)
OPCODE(BIN_BITXOR, "BIN_BITXOR", 2, false)
OPCODE(BIN_LSHIFT, "BIN_LSHIFT", 3, false) // type kind, overflow strategy, right operand type kind
OPCODE(BIN_RSHIFT, "BIN_RSHIFT", 3, false)
OPCODE(BIN_LSHIFT_EXC, "BIN_LSHIFT_EXC", 4, true) // type kind, overflow strategy, right operand type kind, exception
OPCODE(BIN_RSHIFT_EXC, "BIN_RSHIFT_EXC", 4, true)
// All other operations
OPCODE(FIELD, "FIELD", 1, false)
OPCODE(FIELD_TPL, "FIELD_TPL", 1, false) // number of indexes n + n indexes
OPCODE(INVOKE, "INVOKE", 2, false) // number of arguments and method name
OPCODE(INVOKE_EXC, "INVOKE_EXC", 3, true) // number of arguments, method name, jump index for exception
OPCODE(TYPECAST, "TYPECAST", 3, false) // source type kind, target type kind, overflow stategy
OPCODE(TYPECAST_EXC, "TYPECAST_EXC", 4, true) // src + target type kind, overflow strat, jump index for exception
OPCODE(INSTANCEOF, "INSTANCEOF", 1, false)
OPCODE(APPLY, "APPLY", 1, false)
OPCODE(APPLY_EXC, "APPLY_EXC", 2, true) // number of arguments and  and jump index for exception
OPCODE(CAPPLY, "CAPPLY", 1, false) // same as APPLY but used for CFunc
OPCODE(ASG, "ASG", 0, false)
OPCODE(DEREF, "DEREF", 0, false)
OPCODE(RAISE, "RAISE", 0, false)
OPCODE(RAISE_EXC, "RAISE_EXC", 1, true) // taraget block
OPCODE(GET_EXCEPTION, "GET_EXCEPTION", 0, true)
OPCODE(SYSCALL, "SYSCALL", 2, false)
OPCODE(INTRINSIC0, "INTRINSIC0", 1, false)  // intrinsic ID
OPCODE(INTRINSIC1, "INTRINSIC1", 2, false)  // intrinsic ID, extra info 1
OPCODE(INTRINSIC2, "INTRINSIC2", 3, false) // intrinsic ID, extra info 1, extra info 2
// static_cast<size_t>(INTRINSICx_EXC) should be precisely static_cast<size_t>(INTRINSICx) + 3
OPCODE(INTRINSIC0_EXC, "INTRINSIC0_EXC", 2, true) // intrinsic ID, jump index for exception
OPCODE(INTRINSIC1_EXC, "INTRINSIC1_EXC", 3, true) // intrinsic ID, extra info 1, jump index for exception
OPCODE(INTRINSIC2_EXC, "INTRINSIC2_EXC", 4, true) // intrinsic ID, extra info 1, extra info 2, jump index for exception
OPCODE(SWITCH, "SWITCH", 2, false) // value type, number of cases
OPCODE(SPAWN, "SPAWN", 0, false)
OPCODE(SPAWN_EXC, "SPAWN_EXC", 1, true) // jump index for exception
// box/unbox and generics
OPCODE(BOX, "BOX", 1, false) // class ID
OPCODE(UNBOX, "UNBOX", 0, false)
OPCODE(UNBOX_REF, "UNBOX_REF", 0, false)
OPCODE(NOT_SUPPORTED, "NOT_SUPPORTED", 0, false)
OPCODE(ABORT, "ABORT", 0, false) // abort interpretation, can exist only in const eval BCHIR but shouldn't be reached
OPCODE(INVALID, "INVALID", 0, false)
#endif // OPCODE
