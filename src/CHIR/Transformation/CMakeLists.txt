# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

file(GLOB TRANSFORMATION_SRC *.cpp)
add_subdirectory(GenerateVTable)
set(CHIR_OPT_SRC ${CHIR_OPT_SRC} ${TRANSFORMATION_SRC} PARENT_SCOPE)