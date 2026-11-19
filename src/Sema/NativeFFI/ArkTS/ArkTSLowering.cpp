// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
#include "TypeCheckerImpl.h"
#include "ArkTSInterop.h"
#include "Collector.h"
#include "FunctionSource.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/IntLiteral.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Modules/CjoVersion.h"
#include "cangjie/Parse/Parser.h"
#include "Utils/TypeMapper.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <unordered_map>
#include <unordered_set>

using namespace Cangjie;
using namespace Cangjie::AST;

namespace {
constexpr auto CONTEXT_PACKAGE = "cj_arkts_context_v1";
constexpr auto ARKTS_LANGUAGE_PACKAGE = "arkts.lang";
constexpr auto ARKTS_CONVERSION_EXCEPTION = "ExternConversionException";

// Every conversion failure in generated code is thrown through
// ExternConversionException, the single type of that name hosted by the
// arkts.lang facade; it takes one String describing the failure. The message
// keeps the two documented categories: a wrong kind is a type mismatch, a
// right kind with an illegal value is an invalid value. The wording matches
// the function-wrapper emitter so every conversion point reports alike.
std::string FromJS(const std::string& type, const std::string& value,
    const std::string& number, const std::string& result)
{
    auto mismatch = [&](const std::string& reason) {
        return "throw " + std::string(ARKTS_CONVERSION_EXCEPTION) + "(\"ArkTS type mismatch: " + reason + " for " +
            type + "\")\n";
    };
    auto invalid = [&](const std::string& reason) {
        return "throw " + std::string(ARKTS_CONVERSION_EXCEPTION) + "(\"ArkTS value invalid: " + reason + "\")\n";
    };
    if (type == "Nothing") {
        return "throw Exception(\"ArkTS never function returned\")\n";
    }
    if (type == "Unit") {
        return "if (" + value + ".isNull() || " + value + ".isUndefined()) { return }\n" +
            mismatch("not null or undefined") + "return\n";
    }
    if (type == "String" || type == "Bool") {
        auto predicate = type == "String" ? "isString" : "isBoolean";
        auto conversion = type == "String" ? "asString().toString()" : "toBoolean()";
        return "if (!" + value + "." + predicate + "()) { " +
            mismatch(type == "String" ? "not a string" : "not a boolean") + "}\nreturn " + value + "." + conversion +
            "\n";
    }
    if (type == "BigInt" || type == "Int64" || type == "UInt64") {
        std::string code = "if (!" + value + ".isBigInt()) { " + mismatch("not a bigint") +
            "}\nlet " + number + " = " + value + ".asBigInt().toBigInt()\n";
        if (type == "BigInt") {
            return code + "return " + number + "\n";
        }
        if (type == "Int64") {
            code += "if (" + number + " < __cj_arkts_BigInt.parse(\"-9223372036854775808\")) { " +
                invalid("below Int64 range") + "}\n";
            code += "if (" + number + " > __cj_arkts_BigInt.parse(\"9223372036854775807\")) { " +
                invalid("above Int64 range") + "}\n";
        } else {
            code += "if (" + number + " < __cj_arkts_BigInt.parse(\"0\")) { " +
                invalid("negative BigInt for UInt64") + "}\n";
            code += "if (" + number + " > __cj_arkts_BigInt.parse(\"18446744073709551615\")) { " +
                invalid("above UInt64 range") + "}\n";
        }
        return code + "return " + type + ".parse(" + number + ".toString())\n";
    }
    std::string code = "if (!" + value + ".isNumber()) { " + mismatch("not a number") +
        "}\nlet " + number + " = " + value + ".toNumber()\n";
    if (type == "Float64") {
        return code + "return " + number + "\n";
    }
    if (type == "Float16" || type == "Float32") {
        // Match the function-wrapper path's ArkTSFloatBounds: range-check finite
        // values so Cangjie's narrowing constructor cannot silently saturate an
        // out-of-range finite value (Float16(65510.0) -> 65504.0) instead of
        // throwing; NaN/±Infinity pass through unchanged.
        std::string lower = "-65504.0";
        std::string upper = "65504.0";
        if (type == "Float32") {
            lower = "-3.4028234663852886e38";
            upper = "3.4028234663852886e38";
        }
        return code + "if ((" + number + " - " + number + ") == 0.0 && (" + number + " < " + lower + " || " + number +
            " > " + upper + ")) { " + invalid("out of " + type + " range") + "}\nlet " + result + " = " + type + "(" +
            number + ")\nreturn " + result + "\n";
    }
    code += "if (" + number + " != " + number + " || " + number + ".isInf()) { " +
        invalid("not a finite number for " + type) + "}\n";
    static const std::unordered_map<std::string, std::pair<std::string, std::string>> bounds{
        {"Int8", {"-128.0", "127.0"}}, {"Int16", {"-32768.0", "32767.0"}},
        {"Int32", {"-2147483648.0", "2147483647.0"}}, {"UInt8", {"0.0", "255.0"}},
        {"UInt16", {"0.0", "65535.0"}}, {"UInt32", {"0.0", "4294967295.0"}},
        {"Rune", {"0.0", "1114111.0"}}};
    // `type` is always one of the narrow integer kinds, all present in the
    // bounds table; use find rather than at so a table drift cannot throw
    // std::out_of_range and crash the compiler (the function path asserts on
    // the same drift).
    auto range = bounds.find(type);
    CJC_ASSERT_WITH_MSG(range != bounds.end(), "checked scalar missing bounds row");
    code += "if (" + number + " < " + range->second.first + " || " + number + " > " + range->second.second + ") { " +
        invalid(type == "Rune" ? "not a Unicode scalar value for Rune" : "out of " + type + " range") + "}\n";
    code += "if (" + number + " != Float64(Int64(" + number + "))) { " + invalid("not an integer for " + type) + "}\n";
    if (type == "Rune") {
        return code + "if (" + number + " >= 55296.0 && " + number + " <= 57343.0) { " +
            invalid("surrogate code point for Rune") + "}\nreturn Rune(UInt32(" + number + "))\n";
    }
    return code + "return " + type + "(" + number + ")\n";
}

std::string ScalarName(Ptr<Ty> ty)
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
        // Reuse the ArkTS classification's shared scalar whitelist
        // (ArkTSScalarWhitelist) instead of maintaining a third copy that could
        // drift from Utils/TypeMapper.
        return Native::FFI::ArkTS::ArkTSScalarWhitelist().count(name) ? name : std::string{};
    }
    auto decl = Ty::GetDeclPtrOfTy(ty);
    if (decl && decl->fullPackageName == "std.math.numeric" && decl->identifier == "BigInt") {
        return "BigInt";
    }
    return {};
}

