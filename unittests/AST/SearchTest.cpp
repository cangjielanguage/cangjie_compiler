// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <memory>
#include <string>
#include <vector>

#include "gtest/gtest.h"

#include "Collector.h"
#include "QueryParser.h"
#include "TestCompilerInstance.h"

#include "cangjie/AST/ASTContext.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Searcher.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/Position.h"

using namespace Cangjie;
using namespace AST;

class SearchTest : public testing::Test {
protected:
    void SetUp() override
    {
        srcPath = FileUtil::JoinPath(projectPath, "unittests/AST/CangjieFiles/collector");
        diag.SetSourceManager(&sm);
#ifdef __x86_64__
        invocation.globalOptions.target.arch = Cangjie::Triple::ArchType::X86_64;
#else
        invocation.globalOptions.target.arch = Cangjie::Triple::ArchType::AARCH64;
#endif
#ifdef _WIN32
        invocation.globalOptions.target.os = Cangjie::Triple::OSType::WINDOWS;
#elif __unix__
        invocation.globalOptions.target.os = Cangjie::Triple::OSType::LINUX;
#endif
    }

    std::string code = R"(
main(): unit {
var title: String = "world"
var t:Int32
let test = Page(
    component: Div(
        content: [
            Text(
                content: #"     Hello    "#,
                style: TextStyle(
                    width: 10,
                    height: 100,
                    textOverflow: TextOverflow.ellipsis,
                    lines: 1,
                    flex: [1, 0, 0],
                    fontSize: 41.px(),
                    color: Color(0, 0, 0),
                    margin: [0.px(), 0.px(), 0.px(), 10.px()],
                    fontWeight: FontWeight.normal,
                )
             ),
            Text(
                hahaha: "111",
                content: title,
                fontWeight: "200"
            )
       ]
    )
)

test.Build()

}
    )";

    std::string codeLSP = R"(open class Base {
    protected var one = 1
    private var a: Int64 = 0
    init(a: Int64) {
        this.a = a
    }
    public func add(a: Int64, b: Int64) {
        return a + b + this.one
    }
    open func add2(a: Int64, b: Int64) {
        return a + b
    }
}
external class Data <: Base {
    var res = one
    init(a: Int64) {
        super(1)
        this.res = a
    }
    override func add2(a: Int64, b: Int64) {
        return a + b
    }
}

func run(): Int64 {
    var value : Data = Data(1)
    if (value.res != 1) {
        return value.add(1, 2)
    }
    return value.add2(1, 2)
}

abstract class Base1 {
    var one = 1
}

internal class Base2 {
    var one = 1
}

main(): Int64 {
    return run()
}

func testlsp():Int32 {
    var a:Int32 = 0

    var b:Int32 = 1
    if (a == 0) {
        var c : Int32 = 0
        var d : Int32 = 0
        if (c == 0) {
            var e : Int32 = 0
            var f : Int32 = 0
            if (e == 0) {

                return 1
            }
        }
    }
    return 0
}
    )";

    std::string regressionTestCode = R"(
package pkgs

open class Base {}
class Data <: Base {
    var data: Int32 = 1
}
func a1() {
    func a(){ }
}
func a(){ }
func func1(){ }
func afunc(){ }
func int646(){ }
let c = 123

func run(): Int64 {
    var value: Data = Data()
    if (value.data != 1) {
        return 1
    }
    return 0
}
main(): Int64 {
    return run()
}
    )";

#ifdef PROJECT_SOURCE_DIR
    // Gets the absolute path of the project from the compile parameter.
    std::string projectPath = PROJECT_SOURCE_DIR;
#else
    // Just in case, give it a default value.
    // Assume the initial is in the build directory.
    std::string projectPath = "..";
#endif

    SourceManager sm;
    DiagnosticEngine diag;
    CompilerInvocation invocation;
    Parser* parser;
    std::string srcPath;
};

TEST_F(SearchTest, DISABLED_ErrorFeedbackTest)
{
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = regressionTestCode;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    diag.ClearError();

    res = searcher.Search(ctx, "scope_name:*c");
    EXPECT_EQ(diag.GetErrorCount(), 1);
    EXPECT_TRUE(res.empty());

    res = searcher.Search(ctx, "scope_level:-1");
    EXPECT_EQ(diag.GetErrorCount(), 2);
    EXPECT_TRUE(res.empty());

    res = searcher.Search(ctx, "scope_level:string");
    EXPECT_EQ(diag.GetErrorCount(), 3);

    res = searcher.Search(ctx, "name:Day,name:Time");
    EXPECT_EQ(diag.GetErrorCount(), 4);

    res = searcher.Search(ctx, "name:Day&name:Time");
    EXPECT_EQ(diag.GetErrorCount(), 5);

    res = searcher.Search(ctx, "name:Day|name:Time");
    EXPECT_EQ(diag.GetErrorCount(), 6);

    res = searcher.Search(ctx, "name:Day)name:Time");
    diag.EmitCategoryDiagnostics(DiagCategory::PARSE);
    EXPECT_EQ(diag.GetErrorCount(), 7);
    EXPECT_TRUE(res.empty());
}

TEST_F(SearchTest, DISABLED_WildcardCharacterTest)
{
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = regressionTestCode;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);
    Searcher searcher;
    std::vector<Symbol*> res;

    diag.ClearError();
    res = searcher.Search(ctx, "name:func*");
    EXPECT_EQ(res.size(), 1);
    EXPECT_EQ(res[0]->name, "func1");

    res = searcher.Search(ctx, "name:*func*");
    EXPECT_TRUE(res.empty());
    EXPECT_EQ(diag.GetErrorCount(), 1);

    res = searcher.Search(ctx, "name:*func");
    EXPECT_EQ(res.size(), 1);
    EXPECT_EQ(res[0]->name, "afunc");

    res = searcher.Search(ctx, "name:Int64*");
    res.erase(std::remove_if(
        res.begin(), res.end(),
        [](Symbol* sym) { return sym->node->TestAttr(Attribute::COMPILER_ADD); }),
        res.end());
    EXPECT_EQ(res.size(), 2);

    res = searcher.Search(ctx, "name:ini*");
    EXPECT_EQ(res.size(), 2);

    res = searcher.Search(ctx, "name:*");
    EXPECT_EQ(res.size(), 32);
}

TEST_F(SearchTest, DISABLED_SimpleSearchTest)
{
    srcPath = FileUtil::JoinPath(srcPath, "SimpleSearchTest");
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->srcDirs = {srcPath};
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);

    Searcher searcher;
    std::vector<Symbol*> res;
    ASTContext& ctx = *instance->GetASTContextByPackage(instance->GetSourcePackages()[0]);

    res = searcher.Search(ctx, "name:  TextStyle");
    EXPECT_EQ(res[0]->name, "TextStyle");

    res = searcher.Search(ctx, "name:Q*");
    EXPECT_TRUE(res.empty());

    res = searcher.Search(ctx, "name:*Q");
    EXPECT_TRUE(res.empty());

    res = searcher.Search(ctx, "name:Text*");
    // Text, TextStyle, Text, both ref_expr and call_expr.
    EXPECT_EQ(res.size(), 6);

    res = searcher.Search(ctx, "name:*age");
    // Page, both ref_expr and call_expr.
    EXPECT_EQ(res.size(), 2);

    res = searcher.Search(ctx, "name:*chr");
    EXPECT_EQ(res.size(), 0);

    res = searcher.Search(ctx, "scope_name: a", Sort::posAsc);
    EXPECT_EQ(res[0]->scopeName.substr(0, res[0]->scopeName.find('_')), "a");

    res = searcher.Search(ctx, "scope_level: 0");
    const unsigned int expectedScopeLevel = 0;
    EXPECT_EQ(res[0]->scopeLevel, expectedScopeLevel);

    res = searcher.Search(ctx, "ast_kind: func_decl");
    EXPECT_EQ(res[0]->astKind, ASTKind::FUNC_DECL);

    // Release nodes and check symbol has been marked as deleted.
    for (auto& file : ctx.curPackage->files) {
        file->decls.clear();
    }
    EXPECT_TRUE(res[0]->invertedIndexBeenDeleted);
}

TEST_F(SearchTest, QueryParserTest)
{
    std::string queryString = "id : 1 ! id : 2";
    diag.SetSourceManager(&sm);
    // Parse query statement.
    QueryParser queryParser(queryString, diag, sm);
    auto query = queryParser.Parse();
    EXPECT_EQ(query->op, Operator::NOT);
}

