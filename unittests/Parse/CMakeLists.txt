# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

add_executable(ParserTest ParserTest.cpp)
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