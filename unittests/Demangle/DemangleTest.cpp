// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <string>

#include "gtest/gtest.h"

#define private public

#include "Demangler.h"
#include "StdString.h"

using namespace Cangjie;

TEST(DemangleTest, PackageNameColonDelimiter)
{
    Demangler<StdString> demangler_1("4abc:");
    auto result_1 = demangler_1.DemanglePackageName();
    EXPECT_STREQ("abc:", result_1.pkgName.Str());

    Demangler<StdString> demangler_2("5abc:x");
    auto result_2 = demangler_2.DemanglePackageName();
    EXPECT_STREQ("abc::x", result_2.pkgName.Str());

    Demangler<StdString> demangler_3("7abc:xyz");
    auto result_3 = demangler_3.DemanglePackageName();
    EXPECT_STREQ("abc::xyz", result_3.pkgName.Str());
}

TEST(DemangleTest, PackageNameTrailingModuleSplit)
{
    // The module split character '$' at the end of a package name must be
    // converted to '/', including when it is the last character.
    Demangler<StdString> demangler("2a$");
    auto result = demangler.DemanglePackageName();
    EXPECT_STREQ("a/", result.pkgName.Str());
}

TEST(DemangleTest, PrimitiveFloat16)
{
    // Float16 is encoded as "Dh". A lone 'D' is not a valid primitive type.
    Demangler<StdString> float16("Dh");
    auto result = float16.Demangle(true);
    EXPECT_TRUE(result.IsValid());
    EXPECT_STREQ("Float16", result.GetFullName("::").Str());

    Demangler<StdString> loneD("D");
    auto invalid = loneD.Demangle(true);
    EXPECT_FALSE(invalid.IsValid());
}

TEST(DemangleTest, TupleAndVArrayManglingNumber)
{
    // Regress the base62 mangling-number boundary check: "0_" decodes to 1.
    Demangler<StdString> tuple("T0_iE");
    auto tupleResult = tuple.Demangle(true);
    EXPECT_TRUE(tupleResult.IsValid());
    EXPECT_STREQ("Tuple<Int32>", tupleResult.GetFullName("::").Str());

    Demangler<StdString> varray("V0_i");
    auto varrayResult = varray.Demangle(true);
    EXPECT_TRUE(varrayResult.IsValid());
    EXPECT_STREQ("VArray<Int32,$1>", varrayResult.GetFullName("::").Str());
}

TEST(DemangleTest, GenericConstraintsNameTrailingArgs)
{
    // ReplaceString pairs its loop condition with the current string length:
    // a pattern near the tail must still be replaced when earlier replacements
    // changed the length ("," -> " &" inside GetGenericConstraintsName).
    DemangleInfo<StdString> trailingComma{ "C", TypeKind::NAME };
    trailingComma.args = StdString("a,b,");
    EXPECT_STREQ(", C <: a &b &", trailingComma.GetGenericConstraintsName().Str());

    DemangleInfo<StdString> plainArgs{ "C", TypeKind::NAME };
    plainArgs.args = StdString("a,b");
    EXPECT_STREQ(", C <: a &b", plainArgs.GetGenericConstraintsName().Str());
}

TEST(DemangleTest, LocalParam)
{
    const char* mangled = "_CN18stdx.encoding.json9parseJsonHCNY_15JsonParserLocalEQLE";
    Demangler<StdString> demangler(mangled, ".");
    auto di = demangler.Demangle();
    ASSERT_TRUE(di.IsValid());
    EXPECT_STREQ("stdx.encoding.json", di.GetPkgName().Str());
    EXPECT_STREQ("(stdx.encoding.json.JsonParserLocal @ local!)", di.GetArgTypesName().Str());
    const char* expectedFull = "stdx.encoding.json.parseJson(stdx.encoding.json.JsonParserLocal @ local!)";
    std::string full =
        std::string(di.GetPkgName().Str()) + "." + std::string(di.GetFullName(demangler.ScopeResolution()).Str());
    EXPECT_STREQ(expectedFull, full.c_str());
}

