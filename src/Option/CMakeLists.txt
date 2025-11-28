# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

set(OPTION_SRC OptionAction.cpp Option.cpp OptionTable.cpp)

add_subdirectory(NativeFFI)

add_library(CangjieOption OBJECT ${OPTION_SRC})
target_compile_options(CangjieOption PRIVATE ${CJC_EXTRA_WARNINGS})
if(CANGJIE_VISIBLE_OPTIONS_ONLY)
    target_compile_definitions(CangjieOption PUBLIC CANGJIE_VISIBLE_OPTIONS_ONLY)
    add_library(CangjieOption-Fully-Enabled OBJECT ${OPTION_SRC})
    target_compile_options(CangjieOption-Fully-Enabled PRIVATE ${CJC_EXTRA_WARNINGS})
endif()
