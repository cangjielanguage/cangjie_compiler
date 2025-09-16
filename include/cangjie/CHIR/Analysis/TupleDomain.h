// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_TUPLE_DOMAIN_H
#define CANGJIE_CHIR_ANALYSIS_TUPLE_DOMAIN_H

#include "cangjie/CHIR/Expression/Terminator.h"

namespace Cangjie::CHIR {
/// Represents a tuple, a struct or a value-type enum.
/// Note that TupleDomain is mutable
struct TupleDomain final {
    using FieldType = Ptr<const Value>;

    /// init tuple domain with field number.
    explicit TupleDomain(size_t fieldNum);

    /// init tuple domain with field number and field value.
    TupleDomain(size_t fieldNum, std::vector<FieldType> fieldValue);

    /// tuple domain copy constructor.
    TupleDomain(const TupleDomain& other) = default;

    /// tuple domain move constructor.
    TupleDomain(TupleDomain&& other) = default;

    /// init from tuple.
    explicit TupleDomain(const Tuple& tuple);
#ifdef __APPLE__
    TupleDomain& operator=(const TupleDomain& other) = default;
#else
    TupleDomain& operator=(const TupleDomain& other) = delete;
#endif
    /// tuple domain move operator.
    TupleDomain& operator=(TupleDomain&& other);

    /// check tuple domain is top.
    bool IsTop() const;

    /// get field number from this domain.
    size_t FieldNum() const;

    /// get tuple item with index input.
    FieldType operator[](size_t index) const;

    /// get tuple item with index input.
    FieldType& operator[](size_t index);

    /// get iterator first
    std::vector<FieldType>::iterator begin();

    /// get iterator end
    std::vector<FieldType>::iterator end();

private:
#ifdef __APPLE__
    size_t num;
#else
    const size_t num;
#endif
    std::vector<FieldType> val; // each field is mutable; use vector for safety issue
};

/// formater to print tuple domain.
std::ostream& operator<<(std::ostream& out, const TupleDomain& value);
} // namespace Cangjie::CHIR

#endif