TEST(DemangleTest, ThisParam)
{
    const char* mangled =
        "_CN18stdx.encoding.json14JsonArrayLocal3addHWLECN18stdx.encoding.json14JsonValueLocalEQLE";
    Demangler<StdString> demangler(mangled, ".");
    auto di = demangler.Demangle();
    ASSERT_TRUE(di.IsValid());
    const char* expectedFull =
        "stdx.encoding.json.JsonArrayLocal.add(this @ local!, stdx.encoding.json.JsonValueLocal @ local!)";
    std::string full =
        std::string(di.GetPkgName().Str()) + "." + std::string(di.GetFullName(demangler.ScopeResolution()).Str());
    EXPECT_STREQ(expectedFull, full.c_str());
}

TEST(DemangleTest, ClassTypeModal)
{
    // A modal class type: body `CN5hello1AE` + <type-mode> `QLE` (@local!). The closing 'E' of the
    // class body is consumed before the mode-set, so the modal attaches to the class itself.
    Demangler<StdString> demangler("CN5hello1AEQLE", ".");
    auto di = demangler.Demangle(true);
    ASSERT_TRUE(di.IsValid());
    EXPECT_STREQ("hello.A @ local!", di.GetFullName(demangler.ScopeResolution()).Str());
}

TEST(DemangleTest, PrimitiveTypeModal)
{
    // `l` = Int64; `QlE` = @ local?
    Demangler<StdString> demangler("lQlE", ".");
    auto di = demangler.Demangle(true);
    ASSERT_TRUE(di.IsValid());
    EXPECT_STREQ("Int64 @ local?", di.GetFullName(demangler.ScopeResolution()).Str());
}

TEST(DemangleTest, ParamInitNoDuplicatedParamList)
{
    // _CPI (default-param init): the owner renders its own parameter list, and the innermost
    // (param) decl renders its own list exactly once.
    const char* mangled = "_CPI9pkg1.pkg212global_test3HlllE1cHll";
    Demangler<StdString> demangler(mangled, ".");
    auto di = demangler.Demangle();
    ASSERT_TRUE(di.IsValid());
    const char* expectedFull = "pkg1.pkg2.global_test3(Int64, Int64, Int64).c(Int64, Int64)";
    std::string full =
        std::string(di.GetPkgName().Str()) + "." + std::string(di.GetFullName(demangler.ScopeResolution()).Str());
    EXPECT_STREQ(expectedFull, full.c_str());
}

TEST(DemangleTest, GlobalVarInitNoParentheses)
{
    // _CGV (global-var init): the trailing `Hv` must NOT render an empty parameter list —
    // the obfuscation cfg rule "pkg1.pkg2.global_a" (plain field) would otherwise miss the
    // target `global_a()` (field with empty parameter list) and wrongly rename the global.
    const char* mangled = "_CGV9pkg1.pkg28global_aHv";
    Demangler<StdString> demangler(mangled, ".");
    auto di = demangler.Demangle();
    ASSERT_TRUE(di.IsValid());
    const char* expectedFull = "pkg1.pkg2.global_a";
    std::string full =
        std::string(di.GetPkgName().Str()) + "." + std::string(di.GetFullName(demangler.ScopeResolution()).Str());
    EXPECT_STREQ(expectedFull, full.c_str());
}

TEST(DemangleTest, CallOperatorKeepsParamList)
{
    // The call operator's own name is "()"; its parameters still render once.
    const char* mangled = "_CN7default1AclHCNY_1AE";
    Demangler<StdString> demangler(mangled, ".");
    auto di = demangler.Demangle();
    ASSERT_TRUE(di.IsValid());
    const char* expectedFull = "default.A.()(default.A)";
    std::string full =
        std::string(di.GetPkgName().Str()) + "." + std::string(di.GetFullName(demangler.ScopeResolution()).Str());
    EXPECT_STREQ(expectedFull, full.c_str());
}

