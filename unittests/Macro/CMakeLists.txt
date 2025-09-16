# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

include_directories(${CMAKE_SOURCE_DIR}/src/Sema)
if(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    add_executable(TokenSerializationTest TokenSerializationTest.cpp)
    add_executable(NodeSerializationTest NodeSerializationTest.cpp)
    add_executable(MacroTest MacroTest.cpp)

    target_link_libraries(
        TokenSerializationTest
        cangjie-lsp
        ${LINK_LIBS}
        boundscheck-static
        GTest::gtest
        GTest::gtest_main)
    target_link_libraries(
        NodeSerializationTest
        cangjie-lsp
        ${LINK_LIBS}
        boundscheck-static
        GTest::gtest
        GTest::gtest_main)
    target_link_libraries(
        MacroTest
        cangjie-lsp
        ${LINK_LIBS}
        boundscheck-static
        GTest::gtest
        GTest::gtest_main
        TestCompilerInstanceObject)

    add_dependencies(NodeSerializationTest CangjieFlatbuffersHeaders)
    target_include_directories(NodeSerializationTest PRIVATE ${FLATBUFFERS_INCLUDE_DIR})

    add_test(NAME TokenSerializationTest COMMAND TokenSerializationTest)
    add_test(NAME NodeSerializationTest COMMAND NodeSerializationTest)
    add_test(NAME MacroTest COMMAND MacroTest)

endif()
