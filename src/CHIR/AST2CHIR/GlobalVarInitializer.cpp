// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/GlobalVarInitializer.h"

#include "cangjie/AST/Node.h"
#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/CHIR/IR/Annotation.h"
#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/CHIR/Utils/Utils.h"

using namespace Cangjie;
using namespace CHIR;

namespace {
inline std::string GetPackageInitFuncName(const std::string& pkgName, const std::string& suffix = "")
{
    auto specialName = SPECIAL_NAME_FOR_INIT_FUNCTION;
    if (suffix == "_literal") {
        specialName = SPECIAL_NAME_FOR_INIT_LITERAL_FUNCTION;
    } else if (suffix == "_importsInit") {
        specialName = SPECIAL_NAME_FOR_IMPORTS_INIT_FUNCTION;
    } else if (suffix == "_literal_importsInit") {
        specialName = SPECIAL_NAME_FOR_LITERAL_IMPORTS_INIT_FUNCTION;
#ifdef NDEBUG
    } else if (suffix != "") {
        CJC_ABORT();
#endif
    }
    return MANGLE_CANGJIE_PREFIX + MANGLE_GLOBAL_PACKAGE_INIT_PREFIX + MangleUtils::GetOptPkgName(pkgName) +
        specialName + MANGLE_FUNC_PARAM_TYPE_PREFIX + MANGLE_VOID_TY_SUFFIX;
}

inline std::string GetFileInitFuncName(
    const std::string& pkgName, const std::string& fileName, const std::string_view suffix)
{
    auto specialName = suffix.empty() ? SPECIAL_NAME_FOR_INIT_FUNCTION : SPECIAL_NAME_FOR_INIT_LITERAL_FUNCTION;
    return MANGLE_CANGJIE_PREFIX + MANGLE_GLOBAL_FILE_INIT_PREFIX + MangleUtils::GetOptPkgName(pkgName) +
        MANGLE_FILE_ID_PREFIX +
        (BaseMangler::IsHashable(fileName) ? BaseMangler::HashToBase62(fileName)
                                           : (BaseMangler::FileNameWithoutExtension(fileName) + "$")) +
        specialName + MANGLE_FUNC_PARAM_TYPE_PREFIX + MANGLE_VOID_TY_SUFFIX;
}

inline std::string GetGVInitFuncName(const std::string& gvName, const std::string& pkgName)
{
    return MANGLE_CANGJIE_PREFIX + MANGLE_GLOBAL_VARIABLE_INIT_PREFIX + MangleUtils::GetOptPkgName(pkgName) +
        MangleUtils::MangleName(gvName) + MANGLE_FUNC_PARAM_TYPE_PREFIX + MANGLE_VOID_TY_SUFFIX;
}

inline std::string GetVarInitName(const AST::VarDecl& var)
{
    return MANGLE_CANGJIE_PREFIX + MANGLE_GLOBAL_VARIABLE_INIT_PREFIX +
        var.mangledName.substr((MANGLE_CANGJIE_PREFIX + MANGLE_NESTED_PREFIX).size(),
            var.mangledName.size() - (MANGLE_CANGJIE_PREFIX + MANGLE_NESTED_PREFIX + MANGLE_SUFFIX).size()) +
        MANGLE_FUNC_PARAM_TYPE_PREFIX + MANGLE_VOID_TY_SUFFIX;
}

void FlattenPatternName(const AST::Pattern& pattern, std::string& name)
{
    switch (pattern.astKind) {
        case AST::ASTKind::VAR_PATTERN:
            name += ":" + StaticCast<AST::VarPattern*>(&pattern)->varDecl->identifier;
            break;
        case AST::ASTKind::TUPLE_PATTERN: {
            auto tuplePattern = StaticCast<AST::TuplePattern*>(&pattern);
            for (auto& p : tuplePattern->patterns) {
                FlattenPatternName(*p, name);
            }
            break;
        }
        case AST::ASTKind::ENUM_PATTERN: {
            auto enumPattern = StaticCast<AST::EnumPattern*>(&pattern);
            for (auto& p : enumPattern->patterns) {
                FlattenPatternName(*p, name);
            }
            break;
        }
        case AST::ASTKind::WILDCARD_PATTERN:
            name += ":_";
            break;
        default:
            break;
    }
}

inline std::string GenerateWildcardVarIdent(const AST::Pattern& pattern)
{
    return GV_INIT_WILDCARD_PATTERN + std::to_string(pattern.begin.line) + "_" + std::to_string(pattern.begin.column);
}

std::string GetGVInitNameForPattern(const AST::VarWithPatternDecl& decl)
{
    if (decl.irrefutablePattern->astKind == AST::ASTKind::TUPLE_PATTERN) {
        std::string nameSuffix;
        FlattenPatternName(*decl.irrefutablePattern, nameSuffix);
        return GetGVInitFuncName("tuple_pattern", decl.fullPackageName) + nameSuffix;
    } else if (decl.irrefutablePattern->astKind == AST::ASTKind::ENUM_PATTERN) {
        std::string nameSuffix;
        FlattenPatternName(*decl.irrefutablePattern, nameSuffix);
        return GetGVInitFuncName("enum_pattern", decl.fullPackageName) + nameSuffix;
    } else if (decl.irrefutablePattern->astKind == AST::ASTKind::WILDCARD_PATTERN) {
        return GetGVInitFuncName(GenerateWildcardVarIdent(*decl.irrefutablePattern), decl.fullPackageName);
    } else {
        CJC_ABORT();
        return "";
    }
}

template <typename T> struct GVInit; // the primary template is never used and therefore never defined

template <> struct GVInit<AST::Package> {
    explicit GVInit(const AST::Package& pkg, const Translator&, const std::string& suffix = "")
        : srcCodeIdentifier(GetPackageInitFuncName(pkg.fullPackageName, suffix)),
          mangledName(srcCodeIdentifier),
          rawMangledName(""),
          packageName(pkg.fullPackageName)
    {
    }

public:
    // non-const, to be moved from
    std::string srcCodeIdentifier;
    std::string mangledName;
    std::string rawMangledName;
    std::string packageName;
    static constexpr Linkage LINKAGE = Linkage::EXTERNAL;
    DebugLocation loc = INVALID_LOCATION;
    bool isConst{false};
};

template <> struct GVInit<AST::File> {
    explicit GVInit(const AST::File& file, const Translator& trans, const std::string& suffix = "")
        : packageName(file.curPackage->fullPackageName),
          srcCodeIdentifier(GetFileInitFuncName(packageName, file.fileName, suffix)),
          mangledName(srcCodeIdentifier),
          rawMangledName(""),
          loc(trans.TranslateFileLocation(file.begin.fileID))
    {
    }

public:
    std::string packageName;
    std::string srcCodeIdentifier;
    std::string mangledName;
    std::string rawMangledName;
    static constexpr Linkage LINKAGE = Linkage::INTERNAL;
    DebugLocation loc;
    bool isConst{false};
};

template <> struct GVInit<AST::VarDecl> {
    explicit GVInit(const AST::VarDecl& var, const Translator& trans, const std::string& suffix = "")
        : srcCodeIdentifier("gv$_" + var.identifier + suffix),
          mangledName(GetVarInitName(var)),
          rawMangledName(var.rawMangleName),
          packageName(var.fullPackageName),
          loc(trans.TranslateLocation(var)),
          isConst(var.IsConst())
    {
    }

public:
    std::string srcCodeIdentifier;
    std::string mangledName;
    std::string rawMangledName;
    std::string packageName;
    static constexpr Linkage LINKAGE = Linkage::INTERNAL;
    DebugLocation loc;
    bool isConst;
};

template <> struct GVInit<AST::VarWithPatternDecl> {
    explicit GVInit(const AST::VarWithPatternDecl& decl, const Translator& trans)
        : srcCodeIdentifier(GetGVInitNameForPattern(decl)),
          mangledName(MANGLE_CANGJIE_PREFIX + MANGLE_GLOBAL_VARIABLE_INIT_PREFIX +
              decl.mangledName.substr((MANGLE_CANGJIE_PREFIX + MANGLE_NESTED_PREFIX).size(),
                  decl.mangledName.size() - (MANGLE_CANGJIE_PREFIX + MANGLE_NESTED_PREFIX + MANGLE_SUFFIX).size()) +
              MANGLE_FUNC_PARAM_TYPE_PREFIX + MANGLE_VOID_TY_SUFFIX),
          rawMangledName(decl.rawMangleName),
          packageName(decl.fullPackageName),
          loc(trans.TranslateLocation(decl)),
          isConst(decl.IsConst())
    {
    }

public:
    std::string srcCodeIdentifier;
    std::string mangledName;
    std::string rawMangledName;
    std::string packageName;
    static constexpr Linkage LINKAGE = Linkage::INTERNAL;
    DebugLocation loc;
    bool isConst;
};

static bool IsSimpleLiteralValue(const AST::Expr& node)
{
    auto realNode = Translator::GetDesugaredExpr(node);
    if (realNode->astKind != AST::ASTKind::LIT_CONST_EXPR) {
        return false;
    }
    switch (realNode->ty->kind) {
        case AST::TypeKind::TYPE_FLOAT16:
        case AST::TypeKind::TYPE_FLOAT64:
        case AST::TypeKind::TYPE_IDEAL_FLOAT:
        case AST::TypeKind::TYPE_FLOAT32:
        case AST::TypeKind::TYPE_UINT8:
        case AST::TypeKind::TYPE_UINT16:
        case AST::TypeKind::TYPE_UINT32:
        case AST::TypeKind::TYPE_UINT64:
        case AST::TypeKind::TYPE_UINT_NATIVE:
        case AST::TypeKind::TYPE_INT8:
        case AST::TypeKind::TYPE_INT16:
        case AST::TypeKind::TYPE_INT32:
        case AST::TypeKind::TYPE_INT64:
        case AST::TypeKind::TYPE_INT_NATIVE:
        case AST::TypeKind::TYPE_IDEAL_INT:
        case AST::TypeKind::TYPE_RUNE:
        case AST::TypeKind::TYPE_BOOLEAN:
            return true;
        case AST::TypeKind::TYPE_STRUCT:
            return node.ty->IsString();
        default:
            return false;
    }
}

static Ptr<Block> GetBlockWithInitializers(const BlockGroup& blockGroup)
{
    for (auto block : blockGroup.GetBlocks()) {
        if (block->TestAttr(Attribute::INITIALIZER)) {
            return block;
        }
    }

    CJC_ABORT(); // there is no block related to initalization process
    return nullptr;
}

bool NeedInitGlobalVarByInitFunc(const AST::VarDecl& decl)
{
    // If decl without initializer (like common let x: Int64).
    if (!decl.initializer) {
        return false;
    }
    // common/specific cannot be initialized with literal value,
    // it can change to non literal initializer in the future.
    // So, initializer function need to be generated for such variables.
    if (decl.IsCommonOrPlatform()) {
        return true;
    }
    return !(IsSimpleLiteralValue(*decl.initializer) && decl.ty == decl.initializer->ty);
}

inline bool CanInitBeGenerated(const AST::VarDeclAbstract& varDecl)
{
    return varDecl.initializer;
}

/// Looking for a Block with Attribute::INITIALIZER, remove its Exit node.
inline Ptr<Block> DropExitNodeOfInitializer(const Func& packageInit)
{
    CJC_ASSERT(packageInit.TestAttr(Attribute::INITIALIZER));
    auto packageInitBody = packageInit.GetBody();

    auto blockWithInitializers = GetBlockWithInitializers(*packageInitBody);
    // drop Exit node
    auto exit = blockWithInitializers->GetTerminator();
    exit->RemoveSelfFromBlock();

    return blockWithInitializers;
}
} // namespace

