# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

include_directories(${CMAKE_SOURCE_DIR}/src/Sema)

set(BASE_UTIL_SRC
    $<TARGET_OBJECTS:CangjieASTCommon>
    $<TARGET_OBJECTS:CangjieCommonUtil>
    $<TARGET_OBJECTS:CangjieBasic>
    $<TARGET_OBJECTS:CangjieTempFilesUtil>
    $<TARGET_OBJECTS:CangjieOption>
    $<TARGET_OBJECTS:CangjieLex>
    $<TARGET_OBJECTS:CangjieUnicodeUtils>
)

add_executable(SourceManagerTest SourceManagerTest.cpp)
target_link_libraries(
    SourceManagerTest
    ${CMAKE_DL_LIBS}
    ${BASE_UTIL_SRC}
    GTest::gtest
    GTest::gtest_main)
add_test(NAME SourceManagerTest COMMAND SourceManagerTest)

add_executable(EngineTest EngineTest.cpp ${PROJECT_SOURCE_DIR}/src/IncrementalCompilation/Utils.cpp)
target_link_libraries(
    EngineTest
    ${CMAKE_DL_LIBS}
    ${BASE_UTIL_SRC}
    $<TARGET_OBJECTS:CangjieParse>
    GTest::gtest
    GTest::gtest_main)
add_test(NAME EngineTest COMMAND EngineTest)

add_executable(ErrorMessageTest ErrorMessageTest.cpp ${PROJECT_SOURCE_DIR}/src/IncrementalCompilation/Utils.cpp)
target_link_libraries(
    ErrorMessageTest
    ${CMAKE_DL_LIBS}
    ${BASE_UTIL_SRC}
    $<TARGET_OBJECTS:CangjieParse>
    GTest::gtest
    GTest::gtest_main)
add_test(NAME ErrorMessageTest COMMAND ErrorMessageTest)

add_executable(UtilsTest UtilsTest.cpp UtilsTest.cpp)
target_link_libraries(
    UtilsTest
    ${CMAKE_DL_LIBS}
    ${BASE_UTIL_SRC}
    GTest::gtest
    GTest::gtest_main)
add_test(NAME UtilsTest COMMAND UtilsTest)

add_executable(MacroCallDiagInfoTest MacroCallDiagInfoTest.cpp)
target_link_libraries(
    MacroCallDiagInfoTest
    ${CMAKE_DL_LIBS}
    ${BASE_UTIL_SRC}
    GTest::gtest
    GTest::gtest_main)
add_test(NAME MacroCallDiagInfoTest COMMAND MacroCallDiagInfoTest)

add_executable(DiagnosticEmitterTest DiagnosticEmitterTest.cpp)
target_link_libraries(
    DiagnosticEmitterTest
    ${CMAKE_DL_LIBS}
    ${BASE_UTIL_SRC}
    GTest::gtest
    GTest::gtest_main)
add_test(NAME DiagnosticEmitterTest COMMAND DiagnosticEmitterTest)

if(WIN32)
    add_executable(StringConvertorTest StringConvertorTest.cpp StringConvertorTest.cpp)
    target_link_libraries(
        StringConvertorTest
        ${CMAKE_DL_LIBS}
        ${BASE_UTIL_SRC}
        GTest::gtest
        GTest::gtest_main)
    add_test(NAME StringConvertorTest COMMAND StringConvertorTest)
endif()
