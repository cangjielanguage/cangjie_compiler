# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

get_filename_component(CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)
include("${CMAKE_DIR}/darwin_toolchain.cmake")
set(CMAKE_SYSTEM_PROCESSOR "x86_64")

set(TRIPLE x86_64-apple-darwin)
set(MACOS ON)

if("${CMAKE_SYSTEM_NAME}" STREQUAL "${CMAKE_HOST_SYSTEM_NAME}" AND "${CMAKE_SYSTEM_PROCESSOR}" STREQUAL
                                                                   "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    set(CMAKE_CROSSCOMPILING OFF)
endif()