Ptr<Value> GlobalVarInitializer::GetGlobalVariable(const AST::VarDecl& decl)
{
    return globalSymbolTable.Get(decl);
}

template <typename T, typename... Args>
Ptr<Func> GlobalVarInitializer::CreateGVInitFunc(const T& node, Args&&... args) const
{
    auto context = GVInit<T>(node, trans, std::forward<Args>(args)...);
    bool isConst{false};
    // No debug location generated for const var initializer func
    // because these exprs will not be executed at runtime and falsely contribute to coverage
    if constexpr (std::is_same_v<T, AST::VarDecl>) {
        isConst = node.IsConst();
    }
    return trans.CreateEmptyGVInitFunc(std::move(context.mangledName), std::move(context.srcCodeIdentifier),
        std::move(context.rawMangledName), std::move(context.packageName), GVInit<T>::LINKAGE,
        isConst ? INVALID_LOCATION : std::move(context.loc), context.isConst);
}

Func* GlobalVarInitializer::TranslateInitializerToFunction(const AST::VarDecl& decl)
{
    auto variable = GetGlobalVariable(decl);
    auto func = CreateGVInitFunc<AST::VarDecl>(decl);
    if (auto globalVar = DynamicCast<GlobalVar>(variable)) {
        globalVar->SetInitFunc(*func);
    }
    // No debug location generated for const var initializer
    auto initNode = trans.TranslateExprArg(
        *decl.initializer, *StaticCast<RefType*>(variable->GetType())->GetBaseType());
    if (decl.IsConst()) {
        for (auto expr : trans.currentBlock->GetExpressions()) {
            expr->SetDebugLocation(INVALID_LOCATION);
        }
    }
    auto loc = decl.IsConst() ? INVALID_LOCATION : trans.TranslateLocation(decl);
    CJC_ASSERT(variable->GetType()->IsRef());
    auto expectedTy = StaticCast<RefType*>(variable->GetType())->GetBaseType();
    if (initNode->GetType() != expectedTy) {
        initNode = TypeCastOrBoxIfNeeded(*initNode, *expectedTy, builder, *trans.GetCurrentBlock(), loc, true);
    }
    trans.CreateAndAppendExpression<Store>(loc, builder.GetUnitTy(), initNode, variable, trans.GetCurrentBlock());
    auto curBlock = trans.GetCurrentBlock();
    if (curBlock->GetTerminator() == nullptr) {
        trans.CreateAndAppendTerminator<Exit>(curBlock);
    }

    return func;
}

