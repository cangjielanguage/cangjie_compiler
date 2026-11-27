// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "../FunctionSource.h"
#include "../FunctionSourceInternal.h"

#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie::Native::FFI::ArkTS {
using namespace Cangjie::AST;

// A class with type parameters keeps the generic external declaration form; its members cannot be lowered to fixed
// conversions without per-instantiation code, so it is left untouched.
bool IsArkTSForeignClass(const ClassDecl& cd)
{
    return cd.HasAnno(AnnotationKind::ARKTS) && cd.TestAttr(Attribute::FOREIGN) &&
        !cd.TestAttr(Attribute::ARKTS_CLASS_WRAPPER) && (!cd.generic || cd.generic->typeParameters.empty());
}

bool IsArkTSClassWrapper(const Decl& decl)
{
    return decl.TestAttr(Attribute::ARKTS_CLASS_WRAPPER);
}

// Generated argument expression per checked scalar, using the existing ark_interop.JSContext API; the statically
// typed Cangjie side needs no value checks in this direction. Type names are selected from the checked whitelist:
//   Unit -> ctx.undefined().toJSValue()
//   String/Bool -> ctx.string/boolean(arg).toJSValue()
//   Int64/BigInt -> ctx.bigint(arg).toJSValue(); UInt64 is hoisted by
//   ToJS as `let big = BigInt(arg)` before this table runs
//   Rune -> ctx.number(Float64(UInt32(arg))).toJSValue(); other numbers -> ctx.number(Float64(arg)).toJSValue()
// Example: an Int8 element named __locals_elem_0 becomes
// __locals_context.number(Float64(__locals_elem_0)).toJSValue().
std::string ArkTSScalarToJS(const std::string& type, const std::string& context, const std::string& argument)
{
    if (type == "Unit") {
        return context + ".undefined().toJSValue()";
    }
    auto method = std::string("number");
    auto value = argument;
    if (type == "String") {
        method = "string";
    } else if (type == "Bool") {
        method = "boolean";
    } else if (type == "Int64" || type == "BigInt") {
        method = "bigint";
    } else if (type == "Rune") {
        value = "Float64(UInt32(" + argument + "))";
    } else if (type != "Float64") {
        // Default row of the table: keep it in lockstep with ArkTSScalarWhitelist and ScalarFromJS. A newly checked
        // scalar must add its conversion row here; reaching this branch with an unknown or argument-impossible name
        // is an emitter bug, not a user error.
        CJC_ASSERT(type != "Nothing" && "Nothing never crosses as an argument");
        CJC_ASSERT(ArkTSScalarWhitelist().count(type) != 0 &&
            "checked scalar missing a to-JS conversion row: add it to ArkTSScalarToJS");
        value = "Float64(" + argument + ")";
    }
    return context + "." + method + "(" + value + ").toJSValue()";
}

// A JSValue has no toString member; describe it with the kind predicates the facade does expose, so a conversion
// failure can name the actual value.
std::string ArkTSValueDescription(const std::string& value)
{
    // Numbers are deliberately not read here: the range checks already spell the
    // offending value, and a number-to-number read would look like a conversion
    // the BigInt path must not perform.
    return "(if (" + value + ".isNull()) { \"null\" } else if (" + value + ".isUndefined()) { \"undefined\" } " +
        "else if (" + value + ".isBoolean()) { " + value + ".toBoolean().toString() } else if (" + value +
        ".isString()) { " + value + ".asString().toString() } else { \"object\" })";
}

// Every conversion failure in generated code is thrown through this helper. std.core declares the type spelled by
// ARKTS_CONVERSION_EXCEPTION (FunctionSource.h) with a single-String constructor; the lowering walker pins that name
// to the declaration (LowerFunctions.cpp BindArkTSSupportDeclReference), so a same-named source item cannot capture
// it. ARKTS_CONVERSION_EXCEPTION stays the single compiler-side indirection: never change the call sites.
std::string ArkTSConversionThrow(const std::string& reason, const std::string& indent, const std::string& gotExpr)
{
    // A reference or enum type name reaches the message from the mapped
    // declaration (or from a dependent package's CJO), so escape the literal
    // instead of trusting it to be free of quotes and line breaks.
    std::string argument = "\"" + StringConvertor::RecoverForArkTSSource(reason) + "\"";
    if (!gotExpr.empty()) {
        // Append the observed value so the message carries the actual type or
        // out-of-range value, as the conversion contract requires.
        argument += " + \", got \" + " + gotExpr;
    }
    return indent + "throw " + ARKTS_CONVERSION_EXCEPTION + "(" + argument + ")\n";
}

// ---- Runtime failure contract ----
// A namespace segment or named target that does not resolve, and a target that is not callable,
// throw ForeignRuntimeException carrying the declaration context. The target invocation is translated when the library
// reports an outside error (34300002) or an ArkTS-side throw (the JS error's own code, 0 for a plain Error);
// every other BusinessException is rethrown unchanged so thread/lifecycle identities stay with the caller.
// Module loading failures are not caught: their library codes stay the contract. Every placeholder below is
// emitted as its own string literal so the lowering walker rebinds each occurrence to the real name.

