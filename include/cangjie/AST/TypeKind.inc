// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * Only list user writable types in this file.
 * The ORDER of tokens below is IMPORTANT. Once modified, the following must be modified as well.
 * 'enum TypeKind:int16' in CjoFormat.fbs
 * 'external class TokenKindHelper' in TokenKind.cj std.ast
 * 'Patterns: Array<(TokenKind*UInt16*String*String)>' in TokenKind.cj
 * 'operator func ==(right: TokenKind)' in TokenKind.cj
 * 'external enum TokenKind' in TokenKind.cj
 */
#ifdef TYPE_KIND
TYPE_KIND(TYPE_INVALID, InvalidTy, "Invalid")          /**< Invalid type. */
TYPE_KIND(TYPE_UNIT, PrimitiveTy, "Unit")              /**< unit. */
TYPE_KIND(TYPE_INT8, PrimitiveTy, "Int8")              /**< Int8. */
TYPE_KIND(TYPE_INT16, PrimitiveTy, "Int16")            /**< Int16. */
TYPE_KIND(TYPE_INT32, PrimitiveTy, "Int32")            /**< Int32. */
TYPE_KIND(TYPE_INT64, PrimitiveTy, "Int64")            /**< Int64. */
TYPE_KIND(TYPE_INT_NATIVE, PrimitiveTy, "IntNative")   /**< IntNative. */
TYPE_KIND(TYPE_IDEAL_INT, PrimitiveTy, "Int")          /**< Ideal int for default integer literal. */
TYPE_KIND(TYPE_UINT8, PrimitiveTy, "UInt8")            /**< UInt8. */
TYPE_KIND(TYPE_UINT16, PrimitiveTy, "UInt16")          /**< UInt16. */
TYPE_KIND(TYPE_UINT32, PrimitiveTy, "UInt32")          /**< UInt32. */
TYPE_KIND(TYPE_UINT64, PrimitiveTy, "UInt64")          /**< UInt64. */
TYPE_KIND(TYPE_UINT_NATIVE, PrimitiveTy, "UIntNative") /**< UIntNative. */
TYPE_KIND(TYPE_FLOAT16, PrimitiveTy, "Float16")        /**< Float16. */
TYPE_KIND(TYPE_FLOAT32, PrimitiveTy, "Float32")        /**< Float32. */
TYPE_KIND(TYPE_FLOAT64, PrimitiveTy, "Float64")        /**< Float64. */
TYPE_KIND(TYPE_IDEAL_FLOAT, PrimitiveTy, "Float")      /**< Ideal float for default float literal. */
TYPE_KIND(TYPE_RUNE, PrimitiveTy, "Rune")              /**< Rune. */
TYPE_KIND(TYPE_NOTHING, NothingTy, "Nothing")          /**< The Nothing type. */
TYPE_KIND(TYPE_BOOLEAN, PrimitiveTy, "Bool")           /**< Bool. */
TYPE_KIND(TYPE_TUPLE, TupleTy, "Tuple")                /**< Tuple. */
TYPE_KIND(TYPE_ENUM, EnumTy, "Enum")                   /**< Enum. */
TYPE_KIND(TYPE_FUNC, FuncTy, "Function")               /**< Function type) like (float32) -> Int32 */
TYPE_KIND(TYPE_STRUCT, StructTy, "Struct")             /**< Struct. */
TYPE_KIND(TYPE_ARRAY, ArrayTy, "RawArray")             /**< Array. */
TYPE_KIND(TYPE_VARRAY, VArrayTy, "VArray")             /**< VArray. */
TYPE_KIND(TYPE_POINTER, PointerTy, "CPointer")         /**< Pointer. */
TYPE_KIND(TYPE_CSTRING, CStringTy, "CString")          /**< CString. */
TYPE_KIND(TYPE_CLASS, ClassTy, "Class")                /**< Class. */
TYPE_KIND(TYPE_INTERFACE, InterfaceTy, "Interface")    /**< Interface. */
TYPE_KIND(TYPE, TypeAliasTy, "TypeAlias")              /**< Type alias ty. */
TYPE_KIND(TYPE_GENERICS, GenericsTy, "Generic")        /**< Generic type. */
#endif
