# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

file(GLOB OPTION_NATIVE_FFI_SRC *.cpp)
add_subdirectory(Java)
set(OPTION_SRC ${OPTION_SRC} ${OPTION_NATIVE_FFI_SRC} PARENT_SCOPE)
