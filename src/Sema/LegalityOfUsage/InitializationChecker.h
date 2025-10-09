// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 *  This file declares a class to check initilization of variables.
 */

#ifndef CANGJIE_SEMA_LEGALITY_OF_USAGE_INITIALIZATION_CHECKER_H
#define CANGJIE_SEMA_LEGALITY_OF_USAGE_INITIALIZATION_CHECKER_H

#include "TypeCheckerImpl.h"

#include <unordered_map>
#include <unordered_set>

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Frontend/CompilerInstance.h"

namespace Cangjie {
class InitializationChecker {
public:
    static void Check(CompilerInstance& compilerInstance, const ASTContext& ctx, Ptr<AST::Node> n)
    {
        InitializationChecker checker(compilerInstance, ctx);
        checker.CheckInitialization(n);
    }
private:
    explicit InitializationChecker(CompilerInstance& ci, const ASTContext& ctx)
        : ctx(ctx), diag(ci.diag), opts{ci.invocation.globalOptions}
    {
    }

    void CheckInitialization(Ptr<AST::Node> n);
        /**
     * Check whether @p ref has let flag. if @p ref has let flag and is in a constructor, report error.
     *
     * @param ref The expression should be checked.
     */
    void CheckLetFlag(const AST::Expr& ae, const AST::Expr& expr);
    /**
     * Check whether the refExpr used in expr is initialized.
     * @return Return true if the expr is initialized.
     */
    bool CheckInitInExpr(Ptr<AST::Node> node);
    bool CheckInitInRefExpr(const AST::RefExpr& re);
    bool CheckInitInMemberAccess(AST::MemberAccess& ma);
    bool CheckInitInAssignExpr(const AST::AssignExpr& ae);
    bool CheckInitInTryExpr(const AST::TryExpr& te);
    bool CheckInitInBinaryExpr(const AST::BinaryExpr& be);
    bool CheckInitInCondition(AST::Expr& e);
    bool CheckInitInForInExpr(const AST::ForInExpr& fie);
    bool CheckInitInWhileExpr(const AST::WhileExpr& we);
    void CheckInitInVarDecl(AST::VarDecl& vd);
    void CheckInitInVarWithPatternDecl(AST::VarWithPatternDecl& vpd);
    void CheckInitInFuncBody(const AST::FuncBody& fb);
    void CheckInitInExtendDecl(const AST::ExtendDecl& ed);
    void CheckInitInClassDecl(const AST::ClassDecl& cd);
    void CheckLetFlagInMemberAccess(
        const AST::Expr& ae, const AST::MemberAccess& ma, bool inInitFunction);
    CollectDeclsInfo CollectDecls(const AST::Decl& decl);
    void CollectToDeclsInfo(const OwnedPtr<AST::Decl>& decl, CollectDeclsInfo& info);
    void CheckInitInConstructors(
        AST::FuncDecl& fd, const std::vector<Ptr<AST::Decl>>& unInitNonFuncDecls);
    void CheckStaticInitForTypeDecl(const AST::InheritableDecl& id);
    /**
     * Check the initialization status in a constructor
     * @param decls the decls in the body of a class or struct
     */
    void CheckInitInTypeDecl(const AST::Decl& inheritDecl,
        const std::vector<Ptr<AST::Decl>>& superClassNonFuncDecls = std::vector<Ptr<AST::Decl>>());
    void GetNonFuncDeclsInSuperClass(const AST::ClassDecl& cd, std::vector<Ptr<AST::Decl>>& superClassNonFuncDecls,
        std::set<Ptr<AST::Decl>>& superClasses);
    /**
     * Check the initialization status in the Loop,
     * and unset the initialization to the variables out of the loop if @p shouldUnset is set.
     * @param block The block of a loop
     */
    void CheckInitInLoop(Ptr<AST::Block> block, bool shouldUnset = true);
    /**
     * Check the initialization status of the match expression @p me ,
     * an uninitialized variable can get initialized in the match expression,
     * only if it is initialized in each match-case block
     */
    bool CheckInitInMatchExpr(AST::MatchExpr& me);
    /**
     * Check the initialization status of the if expression @p ie ,
     * an uninitialized variable can get initialized in the 'if' expression,
     * only if the if expression has the else block and the variable is initialized in each match-case block.
     */
    bool CheckInitInIfExpr(AST::IfExpr& ie);
    /**
     * Check the initialization status in the given conditional reached @p expr of the condition expr
     */
    void CheckInitInCondBlock(AST::Expr& expr,
        const std::unordered_set<Ptr<AST::Decl>>& uninitsDecls,
        std::unordered_set<Ptr<AST::Decl>>& commonInitedDeclsOfBranches, bool& firstInitBranchInited);
    std::unordered_set<Ptr<AST::Decl>> CheckAndGetConditionalInitDecls(
        AST::Expr& expr, const std::unordered_set<Ptr<AST::Decl>>& uninitsDecls);
    bool IsOrderRelated(const AST::Node& checkNode, AST::Node& targetNode, bool isClassLikeOrStruct) const;
    bool IsVarUsedBeforeDefinition(const AST::Node& checkNode, AST::Node& targetNode) const;
    bool CheckIllegalMemberAccess(const AST::Expr& expr, const AST::Decl& target, const AST::Node& targetStruct);
    bool CheckIllegalRefExprAccess(
        const AST::RefExpr& re, const AST::Symbol& toplevelSymOfRe, const AST::Symbol& toplevelSymOfTarget);

