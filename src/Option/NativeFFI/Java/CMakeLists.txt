# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

file(GLOB OPTION_NATIVE_FFI_JAVA_SRC *.cpp)
set(OPTION_NATIVE_FFI_SRC ${OPTION_NATIVE_FFI_SRC} ${OPTION_NATIVE_FFI_JAVA_SRC} PARENT_SCOPE)
