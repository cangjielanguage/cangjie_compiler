// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/GlobalVarInitializer.h"

#include "cangjie/AST/Node.h"
#include "cangjie/CHIR/Annotation.h"
#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Utils.h"

using namespace Cangjie;
using namespace CHIR;

namespace {
inline std::string GetPackageInitFuncName(const std::string& pkgName, const std::string& suffix = "")
{
    auto specialName = suffix.empty() ? SPECIAL_NAME_FOR_INIT_FUNCTION : SPECIAL_NAME_FOR_INIT_LITERAL_FUNCTION;
    return MANGLE_CANGJIE_PREFIX + MANGLE_GLOBAL_PACKAGE_INIT_PREFIX + MangleUtils::GetOptPkgName(pkgName) +
        specialName + MANGLE_FUNC_PARAM_TYPE_PREFIX + MANGLE_VOID_TY_SUFFIX;
}

inline std::string GetFileInitFuncName(
    const std::string& pkgName, const std::string& fileName, const std::string& suffix)
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
          srcCodeIdentifier(
              GetFileInitFuncName(packageName, file.fileName, suffix)),
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
          mangledName(MANGLE_CANGJIE_PREFIX + MANGLE_GLOBAL_VARIABLE_INIT_PREFIX +
              var.mangledName.substr((MANGLE_CANGJIE_PREFIX + MANGLE_NESTED_PREFIX).size(),
                  var.mangledName.size() - (MANGLE_CANGJIE_PREFIX + MANGLE_NESTED_PREFIX + MANGLE_SUFFIX).size()) +
                      MANGLE_FUNC_PARAM_TYPE_PREFIX + MANGLE_VOID_TY_SUFFIX),
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
} // namespace

Ptr<Value> GlobalVarInitializer::GetGlobalVariable(const AST::VarDecl& decl)
{
    auto var = globalSymbolTable.Get(decl);
    return var;
}

template <typename T, typename... Args>
Ptr<Func> GlobalVarInitializer::CreateGVInitFunc(const T& node, Args&&... args) const
{
    auto context = GVInit<T>(node, trans, std::forward<Args>(args)...);
    return trans.CreateEmptyGVInitFunc(std::move(context.mangledName), std::move(context.srcCodeIdentifier),
        std::move(context.rawMangledName), std::move(context.packageName), GVInit<T>::LINKAGE, std::move(context.loc),
        context.isConst);
}

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

static bool NeedInitGlobalVarByInitFunc(const AST::VarDecl& decl)
{
    return !(IsSimpleLiteralValue(*decl.initializer) && decl.ty == decl.initializer->ty);
}

Func* GlobalVarInitializer::TranslateInitializerToFunction(const AST::VarDecl& decl)
{
    auto variable = GetGlobalVariable(decl);
    auto func = CreateGVInitFunc<AST::VarDecl>(decl);
    if (auto globalVar = DynamicCast<GlobalVar>(variable)) {
        globalVar->SetInitFunc(*func);
    }
    auto initNode = trans.TranslateExprArg(
        *decl.initializer, *StaticCast<RefType*>(variable->GetType())->GetBaseType());
    auto loc = trans.TranslateLocation(decl);
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
    auto ty = builder.GetType<FuncType>(std::vector<Type*>{}, builder.GetVoidTy());
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
        trans.GenerateFuncCall(*func, StaticCast<FuncType*>(func->GetType()), std::vector<Type*>{},
            nullptr, std::vector<Value*>{}, INVALID_LOCATION);
    }
    trans.CreateAndAppendTerminator<Exit>(curBlock);
}

