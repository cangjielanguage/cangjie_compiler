// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "gtest/gtest.h"

#include "../../src/Sema/NativeFFI/ArkTS/FunctionSource.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Parse/Parser.h"
#include "cangjie/Sema/TypeManager.h"

using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI::ArkTS;

// Static checks for the recursive ArkTS type mapping: the classification
// matrix, the conversion emission shapes, and the scalar-name backwards
// compatibility.
// Everything here is parse-level: generated wrappers must parse cleanly and
// carry the documented symbols; runtime behaviour needs the real SDK.
class ArkTSTypeMappingTest : public testing::Test {
protected:
    SourceManager sm;
    DiagnosticEngine diag;

    OwnedPtr<File> Parse(const std::string& code)
    {
        diag.SetSourceManager(&sm);
        GlobalOptions compileOptions;
        compileOptions.experimentalMode = true;
        Parser parser(code, diag, sm, {0, 1, 1}, false, &compileOptions);
        auto file = parser.ParseTopLevel();
        diag.EmitCategoryDiagnostics(DiagCategory::PARSE);
        return file;
    }

    // One generated placeholder literal as it appears in the raw wrapper text:
    // the sentinel prefix separates it from a user string value that happens to
    // equal the same name (for example an enum member @RawValue["__module__"]).
    static std::string Placeholder(const std::string& key)
    {
        return std::string(ARKTS_PLACEHOLDER_LITERAL_PREFIX) + key;
    }

    void ExpectSingleCall(Ptr<File> file, const std::string& label, size_t moduleLoads = 1)
    {
        size_t calls = 0;
        size_t loads = 0;
        Walker(file, [&](Ptr<Node> node) {
            if (auto access = DynamicCast<MemberAccess*>(node)) {
                calls += access->field == "callMethod" ? 1 : 0;
                loads += access->field == "requireArkModule" ? 1 : 0;
            }
            return VisitAction::WALK_CHILDREN;
        }).Walk();
        EXPECT_EQ(calls, 1) << label;
        EXPECT_EQ(loads, moduleLoads) << label;
    }

    // Non-overlapping occurrences of @p needle in @p text. The generated code
    // is inspected as text, so a call site can be pinned to exactly one guard
    // and exactly one library error code comparison.
    static size_t CountOccurrences(const std::string& text, const std::string& needle)
    {
        size_t count = 0;
        for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) {
            ++count;
        }
        return count;
    }

    static ArkTSTypeDesc ScalarDesc(const std::string& name)
    {
        return {ArkTSTypeDesc::Kind::SCALAR, name, {}};
    }

    static ArkTSTypeDesc WrappedDesc(ArkTSTypeDesc::Kind kind, ArkTSTypeDesc inner)
    {
        return {kind, {}, {std::move(inner)}};
    }

    static ArkTSTypeDesc TupleDesc(std::vector<ArkTSTypeDesc> elements)
    {
        return {ArkTSTypeDesc::Kind::TUPLE, {}, std::move(elements)};
    }

    // A prepared foreign class used as the reference leaf of a description.
    static void InitWrappedPoint(ClassDecl& point)
    {
        point.identifier = "Point";
        point.fullPackageName = "pkg";
        point.EnableAttr(Attribute::ARKTS_CLASS_WRAPPER);
    }
};

TEST_F(ArkTSTypeMappingTest, ClassifyCoversTheFullScalarTable)
{
    // Number candidates (never Int64/UInt64), bigint candidates, boolean,
    // string, void/undefined/null and never.
    const std::vector<std::pair<TypeKind, std::string>> primitives{{TypeKind::TYPE_INT8, "Int8"},
        {TypeKind::TYPE_INT16, "Int16"}, {TypeKind::TYPE_INT32, "Int32"}, {TypeKind::TYPE_UINT8, "UInt8"},
        {TypeKind::TYPE_UINT16, "UInt16"}, {TypeKind::TYPE_UINT32, "UInt32"}, {TypeKind::TYPE_FLOAT16, "Float16"},
        {TypeKind::TYPE_FLOAT32, "Float32"}, {TypeKind::TYPE_FLOAT64, "Float64"}, {TypeKind::TYPE_RUNE, "Rune"},
        {TypeKind::TYPE_INT64, "Int64"}, {TypeKind::TYPE_UINT64, "UInt64"}, {TypeKind::TYPE_BOOLEAN, "Bool"},
        {TypeKind::TYPE_UNIT, "Unit"}, {TypeKind::TYPE_NOTHING, "Nothing"}};
    for (const auto& [kind, name] : primitives) {
        auto desc = ArkTSClassifyType(TypeManager::GetPrimitiveTy(kind));
        ASSERT_EQ(desc.kind, ArkTSTypeDesc::Kind::SCALAR) << name;
        EXPECT_EQ(desc.scalar, name);
        EXPECT_TRUE(desc.elements.empty()) << name;
    }
    // String is the std.core struct, not a primitive kind.
    StructDecl stringDecl;
    stringDecl.identifier = "String";
    stringDecl.fullPackageName = "std.core";
    StructTy stringTy("String", stringDecl, {});
    auto stringDesc = ArkTSClassifyType(&stringTy);
    ASSERT_EQ(stringDesc.kind, ArkTSTypeDesc::Kind::SCALAR);
    EXPECT_EQ(stringDesc.scalar, "String");
    StructDecl bigIntDecl;
    bigIntDecl.identifier = "BigInt";
    bigIntDecl.fullPackageName = "std.math.numeric";
    StructTy bigIntTy("BigInt", bigIntDecl, {});
    auto big = ArkTSClassifyType(&bigIntTy);
    ASSERT_EQ(big.kind, ArkTSTypeDesc::Kind::SCALAR);
    EXPECT_EQ(big.scalar, "BigInt");
    EXPECT_EQ(ArkTSClassifyType(nullptr).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
    EXPECT_EQ(ArkTSClassifyType(TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT_NATIVE)).kind,
        ArkTSTypeDesc::Kind::UNSUPPORTED);
}

TEST_F(ArkTSTypeMappingTest, ClassifyCompositesAndTheirNesting)
{
    auto int32 = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT32);
    StructDecl stringDecl;
    stringDecl.identifier = "String";
    stringDecl.fullPackageName = "std.core";
    StructTy stringTy("String", stringDecl, {});
    EnumDecl optionDecl;
    optionDecl.identifier = "Option";
    optionDecl.fullPackageName = "std.core";
    StructDecl arrayDecl;
    arrayDecl.identifier = "Array";
    arrayDecl.fullPackageName = "std.core";
    // The package-pinned prepared generic preset array maps to a REFERENCE
    // leaf carrying its element description; a same-named class without the
    // prepared-wrapper attribute (a stale CJO, or any other same-named
    // declaration) stays unsupported.
    ClassDecl arkTsArrayDecl;
    arkTsArrayDecl.identifier = "ArkTSArray";
    arkTsArrayDecl.fullPackageName = "std.interop.arkts";
    arkTsArrayDecl.EnableAttr(Attribute::ARKTS_CLASS_WRAPPER);
    ClassDecl staleArkTsArrayDecl;
    staleArkTsArrayDecl.identifier = "ArkTSArray";
    staleArkTsArrayDecl.fullPackageName = "std.interop.arkts";

    EnumTy optionInt32("Option", optionDecl, {int32});
    auto optional = ArkTSClassifyType(&optionInt32);
    ASSERT_EQ(optional.kind, ArkTSTypeDesc::Kind::OPTIONAL);
    ASSERT_EQ(optional.elements.size(), 1);
    EXPECT_EQ(optional.elements.front().kind, ArkTSTypeDesc::Kind::SCALAR);
    EXPECT_EQ(optional.elements.front().scalar, "Int32");

    StructTy arrayInt32("Array", arrayDecl, {int32});
    auto copy = ArkTSClassifyType(&arrayInt32);
    ASSERT_EQ(copy.kind, ArkTSTypeDesc::Kind::ARRAY_COPY);
    ASSERT_EQ(copy.elements.size(), 1);
    EXPECT_EQ(copy.elements.front().scalar, "Int32");

    // The stdlib preset itself: a generatable REFERENCE leaf carrying the
    // element description; the same name without the prepared-wrapper
    // attribute (a stale CJO) stays unsupported with no elements.
    ClassTy refInt32("ArkTSArray", arkTsArrayDecl, {int32});
    auto reference = ArkTSClassifyType(&refInt32);
    ASSERT_EQ(reference.kind, ArkTSTypeDesc::Kind::REFERENCE);
    ASSERT_EQ(reference.elements.size(), 1);
    EXPECT_EQ(reference.elements.front().kind, ArkTSTypeDesc::Kind::SCALAR);
    EXPECT_EQ(reference.elements.front().scalar, "Int32");
    EXPECT_TRUE(ArkTSTypeDescGeneratable(reference));
    EXPECT_EQ(ArkTSTypeDescName(reference), "ArkTSArray<Int32>");

    ClassTy staleRefInt32("ArkTSArray", staleArkTsArrayDecl, {int32});
    auto stale = ArkTSClassifyType(&staleRefInt32);
    EXPECT_EQ(stale.kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
    EXPECT_TRUE(stale.elements.empty());

    StructTy arrayOfOption("Array", arrayDecl, {&optionInt32});
    auto nestedCopy = ArkTSClassifyType(&arrayOfOption);
    ASSERT_EQ(nestedCopy.kind, ArkTSTypeDesc::Kind::ARRAY_COPY);
    EXPECT_EQ(nestedCopy.elements.front().kind, ArkTSTypeDesc::Kind::OPTIONAL);

    EnumTy optionOfArray("Option", optionDecl, {&arrayInt32});
    auto nestedOption = ArkTSClassifyType(&optionOfArray);
    ASSERT_EQ(nestedOption.kind, ArkTSTypeDesc::Kind::OPTIONAL);
    EXPECT_EQ(nestedOption.elements.front().kind, ArkTSTypeDesc::Kind::ARRAY_COPY);

    StructTy arrayOfArray("Array", arrayDecl, {&arrayInt32});
    auto doublyNested = ArkTSClassifyType(&arrayOfArray);
    ASSERT_EQ(doublyNested.kind, ArkTSTypeDesc::Kind::ARRAY_COPY);
    EXPECT_EQ(doublyNested.elements.front().kind, ArkTSTypeDesc::Kind::ARRAY_COPY);

    TupleTy pair({int32, &stringTy});
    auto tuple = ArkTSClassifyType(&pair);
    ASSERT_EQ(tuple.kind, ArkTSTypeDesc::Kind::TUPLE);
    ASSERT_EQ(tuple.elements.size(), 2);
    EXPECT_EQ(tuple.elements.front().scalar, "Int32");
    EXPECT_EQ(tuple.elements.back().scalar, "String");

    // An unprepared ArkTSArray element poisons the whole tuple (foreign-side
    // rule: one unsupported element leaves the position unsupported).
    TupleTy mixed({&optionInt32, &staleRefInt32});
    EXPECT_EQ(ArkTSClassifyType(&mixed).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
}

TEST_F(ArkTSTypeMappingTest, ClassifyRejectsUnsupportedPositions)
{
    auto int32 = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT32);
    auto intNative = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT_NATIVE);
    EnumDecl coreOption;
    coreOption.identifier = "Option";
    coreOption.fullPackageName = "std.core";
    EnumDecl userOption;
    userOption.identifier = "Option";
    userOption.fullPackageName = "application";
    StructDecl coreArray;
    coreArray.identifier = "Array";
    coreArray.fullPackageName = "std.core";
    StructDecl userArray;
    userArray.identifier = "Array";
    userArray.fullPackageName = "application";
    StructDecl mapDecl;
    mapDecl.identifier = "Map";
    mapDecl.fullPackageName = "std.collection";
    StructDecl setDecl;
    setDecl.identifier = "Set";
    setDecl.fullPackageName = "std.collection";
    StructDecl userBigInt;
    userBigInt.identifier = "BigInt";
    userBigInt.fullPackageName = "application";
    ClassDecl userArkTsArray;
    userArkTsArray.identifier = "ArkTSArray";
    userArkTsArray.fullPackageName = "application";
    ClassDecl facadeArkTsArray;
    facadeArkTsArray.identifier = "ArkTSArray";
    facadeArkTsArray.fullPackageName = "arkts.lang";

    // Foreign signatures reject nested Option.
    EnumTy innerOption("Option", coreOption, {int32});
    EnumTy nestedOption("Option", coreOption, {&innerOption});
    EXPECT_EQ(ArkTSClassifyType(&nestedOption).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
    // Design §3.4 permits it for a non-foreign exported variable, where the
    // layers unwrap to one ArkTS `T | undefined`; the opt-in keeps every other
    // caller on the default refusal.
    auto allowedNested = ArkTSClassifyType(&nestedOption, nullptr, /*allowNestedOptional=*/true);
    ASSERT_EQ(allowedNested.kind, ArkTSTypeDesc::Kind::OPTIONAL);
    ASSERT_EQ(allowedNested.elements.size(), 1);
    EXPECT_EQ(allowedNested.elements.front().kind, ArkTSTypeDesc::Kind::OPTIONAL);
    EXPECT_FALSE(ArkTSTypeDescGeneratable(allowedNested));
    EXPECT_TRUE(ArkTSTypeDescGeneratable(allowedNested, /*allowNestedOptional=*/true));
    EXPECT_EQ(ArkTSTypeDescName(allowedNested), "Option<Option<Int32>>");
    // The same opt-in accepts deeper layers recursively.
    EnumTy tripleOption("Option", coreOption, {&nestedOption});
    auto layered = ArkTSClassifyType(&tripleOption, nullptr, /*allowNestedOptional=*/true);
    ASSERT_EQ(layered.kind, ArkTSTypeDesc::Kind::OPTIONAL);
    ASSERT_EQ(layered.elements.front().kind, ArkTSTypeDesc::Kind::OPTIONAL);
    EXPECT_EQ(layered.elements.front().elements.front().kind, ArkTSTypeDesc::Kind::OPTIONAL);
    EXPECT_TRUE(ArkTSTypeDescGeneratable(layered, /*allowNestedOptional=*/true));

    EnumTy optionIntNative("Option", coreOption, {intNative});
    EXPECT_EQ(ArkTSClassifyType(&optionIntNative).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);

    StructTy arrayNestedOption("Array", coreArray, {&nestedOption});
    EXPECT_EQ(ArkTSClassifyType(&arrayNestedOption).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);

    EnumTy appOption("Option", userOption, {int32});
    EXPECT_EQ(ArkTSClassifyType(&appOption).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);

    StructTy appArray("Array", userArray, {int32});
    EXPECT_EQ(ArkTSClassifyType(&appArray).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);

    StructTy map("Map", mapDecl, {});
    EXPECT_EQ(ArkTSClassifyType(&map).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);

    StructTy set("Set", setDecl, {int32});
    EXPECT_EQ(ArkTSClassifyType(&set).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);

    StructTy foreignBigInt("BigInt", userBigInt, {});
    EXPECT_EQ(ArkTSClassifyType(&foreignBigInt).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);

    // Every ArkTSArray-named class outside the preset package is unsupported.
    // A class that merely shares the name in another package - a user package
    // or the arkts.lang facade - is unsupported as well: classification never
    // matches a declaration name, so a same-named class can only reach the
    // generic rejection and never the preset deferral routing.
    ClassTy appArkTsArray("ArkTSArray", userArkTsArray, {int32});
    EXPECT_EQ(ArkTSClassifyType(&appArkTsArray).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);

    ClassTy langArkTsArray("ArkTSArray", facadeArkTsArray, {int32});
    EXPECT_EQ(ArkTSClassifyType(&langArkTsArray).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);

    // Only tuples with at least two elements map.
    TupleTy single({int32});
    EXPECT_EQ(ArkTSClassifyType(&single).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);

    TupleTy withBadElement({int32, &set});
    EXPECT_EQ(ArkTSClassifyType(&withBadElement).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
}

TEST_F(ArkTSTypeMappingTest, DeferredExternalTypeRecognitionIsPinnedToThePresetPackage)
{
    auto int32 = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT32);
    // The preset declarations of std.interop.arkts are recognised by their
    // declaring package - the same pinning BigInt gets in std.math.numeric.
    ClassDecl presetArray;
    presetArray.identifier = "ArkTSArray";
    presetArray.fullPackageName = "std.interop.arkts";
    ClassDecl presetString;
    presetString.identifier = "ArkTSString";
    presetString.fullPackageName = "std.interop.arkts";
    InterfaceDecl presetType;
    presetType.identifier = "ArkTSType";
    presetType.fullPackageName = "std.interop.arkts";
    // Same names outside that package: a user package and the arkts.lang facade.
    ClassDecl userArray;
    userArray.identifier = "ArkTSArray";
    userArray.fullPackageName = "application";
    ClassDecl userString;
    userString.identifier = "ArkTSString";
    userString.fullPackageName = "application";
    ClassDecl userType;
    userType.identifier = "ArkTSType";
    userType.fullPackageName = "application";
    ClassDecl facadeArray;
    facadeArray.identifier = "ArkTSArray";
    facadeArray.fullPackageName = "arkts.lang";

    ClassTy presetArrayInt32("ArkTSArray", presetArray, {int32});
    ClassTy presetStringPlain("ArkTSString", presetString, {});
    InterfaceTy presetTypePlain("ArkTSType", presetType, {});
    // The prepared generic preset array left the deferral routing: its
    // positions classify as generatable reference leaves, so the name routes
    // nothing; ArkTSType and an unprepared ArkTSString still defer.
    EXPECT_EQ(ArkTSDeferredExternalTypeName(&presetArrayInt32), "");
    EXPECT_EQ(ArkTSDeferredExternalTypeName(&presetStringPlain), "ArkTSString");
    EXPECT_EQ(ArkTSDeferredExternalTypeName(&presetTypePlain), "ArkTSType");

    // A generic instance resolves to the same declaration, and the nested
    // positions are searched recursively.
    ClassTy userArrayInt32("ArkTSArray", userArray, {int32});
    ClassTy userStringPlain("ArkTSString", userString, {});
    ClassTy userTypePlain("ArkTSType", userType, {});
    ClassTy facadeArrayInt32("ArkTSArray", facadeArray, {int32});
    EXPECT_EQ(ArkTSDeferredExternalTypeName(&userArrayInt32), "");
    EXPECT_EQ(ArkTSDeferredExternalTypeName(&userStringPlain), "");
    EXPECT_EQ(ArkTSDeferredExternalTypeName(&userTypePlain), "");
    EXPECT_EQ(ArkTSDeferredExternalTypeName(&facadeArrayInt32), "");
    StructDecl coreArray;
    coreArray.identifier = "Array";
    coreArray.fullPackageName = "std.core";
    StructTy arrayOfPreset("Array", coreArray, {&presetArrayInt32});
    StructTy arrayOfUser("Array", coreArray, {&userArrayInt32});
    EXPECT_EQ(ArkTSDeferredExternalTypeName(&arrayOfPreset), "");
    EXPECT_EQ(ArkTSDeferredExternalTypeName(&arrayOfUser), "");
    EXPECT_EQ(ArkTSDeferredExternalTypeName(nullptr), "");
    // Recognition only routes diagnostics: an unprepared preset shape (a
    // stale CJO) stays unsupported with the generic message.
    EXPECT_EQ(ArkTSClassifyType(&presetArrayInt32).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
}

TEST_F(ArkTSTypeMappingTest, EmittedOptionBranchesHandleNullAndUndefined)
{
    auto code = ArkTSForeignWrapperSource({WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc("Int32"))},
        WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc("String")), 1, "__locals_");
    // To-JS: the Option.Some arm converts the bound value, the Option.None
    // arm is undefined. The constructors are spelled through the enum name,
    // so only Option itself is a pinnable support reference.
    EXPECT_NE(code.find("match (__argument_0)"), std::string::npos);
    EXPECT_NE(code.find("case Option.Some(__locals_some_0) =>"), std::string::npos);
    EXPECT_NE(code.find("__locals_context.number(Float64(__locals_some_0)).toJSValue()"), std::string::npos);
    EXPECT_NE(code.find("case Option.None => __locals_context.undefined().toJSValue()"), std::string::npos);
    // From-JS: the isNull()/isUndefined() predicates decide Option.None; no
    // asUndefined() probe, everything else reaches Option.Some with the
    // element's own checks. The only catch is the call translation.
    EXPECT_NE(code.find("if (__locals_result.isNull() || __locals_result.isUndefined())"), std::string::npos);
    EXPECT_NE(code.find("Option.Some(__locals_result.asString().toString())"), std::string::npos);
    EXPECT_EQ(code.find("asUndefined"), std::string::npos);
    EXPECT_NE(code.find("catch (__locals_error_0: BusinessException)"), std::string::npos);
    // The placeholder and rebinding contract of the wrapper survives.
    EXPECT_NE(code.find(Placeholder("__module__")), std::string::npos);
    EXPECT_NE(code.find(Placeholder("__namespace_0__")), std::string::npos);
    EXPECT_NE(code.find(Placeholder("__name__")), std::string::npos);
    EXPECT_NE(code.find(": ark_interop.JSContext ="), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ExpectSingleCall(file.get(), "option wrapper");
}

