// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "../FunctionSource.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <unordered_set>

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie::Native::FFI::ArkTS {
using namespace Cangjie::AST;

namespace {
// Qualified spellings of the interop types the wrapper writes. They are
// single-sourced here so the generator and its comments cannot drift from the
// package names the import injection binds.
const std::string ARKTS_JS_CONTEXT = std::string(ARKTS_INTEROP_PACKAGE) + ".JSContext";
const std::string ARKTS_JS_VALUE = std::string(ARKTS_INTEROP_PACKAGE) + ".JSValue";
} // namespace
// Canonical value of an omitted 'module' option: the Cangjie package name
// of the declaration, returned verbatim - no ETS source-hierarchy
// mapping, and a single-segment package name is a legal module name. An
// empty return only means an empty package name (a degenerate compilation
// unit), which lowering refuses through the generic wrapper-unavailable
// diagnostic.
// Example: package "entry" (no module option) -> requireArkModule("entry").
std::string ArkTSDefaultModule(const std::string& package)
{
    return package;
}

// Checked scalar names live in Utils/TypeMapper (ArkTSScalarWhitelist),
// shared with the classification and the generatability guard.

// Checked-scalar-name adapter: one semantic Ty in, one whitelist name (or
// the empty refusal) out - the classification input of every SCALAR
// description:
//   TYPE_INT32 / TYPE_RUNE / TYPE_UNIT   -> "Int32" / "Rune" / "Unit"
//   std.core String struct               -> "String"
//   std.math.numeric BigInt              -> "BigInt"
//   BigInt in any other package, IntNative or a null Ty -> "" (refusal)
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
        return ArkTSScalarWhitelist().count(name) ? name : std::string{};
    }
    auto decl = Ty::GetDeclPtrOfTy(ty);
    return decl && decl->fullPackageName == "std.math.numeric" && decl->identifier == "BigInt" ? "BigInt" : "";
}

ArkTSOptionStatus CheckArkTSOptionLayer(const Annotation& annotation)
{
    ArkTSOptionStatus status;
    std::unordered_set<std::string> explicitOptions;
    for (auto& arg : annotation.args) {
        const auto name = arg->name.Val();
        auto literal = DynamicCast<LitConstExpr*>(arg->expr.get());
        if (!literal || literal->kind != LitConstKind::STRING) {
            status.problem = ArkTSOptionProblem::NON_LITERAL;
            status.option = name;
            status.at = arg->expr.get();
            return status;
        }
        if (name != "module" && name != "namespace" && name != "foreignName") {
            status.problem = ArkTSOptionProblem::UNKNOWN_NAME;
            status.option = name;
            status.at = arg->expr.get();
            return status;
        }
        if (!explicitOptions.emplace(name).second) {
            status.problem = ArkTSOptionProblem::DUPLICATE;
            status.option = name;
            status.at = arg->expr.get();
            return status;
        }
    }
    return status;
}

bool ArkTSNamespaceValid(const std::string& ns)
{
    return ns.empty() || (ns.front() != '.' && ns.back() != '.' && ns.find("..") == std::string::npos);
}

bool ArkTSOptionValueEmpty(const std::string& value)
{
    return std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
}

bool ArkTSParamIsBroken(const FuncParam& param)
{
    return param.TestAttr(Attribute::HAS_BROKEN) || param.TestAttr(Attribute::IS_BROKEN);
}

bool ArkTSFuncBodyHasRestParam(const FuncBody& body)
{
    for (const auto& list : body.paramLists) {
        if (list == nullptr) {
            continue;
        }
        for (const auto& param : list->params) {
            if (param == nullptr) {
                continue;
            }
            if (std::any_of(param->annotations.begin(), param->annotations.end(), [](const auto& annotation) {
                    return annotation->kind == AnnotationKind::REST_PARAM;
                })) {
                return true;
            }
        }
    }
    return false;
}

bool ArkTSForeignClassLikeMember(const Decl& member)
{
    return member.outerDecl != nullptr && member.outerDecl->IsClassLikeDecl() &&
        member.outerDecl->TestAttr(Attribute::FOREIGN) && member.outerDecl->HasAnno(AnnotationKind::ARKTS);
}

bool ArkTSFunctionOptions(const FuncDecl& fd, std::unordered_map<std::string, std::string>& options,
    ArkTSOptionStatus* status)
{
    if (status != nullptr) {
        *status = ArkTSOptionStatus{};
    }
    // The namespace literal is the anchor of the ill-formed-namespace
    // diagnostic: it is the value the user must fix, not the whole annotation.
    Ptr<Node> namespaceAt = nullptr;
    for (auto& annotation : fd.annotations) {
        if (annotation->kind != AnnotationKind::ARKTS) {
            continue;
        }
        auto layer = CheckArkTSOptionLayer(*annotation);
        if (layer.problem != ArkTSOptionProblem::NONE) {
            if (status != nullptr) {
                *status = layer;
            }
            return false;
        }
        for (auto& arg : annotation->args) {
            options[arg->name.Val()] = StaticCast<LitConstExpr>(*arg->expr).stringValue;
            if (arg->name.Val() == "namespace") {
                namespaceAt = arg->expr.get();
            }
        }
    }
    // Options arrive as ordered layers: inherited block defaults first, then
    // the declaration's own options. A repeated key within one layer is
    // rejected even when the values are equal; an explicit layer overrides
    // the inherited default. The effective namespace must have no empty
    // leading, trailing or inner segment.
    const auto ns = options.find("namespace");
    if (ns != options.end() && !ArkTSNamespaceValid(ns->second)) {
        if (status != nullptr) {
            status->problem = ArkTSOptionProblem::ILL_FORMED_NAMESPACE;
            status->option = "namespace";
            status->at = namespaceAt;
        }
        return false;
    }
    return true;
}

