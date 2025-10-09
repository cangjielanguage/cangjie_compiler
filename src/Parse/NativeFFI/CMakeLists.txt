# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

file(GLOB PARSE_NATIVE_FFI_SRC *.cpp)
add_subdirectory(Java)
add_subdirectory(ObjC)
set(PARSE_SRC ${PARSE_SRC} ${PARSE_NATIVE_FFI_SRC} PARENT_SCOPE)
set(PARSE_FFI_SRC ${PARSE_FFI_SRC} ${PARSE_NATIVE_FFI_SRC} PARENT_SCOPE)