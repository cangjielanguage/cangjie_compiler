// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "../FunctionSource.h"

#include <algorithm>
#include <unordered_set>

#include "cangjie/AST/Match.h"

namespace Cangjie::Native::FFI::ArkTS {
using namespace Cangjie::AST;
// Glue input -> output: declaration package "a.b" -> default module "a.b".
// This returns the exact name for a later string-literal AST value; it does not
// rewrite the package AST or synthesize an ArkTS filesystem path.
// The default is the declaration package's exact name. Runtime module
// resolution remains the existing loader's responsibility; no source-path rewrite.
std::string ArkTSDefaultModule(const std::string& package)
{
    return package;
}

// Glue input -> output: resolved semantic Ty (including an alias target) ->
// whitelisted scalar tag, e.g. Int32 -> "Int32", std.math.numeric.BigInt ->
// "BigInt"; invalid/unsupported types -> "". No Type/Ty node is modified.
std::string ArkTSScalarName(Ptr<Ty> ty)
{
    if (!Ty::IsTyCorrect(ty)) {
        return {};
    }
    if (ty->IsString()) {
        return "String";
    }
    if (ty->IsRune()) {
        return "Rune";
    }
    if (ty->IsInteger() || ty->IsFloating() || ty->IsBoolean() || ty->IsUnitOrNothing()) {
        auto name = Ty::ToString(ty);
        static const std::unordered_set<std::string> supported{"Int8", "Int16", "Int32", "UInt8", "UInt16", "UInt32",
            "Float16", "Float32", "Float64", "Int64", "UInt64", "Bool", "Unit", "Nothing"};
        return supported.count(name) ? name : std::string{};
    }
    auto decl = Ty::GetDeclPtrOfTy(ty);
    return decl && decl->fullPackageName == "std.math.numeric" && decl->identifier == "BigInt" ? "BigInt" : "";
}

// Before: fd.annotations contains ordered inherited and explicit @ArkTS layers,
// e.g. [{module:"a", namespace:"n"}, {module:"b"}]. After output options:
// {module:"b", namespace:"n"}; annotations/arguments themselves are unchanged.
// Invalid/duplicate keys or an invalid effective namespace return false; the
// caller must discard that map instead of using partially accumulated options.
// Pseudocode: visit inherited block options before explicit declaration options;
// reject repeated keys within each annotation, including equal values;
// assign each valid key so an explicit declaration value replaces its inherited default.
// Unspecified keys retain their inherited values; sibling declarations have separate maps.
bool ArkTSFunctionOptions(const FuncDecl& fd, std::unordered_map<std::string, std::string>& options)
{
    for (auto& annotation : fd.annotations) {
        if (annotation->kind != AnnotationKind::ARKTS) {
            continue;
        }
        std::unordered_set<std::string> explicitOptions;
        for (auto& arg : annotation->args) {
            auto literal = DynamicCast<LitConstExpr*>(arg->expr.get());
            auto name = arg->name.Val();
            if (!literal || literal->kind != LitConstKind::STRING ||
                (name != "module" && name != "namespace" && name != "foreignName")) {
                return false;
            }
            if (!explicitOptions.emplace(name).second) {
                return false;
            }
            options[name] = literal->stringValue;
        }
    }
    // Validate the effective namespace after declaration options have replaced defaults.
    const auto ns = options.find("namespace");
    return ns == options.end() || ns->second.empty() ||
        (ns->second.front() != '.' && ns->second.back() != '.' && ns->second.find("..") == std::string::npos);
}

// Before glue input: a checked scalar tag and references to context/argument.
// After result: one JSValue-producing expression string, e.g.
// ("Int32", "ctx", "x") -> "ctx.number(Float64(x)).toJSValue()".
// It emits no declarations and changes no AST. The caller parses/rebinds the
// text; for UInt64 it first emits BigInt(x) into a local and passes that local.
// Generated argument expression pseudocode, using the existing JSContext API:
//   Unit -> ctx.undefined().toJSValue()
//   String/Bool -> ctx.string(arg)/ctx.boolean(arg), then toJSValue()
//   Int64/BigInt -> ctx.bigint(arg).toJSValue()
//   UInt64 -> save BigInt(arg) in a separate local, then ctx.bigint(local).toJSValue()
//   Rune -> ctx.number(Float64(UInt32(arg))).toJSValue()
//   Float64 -> ctx.number(arg).toJSValue()
//   remaining supported numbers -> ctx.number(Float64(arg)).toJSValue()
// The bigint path never passes through JS number; Unit still occupies its
// argument position. Type names are selected from the checked scalar whitelist.
static std::string ArkTSScalarToJS(const std::string& type, const std::string& context, const std::string& argument)
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
    } else if (type == "Int64" || type == "UInt64" || type == "BigInt") {
        method = "bigint";
    } else if (type == "Rune") {
        value = "Float64(UInt32(" + argument + "))";
    } else if (type != "Float64") {
        value = "Float64(" + argument + ")";
    }
    return context + "." + method + "(" + value + ").toJSValue()";
}

