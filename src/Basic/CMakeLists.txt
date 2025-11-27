# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

set(BASIC_SRC
    StringConvertor.cpp
    DiagnosticEngine.cpp
    DiagnosticEngineImpl.cpp
    InteropCJPackageConfigReader.cpp
    Position.cpp
    SourceManager.cpp
    Display.cpp
    DiagnosticEmitter.cpp
    DiagnosticJsonFormatter.cpp
    Utils.cpp
    Print.cpp)

set(BASIC_SRC ${BASIC_SRC} Version.cpp)

include_directories(${CMAKE_SOURCE_DIR}/third_party/tinytoml)

add_library(CangjieBasic OBJECT ${BASIC_SRC})
target_compile_options(CangjieBasic PRIVATE -Wno-deprecated-declarations)
target_compile_options(CangjieBasic PRIVATE ${CJC_EXTRA_WARNINGS})
set_source_files_properties(InteropCJPackageConfigReader.cpp PROPERTIES COMPILE_OPTIONS "-fexceptions")