Func* GlobalVarInitializer::TranslateTupleOrEnumPatternInitializer(const AST::VarWithPatternDecl& decl)
{
    auto func = CreateGVInitFunc<AST::VarWithPatternDecl>(decl);
    auto initNode = Translator::TranslateASTNode(*decl.initializer, trans);
    trans.FlattenVarWithPatternDecl(*decl.irrefutablePattern, initNode, false);
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

Ptr<Func> GlobalVarInitializer::TranslateFileInitializer(
    const AST::File& file, const std::vector<Ptr<const AST::Decl>>& decls)
{
    std::vector<Ptr<Value>> varInitFuncs;
    for (auto decl : decls) {
        if (auto initFunc = TranslateVarInit(*decl)) {
            if (decl->IsConst()) {
                initFuncsForConstVar.emplace_back(initFunc);
                SetCompileTimeValueFlagRecursivly(*StaticCast<Func*>(initFunc));
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
    if (vd == nullptr || NeedInitGlobalVarByInitFunc(*vd)) {
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
    func->SetFuncKind(FuncKind::DEFAULT);
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
            initNode = TypeCastOrBoxIfNeeded(
                *initNode, *expectTy, builder, *trans.GetCurrentBlock(), INVALID_LOCATION, true);
        }
        trans.CreateAndAppendExpression<Store>(builder.GetUnitTy(), initNode, globalVar, currentBlock);
    }
    trans.CreateAndAppendTerminator<Exit>(currentBlock);

    return func;
}

void GlobalVarInitializer::AddImportedPackageInit(const AST::Package& curPackage, const std::string& suffix)
{
    auto voidTy = builder.GetVoidTy();
    auto initFuncTy = builder.GetType<FuncType>(std::vector<Type*>{}, voidTy);
    for (auto& dep : importManager.GetCurImportedPackages(curPackage.fullPackageName)) {
        const std::string& pkgName = dep->srcPackage->fullPackageName;
        bool doNotCallInit = dep->srcPackage->isMacroPackage;
        if (doNotCallInit) {
            continue;
        }
        auto context = GVInit<AST::Package>(*dep->srcPackage, trans, suffix);
        auto initFunc = builder.CreateImportedVarOrFunc<ImportedFunc>(
            initFuncTy, context.mangledName, context.srcCodeIdentifier, context.rawMangledName, pkgName);
        initFunc->EnableAttr(Attribute::PUBLIC);
        trans.GenerateFuncCall(*initFunc, StaticCast<FuncType*>(initFunc->GetType()),
            std::vector<Type*>{}, nullptr, std::vector<Value*>{}, INVALID_LOCATION);
    }
}

void GlobalVarInitializer::AddGenericInstantiatedInit()
{
    auto callContext = IntrisicCallContext {
        .kind = IntrinsicKind::PREINITIALIZE
    };
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
    auto initFlag = builder.CreateGlobalVar(
        INVALID_LOCATION, builder.GetType<RefType>(boolTy), initFlagName, initFlagName, "", func->GetPackageName());
    initFlag->SetInitializer(*builder.CreateLiteralValue<BoolLiteral>(boolTy, false));
    initFlag->EnableAttr(Attribute::NO_REFLECT_INFO);
    initFlag->EnableAttr(Attribute::COMPILER_ADD);
    initFlag->EnableAttr(Attribute::NO_DEBUG_INFO);
    initFlag->Set<LinkTypeInfo>(Linkage::INTERNAL);

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

void GlobalVarInitializer::CreatePackageInitFunc(const AST::Package& curPackage,
    [[maybe_unused]] const std::vector<Ptr<Value>>& importedVarInits, std::vector<Ptr<Value>>& initFuncs)
{
    // 1. create base of package init function.
    auto pkgInit = GeneratePackageInitBase(curPackage);
    builder.GetCurPackage()->SetPackageInitFunc(pkgInit);
    // 2. add apply of imported package init function.
    AddImportedPackageInit(curPackage);
    // 3. add apply of static generic instantiated init
    AddGenericInstantiatedInit();
    // 4. insert annotation init functions.
    InsertAnnotationVarInit(initFuncs);
    // 5. add apply of current package init function.
    FillGVInitFuncWithApplyAndExit(initFuncs);
}

void GlobalVarInitializer::CreatePackageLiteralInitFunc(const AST::Package& curPackage,
    const std::vector<Ptr<const AST::VarDecl>>& importedVars, const std::vector<Ptr<Value>>& literalInitFuncs)
{
    // NOTE: this function is only called by CodeGen and only used for macro expand situation.
    // 1. create base of package init literal function.
    GeneratePackageInitBase(curPackage, "_literal");
    // 2. add init of source imported vars
    auto currentBlock = trans.GetCurrentBlock();
    for (auto vd : importedVars) {
        auto globalVar = DynamicCast<GlobalVar>(GetGlobalVariable(*vd));
        auto initNode = Translator::TranslateASTNode(*vd->initializer, trans);
        // this is in gv init for literal, we can't set breakpoint with cjdb, so we can't set DebugLocationInfo
        // for any expression
        initNode->SetDebugLocation(INVALID_LOCATION);
        trans.CreateAndAppendExpression<Store>(builder.GetUnitTy(), initNode, globalVar, currentBlock);
    }
    // 2. apply imported package literal init function.
    AddImportedPackageInit(curPackage, "_literal");
    // 3. add apply of current package init literal function. NOTE: only mutable global primitive type variables.
    FillGVInitFuncWithApplyAndExit(literalInitFuncs);
}

void GlobalVarInitializer::Run(const AST::Package& pkg, const InitOrder& initOrder)
{
    std::vector<Ptr<Value>> importedInitFuncs;
    std::vector<Ptr<Value>> fileInitFuncs;
    for (auto& fileAndVars : initOrder) {
        // maybe there isn't global var decl in one file, in this case, we don't generate file init func
        if (!fileAndVars.second.empty() || enableIncre) {
            if (!fileAndVars.second.empty() && IsSymbolImportedDecl(*fileAndVars.second[0], opts)) {
                for (auto var : fileAndVars.second) {
                    auto init = TranslateVarInit(*var);
                    if (!init) {
                        continue;
                    }
                    importedInitFuncs.emplace_back(init);
                }
                continue;
            }
            // only generate file init for this package, not imported files
            if (auto func = TranslateFileInitializer(*fileAndVars.first, fileAndVars.second)) {
                fileInitFuncs.emplace_back(func);
            }
        }
    }
    CreatePackageInitFunc(pkg, importedInitFuncs, fileInitFuncs);

    // Generate init for the GVs which have simple literal value. Additionally, for these GVs, we also
    // generate a special re-init function which will be used in macro scenario
    std::vector<Ptr<Value>> literalInitFuncs;
    std::vector<Ptr<const AST::VarDecl>> importedVars;
    for (auto& fileAndVars : initOrder) {
        // maybe there isn't global var decl in one file, in this case, we don't generate file init func
        if (!fileAndVars.second.empty()) {
            if (fileAndVars.second[0]->TestAttr(AST::Attribute::IMPORTED)) {
                for (auto var : fileAndVars.second) {
                    if (NeedVarLiteralInitFunc(*var)) {
                        importedVars.emplace_back(StaticCast<AST::VarDecl>(var));
                    }
                }
                continue;
            }
            if (auto func = TranslateFileLiteralInitializer(*fileAndVars.first, fileAndVars.second)) {
                literalInitFuncs.emplace_back(func);
            }
        }
    }
    CreatePackageLiteralInitFunc(pkg, importedVars, literalInitFuncs);
}