std::string OptionValue(const Decl& decl, const std::string& name, const std::string& fallback)
{
    for (auto& anno : decl.annotations) {
        if (anno->kind != AnnotationKind::ARKTS) {
            continue;
        }
        for (auto& arg : anno->args) {
            if (arg->name == name) {
                auto literal = DynamicCast<LitConstExpr*>(arg->expr.get());
                return literal ? literal->stringValue : fallback;
            }
        }
    }
    return fallback;
}

// Exact integer value of one numeric enum member. ArkTS maps the value through
// Float64, but the auto-increment chain must stay exact, so the previous code's
// double (strtod) was not acceptable: strtod only prefix-parses non-decimal
// spellings (0b1010 -> 0, 0o17 -> 0, 1_000 -> 1) and loses integrality beyond
// 2^53, which made the next omitted member silently take the wrong value.
struct ArkTSEnumInteger {
    uint64_t magnitude{0};
    bool negative{false};

    // The canonical decimal spelling emitted into generated code. An unsigned
    // value at or above 2^63 does not fit the unsuffixed Int64 literal type
    // (which the generated Float64(<value>) would otherwise trip over), so it
    // is spelled as a float literal; 2^63 is exactly representable as Float64.
    std::string DecimalSpelling() const
    {
        // A magnitude beyond INT64_MAX does not fit the unsuffixed Int64 literal
        // type, so it is spelled as a float literal on either side of zero; the
        // spelling stays exact for the generated Float64 conversion.
        if (magnitude > static_cast<uint64_t>(INT64_MAX)) {
            return (negative ? "-" : "") + std::to_string(magnitude) + ".0";
        }
        if (negative) {
            return "-" + std::to_string(magnitude);
        }
        return std::to_string(magnitude);
    }

    // Value + 1 for the omitted-member chain. Returns false on 64-bit overflow.
    bool Increment()
    {
        if (negative) {
            if (magnitude == 1) {
                magnitude = 0;
                negative = false;
                return true;
            }
            --magnitude;
            return true;
        }
        if (magnitude == UINT64_MAX) {
            return false;
        }
        ++magnitude;
        return true;
    }
};

