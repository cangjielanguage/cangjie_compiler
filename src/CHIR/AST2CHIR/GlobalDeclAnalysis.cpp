// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/GlobalDeclAnalysis.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/CHIR/Utils/Utils.h"

#include <stack>

#define DEBUG0 false

using namespace Cangjie;
using namespace Cangjie::CHIR;

AST::VisitAction GlobalDeclAnalysis::CollectDepInCallExpr(
    const AST::CallExpr& callExpr, std::vector<Ptr<const AST::Decl>>& dependencies)
{
    if (callExpr.resolvedFunction) {
        AddDependency(*callExpr.resolvedFunction, dependencies);
    }
    if (!callExpr.desugarArgs.has_value()) {
        return AST::VisitAction::WALK_CHILDREN;
    }

    // For those call of a function which has default value in some params, there will be implicitly
    // dependency to the parap default value function after desugar
    //
    // For example, the CJ code below
    //     func foo(x: Int64 = 10) {}
    //     let res = foo()
    // will be desugared into
    //     func foo(x: Int64) {}
    //     func foo_param_x_default_value() { 10 }
    //     let res = foo(foo_param_x_default_value())
    // thus, now `res` will also depends on `foo_param_x_default_value`
    auto& argExprs = callExpr.desugarArgs.value();
    for (size_t i = 0; i < argExprs.size(); ++i) {
        if (argExprs[i]->TestAttr(AST::Attribute::HAS_INITIAL)) {
            CJC_NULLPTR_CHECK(callExpr.resolvedFunction);
            CJC_NULLPTR_CHECK(callExpr.resolvedFunction->funcBody.get());
            CJC_NULLPTR_CHECK(callExpr.resolvedFunction->funcBody->paramLists[0]);
            CJC_NULLPTR_CHECK(callExpr.resolvedFunction->funcBody->paramLists[0]->params[i]);
            auto arg = callExpr.resolvedFunction->funcBody->paramLists[0]->params[i]->desugarDecl.get();
            AddDependency(*arg, dependencies);
        }
    }
    return AST::VisitAction::WALK_CHILDREN;
}

AST::VisitAction GlobalDeclAnalysis::CollectDepInLitConstExpr(
    const AST::LitConstExpr& litConstExpr, std::vector<Ptr<const AST::Decl>>& dependencies)
{
    if (litConstExpr.kind == AST::LitConstKind::JSTRING || !litConstExpr.ref) {
        return AST::VisitAction::WALK_CHILDREN;
    }
    auto structDecl = StaticCast<AST::StructDecl*>(litConstExpr.ref->ref.target);
    if (!litConstExpr.stringValue.empty()) {
        return AST::VisitAction::WALK_CHILDREN;
    }
    // In later compilation process, we always translate an empty string literal into a reference of
    // `empty` variable defined in std.core library, so there will be implicitly dependency to the
    // `empty` variable then
    for (auto& decl : structDecl->body->decls) {
        if (decl->astKind == AST::ASTKind::VAR_DECL && decl->identifier == "empty" &&
            decl->TestAttr(AST::Attribute::STATIC)) {
            AddDependency(*decl, dependencies);
        }
    }
    return AST::VisitAction::WALK_CHILDREN;
}

AST::VisitAction GlobalDeclAnalysis::VisitExprAction(const AST::Expr& expr,
    std::vector<Ptr<const AST::Decl>>& dependencies, std::vector<Ptr<const AST::Decl>>& localConstVarDeps)
{
    if (auto ma = DynamicCast<const AST::MemberAccess*>(&expr); ma) {
        if (!ma->target) {
            return AST::VisitAction::WALK_CHILDREN;
        }
        if (ma->target->astKind == AST::ASTKind::FUNC_DECL) {
            AddDependency(*ma->target, dependencies);
        }
        if (ma->target->astKind == AST::ASTKind::VAR_DECL && ma->target->TestAttr(AST::Attribute::STATIC)) {
            AddDependency(*ma->target, dependencies);
        }
        if (AST::IsPackageMemberAccess(*ma) && ma->target->astKind == AST::ASTKind::VAR_DECL) {
            AddDependency(*ma->target, dependencies);
        }
        return AST::VisitAction::WALK_CHILDREN;
    } else if (auto re = DynamicCast<const AST::RefExpr*>(&expr); re) {
        bool hasRefTarget = re->ref.target &&
            (re->ref.target->astKind == AST::ASTKind::FUNC_DECL || re->ref.target->astKind == AST::ASTKind::VAR_DECL);
        if (hasRefTarget &&
            (re->ref.target->TestAttr(AST::Attribute::GLOBAL) || re->ref.target->TestAttr(AST::Attribute::STATIC))) {
            AddDependency(*re->ref.target, dependencies);
        } else if (hasRefTarget && re->ref.target->IsConst()) {
            AddDependency(*re->ref.target, localConstVarDeps);
        }

        return AST::VisitAction::SKIP_CHILDREN;
    } else if (auto arrExpr = DynamicCast<const AST::ArrayExpr*>(&expr); arrExpr) {
        if (arrExpr->initFunc) {
            AddDependency(*arrExpr->initFunc, dependencies);
        }
        return AST::VisitAction::WALK_CHILDREN;
    } else if (auto arrLit = DynamicCast<const AST::ArrayLit*>(&expr); arrLit) {
        if (arrLit->initFunc) {
            AddDependency(*arrLit->initFunc, dependencies);
        }
        return AST::VisitAction::WALK_CHILDREN;
    } else if (auto callExpr = DynamicCast<const AST::CallExpr*>(&expr); callExpr) {
        return CollectDepInCallExpr(*callExpr, dependencies);
    } else if (auto litConstExpr = DynamicCast<const AST::LitConstExpr*>(&expr); litConstExpr) {
        return CollectDepInLitConstExpr(*litConstExpr, dependencies);
    }
    return AST::VisitAction::WALK_CHILDREN;
}

