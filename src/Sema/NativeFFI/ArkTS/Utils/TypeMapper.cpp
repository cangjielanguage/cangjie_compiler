// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Cangjie <-> ArkTS type mapping: classification of
 * semantic types into recursive descriptions and the predicates over those
 * descriptions shared by lowering and the wrapper generator.
 */

#include "TypeMapper.h"

#include "../FunctionSource.h"

#include <string>
#include <unordered_set>
#include <vector>
#include "cangjie/AST/ASTCasting.h"
#include "cangjie/AST/Types.h"

namespace Cangjie::Native::FFI::ArkTS {
namespace {
// A tuple that maps to the ArkTS tuple shape needs at least two elements.
constexpr size_t MIN_TUPLE_ARITY = 2;
// ArkTSFunc<F> carries exactly one type argument; anything else is not the
// carrier shape the constraint rules describe (a stale or hand-written form).
constexpr size_t ARKTS_FUNC_TYPE_ARGUMENTS = 1;
} // namespace
using namespace Cangjie::AST;

// Checked scalar names (the checked scalar table): the number candidates
// Int8 through UInt32, Float16 through Float64 and Rune; the bigint
// candidates Int64, UInt64 and BigInt; plus Bool, String, Unit and Nothing.
// String, Rune and BigInt resolve earlier in ArkTSScalarName, but the
// description mapping below accepts all checked names uniformly:
//   "Int32"     -> in the set: SCALAR(Int32) stays generatable
//   "IntNative" -> not in the set: the position is refused, no wrapper
const std::unordered_set<std::string>& ArkTSScalarWhitelist()
{
    static const std::unordered_set<std::string> supported{"Int8", "Int16", "Int32", "UInt8", "UInt16", "UInt32",
        "Float16", "Float32", "Float64", "Rune", "Int64", "UInt64", "BigInt", "Bool", "String", "Unit", "Nothing"};
    return supported;
}

std::string ArkTSJoin(const std::vector<std::string>& parts, const std::string& separator)
{
    std::string joined;
    bool first = true;
    for (auto& part : parts) {
        joined += first ? part : separator + part;
        first = false;
    }
    return joined;
}

// Desc -> Cangjie spelling used in diagnostic and mismatch messages.
// Generated type positions use ArkTSValueEmitter::TypeName, which supplies a
// placeholder spelling for the reference leaf instead of the class identifier.
// ARRAY_COPY(SCALAR(String)) -> "Array<String>"
// TUPLE(SCALAR(UInt16), OPTIONAL(SCALAR(Bool))) -> "(UInt16, Option<Bool>)"
std::string ArkTSTypeDescName(const ArkTSTypeDesc& desc, const ArkTSReferenceSpelling& reference)
{
    switch (desc.kind) {
        case ArkTSTypeDesc::Kind::SCALAR:
            return desc.scalar;
        case ArkTSTypeDesc::Kind::OPTIONAL:
            return "Option<" + ArkTSTypeDescName(desc.elements.front(), reference) + ">";
        case ArkTSTypeDesc::Kind::ARRAY_COPY:
            return "Array<" + ArkTSTypeDescName(desc.elements.front(), reference) + ">";
        case ArkTSTypeDesc::Kind::TUPLE: {
            std::vector<std::string> names;
            names.reserve(desc.elements.size());
            for (auto& element : desc.elements) {
                names.emplace_back(ArkTSTypeDescName(element, reference));
            }
            return "(" + ArkTSJoin(names, ", ") + ")";
        }
        case ArkTSTypeDesc::Kind::REFERENCE:
        case ArkTSTypeDesc::Kind::ENUM:
            if (reference) {
                return reference(desc);
            }
            if (desc.kind == ArkTSTypeDesc::Kind::REFERENCE && desc.reference &&
                IsArkTSPresetArray(*desc.reference) && desc.elements.size() == 1) {
                // The generic preset array spells its type argument in
                // diagnostic text, matching the construction spelling.
                return desc.reference->identifier.Val() + "<" +
                    ArkTSTypeDescName(desc.elements.front(), reference) + ">";
            }
            return desc.reference ? desc.reference->identifier.Val() : std::string{};
        case ArkTSTypeDesc::Kind::ARKTS_FUNC: {
            // "ArkTSFunc<(Int32, Point) -> Option<String>>": the function-type
            // spelling is built from the parameter and result descriptions with
            // the same reference callback, so generated type positions get the
            // placeholder spelling and diagnostics get the class identifier.
            std::vector<std::string> names;
            names.reserve(desc.elements.size());
            for (auto& element : desc.elements) {
                names.emplace_back(ArkTSTypeDescName(element, reference));
            }
            std::string spelling = "(" + ArkTSJoin(names, ", ") + ")";
            // The result is always spelled: a parenthesized type is a tuple
            // shape, not `-> Unit`, and generated type positions must carry
            // the exact function type.
            return std::string(desc.reference ? desc.reference->identifier.Val() : "ArkTSFunc") + "<" + spelling +
                " -> " + ArkTSTypeDescName(desc.FuncResult(), reference) + ">";
        }
        case ArkTSTypeDesc::Kind::UNSUPPORTED:
            break;
    }
    return {};
}

namespace {
// Option<T'>: the inner position must itself be supported; a directly nested
// Option is refused unless the caller allows it (design §3.4).
ArkTSTypeDesc ArkTSClassifyOptionalType(const Ty& ty, ImportManager* imports, bool allowNestedOptional)
{
    ArkTSTypeDesc desc;
    auto inner = ArkTSClassifyType(ty.typeArgs.front().Ty(), imports, allowNestedOptional);
    if (inner.kind != ArkTSTypeDesc::Kind::UNSUPPORTED &&
        (inner.kind != ArkTSTypeDesc::Kind::OPTIONAL || allowNestedOptional)) {
        desc.kind = ArkTSTypeDesc::Kind::OPTIONAL;
        desc.elements.emplace_back(std::move(inner));
    }
    return desc;
}

// Array<T'>: copy semantics. An interface reference element is refused (AR-06
// BR-11); a class reference stays legal.
ArkTSTypeDesc ArkTSClassifyArrayType(const Ty& ty, ImportManager* imports, bool allowNestedOptional)
{
    ArkTSTypeDesc desc;
    auto inner = ArkTSClassifyType(ty.typeArgs.front().Ty(), imports, allowNestedOptional);
    if (inner.kind != ArkTSTypeDesc::Kind::UNSUPPORTED && !ArkTSTypeDescContainsInterfaceReference(inner)) {
        desc.kind = ArkTSTypeDesc::Kind::ARRAY_COPY;
        desc.elements.emplace_back(std::move(inner));
    }
    return desc;
}

// An ArkTS class instance is a reference to the ArkTS object. Only a class
// that went through class preparation carries the wrapper attribute; generic
// presets keep their external declaration shape and stay unsupported.
ArkTSTypeDesc ArkTSClassifyReferenceLeaf(Ptr<Decl> decl, ImportManager* imports)
{
    ArkTSTypeDesc desc;
    desc.kind = ArkTSTypeDesc::Kind::REFERENCE;
    desc.reference = decl;
    if (IsArkTSPreparedInterface(*decl)) {
        desc.referenceImpl = ArkTSInterfaceImpl(*decl, imports);
    }
    return desc;
}

// The prepared generic preset array `ArkTSArray<T>`: a reference leaf whose
// single element description records the written type argument. The element
// conversion itself lives in the prepared generic member bodies (the runtime
// declares the class under `T <: JSInteropType<T>`), so a position only needs
// the argument to map its construction spelling `ArkTSArray<T'>(...)` and its
// diagnostic name. A position without exactly one type argument, an element
// that does not itself map, and an element carrying Nothing (no values exist,
// so no JSInteropType conformance can accept it) or a prepared-interface
// reference (the AR-06 container rule arrays share) stay unsupported.
ArkTSTypeDesc ArkTSClassifyPresetArrayLeaf(Ptr<Decl> decl, const Ty& ty, ImportManager* imports)
{
    ArkTSTypeDesc desc;
    if (ty.typeArgs.size() != 1) {
        return desc;
    }
    auto element = ArkTSClassifyType(ty.typeArgs.front().Ty(), imports);
    if (element.kind == ArkTSTypeDesc::Kind::UNSUPPORTED || ArkTSTypeDescContainsNothing(element) ||
        ArkTSTypeDescContainsInterfaceReference(element)) {
        return desc;
    }
    desc.kind = ArkTSTypeDesc::Kind::REFERENCE;
    desc.reference = decl;
    desc.elements.emplace_back(std::move(element));
    return desc;
}

// An @ArkTS foreign enum that went through enumeration preparation carries
// ARKTS_LOWERED; lowering attaches the member value table afterwards. The
// reference field is reused for the enum declaration, mirroring REFERENCE.
ArkTSTypeDesc ArkTSClassifyEnumLeaf(Ptr<Decl> decl)
{
    ArkTSTypeDesc desc;
    desc.kind = ArkTSTypeDesc::Kind::ENUM;
    desc.reference = decl;
    return desc;
}

// ArkTS tuple types carry a fixed arity and Cangjie's TupleTy is exactly the
// base typeArgs list, so a rest element cannot be expressed in a Cangjie
// signature and no tuple-rest branch exists; the tuple-rest rule is applied to
// the ArkTS .d.ts side before any Cangjie type exists.
ArkTSTypeDesc ArkTSClassifyTupleType(const Ty& ty, ImportManager* imports, bool allowNestedOptional)
{
    ArkTSTypeDesc desc;
    desc.kind = ArkTSTypeDesc::Kind::TUPLE;
    for (auto& element : ty.typeArgs) {
        auto inner = ArkTSClassifyType(element.Ty(), imports, allowNestedOptional);
        if (inner.kind == ArkTSTypeDesc::Kind::UNSUPPORTED || ArkTSTypeDescContainsInterfaceReference(inner)) {
            return ArkTSTypeDesc{};
        }
        desc.elements.emplace_back(std::move(inner));
    }
    return desc;
}

// ArkTSFunc<F> carrier decision: `F` must be a function type (design 3.5),
// every parameter and the result must be an ArkTS-compatible position, and
// `F` must not mention an enclosing declaration's type parameter (4.2.1 e3/e4:
// the per-F glue is generated once per concrete F, so a type parameter has
// nothing to instantiate against). The reject field records the first failed
// rule for the dedicated diagnostics; the positive path returns an
// ARKTS_FUNC description whose elements are the parameter descriptions, whose
// funcResult is the result description and whose funcTy is the resolved F.
// A zero-parameter F yields an empty elements list, so callers must not read
// "no elements" as "no parameters checked"; the kind decides.
struct ArkTSFuncCarrierDecision {
    ArkTSTypeDesc desc;
    ArkTSFuncCarrierReject reject{ArkTSFuncCarrierReject::NONE};
};

ArkTSFuncCarrierDecision DecideArkTSFuncCarrier(Ptr<Ty> carrierTy, ImportManager* imports, bool allowNestedOptional)
{
    ArkTSFuncCarrierDecision decision;
    auto& ty = *carrierTy;
    if (ty.typeArgs.size() != ARKTS_FUNC_TYPE_ARGUMENTS) {
        decision.reject = ArkTSFuncCarrierReject::NOT_A_FUNCTION;
        return decision;
    }
    auto funcTy = DynamicCast<FuncTy>(ty.typeArgs.front().Ty());
    if (funcTy == nullptr || funcTy->HasQuestTy()) {
        decision.reject = ArkTSFuncCarrierReject::NOT_A_FUNCTION;
        return decision;
    }
    if (funcTy->HasGeneric()) {
        decision.reject = ArkTSFuncCarrierReject::OUTER_GENERIC;
        return decision;
    }
    std::vector<ArkTSTypeDesc> params;
    for (auto& param : funcTy->paramTys) {
        auto inner = ArkTSClassifyType(param.Ty(), imports, allowNestedOptional);
        if (inner.kind == ArkTSTypeDesc::Kind::UNSUPPORTED) {
            // The failing leaf may itself be a carrier whose own constraint
            // broke: name that rule instead of the generic member message.
            auto nested = ArkTSFuncCarrierRejectReason(param.Ty());
            decision.reject = nested != ArkTSFuncCarrierReject::NONE ? nested
                                                                       : ArkTSFuncCarrierReject::UNSUPPORTED_MEMBER;
            return decision;
        }
        if (ArkTSTypeDescContainsNothing(inner)) {
            decision.reject = ArkTSFuncCarrierReject::UNSUPPORTED_MEMBER;
            return decision;
        }
        params.emplace_back(std::move(inner));
    }
    auto result = ArkTSClassifyType(funcTy->retTy.Ty(), imports, allowNestedOptional);
    if (result.kind == ArkTSTypeDesc::Kind::UNSUPPORTED) {
        auto nested = ArkTSFuncCarrierRejectReason(funcTy->retTy.Ty());
        decision.reject = nested != ArkTSFuncCarrierReject::NONE ? nested
                                                                 : ArkTSFuncCarrierReject::UNSUPPORTED_MEMBER;
        return decision;
    }
    decision.desc.kind = ArkTSTypeDesc::Kind::ARKTS_FUNC;
    decision.desc.reference = Ty::GetDeclPtrOfTy(carrierTy);
    decision.desc.funcTy = ty.typeArgs.front().Ty();
    decision.desc.elements = std::move(params);
    decision.desc.funcResult = std::make_shared<ArkTSTypeDesc>(std::move(result));
    return decision;
}
} // namespace

// Diagnostic routing for a refused ArkTSFunc<F> position: package-pinned
// recognition (same pin as the classification branch below), then the failed
// constraint. Every other type reports NONE and keeps the generic
// unsupported-type or deferral message.
ArkTSFuncCarrierReject ArkTSFuncCarrierRejectReason(Ptr<Ty> ty)
{
    if (!Ty::IsTyCorrect(ty)) {
        return ArkTSFuncCarrierReject::NONE;
    }
    auto decl = Ty::GetDeclPtrOfTy(ty);
    if (decl == nullptr || !IsArkTSPresetFunc(*decl)) {
        return ArkTSFuncCarrierReject::NONE;
    }
    auto decision = DecideArkTSFuncCarrier(ty, nullptr, false);
    if (decision.desc.kind == ArkTSTypeDesc::Kind::ARKTS_FUNC) {
        return ArkTSFuncCarrierReject::NONE;
    }
    return decision.reject;
}

// Classification: scalars resolve through the checked whitelist
// (ArkTSScalarName); std.core Option<T> -> OPTIONAL, std.core Array<T> ->
// ARRAY_COPY (copy semantics), an Option directly nested in another Option
// or an unsupported element refusing the whole position; array and tuple
// elements may be REFERENCE nodes, also under an OPTIONAL element; tuples
// need >= 2 supported elements; the package-pinned prepared generic preset
// array `ArkTSArray<T'>` maps to a REFERENCE leaf carrying its element
// description, and every other ArkTSArray-named class stays unsupported;
// the package-pinned ArkTSFunc<F>
// carrier classifies as ARKTS_FUNC when F satisfies the function-type
// constraints (a bare function type and everything else stay unsupported);
// so is IntNative,
// Map/Set/TypedArray and the remaining composite kinds.
// The Option/Array rows: decided by Ty::IsCoreOptionType()/IsStructArray(),
// no declaration name is matched here; the package-pinned recognition for
// diagnostics lives in ArkTSDeferredExternalTypeName below.
// Example: Option<Array<Int32>> -> OPTIONAL(ARRAY_COPY(SCALAR(Int32))).
ArkTSTypeDesc ArkTSClassifyType(Ptr<Ty> ty, ImportManager* imports, bool allowNestedOptional)
{
    if (!Ty::IsTyCorrect(ty)) {
        return ArkTSTypeDesc{};
    }
    auto scalar = ArkTSScalarName(ty);
    if (!scalar.empty()) {
        ArkTSTypeDesc desc;
        desc.kind = ArkTSTypeDesc::Kind::SCALAR;
        desc.scalar = scalar;
        return desc;
    }
    // The prepared generic preset array maps before the prepared-reference
    // branch: the plain REFERENCE leaf carries no type argument, and the
    // position needs one to spell its construction and diagnostic name.
    auto arrayDecl = Ty::GetDeclPtrOfTy(ty);
    if (arrayDecl && IsArkTSPresetArray(*arrayDecl) && IsArkTSClassWrapper(*arrayDecl)) {
        return ArkTSClassifyPresetArrayLeaf(arrayDecl, *ty, imports);
    }
    if (ty->IsCoreOptionType()) {
        return ArkTSClassifyOptionalType(*ty, imports, allowNestedOptional);
    }
    // IsStructArray() decides the std.core Array struct without a name
    // comparison here; it does not pin the type-argument count, so the
    // single-element requirement stays explicit (a zero- or many-argument
    // Array remains unsupported).
    if (ty->IsStructArray() && ty->typeArgs.size() == 1) {
        return ArkTSClassifyArrayType(*ty, imports, allowNestedOptional);
    }
    auto decl = Ty::GetDeclPtrOfTy(ty);
    // The ArkTSFunc<F> carrier is classified before the prepared-reference
    // branch: the preset keeps a package-pinned rule of its own (the prepared
    // carrier class also carries the wrapper marker, and a REFERENCE leaf
    // would emit object conversions for what is a function handle).
    if (decl != nullptr && IsArkTSPresetFunc(*decl)) {
        return DecideArkTSFuncCarrier(ty, imports, allowNestedOptional).desc;
    }
    if (decl && IsArkTSReferenceDecl(*decl)) {
        return ArkTSClassifyReferenceLeaf(decl, imports);
    }
    if (decl && decl->astKind == ASTKind::ENUM_DECL && decl->TestAttr(Attribute::ARKTS_LOWERED)) {
        return ArkTSClassifyEnumLeaf(decl);
    }
    if (ty->IsTuple() && ty->typeArgs.size() >= MIN_TUPLE_ARITY) {
        return ArkTSClassifyTupleType(*ty, imports, allowNestedOptional);
    }
    return ArkTSTypeDesc{};
}

// Recognition of the external preset types declared by std.interop.arkts
// whose mapping is not available (ArkTSType, and ArkTSString in
// the shapes that never went through wrapper preparation) -
// diagnostic routing ONLY, never a classification input: classification keeps
// returning UNSUPPORTED for every such position; this helper only lets
// LowerFunctions pick the dedicated deferral diagnostic over the generic
// message. A prepared ArkTSString is a generatable REFERENCE leaf and never
// reaches this helper; the prepared generic preset array ArkTSArray<T'> maps
// to a generatable reference leaf for the same reason, so both names left
// this list. The declaring package is pinned the way BigInt is
// pinned to std.math.numeric, so a same-named user or facade class keeps the
// generic rejection instead. The package name is the only recognition key: the
// preset declarations are preinstalled and not built with the application, so
// their members cannot be relied on here. A generic instance resolves to the
// same declaration and reports the bare name; nested positions are searched
// recursively so a preset type inside Array, Option or a tuple still reaches
// this diagnostic.
//   std.interop.arkts.ArkTSType -> "ArkTSType";
//   Array<ArkTSType> of another package -> ""
std::string ArkTSDeferredExternalTypeName(Ptr<Ty> ty)
{
    if (!Ty::IsTyCorrect(ty)) {
        return {};
    }
    auto decl = Ty::GetDeclPtrOfTy(ty);
    if (decl && decl->fullPackageName == "std.interop.arkts" &&
        (decl->identifier == "ArkTSString" || decl->identifier == "ArkTSType")) {
        return decl->identifier.Val();
    }
    for (auto& arg : ty->typeArgs) {
        auto nested = ArkTSDeferredExternalTypeName(arg.Ty());
        if (!nested.empty()) {
            return nested;
        }
    }
    return {};
}

namespace {
bool ArkTSTypeDescReferenceGeneratable(const ArkTSTypeDesc& desc)
{
    if (desc.reference == nullptr) {
        return false;
    }
    // A foreign interface reference also needs its generated implementation
    // class: every from-JS position constructs it.
    if (IsArkTSPreparedInterface(*desc.reference)) {
        return desc.referenceImpl != nullptr;
    }
    // The generic preset array is generatable only when its element position
    // is: the construction spelling carries the element as an explicit type
    // argument, so an unsupported element would leave unspellable syntax.
    if (IsArkTSPresetArray(*desc.reference)) {
        return desc.reference->TestAttr(Attribute::ARKTS_CLASS_WRAPPER) && desc.elements.size() == 1 &&
            ArkTSTypeDescGeneratable(desc.elements.front());
    }
    return desc.reference->TestAttr(Attribute::ARKTS_CLASS_WRAPPER);
}

bool ArkTSTypeDescEnumGeneratable(const ArkTSTypeDesc& desc, bool allowNestedOptional)
{
    // The enum declaration must have gone through preparation and lowering
    // must have attached a non-empty value table; a prepared enum whose
    // declaration was refused (generic, ...) never reaches the table and stays
    // non-generatable. A payload member's associated value must itself be a
    // generatable position.
    if (desc.reference == nullptr || !desc.reference->TestAttr(Attribute::ARKTS_LOWERED) ||
        desc.enumMembers.empty()) {
        return false;
    }
    for (const auto& member : desc.enumMembers) {
        if (member.kind != ArkTSEnumValueKind::PAYLOAD) {
            continue;
        }
        if (member.payload.size() != 1 || !ArkTSTypeDescGeneratable(member.payload.front(), allowNestedOptional)) {
            return false;
        }
    }
    return true;
}
} // namespace

// A description is generatable when every scalar leaf is a checked name, the
// single-element composites carry exactly one element, tuples carry at least
// two, and no Option nests another Option; arrays and tuples may contain
// Option elements. ArkTSClassifyType and the scalar-name adapter already
// produce only generatable descriptions; this guard keeps hand-built ones
// honest too. SCALAR("Int32") -> true; SCALAR("Junk") -> false;
//   OPTIONAL(OPTIONAL(SCALAR(Int32))) -> false; TUPLE(SCALAR(Int8)) -> false
bool ArkTSTypeDescGeneratable(const ArkTSTypeDesc& desc, bool allowNestedOptional)
{
    switch (desc.kind) {
        case ArkTSTypeDesc::Kind::UNSUPPORTED:
            return false;
        case ArkTSTypeDesc::Kind::SCALAR:
            return ArkTSScalarWhitelist().count(desc.scalar) != 0;
        case ArkTSTypeDesc::Kind::OPTIONAL:
            return desc.elements.size() == 1 &&
                (desc.elements.front().kind != ArkTSTypeDesc::Kind::OPTIONAL || allowNestedOptional) &&
                ArkTSTypeDescGeneratable(desc.elements.front(), allowNestedOptional);
        case ArkTSTypeDesc::Kind::ARRAY_COPY:
            return desc.elements.size() == 1 && ArkTSTypeDescGeneratable(desc.elements.front(), allowNestedOptional);
        case ArkTSTypeDesc::Kind::REFERENCE:
            return ArkTSTypeDescReferenceGeneratable(desc);
        case ArkTSTypeDesc::Kind::ENUM:
            return ArkTSTypeDescEnumGeneratable(desc, allowNestedOptional);
        case ArkTSTypeDesc::Kind::ARKTS_FUNC:
            // The classification built the parameter/result descriptions from
            // supported positions; the guard re-checks them (and the carrier
            // declaration) so a hand-built description stays honest.
            if (desc.reference == nullptr || !ArkTSTypeDescGeneratable(desc.FuncResult(), allowNestedOptional)) {
                return false;
            }
            break;
        case ArkTSTypeDesc::Kind::TUPLE:
            if (desc.elements.size() < MIN_TUPLE_ARITY) {
                return false;
            }
            break;
    }
    for (auto& element : desc.elements) {
        if (!ArkTSTypeDescGeneratable(element, allowNestedOptional)) {
            return false;
        }
    }
    return true;
}

// Nothing has no values, so it can never cross the boundary as an
// argument; as a result position it is the never contract handled by the
// wrapper. Parameter SCALAR(Nothing) / OPTIONAL(SCALAR(Nothing)) -> true: no wrapper
//   result SCALAR(Nothing) -> false: the wrapper is generated and ends in
//   the never-contract throw instead of a return
bool ArkTSTypeDescContainsNothing(const ArkTSTypeDesc& desc)
{
    if (desc.kind == ArkTSTypeDesc::Kind::SCALAR) {
        return desc.scalar == "Nothing";
    }
    for (auto& element : desc.elements) {
        if (ArkTSTypeDescContainsNothing(element)) {
            return true;
        }
    }
    for (const auto& member : desc.enumMembers) {
        for (const auto& payload : member.payload) {
            if (ArkTSTypeDescContainsNothing(payload)) {
                return true;
            }
        }
    }
    return false;
}

// A reference or enum leaf is emitted by placeholder, never by its own name,
// and the placeholder is registered in the map supplied to the wrapper
// generator; the generator therefore refuses a description containing one when
// no map is supplied. Scalars and unsupported positions carry no reference,
// OPTIONAL/ARRAY_COPY recurse into their single element, TUPLE into every
// element. REFERENCE(Point) -> true; ENUM(Color) -> true;
//   OPTIONAL(REFERENCE(Point)) -> true;
//   TUPLE(SCALAR(Int32), REFERENCE(Point)) -> true; SCALAR(Int32) -> false
bool ArkTSTypeDescContainsReference(const ArkTSTypeDesc& desc)
{
    if (desc.kind == ArkTSTypeDesc::Kind::REFERENCE || desc.kind == ArkTSTypeDesc::Kind::ENUM) {
        return true;
    }
    // A carrier emits its own parameter and result positions (the generated
    // lambda converts both), so a reference under either makes the whole
    // description placeholder-bound; the carrier itself is spelled by
    // compiler-owned name and needs no slot.
    if (desc.kind == ArkTSTypeDesc::Kind::ARKTS_FUNC) {
        if (ArkTSTypeDescContainsReference(desc.FuncResult())) {
            return true;
        }
    }
    for (auto& element : desc.elements) {
        if (ArkTSTypeDescContainsReference(element)) {
            return true;
        }
    }
    for (const auto& member : desc.enumMembers) {
        for (const auto& payload : member.payload) {
            if (ArkTSTypeDescContainsReference(payload)) {
                return true;
            }
        }
    }
    return false;
}

// Whether any leaf is a prepared foreign-interface reference. Used to keep
// interface references out of Array and tuple positions (AR-06 BR-11); class
// references remain legal there per the AR-04 container contract.
bool ArkTSTypeDescContainsInterfaceReference(const ArkTSTypeDesc& desc)
{
    if (desc.kind == ArkTSTypeDesc::Kind::REFERENCE) {
        return desc.reference != nullptr && IsArkTSPreparedInterface(*desc.reference);
    }
    if (desc.kind == ArkTSTypeDesc::Kind::ARKTS_FUNC && ArkTSTypeDescContainsInterfaceReference(desc.FuncResult())) {
        return true;
    }
    for (auto& element : desc.elements) {
        if (ArkTSTypeDescContainsInterfaceReference(element)) {
            return true;
        }
    }
    for (const auto& member : desc.enumMembers) {
        for (const auto& payload : member.payload) {
            if (ArkTSTypeDescContainsInterfaceReference(payload)) {
                return true;
            }
        }
    }
    return false;
}

// A prepared enum leaf whose declaration is imported carries no value table:
// PrepareEnums skips imported packages, so classification finds no prepared
// value-table entry and the leaf stays non-generatable. The declaration still
// carries ARKTS_LOWERED (the attribute survives the cjo round trip), so it is
// told apart from a same-package refused enum (payload/generic) by IMPORTED.
std::string ArkTSTypeDescImportedEnumName(const ArkTSTypeDesc& desc)
{
    if (desc.kind == ArkTSTypeDesc::Kind::ENUM && desc.enumMembers.empty() && desc.reference &&
        desc.reference->TestAttr(Attribute::IMPORTED)) {
        return desc.reference->identifier.Val();
    }
    if (desc.kind == ArkTSTypeDesc::Kind::ARKTS_FUNC) {
        auto name = ArkTSTypeDescImportedEnumName(desc.FuncResult());
        if (!name.empty()) {
            return name;
        }
    }
    for (auto& element : desc.elements) {
        auto name = ArkTSTypeDescImportedEnumName(element);
        if (!name.empty()) {
            return name;
        }
    }
    for (const auto& member : desc.enumMembers) {
        for (const auto& payload : member.payload) {
            auto name = ArkTSTypeDescImportedEnumName(payload);
            if (!name.empty()) {
                return name;
            }
        }
    }
    return {};
}
} // namespace Cangjie::Native::FFI::ArkTS