bool GlobalVarInitializer::IsIncrementalNoChange(const AST::VarDecl& decl) const
{
    return enableIncre && !decl.toBeCompiled;
}

ImportedFunc* GlobalVarInitializer::TranslateIncrementalNoChangeVar(const AST::VarDecl& decl)
{
    GVInit<AST::VarDecl> context{decl, trans};
    auto ty = builder.GetType<FuncType>(std::vector<Type*>{}, builder.GetUnitTy());
    auto func = builder.CreateImportedVarOrFunc<ImportedFunc>(ty, std::move(context.mangledName),
        std::move(context.srcCodeIdentifier), std::move(context.rawMangledName), context.packageName);
    func->SetFuncKind(FuncKind::GLOBALVAR_INIT);
    func->Set<LinkTypeInfo>(Cangjie::Linkage::INTERNAL);
    if (decl.isConst) {
        func->EnableAttr(Attribute::CONST);
    }
    return func;
}

FuncBase* GlobalVarInitializer::TranslateSingleInitializer(const AST::VarDecl& decl)
{
    if (!CanInitBeGenerated(decl)) {
        return nullptr;
    }
    if (decl.platformImplementation) {
        return nullptr;
    }
    if (auto func = TryGetDeserialized<Func>(GetVarInitName(decl)); func) {
        if (!decl.TestAttr(AST::Attribute::PLATFORM)) {
            return func;
        }
    }
    // The variable with literal init value is hanlded in elsewhere
    if (!NeedInitGlobalVarByInitFunc(decl)) {
        return nullptr;
    }

    if (IsIncrementalNoChange(decl) && !IsSrcCodeImportedGlobalDecl(decl, opts)) {
        if (decl.identifier == STATIC_INIT_VAR) {
            // for static variables to be inited in static ctors, the special variable "$init" falls in this branch
            // (as it has a default value "false")
            return TranslateInitializerToFunction(decl);
        } else {
            // For a non-recompile variable which is not inited in `static.init` func, after the variable itself is
            // translated into a pseudo import. As for the init func, we should also create a pseudo import for it
            // and make sure the package init func will call this imported init
            return TranslateIncrementalNoChangeVar(decl);
        }
    } else {
        return TranslateInitializerToFunction(decl);
    }
}

