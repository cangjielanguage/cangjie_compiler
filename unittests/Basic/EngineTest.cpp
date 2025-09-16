// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/AST/PrintNode.h"
#include "cangjie/Basic/DiagnosticEmitter.h"
#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/Parse/Parser.h"
#include "cangjie/Utils/CheckUtils.h"

#include "gtest/gtest.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace Cangjie;
using namespace AST;

static std::string g_expectStr;

class LSPDiagnosticHandlerTest : public DiagnosticHandler {
public:
    explicit LSPDiagnosticHandlerTest(DiagnosticEngine& diag) : DiagnosticHandler(diag, DiagHandlerKind::LSP_HANDLER)
    {
    }
    void HandleDiagnose(Diagnostic& d) override;
};

void LSPDiagnosticHandlerTest::HandleDiagnose(Diagnostic& d)
{
    g_expectStr = d.errorMessage;
    switch (d.diagSeverity) {
        case DiagSeverity::DS_ERROR:
            Errorln(d.start, " ThisIsLSP ", d.errorMessage);
            diag.IncreaseErrorCount(d.diagCategory);
            break;
        case DiagSeverity::DS_WARNING:
            Warningln(d.errorMessage);
            diag.IncreaseWarningCount(d.diagCategory);
            break;
        case DiagSeverity::DS_NOTE:
            Infoln(d.errorMessage);
            break;
        case DiagSeverity::DS_HINT:
            Infoln(d.errorMessage);
            break;
        default:
            Infoln(d.errorMessage);
    }
}

class EngineTest : public testing::Test {
protected:
    void SetUp() override
    {
        DiagnosticEngine diageg;
    }
};

TEST(EngineTest, Register1)
{
    DiagnosticEngine diag;
    auto handler = std::make_unique<LSPDiagnosticHandlerTest>(diag);
    diag.RegisterHandler(std::move(handler));
}

TEST(EngineTest, Handle1)
{
    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    auto hander = std::make_unique<LSPDiagnosticHandlerTest>(diag);
    std::string code = R"(
    packag demo
    main(argc:int, argv!:string="123") {
    let a:int=40
    let b = 2 ** -a
    print((a+3*b, (a+3) *b))
    }
)";
    diag.RegisterHandler(std::move(hander));
    Parser parser(code, diag, sm);
    OwnedPtr<File> file = parser.ParseTopLevel();
    EXPECT_EQ("expected declaration, found 'packag'", g_expectStr);
}

TEST(EngineTest, HandlerFilter)
{
    Cangjie::ICE::TriggerPointSetter iceSetter(static_cast<int64_t>(Cangjie::ICE::UNITTEST_TP));
    DiagnosticEngine diag;
    auto handler = std::make_unique<LSPDiagnosticHandlerTest>(diag);
    diag.RegisterHandler(std::move(handler));
    diag.DiagnoseRefactor(DiagKindRefactor::parse_expected_name, Position{0, 1, 1}, std::string{"test name"},
        std::string{"test02"}, std::string{"test03"});
    diag.DiagnoseRefactor(DiagKindRefactor::parse_expected_name, Position{0, 1, 1}, std::string{"test name"},
        std::string{"test02"}, std::string{"test03"});
    EXPECT_EQ(2, diag.GetErrorCount());
}

TEST(EngineTest, apiTest)
{
    DiagnosticEngine diag;
    std::string code = R"(
    main(argc:int, argv!:string="123") {
        var a = 1
        var b = """
        cangjie
"""
        var c = a + b
        return 0
    }
)";
    std::string expectedOut = R"(error: expected test name test1, found test2
 ==> test.cj:7:13:
  | 