// Walk the package and validate the declaration rules the function fold does
// not own. A lowered wrapper carries ARKTS_FUNCTION_WRAPPER and is validated
// by ArkTSFunctionOptions in LowerArkTSFunctions, so it is skipped here.
// Every other declaration with an @ArkTS annotation - a class, an interface,
// a variable, a parameter, an entry - is checked layer by layer, and one
// declaration yields at most one option diagnostic at its first illegal
// layer. In addition, every bodyless member of an @ArkTS foreign class-like
// (a function or primary constructor the fold never lowers) must give each
// named parameter a default value; such a member is marked IS_BROKEN and
// yields one sema_arkts_foreign_named_param_without_default per offending
// parameter. A parameter the parser already rejected, and a signature the
// parser already rejected as rest+named, keep that one diagnostic instead.
// Input -> outcome: '@ArkTS[bogus: "x"] public class C {}' -> one
// sema_arkts_option_invalid on that annotation; '@ArkTS[namespace: "a..b"]' ->
// one sema_arkts_namespace_invalid on the namespace literal;
// '@ArkTS[module: "a"]' -> none; an @ArkTS foreign class bodyless
// 'func f(x!: Int32)' -> one sema_arkts_foreign_named_param_without_default on x.
void ArkTSDeclOptionsDiagnose(Package& pkg, DiagnosticEngine& diag)
{
    Walker(&pkg, [&diag](Ptr<Node> node) {
        auto decl = DynamicCast<Decl*>(node);
        if (decl == nullptr || decl->TestAttr(Attribute::ARKTS_FUNCTION_WRAPPER)) {
            return VisitAction::WALK_CHILDREN;
        }
        for (const auto& annotation : decl->annotations) {
            if (annotation->kind != AnnotationKind::ARKTS) {
                continue;
            }
            auto layer = CheckArkTSOptionLayer(*annotation);
            bool valid = layer.problem == ArkTSOptionProblem::NONE;
            std::string emptyName;
            // The namespace literal is the anchor of the ill-formed-namespace
            // diagnostic, so it is kept apart from the annotation-level
            // fallback.
            Ptr<Node> namespaceAt = nullptr;
            std::string namespaceValue;
            if (valid) {
                for (const auto& arg : annotation->args) {
                    auto literal = StaticCast<LitConstExpr>(*arg->expr).stringValue;
                    if (arg->name.Val() == "namespace" && !ArkTSNamespaceValid(literal)) {
                        valid = false;
                        namespaceAt = arg->expr.get();
                        namespaceValue = literal;
                        break;
                    }
                    // An explicitly empty value is a separate mistake from a
                    // malformed namespace or an unknown option: it is fully
                    // spelled and would silently reach the runtime.
                    if (ArkTSOptionValueEmpty(literal)) {
                        emptyName = arg->name.Val();
                        break;
                    }
                }
            }
            if (!valid) {
                // A repeated key has its own message: both the key and the
                // value shape are legal, only the repetition is not. An
                // ill-formed namespace also has its own message.
                if (layer.problem == ArkTSOptionProblem::DUPLICATE) {
                    auto& at = layer.at ? *layer.at : static_cast<Node&>(*annotation);
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_duplicated, at, layer.option);
                } else if (namespaceAt != nullptr) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_namespace_invalid, *namespaceAt, namespaceValue);
                } else {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_invalid, *annotation);
                }
                break;
            }
            if (!emptyName.empty()) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_empty, *annotation, emptyName);
                break;
            }
        }
        // Bodyless members of an @ArkTS foreign class-like carry no marker of
        // their own: FOREIGN is propagated by Sema and the @ArkTS annotation
        // stays on the enclosing declaration, so the owner is consulted (the
        // same shape the parser uses to accept a bodyless init). Their named
        // parameters must have a default value for the same reason as a
        // top-level wrapper: the generated binding cannot tell an omitted
        // named argument from an explicitly passed value. A member has no
        // wrapper of its own, so the declaration is only marked broken.
        FuncBody* memberBody = nullptr;
        if (auto fd = DynamicCast<FuncDecl*>(decl)) {
            memberBody = fd->funcBody.get();
        } else if (auto ctor = DynamicCast<PrimaryCtorDecl*>(decl)) {
            // Defensive coverage: an @ArkTS foreign class never declares a
            // primary constructor today, so this branch is unreachable from
            // source; it stays here so a future parser change cannot silently
            // bypass the rule. The 'init' spelling is a FuncDecl (covered by
            // BodylessForeignClassMemberNamedParameterWithoutDefaultIsRejected).
            memberBody = ctor->funcBody.get();
        }
        if (memberBody != nullptr && memberBody->body == nullptr && ArkTSForeignClassLikeMember(*decl)) {
            // A rest parameter forbids named parameters outright; the parser
            // already reported that mix, so this rule stays silent. The
            // member is still marked broken: the shape is rejected either way.
            const bool restParam = ArkTSFuncBodyHasRestParam(*memberBody);
            for (auto& list : memberBody->paramLists) {
                if (list == nullptr) {
                    continue;
                }
                for (auto& param : list->params) {
                    if (param != nullptr && param->isNamedParam && param->assignment == nullptr) {
                        // A malformed parameter or a rest+named signature was
                        // already rejected; do not restate a different rule on
                        // top of that diagnostic.
                        if (!restParam && !ArkTSParamIsBroken(*param)) {
                            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_named_param_without_default,
                                *param, param->identifier.Val(), decl->identifier.Val());
                        }
                        decl->EnableAttr(Attribute::IS_BROKEN);
                    }
                }
            }
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
}