void GlobalVarInitializer::FillGVInitFuncWithApplyAndExit(const std::vector<Ptr<Value>>& varInitFuncs)
{
    auto curBlock = trans.GetCurrentBlock();
    for (auto& func : varInitFuncs) {
        trans.GenerateFuncCall(*func, StaticCast<FuncType*>(func->GetType()), std::vector<Type*>{}, nullptr,
            std::vector<Value*>{}, INVALID_LOCATION);
    }
    trans.CreateAndAppendTerminator<Exit>(curBlock);
}

Func* GlobalVarInitializer::TranslateTupleOrEnumPatternInitializer(const AST::VarWithPatternDecl& decl)
{
    auto func = CreateGVInitFunc<AST::VarWithPatternDecl>(decl);
    if (decl.IsConst()) {
        func->SetDebugLocation(INVALID_LOCATION);
    }
    auto initNode = Translator::TranslateASTNode(*decl.initializer, trans);
    trans.FlattenVarWithPatternDecl(*decl.irrefutablePattern, initNode, false);
    if (decl.IsConst()) {
        for (auto bl : func->GetBody()->GetBlocks()) {
            for (auto expr : bl->GetExpressions()) {
                expr->SetDebugLocation(INVALID_LOCATION);
            }
        }
    }
    auto curBlock = trans.GetCurrentBlock();
    if (curBlock->GetTerminator() == nullptr) {
        trans.CreateAndAppendTerminator<Exit>(curBlock);
    }
    return func;
}

Func* GlobalVarInitializer::TranslateWildcardPatternInitializer(const AST::VarWithPatternDecl& decl)
{
    auto func = CreateGVInitFunc<AST::VarWithPatternDecl>(decl);
    Translator::TranslateASTNode(*decl.initializer, trans);
    auto curBlock = trans.GetCurrentBlock();
    if (curBlock->GetTerminator() == nullptr) {
        trans.CreateAndAppendTerminator<Exit>(curBlock);
    }

    return func;
}

Func* GlobalVarInitializer::TranslateVarWithPatternInitializer(const AST::VarWithPatternDecl& decl)
{
    switch (decl.irrefutablePattern->astKind) {
        case AST::ASTKind::TUPLE_PATTERN:
        case AST::ASTKind::ENUM_PATTERN:
            return TranslateTupleOrEnumPatternInitializer(decl);
        case AST::ASTKind::WILDCARD_PATTERN: {
            return TranslateWildcardPatternInitializer(decl);
        }
        default:
            CJC_ABORT();
            return nullptr;
    }
}

FuncBase* GlobalVarInitializer::TranslateVarInit(const AST::Decl& var)
{
    if (auto vd = DynamicCast<const AST::VarDecl*>(&var)) {
        return TranslateSingleInitializer(*vd);
    } else if (auto vwpd = DynamicCast<const AST::VarWithPatternDecl*>(&var)) {
        return TranslateVarWithPatternInitializer(*vwpd);
    } else {
        CJC_ABORT();
        return nullptr;
    }
}

Ptr<Func> GlobalVarInitializer::TryGetFileInitialializer(const AST::File& file, const std::string& suffix)
{
    auto initializerName = GetFileInitFuncName(file.curPackage->fullPackageName, file.fileName, suffix);

    return TryGetDeserialized<Func>(initializerName);
}

