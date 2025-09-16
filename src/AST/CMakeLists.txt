# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

set(LIBAST_FFI_AST
    Node.cpp
    RecoverDesugar.cpp
    Types.cpp
    IntLiteral.cpp
    Walker.cpp
    Clone.cpp
    Create.cpp
    Utils.cpp
    Identifier.cpp
    Comment.cpp)
set(AST_EXTRA_SRC
    Cache.cpp
    PrintNode.cpp
    Query.cpp
    Searcher.cpp
    ASTContext.cpp
    ASTTypeValidator.cpp
    ScopeManagerApi.cpp)

add_library(CangjieASTCommon OBJECT ${LIBAST_FFI_AST})
add_library(CangjieASTExtra OBJECT ${AST_EXTRA_SRC})
target_compile_options(CangjieASTCommon PRIVATE ${CJC_EXTRA_WARNINGS})
target_compile_options(CangjieASTExtra PRIVATE ${CJC_EXTRA_WARNINGS})
