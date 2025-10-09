// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/AST2CHIR.h"

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/CHIR/AST2CHIR/AST2CHIRChecker.h"
#include "cangjie/CHIR/AST2CHIR/CollectLocalConstDecl/CollectLocalConstDecl.h"
#include "cangjie/CHIR/AST2CHIR/GlobalDeclAnalysis.h"
#include "cangjie/CHIR/AST2CHIR/GlobalVarInitializer.h"
#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"
#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/CHIR/Visitor/Visitor.h"
#include "cangjie/CHIR/Serializer/CHIRDeserializer.h"
#include "cangjie/Utils/ConstantsUtils.h"
#include "cangjie/Utils/ParallelUtil.h"

namespace Cangjie::CHIR {
bool AST2CHIR::HasFailed() const
{
    return this->failure;
}

void AST2CHIR::RegisterAllSources()
{
    auto& sources = sourceManager.GetSources();
    for (auto& source : sources) {
        auto filePath = source.path;
        auto absPath = FileUtil::GetAbsPath(source.path);
        if (absPath.has_value()) {
            filePath = absPath.value();
        } else {
            filePath = FileUtil::Normalize(filePath);
        }
        builder.GetChirContext().RegisterSourceFileName(source.fileID, filePath);
    }
}

void AST2CHIR::CollectStaticInitFuncInfo()
{
    auto collectStaticInitFuncInfo = [this](const std::vector<Ptr<const AST::Decl>>& funcs) {
        for (auto& func : funcs) {
            if (!IsStaticInitializer(*func)) {
                continue;
            }
            // Get all the static member variables which are inited by this `static.init` func
            std::vector<Ptr<const AST::VarDecl>> staticVars;
            Ptr<const AST::VarDecl> staticInitVar = nullptr;
            auto parentDecl = StaticCast<const AST::InheritableDecl*>(func->outerDecl);
            for (auto memberVar : GetStaticMemberVars(*parentDecl)) {
                // Skip the generated class_static_init variable
                if (memberVar->identifier == STATIC_INIT_VAR) {
                    staticInitVar = memberVar;
                    continue;
                }
                // Skip the variable which has default init value
                if (memberVar->initializer != nullptr) {
                    continue;
                }
                staticVars.emplace_back(memberVar);
                varsInitedByStaticInitFunc.emplace(memberVar);
            }
            CJC_NULLPTR_CHECK(func->outerDecl);
            staticInitFuncInfoMap.emplace(
                func->outerDecl, StaticInitInfo(StaticCast<AST::FuncDecl*>(func), staticInitVar, staticVars));
        }
    };
    collectStaticInitFuncInfo(globalAndMemberFuncs);
}

void AST2CHIR::CollectFuncsAndVars()
{
    auto collectVars = [this](const std::vector<Ptr<const AST::Decl>>& candidateVars) {
        for (auto& var : candidateVars) {
            // Skip the static variables which are inited by the `static init func`
            if (var->TestAttr(AST::Attribute::STATIC) && var->identifier != STATIC_INIT_VAR) {
                bool fromCommon = var->TestAttr(AST::Attribute::FROM_COMMON_PART);
                bool initedInStaticInit = varsInitedByStaticInitFunc.find(var) != varsInitedByStaticInitFunc.end();
                if (!fromCommon && initedInStaticInit) {
                    continue;
                }
            }
            funcsAndVars.AddElement(var);
            fileAndVarMap[var->curFile].emplace_back(var);
        }
    };
    collectVars(globalAndStaticVars);

    auto collectFuncs = [this](const std::vector<Ptr<const AST::Decl>>& candidateFuncs) {
        for (auto& func : candidateFuncs) {
            if (func->TestAttr(AST::Attribute::MAIN_ENTRY) || func->identifier == MAIN_INVOKE) {
                continue;
            }
            funcsAndVars.AddElement(func);
        }
    };
    collectFuncs(globalAndMemberFuncs);
}

void AST2CHIR::CollectLocalConstFuncAndVars(const AST::Package& pkg)
{
    CollectLocalConstDecl collector;
    collector.Collect(globalAndStaticVars, true);
    collector.Collect(globalAndMemberFuncs, true);

    /*  some const decl in member var can't be collected by visiting `init` func
        class CA {
            var b = { => const a = 1 }
        }
        member var `b` has default value, its default value doesn't show in CA.init func
    */
    std::vector<Ptr<const AST::Decl>> instanceMemberVars;
    for (auto& decl : nominalDecls) {
        for (auto member : decl->GetMemberDeclPtrs()) {
            if (member->astKind == AST::ASTKind::VAR_DECL && !member->TestAttr(AST::Attribute::STATIC)) {
                instanceMemberVars.emplace_back(member);
            }
        }
    }
    collector.Collect(instanceMemberVars, true);

    /*  some local func decl can't be collected by visiting its outer decl
        func foo() {
            const func goo<T>() {}
            goo<Int32>()
        }
        we can only collect goo's generic declare by visiting func foo, goo's instantiated declare can't be seen
        goo's instantiated declare can only be seen in `pkg.genericInstantiatedDecls`
    */
    std::vector<Ptr<const AST::Decl>> instLocalconstFuncs;
    for (auto& decl : pkg.genericInstantiatedDecls) {
        // we only collect local const func decl
        if (decl->astKind == AST::ASTKind::FUNC_DECL && decl->IsConst() &&
            IsLocalFunc(*StaticCast<AST::FuncDecl*>(decl.get()))) {
            instLocalconstFuncs.emplace_back(StaticCast<AST::FuncDecl*>(decl.get()));
        }
    }
    collector.Collect(instLocalconstFuncs, false);

    for (const auto decl : collector.GetLocalConstFuncDecls()) {
        localConstFuncs.AddElement(decl);
    }
    for (const auto decl : collector.GetLocalConstVarDecls()) {
        localConstVars.AddElement(decl);
    }
}

std::pair<InitOrder, bool> AST2CHIR::SortGlobalVarDecl(const AST::Package& pkg)
{
    Utils::ProfileRecorder recorder("AST to CHIR Translation", "SortGlobalVarDecl");

    CollectStaticInitFuncInfo();
    CollectFuncsAndVars();
    CollectLocalConstFuncAndVars(pkg);

    ElementList<Ptr<const AST::Decl>> nodesWithDeps;
    for (auto element : funcsAndVars.stableOrderValue) {
        // We will skip those non-recompiled decls in incremental compilation cause they will have no body to be
        // analyze, but the desugar generated `$init` should be kept specially cause it has body
        if (kind == IncreKind::INCR && !element->toBeCompiled && element->identifier != STATIC_INIT_VAR) {
            continue;
        }
        nodesWithDeps.AddElement(element);
    }

    auto analysis = GlobalDeclAnalysis(
        diag, gim, kind, funcsAndVars, localConstVars, staticInitFuncInfoMap, outputCHIR, mergingPlatform);
    auto initOrder = analysis.Run(nodesWithDeps, fileAndVarMap, cachedInfo);

    // Speically, now we want to flattern the local const VarWithPattern decl which will be useful in translation later.
    // Also we need to set these local const vars with `Recompile` flag for the sake of incremental compilation.
    ElementList<Ptr<const AST::Decl>> flatternedLocalConstVars;
    for (auto& element : localConstVars.stableOrderValue) {
        if (element->astKind == AST::ASTKind::VAR_DECL) {
            (const_cast<AST::Decl*>(element.get()))->toBeCompiled = true;
            flatternedLocalConstVars.AddElement(element);
        } else if (auto varWithPattern = DynamicCast<const AST::VarWithPatternDecl*>(element)) {
            (const_cast<AST::VarWithPatternDecl*>(varWithPattern))->toBeCompiled = true;
            auto allPatterns = FlattenVarWithPatternDecl(*varWithPattern);
            for (auto pattern : allPatterns) {
                if (pattern->astKind != AST::ASTKind::VAR_PATTERN) {
                    continue;
                }
                auto varPattern = StaticCast<AST::VarPattern*>(pattern);
                varPattern->varDecl->toBeCompiled = true;
                flatternedLocalConstVars.AddElement(varPattern->varDecl);
            }
        }
    }
    localConstVars = std::move(flatternedLocalConstVars);

    // If we find circular dependency, return false
    bool result = diag.GetErrorCount() == 0;
    return std::make_pair(initOrder, result);
}

void AST2CHIR::CreateGlobalVarSignature(const std::vector<Ptr<const AST::Decl>>& decls, bool isLocalConst)
{
    for (auto& decl : decls) {
        CJC_ASSERT(decl->astKind == AST::ASTKind::VAR_DECL || decl->astKind == AST::ASTKind::VAR_WITH_PATTERN_DECL);
        if (decl->astKind == AST::ASTKind::VAR_DECL) {
            CreateAndCacheGlobalVar(*StaticCast<const AST::VarDecl*>(decl), isLocalConst);
        } else if (decl->astKind == AST::ASTKind::VAR_WITH_PATTERN_DECL) {
            FlatternPattern(*(StaticCast<const AST::VarWithPatternDecl*>(decl)->irrefutablePattern), isLocalConst);
        }
    }
}

void AST2CHIR::FlatternPattern(const AST::Pattern& pattern, bool isLocalConst)
{
    switch (pattern.astKind) {
        case AST::ASTKind::VAR_PATTERN: {
            auto varPattern = StaticCast<const AST::VarPattern*>(&pattern);
            auto varDecl = varPattern->varDecl.get();
            CreateAndCacheGlobalVar(*varDecl, isLocalConst);
            break;
        }
        case AST::ASTKind::TUPLE_PATTERN: {
            auto tuplePattern = StaticCast<const AST::TuplePattern*>(&pattern);
            for (auto& subPattern : tuplePattern->patterns) {
                FlatternPattern(*subPattern, isLocalConst);
            }
            break;
        }
        case AST::ASTKind::ENUM_PATTERN: {
            auto enumPattern = StaticCast<const AST::EnumPattern*>(&pattern);
            for (auto& subPattern : enumPattern->patterns) {
                FlatternPattern(*subPattern, isLocalConst);
            }
            break;
        }
        case AST::ASTKind::WILDCARD_PATTERN: {
            break;
        }
        default: {
            Errorln("decl with unsupported pattern");
            CJC_ABORT();
        }
    }
}

void AST2CHIR::SetInitFuncForStaticVar()
{
    for (auto& skipedStaticVar : varsInitedByStaticInitFunc) {
        // If common var with platform one, need to skip
        auto skipedStaticVarVal = globalCache.TryGet(*skipedStaticVar);
        if (skipedStaticVarVal == nullptr || skipedStaticVar->platformImplementation) {
            continue;
        }
        CJC_ASSERT(skipedStaticVar->astKind == AST::ASTKind::VAR_DECL);
        // Also set the init func here
        if (auto skipedStaticVarInCHIR = DynamicCast<GlobalVar*>(skipedStaticVarVal)) {
            auto staticInitFuncInAST = staticInitFuncInfoMap.at(skipedStaticVar->outerDecl).staticInitFunc;
            auto staticInitFuncInCHIR = DynamicCast<Func*>(globalCache.Get(*staticInitFuncInAST));
            CJC_NULLPTR_CHECK(staticInitFuncInCHIR);
            skipedStaticVarInCHIR->SetInitFunc(*staticInitFuncInCHIR);
        }
    }
}

Translator AST2CHIR::CreateTranslator()
{
    return Translator{builder, chirType, opts, gim, globalCache, localConstVars, localConstFuncs, kind,
        deserializedVals, annoFactoryFuncs, maybeUnreachable, isComputingAnnos, initFuncsForAnnoFactory, types};
}

void AST2CHIR::TranslateInitOfGlobalVars(const AST::Package& pkg, const InitOrder& initOrder)
{
    Utils::ProfileRecorder recorder("TranslateAllDecls", "TranslateInitOfGlobalVars");
    auto trans = CreateTranslator();
    GlobalVarInitializer initializer(trans, importManager, initFuncsForConstVar, kind == IncreKind::INCR);
    initializer.Run(pkg, initOrder);
    SetInitFuncForStaticVar();
}

void AST2CHIR::CollectTopLevelDecls(AST::Package& pkg)
{
    Utils::ProfileRecorder recorder("AST to CHIR Translation", "CollectTopLevelDecls");
    // For some special functions, they are explicitly imported even they are NOT explicitly used.
    // Collect implicit imported/used decl, the decl list is made by
    // `REG_IMPLICIT_IMPORTED_NON_GENERIC_FUNC` and `REG_IMPLICIT_IMPORTED_GENERIC_FUNC`,there are two cases:
    //   1. if `pkg` is not std.core, we collect implicit imported decls, especially those generic decl in std.core, but
    //   instantiated decls in other imported package.
    //   2. if `pkg` is std.core (aka we are compiling std.core), we collect implicit used decls by down stream package.
    CollectImplicitFuncs();

    // Collect Imported Pkg Top-Level Decl Part
    CollectImportedDecls(pkg);

    // Collect Current Pkg Top-Level Decl & Generic Top-Level Decl Part
    CollectDeclsInCurPkg(pkg);
}

void AST2CHIR::CacheSomeDeclsToGlobalSymbolTable()
{
    Utils::ProfileRecorder recorder("AST to CHIR Translation", "CacheSomeDeclsToGlobalSymbolTable");
    // translate all custom type decl, and cache them to global symbol table
    // only create a shell of `CustomTypeDef`, including its ptr and attribute, not fill its method, vtable and so on
    CacheCustomTypeDefToGlobalSymbolTable();
    // translate all custom type's type, and set type to `CustomTypeDef`
    TranslateAllCustomTypeTy();
    // create all top-level func decl's shell and var decls, cache them to global symbol table.
    CacheTopLevelDeclToGlobalSymbolTable();

    SetGenericDecls();
}

void AST2CHIR::SetGenericDecls() const
{
    if (!gim) {
        return;
    }
    auto genericToInsMap = gim->GetAllGenericToInsDecls();
    for (auto& mapIt : genericToInsMap) {
        if (mapIt.first->astKind == AST::ASTKind::EXTEND_DECL) {
            continue;
        }
        if (mapIt.first->IsNominalDecl()) {
            auto chirGeneric = chirType.TryGetGlobalNominalCache(*mapIt.first);
            CJC_NULLPTR_CHECK(chirGeneric);
            for (auto insDecl : mapIt.second) {
                auto chirIns = chirType.TryGetGlobalNominalCache(*insDecl);
                // instantiated decl may be imported but not used in current package, so it's not translated by CHIR
                if (chirIns == nullptr) {
                    continue;
                }
                chirIns->SetGenericDecl(*chirGeneric);
            }
        } else if (mapIt.first->astKind == AST::ASTKind::FUNC_DECL) {
            auto chirGeneric = globalCache.TryGet(*mapIt.first);
            // intrinsic
            if (chirGeneric == nullptr) {
                continue;
            }
            for (auto insDecl : mapIt.second) {
                auto chirIns = globalCache.TryGet(*insDecl);
                // shouldn't be nullptr
                if (chirIns == nullptr) {
                    continue;
                }
                VirtualCast<FuncBase*>(chirIns)->SetGenericDecl(*VirtualCast<FuncBase*>(chirGeneric));
            }
        }
    }
}

void AST2CHIR::TranslateAllDecls(const AST::Package& pkg, const InitOrder& initOrder)
{
    Utils::ProfileRecorder recorder("AST to CHIR Translation", "TranslateAllDecls");
    // step 1: translate nominal decls first
    TranslateNominalDecls(pkg);

    // step 2: translate initialization of global var
    TranslateInitOfGlobalVars(pkg, initOrder);

    // do this after init of gv to ensure gvinit of CustomAnnotations go after real gv's
    // this is always safe because such gvinit's are generated by CHIR and cannot be referenced in source
    for (auto decl : annoOnlyDecls) {
        CreateAnnoOnlyDeclSig(*decl);
    }

    // step 3: translate body of all funcs, the process can be run in parallel according to the compilation option
    Utils::ProfileRecorder::Start("TranslateAllDecls", "TranslateOtherTopLevelDecls");
    if (opts.GetJobs() > 1) {
        TranslateTopLevelDeclsInParallel();
    } else {
        for (auto decl : globalAndMemberFuncs) {
            if (!NeedTranslate(*decl)) {
                continue;
            }
            auto trans = CreateTranslator();
            trans.SetTopLevel(*decl);
            Translator::TranslateASTNode(*decl, trans);
            if (decl->TestAttr(AST::Attribute::GLOBAL)) {
                trans.CollectValueAnnotation(*decl);
            }
        }
        for (auto decl : std::as_const(localConstFuncs.stableOrderValue)) {
            auto trans = CreateTranslator();
            trans.SetTopLevel(*decl);
            Translator::TranslateASTNode(*decl, trans);
        }
    }
    // the parallel helper does not accept this type, use serialised translation
    for (auto decl : annoFactoryFuncs) {
        auto trans = CreateTranslator();
        trans.SetTopLevel(*decl.first);
        trans.TranslateAnnoFactoryFuncBody(*decl.first, *decl.second);
    }
    Utils::ProfileRecorder::Stop("TranslateAllDecls", "TranslateOtherTopLevelDecls");

    // step 4: set `CompileTimeValue` for lambda
    Utils::ProfileRecorder::Start("TranslateAllDecls", "SetCompileTimeValueFlag");
    for (auto func : package->GetGlobalFuncs()) {
        if (func->TestAttr(Attribute::CONST)) {
            SetCompileTimeValueFlagRecursivly(*func);
        }
    }
    Utils::ProfileRecorder::Stop("TranslateAllDecls", "SetCompileTimeValueFlag");
}

void AST2CHIR::TranslateInParallel(const std::vector<Ptr<const AST::Decl>>& decls)
{
    Utils::ParallelUtil allDeclsParallel(builder, opts.GetJobs());
    allDeclsParallel.RunAST2CHIRInParallel(decls, chirType, opts, gim, globalCache, localConstVars, localConstFuncs,
        kind, deserializedVals, Translator::TranslateASTNode, maybeUnreachable, isComputingAnnos,
        initFuncsForAnnoFactory, types, annoFactoryFuncs);
}

void AST2CHIR::TranslateTopLevelDeclsInParallel()
{
    // collect all top-level funcs into a vector.
    std::vector<Ptr<const AST::Decl>> allDecls;
    auto needTrans = [this](Ptr<const AST::Decl> decl) { return NeedTranslate(*decl); };
    // Filter out decls that do not require translation.
    std::copy_if(globalAndMemberFuncs.begin(), globalAndMemberFuncs.end(),
        std::back_inserter(allDecls), needTrans);
    TranslateInParallel(allDecls);

    std::vector<Ptr<const AST::Decl>> localFuncsGeneric; // generic decls
    std::vector<Ptr<const AST::Decl>> localFuncsOther; // instantiated or non-generic decls
    for (auto func : std::as_const(localConstFuncs.stableOrderValue)) {
        if (func->TestAttr(AST::Attribute::GENERIC)) {
            localFuncsGeneric.push_back(func);
        } else {
            localFuncsOther.push_back(func);
        }
    }
    TranslateInParallel(localFuncsGeneric);
    TranslateInParallel(localFuncsOther);
}

void AST2CHIR::AST2CHIRCheck()
{
    if (!opts.chirWFC) {
        return;
    }
    Utils::ProfileRecorder recorder("AST to CHIR Translation", "AST2CHIRCheck");
    // check customDefTypes
    auto customDefMap = chirType.GetAllTypeDef();
    for (auto& it : customDefMap) {
        const AST::Node& astNode = *it.first;
        const CHIR::CustomTypeDef& chirNode = *it.second;
        if (astNode.TestAttr(AST::Attribute::GENERIC, AST::Attribute::IMPORTED)) {
            continue;
        }
        if (astNode.TestAttr(AST::Attribute::IMPORTED) && chirNode.TestAttr(Attribute::DESERIALIZED)) {
            continue;
        }
        if (chirNode.TestAttr(Attribute::SKIP_ANALYSIS)) {
            continue;
        }
        auto ret = AST2CHIRCheckCustomTypeDef(astNode, chirNode, globalCache);
        if (!ret) {
            this->failure = true;
            it.second->Dump();
        }
    }
    // check globalFunc and globalVar
    for (auto& it : globalCache.GetALL()) {
        if (it.second->TestAttr(Attribute::SKIP_ANALYSIS)) {
            continue;
        }
        auto ret = AST2CHIRCheckValue(*it.first, *it.second);
        if (!ret) {
            this->failure = true;
            it.second->Dump();
        }
    }
}

/// Return true if chir was deserialized
bool AST2CHIR::TryToDeserializeCHIR()
{
    auto& chirFiles = opts.inputChirFiles;
    ToCHIR::Phase phase;
    if (chirFiles.size() == 1) {
        CHIRDeserializer::Deserialize(chirFiles.at(0), builder, phase, true);
        mergingPlatform = true;
        return true;
    } else if (chirFiles.size() != 0) {
        // synthetic limitation, however need to distinguish different chir sources
        diag.DiagnoseRefactor(DiagKindRefactor::frontend_can_not_handle_to_many_chir, DEFAULT_POSITION);
    }

    return false;
}

static Package::AccessLevel BuildPackageAccessLevel(const AST::AccessLevel& level)
{
    static std::unordered_map<AST::AccessLevel, Package::AccessLevel> accessLevelMap = {
        {AST::AccessLevel::INTERNAL, Package::AccessLevel::INTERNAL},
        {AST::AccessLevel::PROTECTED, Package::AccessLevel::PROTECTED},
        {AST::AccessLevel::PUBLIC, Package::AccessLevel::PUBLIC},
    };

    auto it = accessLevelMap.find(level);
    CJC_ASSERT(it != accessLevelMap.end());
    return it->second;
}

bool AST2CHIR::ToCHIRPackage(AST::Package& node)
{
    // It can be not null in case of part of the package was deserialized from .chir
    if (TryToDeserializeCHIR()) {
        package = builder.GetCurPackage();
        BuildDeserializedTable();
    } else {
        package = builder.CreatePackage(node.fullPackageName);
        outputCHIR = opts.outputMode == GlobalOptions::OutputMode::CHIR;
    }
    // Translating common alongside with merging platform is not supported
    CJC_ASSERT(!(outputCHIR && mergingPlatform));
    package->SetPackageAccessLevel(BuildPackageAccessLevel(node.accessible));
    RegisterAllSources();
    CJC_NULLPTR_CHECK(package);
    dependencyPkg = importManager.GetAllDependentPackageNames(node.fullPackageName);

    // step 1: collect all top-level decls
    CollectTopLevelDecls(node);

    // step 2: sort and do circular dependency checking
    auto [initOrder, result] = SortGlobalVarDecl(node);
    if (!result) {
        return false;
    }

    // step 3: create signature of nominal decls and top-level func decls
    CacheSomeDeclsToGlobalSymbolTable();

    // step 4: translate all colleted decls
    TranslateAllDecls(node, initOrder);

    // step 5: ast2chir check
    AST2CHIRCheck();

    return !HasFailed();
}
} // namespace Cangjie::CHIR
