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
#include <cerrno>
#include <charconv>
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

// Exact integer value of one numeric enum member. ArkTS maps the value through
// Float64, but the auto-increment chain must stay exact, so the previous code's
// double (strtod) was not acceptable: strtod only prefix-parses non-decimal
// spellings (0b1010 -> 0, 0o17 -> 0, 1_000 -> 1) and loses integrality beyond
// 2^53, which made the next omitted member silently take the wrong value.
struct ArkTSEnumNumberCounter {
    uint64_t magnitude{0};
    bool negative{false};
    // Once a floating member appears, omitted values keep incrementing in the
    // floating domain (1.5 -> 2.5), as the design's numeric literal rule
    // requires. Large integer members stay exact until that point.
    bool floating{false};
    double floatValue{0};

    // The canonical decimal spelling emitted into generated code. An unsigned
    // value at or above 2^63 does not fit the unsuffixed Int64 literal type
    // (which the generated Float64(<value>) would otherwise trip over), so it
    // is spelled as a float literal; 2^63 is exactly representable as Float64.
    std::string DecimalSpelling() const
    {
        if (floating) {
            char buffer[64];
            auto result = std::to_chars(buffer, buffer + sizeof(buffer), floatValue, std::chars_format::general);
            std::string spelling =
                result.ec == std::errc() ? std::string(buffer, result.ptr) : std::to_string(floatValue);
            // The Cangjie lexer accepts only a '-' sign after the exponent
            // marker; std::to_chars writes "1e+20". Drop the '+' so the
            // spelling is legal generated source.
            for (size_t pos = spelling.find("e+"); pos != std::string::npos; pos = spelling.find("e+", pos)) {
                spelling.erase(pos + 1, 1);
            }
            return spelling;
        }
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

    // Value + 1 for the omitted-member chain. Returns false on overflow or a
    // non-finite floating result.
    bool Increment()
    {
        if (floating) {
            floatValue += 1.0;
            return std::isfinite(floatValue);
        }
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
bool ParseArkTSEnumInteger(const std::string& spelling, ArkTSEnumNumberCounter& out)
{
    IntLiteral literal;
    out.floating = false;
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

// Parse a floating @RawValue spelling into the enum counter. The design's
// EnumValueLiteral is NumericLiteral, so ArkTS enum members such as `A = 1.5`
// are legal. Only a finite decimal spelling is accepted; a suffix such as
// `f32` is stripped before the conversion, and the whole text must parse
// because it is later spliced into generated source as Float64(<spelling>).
bool ParseArkTSEnumFloat(const std::string& spelling, ArkTSEnumNumberCounter& out)
{
    std::string text = spelling;
    if (text.size() > 3) {
        const auto tail = text.substr(text.size() - 3);
        if (tail == "f16" || tail == "f32" || tail == "f64" || tail == "F16" || tail == "F32" || tail == "F64") {
            text.resize(text.size() - 3);
        }
    }
    // Cangjie numeric literals may group digits with '_'; strtod does not.
    text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size() || errno == ERANGE || !std::isfinite(value)) {
        return false;
    }
    out.floating = true;
    out.floatValue = value;
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

// The experimental gate is not part of the per-pass lowering tables: the
// checker sets it before ResetState, and it must survive that reset.
bool g_arktsExperimentalEnabled = false;
}

namespace Cangjie::Native::FFI::ArkTS {
// Internal prepare helpers, defined later in this file.
void PrepareVariables(AST::Package& pkg, DiagnosticEngine& diag);
void PrepareEnums(AST::Package& pkg, DiagnosticEngine& diag);
void PrepareEnum(AST::EnumDecl& decl, DiagnosticEngine& diag);
}

bool TypeChecker::TypeCheckerImpl::SpliceArkTSGeneratedBody(ASTContext& ctx, AST::FuncDecl& fd,
    const std::string& code, const std::unordered_map<std::string, std::string>& literals,
    const std::vector<Ptr<AST::FuncParam>>& params,
    const std::unordered_map<std::string, Ptr<AST::Decl>>* classRefs)
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
        if (auto literal = DynamicCast<LitConstExpr*>(node); literal) {
            std::string rebound;
            if (Native::FFI::ArkTS::ArkTSRebindPlaceholderLiteral(literal->stringValue, literals, rebound)) {
                literal->stringValue = rebound;
            }
        }
        // Reference leaves of a generated getter body carry a class/enum
        // placeholder: a type position (Array<T>, Option<T>) or a constructor
        // reference. Bind both to the recorded declaration like the function
        // and class walkers do.
        if (auto refType = DynamicCast<RefType*>(node)) {
            auto name = refType->ref.identifier.Val();
            // A generated type annotation may qualify a support type
            // (ark_interop.JSContext, ark_interop.JSValue); bind that package
            // base like the function wrapper's support-reference pass does.
            if (name == Native::FFI::ArkTS::ARKTS_INTEROP_PACKAGE ||
                name == Native::FFI::ArkTS::ARKTS_CONTEXT_ABI_PACKAGE) {
                auto fullName = name == Native::FFI::ArkTS::ARKTS_INTEROP_PACKAGE
                                    ? std::string("ohos.ark_interop")
                                    : std::string(Native::FFI::ArkTS::ARKTS_CONTEXT_ABI_PACKAGE);
                if (auto target = importManager.GetPackageDecl(fullName)) {
                    refType->ref.target = target;
                    refType->ref.targets = {target};
                    refType->SetTy({TypeManager::GetInvalidTy()});
                }
                return VisitAction::WALK_CHILDREN;
            }
            if (name == "Option") {
                // A generated annotation such as Option<T> is pinned to
                // std.core; a parameterized reference keeps its type arguments.
                if (auto target = importManager.GetImportedDecl("std.core", "Option")) {
                    refType->ref.target = target;
                    refType->ref.targets = {target};
                    if (refType->typeArguments.empty()) {
                        refType->SetTy(target->GetTy());
                    }
                }
                return VisitAction::WALK_CHILDREN;
            }
            if (classRefs != nullptr) {
                auto found = classRefs->find(name);
                if (found != classRefs->end()) {
                    refType->ref.identifier = found->second->identifier;
                    refType->ref.target = found->second;
                    refType->ref.targets = {found->second};
                    refType->SetTy(found->second->GetTy());
                }
            }
            return VisitAction::WALK_CHILDREN;
        }
        if (auto re = DynamicCast<RefExpr*>(node)) {
            auto name = re->ref.identifier.Val();
            // Generated code spells the interop surface through the injected
            // package qualifiers; bind the base reference to its PackageDecl so
            // a same-named user declaration cannot capture it.
            if (name == Native::FFI::ArkTS::ARKTS_INTEROP_PACKAGE ||
                name == Native::FFI::ArkTS::ARKTS_CONTEXT_ABI_PACKAGE) {
                auto fullName = name == Native::FFI::ArkTS::ARKTS_INTEROP_PACKAGE
                                    ? std::string("ohos.ark_interop")
                                    : std::string(Native::FFI::ArkTS::ARKTS_CONTEXT_ABI_PACKAGE);
                if (auto target = importManager.GetPackageDecl(fullName)) {
                    re->ref.target = target;
                    re->ref.targets = {target};
                    // A package qualifier has no value type; keep it untyped so
                    // the qualified node resolves its member through the
                    // PackageDecl.
                    re->SetTy({TypeManager::GetInvalidTy()});
                }
                return VisitAction::WALK_CHILDREN;
            }
            if (classRefs != nullptr) {
                auto found = classRefs->find(name);
                if (found != classRefs->end()) {
                    re->ref.identifier = found->second->identifier;
                    re->ref.target = found->second;
                    re->ref.targets = {found->second};
                    re->SetTy(found->second->GetTy());
                }
            }
            if (name == "Option") {
                if (auto target = importManager.GetImportedDecl("std.core", "Option")) {
                    re->ref.target = target;
                    re->ref.targets = {target};
                    re->SetTy(target->GetTy());
                }
            }
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
            if (name == ARKTS_CONVERSION_EXCEPTION || name == Native::FFI::ArkTS::ARKTS_CONVERSION_ALIAS ||
                name == Native::FFI::ArkTS::ARKTS_RUNTIME_EXCEPTION ||
                name == Native::FFI::ArkTS::ARKTS_BUSINESS_EXCEPTION) {
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
    ArkTSEnumNumberCounter counter{1, true}; // -1, so the first omitted member is 0.
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
                            payloadTy = param->DataTy();
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
            // like a string member it disables the numeric auto-increment. The
            // design keeps boolean for the §7.2 union-type mapping; an ArkTS
            // enum and a union carrier are indistinguishable here, so a boolean
            // member is accepted as that case. The spelling is spliced into
            // generated source, so an imported package whose CJO carries any
            // other text is refused here instead of reaching the generator.
            counterValid = false;
            if (literal->stringValue != "true" && literal->stringValue != "false") {
                report(DiagKindRefactor::sema_arkts_enum_value_type_mismatch, *varCtor, "Boolean",
                    "true or false");
                valid = false;
                continue;
            }
            members.push_back({varCtor, ArkTSEnumValueKind::BOOLEAN, literal->stringValue, nullptr, nullptr});
            continue;
        }
        if (literal && literal->kind == LitConstKind::FLOAT) {
            // NumericLiteral in the design includes floating values, so an
            // ArkTS enum member may be `A = 1.5`; the value maps through
            // Float64 exactly like an integer member.
            if (!ParseArkTSEnumFloat(literal->stringValue, counter)) {
                report(DiagKindRefactor::sema_arkts_enum_value_type_mismatch, *varCtor, "Float64", "out of range");
                valid = false;
                continue;
            }
            counterValid = true;
            members.push_back({varCtor, ArkTSEnumValueKind::NUMBER, counter.DecimalSpelling(), nullptr, nullptr});
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
            // An omitted value follows a string or boolean member, so no
            // auto-increment rule applies; name the requirement instead of
            // claiming a String predecessor.
            report(DiagKindRefactor::sema_arkts_enum_value_type_mismatch, *varCtor, "an explicit '@RawValue'",
                "a value after a non-numeric member");
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
    // An exported enum reaches ArkTS as a const enum, whose members are
    // numeric or string literals. A boolean value belongs to the section 7.2
    // union mapping; when that mapping is fully supported it will be exported
    // as a union type, not as this enum. Refuse it here instead of carrying a
    // boolean into the const-enum export.
    for (auto& ctor : decl.constructors) {
        auto varCtor = DynamicCast<VarDecl*>(ctor.get());
        if (varCtor == nullptr) {
            continue;
        }
        for (auto& anno : varCtor->annotations) {
            if (anno == nullptr || anno->kind != AnnotationKind::RAW_VALUE || anno->args.size() != 1 ||
                anno->args.front() == nullptr || anno->args.front()->expr == nullptr) {
                continue;
            }
            auto literal = DynamicCast<LitConstExpr*>(anno->args.front()->expr.get());
            if (literal != nullptr && literal->kind == LitConstKind::BOOL) {
                diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_arkts_enum_value_type_mismatch, *varCtor, "number or string", "Boolean");
                return;
            }
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

void Native::FFI::ArkTS::SetExperimentalEnabled(bool enabled)
{
    g_arktsExperimentalEnabled = enabled;
}

bool Native::FFI::ArkTS::ExperimentalEnabled()
{
    return g_arktsExperimentalEnabled;
}

bool Native::FFI::ArkTS::PassEnabled(const AST::Package& pkg)
{
    return ExperimentalEnabled() && !pkg.TestAttr(Attribute::IMPORTED);
}

void Native::FFI::ArkTS::PrepareVariablesAndEnums(AST::Package& pkg, DiagnosticEngine& diag)
{
    if (!PassEnabled(pkg)) {
        return;
    }
    PrepareVariables(pkg, diag);
    PrepareEnums(pkg, diag);
}

void TypeChecker::TypeCheckerImpl::LowerArkTSVariables(ASTContext& ctx)
{
    if (!Native::FFI::ArkTS::PassEnabled(*ctx.curPackage)) {
        return;
    }
    for (auto& file : ctx.curPackage->files) {
        // A public non-foreign @ArkTS global variable is exported to ArkTS,
        // so its declared type must be ArkTS-compatible. An inferred variable
        // has no written type yet; CheckArkTSExportedVariables re-checks it on
        // the resolved `vd->ty` after type checking.
        for (auto& decl : file->decls) {
            auto vd = DynamicCast<VarDecl*>(decl.get());
            if (!vd || vd->outerDecl || !vd->HasAnno(AnnotationKind::ARKTS) ||
                vd->TestAnyAttr(Attribute::FOREIGN, Attribute::ARKTS_LOWERED, Attribute::IS_BROKEN) ||
                !vd->TestAttr(Attribute::PUBLIC) || !vd->type) {
                continue;
            }
            // A non-foreign exported variable is not a foreign signature, so
            // design §3.4 permits a nested Option here: the layers unwrap to
            // one ArkTS `T | undefined` at the boundary.
            auto desc = ArkTSClassifyPosition(ctx, vd->type.get(), /*allowNestedOptional=*/true);
            if (ArkTSTypeDescGeneratable(desc, /*allowNestedOptional=*/true)) {
                continue;
            }
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_exported_var_unsupported_type, *vd,
                vd->type->DataTy() ? Ty::ToString(vd->type->DataTy()) : vd->identifier.Val());
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
            // The getter result maps through the same classifier as a foreign
            // function return, so every ArkTS-compatible position (scalar,
            // Option, Array, tuple, foreign class/interface, foreign enum) is
            // accepted; a genuinely unsupported position keeps the general
            // foreign return-type diagnostic below.
            auto resultType = ArkTSClassifyPosition(ctx, fd->funcBody->retType.get());
            if (!ArkTSTypeDescGeneratable(resultType)) {
                auto& retType = *fd->funcBody->retType;
                auto& at = retType.begin.IsZero() ? static_cast<Node&>(*fd) : static_cast<Node&>(retType);
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_ret_type_unsupported, at,
                    retType.DataTy() ? Ty::ToString(retType.DataTy()) : std::string(), fd->identifier.Val());
                continue;
            }
            // A globalVar function is excluded from the wrapper option pass in
            // LowerFunctions, so its options are folded and diagnosed here.
            // It must obey the same module non-empty / path-like rules as an
            // ordinary foreign function.
            std::unordered_map<std::string, std::string> options;
            Native::FFI::ArkTS::ArkTSOptionStatus optionStatus;
            if (!Native::FFI::ArkTS::ArkTSDeclOptions(*fd, options, &optionStatus)) {
                Native::FFI::ArkTS::DiagnoseArkTSOptionProblem(diag, *fd, options, optionStatus);
                fd->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            // The wrapper option pass also refuses an explicitly empty string
            // option; the globalVar path must not accept one just because that
            // pass skips it.
            Ptr<Annotation> arktsLayer = nullptr;
            for (auto& annotation : fd->annotations) {
                if (annotation != nullptr && annotation->kind == AnnotationKind::ARKTS) {
                    arktsLayer = annotation.get();
                    break;
                }
            }
            bool emptyOption = false;
            for (const auto& [optionName, optionValue] : options) {
                if (!Native::FFI::ArkTS::ArkTSOptionValueEmpty(optionValue)) {
                    continue;
                }
                diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_empty,
                    arktsLayer != nullptr ? static_cast<Node&>(*arktsLayer) : static_cast<Node&>(*fd), optionName);
                emptyOption = true;
                break;
            }
            if (emptyOption) {
                fd->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            std::string moduleValue =
                options.count("module") != 0 ? options.at("module") : ctx.curPackage->fullPackageName;
            if (moduleValue.empty() ||
                (options.count("module") != 0 && Native::FFI::ArkTS::ArkTSModulePathLike(moduleValue))) {
                if (moduleValue.empty()) {
                    diag.DiagnoseRefactor(
                        DiagKindRefactor::sema_arkts_foreign_unsupported_signature, *fd, fd->identifier.Val());
                } else {
                    diag.DiagnoseRefactor(
                        DiagKindRefactor::sema_arkts_foreign_module_path_invalid, *fd, moduleValue, fd->identifier.Val());
                }
                fd->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            // The receiver follows the shared rule: an omitted module option in
            // arkts.lang selects ctx.global, every other declaration resolves
            // the ArkTS module object.
            Native::FFI::ArkTS::ArkTSReceiverKind receiver =
                Native::FFI::ArkTS::ArkTSReceiverFor(options.count("module") != 0, moduleValue);
            std::unordered_map<std::string, std::string> literals{
                {Native::FFI::ArkTS::ARKTS_PLACEHOLDER_MODULE, moduleValue},
                {Native::FFI::ArkTS::ARKTS_PLACEHOLDER_NAME,
                    options.count("foreignName") ? options.at("foreignName") : fd->identifier.Val()}};
            auto segments =
                Native::FFI::ArkTS::ArkTSNamespaceSegments(options.count("namespace") ? options.at("namespace") : "");
            for (size_t i = 0; i < segments.size(); ++i) {
                literals[std::string(Native::FFI::ArkTS::ARKTS_PLACEHOLDER_NAMESPACE) + std::to_string(i) + "__"] =
                    segments[i];
            }
            // Reference leaves (foreign class/interface/enum) are emitted by
            // placeholder; SpliceArkTSGeneratedBody binds them through this map.
            std::unordered_map<std::string, Ptr<Decl>> classRefs;
            auto code = Native::FFI::ArkTS::ArkTSGlobalVariableGetterSource(resultType, segments.size(),
                Native::FFI::ArkTS::ARKTS_GENERATED_PREFIX, receiver, &classRefs);
            // The getter's conversion throws use the private alias injected
            // for this file; a bare ExternConversionException is ambiguous
            // with std.core's export.
            const std::string bareConversion = "ExternConversionException";
            const std::string aliasedConversion = Native::FFI::ArkTS::ARKTS_CONVERSION_ALIAS;
            for (size_t pos = code.find(bareConversion); pos != std::string::npos;
                 pos = code.find(bareConversion, pos + aliasedConversion.size())) {
                code.replace(pos, bareConversion.size(), aliasedConversion);
            }
            if (code.empty() || !SpliceArkTSGeneratedBody(ctx, *fd, code, literals, {}, &classRefs)) {
                // A failed splice leaves the declaration without a body; report
                // it here instead of letting it fail later in CHIR or codegen.
                diag.DiagnoseRefactor(
                    DiagKindRefactor::sema_arkts_foreign_wrapper_unavailable, *fd, fd->identifier.Val());
                fd->EnableAttr(Attribute::IS_BROKEN);
                continue;
            }
            // A composite getter emits the same support references as a
            // function wrapper (ExternConversionException and friends); bind
            // them to the imported declarations so an unqualified name cannot
            // resolve ambiguously through two star imports.
            if (!Native::FFI::ArkTS::BindArkTSSupportReferences(
                    *fd, *fd->funcBody->body, *ci->importManager, diag, arktsSupportRefs, fd->identifier.Val())) {
                fd->EnableAttr(Attribute::IS_BROKEN);
            }
        }
    }
}

// Post-type-check compatibility rule for a public non-foreign @ArkTS global
// variable. LowerArkTSVariables can only check a written Type node; an inferred
// declaration has none until the checker synthesizes `vd->ty`, so the same rule
// runs again here on that resolved type. An explicit declaration that already
// failed is IS_BROKEN and stays out of this pass, so it is diagnosed once.
void TypeChecker::TypeCheckerImpl::CheckArkTSExportedVariables(ASTContext& ctx)
{
    auto& pkg = *ctx.curPackage;
    if (!Native::FFI::ArkTS::PassEnabled(pkg)) {
        return;
    }
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            auto vd = DynamicCast<VarDecl*>(decl.get());
            // Only the inferred form is left: every written type was checked
            // before type checking.
            if (!vd || vd->outerDecl || !vd->HasAnno(AnnotationKind::ARKTS) ||
                vd->TestAnyAttr(Attribute::FOREIGN, Attribute::ARKTS_LOWERED, Attribute::IS_BROKEN) ||
                !vd->TestAttr(Attribute::PUBLIC) || vd->type || !Ty::IsTyCorrect(vd->GetTy().get())) {
                continue;
            }
            auto desc = Native::FFI::ArkTS::ArkTSClassifyType(
                vd->GetTy().get(), ci->importManager, /*allowNestedOptional=*/true);
            // Attach the prepared member value table to every enum leaf, so a
            // variable inferred as a prepared foreign enum is judged by the
            // same rule as a written one.
            auto classifyType = [this, &ctx](Ptr<Type> payloadType) {
                return ArkTSClassifyPosition(ctx, payloadType);
            };
            auto classifyTy = [this](Ptr<Ty> payloadTy) {
                return Native::FFI::ArkTS::ArkTSClassifyType(payloadTy, ci->importManager);
            };
            Native::FFI::ArkTS::AttachEnumMembers(desc, classifyType, classifyTy, ci->importManager);
            if (Native::FFI::ArkTS::ArkTSTypeDescGeneratable(desc, /*allowNestedOptional=*/true)) {
                continue;
            }
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_exported_var_unsupported_type, *vd,
                Ty::ToString(vd->GetTy().get()));
            vd->EnableAttr(Attribute::IS_BROKEN);
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
