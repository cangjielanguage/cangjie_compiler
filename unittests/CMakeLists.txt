# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

enable_testing()
include(CTest)
include_directories(${GTEST_INCLUDE_DIRS})
include_directories(TestCompilerInstance)
add_definitions(-DPROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

# use llvm
include_directories(${LLVM_INCLUDE_DIRS})
get_target_property(LINK_LIBS cjc LINK_LIBRARIES)

list(APPEND LINK_LIBS ffi)

if(CANGJIE_BUILD_TESTS)
    add_compile_definitions(CANGJIE_BUILD_TESTS)
    add_subdirectory(TestCompilerInstance)
    add_subdirectory(Driver)
    add_subdirectory(Frontend)
    add_subdirectory(Lex)
    add_subdirectory(Parse)
    add_subdirectory(AST)
    add_subdirectory(Utils)
    add_subdirectory(Sema)
    add_subdirectory(Option)
    add_subdirectory(Modules)
    add_subdirectory(Basic)
    add_subdirectory(Macro)
    add_subdirectory(ConditionalCompilation)
    add_subdirectory(IncrCompile)
    add_subdirectory(CHIR)
endif()
