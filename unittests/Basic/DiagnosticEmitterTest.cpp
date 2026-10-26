// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <fstream>
#include <sstream>

#include "gtest/gtest.h"
#include "cangjie/Basic/DiagnosticEmitter.h"

using namespace Cangjie;

namespace {
// Real-world shapes of macro-expansion warnings (issue #1172): a warning range may begin
// at a repacked expansion position while its end keeps an original source position, leaving
// a degenerate or even inverted range (Position::operator< ignores fileID) for the emitter.
// Any real (non-sentinel) kind works; the emitter only needs a valid severity and message.
constexpr auto DIAG_KIND = DiagKindRefactor::parse_expect_escape_dollar_token;

// Unlike MakeRange, this keeps the given begin/end even if fileIDs differ, mirroring the
// mixed macro-expansion coordinates that reach the emitter from macro diagnostics.
Range MakeRawRange(const Position& begin, const Position& end)
{
    Range range = MakeRange(DEFAULT_POSITION, DEFAULT_POSITION);
    range.begin = begin;
    range.end = end;
    return range;
}

Diagnostic MakeMacroRangeDiag(const Range& mainRange, const Range& hintRange)
{
    Diagnostic d(true, mainRange, DIAG_KIND);
    d.start = hintRange.begin;
    d.end = hintRange.end;
    d.mainHint.range = hintRange;
    d.mainHint.str = "unused function";
    return d;
}

TEST(DiagnosticEmitterTest, EmitCrossFileInvertedRange)
{
    // The crashing shape of issue #1172: begin lands in the *.macrocall source at a high
    // line while end stays in the original file at a low line, so the hint range is inverted
    // under Position::operator<. The emitter must degrade gracefully instead of aborting.
    // The sources must exist on disk: *.macrocall rendering checks their existence.
    SourceManager sm;
    const std::string origPath = "/tmp/cj_diag_emitter_ut_orig.cj";
    const std::string macroPath = "/tmp/cj_diag_emitter_ut_orig.macrocall";
    {
        std::ofstream origFile(origPath);
        origFile << "a\nb\nc\nfoo(){}\n";
        std::ofstream macroFile(macroPath);
        macroFile << std::string(20, 'x') << "\n";
    }
    auto orig = sm.AddSource(origPath, "a\nb\nc\nfoo(){}\n");
    auto macrocall = sm.AddSource(macroPath, std::string(20, 'x') + "\n");
    Diagnostic d = MakeMacroRangeDiag(MakeRange(Position(macrocall, 9, 3), Position(macrocall, 9, 4)),
        MakeRawRange(Position(macrocall, 9, 3), Position(orig, 4, 2)));
    std::ostringstream out;
    DiagnosticEmitter emitter(d, true, false, out, sm);
    EXPECT_TRUE(emitter.Emit());
    // The location header is still rendered from the macrocall side.
    EXPECT_NE(out.str().find("cj_diag_emitter_ut_orig.macrocall:9:3"), std::string::npos);
}

TEST(DiagnosticEmitterTest, EmitInvertedSameLineRange)
{
    // Same-line inversion falls back to the default displayed width instead of a bogus substr.
    SourceManager sm;
    auto fileID = sm.AddSource("inv.cj", "foo(){}\n");
    Diagnostic d = MakeMacroRangeDiag(MakeRange(Position(fileID, 1, 1), Position(fileID, 1, 2)),
        MakeRange(Position(fileID, 1, 8), Position(fileID, 1, 4)));
    std::ostringstream out;
    DiagnosticEmitter emitter(d, true, false, out, sm);
    EXPECT_TRUE(emitter.Emit());
    EXPECT_NE(out.str().find("foo(){}"), std::string::npos);
}

TEST(DiagnosticEmitterTest, EmitMacroCallNoteBox)
{
    // The uf1 rendering shape: a single-line main hint plus a cross-line macro-call note box.
    SourceManager sm;
    auto fileID = sm.AddSource("uf.cj", "@M1\nfoo(){}\nbar(){}\n");
    Diagnostic d = MakeMacroRangeDiag(MakeRange(Position(fileID, 2, 1), Position(fileID, 2, 2)),
        MakeRange(Position(fileID, 2, 1), Position(fileID, 2, 2)));
    d.end = Position(fileID, 3, 2);
    d.subDiags.emplace_back(MakeRange(Position(fileID, 2, 1), Position(fileID, 3, 2)), "note");
    std::ostringstream out;
    DiagnosticEmitter emitter(d, true, false, out, sm);
    EXPECT_TRUE(emitter.Emit());
    EXPECT_NE(out.str().find("foo(){}"), std::string::npos);
    EXPECT_NE(out.str().find("bar(){}"), std::string::npos);
}
} // namespace
