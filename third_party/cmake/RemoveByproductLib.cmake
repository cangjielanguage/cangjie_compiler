# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

#[[
cmake -P
    CompileRTLib.cmake (arg 2)
    ${CMAKE_BINARY_DIR} (arg 3)
    ${THIRD_PARTY_LLVM} (arg 4)
]]
set(CMAKE_BINARY_DIR ${CMAKE_ARGV3})
set(THIRD_PARTY_LLVM ${CMAKE_ARGV4})
set(KEEP_CLANG_CPP_LIBS ${CMAKE_ARGV5})

# 1. Since other libraries will be built together with the specific library, we
#   should remove them.
file(GLOB CLANG_LIBS_TO_REMOVE ${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/lib/libclang*.*)

if(KEEP_CLANG_CPP_LIBS)
    list(REMOVE_ITEM CLANG_LIBS_TO_REMOVE
        "${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/lib/libclang.dylib"
        "${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/lib/libclang-cpp.dylib"
        "${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/lib/libclang-cpp.so.15"
        "${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/lib/libclang-cpp.so.15.0.4")
endif()

list(LENGTH CLANG_LIBS_TO_REMOVE CLANG_LIBS_TO_REMOVE_LENGTH)
if(CLANG_LIBS_TO_REMOVE_LENGTH GREATER 0)
    file(REMOVE ${CLANG_LIBS_TO_REMOVE})
endif()
if(EXISTS ${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/lib/clang)
    file(REMOVE_RECURSE ${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/lib/clang)
endif()

# 2. Remove clang related binaries
file(GLOB CLANG_BINS_TO_REMOVE ${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/bin/clang*)
list(LENGTH CLANG_BINS_TO_REMOVE CLANG_BINS_TO_REMOVE_LENGTH)
if(CLANG_BINS_TO_REMOVE_LENGTH GREATER 0)
    set(OTHER_CLANG_BINS_TO_REMOVE
        c-index-test
        diagtool
        flang
        git-clang-format
        hmaptool
        scan-build
        scan-view)
    foreach(BIN_NAME ${OTHER_CLANG_BINS_TO_REMOVE})
        list(APPEND CLANG_BINS_TO_REMOVE ${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/bin/${BIN_NAME})
    endforeach()

    file(REMOVE ${CLANG_BINS_TO_REMOVE})
endif()
