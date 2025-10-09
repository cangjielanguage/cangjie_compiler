# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

file(GLOB SEMA_NATIVE_FFI_OBJ_C_SRC *.cpp)
add_subdirectory(AfterTypeCheck)
add_subdirectory(BeforeTypeCheck)
add_subdirectory(ObjCCodeGenerator)
add_subdirectory(TypeCheck)
add_subdirectory(Utils)
set(SEMA_NATIVE_FFI_SRC ${SEMA_NATIVE_FFI_SRC} ${SEMA_NATIVE_FFI_OBJ_C_SRC} PARENT_SCOPE)