TEST_F(SearchTest, ScopeName)
{
    EXPECT_EQ(ScopeManagerApi::GetScopeGateName("a0b"), "a_b");
    EXPECT_EQ(ScopeManagerApi::GetScopeGateName("a0b_a"), "a_b");
    EXPECT_EQ(ScopeManagerApi::GetScopeGateName("a"), "");
    EXPECT_EQ(ScopeManagerApi::GetScopeGateName("a_a"), "");
    EXPECT_EQ(ScopeManagerApi::GetChildScopeName("a_a"), "a0a");
}

TEST_F(SearchTest, PrefixTrie)
{
    Trie prefixTrie;
    prefixTrie.Insert("a0b");
    prefixTrie.Insert("a0b0c0e");
    prefixTrie.Insert("a0b");
    prefixTrie.Insert("a0c");
    prefixTrie.Insert("a0b0d");
    prefixTrie.Insert("a0d");
    auto results = prefixTrie.PrefixMatch("a0b");
    std::vector<std::string> expectStrings = {"a0b", "a0b0c0e", "a0b0d"};
    EXPECT_TRUE(std::equal(results.begin(), results.end(), expectStrings.begin()));

    prefixTrie.Reset();
    prefixTrie.Insert("a0b0c");
    prefixTrie.Insert("a0c");
    prefixTrie.Insert("a0b0d");
    prefixTrie.Insert("a0d");
    results = prefixTrie.PrefixMatch("a0b");
    expectStrings = {"a0b0c", "a0b0d"};
    EXPECT_TRUE(std::equal(results.begin(), results.end(), expectStrings.begin()));
}

TEST_F(SearchTest, SuffixTrie)
{
    Trie suffixTrie;
    suffixTrie.Insert("var_decl");
    suffixTrie.Insert("ref_type");
    suffixTrie.Insert("member_access");
    suffixTrie.Insert("func_decl");
    suffixTrie.Insert("ref_expr");
    suffixTrie.Insert("record_decl");
    suffixTrie.Insert("class_decl");
    auto results = suffixTrie.SuffixMatch("decl");
    std::vector<std::string> expectStrings = {"class_decl", "func_decl", "record_decl", "var_decl"};
    EXPECT_TRUE(std::equal(results.begin(), results.end(), expectStrings.begin()));
}

TEST_F(SearchTest, FuzzyQuery)
{
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    OwnedPtr<Package> pkg = MakeOwned<Package>();
    pkg->files.emplace_back(parser.ParseTopLevel());
    ASTContext ctx(diag, *pkg);
    ScopeManager scopeManager;
    Collector collector(scopeManager);
    collector.BuildSymbolTable(ctx, pkg.get());
    Searcher searcher;

    // Test for prefix query.
    std::string queryString = "scope_name : a*";
    auto res = searcher.Search(ctx, queryString);
    for (auto n : res) {
        EXPECT_EQ(n->scopeName[0], 'a');
    }
    EXPECT_EQ(res.size(), ctx.symbolTable.size());

    std::string queryString2 = "ast_kind : *decl";
    auto res2 = searcher.Search(ctx, queryString2);
    for (auto n : res2) {
        EXPECT_EQ(ASTKIND_TO_STRING_MAP[n->astKind].substr(ASTKIND_TO_STRING_MAP[n->astKind].size() - 4), "decl");
    }

    std::string queryString3 = "name: t* && ast_kind: *decl";
    auto res3 = searcher.Search(ctx, queryString3);
    for (auto n : res3) {
        EXPECT_EQ(n->name[0], 't');
    }

    std::string queryString4 = "name: *eight && ast_kind: func_arg";
    auto res4 = searcher.Search(ctx, queryString4);
    EXPECT_EQ(res4.size(), 3);

    std::string queryString5 = "name: *eight";
    auto res5 = searcher.Search(ctx, queryString5);
    EXPECT_EQ(res5.size(), 4);
    // Need to release AST before ASTContext.
    pkg.reset();
}

TEST_F(SearchTest, RangeQuery)
{
    diag.SetSourceManager(&sm);
    Parser parser(code, diag, sm);
    OwnedPtr<Package> pkg = MakeOwned<Package>();
    pkg->files.emplace_back(parser.ParseTopLevel());
    ASTContext ctx(diag, *pkg);
    ScopeManager scopeManager;
    Collector collector(scopeManager);
    collector.BuildSymbolTable(ctx, pkg.get());
    Searcher searcher;

    Position pos = {0, 6, 16};

    // Test function of cast number to string of PosSearchApi.
    std::string posStr = PosSearchApi::PosToStr(pos);
    EXPECT_EQ(posStr, "00000000600016");

    // Test FindCommonRootInPosTrie of PosSearchApi.
    std::set<Symbol*> ids;
    TrieNode* root = PosSearchApi::FindCommonRootInPosTrie(*ctx.invertedIndex.posBeginTrie, pos);
    EXPECT_EQ(root->value, "00000000600016");
    const int expectDepth = 14;
    EXPECT_EQ(root->depth, expectDepth);

    // Test GetIDsGreaterTheanPos of PosSearchApi.
    ids = PosSearchApi::GetIDsGreaterThanPos(*ctx.invertedIndex.posBeginTrie, pos);
    for (auto i : ids) {
        EXPECT_TRUE(pos <= i->node->begin);
    }

    // Test GetIDsLessThanPos of PosSearchApi.
    ids = PosSearchApi::GetIDsLessThanPos(*ctx.invertedIndex.posBeginTrie, pos);
    for (auto i : ids) {
        EXPECT_TRUE(i->node->begin <= pos);
    }

    // Test GetIDsWithinPosXByRange of PosSearchApi.
    Position startPos = Position{0, 6, 1};
    Position endPos = Position{0, 6, 16};
    ids = PosSearchApi::GetIDsWithinPosXByRange(*ctx.invertedIndex.posBeginTrie, startPos, endPos);
    auto expectSize = 1;
    EXPECT_EQ(ids.size(), expectSize);
    ids = PosSearchApi::GetIDsWithinPosXByRange(*ctx.invertedIndex.posBeginTrie, startPos, endPos, true, true);
    auto expectSize2 = 3;
    EXPECT_EQ(ids.size(), expectSize2);

    std::string queryString3 = "_ < (0, 4, 5) && ast_kind: var_decl";
    auto res7 = searcher.Search(ctx, queryString3);
    auto expectSize7 = 1;
    EXPECT_EQ(res7.size(), expectSize7);
    std::string queryString4 = "_ > (0, 3, 6) && ast_kind: var_decl";
    auto res8 = searcher.Search(ctx, queryString4);
    auto expectSize8 = 2;
    EXPECT_EQ(res8.size(), expectSize8);
    // Need to release AST before ASTContext.
    pkg.reset();
}

TEST_F(SearchTest, DISABLED_SelectedhighlightTest000)
{
    srcPath = FileUtil::JoinPath(srcPath, "SimpleSearchTest");
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->srcDirs = {srcPath};
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->Compile(CompileStage::SEMA);
    Searcher searcher;
    auto& ctx = *instance->GetASTContextByPackage(instance->GetSourcePackages()[0]);
    auto fileID = instance->GetSourcePackages()[0]->files[0]->begin.fileID;
    auto res = searcher.Search(ctx, "_ = (" + std::to_string(fileID) + ", 60, 13)");
    EXPECT_EQ(res[0]->name, "String");
    Ptr<RefType> rt = As<ASTKind::REF_TYPE>(res[0]->node);
    EXPECT_EQ(rt->ref.target->identifier, "String");
    EXPECT_TRUE(rt->ref.target->identifier.Begin() != INVALID_POSITION);
}

TEST_F(SearchTest, DISABLED_SelectedhighlightTest001)
{
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeLSP;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    res = searcher.Search(ctx, "_ = (1, 27, 12)");
    EXPECT_EQ(res[0]->name, "value");
    Ptr<RefExpr> re = As<ASTKind::REF_EXPR>(res[0]->node);
    EXPECT_EQ(re->ref.target->identifier, "value");
    EXPECT_EQ(re->ref.target->identifier.Begin().line, 26);
    EXPECT_EQ(re->ref.target->identifier.Begin().column, 9);
    EXPECT_EQ(re->ref.target->keywordPos.line, 26);
    EXPECT_EQ(re->ref.target->keywordPos.column, 5);
}

