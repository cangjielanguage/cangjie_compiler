// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Analysis/SIntDomain.h"
#include "cangjie/CHIR/Analysis/BoolDomain.h"

namespace Cangjie::CHIR {
SIntDomain::SIntDomain(const ConstantRange& numeric, bool isUnsigned)
    : numeric{numeric}, symbolics{}, unsignedFlag{isUnsigned}
{
}

SIntDomain::SIntDomain(const ConstantRange& numeric, SymbolicBoundsMap&& symbolics, bool isUnsigned)
    : numeric{numeric}, symbolics{std::move(symbolics)}, unsignedFlag{isUnsigned}
{
    for (const auto& it : symbolics) {
        CJC_ASSERT(it.second.IsNonTrivial());
    }
}

SIntDomain::SIntDomain(const ConstantRange& numeric, PtrSymbol symbol, const ConstantRange& symbolicBound)
    : numeric{numeric}, symbolics{}, unsignedFlag{symbol->GetType()->IsUnsignedInteger()}
{
    symbolics.emplace(symbol, symbolicBound);
    CJC_ASSERT(symbolicBound.Width() == numeric.Width());
}

bool SIntDomain::IsTop() const
{
    return numeric.IsFullSet() && symbolics.empty();
}

bool SIntDomain::IsNonTrivial() const
{
    return !IsTop();
}

bool SIntDomain::IsBottom() const
{
    return numeric.IsEmptySet();
}

const SIntDomain* SIntDomain::Formatter::operator->() const
{
    return &d;
}

SIntDomain::Formatter SIntDomain::ToString(bool asUnsigned, Radix radix) const
{
    return {*this, asUnsigned, radix};
}

SIntDomain::Formatter SIntDomain::ToString() const
{
    return ToString(unsignedFlag, Radix::R10);
}

std::ostream& operator<<(std::ostream& out, const SIntDomain& d)
{
    return out << d.ToString();
}

Mem<SIntDomain> SIntDomain::From(const LiteralValue& literal)
{
    CJC_ASSERT(literal.IsIntLiteral());
    const IntLiteral* intLiteral = StaticCast<IntLiteral*>(&literal);
    if (intLiteral->IsSigned()) {
        auto v = static_cast<uint64_t>(intLiteral->GetSignedVal());
        return {ConstantRange{{ToWidth(literal.GetType()), v}}, false};
    } else {
        return {ConstantRange{{ToWidth(literal.GetType()), intLiteral->GetUnsignedVal()}}, true};
    }
}

Mem<SIntDomain> SIntDomain::FromNumeric(RelationalOperation rel, const SInt& value, bool isUnsigned)
{
    return {ConstantRange::From(rel, value, !isUnsigned), isUnsigned};
}

Mem<SIntDomain> SIntDomain::FromSymbolic(RelationalOperation rel, PtrSymbol symbol, IntWidth width)
{
    SIntDomain res{ConstantRange::Full(width), /* isUnsigned = */ false};
    if (rel == RelationalOperation::NE) {
        return res;
    }
    // A symbolic bound records the difference `value - symbol`, and consumers always interpret that
    // difference as a signed range (see NumericBoundToSignedSymbolicBound and the signed Compare calls
    // in ComputeTotalOrderingIntBinop). Building it as unsigned would clamp the negative half away:
    // unsigned From(LE, 0) is the single value {0}, which claims `value == symbol` instead of
    // `value <= symbol`.
    //
    // Because the encoding is inherently signed, it can only express an unsigned relation when the
    // difference happens to be signed-representable. It is not in general -- for UInt64 a = 0,
    // b = UInt64.Max, `a <u b` holds while `a - b` is +1 as a signed value -- so callers must not
    // build symbolic bounds from unsigned comparisons.
    res.symbolics.emplace(symbol, ConstantRange::From(rel, {width, 0}, /* isSigned = */ true));
    return res;
}

const ConstantRange& SIntDomain::NumericBound() const&
{
    return numeric;
}

ConstantRange SIntDomain::NumericBound() &&
{
    return numeric;
}

IntWidth SIntDomain::Width() const
{
    return numeric.Width();
}

bool SIntDomain::IsUnsigned() const
{
    return unsignedFlag;
}

SIntDomain::SymbolicBoundsMapIterator::SymbolicBoundsMapIterator(const SymbolicBoundsMap& map) : map{map}
{
}

std::map<PtrSymbol, ConstantRange>::const_iterator SIntDomain::SymbolicBoundsMapIterator::Begin() const
{
    return map.begin();
}

std::map<PtrSymbol, ConstantRange>::const_iterator SIntDomain::SymbolicBoundsMapIterator::End() const
{
    return map.end();
}

bool SIntDomain::SymbolicBoundsMapIterator::Empty() const
{
    return map.empty();
}

SIntDomain::SymbolicBoundsMapIterator SIntDomain::SymbolicBounds() const
{
    return SymbolicBoundsMapIterator{symbolics};
}

const ConstantRange* SIntDomain::FindSymbolicBound(PtrSymbol symbol) const
{
    if (auto it = symbolics.find(symbol); it != symbolics.end()) {
        return &it->second;
    }
    return nullptr;
}

bool SIntDomain::IsSingleValue() const
{
    return NumericBound().IsSingleElement();
}

bool SIntDomain::IsSame(const SIntDomain& domain) const
{
    return numeric == domain.numeric;
}

std::ostream& operator<<(std::ostream& out, const SIntDomain::SymbolicFormatter& fmt)
{
    out << fmt.symbol->GetSrcCodeIdentifier() << ':';
    // operator<< is not virtual function, cast to reference to base class would output it as a pure numeric
    // ConstantRange. Also, cast from reference to reference triggers no derived-class-to-base-class truncating copy.
    return out << static_cast<const ConstantRange::Formatter&>(fmt);
}

std::ostream& operator<<(std::ostream& out, const SIntDomain::Formatter& fmt)
{
    if (fmt->IsBottom()) {
        return out << fmt->NumericBound().ToString(fmt.asUnsigned, fmt.radix);
    }
    if (fmt->symbolics.empty()) {
        return out << fmt->NumericBound().ToString(fmt.asUnsigned, fmt.radix);
    }
    out << ConstantRange::Formatter::DIVIDOR;
    if (fmt->NumericBound().IsNonTrivial()) {
        out << fmt->NumericBound().ToString(fmt.asUnsigned, fmt.radix);
    }
    std::vector<decltype(fmt->SymbolicBounds().Begin())> pt(fmt->symbolics.size());
    auto ptins{pt.begin()};
    for (auto it{fmt->SymbolicBounds().Begin()}; it != fmt->SymbolicBounds().End(); ++it) {
        *ptins++ = it;
    }
    std::sort(pt.begin(), pt.end(), [](const auto& a, const auto& b) {
        return a->first->GetSrcCodeIdentifier() <= b->first->GetSrcCodeIdentifier();
    });
    for (auto it : pt) {
        out << ';' << SIntDomain::SymbolicFormatter{{{false, fmt.radix}, it->second}, it->first};
    }
    return out << ConstantRange::Formatter::DIVIDOR;
}

static RelationalOperation From(const BinaryExprKind& op)
{
    switch (op) {
        case BinaryExprKind::EQUAL:
            return RelationalOperation::EQ;
        case BinaryExprKind::LE:
            return RelationalOperation::LE;
        case BinaryExprKind::LT:
            return RelationalOperation::LT;
        case BinaryExprKind::GT:
            return RelationalOperation::GT;
        case BinaryExprKind::GE:
            return RelationalOperation::GE;
        case BinaryExprKind::NOTEQUAL:
            return RelationalOperation::NE;
        default:
            CJC_ABORT();
            return RelationalOperation::LE;
    }
}

RelationalOperation SymbolicNeg(RelationalOperation a)
{
    switch (a) {
        case RelationalOperation::LT:
            return RelationalOperation::GT;
        case RelationalOperation::LE:
            return RelationalOperation::GE;
        case RelationalOperation::EQ:
            return RelationalOperation::EQ;
        case RelationalOperation::GE:
            return RelationalOperation::LE;
        case RelationalOperation::GT:
            return RelationalOperation::LT;
        default:
            return RelationalOperation::NE;
    }
}

// Region: SIntDomain
namespace {
inline bool IsSaturating(Cangjie::OverflowStrategy ov)
{
    return ov == OverflowStrategy::SATURATING;
}

ConstantRange NumericConversionU2SSameWidth(const ConstantRange& src);
// Convert the numeric bound from an SIntDomain value to signed to be stored as symbolic bound
// as symbolic bounds are always stored as signed range.
// That is, cast the range to signed with throwing strategy if it is an unsigned bound, if this
// cast narrowed the range, the result may not cover all possible symbolic ranges, discard it; otherwise
// return the range. If the range is signed itself, do nothing.
std::optional<ConstantRange> NumericBoundToSignedSymbolicBound(const ConstantRange& origin, bool isUnsigned)
{
    if (!isUnsigned) {
        return origin;
    }
    auto res{NumericConversionU2SSameWidth(origin)};
    if (res == origin) {
        return res;
    }
    return std::nullopt;
}

SIntDomain ComputeAdd(const CHIRArithmeticBinopArgs& args)
{
    auto& lhs = args.ld;
    auto& rhs = args.rd;
    auto& ln = lhs.NumericBound();
    auto& rn = rhs.NumericBound();
    bool isUnsigned = args.uns;
    ConstantRange numeric = !IsSaturating(args.ov) ? ln.Add(rn) : (isUnsigned ? ln.UAddSat(rn) : ln.SAddSat(rn));
    // combine numeric range with arithmetic range is only safe in THROWING strategy
    if (numeric.IsEmptySet() || args.ov != OverflowStrategy::THROWING) {
        return {numeric, isUnsigned};
    }
    SIntDomain::SymbolicBoundsMap mp{};
    if (!ln.IsSingleElement() && rn.IsNonTrivial()) {
        if (auto rhsAsSymbolicRange{NumericBoundToSignedSymbolicBound(rn, isUnsigned)}) {
            mp.emplace(args.l, *rhsAsSymbolicRange);
        }
    }
    if (!rn.IsSingleElement() && ln.IsNonTrivial()) {
        if (auto lhsAsSymbolicRange{NumericBoundToSignedSymbolicBound(ln, isUnsigned)}) {
            mp.emplace(args.r, *lhsAsSymbolicRange);
        }
    }
    return {numeric, std::move(mp), isUnsigned};
}

SIntDomain ComputeSub(const CHIRArithmeticBinopArgs& args)
{
    auto& lhs = args.ld;
    auto& rhs = args.rd;
    auto& ln = lhs.NumericBound();
    auto& rn = rhs.NumericBound();
    ConstantRange numeric = !IsSaturating(args.ov) ? ln.Sub(rn) : (args.uns ? ln.USubSat(rn) : ln.SSubSat(rn));
    if (numeric.IsEmptySet() || args.ov != OverflowStrategy::THROWING) {
        return {numeric, args.uns};
    }
    if (!ln.IsSingleElement() && rn.IsNonTrivial()) {
        // symbola - symbolb: if a is not a single element, add a symbolic bound:
        // symbola:[-b.upper,-b.lower]
        // Also, cannot negate signed min (implementation issue)
        // In that case, the symbolic bound is discarded
        if (rn != SInt::SMinValue(rn.Width())) {
            return {numeric, args.l, rn.Negate()};
        }
    }
    return {numeric, args.uns};
}

SIntDomain ComputeMul(const CHIRArithmeticBinopArgs& args)
{
    auto& ln = args.ld.NumericBound();
    auto& rn = args.rd.NumericBound();
    bool isUnsigned = args.uns;
    ConstantRange r{!IsSaturating(args.ov) ? (isUnsigned ? ln.UMul(rn) : ln.SMul(rn))
                                           : (isUnsigned ? ln.UMulSat(rn) : ln.SMulSat(rn))};
    return {r, args.uns};
}

SIntDomain ComputeDiv(const CHIRArithmeticBinopArgs& args)
{
    auto& ln = args.ld.NumericBound();
    auto& rn = args.rd.NumericBound();
    if (rn.Contains(SInt::Zero(rn.Width()))) {
        // SDiv will return set in which the result may lie when operation is valid. If divisor constains 0,
        // then sdiv will still compute the results for the parts where the divisor is not 0.
        // So it is necessary to check 0 in advance.
        return {ConstantRange::Full(ln.Width()), args.uns};
    }
    ConstantRange r{args.uns ? ln.UDiv(rn) : (IsSaturating(args.ov) ? ln.SDivSat(rn) : ln.SDiv(rn))};
    return {r, args.uns};
}

SIntDomain ComputeMod(const CHIRArithmeticBinopArgs& args)
{
    auto& ln = args.ld.NumericBound();
    auto& rn = args.rd.NumericBound();
    return args.uns ? SIntDomain{ln.URem(rn), true} : SIntDomain{ln.SRem(rn), false};
}

// Define the result of a relational operation that may yield unordered
enum class CompareResult : uint8_t { UNO, TRUE, FALSE };
bool ToBool(CompareResult r)
{
    return r == CompareResult::TRUE;
}

CompareResult Neg(CompareResult a)
{
    switch (a) {
        case CompareResult::UNO:
            return CompareResult::UNO;
        case CompareResult::TRUE:
            return CompareResult::FALSE;
        default:
            return CompareResult::TRUE;
    }
}

CompareResult Unless(bool exp)
{
    return exp ? CompareResult::TRUE : CompareResult::UNO;
}

CompareResult Lt(const ConstantRange& a, const ConstantRange& b, bool isUnsigned)
{
    return Unless(isUnsigned ? a.UMaxValue().Ult(b.UMinValue()) : a.SMaxValue().Slt(b.SMinValue()));
}

CompareResult Le(const ConstantRange& a, const ConstantRange& b, bool isUnsigned)
{
    return Unless(isUnsigned ? a.UMaxValue().Ule(b.UMinValue()) : a.SMaxValue().Sle(b.SMinValue()));
}

CompareResult Ge(const ConstantRange& a, const ConstantRange& b, bool isUnsigned)
{
    return Unless(isUnsigned ? a.UMinValue().Uge(b.UMaxValue()) : a.SMinValue().Sge(b.SMaxValue()));
}

CompareResult Gt(const ConstantRange& a, const ConstantRange& b, bool isUnsigned)
{
    return Unless(isUnsigned ? a.UMinValue().Ugt(b.UMaxValue()) : a.SMinValue().Sgt(b.SMaxValue()));
}

// Return the first ordered (i.e. True/False) result of a and b, or Unordered if both Unordered.
// The result is undefined if both operands are not Unordered.
CompareResult UnlessBothUnordered(CompareResult a, CompareResult b)
{
    return a != CompareResult::UNO ? a : b;
}

// Return the operation that always returns false when the original returns true
RelationalOperation Neg(RelationalOperation a)
{
    switch (a) {
        case RelationalOperation::LT:
            return RelationalOperation::GE;
        case RelationalOperation::LE:
            return RelationalOperation::GT;
        case RelationalOperation::EQ:
            return RelationalOperation::NE;
        case RelationalOperation::GE:
            return RelationalOperation::LT;
        case RelationalOperation::GT:
            return RelationalOperation::LE;
        default:
            return RelationalOperation::EQ;
    }
}

CompareResult PartialCompare(
    RelationalOperation compare, const ConstantRange& a, const ConstantRange& b, bool isUnsigned)
{
    // cannot compare wrapped set
    if (isUnsigned) {
        if (a.IsWrappedSet() || b.IsWrappedSet()) {
            return CompareResult::UNO;
        }
    } else {
        if (a.IsSignWrappedSet() || b.IsSignWrappedSet()) {
            return CompareResult::UNO;
        }
    }
    switch (compare) {
        case RelationalOperation::LE:
            return Le(a, b, isUnsigned);
        case RelationalOperation::LT:
            return Lt(a, b, isUnsigned);
        case RelationalOperation::GT:
            return Gt(a, b, isUnsigned);
        case RelationalOperation::GE:
            return Ge(a, b, isUnsigned);
        default:
            CJC_ABORT();
            return CompareResult::FALSE;
    }
}

bool IsUnsignedCompareToZero(const ConstantRange& a)
{
    if (a.IsSingleElement() && a.GetSingleElement() == 0) {
        return true;
    }
    return false;
}

CompareResult Compare(RelationalOperation compare, const ConstantRange& a, const ConstantRange& b, bool isUnsigned)
{
    if (a.IsEmptySet() || b.IsEmptySet()) {
        return CompareResult::UNO;
    }
    if ((a.IsFullSet() && !IsUnsignedCompareToZero(b)) || (b.IsFullSet() && !IsUnsignedCompareToZero(a))) {
        return CompareResult::UNO;
    }
    return UnlessBothUnordered(
        PartialCompare(compare, a, b, isUnsigned), Neg(PartialCompare(Neg(compare), a, b, isUnsigned)));
}

// Compute the binary result for total ordering (i.e. lt, le, gt, ge)
decltype(BoolDomain::Top()) ComputeTotalOrderingIntBinop(const CHIRRelIntBinopArgs& args)
{
    auto a = args.l;
    auto b = args.r;
    CJC_ASSERT(a != b);
    auto op = From(args.op);
    bool uns = args.uns;
    auto& ia{args.ld};
    auto& ib{args.rd};
    if (ia.IsBottom() || ib.IsBottom()) {
        return BoolDomain::Bottom();
    }
    auto width = ia.Width();
    if (auto r = Compare(op, ia.NumericBound(), ib.NumericBound(), uns); r != CompareResult::UNO) {
        return BoolDomain::FromBool(ToBool(r));
    }
    if (auto ibs = ia.FindSymbolicBound(b)) {
        // symbol `a' is equivalent to a[0]..a[0], so we have
        // a[0]..a..a[0], b[ibs.second.lower]..b..a[ibs.second.upper]
        if (auto r = Compare(op, *ibs, ConstantRange{{width, 0}}, false); r != CompareResult::UNO) {
            return BoolDomain::FromBool(ToBool(r));
        }
    }
    if (auto ias = ib.FindSymbolicBound(a)) {
        // b[ias.second.lower].a..b[ias.second.upper], b[0]..b..b[0]
        if (auto r = Compare(SymbolicNeg(op), *ias, ConstantRange{{width, 0}}, false); r != CompareResult::UNO) {
            return BoolDomain::FromBool(ToBool(r));
        }
    }
    // traverse the two symbolic bound maps to find symbolic bound against a common symbol
    auto ita = ia.SymbolicBounds().Begin();
    auto itb = ib.SymbolicBounds().Begin();
    while (ita != ia.SymbolicBounds().End() && itb != ib.SymbolicBounds().End()) {
        if (ita->first.get() < itb->first.get()) {
            ++ita;
            continue;
        }
        if (ita->first.get() > itb->first.get()) {
            ++itb;
            continue;
        }
        // compare the symbolic bound so we can generate range info based on transitive partial
        // ordering relation
        if (auto r = Compare(op, ita++->second, itb++->second, false); r != CompareResult::UNO) {
            return BoolDomain::FromBool(ToBool(r));
        }
    }
    return BoolDomain::Top();
}

// Try to find transitive equality relation from the symbolic bounds of two symbol
// i.e. they have symbolic bound against the same symbol, and if their bounds do not intersect, the equality operation
// would never yield true.
// Returns null if no decisive result exists.
decltype(BoolDomain::Top()) CompareTransitiveEquality(decltype(std::declval<SIntDomain>().SymbolicBounds()) ia,
    decltype(std::declval<SIntDomain>().SymbolicBounds()) ib, bool cne)
{
    auto ita = ia.Begin();
    auto itb = ib.Begin();
    while (ita != ia.End() && itb != ib.End()) {
        if (ita->first.get() < itb->first.get()) {
            ++ita;
            continue;
        }
        if (ita->first.get() > itb->first.get()) {
            ++itb;
            continue;
        }
        // compare the symbolic bound so we can generate range info based on transitive equality relation
        auto sa = ita++->second;
        auto sb = itb++->second;
        if (sa.IsSingleElement() && sb.IsSingleElement()) {
            // a bound to c..[c1,c2], b bound to c..[c3,c4], if c1=c2 and c3=c4:
            // if c1==c3, then euqal; if c1!=c3, then not equal
            return BoolDomain::FromBool((sa.GetSingleElement() == sb.GetSingleElement()) != cne);
        }
        // a and b do not intersect, then never equal
        // note that symbolic range is always signed
        if (sa.IntersectWith(sb, PreferredRangeType::Signed).IsEmptySet()) {
            return BoolDomain::FromBool(cne);
        }
    }
    return BoolDomain::Top();
}

decltype(BoolDomain::Top()) ComputeEqualityIntBinop(
    const SIntDomain& ia, const SIntDomain& ib, const Ptr<Value>& a, const Ptr<Value>& b, bool cne)
{
    if (ia.IsBottom() || ib.IsBottom()) {
        return BoolDomain::Bottom();
    }
    // single element comparison
    if (ia.NumericBound().IsSingleElement() && ib.NumericBound().IsSingleElement()) {
        auto v1 = ia.NumericBound().GetSingleElement();
        auto v2 = ib.NumericBound().GetSingleElement();
        // if single element identical, and relop is eq, return true;
        // if single element different, and relop is ne, return true;
        // otherwise return false
        return BoolDomain::FromBool((v1 == v2) != cne);
    }
    // Non-intersecting sets can never equal
    if (ia.NumericBound().IntersectWith(ib.NumericBound(), PreferFromBool(ia.IsUnsigned())).IsEmptySet()) {
        // compare neq, intersection is empty: true
        // compare eq, intersection is empty: false
        return BoolDomain::FromBool(cne);
    }
    // a bound to b..[], if range is exactly one value 0, then equal
    if (auto ibs = ia.FindSymbolicBound(b); ibs && ibs->IsSingleElement()) {
        if (ibs->IsSingleElement()) {
            return BoolDomain::FromBool((ibs->GetSingleElement() == 0ull) != cne);
        }
        // ibs does not contain 0, say a is bound to b..[3,7], then never equal
        if (!ibs->Contains(SInt::Zero(ia.Width()))) {
            return BoolDomain::FromBool(cne);
        }
    }
    // likewise, b bound to a..[], if range is exactly one value 0, then equal
    if (auto ias = ib.FindSymbolicBound(a)) {
        if (ias->IsSingleElement()) {
            return BoolDomain::FromBool((ias->GetSingleElement() == 0ull) != cne);
        }
        if (!ias->Contains(SInt::Zero(ia.Width()))) {
            return BoolDomain::FromBool(cne);
        }
    }
    if (auto transitiveRes = CompareTransitiveEquality(ia.SymbolicBounds(), ib.SymbolicBounds(), cne);
        transitiveRes.IsNonTrivial()) {
        return transitiveRes;
    }
    return BoolDomain::Top();
}

// Returns true if both lhs and rhs refer to the same symbol (i.e. this relational operation supersets
// the equality relation)
bool TrueOnSameSymbol(BinaryExprKind op)
{
    switch (op) {
        case BinaryExprKind::EQUAL:
        case BinaryExprKind::LE:
        case BinaryExprKind::GE:
            return true;
        default:
            return false;
    }
}
} // anonymous namespace

decltype(BoolDomain::Top()) ComputeRelIntBinop(CHIRRelIntBinopArgs&& args)
{
    if (args.l == args.r) {
        return BoolDomain::FromBool(TrueOnSameSymbol(args.op));
    }
    auto op{From(args.op)};
    if (op == RelationalOperation::EQ || op == RelationalOperation::NE) {
        return ComputeEqualityIntBinop(args.ld, args.rd, args.l, args.r, args.op == BinaryExprKind::NOTEQUAL);
    }
    return ComputeTotalOrderingIntBinop(args);
}

decltype(BoolDomain::Top()) ComputeEqualityBoolBinop(const BoolDomain& ld, const BoolDomain& rd, BinaryExprKind op)
{
    if (ld.IsBottom() || rd.IsBottom()) {
        return BoolDomain::Bottom();
    }
    if (ld.IsSingleValue() && rd.IsSingleValue()) {
        return BoolDomain::FromBool(
            (ld.GetSingleValue() == rd.GetSingleValue()) != (op == BinaryExprKind::NOTEQUAL));
    }
    return BoolDomain::Top();
}

// Compute a new SIntDomain with this and \p rhs by a binary operator \p op
SIntDomain ComputeArithmeticBinop(CHIRArithmeticBinopArgs&& args)
{
    switch (args.op) {
        case BinaryExprKind::ADD:
            return ComputeAdd(args);
        case BinaryExprKind::SUB:
            return ComputeSub(args);
        case BinaryExprKind::MUL:
            return ComputeMul(args);
        case BinaryExprKind::DIV:
            return ComputeDiv(args);
        case BinaryExprKind::MOD:
            return ComputeMod(args);
        default:
            CJC_ABORT();
            return {ConstantRange{{ToWidth(*args.l->GetType()), 0}}, args.uns};
    }
}

Mem<SIntDomain> SIntDomain::Top(IntWidth width, bool isUnsigned)
{
    return {ConstantRange::Full(width), isUnsigned};
}

Mem<SIntDomain> SIntDomain::Bottom(IntWidth width, bool isUnsigned)
{
    return {ConstantRange::Empty(width), isUnsigned};
}

IntWidth SIntDomain::ToWidth(const Ptr<Type>& type)
{
    switch (type->GetTypeKind()) {
        case Type::TypeKind::TYPE_INT8:
        case Type::TypeKind::TYPE_UINT8:
            return IntWidth::I8;
        case Type::TypeKind::TYPE_INT16:
        case Type::TypeKind::TYPE_UINT16:
            return IntWidth::I16;
        case Type::TypeKind::TYPE_INT32:
        case Type::TypeKind::TYPE_UINT32:
            return IntWidth::I32;
        default:
            return IntWidth::I64;
    }
}

namespace {
struct SIntDomainMergeImpl {
    static Mem<SIntDomain> Intersects(const SIntDomain& a, const SIntDomain& b)
    {
        return MergeRangesForLogicalOperation(a, b, &SIntDomainMergeImpl::ConjunctionInternalMergeFun);
    }

