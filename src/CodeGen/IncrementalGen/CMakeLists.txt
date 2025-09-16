# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

file(GLOB CodeGenIncrementalGen *.cpp)

set(INCREMENTAL_COMPILATION_SRC ${CodeGenIncrementalGen})

# The libraries of llvm are not compiled with RTTI, since
# some llvm classes without typeinfo are used in codegen
# incremental compilation part, it is required to set
# `-fno-rtti` flag to compile this part.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")

add_library(CangjieCodeGenIncrementalGen OBJECT ${INCREMENTAL_COMPILATION_SRC})

# use llvm
if(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    add_dependencies(CangjieCodeGenIncrementalGen cjnative)
endif()
target_include_directories(CangjieCodeGenIncrementalGen PRIVATE ${LLVM_INCLUDE_DIRS})