TEST_F(SearchTest, DISABLED_SelectedhighlightTest002)
{
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeLSP;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    res = searcher.Search(ctx, "_ = (1, 26, 19)");
    EXPECT_EQ(res[0]->name, "Data");
    Ptr<RefType> rt = As<ASTKind::REF_TYPE>(res[0]->node);
    EXPECT_EQ(rt->ref.target->identifier, "Data");
    EXPECT_EQ(rt->ref.target->identifier.Begin().line, 14);
    EXPECT_EQ(rt->ref.target->identifier.Begin().column, 16);
}

TEST_F(SearchTest, DISABLED_SelectedhighlightTest003)
{
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeLSP;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    res = searcher.Search(ctx, "_ = (1, 30, 20)");

    Ptr<MemberAccess> ma = As<ASTKind::MEMBER_ACCESS>(res[0]->node);
    EXPECT_EQ(ma->target->identifier, "add2");
    EXPECT_EQ(As<ASTKind::MEMBER_ACCESS>(res[0]->node)->target->identifier.Begin().line, 20);
    EXPECT_EQ(ma->target->identifier.Begin().column, 19);
}

TEST_F(SearchTest, DISABLED_SelectedhighlightTest004)
{
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeLSP;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    res = searcher.Search(ctx, "_ = (1, 27, 17)");

    Ptr<MemberAccess> ma = As<ASTKind::MEMBER_ACCESS>(res[0]->node);
    EXPECT_EQ(ma->target->identifier, "res");
    EXPECT_EQ(ma->target->identifier.Begin().line, 15);
    EXPECT_EQ(ma->target->identifier.Begin().column, 9);
    EXPECT_EQ(ma->field.Begin().line, 27);
    EXPECT_EQ(ma->field.Begin().column, 15);
}

TEST_F(SearchTest, DISABLED_SelectedhighlightTest005)
{
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeLSP;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    res = searcher.Search(ctx, "_ = (1, 27, 17)");

    Ptr<MemberAccess> ma = As<ASTKind::MEMBER_ACCESS>(res[0]->node);
    EXPECT_EQ(ma->target->identifier, "res");
    EXPECT_EQ(ma->target->identifier.Begin().line, 15);
    EXPECT_EQ(ma->target->identifier.Begin().column, 9);
    EXPECT_EQ(ma->field.Begin().line, 27);
    EXPECT_EQ(ma->field.Begin().column, 15);
}

TEST_F(SearchTest, DISABLED_SelectedhighlightTest006)
{
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeLSP;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    res = searcher.Search(ctx, "_ = (1, 25, 8)");

    Ptr<FuncBody> fb = As<ASTKind::FUNC_BODY>(res[0]->node);
    EXPECT_EQ(fb->funcDecl->identifier, "run");
    EXPECT_EQ(fb->funcDecl->identifier.Begin().line, 25);
    EXPECT_EQ(fb->funcDecl->identifier.Begin().column, 6);
}

TEST_F(SearchTest, DISABLED_SelectedhighlightTest007)
{
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeLSP;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    res = searcher.Search(ctx, "_ = (1, 14, 18)");

    Ptr<ClassDecl> cd = As<ASTKind::CLASS_DECL>(res[0]->node);
    EXPECT_EQ(cd->identifier, "Data");
    EXPECT_EQ(cd->identifier.Begin().line, 14);
    EXPECT_EQ(cd->identifier.Begin().column, 16);
}

TEST_F(SearchTest, DISABLED_SelectedhighlightTest008)
{
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = R"(
struct Foor<T> {
    let a: T
    init(a1: T) {
        this.a = a1
    }
}
    )";
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res = searcher.Search(ctx, "_ = (1, 2, 14)");
    EXPECT_EQ(res[0]->name, "T");
}

bool InsideMethodScope(ASTContext& context, std::string scopeName)
{
    ScopeManager scopeManager;
    for (; !scopeName.empty(); scopeName = ScopeManagerApi::GetScopeGateName(scopeName)) {
        Symbol* symbol = ScopeManagerApi::GetScopeGate(context, scopeName);
        if (!symbol) {
            continue;
        }
        if (symbol->astKind == ASTKind::FUNC_BODY) {
            return true;
        }
    }
    return false;
}

TEST_F(SearchTest, DISABLED_CompletionTest001)
{
    std::string codeTest = R"(func testlsp():Unit {
    var a:Int32 = 0
    var b:Int32 = 1
    if (a == 0) {
    }
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeTest;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    std::string curScopeName;
    res = searcher.Search(ctx, "_ = (1, 5, 9)");
    curScopeName = ScopeManagerApi::GetScopeNameWithoutTail(res[0]->scopeName);
    std::vector<std::string> result;

    while (!curScopeName.empty()) {
        std::string q = "scope_name: " + curScopeName + "!ast_kind: file";
        if (InsideMethodScope(ctx, curScopeName)) {
            q += "&& _ < (1, 5, 9)";
        }
        res = searcher.Search(ctx, q);
        for (auto tmp : res) {
            if (tmp->name.empty() || tmp->scopeName == "a") {
                continue;
            }
            result.push_back(tmp->name);
        }
        curScopeName = ScopeManagerApi::GetParentScopeName(curScopeName);
    }

    EXPECT_EQ(result.size(), 6);
    EXPECT_EQ(result[0], "Int32");
    EXPECT_EQ(result[1], "b");
    EXPECT_EQ(result[2], "Int32");
    EXPECT_EQ(result[3], "a");
    EXPECT_EQ(result[4], "Unit");
    EXPECT_EQ(result[5], "testlsp");
}

TEST_F(SearchTest, DISABLED_CompletionTest003)
{
    std::string codeTest = R"(open class Base {
    protected var one = 1
    private var a: Int32 = 0
    init() {}
    init(a: Int32) {
        this.a = a
    }
    public func add(a: Int32, b: Int32) {
        return a + b + this.one
    }
    open func add2(a: Int32, b: Int32) {
        return a + b
    }
}
public class Data <: Base {
    var res = one
    init(a: Int32) {
        this.res = a
    }
    override func add2(a: Int32, b: Int32) {
        return a + b
    }
}

func testlsp():Int32 {
    var value : Data = Data(1)
    value.
    return 0
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeTest;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    res = searcher.Search(ctx, "_ = (1, 27, 11)");

    Ptr<ClassTy> type =
        dynamic_cast<ClassTy*>(As<ASTKind::MEMBER_ACCESS>(res[0]->node)->baseExpr.get()->symbol->node->ty.get());
    EXPECT_EQ(type->decl->body->decls.size(), 3);
    EXPECT_EQ(type->decl->body->decls[0]->identifier, "res");
    EXPECT_EQ(type->decl->body->decls[1]->identifier, "init");
    EXPECT_EQ(type->decl->body->decls[2]->identifier, "add2");
}

TEST_F(SearchTest, DISABLED_CompletionTest004)
{
    std::string codeTest = R"(open class Base {
    protected var one = 1
    private var a: Int32 = 0
    init(a: Int32) {
        this.a = a
    }
    public func add(a: Int32, b: Int32) {
        return a + b + this.one
    }
    open func add2(a: Int32, b: Int32) {
        return a + b
    }
}
external class Data <: Base {
    var res = one
    init(a: Int32) {
        super.
    }
    override func add2(a: Int32, b: Int32) {
        return a + b
    }
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeTest;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    res = searcher.Search(ctx, "_ = (1, 17, 15)");

    Ptr<ClassTy> type =
        dynamic_cast<ClassTy*>(As<ASTKind::MEMBER_ACCESS>(res[0]->node)->baseExpr.get()->symbol->node->ty.get());
    EXPECT_EQ(type->decl->body->decls.size(), 5);
    EXPECT_EQ(type->decl->body->decls[0]->identifier, "one");
    EXPECT_EQ(type->decl->body->decls[1]->identifier, "a");
    EXPECT_EQ(type->decl->body->decls[2]->identifier, "init");
    EXPECT_EQ(type->decl->body->decls[3]->identifier, "add");
    EXPECT_EQ(type->decl->body->decls[4]->identifier, "add2");
}