void GlobalVarInitializer::RemoveInitializerForVarDecl(const AST::VarDecl& varDecl, Func& fileInit) const
{
    auto declInitName = "@" + GetVarInitName(varDecl);

    auto blockWithInitializers = fileInit.GetBody()->GetBlocks().back();
    Expression* init = nullptr;
    for (auto expr : blockWithInitializers->GetExpressions()) {
        auto kind = expr->GetExprKind();
        if (kind != ExprKind::APPLY) {
            continue;
        }

        auto apply = StaticCast<const Apply*>(expr);
        auto initFunc = apply->GetCallee();
        CJC_ASSERT(initFunc->TestAttr(Attribute::INITIALIZER));
        auto applyCalleeName = initFunc->GetIdentifier();
        if (applyCalleeName == declInitName) {
            init = expr;
            break;
        }
    }

    // if it is in this initializer
    if (init) {
        init->RemoveSelfFromBlock();
    }
}

void GlobalVarInitializer::RemoveCommonInitializersReplacedWithPlatform(
    Func& fileInit, const std::vector<Ptr<const AST::Decl>>& decls) const
{
    for (auto decl : decls) {
        if (decl->IsCommonMatchedWithPlatform()) {
            CJC_ASSERT(decl->astKind == AST::ASTKind::VAR_DECL);
            const AST::VarDecl& varDecl = StaticCast<const AST::VarDecl>(*decl);

            RemoveInitializerForVarDecl(varDecl, fileInit);
        }
    }
}

Ptr<Func> GlobalVarInitializer::TranslateFileInitializer(
    const AST::File& file, const std::vector<Ptr<const AST::Decl>>& decls)
{
    auto fileInit = TryGetFileInitialializer(file);
    if (fileInit) {
        CJC_ASSERT(fileInit->TestAttr(Attribute::INITIALIZER));

        RemoveCommonInitializersReplacedWithPlatform(*fileInit, decls);

        return fileInit;
    }

    std::vector<Ptr<Value>> varInitFuncs;
    for (auto decl : decls) {
        if (auto initFunc = TranslateVarInit(*decl)) {
            if (decl->IsConst()) {
                initFuncsForConstVar.emplace_back(initFunc);
                // In incremental compilation scenarios, only changes need to be re-evaluated.
                if (!enableIncre || decl->toBeCompiled) {
                    SetCompileTimeValueFlagRecursivly(*StaticCast<Func*>(initFunc));
                }
            }
            varInitFuncs.push_back(initFunc);
        }
    }

    auto func = CreateGVInitFunc<AST::File>(file);
    FillGVInitFuncWithApplyAndExit(varInitFuncs);
    if (varInitFuncs.empty()) {
        func->DisableAttr(Attribute::NO_INLINE);
    }

    return func;
}

bool GlobalVarInitializer::NeedVarLiteralInitFunc(const AST::Decl& decl)
{
    auto vd = DynamicCast<const AST::VarDecl*>(&decl);
    if (vd == nullptr || !vd->initializer || NeedInitGlobalVarByInitFunc(*vd)) {
        return false;
    }
    // common var with platform one should not be retranslated.
    if (decl.TestAttr(AST::Attribute::COMMON) && decl.platformImplementation) {
        return false;
    }
    // `decl` may be a wildcard, like: `var _ = 1`, doesn't need to be translated;
    // incremental no-change var does not have initialiser; they are copied from cached bc in codegen
    if (IsIncrementalNoChange(*vd)) {
        return false;
    }

    CJC_ASSERT(vd->initializer->astKind == AST::ASTKind::LIT_CONST_EXPR);
    auto litExpr = StaticCast<AST::LitConstExpr*>(vd->initializer.get());
    auto globalVar = DynamicCast<GlobalVar>(GetGlobalVariable(*vd));
    CJC_ASSERT(globalVar);
    globalVar->SetInitializer(*trans.TranslateLitConstant(*litExpr, *litExpr->ty));

    // mutable var decl need to be initialized in `file_literal`, codegen will call `file_literal` in
    // macro expand situation, immutable var decl doesn't need to
    if (!vd->isVar) {
        return false;
    }
    return true;
}

Ptr<Func> GlobalVarInitializer::TranslateFileLiteralInitializer(
    const AST::File& file, const std::vector<Ptr<const AST::Decl>>& decls)
{
    std::list<const AST::VarDecl*> varsToGenInit{};
    for (auto decl : decls) {
        if (NeedVarLiteralInitFunc(*decl)) {
            varsToGenInit.push_back(StaticCast<AST::VarDecl>(decl));
        }
    }
    // do not generate if no literal need initialisation
    if (varsToGenInit.empty()) {
        return nullptr;
    }

    // NOTE: this function is only called by CodeGen and only used for macro expand situation.
    // And only mutable primitive values need to be re-initialized.
    // Literal reset functions do not need 'NO_INLINE' attr.
    auto func = CreateGVInitFunc<AST::File>(file, "_literal");
    func->DisableAttr(Attribute::NO_INLINE);
    func->EnableAttr(Attribute::INITIALIZER);
    func->SetDebugLocation(INVALID_LOCATION);
    auto currentBlock = trans.GetCurrentBlock();
    for (auto vd : varsToGenInit) {
        auto globalVar = VirtualCast<GlobalVar*>(GetGlobalVariable(*vd));
        auto initNode = trans.TranslateExprArg(*vd->initializer);
        // this is in gv init for literal, we can't set breakpoint with cjdb, so we can't set DebugLocationInfo
        // for any expression
        initNode->SetDebugLocation(INVALID_LOCATION);
        auto expectTy = StaticCast<RefType*>(globalVar->GetType())->GetBaseType();
        if (expectTy != initNode->GetType()) {
            initNode =
                TypeCastOrBoxIfNeeded(*initNode, *expectTy, builder, *trans.GetCurrentBlock(), INVALID_LOCATION, true);
        }
        trans.CreateAndAppendExpression<Store>(builder.GetUnitTy(), initNode, globalVar, currentBlock);
    }
    trans.CreateAndAppendTerminator<Exit>(currentBlock);

    return func;
}

