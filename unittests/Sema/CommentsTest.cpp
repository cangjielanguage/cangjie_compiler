// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Comments related unit tests.
 */

#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>
#include "gtest/gtest.h"
#include "TestCompilerInstance.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Basic/Match.h"

using namespace Cangjie;
using namespace AST;

class CommentsTest : public testing::Test {
protected:
    void SetUp() override
    {
        instance = std::make_unique<TestCompilerInstance>(invocation, diag);
#ifdef PROJECT_SOURCE_DIR
        // Gets the absolute path of the project from the compile parameter.
        projectPath = PROJECT_SOURCE_DIR;
#else
        // Just in case, give it a default value.
        // Assume the initial is in the build directory.
        projectPath = "..";
#endif
#ifdef __x86_64__
        instance->invocation.globalOptions.target.arch = Cangjie::Triple::ArchType::X86_64;
#else
        instance->invocation.globalOptions.target.arch = Cangjie::Triple::ArchType::AARCH64;
#endif
#ifdef _WIN32
        instance->invocation.globalOptions.target.os = Cangjie::Triple::OSType::WINDOWS;
        invocation.globalOptions.executablePath = projectPath + "\\output\\bin\\";
#elif __unix__
        instance->invocation.globalOptions.target.os = Cangjie::Triple::OSType::LINUX;
        invocation.globalOptions.executablePath = projectPath + "/output/bin/";
#endif
        Cangjie::MacroProcMsger::GetInstance().CloseMacroSrv();
    }
    std::string srcPath;
    std::string projectPath;
    DiagnosticEngine diag;
    CompilerInvocation invocation;
    std::unique_ptr<TestCompilerInstance> instance;
};

