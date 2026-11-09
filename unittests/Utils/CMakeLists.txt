# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

include_directories(${CMAKE_SOURCE_DIR}/src/Driver/Toolchains)
set(BASE_UTIL_SRC
    $<TARGET_OBJECTS:CangjieASTCommon>
    $<TARGET_OBJECTS:CangjieCommonUtil>
    $<TARGET_OBJECTS:CangjieBasic>
    $<TARGET_OBJECTS:CangjieTempFilesUtil>
    $<TARGET_OBJECTS:CangjieOption>
    $<TARGET_OBJECTS:CangjieUnicodeUtils>
    $<TARGET_OBJECTS:CangjieLex>
)

add_executable(UtilsTests
    UtilsTests.cpp
    FileUtilTests.cpp
    ProfileRecorderTests.cpp
    SafePointerTests.cpp
    SemaphoreTests.cpp
    StdUtilsTests.cpp
    TaskQueueTests.cpp)
# ProfileRecorderTests drives the recorders through their own headers, which are private to src/Utils.
target_include_directories(UtilsTests PRIVATE ${CMAKE_SOURCE_DIR}/src/Utils)
if(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
target_link_libraries(
    UtilsTests
    ${CMAKE_DL_LIBS}
    ${BASE_UTIL_SRC}
    $<TARGET_OBJECTS:CangjieProfileUtils>
    GTest::gtest
    GTest::gtest_main)
endif()
add_test(NAME UtilsTests COMMAND UtilsTests)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(CANGJIE_BASH_EXECUTABLE bash)
    if(CANGJIE_BASH_EXECUTABLE)
        add_test(
            NAME RuntimeEnvSetupTest
            COMMAND ${CANGJIE_BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/RuntimeEnvSetupTest.sh
                ${CMAKE_SOURCE_DIR}/utils/envsetup/runtime)
    endif()
endif()

set(CAST_TEST_SRC CastASTTests.cpp CastTyTests.cpp)
set(CAST_TEST_LIB cangjie-lsp ${CMAKE_DL_LIBS})


#add_executable(
#    CastTest
#    ${CAST_TEST_SRC})
#target_link_libraries(
#    CastTest
#    cangjie-lsp
#    ${CAST_TEST_LIB}
#    GTest::gtest
 #   GTest::gtest_main)
#set_target_properties(CastTest PROPERTIES COMPILE_FLAGS "-DUT=1")
#add_test(NAME CastTest COMMAND CastTest)

if(CMAKE_BUILD_TYPE MATCHES Release)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fomit-frame-pointer")
    add_definitions(-DRELEASE)

    add_executable(SignalTests SignalTests.cpp)
    target_link_libraries(
        SignalTests
        ${CMAKE_DL_LIBS}
        ${BASE_UTIL_SRC}
        GTest::gtest
        GTest::gtest_main)
    add_test(NAME SignalTests COMMAND SignalTests)

    add_executable(SignalTestCJC SignalTestCJC.cpp ${CANGJIE_SRC_OBJECTS})
    target_link_libraries(SignalTestCJC ${LINK_LIBS})
endif()

add_subdirectory(UnicodeTest)