void GlobalVarInitializer::AddImportedPackageInit(const AST::Package& curPackage, const std::string& suffix)
{
    auto voidTy = builder.GetUnitTy();
    auto initFuncTy = builder.GetType<FuncType>(std::vector<Type*>{}, voidTy);
    for (auto& dep : importManager.GetCurImportedPackages(curPackage.fullPackageName)) {
        const std::string& pkgName = dep->srcPackage->fullPackageName;
        bool doNotCallInit = dep->srcPackage->isMacroPackage;
        if (doNotCallInit) {
            continue;
        }
        auto context = GVInit<AST::Package>(*dep->srcPackage, trans, suffix);
        // Try get deserialized one.
        ImportedFunc* initFunc = TryGetDeserialized<ImportedFunc>(context.mangledName);
        // Already be translated when compiling common part.
        if (initFunc) {
            continue;
        }
        auto attrs = AttributeInfo();
        attrs.SetAttr(Attribute::INITIALIZER, true);
        initFunc = builder.CreateImportedVarOrFunc<ImportedFunc>(
            initFuncTy, context.mangledName, context.srcCodeIdentifier, context.rawMangledName, pkgName);
        initFunc->AppendAttributeInfo(attrs);
        initFunc->EnableAttr(Attribute::PUBLIC);
        trans.GenerateFuncCall(*initFunc, StaticCast<FuncType*>(initFunc->GetType()),
            std::vector<Type*>{}, nullptr, std::vector<Value*>{}, INVALID_LOCATION);
    }
}

// [CJMP]: define inlining policy here and in other places after disabling CHIR transformations of common part
Ptr<Func> GlobalVarInitializer::CreateImportsInitFunc(const AST::Package& curPackage, const std::string& suffix)
{
    auto importsInitFunc = CreateGVInitFunc<AST::Package>(curPackage, suffix + "_importsInit");
    importsInitFunc->Set<LinkTypeInfo>(Linkage::INTERNAL);
    auto curBlockGroup = importsInitFunc->GetBody();
    auto curBlock = curBlockGroup->GetEntryBlock();
    curBlock->EnableAttr(Attribute::INITIALIZER);
    trans.CreateAndAppendTerminator<Exit>(curBlock);

    return importsInitFunc;
}

Ptr<Func> GlobalVarInitializer::GetImportsInitFunc(const AST::Package& curPackage, const std::string& suffix)
{
    auto initializerName = GetPackageInitFuncName(curPackage.fullPackageName, suffix + "_importsInit");
    auto importsInitFunc = TryGetDeserialized<Func>(initializerName);
    CJC_ASSERT(importsInitFunc);

    return importsInitFunc;
}

namespace {
bool DoNotGenerateInitForImport(AST::PackageDecl& dep)
{
    bool doNotCallInit = dep.srcPackage->isMacroPackage;
#ifdef CANGJIE_CODEGEN_CJVM_BACKEND
    const std::string& pkgName = dep.srcPackage->fullPackageName;
    doNotCallInit =
        doNotCallInit || Utils::IsJava8Module(pkgName) || dep.srcPackage->TestAttr(AST::Attribute::TOOL_ADD);
#endif

    return doNotCallInit;
}
} // namespace

void GlobalVarInitializer::UpdateImportsInit(
    const AST::Package& curPackage, Func& importsInitFunc, const std::string& suffix)
{
    Block* curBlock = DropExitNodeOfInitializer(importsInitFunc);
    trans.SetCurrentBlock(*curBlock);

    for (auto& dep : importManager.GetCurImportedPackages(curPackage.fullPackageName)) {
        if (DoNotGenerateInitForImport(*dep)) {
            continue;
        }

        auto context = GVInit<AST::Package>(*dep->srcPackage, trans, suffix);
        if (auto initFunc = TryGetDeserialized<ImportedFunc>(context.mangledName); initFunc) {
            continue;
        }

        // Creating import initializer
        auto initFuncTy = builder.GetType<FuncType>(std::vector<Type*>{}, builder.GetUnitTy());

        auto attrs = AttributeInfo();
        attrs.SetAttr(Attribute::INITIALIZER, true);
        auto importInit = builder.CreateImportedVarOrFunc<ImportedFunc>(initFuncTy, context.mangledName,
            context.srcCodeIdentifier, context.rawMangledName, dep->srcPackage->fullPackageName);
        importInit->AppendAttributeInfo(attrs);
        importInit->SetFuncKind(FuncKind::GLOBALVAR_INIT);
        importInit->EnableAttr(Attribute::PUBLIC);

        InsertInitializerIntoPackageInitializer(*importInit, importsInitFunc);
    }

    trans.CreateAndAppendTerminator<Exit>(curBlock);
}