void GlobalDeclAnalysis::WalkAndCollectDep(const AST::Node& curNode, std::vector<Ptr<const AST::Decl>>& dependencies,
    std::vector<Ptr<const AST::Decl>>& localConstVarDeps)
{
    std::function<AST::VisitAction(Ptr<AST::Node>)> visitor = [this, &visitor, &dependencies, &localConstVarDeps](
                                                                  const Ptr<AST::Node> n) {
        return Meta::match(*n)(
            [this, &visitor, &dependencies, &localConstVarDeps](AST::Expr& expr) {
                AST::Expr* exprPtr = &expr;
                if (exprPtr->desugarExpr != nullptr) {
                    while (exprPtr->desugarExpr != nullptr) {
                        exprPtr = exprPtr->desugarExpr.get();
                    }
                    AST::Walker walker(exprPtr, visitor);
                    walker.Walk();
                    return AST::VisitAction::SKIP_CHILDREN;
                }
                return VisitExprAction(expr, dependencies, localConstVarDeps);
            },
            [&visitor](const AST::FuncParam& param) {
                if (param.assignment) {
                    // If this is func param which has default value, we should always
                    // skip walk the `assignment` part cause it has been desugared into
                    // the `desugarDecl` part
                    AST::Walker walker(param.desugarDecl, visitor);
                    walker.Walk();
                    return AST::VisitAction::SKIP_CHILDREN;
                }
                return AST::VisitAction::WALK_CHILDREN;
            },
            [](const AST::FuncArg& arg) {
                if (arg.TestAttr(AST::Attribute::HAS_INITIAL)) {
                    // If this is func arg will be desugared into a call to the default value
                    // func, we should also skip since we have handled them in the `callExpr`
                    return AST::VisitAction::SKIP_CHILDREN;
                }
                return AST::VisitAction::WALK_CHILDREN;
            },
            [this, &visitor](const AST::FuncDecl& func) {
                if (gim && func.TestAttr(AST::Attribute::GENERIC)) {
                    auto instantiatedDecls = gim->GetInstantiatedDecls(func);
                    for (auto& instance : instantiatedDecls) {
                        AST::Walker walker(instance, visitor);
                        walker.Walk();
                    }
                }
                return AST::VisitAction::WALK_CHILDREN;
            },
            []() { return AST::VisitAction::WALK_CHILDREN; });
    };
    AST::Walker walker(const_cast<AST::Node*>(&curNode), visitor);
    walker.Walk();
}

void GlobalDeclAnalysis::AnalysisDepOf(const AST::Decl& rootDecl, std::vector<Ptr<const AST::Decl>>& dependencies,
    std::vector<Ptr<const AST::Decl>>& localConstVarDeps)
{
    WalkAndCollectDep(rootDecl, dependencies, localConstVarDeps);

    // Speical Case 1): if current node is a static init function, then we need to filter outer the usage of the
    // initialized static member variables
    // Special Case 2): if current node is a constructor, then we also need to consider the implicit dependency to
    // the default value of the member variables defined in the struct/class
    if (auto func = DynamicCast<const AST::FuncDecl*>(&rootDecl); func && func->TestAttr(AST::Attribute::CONSTRUCTOR)) {
        if (func->TestAttr(AST::Attribute::STATIC)) {
            AdditionalAnalysisDepOfStaticInit(func, dependencies);
        } else {
            AdditionalAnalysisDepOfNonStaticCtor(*func, dependencies, localConstVarDeps);
        }
    }
}

bool GlobalDeclAnalysis::SkipDependencyTo(const AST::Decl& decl)
{
    // 1) the target decls are only declarations which don't have body
    if (decl.TestAttr(AST::Attribute::FOREIGN) || decl.TestAttr(AST::Attribute::INTRINSIC) ||
        decl.TestAttr(AST::Attribute::ENUM_CONSTRUCTOR)) {
        return true;
    }
    // 2) the target decls are imported which also don't have body. Note that we need to exclude the source imported
    // stuff
    if (decl.TestAttr(AST::Attribute::IMPORTED)) {
        if (!decl.TestAttr(AST::Attribute::SRC_IMPORTED)) {
            return true;
        }
    }

    // 3) now we do a search to check if the target decl is in our candidate list
    bool res = !funcsAndVars.HasElement(&decl) && !localConstVars.HasElement(&decl);
    return res;
}

