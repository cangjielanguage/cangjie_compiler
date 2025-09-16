# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

add_executable(OptionTest OptionTest.cpp OptionTableTest.cpp ${CANGJIE_SRC_OBJECTS})
if(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    set(CJNATIVE_BACKEND "cjnative")
    string(TOLOWER ${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}_${CJNATIVE_BACKEND} output_lib_dir)
    set(RUNTIME_LIB_DIR ${CMAKE_BINARY_DIR}/lib/${output_lib_dir})
endif()
target_link_libraries(
    OptionTest
    ${LINK_LIBS}
    GTest::gtest
    GTest::gtest_main)
add_test(NAME OptionTest COMMAND OptionTest)
