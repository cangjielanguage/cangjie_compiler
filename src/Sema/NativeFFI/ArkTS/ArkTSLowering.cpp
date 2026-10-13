// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
#include "TypeCheckerImpl.h"
#include "ArkTSInterop.h"
#include "Collector.h"
#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Utils.h"
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

// Every conversion failure in generated code is thrown through
// ExternConversionException, the single type of that name hosted by the
// arkts.lang facade; it takes one String describing the failure.
std::string FromJS(const std::string& type, const std::string& value,
    const std::string& number, const std::string& result)
{
    auto failure = [&](const std::string& reason) {
        return "throw ExternConversionException(\"ArkTS conversion failed: " + reason + " for " + type + "\")\n";
    };
    if (type == "Nothing") {
        return "throw Exception(\"ArkTS never function returned\")\n";
    }
    if (type == "Unit") {
        return "if (" + value + ".isNull()) { return }\n" + value + ".asUndefined()\nreturn\n";
    }
    if (type == "String" || type == "Bool") {
        auto predicate = type == "String" ? "isString" : "isBoolean";
        auto conversion = type == "String" ? "asString().toString()" : "toBoolean()";
        return "if (!" + value + "." + predicate + "()) { " + failure("TypeMismatch") +
            "}\nreturn " + value + "." + conversion + "\n";
    }
    if (type == "BigInt" || type == "Int64" || type == "UInt64") {
        std::string code = "if (!" + value + ".isBigInt()) { " + failure("TypeMismatch") +
            "}\nlet " + number + " = " + value + ".asBigInt().toBigInt()\n";
        if (type == "BigInt") {
            return code + "return " + number + "\n";
        }
        auto minimum = type == "Int64" ? "-9223372036854775808" : "0";
        auto maximum = type == "Int64" ? "9223372036854775807" : "18446744073709551615";
        code += "if (" + number + " < __cj_arkts_BigInt.parse(\"" + minimum + "\") || " +
            number + " > __cj_arkts_BigInt.parse(\"" + maximum + "\")) { " + failure("OutOfRange") + "}\n";
        return code + "return " + type + ".parse(" + number + ".toString())\n";
    }
    std::string code = "if (!" + value + ".isNumber()) { " + failure("TypeMismatch") +
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
            " > " + upper + ")) { " + failure("OutOfRange") + "}\nlet " + result + " = " + type + "(" + number +
            ")\nreturn " + result + "\n";
    }
    code += "if (" + number + " != " + number + " || " + number + ".isInf()) { " +
        failure("NonFinite") + "}\n";
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
        failure(type == "Rune" ? "InvalidCodePoint" : "OutOfRange") + "}\n";
    code += "if (" + number + " != Float64(Int64(" + number + "))) { " + failure("NotInteger") + "}\n";
    if (type == "Rune") {
        return code + "if (" + number + " >= 55296.0 && " + number + " <= 57343.0) { " +
            failure("InvalidCodePoint") + "}\nreturn Rune(UInt32(" + number + "))\n";
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
        // type, so it is spelled as a float literal on either side of zero; -2^63
        // itself is exactly representable and stays a plain negative integer.
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

// Parse a Cangjie integer literal spelling (0b/0o/0x prefix or decimal, '_'
// digit separators, optional leading sign) into an exact value. Returns false
// when the spelling is not a legal integer literal or exceeds 64-bit magnitude.
bool ParseArkTSEnumInteger(const std::string& spelling, ArkTSEnumInteger& out)
{
    const char* p = spelling.c_str();
    out.negative = false;
    out.magnitude = 0;
    if (*p == '-' || *p == '+') {
        out.negative = (*p == '-');
        ++p;
    }
    int base = 10;
    if (p[0] == '0' && (p[1] == 'b' || p[1] == 'B')) {
        base = 2;
        p += 2;
    } else if (p[0] == '0' && (p[1] == 'o' || p[1] == 'O')) {
        base = 8;
        p += 2;
    } else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    }
    uint64_t value = 0;
    bool any = false;
    while (*p != '\0' && *p != 'i' && *p != 'u') {
        if (*p == '_') {
            ++p;
            continue;
        }
        int digit = -1;
        if (*p >= '0' && *p <= '9') {
            digit = *p - '0';
        } else if (*p >= 'a' && *p <= 'f') {
            digit = *p - 'a' + 10;
        } else if (*p >= 'A' && *p <= 'F') {
            digit = *p - 'A' + 10;
        }
        if (digit < 0 || digit >= base) {
            return false;
        }
        if (value > (UINT64_MAX - static_cast<uint64_t>(digit)) / static_cast<uint64_t>(base)) {
            return false;
        }
        value = value * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
        any = true;
        ++p;
    }
    if (!any) {
        return false;
    }
    // A trailing integer type suffix (i8/i16/i32/i64/u8/u16/u32/u64) is legal and
    // does not change the value, so accept and ignore it instead of reporting an
    // out-of-range-like refusal for a well-formed spelling.
    if (*p == 'i' || *p == 'u') {
        const char* s = p + 1;
        if (!((s[0] == '8' && s[1] == '\0') || (s[0] == '1' && s[1] == '6' && s[2] == '\0') ||
                (s[0] == '3' && s[1] == '2' && s[2] == '\0') || (s[0] == '6' && s[1] == '4' && s[2] == '\0'))) {
            return false;
        }
    }
    out.magnitude = value;
    // "-0" parses to magnitude 0 with the negative flag; fold it to +0 so
    // Increment never sees a negative zero (whose -1 step would underflow).
    if (out.negative && out.magnitude == 0) {
        out.negative = false;
    }
    return true;
}