TEST_F(ArkTSTypeMappingTest, OptionalArgumentsForwardUpToTheLastSome)
{
    // The wrapper cannot tell an omitted optional from an explicit None, so
    // the Option value selects the forwarded list. These expectations encode
    // the rule:
    //   1 optional: None -> no argument, Some -> [value]
    //   2 optionals: (None, Some) -> [undefined, value],
    //                (Some, None) -> [value],
    //                (None, None) -> []
    //   required + optional: the required position is always forwarded, then
    //                the same trailing rule applies.
    auto optional = [](const std::string& name) {
        return WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc(name));
    };
    auto single = ArkTSForeignWrapperSource({optional("Int32")}, ScalarDesc("Unit"), 0, "__locals_");
    EXPECT_NE(single.find("let __locals_last_0 = if (__argument_0.isSome()) {\n        0\n    } else {\n        -1\n    }"),
        std::string::npos);
    EXPECT_NE(single.find("Array<ark_interop.JSValue>(__locals_last_0 + 1, { __locals_index_0 =>"), std::string::npos);
    EXPECT_NE(single.find("case 0 => __locals_arg_0"), std::string::npos);
    auto singleFile = Parse(single);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ExpectSingleCall(singleFile.get(), "single optional");

    auto pair = ArkTSForeignWrapperSource({optional("Int32"), optional("String")}, ScalarDesc("Unit"), 0, "__locals_");
    EXPECT_NE(pair.find("let __locals_last_0 = if (__argument_1.isSome()) {\n        1\n    } else if "
                        "(__argument_0.isSome()) {\n        0\n    } else {\n        -1\n    }"),
        std::string::npos);
    EXPECT_NE(pair.find("Array<ark_interop.JSValue>(__locals_last_0 + 1, { __locals_index_0 =>"), std::string::npos);
    EXPECT_NE(pair.find("case 0 => __locals_arg_0"), std::string::npos);
    EXPECT_NE(pair.find("case 1 => __locals_arg_1"), std::string::npos);
    auto pairFile = Parse(pair);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ExpectSingleCall(pairFile.get(), "optional pair");

    auto requiredThenOptional =
        ArkTSForeignWrapperSource({ScalarDesc("Int32"), optional("String")}, ScalarDesc("Unit"), 0, "__locals_");
    EXPECT_NE(requiredThenOptional.find("let __locals_last_0 = if (__argument_1.isSome()) {\n        1\n    } else "
                                        "{\n        0\n    }"),
        std::string::npos);
    EXPECT_NE(requiredThenOptional.find("case 0 => __locals_arg_0"), std::string::npos);
    EXPECT_NE(requiredThenOptional.find("case 1 => __locals_arg_1"), std::string::npos);
    auto requiredFile = Parse(requiredThenOptional);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ExpectSingleCall(requiredFile.get(), "required plus optional");
}

TEST_F(ArkTSTypeMappingTest, PositionalOptionStaysAnExplicitArgument)
{
    // The with-rest/open spelling maps an optional parameter to a positional
    // Option<T'> that cannot be omitted (design §4.1.2). Lowering passes an
    // empty optional run, so None is forwarded as ctx.undefined() instead of
    // being elided and the wrapper keeps the plain fixed argument list.
    const std::vector<ArkTSTypeDesc> types{WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc("Int32"))};
    auto code = ArkTSForeignWrapperSource(types, ScalarDesc("Unit"), 0, "__locals_", ArkTSReceiverKind::MODULE,
        types.size());
    EXPECT_EQ(code.find("isSome"), std::string::npos);
    EXPECT_NE(code.find("case Option.None => __locals_context.undefined().toJSValue()"), std::string::npos);
    EXPECT_NE(code.find("callMethod("), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ExpectSingleCall(file.get(), "positional option");
}

TEST_F(ArkTSTypeMappingTest, EmittedArrayCopyBranchesUseTheInteropArrayApi)
{
    auto code = ArkTSForeignWrapperSource({WrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, ScalarDesc("Int8"))},
        WrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, ScalarDesc("String")), 0, "__locals_");
    // To-JS: std.core Array<T> is a fixed-length struct without an append
    // member, so the buffer is built through the size-and-initializer
    // constructor (the interop library's own shape), reading each element
    // once with [], then packaged through the real ctx.array factory and
    // toJSValue of the library's context and heap APIs.
    EXPECT_NE(code.find("let __locals_elems_0 = Array<ark_interop.JSValue>(__argument_0.size, { __locals_index_0 =>"),
        std::string::npos);
    EXPECT_NE(code.find("let __locals_elem_0 = __argument_0[__locals_index_0]"), std::string::npos);
    EXPECT_NE(code.find("__locals_context.number(Float64(__locals_elem_0)).toJSValue()"), std::string::npos);
    EXPECT_NE(code.find("let __locals_arg_0 = __locals_context.array(__locals_elems_0).toJSValue()"),
        std::string::npos);
    // From-JS: null guard first, then the isArray kind guard (wrong-kind
    // values are type mismatches), the length from
    // asArray().size, and the copy through the same size-and-initializer
    // constructor; each element is read exactly once through getElement and
    // converted recursively inside the block lambda body.
    EXPECT_NE(code.find("ArkTS type mismatch: null for Array<String>"), std::string::npos);
    EXPECT_NE(code.find("if (!__locals_result.isArray())"), std::string::npos);
    EXPECT_NE(code.find("ArkTS type mismatch: not an array for Array<String>"), std::string::npos);
    EXPECT_NE(code.find("let __locals_length_0 = __locals_result.asArray().size"), std::string::npos);
    EXPECT_NE(code.find("let __locals_copy_0 = Array<String>(__locals_length_0, { __locals_index_1 =>"),
        std::string::npos);
    EXPECT_NE(code.find("let __locals_item_0 = __locals_result.getElement(__locals_index_1)"), std::string::npos);
    EXPECT_NE(code.find("if (!__locals_item_0.isString())"), std::string::npos);
    EXPECT_NE(code.find("ArkTS type mismatch: not a string for String"), std::string::npos);
    EXPECT_NE(code.find("__locals_item_0.asString().toString()"), std::string::npos);
    // Regression guard: the fixed-length std.core Array has no append member,
    // so no generated wrapper may ever call it.
    EXPECT_EQ(code.find(".append("), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ExpectSingleCall(file.get(), "array copy wrapper");
}