2 |       main(argc:int, argv!:string="123") {
  |  _____________~
3 | |         var a = 1
  | |        ~    ~~~~~ test
  | |________|
  |          
4 |           var b = """
  |   ...
7 |           var c = a + b
  |               ^ expected test3 name here
  | 
note: this is a note test
 ==> test.cj:3:9:
  | 
3 |           var a = 1
  |  _________^
4 | |         var b = """
  | |________^
  | 
  # help: this is a note help test
note: this is a note test
 ==> test.cj:4:17:
  | 
4 |           var b = """
  |  _________________^
5 | |         cangjie
6 | | """
  | |___^
  | 
  # help: this is a note help test number 2

)";
    auto file = std::string{"test.cj"};
    SourceManager sm;
    sm.AddSource(file | FileUtil::IdenticalFunc, code);

    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::parse_expected_name, Position{1, 7, 13},
        std::string{"test name"}, std::string{"test1"}, std::string{"test2"});

    builder.AddMainHintArguments(std::string{"test3 name"});
    builder.AddHint(MakeRange(Position{1, 3, 13}, Position{1, 3, 18}), std::string{"test"});
    builder.AddHint(MakeRange(Position{1, 2, 13}, Position{1, 3, 9}), std::string{"this is other hint"});

    auto sub1 = SubDiagnostic{MakeRange(Position{1, 3, 9}, Position{1, 4, 9}), std::string{"this is a note test"}};

    auto noteHelp1 = DiagHelp{std::string{"this is a note help test"}};
    sub1.AddHelp(noteHelp1);
    builder.AddNote(sub1);

    auto sub2 = SubDiagnostic{MakeRange(Position{1, 4, 17}, Position{1, 6, 4}), std::string{"this is a note test"}};

    auto noteHelp2 = DiagHelp{std::string{"this is a note help test number 2"}};
    sub2.AddHelp(noteHelp2);
    builder.AddNote(sub2);

    std::ostringstream sOut;
    DiagnosticEmitter(builder.diagnostic, true, true, sOut, sm).Emit();

    EXPECT_EQ(sOut.str(), expectedOut);
}

TEST(EngineTest, apiTest2)
{
    DiagnosticEngine diag;
    std::string code = R"(
    main(argc:int,argv!:string="123") { var a = 1 + 2 * 3 }
)";
    std::string expectedOut = R"(error: expected test name test1, found test2
 ==> test.cj:2:10:
  | 
2 |     main(argc:int,argv!:string="123") { var a = 1 + 2 * 3 }
  |          ^   ~    ~     ~ this is other hint
  |          |        |
  |          |        test
  |          |
  |          expected test3 name here
  | 
note: this is a note test
 ==> test.cj:2:9:
  | 
2 |     main(argc:int,argv!:string="123") { var a = 1 + 2 * 3 }
  |         ^ 
  | 

)";
    auto file = std::string{"test.cj"};
    SourceManager sm;
    sm.AddSource(file | FileUtil::IdenticalFunc, code);

    auto builder = diag.DiagnoseRefactor(DiagKindRefactor::parse_expected_name, Position{1, 2, 10},
        std::string{"test name"}, std::string{"test1"}, std::string{"test2"});

    builder.AddMainHintArguments(std::string{"test3 name"});
    builder.AddHint(MakeRange(Position{1, 2, 19}, Position{1, 2, 20}), std::string{"test"});
    builder.AddHint(MakeRange(Position{1, 2, 25}, Position{1, 2, 26}), std::string{"this is other hint"});
    builder.AddHint(MakeRange(Position{1, 2, 14}, Position{1, 2, 15}), std::string{});

    auto sub1 = SubDiagnostic{MakeRange(Position{1, 2, 9}, Position{1, 2, 9}), std::string{"this is a note test"}};
    builder.AddNote(sub1);

    std::ostringstream sOut;
    DiagnosticEmitter(builder.diagnostic, true, true, sOut, sm).Emit();

    EXPECT_EQ(sOut.str(), expectedOut);
}

TEST(EngineTest, apiTest4)
{
    DiagnosticEngine diag;
    auto code = R"(
main(){
})";
    auto file = std::string{"test.cj"};
    SourceManager sm;
    sm.AddSource(file | FileUtil::IdenticalFunc, std::string(code));
    diag.SetSourceManager(&sm);
    auto node = Node();
    node.isInMacroCall = true;
    node.begin = Position{1, 1, 1};
    node.end = Position{1, 1, 5};

    diag.Diagnose(node, DiagKind::sema_ambiguous_arg_type, Position{1, 1, 1});
    EXPECT_EQ(diag.GetErrorCount(), 0);

    diag.Diagnose(node, DiagKind::sema_mismatched_type_for_pattern_in_vardecl);
    EXPECT_EQ(diag.GetErrorCount(), 0);

    diag.DiagnoseRefactor(DiagKindRefactor::sema_invalid_node_after_check, node);
    EXPECT_EQ(diag.GetErrorCount(), 0);

    diag.DiagnoseRefactor(DiagKindRefactor::sema_core_object_not_found_when_no_prelude, node, Position{1, 1, 1});
    EXPECT_EQ(diag.GetErrorCount(), 0);

    diag.DiagnoseRefactor(
        DiagKindRefactor::sema_unable_to_infer_return_type, node, MakeRange(Position{1, 1, 1}, Position{1, 1, 2}));
    EXPECT_EQ(diag.GetErrorCount(), 0);

    diag.DiagnoseRefactor(DiagKindRefactor::sema_invalid_called_object, node,
        Token(TokenKind::STATIC, "static", Position{1, 1, 6}, {1, 1, 12}));
    EXPECT_EQ(diag.GetErrorCount(), 0);
}