void GlobalDeclAnalysis::AddDependencyImpl(
    const AST::Decl& depDecl, std::vector<Ptr<const AST::Decl>>& dependencies) const
{
    if (std::find(dependencies.begin(), dependencies.end(), &depDecl) != dependencies.end()) {
        return;
    }
    dependencies.emplace_back(&depDecl);
}

void GlobalDeclAnalysis::AddDependency(const AST::Decl& depDecl, std::vector<Ptr<const AST::Decl>>& dependencies)
{
    // If the target decl is a static variable which is initialized inside a `static init func`,
    // then we need to transfer the dependency to the `static init func`
    if (auto varDecl = DynamicCast<const AST::VarDecl*>(&depDecl)) {
        if (varDecl->TestAttr(AST::Attribute::STATIC) && varDecl->initializer == nullptr) {
            auto pos = staticInitFuncInfoMap.find(varDecl->outerDecl);
            if (pos != staticInitFuncInfoMap.end()) {
                AddDependencyImpl(*pos->second.staticInitVar, dependencies);
                return;
            }
        }
    }

    // If the variable declaration is from `VarPatternDecl`, we need to add the
    // dependency to the `VarPatternDecl` instead
    if (auto varDecl = DynamicCast<const AST::VarDecl*>(&depDecl); varDecl && varDecl->parentPattern) {
        auto pos = varPtternDeclMap.find(varDecl);
        if (pos == varPtternDeclMap.end()) {
            return;
        }
        AddDependencyImpl(*pos->second, dependencies);
        return;
    }

    if (SkipDependencyTo(depDecl)) {
        return;
    }
    AddDependencyImpl(depDecl, dependencies);
}

void GlobalDeclAnalysis::ParsePattern(const AST::VarWithPatternDecl& rootDecl, AST::Pattern& pattern)
{
    if (auto tuplePattern = DynamicCast<AST::TuplePattern*>(&pattern); tuplePattern) {
        for (auto& tPattern : tuplePattern->patterns) {
            ParsePattern(rootDecl, *tPattern.get());
        }
    } else if (auto enumPattern = DynamicCast<AST::EnumPattern*>(&pattern); enumPattern) {
        for (auto& ePattern : enumPattern->patterns) {
            ParsePattern(rootDecl, *ePattern.get());
        }
    } else if (auto varPattern = DynamicCast<AST::VarPattern*>(&pattern); varPattern) {
        varPtternDeclMap.emplace(varPattern->varDecl.get(), &rootDecl);
    } else if (auto wildPattern = DynamicCast<AST::WildcardPattern*>(&pattern); wildPattern) {
        // Do nothing here
    } else {
        CJC_ABORT();
    }
}

void GlobalDeclAnalysis::ParseVarWithPatternDecl(const AST::VarWithPatternDecl& root)
{
    ParsePattern(root, *root.irrefutablePattern.get());
}

void GlobalDeclAnalysis::AdditionalAnalysisDepOfNonStaticCtor(const AST::FuncDecl& func,
    std::vector<Ptr<const AST::Decl>>& dependencies, std::vector<Ptr<const AST::Decl>>& localConstVarDeps)
{
    CJC_ASSERT(func.funcBody->parentClassLike != nullptr || func.funcBody->parentStruct != nullptr);
    const AST::InheritableDecl* parentDecl = nullptr;
    if (func.funcBody->parentClassLike) {
        parentDecl = func.funcBody->parentClassLike;
    } else {
        parentDecl = func.funcBody->parentStruct;
    }
    auto memberVars = GetNonStaticMemberVars(*parentDecl);
    for (auto memberVar : memberVars) {
        if (memberVar->initializer != nullptr) {
            WalkAndCollectDep(*memberVar, dependencies, localConstVarDeps);
        }
    }
}

/// Static member variable is a dependency if it's initialized in another file.
/// This is required because of all direct initializers must be applied before static init,
/// so files with direct initializers are dependencies for file with static init.
static bool DoesStaticInitDependOnStaticVar(const Ptr<const AST::FuncDecl>& staticInit,
    Ptr<Cangjie::AST::VarDecl> &staticVar)
{
    std::optional<unsigned int> staticVarInitializerFileId = std::nullopt;
    if (staticVar->platformImplementation) {
        auto platformStaticVar = DynamicCast<AST::VarDecl>(staticVar->platformImplementation);
        CJC_NULLPTR_CHECK(platformStaticVar);
        if (platformStaticVar->initializer) {
            staticVarInitializerFileId = platformStaticVar->initializer->begin.fileID;
        }
    } else {
        if (staticVar->initializer) {
            staticVarInitializerFileId = staticVar->initializer->begin.fileID;
        }
    }

    auto staticInitFileId = staticInit->begin.fileID;
    return staticInitFileId != staticVarInitializerFileId;
}

