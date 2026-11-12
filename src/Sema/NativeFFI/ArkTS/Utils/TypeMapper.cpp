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

namespace Cangjie::Native::FFI::ArkTS {
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
            if (reference) {
                return reference(desc);
            }
            return desc.reference ? desc.reference->identifier.Val() : std::string{};
        case ArkTSTypeDesc::Kind::UNSUPPORTED:
            break;
    }
    return {};
}

// Classification: scalars resolve through the checked whitelist
// (ArkTSScalarName); std.core Option<T> -> OPTIONAL, std.core Array<T> ->
// ARRAY_COPY (copy semantics), an Option directly nested in another Option
// or an unsupported element refusing the whole position; array and tuple
// elements may be REFERENCE nodes, also under an OPTIONAL element; tuples
// need >= 2 supported elements; every
// ArkTSArray-named class (the stdlib preset included) is unsupported -
// its reference semantics is deferred; so is everything else (IntNative,
// Map/Set/TypedArray and friends, function types, same-name user decls).
// The Option/Array rows: decided by Ty::IsCoreOptionType()/IsStructArray(),
// no declaration name is matched here; the package-pinned recognition for
// diagnostics lives in ArkTSDeferredExternalTypeName below.
// Example: Option<Array<Int32>> -> OPTIONAL(ARRAY_COPY(SCALAR(Int32))).
ArkTSTypeDesc ArkTSClassifyType(Ptr<Ty> ty)
{
    ArkTSTypeDesc desc;
    if (!Ty::IsTyCorrect(ty)) {
        return desc;
    }
    auto scalar = ArkTSScalarName(ty);
    if (!scalar.empty()) {
        desc.kind = ArkTSTypeDesc::Kind::SCALAR;
        desc.scalar = scalar;
        return desc;
    }
    // Every ArkTSArray-named class - the stdlib preset std.interop.arkts
    // included - is unsupported by this classification: its reference
    // semantics is deferred (the member-level glue it would need is not
    // declarable on the stdlib face). Diagnostic routing is separate:
    // ArkTSDeferredExternalTypeName recognises the preset by its declaring
    // package and reports the deferral message, every other same-named
    // declaration keeps the generic unsupported-type diagnostics.
    if (ty->IsCoreOptionType()) {
        auto inner = ArkTSClassifyType(ty->typeArgs.front());
        if (inner.kind != ArkTSTypeDesc::Kind::UNSUPPORTED && inner.kind != ArkTSTypeDesc::Kind::OPTIONAL) {
            desc.kind = ArkTSTypeDesc::Kind::OPTIONAL;
            desc.elements.emplace_back(std::move(inner));
        }
        return desc;
    }
    // IsStructArray() decides the std.core Array struct without a name
    // comparison here; it does not pin the type-argument count, so the
    // single-element requirement stays explicit (a zero- or many-argument
    // Array remains unsupported).
    if (ty->IsStructArray() && ty->typeArgs.size() == 1) {
        auto inner = ArkTSClassifyType(ty->typeArgs.front());
        if (inner.kind != ArkTSTypeDesc::Kind::UNSUPPORTED) {
            desc.kind = ArkTSTypeDesc::Kind::ARRAY_COPY;
            desc.elements.emplace_back(std::move(inner));
        }
        return desc;
    }
    // An ArkTS class instance is a reference to the ArkTS object. Only a
    // class that went through class preparation carries the wrapper
    // attribute; generic presets keep their external declaration shape and
    // stay unsupported here.
    if (auto decl = Ty::GetDeclPtrOfTy(ty); decl && decl->TestAttr(Attribute::ARKTS_CLASS_WRAPPER)) {
        desc.kind = ArkTSTypeDesc::Kind::REFERENCE;
        desc.reference = decl;
        return desc;
    }
    // ArkTS tuple types carry a fixed arity and Cangjie's TupleTy is
    // exactly the base typeArgs list, so a rest element cannot be
    // expressed in a Cangjie signature and no tuple-rest branch exists;
    // the tuple-rest rule is applied to the ArkTS .d.ts side before any
    // Cangjie type exists.
    if (ty->IsTuple() && ty->typeArgs.size() >= 2) {
        desc.kind = ArkTSTypeDesc::Kind::TUPLE;
        for (auto& element : ty->typeArgs) {
            auto inner = ArkTSClassifyType(element);
            if (inner.kind == ArkTSTypeDesc::Kind::UNSUPPORTED) {
                return ArkTSTypeDesc{};
            }
            desc.elements.emplace_back(std::move(inner));
        }
    }
    return desc;
}

