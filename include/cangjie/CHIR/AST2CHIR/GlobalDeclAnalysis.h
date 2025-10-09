// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_GLOBALDECLANALYSIS_H
#define CANGJIE_CHIR_GLOBALDECLANALYSIS_H

#include <list>
#include <queue>
#include <stack>

#include "cangjie/AST/Node.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Sema/GenericInstantiationManager.h"

namespace Cangjie::CHIR {
class GlobalDeclAnalysis {
public:
    GlobalDeclAnalysis(DiagAdapter& ciDiag, const GenericInstantiationManager* gim, IncreKind kind,
        const ElementList<Ptr<const AST::Decl>>& funcsAndVars, const ElementList<Ptr<const AST::Decl>>& localConstVars,
        const StaticInitInfoMap& staticInitFuncInfoMap, bool outputChir = false, bool mergingPlatform = false)
        : diag(&ciDiag),
          gim(gim),
          kind(kind),
          funcsAndVars(funcsAndVars),
          localConstVars(localConstVars),
          staticInitFuncInfoMap(staticInitFuncInfoMap),
          outputChir(outputChir),
          mergingPlatform(mergingPlatform)
    {
    }

    /**
    * @brief analysis circular dependency in global var decls
    *
    * @param nodesWithDeps AST decls
    * @param fileAndVarMap key is file, value is decls in this file
    * @param cachedInfo incremental compilation cached info
    * @return InitOrder ordered var decls
    */
    InitOrder Run(const ElementList<Ptr<const AST::Decl>>& nodesWithDeps,
        const std::unordered_map<Ptr<const AST::File>, std::vector<Ptr<const AST::Decl>>>& fileAndVarMap,
        CompilationCache& cachedInfo);

private:
    bool SkipDependencyTo(const AST::Decl& decl);

    void CheckUseBeforeInit(const std::vector<Ptr<const AST::Decl>>& vars);

    void AnalysisFileDependencyImpl(const Ptr<const AST::Decl>& rootDecl, const Ptr<const AST::Decl>& curDecl,
        std::unordered_set<Ptr<const AST::Decl>>& visited);
    void AnalysisFileDependency();

    void AnalysisGlocalVarsAndLocalConstVarsDependencyImpl(const Ptr<const AST::Decl>& rootDecl,
        const Ptr<const AST::Decl>& curDecl, std::unordered_set<Ptr<const AST::Decl>>& visited);
    void AnalysisGlobalVarsAndLocalConstVarsDependency(const InitOrder& initOrder);
    void AnalysisDependency(const ElementList<Ptr<const AST::Decl>>& nodesWithDeps);
    void AdditionalAnalysisDepOfNonStaticCtor(const AST::FuncDecl& func,
        std::vector<Ptr<const AST::Decl>>& dependencies, std::vector<Ptr<const AST::Decl>>& localConstVarDeps);
    void AdditionalAnalysisDepOfStaticInit(
        const Ptr<const AST::FuncDecl>& staticInit, std::vector<Ptr<const AST::Decl>>& dependencies) const;

    void WalkAndCollectDep(const AST::Node& curNode, std::vector<Ptr<const AST::Decl>>& dependencies,
        std::vector<Ptr<const AST::Decl>>& localConstVarDeps);
    void AnalysisDepOf(const AST::Decl& rootDecl, std::vector<Ptr<const AST::Decl>>& dependencies,
        std::vector<Ptr<const AST::Decl>>& localConstVarDeps);
    void ParseVarWithPatternDecl(const AST::VarWithPatternDecl& root);
    void ParsePattern(const AST::VarWithPatternDecl& rootDecl, AST::Pattern& pattern);
    void AddDependencyImpl(const AST::Decl& depDecl, std::vector<Ptr<const AST::Decl>>& dependencies) const;
    void AddDependency(const AST::Decl& depDecl, std::vector<Ptr<const AST::Decl>>& dependencies);
    AST::VisitAction VisitExprAction(const AST::Expr& expr, std::vector<Ptr<const AST::Decl>>& dependencies,
        std::vector<Ptr<const AST::Decl>>& localConstVarDeps);
    AST::VisitAction CollectDepInCallExpr(
        const AST::CallExpr& callExpr, std::vector<Ptr<const AST::Decl>>& dependencies);
    AST::VisitAction CollectDepInLitConstExpr(
        const AST::LitConstExpr& litConstExpr, std::vector<Ptr<const AST::Decl>>& dependencies);

    InitOrder MergeLocalConstVarInitOrder(
        const InitOrder& initOrder, const std::vector<Ptr<const AST::Decl>> sortedVars) const;

    std::vector<Ptr<const AST::File>> SortGlobalVarDep(const ElementList<Ptr<const AST::Decl>>& nodesWithDeps,
        const std::unordered_map<Ptr<const AST::File>, std::vector<Ptr<const AST::Decl>>>& fileAndVarMap,
        CompilationCache& cachedInfo);
    InitOrder SortLocalConstVarDep(const InitOrder& initOrder);

