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
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Basic/SourceManager.h"
#include "cangjie/Lex/Lexer.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Modules/CjoVersion.h"
#include "cangjie/Parse/Parser.h"
#include "Utils/TypeMapper.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

using namespace Cangjie;
using namespace Cangjie::AST;

namespace {

// Exact integer value of one numeric enum member. ArkTS maps the value through
// Float64, but the auto-increment chain must stay exact: a double would only
// prefix-parse non-decimal spellings (0b1010 -> 0, 0o17 -> 0, 1_000 -> 1) and
// would lose integrality beyond 2^53, silently changing the next omitted value.
struct ArkTSEnumNumberCounter {
    uint64_t magnitude{0};
    bool negative{false};
    // Once a floating member appears, omitted values keep incrementing in the
    // floating domain (1.5 -> 2.5), as the design's numeric literal rule
    // requires. Large integer members stay exact until that point.
    bool floating{false};
    double floatValue{0};

    // The canonical decimal spelling emitted into generated code. An unsigned
    // value above INT64_MAX does not fit the unsuffixed Int64 literal type, so
    // it is spelled with a '.0' suffix and read back as Float64.
    std::string DecimalSpelling() const
    {
        if (floating) {
            // Spell the value with the shortest precision that reads back exactly. The
            // stream uses the classic locale and an explicit precision so the generated
            // source stays identical whatever standard library or process locale the
            // compiler runs under.
            auto spellWithPrecision = [this](int precision) {
                std::ostringstream stream;
                stream.imbue(std::locale::classic());
                stream << std::setprecision(precision) << floatValue;
                return stream.str();
            };
            std::string spelling;
            constexpr int maxPrecision = std::numeric_limits<double>::max_digits10;
            for (int precision = 1; precision <= maxPrecision; ++precision) {
                std::string candidate = spellWithPrecision(precision);
                char* end = nullptr;
                double parsed = std::strtod(candidate.c_str(), &end);
                if (end != nullptr && *end == '\0' && !(parsed < floatValue) && !(floatValue < parsed)) {
                    spelling = std::move(candidate);
                    break;
                }
            }
            if (spelling.empty()) {
                spelling = spellWithPrecision(maxPrecision);
            }
            // The Cangjie lexer accepts only a '-' sign after the exponent
            // marker; the stream writes "1e+20". Drop the '+' so the
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

// A numeric @RawValue argument is a LitConstExpr. The CJO loader and the type
// checker fill its parsed value through AST::InitializeLitConstValue; a literal
// in the current package is validated here before type checking, so it still
// has no semantic type. Give it the ideal type the type checker would assign and
// run the same initializer instead of parsing the spelling again.
void InitializeRawValueLiteral(LitConstExpr& literal)
{
    if (literal.kind != LitConstKind::INTEGER && literal.kind != LitConstKind::FLOAT) {
        return;
    }
    const auto savedTy = literal.GetTy();
    if (!savedTy.IsCorrect()) {
        const auto idealKind =
            literal.kind == LitConstKind::INTEGER ? TypeKind::TYPE_IDEAL_INT : TypeKind::TYPE_IDEAL_FLOAT;
        literal.SetTy({TypeManager::GetPrimitiveTy(idealKind)});
    }
    InitializeLitConstValue(literal);
    literal.SetTy(savedTy);
}

// The CJO value is only semi-trusted, and the compiler's literal parser
// prefix-parses a spelling. Validate the whole text with the compiler's own
// lexer: exactly one literal token, of the expected kind, then END. A leading
// sign is a separate operator token, so only the magnitude is lexed.
bool IsWholeNumberLiteralSpelling(const std::string& spelling, bool floating)
{
    if (spelling.empty()) {
        return false;
    }
    const bool signedSpelling = spelling.front() == '-' || spelling.front() == '+';
    const auto magnitude = spelling.substr(signedSpelling ? 1 : 0);
    if (magnitude.empty()) {
        return false;
    }
    DiagnosticEngine diag; // No handlers: lexer diagnostics stay local and are dropped.
    SourceManager sourceManager;
    Lexer lexer(magnitude, diag, sourceManager);
    const auto literal = lexer.Next();
    if (literal.kind != (floating ? TokenKind::FLOAT_LITERAL : TokenKind::INTEGER_LITERAL)) {
        return false;
    }
    return lexer.Next().kind == TokenKind::END;
}

// Read the exact integer @RawValue from the literal's parsed value. IntLiteral
// already folds 0b/0o/0x prefixes, '_' separators, an optional sign and a type
// suffix, and records a 64-bit overflow in outOfRange; the counter keeps the
// magnitude and sign separately so the auto-increment chain stays exact.
bool ParseArkTSEnumInteger(LitConstExpr& literal, ArkTSEnumNumberCounter& out)
{
    if (!IsWholeNumberLiteralSpelling(literal.stringValue, false)) {
        return false;
    }
    InitializeRawValueLiteral(literal);
    // An empty spelling cannot be distinguished through IntLiteral: InitIntLiteral
    // returns early and leaves the default 0 with outOfRange false, so guard the
    // text itself to avoid a silent 0 from a dependent package's CJO.
    if (literal.stringValue.empty() || literal.constNumValue.asInt.IsOutOfRange()) {
        return false;
    }
    const auto& value = literal.constNumValue.asInt;
    out.floating = false;
    out.negative = value.IsNegativeNum();
    // Uint64() holds the two's-complement representation of a negative value.
    out.magnitude = out.negative ? 0 - value.Uint64() : value.Uint64();
    return true;
}

// Read the floating @RawValue from the literal's parsed value. The design's
// EnumValueLiteral is NumericLiteral, so ArkTS enum members such as `A = 1.5`
// are legal. InitializeLitConstValue records a signed underflow in flowStatus
// and a value that does not fit as non-finite; together with the double range
// conversion below that keeps the rejections the spelling parser had.
bool ParseArkTSEnumFloat(LitConstExpr& literal, ArkTSEnumNumberCounter& out)
{
    if (!IsWholeNumberLiteralSpelling(literal.stringValue, true)) {
        return false;
    }
    InitializeRawValueLiteral(literal);
    const auto& value = literal.constNumValue.asFloat;
    if (literal.stringValue.empty() || value.flowStatus != Expr::FlowStatus::NORMAL) {
        return false;
    }
    const double number = static_cast<double>(value.value);
    // long double represents values beyond double and near-zero values that
    // round to 0; reject both instead of splicing an infinity or a silent zero.
    // A subnormal double is also rejected, matching the strtod range check the
    // spelling parser applied (it reported every subnormal as ERANGE).
    if (!std::isfinite(number) || std::fpclassify(number) == FP_SUBNORMAL ||
        (number == 0.0 && value.value != 0.0L)) {
        return false;
    }
    out.floating = true;
    out.floatValue = number;
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

// The capability gates are not part of the per-pass lowering tables: the
// checker sets them before ResetState, and they must survive that reset.
bool g_arktsExperimentalEnabled = false;
bool g_arktsTargetSupported = false;

// Bind an injected package qualifier (ark_interop, cj_arkts_context_v1) to its
// PackageDecl, so a same-named user declaration cannot capture it. Returns
// whether the name was one of the package qualifiers.
bool BindArkTSGeneratedPackageRef(Node& node, Reference& ref, const std::string& name,
    ImportManager& importManager)
{
    if (name != Native::FFI::ArkTS::ARKTS_INTEROP_PACKAGE &&
        name != Native::FFI::ArkTS::ARKTS_CONTEXT_ABI_PACKAGE) {
        return false;
    }
    auto fullName = name == Native::FFI::ArkTS::ARKTS_INTEROP_PACKAGE
                        ? std::string(Native::FFI::ArkTS::ARKTS_INTEROP_FULL_PACKAGE)
                        : std::string(Native::FFI::ArkTS::ARKTS_CONTEXT_ABI_PACKAGE);
    if (auto target = importManager.GetPackageDecl(fullName)) {
        ref.target = target;
        ref.targets = {target};
        // A package qualifier has no value type; keep it untyped so the
        // qualified node resolves its member through the PackageDecl.
        node.SetTy({TypeManager::GetInvalidTy()});
    }
    return true;
}

// Bind a class/enum placeholder leaf of generated code to the declaration the
// wrapper recorded for it.
void BindArkTSGeneratedClassRef(Node& node, Reference& ref, const std::string& name,
    const std::unordered_map<std::string, Ptr<Decl>>* classRefs)
{
    if (classRefs == nullptr) {
        return;
    }
    auto found = classRefs->find(name);
    if (found == classRefs->end()) {
        return;
    }
    ref.identifier = found->second->identifier;
    ref.target = found->second;
    ref.targets = {found->second};
    node.SetTy({found->second->GetTy()});
}

// Bind a well-known generated name (Option, the context support symbols and
// the arkts.lang exceptions) to its imported declaration.
void BindArkTSGeneratedKnownRef(RefExpr& re, const std::string& name, ImportManager& importManager)
{
    Ptr<Decl> target;
    if (name == "Option") {
        target = importManager.GetImportedDecl("std.core", "Option");
    } else if (name == Native::FFI::ArkTS::ARKTS_CONTEXT_CURRENT) {
        target = importManager.GetImportedDecl(Native::FFI::ArkTS::ARKTS_CONTEXT_ABI_PACKAGE, name);
    } else if (name == "__cj_arkts_BigInt") {
        target = importManager.GetImportedDecl("std.math.numeric", "BigInt");
    } else if (name == "Exception") {
        target = importManager.GetImportedDecl("std.core", "Exception");
    } else if (name == Native::FFI::ArkTS::ARKTS_CONVERSION_EXCEPTION ||
        name == Native::FFI::ArkTS::ARKTS_CONVERSION_ALIAS) {
        // The getter spells the conversion exception through the private alias
        // the injected import binds; both spellings denote one declaration.
        target = importManager.GetImportedDecl(
            Native::FFI::ArkTS::ARKTS_LANGUAGE_PACKAGE, Native::FFI::ArkTS::ARKTS_CONVERSION_EXCEPTION);
    } else if (name == Native::FFI::ArkTS::ARKTS_RUNTIME_EXCEPTION ||
        name == Native::FFI::ArkTS::ARKTS_BUSINESS_EXCEPTION) {
        target = importManager.GetImportedDecl(Native::FFI::ArkTS::ARKTS_LANGUAGE_PACKAGE, name);
    }
    if (target == nullptr) {
        return;
    }
    re.ref.target = target;
    re.ref.targets = {target};
    re.SetTy(target->GetTy());
}

// Bind the synthesized `__argument_N` names to the function parameters. A
// parameter precedes the generated body; using the declaration's beginning
// makes Lookup reject it as declared after this compiler-generated use.
void BindArkTSGeneratedArgumentRef(RefExpr& re, const FuncDecl& fd, const std::string& name,
    const std::vector<Ptr<FuncParam>>& params)
{
    for (size_t i = 0; i < params.size(); ++i) {
        if (name != "__argument_" + std::to_string(i)) {
            continue;
        }
        re.ref.identifier = params[i]->identifier;
        re.ref.target = params[i];
        re.begin = fd.end;
    }
}

// Rebinding of one generated RefType: a support-type qualifier, the shared
// Option annotation, or a class/enum reference leaf.
VisitAction RebindArkTSGeneratedRefType(RefType& refType, ImportManager& importManager,
    const std::unordered_map<std::string, Ptr<Decl>>* classRefs)
{
    const auto name = refType.ref.identifier.Val();
    if (BindArkTSGeneratedPackageRef(refType, refType.ref, name, importManager)) {
        return VisitAction::WALK_CHILDREN;
    }
    // A generated annotation such as Option<T> is pinned to std.core; a
    // parameterized reference keeps its type arguments.
    if (name == "Option") {
        if (auto target = importManager.GetImportedDecl("std.core", "Option")) {
            refType.ref.target = target;
            refType.ref.targets = {target};
            if (refType.typeArguments.empty()) {
                refType.SetTy(target->GetTy());
            }
        }
        return VisitAction::WALK_CHILDREN;
    }
    BindArkTSGeneratedClassRef(refType, refType.ref, name, classRefs);
    return VisitAction::WALK_CHILDREN;
}

// Rebinding of one generated RefExpr: a package qualifier, a reference leaf,
// a support symbol or one of the function's own parameters.
VisitAction RebindArkTSGeneratedRefExpr(RefExpr& re, const FuncDecl& fd,
    const std::vector<Ptr<FuncParam>>& params, ImportManager& importManager,
    const std::unordered_map<std::string, Ptr<Decl>>* classRefs)
{
    const auto name = re.ref.identifier.Val();
    if (BindArkTSGeneratedPackageRef(re, re.ref, name, importManager)) {
        return VisitAction::WALK_CHILDREN;
    }
    BindArkTSGeneratedClassRef(re, re.ref, name, classRefs);
    BindArkTSGeneratedKnownRef(re, name, importManager);
    BindArkTSGeneratedArgumentRef(re, fd, name, params);
    return VisitAction::WALK_CHILDREN;
}

// Rebind one parsed node of a generated body: positions, placeholder literals,
// reference leaves and return expressions pointing at the spliced body.
VisitAction RebindArkTSGeneratedNode(Ptr<Node> node, const FuncDecl& fd, FuncBody& fb,
    const std::unordered_map<std::string, std::string>& literals,
    const std::vector<Ptr<FuncParam>>& params,
    const std::unordered_map<std::string, Ptr<Decl>>* classRefs, ImportManager& importManager)
{
    node->begin = fd.begin;
    node->end = fd.end;
    if (auto literal = DynamicCast<LitConstExpr*>(node)) {
        std::string rebound;
        // The raw spelling decides: a placeholder literal is generated without
        // escapes, while a user value that collides with a placeholder name is
        // escaped in the source, so only the real placeholders match here.
        if (Native::FFI::ArkTS::ArkTSRebindPlaceholderLiteral(literal->rawString, literals, rebound)) {
            literal->stringValue = rebound;
        }
    }
    if (auto refType = DynamicCast<RefType*>(node)) {
        return RebindArkTSGeneratedRefType(*refType, importManager, classRefs);
    }
    if (auto re = DynamicCast<RefExpr*>(node)) {
        return RebindArkTSGeneratedRefExpr(*re, fd, params, importManager, classRefs);
    }
    if (auto returned = DynamicCast<ReturnExpr*>(node)) {
        returned->refFuncBody = &fb;
    }
    return VisitAction::WALK_CHILDREN;
}

// Enter the generated body's scope (one level below the function body),
// collect its statements into the symbol table and expose the new locals to
// declaration lookup; restores the previous scope before returning.
void CollectArkTSGeneratedBodyScope(ASTContext& ctx, FuncBody& fb,
    const std::vector<Ptr<FuncParam>>& params, ScopeManager& scopeManager, bool buildTrie)
{
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
        collector.BuildSymbolTable(ctx, statement.get(), buildTrie);
    }
    // PreCheck has already built the declaration lookup map. Updating
    // only the symbol table leaves these new locals invisible to Lookup.
    auto firstGenerated =
        previousSymbol == ctx.symbolTable.end() ? ctx.symbolTable.begin() : std::next(previousSymbol);
    for (auto it = firstGenerated; it != ctx.symbolTable.end(); ++it) {
        auto& sym = *it;
        if (auto generatedDecl = DynamicCast<Decl*>(sym->node)) {
            ctx.AddDeclName({sym->name, ScopeManagerApi::GetScopeNameWithoutTail(sym->scopeName)}, *generatedDecl);
        }
    }
    ctx.currentScopeName = oldScope;
    ctx.currentScopeLevel = oldLevel;
}
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
    Walker(fb.body.get(), [&fd, &fb, &literals, &params, classRefs, this](Ptr<Node> node) {
        return RebindArkTSGeneratedNode(node, fd, fb, literals, params, classRefs, importManager);
    }).Walk();
    AddCurFile(*fb.body, file);
    CollectArkTSGeneratedBodyScope(ctx, fb, params, scopeManager, ci->buildTrie);
    return true;
}

// Refuse the two removed `foreign let`/`foreign var` spellings at one
// top-level declaration. ArkTS global variables are declared with
// `@ArkTS[globalVar: true] foreign func`.
void CheckArkTSGlobalVarDecl(Decl& decl, DiagnosticEngine& diag)
{
    if (decl.outerDecl) {
        return;
    }
    if (Native::FFI::ArkTS::IsGlobalVar(decl)) {
        auto fd = DynamicCast<FuncDecl*>(&decl);
        if (fd == nullptr || !decl.TestAttr(Attribute::FOREIGN)) {
            diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_global_var_not_foreign_func, decl,
                decl.identifier.Val());
        }
        return;
    }
    if (!decl.HasAnno(AnnotationKind::ARKTS) || !decl.TestAttr(Attribute::FOREIGN)) {
        return;
    }
    auto vd = DynamicCast<VarDecl*>(&decl);
    if (vd) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_var_unsupported, *vd, vd->identifier.Val());
    }
}