// ---------------------------------------------------------------------------
// Modal (local-mode) demangling.
//
// Symbol shapes below are the exact defines emitted by
// cjc --save-temps for a package with `class Box { mut prop pA: Int64; func touch(): Unit }`
// compiled with modal this/param annotations, i.e. real-world linkage names.
// ---------------------------------------------------------------------------

TEST(DemangleTest, ThisModeOverloads)
{
    // <this-mode> ::= W<payload>E. The three this-locality overloads of one member must
    // demangle to distinct readable names, and the W payload must not leak into the
    // parameter list (empty `()` is rendered from `v`, not from the mode-set).
    struct Case {
        const char* mangled;
        const char* expected;
    };
    for (auto c : {Case{"_CN7default3Box5touchHv", "default.Box.touch()"},
           Case{"_CN7default3Box5touchHWlEv", "default.Box.touch(this @ local?)"},
           Case{"_CN7default3Box5touchHWLEv", "default.Box.touch(this @ local!)"}}) {
        Demangler<StdString> demangler(c.mangled, ".");
        auto di = demangler.Demangle();
        ASSERT_TRUE(di.IsValid()) << c.mangled;
        std::string full =
            std::string(di.GetPkgName().Str()) + "." + std::string(di.GetFullName(demangler.ScopeResolution()).Str());
        EXPECT_STREQ(c.expected, full.c_str()) << c.mangled;
    }
}

TEST(DemangleTest, ModalConstructor)
{
    // Init without param modes: `6<init>HWLEv` must render as `.init(this @ local!)`.
    const char* mangled = "_CN7default3Box6<init>HWLEv";
    Demangler<StdString> demangler(mangled, ".");
    auto di = demangler.Demangle();
    ASSERT_TRUE(di.IsValid());
    std::string full =
        std::string(di.GetPkgName().Str()) + "." + std::string(di.GetFullName(demangler.ScopeResolution()).Str());
    EXPECT_STREQ("default.Box.init(this @ local!)", full.c_str());
}

TEST(DemangleTest, ModalPropertyAccessors)
{
    // Prop getter with modal this; setter additionally carries a modal value parameter.
    // `::get` / `::set` must stay stable because obfuscation configs reference them.
    struct Case {
        const char* mangled;
        const char* expected;
    };
    for (auto c : {Case{"_CN7default3Box2pApgHv", "default.Box.pA::get()"},
           Case{"_CN7default3Box2pApgHWlEv", "default.Box.pA::get(this @ local?)"},
           Case{"_CN7default3Box2pApgHWLEv", "default.Box.pA::get(this @ local!)"},
           Case{"_CN7default3Box2pApsHWlElQlE", "default.Box.pA::set(this @ local?, Int64 @ local?)"},
           Case{"_CN7default3Box2pApsHWLElQLE", "default.Box.pA::set(this @ local!, Int64 @ local!)"}}) {
        Demangler<StdString> demangler(c.mangled, ".");
        auto di = demangler.Demangle();
        ASSERT_TRUE(di.IsValid()) << c.mangled;
        std::string full =
            std::string(di.GetPkgName().Str()) + "." + std::string(di.GetFullName(demangler.ScopeResolution()).Str());
        EXPECT_STREQ(c.expected, full.c_str()) << c.mangled;
    }
}

TEST(DemangleTest, ModalParamList)
{
    // Both <type-mode> encodings in one parameter list: (Int64 @local?, Int64 @local!).
    // The arg list must render each mode in place and keep the comma separation intact.
    const char* mangled = "_CN7default14withLocalParamHlQlElQLE";
    Demangler<StdString> demangler(mangled, ".");
    auto di = demangler.Demangle();
    ASSERT_TRUE(di.IsValid());
    EXPECT_STREQ("(Int64 @ local?, Int64 @ local!)", di.GetArgTypesName().Str());
    std::string full =
        std::string(di.GetPkgName().Str()) + "." + std::string(di.GetFullName(demangler.ScopeResolution()).Str());
    EXPECT_STREQ("default.withLocalParam(Int64 @ local?, Int64 @ local!)", full.c_str());
}