std::string ArkTSLiteral(const std::string& text)
{
    return "\"" + text + "\"";
}

// A user string value must never look like one of the compiler's placeholder literals: the lowering walker would
// rewrite it to the module or member name. Escape the first character so the decoded value stays identical and
// the sentinel prefix never matches.
std::string ArkTSUserStringSpelling(const std::string& value)
{
    std::string escaped = StringConvertor::RecoverForArkTSSource(value);
    if (escaped.rfind(ARKTS_PLACEHOLDER_LITERAL_PREFIX, 0) == 0) {
        escaped = "\\u{5f}" + escaped.substr(1);
    }
    return escaped;
}

// Placeholder literals carry a compiler-owned sentinel prefix so the rebinding walkers can tell them apart from a user
// string value that happens to equal a placeholder name (for example @RawValue["__module__"], which the wrapper emits
// as an ordinary string literal). Only this helper adds the prefix; ArkTSQuotedLiteral and ArkTSLiteral quote
// user-facing text verbatim.
std::string ArkTSPlaceholderLiteralImpl(const std::string& text)
{
    return ArkTSLiteral(std::string(ARKTS_PLACEHOLDER_LITERAL_PREFIX) + text);
}

// "ArkTS foreign <kind> '<name>'" as a generated string expression.
std::string ArkTSFailureContext(const std::string& kind, const std::string& name)
{
    return ArkTSLiteral("ArkTS foreign " + kind + " '") + " + " + name + " + " + ArkTSLiteral("'");
}

// "ArkTS foreign <kind> '<owner>' member '<member>' in module '<module>'" as a generated string expression.
std::string ArkTSMemberFailureContext(const std::string& kind, const std::string& owner,
    const std::string& member)
{
    return ArkTSLiteral("ArkTS foreign " + kind + " '") + " + " + owner + " + " + ArkTSLiteral("' member '") + " + " +
        member + " + " + ArkTSLiteral("' in module '") + " + " + ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_MODULE) +
        " + " + ArkTSLiteral("'");
}

// The locator of a named target: the module specifier, or the global object for the arkts.lang receiver exception.
std::string ArkTSFailureLocator(bool globalReceiver)
{
    if (globalReceiver) {
        return ArkTSLiteral("the global object");
    }
    return ArkTSLiteral("module '") + " + " + ArkTSPlaceholderLiteralImpl(ARKTS_PLACEHOLDER_MODULE) + " + " +
        ArkTSLiteral("'");
}

std::string ArkTSRuntimeThrow(const std::string& messageExpr, const std::string& indent)
{
    return indent + "throw " + ARKTS_RUNTIME_EXCEPTION + "(" + messageExpr + ")\n";
}

// One namespace placeholder literal.
std::string ArkTSNamespaceSegment(size_t index)
{
    return ArkTSPlaceholderLiteralImpl(std::string(ARKTS_PLACEHOLDER_NAMESPACE) + std::to_string(index) + "__");
}

// A dotted namespace path as a generated string expression: segments [begin, end) joined by '.', each one its own
// placeholder literal.
std::string ArkTSNamespacePath(size_t begin, size_t end)
{
    std::string path;
    for (size_t i = begin; i < end; ++i) {
        if (i != begin) {
            path += " + " + ArkTSLiteral(".");
        }
        path += " + " + ArkTSNamespaceSegment(i);
    }
    return path;
}

// Namespace segment resolution: one existence check before asObject(). The message names the module (or global object)
// and the namespace path up to and including the failing segment.
std::string ArkTSNamespaceGuard(const std::string& receiver, const std::string& value, const std::string& next,
    size_t index, const std::string& context, bool globalReceiver, const std::string& indent)
{
    const auto segment = ArkTSNamespaceSegment;
    const auto path = ArkTSNamespacePath(0, index + 1);
    const auto message = context + " + " + ArkTSLiteral(": ") + " + " + ArkTSFailureLocator(globalReceiver) +
        " + " + ArkTSLiteral(" has no namespace '") + path + " + " + ArkTSLiteral("'");
    return indent + "let " + value + " = " + receiver + ".getProperty(" + segment(index) + ")\n" + indent + "if (" +
        value + ".isUndefined()) {\n" + ArkTSRuntimeThrow(message, indent + "    ") + indent + "}\n" + indent +
        "let " + next + " = " + value + ".asObject()\n";
}