// Before glue input: checked CJ result tag and a JSValue local reference.
// After result: return/check/throw statement text, e.g.
// ("Int32", "result") -> "return Int32(result.toNumber())".
// Nothing emits an unconditional throw if the ArkTS call unexpectedly returns;
// Unit accepts null or checks undefined before a bare return. This helper does
// not itself parse the text or attach ReturnExpr nodes to a FuncBody.
// Generated return-body pseudocode (existing JSValue conversion APIs):
//   Nothing -> throw Exception("ArkTS never function returned") if call returns
//   Unit -> if result is not null, require asUndefined(); return
//   String -> return result.asString().toString()
//   Bool -> return result.toBoolean()
//   BigInt -> return result.asBigInt().toBigInt()
//   Int64/UInt64 -> return result.asBigInt().toBigInt().toInt64()/toUInt64()
//   Float64 -> return result.toNumber()
//   Rune -> return Rune(UInt32(result.toNumber()))
//   remaining supported numbers -> return T(result.toNumber())
// Existing BigInt conversions throw on overflow by default. API/conversion
// failures propagate through the entry finally block, which restores the
// previous context; no string parsing or extra conversion runtime is called.
static std::string ArkTSScalarFromJS(const std::string& type, const std::string& value)
{
    if (type == "Nothing") {
        return "    throw Exception(\"ArkTS never function returned\")\n";
    }
    if (type == "Unit") {
        return "    if (!" + value + ".isNull()) {\n        " + value + ".asUndefined()\n    }\n    return\n";
    }
    if (type == "String") {
        return "    return " + value + ".asString().toString()\n";
    }
    if (type == "Bool") {
        return "    return " + value + ".toBoolean()\n";
    }
    if (type == "BigInt") {
        return "    return " + value + ".asBigInt().toBigInt()\n";
    }
    if (type == "Int64" || type == "UInt64") {
        return "    return " + value + ".asBigInt().toBigInt().to" + type + "()\n";
    }
    if (type == "Float64") {
        return "    return " + value + ".toNumber()\n";
    }
    if (type == "Rune") {
        return "    return Rune(UInt32(" + value + ".toNumber()))\n";
    }
    return "    return " + type + "(" + value + ".toNumber())\n";
}

// Before: source parameters may already use generated-looking names, e.g.
// `__cj_arkts_context`. After result: a disjoint prefix such as `__cj_arkts_0_`.
// This selects text for generated locals; it never renames source FuncParam nodes.
// Local-name selection pseudocode:
//   begin with the compiler-owned ASCII prefix
//   if any source parameter begins with that prefix, try a numbered prefix
//   repeat until all generated context/module/namespace/result names are disjoint
// Only compiler-owned characters enter generated syntax; parameter names are
// compared here and their values remain AST references in the generated body.
std::string ArkTSForeignLocalPrefix(const std::vector<Ptr<FuncParam>>& params)
{
    const std::string base = "__cj_arkts_";
    auto prefix = base;
    size_t suffix = 0;
    auto conflicts = [&](Ptr<FuncParam> param) {
        return param->identifier.Val().compare(0, prefix.size(), prefix) == 0;
    };
    while (std::any_of(params.begin(), params.end(), conflicts)) {
        prefix = base + std::to_string(suffix++) + "_";
    }
    return prefix;
}

