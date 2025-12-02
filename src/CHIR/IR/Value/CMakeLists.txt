# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

file(GLOB VALUE_SRC *.cpp)
set(CHIR_BASE_SRC ${CHIR_BASE_SRC} ${VALUE_SRC} PARENT_SCOPE)