#ifdef __unix__
TEST(EngineTest, ShowColorTest)
{
    DiagnosticEngine diag;
    SourceManager sm;
    auto file = std::string{"test.cj"};
    auto code = std::string{""};
    sm.AddSource(file | FileUtil::IdenticalFunc, code);
    diag.SetSourceManager(&sm);

    auto builder2 = diag.DiagnoseRefactor(DiagKindRefactor::parse_unmatched_right_delimiter, Position{1, 1, 1}, "");
    std::ostringstream enableColorOut2;
    DiagnosticEmitter(builder2.diagnostic, false, true, enableColorOut2, sm).Emit();
    // red
    EXPECT_EQ(enableColorOut2.str().find("\x1b[31m"), 0);

    std::ostringstream disableColorOut2;
    DiagnosticEmitter(builder2.diagnostic, true, true, disableColorOut2, sm).Emit();
    EXPECT_EQ(disableColorOut2.str().find("\x1b[31m"), std::string::npos);
}

#elif _WIN32
TEST(EngineTest, ShowColorTest)
{
    DiagnosticEngine diag;
    SourceManager sm;
    auto file = std::string{"test.cj"};
    auto code = std::string{""};
    sm.AddSource(file | FileUtil::IdenticalFunc, code);
    diag.SetSourceManager(&sm);

    auto osVersion = Utils::GetOSVersion();
    if (osVersion.dwMajorVersion >= 10 && osVersion.dwBuildNumber >= 10586) {
        auto builder2 = diag.DiagnoseRefactor(DiagKindRefactor::parse_unmatched_right_delimiter, Position{1, 1, 1}, "");
        std::ostringstream enableColorOut2;
        DiagnosticEmitter(builder2.diagnostic, false, true, enableColorOut2, sm).Emit();
        // red
        EXPECT_EQ(enableColorOut2.str().find("\x1b[31m"), 0);

        std::ostringstream disableColorOut2;
        DiagnosticEmitter(builder2.diagnostic, true, true, disableColorOut2, sm).Emit();
        EXPECT_EQ(disableColorOut2.str().find("\x1b[31m"), std::string::npos);
    }
}
#endif

TEST(EngineTest, OldDiagKindDefGuard)
{
    // Note: The diagnostic engine has been refactored. This case is mainly used to ensure that no new
    // kind is added to the old diagnostic engine def file (cangjie/Basic/DiagnosticsAll.def). If you need to
    // modify, ensure that you only delete existing kinds, and do not add new ones.

    constexpr const char* const diagKindStr[]{
#define ERROR(Kind, ...) #Kind,
#define WARNING(Kind, ...) #Kind,
#define NOTE(Kind, Info) Info,
#include "cangjie/Basic/DiagnosticsAll.def"
#undef WARNING
#undef ERROR
#undef NOTE
    };
    constexpr const size_t diagEnds[]{
        static_cast<size_t>(DiagKind::chir_diag_end),
        static_cast<size_t>(DiagKind::macro_expand_diag_end),
        static_cast<size_t>(DiagKind::sema_diag_end),
    };
    // current number of each ending kind, need be modified when modify the def in cangjie/Basic/DiagnosticsAll.def
    const std::vector<size_t> endNumber = {
        15,  // 0
        57,  // 1
        295, // 2
    };
    bool oldDiagKindBeModified = false;
    for (size_t i = 0; i < endNumber.size(); ++i) {
        if (diagEnds[i] == endNumber[i]) {
            continue;
        }
        oldDiagKindBeModified = true;
        if (diagEnds[i] > endNumber[i]) {
            std::cerr << "The number of diagnostic kind: " + std::string(diagKindStr[diagEnds[i]]) + "(" +
                    std::to_string(diagEnds[i]) +
                    ") must not increase! Please add new diagnostic kind in .def files under the "
                    "include/cangjie/Basic/DiagRefactor."
                      << std::endl;
            break;
        } else {
            std::cout << "The number of diagnostic kind: " + std::string(diagKindStr[diagEnds[i]]) + "(" +
                    std::to_string(diagEnds[i]) + ") be modified! Please modify the expected item in endNumber[" +
                    std::to_string(i) + "]."
                      << std::endl;
        }
    }
    ASSERT_FALSE(oldDiagKindBeModified)
        << "Old diag kind be modified, please adpate case or def according the above hints.";
}