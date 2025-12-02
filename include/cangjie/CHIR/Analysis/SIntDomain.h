// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_ANALYSIS_SINT_DOMAIN_H
#define CANGJIE_CHIR_ANALYSIS_SINT_DOMAIN_H

#include "cangjie/CHIR/Analysis/BoolDomain.h"
#include "cangjie/CHIR/Analysis/ConstantRange.h"
#include "cangjie/CHIR/Analysis/SInt.h"
#include "cangjie/CHIR/Analysis/Utils.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"

namespace Cangjie::CHIR {
/**
 * @brief mem template define, using for pointer of SIntDomain
 */
template <class T> using Mem = T;

/**
 * @brief domain structure of SInt, using as value to const analysis.
 */
class SIntDomain {
public:
    /// Construct an SInt domain with \p numeric as numeric range
    /// \p isUnsigned indicates whether the numeric range is a unsigned range or a signed range
    /// symbolic ranges are always stored as signed ranges
    SIntDomain(const ConstantRange& numeric, bool isUnsigned);
    SIntDomain(const SIntDomain& other) = default;
    SIntDomain(SIntDomain&& other) = default;
    // no copy assignment
#ifdef __APPLE__
    // The code doesn't compile on MacOS without operator=.
    SIntDomain& operator=(const SIntDomain& other) = default;
#else
    SIntDomain& operator=(const SIntDomain& other) = delete;
#endif
    SIntDomain& operator=(SIntDomain&& other) = default;
    ~SIntDomain() = default;

    /**
     * @brief from Symbol to constant range.
     */
    using SymbolicBoundsMap = std::map<PtrSymbol, ConstantRange>;
    /// Construct an SIntDomain with \p numeric as numeric range and \p symbolics as symbolic range
    /// This is usually used only for internal constructs.
    /// Note that each numeric bound of symbolic ranges must be non trivial.
    SIntDomain(const ConstantRange& numeric, SymbolicBoundsMap&& symbolics, bool isUnsigned);

    /// Constructs an SIntDomain with \p numeric as numeric range and one pair of symbolic range represented
    /// by {\p symbol, \p symbolicBound}.
    SIntDomain(const ConstantRange& numeric, PtrSymbol symbol, const ConstantRange& symbolicBound);

    /// check domain is top
    bool IsTop() const;

    /// check domain is non top
    bool IsNonTrivial() const;

    /// check domain is bottom
    bool IsBottom() const;

    /**
     * @brief beauty print formatter for SIntDomain symbol.
     */
    struct SymbolicFormatter final : public ConstantRange::Formatter {
        /// symbol to print.
        PtrSymbol symbol;
        /// output ostream function.
        friend std::ostream& operator<<(std::ostream& out, const SymbolicFormatter& fmt);
    };

    /**
     * @brief beauty print formatter for SIntDomain.
     */
    struct Formatter final {
        /// SIntDomain to print.
        const SIntDomain& d;
        /// whether SIntDomain is unsigned.
        const bool asUnsigned;
        /// output radix for domain.
        const Radix radix;

        /// constructor for beauty print formatter of SIntDomain
        template <class... Args>
        Formatter(const SIntDomain& value, bool asUnsigned, Radix radix)
            : d{value}, asUnsigned{asUnsigned}, radix{radix}
        {
        }

        /// get domain from formatter.
        const SIntDomain* operator->() const;
    };

    /// output function of beauty print formatter for SIntDomain.
    friend std::ostream& operator<<(std::ostream& out, const Formatter& fmt);

    /// beauty printer for SIntDomain with specific unsigned flag and radix.
    Formatter ToString(bool asUnsigned, Radix radix = Radix::R10) const;

    /// default beauty printer for SIntDomain.
    Formatter ToString() const;

    /// output function for SIntDomain.
    friend std::ostream& operator<<(std::ostream& out, const SIntDomain& d);

    /**
     * @brief static constructor from CHIR literal value.
     * @param literal CHIR literal value.
     * @return pointer of SIntDomain.
     */
    static Mem<SIntDomain> From(const LiteralValue& literal);

    /**
     * @brief static constructor from SInt value and relation, for example < 10.
     * @param rel compare operator.
     * @param value SInt number.
     * @param isUnsigned whether range is unsigned.
     * @return pointer of SIntDomain.
     */
    static Mem<SIntDomain> FromNumeric(RelationalOperation rel, const SInt& value, bool isUnsigned);

    /**
     * @brief static constructor from symbol and relation.
     * @param rel compare operator.
     * @param symbol symbol to indicate range.
     * @param width SInt width.
     * @param isUnsigned whether range is unsigned.
     * @return pointer of SIntDomain.
     */
    static Mem<SIntDomain> FromSymbolic(RelationalOperation rel, PtrSymbol symbol, IntWidth width, bool isUnsigned);

    /**
     * @brief static constructor of top domain
     * @param width SInt width
     * @param isUnsigned whether range is unsigned.
     * @return pointer of SIntDomain.
     */
    static Mem<SIntDomain> Top(IntWidth width, bool isUnsigned);

