# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

add_executable(ParseJsonTest ParseJsonTest.cpp)
target_link_libraries(
    ParseJsonTest
    cangjie-lsp
    ${LINK_LIBS}
    GTest::gtest
    GTest::gtest_main)
add_test(NAME ParseJsonTest COMMAND ParseJsonTest)

add_executable(TypeCheckerTest TypeCheckerTest.cpp)
target_link_libraries(
    TypeCheckerTest
    cangjie-lsp
    ${LINK_LIBS}
    boundscheck-static
    GTest::gtest
    GTest::gtest_main
    TestCompilerInstanceObject)
add_test(NAME TypeCheckerTest COMMAND TypeCheckerTest)

add_executable(GenericTest GenericTest.cpp)
target_link_libraries(
    GenericTest
    cangjie-lsp
    ${LINK_LIBS}
    boundscheck-static
    GTest::gtest
    GTest::gtest_main
    TestCompilerInstanceObject)
add_test(NAME GenericTest COMMAND GenericTest)

add_executable(CommentsTest CommentsTest.cpp)
target_link_libraries(
    CommentsTest
    cangjie-lsp
    ${LINK_LIBS}
    boundscheck-static
    GTest::gtest
    GTest::gtest_main
    TestCompilerInstanceObject)
add_test(NAME CommentsTest COMMAND CommentsTest)

add_executable(APILevelVersionTest APILevelVersionTest.cpp)
target_link_libraries(
    APILevelVersionTest
    cangjie-lsp
    ${LINK_LIBS}
    boundscheck-static
    GTest::gtest
    GTest::gtest_main
    TestCompilerInstanceObject)
add_test(NAME APILevelVersionTest COMMAND APILevelVersionTest)

add_executable(GetTopOverriddenFuncDeclTest GetTopOverriddenFuncDeclTest.cpp)
target_link_libraries(
    GetTopOverriddenFuncDeclTest
    cangjie-lsp
    ${LINK_LIBS}
    boundscheck-static
    GTest::gtest
    GTest::gtest_main
    TestCompilerInstanceObject)
add_test(NAME GetTopOverriddenFuncDeclTest COMMAND GetTopOverriddenFuncDeclTest)
