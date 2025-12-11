# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

file(GLOB CG_TEST_SRC *.cpp)

add_executable(CodeGenTests
    ${CG_TEST_SRC}
    ${CMAKE_SOURCE_DIR}/src/Basic/Print.cpp)

target_link_libraries(
    CodeGenTests
    cangjie-frontend
    GTest::gtest
    GTest::gtest_main)

add_test(NAME CodeGenTests COMMAND CodeGenTests)

if(CANGJIE_DOWNLOAD_DEPS)
    add_dependencies(CodeGenTests binary-deps)
elseif(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    add_dependencies(CodeGenTests cjnative)
endif()

target_include_directories(CodeGenTests PRIVATE ${CMAKE_SOURCE_DIR}/src/CodeGen)
target_include_directories(CodeGenTests PRIVATE ${LLVM_INCLUDE_DIRS})