TEST_F(SearchTest, DISABLED_CompletionTest005)
{
    std::string codeTest = R"(open class Base {
    protected var one = 1
    private var a: Int32 = 0
    init(a: Int32) {
        this.a = a
    }
    public func add(a: Int32, b: Int32) {
        return a + b + this.one
    }
    open func add2(a: Int32, b: Int32) {
        return a + b
    }
}
external class Data <: Base {
    var res1: Int32 = 0
    protected var res2: Int32 = 0
    private var res3: Int32 = 0
    init(a: Int32) {
        this.
    }
    override func add2(a: Int32, b: Int32) {
        return a + b
    }
}
    )";
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeTest;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    res = searcher.Search(ctx, "_ = (1, 19, 14)");

    Ptr<ClassTy> type =
        dynamic_cast<ClassTy*>(As<ASTKind::MEMBER_ACCESS>(res[0]->node)->baseExpr.get()->symbol->node->ty.get());
    EXPECT_EQ(type->decl->body->decls.size(), 5);
    EXPECT_EQ(type->decl->body->decls[0]->identifier, "res1");
    EXPECT_EQ(type->decl->body->decls[1]->identifier, "res2");
    EXPECT_EQ(type->decl->body->decls[2]->identifier, "res3");
    EXPECT_EQ(type->decl->body->decls[3]->identifier, "init");
    EXPECT_EQ(type->decl->body->decls[4]->identifier, "add2");
    res = searcher.Search(ctx, "ast_kind:*_decl");
    EXPECT_EQ(res.size(), 12);
    res = searcher.Search(ctx, "ast_kind:*decl");
    EXPECT_EQ(res.size(), 12);
}

TEST_F(SearchTest, DISABLED_CompletionTest006)
{
    std::string codeTest = R"(interface I {
    let aclass: Int32 = 1
    func add(a: Int32, b: Int32)
    func getNum1():Int32 {
        return 1
    }

    static func getSum2():Int32 {
        return 1
    }
}

class Base <: I {
    static var classa = 1
    static var Class = 2
    var a = 3
    init(a: Int32) {
        this.a = I.
    }
    static func add2(a: Int32, b: Int32):Int32 {
        return a + b
    }
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeTest;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    res = searcher.Search(ctx, "_ = (1, 18, 20)");

    Ptr<InterfaceTy> type =
        dynamic_cast<InterfaceTy*>(As<ASTKind::MEMBER_ACCESS>(res[0]->node)->baseExpr.get()->symbol->node->ty.get());
    EXPECT_EQ(type->decl->body->decls.size(), 4);
    EXPECT_EQ(type->decl->body->decls[0]->identifier, "aclass");
    EXPECT_EQ(type->decl->body->decls[1]->identifier, "add");
    EXPECT_EQ(type->decl->body->decls[2]->identifier, "getNum1");
    EXPECT_EQ(type->decl->body->decls[3]->identifier, "getSum2");
}

TEST_F(SearchTest, DISABLED_CompletionTest007)
{
    std::string code = "let g = A.";
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = code;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    res = searcher.Search(ctx, "_ = (1, 1, 11)");
    EXPECT_EQ(res[0]->astKind, ASTKind::MEMBER_ACCESS);
    EXPECT_EQ(res[2]->astKind, ASTKind::VAR_DECL);
}

TEST_F(SearchTest, DISABLED_VarOrEnumPattern00)
{
    std::string code = R"(
enum E {
    | A
    | B
}

main() {
    let x = B
    match (x) {
        case A => 0
        case C => 1
    }
}
)";
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = code;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    // Begin pos of A
    res = searcher.Search(ctx, "_ = (1, 10, 14)");
    EXPECT_EQ(res[0]->name, "A");
    EXPECT_EQ(res[0]->astKind, ASTKind::REF_EXPR);
    EXPECT_TRUE(res[0]->target != nullptr);
    EXPECT_EQ(res[0]->target->astKind, ASTKind::VAR_DECL);
    EXPECT_EQ(res[0]->target->begin.line, 3);
    EXPECT_EQ(res[0]->target->begin.column, 7);
    // Begin pos of C
    res = searcher.Search(ctx, "_ = (1, 11, 14)");
    EXPECT_EQ(res[0]->name, "C");
    EXPECT_EQ(res[0]->astKind, ASTKind::VAR_DECL);
}

TEST_F(SearchTest, DISABLED_Annotation00)
{
    std::string code = R"(
@Annotation
public class C34 {
    public const init(a:Bool) {}
    public const C34(b:Int64) {}
}
class CA12 {
    public func foo(@C33[true] i: Int32): Unit {}
    @C34[true]
    init() {}
}
main() {}
)";
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = code;
    instance->invocation.globalOptions.enableMacroInLSP = true;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    res = searcher.Search(ctx, "_ = (1, 8, 22)");
    EXPECT_NE(res.size(), 0);
    EXPECT_TRUE(Utils::In(res, [](const auto n) { return n->astKind == ASTKind::REF_EXPR; }));
    EXPECT_EQ(diag.GetErrorCount(), 1);
}

TEST_F(SearchTest, DISABLED_Annotation01)
{
    std::string code = R"(
@Annotation
public class C33 {
    public const init(a:Int64) {}
}
class CA12 {
    public func foo(@C33[6] i: Int32): Unit {}
    @C33[9]
    init() {}
}
main() {}
)";
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = code;
    instance->invocation.globalOptions.enableMacroInLSP = true;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    res = searcher.Search(ctx, "_ = (1, 7, 22)");
    EXPECT_NE(res.size(), 0);
    EXPECT_TRUE(Utils::In(res, [](const auto n) { return n->astKind == ASTKind::REF_EXPR; }));
    EXPECT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(SearchTest, DISABLED_Annotation02)
{
    std::string code = R"(
@Annotation
public class C6 {
    let name: String
    let version: Int64

    public const init(name: String, version!: Int64 = 0) {
        this.name = name
        this.version = version
    }
}
@C6["Sample", version: 1]
class Foo1 {
    static var aa = 0
}
func test1() {
    var a = Foo1.aa
}

@Annotation
public abstract class C2{}
main() {}
)";
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = code;
    instance->invocation.globalOptions.enableMacroInLSP = true;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    EXPECT_EQ(diag.GetErrorCount(), 1);
}

TEST_F(SearchTest, DISABLED_LambdaExpr)
{
    std::string code = "var sum1_newname: (Int32, Int32) -> Int32 = { aaaa:Int32, bbbb => aaaa + bbbb }";
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = code;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    // Begin pos of bbbb
    res = searcher.Search(ctx, "_ = (1, 1, 59)");
    EXPECT_EQ(res[0]->astKind, ASTKind::FUNC_PARAM);
    // End pos of bbbb
    res = searcher.Search(ctx, "_ = (1, 1, 63)");
    EXPECT_EQ(res[0]->name, "bbbb");
}