void GlobalVarInitializer::AddGenericInstantiatedInit()
{
    auto callContext = IntrisicCallContext{.kind = IntrinsicKind::PREINITIALIZE};
    trans.CreateAndAppendExpression<Intrinsic>(builder.GetUnitTy(), callContext, trans.GetCurrentBlock());
}

Ptr<Func> GlobalVarInitializer::GeneratePackageInitBase(const AST::Package& curPackage, const std::string& suffix)
{
    /*  var initFlag: Bool = false
        func pkg_init_suffix()
        {
            if (initFlag) {
                return
            }
            initFlag = true
            apply all imported package init_suffix funcs
            apply all current package file init_suffix funcs
        }
    */
    auto func = CreateGVInitFunc<AST::Package>(curPackage, suffix);

    // 1. Create global variable as guard condition.
    auto boolTy = builder.GetBoolTy();
    auto initFlagName = suffix.empty() ? GV_PKG_INIT_ONCE_FLAG : "has_invoked_pkg_init_literal";
    GlobalVar* initFlag = TryGetDeserialized<GlobalVar>(initFlagName);
    if (!initFlag) {
        initFlag = builder.CreateGlobalVar(
            INVALID_LOCATION, builder.GetType<RefType>(boolTy), initFlagName, initFlagName, "", func->GetPackageName());
        initFlag->SetInitializer(*builder.CreateLiteralValue<BoolLiteral>(boolTy, false));
        initFlag->EnableAttr(Attribute::NO_REFLECT_INFO);
        initFlag->EnableAttr(Attribute::COMPILER_ADD);
        initFlag->EnableAttr(Attribute::INITIALIZER);
        initFlag->EnableAttr(Attribute::NO_DEBUG_INFO);
        initFlag->Set<LinkTypeInfo>(Linkage::INTERNAL);
    }

    // 2. Add `if (initFlag) { return }`
    auto curBlockGroup = func->GetBody();
    auto curBlock = curBlockGroup->GetEntryBlock();
    auto flagRef = trans.CreateAndAppendExpression<Load>(boolTy, initFlag, curBlock)->GetResult();

    auto returnBlock = builder.CreateBlock(curBlockGroup);
    auto applyInitFuncBlock = builder.CreateBlock(curBlockGroup);
    trans.CreateAndAppendTerminator<Branch>(flagRef, returnBlock, applyInitFuncBlock, curBlock);

    // if has initialized, return
    trans.CreateAndAppendTerminator<Exit>(returnBlock);

    // 3. set `initFlag` true
    trans.SetCurrentBlock(*applyInitFuncBlock);
    auto unitTy = builder.GetUnitTy();
    auto trueLit = trans.CreateAndAppendConstantExpression<BoolLiteral>(boolTy, *applyInitFuncBlock, true)->GetResult();
    trans.CreateAndAppendExpression<Store>(unitTy, trueLit, initFlag, applyInitFuncBlock);
    return func;
}

static Ptr<Apply> FindApplyIn(const Block& block, FuncBase& applyCallee)
{
    auto expressions = block.GetExpressions();
    for (auto expression : expressions) {
        if (auto apply = DynamicCast<Apply>(expression)) {
            if (apply->GetCallee() == &applyCallee) {
                return apply;
            }
        }
    }

    CJC_ABORT();
    return nullptr;
}

void GlobalVarInitializer::InsertInitializerIntoPackageInitializer(FuncBase& init, Func& packageInit)
{
    auto packageInitBody = packageInit.GetBody();
    auto blockWithInitializers = GetBlockWithInitializers(*packageInitBody);

    if (init.TestAttr(Attribute::DESERIALIZED) && packageInit.TestAttr(Attribute::DESERIALIZED)) {
        // It was inserted at previous compilation phase ==>

        auto initCallExpr = FindApplyIn(*blockWithInitializers, init);
        auto lastExpr = blockWithInitializers->GetExpressions().back();
        if (initCallExpr != lastExpr) {
            // ==> need to push to the end
            initCallExpr->MoveAfter(lastExpr);
        }
        return;
    }

    trans.SetCurrentBlock(*blockWithInitializers);

    trans.GenerateFuncCall(init, StaticCast<FuncType*>(init.GetType()), std::vector<Type*>{}, nullptr,
        std::vector<Value*>{}, INVALID_LOCATION);
}

