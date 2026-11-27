// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Internal surface shared by the ArkTS generated-source translation units
 * (Utils/FunctionSource.cpp and Utils/FunctionSourcePreset.cpp). Every
 * declaration here is defined in one of those two files and is not part of
 * the public generator API declared by FunctionSource.h.
 */

#ifndef CANGJIE_PRIVATE_SEMA_ARKTS_FUNCTION_SOURCE_INTERNAL_H
#define CANGJIE_PRIVATE_SEMA_ARKTS_FUNCTION_SOURCE_INTERNAL_H

#include <string>

#include "FunctionSource.h"

namespace Cangjie::Native::FFI::ArkTS {
// Scalar to-JS conversion row of the checked whitelist (the table lives with
// the definition in FunctionSourcePreset.cpp).
std::string ArkTSScalarToJS(const std::string& type, const std::string& context, const std::string& argument);
// Readable description of one JSValue for conversion-failure messages.
std::string ArkTSValueDescription(const std::string& value);
// Conversion-failure throw statement; `gotExpr` optionally appends the
// observed value to the message.
std::string ArkTSConversionThrow(const std::string& reason, const std::string& indent,
    const std::string& gotExpr = {});
// One double-quoted string literal expression.
std::string ArkTSLiteral(const std::string& text);
// User string spelling that can never collide with a placeholder literal.
std::string ArkTSUserStringSpelling(const std::string& value);
// Compiler-owned placeholder literal with its sentinel prefix.
std::string ArkTSPlaceholderLiteralImpl(const std::string& text);
// "ArkTS foreign <kind> '<name>'" as a generated string expression.
std::string ArkTSFailureContext(const std::string& kind, const std::string& name);
// Member failure context naming kind, owner, member and module.
std::string ArkTSMemberFailureContext(const std::string& kind, const std::string& owner,
    const std::string& member);
// Locator phrase of a named target (module specifier or global object).
std::string ArkTSFailureLocator(bool globalReceiver);
// Runtime-failure throw statement of the shared ForeignRuntimeException
// contract.
std::string ArkTSRuntimeThrow(const std::string& messageExpr, const std::string& indent);
// One namespace placeholder literal.
std::string ArkTSNamespaceSegment(size_t index);
// Dotted namespace path as a generated string expression over [begin, end).
std::string ArkTSNamespacePath(size_t begin, size_t end);
// Namespace segment resolution with a precise missing-segment error.
std::string ArkTSNamespaceGuard(const std::string& receiver, const std::string& value, const std::string& next,
    size_t index, const std::string& context, bool globalReceiver, const std::string& indent);
// Named export resolution with a precise missing-export error.
std::string ArkTSExportGuard(const std::string& receiver, const std::string& target, const std::string& name,
    const std::string& context, bool globalReceiver, const std::string& indent, size_t namespaceDepth = 0,
    const std::string& error = {}, bool undefinedIsMissing = true);
// Member resolution on an object or class value with a precise missing-member
// error.
std::string ArkTSMemberGuard(const std::string& receiver, const std::string& target, const std::string& member,
    const std::string& context, const std::string& indent);
// Callability check for a resolved target.
std::string ArkTSCallableGuard(const std::string& target, const std::string& context, const std::string& reason,
    const std::string& indent);
// Runtime-failure translation wrapper around one generated call expression.
std::string ArkTSCallTranslation(const std::string& callExpr, const std::string& context,
    const std::string& error, const std::string& indent);
// Inclusive Float64 bounds of the number->narrow-int conversion rows.
bool ArkTSNarrowBounds(const std::string& type, std::string& lower, std::string& upper);
// Inclusive Float64 bounds of the number->narrow-float conversion rows.
bool ArkTSFloatBounds(const std::string& type, std::string& lower, std::string& upper);
} // namespace Cangjie::Native::FFI::ArkTS

#endif // CANGJIE_PRIVATE_SEMA_ARKTS_FUNCTION_SOURCE_INTERNAL_H