void GlobalDeclAnalysis::AdditionalAnalysisDepOfStaticInit(
    const Ptr<const AST::FuncDecl>& staticInit, std::vector<Ptr<const AST::Decl>>& dependencies) const
{
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    CJC_ASSERT(IsStaticInit(*staticInit));
#endif

    // Get all the static member variables which are inited by this `static.init` func
    // and filter out the dependency to these static member variables
    auto parentDecl = DynamicCast<const AST::InheritableDecl*>(staticInit->outerDecl);
    CJC_NULLPTR_CHECK(parentDecl);
    auto staticVars = GetStaticMemberVars(*parentDecl);
    std::vector<Ptr<const AST::Decl>> filtedDependencies;
    for (auto& dep : dependencies) {
        if (std::find(staticVars.begin(), staticVars.end(), dep) != staticVars.end()) {
            continue;
        }
        filtedDependencies.emplace_back(dep);
    }
    for (auto& staticVar : staticVars) {
        if (DoesStaticInitDependOnStaticVar(staticInit, staticVar)) {
            filtedDependencies.emplace_back(staticVar);
        }
    }

    dependencies = filtedDependencies;
}

static void ReplaceCommonDependenciesWithPlatform(std::vector<Ptr<const AST::Decl>>& dependencies)
{
    for (size_t i = 0; i < dependencies.size(); i++) {
        if (dependencies[i]->platformImplementation) {
            dependencies[i] = dependencies[i]->platformImplementation;
        }
    }
}

static void SaveCJMPDependencies(Ptr<const AST::Decl> decl, std::vector<Ptr<const AST::Decl>>& dependencies)
{
    for (auto dependency : dependencies) {
        decl->dependencies.emplace_back(dependency);
    }
}

static void RestoreCJMPDependencies(Ptr<const AST::Decl> decl, std::vector<Ptr<const AST::Decl>>& dependencies)
{
    for (auto dependency : decl->dependencies) {
        if (dependency->platformImplementation) {
            dependencies.emplace_back(dependency);
            dependencies.emplace_back(dependency->platformImplementation);
        } else {
            dependencies.emplace_back(dependency);
        }
    }
}

void GlobalDeclAnalysis::AnalysisDependency(const ElementList<Ptr<const AST::Decl>>& nodesWithDeps)
{
    // We have to handle the `VarPatternDecl` specially, cause it may holds
    // multiple variable declarations, all the dependencies to these variables
    // should be transformed to be dependencies to the `VarPatternDecl` itself
    for (auto node : nodesWithDeps.stableOrderValue) {
        if (auto tuple = DynamicCast<const AST::VarWithPatternDecl*>(node); tuple) {
            ParseVarWithPatternDecl(*tuple);
        }
    }

    for (auto node : nodesWithDeps.stableOrderValue) {
        // Note: what are we doing here?
        if (auto vd = DynamicCast<AST::VarDecl*>(node);
            vd && vd->TestAttr(AST::Attribute::STATIC) && !vd->TestAttr(AST::Attribute::DEFAULT)) {
            // The dependencies of static variables which initialised in the static initialiser will be
            // collected when traversing the static initialiser.
            continue;
        }
        std::vector<Ptr<const AST::Decl>> dependencies;
        std::vector<Ptr<const AST::Decl>> localConstVarDeps;
        AnalysisDepOf(*node, dependencies, localConstVarDeps);
        if (outputChir) {
            SaveCJMPDependencies(node, dependencies);
        }
        if (mergingPlatform) {
            ReplaceCommonDependenciesWithPlatform(dependencies);
            RestoreCJMPDependencies(node, dependencies);
        }
        (void)std::remove_if(dependencies.begin(), dependencies.end(),
            [&node](const Ptr<const AST::Decl>& element) { return element == node; });
        funcsAndVarsDepMap.AddDeps(node, dependencies);
        localConstVarsDepMap.emplace(node, localConstVarDeps);
    }
}

void GlobalDeclAnalysis::AnalysisFileDependencyImpl(const Ptr<const AST::Decl>& rootDecl,
    const Ptr<const AST::Decl>& curDecl, std::unordered_set<Ptr<const AST::Decl>>& visited)
{
    auto pos = funcsAndVarsDepMap.randomOrderValue.find(curDecl);
    if (pos == funcsAndVarsDepMap.randomOrderValue.end()) {
        CJC_ASSERT(curDecl->TestAttr(AST::Attribute::SRC_IMPORTED));
        return;
    }
    const auto& directDepsOfCurDecl = pos->second;
    for (auto& dep : directDepsOfCurDecl) {
        if (visited.find(dep) != visited.end()) {
            continue;
        }
        visited.emplace(dep);

        // Generate file dependency if the `dep` is a variable, otherwise, we need to further traverse the decl
        // dependency to search
        if ((dep->astKind == AST::ASTKind::VAR_DECL || dep->astKind == AST::ASTKind::VAR_WITH_PATTERN_DECL)) {
            if (rootDecl->curFile == dep->curFile) {
                var2varDepsInFile[rootDecl].emplace(dep);
            } else {
                fileDependencyMap.AddDep(rootDecl->curFile, dep->curFile);
            }
        } else {
            AnalysisFileDependencyImpl(rootDecl, dep, visited);
        }
    }
}

