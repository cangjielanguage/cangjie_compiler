# Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

add_executable(ParserTest ParserTest.cpp ParserTest2.cpp)
target_link_libraries(
    ParserTest
    cangjie-lsp
    ${CMAKE_DL_LIBS}
    GTest::gtest
    GTest::gtest_main)
add_test(NAME ParserTest COMMAND ParserTest)

add_executable(ParseTypeTest ParseTypeTest.cpp)
target_link_libraries(
    ParseTypeTest
    cangjie-lsp
    ${CMAKE_DL_LIBS}
    GTest::gtest
    GTest::gtest_main)
add_test(NAME ParseTypeTest COMMAND ParseTypeTest)

add_executable(ParseTypeAliasTest ParseTypeAliasTest.cpp)
target_link_libraries(
    ParseTypeAliasTest
    cangjie-lsp
    ${CMAKE_DL_LIBS}
    GTest::gtest
    GTest::gtest_main)
add_test(NAME ParseTypeAliasTest COMMAND ParseTypeAliasTest)

add_executable(ParseCommentTest ParseCommentTest.cpp ParseCommentTest2.cpp)
target_link_libraries(
    ParseCommentTest
    cangjie-lsp
    ${LINK_LIBS}
    boundscheck-static
    GTest::gtest
    GTest::gtest_main
    TestCompilerInstanceObject)
add_test(NAME ParseCommentTest COMMAND ParseCommentTest)

add_executable(CheckCjdTest CheckCjdTest.cpp)
target_link_libraries(
    CheckCjdTest
    cangjie-lsp
    ${CMAKE_DL_LIBS}
    GTest::gtest
    GTest::gtest_main)
add_test(NAME CheckCjdTest COMMAND CheckCjdTest)

add_executable(ParseIncrTest ParseIncrTest.cpp)
target_link_libraries(
    ParseIncrTest
    cangjie-lsp
    ${CMAKE_DL_LIBS}
    boundscheck-static
    GTest::gtest
    GTest::gtest_main
    TestCompilerInstanceObject)
add_test(NAME ParseIncrTest COMMAND ParseIncrTest)

add_executable(ParseOuterInterfaceTest ParseOuterInterfaceTest.cpp)
target_link_libraries(
    ParseOuterInterfaceTest
    cangjie-lsp
    ${CMAKE_DL_LIBS}
    boundscheck-static
    GTest::gtest
    GTest::gtest_main
    TestCompilerInstanceObject)
add_test(NAME ParseOuterInterfaceTest COMMAND ParseOuterInterfaceTest)

# These tests exercise the ArkTS interop parse behavior: the five ArkTS-only
# annotation names are builtin only in a capability build targeting OHOS, so they
# are only meaningful (and only pass) when CANGJIE_ENABLE_ARKTS_INTEROP is ON.
if(CANGJIE_ENABLE_ARKTS_INTEROP)
    add_executable(ArkTSAnnotationTest ArkTSAnnotationTest.cpp)
    target_link_libraries(
        ArkTSAnnotationTest cangjie-lsp ${CMAKE_DL_LIBS} boundscheck-static GTest::gtest GTest::gtest_main)
    target_include_directories(ArkTSAnnotationTest PRIVATE ${FLATBUFFERS_INCLUDE_DIR})
    add_test(NAME ArkTSAnnotationTest COMMAND ArkTSAnnotationTest)
endif()

add_executable(ArkTSTypeMappingTest ArkTSTypeMappingTest.cpp)
target_link_libraries(ArkTSTypeMappingTest cangjie-lsp ${CMAKE_DL_LIBS} boundscheck-static GTest::gtest GTest::gtest_main)
add_test(NAME ArkTSTypeMappingTest COMMAND ArkTSTypeMappingTest)