// Parse a Cangjie integer literal spelling into an exact value. The compiler's
// canonical literal parser handles 0b/0o/0x prefixes, '_' separators, an
// optional sign, a legal type suffix and the 64-bit range. A negative spelling
// uses the signed literal type; a non-negative one may reach UINT64_MAX.
bool ParseArkTSEnumInteger(const std::string& spelling, ArkTSEnumInteger& out)
{
    IntLiteral literal;
    const bool negative = !spelling.empty() && spelling.front() == '-';
    literal.InitIntLiteral(spelling, negative ? TypeKind::TYPE_IDEAL_INT : TypeKind::TYPE_UINT64);
    if (literal.IsOutOfRange()) {
        return false;
    }
    auto text = literal.GetValue();
    out.negative = literal.IsNegativeNum();
    if (out.negative) {
        text.erase(0, 1);
    }
    out.magnitude = std::stoull(text);
    return true;
}

// Prepared @RawValue mapping for one constructor of a lowered foreign enum.
struct ArkTSEnumMapping {
    const Decl* ctor;
    Native::FFI::ArkTS::ArkTSEnumValueKind kind;
    std::string value;
    // Associated-value position of a PAYLOAD member; a source type node in the
    // declaring package, or the resolved type of an imported constructor.
    // Both are null for literal members.
    Ptr<Type> payloadType;
    Ptr<Ty> payloadTy;
};

// State shared across the ArkTS prepare/lower passes. Private to this file: the
// checker reaches it only through the entry points declared in ArkTSInterop.h.
struct ArkTSLoweringState {
    std::unordered_map<const EnumDecl*, std::vector<ArkTSEnumMapping>> enums;
    // Enums whose member tables are being attached; a recursive payload leaf
    // stops here instead of rebuilding the same table forever.
    std::unordered_set<const EnumDecl*> attaching;

    void Clear()
    {
        enums.clear();
        attaching.clear();
    }
};

ArkTSLoweringState g_arktsState;
}

namespace Cangjie::Native::FFI::ArkTS {
// Internal prepare helpers, defined later in this file.
void PrepareVariables(AST::Package& pkg, DiagnosticEngine& diag);
void PrepareEnums(AST::Package& pkg, DiagnosticEngine& diag);
void PrepareEnum(AST::EnumDecl& decl, DiagnosticEngine& diag);
}

