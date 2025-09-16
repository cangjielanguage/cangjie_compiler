# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

set(LEX_UT_SRC
    $<TARGET_OBJECTS:CangjieASTCommon>
    $<TARGET_OBJECTS:CangjieCommonUtil>
    $<TARGET_OBJECTS:CangjieBasic>
    $<TARGET_OBJECTS:CangjieTempFilesUtil>
    $<TARGET_OBJECTS:CangjieOption>
    $<TARGET_OBJECTS:CangjieLex>
    $<TARGET_OBJECTS:CangjieProfileUtils>
    $<TARGET_OBJECTS:CangjieUnicodeUtils>
)

add_executable(LexerTest LexerTest.cpp)
target_link_libraries(
    LexerTest
    ${CMAKE_DL_LIBS}
    ${LEX_UT_SRC}
    GTest::gtest
    GTest::gtest_main)
add_test(NAME LexerTest COMMAND LexerTest)

add_executable(TotalTest TotalTest.cpp)
target_link_libraries(
    TotalTest
    ${CMAKE_DL_LIBS}
    ${LEX_UT_SRC}
    GTest::gtest
    GTest::gtest_main)
add_test(NAME TotalTest COMMAND TotalTest)

add_executable(LexerTerminatorTest LexerTerminatorTest.cpp)
target_link_libraries(
    LexerTerminatorTest
    ${CMAKE_DL_LIBS}
    ${LEX_UT_SRC}
    GTest::gtest
    GTest::gtest_main)
add_test(NAME LexerTerminatorTest COMMAND LexerTerminatorTest)