// Generated argument expression per checked scalar, using the existing
// ark_interop.JSContext API; the statically typed Cangjie side needs no value checks
// in this direction. Type names are selected from the checked whitelist:
//   Unit -> ctx.undefined().toJSValue()
//   String/Bool -> ctx.string/boolean(arg).toJSValue()
//   Int64/BigInt -> ctx.bigint(arg).toJSValue(); UInt64 is hoisted by
//   ToJS as `let big = BigInt(arg)` before this table runs
//   Rune -> ctx.number(Float64(UInt32(arg))).toJSValue(); other numbers -> ctx.number(Float64(arg)).toJSValue()
// Before: Int8 element __locals_elem_0
// After:  __locals_context.number(Float64(__locals_elem_0)).toJSValue()
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
    } else if (type == "Int64" || type == "BigInt") {
        method = "bigint";
    } else if (type == "Rune") {
        value = "Float64(UInt32(" + argument + "))";
    } else if (type != "Float64") {
        // Default row of the table: keep it in lockstep with
        // ArkTSScalarWhitelist and ScalarFromJS. A newly checked scalar must
        // add its conversion row here; reaching this branch with an unknown
        // or argument-impossible name is an emitter bug, not a user error.
        CJC_ASSERT(type != "Nothing" && "Nothing never crosses as an argument");
        CJC_ASSERT(ArkTSScalarWhitelist().count(type) != 0 &&
            "checked scalar missing a to-JS conversion row: add it to ArkTSScalarToJS");
        value = "Float64(" + argument + ")";
    }
    return context + "." + method + "(" + value + ").toJSValue()";
}

// Every conversion failure in generated code is thrown through this helper.
// No upstream provider ships an ExternConversionException yet, so the
// facade arkts.lang hosts the only type of this name (a stand-in,
// utils/arkts/sdk/arkts/src/lang/lang.cj) with a single-String
// constructor; the lowering walker pins this name to that declaration
// (LowerFunctions.cpp BindSupportReferences), so a same-named source item
// cannot capture it. This constant stays the single compiler-side
// indirection: when the real type lands, re-export it from the facade (or
// change only this constant, the pinning package and the throw shape
// below), never the call sites.
static constexpr const char* const ARKTS_CONVERSION_EXCEPTION = "ExternConversionException";

// Option constructors are spelled as qualified members of the std.core
// enum: Some/None are the enum's constructors, not package members, so no
// import lookup can pin a bare Some/None - a same-named source function
// would capture it. `Option.Some(x)` / `Option.None` (also inside match
// patterns) leave only `Option` as a name to pin to std.core, and the
// constructor resolves through the member access.
static constexpr const char* const ARKTS_OPTION = "Option";

// Failure categories: kind checks come first, then value checks, and the
// message states which category fired.
static constexpr const char* const ARKTS_MISMATCH = "ArkTS type mismatch: ";
static constexpr const char* const ARKTS_INVALID = "ArkTS value invalid: ";

// A never-typed position is a contract violation rather than a conversion
// failure, so it keeps the plain violation throw and bypasses the
// conversion exception indirection.
static constexpr const char* const ARKTS_NEVER_EXCEPTION = "Exception";
static constexpr const char* const ARKTS_NEVER_MESSAGE = "ArkTS never function returned";

static std::string ArkTSConversionThrow(const std::string& reason, const std::string& indent)
{
    return indent + "throw " + ARKTS_CONVERSION_EXCEPTION + "(\"" + reason + "\")\n";
}

// ---- Verified ohos.ark_interop array face ----
// Array conversions use only the real library API, never compiler-owned
// helper names: ctx.array(Array<ark_interop.JSValue>) packages converted values,
// JSHeapObject.toJSValue() turns a JSArray into a call argument,
// value.isArray() is the kind predicate, asArray().size the element
// count and getElement(index) one element. std.core Array<T> is a
// fixed-length struct with no append member, so both copy directions
// build their buffer through the size-and-initializer constructor with
// the source length and one read per element; the preinstalled
// JSInteropByte byte-view extend is a different semantics, not used.

// The undefined checks below use the isNull()/isUndefined() predicates of
// the real ohos.ark_interop face, matching the library's own
// Option<T>.fromJSValue path; those predicates run the shared
// lifecycle/thread check of the library.

// Inclusive Float64 bounds for the number->narrow-int rows. Every bound
// is exactly representable in Float64, so the comparisons cannot
// themselves lose precision:
//   Int8 -> "-128.0"/"127.0" ... UInt32 -> "0.0"/"4294967295.0"; the
//   emitted guard reads "if (__locals_num_0 < -128.0 || __locals_num_0 > 127.0)"
static bool ArkTSNarrowBounds(const std::string& type, std::string& lower, std::string& upper)
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

// Inclusive Float64 bounds for the number->narrow-float rows: each bound
// is the Float64 image of the target type's largest finite value, and
// both decimal literals are exactly representable in Float64, so the
// comparisons cannot themselves lose precision - an out-of-range number
// must not pass silently as an infinity:
//   Float16 -> "-65504.0"/"65504.0"; Float32 -> ±3.4028234663852886e38
static bool ArkTSFloatBounds(const std::string& type, std::string& lower, std::string& upper)
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

// ArkTSJoin and ArkTSTypeDescName live in Utils/TypeMapper: the
// description spelling is part of the type-mapping surface and is shared
// with the classification predicates.

// One emitted conversion: statements that must run before the expression
// ("pre", each line already indented for its final position) plus the value
// expression itself. Composite nodes embed their children's pre-statements
// inside the construct that consumes them, one nesting level deeper.
struct ArkTSEmitted {
    std::string pre;
    std::string expr;
};

// Recursive conversion emitter. Every generated local is prefixed with the
// parameter-disjoint prefix chosen by ArkTSForeignLocalPrefix and suffixed
// with a per-role counter, so source identifiers can never collide with
// generated names and each role keeps stable, predictable numbering; user
// values only enter as AST-bound placeholder references.
struct ArkTSValueEmitter {
    std::string prefix;
    std::unordered_map<std::string, size_t> nextLocal{};

    std::string Fresh(const std::string& role)
    {
        return prefix + role + "_" + std::to_string(nextLocal[role]++);
    }