    static Mem<SIntDomain> Unions(const SIntDomain& a, const SIntDomain& b)
    {
        return MergeRangesForLogicalOperation(a, b, &SIntDomainMergeImpl::DisjunctionInternalMergeFun);
    }

private:
    using OptionalMergeFun = ConstantRange (*)(const ConstantRange*, const ConstantRange*);

    static ConstantRange MergeNumericRangesForLogicalOperation(
        const SIntDomain& a, const SIntDomain& b, OptionalMergeFun mergeFunc)
    {
        auto& na = a.NumericBound();
        auto& nb = b.NumericBound();
        return mergeFunc(&na, &nb);
    }

    static std::list<std::pair<PtrSymbol, ConstantRange>>
    MergeSymbolicRangesForLogicalOperationWithNulloptAsMissingBound(
        const SIntDomain& a, const SIntDomain& b, OptionalMergeFun mergeFunc)
    {
        auto ia = a.SymbolicBounds().Begin();
        auto ib = b.SymbolicBounds().Begin();
        auto enda = a.SymbolicBounds().End();
        auto endb = b.SymbolicBounds().End();
        std::list<std::pair<PtrSymbol, ConstantRange>> mergeTmps{};
        while (ia != enda && ib != endb) {
            if (ia->first < ib->first) {
                mergeTmps.emplace_back(ia->first, mergeFunc(&ia->second, nullptr));
                ++ia;
            } else if (ib->first < ia->first) {
                mergeTmps.emplace_back(ib->first, mergeFunc(nullptr, &ib->second));
                ++ib;
            } else {
                mergeTmps.emplace_back(ia->first, mergeFunc(&ia->second, &ib->second));
                ++ia;
                ++ib;
            }
        }
        while (ia != enda) {
            mergeTmps.emplace_back(ia->first, mergeFunc(&ia->second, nullptr));
            ++ia;
        }
        while (ib != endb) {
            mergeTmps.emplace_back(ib->first, mergeFunc(nullptr, &ib->second));
            ++ib;
        }
        return mergeTmps;
    }

