// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Cangjie <-> ArkTS type mapping: the recursive
 * description of one ArkTS-compatible type position, the semantic-Ty
 * classification that produces it, and the predicates over descriptions
 * shared by lowering and the wrapper generator.
 */

#ifndef CANGJIE_PRIVATE_SEMA_ARKTS_UTILS_TYPE_MAPPER_H
#define CANGJIE_PRIVATE_SEMA_ARKTS_UTILS_TYPE_MAPPER_H

#include <string>
#include <unordered_set>
#include <vector>

#include "cangjie/AST/Node.h"

// Private Sema implementation surface, also used by compiler unit tests.
namespace Cangjie::Native::FFI::ArkTS {
// Recursive description of one ArkTS-compatible type position: either
// unsupported, a checked scalar leaf, or a composite node whose parts are
// again descriptions; classification never invents names outside the
// checked sets.
// Example: Option<Array<Int32>> -> OPTIONAL{ARRAY_COPY{SCALAR "Int32"}};
//          ArkTSArray<Int32> or a nested Option -> UNSUPPORTED with no
//          elements - no wrapper is generated for such positions.
struct ArkTSTypeDesc {
    enum class Kind {
        UNSUPPORTED,  // not usable in an ArkTS interop signature
        SCALAR,       // checked scalar leaf (scalar holds its name)
        OPTIONAL,     // Option<T'>: nullable union on the ArkTS side
        ARRAY_COPY,   // Array<T'>: copy semantics, no shared memory
        TUPLE,        // (T1', ..., Tn') with n >= 2
    };
    Kind kind{Kind::UNSUPPORTED};
    // Checked scalar name for SCALAR nodes (for example "Int8"); empty for
    // every other kind.
    std::string scalar;
    // OPTIONAL and ARRAY_COPY carry exactly one element; TUPLE carries at
    // least two; UNSUPPORTED carries none. A supported composite never
    // contains an UNSUPPORTED or nested OPTIONAL element.
    std::vector<ArkTSTypeDesc> elements;
};

// Checked scalar names (the checked scalar table): the number candidates
// Int8 through UInt32, Float16 through Float64 and Rune; the bigint
// candidates Int64, UInt64 and BigInt; plus Bool, String, Unit and Nothing.
// String, Rune and BigInt resolve earlier in ArkTSScalarName, but the
// classification and the generatability guard below accept all checked
// names uniformly. Shared by FunctionSource (scalar-name adapter,
// generator) and this file, so no copy of the set can drift apart.
const std::unordered_set<std::string>& ArkTSScalarWhitelist();

// Joins parts with a separator; used to spell the element list of tuple
// descriptions and the tuple locals of generated wrappers.
std::string ArkTSJoin(const std::vector<std::string>& parts, const std::string& separator);

// Maps a semantic type to its description. Everything the foreign side
// cannot accept yields kind UNSUPPORTED with no elements: non-whitelisted
// scalars (IntNative and friends), non-core Option/Array decls,
// ArkTSArray-named classes - the stdlib preset std.interop.arkts included,
// because its reference semantics is deferred and such signatures are
// rejected - nested Option, tuples with fewer than two elements, Map/Set/
// TypedArray and the remaining composite kinds.
ArkTSTypeDesc ArkTSClassifyType(Ptr<AST::Ty> ty);

// Recognition of the external ArkTS preset types whose mapping is deferred -
// std.interop.arkts ArkTSArray<T>, ArkTSString and ArkTSType (the ArkTS
// `unknown` parent type). Returns the type name ("ArkTSArray", "ArkTSString",
// "ArkTSType") for a position spelled with one of them, empty otherwise.
// Diagnostic routing ONLY - never a classification input: ArkTSClassifyType
// keeps classifying such positions as UNSUPPORTED; LowerFunctions uses the
// returned name solely to pick the dedicated deferral diagnostic over the
// generic unsupported-type message. Recognition is pinned to the declaring
// package - the way BigInt is pinned to std.math.numeric - so a same-named
// declaration in any other package keeps the generic rejection.
std::string ArkTSDeferredExternalTypeName(Ptr<AST::Ty> ty);

// Cangjie-side spelling of a description, used for generated array locals and
// for mismatch messages; only checked names enter this text.
std::string ArkTSTypeDescName(const ArkTSTypeDesc& desc);

// Generatability and Nothing-argument predicates over a description,
// shared by lowering and the generator: lowering diagnoses before any code
// is generated, the generator keeps the same checks as defense in depth,
// so the two can never drift apart. A Nothing element can never cross the
// boundary as an argument, so any parameter description containing one is
// rejected (a Nothing result stays legal: it is the never contract).
bool ArkTSTypeDescGeneratable(const ArkTSTypeDesc& desc);
bool ArkTSTypeDescContainsNothing(const ArkTSTypeDesc& desc);
} // namespace Cangjie::Native::FFI::ArkTS
#endif