    // To-JS conversion per kind (an ArkTSArray<T'> position never reaches
    // the emitter: classification rejects it as deferred):
    //   scalar -> the checked ctx.number/string/boolean/bigint table above
    //   Option<T'> -> match: Option.Some(v) converts v recursively, Option.None -> ctx.undefined()
    //   Array<T'> -> Array<ark_interop.JSValue>(source.size) { i => ... }, then
    //                 ctx.array(...).toJSValue() packages it
    //   tuple -> convert each element, hoist to locals, ctx.array([...]).toJSValue()
    // Before: Option<Int32> __argument_0
    // After:  "match (__argument_0) { case Option.Some(__locals_some_0) => ... }" with
    //         "case Option.None => __locals_context.undefined().toJSValue()"
    ArkTSEmitted ToJS(const ArkTSTypeDesc& desc, const std::string& source, const std::string& ctx,
        const std::string& indent)
    {
        ArkTSEmitted emitted;
        if (desc.kind == ArkTSTypeDesc::Kind::SCALAR) {
            // Hoist the bound constructor out of the argument subtree of
            // ctx.bigint: speculative overload checking clears and re-resolves
            // that subtree, which would drop the BigInt conversion.
            if (desc.scalar == "UInt64") {
                auto big = Fresh("bigint");
                emitted.pre = indent + "let " + big + " = BigInt(" + source + ")\n";
                emitted.expr = ctx + ".bigint(" + big + ").toJSValue()";
                return emitted;
            }
            emitted.expr = ArkTSScalarToJS(desc.scalar, ctx, source);
            return emitted;
        }
        if (desc.kind == ArkTSTypeDesc::Kind::OPTIONAL) {
            auto bound = Fresh("some");
            auto inner = ToJS(desc.elements.front(), bound, ctx, indent + "        ");
            emitted.expr = "match (" + source + ") {\n" + indent + "    case " + ARKTS_OPTION + ".Some(" + bound +
                ") =>\n" + inner.pre + indent + "        " + inner.expr + "\n" + indent + "    case " + ARKTS_OPTION +
                ".None => " + ctx + ".undefined().toJSValue()\n" + indent + "}";
            return emitted;
        }
        if (desc.kind == ArkTSTypeDesc::Kind::ARRAY_COPY) {
            auto elements = Fresh("elems");
            auto index = Fresh("index");
            auto item = Fresh("elem");
            auto inner = ToJS(desc.elements.front(), item, ctx, indent + "    ");
            emitted.pre = indent + "let " + elements + " = Array<" + ARKTS_JS_VALUE + ">(" + source + ".size, { " +
                index + " =>\n" + indent + "    let " + item + " = " + source + "[" + index + "]\n" + inner.pre +
                indent + "    " + inner.expr + "\n" + indent + "})\n";
            emitted.expr = ctx + ".array(" + elements + ").toJSValue()";
            return emitted;
        }
        if (desc.kind == ArkTSTypeDesc::Kind::TUPLE) {
            std::vector<std::string> converted;
            for (size_t i = 0; i < desc.elements.size(); ++i) {
                auto part = Fresh("part");
                auto inner = ToJS(desc.elements[i], source + "[" + std::to_string(i) + "]", ctx, indent);
                emitted.pre += inner.pre;
                emitted.pre += indent + "let " + part + " = " + inner.expr + "\n";
                converted.emplace_back(std::move(part));
            }
            emitted.expr = ctx + ".array([" + ArkTSJoin(converted, ", ") + "]).toJSValue()";
            return emitted;
        }
        return emitted;
    }

    // (value, typeName) -> 'if (value.isNull()) { throw
    // ExternConversionException("ArkTS type mismatch: null for <typeName>")
    // }': a JS null in a non-optional position is a type mismatch (kind
    // checks come first); JS undefined is rejected through the isUndefined()
    // predicate wherever it is not a legal value.
    std::string NullGuard(const std::string& value, const std::string& typeName, const std::string& indent)
    {
        return indent + "if (" + value + ".isNull()) {\n" +
            ArkTSConversionThrow(std::string(ARKTS_MISMATCH) + "null for " + typeName, indent + "    ") + indent +
            "}\n";
    }

    // (value, predicate, kind, typeName) -> 'if (!value.<predicate>()) {
    // throw ExternConversionException("ArkTS type mismatch: not <kind> for
    // <typeName>") }': a wrong-kind value is a type mismatch, so every
    // from-JS accessor is preceded by the matching kind predicate of the
    // real ohos.ark_interop face (isNumber/isString/isBoolean/isArray/
    // isBigInt); the accessor's own mismatch exception stays behind the
    // guard as a redundant safety net. Unlike the null guard this is
    // never skipped inside a Some branch: a non-null value can still have
    // the wrong kind.
    std::string KindGuard(const std::string& value, const std::string& predicate, const std::string& kind,
        const std::string& typeName, const std::string& indent)
    {
        return indent + "if (!" + value + "." + predicate + "()) {\n" +
            ArkTSConversionThrow(std::string(ARKTS_MISMATCH) + "not " + kind + " for " + typeName, indent + "    ") +
            indent + "}\n";
    }

