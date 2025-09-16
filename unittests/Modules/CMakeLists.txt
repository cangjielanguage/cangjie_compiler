# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

add_executable(PackageTest PackageTest.cpp)

target_include_directories(PackageTest PRIVATE ${FLATBUFFERS_INCLUDE_DIR})
target_include_directories(PackageTest PRIVATE ${CMAKE_SOURCE_DIR}/src/IncrementalCompilation)

target_link_libraries(
    PackageTest
    cangjie-lsp
    ${LINK_LIBS}
    boundscheck-static
    GTest::gtest
    GTest::gtest_main
    TestCompilerInstanceObject)

add_test(NAME PackageTest COMMAND PackageTest)
