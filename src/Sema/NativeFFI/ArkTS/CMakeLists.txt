# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

# Compiler-private ArkTS native interop stages and source generation.
file(GLOB_RECURSE SEMA_NATIVE_FFI_ARKTS_SRC CONFIGURE_DEPENDS *.cpp)
set(SEMA_SRC ${SEMA_SRC} ${SEMA_NATIVE_FFI_ARKTS_SRC} PARENT_SCOPE)
