# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

set(INCREMENTALCOMPILATIONLOGGER_UT_SRC
    $<TARGET_OBJECTS:CangjieASTCommon>
    $<TARGET_OBJECTS:CangjieCommonUtil>
    $<TARGET_OBJECTS:CangjieBasic>
    $<TARGET_OBJECTS:CangjieTempFilesUtil>
    $<TARGET_OBJECTS:CangjieOption>
    $<TARGET_OBJECTS:CangjieProfileUtils>
    $<TARGET_OBJECTS:CangjieUnicodeUtils>
    $<TARGET_OBJECTS:CangjieLex>
)
add_executable(IncrementalCompilationLoggerTest IncrementalCompilationLoggerTest.cpp)

target_link_libraries(
    IncrementalCompilationLoggerTest
    ${CMAKE_DL_LIBS}
    ${INCREMENTALCOMPILATIONLOGGER_UT_SRC}
    GTest::gtest
    GTest::gtest_main)
add_test(NAME IncrementalCompilationLoggerTest COMMAND IncrementalCompilationLoggerTest)