    // From-JS conversion per kind (nullablePosition: the value is already
    // known non-null, so the null guard is skipped; the kind guard never is):
    //   scalar -> the ScalarFromJS rows below
    //   Option<T'> -> null or undefined is Option.None, otherwise Option.Some(convert)
    //   Array<T'> -> null and isArray guards, the length from asArray().size,
    //             then the per-element copy through getElement into the
    //             size-and-initializer constructor
    //   tuple -> isArray guard plus an exact-arity check, then per-element conversion
    // Before: Option<String> __locals_result
    // After:  "(if (...isNull() || ...isUndefined()) { Option.None } else { Option.Some(<converted>) })"
    ArkTSEmitted FromJS(const ArkTSTypeDesc& desc, const std::string& value, const std::string& indent,
        bool nullablePosition = false)
    {
        ArkTSEmitted emitted;
        if (desc.kind == ArkTSTypeDesc::Kind::SCALAR) {
            return ScalarFromJS(desc.scalar, value, indent, nullablePosition);
        }
        if (desc.kind == ArkTSTypeDesc::Kind::OPTIONAL) {
            // Option<T'> from-JS pseudocode: JS null and JS
            // undefined both map to Option.None, checked through the isNull() and
            // isUndefined() predicates; anything else reaches Option.Some with the
            // element's own kind and value checks inside the branch, where
            // the value is already known non-null.
            auto inner = FromJS(desc.elements.front(), value, indent + "    ", true);
            emitted.expr = "(if (" + value + ".isNull() || " + value + ".isUndefined()) {\n" + indent + "    " +
                ARKTS_OPTION + ".None\n" + indent + "} else {\n" + inner.pre + indent + "    " + ARKTS_OPTION +
                ".Some(" + inner.expr + ")\n" + indent + "})";
            return emitted;
        }
        if (desc.kind == ArkTSTypeDesc::Kind::ARRAY_COPY) {
            auto length = Fresh("length");
            auto copy = Fresh("copy");
            auto index = Fresh("index");
            auto item = Fresh("item");
            auto inner = FromJS(desc.elements.front(), item, indent + "    ");
            if (!nullablePosition) {
                emitted.pre += NullGuard(value, ArkTSTypeDescName(desc), indent);
            }
            emitted.pre += KindGuard(value, "isArray", "an array", ArkTSTypeDescName(desc), indent);
            emitted.pre += indent + "let " + length + " = " + value + ".asArray().size\n";
            emitted.pre += indent + "let " + copy + " = Array<" + ArkTSTypeDescName(desc.elements.front()) +
                ">(" + length + ", { " + index + " =>\n" + indent + "    let " + item + " = " + value +
                ".getElement(" + index + ")\n" + inner.pre + indent + "    " + inner.expr + "\n" + indent +
                "})\n";
            emitted.expr = copy;
            return emitted;
        }
        if (desc.kind == ArkTSTypeDesc::Kind::TUPLE) {
            if (!nullablePosition) {
                emitted.pre += NullGuard(value, ArkTSTypeDescName(desc), indent);
            }
            emitted.pre += KindGuard(value, "isArray", "an array", ArkTSTypeDescName(desc), indent);
            auto length = Fresh("length");
            emitted.pre += indent + "let " + length + " = " + value + ".asArray().size\n";
            emitted.pre += indent + "if (" + length + " != " + std::to_string(desc.elements.size()) + ") {\n" +
                ArkTSConversionThrow(std::string(ARKTS_MISMATCH) + "not a " + std::to_string(desc.elements.size()) +
                    "-element array for " + ArkTSTypeDescName(desc), indent + "    ") +
                indent + "}\n";
            std::vector<std::string> converted;
            for (size_t i = 0; i < desc.elements.size(); ++i) {
                // Read each element exactly once through getElement, then let
                // the recursive conversion guard and convert the local. The
                // part local of an Option element is annotated: its if
                // expression ends in Option.None, whose type argument cannot
                // be inferred in a plain let binding without the annotation.
                auto item = Fresh("item");
                emitted.pre += indent + "let " + item + " = " + value + ".getElement(" +
                    std::to_string(i) + ")\n";
                auto part = Fresh("part");
                auto inner = FromJS(desc.elements[i], item, indent);
                emitted.pre += inner.pre;
                emitted.pre += indent + "let " + part +
                    (desc.elements[i].kind == ArkTSTypeDesc::Kind::OPTIONAL
                            ? ": " + ArkTSTypeDescName(desc.elements[i])
                            : "") +
                    " = " + inner.expr + "\n";
                converted.emplace_back(std::move(part));
            }
            emitted.expr = "(" + ArkTSJoin(converted, ", ") + ")";
            return emitted;
        }
        return emitted;
    }

