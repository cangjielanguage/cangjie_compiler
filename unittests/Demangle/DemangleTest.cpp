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