// Prepare the variable form: `@ArkTS[globalVar: true] foreign func` keeps its
// declaration and receives the read body after type checking. The removed
// `foreign let`/`foreign var` spellings are refused here.
void Native::FFI::ArkTS::PrepareVariables(Package& pkg, DiagnosticEngine& diag)
{
    if (pkg.TestAttr(Attribute::IMPORTED)) {
        return;
    }
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            CheckArkTSGlobalVarDecl(*decl, diag);
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
    auto report = [&diag](DiagKindRefactor kind, Node& at, auto&&... args) {
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

namespace {
const Annotation* FindArkTSRawValue(const Decl& ctor)
{
    for (auto& anno : ctor.annotations) {
        if (anno->kind == AnnotationKind::ARKTS_RAW_VALUE) {
            return anno.get(); // Duplicates are rejected by the parser.
        }
    }
    return nullptr;
}

LitConstExpr* ArkTSRawValueLiteral(const Annotation* value)
{
    if (value == nullptr || value->args.size() != 1 || value->args[0]->expr == nullptr) {
        return nullptr;
    }
    return DynamicCast<LitConstExpr*>(value->args[0]->expr.get());
}

void ReportArkTSEnumMismatch(
    DiagnosticEngine* diag, Node& at, const std::string& expected, const std::string& actual)
{
    if (diag != nullptr) {
        diag->DiagnoseRefactor(DiagKindRefactor::sema_arkts_enum_value_type_mismatch, at, expected, actual);
    }
}

void ReportArkTSEnumPayloadArity(DiagnosticEngine* diag, Node& at, const std::string& name)
{
    if (diag != nullptr) {
        diag->DiagnoseRefactor(DiagKindRefactor::sema_arkts_enum_payload_arity, at, name);
    }
}

// Associated-value position of a payload constructor: arity plus the first
// parameter's type node (declaring package) or resolved type (imported ctor).
struct ArkTSPayloadSlot {
    Ptr<Type> type;
    Ptr<Ty> ty;
    size_t arity{0};
};

ArkTSPayloadSlot CollectArkTSPayloadSlot(const FuncDecl& funcCtor)
{
    ArkTSPayloadSlot slot;
    if (funcCtor.funcBody == nullptr) {
        return slot;
    }
    for (auto& list : funcCtor.funcBody->paramLists) {
        slot.arity += list->params.size();
        if (slot.type != nullptr || slot.ty != nullptr || list->params.empty()) {
            continue;
        }
        slot.type = list->params.front()->type;
        if (slot.type == nullptr) {
            slot.ty = list->params.front()->DataTy();
        }
    }
    return slot;
}

// A constructor with an associated value maps a type member of a union-mode
// enum. It carries its payload type, never a literal @RawValue value.
bool AddArkTSEnumPayloadMember(FuncDecl& funcCtor, DiagnosticEngine* diag,
    std::vector<ArkTSEnumMapping>& members)
{
    if (FindArkTSRawValue(funcCtor) != nullptr) {
        return false; // The annotation check reports the misplaced value.
    }
    const auto slot = CollectArkTSPayloadSlot(funcCtor);
    if (slot.arity != 1 || (slot.type == nullptr && slot.ty == nullptr)) {
        ReportArkTSEnumPayloadArity(diag, funcCtor, funcCtor.identifier.Val());
        return false;
    }
    members.push_back({&funcCtor, ArkTSEnumValueKind::PAYLOAD, "", slot.type, slot.ty});
    return true;
}

bool AddArkTSEnumBoolMember(VarDecl& varCtor, const LitConstExpr& literal, DiagnosticEngine* diag,
    bool& counterValid, std::vector<ArkTSEnumMapping>& members)
{
    // A boolean member maps its literal spelling verbatim (true/false); like a
    // string member it disables the numeric auto-increment. The design keeps
    // boolean for the §7.2 union-type mapping; an ArkTS enum and a union
    // carrier are indistinguishable here, so a boolean member is accepted as
    // that case. The spelling is spliced into generated source, so an imported
    // package whose CJO carries any other text is refused here.
    counterValid = false;
    if (literal.stringValue != "true" && literal.stringValue != "false") {
        ReportArkTSEnumMismatch(diag, varCtor, "Boolean", "true or false");
        return false;
    }
    members.push_back({&varCtor, ArkTSEnumValueKind::BOOLEAN, literal.stringValue, nullptr, nullptr});
    return true;
}

bool AddArkTSEnumFloatMember(VarDecl& varCtor, LitConstExpr& literal, DiagnosticEngine* diag,
    ArkTSEnumNumberCounter& counter, bool& counterValid, std::vector<ArkTSEnumMapping>& members)
{
    // NumericLiteral in the design includes floating values, so an ArkTS enum
    // member may be `A = 1.5`; the value maps through Float64 exactly like an
    // integer member.
    if (!ParseArkTSEnumFloat(literal, counter)) {
        ReportArkTSEnumMismatch(diag, varCtor, "Float64", "out of range");
        return false;
    }
    counterValid = true;
    members.push_back({&varCtor, ArkTSEnumValueKind::NUMBER, counter.DecimalSpelling(), nullptr, nullptr});
    return true;
}

bool AddArkTSEnumIntegerMember(VarDecl& varCtor, LitConstExpr& literal, DiagnosticEngine* diag,
    ArkTSEnumNumberCounter& counter, bool& counterValid, std::vector<ArkTSEnumMapping>& members)
{
    // Normalize the spelling to canonical decimal text (folding 0b/0o/0x
    // prefixes and '_' separators) so the auto-increment chain and the
    // generated Float64(<value>) both see the same exact number; a value at or
    // above 2^63 is spelled as a float literal so the unsuffixed Int64 range
    // check does not reject it.
    if (!ParseArkTSEnumInteger(literal, counter)) {
        ReportArkTSEnumMismatch(diag, varCtor, "Integer", "out of range");
        return false;
    }
    counterValid = true;
    members.push_back({&varCtor, ArkTSEnumValueKind::NUMBER, counter.DecimalSpelling(), nullptr, nullptr});
    return true;
}

// An omitted value continues the numeric chain. It is refused after a string
// or boolean member, where no auto-increment rule applies.
bool AddArkTSEnumOmittedMember(VarDecl& varCtor, DiagnosticEngine* diag,
    ArkTSEnumNumberCounter& counter, bool counterValid, std::vector<ArkTSEnumMapping>& members)
{
    if (!counterValid) {
        ReportArkTSEnumMismatch(
            diag, varCtor, "an explicit '@RawValue'", "a value after a non-numeric member");
        return false;
    }
    if (!counter.Increment()) {
        ReportArkTSEnumMismatch(diag, varCtor, "Integer", "out of range");
        return false;
    }
    members.push_back({&varCtor, ArkTSEnumValueKind::NUMBER, counter.DecimalSpelling(), nullptr, nullptr});
    return true;
}

// One non-payload constructor: dispatch on the literal kind of its @RawValue,
// falling back to the auto-increment rule when no literal is present.
bool AddArkTSEnumValueMember(VarDecl& varCtor, const Annotation* value, DiagnosticEngine* diag,
    ArkTSEnumNumberCounter& counter, bool& counterValid, std::vector<ArkTSEnumMapping>& members)
{
    auto literal = ArkTSRawValueLiteral(value);
    if (literal != nullptr) {
        switch (literal->kind) {
            case LitConstKind::STRING:
                counterValid = false;
                members.push_back(
                    {&varCtor, ArkTSEnumValueKind::STRING, literal->stringValue, nullptr, nullptr});
                return true;
            case LitConstKind::BOOL:
                return AddArkTSEnumBoolMember(varCtor, *literal, diag, counterValid, members);
            case LitConstKind::FLOAT:
                return AddArkTSEnumFloatMember(varCtor, *literal, diag, counter, counterValid, members);
            case LitConstKind::INTEGER:
                return AddArkTSEnumIntegerMember(varCtor, *literal, diag, counter, counterValid, members);
            default:
                break;
        }
    }
    return AddArkTSEnumOmittedMember(varCtor, diag, counter, counterValid, members);
}
} // namespace

// Build the value chain of an @ArkTS enum. `diag` is null when the enum is
// imported; the declaring package already validated the values and only needs
// the serialized mapping.
bool BuildArkTSEnumValueChain(EnumDecl& decl, DiagnosticEngine* diag, std::vector<ArkTSEnumMapping>& members)
{
    bool valid = true;
    // Auto numbering mirrors ArkTS: the first member defaults to 0 and an
    // omitted value continues the previous numeric value + 1; a string member
    // disables the counter, so a later omitted value is an error. The counter
    // is exact (not double), so large or non-decimal spellings do not drift.
    ArkTSEnumNumberCounter counter{1, true}; // -1, so the first omitted member is 0.
    bool counterValid = true;
    for (auto& ctor : decl.constructors) {
        if (auto funcCtor = DynamicCast<FuncDecl*>(ctor.get())) {
            valid = AddArkTSEnumPayloadMember(*funcCtor, diag, members) && valid;
            continue;
        }
        auto varCtor = DynamicCast<VarDecl*>(ctor.get());
        if (varCtor == nullptr) {
            ReportArkTSEnumPayloadArity(diag, *ctor, ctor->identifier.Val());
            valid = false;
            continue;
        }
        valid = AddArkTSEnumValueMember(*varCtor, FindArkTSRawValue(*varCtor), diag, counter, counterValid,
                    members) &&
            valid;
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
            if (anno == nullptr || anno->kind != AnnotationKind::ARKTS_RAW_VALUE || anno->args.size() != 1 ||
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

void Native::FFI::ArkTS::SetTargetSupported(bool supported)
{
    g_arktsTargetSupported = supported;
}

bool Native::FFI::ArkTS::TargetSupported()
{
    return g_arktsTargetSupported;
}

bool Native::FFI::ArkTS::CapabilityEnabled()
{
    return ExperimentalEnabled() && TargetSupported();
}

bool Native::FFI::ArkTS::PassEnabled(const AST::Package& pkg)
{
    return CapabilityEnabled() && !pkg.TestAttr(Attribute::IMPORTED);
}

void Native::FFI::ArkTS::PrepareVariablesAndEnums(AST::Package& pkg, DiagnosticEngine& diag)
{
    if (!PassEnabled(pkg)) {
        return;
    }
    PrepareVariables(pkg, diag);
    PrepareEnums(pkg, diag);
}

namespace {
// Checker-owned entry points and state that a free helper cannot reach
// directly, assembled once per package by LowerArkTSVariables.
struct ArkTSGlobalVarLowering {
    ASTContext& ctx;
    DiagnosticEngine& diag;
    ImportManager& importManager;
    std::unordered_map<Ptr<const Node>, Ptr<Decl>>& supportRefs;
    std::function<Native::FFI::ArkTS::ArkTSTypeDesc(Ptr<Type>, bool)> classifyPosition;
    std::function<bool(FuncDecl&, const std::string&, const std::unordered_map<std::string, std::string>&,
        const std::vector<Ptr<FuncParam>>&, const std::unordered_map<std::string, Ptr<Decl>>*)>
        spliceBody;
};

bool IsArkTSGlobalVarGetter(const FuncDecl& fd)
{
    return fd.funcBody != nullptr && fd.funcBody->body == nullptr && Native::FFI::ArkTS::IsGlobalVar(fd);
}

bool ArkTSGlobalVarHasParams(const FuncDecl& fd)
{
    for (auto& list : fd.funcBody->paramLists) {
        if (!list->params.empty()) {
            return true;
        }
    }
    return false;
}

// Fold and diagnose the @ArkTS options of a globalVar getter, then resolve the
// module value its receiver is selected from.
bool FoldArkTSGlobalVarOptions(FuncDecl& fd, const ASTContext& ctx, DiagnosticEngine& diag,
    std::unordered_map<std::string, std::string>& options, std::string& moduleValue)
{
    Native::FFI::ArkTS::ArkTSOptionStatus optionStatus;
    if (!Native::FFI::ArkTS::ArkTSDeclOptions(fd, options, &optionStatus)) {
        Native::FFI::ArkTS::DiagnoseArkTSOptionProblem(diag, fd, options, optionStatus);
        fd.EnableAttr(Attribute::IS_BROKEN);
        return false;
    }
    // The wrapper option pass also refuses an explicitly empty string option;
    // the globalVar path must not accept one just because that pass skips it.
    Ptr<Annotation> arktsLayer = nullptr;
    for (auto& annotation : fd.annotations) {
        if (annotation != nullptr && annotation->kind == AnnotationKind::ARKTS) {
            arktsLayer = annotation.get();
            break;
        }
    }
    for (const auto& [optionName, optionValue] : options) {
        if (!Native::FFI::ArkTS::ArkTSOptionValueEmpty(optionValue)) {
            continue;
        }
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_option_empty,
            arktsLayer != nullptr ? static_cast<Node&>(*arktsLayer) : static_cast<Node&>(fd), optionName);
        fd.EnableAttr(Attribute::IS_BROKEN);
        return false;
    }
    moduleValue = options.count("module") != 0 ? options.at("module") : ctx.curPackage->fullPackageName;
    if (moduleValue.empty()) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_unsupported_signature, fd, fd.identifier.Val());
        fd.EnableAttr(Attribute::IS_BROKEN);
        return false;
    }
    if (options.count("module") != 0 && Native::FFI::ArkTS::ArkTSModulePathLike(moduleValue)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_module_path_invalid, fd, moduleValue,
            fd.identifier.Val());
        fd.EnableAttr(Attribute::IS_BROKEN);
        return false;
    }
    return true;
}

std::unordered_map<std::string, std::string> MakeArkTSGlobalVarLiterals(const FuncDecl& fd,
    const std::unordered_map<std::string, std::string>& options, const std::string& moduleValue,
    const std::vector<std::string>& segments)
{
    std::unordered_map<std::string, std::string> literals{
        {Native::FFI::ArkTS::ARKTS_PLACEHOLDER_MODULE, moduleValue},
        {Native::FFI::ArkTS::ARKTS_PLACEHOLDER_NAME,
            options.count("foreignName") ? options.at("foreignName") : fd.identifier.Val()}};
    for (size_t i = 0; i < segments.size(); ++i) {
        literals[std::string(Native::FFI::ArkTS::ARKTS_PLACEHOLDER_NAMESPACE) + std::to_string(i) + "__"] =
            segments[i];
    }
    return literals;
}

// The getter's conversion throws use the private alias injected for this file;
// a bare ExternConversionException is ambiguous with std.core's export.
std::string AliasArkTSConversionException(const std::string& code)
{
    // Only identifier occurrences in the generated *code* are aliased: a user
    // string value that spells the same text (an enum @RawValue, for example)
    // must keep its bytes. The generator's strings use only the escapes emitted
    // by StringConvertor, so a quote/backslash scanner is enough here.
    const std::string bareConversion = Native::FFI::ArkTS::ARKTS_CONVERSION_EXCEPTION;
    const std::string aliasedConversion = Native::FFI::ArkTS::ARKTS_CONVERSION_ALIAS;
    auto isIdentifierChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; };
    // Extra capacity for the alias expansion of every conversion-throw site.
    constexpr size_t aliasReserveSlack = 32;
    std::string result;
    result.reserve(code.size() + aliasReserveSlack);
    bool inString = false;
    bool escaped = false;
    for (size_t i = 0; i < code.size();) {
        const char c = code[i];
        if (inString) {
            result += c;
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            ++i;
            continue;
        }
        if (c == '"') {
            inString = true;
            result += c;
            ++i;
            continue;
        }
        const bool matches = code.compare(i, bareConversion.size(), bareConversion) == 0;
        const bool leftBoundary = i == 0 || !isIdentifierChar(code[i - 1]);
        const size_t after = i + bareConversion.size();
        const bool rightBoundary = after >= code.size() || !isIdentifierChar(code[after]);
        if (matches && leftBoundary && rightBoundary) {
            result += aliasedConversion;
            i = after;
            continue;
        }
        result += c;
        ++i;
    }
    return result;
}

// Lower one `@ArkTS[globalVar: true] foreign func` into its read body. The
// declaration passed the IsArkTSGlobalVarGetter guard.
void LowerArkTSGlobalVarGetter(ArkTSGlobalVarLowering& lower, FuncDecl& fd)
{
    auto& diag = lower.diag;
    // A globalVar func maps an ArkTS global variable: it must be parameterless
    // and carry an explicit return type (the value's type). Lowering reads the
    // module property once and converts it; this is the whole globalVar
    // getter contract.
    if (ArkTSGlobalVarHasParams(fd)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_global_var_has_params, fd, fd.identifier.Val());
        return;
    }
    if (fd.funcBody->retType == nullptr) {
        // The parser already rejects a foreign function without an explicit
        // return type; keep the guard so the lowering below never dereferences
        // a null type.
        return;
    }
    if (!lower.importManager.GetImportedDecl(
        Native::FFI::ArkTS::ARKTS_CONTEXT_ABI_PACKAGE, Native::FFI::ArkTS::ARKTS_CONTEXT_CURRENT)) {
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_bridge_pending, fd);
        return;
    }
    // The getter result maps through the same classifier as a foreign function
    // return, so every ArkTS-compatible position (scalar, Option, Array, tuple,
    // foreign class/interface, foreign enum) is accepted; a genuinely
    // unsupported position keeps the general foreign return-type diagnostic.
    auto resultType = lower.classifyPosition(fd.funcBody->retType.get(), false);
    if (!Native::FFI::ArkTS::ArkTSTypeDescGeneratable(resultType)) {
        auto& retType = *fd.funcBody->retType;
        auto& at = retType.begin.IsZero() ? static_cast<Node&>(fd) : static_cast<Node&>(retType);
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_ret_type_unsupported, at,
            retType.DataTy() ? Ty::ToString(retType.DataTy()) : std::string(), fd.identifier.Val());
        return;
    }
    // A globalVar function is excluded from the wrapper option pass in
    // LowerFunctions, so its options are folded and diagnosed here. It must
    // obey the same module non-empty / path-like rules as an ordinary foreign
    // function.
    std::unordered_map<std::string, std::string> options;
    std::string moduleValue;
    if (!FoldArkTSGlobalVarOptions(fd, lower.ctx, diag, options, moduleValue)) {
        return;
    }
    // The receiver follows the shared rule: an omitted module option in
    // arkts.lang selects ctx.global, every other declaration resolves the
    // ArkTS module object.
    Native::FFI::ArkTS::ArkTSReceiverKind receiver =
        Native::FFI::ArkTS::ArkTSReceiverFor(options.count("module") != 0, moduleValue);
    auto segments =
        Native::FFI::ArkTS::ArkTSNamespaceSegments(options.count("namespace") ? options.at("namespace") : "");
    auto literals = MakeArkTSGlobalVarLiterals(fd, options, moduleValue, segments);
    // Reference leaves (foreign class/interface/enum) are emitted by
    // placeholder; SpliceArkTSGeneratedBody binds them through this map.
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto code = AliasArkTSConversionException(Native::FFI::ArkTS::ArkTSGlobalVariableGetterSource(
        resultType, segments.size(), Native::FFI::ArkTS::ARKTS_GENERATED_PREFIX, receiver, &classRefs));
    if (code.empty() || !lower.spliceBody(fd, code, literals, {}, &classRefs)) {
        // A failed splice leaves the declaration without a body; report it here
        // instead of letting it fail later in CHIR or codegen.
        diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_foreign_wrapper_unavailable, fd, fd.identifier.Val());
        fd.EnableAttr(Attribute::IS_BROKEN);
        return;
    }
    // A composite getter emits the same support references as a function
    // wrapper (ExternConversionException and friends); bind them to the
    // imported declarations so an unqualified name cannot resolve ambiguously
    // through two star imports.
    if (!Native::FFI::ArkTS::BindArkTSSupportReferences(
        fd, *fd.funcBody->body, lower.importManager, diag, lower.supportRefs, fd.identifier.Val())) {
        fd.EnableAttr(Attribute::IS_BROKEN);
    }
}

