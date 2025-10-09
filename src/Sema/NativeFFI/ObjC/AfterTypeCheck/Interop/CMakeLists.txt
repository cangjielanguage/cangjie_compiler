# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

file(GLOB SEMA_NATIVE_FFI_OBJ_C_AFTER_TYPECHECK_INTEROP_SRC *.cpp)
set(SEMA_NATIVE_FFI_OBJ_C_AFTER_TYPECHECK_SRC ${SEMA_NATIVE_FFI_OBJ_C_AFTER_TYPECHECK_SRC} ${SEMA_NATIVE_FFI_OBJ_C_AFTER_TYPECHECK_INTEROP_SRC} PARENT_SCOPE)
