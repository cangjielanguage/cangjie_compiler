# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

file(GLOB PARSE_SRC *.cpp)

set(PARSE_FFI_SRC
    AttachComment.cpp
    ParseAnnotations.cpp
    ParseAtom.cpp
    ParseCJMPDecl.cpp
    ParseDecl.cpp
    ParseExpr.cpp
    ParseImports.cpp
    ParseFeatures.cpp
    ParseMacro.cpp
    ParseModifiers.cpp
    ParsePattern.cpp
    ParseQuote.cpp
    ParseType.cpp
    Parser.cpp
    ParserDiag.cpp
    ParserImpl.cpp
    ParserModifierRules.cpp
    ParserUtils.cpp
)

add_subdirectory(NativeFFI)

if(NOT CMAKE_ENABLE_ASSERT
   AND NOT CMAKE_BUILD_TYPE MATCHES Debug)
    get_filename_component(ASTChecker ${CMAKE_CURRENT_SOURCE_DIR}/ASTChecker.cpp ABSOLUTE)
    list(REMOVE_ITEM PARSE_SRC ${ASTChecker})
endif()

add_library(CangjieParse OBJECT ${PARSE_SRC})
add_library(CangjieParseFFI OBJECT ${PARSE_FFI_SRC})
target_compile_options(CangjieParse PRIVATE ${CJC_EXTRA_WARNINGS})
