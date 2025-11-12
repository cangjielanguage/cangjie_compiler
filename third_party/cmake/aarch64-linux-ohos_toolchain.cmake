# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

# set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_VERBOSE_MAKEFILE ON)

# set the name of the target operating system
set(CMAKE_SYSTEM_NAME Linux)

# set the processor or handware name of the target system
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(BUILD_TOOLS_ROOT "/opt/buildtools")
set(OHOS_ROOT "${BUILD_TOOLS_ROOT}/ohos/ohos_root")
set(CMAKE_SYSROOT ${OHOS_ROOT}/out/sdk/obj/third_party/musl/sysroot)

set(TRIPLE aarch64-linux-ohos)
set(CMAKE_C_COMPILER_TARGET   ${TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${TRIPLE})

add_compile_definitions(__OHOS__)
add_compile_definitions(__MUSL__)
add_compile_definitions(STANDARD_SYSTEM)