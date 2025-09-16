// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares normal type and exception type maping struct template.
 */

#ifndef CANGJIE_CHIR_EXCEPTION_TYPEMAPPING_H
#define CANGJIE_CHIR_EXCEPTION_TYPEMAPPING_H

#include "cangjie/CHIR/Expression/Terminator.h"

namespace Cangjie::CHIR {
template <typename T> struct CHIRNodeMap {
};

// Defined CHIR type mapping register macro.
#define DEFINE_CHIR_TYPE_MAPPING(TYPE)                                                                                 \
    template <> struct CHIRNodeMap<TYPE> {                                                                             \
        using Normal = TYPE;                                                                                           \
        using Exception = TYPE##WithException;                                                                         \
    }

DEFINE_CHIR_TYPE_MAPPING(CHIR::Apply);
DEFINE_CHIR_TYPE_MAPPING(CHIR::Invoke);
DEFINE_CHIR_TYPE_MAPPING(CHIR::InvokeStatic);
DEFINE_CHIR_TYPE_MAPPING(CHIR::TypeCast);
DEFINE_CHIR_TYPE_MAPPING(CHIR::Allocate);
DEFINE_CHIR_TYPE_MAPPING(CHIR::Spawn);
DEFINE_CHIR_TYPE_MAPPING(CHIR::Intrinsic);
DEFINE_CHIR_TYPE_MAPPING(CHIR::RawArrayAllocate);

template <> struct CHIRNodeMap<UnaryExpression> {
    using Normal = CHIR::UnaryExpression;
    using Exception = CHIR::IntOpWithException;
};
template <> struct CHIRNodeMap<BinaryExpression> {
    using Normal = CHIR::BinaryExpression;
    using Exception = CHIR::IntOpWithException;
};

template <typename T> using CHIRNodeNormalT = typename CHIRNodeMap<T>::Normal;
template <typename T> using CHIRNodeExceptionT = typename CHIRNodeMap<T>::Exception;
} // namespace Cangjie::CHIR
#endif