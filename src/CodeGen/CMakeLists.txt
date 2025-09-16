# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

file(GLOB CodeGenBase
    *.cpp
    Base/*.cpp
    Base/CGTypes/*.cpp
    Base/ExprDispatcher/*.cpp
    Utils/*.cpp
)

include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR})

if(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    file(GLOB CodeGenCJNative CJNative/*.cpp CJNative/CGTypes/*.cpp)
    list(REMOVE_ITEM CodeGenCJNative ${CMAKE_CURRENT_SOURCE_DIR}/CJNative/CJNativeGenArray.cpp)
    set(CODEGEN_SRC ${CodeGenBase} ${CodeGenCJNative})
    add_subdirectory(IncrementalGen)
endif()

add_library(CangjieCodeGen OBJECT ${CODEGEN_SRC})

if(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    add_dependencies(CangjieCodeGen cjnative)
endif()
target_include_directories(CangjieCodeGen PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_options(CangjieCodeGen PRIVATE ${CJC_WITH_LLVM_EXTRA_WARNINGS})