// Before glue input: checked parameter/result tags, namespace depth and a
// parameter-disjoint prefix. After result: temporary `func __generated() { ... }`
// source holding the body shown below, not a replacement public declaration.
// For an echo(Int32):Int32 binding it creates context/module locals, a converted
// argument, callMethod("__name__", [argument]) and return Int32(result.toNumber()).
// CreateForeignBody extracts the Block; LowerArkTSFunctions attaches it to the
// original signature and binds literals, parameters, imports and return ownership.
// Placeholder strings and parameter references are replaced as AST nodes.
// Neither module names nor source identifiers are interpolated into source.
// Generated CJ pseudocode for a checked synchronous foreign declaration:
//   func __generated() {
//       ctx: JSContext = current_scope()                  // throws if no valid entry
//       receiver = ctx.requireArkModule("<module>").asObject()
//       for each namespace segment, left to right:
//           receiver = receiver.getProperty("<segment>").asObject()
//       for each argument i, in source order:
//           if UInt64: bigint_i = BigInt(argument[i])
//           value_i = convert_to_JS(ctx, bigint_i or argument[i], scalar_type[i])
//       args = [value_0, value_1, ...]
//       result = receiver.callMethod("<foreign name>", args) // preserves this
//       convert_from_JS_and_return(result, checked_result_type)
//   }
// Empty/one/many-argument branches emit the same call contract with cjfmt-stable
// layout. Module/namespace/name/parameter placeholders are rebound as AST nodes
// after parsing; only whitelisted types and parameter-disjoint local names enter syntax.
std::string ArkTSForeignWrapperSource(const std::vector<std::string>& types, const std::string& resultType,
    size_t namespaceDepth, const std::string& prefix)
{
    auto ctx = prefix + "context";
    auto receiver = prefix + "module";
    std::string code = "func __generated() {\n    let " + ctx + ": JSContext = __cj_arkts_current()\n    let " +
        receiver + " = " + ctx + ".requireArkModule(\"__module__\").asObject()\n";
    for (size_t i = 0; i < namespaceDepth; ++i) {
        auto next = prefix + "namespace_" + std::to_string(i);
        code += "    let " + next + " = " + receiver + ".getProperty(\"__namespace_" + std::to_string(i) +
            "__\").asObject()\n";
        receiver = next;
    }
    std::string args;
    std::vector<std::string> converted;
    for (size_t i = 0; i < types.size(); ++i) {
        auto argument = "__argument_" + std::to_string(i);
        auto type = types[i];
        if (type == "UInt64") {
            // Keep the bound constructor outside overload candidates' argument
            // subtrees: speculative checking clears and re-resolves those trees.
            auto bigint = prefix + "bigint_" + std::to_string(i);
            code += "    let " + bigint + " = BigInt(" + argument + ")\n";
            argument = bigint;
            type = "BigInt";
        }
        auto value = prefix + "argument_" + std::to_string(i);
        code += "    let " + value + " = " + ArkTSScalarToJS(type, ctx, argument) + "\n";
        converted.emplace_back(value);
        args += "            " + value + (i + 1 < types.size() ? ",\n" : "\n");
    }
    auto result = prefix + "result";
    if (types.empty()) {
        code += "    let " + result + " = " + receiver + ".callMethod(\"__name__\", [])\n";
    } else if (types.size() == 1) {
        code += "    let " + result + " = " + receiver + ".callMethod(\n        \"__name__\",\n        [" +
            converted.front() + "]\n    )\n";
    } else {
        code += "    let " + result + " = " + receiver + ".callMethod(\n        \"__name__\",\n        [\n" + args +
            "        ]\n    )\n";
    }
    return code + ArkTSScalarFromJS(resultType, result) + "}\n";
}
} // namespace Cangjie::Native::FFI::ArkTS
