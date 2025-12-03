# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

file(GLOB CHIR_IR_SRC *.cpp)
add_subdirectory(Expression)
add_subdirectory(Value)
add_subdirectory(Type)
set(CHIR_BASE_SRC ${CHIR_BASE_SRC} ${CHIR_IR_SRC} PARENT_SCOPE)