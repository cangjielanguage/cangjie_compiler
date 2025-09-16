# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

add_executable(DriverTest DriverTest.cpp ToolchainTest.cpp TempFileManagerTest.cpp ${CANGJIE_SRC_OBJECTS})
target_link_libraries(
    DriverTest
    GTest::gtest
    GTest::gtest_main
    ${LINK_LIBS})
add_test(NAME DriverTest COMMAND DriverTest)