void GlobalDeclAnalysis::AnalysisFileDependency()
{
    for (auto& element : funcsAndVarsDepMap.stableOrderValue) {
        auto srcDecl = element.first;
        if (srcDecl->astKind != AST::ASTKind::VAR_DECL && srcDecl->astKind != AST::ASTKind::VAR_WITH_PATTERN_DECL) {
            continue;
        }
        std::unordered_set<Ptr<const AST::Decl>> visited;
        AnalysisFileDependencyImpl(srcDecl, srcDecl, visited);
    }
}

void GlobalDeclAnalysis::CheckUseBeforeInit(const std::vector<Ptr<const AST::Decl>>& vars)
{
    std::unordered_set<Ptr<const AST::Decl>> alreadyInited;
    for (auto& var : vars) {
        alreadyInited.emplace(var);
        if (var->identifier == STATIC_INIT_VAR) {
            continue;
        }

        // Should not depends on any other vars initialized later
        auto pos = var2varDepsInFile.find(var);
        if (pos != var2varDepsInFile.end()) {
            auto& deps = pos->second;
            for (auto& dep : deps) {
                if (alreadyInited.find(dep) == alreadyInited.end()) {
                    diag->Diagnose(DiagKind::chir_used_before_initialization, dep->identifier.Val());
                }
            }
        }
    }
}

std::vector<Ptr<const AST::File>> GlobalDeclAnalysis::SortGlobalVarDep(
    const ElementList<Ptr<const AST::Decl>>& nodesWithDeps,
    const std::unordered_map<Ptr<const AST::File>, std::vector<Ptr<const AST::Decl>>>& fileAndVarMap,
    CompilationCache& cachedInfo)
{
    // 1) Analysis the decl dependencies
    // Note: shall we do parallel analysis here?
    AnalysisDependency(nodesWithDeps);

    // 2) Cache and update the decl dependencies result for incremental compilation
    for (auto& element : cachedInfo.varAndFuncDep) {
        auto pos = funcsAndVarsDepMap.randomOrderValue.find(element.first);
        if (pos == funcsAndVarsDepMap.randomOrderValue.end()) {
            funcsAndVarsDepMap.AddDeps(element.first, element.second);
        }
    }
    cachedInfo.varAndFuncDep = funcsAndVarsDepMap.stableOrderValue;

    // 3) Map the decl dependencies into file dependencies
    // Init file dependency map, note that we will have files from upstream package included here
    for (auto& decl : funcsAndVars.stableOrderValue) {
        if (fileDependencyMap.randomOrderValue.find(decl->curFile) == fileDependencyMap.randomOrderValue.end()) {
            fileDependencyMap.InitDeps(decl->curFile);
        }
    }
    AnalysisFileDependency();

    // 4) find whether files circular dependency exist and get sorted files in current package.
    std::vector<std::string> diagLog;
    FileCirDepsChecker fileChecker(*diag, diagLog);
    fileChecker.InitFileDep(fileDependencyMap);
    fileChecker.DoTarjanSort();
    fileChecker.UpdateSccUnit(fileDependencyMap.randomOrderValue);
    auto sortedFiles = fileChecker.GetOrderElement();

    // Check if there is use before init issue which might not been recognized by sema in incremental compilation
    // There is use-before issue in std/tls/net lib
    if (kind == IncreKind::INCR) {
        for (auto element : fileAndVarMap) {
            CheckUseBeforeInit(element.second);
        }
    }

    return sortedFiles;
}

void GlobalDeclAnalysis::AnalysisGlocalVarsAndLocalConstVarsDependencyImpl(const Ptr<const AST::Decl>& rootDecl,
    const Ptr<const AST::Decl>& curDecl, std::unordered_set<Ptr<const AST::Decl>>& visited)
{
    auto pos = funcsAndVarsDepMap.randomOrderValue.find(curDecl);
    CJC_ASSERT(pos != funcsAndVarsDepMap.randomOrderValue.end());
    const auto& directDepsOfCurDecl = pos->second;
    for (auto& dep : directDepsOfCurDecl) {
        if (visited.find(dep) != visited.end()) {
            continue;
        }
        visited.emplace(dep);

        // Generate file dependency if the `dep` is a variable, otherwise, we need to further traverse the decl
        // dependency to search
        if ((dep->astKind == AST::ASTKind::VAR_DECL || dep->astKind == AST::ASTKind::VAR_WITH_PATTERN_DECL)) {
            globalVarsAndLocalConstVarsDepMap.AddDep(rootDecl, dep);
        } else {
            AnalysisGlocalVarsAndLocalConstVarsDependencyImpl(rootDecl, dep, visited);
        }
    }
}

