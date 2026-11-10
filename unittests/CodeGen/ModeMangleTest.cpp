// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * Unit tests for the modal (local-mode) part of the mangling pipeline:
 *  - MangleUtils::MangleModePayload / MangleTypeMode / MangleThisMode / WithModal
 *    (the <mode-set> encoding `Q<payload>E` / `W<payload>E` of scheme B in
 *    cangjie-mangling-modal-design.md),
 *  - modal suffixes appended by the CHIR type mangler (CHIRTypeManglingUtils.cpp) for
 *    every type kind, including the direct-dispatch MangleArraySliceType,
 *  - the closure-conversion generic base class name (CHIRManglingUtils.cpp) whose
 *    axis-degree letters are now `L`/`l` instead of `!`/`?`.
 */

#include "CGTest.h"

#include "cangjie/AST/ModalInfo.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Mangle/CHIRManglingUtils.h"
#include "cangjie/Mangle/CHIRTypeManglingUtils.h"

using namespace Cangjie;
using namespace Cangjie::MangleUtils;
using namespace Cangjie::CHIR;
using TypeKind = Cangjie::CHIR::Type::TypeKind;

// MangleArraySliceType is an internal (non-header) entry of CHIRTypeManglingUtils.cpp;
// declare it here so the unit test can pin its direct-dispatch encoding.
namespace Cangjie::CHIR {
std::string MangleArraySliceType(
    const CHIR::Type& t, const std::vector<std::string>& genericsTypeStack, bool useGenericName = false);
} // namespace Cangjie::CHIR
using Cangjie::CHIR::MangleArraySliceType;

// ---------------------------------------------------------------------------
// <payload> / <type-mode> / <this-mode> encoding (scheme B).
// ---------------------------------------------------------------------------

TEST(MangleModeSetTest, PayloadForASTModal)
{
    // Only the local axis is implemented: NOT emits nothing, HALF emits 'l', FULL emits 'L'.
    EXPECT_EQ(MangleModePayload(Cangjie::ModalInfo{Cangjie::Mode::NOT}), "");
    EXPECT_EQ(MangleModePayload(Cangjie::ModalInfo{Cangjie::Mode::HALF}), "l");
    EXPECT_EQ(MangleModePayload(Cangjie::ModalInfo{Cangjie::Mode::FULL}), "L");
}

TEST(MangleModeSetTest, PayloadForCHIRModal)
{
    // The CHIR entry points convert into the payload letters internally, so verify the
    // same L/l mapping through the <type-mode> leader form.
    EXPECT_EQ(MangleTypeMode(CHIR::ModalInfo{CHIR::Mode::NONE}), "");
    EXPECT_EQ(MangleTypeMode(CHIR::ModalInfo{CHIR::Mode::MAYBE}), "QlE");
    EXPECT_EQ(MangleTypeMode(CHIR::ModalInfo{CHIR::Mode::MUST}), "QLE");
}

TEST(MangleModeSetTest, TypeModeEncodesAsQLeader)
{
    // <type-mode> ::= Q<payload>E; empty when every axis is at its default degree so that
    // the mangled names of non-modal code stay byte-for-byte identical (ABI compat).
    EXPECT_EQ(MangleTypeMode(Cangjie::ModalInfo{Cangjie::Mode::NOT}), "");
    EXPECT_EQ(MangleTypeMode(Cangjie::ModalInfo{Cangjie::Mode::HALF}), "QlE");
    EXPECT_EQ(MangleTypeMode(Cangjie::ModalInfo{Cangjie::Mode::FULL}), "QLE");
}

TEST(MangleModeSetTest, ThisModeEncodesAsWLeader)
{
    // <this-mode> ::= W<payload>E; the W leader distinguishes a `this` mode from a data
    // type mode inside the H-separated parameter list of a function symbol.
    EXPECT_EQ(MangleThisMode(Cangjie::ModalInfo{Cangjie::Mode::NOT}), "");
    EXPECT_EQ(MangleThisMode(Cangjie::ModalInfo{Cangjie::Mode::HALF}), "WlE");
    EXPECT_EQ(MangleThisMode(Cangjie::ModalInfo{Cangjie::Mode::FULL}), "WLE");
}

