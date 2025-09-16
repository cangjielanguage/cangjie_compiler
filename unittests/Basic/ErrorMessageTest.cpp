// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "gtest/gtest.h"
#include "cangjie/AST/Node.h"
#include "cangjie/Basic/Display.h"
#include "cangjie/Parse/Parser.h"

using namespace Cangjie;

TEST(ErrorMessageTest, MassageTest)
{
    static std::unordered_map<std::string, int> columnMap = {
        {"/*中中中*/ aaa 2\n", 15},
        {"\t/*中*/ aaa 2\n", 15},
        {"/*Ａ Ｂ Ｃ Ｄ Ｅ Ｆ Ｇ Ｈ Ｉ Ｊ Ｋ Ｌ Ｍ Ｎ Ｏ Ｐ Ｑ Ｒ Ｓ Ｔ Ｕ Ｖ Ｗ Ｘ Ｙ Ｚ*/ aaa 2\n", 86},
        {"/*∀ ∁ ∂ ∃ ∄ ∅ ∆ ∇ ∈ ∉ ∊ ∋ ∌ ∍ ∎ ∏ ∐ ∑ − ∓ ∔ ∕  ∗ °  √ ∛ ∜*/ aaa 2\n", 64},
        {"/*እው ሰላም ነው. እንዴት ነህ?*/ aaa 2\n", 28},
        {"/**Je t’aime*/ aaa 2\n", 19},
        {"/*Σ΄αγαπώ (Se agapo)*/ aaa 2\n", 27},
        {"/*你好。 你好吗？*/ aaa 2\n", 24},
        {"/*愛してる*/ aaa 2\n", 17},
        {"/*사랑해 (Saranghae)*/ aaa 2\n", 27},
        {"/*Я тебя люблю (Ya tebya liubliu)*/ aaa 2\n", 40},
        {"/* ?*/ aaa 2\n", 11},
        {"/*நீங்கள் எப்படி இருக்கிறீர்கள்?*/ aaa 2\n", 31},
        {"/*ਤੁਸੀਂ ਕਿਵੇਂ ਹੋ?*/ aaa 2\n", 19},
        {"/*👩  <200d>🔬  */ aaa 2\n", 21},
        {"/*𝓽𝓱𝓲𝓼 𝓲𝓼 𝓬𝓸𝓸𝓵*/ aaa 2\n", 21},
        {"/*(-■_■)*/ aaa 2\n", 15},
        {"/*(☞ﾟ∀ﾟ)☞*/ aaa 2\n", 16},
        {"/*         */ aaa 2\n", 18},
        {"/*／人 ◕ ‿‿ ◕ 人＼*/ aaa 2\n", 25},
        {"/*▣ ■ □ ▢ ◯ ▲ ▶ ► ▼ ◆ ◢ ◣ ◤ ◥*/ aaa 2\n", 36},
        {"/*₁₂₃₄*/ aaa 2\n", 13},
        {"/*Μένω στους Παξούς*/ aaa 2\n", 26},
    };
    std::string code = "func test() {\n";
    std::for_each(columnMap.begin(), columnMap.end(), [&](auto& c) { code += c.first; });
    code += "}\n";

    SourceManager sm;
    DiagnosticEngine diag;
    diag.SetSourceManager(&sm);
    std::unique_ptr<Parser> parser = std::make_unique<Parser>(1, code, diag, sm);
    parser->ParseTopLevel();

    std::vector<Diagnostic> ds = diag.GetCategoryDiagnostic(DiagCategory::PARSE);

    EXPECT_TRUE(!ds.empty()) << "Expected error in parser";
    std::for_each(ds.begin(), ds.end(), [&](auto& d) {
        auto source = sm.GetContentBetween(
            d.start.fileID, Position(d.start.line, 1), Position(d.start.line, std::numeric_limits<int>::max()));
        if (columnMap.count(source)) {
            auto printColumn = GetSpaceBeforeTarget(source, d.mainHint.range.begin.column);
            EXPECT_EQ(printColumn.length(), columnMap.at(source)) << "Column space differ in line" << source;
        }
    });
}