    // Merge two ranges into one range using @p:mergeFunc. Missing ranges are represented by null optional value,
    // numeric bound is represented by a null optional symbol. A null return value of the functor @p:mergeFunc indicates
    // that the result cannot be calculated and the corresponding symbolic bound is ignored.
    static Mem<SIntDomain> MergeRangesForLogicalOperation(
        const SIntDomain& a, const SIntDomain& b, OptionalMergeFun mergeFunc)
    {
        ConstantRange n = MergeNumericRangesForLogicalOperation(a, b, mergeFunc);
        if (n.IsEmptySet()) {
            return SIntDomain::Bottom(a.Width(), a.IsUnsigned());
        }
        auto mergeTmps = MergeSymbolicRangesForLogicalOperationWithNulloptAsMissingBound(a, b, mergeFunc);
        SIntDomain::SymbolicBoundsMap m;
        for (auto [symbol, boundOpt] : mergeTmps) {
            if (boundOpt.IsEmptySet()) {
                return SIntDomain::Bottom(a.Width(), a.IsUnsigned());
            }
            if (boundOpt.IsNonTrivial()) {
                m.emplace(symbol, boundOpt);
            }
        }
        return {n, std::move(m), a.IsUnsigned()};
    }

    static ConstantRange ConjunctionInternalMergeFun(const ConstantRange* a, const ConstantRange* b)
    {
        return !a ? *b : !b ? *a : a->IntersectWith(*b);
    }

