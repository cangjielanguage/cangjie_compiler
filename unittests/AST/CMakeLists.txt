# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

include_directories(${CMAKE_SOURCE_DIR}/src/Sema)
include_directories(${CMAKE_SOURCE_DIR}/src/AST)

add_executable(WalkerTest WalkerTest.cpp)
target_link_libraries(
    WalkerTest
    ${CMAKE_DL_LIBS}
    cangjie-lsp
    GTest::gtest
    GTest::gtest_main)
add_test(NAME WalkerTest COMMAND WalkerTest)

add_executable(CloneTest CloneTest.cpp)
target_link_libraries(
    CloneTest
    ${CMAKE_DL_LIBS}
    $<TARGET_OBJECTS:CangjieASTExtra>
    cangjie-lsp
    GTest::gtest
    GTest::gtest_main)
add_test(NAME CloneTest COMMAND CloneTest)
add_executable(CreateTest CreateTest.cpp)
target_link_libraries(
    CreateTest
    ${CMAKE_DL_LIBS}
    $<TARGET_OBJECTS:CangjieASTExtra>
    cangjie-lsp
    GTest::gtest
    GTest::gtest_main)
add_test(NAME CreateTest COMMAND CreateTest)

add_executable(SearchTest SearchTest.cpp)
target_link_libraries(
    SearchTest
    cangjie-lsp
    ${LINK_LIBS}
    boundscheck-static
    GTest::gtest
    GTest::gtest_main
    TestCompilerInstanceObject)
add_test(NAME SearchTest COMMAND SearchTest)

add_executable(SearchPlusTest SearchPlusTest.cpp)
target_link_libraries(
    SearchPlusTest
    cangjie-lsp
    ${LINK_LIBS}
    boundscheck-static
    GTest::gtest
    GTest::gtest_main
    TestCompilerInstanceObject)
add_test(NAME SearchPlusTest COMMAND SearchPlusTest)

add_executable(ASTToSourceTest ASTToSourceTest.cpp)
target_link_libraries(
    ASTToSourceTest
    ${CMAKE_DL_LIBS}
    cangjie-lsp
    GTest::gtest
    GTest::gtest_main)
add_test(NAME ASTToSourceTest COMMAND ASTToSourceTest)
