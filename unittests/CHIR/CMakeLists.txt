# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

if(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    add_executable(CHIRSerialzierTest CHIRSerialzierTest.cpp CHIRDeSerialzierTest.cpp)
    target_link_libraries(
        CHIRSerialzierTest
        cangjie-lsp
        ${LINK_LIBS}
        boundscheck-static
        GTest::gtest
        GTest::gtest_main
        TestCompilerInstanceObject)
    add_dependencies(CHIRSerialzierTest CangjieFlatbuffersHeaders)
    target_include_directories(CHIRSerialzierTest PRIVATE ${FLATBUFFERS_INCLUDE_DIR})
    add_test(NAME CHIRSerialzierTest COMMAND CHIRSerialzierTest)

    add_executable(CHIROptTests ActiveStatePoolTest.cpp AnalysisToStringTest.cpp ArithmeticTest.cpp
        ConstantRangeTest.cpp SIntDomainTest.cpp SIntTest.cpp)
    target_link_libraries(
        CHIROptTests
        cangjie-lsp
        ${LINK_LIBS}
        GTest::gtest
        GTest::gtest_main
    )
    add_test(NAME CHIROptTests COMMAND CHIROptTests)
endif()