// Prepared @RawValue mapping for one constructor of a lowered foreign enum.
struct ArkTSEnumMapping {
    const VarDecl* ctor;
    Native::FFI::ArkTS::ArkTSEnumValueKind kind;
    std::string value;
};

// State shared across the ArkTS prepare/lower passes. Private to this file: the
// checker reaches it only through the entry points declared in ArkTSInterop.h.
struct ArkTSLoweringState {
    std::unordered_map<const EnumDecl*, std::vector<ArkTSEnumMapping>> enums;

    void Clear()
    {
        enums.clear();
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
            if (name == "ExternConversionException") {
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

void Native::FFI::ArkTS::PrepareEnums(AST::Package& pkg, DiagnosticEngine& diag)
{
    if (pkg.TestAttr(Attribute::IMPORTED)) {
        return;
    }
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            auto ed = DynamicCast<EnumDecl*>(decl.get());
            if (!ed || !ed->HasAnno(AnnotationKind::ARKTS) || !ed->TestAttr(Attribute::FOREIGN) || ed->outerDecl) {
                continue;
            }
            PrepareEnum(*ed, diag);
        }
    }
}

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
    if (decl.GetGeneric()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_enum_generic, decl, decl.identifier.Val());
        return;
    }
    if (!decl.inheritedTypes.empty()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_enum_super, decl, decl.identifier.Val());
        return;
    }
    if (decl.hasEllipsis) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_enum_non_exhaustive, decl, decl.identifier.Val());
        return;
    }
    if (!decl.members.empty()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_enum_member, decl, decl.identifier.Val());
        return;
    }
    std::vector<ArkTSEnumMapping> members;
    bool valid = true;
    // Auto numbering mirrors ArkTS: the first member defaults to 0 and an
    // omitted value continues the previous numeric value + 1; a string member
    // disables the counter, so a later omitted value is an error. The counter
    // is exact (not double), so large or non-decimal spellings do not drift.
    ArkTSEnumInteger counter{1, true}; // -1, so the first omitted member is 0.
    bool counterValid = true;
    for (auto& ctor : decl.constructors) {
        auto varCtor = DynamicCast<VarDecl*>(ctor.get());
        if (!varCtor) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_enum_payload, *ctor, ctor->identifier.Val());
            valid = false;
            continue;
        }
        const Annotation* value = nullptr;
        for (auto& anno : varCtor->annotations) {
            if (anno->kind == AnnotationKind::RAW_VALUE) {
                value = anno.get(); // Duplicates are rejected by the annotation check.
                break;
            }
        }
        auto literal = (value != nullptr && value->args.size() == 1 && value->args[0]->expr != nullptr)
                           ? DynamicCast<LitConstExpr*>(value->args[0]->expr.get())
                           : nullptr;
        if (literal && literal->kind == LitConstKind::STRING) {
            counterValid = false;
            members.push_back({varCtor, ArkTSEnumValueKind::STRING, literal->stringValue});
            continue;
        }
        if (literal && literal->kind == LitConstKind::BOOL) {
            // A boolean member maps its literal spelling verbatim (true/false);
            // like a string member it disables the numeric auto-increment.
            counterValid = false;
            members.push_back({varCtor, ArkTSEnumValueKind::BOOLEAN, literal->stringValue});
            continue;
        }
        if (literal && literal->kind == LitConstKind::INTEGER) {
            // Normalize the spelling to canonical decimal text (folding 0b/0o/0x
            // prefixes and '_' separators) so the auto-increment chain and the
            // generated Float64(<value>) both see the same exact number; a value
            // at or above 2^63 is spelled as a float literal so the unsuffixed
            // Int64 range check does not reject it.
            if (!ParseArkTSEnumInteger(literal->stringValue, counter)) {
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_enum_value_type_mismatch, *varCtor, "Integer",
                    "out of range");
                valid = false;
                continue;
            }
            counterValid = true;
            members.push_back({varCtor, ArkTSEnumValueKind::NUMBER, counter.DecimalSpelling()});
            continue;
        }
        if (!counterValid) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_enum_value_type_mismatch, *varCtor, "String",
                "auto-increment");
            valid = false;
            continue;
        }
        if (!counter.Increment()) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_enum_value_type_mismatch, *varCtor, "Integer",
                "out of range");
            valid = false;
            continue;
        }
        members.push_back({varCtor, ArkTSEnumValueKind::NUMBER, counter.DecimalSpelling()});
    }
    if (valid && !members.empty()) {
        g_arktsState.enums.emplace(&decl, std::move(members));
    }
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
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_global_var_needs_return_type, *fd,
                    fd->identifier.Val());
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
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_var_unsupported_type, *fd,
                    fd->funcBody->retType->GetTy() ? Ty::ToString(fd->funcBody->retType->GetTy())
                                                  : fd->identifier.Val());
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
                auto next = freshName("__cj_namespace_" + std::to_string(nsIndex++));
                literals[key] = ns.substr(start, end == std::string::npos ? end : end - start);
                code += "let " + next + " = " + receiver + ".getProperty(\"" + key + "\").asObject()\n";
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

void Native::FFI::ArkTS::AttachEnumMembers(ArkTSTypeDesc& desc)
{
    if (desc.kind == ArkTSTypeDesc::Kind::ENUM) {
        auto decl = DynamicCast<EnumDecl*>(desc.reference.get());
        if (decl != nullptr) {
            auto found = g_arktsState.enums.find(decl);
            if (found != g_arktsState.enums.end()) {
                desc.enumMembers.reserve(found->second.size());
                for (const auto& member : found->second) {
                    desc.enumMembers.push_back({member.ctor->identifier.Val(), member.kind, member.value});
                }
            }
        }
        return;
    }
    for (auto& element : desc.elements) {
        AttachEnumMembers(element);
    }
}