// Named export resolution with a precise missing-export error. The value stays a JSValue so the caller can check
// callability before use.
std::string ArkTSExportGuard(const std::string& receiver, const std::string& target, const std::string& name,
    const std::string& context, bool globalReceiver, const std::string& indent, size_t namespaceDepth,
    const std::string& error, bool undefinedIsMissing)
{
    auto message = context + " + " + ArkTSLiteral(": ") + " + " + ArkTSFailureLocator(globalReceiver) +
        " + " + ArkTSLiteral(" has no export '") + " + " + name + " + " + ArkTSLiteral("'");
    if (namespaceDepth > 0) {
        // The namespace path narrows the lookup site when the declaration binds an ArkTS namespace.
        message += " + " + ArkTSLiteral(" in namespace '") + ArkTSNamespacePath(0, namespaceDepth) + " + " +
            ArkTSLiteral("'");
    }
    // `error` selects the translated read form: the property access itself may run an ArkTS getter that throws, so
    // the variable getter wraps it with the shared call translation. A caller without an error local (the function
    // and class wrappers, whose target is then called) keeps the plain read.
    std::string read = receiver + ".getProperty(" + name + ")";
    if (!error.empty()) {
        read = ArkTSCallTranslation(read, context, error, indent);
    }
    std::string guard = indent + "let " + target + " = " + read + "\n";
    if (undefinedIsMissing) {
        guard += indent + "if (" + target + ".isUndefined()) {\n" + ArkTSRuntimeThrow(message, indent + "    ") +
            indent + "}\n";
    }
    return guard;
}

// Member resolution on an object or class value with a precise missing-member error.
std::string ArkTSMemberGuard(const std::string& receiver, const std::string& target, const std::string& member,
    const std::string& context, const std::string& indent)
{
    const auto message = context + " + " + ArkTSLiteral(" does not exist");
    return indent + "let " + target + " = " + receiver + ".getProperty(" + member + ")\n" + indent + "if (" + target +
        ".isUndefined()) {\n" + ArkTSRuntimeThrow(message, indent + "    ") + indent + "}\n";
}

// Callability check for a resolved target. `reason` is the message suffix naming the expected shape (a function,
// a constructor or a callable value).
std::string ArkTSCallableGuard(const std::string& target, const std::string& context, const std::string& reason,
    const std::string& indent)
{
    return indent + "if (!" + target + ".isFunction()) {\n" +
        ArkTSRuntimeThrow(context + " + " + ArkTSLiteral(reason), indent + "    ") + indent + "}\n";
}

// Translate a runtime failure: an outside error and an ArkTS-side throw are wrapped; every other BusinessException
// keeps its type, code and message.
std::string ArkTSCallTranslation(const std::string& callExpr, const std::string& context,
    const std::string& error, const std::string& indent)
{
    // The library exception's toString() carries its class name, ArkTS message
    // and ArkTS-side stack, so the translated message keeps all three.
    const auto message = context + " + " + ArkTSLiteral(" failed: ") + " + " + error + ".toString()";
    return "try {\n" + indent + "    " + callExpr + "\n" + indent + "} catch (" + error + ": " +
        ARKTS_BUSINESS_EXCEPTION + ") {\n" + indent + "    if (" + error + ".code != " + ARKTS_LIBRARY_EXTERNAL_ERROR +
        " && " + error + ".code != " + ARKTS_LIBRARY_ARKTS_THROW + ") {\n" + indent + "        throw " + error + "\n" +
        indent + "    }\n" + ArkTSRuntimeThrow(message, indent + "    ") + indent + "}";
}

// Inclusive Float64 bounds for the number->narrow-int rows. Every bound is exactly representable in Float64, so the
// comparisons cannot themselves lose precision. Int8 maps to "-128.0"/"127.0" through UInt32 to "0.0"/"4294967295.0",
// and the emitted guard compares the converted number against the two bound literals in order.
bool ArkTSNarrowBounds(const std::string& type, std::string& lower, std::string& upper)
{
    if (type == "Int8") {
        lower = "-128.0";
        upper = "127.0";
    } else if (type == "Int16") {
        lower = "-32768.0";
        upper = "32767.0";
    } else if (type == "Int32") {
        lower = "-2147483648.0";
        upper = "2147483647.0";
    } else if (type == "UInt8") {
        lower = "0.0";
        upper = "255.0";
    } else if (type == "UInt16") {
        lower = "0.0";
        upper = "65535.0";
    } else if (type == "UInt32") {
        lower = "0.0";
        upper = "4294967295.0";
    } else {
        return false;
    }
    return true;
}

// Inclusive Float64 bounds for the number->narrow-float rows: each bound is the Float64 image of the target type's
// largest finite value, and both decimal literals are exactly representable in Float64, so the comparisons cannot
// themselves lose precision - an out-of-range number must not pass silently as an infinity.
// Float16 -> "-65504.0"/"65504.0"; Float32 -> ±3.4028234663852886e38
bool ArkTSFloatBounds(const std::string& type, std::string& lower, std::string& upper)
{
    if (type == "Float16") {
        lower = "-65504.0";
        upper = "65504.0";
    } else if (type == "Float32") {
        lower = "-3.4028234663852886e38";
        upper = "3.4028234663852886e38";
    } else {
        return false;
    }
    return true;
}
} // namespace Cangjie::Native::FFI::ArkTS
