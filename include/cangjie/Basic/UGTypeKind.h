// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_UGTYPEKIND_H
#define CANGJIE_UGTYPEKIND_H

enum UGTypeKind {
    UG_CLASS = -128,
    UG_INTERFACE = -127,
    UG_RAWARRAY = -126,
    UG_FUNC = -125,
    UG_COMMON_ENUM = -124,
    UG_WEAKREF = -123,
    UG_INTEROP_FOREIGN_PROXY = -122,
    UG_INTEROP_EXPORTED_REF = -121,
    UG_GENERIC_CUSTOM = -2,
    UG_GENERIC = -1,

    UG_NOTHING = 0,
    UG_UNIT = 1,
    UG_BOOLEAN = 2,
    UG_RUNE = 3,
    UG_UINT8 = 4,
    UG_UINT16 = 5,
    UG_UINT32 = 6,
    UG_UINT64 = 7,
    UG_UINT_NATIVE = 8,
    UG_INT8 = 9,
    UG_INT16 = 10,
    UG_INT32 = 11,
    UG_INT64 = 12,
    UG_INT_NATIVE = 13,
    UG_FLOAT16 = 14,
    UG_FLOAT32 = 15,
    UG_FLOAT64 = 16,
    UG_CSTRING = 17,
    UG_CPOINTER = 18,
    UG_CFUNC = 19,
    UG_VARRAY = 20,
    UG_TUPLE = 21,
    UG_STRUCT = 22,
    UG_ENUM = 23,
};
#endif // CANGJIE_UGTYPEKIND_H