TEST(DemangleTest, ModalTupleType)
{
    // Tuple body `T1_lE` + <type-mode> `QlE`: the mode-set attaches to the whole tuple.
    Demangler<StdString> demangler("T1_lEQlE", ".");
    auto di = demangler.Demangle(true);
    ASSERT_TRUE(di.IsValid());
    EXPECT_STREQ("Tuple<Int64> @ local?", di.GetFullName(demangler.ScopeResolution()).Str());
}

// For RawArray/VArray/CPointer the bodies are not 'E'-terminated: the <type-mode> directly
// follows the element body and belongs to the container (WithModal placement).
TEST(DemangleTest, ModalRawArrayType)
{
    // RawArray body `A0_l` (dims-1 = 0, element Int64, no 'E') + array mode `QLE`.
    Demangler<StdString> demangler("A0_lQLE", ".");
    auto di = demangler.Demangle(true);
    ASSERT_TRUE(di.IsValid());
    EXPECT_STREQ("RawArray<Int64>[] @ local!", di.GetFullName(demangler.ScopeResolution()).Str());
}

TEST(DemangleTest, ModalVArrayType)
{
    // VArray body `V3_l` (size-1 = 3, element Int64, no 'E') + array mode `QlE`.
    Demangler<StdString> demangler("V3_lQlE", ".");
    auto di = demangler.Demangle(true);
    ASSERT_TRUE(di.IsValid());
    EXPECT_STREQ("VArray<Int64,$4> @ local?", di.GetFullName(demangler.ScopeResolution()).Str());
}

TEST(DemangleTest, ModalCPointerType)
{
    // CPointer body `Pl` (prefix P, element Int64, no 'E') + pointer mode `QLE`.
    Demangler<StdString> demangler("PlQLE", ".");
    auto di = demangler.Demangle(true);
    ASSERT_TRUE(di.IsValid());
    EXPECT_STREQ("CPointer<Int64> @ local!", di.GetFullName(demangler.ScopeResolution()).Str());
}

TEST(DemangleTest, ModalContainerKeepsElementAndContainerModalDistinct)
{
    // The mode-set after the element body always belongs to the container.
    Demangler<StdString> demangler("A0_lQlE", ".");
    auto di = demangler.Demangle(true);
    ASSERT_TRUE(di.IsValid());
    EXPECT_STREQ("RawArray<Int64>[] @ local?", di.GetFullName(demangler.ScopeResolution()).Str());
}

TEST(DemangleTest, ModalRawArrayOfNestedArrays)
{
    // 1-dim array of 2-dim arrays: the inner array keeps its own `[]`, the outer one carries
    // the mode-set (`A0_` + inner body `A1_l` + outer mode `QLE`).
    Demangler<StdString> demangler("A0_A1_lQLE", ".");
    auto di = demangler.Demangle(true);
    ASSERT_TRUE(di.IsValid());
    EXPECT_STREQ("RawArray<RawArray<Int64>[][]>[] @ local!",
        di.GetFullName(demangler.ScopeResolution()).Str());
}

TEST(DemangleTest, ModalRawArrayOfFunctionType)
{
    // Array of a modal-return function type: the inner modal stays on the return type, the
    // array's own mode-set lands on the container.
    Demangler<StdString> demangler("A0_F0lQlEiEQLE", ".");
    auto di = demangler.Demangle(true);
    ASSERT_TRUE(di.IsValid());
    EXPECT_STREQ("RawArray<(Int32) -> Int64 @ local?>[] @ local!",
        di.GetFullName(demangler.ScopeResolution()).Str());
}

TEST(DemangleTest, ModalFunctionTypeKeepsMode)
{
    // A modal function type keeps its mode-set in the rendering.
    Demangler<StdString> demangler("F0ilEQlE", ".");
    auto di = demangler.Demangle(true);
    ASSERT_TRUE(di.IsValid());
    EXPECT_STREQ("(Int64) -> Int32 @ local?", di.GetFullName(demangler.ScopeResolution()).Str());
}