TEST_F(ArkTSTypeMappingTest, PresetArrayPositionsGenerateReferenceGlueAndArrayCopyStillGenerates)
{
    // The prepared generic preset array maps to a generatable reference leaf
    // with an explicit element description, so a signature position carrying
    // it lowers like any wrapper position: to-JS passes the stored JSObject
    // handle, from-JS guards isArray and constructs the wrapper with the
    // element spelled as an explicit type argument. An unprepared same-name
    // class (a stale CJO) stays unsupported and refuses generation. The
    // Array<T'> copy path is untouched.
    auto int32 = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT32);
    ClassDecl arkTsArrayDecl;
    arkTsArrayDecl.identifier = "ArkTSArray";
    arkTsArrayDecl.fullPackageName = "std.interop.arkts";
    arkTsArrayDecl.EnableAttr(Attribute::ARKTS_CLASS_WRAPPER);
    ClassTy refInt32("ArkTSArray", arkTsArrayDecl, {int32});
    auto reference = ArkTSClassifyType(&refInt32);
    ASSERT_EQ(reference.kind, ArkTSTypeDesc::Kind::REFERENCE);
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto code = ArkTSForeignWrapperSource({reference}, reference, 0, "__locals_", ArkTSReceiverKind::MODULE,
        ARKTS_DERIVE_OPTIONAL, &classRefs);
    ASSERT_FALSE(code.empty());
    EXPECT_EQ(classRefs.size(), 1);
    // To-JS: the stored reference crosses through its accessor and the heap
    // handle conversion, exactly like any wrapper reference.
    EXPECT_NE(code.find("(__argument_0).__cj_arkts_ref_value().toJSValue()"), std::string::npos);
    // From-JS: null and undefined guards, then the isArray kind guard, then
    // the wrapping constructor carrying the element type argument on the
    // asObject carrier.
    EXPECT_NE(code.find("if (!__locals_result.isArray())"), std::string::npos);
    EXPECT_NE(code.find("ArkTS type mismatch: not an array for ArkTSArray<Int32>"), std::string::npos);
    EXPECT_NE(code.find("asObject(), ())"), std::string::npos);
    EXPECT_NE(code.find("<Int32>("), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ExpectSingleCall(file.get(), "preset array wrapper");

    ClassDecl staleArrayDecl;
    staleArrayDecl.identifier = "ArkTSArray";
    staleArrayDecl.fullPackageName = "std.interop.arkts";
    ClassTy staleRefInt32("ArkTSArray", staleArrayDecl, {int32});
    auto stale = ArkTSClassifyType(&staleRefInt32);
    ASSERT_EQ(stale.kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
    EXPECT_TRUE(ArkTSForeignWrapperSource({stale}, stale, 0, "__locals_").empty());
    EXPECT_TRUE(ArkTSForeignWrapperSource({stale}, ScalarDesc("Int32"), 0, "__locals_").empty());
    EXPECT_TRUE(ArkTSForeignWrapperSource({}, stale, 0, "__locals_").empty());

    StructDecl arrayDecl;
    arrayDecl.identifier = "Array";
    arrayDecl.fullPackageName = "std.core";
    StructTy arrayInt32("Array", arrayDecl, {int32});
    auto copy = ArkTSClassifyType(&arrayInt32);
    ASSERT_EQ(copy.kind, ArkTSTypeDesc::Kind::ARRAY_COPY);
    auto copyCode = ArkTSForeignWrapperSource({copy}, copy, 0, "__locals_");
    EXPECT_FALSE(copyCode.empty());
    EXPECT_NE(copyCode.find("let __locals_elems_0 = Array<ark_interop.JSValue>(__argument_0.size, {"),
        std::string::npos);
    EXPECT_NE(copyCode.find("let __locals_arg_0 = __locals_context.array(__locals_elems_0).toJSValue()"),
        std::string::npos);
    EXPECT_NE(copyCode.find("let __locals_copy_0 = Array<Int32>("), std::string::npos);
    auto copyFile = Parse(copyCode);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ExpectSingleCall(copyFile.get(), "array copy wrapper after the flip");
}

TEST_F(ArkTSTypeMappingTest, EmittedTupleConversionRecursesPerElement)
{
    auto code = ArkTSForeignWrapperSource(
        {TupleDesc({ScalarDesc("Int8"), ScalarDesc("String")})},
        TupleDesc({ScalarDesc("UInt16"), WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc("Bool"))}), 0,
        "__locals_");
    // To-JS: each tuple accessor converts recursively, packaged through the
    // real ctx.array factory.
    EXPECT_NE(code.find("__argument_0[0]"), std::string::npos);
    EXPECT_NE(code.find("__argument_0[1]"), std::string::npos);
    EXPECT_NE(code.find("let __locals_part_0 = __locals_context.number(Float64(__argument_0[0])).toJSValue()"),
        std::string::npos);
    EXPECT_NE(code.find("let __locals_part_1 = __locals_context.string(__argument_0[1]).toJSValue()"),
        std::string::npos);
    EXPECT_NE(code.find("__locals_context.array([__locals_part_0, __locals_part_1]).toJSValue()"), std::string::npos);
    // From-JS: the isArray guard and the exact-arity check make wrong-kind
    // and wrong-length values type mismatches, then
    // indexed getElement reads convert recursively, including the nested
    // Option element with its null/undefined predicates and its kind guard
    // inside the Option.Some branch; each element is read exactly once, and the
    // Option element's part local carries the annotation Option.None needs.
    EXPECT_NE(code.find("ArkTS type mismatch: null for (UInt16, Option<Bool>)"), std::string::npos);
    EXPECT_NE(code.find("if (!__locals_result.isArray())"), std::string::npos);
    EXPECT_NE(code.find("ArkTS type mismatch: not an array for (UInt16, Option<Bool>)"), std::string::npos);
    EXPECT_NE(code.find("let __locals_length_0 = __locals_result.asArray().size"), std::string::npos);
    EXPECT_NE(code.find("if (__locals_length_0 != 2)"), std::string::npos);
    EXPECT_NE(code.find("ArkTS type mismatch: not a 2-element array for (UInt16, Option<Bool>)"), std::string::npos);
    EXPECT_NE(code.find("let __locals_item_0 = __locals_result.getElement(0)"), std::string::npos);
    EXPECT_NE(code.find("let __locals_item_1 = __locals_result.getElement(1)"), std::string::npos);
    EXPECT_NE(code.find("not a number for UInt16"), std::string::npos);
    EXPECT_NE(code.find("out of UInt16 range"), std::string::npos);
    EXPECT_NE(code.find("__locals_item_1.isNull() || __locals_item_1.isUndefined()"), std::string::npos);
    EXPECT_NE(code.find("if (!__locals_item_1.isBoolean())"), std::string::npos);
    EXPECT_NE(code.find("Option.Some(__locals_item_1.toBoolean())"), std::string::npos);
    EXPECT_NE(code.find("let __locals_part_3: Option<Bool> = (if (__locals_item_1.isNull() || "
                        "__locals_item_1.isUndefined())"),
        std::string::npos);
    EXPECT_NE(code.find("return (__locals_part_2, __locals_part_3)"), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ExpectSingleCall(file.get(), "tuple wrapper");
}

TEST_F(ArkTSTypeMappingTest, EmittedNarrowIntegerChecksCoverRangeAndIntegrality)
{
    const std::vector<std::tuple<std::string, std::string, std::string>> narrow{{"Int8", "-128.0", "127.0"},
        {"Int16", "-32768.0", "32767.0"}, {"Int32", "-2147483648.0", "2147483647.0"}, {"UInt8", "0.0", "255.0"},
        {"UInt16", "0.0", "65535.0"}, {"UInt32", "0.0", "4294967295.0"}};
    for (const auto& [type, lower, upper] : narrow) {
        auto code = ArkTSForeignWrapperSource({}, ScalarDesc(type), 0, "__locals_");
        // Kind check first (wrong-kind values are type mismatches too),
        // then finite, range and integrality value checks.
        EXPECT_NE(code.find("if (!__locals_result.isNumber())"), std::string::npos) << type;
        EXPECT_NE(code.find("ArkTS type mismatch: not a number for " + type), std::string::npos) << type;
        EXPECT_NE(code.find("ArkTS type mismatch: null for " + type), std::string::npos) << type;
        EXPECT_NE(code.find("not a finite number for " + type), std::string::npos) << type;
        EXPECT_NE(code.find("< " + lower + " ||"), std::string::npos) << type;
        EXPECT_NE(code.find("> " + upper + ")"), std::string::npos) << type;
        EXPECT_NE(code.find("not an integer for " + type), std::string::npos) << type;
        EXPECT_NE(code.find("Float64(Int64("), std::string::npos) << type;
        auto file = Parse(code);
        ASSERT_EQ(diag.GetErrorCount(), 0) << type;
        ExpectSingleCall(file.get(), type);
    }
}

TEST_F(ArkTSTypeMappingTest, EmittedFloat16ChecksRangeForFiniteValuesOnly)
{
    // Behind the isNumber guard only a finite value beyond the exact
    // Float64 image of the largest finite Float16, 65504.0, is rejected.
    // NaN, +/-Infinity and negative zero are representable Float16 values;
    // the finite test exempts them while a finite overflow still throws.
    // No integrality check: fractional values inside the bounds convert
    // at the target precision.
    auto code = ArkTSForeignWrapperSource({}, ScalarDesc("Float16"), 0, "__locals_");
    EXPECT_NE(code.find("if (!__locals_result.isNumber())"), std::string::npos);
    EXPECT_NE(code.find("ArkTS type mismatch: not a number for Float16"), std::string::npos);
    EXPECT_NE(code.find("let __locals_num_0 = __locals_result.toNumber()"), std::string::npos);
    EXPECT_EQ(code.find("not a finite number for Float16"), std::string::npos);
    EXPECT_NE(code.find("if ((__locals_num_0 - __locals_num_0) == 0.0 && "
                        "(__locals_num_0 < -65504.0 || __locals_num_0 > 65504.0))"), std::string::npos);
    EXPECT_NE(code.find("out of Float16 range"), std::string::npos);
    EXPECT_NE(code.find("return Float16(__locals_num_0)"), std::string::npos);
    EXPECT_EQ(code.find("not an integer for Float16"), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSTypeMappingTest, EmittedFloat32ChecksRangeForFiniteValuesWhileFloat64StaysUnchecked)
{
    auto code = ArkTSForeignWrapperSource({}, ScalarDesc("Float32"), 0, "__locals_");
    // 3.4028234663852886e38 is the shortest decimal that round-trips to
    // the Float64 image of the binary32 maximum (2 - 2^-24) * 2^127, so
    // the generated comparison cannot itself lose precision. The finite
    // test exempts NaN and infinities; only finite overflow throws.
    EXPECT_EQ(code.find("not a finite number for Float32"), std::string::npos);
    EXPECT_NE(code.find("if ((__locals_num_0 - __locals_num_0) == 0.0 && "
                        "(__locals_num_0 < -3.4028234663852886e38 || "
                        "__locals_num_0 > 3.4028234663852886e38))"), std::string::npos);
    EXPECT_NE(code.find("out of Float32 range"), std::string::npos);
    EXPECT_NE(code.find("return Float32(__locals_num_0)"), std::string::npos);
    EXPECT_EQ(code.find("not an integer for Float32"), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    // Float64 is the number row's full-range carrier: NaN and infinities
    // are legal number values, so its wrapper keeps the bare accessor and
    // none of the finite/range machinery - the intentional difference.
    auto wide = ArkTSForeignWrapperSource({}, ScalarDesc("Float64"), 0, "__locals_");
    EXPECT_NE(wide.find("return __locals_result.toNumber()"), std::string::npos);
    EXPECT_EQ(wide.find("not a finite number for Float64"), std::string::npos);
    EXPECT_EQ(wide.find("out of Float64 range"), std::string::npos);
    EXPECT_EQ(wide.find("ArkTS value invalid: "), std::string::npos);
    EXPECT_EQ(wide.find("__locals_num_"), std::string::npos);
    auto wideFile = Parse(wide);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSTypeMappingTest, EmittedRuneCheckExcludesSurrogateCodePoints)
{
    auto code = ArkTSForeignWrapperSource({}, ScalarDesc("Rune"), 0, "__locals_");
    // Unicode scalar range 0..0x10FFFF and the surrogate block
    // U+D800..U+DFFF (55296..57343) are both rejected, behind the isNumber
    // kind guard.
    EXPECT_NE(code.find("ArkTS type mismatch: not a number for Rune"), std::string::npos);
    EXPECT_NE(code.find("not a Unicode scalar value for Rune"), std::string::npos);
    EXPECT_NE(code.find("< 0.0 ||"), std::string::npos);
    EXPECT_NE(code.find("> 1114111.0)"), std::string::npos);
    EXPECT_NE(code.find(">= 55296.0 &&"), std::string::npos);
    EXPECT_NE(code.find("<= 57343.0)"), std::string::npos);
    EXPECT_NE(code.find("surrogate code point for Rune"), std::string::npos);
    EXPECT_NE(code.find("return Rune(UInt32(__locals_num_0))"), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSTypeMappingTest, EmittedBigIntBoundsGuardSignedAndUnsigned)
{
    auto signedCode = ArkTSForeignWrapperSource({}, ScalarDesc("Int64"), 0, "__locals_");
    EXPECT_NE(signedCode.find("if (!__locals_result.isBigInt())"), std::string::npos);
    EXPECT_NE(signedCode.find("ArkTS type mismatch: not a bigint for Int64"), std::string::npos);
    EXPECT_NE(signedCode.find("let __locals_big_0 = __locals_result.asBigInt().toBigInt()"), std::string::npos);
    EXPECT_NE(signedCode.find("BigInt(Int64(-9223372036854775808))"), std::string::npos);
    EXPECT_NE(signedCode.find("below Int64 range"), std::string::npos);
    EXPECT_NE(signedCode.find("BigInt(Int64(9223372036854775807))"), std::string::npos);
    EXPECT_NE(signedCode.find("above Int64 range"), std::string::npos);
    EXPECT_NE(signedCode.find("return __locals_big_0.toInt64()"), std::string::npos);
    auto unsignedCode = ArkTSForeignWrapperSource({}, ScalarDesc("UInt64"), 0, "__locals_");
    EXPECT_NE(unsignedCode.find("ArkTS type mismatch: not a bigint for UInt64"), std::string::npos);
    EXPECT_NE(unsignedCode.find("BigInt(Int64(0))"), std::string::npos);
    EXPECT_NE(unsignedCode.find("negative BigInt for UInt64"), std::string::npos);
    EXPECT_NE(unsignedCode.find("BigInt(UInt64(18446744073709551615u64))"), std::string::npos);
    // Regression guard: a 2^64-1 literal without a type suffix would be
    // rejected as out of Int64 range (sema_exceed_num_value_range), so the
    // generated source must carry the explicit u64 suffix.
    EXPECT_EQ(unsignedCode.find("BigInt(UInt64(18446744073709551615))"), std::string::npos);
    EXPECT_NE(unsignedCode.find("above UInt64 range"), std::string::npos);
    EXPECT_NE(unsignedCode.find("return __locals_big_0.toUInt64()"), std::string::npos);
    auto bigIntCode = ArkTSForeignWrapperSource({}, ScalarDesc("BigInt"), 0, "__locals_");
    EXPECT_NE(bigIntCode.find("ArkTS type mismatch: not a bigint for BigInt"), std::string::npos);
    EXPECT_NE(bigIntCode.find("return __locals_result.asBigInt().toBigInt()"), std::string::npos);
    EXPECT_EQ(bigIntCode.find("range"), std::string::npos);
    auto signedFile = Parse(signedCode);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    auto unsignedFile = Parse(unsignedCode);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    auto bigIntFile = Parse(bigIntCode);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSTypeMappingTest, EmittedFailuresDistinguishMismatchFromInvalidValue)
{
    auto code = ArkTSForeignWrapperSource({}, ScalarDesc("Int32"), 0, "__locals_");
    EXPECT_NE(code.find("ArkTS type mismatch: null for Int32"), std::string::npos);
    EXPECT_NE(code.find("ArkTS value invalid: "), std::string::npos);
    // Ordering: the kind check precedes every value check.
    EXPECT_LT(code.find("ArkTS type mismatch: "), code.find("ArkTS value invalid: "));
    // Conversion failures throw the facade stand-in by name, in the single
    // throw shape, and never the core Exception type.
    EXPECT_NE(code.find("throw ExternConversionException(\"ArkTS type mismatch: null for Int32\")"),
        std::string::npos);
    EXPECT_NE(code.find("throw ExternConversionException(\"ArkTS value invalid: "), std::string::npos);
    EXPECT_EQ(code.find("throw Exception("), std::string::npos);
}

TEST_F(ArkTSTypeMappingTest, EmittedWrongKindGuardsThrowConversionExceptions)
{
    // A wrong-kind value is a type mismatch, so every from-JS accessor is
    // preceded by its kind predicate of the real ohos.ark_interop face,
    // and the failure goes through the single conversion-exception throw;
    // the accessor's own BusinessException(34300005) is only a redundant
    // safety net that a guarded wrapper never reaches.
    const std::vector<std::tuple<std::string, std::string, std::string>> kinds{{"Int32", "isNumber",
        "not a number for Int32"},
        {"Float16", "isNumber", "not a number for Float16"}, {"Float64", "isNumber", "not a number for Float64"},
        {"Rune", "isNumber", "not a number for Rune"}, {"String", "isString", "not a string for String"},
        {"Bool", "isBoolean", "not a boolean for Bool"}, {"Int64", "isBigInt", "not a bigint for Int64"},
        {"UInt64", "isBigInt", "not a bigint for UInt64"}, {"BigInt", "isBigInt", "not a bigint for BigInt"}};
    for (const auto& [type, predicate, message] : kinds) {
        auto code = ArkTSForeignWrapperSource({}, ScalarDesc(type), 0, "__locals_");
        EXPECT_NE(code.find("if (!__locals_result." + predicate + "())"), std::string::npos) << type;
        EXPECT_NE(code.find("throw ExternConversionException(\"ArkTS type mismatch: " + message +
                        "\" + \", got \" + "),
            std::string::npos) << type;
        // The kind guard precedes every value check.
        EXPECT_LT(code.find(message), code.find("ArkTS value invalid: ")) << type;
        auto file = Parse(code);
        ASSERT_EQ(diag.GetErrorCount(), 0) << type;
    }
    // Wrong kind inside an Option.Some branch: the kind guard is never skipped
    // where the null guard is - the value is known non-null but may still have
    // the wrong kind.
    auto optionCode = ArkTSForeignWrapperSource(
        {}, WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc("Int32")), 0, "__locals_");
    EXPECT_NE(optionCode.find("if (!__locals_result.isNumber())"), std::string::npos);
    EXPECT_NE(optionCode.find("Option.Some(Int32(__locals_num_0))"), std::string::npos);
    auto optionFile = Parse(optionCode);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSTypeMappingTest, EmittedUndefinedChecksUsePredicatesNeverProbes)
{
    // Every undefined decision goes through the isNull()/isUndefined()
    // predicates; no wrapper probes with a consuming asUndefined(), for
    // Option and Unit alike. Exception control flow is limited to the call
    // translation of the foreign target.
    auto optionCode = ArkTSForeignWrapperSource({}, WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc("Bool")),
        0, "__locals_");
    EXPECT_NE(optionCode.find("__locals_result.isNull() || __locals_result.isUndefined()"), std::string::npos);
    EXPECT_NE(optionCode.find("Option.Some(__locals_result.toBoolean())"), std::string::npos);
    auto unitCode = ArkTSForeignWrapperSource({}, ScalarDesc("Unit"), 0, "__locals_");
    EXPECT_NE(unitCode.find("__locals_result.isNull() || __locals_result.isUndefined()"), std::string::npos);
    EXPECT_NE(unitCode.find("not null or undefined for Unit"), std::string::npos);
    for (const auto& code : {optionCode, unitCode}) {
        EXPECT_EQ(code.find("asUndefined"), std::string::npos);
        EXPECT_NE(code.find("catch (__locals_error_0: BusinessException)"), std::string::npos);
        auto file = Parse(code);
        ASSERT_EQ(diag.GetErrorCount(), 0);
        ExpectSingleCall(file.get(), "undefined predicate wrapper");
    }
}

TEST_F(ArkTSTypeMappingTest, SeamPlaceholderNamesNeverReturn)
{
    // Negative guard: generated wrappers reference the real ohos.ark_interop
    // API face only; the five compiler-owned seam placeholder names must
    // never appear in any generated wrapper, whatever the shape.
    const std::vector<std::string> seams{"__cj_arkts_js_array", "__cj_arkts_js_length", "__cj_arkts_js_element",
        "__cj_arkts_ref_to_js", "__cj_arkts_ref_from_js"};
    const std::vector<ArkTSTypeDesc> shapes{ScalarDesc("Bool"),
        WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc("String")),
        WrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, ScalarDesc("Rune")),
        WrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc("Int64"))),
        TupleDesc({ScalarDesc("Int8"), ScalarDesc("UInt8")}),
        TupleDesc({ScalarDesc("Int16"), WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc("Bool")),
            WrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, ScalarDesc("String"))}),
        WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL,
            TupleDesc({ScalarDesc("Float32"), WrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, ScalarDesc("Bool"))})),
        WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL,
            WrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, ScalarDesc("Int8")))};
    for (const auto& shape : shapes) {
        auto code = ArkTSForeignWrapperSource({shape}, shape, 1, "__locals_");
        ASSERT_FALSE(code.empty());
        for (const auto& seam : seams) {
            EXPECT_EQ(code.find(seam), std::string::npos) << seam << " in shape " << code;
        }
        // Regression guard: std.core Array has no append member, so no
        // generated wrapper may call it in any shape.
        EXPECT_EQ(code.find(".append("), std::string::npos) << "append in shape " << code;
    }
    // Scalar shapes stay seam-free through the same description-driven
    // generator (the scalar-name overload is gone; the scalar contract rides
    // SCALAR descriptions).
    auto scalarCode = ArkTSForeignWrapperSource({ScalarDesc("UInt64")}, ScalarDesc("Unit"), 2, "__locals_");
    ASSERT_FALSE(scalarCode.empty());
    for (const auto& seam : seams) {
        EXPECT_EQ(scalarCode.find(seam), std::string::npos) << seam;
    }
}