TEST(MangleModeSetTest, WithModalAppendsTypeModeOnly)
{
    // WithModal is the single append point used by every type mangler: default modal
    // keeps the base intact, non-default appends the <type-mode> (never <this-mode>).
    EXPECT_EQ(WithModal(std::string("l"), Cangjie::ModalInfo{Cangjie::Mode::NOT}), "l");
    EXPECT_EQ(WithModal(std::string("l"), Cangjie::ModalInfo{Cangjie::Mode::HALF}), "lQlE");
    EXPECT_EQ(WithModal(std::string("CN1a5AlphaE"), Cangjie::ModalInfo{Cangjie::Mode::FULL}), "CN1a5AlphaEQLE");
    EXPECT_EQ(WithModal(std::string("T2_asiE"), CHIR::ModalInfo{CHIR::Mode::MUST}), "T2_asiEQLE");
}

TEST(MangleModeSetTest, PayloadCharactersStayInPortableSubset)
{
    // The design forbids `?`/`!` in mangled names (POSIX glob metacharacter hazard for
    // linker version scripts / objcopy --wildcard): only [A-Za-z0-9_$.] may appear.
    for (auto modal :
        {CHIR::ModalInfo{CHIR::Mode::NONE}, CHIR::ModalInfo{CHIR::Mode::MAYBE}, CHIR::ModalInfo{CHIR::Mode::MUST}}) {
        auto mode = MangleTypeMode(modal);
        for (auto ch : mode) {
            EXPECT_TRUE((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                ch == '_' || ch == '$' || ch == '.')
                << "illegal character '" << ch << "' in mode-set " << mode;
        }
    }
}

// ---------------------------------------------------------------------------
// Modal suffixes through the CHIR type mangler (CHIRTypeManglingUtils.cpp).
// ---------------------------------------------------------------------------

TEST_F(MangleTypeTest, ClassTypeWithModal)
{
    auto classDef = builder.CreateClass(defaultLoc, "Alpha", "_CN1a5AlphaE", "a", true, true);
    auto mustTy = builder.GetType<ClassType>(classDef, std::vector<Type*>{}, CHIR::ModalInfo{CHIR::Mode::MUST});
    EXPECT_EQ(MangleType(*mustTy), "CN1a5AlphaEQLE");
    auto maybeTy = builder.GetType<ClassType>(classDef, std::vector<Type*>{}, CHIR::ModalInfo{CHIR::Mode::MAYBE});
    EXPECT_EQ(MangleType(*maybeTy), "CN1a5AlphaEQlE");
    // Default modal must not change the historical encoding.
    auto plainTy = builder.GetType<ClassType>(classDef, std::vector<Type*>{}, CHIR::ModalInfo{});
    EXPECT_EQ(MangleType(*plainTy), "CN1a5AlphaE");
}

TEST_F(MangleTypeTest, StructTypeWithModal)
{
    auto structDef = builder.CreateStruct(defaultLoc, "Some", "_CN1a10SomeStructE", "a", true);
    auto mustTy = builder.GetType<StructType>(structDef, std::vector<Type*>{}, CHIR::ModalInfo{CHIR::Mode::MUST});
    EXPECT_EQ(MangleType(*mustTy), "RN1a10SomeStructEQLE");
}

TEST_F(MangleTypeTest, PrimitiveTypeWithModal)
{
    // `l` is Int64; the dispatcher appends the mode after the primitive body.
    auto mustI64 = builder.GetType<IntType>(Type::TypeKind::TYPE_INT64, CHIR::ModalInfo{CHIR::Mode::MUST});
    EXPECT_EQ(MangleType(*mustI64), "lQLE");
    auto maybeI64 = builder.GetType<IntType>(Type::TypeKind::TYPE_INT64, CHIR::ModalInfo{CHIR::Mode::MAYBE});
    EXPECT_EQ(MangleType(*maybeI64), "lQlE");
}

TEST_F(MangleTypeTest, TupleTypeWithModal)
{
    auto tupleTy = builder.GetType<TupleType>(std::vector<Type*>{int8Ty, int16Ty}, CHIR::ModalInfo{CHIR::Mode::MUST});
    // Tuple element count is encoded as N-1 (matches the existing TupleTypes test above).
    EXPECT_EQ(MangleType(*tupleTy), "T1_asEQLE");
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
TEST_F(MangleTypeTest, RawArrayTypeWithModal)
{
    auto rawArrayTy = builder.GetType<RawArrayType>(int64Ty, 1, CHIR::ModalInfo{CHIR::Mode::MUST});
    EXPECT_EQ(MangleType(*rawArrayTy), "A0_lQLE");
}

TEST_F(MangleTypeTest, VArrayTypeWithModal)
{
    auto vArrayTy = builder.GetType<VArrayType>(int64Ty, 4, CHIR::ModalInfo{CHIR::Mode::MAYBE});
    // VArray size is encoded as N-1 (same convention as tuple counts).
    EXPECT_EQ(MangleType(*vArrayTy), "V3_lQlE");
}
#endif

TEST_F(MangleTypeTest, RefTypeOfModalTypeKeepsModal)
{
    // A RefType forwards to its referenced type; the modal lives on the pointee, so a
    // reference to a local! object keeps the QLE suffix after stripping the ref.
    auto classDef = builder.CreateClass(defaultLoc, "Beta", "_CN1a4BetaE", "a", true, true);
    auto classTy = builder.GetType<ClassType>(classDef, std::vector<Type*>{}, CHIR::ModalInfo{CHIR::Mode::MUST});
    auto refTy = builder.GetType<RefType>(classTy);
    EXPECT_EQ(MangleType(*refTy), "CN1a4BetaEQLE");
}

TEST_F(MangleTypeTest, ArraySliceTypeWithModal)
{
    // MangleArraySliceType appends the <type-mode> itself (S_<elem>E<mode>): the historical
    // encoding emitted the raw `!`/`?` characters here via MangleLocalModifier, which are
    // forbidden in mangled names; it must now go through MangleTypeMode. The element is
    // read from GetTypeArgs()[0], so model the slice as a parameterized type.
    auto classDef = builder.CreateClass(defaultLoc, "SliceElem", "_CN1a9SliceElemE", "a", true, true);
    auto mustClassTy = builder.GetType<ClassType>(classDef, std::vector<Type*>{int64Ty}, CHIR::ModalInfo{CHIR::Mode::MUST});
    std::vector<std::string> genericsStack;
    EXPECT_EQ(MangleArraySliceType(*mustClassTy, genericsStack), "S_lEQLE");

    auto maybeClassTy = builder.GetType<ClassType>(classDef, std::vector<Type*>{int64Ty}, CHIR::ModalInfo{CHIR::Mode::MAYBE});
    EXPECT_EQ(MangleArraySliceType(*maybeClassTy, genericsStack), "S_lEQlE");

    auto plainClassTy = builder.GetType<ClassType>(classDef, std::vector<Type*>{int64Ty}, CHIR::ModalInfo{});
    EXPECT_EQ(MangleArraySliceType(*plainClassTy, genericsStack), "S_lE");
}

// ---------------------------------------------------------------------------
// Closure-conversion generic base class name: axis-degree letters are now
// uppercase/lowercase letters (L/l) instead of `!`/`?` characters.
// ---------------------------------------------------------------------------

TEST_F(MangleTypeTest, ClosureGenericBaseClassMangleNameWithModal)
{
    auto classDef = builder.CreateClass(defaultLoc, "Cap", "_CN1a3CapE", "a", true, true);
    auto mustClass = builder.GetType<ClassType>(classDef, std::vector<Type*>{}, CHIR::ModalInfo{CHIR::Mode::MUST});
    auto maybeClass = builder.GetType<ClassType>(classDef, std::vector<Type*>{}, CHIR::ModalInfo{CHIR::Mode::MAYBE});
    auto mustRef = builder.GetType<RefType>(mustClass);
    auto maybeRef = builder.GetType<RefType>(maybeClass);

    // Params (0: @local!, 1: @local?) with a non-modal return type. StripAllRefs must see
    // through the RefType, otherwise the axis degrees would be silently dropped.
    auto funcTy = builder.GetType<FuncType>(std::vector<Type*>{mustRef, maybeRef}, unitTy);
    EXPECT_EQ(Cangjie::CHIRMangling::ClosureConversion::GenerateGenericBaseClassMangleName(*funcTy), "$Cg2_0L_1l_");
}

TEST_F(MangleTypeTest, ClosureGenericBaseClassMangleNameReturnModal)
{
    auto classDef = builder.CreateClass(defaultLoc, "Ret", "_CN1a3RetE", "a", true, true);
    auto mustClass = builder.GetType<ClassType>(classDef, std::vector<Type*>{}, CHIR::ModalInfo{CHIR::Mode::MUST});
    auto mustRef = builder.GetType<RefType>(mustClass);

    // No params, modal return type: the `r` prefix marks the return-type axis degree.
    auto funcTy = builder.GetType<FuncType>(std::vector<Type*>{}, mustRef);
    EXPECT_EQ(Cangjie::CHIRMangling::ClosureConversion::GenerateGenericBaseClassMangleName(*funcTy), "$Cg0_rL_");

    // Non-modal closure types must keep producing the bare `$Cg<num>_` form.
    auto plainFuncTy = builder.GetType<FuncType>(std::vector<Type*>{int64Ty}, unitTy);
    EXPECT_EQ(Cangjie::CHIRMangling::ClosureConversion::GenerateGenericBaseClassMangleName(*plainFuncTy), "$Cg1_");
}
