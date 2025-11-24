# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

get_filename_component(CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)
include("${CMAKE_DIR}/linux_toolchain.cmake")
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
if("${CMAKE_HOST_SYSTEM_PROCESSOR}" STREQUAL "AMD64")
    set(CMAKE_HOST_SYSTEM_PROCESSOR x86_64)
endif()

set(TRIPLE aarch64-linux-ohos)
set(OHOS ON)

# We add --target option for clang only since gcc does not support --target option.
# In case of gcc, cross compilation requires a target-specific gcc (a cross compiler).
add_compile_options(--target=${TRIPLE})
add_link_options(--target=${TRIPLE})

add_compile_definitions(__ohos__)
add_compile_definitions(OPENSSL_ARM64_PLATFORM)

add_compile_options(-mbranch-protection=pac-ret)

set(CMAKE_C_FLAGS "-fno-emulated-tls ${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "-fno-emulated-tls ${CMAKE_CXX_FLAGS}")
set(CMAKE_RANLIB "${CANGJIE_TARGET_TOOLCHAIN}/llvm-ranlib")

set(TARGET_TRIPLE_DIRECTORY_PREFIX "linux_ohos_aarch64")

set(LINKER_OPTION_PREFIX)
