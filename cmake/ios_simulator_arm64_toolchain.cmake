# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

get_filename_component(CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)
include("${CMAKE_DIR}/darwin_toolchain.cmake")
 
set(CMAKE_SYSTEM_NAME "ios")
set(CMAKE_SYSTEM_PROCESSOR "aarch64")
set(TRIPLE arm64-apple-ios11-simulator)
set(CXX_COMPATIABLE_TRIPLE arm64-apple-ios12-simulator)
set(TARGET_TRIPLE_DIRECTORY_PREFIX ios_simulator_aarch64)
 
add_compile_options(--target=${TRIPLE})
add_link_options(--target=${TRIPLE})
 
set(IOS ON)
set(IOS_PLATFORM SIMULATOR)
set(IOS_PLATFORM_LOCATION "iPhoneSimulator.platform")
set(CMAKE_IOS_DEVELOPER_ROOT "/Applications/Xcode.app/Contents/Developer/Platforms/${IOS_PLATFORM_LOCATION}/Developer")
set(CMAKE_IOS_SDK_ROOT "${CMAKE_IOS_DEVELOPER_ROOT}/SDKs/iPhoneSimulator17.5.sdk")
set(CMAKE_OSX_SYSROOT "${CMAKE_IOS_DEVELOPER_ROOT}/SDKs/iPhoneSimulator17.5.sdk")