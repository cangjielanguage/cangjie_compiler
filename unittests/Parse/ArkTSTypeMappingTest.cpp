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
        Parser parser(code, diag, sm);
        auto file = parser.ParseTopLevel();
        diag.EmitCategoryDiagnostics(DiagCategory::PARSE);
        return file;
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
    // Every ArkTSArray-named class classifies as unsupported - the stdlib
    // preset std.interop.arkts included - because reference semantics are not
    // part of this version. The Sema diagnostics own the rejection, and the
    // package-pinned routing helper picks the deferral message for the preset.
    ClassDecl arkTsArrayDecl;
    arkTsArrayDecl.identifier = "ArkTSArray";
    arkTsArrayDecl.fullPackageName = "std.interop.arkts";

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

    // The stdlib preset itself: unsupported with no elements (the
    // reference-semantics classification is gone).
    ClassTy refInt32("ArkTSArray", arkTsArrayDecl, {int32});
    auto reference = ArkTSClassifyType(&refInt32);
    EXPECT_EQ(reference.kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
    EXPECT_TRUE(reference.elements.empty());

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

    // An ArkTSArray element poisons the whole tuple (foreign-side rule:
    // one unsupported element leaves the position unsupported).
    TupleTy mixed({&optionInt32, &refInt32});
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

    // Every ArkTSArray-named class is unsupported, the stdlib preset included
    // (reference semantics are out of scope). A class that merely shares the
    // name in another package - a user package or the arkts.lang facade - is
    // unsupported as well: classification never matches a declaration name, so
    // a same-named class can only reach the generic rejection and never the
    // preset deferral routing.
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
    EXPECT_EQ(ArkTSDeferredExternalTypeName(&presetArrayInt32), "ArkTSArray");
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
    EXPECT_EQ(ArkTSDeferredExternalTypeName(&arrayOfPreset), "ArkTSArray");
    EXPECT_EQ(ArkTSDeferredExternalTypeName(&arrayOfUser), "");
    EXPECT_EQ(ArkTSDeferredExternalTypeName(nullptr), "");
    // Recognition only routes diagnostics: the position stays unsupported.
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
    // asUndefined() probe and no try/catch, everything else reaches
    // Option.Some with the element's own checks.
    EXPECT_NE(code.find("if (__locals_result.isNull() || __locals_result.isUndefined())"), std::string::npos);
    EXPECT_NE(code.find("Option.Some(__locals_result.asString().toString())"), std::string::npos);
    EXPECT_EQ(code.find("asUndefined"), std::string::npos);
    EXPECT_EQ(code.find("catch"), std::string::npos);
    // The placeholder and rebinding contract of the wrapper survives.
    EXPECT_NE(code.find("__module__"), std::string::npos);
    EXPECT_NE(code.find("__namespace_0__"), std::string::npos);
    EXPECT_NE(code.find("__name__"), std::string::npos);
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

TEST_F(ArkTSTypeMappingTest, ArkTSArrayPositionsRefuseGenerationAndArrayCopyStillGenerates)
{
    // Every ArkTSArray-named class - the stdlib preset included - classifies
    // as unsupported, so a signature position carrying it refuses generation
    // (the Sema diagnostics own the rejection; the empty string is the
    // generator's defense in depth). The Array<T'> copy path is untouched.
    auto int32 = TypeManager::GetPrimitiveTy(TypeKind::TYPE_INT32);
    ClassDecl arkTsArrayDecl;
    arkTsArrayDecl.identifier = "ArkTSArray";
    arkTsArrayDecl.fullPackageName = "std.interop.arkts";
    ClassTy refInt32("ArkTSArray", arkTsArrayDecl, {int32});
    auto reference = ArkTSClassifyType(&refInt32);
    ASSERT_EQ(reference.kind, ArkTSTypeDesc::Kind::UNSUPPORTED);
    EXPECT_TRUE(ArkTSForeignWrapperSource({reference}, reference, 0, "__locals_").empty());
    EXPECT_TRUE(ArkTSForeignWrapperSource({reference}, ScalarDesc("Int32"), 0, "__locals_").empty());
    EXPECT_TRUE(ArkTSForeignWrapperSource({}, reference, 0, "__locals_").empty());
    StructDecl arrayDecl;
    arrayDecl.identifier = "Array";
    arrayDecl.fullPackageName = "std.core";
    StructTy arrayInt32("Array", arrayDecl, {int32});
    auto copy = ArkTSClassifyType(&arrayInt32);
    ASSERT_EQ(copy.kind, ArkTSTypeDesc::Kind::ARRAY_COPY);
    auto code = ArkTSForeignWrapperSource({copy}, copy, 0, "__locals_");
    EXPECT_FALSE(code.empty());
    EXPECT_NE(code.find("let __locals_elems_0 = Array<ark_interop.JSValue>(__argument_0.size, {"), std::string::npos);
    EXPECT_NE(code.find("let __locals_arg_0 = __locals_context.array(__locals_elems_0).toJSValue()"),
        std::string::npos);
    EXPECT_NE(code.find("let __locals_copy_0 = Array<Int32>("), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ExpectSingleCall(file.get(), "array copy wrapper after the flip");
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
        EXPECT_NE(code.find("throw ExternConversionException(\"ArkTS type mismatch: " + message + "\")"),
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
    // predicates; no wrapper probes with a consuming asUndefined() or leans
    // on exception control flow (catch), for Option and Unit alike.
    auto optionCode = ArkTSForeignWrapperSource({}, WrappedDesc(ArkTSTypeDesc::Kind::OPTIONAL, ScalarDesc("Bool")),
        0, "__locals_");
    EXPECT_NE(optionCode.find("__locals_result.isNull() || __locals_result.isUndefined()"), std::string::npos);
    EXPECT_NE(optionCode.find("Option.Some(__locals_result.toBoolean())"), std::string::npos);
    auto unitCode = ArkTSForeignWrapperSource({}, ScalarDesc("Unit"), 0, "__locals_");
    EXPECT_NE(unitCode.find("__locals_result.isNull() || __locals_result.isUndefined()"), std::string::npos);
    EXPECT_NE(unitCode.find("not null or undefined for Unit"), std::string::npos);
    for (const auto& code : {optionCode, unitCode}) {
        EXPECT_EQ(code.find("asUndefined"), std::string::npos);
        EXPECT_EQ(code.find("catch"), std::string::npos);
        auto file = Parse(code);
        ASSERT_EQ(diag.GetErrorCount(), 0);
        ExpectSingleCall(file.get(), "undefined predicate wrapper");
    }
}

TEST_F(ArkTSTypeMappingTest, SeamPlaceholderNamesNeverReturn)
{
    // Negative guard: the five seam placeholders were replaced by the real
    // ohos.ark_interop API face; none of the compiler-owned helper names
    // may reappear in any generated wrapper, whatever the shape.
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
    EXPECT_NE(code.find("let __locals_module = __locals_context.requireArkModule(\"__module__\")"), std::string::npos);
    EXPECT_NE(code.find("let __locals_result = __locals_module.callMethod("), std::string::npos);
    EXPECT_NE(code.find("__module__"), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
    ExpectSingleCall(file.get(), "module wrapper");
    auto namespaced = ArkTSForeignWrapperSource({}, ScalarDesc("Unit"), 1, "__locals_");
    EXPECT_NE(namespaced.find("let __locals_module = __locals_context.requireArkModule(\"__module__\")"),
        std::string::npos);
    EXPECT_NE(namespaced.find("__locals_module.getProperty(\"__namespace_0__\")"), std::string::npos);
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
    // The scalar-name overload was removed, so the scalar contract is carried
    // by leaf SCALAR descriptions through the same generator; the whitelist
    // refusal of an unknown name becomes the generatability refusal of a
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
    EXPECT_NE(bound.find("__name__"), std::string::npos);
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
    // Unknown scalar names no longer produce a wrapper either: a hand-built
    // SCALAR description with a non-whitelisted name fails the generatable
    // guard, the desc-driven successor of the old unknown-name refusal.
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
        ArkTSMemberReceiver::INSTANCE, ArkTSReceiverKind::MODULE, classRefs);
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
        ArkTSMemberReceiver::INSTANCE, ArkTSReceiverKind::MODULE, classRefs);
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
        ArkTSReceiverKind::MODULE, classRefs);
    ASSERT_FALSE(code.empty());
    EXPECT_NE(code.find("asClass()"), std::string::npos);
    EXPECT_NE(code.find(".new("), std::string::npos);
    EXPECT_NE(code.find("__cj_arkts_ref = "), std::string::npos);
    EXPECT_NE(code.find("asObject()"), std::string::npos);
    auto file = Parse(code);
    ASSERT_EQ(diag.GetErrorCount(), 0);
}