void GlobalDeclAnalysis::AnalysisGlobalVarsAndLocalConstVarsDependency(const InitOrder& initOrder)
{
    // Init the dependency map with the global vars dependencies, we will add a fake dependency
    // by the init order of the global vars
    Ptr<const AST::Decl> previous = nullptr;
    for (auto& element : initOrder) {
        for (auto& gv : element.second) {
            if (previous != nullptr) {
                globalVarsAndLocalConstVarsDepMap.AddDep(gv, previous);
            }
            previous = gv;
        }
    }

    // Then we add the local vars dependecies into the map
    for (auto& localConstVar : localConstVars.stableOrderValue) {
        std::unordered_set<Ptr<const AST::Decl>> visited;
        AnalysisGlocalVarsAndLocalConstVarsDependencyImpl(localConstVar, localConstVar, visited);
    }
}

InitOrder GlobalDeclAnalysis::MergeLocalConstVarInitOrder(
    const InitOrder& initOrder, const std::vector<Ptr<const AST::Decl>> sortedVars) const
{
    InitOrder finalInitOrder;
    size_t j = 0;
    for (auto& pair : initOrder) {
        auto& file = pair.first;
        auto& orignalVars = pair.second;
        if (orignalVars.empty()) {
            finalInitOrder.emplace_back(std::make_pair(file, std::vector<Ptr<const AST::Decl>>{}));
            continue;
        }

        size_t i = 0;
        std::vector<Ptr<const AST::Decl>> varsInCurFile;
        while (j < sortedVars.size()) {
            if (orignalVars[i] == sortedVars[j]) {
                ++i;
            }
            varsInCurFile.emplace_back(sortedVars[j]);
            ++j;
            if (i == orignalVars.size()) {
                break;
            }
        }
        while (i < orignalVars.size()) {
            varsInCurFile.emplace_back(orignalVars[i]);
            ++i;
        }

        finalInitOrder.emplace_back(std::make_pair(file, varsInCurFile));
    }
    if (j < sortedVars.size()) {
        auto file = sortedVars[j]->curFile;
        if (finalInitOrder.empty()) {
            std::vector<Ptr<const AST::Decl>> varsInCurFile;
            while (j < sortedVars.size()) {
                varsInCurFile.emplace_back(sortedVars[j]);
                ++j;
            }
            finalInitOrder.emplace_back(std::make_pair(file, varsInCurFile));
        } else {
            auto& varsInCurFile = finalInitOrder.back().second;
            while (j < sortedVars.size()) {
                varsInCurFile.emplace_back(sortedVars[j]);
                ++j;
            }
        }
    }
    return finalInitOrder;
}

InitOrder GlobalDeclAnalysis::SortLocalConstVarDep(const InitOrder& initOrder)
{
    std::vector<Ptr<const AST::Decl>> globalVarsAndLocalConstVars;
    for (auto element : funcsAndVars.stableOrderValue) {
        if (element->astKind == AST::ASTKind::VAR_DECL || element->astKind == AST::ASTKind::VAR_WITH_PATTERN_DECL) {
            globalVarsAndLocalConstVars.emplace_back(element);
        }
    }

    if (globalVarsAndLocalConstVars.empty()) {
        return initOrder;
    }

    // 1) Analysis the local const decl dependencies
    AnalysisDependency(localConstVars);

    // 3) Map the decl dependencies into var-to-var dependencies
    for (auto element : localConstVarsDepMap) {
        auto& deps = funcsAndVarsDepMap.randomOrderValue[element.first];
        deps.insert(deps.end(), element.second.begin(), element.second.end());
        for (auto& pair : funcsAndVarsDepMap.stableOrderValue) {
            if (pair.first == element.first) {
                pair.second.insert(pair.second.end(), element.second.begin(), element.second.end());
            }
        }
    }
    AnalysisGlobalVarsAndLocalConstVarsDependency(initOrder);

    // 4) sort all variables
    VarCirDepsChecker varDepsChecker(*diag);
    varDepsChecker.InitDependency(globalVarsAndLocalConstVars, globalVarsAndLocalConstVarsDepMap.randomOrderValue);
    varDepsChecker.DoTarjanSort();
    varDepsChecker.UpdateSccUnit(globalVarsAndLocalConstVarsDepMap.randomOrderValue);
    auto sortedVars = varDepsChecker.GetOrderElement();

    if (diag->GetErrorCount() != 0) {
        return initOrder;
    }

    auto finalInitOrder = MergeLocalConstVarInitOrder(initOrder, sortedVars);
    return finalInitOrder;
}

