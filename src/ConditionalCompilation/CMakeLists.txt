# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

if(CANGJIE_BUILD_CJC)
    set(CONDITIONALCOMPILATION_SRC
        ConditionalCompilation.cpp)
    add_library(CangjieConditionalCompilation OBJECT ${CONDITIONALCOMPILATION_SRC})
    target_compile_options(CangjieConditionalCompilation PRIVATE ${CJC_EXTRA_WARNINGS})
endif()

