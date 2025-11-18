// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/Basic/DiagnosticEngine.h"
#include "cangjie/Basic/SourceManager.h"
#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/Basic/Utils.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/Parse/Parser.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/ProfileRecorder.h"
#include "cangjie/Utils/ICEUtil.h"
#include <gtest/gtest.h>

using namespace Cangjie;

std::vector<Diagnostic> FormatCjdCheckResult(DiagnosticEngine& diag)
{
    auto diags = diag.GetCategoryDiagnostic(DiagCategory::OTHER);
    auto parseDiags = diag.GetCategoryDiagnostic(DiagCategory::PARSE);
    auto lexDiags = diag.GetCategoryDiagnostic(DiagCategory::LEX);
    diags.insert(diags.end(), lexDiags.begin(), lexDiags.end());
    diags.insert(diags.end(), parseDiags.begin(), parseDiags.end());
    return diags;
}

std::vector<Diagnostic> CheckCjd(const std::string& cjdFilePath)
{
    Utils::ProfileRecorder recorder("CheckCjd", "CheckCjd");
    Cangjie::ICE::TriggerPointSetter iceSetter(CompileStage::PARSE);
    DiagnosticEngine diag;
    SourceManager sm;
    // disable console output of errors
    diag.SetSourceManager(&sm);
    diag.SetDisableWarning(true);
    diag.SetIsEmitter(false);
    std::string failedReason;
    GlobalOptions::ValidateInputFilePath(cjdFilePath, DiagKindRefactor::driver_invalid_source_file, diag);
    auto driverDiags = diag.GetCategoryDiagnostic(DiagCategory::OTHER);
    if (!driverDiags.empty()) {
        return FormatCjdCheckResult(diag);
    }
    auto content = FileUtil::ReadFileContent(cjdFilePath, failedReason);
    CJC_ASSERT(content);
    auto fileID = sm.AddSource(cjdFilePath, *content);
    Parser parser{fileID, *content, diag, sm, false, true};
    parser.ParseTopLevel();
    return FormatCjdCheckResult(diag);
}

class CheckCjdTest : public ::testing::Test {
protected:
    void SetUp() override
    {
#ifdef PROJECT_SOURCE_DIR
        std::string projectPath = PROJECT_SOURCE_DIR;
#else
        std::string projectPath = "..";
#endif
#ifdef _WIN32
        srcPath = projectPath + "\\unittests\\Parse\\ParseCangjieFiles\\";
#else
        srcPath = projectPath + "/unittests/Parse/ParseCangjieFiles/";
#endif
    }
    std::string srcPath;
};

TEST_F(CheckCjdTest, TwoErrors)
{
    auto src = FileUtil::JoinPath(srcPath, "twoerrors.cj.d");
    auto diags = CheckCjd(src);
    ASSERT_EQ(diags.size(), 2);
    EXPECT_EQ(diags[0].rKind, DiagKindRefactor::lex_unterminated_single_line_string);
    EXPECT_EQ(diags[1].rKind, DiagKindRefactor::parse_expected_one_of_identifier_or_pattern);
}

TEST_F(CheckCjdTest, Good)
{
    auto src = FileUtil::JoinPath(srcPath, "good.cj.d");
    auto diags = CheckCjd(src);
    ASSERT_EQ(diags.size(), 0);
}