TEST(DemangleTest, InvalidModalPayloadRejected)
{
    // Payload letters other than L/l (U/u and I/i are reserved for future axes) must be
    // rejected instead of being silently rendered.
    Demangler<StdString> demangler("QxE", ".");
    auto di = demangler.Demangle(true);
    EXPECT_FALSE(di.IsValid());
}

TEST(DemangleTest, UnterminatedModalEncodingRejected)
{
    // A mode-set missing its terminating `E` must be rejected: a truncated symbol that
    // silently demangled would make obfuscation configs and stack traces misleading.
    Demangler<StdString> demangler("Ql", ".");
    auto di = demangler.Demangle(true);
    EXPECT_FALSE(di.IsValid());
}

// ---------------------------------------------------------------------------
// Nested local-function chains and compiler-generated wrappers: parent levels render
// their names only, and `_CV` wrappers place `<compiler generated>` before a single
// parameter list.
// ---------------------------------------------------------------------------

TEST(DemangleTest, NestedLocalFunctionParentRendersWithoutParams)
{
    // Local function chain: parent levels render their names only; only the innermost
    // function renders a parameter list.
    struct Case {
        const char* mangled;
        const char* expected;
    };
    for (auto c : {Case{"_CN6pkg_re4mainHRNat5ArrayIRNat6StringEEE10test_inputHY2_E12test_input_3Hl",
                       "pkg_re::main::test_input::test_input_3(Int64)"},
            Case{"_CN6pkg_re4mainHRNat5ArrayIRNat6StringEEE10test_inputHY2_E",
                       "pkg_re::main::test_input(std.core::Array<std.core::String>)"},
            Case{"_CN6pkg_re4mainHRNat5ArrayIRNat6StringEEE10test_inputHY2_E12test_input_3HlQlE",
                       "pkg_re::main::test_input::test_input_3(Int64 @ local?)"}}) {
        Demangler<StdString> demangler(c.mangled, "::");
        auto di = demangler.Demangle();
        ASSERT_TRUE(di.IsValid()) << c.mangled;
        std::string full = std::string(di.GetPkgName().Str()) + "::" +
            std::string(di.GetFullName(demangler.ScopeResolution()).Str());
        EXPECT_STREQ(c.expected, full.c_str()) << c.mangled;
    }
}

TEST(DemangleTest, NestedChainInsideLocalFunctionKeepsInnermostParams)
{
    // The middle level carries the same parameter type via substitution but does not
    // render it; only the innermost level does.
    const char* mangled = "_CN6pkg_re4mainHRNat5ArrayIRNat6StringEEE10test_inputHY2_E12test_input_2Hl";
    Demangler<StdString> demangler(mangled, "::");
    auto di = demangler.Demangle();
    ASSERT_TRUE(di.IsValid());
    std::string full =
        std::string(di.GetPkgName().Str()) + "::" + std::string(di.GetFullName(demangler.ScopeResolution()).Str());
    EXPECT_STREQ("pkg_re::main::test_input::test_input_2(Int64)", full.c_str());
}

TEST(DemangleTest, WrappedFunctionSuffixBeforeSingleParamList)
{
    // _CV virtual wrapper (real cjc --save-temps symbol): the `<compiler generated>` suffix
    // precedes ONE parameter list.
    const char* mangled = "_CVNad10LoggerImpl3logHCNax6TokensERNat6StringEb$Nad10LoggerImplE$CNbl6LoggerE";
    Demangler<StdString> demangler(mangled, "::");
    auto di = demangler.Demangle();
    ASSERT_TRUE(di.IsValid());
    std::string full =
        std::string(di.GetPkgName().Str()) + "::" + std::string(di.GetFullName(demangler.ScopeResolution()).Str());
    EXPECT_STREQ("std.deriving::LoggerImpl::log<compiler generated>(std.ast::Tokens, std.core::String, Bool)",
        full.c_str());
}