TEST_F(SearchTest, DISABLED_EnumBodySearchTest)
{
    std::string codeTest = R"(
enum E<ABC> {
    | A111(ABC) | B | C111 |
}

enum E2<ABC> {
    | A22(ABC) | B
    func E11<EFG>(a: ABC) {}
    func kk(a:ABC) {}
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeTest;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASSERT_EQ(pkgs.size(), 1);
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    res = searcher.Search(ctx, "_ = (1, 3, 30)");
    EXPECT_TRUE(!res.empty());
    EXPECT_EQ(res[0]->astKind, ASTKind::DUMMY_BODY);

    res = searcher.Search(ctx, "_ = (1, 10, 5)");
    EXPECT_TRUE(!res.empty());
    EXPECT_EQ(res[0]->astKind, ASTKind::DUMMY_BODY);
    std::string scopeName = ScopeManagerApi::GetScopeNameWithoutTail(res[0]->scopeName);
    EXPECT_EQ(scopeName, "a0b");
    std::string queryString = "scope_name: " + scopeName + " && ast_kind: *decl";
    res = searcher.Search(ctx, queryString);
    EXPECT_TRUE(!res.empty());
    EXPECT_EQ(res[0]->name, "ABC"); // Found generic decl.
}

TEST_F(SearchTest, DISABLED_NamedFuncArgSearchTest)
{
    std::string codeTest = R"(
 public open class Test12 {
    public init(a:Int32) {}
    public init(a:Int64) {}
    public init(a!:Bool) {}
    public init(c!:String = "cc") {}
    public Test12(a:Int64, b!:Int64) {a+b}
    public init(a:Int64, b!:Int32, c!:Bool=true){ }
    public var abc = 0
}
func test42() {
    var x1 = Test12(1)
    var x2 = Test12(1, b: 2) // (13, 24)
    var x3 = Test12(1, b: 2, c:true)
    var x4 = x1.abc
    Test12()
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeTest;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    ASTContext& ctx = *instance->GetASTContextByPackage(instance->GetSourcePackages()[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    res = searcher.Search(ctx, "_ = (1, 13, 24)");
    ASSERT_TRUE(!res.empty());
    EXPECT_EQ(res[0]->astKind, ASTKind::FUNC_ARG);
    auto argTarget = res[0]->target;

    res = searcher.Search(ctx, "_ = (1, 13, 17)");
    ASSERT_TRUE(!res.empty());
    EXPECT_EQ(res[0]->astKind, ASTKind::REF_EXPR);
    auto refTarget = res[0]->target;
    ASSERT_TRUE(refTarget && refTarget->astKind == ASTKind::FUNC_DECL);
    auto fd = RawStaticCast<FuncDecl*>(refTarget);
    ASSERT_TRUE(fd->funcBody && !fd->funcBody->paramLists.empty());
    // Parameter 'b' is second param at index 1.
    auto param = fd->funcBody->paramLists[0]->params[1].get();
    EXPECT_EQ(argTarget, param);
}

TEST_F(SearchTest, DISABLED_BinaryExprSearchTest)
{
    std::string codeTest = R"(
main() {
    let a : Int64 = 1
    let b : Float64 = 1.0
    let c : Float64 = 1.0
    let d = a + b + c
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeTest;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    ASTContext& ctx = *instance->GetASTContextByPackage(instance->GetSourcePackages()[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    res = searcher.Search(ctx, "_ = (1, 6, 22)");
    ASSERT_TRUE(!res.empty());
    EXPECT_EQ(res[0]->astKind, ASTKind::REF_EXPR);
    auto argTarget = res[0]->target;
    ASSERT_TRUE(argTarget != nullptr);
    EXPECT_EQ(argTarget->identifier, "c");
}

TEST_F(SearchTest, DISABLED_AliasCtorSearchTest)
{
    std::string codeTest = R"(
class C {
    init(a: Int64) {}
}
type ccc = C
main() {
    var a : ccc = ccc(1)
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeTest;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    ASTContext& ctx = *instance->GetASTContextByPackage(instance->GetSourcePackages()[0]);

    Searcher searcher;
    std::vector<Symbol*> res;

    res = searcher.Search(ctx, "_ = (1, 7, 21)");
    ASSERT_TRUE(!res.empty());
    EXPECT_EQ(res[0]->astKind, ASTKind::REF_EXPR);
    auto func = res[0]->target;
    ASSERT_TRUE(func != nullptr);
    auto ref = dynamic_cast<NameReferenceExpr*>(res[0]->node.get());
    ASSERT_TRUE(ref != nullptr);
    ASSERT_TRUE(ref->aliasTarget != nullptr && ref->aliasTarget->type != nullptr);
    EXPECT_EQ(func->outerDecl, ref->aliasTarget->type->GetTarget());
}

TEST_F(SearchTest, DISABLED_MultipleAssignExprHightTest)
{
    std::string codeTest = R"(
main() {
    var f = { => (1, (2.0, 3))}
    var aaa: Int64
    var bbb: Float64
    var ccc: Int32
    (aaa, (bbb, ccc)) = f()
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeTest;
    instance->invocation.globalOptions.implicitPrelude = false;
    instance->Compile(CompileStage::SEMA);
    ASTContext& ctx = *instance->GetASTContextByPackage(instance->GetSourcePackages()[0]);

    Searcher searcher;
    std::vector<Symbol*> syms;

    syms = searcher.Search(ctx, "_ = (1, 7, 15)");
    EXPECT_FALSE(syms.empty());
    bool flag = false;
    for (auto& sym : syms) {
        if (sym->node->TestAttr(Attribute::COMPILER_ADD) && !sym->node->TestAttr(Attribute::IS_CLONED_SOURCE_CODE)) {
            continue;
        }
        auto ref = As<ASTKind::REF_EXPR>(sym->node);
        if (ref && ref->ref.target != nullptr && ref->ref.identifier == "bbb" &&
            !(ref->ref.target->TestAttr(Attribute::COMPILER_ADD) &&
                !ref->ref.target->TestAttr(Attribute::IS_CLONED_SOURCE_CODE))) {
            flag = true;
            break;
        }
    }
    EXPECT_TRUE(flag);
}

TEST_F(SearchTest, DISABLED_PrimaryCtorHighlightTest)
{
    srcPath = FileUtil::JoinPath(srcPath, "PrimaryCtorHighlightTest");
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->srcDirs = {srcPath};
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->Compile(CompileStage::SEMA);
    ASTContext& ctx = *instance->GetASTContextByPackage(instance->GetSourcePackages()[0]);
    auto fileID = ctx.curPackage->files[0]->begin.fileID;

    Searcher searcher;
    std::vector<Symbol*> syms;

    syms = searcher.Search(ctx, "_ = (" + std::to_string(fileID) + ", 9, 2)");
    EXPECT_FALSE(syms.empty());
    EXPECT_EQ(syms[0]->astKind, ASTKind::REF_EXPR);
    bool flag = false;
    for (auto& sym : syms) {
        if (sym->node->TestAttr(Attribute::COMPILER_ADD) && !sym->node->TestAttr(Attribute::IS_CLONED_SOURCE_CODE)) {
            continue;
        }
        auto ref = As<ASTKind::REF_EXPR>(sym->node);
        if (ref && ref->ref.target != nullptr &&
            !(ref->ref.target->TestAttr(Attribute::COMPILER_ADD) &&
                !ref->ref.target->TestAttr(Attribute::IS_CLONED_SOURCE_CODE))) {
            flag = true;
            break;
        }
    }
    EXPECT_TRUE(flag);

    syms = searcher.Search(ctx, "_ = (" + std::to_string(fileID) + ", 9, 7)");
    EXPECT_FALSE(syms.empty());
    EXPECT_EQ(syms[0]->astKind, ASTKind::REF_EXPR);
    flag = false;
    for (auto& sym : syms) {
        if (sym->node->TestAttr(Attribute::COMPILER_ADD) && !sym->node->TestAttr(Attribute::IS_CLONED_SOURCE_CODE)) {
            continue;
        }
        auto ref = As<ASTKind::REF_EXPR>(sym->node);
        if (ref && ref->ref.target != nullptr &&
            !(ref->ref.target->TestAttr(Attribute::COMPILER_ADD) &&
                !ref->ref.target->TestAttr(Attribute::IS_CLONED_SOURCE_CODE))) {
            flag = true;
            break;
        }
    }
    EXPECT_TRUE(flag);

    syms = searcher.Search(ctx, "_ = (" + std::to_string(fileID) + ", 8, 7)");
    EXPECT_FALSE(syms.empty());
    EXPECT_EQ(syms[0]->astKind, ASTKind::FUNC_PARAM);
    flag = false;
    for (auto& sym : syms) {
        if (sym->node->TestAttr(Attribute::COMPILER_ADD) && !sym->node->TestAttr(Attribute::IS_CLONED_SOURCE_CODE)) {
            continue;
        }
        auto param = As<ASTKind::FUNC_PARAM>(sym->node);
        if (param && param->identifier.Begin() != INVALID_POSITION &&
            !(param->TestAttr(Attribute::COMPILER_ADD) && !param->TestAttr(Attribute::IS_CLONED_SOURCE_CODE))) {
            flag = true;
            break;
        }
    }
    EXPECT_TRUE(flag);

    syms = searcher.Search(ctx, "_ = (" + std::to_string(fileID) + ", 8, 16)");
    EXPECT_FALSE(syms.empty());
    EXPECT_EQ(syms[0]->astKind, ASTKind::FUNC_PARAM);
    flag = false;
    for (auto& sym : syms) {
        if (sym->node->TestAttr(Attribute::COMPILER_ADD) && !sym->node->TestAttr(Attribute::IS_CLONED_SOURCE_CODE)) {
            continue;
        }
        auto param = As<ASTKind::FUNC_PARAM>(sym->node);
        if (param && param->identifier.Begin() != INVALID_POSITION &&
            !(param->TestAttr(Attribute::COMPILER_ADD) && !param->TestAttr(Attribute::IS_CLONED_SOURCE_CODE))) {
            flag = true;
            break;
        }
    }
    EXPECT_TRUE(flag);

    syms = searcher.Search(ctx, "_ = (" + std::to_string(fileID) + ", 20, 17)");
    EXPECT_FALSE(syms.empty());
    EXPECT_EQ(syms[0]->astKind, ASTKind::MEMBER_ACCESS);
    flag = false;
    for (auto& sym : syms) {
        if (sym->node->TestAttr(Attribute::COMPILER_ADD) && !sym->node->TestAttr(Attribute::IS_CLONED_SOURCE_CODE)) {
            continue;
        }
        auto ma = As<ASTKind::MEMBER_ACCESS>(sym->node);
        if (ma && ma->target &&
            !(ma->TestAttr(Attribute::COMPILER_ADD) && !ma->TestAttr(Attribute::IS_CLONED_SOURCE_CODE)) &&
            !(ma->target->TestAttr(Attribute::COMPILER_ADD) &&
                !ma->target->TestAttr(Attribute::IS_CLONED_SOURCE_CODE))) {
            flag = true;
            break;
        }
    }
    EXPECT_TRUE(flag);
}

namespace {
std::string GetScopeName(TestCompilerInstance& instance, const std::string& codeTest, const std::string& position)
{
    instance.code = codeTest;
    instance.invocation.globalOptions.implicitPrelude = true;
    instance.Compile(CompileStage::SEMA);
    auto pkgs = instance.GetSourcePackages();
    if (pkgs.size() != 1) {
        return "";
    }
    ASTContext& ctx = *instance.GetASTContextByPackage(pkgs[0]);
    Searcher searcher;
    auto syms = searcher.Search(ctx, position);
    if (syms.empty() || syms[0]->astKind != ASTKind::VAR_DECL) {
        return "";
    }
    return syms[0]->scopeName;
}
} // namespace

TEST_F(SearchTest, DISABLED_SynReferenceAfterSema_NameReference)
{
    // Test for normal name reference accessing.
    std::string codeTest = R"(
class A {
    let aclass: Int32 = 1
    func getNum1():Int32 {
        return 1
    }

    static func getSum2():Int32 {
        return 1
    }
}

class Base {
    static var classa = 1
    static var Class = 2
    var a = 3
    var ins = A()
    static func add2(a: Int32, b: Int32):Int32 {
        return a + b
    }
}
let x = A()
main() {
    var x = Base()
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    auto scopeName = GetScopeName(*instance, codeTest, "_ = (1, 24, 9)");
    ASSERT_FALSE(scopeName.empty());
    auto pkgs = instance->GetSourcePackages();
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);
    // Find local variable.
    // 'x'
    auto re = CreateRefExpr("x");
    re->curFile = pkgs[0]->files[0].get();
    auto results = instance->GetGivenReferenceTarget(ctx, scopeName, *re, true);
    ASSERT_TRUE(results.hasDecl);
    auto decls = results.decls;
    ASSERT_EQ(decls.size(), 1);
    EXPECT_EQ(decls[0]->ty->String(), "Class-Base");
    // 'x.ins'
    auto ma = CreateMemberAccess(std::move(re), "ins");
    ma->curFile = pkgs[0]->files[0].get();
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *ma, true);
    ASSERT_TRUE(results.hasDecl);
    decls = results.decls;
    ASSERT_EQ(decls.size(), 1);
    EXPECT_EQ(decls[0]->ty->String(), "Class-A");
    // 'x.ins.aclass'
    ma = CreateMemberAccess(std::move(ma), "aclass");
    ma->curFile = pkgs[0]->files[0].get();
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *ma, true);
    ASSERT_TRUE(results.hasDecl);
    decls = results.decls;
    ASSERT_EQ(decls.size(), 1);
    EXPECT_EQ(decls[0]->ty->String(), "Int32");
    // Find variables not in current scope.
    re = CreateRefExpr("x");
    re->curFile = pkgs[0]->files[0].get();
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *re, false);
    ASSERT_TRUE(results.hasDecl);
    decls = results.decls;
    ASSERT_EQ(decls.size(), 1);
    EXPECT_EQ(decls[0]->ty->String(), "Class-A");
}

TEST_F(SearchTest, DISABLED_SynReferenceAfterSema_NameReference_ThisAndSuper)
{
    // Test for normal name reference accessing.
    std::string codeTest = R"(
class A <: B {
    let a: Int32 = 1
    func getNum1():Int32 {
        var a =
        return 1
    }
}
open class B {}
    )";
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    auto scopeName = GetScopeName(*instance, codeTest, "_ = (1, 5, 17)");
    ASSERT_FALSE(scopeName.empty());
    auto pkgs = instance->GetSourcePackages();
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    // 'this'
    auto reThis = CreateRefExpr("this");
    reThis->curFile = pkgs[0]->files[0].get();
    auto results = instance->GetGivenReferenceTarget(ctx, scopeName, *reThis, true);
    ASSERT_EQ(results.tys.size(), 1);
    EXPECT_EQ((*results.tys.begin())->String(), "This");
    EXPECT_EQ((*results.tys.begin())->name, "A");
    // 'super'
    auto reSuper = CreateRefExpr("super");
    reSuper->curFile = pkgs[0]->files[0].get();
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *reSuper, true);
    ASSERT_EQ(results.tys.size(), 1);
    EXPECT_EQ((*results.tys.begin())->String(), "Class-B");
    EXPECT_EQ((*results.tys.begin())->name, "B");
}

TEST_F(SearchTest, DISABLED_SynReferenceAfterSema_CallAccess)
{
    // Test for call access.
    std::string codeTest = R"(
class A {
    let v = 1
    func foo() : B {
        B()
    }
}
class B {
    let x = 2
}
func test() : A {
    return A()
}
func test(a: Int64) : Int64 {
    return a
}
let x = A()
main() {
    var x = B()
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    auto scopeName = GetScopeName(*instance, codeTest, "_ = (1, 19, 9)");
    ASSERT_FALSE(scopeName.empty());
    auto pkgs = instance->GetSourcePackages();
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);
    // Find local variable.
    // 'test()'
    auto ce = CreateCallExpr(CreateRefExpr("test"), {});
    AddCurFile(*ce, pkgs[0]->files[0].get());
    auto results = instance->GetGivenReferenceTarget(ctx, scopeName, *ce, false);
    ASSERT_FALSE(results.hasDecl);
    auto tys = results.tys;
    ASSERT_EQ(tys.size(), 2);
    auto str = Ty::GetTypesToStableStr(std::set<Ptr<Ty>>(tys.begin(), tys.end()), " ");
    EXPECT_EQ(str, "Class-A Int64");
    // 'test().foo()'
    auto ma = CreateMemberAccess(std::move(ce), "foo");
    ce = CreateCallExpr(std::move(ma), {});
    AddCurFile(*ce, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *ce, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Class-B");
}

TEST_F(SearchTest, DISABLED_SynReferenceAfterSema_CallAccess02)
{
    // Test for call access.
    std::string codeTest = R"(
main() {
    var x = 1
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    auto scopeName = GetScopeName(*instance, codeTest, "_ = (1, 3, 9)");
    ASSERT_FALSE(scopeName.empty());
    auto pkgs = instance->GetSourcePackages();
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);
    // 'CPointer()'
    OwnedPtr<Expr> access01 = Parser("CPointer<Int8>()", diag, sm).ParseExpr();
    AddCurFile(*access01, pkgs[0]->files[0].get());
    auto results = instance->GetGivenReferenceTarget(ctx, scopeName, *access01, false);
    ASSERT_FALSE(results.hasDecl);
    auto tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "CPointer<Int8>");
    // 'CString()'
    OwnedPtr<Expr> access02 = Parser("CString()", diag, sm).ParseExpr();
    AddCurFile(*access02, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *access02, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "CString");
}

TEST_F(SearchTest, DISABLED_SynReferenceAfterSema_ThisCallAccess)
{
    // Test for call access for 'This' type.
    std::string codeTest = R"(
open class A {
    func get() {
        this
    }
}
class B <: A {
    func test() : Int64 {1}
}

main() {
    var x = B()
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    auto scopeName = GetScopeName(*instance, codeTest, "_ = (1, 12, 9)");
    ASSERT_FALSE(scopeName.empty());
    auto pkgs = instance->GetSourcePackages();
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);
    // Test for dynamic binding 'This' type.
    OwnedPtr<Expr> access01 = Parser("x.get().test()", diag, sm).ParseExpr();
    AddCurFile(*access01, pkgs[0]->files[0].get());
    auto results = instance->GetGivenReferenceTarget(ctx, scopeName, *access01, true);
    ASSERT_FALSE(results.hasDecl);
    auto tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Int64");
}

TEST_F(SearchTest, DISABLED_SynReferenceAfterSema_LiteralAccess)
{
    // Test for literal access.
    std::string codeTest = R"(
main() {
    var x = 1
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    auto scopeName = GetScopeName(*instance, codeTest, "_ = (1, 3, 9)");
    ASSERT_FALSE(scopeName.empty());
    auto pkgs = instance->GetSourcePackages();
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);
    OwnedPtr<Expr> literal = Parser("1i64", diag, sm).ParseExpr();
    AddCurFile(*literal, pkgs[0]->files[0].get());
    auto results = instance->GetGivenReferenceTarget(ctx, scopeName, *literal, false);
    ASSERT_FALSE(results.hasDecl);
    auto tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Int64");
    // '"str"'
    OwnedPtr<Expr> strLit = Parser("\"str\"", diag, sm).ParseExpr();
    AddCurFile(*strLit, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *strLit, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Struct-String");
    // '""'
    OwnedPtr<Expr> emptyStrLit = Parser("\"\"", diag, sm).ParseExpr();
    AddCurFile(*emptyStrLit, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *emptyStrLit, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Struct-String");
    // '"000${x}222"'
    OwnedPtr<Expr> siStr = Parser("\"000${x}222\"", diag, sm).ParseExpr();
    AddCurFile(*siStr, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *siStr, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Struct-String");
    // '21'
    OwnedPtr<Expr> i64Lit = Parser("21", diag, sm).ParseExpr();
    AddCurFile(*i64Lit, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *i64Lit, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Int");
}

TEST_F(SearchTest, DISABLED_SynReferenceAfterSema_ArrayLitAccess01)
{
    // Test for arraylit access.
    std::string codeTest = R"(
var x = 1
main() {
    var y = [1, 2, 3]
    0
}
    )";
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    auto scopeName = GetScopeName(*instance, codeTest, "_ = (1, 4, 9)");
    ASSERT_FALSE(scopeName.empty());
    auto pkgs = instance->GetSourcePackages();
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    // Test [1, 2, 3]
    OwnedPtr<Expr> literal = Parser("[1, 2, 3]", diag, sm).ParseExpr();
    AddCurFile(*literal, pkgs[0]->files[0].get());
    auto results = instance->GetGivenReferenceTarget(ctx, scopeName, *literal, false);
    ASSERT_FALSE(results.hasDecl);
    auto tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Struct-Array<Int>");

    // Test [x, 2, 3]
    OwnedPtr<Expr> literalWithVar = Parser("[x, 2, 3]", diag, sm).ParseExpr();
    AddCurFile(*literalWithVar, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *literalWithVar, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Struct-Array<Int64>");

    // Test ["1", "2", "3"]
    OwnedPtr<Expr> literalStr = Parser("[\"1\", \"2\", \"3\"]", diag, sm).ParseExpr();
    AddCurFile(*literalStr, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *literalStr, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Struct-Array<Struct-String>");

    // Test ["1 ${x}", "2", "3"]
    OwnedPtr<Expr> literalInsertStr = Parser("[\"1 ${x}\", \"2\", \"3\"]", diag, sm).ParseExpr();
    AddCurFile(*literalInsertStr, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *literalInsertStr, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Struct-Array<Struct-String>");

    // Test [["1", "2", "3"]]
    OwnedPtr<Expr> literalNestArray = Parser("[[\"1\", \"2\", \"3\"]]", diag, sm).ParseExpr();
    AddCurFile(*literalNestArray, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *literalNestArray, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Struct-Array<Struct-Array<Struct-String>>");

    // Test []
    OwnedPtr<Expr> enmptyLitArray = Parser("[]", diag, sm).ParseExpr();
    AddCurFile(*enmptyLitArray, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *enmptyLitArray, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Invalid");
}

TEST_F(SearchTest, DISABLED_SynReferenceAfterSema_ArrayLitAccess02)
{
    // Test for arraylit access.
    std::string codeTest = R"(
open class I {}
class A <: I {}
class B <: I {}
main() {
    var y = [1, 2, 3]
    0
}
    )";
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    auto scopeName = GetScopeName(*instance, codeTest, "_ = (1, 6, 9)");
    ASSERT_FALSE(scopeName.empty());
    auto pkgs = instance->GetSourcePackages();
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);

    // Test [A(), B()]
    OwnedPtr<Expr> literal = Parser("[A(), B()]", diag, sm).ParseExpr();
    AddCurFile(*literal, pkgs[0]->files[0].get());
    auto results = instance->GetGivenReferenceTarget(ctx, scopeName, *literal, false);
    ASSERT_FALSE(results.hasDecl);
    auto tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Struct-Array<Class-I>");
}

TEST_F(SearchTest, DISABLED_SynReferenceAfterSema_SubscriptAccess)
{
    // Test for subscript access.
    std::string codeTest = R"(
class A {
    let v = 1
    operator func [](index: Int64) : A {
        return A()
    }
}

let x = A()
let y = (1u8, 2i16, "str")
let z = ["1", "2"]
main() {
    var x0 = 1
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    auto scopeName = GetScopeName(*instance, codeTest, "_ = (1, 13, 9)");
    ASSERT_FALSE(scopeName.empty());
    auto pkgs = instance->GetSourcePackages();
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);
    OwnedPtr<Expr> access01 = Parser("x[2]", diag, sm).ParseExpr();
    AddCurFile(*access01, pkgs[0]->files[0].get());
    auto results = instance->GetGivenReferenceTarget(ctx, scopeName, *access01, false);
    ASSERT_FALSE(results.hasDecl);
    auto tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Class-A");

    OwnedPtr<Expr> access02 = Parser("y[0]", diag, sm).ParseExpr();
    AddCurFile(*access02, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *access02, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "UInt8");

    OwnedPtr<Expr> access03 = Parser("y[1]", diag, sm).ParseExpr();
    AddCurFile(*access03, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *access03, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Int16");

    OwnedPtr<Expr> access04 = Parser("y[2]", diag, sm).ParseExpr();
    AddCurFile(*access04, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *access04, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Struct-String");

    OwnedPtr<Expr> access05 = Parser("z[0]", diag, sm).ParseExpr();
    AddCurFile(*access05, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *access05, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 3);
    auto str = Ty::GetTypesToStableStr(std::set<Ptr<Ty>>(tys.begin(), tys.end()), " ");
    EXPECT_EQ(str, "Struct-Array<Struct-String> Struct-String Unit");

    OwnedPtr<Expr> access06 = Parser("z[0].get(0).getOrThrow()", diag, sm).ParseExpr();
    AddCurFile(*access06, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *access06, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 2);
    str = Ty::GetTypesToStableStr(std::set<Ptr<Ty>>(tys.begin(), tys.end()), " ");
    EXPECT_EQ(str, "Struct-String UInt8");
}

TEST_F(SearchTest, DISABLED_SynReferenceAfterSema_SubscriptAccess02)
{
    // Test for subscript access with generic.
    std::string codeTest = R"(
class A<T> {
    let v : T
    init(a: T) {
        v = a
    }
    operator func [](v: T) : T {
        return v
    }
    static func test(a: T) : T {
        let ins = A<T>(a)
        return ins.v;
    }
}

let x = A<Array<String>>(["1"])
main() {
    var x0 = 1
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    auto scopeName = GetScopeName(*instance, codeTest, "_ = (1, 18, 9)");
    ASSERT_FALSE(scopeName.empty());
    auto pkgs = instance->GetSourcePackages();
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);
    OwnedPtr<Expr> access01 = Parser("x.v[1].get(0).getOrThrow()", diag, sm).ParseExpr();
    AddCurFile(*access01, pkgs[0]->files[0].get());
    auto results = instance->GetGivenReferenceTarget(ctx, scopeName, *access01, false);
    ASSERT_FALSE(results.hasDecl);
    auto tys = results.tys;
    auto str = Ty::GetTypesToStableStr(std::set<Ptr<Ty>>(tys.begin(), tys.end()), " ");
    EXPECT_EQ(str, "Struct-String UInt8");

    OwnedPtr<Expr> access02 = Parser("x[1].get(0).getOrThrow()", diag, sm).ParseExpr();
    AddCurFile(*access02, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *access02, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    str = Ty::GetTypesToStableStr(std::set<Ptr<Ty>>(tys.begin(), tys.end()), " ");
    EXPECT_EQ(str, "Struct-String");

    OwnedPtr<Expr> access03 = Parser("A<Array<String>>([\"1\"])[1].get(0).getOrThrow()", diag, sm).ParseExpr();
    AddCurFile(*access03, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *access03, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    str = Ty::GetTypesToStableStr(std::set<Ptr<Ty>>(tys.begin(), tys.end()), " ");
    EXPECT_EQ(str, "Struct-String");

    OwnedPtr<Expr> access04 = Parser("A<Array<String>>.test([\"1\"])[1].get(0).getOrThrow()", diag, sm).ParseExpr();
    AddCurFile(*access04, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *access04, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    str = Ty::GetTypesToStableStr(std::set<Ptr<Ty>>(tys.begin(), tys.end()), " ");
    EXPECT_EQ(str, "Struct-String UInt8");
}

TEST_F(SearchTest, DISABLED_SynReferenceAfterSema_FunctionCallOperator)
{
    // Test for operator '()' overloading access.
    std::string codeTest = R"(
class A {
    operator func ()(index: Int64) : B {
        return B()
    }
}

class B {
    let a = A()
}

main() {
    let x = A()
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    auto scopeName = GetScopeName(*instance, codeTest, "_ = (1, 13, 9)");
    ASSERT_FALSE(scopeName.empty());
    auto pkgs = instance->GetSourcePackages();
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);
    OwnedPtr<Expr> access01 = Parser("x(1)", diag, sm).ParseExpr();
    AddCurFile(*access01, pkgs[0]->files[0].get());
    auto results = instance->GetGivenReferenceTarget(ctx, scopeName, *access01, true);
    ASSERT_FALSE(results.hasDecl);
    auto tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Class-B");

    auto ma = CreateMemberAccess(std::move(access01), "a");
    AddCurFile(*ma, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *ma, true);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Class-A");
}

TEST_F(SearchTest, DISABLED_SynReferenceAfterSema_OptionalChain)
{
    // Test for operator '()' overloading access.
    std::string codeTest = R"(
class A {
    let b = Some(B())
}

class B {
    let a = A()
    operator func ()() : A {
        return a
    }
    operator func [](index: Int64) : Option<B> {
        return Some(this)
    }
}

main() {
    let x = Option<A>.Some(A())
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    auto scopeName = GetScopeName(*instance, codeTest, "_ = (1, 17, 9)");
    ASSERT_FALSE(scopeName.empty());
    auto pkgs = instance->GetSourcePackages();
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);
    OwnedPtr<Expr> access01 = Parser("x?.b", diag, sm).ParseExpr();
    AddCurFile(*access01, pkgs[0]->files[0].get());
    auto results = instance->GetGivenReferenceTarget(ctx, scopeName, *access01, true);
    ASSERT_FALSE(results.hasDecl);
    auto tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Enum-Option<Class-B>");

    OwnedPtr<Expr> access02 = Parser("x?.b?[0]", diag, sm).ParseExpr();
    AddCurFile(*access02, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *access02, true);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Enum-Option<Class-B>");

    OwnedPtr<Expr> access03 = Parser("x?.b?[1]?()", diag, sm).ParseExpr();
    AddCurFile(*access03, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *access03, true);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Class-A");
}

TEST_F(SearchTest, DISABLED_SynReferenceAfterSema_PkgName)
{
    std::string codeTest = R"(
package pkgD

let pkgDc= 3333
let pkgDd = 2
struct pkgDTest { init(a:Int32){}}
public class pkgDTestClass {  init(a:Int32){}}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeTest;
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->Compile(CompileStage::SEMA);
    auto pkgs = instance->GetSourcePackages();
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);
    auto re = CreateRefExpr("pkgD");
    re->curFile = pkgs[0]->files[0].get();
    auto results = instance->GetGivenReferenceTarget(ctx, "a", *re, false);
    ASSERT_TRUE(results.hasDecl);
    ASSERT_EQ(results.decls.size(), 0);
}

TEST_F(SearchTest, DISABLED_RemoveMacroInitError)
{
    // Test for normal name reference accessing.
    std::string codeTest = R"(
class A {
    @Differentiabe
    func sum(b: Float64) : Float64 {
        return a + b
    }
    let a : Float64 = 1.0
}
main(){ }
    )";
    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    instance->code = codeTest;
    instance->invocation.globalOptions.implicitPrelude = true;
    instance->Compile(CompileStage::SEMA);
    EXPECT_EQ(diag.GetErrorCount(), 1);
}

TEST_F(SearchTest, DISABLED_SynReferenceAfterSema_TrailingClosureExpr01)
{
    // Test for trailing closure of refExpr.
    std::string codeTest = R"(
class A {
    var a = 1
}

class B {
    init() {}
    init(f: ()->Unit) {}
    func width() : A {
        A()
    }
}

main() {
    let v = 1
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    auto scopeName = GetScopeName(*instance, codeTest, "_ = (1, 15, 9)");
    ASSERT_FALSE(scopeName.empty());
    auto pkgs = instance->GetSourcePackages();
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);
    OwnedPtr<Expr> tce01 = Parser("B{}", diag, sm).ParseExpr();
    AddCurFile(*tce01, pkgs[0]->files[0].get());
    auto results = instance->GetGivenReferenceTarget(ctx, scopeName, *tce01, false);
    ASSERT_FALSE(results.hasDecl);
    auto tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Class-B");

    OwnedPtr<Expr> tce02 = Parser("B{}.width()", diag, sm).ParseExpr();
    AddCurFile(*tce02, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *tce02, false);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Class-A");
}

TEST_F(SearchTest, DISABLED_SynReferenceAfterSema_TrailingClosureExpr02)
{
    // Test for trailing closure of callExpr.
    std::string codeTest = R"(
class A {
    var a = 1
    func get() : Int64 {1}
}

class B {
    init() {}
    init(f: ()->Unit) {}
    func foo(a: Int64, f: ()->Unit) : A {
        A()
    }
    func coo(a: Int64, f: ()->Unit, c!: Int64 = 1) : A {
        A()
    }
    func zoo(a: Int64, f: ()->Unit, c: Int64) : A {
        A()
    }
}

main() {
    let v = B()
}
    )";

    std::unique_ptr<TestCompilerInstance> instance = std::make_unique<TestCompilerInstance>(invocation, diag);
    auto scopeName = GetScopeName(*instance, codeTest, "_ = (1, 22, 9)");
    ASSERT_FALSE(scopeName.empty());
    auto pkgs = instance->GetSourcePackages();
    ASTContext& ctx = *instance->GetASTContextByPackage(pkgs[0]);
    OwnedPtr<Expr> tce01 = Parser("v.foo(1){}", diag, sm).ParseExpr();
    AddCurFile(*tce01, pkgs[0]->files[0].get());
    auto results = instance->GetGivenReferenceTarget(ctx, scopeName, *tce01, true);
    ASSERT_FALSE(results.hasDecl);
    auto tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Class-A");

    OwnedPtr<Expr> tce02 = Parser("v.foo(1){}.get()", diag, sm).ParseExpr();
    AddCurFile(*tce02, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *tce02, true);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Int64");

    OwnedPtr<Expr> tce03 = Parser("v.coo(1){}.get()", diag, sm).ParseExpr();
    AddCurFile(*tce03, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *tce03, true);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    ASSERT_EQ(tys.size(), 1);
    EXPECT_EQ((*tys.begin())->String(), "Int64");

    OwnedPtr<Expr> tce04 = Parser("v.zoo(1){}", diag, sm).ParseExpr();
    AddCurFile(*tce04, pkgs[0]->files[0].get());
    results = instance->GetGivenReferenceTarget(ctx, scopeName, *tce04, true);
    ASSERT_FALSE(results.hasDecl);
    tys = results.tys;
    // Fuzzy result given result size 1.
    ASSERT_EQ(tys.size(), 1);
}
