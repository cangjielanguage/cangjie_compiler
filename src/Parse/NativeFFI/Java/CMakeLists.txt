# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

file(GLOB PARSE_NATIVE_FFI_JAVA_SRC *.cpp)
set(PARSE_NATIVE_FFI_SRC ${PARSE_NATIVE_FFI_SRC} ${PARSE_NATIVE_FFI_JAVA_SRC} PARENT_SCOPE)