TEST_F(CommentsTest, DISABLED_MacroDesugarFuncCommentsTest)
{
    std::string code = R"(
macro package hello
import std.ast.*

/**
* comment
*/
public macro StorageProp1(attr: Tokens, input: Tokens): Tokens{
    return input;
}
    )";

    instance->code = code;
    instance->invocation.globalOptions.enableMacroInLSP = true;
    instance->invocation.globalOptions.enableAddCommentToAst = true;
    instance->invocation.globalOptions.compileMacroPackage = true;
    instance->invocation.globalOptions.outputMode = GlobalOptions::OutputMode::SHARED_LIB;
    instance->Compile(CompileStage::SEMA);
    bool hit = false;
    Walker walker(instance->GetSourcePackages()[0]->files[0].get(), [&hit](Ptr<Node> node) -> VisitAction {
        Meta::match (*node)([&hit](const MacroDecl& decl) {
            if (decl.identifier == "StorageProp1" && decl.desugarDecl) {
                hit = true;
                ASSERT_FALSE(decl.desugarDecl->comments.leadingComments.empty());
            }
        });
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    ASSERT_TRUE(hit);
}

TEST_F(CommentsTest, DISABLED_MainDesugarFuncCommentsTest)
{
    std::string code = R"(
/**
* comment
*/
main () {
    0;
}
    )";

    instance->code = code;
    instance->invocation.globalOptions.enableMacroInLSP = true;
    instance->invocation.globalOptions.enableAddCommentToAst = true;
    instance->invocation.globalOptions.compileMacroPackage = true;
    instance->invocation.globalOptions.outputMode = GlobalOptions::OutputMode::SHARED_LIB;
    instance->Compile(CompileStage::SEMA);
    bool hit = false;
    Walker walker(instance->GetSourcePackages()[0]->files[0].get(), [&hit](Ptr<Node> node) -> VisitAction {
        Meta::match (*node)([&hit](const MainDecl& decl) {
            if (decl.identifier == "main" && decl.desugarDecl) {
                hit = true;
                ASSERT_FALSE(decl.desugarDecl->comments.leadingComments.empty());
            }
        });
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    ASSERT_TRUE(hit);
}

Ptr<const Node> GetCurMacro(const Decl& decl)
{
    Ptr<const Node> curCall = nullptr;
    if (decl.curMacroCall) {
        curCall = decl.curMacroCall;
    } else {
        Ptr<const Decl> tmpDecl = &decl;
        while (tmpDecl->outerDecl) {
            tmpDecl = tmpDecl->outerDecl;
            if (tmpDecl->curMacroCall) {
                curCall = tmpDecl->curMacroCall;
                break;
            }
        }
    }
    return curCall;
}

bool FindCommentsInMacroCall(const Ptr<const Decl> node, Ptr<const Node> curCall)
{
    if (!node || !curCall) {
        return false;
    }
    bool ret = false;
    std::string id = node->identifier;
    auto begin = node->GetBegin();
    auto end = node->GetEnd();
    if (curCall) {
        ConstWalker walker(curCall, [&ret, &id, &begin, &end](Ptr<const Node> node) -> VisitAction {
            Meta::match (*node)([&ret, &id, &begin, &end](const Decl& decl) {
                if (decl.identifier == id && begin == decl.GetBegin() && end == decl.GetEnd() &&
                    !decl.comments.IsEmpty()) {
                    ret = true;
                }
            });
            if (ret) {
                return VisitAction::STOP_NOW;
            }
            return VisitAction::WALK_CHILDREN;
        });
        walker.Walk();
    }
    return ret;
}

TEST_F(CommentsTest, DISABLED_CustomAnnotionCommentsTest)
{
    std::string code = R"(
@Annotation
public class APILevel {
    let a: UInt8
    const init(v: UInt8) {
        a = v
    }
}
// C2 comments
@!APILevel[
    12
]
public class C2 {
    // goo comments
    public func goo () {}
}
)";

    instance->code = code;
    instance->invocation.globalOptions.enableMacroInLSP = true;
    instance->invocation.globalOptions.enableAddCommentToAst = true;
    instance->invocation.globalOptions.compileMacroPackage = true;
    instance->invocation.globalOptions.outputMode = GlobalOptions::OutputMode::SHARED_LIB;
    instance->Compile(CompileStage::SEMA);
    bool hitC2 = false;
    bool hitGoo = false;
    Walker walker(instance->GetSourcePackages()[0]->files[0].get(), [&hitC2, &hitGoo](Ptr<Node> node) -> VisitAction {
        Meta::match (*node)([&hitC2, &hitGoo](const Decl& decl) {
            if (auto curCall = GetCurMacro(decl)) {
                bool find = FindCommentsInMacroCall(&decl, curCall);
                if (find) {
                    if (decl.identifier == "C2") {
                        hitC2 = true;
                    }
                    if (decl.identifier == "goo") {
                        hitGoo = true;
                    }
                }
            }
        });
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    ASSERT_TRUE(hitC2);
    ASSERT_TRUE(hitGoo);
}

TEST_F(CommentsTest, DISABLED_MacroCommentsTest)
{
    std::string code = R"(
import std.deriving.*
// C2 comments
@Derive[ToString]
@Derive[Equatable]
public class C2 {
    // goo comments
    public func goo () {}
}
    )";

    instance->code = code;
    instance->invocation.globalOptions.enableMacroInLSP = true;
    instance->invocation.globalOptions.enableAddCommentToAst = true;
    instance->invocation.globalOptions.compileMacroPackage = true;
    instance->invocation.globalOptions.outputMode = GlobalOptions::OutputMode::SHARED_LIB;
    instance->Compile(CompileStage::SEMA);
    bool hitC2 = false;
    bool hitGoo = false;
    Walker walker(instance->GetSourcePackages()[0]->files[0].get(), [&hitC2, &hitGoo](Ptr<Node> node) -> VisitAction {
        Meta::match (*node)([&hitC2, &hitGoo](const Decl& decl) {
            if (auto curCall = GetCurMacro(decl)) {
                bool find = FindCommentsInMacroCall(&decl, curCall);
                if (find) {
                    if (decl.identifier == "C2") {
                        hitC2 = true;
                    }
                    if (decl.identifier == "goo") {
                        hitGoo = true;
                    }
                }
            }
        });
        return VisitAction::WALK_CHILDREN;
    });
    walker.Walk();
    ASSERT_TRUE(hitC2);
    ASSERT_TRUE(hitGoo);
}