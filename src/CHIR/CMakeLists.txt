# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

file(GLOB CHIR_BASE_SRC *.cpp)
add_subdirectory(Analysis)
add_subdirectory(AST2CHIR)
add_subdirectory(Checker)
add_subdirectory(Expression)
add_subdirectory(GenerateVTable)
add_subdirectory(Type)
add_subdirectory(Visitor)
add_subdirectory(NativeFFI)

add_library(CangjieCHIRBase OBJECT ${CHIR_BASE_SRC})

add_subdirectory(Interpreter)
add_subdirectory(Serializer)
add_subdirectory(Transformation)
add_library(CangjieCHIRExtra OBJECT ${CHIR_OPT_SRC} ${CHIRInterpreter} ${CHIRSerializer} ${ComputeAnnotations})
target_include_directories(CangjieCHIRExtra PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/AST2CHIR/TranslateASTNode/)

# the interpreter needs third-party and also flatbuffers
target_include_directories(CangjieCHIRExtra PRIVATE ${BOUNDSCHECK}/include)
add_dependencies(CangjieCHIRExtra CangjieFlatbuffersHeaders)
target_include_directories(CangjieCHIRExtra PRIVATE ${FLATBUFFERS_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/Annotations)

# CHIR.cpp indirectly includes PackageFormat_generated.h, this will be refactored later.
target_include_directories(CangjieCHIRBase PRIVATE ${FLATBUFFERS_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/Annotations)
add_dependencies(CangjieCHIRBase CangjieFlatbuffersHeaders)

target_link_libraries(CangjieCHIRExtra)
target_compile_options(CangjieCHIRBase PRIVATE ${CJC_EXTRA_WARNINGS})
target_compile_options(CangjieCHIRExtra PRIVATE ${CJC_EXTRA_WARNINGS})