    DiagAdapter* diag;
    const GenericInstantiationManager* gim;
    IncreKind kind;

    // All funcs and variables which will form the dependency graph by their dependencies
    ElementList<Ptr<const AST::Decl>> funcsAndVars;
    // All local const variables which will be lifted to global
    const ElementList<Ptr<const AST::Decl>>& localConstVars;
    // The info of what static init func initialize what static variables
    const StaticInitInfoMap& staticInitFuncInfoMap;

    // A map to track what `VarDecl`s are hold by `VarWithPatternDecl`
    std::unordered_map<Ptr<const AST::Node>, Ptr<const AST::VarWithPatternDecl>> varPtternDeclMap;
    // A map to track the dependencies between the funcs and global/static variables
    DeclDepMap funcsAndVarsDepMap;
    // A map to track the dependencies from the funcs and global/static variables to local const vars
    std::unordered_map<Ptr<const AST::Decl>, std::vector<Ptr<const AST::Decl>>> localConstVarsDepMap;
    // A map to track the dependencies between the files
    FileDepMap fileDependencyMap;
    // A map to track the dependencies between global/static variables in same file, this will be used to check
    // `use before initialization` error
    std::unordered_map<Ptr<const AST::Decl>, std::unordered_set<Ptr<const AST::Decl>>> var2varDepsInFile;
    // A map to track the dependencies between the global/static variables and local const variables, this wil be used
    // to determine a final init order of all variables
    DeclDepMap globalVarsAndLocalConstVarsDepMap;
    bool outputChir = false;
    bool mergingPlatform = false;
};

template <class T> class DepsUnit {
public:
    T element;
    std::vector<DepsUnit*> depsUnits;
    std::vector<DepsUnit*> userUnits;
    std::vector<T> circularDepElements;
    int disc = -1;
    int low = -1;
    bool stackMember = false;
    explicit DepsUnit(T ele)
    {
        this->element = ele;
    }
    void AddDeps(DepsUnit& unit)
    {
        auto pos = std::find(depsUnits.begin(), depsUnits.end(), &unit);
        if (pos == depsUnits.end()) {
            depsUnits.emplace_back(&unit);
        }
    }
    void AddUsers(DepsUnit& unit)
    {
        auto pos = std::find(userUnits.begin(), userUnits.end(), &unit);
        if (pos == userUnits.end()) {
            (void)userUnits.emplace_back(&unit);
        }
    }
};

// use class to provide sort ability
template <class T> class TarjanSort {
public:
    void InitDependency(const std::vector<T>& rawElements, const std::unordered_map<T, std::vector<T>>& dependencyMap);
    void DoTarjanSort();
    void TarjanSortImpl(DepsUnit<T>& unit, int& index, std::stack<DepsUnit<T>*>& st);
    void UpdateSccUnit(std::unordered_map<T, std::vector<T>>& dependencyMap);
    void UpdateSccUnitDeps(DepsUnit<T>& unit, std::unordered_map<T, std::vector<T>>& dependencyMap);

    std::vector<T> GetOrderElement();
    virtual void DetectCircularDep() = 0;
    virtual std::string GetEleStr(T) const = 0;
    virtual ~TarjanSort();
    std::vector<DepsUnit<T>*> newSccUnits;
    std::vector<std::unique_ptr<DepsUnit<T>>> allNewSccUnits;
    std::map<T, DepsUnit<T>*> node2SccUnitMap;

    std::vector<DepsUnit<T>*> diagUnits;
    std::vector<DepsUnit<T>*> units;
    std::vector<std::unique_ptr<DepsUnit<T>>> allUnits;
    std::unordered_set<DepsUnit<T>*> visitedUnits;
    std::map<T, DepsUnit<T>*> node2UnitMap;
};
class VarCirDepsChecker : public TarjanSort<Ptr<const AST::Decl>> {
public:
    explicit VarCirDepsChecker(DiagAdapter& ciDiag);
    void DetectCircularDep() override;
    std::string GetEleStr(Ptr<const AST::Decl> decl) const override
    {
        return decl->identifier;
    }
    bool IsVar(const AST::Decl& decl) const;
    DiagAdapter* diag;
    std::vector<std::string> diagLog;
};

class FileCirDepsChecker : public TarjanSort<Ptr<const AST::File>> {
public:
    FileCirDepsChecker(DiagAdapter& ciDiag, const std::vector<std::string>& vardiagLog);
    void InitFileDep(const FileDepMap& depMap);
    void DetectCircularDep() override;
    std::string GetEleStr(Ptr<const AST::File> node) const override
    {
        return node->fileName;
    }
    DiagAdapter* diag;
    std::vector<std::string> diagLog;
};

} // namespace Cangjie::CHIR

#endif