bool TypeChecker::TypeCheckerImpl::SpliceArkTSGeneratedBody(ASTContext& ctx, AST::FuncDecl& fd,
    const std::string& code, const std::unordered_map<std::string, std::string>& literals,
    const std::vector<Ptr<AST::FuncParam>>& params)
{
    auto& fb = *fd.funcBody;
    auto file = fd.curFile;
    if (!file) {
        return false;
    }
    Parser parser(code, diag, ci->GetSourceManager(), fd.begin);
    parser.SetCurFile(file);
    auto generated = parser.ParseDecl(ScopeKind::TOPLEVEL);
    auto generatedFunc = DynamicCast<FuncDecl*>(generated.get());
    if (!generatedFunc || !generatedFunc->funcBody || !generatedFunc->funcBody->body) {
        return false;
    }
    fb.body = std::move(generatedFunc->funcBody->body);
    Walker(fb.body.get(), [&](Ptr<Node> node) {
        node->begin = fd.begin;
        node->end = fd.end;
        if (auto literal = DynamicCast<LitConstExpr*>(node); literal && literals.count(literal->stringValue)) {
            literal->stringValue = literals.at(literal->stringValue);
        }
        if (auto re = DynamicCast<RefExpr*>(node)) {
            auto name = re->ref.identifier.Val();
            if (name == "__cj_arkts_current") {
                if (auto target = importManager.GetImportedDecl(CONTEXT_PACKAGE, name)) {
                    re->ref.target = target;
                    re->ref.targets = {target};
                    re->SetTy(target->GetTy());
                }
            }
            if (name == "__cj_arkts_BigInt") {
                if (auto target = importManager.GetImportedDecl("std.math.numeric", "BigInt")) {
                    re->ref.target = target;
                    re->ref.targets = {target};
                    re->SetTy(target->GetTy());
                }
            }
            if (name == "Exception") {
                if (auto target = importManager.GetImportedDecl("std.core", "Exception")) {
                    re->ref.target = target;
                    re->ref.targets = {target};
                    re->SetTy(target->GetTy());
                }
            }
            if (name == ARKTS_CONVERSION_EXCEPTION || name == Native::FFI::ArkTS::ARKTS_RUNTIME_EXCEPTION) {
                if (auto target = importManager.GetImportedDecl(ARKTS_LANGUAGE_PACKAGE, name)) {
                    re->ref.target = target;
                    re->ref.targets = {target};
                    re->SetTy(target->GetTy());
                }
            }
            for (size_t i = 0; i < params.size(); ++i) {
                if (name == "__argument_" + std::to_string(i)) {
                    re->ref.identifier = params[i]->identifier;
                    re->ref.target = params[i];
                    // Parameters precede the generated body. Using the
                    // declaration's beginning makes Lookup reject them
                    // as declared after this compiler-generated use.
                    re->begin = fd.end;
                }
            }
        }
        if (auto returned = DynamicCast<ReturnExpr*>(node)) {
            returned->refFuncBody = &fb;
        }
        return VisitAction::WALK_CHILDREN;
    }).Walk();
    AddCurFile(*fb.body, file);
    auto oldScope = ctx.currentScopeName;
    auto oldLevel = ctx.currentScopeLevel;
    ctx.currentScopeName = params.empty() ? fb.scopeName : params.front()->scopeName;
    if (params.empty()) {
        auto gate = ctx.currentScopeName.find_last_of(ScopeManagerApi::childScopeNameSplit);
        if (gate != std::string::npos) {
            ctx.currentScopeName[gate] = ScopeManagerApi::scopeNameSplit;
        }
    }
    ctx.currentScopeLevel = fb.scopeLevel + 1;
    Collector collector(scopeManager);
    auto previousSymbol = ctx.symbolTable.empty() ? ctx.symbolTable.end() : std::prev(ctx.symbolTable.end());
    // Function-body statements live directly in the parameter scope;
    // collecting the Block would create a nested scope unlike
    // Collector::CollectFuncBody and hide the generated local bindings.
    for (auto& statement : fb.body->body) {
        collector.BuildSymbolTable(ctx, statement.get(), ci->buildTrie);
    }
    // PreCheck has already built the declaration lookup map. Updating
    // only the symbol table leaves these new locals invisible to Lookup.
    auto firstGenerated = previousSymbol == ctx.symbolTable.end() ? ctx.symbolTable.begin() : std::next(previousSymbol);
    for (auto it = firstGenerated; it != ctx.symbolTable.end(); ++it) {
        auto& sym = *it;
        if (auto generatedDecl = DynamicCast<Decl*>(sym->node)) {
            ctx.AddDeclName({sym->name, ScopeManagerApi::GetScopeNameWithoutTail(sym->scopeName)},
                *generatedDecl);
        }
    }
    ctx.currentScopeName = oldScope;
    ctx.currentScopeLevel = oldLevel;
    return true;
}

// Prepare the variable form: `@ArkTS[globalVar: true] foreign func` keeps its
// declaration and receives the read body after type checking. The removed
// `foreign let`/`foreign var` spellings are refused here.
void Native::FFI::ArkTS::PrepareVariables(Package& pkg, DiagnosticEngine& diag)
{
    if (pkg.TestAttr(Attribute::IMPORTED)) {
        return;
    }
    // ArkTS global variables are declared with `@ArkTS[globalVar: true] foreign
    // func`. Refuse the two removed spellings here, before the C FFI path would
    // try to lower them as external C bindings: a foreign `let`/`var`, and a
    // `globalVar: true` on anything other than a foreign func.
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            if (decl->outerDecl) {
                continue;
            }
            if (IsGlobalVar(*decl)) {
                auto fd = DynamicCast<FuncDecl*>(decl.get());
                if (fd == nullptr || !decl->TestAttr(Attribute::FOREIGN)) {
                    diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_global_var_not_foreign_func, *decl,
                        decl->identifier.Val());
                }
                continue;
            }
            if (!decl->HasAnno(AnnotationKind::ARKTS) || !decl->TestAttr(Attribute::FOREIGN)) {
                continue;
            }
            auto vd = DynamicCast<VarDecl*>(decl.get());
            if (vd) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_var_unsupported, *vd, vd->identifier.Val());
            }
        }
    }
}

namespace Cangjie::Native::FFI::ArkTS {
// Shape and value contract of a non-foreign @ArkTS enum. The enum is exported
// to ArkTS but not lowered: only the declaration is checked, and no value
// table is registered.
void ValidateExportedEnum(AST::EnumDecl& decl, DiagnosticEngine& diag);
} // namespace Cangjie::Native::FFI::ArkTS