// Recognition of the external preset types declared by std.interop.arkts
// whose mapping is deferred (ArkTSArray<T>, ArkTSString, ArkTSType) -
// diagnostic routing ONLY, never a classification input: classification keeps
// returning UNSUPPORTED for every such position; this helper only lets
// LowerFunctions pick the dedicated deferral diagnostic over the generic
// message. The declaring package is pinned the way BigInt is pinned to
// std.math.numeric, so a same-named user or facade class keeps the generic
// rejection instead. The package name is the only recognition key: the preset
// declarations are preinstalled and not built with the application, so their
// members cannot be relied on here. A generic instance resolves to the same
// declaration and reports the bare name; nested positions are searched
// recursively so a preset type inside Array, Option or a tuple still reaches
// this diagnostic.
//   std.interop.arkts.ArkTSArray<Int32> -> "ArkTSArray";
//   Array<ArkTSArray<Int32>> of another package -> ""
std::string ArkTSDeferredExternalTypeName(Ptr<Ty> ty)
{
    if (!Ty::IsTyCorrect(ty)) {
        return {};
    }
    auto decl = Ty::GetDeclPtrOfTy(ty);
    if (decl && decl->fullPackageName == "std.interop.arkts" &&
        (decl->identifier == "ArkTSArray" || decl->identifier == "ArkTSString" ||
            decl->identifier == "ArkTSType")) {
        return decl->identifier.Val();
    }
    for (auto& arg : ty->typeArgs) {
        auto nested = ArkTSDeferredExternalTypeName(arg);
        if (!nested.empty()) {
            return nested;
        }
    }
    return {};
}

// A description is generatable when every scalar leaf is a checked name, the
// single-element composites carry exactly one element, tuples carry at least
// two, and no Option nests another Option; arrays and tuples may contain
// Option elements. ArkTSClassifyType and the scalar-name adapter already
// produce only generatable descriptions; this guard keeps hand-built ones
// honest too:
//   SCALAR("Int32") -> true; SCALAR("Junk") -> false;
//   OPTIONAL(OPTIONAL(SCALAR(Int32))) -> false; TUPLE(SCALAR(Int8)) -> false
bool ArkTSTypeDescGeneratable(const ArkTSTypeDesc& desc)
{
    switch (desc.kind) {
        case ArkTSTypeDesc::Kind::UNSUPPORTED:
            return false;
        case ArkTSTypeDesc::Kind::SCALAR:
            return ArkTSScalarWhitelist().count(desc.scalar) != 0;
        case ArkTSTypeDesc::Kind::OPTIONAL:
            return desc.elements.size() == 1 && desc.elements.front().kind != ArkTSTypeDesc::Kind::OPTIONAL &&
                ArkTSTypeDescGeneratable(desc.elements.front());
        case ArkTSTypeDesc::Kind::ARRAY_COPY:
            return desc.elements.size() == 1 && ArkTSTypeDescGeneratable(desc.elements.front());
        case ArkTSTypeDesc::Kind::REFERENCE:
            return desc.reference != nullptr && desc.reference->TestAttr(Attribute::ARKTS_CLASS_WRAPPER);
        case ArkTSTypeDesc::Kind::TUPLE:
            if (desc.elements.size() < 2) {
                return false;
            }
            break;
    }
    for (auto& element : desc.elements) {
        if (!ArkTSTypeDescGeneratable(element)) {
            return false;
        }
    }
    return true;
}

// Nothing has no values, so it can never cross the boundary as an
// argument; as a result position it is the never contract handled by the
// wrapper:
//   parameter SCALAR(Nothing) / OPTIONAL(SCALAR(Nothing)) -> true: no wrapper
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
    return false;
}

// A reference leaf is emitted by placeholder, never by class name, and the
// placeholder is registered in the map supplied to the wrapper generator; the
// generator therefore refuses a description containing one when no map is
// supplied. Scalars and unsupported positions carry no reference,
// OPTIONAL/ARRAY_COPY recurse into their single element, TUPLE into every
// element:
//   REFERENCE(Point) -> true; OPTIONAL(REFERENCE(Point)) -> true;
//   TUPLE(SCALAR(Int32), REFERENCE(Point)) -> true; SCALAR(Int32) -> false
bool ArkTSTypeDescContainsReference(const ArkTSTypeDesc& desc)
{
    if (desc.kind == ArkTSTypeDesc::Kind::REFERENCE) {
        return true;
    }
    for (auto& element : desc.elements) {
        if (ArkTSTypeDescContainsReference(element)) {
            return true;
        }
    }
    return false;
}
} // namespace Cangjie::Native::FFI::ArkTS
