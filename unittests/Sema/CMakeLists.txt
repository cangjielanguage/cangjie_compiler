# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

add_executable(TypeCheckerTest TypeCheckerTest.cpp ${CANGJIE_SRC_OBJECTS})
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