namespace {
// Validate the option layer of an enum that has no later option fold of its
// own. A malformed layer is reported once and the enum is left unvalidated.
bool ValidateArkTSEnumOptions(EnumDecl& decl, DiagnosticEngine& diag)
{
    std::unordered_map<std::string, std::string> options;
    Native::FFI::ArkTS::ArkTSOptionStatus status;
    if (!Native::FFI::ArkTS::ArkTSDeclOptions(decl, options, &status)) {
        Native::FFI::ArkTS::DiagnoseArkTSOptionProblem(diag, decl, options, status);
        decl.EnableAttr(Attribute::IS_BROKEN);
        return false;
    }
    return true;
}
} // namespace

void Native::FFI::ArkTS::PrepareEnums(AST::Package& pkg, DiagnosticEngine& diag)
{
    if (pkg.TestAttr(Attribute::IMPORTED)) {
        return;
    }
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            auto ed = DynamicCast<EnumDecl*>(decl.get());
            // A declaration refused by the option contract keeps its single
            // diagnostic and is not lowered or validated again.
            if (!ed || !ed->HasAnno(AnnotationKind::ARKTS) || ed->outerDecl ||
                ed->TestAttr(Attribute::IS_BROKEN) || !ValidateArkTSEnumOptions(*ed, diag)) {
                continue;
            }
            if (ed->TestAttr(Attribute::FOREIGN)) {
                PrepareEnum(*ed, diag);
            } else {
                ValidateExportedEnum(*ed, diag);
            }
        }
    }
}