    /**
     * @brief static constructor of bottom domain
     * @param width SInt width
     * @param isUnsigned whether range is unsigned.
     * @return pointer of SIntDomain.
     */
    static Mem<SIntDomain> Bottom(IntWidth width, bool isUnsigned);

    /// Get constant range from domain.
    const ConstantRange& NumericBound() const&;

    /// Get constant range from domain.
    ConstantRange NumericBound() &&;

    /// Get domain width.
    IntWidth Width() const;

    /// whether domain is unsigned.
    bool IsUnsigned() const;

    /// Symbolic bounds map iterate helper
    struct SymbolicBoundsMapIterator {
        /// iterator constructor for symbolic bounds.
        SymbolicBoundsMapIterator(const SymbolicBoundsMap& map);

        /// get begin iterator.
        std::map<PtrSymbol, ConstantRange>::const_iterator Begin() const;

        /// get end iterator.
        std::map<PtrSymbol, ConstantRange>::const_iterator End() const;

        /// check whether map is empty.
        bool Empty() const;
    private:
        /// symbolic Bounds map from SIntDomain.
        const SymbolicBoundsMap& map;
    };

    /// get symbolic bounds from SIntDomain.
    SymbolicBoundsMapIterator SymbolicBounds() const;

    /// Returns a pointer to the ConstantRange if this domain has a bound against \p symbol, or null pointer otherwise
    const ConstantRange* FindSymbolicBound(PtrSymbol symbol) const;

    /// check if constant range in domain is single value. [a, a+1)
    bool IsSingleValue() const;

    /// get width from CHIR type
    static IntWidth ToWidth(const Ptr<Type>& type);

    ///  get intersect domain from two domains.
    static Mem<SIntDomain> Intersects(const Mem<SIntDomain>& lhs, const Mem<SIntDomain>& rhs);

    ///  get union domain from two domains.
    static Mem<SIntDomain> Unions(const Mem<SIntDomain>& lhs, const Mem<SIntDomain>& rhs);

    /// check whether domain is same as input domain.
    bool IsSame(const SIntDomain& domain) const;

private:
    ConstantRange numeric;
    SymbolicBoundsMap symbolics;
    bool unsignedFlag;
};

/**
 * @brief get opposite relation operator from input relation operation.
 * @param a input relation operation.
 * @return opposite relation operation.
 */
RelationalOperation SymbolicNeg(RelationalOperation a);

/**
 * @brief arithmetic operation info collector.
 */
struct CHIRArithmeticBinopArgs {
    /// binary operation left side domain
    const SIntDomain& ld;
    /// binary operation right side domain
    const SIntDomain& rd;
    /// Resolved lhs & rhs symbols (i.e. resolved by calling ValueRangeCache::Projection)
    PtrSymbol l, r;
    /// binary operation op kind.
    ExprKind op;
    /// overflow strategy.
    Cangjie::OverflowStrategy ov;
    /// flag if arithmetic is unsigned operation.
    bool uns;

    CHIRArithmeticBinopArgs(const SIntDomain& ld, const SIntDomain& rd, PtrSymbol l, PtrSymbol r, ExprKind op,
        Cangjie::OverflowStrategy ov, bool isUnsigned)
        : ld{ld},
          rd{rd},
          l{std::move(l)},
          r{std::move(r)},
          op{op},
          ov{ov},
          uns{isUnsigned}
    {
    }
};

/// compute arithmetic binary op with two SIntDomain inputs, one SIntDomain output
SIntDomain ComputeArithmeticBinop(CHIRArithmeticBinopArgs&& args);

/**
 * @brief relation operation info collector, such as <.
 */
struct CHIRRelIntBinopArgs {
    /// binary operation left side domain
    const Mem<SIntDomain>& ld;
    /// binary operation right side domain
    const Mem<SIntDomain>& rd;
    /// Resolved lhs & rhs symbols (i.e. resolved by calling ValueRangeCache::Projection)
    PtrSymbol l, r;
    /// binary operation op kind.
    ExprKind op;
    /// flag if arithmetic is unsigned operation.
    bool uns;

    CHIRRelIntBinopArgs(
        const Mem<SIntDomain>& ld, const Mem<SIntDomain>& rd, PtrSymbol l, PtrSymbol r, ExprKind op, bool isUnsigned)
        : ld{ld}, rd{rd}, l{std::move(l)}, r{std::move(r)}, op{op}, uns{isUnsigned}
    {
    }
};
/// compute relation binary op with two SIntDomain inputs, BoolDomain output
BoolDomain ComputeRelIntBinop(CHIRRelIntBinopArgs&& args);
/// compute equality binary op with two BoolDomain inputs, BoolDomain output
BoolDomain ComputeEqualityBoolBinop(const BoolDomain& ld, const BoolDomain& rd, ExprKind op);

/// constant range converter from unsigned to signed or signed to unsigned.
ConstantRange NumericConversion(
    const ConstantRange& src, IntWidth dstSize, bool srcUnsigned, bool dstUnsigned, Cangjie::OverflowStrategy ov);

/// compute new constant bounds from type cast operations.
ConstantRange ComputeTypeCastNumericBound(
    const SIntDomain& v, IntWidth dstSize, bool dstUnsigned, Cangjie::OverflowStrategy ov);
} // namespace Cangjie::CHIR

#endif