inline std::pair<Func*, Block*> GlobalVarInitializer::PreparePackageInit(const AST::Package& curPackage)
{
    // create/use deserialized base of package initializer function.
    Func* packageInit = builder.GetCurPackage()->GetPackageInitFunc();
    Block* curBlock;
    if (packageInit) {
        curBlock = DropExitNodeOfInitializer(*packageInit);
        auto importsInitFunc = GetImportsInitFunc(curPackage);
        UpdateImportsInit(curPackage, *importsInitFunc);
        trans.SetCurrentBlock(*curBlock);
    } else {
        packageInit = GeneratePackageInitBase(curPackage).get();
        builder.GetCurPackage()->SetPackageInitFunc(packageInit);
        curBlock = trans.GetCurrentBlock();
        // Mark the block to find it later for modification on the next compilations.
        curBlock->EnableAttr(Attribute::INITIALIZER);

        // add apply of imported package init function.
        auto importsInitFunc = CreateImportsInitFunc(curPackage);
        InsertInitializerIntoPackageInitializer(*importsInitFunc, *packageInit);
        UpdateImportsInit(curPackage, *importsInitFunc);
        trans.SetCurrentBlock(*curBlock);
        // add apply of static generic instantiated init
        AddGenericInstantiatedInit();
    }

    return std::make_pair(packageInit, curBlock);
}

/// Generate package initializer - the function that calls initializers of files
/// in order that was defined and verified in previous stages.
/// Package initalizer also call initializer of dependent package.
void GlobalVarInitializer::CreatePackageInit(const AST::Package& curPackage, const InitOrder& initOrder)
{
    auto [packageInit, curBlock] = PreparePackageInit(curPackage);

    for (auto& fileAndVars : initOrder) {
        // maybe there isn't global var decl in one file, in this case, we don't generate file init func
        if (!fileAndVars.second.empty() || enableIncre) {
            if (!fileAndVars.second.empty() && IsSymbolImportedDecl(*fileAndVars.second[0], opts)) {
                continue;
            }
            // only generate file init for this package, not imported files
            if (auto fileInit = TranslateFileInitializer(*fileAndVars.first, fileAndVars.second)) {
                InsertInitializerIntoPackageInitializer(*fileInit, *packageInit);
            }
        }
    }

    InsertAnnotationVarInitInto(*packageInit);

    trans.CreateAndAppendTerminator<Exit>(curBlock);
}

inline std::pair<Func*, Block*> GlobalVarInitializer::PreparePackageLiteralInit(const AST::Package& curPackage)
{
    // create/use deserialized base of package initializer function.
    Func* packageLiteralInit = builder.GetCurPackage()->GetPackageLiteralInitFunc();
    Block* curBlock;
    if (packageLiteralInit) {
        curBlock = DropExitNodeOfInitializer(*packageLiteralInit);
        auto importsInitFunc = GetImportsInitFunc(curPackage, "_literal");
        UpdateImportsInit(curPackage, *importsInitFunc, "_literal");
        trans.SetCurrentBlock(*curBlock);
    } else {
        // create base of package init literal function.
        packageLiteralInit = GeneratePackageInitBase(curPackage, "_literal");
        builder.GetCurPackage()->SetPackageLiteralInitFunc(packageLiteralInit);
        curBlock = trans.GetCurrentBlock();
        curBlock->EnableAttr(Attribute::INITIALIZER);
        auto importsInitFunc = CreateImportsInitFunc(curPackage, "_literal");
        InsertInitializerIntoPackageInitializer(*importsInitFunc, *packageLiteralInit);
        UpdateImportsInit(curPackage, *importsInitFunc, "_literal");
        trans.SetCurrentBlock(*curBlock);
    }

    return std::make_pair(packageLiteralInit, curBlock);
}

/// Generate package literal values initializer
void GlobalVarInitializer::CreatePackageLiteralInit(const AST::Package& curPackage, const InitOrder& initOrder)
{
    // NOTE: this function is only called by CodeGen and only used for macro expand situation.
    auto [packageLiteralInit, curBlock] = PreparePackageLiteralInit(curPackage);

    for (auto& fileAndVars : initOrder) {
        // maybe there isn't global var decl in one file, in this case, we don't generate file init func
        if (!fileAndVars.second.empty()) {
            if (fileAndVars.second[0]->TestAttr(AST::Attribute::IMPORTED)) {
                for (auto var : fileAndVars.second) {
                    if (var->IsCommonOrPlatform()) {
                        // They are always initialized via init functions in corresponding file init,
                        // because e.g. it can be literal in common and not literal in platform,
                        // and therefore they are not inlined in importing package.
                        continue;
                    }

                    if (NeedVarLiteralInitFunc(*var)) {
                        auto vd = StaticCast<AST::VarDecl>(var);
                        auto globalVar = VirtualCast<GlobalVar>(GetGlobalVariable(*vd));
                        auto initNode = Translator::TranslateASTNode(*vd->initializer, trans);
                        initNode->SetDebugLocation(INVALID_LOCATION);
                        trans.CreateAndAppendExpression<Store>(builder.GetUnitTy(), initNode, globalVar, curBlock);
                    }
                }
                continue;
            }
            if (auto fileLiteralInit = TranslateFileLiteralInitializer(*fileAndVars.first, fileAndVars.second)) {
                InsertInitializerIntoPackageInitializer(*fileLiteralInit, *packageLiteralInit);
            }
        }
    }

    trans.CreateAndAppendTerminator<Exit>(curBlock);
}

void GlobalVarInitializer::Run(const AST::Package& pkg, const InitOrder& initOrder)
{
    CreatePackageInit(pkg, initOrder);
    CreatePackageLiteralInit(pkg, initOrder);
}