namespace Cangjie::Native::FFI::ArkTS {

bool BuildArkTSEnumValueChain(EnumDecl& decl, DiagnosticEngine* diag, std::vector<ArkTSEnumMapping>& members);

// Common declaration shape of an @ArkTS enum: no generics, no super
// interfaces, no non-exhaustive constructor. A foreign enum also refuses
// members other than constructors; an exported enum keeps them unexported.
bool ValidateArkTSEnumShape(EnumDecl& decl, DiagnosticEngine* diag, bool foreign)
{
    auto report = [&](DiagKindRefactor kind, Node& at, auto&&... args) {
        if (diag != nullptr) {
            diag->DiagnoseRefactor(kind, at, std::forward<decltype(args)>(args)...);
        }
    };
    if (decl.GetGeneric()) {
        report(DiagKindRefactor::sema_arkts_enum_generic, decl, decl.identifier.Val());
        return false;
    }
    if (!decl.inheritedTypes.empty()) {
        report(DiagKindRefactor::sema_arkts_enum_super, decl, decl.identifier.Val());
        return false;
    }
    if (decl.hasEllipsis) {
        report(DiagKindRefactor::sema_arkts_enum_non_exhaustive, decl, decl.identifier.Val());
        return false;
    }
    if (foreign && !decl.members.empty()) {
        report(DiagKindRefactor::sema_arkts_enum_member, decl, decl.identifier.Val());
        return false;
    }
    return true;
}

// Build the value table of a lowered foreign enum. `diag` is null when the enum
// is imported: its declaration was validated in the declaring package, and only
// the serialized value mapping is needed here.
bool BuildArkTSEnumMembers(EnumDecl& decl, DiagnosticEngine* diag, std::vector<ArkTSEnumMapping>& members)
{
    if (!ValidateArkTSEnumShape(decl, diag, true)) {
        return false;
    }
    return BuildArkTSEnumValueChain(decl, diag, members);
}

// Build the value chain of an @ArkTS enum. `diag` is null when the enum is
// imported; the declaring package already validated the values and only needs
// the serialized mapping.
bool BuildArkTSEnumValueChain(EnumDecl& decl, DiagnosticEngine* diag, std::vector<ArkTSEnumMapping>& members)
{
    auto report = [&](DiagKindRefactor kind, Node& at, auto&&... args) {
        if (diag != nullptr) {
            diag->DiagnoseRefactor(kind, at, std::forward<decltype(args)>(args)...);
        }
    };
    bool valid = true;
    // Auto numbering mirrors ArkTS: the first member defaults to 0 and an
    // omitted value continues the previous numeric value + 1; a string member
    // disables the counter, so a later omitted value is an error. The counter
    // is exact (not double), so large or non-decimal spellings do not drift.
    ArkTSEnumInteger counter{1, true}; // -1, so the first omitted member is 0.
    bool counterValid = true;
    auto findRawValue = [](const Decl& ctor) -> const Annotation* {
        for (auto& anno : ctor.annotations) {
            if (anno->kind == AnnotationKind::RAW_VALUE) {
                return anno.get(); // Duplicates are rejected by the annotation check.
            }
        }
        return nullptr;
    };
    for (auto& ctor : decl.constructors) {
        auto varCtor = DynamicCast<VarDecl*>(ctor.get());
        auto funcCtor = DynamicCast<FuncDecl*>(ctor.get());
        if (funcCtor != nullptr) {
            // A constructor with an associated value maps a type member of a
            // union-mode enum. It carries its payload type, never a literal
            // @RawValue value.
            if (findRawValue(*funcCtor) != nullptr) {
                valid = false; // The annotation check reports the misplaced value.
                continue;
            }
            Ptr<Type> payloadType;
            Ptr<Ty> payloadTy;
            size_t arity = 0;
            if (funcCtor->funcBody != nullptr) {
                for (auto& list : funcCtor->funcBody->paramLists) {
                    arity += list->params.size();
                    if (payloadType == nullptr && payloadTy == nullptr && !list->params.empty()) {
                        auto& param = list->params.front();
                        payloadType = param->type;
                        if (payloadType == nullptr) {
                            payloadTy = param->GetTy();
                        }
                    }
                }
            }
            if (arity != 1 || (payloadType == nullptr && payloadTy == nullptr)) {
                report(DiagKindRefactor::sema_arkts_enum_payload_arity, *funcCtor, funcCtor->identifier.Val());
                valid = false;
                continue;
            }
            members.push_back({funcCtor, ArkTSEnumValueKind::PAYLOAD, "", payloadType, payloadTy});
            continue;
        }
        if (varCtor == nullptr) {
            report(DiagKindRefactor::sema_arkts_enum_payload_arity, *ctor, ctor->identifier.Val());
            valid = false;
            continue;
        }
        const Annotation* value = findRawValue(*varCtor);
        auto literal = (value != nullptr && value->args.size() == 1 && value->args[0]->expr != nullptr)
                           ? DynamicCast<LitConstExpr*>(value->args[0]->expr.get())
                           : nullptr;
        if (literal && literal->kind == LitConstKind::STRING) {
            counterValid = false;
            members.push_back({varCtor, ArkTSEnumValueKind::STRING, literal->stringValue, nullptr, nullptr});
            continue;
        }
        if (literal && literal->kind == LitConstKind::BOOL) {
            // A boolean member maps its literal spelling verbatim (true/false);
            // like a string member it disables the numeric auto-increment.
            counterValid = false;
            members.push_back({varCtor, ArkTSEnumValueKind::BOOLEAN, literal->stringValue, nullptr, nullptr});
            continue;
        }
        if (literal && literal->kind == LitConstKind::INTEGER) {
            // Normalize the spelling to canonical decimal text (folding 0b/0o/0x
            // prefixes and '_' separators) so the auto-increment chain and the
            // generated Float64(<value>) both see the same exact number; a value
            // at or above 2^63 is spelled as a float literal so the unsuffixed
            // Int64 range check does not reject it.
            if (!ParseArkTSEnumInteger(literal->stringValue, counter)) {
                report(DiagKindRefactor::sema_arkts_enum_value_type_mismatch, *varCtor, "Integer", "out of range");
                valid = false;
                continue;
            }
            counterValid = true;
            members.push_back({varCtor, ArkTSEnumValueKind::NUMBER, counter.DecimalSpelling(), nullptr, nullptr});
            continue;
        }
        if (!counterValid) {
            report(DiagKindRefactor::sema_arkts_enum_value_type_mismatch, *varCtor, "String", "auto-increment");
            valid = false;
            continue;
        }
        if (!counter.Increment()) {
            report(DiagKindRefactor::sema_arkts_enum_value_type_mismatch, *varCtor, "Integer", "out of range");
            valid = false;
            continue;
        }
        members.push_back({varCtor, ArkTSEnumValueKind::NUMBER, counter.DecimalSpelling(), nullptr, nullptr});
    }
    return valid && !members.empty();
}

void ValidateExportedEnum(EnumDecl& decl, DiagnosticEngine& diag)
{
    if (!ValidateArkTSEnumShape(decl, &diag, false)) {
        return;
    }
    // An ArkTS enum carries literal values only, so a constructor with an
    // associated value has no exported representation.
    for (auto& ctor : decl.constructors) {
        if (!DynamicCast<VarDecl*>(ctor.get())) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_enum_payload, *ctor, ctor->identifier.Val());
            return;
        }
    }
    // The value chain is checked without registering a mapping: the enum is
    // not lowered, so no foreign-function glue can consult it.
    std::vector<ArkTSEnumMapping> members;
    (void)BuildArkTSEnumValueChain(decl, &diag, members);
}

} // namespace Cangjie::Native::FFI::ArkTS

