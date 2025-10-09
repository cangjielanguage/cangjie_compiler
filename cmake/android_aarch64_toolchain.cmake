# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

get_filename_component(CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)
include("${CMAKE_DIR}/linux_toolchain.cmake")

set(CMAKE_SYSTEM_NAME "Android")
set(CMAKE_SYSTEM_PROCESSOR "aarch64")
if(NOT CMAKE_ANDROID_API)
    set(CMAKE_ANDROID_API 31)
    message(STATUS "Android API level is not set, use default setting: ${CMAKE_ANDROID_API}")
endif()
set(CMAKE_ANDROID_ARCH_ABI "arm64-v8a")
string(TOLOWER ${CMAKE_HOST_SYSTEM_NAME} HOST_OS)
set(CANGJIE_TARGET_TOOLCHAIN "${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/${HOST_OS}-${CMAKE_HOST_SYSTEM_PROCESSOR}/bin")
set(TRIPLE aarch64-linux-android${CMAKE_ANDROID_API})
set(TARGET_TRIPLE_DIRECTORY_PREFIX linux_android${CMAKE_ANDROID_API}_aarch64)

# Variable ANDROID will be set by CMake. Custom variables are not necessary here.