TEST_F(ArkTSTypeMappingTest, EverySignatureResolvesThroughRequireArkModule)
{
    // The module name is the explicit module option or the Cangjie package
    // name of the declaration, and the MODULE receiver - the generator's
    // default - binds it through requireArkModule with the module placeholder,
    // namespace segments resolving through getProperty. The arkts.lang
    // TS-preset exception (the only GLOBAL receiver, selected when the package
    // is arkts.lang and no module option is written) is pinned by the receiver
    // tests of ArkTSAnnotationTest.
    auto code = ArkTSForeignWrapperSource({ScalarDesc("String")}, ScalarDesc("Float64"), 0, "__locals_");
    EXPECT_NE(code.find("let __locals_module = __locals_context.requireArkModule(\"" + Placeholder("__module__") +
                    "\")"),
        std::string::npos);
    EXPECT_NE(code.find("let __locals_result = try {"), std::string::npos);
    EXPECT_NE(code.find("__locals_module.callMethod("), std::string::npos);
    EXPECT_NE(code.find(Placeholder("__module__")), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ExpectSingleCall(file.get(), "module wrapper");
    auto namespaced = ArkTSForeignWrapperSource({}, ScalarDesc("Unit"), 1, "__locals_");
    EXPECT_NE(namespaced.find("let __locals_module = __locals_context.requireArkModule(\"" + Placeholder("__module__") +
                    "\")"),
        std::string::npos);
    EXPECT_NE(namespaced.find("let __locals_nsvalue_0 = __locals_module.getProperty(\"" +
                    Placeholder("__namespace_0__") + "\")"),
        std::string::npos);
    auto namespacedFile = Parse(namespaced);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ExpectSingleCall(namespacedFile.get(), "namespaced module wrapper");
}

TEST_F(ArkTSTypeMappingTest, EmittedNothingResultThrowsContractViolation)
{
    auto code = ArkTSForeignWrapperSource({}, ScalarDesc("Nothing"), 0, "__locals_");
    EXPECT_NE(code.find("ArkTS never function returned"), std::string::npos);
    EXPECT_EQ(code.find("\n    return"), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSTypeMappingTest, ScalarDescriptionsKeepTheAr02Contract)
{
    // The generator has no scalar-name overload; the scalar contract is
    // carried by leaf SCALAR descriptions through the same generator, and the
    // refusal of an unknown name is the generatability refusal of a
    // hand-built non-whitelisted scalar.
    const std::vector<std::string> types{"Int8", "Int16", "Int32", "UInt8", "UInt16", "UInt32", "Float16", "Float32",
        "Float64", "Rune", "Int64", "UInt64", "BigInt", "Bool", "String", "Unit"};
    for (const auto& type : types) {
        auto file = Parse(ArkTSForeignWrapperSource({ScalarDesc(type)}, ScalarDesc(type), 2, "__locals_"));
        ASSERT_EQ(diag.GetErrorCount(), 0) << type;
        ExpectSingleCall(file.get(), type);
    }
    auto nothing = Parse(ArkTSForeignWrapperSource({}, ScalarDesc("Nothing"), 0, "__locals_"));
    ASSERT_EQ(diag.GetErrorCount(), 0);
    // The bigint family never passes through JS number or string parsing.
    for (const auto& type : {"Int64", "UInt64", "BigInt"}) {
        auto code = ArkTSForeignWrapperSource({ScalarDesc(type)}, ScalarDesc(type), 0, "__locals_");
        EXPECT_NE(code.find(".bigint("), std::string::npos) << type;
        EXPECT_NE(code.find(".asBigInt().toBigInt()"), std::string::npos) << type;
        EXPECT_EQ(code.find(".number("), std::string::npos) << type;
        EXPECT_EQ(code.find(".toNumber()"), std::string::npos) << type;
        EXPECT_EQ(code.find(".parse("), std::string::npos) << type;
    }
    auto unsignedCode = ArkTSForeignWrapperSource({ScalarDesc("UInt64")}, ScalarDesc("UInt64"), 0, "__locals_");
    EXPECT_NE(unsignedCode.find("BigInt(__argument_0)"), std::string::npos);
    EXPECT_NE(unsignedCode.find(".toUInt64()"), std::string::npos);
    auto signedCode = ArkTSForeignWrapperSource({}, ScalarDesc("Int64"), 0, "__locals_");
    EXPECT_NE(signedCode.find(".toInt64()"), std::string::npos);
    auto bound = ArkTSForeignWrapperSource({}, ScalarDesc("String"), 0, "__locals_");
    EXPECT_NE(bound.find(Placeholder("__name__")), std::string::npos);
    EXPECT_NE(bound.find(": ark_interop.JSContext ="), std::string::npos);
}

TEST_F(ArkTSTypeMappingTest, UnsupportedDescriptionsRefuseGeneration)
{
    const std::vector<ArkTSTypeDesc> unsupported{{}, {ArkTSTypeDesc::Kind::SCALAR, "IntNative", {}},
        {ArkTSTypeDesc::Kind::SCALAR, "", {}},
        WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc("Int32"))),
        TupleDesc({ScalarDesc("Int8")})};
    for (const auto& bad : unsupported) {
        EXPECT_TRUE(ArkTSForeignWrapperSource({bad}, ScalarDesc("Int32"), 0, "__locals_").empty());
        EXPECT_TRUE(ArkTSForeignWrapperSource({}, bad, 0, "__locals_").empty());
    }
    // Nothing has no values and can never cross as an argument.
    EXPECT_TRUE(ArkTSForeignWrapperSource({ScalarDesc("Nothing")}, ScalarDesc("Int32"), 0, "__locals_").empty());
    EXPECT_TRUE(
        ArkTSForeignWrapperSource({WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc("Nothing"))},
            ScalarDesc("Int32"), 0, "__locals_")
            .empty());
    // Classification results flow straight into the refusal contract.
    StructDecl mapDecl;
    mapDecl.identifier = "Map";
    mapDecl.fullPackageName = "std.collection";
    StructTy map("Map", mapDecl, {});
    auto classified = ArkTSClassifyType(&map);
    EXPECT_EQ(classified.kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
    EXPECT_TRUE(ArkTSForeignWrapperSource({classified}, classified, 0, "__locals_").empty());
    // A same-name ArkTSArray outside std.interop.arkts classifies as
    // unsupported and therefore refuses generation, exactly like Map above.
    auto int32 = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT32);
    ClassDecl userArrayDecl;
    userArrayDecl.identifier = "ArkTSArray";
    userArrayDecl.fullPackageName = "application";
    ClassTy userArray("ArkTSArray", userArrayDecl, {int32});
    auto userRef = ArkTSClassifyType(&userArray);
    EXPECT_EQ(userRef.kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
    EXPECT_TRUE(ArkTSForeignWrapperSource({userRef}, userRef, 0, "__locals_").empty());
    // Unknown scalar names never produce a wrapper either: a hand-built
    // SCALAR description with a non-whitelisted name fails the generatable
    // guard.
    EXPECT_TRUE(ArkTSForeignWrapperSource({ScalarDesc("Junk")}, ScalarDesc("Int32"), 0, "__locals_").empty());
    EXPECT_TRUE(ArkTSForeignWrapperSource({}, ScalarDesc("Junk"), 0, "__locals_").empty());
}

TEST_F(ArkTSTypeMappingTest, GeneratedWrappersRemainParseableAcrossKinds)
{
    const std::vector<ArkTSTypeDesc> shapes{ScalarDesc("Bool"),
        WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc("String")),
        WrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, ScalarDesc("Rune")),
        WrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc("Int64"))),
        TupleDesc({ScalarDesc("Int8"), ScalarDesc("UInt8")}),
        TupleDesc({ScalarDesc("Int16"), WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc("Bool")),
            WrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, ScalarDesc("String"))}),
        // Round-1 P2-3: the Option-wrapped array shapes put the array
        // conversion (now a block-lambda constructor) inside a match arm or
        // a Some branch, a nesting the plain shapes do not cover.
        WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL,
            TupleDesc({ScalarDesc("Float32"), WrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, ScalarDesc("Bool"))})),
        WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL,
            WrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, ScalarDesc("Int8")))};
    for (const auto& shape : shapes) {
        auto file = Parse(ArkTSForeignWrapperSource({shape}, shape, 1, "__locals_"));
        ASSERT_EQ(diag.GetErrorCount(), 0);
        ExpectSingleCall(file.get(), "shape");
    }
}

TEST_F(ArkTSTypeMappingTest, RestParamKeepsArrayCopyAndRefusesArkTSArray)
{
    // The rest position accepts Array<T'> only. An ArkTSArray<T'> rest
    // position is rejected by the Sema rest diagnostic before the generator
    // runs, so the generator itself has no branch for it and returns an empty
    // string - this assertion is the generator-side half of that contract. The
    // Array<T'> rest spread keeps generating.
    auto restCopy = ArkTSForeignWrapperSource({WrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, ScalarDesc("String"))},
        ScalarDesc("Unit"), 0, "__locals_", 0);
    EXPECT_FALSE(restCopy.empty());
    EXPECT_NE(restCopy.find("let __locals_rest_0 = Array<ark_interop.JSValue>(__argument_0.size, {"), std::string::npos);
    EXPECT_NE(restCopy.find("callMethod("), std::string::npos);
    auto restCopyFile = Parse(restCopy);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ExpectSingleCall(restCopyFile.get(), "array copy rest wrapper");
    auto int32 = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT32);
    ClassDecl arkTsArrayDecl;
    arkTsArrayDecl.identifier = "ArkTSArray";
    arkTsArrayDecl.fullPackageName = "std.interop.arkts";
    ClassTy refInt32("ArkTSArray", arkTsArrayDecl, {int32});
    auto reference = ArkTSClassifyType(&refInt32);
    ASSERT_EQ(reference.kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
    EXPECT_TRUE(ArkTSForeignWrapperSource({reference}, ScalarDesc("Unit"), 0, "__locals_", 0).empty());
}

// Generated code names the interop surface through the packages the compiler
// injects, never through short names a user declaration could capture:
// ark_interop.JSContext / ark_interop.JSValue and
// cj_arkts_context_v1.__cj_arkts_current. std.core Array<T> targets of the
// copy conversion stay short, because std.core is not an interop package.
TEST_F(ArkTSTypeMappingTest, WrapperSourceUsesQualifiedInteropNames)
{
    auto code = ArkTSForeignWrapperSource({WrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, ScalarDesc("Int32"))},
        WrappedDesc(ArkTSTypeDesc::Kind::ARRAY_COPY, ScalarDesc("Int32")), 0, "__locals_");
    ASSERT_FALSE(code.empty());
    EXPECT_NE(code.find(": ark_interop.JSContext = cj_arkts_context_v1.__cj_arkts_current()"), std::string::npos);
    EXPECT_NE(code.find("Array<ark_interop.JSValue>("), std::string::npos);
    EXPECT_NE(code.find("Array<Int32>("), std::string::npos);
    EXPECT_EQ(code.find(": JSContext"), std::string::npos);
    EXPECT_EQ(code.find("Array<JSValue>"), std::string::npos);
    // The syntax parses without any import resolution happening here.
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ExpectSingleCall(file.get(), "qualified interop names");
}