void Native::FFI::ArkTS::PrepareEnum(AST::EnumDecl& decl, DiagnosticEngine& diag)
{
    // Phase 1 keeps the Cangjie side a plain enum: its constructors stay normal
    // enum values and only the foreign-function glue consults the @RawValue
    // mapping registered below. Mark ARKTS_LOWERED so later annotation checks
    // accept @ArkTS/@RawValue on this enum and its constructors.
    decl.EnableAttr(Attribute::ARKTS_LOWERED);
    decl.DisableAttr(Attribute::FOREIGN, Attribute::C, Attribute::NO_MANGLE);
    for (auto it = decl.modifiers.begin(); it != decl.modifiers.end();) {
        if (it->modifier == TokenKind::FOREIGN) {
            it = decl.modifiers.erase(it);
        } else {
            ++it;
        }
    }
    std::vector<ArkTSEnumMapping> members;
    if (!BuildArkTSEnumMembers(decl, &diag, members)) {
        return;
    }
    g_arktsState.enums.emplace(&decl, std::move(members));
}

void Native::FFI::ArkTS::PrepareVariablesAndEnums(AST::Package& pkg, DiagnosticEngine& diag)
{
    PrepareVariables(pkg, diag);
    PrepareEnums(pkg, diag);
}

void TypeChecker::TypeCheckerImpl::LowerArkTSVariables(ASTContext& ctx)
{
    if (ctx.curPackage->TestAttr(Attribute::IMPORTED)) {
        return;
    }
    for (auto& file : ctx.curPackage->files) {
        // A public non-foreign @ArkTS global variable is exported to ArkTS,
        // so its declared type must be ArkTS-compatible. An inferred variable
        // takes its type from the initializer and is not re-checked here.
        for (auto& decl : file->decls) {
            auto vd = DynamicCast<VarDecl*>(decl.get());
            if (!vd || vd->outerDecl || !vd->HasAnno(AnnotationKind::ARKTS) ||
                vd->TestAnyAttr(Attribute::FOREIGN, Attribute::ARKTS_LOWERED, Attribute::IS_BROKEN) ||
                !vd->TestAttr(Attribute::PUBLIC) || !vd->type) {
                continue;
            }
            auto desc = ArkTSClassifyPosition(ctx, vd->type.get());
            if (ArkTSTypeDescGeneratable(desc)) {
                continue;
            }
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_exported_var_unsupported_type, *vd,
                vd->type->GetTy() ? Ty::ToString(vd->type->GetTy()) : vd->identifier.Val());
            vd->EnableAttr(Attribute::IS_BROKEN);
        }
        for (auto& decl : file->decls) {
            auto fd = DynamicCast<FuncDecl*>(decl.get());
            if (!fd || !fd->funcBody || fd->funcBody->body || !Native::FFI::ArkTS::IsGlobalVar(*fd)) {
                continue;
            }
            // A globalVar func maps an ArkTS global variable: it must be
            // parameterless and carry an explicit return type (the value's
            // type). Lowering reads the module property once and converts it,
            // exactly like the former `foreign let` getter did.
            bool hasParams = false;
            for (auto& list : fd->funcBody->paramLists) {
                hasParams = hasParams || !list->params.empty();
            }
            if (hasParams) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_global_var_has_params, *fd, fd->identifier.Val());
                continue;
            }
            if (fd->funcBody->retType == nullptr) {
                // The parser already rejects a foreign function without an
                // explicit return type; keep the guard so the lowering below
                // never dereferences a null type.
                continue;
            }
            if (!importManager.GetImportedDecl(CONTEXT_PACKAGE, "__cj_arkts_current")) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_bridge_pending, *fd);
                continue;
            }
            auto resolveScalar = [&](Ptr<Type> type) {
                if (!type) {
                    return std::string{};
                }
                SetTypeTy(ctx, *type); // Also substitutes aliases and preserves aliasTy for cjo.
                return ScalarName(type->GetTy());
            };
            auto ret = resolveScalar(fd->funcBody->retType.get());
            if (ret.empty()) {
                // A globalVar getter is a foreign function whose return type
                // must be a supported scalar; report it through the general
                // foreign return-type diagnostic.
                auto& retType = *fd->funcBody->retType;
                auto& at = retType.begin.IsZero() ? static_cast<Node&>(*fd) : static_cast<Node&>(retType);
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_ret_type_unsupported, at,
                    retType.GetTy() ? Ty::ToString(retType.GetTy()) : std::string(), fd->identifier.Val());
                continue;
            }
            std::unordered_set<std::string> allocatedNames;
            auto freshName = [&](std::string name) {
                while (allocatedNames.count(name)) {
                    name += "_";
                }
                allocatedNames.emplace(name);
                return name;
            };
            auto contextName = freshName("__cj_arkts_context");
            auto moduleName = freshName("__cj_module");
            std::string code = "func __generated() { let " + contextName + " = __cj_arkts_current()\nlet " + moduleName +
                " = " + contextName + ".requireArkModule(\"__module__\").asObject()\n";
            std::unordered_map<std::string, std::string> literals{
                {"__module__", OptionValue(*fd, "module", ctx.curPackage->fullPackageName)},
                {"__name__", OptionValue(*fd, "foreignName", fd->identifier.Val())}};
            auto ns = OptionValue(*fd, "namespace", "");
            std::string receiver = moduleName;
            size_t nsIndex = 0;
            size_t start = 0;
            while (start < ns.size()) {
                auto end = ns.find('.', start);
                auto key = "__namespace_" + std::to_string(nsIndex) + "__";
                auto value = freshName("__cj_namespace_value_" + std::to_string(nsIndex));
                auto next = freshName("__cj_namespace_" + std::to_string(nsIndex++));
                literals[key] = ns.substr(start, end == std::string::npos ? end : end - start);
                // A missing namespace segment names the variable, the module
                // and the path prefix at the failing segment.
                code += Native::FFI::ArkTS::ArkTSNamespaceResolution(receiver, value, next, nsIndex - 1, "variable",
                    Native::FFI::ArkTS::ArkTSStringLiteral("__name__"), false, "");
                receiver = next;
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
            }
            auto valueName = freshName("__cj_result_value");
            auto numberName = freshName("__cj_result_number");
            auto resultName = freshName("__cj_result_converted");
            code += "let " + valueName + " = " + receiver + ".getProperty(\"__name__\")\n" +
                FromJS(ret, valueName, numberName, resultName) + "}";
            (void)SpliceArkTSGeneratedBody(ctx, *fd, code, literals, {});
        }
    }
}