InitOrder GlobalDeclAnalysis::Run(const ElementList<Ptr<const AST::Decl>>& nodesWithDeps,
    const std::unordered_map<Ptr<const AST::File>, std::vector<Ptr<const AST::Decl>>>& fileAndVarMap,
    CompilationCache& cachedInfo)
{
    auto orderedFiles = SortGlobalVarDep(nodesWithDeps, fileAndVarMap, cachedInfo);
    // No need to carry on if we find a circular dependency in files
    if (diag->GetErrorCount() != 0) {
        return InitOrder{};
    }

    InitOrder initOrder;
    for (auto file : orderedFiles) {
        auto pos = fileAndVarMap.find(file);
        if (pos != fileAndVarMap.end()) {
            auto vars = pos->second;
            initOrder.emplace_back(file, vars);
        } else {
            initOrder.emplace_back(file, std::vector<Ptr<const AST::Decl>>{});
        }
    }
    funcsAndVars.AddElements(localConstVars.stableOrderValue);

    auto finalInitOrder = SortLocalConstVarDep(initOrder);
    return finalInitOrder;
}

template <class T> TarjanSort<T>::~TarjanSort()
{
}

// using Tarjan's algorithm to group the SCC unit
template <class T> void TarjanSort<T>::DoTarjanSort()
{
    std::stack<DepsUnit<T>*> st;
    int index = 1;
    for (auto unit : units) {
        if (visitedUnits.find(unit) == visitedUnits.end()) {
            TarjanSortImpl(*unit, index, st);
        }
    }
}

template <class T>
void TarjanSort<T>::InitDependency(
    const std::vector<T>& rawElements, const std::unordered_map<T, std::vector<T>>& dependencyMap)
{
    for (auto& element : rawElements) {
        auto unit = std::make_unique<DepsUnit<T>>(element);
        units.emplace_back(unit.get());
        node2UnitMap.emplace(element, unit.get());
        allUnits.emplace_back(std::move(unit));
    }
    for (auto& element : rawElements) {
        if (dependencyMap.count(element) == 0) {
            continue;
        }
        auto varUnit = node2UnitMap[element];
        for (auto dep : dependencyMap.at(element)) {
            auto tempUnit = node2UnitMap[dep];
            varUnit->depsUnits.emplace_back(tempUnit);
            (void)tempUnit->userUnits.emplace_back(varUnit);
        }
    }
}

template <class T> std::vector<T> TarjanSort<T>::GetOrderElement()
{
    std::vector<T> sortedNodes;
    std::stack<DepsUnit<T>*> freeNodes;
    // 1) find nodes which have no in-dependency and others depend on it.
    for (auto unit : newSccUnits) {
        if (unit->depsUnits.empty()) {
            freeNodes.push(unit);
        }
    }
    // 2) sort the free nodes first and update the dependency map
    while (!freeNodes.empty()) {
        auto curFreeUnit = freeNodes.top();
        freeNodes.pop();
        for (auto ele : curFreeUnit->circularDepElements) {
            sortedNodes.emplace_back(ele);
        }
        for (auto& unit : curFreeUnit->userUnits) {
            // Delete curFreeUnit and delete it in depsUnits.
            auto& deps = unit->depsUnits;
            auto posA = std::find(deps.begin(), deps.end(), curFreeUnit);
            if (posA == deps.end()) {
                continue;
            }
            deps.erase(posA);
            if (deps.empty()) {
                freeNodes.push(unit);
            }
        }
    }
    visitedUnits.clear();
    node2UnitMap.clear();
    node2SccUnitMap.clear();
    newSccUnits.clear();
    return sortedNodes;
}

template <class T>
void TarjanSort<T>::UpdateSccUnitDeps(DepsUnit<T>& unit, std::unordered_map<T, std::vector<T>>& dependencyMap)
{
    auto& cirElements = unit.circularDepElements;
    for (auto element : cirElements) {
        auto deps = dependencyMap[element];
        for (auto& dep : deps) {
            if (std::find(cirElements.begin(), cirElements.end(), dep) == cirElements.end()) {
                unit.AddDeps(*node2SccUnitMap[dep]);
                node2SccUnitMap[dep]->AddUsers(unit);
            }
        }
    }
}

template <class T> void TarjanSort<T>::UpdateSccUnit(std::unordered_map<T, std::vector<T>>& dependencyMap)
{
    // update CircularDepElements for sccUnits
    for (auto unit : newSccUnits) {
        UpdateSccUnitDeps(*unit, dependencyMap);
    }
#if DEBUG0
    if (!newSccUnits.empty()) {
        std::cout << "============== SccUnits Result===============" << std::endl;
        for (size_t i = 0; i < newSccUnits.size(); i++) {
            auto elements = newSccUnits[i]->circularDepElements;
            std::string curSccStr;
            for (auto iter = elements.begin(); iter != elements.end(); iter++) {
                curSccStr = iter == elements.begin() ? GetEleStr(*iter) : curSccStr + " and " + GetEleStr(*iter);
            }
            std::cout << "SccUnit " << i << " has members : [" << curSccStr << "] " << std::endl;
            std::string depSccUnit;
            for (auto depScc : newSccUnits[i]->depsUnits) {
                auto iter = std::find(newSccUnits.begin(), newSccUnits.end(), depScc);
                int index = std::distance(newSccUnits.begin(), iter);
                depSccUnit = "  SccUnit " + std::to_string(index) + depSccUnit;
            }
            std::cout << "depends on " << depSccUnit << std::endl;
        }
        std::cout << "=========================================" << std::endl;
    }
#endif
}

