# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

if(EXISTS ${CANGJIE_CJNATIVE_SOURCE_DIR})
    set(UNICODE_DEPENDENCIES ${CANGJIE_CJNATIVE_SOURCE_DIR})
else()
    execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/utils_dep/
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    execute_process(COMMAND git clone https://gitcode.com/openharmony/third_party_llvm-project.git --depth 1
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/utils_dep/
        RESULT_VARIABLE GIT_CLONE_RESULT)
    execute_process(COMMAND git fetch --depth 1 origin 5c68a1cb123161b54b72ce90e7975d95a8eaf2a4
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/utils_dep/third_party_llvm-project
        RESULT_VARIABLE GIT_FETCH_RESULT)
    execute_process(COMMAND git checkout 5c68a1cb123161b54b72ce90e7975d95a8eaf2a4
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/utils_dep/third_party_llvm-project
        RESULT_VARIABLE GIT_FETCH_RESULT)
    set(UNICODE_DEPENDENCIES ${CMAKE_BINARY_DIR}/utils_dep/third_party_llvm-project)
endif()

set(UNICODE_UTIL_SRC
    Unicode.cpp
    UnicodeNFC.cpp
    UnicodeWidth.cpp
    ${UNICODE_DEPENDENCIES}/llvm/lib/Support/ConvertUTF.cpp)

set(COMMON_UTIL_SRC
    FileUtil.cpp
    SafePointer.cpp
    SignalUtil.cpp
    FloatFormat.cpp
    Utils.cpp
    ICEUtil.cpp
    Semaphore.cpp
    StdUtils/StdUtils.cpp)

set(PROFILE_SRC
    ProfileRecorder.cpp
    UserBase.cpp
    UserTimer.cpp
    UserMemoryUsage.cpp
    UserCodeInfo.cpp)

if(CANGJIE_BUILD_CJC OR CANGJIE_BUILD_TESTS)
    if(WIN32)
        list(APPEND COMMON_UTIL_SRC SignalWin.cpp)
    else()
        list(APPEND COMMON_UTIL_SRC SignalUnix.cpp)
    endif()
endif()

add_library(CangjieCommonUtil OBJECT ${COMMON_UTIL_SRC})
add_library(CangjieCommonUtilFFI OBJECT
            FileUtil.cpp
            SafePointer.cpp
            FloatFormat.cpp
            Utils.cpp
            ICEUtil.cpp
            Semaphore.cpp
            StdUtils/StdUtils.cpp)
set_source_files_properties(StdUtils/StdUtils.cpp PROPERTIES COMPILE_OPTIONS "-fexceptions")
add_library(CangjieUnicodeUtils OBJECT ${UNICODE_UTIL_SRC})

target_include_directories(CangjieUnicodeUtils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(CangjieUnicodeUtils PRIVATE ${UNICODE_DEPENDENCIES}/llvm/include)
if(CANGJIE_GENERATE_UNICODE_TABLE)
    add_dependencies(CangjieUnicodeUtils GenerateNFCData)
    add_dependencies(CangjieUnicodeUtils GenerateWidthData)
endif()

if(CANGJIE_BUILD_CJC)
    add_library(CangjieProfileUtils OBJECT ${PROFILE_SRC})
    target_compile_options(CangjieProfileUtils PRIVATE ${CJC_EXTRA_WARNINGS})
endif()
target_include_directories(CangjieCommonUtil PRIVATE ${BOUNDSCHECK}/include)

target_compile_options(CangjieCommonUtil PRIVATE ${CJC_WITH_LLVM_EXTRA_WARNINGS})
target_compile_options(CangjieCommonUtilFFI PRIVATE ${CJC_WITH_LLVM_EXTRA_WARNINGS})
target_compile_options(CangjieUnicodeUtils PRIVATE ${CJC_WITH_LLVM_EXTRA_WARNINGS})