void Native::FFI::ArkTS::ResetState()
{
    g_arktsState.Clear();
}

void Native::FFI::ArkTS::AttachEnumMembers(ArkTSTypeDesc& desc,
    const std::function<ArkTSTypeDesc(Ptr<Type>)>& classify,
    const std::function<ArkTSTypeDesc(Ptr<Ty>)>& classifyTy, ImportManager* imports)
{
    // The minor that appended AnnoKind.RawValue; an older cjo has no value
    // mapping for an imported enum, so the leaf stays non-generatable.
    constexpr uint8_t RAW_VALUE_CJO_MINOR = 3;
    if (desc.kind == ArkTSTypeDesc::Kind::ENUM) {
        auto decl = DynamicCast<EnumDecl*>(desc.reference.get());
        if (decl != nullptr) {
            if (!g_arktsState.attaching.insert(decl).second) {
                return; // recursive payload: leave this leaf without a table
            }
            auto found = g_arktsState.enums.find(decl);
            if (found == g_arktsState.enums.end() && decl->TestAttr(Attribute::IMPORTED) && imports != nullptr) {
                std::string packageName;
                if (decl->curFile != nullptr && decl->curFile->curPackage != nullptr) {
                    packageName = decl->curFile->curPackage->fullPackageName;
                }
                auto version = imports->GetPackageCjoVersion(packageName);
                if (version.first == CJO_MAJOR_VERSION && version.second >= RAW_VALUE_CJO_MINOR) {
                    std::vector<ArkTSEnumMapping> built;
                    if (BuildArkTSEnumMembers(*decl, nullptr, built)) {
                        found = g_arktsState.enums.emplace(decl, std::move(built)).first;
                    }
                }
            }
            if (found != g_arktsState.enums.end()) {
                desc.enumMembers.reserve(found->second.size());
                for (const auto& member : found->second) {
                    ArkTSEnumMember mapped{member.ctor->identifier.Val(), member.kind, member.value, {}};
                    if (member.kind == ArkTSEnumValueKind::PAYLOAD) {
                        if (member.payloadType != nullptr) {
                            mapped.payload.push_back(classify(member.payloadType));
                        } else if (member.payloadTy != nullptr) {
                            mapped.payload.push_back(classifyTy(member.payloadTy));
                        }
                    }
                    desc.enumMembers.push_back(std::move(mapped));
                }
            }
            g_arktsState.attaching.erase(decl);
        }
        return;
    }
    for (auto& element : desc.elements) {
        AttachEnumMembers(element, classify, classifyTy, imports);
    }
}
