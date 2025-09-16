# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

if(CANGJIE_BUILD_CJC)
    set(SRC_FILES ./*.cpp Toolchains/GCCPathScanner.cpp Toolchains/Gnu.cpp Toolchains/MachO.cpp ./Backend/Backend.cpp)
    if(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
        set(SRC_FILES
            ${SRC_FILES}
                Backend/CJNATIVEBackend.cpp
            Toolchains/ToolChain.cpp
            Toolchains/CJNATIVE/Linux_CJNATIVE.cpp
            Toolchains/CJNATIVE/MinGW_CJNATIVE.cpp
            Toolchains/CJNATIVE/Darwin_CJNATIVE.cpp
            Toolchains/CJNATIVE/Ohos_CJNATIVE.cpp)
    endif()
    file(GLOB DRIVER_SRC ${SRC_FILES})
    include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR})

    add_library(CangjieDriver OBJECT ${DRIVER_SRC})
    # use llvm
    if(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
        add_dependencies(CangjieDriver cjnative)
    endif()
    target_include_directories(CangjieDriver PRIVATE ${LLVM_INCLUDE_DIRS})
    if(CANGJIE_DISABLE_STACK_GROW_FEATURE)
        target_compile_definitions(CangjieDriver PRIVATE CANGJIE_DISABLE_STACK_GROW_FEATURE)
    endif()
    target_compile_options(CangjieDriver PRIVATE ${CJC_WITH_LLVM_EXTRA_WARNINGS})
endif()

# build for lsp
set(LIB_TEMP_FILES_UTIL TempFileManager.cpp)
add_library(CangjieTempFilesUtil OBJECT ${LIB_TEMP_FILES_UTIL})
target_compile_options(CangjieTempFilesUtil PRIVATE ${CJC_WITH_LLVM_EXTRA_WARNINGS})