    // Scalar from-JS rows: the kind guard first, then the value checks -
    // narrow ints: finite, range and integrality; Float16/Float32: range for
    // finite values only (NaN, +/-Infinity and negative zero pass through;
    // fractional in-range values convert at target precision);
    // Float64: the bare toNumber() (NaN and infinities are legal); Rune:
    // plus Unicode-scalar and surrogate checks; Int64/UInt64: bigint
    // bounds. The UInt64 upper bound carries a u64 suffix: an unsuffixed
    // 2^64-1 literal is typed Int64 and rejected; the Int64 minimum stays
    // unsuffixed because the parser folds the sign into the literal.
    // Before: Int32 __locals_result  After: guards, then "Int32(__locals_num_0)"
    ArkTSEmitted ScalarFromJS(const std::string& type, const std::string& value, const std::string& indent,
        bool nullablePosition = false)
    {
        ArkTSEmitted emitted;
        std::string lower;
        std::string upper;
        if (ArkTSNarrowBounds(type, lower, upper)) {
            auto number = Fresh("num");
            if (!nullablePosition) {
                emitted.pre += NullGuard(value, type, indent);
            }
            emitted.pre += KindGuard(value, "isNumber", "a number", type, indent);
            emitted.pre += indent + "let " + number + " = " + value + ".toNumber()\n";
            emitted.pre += indent + "if (" + number + " - " + number + " != 0.0) {\n" +
                ArkTSConversionThrow(std::string(ARKTS_INVALID) + "not a finite number for " + type,
                    indent + "    ") +
                indent + "}\n";
            emitted.pre += indent + "if (" + number + " < " + lower + " || " + number + " > " + upper + ") {\n" +
                ArkTSConversionThrow(std::string(ARKTS_INVALID) + "out of " + type + " range", indent + "    ") +
                indent + "}\n";
            emitted.pre += indent + "if (" + number + " != Float64(Int64(" + number + "))) {\n" +
                ArkTSConversionThrow(std::string(ARKTS_INVALID) + "not an integer for " + type, indent + "    ") +
                indent + "}\n";
            emitted.expr = type + "(" + number + ")";
            return emitted;
        }
        if (type == "Rune") {
            auto number = Fresh("num");
            if (!nullablePosition) {
                emitted.pre += NullGuard(value, type, indent);
            }
            emitted.pre += KindGuard(value, "isNumber", "a number", type, indent);
            emitted.pre += indent + "let " + number + " = " + value + ".toNumber()\n";
            emitted.pre += indent + "if (" + number + " - " + number + " != 0.0) {\n" +
                ArkTSConversionThrow(std::string(ARKTS_INVALID) + "not a finite number for Rune", indent + "    ") +
                indent + "}\n";
            emitted.pre += indent + "if (" + number + " < 0.0 || " + number + " > 1114111.0) {\n" +
                ArkTSConversionThrow(std::string(ARKTS_INVALID) + "not a Unicode scalar value for Rune",
                    indent + "    ") +
                indent + "}\n";
            emitted.pre += indent + "if (" + number + " != Float64(Int64(" + number + "))) {\n" +
                ArkTSConversionThrow(std::string(ARKTS_INVALID) + "not an integer for Rune", indent + "    ") +
                indent + "}\n";
            emitted.pre += indent + "if (" + number + " >= 55296.0 && " + number + " <= 57343.0) {\n" +
                ArkTSConversionThrow(std::string(ARKTS_INVALID) + "surrogate code point for Rune",
                    indent + "    ") +
                indent + "}\n";
            emitted.expr = "Rune(UInt32(" + number + "))";
            return emitted;
        }
        if (type == "Int64" || type == "UInt64" || type == "BigInt") {
            if (!nullablePosition) {
                emitted.pre += NullGuard(value, type, indent);
            }
            emitted.pre += KindGuard(value, "isBigInt", "a bigint", type, indent);
            if (type == "BigInt") {
                emitted.expr = value + ".asBigInt().toBigInt()";
                return emitted;
            }
            auto big = Fresh("big");
            emitted.pre += indent + "let " + big + " = " + value + ".asBigInt().toBigInt()\n";
            if (type == "Int64") {
                emitted.pre += indent + "if (" + big + " < BigInt(Int64(-9223372036854775808))) {\n" +
                    ArkTSConversionThrow(std::string(ARKTS_INVALID) + "below Int64 range", indent + "    ") +
                    indent + "}\n";
                emitted.pre += indent + "if (" + big + " > BigInt(Int64(9223372036854775807))) {\n" +
                    ArkTSConversionThrow(std::string(ARKTS_INVALID) + "above Int64 range", indent + "    ") +
                    indent + "}\n";
                emitted.expr = big + ".toInt64()";
                return emitted;
            }
            emitted.pre += indent + "if (" + big + " < BigInt(Int64(0))) {\n" +
                ArkTSConversionThrow(std::string(ARKTS_INVALID) + "negative BigInt for UInt64", indent + "    ") +
                indent + "}\n";
            emitted.pre += indent + "if (" + big + " > BigInt(UInt64(18446744073709551615u64))) {\n" +
                ArkTSConversionThrow(std::string(ARKTS_INVALID) + "above UInt64 range", indent + "    ") + indent +
                "}\n";
            emitted.expr = big + ".toUInt64()";
            return emitted;
        }
        if (type == "String" || type == "Bool" || type == "Float16" || type == "Float32" || type == "Float64") {
            if (!nullablePosition) {
                emitted.pre += NullGuard(value, type, indent);
            }
            if (type == "String") {
                emitted.pre += KindGuard(value, "isString", "a string", type, indent);
                emitted.expr = value + ".asString().toString()";
            } else if (type == "Bool") {
                emitted.pre += KindGuard(value, "isBoolean", "a boolean", type, indent);
                emitted.expr = value + ".toBoolean()";
            } else if (ArkTSFloatBounds(type, lower, upper)) {
                // Behind the isNumber guard, only a finite value beyond the
                // exact Float64 image of the target's largest finite value
                // is rejected: the narrowing constructor would otherwise
                // turn it into an infinity silently. NaN, +/-Infinity and
                // negative zero are values the target type represents, so
                // they convert as-is; in-range values convert at the target
                // precision and may lose precision (no integrality check).
                emitted.pre += KindGuard(value, "isNumber", "a number", type, indent);
                auto number = Fresh("num");
                emitted.pre += indent + "let " + number + " = " + value + ".toNumber()\n";
                // The finite test is what exempts the specials: "n - n" is
                // zero exactly for finite n, so NaN and +/-Infinity skip the
                // range rejection while a finite overflow still throws.
                emitted.pre += indent + "if ((" + number + " - " + number + ") == 0.0 && (" + number + " < " + lower +
                    " || " + number + " > " + upper + ")) {\n" +
                    ArkTSConversionThrow(std::string(ARKTS_INVALID) + "out of " + type + " range", indent + "    ") +
                    indent + "}\n";
                emitted.expr = type + "(" + number + ")";
            } else {
                // Float64 - the only row left in this branch - is the
                // full-range number carrier (number -> Float64 converts
                // directly with no precision loss), so NaN and infinities
                // are legal number values and toNumber() passes them
                // through; the absence of the finite/range checks here is
                // an intentional difference from the Float16/Float32 and
                // narrow-int rows above.
                emitted.pre += KindGuard(value, "isNumber", "a number", type, indent);
                emitted.expr = value + ".toNumber()";
            }
            return emitted;
        }
        if (type == "Unit") {
            // Unit from-JS pseudocode: JS null and JS undefined both satisfy
            // Unit, checked through the isNull()/isUndefined() predicates;
            // every other value is a kind mismatch.
            emitted.expr = "(if (" + value + ".isNull() || " + value + ".isUndefined()) {\n" + indent +
                "    ()\n" + indent + "} else {\n" +
                ArkTSConversionThrow(std::string(ARKTS_MISMATCH) + "not null or undefined for Unit", indent + "    ") +
                indent + "})";
            return emitted;
        }
        if (type == "Nothing") {
            // A never-typed element can never produce a value; reaching it is
            // the same contract violation as a never-typed result.
            emitted.expr = std::string("throw ") + ARKTS_NEVER_EXCEPTION + "(\"" + ARKTS_NEVER_MESSAGE + "\")";
            return emitted;
        }
        // Default row of the table. Every name in ArkTSScalarWhitelist has a
        // conversion row above, so an empty expression here would emit
        // incomplete code; keep ArkTSScalarWhitelist, this table and
        // ArkTSScalarToJS in lockstep and add the row together with the
        // whitelist entry. std::abort() is unconditional: CJC_ABORT() is
        // empty when NDEBUG is defined without CMAKE_ENABLE_ASSERT, which
        // would turn this drift into a silent empty expression in a release
        // build. A missing conversion row is an internal error, never a
        // recoverable user error.
        std::abort(); // checked scalar missing a from-JS conversion row: add it to ScalarFromJS
        return emitted;
    }
};