// A public non-foreign @ArkTS global variable is exported to ArkTS, so its
// declared type must be ArkTS-compatible. A non-foreign exported variable is
// not a foreign signature, so design §3.4 permits a nested Option here: the
// layers unwrap to one ArkTS `T | undefined` at the boundary. An inferred
// variable has no written type yet; CheckArkTSExportedVariables re-checks it
// on the resolved type after type checking.
void DiagnoseArkTSExportedVarWrittenType(const ArkTSGlobalVarLowering& lower, VarDecl& vd)
{
    auto desc = lower.classifyPosition(vd.type.get(), true);
    if (ArkTSTypeDescGeneratable(desc, true)) {
        return;
    }
    lower.diag.DiagnoseRefactor(DiagKindRefactor::sema_arkts_exported_var_unsupported_type, vd,
        vd.type->DataTy() ? Ty::ToString(vd.type->DataTy()) : vd.identifier.Val());
    vd.EnableAttr(Attribute::IS_BROKEN);
}
} // namespace

void TypeChecker::TypeCheckerImpl::LowerArkTSVariables(ASTContext& ctx)
{
    if (!Native::FFI::ArkTS::PassEnabled(*ctx.curPackage)) {
        return;
    }
    ArkTSGlobalVarLowering lower{ctx, diag, importManager, arktsSupportRefs,
        [this, &ctx](Ptr<Type> type, bool allowNestedOptional) {
            return ArkTSClassifyPosition(ctx, type, allowNestedOptional);
        },
        [this, &ctx](FuncDecl& fd, const std::string& code,
            const std::unordered_map<std::string, std::string>& literals,
            const std::vector<Ptr<FuncParam>>& params,
            const std::unordered_map<std::string, Ptr<Decl>>* classRefs) {
            return SpliceArkTSGeneratedBody(ctx, fd, code, literals, params, classRefs);
        }};
    for (auto& file : ctx.curPackage->files) {
        for (auto& decl : file->decls) {
            auto vd = DynamicCast<VarDecl*>(decl.get());
            if (!vd || vd->outerDecl || !vd->HasAnno(AnnotationKind::ARKTS) ||
                vd->TestAnyAttr(Attribute::FOREIGN, Attribute::ARKTS_LOWERED, Attribute::IS_BROKEN) ||
                !vd->TestAttr(Attribute::PUBLIC) || !vd->type) {
                continue;
            }
            DiagnoseArkTSExportedVarWrittenType(lower, *vd);
        }
        for (auto& decl : file->decls) {
            auto fd = DynamicCast<FuncDecl*>(decl.get());
            if (!fd || !IsArkTSGlobalVarGetter(*fd)) {
                continue;
            }
            LowerArkTSGlobalVarGetter(lower, *fd);
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
            // before type checking. A non-foreign exported variable is not a
            // foreign signature, so design §3.4 permits a nested Option in the
            // classification below: the layers unwrap to one ArkTS
            // `T | undefined` at the boundary.
            if (!vd || vd->outerDecl || !vd->HasAnno(AnnotationKind::ARKTS) ||
                vd->TestAnyAttr(Attribute::FOREIGN, Attribute::ARKTS_LOWERED, Attribute::IS_BROKEN) ||
                !vd->TestAttr(Attribute::PUBLIC) || vd->type || !Ty::IsTyCorrect(vd->GetTy().get())) {
                continue;
            }
            auto desc =
                Native::FFI::ArkTS::ArkTSClassifyType(vd->GetTy().get(), ci->importManager, true);
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
            if (Native::FFI::ArkTS::ArkTSTypeDescGeneratable(desc, true)) {
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

namespace Cangjie::Native::FFI::ArkTS {
namespace {
// Find the prepared value table of a lowered enum, rebuilding it from the
// serialized mapping for an imported enum whose cjo carries @RawValue.
const std::vector<ArkTSEnumMapping>* FindArkTSEnumTable(EnumDecl& decl, ImportManager* imports)
{
    auto found = g_arktsState.enums.find(&decl);
    if (found != g_arktsState.enums.end()) {
        return &found->second;
    }
    if (!decl.TestAttr(Attribute::IMPORTED) || imports == nullptr) {
        return nullptr;
    }
    // The minor that appended AnnoKind.RawValue; an older cjo has no value
    // mapping for an imported enum, so the leaf stays non-generatable.
    constexpr uint8_t rawValueCjoMinor = 3;
    std::string packageName;
    if (decl.curFile != nullptr && decl.curFile->curPackage != nullptr) {
        packageName = decl.curFile->curPackage->fullPackageName;
    }
    auto cjoManager = imports->GetCjoManager();
    auto version = cjoManager == nullptr ? std::pair<uint8_t, uint8_t>{0, 0}
                                         : cjoManager->GetPackageCjoVersion(packageName);
    if (version.first != CJO_MAJOR_VERSION || version.second < rawValueCjoMinor) {
        return nullptr;
    }
    std::vector<ArkTSEnumMapping> built;
    if (!BuildArkTSEnumMembers(decl, nullptr, built)) {
        return nullptr;
    }
    return &g_arktsState.enums.emplace(&decl, std::move(built)).first->second;
}

// Map one prepared member to its generated form. Returns false when the
// constructor name cannot be spelled in generated source.
bool MapArkTSEnumMember(const ArkTSEnumMapping& member,
    const std::function<ArkTSTypeDesc(Ptr<Type>)>& classify,
    const std::function<ArkTSTypeDesc(Ptr<Ty>)>& classifyTy, ArkTSEnumMember& mapped)
{
    // The generated wrapper spells the constructor as `case <slot>.<name>`, so
    // the identifier has to be a legal Cangjie identifier. A keyword that came
    // from a raw identifier gets its backticks back; anything that is not even
    // a raw identifier (a forged or corrupted CJO) drops the whole table
    // instead of reaching the source.
    std::string memberName = member.ctor->identifier.Val();
    if (!FileUtil::IsIdentifier(memberName, true)) {
        std::string rawName = "`" + memberName + "`";
        if (!FileUtil::IsIdentifier(rawName, true)) {
            return false;
        }
        memberName = rawName;
    }
    mapped.name = memberName;
    mapped.kind = member.kind;
    mapped.value = member.value;
    if (member.kind == ArkTSEnumValueKind::PAYLOAD) {
        if (member.payloadType != nullptr) {
            mapped.payload.push_back(classify(member.payloadType));
        } else if (member.payloadTy != nullptr) {
            mapped.payload.push_back(classifyTy(member.payloadTy));
        }
    }
    return true;
}

void PopulateArkTSEnumMembers(ArkTSTypeDesc& desc, const std::vector<ArkTSEnumMapping>& table,
    const std::function<ArkTSTypeDesc(Ptr<Type>)>& classify,
    const std::function<ArkTSTypeDesc(Ptr<Ty>)>& classifyTy)
{
    desc.enumMembers.reserve(table.size());
    for (const auto& member : table) {
        ArkTSEnumMember mapped;
        if (!MapArkTSEnumMember(member, classify, classifyTy, mapped)) {
            desc.enumMembers.clear();
            return;
        }
        desc.enumMembers.push_back(std::move(mapped));
    }
}

// Attach the value table to one ENUM leaf. A recursive payload stops at an
// already-attaching enum instead of rebuilding the same table forever.
void AttachArkTSEnumLeaf(ArkTSTypeDesc& desc,
    const std::function<ArkTSTypeDesc(Ptr<Type>)>& classify,
    const std::function<ArkTSTypeDesc(Ptr<Ty>)>& classifyTy, ImportManager* imports)
{
    auto decl = DynamicCast<EnumDecl*>(desc.reference.get());
    if (decl == nullptr) {
        return;
    }
    if (!g_arktsState.attaching.insert(decl).second) {
        return;
    }
    if (const auto* table = FindArkTSEnumTable(*decl, imports)) {
        PopulateArkTSEnumMembers(desc, *table, classify, classifyTy);
    }
    g_arktsState.attaching.erase(decl);
}
} // namespace
} // namespace Cangjie::Native::FFI::ArkTS

void Native::FFI::ArkTS::AttachEnumMembers(ArkTSTypeDesc& desc,
    const std::function<ArkTSTypeDesc(Ptr<Type>)>& classify,
    const std::function<ArkTSTypeDesc(Ptr<Ty>)>& classifyTy, ImportManager* imports)
{
    if (desc.kind == ArkTSTypeDesc::Kind::ENUM) {
        AttachArkTSEnumLeaf(desc, classify, classifyTy, imports);
        return;
    }
    for (auto& element : desc.elements) {
        AttachEnumMembers(element, classify, classifyTy, imports);
    }
}