template <class T> void TarjanSort<T>::TarjanSortImpl(DepsUnit<T>& unit, int& index, std::stack<DepsUnit<T>*>& st)
{
    unit.disc = index;
    unit.low = index;
    ++index;
    st.push(&unit);
    unit.stackMember = true;
    visitedUnits.insert(&unit);

    for (auto dep : unit.depsUnits) {
        if (dep->disc == -1) {
            // For the dep nodes which are not visited yet, traverse them recursively
            // and update the `low` value based on the traverse result
            TarjanSortImpl(*dep, index, st);
            unit.low = std::min(unit.low, dep->low);
        } else {
            // For the dep nodes which are already visited and also in the stack
            if (dep->stackMember) {
                unit.low = std::min(unit.low, dep->disc);
            }
        }
    }

    // If the `disc` and `low` value is equal, which means it is the root of the SCC,
    // then we can pop out the stack the get the whole set of the SCC.
    if (unit.disc == unit.low) {
        diagUnits.emplace_back(&unit);
        auto newsccUnit = std::make_unique<DepsUnit<T>>(unit);
        newsccUnit->depsUnits.clear();
        node2SccUnitMap[unit.element] = newsccUnit.get();
        newsccUnit->circularDepElements.emplace_back(unit.element);
        while (st.top() != &unit) {
            auto topUnit = st.top();
            topUnit->stackMember = false;
            diagUnits.emplace_back(topUnit);
            node2SccUnitMap[topUnit->element] = newsccUnit.get();
            newsccUnit->circularDepElements.emplace_back(topUnit->element);
            st.pop();
        }
        auto topUnit = st.top();
        topUnit->stackMember = false;
        st.pop();
        DetectCircularDep();
        newSccUnits.emplace_back(newsccUnit.get());
        allNewSccUnits.emplace_back(std::move(newsccUnit));
    }
    diagUnits.clear();
}

VarCirDepsChecker::VarCirDepsChecker(DiagAdapter& ciDiag) : diag(&ciDiag)
{
}

bool VarCirDepsChecker::IsVar(const AST::Decl& decl) const
{
    if (decl.astKind == AST::ASTKind::VAR_DECL || decl.astKind == AST::ASTKind::VAR_WITH_PATTERN_DECL) {
        return true;
    }
    return false;
}

void VarCirDepsChecker::DetectCircularDep()
{
    if (diagUnits.size() <= 1) {
        return;
    }
    bool hasVar = false;
    for (auto iter = diagUnits.begin(); iter != diagUnits.end(); ++iter) {
        if (IsVar(*(*iter)->element)) {
            hasVar = true;
            break;
        }
    }
    // Beware: If one or more var in circular dependency, we need to diag
    if (!hasVar) {
        return;
    }
    std::string diagStr;
    for (auto iter = diagUnits.begin(); iter != diagUnits.end(); ++iter) {
        auto decl = StaticCast<AST::Decl*>((*iter)->element);
        std::string varName = decl->identifier;
        auto line = ":" + std::to_string((*iter)->element->begin.line);
        auto column = ":" + std::to_string((*iter)->element->begin.column);
        std::string temp = varName + " in " + (*iter)->element->curFile->fileName + line + column;
        if (iter == diagUnits.begin()) {
            diagStr = temp;
        } else {
            diagStr = diagStr + " and " + temp;
        }
    }
    diag->Diagnose(DiagKind::chir_var_might_circular_dependency, diagStr);
}

FileCirDepsChecker::FileCirDepsChecker(DiagAdapter& ciDiag, const std::vector<std::string>& vardiagLog)
    : diag(&ciDiag), diagLog(vardiagLog)
{
}

void FileCirDepsChecker::InitFileDep(const FileDepMap& depMap)
{
    std::vector<Ptr<const AST::File>> files;
    for (auto& dep : depMap.stableOrderValue) {
        files.emplace_back(dep.first);
    }
    InitDependency(files, depMap.randomOrderValue);
}

void FileCirDepsChecker::DetectCircularDep()
{
    if (diagUnits.size() <= 1) {
        return;
    }
    std::string diagStr;
    for (auto iter = diagUnits.begin(); iter != diagUnits.end(); ++iter) {
        std::string fileName = (*iter)->element->fileName;
        if (iter == diagUnits.begin()) {
            diagStr = fileName;
        } else {
            diagStr = diagStr + " and " + fileName;
        }
    }
    diag->Diagnose(DiagKind::chir_file_might_circular_dependency, diagStr);
#if DEBUG0
    for (auto strlog : diagLog) {
        std::cout << strlog << std::endl;
    }
#endif
}
