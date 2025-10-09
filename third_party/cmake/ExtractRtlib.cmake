# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

# cmake -P
# ExtractRtlib.cmake (arg 2)
# ${CMAKE_BINARY_DIR} (arg 3)
# ${SPECIFIC_LIB} (arg 4)
# ${NEW_SPECIFIC_LIB_PATH} (arg 5)
# ${CANGJIE_TARGET_ARCH} (arg 6)
# ${CROSS_COMPILING} (arg 7)
# ${THIRD_PARTY_LLVM} (arg 8)
# ${IOS} (arg 9)
set(CMAKE_BINARY_DIR ${CMAKE_ARGV3})
set(SPECIFIC_LIB ${CMAKE_ARGV4})
set(NEW_SPECIFIC_LIB_PATH ${CMAKE_ARGV5})
set(CANGJIE_TARGET_ARCH ${CMAKE_ARGV6})
set(CANGJIE_BUILD_CJC ${CMAKE_ARGV7})
set(THIRD_PARTY_LLVM ${CMAKE_ARGV8})
set(IOS ${CMAKE_ARGV9})

set(LIB_SUFFIX ".a")

# Create the specific library
# 1. Find the path of the specific library compiled from llvm compiler-rt
if(CANGJIE_BUILD_CJC) # It is native-compiling
    # asan/tsan has multiple product, select precisely
    if(("asan" STREQUAL "${SPECIFIC_LIB}") OR ("tsan" STREQUAL "${SPECIFIC_LIB}"))
        file(GLOB OLD_SPECIFIC_LIB_PATHS
            ${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/lib/clang/15.0.4/lib/*/libclang_rt.${SPECIFIC_LIB}-${CANGJIE_TARGET_ARCH}${LIB_SUFFIX})
        if(NOT "${OLD_SPECIFIC_LIB_PATHS}")
            file(GLOB OLD_SPECIFIC_LIB_PATHS
                 ${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/lib/clang/15.0.4/lib/*/libclang_rt.${SPECIFIC_LIB}${LIB_SUFFIX})
        endif()
    else()
        file(GLOB OLD_SPECIFIC_LIB_PATHS
             ${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/lib/clang/15.0.4/lib/*/libclang_rt.${SPECIFIC_LIB}*${LIB_SUFFIX})
    endif()
    foreach(f ${OLD_SPECIFIC_LIB_PATHS})
        if(f MATCHES ".*${CANGJIE_TARGET_ARCH}.*\\.a" OR f MATCHES ".*darwin.*\\.a")
            set(OLD_SPECIFIC_LIB_PATH ${f})
        endif()
    endforeach()
else() # It is cross-compiling
    if(IOS)
        file(GLOB OLD_SPECIFIC_LIB_PATH
            ${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/lib/*/libclang_rt.${SPECIFIC_LIB}${LIB_SUFFIX})
    else()
        file(GLOB OLD_SPECIFIC_LIB_PATH
            ${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/lib/*/libclang_rt.${SPECIFIC_LIB}-${CANGJIE_TARGET_ARCH}${LIB_SUFFIX})
    endif()
endif()

# 2. Move the specific lib to a temporary location; currently we use the ${CMAKE_BINARY_DIR}/lib
if(NOT "${OLD_SPECIFIC_LIB_PATH}" STREQUAL "" AND EXISTS ${OLD_SPECIFIC_LIB_PATH})
    # The libraries to be extracted have the following relationship with the supported target arch:
    # | lib \ Arch     | x86_64_ | aarch64 |
    # | -------------- | ------- | ------- |
    # | clang-builtins | need    | no need |
    # | clang-profile  | need    | need    |
    # | clang-asan     | need    | need    |
    # | clang-tsan     | need    | need    |
    if(NOT (CANGJIE_TARGET_ARCH STREQUAL "aarch64" AND ${SPECIFIC_LIB} STREQUAL "builtins"))
        file(RENAME ${OLD_SPECIFIC_LIB_PATH} ${NEW_SPECIFIC_LIB_PATH})
    endif()
else()
    message(STATUS "The libclang_rt.${SPECIFIC_LIB}-${CANGJIE_TARGET_ARCH}.a has been extracted and does not exist.")
endif()

# 3. Avoid installing redundant empty folder when cross-compiling.
# All its contents have been already moved to Cangjie's lib folder, so it is empty and should be removed.
if(NOT CANGJIE_BUILD_CJC)
    file(GLOB REMAINING_FILES ${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/lib/linux/*)
    list(LENGTH REMAINING_FILES LEN)
    if(${LEN} STREQUAL "0")
        file(REMOVE_RECURSE ${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/lib/linux)
    endif()
endif()

# 4. Copy the specific libraries to the runtime directory for build-binary-tar (see TARGET cjnative POST_BUILD)
