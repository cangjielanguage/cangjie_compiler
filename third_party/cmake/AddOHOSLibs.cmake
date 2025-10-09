# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

set(OHOS_PATH $ENV{OHOS_ROOT})
set(CJNATIVE_BACKEND "cjnative")

string(TOLOWER ${CMAKE_HOST_SYSTEM_NAME} HOST_OS)
string(TOLOWER ${CMAKE_HOST_SYSTEM_PROCESSOR} HOST_ARCH)
if("${HOST_OS}" STREQUAL "darwin" AND "${HOST_ARCH}" STREQUAL "arm64")
    set(HOST_ARCH "aarch64")
endif()
add_custom_command(
    OUTPUT
        ${CMAKE_BINARY_DIR}/lib/${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND}/libclang_rt.builtins.a
        ${CMAKE_BINARY_DIR}/lib/${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND}/libclang_rt.profile.a
        ${CMAKE_BINARY_DIR}/lib/${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND}/libunwind.a
    COMMAND ${CMAKE_COMMAND} -E copy
        ${OHOS_PATH}/prebuilts/clang/ohos/${HOST_OS}-${HOST_ARCH}/llvm/lib/clang/15.0.4/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-ohos/libclang_rt.builtins.a
        ${CMAKE_BINARY_DIR}/lib/${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND}/libclang_rt.builtins.a
    COMMAND ${CMAKE_COMMAND} -E copy
        ${OHOS_PATH}/prebuilts/clang/ohos/${HOST_OS}-${HOST_ARCH}/llvm/lib/clang/15.0.4/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-ohos/libclang_rt.profile.a
        ${CMAKE_BINARY_DIR}/lib/${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND}/libclang_rt.profile.a
    COMMAND ${CMAKE_COMMAND} -E copy
        ${OHOS_PATH}/prebuilts/clang/ohos/${HOST_OS}-${HOST_ARCH}/llvm/lib/${CMAKE_SYSTEM_PROCESSOR}-linux-ohos/libunwind.a
        ${CMAKE_BINARY_DIR}/lib/${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND}/libunwind.a
    COMMENT "Copying OHOS libraries...")
add_custom_target(external-deps-ohos ALL
    DEPENDS
        ${CMAKE_BINARY_DIR}/lib/${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND}/libclang_rt.builtins.a
        ${CMAKE_BINARY_DIR}/lib/${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND}/libclang_rt.profile.a
        ${CMAKE_BINARY_DIR}/lib/${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND}/libunwind.a)

install(
    FILES
        ${CMAKE_BINARY_DIR}/lib/${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND}/libclang_rt.builtins.a
        ${CMAKE_BINARY_DIR}/lib/${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND}/libclang_rt.profile.a
        ${CMAKE_BINARY_DIR}/lib/${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND}/libunwind.a
    DESTINATION lib/${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND}${SANITIZER_SUBPATH})
