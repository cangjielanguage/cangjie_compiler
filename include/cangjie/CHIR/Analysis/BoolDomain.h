// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_BOOL_DOMAIN_H
#define CANGJIE_CHIR_ANALYSIS_BOOL_DOMAIN_H

#include "cangjie/CHIR/IR/Expression/Terminator.h"

namespace Cangjie::CHIR {
using PtrSymbol = Ptr<Value>;

/// Represents all possible values of a CHIRNode that has Ty bool.
class BoolDomain {
public:
    /// deleted constructor, use BoolDomain::FromBool instead.
    BoolDomain(bool) = delete;

    BoolDomain(const BoolDomain& other);

    BoolDomain(BoolDomain&& other);

    BoolDomain& operator=(const BoolDomain& other);

    BoolDomain& operator=(BoolDomain&& other);

    ~BoolDomain();

    /// Shared instances that represents all the possible values of BoolDomain.
    static BoolDomain True();
    static BoolDomain False();
    static BoolDomain Top();
    static BoolDomain Bottom();

    bool IsTrue() const;
    bool IsFalse() const;
    /// every bool is possible.
    bool IsTop() const;
    /// every bool is not possible or init state.
    bool IsBottom() const;
    /// non top
    bool IsNonTrivial() const;
    /// whether state is determined.
    bool IsSingleValue() const;
    /// get determined state.
    bool GetSingleValue() const;

    /// Construct from bool value
    static BoolDomain FromBool(bool v);

    /// operator of bool
    friend BoolDomain operator&(const BoolDomain& a, const BoolDomain& b);
    friend BoolDomain operator|(const BoolDomain& a, const BoolDomain& b);
    friend BoolDomain LogicalAnd(const BoolDomain& a, const BoolDomain& b);
    friend BoolDomain LogicalOr(const BoolDomain& a, const BoolDomain& b);
    friend BoolDomain operator!(const BoolDomain& v);
    friend std::ostream& operator<<(std::ostream& out, const BoolDomain& v);

    /// union of two states
    static BoolDomain Union(const BoolDomain& a, const BoolDomain& b);

    /// whether two states are same
    bool IsSame(const BoolDomain& domain) const;
private:
    unsigned v;
    // Construct from integer value \p v. This constructor is private; use True/False/Top/Bottom instead.
    explicit BoolDomain(unsigned v);
};
// operator== on BoolDomain is deleted because there is no definite meaning of equality on BoolDomain, be it the
// identity of a boolean domain or the identity of boolean logical value. Use IsTrue/IsTop/... to check the value
// of a BoolDomain
bool operator==(const BoolDomain& a, const BoolDomain& b) = delete;
} // namespace Cangjie::CHIR

#endif