    void UpdateScopeStatus(const AST::Node& node);
    void UpdateInitializationStatus(const AST::AssignExpr& assign, AST::Decl& decl);
    void ClearScopeStatus(const std::string& scopeName)
    {
        contextVariables.erase(scopeName);
        variablesBeforeTeminatedScope.erase(scopeName);
        initVarsAfterTerminator.erase(scopeName);
    }
    void CheckNonCommonVariablesInitInCommonDecl(const AST::InheritableDecl& id);
    void RecordInstanceVariableUsage(const AST::Decl& target);
    const ASTContext& ctx;
    DiagnosticEngine& diag;
    const GlobalOptions& opts;
    /**
     * Map of the variables defined in visible scopes.
     * eg: func foo() {
     *       let x : Int64  // map: s0: x
     *       if (condition) {
     *         let y = 2    // map: s0: x; s1: y
     *       }
     *       var z = 1      // map: s0: x, z
     *     }
     */
    std::unordered_map<std::string, std::unordered_set<Ptr<const AST::VarDecl>>> contextVariables;
    /**
     * Map of varaibles declared before the specific specific is terminated by 'throw', 'return', 'break' or 'continue'
     * eg: func foo() {
     *       let x : Int64
     *       if (condition) {
     *         let y = 2
     *         return 1  <--- this line terminated the scope of if-then branch, the collected decls are 'x' and 'y'.
     *         let a = 1
     *         return a
     *       }
     *     }
     */
    std::unordered_map<std::string, std::unordered_set<Ptr<const AST::Decl>>> variablesBeforeTeminatedScope;
    /**
     * Map of constructor to uninitialized member variables when meeting return expression inside ctor.
     * eg: class A {
     *       init(a: Int64, c: Bool) {
     *          let b = 1
     *          if (c) {
     *              return <-- map will be updated here: 'init(a: Int64, c: Bool)' -> 'x'
     *          }
     *          x = a
     *          println(x) <-- should not report unitialized
     *       }
     *       public var x: Int64
     *     }
     */
    std::unordered_map<Ptr<const AST::FuncDecl>, std::unordered_set<Ptr<const AST::Decl>>> ctorUninitVarsMap;
    /**
     * Map of first terminating kind of the specific specific.
     * eg: func foo() {
     *       throw Exception()  <-- only record the kind of throw, ignore later kind 'return'
     *       retrun 1
     *     }
     */
    std::unordered_map<std::string, AST::ASTKind> scopeTerminationKinds;
    /**
     * Map of initiliazed variables after terminate expression in the specific scope.
     * eg: func foo() {
     *       var a : Int64 // s0
     *       if (condition) { // s1
     *           throw Exception()  <-- terminate expresson
     *           a = 1       <-- map updated: 's1' -> 'a'
     *           println(a)  <-- privious initialization is reachable for current line, will not report error
     *       } else { a = 2 }
     *       println(a)  <-- initialization in if-then body is unreachable, will report 'used before initialization'
     *     }
     */
    std::unordered_map<std::string, std::unordered_set<Ptr<const AST::Decl>>> initVarsAfterTerminator;
    /**
     * Holds a dependencies to other member variables when analyzing initializer of meber variable
     * eg: let a = b + 1 // { b }
     */
    std::optional<std::unordered_set<Ptr<const AST::Decl>>> currentInitializingVarDependencies;
    /**
     * When current is in the context that may not run termination as normal, 'optionalCtxDepth' plus 1.
     * eg: at the right hand side of 'coalescing', 'and', 'or' expressions.
     */
    size_t optionalCtxDepth{0};
    /** When current is inside the tryBlock, 'tryDepth' plus 1. */
    size_t tryDepth{0};
};
} // namespace Cangjie
#endif