// Local-name selection: begin with the compiler-owned ASCII prefix; if any
// source parameter begins with that prefix, try a numbered prefix and
// repeat until all generated context/module/namespace/result names are
// disjoint. Only compiler-owned characters enter generated syntax;
// parameter names are compared here and their values remain AST references
// in the generated body.
// Example: a clash-free parameter list -> "__cj_arkts_"; a list naming
//          __cj_arkts_0_context and __cj_arkts_1_module -> "__cj_arkts_2_"
std::string ArkTSForeignLocalPrefix(const std::vector<Ptr<FuncParam>>& params)
{
    const std::string base = ARKTS_GENERATED_PREFIX;
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

// Emits the wrapper body of a checked synchronous foreign declaration; the
// lowering walker rebinds placeholders and parameter references as AST
// values; any unsupported position returns "". Pseudocode:
//   ctx = cj_arkts_context_v1.__cj_arkts_current(); receiver =
//   ctx.requireArkModule("<module>").asObject() or ctx.global (arkts.lang)
//   receiver = receiver.getProperty("<ns_i>").asObject() ...   // per namespace segment
//   args = [toJS(ctx, argument_i, type_i) ...]; result = receiver.callMethod("<name>", args)
//   return fromJS(result, checked_result_type)   // a Nothing result throws instead
// Rest form (restIndex = k): the last parameter converts element-wise into
// Array<ark_interop.JSValue>(argument_k.size); the call array is sized "k + rest.size"
// with "case _ => rest[i - k]" - each rest item is one individual argument.

std::string ArkTSForeignWrapperSource(const std::vector<ArkTSTypeDesc>& types, const ArkTSTypeDesc& resultType,
    size_t namespaceDepth, const std::string& prefix, size_t restIndex, ArkTSReceiverKind receiver,
    size_t optionalStart)
{
    const bool rest = restIndex != ARKTS_NO_REST;
    if (rest && (restIndex + 1 != types.size() || types[restIndex].kind != ArkTSTypeDesc::Kind::ARRAY_COPY)) {
        return {};
    }
    for (auto& type : types) {
        if (!ArkTSTypeDescGeneratable(type) || ArkTSTypeDescContainsNothing(type)) {
            return {};
        }
    }
    if (!ArkTSTypeDescGeneratable(resultType)) {
        return {};
    }
    auto ctx = prefix + "context";
    auto recv = prefix + std::string("module");
    std::string code = std::string("func ") + ARKTS_WRAPPER_ENTRY + "() {\n    let " + ctx + ": " +
        ARKTS_JS_CONTEXT + " = " + ARKTS_CONTEXT_ABI_PACKAGE + "." + ARKTS_CONTEXT_CURRENT + "()\n";
    // The receiver line is the only difference between the two forms: the
    // GLOBAL form keeps the same local (prefix + "module") so every
    // namespace and call site below is shared, but binds it to the
    // context's global object and never emits requireArkModule or the
    // module placeholder (there is no ArkTS runtime module named
    // arkts.lang to require: the package is the TS-preset facade).
    if (receiver == ArkTSReceiverKind::GLOBAL) {
        code += "    let " + recv + " = " + ctx + ".global\n";
    } else {
        code += "    let " + recv + " = " + ctx + ".requireArkModule(\"" + ARKTS_PLACEHOLDER_MODULE +
            "\").asObject()\n";
    }
    for (size_t i = 0; i < namespaceDepth; ++i) {
        auto next = prefix + "namespace_" + std::to_string(i);
        code += "    let " + next + " = " + recv + ".getProperty(\"" + ARKTS_PLACEHOLDER_NAMESPACE +
            std::to_string(i) + "__\").asObject()\n";
        recv = next;
    }
    ArkTSValueEmitter emitter{prefix};
    std::vector<std::string> args;
    size_t fixed = rest ? restIndex : types.size();
    for (size_t i = 0; i < fixed; ++i) {
        auto emitted = emitter.ToJS(types[i], ARKTS_PLACEHOLDER_ARGUMENT + std::to_string(i), ctx, "    ");
        auto local = emitter.Fresh("arg");
        code += emitted.pre;
        code += "    let " + local + " = " + emitted.expr + "\n";
        args.emplace_back(std::move(local));
    }
    // ArkTS optional parameters are the trailing named `Option<T'> = None`
    // parameters; lowering computes that run from the parameter spelling and
    // passes its first index. The generator-only entry point derives it from
    // the trailing OPTIONAL descriptions, but that is a test entry point
    // only: a type description cannot tell `x!: Option<T'> = None` from a
    // named `x!: Option<T'>` without a default (which Sema rejects before
    // generation), so production must always pass optionalStart from the
    // lowering walker.
    // The wrapper cannot distinguish "the caller omitted the argument" from
    // "the caller passed None", so the Option value decides the forwarded
    // list: every position up to the last Some is forwarded (a None in that
    // range becomes ctx.undefined()) and every optional position after the
    // last Some is omitted. Required positions are always forwarded. With a
    // single optional, None therefore forwards no argument at all. A
    // positional `Option<T'>` (the with-rest form) is a required nullable
    // argument and keeps being forwarded explicitly.
    if (optionalStart == ARKTS_DERIVE_OPTIONAL) {
        optionalStart = types.size();
        while (optionalStart > 0 && types[optionalStart - 1].kind == ArkTSTypeDesc::Kind::OPTIONAL) {
            --optionalStart;
        }
    }
    auto result = prefix + "result";
    if (!rest && optionalStart < types.size()) {
        // Pseudocode (optionalStart = k; optional positions k..n-1):
        //   last = index of the last Some among the optionals, else k - 1
        //   args = Array<ark_interop.JSValue>(last + 1, { index => ... })  // block-lambda
        //   call(receiver, name, args)
        // The call array is a block-lambda constructor, never a trailing
        // closure: the generated body is parsed after the trailing-closure
        // desugar pass, so `Array<ark_interop.JSValue>(n) { ... }` would stay an untyped
        // TRAIL_CLOSE_EXPR.
        // Example: optionals [Some(a), None, None] -> last = k,     args = [.., a]
        // Example: optionals [None, Some(b), None] -> last = k + 1, args = [.., undefined, b]
        // Example: optionals [None, None]          -> last = k - 1, no optional forwarded
        // The last Some is forwarded as its own converted value; a None
        // before it is forwarded as the ctx.undefined() produced by its
        // conversion; every position after the last Some is not passed at
        // all (the ".." prefix stands for the required positions).
        auto last = emitter.Fresh("last");
        auto callIndex = emitter.Fresh("index");
        auto callArgs = emitter.Fresh("args");
        code += "    let " + last + " = ";
        bool firstCondition = true;
        for (size_t i = types.size(); i > optionalStart; --i) {
            const size_t position = i - 1;
            code += firstCondition ? "if (" : "else if (";
            code += std::string(ARKTS_PLACEHOLDER_ARGUMENT) + std::to_string(position) + ".isSome()) {\n        " +
                std::to_string(position) + "\n    } ";
            firstCondition = false;
        }
        code += "else {\n        " + std::to_string(static_cast<long long>(optionalStart) - 1) + "\n    }\n";
        code += "    let " + callArgs + " = Array<" + ARKTS_JS_VALUE + ">(" + last + " + 1, { " + callIndex + " =>\n";
        code += "        match (" + callIndex + ") {\n";
        for (size_t i = 0; i < args.size(); ++i) {
            code += "            case " + std::to_string(i) + " => " + args[i] + "\n";
        }
        // Unreachable fallback: the array length is last + 1 and every
        // position up to last has a case above. It keeps the match exhaustive
        // without an lvalue that could be mistaken for a forwarded value.
        code += "            case _ => " + ctx + ".undefined().toJSValue()\n";
        code += "        }\n    })\n";
        code += "    let " + result + " = " + recv + ".callMethod(\"" + ARKTS_PLACEHOLDER_NAME + "\", " + callArgs +
            ")\n";
    } else if (!rest) {
        if (args.empty()) {
            code += "    let " + result + " = " + recv + ".callMethod(\"" + ARKTS_PLACEHOLDER_NAME + "\", [])\n";
        } else if (args.size() == 1) {
            code += "    let " + result + " = " + recv + ".callMethod(\n        \"" + ARKTS_PLACEHOLDER_NAME +
                "\",\n        [" + args[0] + "]\n    )\n";
        } else {
            code +=
                "    let " + result + " = " + recv + ".callMethod(\n        \"" + ARKTS_PLACEHOLDER_NAME + "\",\n        [\n";
            for (size_t i = 0; i < args.size(); ++i) {
                code += "            " + args[i] + (i + 1 < args.size() ? ",\n" : "\n");
            }
            code += "        ]\n    )\n";
        }
    } else {
        // The rest element buffer: per-item conversion for Array<T'>, the
        // only rest kind left - an ArkTSArray<T'> rest position was rejected
        // by the rest validation above, so this branch always converts
        // element-wise. The buffer and call-array locals come from the
        // shared per-role Fresh counter, like every other generated local.
        auto items = emitter.Fresh("rest");
        auto index = emitter.Fresh("index");
        auto item = emitter.Fresh("elem");
        auto inner = emitter.ToJS(types[restIndex].elements.front(), item, ctx, "        ");
        auto restLength = std::string(ARKTS_PLACEHOLDER_ARGUMENT) + std::to_string(restIndex) + ".size";
        code += "    let " + items + " = Array<" + ARKTS_JS_VALUE + ">(" + restLength + ", { " + index +
            " =>\n        let " + item + " = " + ARKTS_PLACEHOLDER_ARGUMENT + std::to_string(restIndex) + "[" + index +
            "]\n" + inner.pre + "        " + inner.expr + "\n    })\n";
        if (args.empty()) {
            code += "    let " + result + " = " + recv + ".callMethod(\n        \"" + ARKTS_PLACEHOLDER_NAME +
                "\",\n        " + items + "\n    )\n";
        } else {
            // Fixed arguments keep their converted locals; the rest items
            // follow them, selected by the run-time index. The call array is
            // a block-lambda constructor, never a trailing closure: the
            // generated body is parsed after the trailing-closure desugar
            // pass, so `f(n) { ... }` would stay an untyped TRAIL_CLOSE_EXPR.
            auto callIndex = emitter.Fresh("index");
            auto callArgs = emitter.Fresh("args");
            code += "    let " + callArgs + " = Array<" + ARKTS_JS_VALUE + ">(" + std::to_string(args.size()) + " + " +
                restLength + ", { " + callIndex + " =>\n        match (" + callIndex + ") {\n";
            for (size_t i = 0; i < args.size(); ++i) {
                code += "            case " + std::to_string(i) + " => " + args[i] + "\n";
            }
            code += "            case _ => " + items + "[" + callIndex + " - " + std::to_string(args.size()) +
                "]\n        }\n    })\n";
            code += "    let " + result + " = " + recv + ".callMethod(\n        \"" + ARKTS_PLACEHOLDER_NAME +
                "\",\n        " + callArgs + "\n    )\n";
        }
    }
    if (resultType.kind == ArkTSTypeDesc::Kind::SCALAR && resultType.scalar == "Nothing") {
        return code + "    throw " + std::string(ARKTS_NEVER_EXCEPTION) + "(\"" + ARKTS_NEVER_MESSAGE + "\")\n}\n";
    }
    auto emitted = emitter.FromJS(resultType, result, "    ");
    return code + emitted.pre + "    return " + emitted.expr + "\n}\n";
}

// Plain form: forwards to the overload above with ARKTS_NO_REST.
std::string ArkTSForeignWrapperSource(const std::vector<ArkTSTypeDesc>& types, const ArkTSTypeDesc& resultType,
    size_t namespaceDepth, const std::string& prefix, ArkTSReceiverKind receiver, size_t optionalStart)
{
    return ArkTSForeignWrapperSource(types, resultType, namespaceDepth, prefix, ARKTS_NO_REST, receiver, optionalStart);
}

} // namespace Cangjie::Native::FFI::ArkTS