    static ConstantRange DisjunctionInternalMergeFun(const ConstantRange* a, const ConstantRange* b)
    {
        return !a ? ConstantRange::Full(b->Width()) : !b ? ConstantRange::Full(a->Width()) : a->UnionWith(*b);
    }
};
} // namespace

Mem<SIntDomain> SIntDomain::Intersects(const Mem<SIntDomain>& lhs, const Mem<SIntDomain>& rhs)
{
    return SIntDomainMergeImpl::Intersects(lhs, rhs);
}
Mem<SIntDomain> SIntDomain::Unions(const Mem<SIntDomain>& lhs, const Mem<SIntDomain>& rhs)
{
    return SIntDomainMergeImpl::Unions(lhs, rhs);
}

namespace {
ConstantRange NCSameSignessInner(
    const ConstantRange& src, IntWidth dstSize, bool isUnsigned, Cangjie::OverflowStrategy ov)
{
    auto srcSize = src.Width();
    if (src.IsFullSet()) {
        return ConstantRange::Full(dstSize);
    }
    if (src.IsEmptySet()) {
        return ConstantRange::Empty(dstSize);
    }
    auto higherPart =
        (isUnsigned ? ConstantRange::From(RelationalOperation::GE, SInt::UMinValue(dstSize).ZExt(srcSize), false)
                    : ConstantRange::From(RelationalOperation::GE, SInt::SMinValue(dstSize).SExt(srcSize), true))
            .IntersectWith(src, PreferFromBool(isUnsigned));
    auto unchangedPart =
        (isUnsigned ? ConstantRange::From(RelationalOperation::LE, SInt::UMaxValue(dstSize).ZExt(srcSize), false)
                    : ConstantRange::From(RelationalOperation::LE, SInt::SMaxValue(dstSize).SExt(srcSize), true))
            .IntersectWith(higherPart, PreferFromBool(isUnsigned));
    auto res = unchangedPart.Truncate(dstSize);
    if (src == unchangedPart || ov == OverflowStrategy::THROWING) {
        return res;
    }
    if (ov == OverflowStrategy::WRAPPING) {
        return ConstantRange::Full(dstSize);
    }
    if (src.Difference(higherPart).IsNotEmptySet()) {
        res = res.UnionWith(
            {isUnsigned ? SInt::UMinValue(dstSize) : SInt::SMinValue(dstSize)}, PreferFromBool(isUnsigned));
    }
    if (higherPart.Difference(unchangedPart).IsNotEmptySet()) {
        res = res.UnionWith(
            {isUnsigned ? SInt::UMaxValue(dstSize) : SInt::SMaxValue(dstSize)}, PreferFromBool(isUnsigned));
    }
    return res;
}

ConstantRange NumericConversionWithSameSigness(
    const ConstantRange& src, IntWidth dstSize, bool isUnsigned, Cangjie::OverflowStrategy ov)
{
    auto srcSize = src.Width();
    if (srcSize == dstSize) {
        return src;
    }
    if (srcSize < dstSize) {
        return isUnsigned ? src.ZeroExtend(dstSize) : src.SignExtend(dstSize);
    }
    if (isUnsigned ? src.IsWrappedSet() : src.IsSignWrappedSet()) {
        auto [l, r] = src.SplitWrapping(isUnsigned);
        auto l1 = NCSameSignessInner(l, dstSize, isUnsigned, ov);
        auto r1 = NCSameSignessInner(r, dstSize, isUnsigned, ov);
        return l1.UnionWith(r1, PreferFromBool(isUnsigned));
    }
    return NCSameSignessInner(src, dstSize, isUnsigned, ov);
}

// For \p src {a...b} where b is upper wrapped but \p src not wrapped, raw truncation from srcSize to \p dstSize
// would wrap b into an unexpected small value (i.e. equal to a), and this function handles this special case
// to keep the range mathemetically consistent. For other non-wrapped set, this function truncates \p src as
// normal.
// If \p src is a wrapped set, the behaviour is undefined.
ConstantRange TruncWithUpperWrapped(const ConstantRange& src, bool dstUnsigned, IntWidth dstSize)
{
    CJC_ASSERT(dstUnsigned ? !src.IsWrappedSet() : !src.IsSignWrappedSet());
    CJC_ASSERT(src.Width() > dstSize);
    if (src.Upper() ==
        (dstUnsigned ? SInt::UMaxValue(dstSize).ZExt(src.Width()) : SInt::SMaxValue(dstSize).ZExt(src.Width())) + 1) {
        auto lower = src.Lower().Trunc(dstSize);
        auto upper = (src.Upper() - 1).Trunc(dstSize) + 1;
        if (lower == upper) {
            return ConstantRange::Full(dstSize);
        }
        return {lower, upper};
    }
    return src.Truncate(dstSize);
}

// unsigned to signed, srcSize >= dstSize
ConstantRange NCU2SInner(const ConstantRange& src, IntWidth dstSize, Cangjie::OverflowStrategy ov)
{
    auto dstMax = SInt::SMaxValue(dstSize);
    auto srcSize = src.Width();
    if (srcSize > dstSize) {
        dstMax = dstMax.SExt(srcSize);
    }
    if (src.IsEmptySet()) {
        return ConstantRange::Empty(dstSize);
    }
    auto unchangedRange =
        ConstantRange::From(RelationalOperation::LE, dstMax, false).IntersectWith(src, PreferredRangeType::Unsigned);
    if (unchangedRange == src || ov == OverflowStrategy::THROWING) {
        if (srcSize > dstSize) {
            return TruncWithUpperWrapped(unchangedRange, false, dstSize);
        }
        return unchangedRange;
    }
    if (ov == OverflowStrategy::SATURATING) {
        auto ans = unchangedRange.UnionWith(dstMax, PreferredRangeType::Signed);
        if (srcSize > dstSize) {
            return TruncWithUpperWrapped(ans, false, dstSize);
        }
        return ans;
    }
    if (srcSize == dstSize) {
        return src;
    }
    // too hard to compute truncating range with wrapping, return full
    return ConstantRange::Full(dstSize);
}

ConstantRange NumericConversionU2S(const ConstantRange& src, IntWidth dstSize, Cangjie::OverflowStrategy ov)
{
    auto srcSize = src.Width();
    if (dstSize > srcSize) {
        return src.ZeroExtend(dstSize);
    }
    if (!src.IsWrappedSet()) {
        return NCU2SInner(src, dstSize, ov);
    }
    auto [l, r] = src.SplitWrapping(true);
    return NCU2SInner(l, dstSize, ov).UnionWith(NCU2SInner(r, dstSize, ov), PreferredRangeType::Signed);
}

ConstantRange NumericConversionU2SSameWidth(const ConstantRange& src)
{
    return NumericConversionU2S(src, src.Width(), OverflowStrategy::THROWING);
}

// signed to unsigned, dst size smaller than src size
ConstantRange NCS2UInnerTrunc(const ConstantRange& src, IntWidth dstSize, Cangjie::OverflowStrategy ov)
{
    if (src.IsEmptySet()) {
        return ConstantRange::Empty(dstSize);
    }
    auto srcSize = src.Width();
    auto nonNegRange = src.IntersectWith(
        ConstantRange::From(RelationalOperation::GE, SInt::Zero(srcSize), true), PreferredRangeType::Signed);
    auto negRange = src.Difference(nonNegRange);
    auto unchangedRange = nonNegRange.IntersectWith(
        ConstantRange::From(RelationalOperation::LE, SInt::UMaxValue(dstSize).ZExt(srcSize), true));
    if (unchangedRange == src || ov == OverflowStrategy::THROWING) {
        return TruncWithUpperWrapped(unchangedRange, true, dstSize);
    }
    if (ov == OverflowStrategy::SATURATING) {
        auto res = unchangedRange;
        if (negRange.IsNotEmptySet()) {
            res = res.UnionWith(SInt::Zero(srcSize), PreferredRangeType::Unsigned);
        }
        if (unchangedRange != nonNegRange) {
            res = res.UnionWith(SInt::UMaxValue(dstSize).ZExt(srcSize), PreferredRangeType::Unsigned);
        }
        return TruncWithUpperWrapped(res, true, dstSize);
    }
    return ConstantRange::Full(dstSize);
}

// singed to unsigned, dst size not smaller than src
ConstantRange NCS2UInnerExtend(const ConstantRange& src, IntWidth dstSize, Cangjie::OverflowStrategy ov)
{
    if (src.IsEmptySet()) {
        return ConstantRange::Empty(dstSize);
    }
    auto srcSize = src.Width();
    auto nonNegRange = src.IntersectWith(
        ConstantRange::From(RelationalOperation::GE, SInt::Zero(srcSize), true), PreferredRangeType::Signed);
    auto negRange = src.Difference(nonNegRange);
    if (nonNegRange == src || ov == OverflowStrategy::THROWING) {
        return srcSize == dstSize ? nonNegRange : nonNegRange.ZeroExtend(dstSize);
    }
    if (ov == OverflowStrategy::SATURATING) {
        if (negRange.IsNotEmptySet()) {
            nonNegRange = nonNegRange.UnionWith({SInt::UMinValue(srcSize)});
        }
        return srcSize == dstSize ? nonNegRange : nonNegRange.ZeroExtend(dstSize);
    }
    ConstantRange negOverflowRange{negRange.SMinValue().SExt(dstSize), negRange.SMaxValue().SExt(dstSize) + 1};
    return negOverflowRange.UnionWith(
        srcSize == dstSize ? nonNegRange : nonNegRange.ZeroExtend(dstSize), PreferredRangeType::Unsigned);
}

ConstantRange NumericConversionS2U(const ConstantRange& src, IntWidth dstSize, Cangjie::OverflowStrategy ov)
{
    auto srcSize = src.Width();
    if (dstSize < srcSize) {
        if (src.IsSignWrappedSet()) {
            auto [l, r] = src.SplitWrapping(false);
            return NCS2UInnerTrunc(l, dstSize, ov)
                .UnionWith(NCS2UInnerTrunc(r, dstSize, ov), PreferredRangeType::Unsigned);
        }
        return NCS2UInnerTrunc(src, dstSize, ov);
    }
    return NCS2UInnerExtend(src, dstSize, ov);
}
} // anonymous namespace

ConstantRange NumericConversion(
    const ConstantRange& src, IntWidth dstSize, bool srcUnsigned, bool dstUnsigned, Cangjie::OverflowStrategy ov)
{
    if (srcUnsigned == dstUnsigned) {
        return NumericConversionWithSameSigness(src, dstSize, srcUnsigned, ov);
    }
    if (srcUnsigned) {
        return NumericConversionU2S(src, dstSize, ov);
    }
    return NumericConversionS2U(src, dstSize, ov);
}

ConstantRange ComputeTypeCastNumericBound(
    const SIntDomain& v, IntWidth dstSize, bool dstUnsigned, Cangjie::OverflowStrategy ov)
{
    if (v.IsBottom()) {
        return ConstantRange::Empty(dstSize);
    }
    return NumericConversion(v.NumericBound(), dstSize, v.IsUnsigned(), dstUnsigned, ov);
}
} // namespace Cangjie::CHIR