TEST_F(ArkTSTypeMappingTest, ClassifyForeignClassAsObjectReference)
{
    // A prepared foreign class is a reference position; the same declaration
    // without the wrapper attribute stays unsupported (generic presets keep
    // their external form).
    ClassDecl point;
    point.identifier = "Point";
    point.fullPackageName = "pkg";
    ClassTy pointTy("Point", point, {});
    point.EnableAttr(Attribute::ARKTS_CLASS_WRAPPER);
    auto desc = ArkTSClassifyType(&pointTy);
    ASSERT_EQ(desc.kind, ArkTSTypeDesc::Kind::REFERENCE);
    EXPECT_EQ(desc.reference, Ptr<Decl>(&point));
    EXPECT_TRUE(ArkTSTypeDescGeneratable(desc));
    point.DisableAttr(Attribute::ARKTS_CLASS_WRAPPER);
    EXPECT_EQ(ArkTSClassifyType(&pointTy).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
}

TEST_F(ArkTSTypeMappingTest, ReferenceElementsAreSupportedInContainers)
{
    // A prepared class is one reference leaf: usable directly, inside
    // Option, inside Array and as a tuple element.
    ClassDecl point;
    InitWrappedPoint(point);
    ClassTy pointTy("Point", point, {});
    EnumDecl optionDecl;
    optionDecl.identifier = "Option";
    optionDecl.fullPackageName = "std.core";
    EnumTy optionalPoint("Option", optionDecl, {&pointTy});
    auto optional = ArkTSClassifyType(&optionalPoint);
    ASSERT_EQ(optional.kind, ArkTSTypeDesc::Kind::OPTIONAL);
    ASSERT_EQ(optional.elements.size(), 1);
    EXPECT_EQ(optional.elements.front().kind, ArkTSTypeDesc::Kind::REFERENCE);
    EXPECT_TRUE(ArkTSTypeDescGeneratable(optional));
    // Array<T'> copies the container; an object element keeps its reference.
    StructDecl arrayDecl;
    arrayDecl.identifier = "Array";
    arrayDecl.fullPackageName = "std.core";
    StructTy pointArray("Array", arrayDecl, {&pointTy});
    auto array = ArkTSClassifyType(&pointArray);
    ASSERT_EQ(array.kind, ArkTSTypeDesc::Kind::ARRAY_COPY);
    ASSERT_EQ(array.elements.size(), 1);
    EXPECT_EQ(array.elements.front().kind, ArkTSTypeDesc::Kind::REFERENCE);
    EXPECT_TRUE(ArkTSTypeDescGeneratable(array));
    // A nested Option stays refused, including when its leaf is a reference.
    EnumTy nestedOptional("Option", optionDecl, {&optionalPoint});
    EXPECT_EQ(ArkTSClassifyType(&nestedOptional).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
    StructTy arrayOfNestedOptional("Array", arrayDecl, {&nestedOptional});
    EXPECT_EQ(ArkTSClassifyType(&arrayOfNestedOptional).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
    // Containers nest recursively under the same descriptions.
    StructTy nestedPointArray("Array", arrayDecl, {&pointArray});
    auto nested = ArkTSClassifyType(&nestedPointArray);
    ASSERT_EQ(nested.kind, ArkTSTypeDesc::Kind::ARRAY_COPY);
    ASSERT_EQ(nested.elements.size(), 1);
    EXPECT_EQ(nested.elements.front().kind, ArkTSTypeDesc::Kind::ARRAY_COPY);
    EXPECT_TRUE(ArkTSTypeDescGeneratable(nested));
    StructTy optionalPointArray("Array", arrayDecl, {&optionalPoint});
    auto maybeArray = ArkTSClassifyType(&optionalPointArray);
    ASSERT_EQ(maybeArray.kind, ArkTSTypeDesc::Kind::ARRAY_COPY);
    ASSERT_EQ(maybeArray.elements.size(), 1);
    EXPECT_EQ(maybeArray.elements.front().kind, ArkTSTypeDesc::Kind::OPTIONAL);
    ASSERT_EQ(maybeArray.elements.front().elements.size(), 1);
    EXPECT_EQ(maybeArray.elements.front().elements.front().kind, ArkTSTypeDesc::Kind::REFERENCE);
    EXPECT_TRUE(ArkTSTypeDescGeneratable(maybeArray));
    // Option wraps an array whose element is a reference.
    EnumTy optionalArrayTy("Option", optionDecl, {&pointArray});
    auto optionalArray = ArkTSClassifyType(&optionalArrayTy);
    ASSERT_EQ(optionalArray.kind, ArkTSTypeDesc::Kind::OPTIONAL);
    ASSERT_EQ(optionalArray.elements.size(), 1);
    EXPECT_EQ(optionalArray.elements.front().kind, ArkTSTypeDesc::Kind::ARRAY_COPY);
    ASSERT_EQ(optionalArray.elements.front().elements.size(), 1);
    EXPECT_EQ(optionalArray.elements.front().elements.front().kind, ArkTSTypeDesc::Kind::REFERENCE);
    EXPECT_TRUE(ArkTSTypeDescGeneratable(optionalArray));
    // Tuple elements are converted the same way.
    auto float64 = TypeManager::GetPrimitiveTy(TypeKind::TYPE_FLOAT64);
    TupleTy pair({&pointTy, float64});
    auto tuple = ArkTSClassifyType(&pair);
    ASSERT_EQ(tuple.kind, ArkTSTypeDesc::Kind::TUPLE);
    ASSERT_EQ(tuple.elements.size(), 2);
    EXPECT_EQ(tuple.elements.front().kind, ArkTSTypeDesc::Kind::REFERENCE);
    EXPECT_EQ(tuple.elements.back().kind, ArkTSTypeDesc::Kind::SCALAR);
    EXPECT_TRUE(ArkTSTypeDescGeneratable(tuple));
}

TEST_F(ArkTSTypeMappingTest, ContainerElementsConvertObjectReferences)
{
    // Array and tuple elements leave through the stored reference and come
    // back through the object guard and wrapping constructor.
    ClassDecl point;
    InitWrappedPoint(point);
    ArkTSTypeDesc reference{ArkTSTypeDesc::Kind::REFERENCE, "", {}, Ptr<Decl>(&point)};
    ArkTSTypeDesc maybeReference{ArkTSTypeDesc::Kind::OPTIONAL, "", {reference}};
    ArkTSTypeDesc array{ArkTSTypeDesc::Kind::ARRAY_COPY, "", {reference}};
    ArkTSTypeDesc tuple{ArkTSTypeDesc::Kind::TUPLE, "", {maybeReference, ScalarDesc("Float64")}};
    // To-JS through a tuple parameter; from-JS through an array result.
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto toCode = ArkTSForeignWrapperSource({tuple}, array, 0, "__locals_", ARKTS_NO_REST,
        ArkTSReceiverKind::MODULE, ARKTS_DERIVE_OPTIONAL, &classRefs);
    ASSERT_FALSE(toCode.empty());
    EXPECT_NE(toCode.find("__cj_arkts_ref_value().toJSValue()"), std::string::npos);
    EXPECT_NE(toCode.find("let __locals_copy_0 = Array<__locals_type_0>(__locals_length_0"),
        std::string::npos);
    EXPECT_EQ(toCode.find("Array<Point>("), std::string::npos);
    EXPECT_NE(toCode.find("if (!__locals_item_"), std::string::npos);
    EXPECT_NE(toCode.find("isObject()"), std::string::npos);
    EXPECT_NE(toCode.find(".asObject(), ())"), std::string::npos);
    ASSERT_EQ(classRefs.size(), 2);
    for (auto& entry : classRefs) {
        EXPECT_EQ(entry.second, Ptr<Decl>(&point));
    }
    auto toFile = Parse(toCode);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    // From-JS through a tuple result: the nullable reference part carries a
    // placeholder type instead of a spliced class name, and the arity check
    // stays in place.
    classRefs.clear();
    auto fromCode = ArkTSForeignWrapperSource({array}, tuple, 0, "__locals_", ARKTS_NO_REST,
        ArkTSReceiverKind::MODULE, ARKTS_DERIVE_OPTIONAL, &classRefs);
    ASSERT_FALSE(fromCode.empty());
    EXPECT_NE(fromCode.find("Option<__locals_type_"), std::string::npos);
    EXPECT_NE(fromCode.find("not a 2-element array"), std::string::npos);
    EXPECT_NE(fromCode.find("isUndefined()"), std::string::npos);
    EXPECT_NE(fromCode.find("Option<__locals_type_0>"), std::string::npos);
    EXPECT_NE(fromCode.find("__locals_class_0("), std::string::npos);
    ASSERT_EQ(classRefs.size(), 2);
    for (auto& entry : classRefs) {
        EXPECT_EQ(entry.second, Ptr<Decl>(&point));
    }
    auto fromFile = Parse(fromCode);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    // Recursive containers: Option<Array<T'>> leaves through the per-element
    // conversion, Array<Array<T'>> binds one type placeholder per level.
    ArkTSTypeDesc nestedArray{ArkTSTypeDesc::Kind::ARRAY_COPY, "", {array}};
    ArkTSTypeDesc optionalArray{ArkTSTypeDesc::Kind::OPTIONAL, "", {array}};
    classRefs.clear();
    auto nestedCode = ArkTSForeignWrapperSource({optionalArray}, nestedArray, 0, "__locals_", ARKTS_NO_REST,
        ArkTSReceiverKind::MODULE, ARKTS_DERIVE_OPTIONAL, &classRefs);
    ASSERT_FALSE(nestedCode.empty());
    EXPECT_NE(nestedCode.find("Array<ark_interop.JSValue>("), std::string::npos);
    EXPECT_NE(nestedCode.find("Array<Array<__locals_type_"), std::string::npos);
    EXPECT_EQ(nestedCode.find("Array<Point>("), std::string::npos);
    EXPECT_NE(nestedCode.find(".asObject(), ())"), std::string::npos);
    ASSERT_EQ(classRefs.size(), 3);
    for (auto& entry : classRefs) {
        EXPECT_EQ(entry.second, Ptr<Decl>(&point));
    }
    auto nestedFile = Parse(nestedCode);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSTypeMappingTest, ReferencePositionsRequireTheClassRefsMap)
{
    // A reference position is emitted by placeholder and can only be bound
    // through the classRefs map. Without the map the wrapper fails closed
    // instead of emitting malformed syntax (empty expression or type argument);
    // every container that reaches a reference leaf behaves the same in both
    // parameter and result position.
    ClassDecl point;
    InitWrappedPoint(point);
    ArkTSTypeDesc reference{ArkTSTypeDesc::Kind::REFERENCE, "", {}, Ptr<Decl>(&point)};
    ArkTSTypeDesc array{ArkTSTypeDesc::Kind::ARRAY_COPY, "", {reference}};
    ArkTSTypeDesc optional{ArkTSTypeDesc::Kind::OPTIONAL, "", {reference}};
    ArkTSTypeDesc tuple{ArkTSTypeDesc::Kind::TUPLE, "", {reference, ScalarDesc("Int32")}};
    for (const auto& position : {reference, array, optional, tuple}) {
        EXPECT_TRUE(ArkTSForeignWrapperSource({position}, ScalarDesc("Unit"), 0, "__locals_").empty());
        EXPECT_TRUE(ArkTSForeignWrapperSource({}, position, 0, "__locals_").empty());
    }
    // Negative control: a reference-free signature still generates without the
    // map, so the guard keys on the reference leaf rather than on the pointer.
    EXPECT_FALSE(ArkTSForeignWrapperSource({ScalarDesc("Int32")}, ScalarDesc("Unit"), 0, "__locals_").empty());
    // Positive control: a result reference needs the map, and with it the same
    // call generates and records the class for the lowering walker.
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto code = ArkTSForeignWrapperSource({}, reference, 0, "__locals_",
        ArkTSReceiverKind::MODULE, ARKTS_DERIVE_OPTIONAL, &classRefs);
    ASSERT_FALSE(code.empty());
    ASSERT_EQ(classRefs.size(), 1);
    EXPECT_EQ(classRefs.begin()->second, Ptr<Decl>(&point));
}

TEST_F(ArkTSTypeMappingTest, RestPositionAcceptsReferenceElements)
{
    // The rest position stays an Array<T'>; a reference element converts
    // each item into its own argument.
    ClassDecl point;
    InitWrappedPoint(point);
    ArkTSTypeDesc reference{ArkTSTypeDesc::Kind::REFERENCE, "", {}, Ptr<Decl>(&point)};
    ArkTSTypeDesc array{ArkTSTypeDesc::Kind::ARRAY_COPY, "", {reference}};
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto code = ArkTSForeignWrapperSource({array}, ScalarDesc("Unit"), 0, "__locals_", 0,
        ArkTSReceiverKind::MODULE, ARKTS_DERIVE_OPTIONAL, &classRefs);
    ASSERT_FALSE(code.empty());
    EXPECT_NE(code.find("__cj_arkts_ref_value().toJSValue()"), std::string::npos);
    EXPECT_NE(code.find("Array<ark_interop.JSValue>("), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSTypeMappingTest, ClassMemberSourceConvertsObjectReferences)
{
    // Instance receiver: arguments leave through the stored reference and
    // incoming objects are guarded and wrapped. No class name is spliced into
    // the generated syntax; the class is recorded by placeholder.
    ClassDecl point;
    point.identifier = "Point";
    point.fullPackageName = "pkg";
    point.EnableAttr(Attribute::ARKTS_CLASS_WRAPPER);
    ArkTSTypeDesc reference{ArkTSTypeDesc::Kind::REFERENCE, "", {}, Ptr<Decl>(&point)};
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto code = ArkTSClassMethodSource({reference}, reference, 0, "__locals_", ARKTS_NO_REST,
        ArkTSMemberReceiver::INSTANCE, ArkTSReceiverKind::MODULE, 1, classRefs);
    ASSERT_FALSE(code.empty());
    EXPECT_NE(code.find("__cj_arkts_ref_value"), std::string::npos);
    EXPECT_NE(code.find("toJSValue"), std::string::npos);
    EXPECT_NE(code.find("isObject"), std::string::npos);
    EXPECT_NE(code.find("asObject()"), std::string::npos);
    ASSERT_EQ(classRefs.size(), 1);
    EXPECT_EQ(classRefs.begin()->second, Ptr<Decl>(&point));
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    // Class member container positions use the same conversion and record a
    // type placeholder for the reference element.
    ArkTSTypeDesc array{ArkTSTypeDesc::Kind::ARRAY_COPY, "", {reference}};
    ArkTSTypeDesc optional{ArkTSTypeDesc::Kind::OPTIONAL, "", {reference}};
    classRefs.clear();
    auto containerCode = ArkTSClassMethodSource({optional}, array, 0, "__locals_", ARKTS_NO_REST,
        ArkTSMemberReceiver::INSTANCE, ArkTSReceiverKind::MODULE, 1, classRefs);
    ASSERT_FALSE(containerCode.empty());
    EXPECT_NE(containerCode.find("Array<__locals_type_"), std::string::npos);
    EXPECT_EQ(containerCode.find("Array<Point>("), std::string::npos);
    EXPECT_NE(containerCode.find("getElement"), std::string::npos);
    EXPECT_NE(containerCode.find("isObject"), std::string::npos);
    ASSERT_EQ(classRefs.size(), 2);
    for (auto& entry : classRefs) {
        EXPECT_EQ(entry.second, Ptr<Decl>(&point));
    }
    auto containerFile = Parse(containerCode);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    classRefs.clear();
    auto propertyCode = ArkTSClassPropertySource(array, false, ArkTSMemberReceiver::INSTANCE, 0, "__locals_",
        ArkTSReceiverKind::MODULE, classRefs);
    ASSERT_FALSE(propertyCode.empty());
    EXPECT_NE(propertyCode.find("Array<__locals_type_"), std::string::npos);
    ASSERT_EQ(classRefs.size(), 2);
    auto propertyFile = Parse(propertyCode);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSTypeMappingTest, ClassConstructorSourceUsesTheResolvedClass)
{
    // Constructor: the class value resolves through the module, is converted
    // to a class and constructed with the converted arguments.
    ClassDecl point;
    point.identifier = "Point";
    point.fullPackageName = "pkg";
    point.EnableAttr(Attribute::ARKTS_CLASS_WRAPPER);
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto code = ArkTSClassConstructorSource({ScalarDesc("Float64")}, 0, "__locals_", ARKTS_NO_REST,
        ArkTSReceiverKind::MODULE, 1, classRefs);
    ASSERT_FALSE(code.empty());
    EXPECT_NE(code.find("asClass()"), std::string::npos);
    EXPECT_NE(code.find(".new("), std::string::npos);
    EXPECT_NE(code.find("__cj_arkts_ref = "), std::string::npos);
    EXPECT_NE(code.find("asObject()"), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSTypeMappingTest, SuperThunkConstructorKeepsSuperFirst)
{
    // The child wrapper form (a class whose parent is a prepared wrapper):
    // the construction statements move inside one thunk literal passed to
    // super(..., ()) - the parent's thunk-taking wrapping constructor stores
    // the value - because Cangjie requires super(...) to be the constructor's
    // first expression. The context establishment runs inside the thunk with
    // the construction, and no reference-field assignment is emitted.
    ClassDecl point;
    point.identifier = "Point";
    point.fullPackageName = "pkg";
    point.EnableAttr(Attribute::ARKTS_CLASS_WRAPPER);
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto code = ArkTSClassConstructorSource({ScalarDesc("Float64")}, 0, "__locals_", ARKTS_NO_REST,
        ArkTSReceiverKind::MODULE, 1, classRefs, ArkTSConstructorForm::SUPER_THUNK);
    ASSERT_FALSE(code.empty());
    EXPECT_NE(code.find("super({ =>"), std::string::npos);
    EXPECT_NE(code.find("}, ())"), std::string::npos);
    EXPECT_NE(code.find("asClass()"), std::string::npos);
    EXPECT_NE(code.find(".new("), std::string::npos);
    // The thunk's value is the new object reference and nothing is assigned
    // to a field the child does not declare.
    EXPECT_NE(code.find("__locals_object.asObject()"), std::string::npos);
    EXPECT_EQ(code.find(ARKTS_SELF), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    // The same form composes with the trailing-optional forwarding: the
    // block-lambda call array is built inside the thunk.
    auto optional = [](const std::string& name) {
        return WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc(name));
    };
    classRefs.clear();
    const std::vector<ArkTSTypeDesc> types{ScalarDesc("Int32"), optional("String")};
    auto optionalCode = ArkTSClassConstructorSource(types, 0, "__locals_", ARKTS_NO_REST, ArkTSReceiverKind::MODULE,
        1, classRefs, ArkTSConstructorForm::SUPER_THUNK);
    ASSERT_FALSE(optionalCode.empty());
    EXPECT_NE(optionalCode.find("super({ =>"), std::string::npos);
    EXPECT_NE(optionalCode.find("Array<ark_interop.JSValue>(__locals_last_0 + 1,"), std::string::npos);
    EXPECT_NE(optionalCode.find(".new(__locals_args_0)"), std::string::npos);
    auto optionalFile = Parse(optionalCode);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSTypeMappingTest, ClassOptionalMembersForwardUpToTheLastSome)
{
    // Class members and constructors use the same trailing-optional rule as
    // the function path: an optional position up to the last Some is
    // forwarded (a None before it as ctx.undefined()), and every optional
    // after the last Some is omitted. These expectations pin the shared
    // block-lambda array and the omitted tail; the generated locals use the
    // same role names as the function path.
    auto optional = [](const std::string& name) {
        return WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc(name));
    };
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    const std::vector<ArkTSTypeDesc> types{ScalarDesc("Int32"), optional("Int32"), optional("String")};
    auto method = ArkTSClassMethodSource(types, ScalarDesc("Unit"), 0, "__locals_", ARKTS_NO_REST,
        ArkTSMemberReceiver::INSTANCE, ArkTSReceiverKind::MODULE, 1, classRefs);
    ASSERT_FALSE(method.empty());
    EXPECT_NE(method.find("let __locals_last_0 = if (__argument_2.isSome()) {\n        2\n    } else if "
                          "(__argument_1.isSome()) {\n        1\n    } else {\n        0\n    }"),
        std::string::npos);
    EXPECT_NE(method.find("Array<ark_interop.JSValue>(__locals_last_0 + 1, { __locals_index_0 =>"), std::string::npos);
    EXPECT_NE(method.find("case 0 => __locals_arg_0"), std::string::npos);
    EXPECT_NE(method.find("case 1 => __locals_arg_1"), std::string::npos);
    EXPECT_NE(method.find("case 2 => __locals_arg_2"), std::string::npos);
    EXPECT_NE(method.find("case _ => __locals_context.undefined().toJSValue()"), std::string::npos);
    EXPECT_NE(method.find("callMethod(\"" + Placeholder("__member_name__") + "\", __locals_args_0)"),
        std::string::npos);
    auto methodFile = Parse(method);
    ASSERT_EQ(diag.GetErrorCount(), 0);

    classRefs.clear();
    auto ctor = ArkTSClassConstructorSource(types, 0, "__locals_", ARKTS_NO_REST, ArkTSReceiverKind::MODULE, 1,
        classRefs);
    ASSERT_FALSE(ctor.empty());
    EXPECT_NE(ctor.find("let __locals_last_0 = if (__argument_2.isSome()) {"), std::string::npos);
    EXPECT_NE(ctor.find("let __locals_args_0 = Array<ark_interop.JSValue>(__locals_last_0 + 1,"), std::string::npos);
    EXPECT_NE(ctor.find(".new(__locals_args_0)"), std::string::npos);
    auto ctorFile = Parse(ctor);
    ASSERT_EQ(diag.GetErrorCount(), 0);

    // An empty optional run (optionalStart == types.size()) keeps the plain
    // fixed argument array; no gate or block-lambda is emitted.
    classRefs.clear();
    auto plain = ArkTSClassMethodSource(types, ScalarDesc("Unit"), 0, "__locals_", ARKTS_NO_REST,
        ArkTSMemberReceiver::INSTANCE, ArkTSReceiverKind::MODULE, types.size(), classRefs);
    ASSERT_FALSE(plain.empty());
    EXPECT_EQ(plain.find("isSome"), std::string::npos);
    EXPECT_NE(plain.find("callMethod(\"" + Placeholder("__member_name__") +
                    "\", [__locals_arg_0, __locals_arg_1, __locals_arg_2])"),
        std::string::npos);
    auto plainFile = Parse(plain);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSTypeMappingTest, ClassStaticMemberCallsWithThisArg)
{
    // A static member has no stored object reference: it resolves the class
    // value and calls the method function with the class value as `this`.
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto code = ArkTSClassMethodSource({ScalarDesc("Float64")}, ScalarDesc("Unit"), 0, "__locals_", ARKTS_NO_REST,
        ArkTSMemberReceiver::STATIC, ArkTSReceiverKind::MODULE, 1, classRefs);
    ASSERT_FALSE(code.empty());
    EXPECT_NE(code.find("asFunction().call("), std::string::npos);
    EXPECT_NE(code.find("thisArg:"), std::string::npos);
    EXPECT_NE(code.find("getProperty(\"" + Placeholder("__class_name__") + "\")"), std::string::npos);
    // The static path never reads the instance reference accessor.
    EXPECT_EQ(code.find("__cj_arkts_ref_value"), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSTypeMappingTest, ClassifyForeignInterfaceAsObjectReference)
{
    // A prepared foreign interface is a reference to an ArkTS object, the
    // same shape as a prepared foreign class. The implementation class is
    // resolved separately by the caller's ImportManager.
    InterfaceDecl person;
    person.identifier = "IPerson";
    person.fullPackageName = "pkg";
    person.EnableAttr(Attribute::ARKTS_INTERFACE_WRAPPER);
    InterfaceTy personTy("IPerson", person, {});
    auto reference = ArkTSClassifyType(&personTy);
    ASSERT_EQ(reference.kind, ArkTSTypeDesc::Kind::REFERENCE);
    EXPECT_EQ(reference.reference, Ptr<Decl>(&person));
    EXPECT_EQ(reference.referenceImpl, nullptr);
    // Without the generated implementation class the reference cannot be
    // constructed from a JS value, so the position is not generatable.
    EXPECT_FALSE(ArkTSTypeDescGeneratable(reference));
    EXPECT_EQ(ArkTSTypeDescName(reference), "IPerson");

    EnumDecl optionDecl;
    optionDecl.identifier = "Option";
    optionDecl.fullPackageName = "std.core";
    EnumTy optional("Option", optionDecl, {&personTy});
    auto optionalDesc = ArkTSClassifyType(&optional);
    ASSERT_EQ(optionalDesc.kind, ArkTSTypeDesc::Kind::OPTIONAL);
    ASSERT_EQ(optionalDesc.elements.size(), 1);
    EXPECT_EQ(optionalDesc.elements.front().kind, ArkTSTypeDesc::Kind::REFERENCE);

    StructDecl arrayDecl;
    arrayDecl.identifier = "Array";
    arrayDecl.fullPackageName = "std.core";
    StructTy array("Array", arrayDecl, {&personTy});
    // Interface references stay out of Array positions (AR-06 BR-11); only a
    // prepared foreign class reference may ride the AR-04 container copy.
    EXPECT_EQ(ArkTSClassifyType(&array).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
    ClassDecl wrapper;
    wrapper.identifier = "IPersonClass";
    wrapper.fullPackageName = "pkg";
    wrapper.EnableAttr(Attribute::ARKTS_CLASS_WRAPPER);
    ClassTy wrapperTy("IPersonClass", wrapper, {});
    StructTy classArray("Array", arrayDecl, {&wrapperTy});
    EXPECT_EQ(ArkTSClassifyType(&classArray).kind, ArkTSTypeDesc::Kind::ARRAY_COPY);
}

TEST_F(ArkTSTypeMappingTest, InterfaceReferenceConversionUsesTheImplementationClass)
{
    // to-JS reads the interface accessor; from-JS constructs the generated
    // implementation class recorded by placeholder.
    InterfaceDecl person;
    person.identifier = "IPerson";
    person.fullPackageName = "pkg";
    person.EnableAttr(Attribute::ARKTS_INTERFACE_WRAPPER);
    ClassDecl impl;
    impl.identifier = ArkTSInterfaceImplName(person);
    impl.fullPackageName = "pkg";
    impl.EnableAttr(Attribute::ARKTS_INTERFACE_WRAPPER);
    EXPECT_EQ(impl.identifier.Val(), "IPerson$arkts_impl");
    ArkTSTypeDesc reference{ArkTSTypeDesc::Kind::REFERENCE, "", {}, Ptr<Decl>(&person), Ptr<Decl>(&impl)};
    EXPECT_TRUE(ArkTSTypeDescGeneratable(reference));
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto code = ArkTSClassMethodSource({reference}, reference, 0, "__locals_", ARKTS_NO_REST,
        ArkTSMemberReceiver::INSTANCE, ArkTSReceiverKind::MODULE, 1, classRefs);
    ASSERT_FALSE(code.empty());
    EXPECT_NE(code.find("__cj_arkts_ref_value"), std::string::npos);
    EXPECT_NE(code.find("toJSValue"), std::string::npos);
    EXPECT_NE(code.find("isObject"), std::string::npos);
    ASSERT_EQ(classRefs.size(), 1);
    EXPECT_EQ(classRefs.begin()->second, Ptr<Decl>(&impl));
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSTypeMappingTest, InterfaceCallAndIndexSourcesUseTheStoredObject)
{
    std::unordered_map<std::string, Ptr<Decl>> callRefs;
    auto call = ArkTSInterfaceCallSource(
        {ScalarDesc("Float64"), ScalarDesc("Float64")}, ScalarDesc("Float64"), "__locals_", callRefs);
    ASSERT_FALSE(call.empty());
    EXPECT_NE(call.find("__cj_arkts_ref_value"), std::string::npos);
    EXPECT_NE(call.find("asFunction"), std::string::npos);
    EXPECT_NE(call.find("thisArg"), std::string::npos);

    auto getter = ArkTSInterfaceIndexSource(ScalarDesc("String"), ScalarDesc("String"), false, "__locals_", callRefs);
    ASSERT_FALSE(getter.empty());
    EXPECT_NE(getter.find("getProperty"), std::string::npos);
    EXPECT_NE(getter.find("Option.None"), std::string::npos);
    EXPECT_NE(getter.find("Option.Some"), std::string::npos);

    auto setter =
        ArkTSInterfaceIndexSource(ScalarDesc("String"), ScalarDesc("String"), true, "__locals_", callRefs);
    ASSERT_FALSE(setter.empty());
    EXPECT_NE(setter.find("setProperty"), std::string::npos);

    auto numeric =
        ArkTSInterfaceIndexSource(ScalarDesc("Float64"), ScalarDesc("Float64"), false, "__locals_", callRefs);
    ASSERT_FALSE(numeric.empty());
    EXPECT_NE(numeric.find("Int64("), std::string::npos);
    EXPECT_NE(numeric.find("toString()"), std::string::npos);

    EXPECT_TRUE(
        ArkTSInterfaceIndexSource(ScalarDesc("Int32"), ScalarDesc("String"), false, "__locals_", callRefs).empty());
}

// The function wrapper resolves the module and every namespace segment before
// looking up the export: a missing segment names the declaration, the module
// and the failing path; a missing export names the export; a non-callable
// export names the declaration and the export. The module load itself stays
// outside any guard, so its library failure is never translated.
TEST_F(ArkTSTypeMappingTest, FunctionWrapperGuardsNamespaceExportAndCallability)
{
    auto code =
        ArkTSForeignWrapperSource({ScalarDesc("String")}, ScalarDesc("Float64"), 1, "__locals_");
    ASSERT_FALSE(code.empty());
    // Namespace guard: one existence check before asObject(), with the failing
    // segment spelled by placeholder.
    EXPECT_NE(code.find("let __locals_nsvalue_0 = __locals_module.getProperty(\"" + Placeholder("__namespace_0__") +
                    "\")"),
        std::string::npos);
    EXPECT_NE(code.find("if (__locals_nsvalue_0.isUndefined()) {"), std::string::npos);
    EXPECT_NE(code.find("let __locals_namespace_0 = __locals_nsvalue_0.asObject()"), std::string::npos);
    EXPECT_NE(code.find("throw ForeignRuntimeException("), std::string::npos);
    // Message pieces: declaration context, module locator, failing prefix.
    EXPECT_NE(code.find("\"ArkTS foreign function '\" + \"" + Placeholder("__name__") + "\" + \"'\""),
        std::string::npos);
    EXPECT_NE(code.find("\"module '\" + \"" + Placeholder("__module__") + "\" + \"'\""), std::string::npos);
    EXPECT_NE(code.find("\" has no namespace '\""), std::string::npos);
    // Export guard: the same lookup yields a JSValue, so callability can be
    // decided before the call.
    EXPECT_NE(code.find("__locals_namespace_0.getProperty(\"" + Placeholder("__name__") + "\")"),
        std::string::npos);
    EXPECT_NE(code.find("if (__locals_target_0.isUndefined()) {"), std::string::npos);
    EXPECT_NE(code.find("\" has no export '\""), std::string::npos);
    // The namespace path narrows the export lookup site.
    EXPECT_NE(code.find("\" in namespace '\""), std::string::npos);
    // Callability guard names the declaration and the export.
    EXPECT_NE(code.find("if (!__locals_target_0.isFunction()) {"), std::string::npos);
    EXPECT_NE(code.find("\": the export is not a function\""), std::string::npos);
    // The module load is not captured; the export lookup and the call are the
    // two translated statements.
    EXPECT_NE(code.find("let __locals_module = __locals_context.requireArkModule(\"" + Placeholder("__module__") +
                    "\").asObject()"),
        std::string::npos);
    EXPECT_EQ(CountOccurrences(code, "requireArkModule"), 1);
    EXPECT_EQ(CountOccurrences(code, "try {"), 2);
    // The export lookup and the call itself are translated.
    EXPECT_NE(code.find("__locals_namespace_0.callMethod(\"" + Placeholder("__name__") + "\", [__locals_arg_0])"),
        std::string::npos);
    EXPECT_NE(code.find("let __locals_result = try {"), std::string::npos);
    EXPECT_NE(code.find("} catch (__locals_error_0: BusinessException) {"), std::string::npos);
    EXPECT_NE(code.find("if (__locals_error_0.code != 34300002 && __locals_error_0.code != 0) {"), std::string::npos);
    EXPECT_NE(code.find("throw __locals_error_0"), std::string::npos);
    EXPECT_NE(code.find("\" failed: \" + __locals_error_0.toString()"), std::string::npos);
    EXPECT_EQ(CountOccurrences(code, "catch ("), 2);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ExpectSingleCall(file.get(), "function runtime failure wrapper");
}

// A never-typed result that unexpectedly produces a value is an ArkTS runtime
// failure (Appendix B), so the violation throw uses ForeignRuntimeException.
// The call keeps the same translation, and the violation throw is emitted after
// the translated call and never passes through a conversion exception.
TEST_F(ArkTSTypeMappingTest, NothingResultThrowsForeignRuntimeException)
{
    auto code = ArkTSForeignWrapperSource({}, ScalarDesc("Nothing"), 1, "__locals_");
    ASSERT_FALSE(code.empty());
    EXPECT_NE(code.find("    throw ForeignRuntimeException(\"ArkTS never function returned\")"), std::string::npos);
    // The violation throw follows the translated call; the call translation
    // itself is still present for the target invocation.
    EXPECT_NE(code.find("} catch (__locals_error_0: BusinessException) {"), std::string::npos);
    EXPECT_NE(code.find("    }\n    throw ForeignRuntimeException(\"ArkTS never function returned\")"),
        std::string::npos);
    // No conversion wraps the never violation.
    EXPECT_EQ(code.find("ExternConversionException"), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

// The class wrapper resolves the class value with the same export guard as a
// function export, rejects a non-constructor class value, and translates the
// construction call. The class value stays a JSValue until the constructor
// check has passed, so asClass() is never reached with a non-class value.
TEST_F(ArkTSTypeMappingTest, ClassWrapperGuardsTheClassValueAndConstructor)
{
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto code = ArkTSClassConstructorSource({ScalarDesc("Float64")}, 1, "__locals_", ARKTS_NO_REST,
        ArkTSReceiverKind::MODULE, 1, classRefs);
    ASSERT_FALSE(code.empty());
    // Namespace and export resolution before asClass().
    EXPECT_NE(code.find("let __locals_nsvalue_0 = __locals_module.getProperty(\"" + Placeholder("__namespace_0__") +
                    "\")"),
        std::string::npos);
    EXPECT_NE(code.find("if (__locals_nsvalue_0.isUndefined()) {"), std::string::npos);
    EXPECT_NE(code.find("\"ArkTS foreign class '\" + \"" + Placeholder("__class_name__") + "\" + \"'\""),
        std::string::npos);
    EXPECT_NE(code.find("\" has no namespace '\""), std::string::npos);
    EXPECT_NE(code.find("let __locals_classvalue = __locals_namespace_0.getProperty(\"" +
                    Placeholder("__class_name__") + "\")"),
        std::string::npos);
    EXPECT_NE(code.find("if (__locals_classvalue.isUndefined()) {"), std::string::npos);
    EXPECT_NE(code.find("\" has no export '\""), std::string::npos);
    // A non-callable class value is reported as a non-constructor.
    EXPECT_NE(code.find("if (!__locals_classvalue.isFunction()) {"), std::string::npos);
    EXPECT_NE(code.find("\": the export is not a constructor\""), std::string::npos);
    // Only after the check is the value converted to a class.
    auto callable = code.find("if (!__locals_classvalue.isFunction()) {");
    auto asClass = code.find("let __locals_class = __locals_classvalue.asClass()");
    ASSERT_NE(callable, std::string::npos);
    ASSERT_NE(asClass, std::string::npos);
    EXPECT_LT(callable, asClass);
    // The construction is the only translated call.
    EXPECT_NE(code.find("let __locals_object = try {"), std::string::npos);
    EXPECT_NE(code.find("__locals_class.new(__locals_arg_0)"), std::string::npos);
    EXPECT_NE(code.find("} catch (__locals_error_0: BusinessException) {"), std::string::npos);
    EXPECT_NE(code.find("if (__locals_error_0.code != 34300002 && __locals_error_0.code != 0) {"), std::string::npos);
    EXPECT_NE(code.find("throw __locals_error_0"), std::string::npos);
    EXPECT_NE(code.find("\" failed: \" + __locals_error_0.toString()"), std::string::npos);
    EXPECT_EQ(CountOccurrences(code, "catch ("), 1);
    EXPECT_NE(code.find("__cj_arkts_self.__cj_arkts_ref = __locals_object.asObject()"), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);

    // The child wrapper form keeps the same guards and translation inside the
    // super thunk; it stores nothing itself.
    classRefs.clear();
    auto thunk = ArkTSClassConstructorSource({ScalarDesc("Float64")}, 1, "__locals_", ARKTS_NO_REST,
        ArkTSReceiverKind::MODULE, 1, classRefs, ArkTSConstructorForm::SUPER_THUNK);
    ASSERT_FALSE(thunk.empty());
    EXPECT_NE(thunk.find("if (!__locals_classvalue.isFunction()) {"), std::string::npos);
    EXPECT_NE(thunk.find("\": the export is not a constructor\""), std::string::npos);
    EXPECT_NE(thunk.find("let __locals_object = try {"), std::string::npos);
    EXPECT_NE(thunk.find("} catch (__locals_error_0: BusinessException) {"), std::string::npos);
    EXPECT_NE(thunk.find("if (__locals_error_0.code != 34300002 && __locals_error_0.code != 0) {"), std::string::npos);
    EXPECT_EQ(CountOccurrences(thunk, "catch ("), 1);
    EXPECT_EQ(thunk.find(ARKTS_SELF), std::string::npos);
    auto thunkFile = Parse(thunk);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

// Instance and static members resolve their target before calling it: a
// missing member names the class and the member, a non-callable member names
// the same context, and the call is translated. A static member resolves the
// class value first and uses it as the call's `this` argument.
TEST_F(ArkTSTypeMappingTest, ClassMembersGuardExistenceAndCallability)
{
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto instance = ArkTSClassMethodSource({}, ScalarDesc("Unit"), 0, "__locals_", ARKTS_NO_REST,
        ArkTSMemberReceiver::INSTANCE, ArkTSReceiverKind::MODULE, 0, classRefs);
    ASSERT_FALSE(instance.empty());
    EXPECT_NE(instance.find("let __locals_target_0 = __cj_arkts_self.__cj_arkts_ref_value().getProperty(\"" +
                            Placeholder("__member_name__") + "\")"),
        std::string::npos);
    EXPECT_NE(instance.find("if (__locals_target_0.isUndefined()) {"), std::string::npos);
    EXPECT_NE(instance.find("\"ArkTS foreign class '\" + \"" + Placeholder("__class_name__") +
                            "\" + \"' member '\" + \"" + Placeholder("__member_name__") + "\" + " +
                            "\"' in module '\" + \"" + Placeholder("__module__") + "\" + \"'\""),
        std::string::npos);
    EXPECT_NE(instance.find("\" does not exist\""), std::string::npos);
    EXPECT_NE(instance.find("if (!__locals_target_0.isFunction()) {"), std::string::npos);
    EXPECT_NE(instance.find("\" is not a function\""), std::string::npos);
    EXPECT_NE(instance.find("let __locals_result = try {"), std::string::npos);
    EXPECT_NE(instance.find("__cj_arkts_self.__cj_arkts_ref_value().callMethod(\"" + Placeholder("__member_name__") +
                            "\", Array<ark_interop.JSValue>())"),
        std::string::npos);
    EXPECT_NE(instance.find("} catch (__locals_error_0: BusinessException) {"), std::string::npos);
    EXPECT_NE(instance.find("if (__locals_error_0.code != 34300002 && __locals_error_0.code != 0) {"),
        std::string::npos);
    EXPECT_EQ(CountOccurrences(instance, "catch ("), 1);
    auto instanceFile = Parse(instance);
    ASSERT_EQ(diag.GetErrorCount(), 0);

    classRefs.clear();
    auto staticMember = ArkTSClassMethodSource({}, ScalarDesc("Unit"), 1, "__locals_", ARKTS_NO_REST,
        ArkTSMemberReceiver::STATIC, ArkTSReceiverKind::MODULE, 0, classRefs);
    ASSERT_FALSE(staticMember.empty());
    // The static path resolves the class value through the namespace first.
    EXPECT_NE(staticMember.find("let __locals_classvalue = __locals_namespace_0.getProperty(\"" +
                    Placeholder("__class_name__") + "\")"),
        std::string::npos);
    EXPECT_NE(staticMember.find("if (__locals_classvalue.isUndefined()) {"), std::string::npos);
    EXPECT_NE(staticMember.find("\" has no export '\""), std::string::npos);
    EXPECT_NE(staticMember.find("let __locals_target_0 = __locals_classvalue.getProperty(\"" +
                    Placeholder("__member_name__") + "\")"),
        std::string::npos);
    EXPECT_NE(staticMember.find("if (__locals_target_0.isUndefined()) {"), std::string::npos);
    EXPECT_NE(staticMember.find("\" does not exist\""), std::string::npos);
    EXPECT_NE(staticMember.find("if (!__locals_target_0.isFunction()) {"), std::string::npos);
    EXPECT_NE(staticMember.find("\" is not a function\""), std::string::npos);
    EXPECT_NE(
        staticMember.find("__locals_target_0.asFunction().call(Array<ark_interop.JSValue>(), thisArg: "
                          "__locals_classvalue)"),
        std::string::npos);
    EXPECT_EQ(CountOccurrences(staticMember, "catch ("), 1);
    auto staticFile = Parse(staticMember);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

// A property value may legally be undefined, so property access has no
// existence rejection: the getter reads and the setter writes are translated
// directly. The Int32 value type keeps the conversion free of undefined
// decisions, so any isUndefined() in the body would be an access-level guard.
TEST_F(ArkTSTypeMappingTest, ClassPropertyAccessIsTranslatedWithoutExistenceRejection)
{
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto getter = ArkTSClassPropertySource(ScalarDesc("Int32"), false, ArkTSMemberReceiver::INSTANCE, 0, "__locals_",
        ArkTSReceiverKind::MODULE, classRefs);
    ASSERT_FALSE(getter.empty());
    EXPECT_NE(getter.find("let __locals_result = try {"), std::string::npos);
    EXPECT_NE(getter.find("__cj_arkts_self.__cj_arkts_ref_value().getProperty(\"" + Placeholder("__member_name__") +
                    "\")"),
        std::string::npos);
    EXPECT_NE(getter.find("} catch (__locals_error_0: BusinessException) {"), std::string::npos);
    EXPECT_NE(getter.find("if (__locals_error_0.code != 34300002 && __locals_error_0.code != 0) {"), std::string::npos);
    EXPECT_NE(getter.find("throw __locals_error_0"), std::string::npos);
    EXPECT_NE(getter.find("\" failed: \" + __locals_error_0.toString()"), std::string::npos);
    EXPECT_EQ(CountOccurrences(getter, "catch ("), 1);
    EXPECT_EQ(getter.find("if (__locals_result.isUndefined()) {\n"), std::string::npos);
    auto getterFile = Parse(getter);
    ASSERT_EQ(diag.GetErrorCount(), 0);

    classRefs.clear();
    auto setter = ArkTSClassPropertySource(ScalarDesc("Int32"), true, ArkTSMemberReceiver::INSTANCE, 0, "__locals_",
        ArkTSReceiverKind::MODULE, classRefs);
    ASSERT_FALSE(setter.empty());
    EXPECT_NE(setter.find("__cj_arkts_self.__cj_arkts_ref_value().setProperty(\"" + Placeholder("__member_name__") +
                          "\", __locals_context.number(Float64(__argument_0)).toJSValue())"),
        std::string::npos);
    EXPECT_NE(setter.find("} catch (__locals_error_0: BusinessException) {"), std::string::npos);
    EXPECT_NE(setter.find("if (__locals_error_0.code != 34300002 && __locals_error_0.code != 0) {"), std::string::npos);
    EXPECT_NE(setter.find("throw __locals_error_0"), std::string::npos);
    EXPECT_EQ(CountOccurrences(setter, "catch ("), 1);
    EXPECT_EQ(setter.find("if (__locals_result.isUndefined()) {\n"), std::string::npos);
    auto setterFile = Parse(setter);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

// The interface call signature is itself the target: a non-callable stored
// value names the interface and the member, the call is translated, and the
// index get/set accessors are translated without a member existence check
// (a missing index yields null/undefined, which the getter maps to None).
TEST_F(ArkTSTypeMappingTest, InterfaceWrapperGuardsTheCallSignatureAndTranslatesAccess)
{
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto call = ArkTSInterfaceCallSource(
        {ScalarDesc("Float64"), ScalarDesc("Float64")}, ScalarDesc("Float64"), "__locals_", classRefs);
    ASSERT_FALSE(call.empty());
    EXPECT_NE(call.find("let __locals_self_0 = __cj_arkts_self.__cj_arkts_ref_value()"), std::string::npos);
    EXPECT_NE(call.find("let __locals_selfvalue_0 = __locals_self_0.toJSValue()"), std::string::npos);
    EXPECT_NE(call.find("if (!__locals_selfvalue_0.isFunction()) {"), std::string::npos);
    EXPECT_NE(call.find("\"ArkTS foreign interface '\" + \"" + Placeholder("__class_name__") +
                        "\" + \"' member '\" + \"" + Placeholder("__member_name__") + "\" + " +
                        "\"' in module '\" + \"" + Placeholder("__module__") + "\" + \"'\""),
        std::string::npos);
    EXPECT_NE(call.find("\" is not callable\""), std::string::npos);
    EXPECT_NE(call.find("let __locals_function_0 = __locals_selfvalue_0.asFunction()"), std::string::npos);
    EXPECT_NE(call.find("__locals_function_0.call([__locals_arg_0, __locals_arg_1], thisArg: __locals_selfvalue_0)"),
        std::string::npos);
    EXPECT_NE(call.find("let __locals_result_0 = try {"), std::string::npos);
    EXPECT_NE(call.find("} catch (__locals_error_0: BusinessException) {"), std::string::npos);
    EXPECT_NE(call.find("if (__locals_error_0.code != 34300002 && __locals_error_0.code != 0) {"), std::string::npos);
    EXPECT_NE(call.find("throw __locals_error_0"), std::string::npos);
    EXPECT_NE(call.find("\" failed: \" + __locals_error_0.toString()"), std::string::npos);
    EXPECT_EQ(CountOccurrences(call, "catch ("), 1);
    auto callFile = Parse(call);
    ASSERT_EQ(diag.GetErrorCount(), 0);

    classRefs.clear();
    auto indexGetter =
        ArkTSInterfaceIndexSource(ScalarDesc("String"), ScalarDesc("String"), false, "__locals_", classRefs);
    ASSERT_FALSE(indexGetter.empty());
    EXPECT_NE(indexGetter.find("let __locals_self_0 = __cj_arkts_self.__cj_arkts_ref_value()"), std::string::npos);
    EXPECT_NE(indexGetter.find("let __locals_result_0 = try {"), std::string::npos);
    EXPECT_NE(indexGetter.find("__locals_self_0.getProperty(__argument_0)"), std::string::npos);
    EXPECT_NE(indexGetter.find("} catch (__locals_error_0: BusinessException) {"), std::string::npos);
    EXPECT_NE(indexGetter.find("if (__locals_error_0.code != 34300002 && __locals_error_0.code != 0) {"),
        std::string::npos);
    EXPECT_NE(indexGetter.find("\" failed: \" + __locals_error_0.toString()"), std::string::npos);
    EXPECT_NE(indexGetter.find("Option.None"), std::string::npos);
    EXPECT_NE(indexGetter.find("Option.Some"), std::string::npos);
    EXPECT_EQ(CountOccurrences(indexGetter, "catch ("), 1);
    auto indexGetterFile = Parse(indexGetter);
    ASSERT_EQ(diag.GetErrorCount(), 0);

    classRefs.clear();
    auto indexSetter =
        ArkTSInterfaceIndexSource(ScalarDesc("String"), ScalarDesc("String"), true, "__locals_", classRefs);
    ASSERT_FALSE(indexSetter.empty());
    EXPECT_NE(indexSetter.find("let __locals_self_0 = __cj_arkts_self.__cj_arkts_ref_value()"), std::string::npos);
    EXPECT_NE(indexSetter.find("__locals_self_0.setProperty(__argument_0, "
                               "__locals_context.string(__argument_1).toJSValue())"),
        std::string::npos);
    EXPECT_NE(indexSetter.find("} catch (__locals_error_0: BusinessException) {"), std::string::npos);
    EXPECT_NE(indexSetter.find("if (__locals_error_0.code != 34300002 && __locals_error_0.code != 0) {"),
        std::string::npos);
    EXPECT_NE(indexSetter.find("throw __locals_error_0"), std::string::npos);
    EXPECT_EQ(CountOccurrences(indexSetter, "catch ("), 1);
    auto indexSetterFile = Parse(indexSetter);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

// Every generated call site translates exactly the two runtime-failure codes
// (an outside error and an ArkTS-side throw); every other BusinessException is
// rethrown with its own code and message.
TEST_F(ArkTSTypeMappingTest, OnlyRuntimeFailureCodesAreTranslatedAtEachCallSite)
{
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    struct Site {
        std::string label;
        std::string code;
    };
    std::vector<Site> sites;
    sites.push_back({"function", ArkTSForeignWrapperSource({}, ScalarDesc("Unit"), 0, "__locals_")});
    sites.push_back({"class member", ArkTSClassMethodSource({}, ScalarDesc("Unit"), 0, "__locals_", ARKTS_NO_REST,
                                      ArkTSMemberReceiver::INSTANCE, ArkTSReceiverKind::MODULE, 0, classRefs)});
    sites.push_back({"constructor", ArkTSClassConstructorSource({}, 0, "__locals_", ARKTS_NO_REST,
                                       ArkTSReceiverKind::MODULE, 0, classRefs)});
    sites.push_back({"property get", ArkTSClassPropertySource(ScalarDesc("Int32"), false,
                                         ArkTSMemberReceiver::INSTANCE, 0, "__locals_", ArkTSReceiverKind::MODULE,
                                         classRefs)});
    sites.push_back({"property set", ArkTSClassPropertySource(ScalarDesc("Int32"), true,
                                         ArkTSMemberReceiver::INSTANCE, 0, "__locals_", ArkTSReceiverKind::MODULE,
                                         classRefs)});
    sites.push_back({"interface call", ArkTSInterfaceCallSource({}, ScalarDesc("Unit"), "__locals_", classRefs)});
    sites.push_back({"interface index get", ArkTSInterfaceIndexSource(ScalarDesc("String"), ScalarDesc("String"),
                                                 false, "__locals_", classRefs)});
    sites.push_back({"interface index set", ArkTSInterfaceIndexSource(ScalarDesc("String"), ScalarDesc("String"),
                                                 true, "__locals_", classRefs)});
    for (const auto& site : sites) {
        ASSERT_FALSE(site.code.empty()) << site.label;
        // The function wrapper translates its export lookup as well as the call.
        const size_t translated = site.label == "function" ? 2 : 1;
        EXPECT_EQ(CountOccurrences(site.code, "catch ("), translated) << site.label;
        EXPECT_EQ(CountOccurrences(site.code, "34300002"), translated) << site.label;
        EXPECT_NE(site.code.find("if (__locals_error_0.code != 34300002 && __locals_error_0.code != 0) {"),
            std::string::npos)
            << site.label;
        EXPECT_NE(site.code.find("throw __locals_error_0"), std::string::npos) << site.label;
        for (const auto& otherCode : {"34300003", "34300004", "34300005", "34300006", "34300007", "34300008"}) {
            EXPECT_EQ(site.code.find(otherCode), std::string::npos) << site.label << " " << otherCode;
        }
    }
}

// The ArkTSString preset's constructor stores the ctx.string primitive
// directly (PRESET_STRING form): the Option<String> argument converts through
// the scalar string row, and None becomes the empty string per the preset
// ruling, so both call spellings carry a real primitive into the wrapper
// field. No ArkTS class value is resolved and no `new` runs.
TEST_F(ArkTSTypeMappingTest, PresetStringConstructorStoresTheStringPrimitive)
{
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto code = ArkTSClassConstructorSource(
        {WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc("String"))}, 0, "__locals_", ARKTS_NO_REST,
        ArkTSReceiverKind::GLOBAL, 0, classRefs, ArkTSConstructorForm::PRESET_STRING);
    ASSERT_FALSE(code.empty());
    // Both Option spellings lower to ctx.string(...).toJSValue(); None is the
    // empty string, not ctx.undefined().
    EXPECT_NE(code.find("case Option.Some(__locals_some_0) =>"), std::string::npos);
    EXPECT_NE(code.find("__locals_context.string(__locals_some_0).toJSValue()"), std::string::npos);
    EXPECT_NE(code.find("case Option.None =>"), std::string::npos);
    EXPECT_NE(code.find("__locals_context.string(\"\").toJSValue()"), std::string::npos);
    EXPECT_EQ(CountOccurrences(code, "ctx.string"), 0);
    // The class-value preamble never runs: no requireArkModule and no new().
    EXPECT_EQ(code.find("requireArkModule"), std::string::npos);
    EXPECT_EQ(code.find("asClass"), std::string::npos);
    EXPECT_NE(code.find("__cj_arkts_self.__cj_arkts_ref = "), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

// A preset-string member call resolves the member on the boxed receiver:
// the stored handle is a JS primitive and member access rejects primitives,
// so the member is looked up on Object(value). The stored handle itself stays
// a primitive, so a value crossing back to ArkTS is still a string.
TEST_F(ArkTSTypeMappingTest, PresetStringMemberCallUsesBoxedReceiver)
{
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto code = ArkTSClassMethodSource({ScalarDesc("Float64")}, ScalarDesc("String"), 0, "__locals_", ARKTS_NO_REST,
        ArkTSMemberReceiver::INSTANCE, ArkTSReceiverKind::GLOBAL, 1, classRefs, "class", true);
    ASSERT_FALSE(code.empty());
    EXPECT_NE(code.find("let __locals_boxed = __locals_context.global.getProperty(\"Object\").asClass().new("
                        "__cj_arkts_self.__cj_arkts_ref_value()).asObject()"),
        std::string::npos);
    EXPECT_NE(code.find("let __locals_target_0 = __locals_boxed.getProperty(\"" +
                        Placeholder("__member_name__") + "\")"),
        std::string::npos);
    EXPECT_NE(code.find("if (!__locals_target_0.isFunction()) {"), std::string::npos);
    EXPECT_NE(code.find("__locals_boxed.callMethod(\"" + Placeholder("__member_name__") + "\", [__locals_arg_0])"),
        std::string::npos);
    // The stored primitive is never used as a member receiver directly.
    EXPECT_EQ(code.find("__cj_arkts_self.__cj_arkts_ref_value().getProperty"), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

// ---- B4b: ArkTSFunc<F> carrier classification and construction glue ----

namespace {
// A hand-built carrier description for the generator tests: the same shape
// classification produces for a satisfied `F`.
ArkTSTypeDesc CarrierDescForTest(Ptr<Decl> preset, std::vector<ArkTSTypeDesc> params, const ArkTSTypeDesc& result)
{
    ArkTSTypeDesc desc;
    desc.kind = ArkTSTypeDesc::Kind::ARKTS_FUNC;
    desc.reference = preset;
    desc.elements = std::move(params);
    desc.funcResult = std::make_shared<ArkTSTypeDesc>(result);
    return desc;
}

void FillPresetFuncDecl(ClassDecl& decl, const std::string& package)
{
    decl.identifier = "ArkTSFunc";
    decl.fullPackageName = package;
}
} // namespace

TEST_F(ArkTSTypeMappingTest, CarrierClassifiesWhenFunctionConstraintsHold)
{
    auto int32 = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT32);
    auto boolean = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    ClassDecl presetDecl;
    FillPresetFuncDecl(presetDecl, "std.interop.arkts");
    auto& preset = presetDecl;
    FuncTy func({ModalTy{int32}}, ModalTy{boolean});
    ClassTy carrier("ArkTSFunc", preset, {&func});
    auto desc = ArkTSClassifyType(&carrier);
    ASSERT_EQ(desc.kind, ArkTSTypeDesc::Kind::ARKTS_FUNC);
    EXPECT_EQ(desc.reference, &preset);
    ASSERT_EQ(desc.elements.size(), 1);
    EXPECT_EQ(desc.elements.front().scalar, "Int32");
    EXPECT_EQ(desc.FuncResult().scalar, "Bool");
    EXPECT_TRUE(ArkTSTypeDescGeneratable(desc));
    EXPECT_EQ(ArkTSFuncCarrierRejectReason(&carrier), ArkTSFuncCarrierReject::NONE);
}

TEST_F(ArkTSTypeMappingTest, CarrierRejectsNonFunctionArgument)
{
    auto int32 = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT32);
    ClassDecl presetDecl;
    FillPresetFuncDecl(presetDecl, "std.interop.arkts");
    auto& preset = presetDecl;
    ClassTy carrier("ArkTSFunc", preset, {int32});
    EXPECT_EQ(ArkTSClassifyType(&carrier).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
    EXPECT_EQ(ArkTSFuncCarrierRejectReason(&carrier), ArkTSFuncCarrierReject::NOT_A_FUNCTION);
    // A same-named user class keeps the generic routing (NONE here too: the
    // dedicated messages are preset-pinned).
    ClassDecl userDecl;
    FillPresetFuncDecl(userDecl, "application");
    auto& user = userDecl;
    ClassTy userCarrier("ArkTSFunc", user, {int32});
    EXPECT_EQ(ArkTSClassifyType(&userCarrier).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
    EXPECT_EQ(ArkTSFuncCarrierRejectReason(&userCarrier), ArkTSFuncCarrierReject::NONE);
}

TEST_F(ArkTSTypeMappingTest, CarrierRejectsUnsupportedMemberAndOuterGeneric)
{
    auto boolean = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);
    auto intNative = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT_NATIVE);
    ClassDecl presetDecl;
    FillPresetFuncDecl(presetDecl, "std.interop.arkts");
    auto& preset = presetDecl;
    FuncTy badMember({ModalTy{intNative}}, ModalTy{boolean});
    ClassTy badMemberCarrier("ArkTSFunc", preset, {&badMember});
    EXPECT_EQ(ArkTSClassifyType(&badMemberCarrier).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
    EXPECT_EQ(ArkTSFuncCarrierRejectReason(&badMemberCarrier), ArkTSFuncCarrierReject::UNSUPPORTED_MEMBER);

    GenericParamDecl param;
    GenericsTy typeVar("T", param);
    FuncTy generic({ModalTy{&typeVar}}, ModalTy{boolean});
    ClassTy genericCarrier("ArkTSFunc", preset, {&generic});
    EXPECT_EQ(ArkTSClassifyType(&genericCarrier).kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
    EXPECT_EQ(ArkTSFuncCarrierRejectReason(&genericCarrier), ArkTSFuncCarrierReject::OUTER_GENERIC);
}

TEST_F(ArkTSTypeMappingTest, ConstructionGlueConvertsEveryPosition)
{
    ClassDecl presetDecl;
    FillPresetFuncDecl(presetDecl, "std.interop.arkts");
    auto& preset = presetDecl;
    auto carrier = CarrierDescForTest(&preset, {ScalarDesc("Int32"), ScalarDesc("String")}, ScalarDesc("Bool"));
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto code = ArkTSFuncConstructionSource(carrier, "__cj_arkts_fc_", classRefs);
    ASSERT_FALSE(code.empty());
    EXPECT_NE(code.find("return ArkTSFunc<(Int32, String) -> Bool>("), std::string::npos);
    EXPECT_NE(code.find("cj_arkts_context_v1.__cj_arkts_current().function("), std::string::npos);
    EXPECT_NE(code.find("let __cj_arkts_fc_raw_0 = __cj_arkts_fc_info[0]"), std::string::npos);
    EXPECT_NE(code.find("let __cj_arkts_fc_raw_1 = __cj_arkts_fc_info[1]"), std::string::npos);
    EXPECT_NE(code.find("throw ExternConversionException(\"ArkTS type mismatch: null for Int32\")"), std::string::npos);
    EXPECT_NE(code.find("__cj_arkts_fc_fn(__cj_arkts_fc_arg_0, __cj_arkts_fc_arg_1)"), std::string::npos);
    EXPECT_NE(code.find("__cj_arkts_fc_ctx.boolean(__cj_arkts_fc_result_0).toJSValue()"), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}

TEST_F(ArkTSTypeMappingTest, ConstructionGlueHandlesZeroParameterUnitResult)
{
    ClassDecl presetDecl;
    FillPresetFuncDecl(presetDecl, "std.interop.arkts");
    auto& preset = presetDecl;
    auto carrier = CarrierDescForTest(&preset, {}, ScalarDesc("Unit"));
    std::unordered_map<std::string, Ptr<Decl>> classRefs;
    auto code = ArkTSFuncConstructionSource(carrier, "__cj_arkts_fc_", classRefs);
    ASSERT_FALSE(code.empty());
    EXPECT_NE(code.find("return ArkTSFunc<() -> Unit>("), std::string::npos);
    EXPECT_NE(code.find("__cj_arkts_fc_fn()"), std::string::npos);
    EXPECT_NE(code.find("__cj_arkts_fc_ctx.undefined().toJSValue()"), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}
