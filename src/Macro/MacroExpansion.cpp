// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/Macro/MacroExpansion.h"

#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/ConditionalCompilation/ConditionalCompilation.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Macro/MacroEvaluation.h"
#include "cangjie/Macro/TestEntryConstructor.h"
#include "cangjie/Parse/Parser.h"

#include <cstdio>
#include <fstream>

#include "MacroUpdateInfo.h"

using namespace Cangjie;
using namespace AST;

namespace {

bool HasNoMacros(const MacroCollector& mc)
{
    return mc.macroDefFuncs.empty() && mc.macCalls.empty();
}

bool HasNoMacroCalls(const std::vector<MacroCall>& macCalls)
{
    return macCalls.empty();
}

bool HasDefAndCallInSamePkg(const MacroCollector& macroCollector, DiagnosticEngine& diag)
{
    // macro-def and macro-call can't be in the same package.
    bool ret = false;
    std::unordered_set<std::string> s1;
    for (auto fd : macroCollector.macroDefFuncs) {
        (void)s1.insert(fd->identifier);
    }
    for (auto call : macroCollector.macCalls) {
        if (s1.find(call.GetFullName()) != s1.end()) {
            ret = true;
            (void)diag.Diagnose(call.GetBeginPos(), DiagKind::macro_unexpect_def_and_call_in_same_pkg);
        }
    }
    return ret;
}

bool HasMacroCallInNode(const OwnedPtr<Node>& node, DiagnosticEngine& diag)
{
    bool hasMacroCall = false;
    auto checkMacroCall = [&hasMacroCall, &node, &diag](Ptr<const Node> curNode) -> VisitAction {
        if (curNode->IsMacroCallNode()) {
            hasMacroCall = true;
            (void)diag.Diagnose(*node, curNode->GetConstInvocation()->identifierPos,
                DiagKind::macro_undeclared_identifier, curNode->GetConstInvocation()->identifier);
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker macWalker(node.get(), checkMacroCall);
    macWalker.Walk();
    return hasMacroCall;
}

void CollectMacroEachFile(File& file, MacroCollector& collector)
{
    auto collectFunc = [&collector](Ptr<Node> curNode) -> VisitAction {
        if (curNode->astKind == ASTKind::MACRO_DECL) {
            // macro definitions.
            (void)collector.macroDefFuncs.emplace_back(RawStaticCast<MacroDecl*>(curNode));
        }
        UpdateMacroInfo(curNode, collector);
        if (curNode->IsMacroCallNode()) {
            return VisitAction::SKIP_CHILDREN;
        }
        return VisitAction::WALK_CHILDREN;
    };
    Walker macWalker(&file, collectFunc);
    macWalker.Walk();
}

void CheckUnhandledMacroCall(Package& package, DiagnosticEngine& diag)
{
    if (diag.GetErrorCount() > 0) {
        // Already have diagnose error, no need to check.
        return;
    }
    for (const auto& file : package.files) {
        if (!file || !file->hasMacro) {
            continue;
        }
        // Check whether there are unhandled macrocalls.
        auto checkMacroCall = [&diag](Ptr<const Node> curNode) -> VisitAction {
            // do not report on @IfAvailable, to be dealt with later
            if (auto me = DynamicCast<MacroExpandExpr>(curNode); me && me->invocation.IsIfAvailable()) {
                return VisitAction::WALK_CHILDREN;
            }
            if (curNode->IsMacroCallNode()) {
                (void)diag.Diagnose(*curNode, curNode->GetConstInvocation()->identifierPos,
                    DiagKind::macro_undeclared_identifier, curNode->GetConstInvocation()->identifier);
                return VisitAction::SKIP_CHILDREN;
            }
            return VisitAction::WALK_CHILDREN;
        };
        Walker mcWalker(file.get(), checkMacroCall);
        mcWalker.Walk();
    }
}

void CheckWhenAfterMacroExpand(Ptr<const Node> curNode, DiagnosticEngine& diag)
{
    // Check whether there are unhandled macrocalls.
    auto checkWhen = [&diag](Ptr<const Node> curNode) -> VisitAction {
        if (auto decl = DynamicCast<Decl*>(curNode); decl) {
            auto annotation = std::find_if(decl->annotations.begin(), decl->annotations.end(),
                [](auto& anno) { return anno->kind == AST::AnnotationKind::WHEN; });
            if (annotation != decl->annotations.end()) {
                diag.DiagnoseRefactor(DiagKindRefactor::conditional_compilation_unexpected_after_macro, **annotation);
            }
        }
        return VisitAction::WALK_CHILDREN;
    };
    ConstWalker walker(curNode, checkWhen);
    walker.Walk();
}
} // namespace

void MacroExpansion::ReplaceEachFileNode(const File& file)
{
    auto debugFileID = ci->GetSourceManager().GetFileID(file.macroCallFilePath);
    if (debugFileID == -1) {
        return;
    }
    auto newBuffer = ci->GetSourceManager().GetSource(static_cast<unsigned int>(debugFileID)).buffer;
    Parser newParser(static_cast<unsigned int>(debugFileID), newBuffer, ci->diag, ci->diag.GetSourceManager(),
        ci->invocation.globalOptions.enableAddCommentToAst);
    auto names = Utils::SplitQualifiedName(file.curPackage->fullPackageName);
    std::string moduleName = names.size() > 1 ? names[0] : ""; // Only used for 'std' package case.
    (void)newParser.SetModuleName(moduleName).EnableCustomAnno();
    auto newFile = newParser.ParseTopLevel();
    ConditionalCompilation cc{ci};
    cc.HandleFileConditionalCompilation(*newFile);
    if (curPackage) {
        ci->importManager.UpdateFileNodeImportInfo(*curPackage, file, newFile);
    }
}

void MacroExpansion::CollectMacros(Package& package)
{
    // Each time we collect macros, we should reset the old one.
    macroCollector.clear();
    macroCollector.curPkg = &package;
    size_t oldMacCallNum = 0;
    size_t oldMacDefNum = 0;
    for (const auto& file : package.files) {
        if (!file || !file->hasMacro) {
            continue;
        }
        oldMacCallNum = macroCollector.macCalls.size();
        oldMacDefNum = macroCollector.macroDefFuncs.size();
        CollectMacroEachFile(*file.get(), macroCollector);

        // it is certain that at least one of macCalls and macroDefFuncs will increase, because file->hasMacro is true.
        CJC_ASSERT(ci->diag.GetErrorCount() != 0 ||
            ((macroCollector.macCalls.size() > oldMacCallNum || macroCollector.macroDefFuncs.size() > oldMacDefNum) &&
                "Collect Macro Failed."));
    }
    // The macroCall is not collected by order strictly, need sort.
    std::sort(macroCollector.macCalls.begin(), macroCollector.macCalls.end(), [](auto& m1, auto& m2) -> bool {
        auto pos1 = m1.GetBeginPos();
        auto pos2 = m2.GetBeginPos();
        return std::tie(pos1.fileID, pos1) < std::tie(pos2.fileID, pos2);
    });
    macroCollector.importedMacroPkgs = ci->importManager.GetImportedPkgsForMacro();
}

void MacroExpansion::CheckReplacedEnumCaseMember(MacroCall& macroNode, PtrVector<Decl>& newNodes) const
{
    auto invocation = macroNode.GetInvocation();
    if (!invocation || !invocation->parent) {
        return;
    }
    if (invocation->parent->astKind != ASTKind::ENUM_DECL) {
        return;
    }
    auto ed = StaticAs<ASTKind::ENUM_DECL>(invocation->parent);
    if (macroNode.GetNode()->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
        if (!newNodes[0].get()->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
            (void)ci->diag.Diagnose(*newNodes[0].get(), DiagKind::macro_expect_enum_constructor);
        }
        if (newNodes.size() > 1) {
            (void)ci->diag.Diagnose(*newNodes[1].get(), DiagKind::macro_expect_one_enum_constructor);
        }
        if (newNodes[0]->astKind == ASTKind::FUNC_DECL) {
            ed->hasArguments = true;
        }
    } else {
        (void)std::for_each(newNodes.begin(), newNodes.end(), [&, this](auto& decl) {
            if (decl.get()->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
                (void)ci->diag.Diagnose(
                    *decl, DiagKind::macro_expect_decl_not_enum_constructor, decl->identifier.Val());
            }
        });
    }
}

template <typename T>
void MacroExpansion::CheckNodesConsistency(
    PtrVector<T>& nodes, PtrVector<AST::Node>& newNodes, VectorTarget<OwnedPtr<T>>& target) const
{
    (void)std::for_each(newNodes.begin(), newNodes.end(), [&, this](auto& node) {
        auto targetNode = dynamic_cast<T*>(node.get().get());
        if (!targetNode) {
            (void)ci->diag.Diagnose(*node, DiagKind::macro_expect_declaration);
            return;
        }
        // Assign the outer decl.
        targetNode->outerDecl = (target.pointer->begin() + static_cast<int32_t>(target.loc))->get()->outerDecl;
        (void)nodes.emplace_back(static_cast<T*>(node.release()));
    });
}

void MacroExpansion::ReplaceDecls(
    MacroCall& macroNode, PtrVector<AST::Node>& newNodes, VectorTarget<OwnedPtr<Decl>>& target) const
{
    if (newNodes.empty()) {
        auto file = macroNode.GetNode()->curFile;
        if (file) {
            (void)file->originalMacroCallNodes.emplace_back(target.pointer->at(target.loc).release());
        }
        (void)target.pointer->erase(target.pointer->begin() + static_cast<int32_t>(target.loc));
        return;
    }
    PtrVector<Decl> decls;
    CheckNodesConsistency(decls, newNodes, target);
    if (decls.empty()) {
        return;
    }
    CheckReplacedEnumCaseMember(macroNode, decls);
    (void)decls[0]->curFile->originalMacroCallNodes.emplace_back(target.pointer->at(target.loc).release());
    (void)target.pointer->erase(target.pointer->begin() + static_cast<int32_t>(target.loc)); // Erase macro Node.
    (void)target.pointer->insert(target.pointer->begin() + static_cast<int32_t>(target.loc), // Insert real macro Node.
        std::make_move_iterator(decls.begin()), std::make_move_iterator(decls.end()));
}

void MacroExpansion::CheckReplacedFuncParamList(
    const MacroCall& macroNode, const VectorTarget<OwnedPtr<FuncParam>>& target) const
{
    auto invocation = macroNode.GetInvocation();
    if (!invocation || !invocation->parent) {
        return;
    }
    if (invocation->parent->astKind != ASTKind::FUNC_PARAM_LIST) {
        return;
    }
    bool meetNamedParameter = false;
    for (auto it = target.pointer->begin(); it != target.pointer->end(); ++it) {
        if (!meetNamedParameter && (*it)->isNamedParam) {
            meetNamedParameter = true;
        } else if (meetNamedParameter && !(*it)->isNamedParam) {
            (void)ci->diag.Diagnose(**it, DiagKind::macro_named_parameter_after_unnamed);
        }
    }
}

void MacroExpansion::ReplaceParams(
    MacroCall& macroNode, PtrVector<Node>& newNodes, VectorTarget<OwnedPtr<FuncParam>>& target) const
{
    if (newNodes.empty()) {
        (void)ci->diag.Diagnose(macroNode.GetBeginPos(), DiagKind::macro_unexpected_empty_parameter);
        return;
    }
    PtrVector<FuncParam> decls;
    CheckNodesConsistency(decls, newNodes, target);
    if (decls.empty()) {
        return;
    }
    (void)decls[0]->curFile->originalMacroCallNodes.emplace_back(target.pointer->at(target.loc).release());
    (void)target.pointer->erase(target.pointer->begin() + static_cast<int32_t>(target.loc));
    (void)target.pointer->insert(target.pointer->begin() + static_cast<int32_t>(target.loc),
        std::make_move_iterator(decls.begin()), std::make_move_iterator(decls.end()));
    CheckReplacedFuncParamList(macroNode, target);
}

void MacroExpansion::ReplaceEachMacroHelper(MacroCall& macroNode, PtrVector<Node>& newNodes) const
{
    auto file = macroNode.GetNode()->curFile;
    if (!file) {
        return;
    }
    if (auto expr = std::get_if<OwnedPtr<Expr>*>(&macroNode.replaceLoc); expr) { // For all expr.
        if (newNodes.empty()) {
            (void)ci->diag.Diagnose(macroNode.GetBeginPos(), DiagKind::macro_unexpect_no_expr);
            return;
        }
        if (newNodes.size() == 1 && DynamicCast<Expr*>(newNodes[0].get())) {
            (void)file->originalMacroCallNodes.emplace_back((*expr)->release());
            (*expr)->reset(RawStaticCast<Expr*>(newNodes[0].release()));
            return;
        }
        (void)ci->diag.Diagnose(*newNodes.back(), DiagKind::macro_expect_one_expr);
    } else if (auto nodes = std::get_if<VectorTarget<OwnedPtr<Node>>>(&macroNode.replaceLoc); nodes) {
        if (newNodes.empty()) {
            (void)file->originalMacroCallNodes.emplace_back((*nodes).pointer->at((*nodes).loc).release());
            (void)(*nodes).pointer->erase((*nodes).pointer->begin() + static_cast<int32_t>((*nodes).loc));
            return;
        }
        (void)file->originalMacroCallNodes.emplace_back((*nodes).pointer->at((*nodes).loc).release());
        // Erase macro Node.
        (void)(*nodes).pointer->erase((*nodes).pointer->begin() + static_cast<int32_t>((*nodes).loc));
        // Insert real macro Node.
        (void)(*nodes).pointer->insert((*nodes).pointer->begin() + static_cast<int32_t>((*nodes).loc),
            std::make_move_iterator(newNodes.begin()), std::make_move_iterator(newNodes.end()));
    } else if (auto exprs = std::get_if<VectorTarget<OwnedPtr<Expr>>>(&macroNode.replaceLoc); exprs) {
        if (newNodes.empty()) {
            (void)ci->diag.Diagnose(macroNode.GetBeginPos(), DiagKind::macro_unexpect_no_expr);
            return;
        }
        if (newNodes.size() == 1 && DynamicCast<Expr*>(newNodes[0].get())) {
            (void)file->originalMacroCallNodes.emplace_back((*exprs).pointer->at((*exprs).loc).release());
            // Erase macro Node.
            (void)(*exprs).pointer->erase((*exprs).pointer->begin() + static_cast<int32_t>((*exprs).loc));
            (void)(*exprs).pointer->insert((*exprs).pointer->begin() + static_cast<int32_t>((*exprs).loc),
                OwnedPtr<Expr>(RawStaticCast<Expr*>(newNodes.front().release())));
            return;
        }
        (void)ci->diag.Diagnose(*newNodes.back(), DiagKind::macro_expect_one_expr);
    } else if (auto decls = std::get_if<VectorTarget<OwnedPtr<Decl>>>(&macroNode.replaceLoc); decls) {
        ReplaceDecls(macroNode, newNodes, *decls);
    } else if (auto params = std::get_if<VectorTarget<OwnedPtr<FuncParam>>>(&macroNode.replaceLoc); params) {
        ReplaceParams(macroNode, newNodes, *params);
    } else {
        (void)ci->diag.Diagnose(macroNode.GetBeginPos(), DiagKind::macro_expand_invalid_node_replace);
    }
}

void MacroExpansion::ReplaceEachMacro(MacroCall& macCall)
{
    auto pInvocation = macCall.GetInvocation();
    // macro expand failed will get ILLEGAL token.
    if (MacroExpandFailed(pInvocation->newTokens) || macCall.status == MacroEvalStatus::FAIL) {
        return;
    }
    Parser parser{pInvocation->newTokens, ci->diag, ci->diag.GetSourceManager(),
        ci->invocation.globalOptions.enableAddCommentToAst, ci->invocation.globalOptions.compileCjd};
    parser.SetCompileOptions(ci->invocation.globalOptions);
    (void)parser.SetPrimaryDecl(pInvocation->outerDeclIdent).EnableCustomAnno();
    // Reparsing newTokens to get macro-generate ASTs.
    auto nodes = parser.ParseNodes(
        pInvocation->scope, *macCall.GetNode(), macCall.GetModifiers(), macCall.GetAnnotations());
    for (auto& node : nodes) {
        if (!node || node->IsInvalid()) {
            return;
        }
        // Make sure no macro calls exist after macro expansion.
        if (HasMacroCallInNode(node, ci->diag)) {
            return;
        }
        CheckWhenAfterMacroExpand(node, ci->diag);
    }
    ReplaceEachMacroHelper(macCall, nodes);
}

void MacroExpansion::ReplaceAST(Package& package)
{
    // Normal mode or Debug mode: replace all macro nodes, and check if ast is valid.
    auto macroCalls = macroCollector.macCalls;
    std::reverse(macroCalls.begin(), macroCalls.end());
    for (auto mc : macroCalls) {
        ReplaceEachMacro(mc);
    }
    if (ci->diag.GetErrorCount() != 0) {
        ci->diag.EmitCategoryGroup();
    }
    // Debug mode: replace file nodes which have macro calls.
    if (ci->diag.GetErrorCount() == 0 && ci->invocation.globalOptions.enableMacroDebug) {
        for (auto& file : package.files) {
            ReplaceEachFileNode(*file);
        }
    }
}

void MacroExpansion::EvaluateMacros()
{
    // Notice: When interpreter-based evaluation strategy is introduced, just modify the
    // implementation of class MacroEvaluation. Make sure the key data structure to
    // communicate between Expansion and Evaluation.
    bool useChildProcess = ci->invocation.globalOptions.enableMacroInLSP;
    MacroEvaluation evaluator(ci, &macroCollector, useChildProcess);
    evaluator.Evaluate();
    tokensEvalInMacro = evaluator.GetVecOfGeneratedCodes();
    return;
}

// In the LSP scenario, there may be concurrent calls to macro expansion when process cjd file. If Execute is executed
// concurrently with the same macro declaration, it will cause a problem. The global lock is used to protect macro
// expansion calls in different CompilerInstances.
std::mutex globalMacroExpandLock;

void MacroExpansion::Execute(Package& package)
{
    std::lock_guard<std::mutex> guard(globalMacroExpandLock);
    curPackage = &package;
    // Step 1: Collect macro-defs, macro-calls.
    CollectMacros(package);
    if (HasNoMacros(this->macroCollector) || HasNoMacroCalls(this->macroCollector.macCalls) ||
        HasDefAndCallInSamePkg(this->macroCollector, ci->diag) ||
        (ci->diag.GetErrorCount() > 0 && !ci->invocation.globalOptions.enableMacroInLSP)) {
        return;
    }
    // Step 2: Evaluate macros. Generate new tokens for further AST replacement.
    EvaluateMacros();
    // Step 3: Map macro information and save the expanded macro contents to a file.
    ProcessMacros(package);
    // Step 4: Replace MacroCall AST with new generated AST.
    ReplaceAST(package);
}

void MacroExpansion::Execute(std::vector<OwnedPtr<AST::Package>>& packages)
{
    if (ci->invocation.globalOptions.enableCompileTest) {
        TestEntryConstructor testEntry(ci->diag);
        testEntry.CheckTestSuite(packages);
    }
    for (auto& package : packages) {
        if (package) {
            Execute(*package);
        }
    }
    if (ci->invocation.globalOptions.enableCompileTest &&
        ci->invocation.globalOptions.outputMode == GlobalOptions::OutputMode::EXECUTABLE) {
        TestEntryConstructor::ConstructTestSuite(ci->invocation.globalOptions.moduleName, packages,
            ci->importManager.GetAllImportedPackages(), ci->invocation.globalOptions.compileTestsOnly);
    }
    for (auto& package : packages) {
        AddCurFile(*package);
        CheckUnhandledMacroCall(*package, ci->diag);
    }
}
