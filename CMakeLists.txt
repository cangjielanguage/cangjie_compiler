# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

cmake_minimum_required(VERSION 3.16.5)
project(cangjie)
set(CJ_SDK_VERSION "1.0.0")
# Add path for custom CMake modules and add cmake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CJNATIVE_BACKEND "cjnative")

if(NOT CMAKE_INCLUDE_SYSTEM_FLAG_CXX)
    set(CXX_SYSTEM_INCLUDE_CONFIGURATION_FLAG /experimental:external /external:W0)
    set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX /external:I)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type selected, default to Debug")
    set(CMAKE_BUILD_TYPE
        "Debug"
        CACHE STRING "Build type (default Debug)" FORCE)
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" uppercase_CMAKE_BUILD_TYPE)

if(CMAKE_BUILD_TYPE AND NOT ("${uppercase_CMAKE_BUILD_TYPE}" MATCHES "^(DEBUG|RELEASE|RELWITHDEBINFO|MINSIZEREL|MINSIZERELWITHDEBINFO)$"))
    message(FATAL_ERROR "Invalid value for CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
endif()

if(CMAKE_BUILD_TYPE
   AND ("${uppercase_CMAKE_BUILD_TYPE}" MATCHES "^(RELEASE)$")
   AND NOT CMAKE_ENABLE_ASSERT)
    add_compile_definitions(CANGJIE_CHIR_WFC_OFF)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Because stdlib is currently compiled very slowly, add this to avoid recompiling stdlib when install.
set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY TRUE)

option(CMAKE_ENABLE_ASSERT "Enable the assert and checking" OFF)
option(CANGJIE_CODEGEN_CJNATIVE_BACKEND "Build a version for CJNATIVE backend" ON)
option(CANGJIE_BUILD_TESTS "Build cangjie tests" ON)
option(CANGJIE_BUILD_CJC "Build cangjie compiler" ON)
option(CANGJIE_SKIP_BUILD_CLANG_RT "Do not build clang_rt libraries, only for cross-compiling" OFF)
option(CANGJIE_BUILD_STD_SUPPORT "Build cangjie depndency of libast" ON)
option(CANGJIE_ENABLE_CCACHE "Build cangjie with ccache" OFF)
option(CANGJIE_DOWNLOAD_FLATBUFFERS "Download flatbuffers" ON)
option(COMPILER_EXPLORER_RACE_FIX "Enable patched behaviour specific to CE" OFF)
option(CANGJIE_DISABLE_STACK_GROW_FEATURE "Disable stack grow feature for cjnative BE" OFF)
option(CANGJIE_ENABLE_COMPILER_TSAN
    "Enable tsan for compiler and tools (relwithbebinfo or debug, Linux builds only)" OFF)
option(CANGJIE_GENERATE_UNICODE_TABLE "Regenerated unicode data tables (should be used only when the Unicode standard cangjie conforms to changes)" OFF)
option(CANGJIE_WRITE_PROFILE "`--profile-compile-time` and `--profile-compile-memory` options for cjc are supported even in release, and write result into PACKAGE_NAME.cj.prof and PACKAGE_NAME.cj.mem.prof." OFF)
option(CANGJIE_ENABLE_ASAN_COV "build with asan and sanitize-coverage, used for cjc_fuzz and lsp_test" OFF)
option(CANGJIE_VISIBLE_OPTIONS_ONLY "open CANGJIE_VISIBLE_OPTIONS_ONLY to only build options that are visible to users" ON)
option(CANGJIE_USE_OH_LLVM_REPO "use OpenHarmony llvm repo with Cangjie llvm patch instead of cangjie llvm repo for building" OFF)
set(CANGJIE_LLVM_BUILD_TYPE Default CACHE STRING "Build type for llvm")
set(CANGJIE_CJDB_BUILD_TYPE Default CACHE STRING "Build type for cjdb")

message(STATUS "Build type for the current project: ${CMAKE_BUILD_TYPE}")
if(CANGJIE_LLVM_BUILD_TYPE MATCHES "^Default$")
    set(CANGJIE_LLVM_BUILD_TYPE Release)
endif()
message(STATUS "Build type for llvm: ${CANGJIE_LLVM_BUILD_TYPE}")
if(CANGJIE_CJDB_BUILD_TYPE MATCHES "^Default$")
    set(CANGJIE_CJDB_BUILD_TYPE ${CMAKE_BUILD_TYPE})
endif()
message(STATUS "Build type for cjdb (llvm excluded): ${CANGJIE_CJDB_BUILD_TYPE}")

if(CANGJIE_BUILD_CJDB AND CANGJIE_CJDB_BUILD_TYPE MATCHES "^Debug$" AND NOT CANGJIE_LLVM_BUILD_TYPE MATCHES "^Debug$")
    message(WARNING "Build type of cjdb differs from build type of llvm, some parts of cjdb may not debuggable.")
endif()

if(NOT DEFINED CANGJIE_CJNATIVE_SOURCE_DIR)
    set(CANGJIE_CJNATIVE_SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/llvm-project)
endif()
set(BOUNDSCHECK ${CMAKE_SOURCE_DIR}/third_party/boundscheck)
if(NOT DEFINED CANGJIE_XML2_SOURCE_DIR)
    set(CANGJIE_XML2_SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/libxml2-2.14.0)
endif()
if(NOT EXISTS ${BOUNDSCHECK})
    set(REPOSITORY_PATH https://gitcode.com/openharmony/third_party_bounds_checking_function.git)
    message(STATUS "Set boundscheck REPOSITORY_PATH: ${REPOSITORY_PATH}")
    execute_process(
        COMMAND git clone --branch OpenHarmony-v6.0-Release ${REPOSITORY_PATH} ${BOUNDSCHECK}
    )
endif()
file(COPY ${CMAKE_SOURCE_DIR}/third_party/cmake/CMakeLists.txt DESTINATION ${BOUNDSCHECK}/)

if(CANGJIE_ENABLE_COMPILER_TSAN)
    if(NOT CANGJIE_BUILD_CJC)
        message(FATAL_ERROR "CANGJIE_ENABLE_COMPILER_TSAN option is only available while building CJC")
    endif()
    if(MINGW)
        message(FATAL_ERROR "CANGJIE_ENABLE_COMPILER_TSAN option does not support windows target currently")
    endif()
endif()

if(CANGJIE_ENABLE_SANITIZE_OPTION)
    add_compile_definitions(CANGJIE_ENABLE_SANITIZE_OPTION)
endif()

set(CANGJIE_ASAN_SUPPORT OFF)
set(CANGJIE_TSAN_SUPPORT OFF)
set(CANGJIE_HWASAN_SUPPORT OFF)
set(CANGJIE_SANITIZER_SUPPORT_ENABLED OFF)
set(CANGJIE_SANITIZER_SUPPORT "" CACHE STRING "Enable cangjie sanitizer support for cangjie libraries")
if ("${CANGJIE_SANITIZER_SUPPORT}" STREQUAL "asan")
    set(CANGJIE_ASAN_SUPPORT ON)
    set(CANGJIE_SANITIZER_SUPPORT_ENABLED ON)
elseif ("${CANGJIE_SANITIZER_SUPPORT}" STREQUAL "tsan")
    set(CANGJIE_TSAN_SUPPORT ON)
    set(CANGJIE_SANITIZER_SUPPORT_ENABLED ON)
elseif ("${CANGJIE_SANITIZER_SUPPORT}" STREQUAL "hwasan")
    set(CANGJIE_HWASAN_SUPPORT ON)
    set(CANGJIE_SANITIZER_SUPPORT_ENABLED ON)
endif()

if(CANGJIE_SANITIZER_SUPPORT_ENABLED)
    if(NOT CANGJIE_CODEGEN_CJNATIVE_BACKEND)
        message(FATAL_ERROR "Cangjie with sanitizer support is supposed to be built in cjnative backend")
    endif()
    if(CANGJIE_ENABLE_ASAN OR CANGJIE_ENABLE_ASAN_COV)
        message(FATAL_ERROR "Cangjie with sanitizer support is not compatible with asan build version")
    endif()
    if(CANGJIE_BUILD_CJC)
        message(FATAL_ERROR "Cangjie with sanitizer support is not compatible while building cjc")
    endif()
    if(NOT CANGJIE_BUILD_STD_SUPPORT)
        message(FATAL_ERROR "Please build sanitizer support version with CANGJIE_BUILD_STD_SUPPORT")
    endif()
endif()

if(CANGJIE_ENABLE_HWASAN)
    add_compile_options(-mllvm -hwasan-globals=0)
endif()

if(CANGJIE_ASAN_SUPPORT)
    if(WIN32)
        message(FATAL_ERROR "Cangjie with asan support only supports on Linux/OHOS platform")
    endif()
    # Here just use add_compile_options for asan instrumentation
    # do not use add_link_options(-fsanitize=address) which will link library while using gcc specifically
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    set(SANITIZER_SUBPATH /asan)
elseif(CANGJIE_TSAN_SUPPORT)
    if(OHOS OR CMAKE_CROSSCOMPILING)
        message(FATAL_ERROR "Cangjie with tsan support only supports on Linux/Windows platform")
    endif()
    if(CANGJIE_SKIP_BUILD_CLANG_RT)
        message(FATAL_ERROR "You have to build compiler-rt locally")
    endif()
    set(SANITIZER_SUBPATH /tsan)
elseif(CANGJIE_HWASAN_SUPPORT)
    if(WIN32)
        message(FATAL_ERROR "Cangjie with asan support only supports on Linux/OHOS platform")
    endif()
    add_compile_options(-fsanitize=hwaddress -fno-omit-frame-pointer)
    set(SANITIZER_SUBPATH /hwasan)
endif()

set(CANGJIE_CJ_SANCOV_USE_8BIT OFF)
set(CANGJIE_BUILD_STDLIB_WITH_CJ_SANCOV OFF)

if(CANGJIE_BUILD_STDLIB_WITH_CJ_SANCOV)
    if((OHOS OR CMAKE_CROSSCOMPILING) OR WIN32)
        message(FATAL_ERROR "Cangjie with Sanitizer Coverage support only supports on Linux platform")
    endif()
    if(NOT CANGJIE_CODEGEN_CJNATIVE_BACKEND)
        message(FATAL_ERROR "Cangjie with Sanitizer Coverage support only supports cjnative")
    endif()
endif()

if(CANGJIE_ENABLE_CCACHE
   OR CMAKE_BUILD_TYPE MATCHES Debug
   OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
    include(cmake/optional/CangjieCCache.cmake)
endif()

if(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    add_compile_definitions(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
endif()

if(CANGJIE_WRITE_PROFILE)
    # Enable the --profile-compile-time and --profile-compile-memory option.
    add_compile_definitions(CANGJIE_WRITE_PROFILE)
endif()


if(CANGJIE_BUILD_TESTS)
    add_compile_definitions(CANGJIE_BUILD_TESTS)
endif()

if(CJ_SDK_VERSION)
    add_definitions(-DCJ_SDK_VERSION="${CJ_SDK_VERSION}")
endif()

if(CMAKE_ENABLE_ASSERT)
    add_compile_definitions(CMAKE_ENABLE_ASSERT)
endif()

if(OHOS)
    # This is a CMake bug until 3.22, where MINGW is wrongly set
    # when targeting other platforms on a Windows machine with a MinGW toolchain.
    # See more at CMake issue tracker #22647.
    set(MINGW 0)
endif()

if(NOT TRIPLE)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}-gnu" TRIPLE)
endif()

if(MINGW)
    set(TRIPLE x86_64-w64-mingw32)
endif()

if(NOT TARGET_TRIPLE_DIRECTORY_PREFIX)
    if(IOS AND IOS_PLATFORM MATCHES "SIMULATOR")
        set(EXTRA_OS_SUFFIX _simulator)
    endif()
    string(REPLACE "-" "_" TARGET_TRIPLE_DIRECTORY_PREFIX "${CMAKE_SYSTEM_NAME}${EXTRA_OS_SUFFIX}_${CMAKE_SYSTEM_PROCESSOR}")
    string(TOLOWER "${TARGET_TRIPLE_DIRECTORY_PREFIX}" TARGET_TRIPLE_DIRECTORY_PREFIX)
endif()

if(CMAKE_CROSSCOMPILING)
    message(STATUS "CROSS COMPILING libs from ${CMAKE_HOST_SYSTEM_PROCESSOR}-${CMAKE_HOST_SYSTEM_NAME} to ${TRIPLE}")
endif()

# Always build clang_rt when it is native-compiling
if(NOT CMAKE_CROSSCOMPILING)
    set(CANGJIE_SKIP_BUILD_CLANG_RT OFF)
endif()
if(OHOS)
    set(CANGJIE_SKIP_BUILD_CLANG_RT ON)
endif()

include(SetupAr)

message(STATUS "Building with target=${TRIPLE}")

if(MACOS)
    include(ReadDarwinSDKInfo)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 12.0.0)
    set(CANGJIE_DISABLE_STACK_GROW_FEATURE ON)
endif()

if(CANGJIE_INCLUDE)
    foreach(include_path ${CANGJIE_INCLUDE})
        include_directories(${include_path})
    endforeach()
endif()

if(NOT (CMAKE_BUILD_TYPE MATCHES Debug))
    add_definitions(-DNDEBUG)
endif()

if(CMAKE_BUILD_TYPE MATCHES Release)
    add_definitions(-DRELEASE)
endif()

# BUILD_GCC_TOOLCHAIN specifies toolchain for cjc, stdlib and BE at the same time, which means
# that it is used for both targets for HOST (e.g. cjc, llc) and targets for TARGET
# (e.g. stdlib, runtime). In the case that we need to build a cjc for a host which is not a target,
# we must build cjc and stdlib separately for specifying different toolchains for them.
if(BUILD_GCC_TOOLCHAIN AND (CMAKE_C_COMPILER_ID STREQUAL "Clang" OR (CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
)# It is clang that takes --gcc-toolchain options.
    message(STATUS "Add compile option for clang, --gcc-toolchain=${BUILD_GCC_TOOLCHAIN}")
    add_compile_options(--gcc-toolchain=${BUILD_GCC_TOOLCHAIN})
    add_link_options(--gcc-toolchain=${BUILD_GCC_TOOLCHAIN})
endif()

foreach(libpath ${CANGJIE_TARGET_LIB})
    add_link_options("-L${libpath}")
endforeach()

add_compile_options(${CXX_SYSTEM_INCLUDE_CONFIGURATION_FLAG})

if(CANGJIE_LINK_JOB_POOL AND CMAKE_GENERATOR MATCHES "Ninja")
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS link_job_pool=${CANGJIE_LINK_JOB_POOL})
    set(CMAKE_JOB_POOL_LINK link_job_pool)
endif()

if(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
include_directories(${CMAKE_SOURCE_DIR}/include)
else()
include_directories(${CMAKE_SOURCE_DIR}/includeVM)
include_directories(AFTER ${CMAKE_SOURCE_DIR}/include)
endif()

if(WIN32)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)
endif()
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(CANGJIE_VERSION)
    add_compile_definitions(VERSION_TAIL="${CANGJIE_VERSION}")
endif()

string(TOLOWER "${CMAKE_SYSTEM_NAME}" TARGET_OS)
add_compile_definitions(__${TARGET_OS}__)

if(CANGJIE_CODEGEN_CJNATIVE_BACKEND AND CANGJIE_BUILD_CJC)
    include(ExternalProject)
    set(CANGJIE_DEMANGLER_CMAKE_ARGS
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCANGJIE_TARGET_TOOLCHAIN=${CANGJIE_TARGET_TOOLCHAIN}
        -DCMAKE_SYSROOT=${CMAKE_SYSROOT}
        -DBUILD_GCC_TOOLCHAIN=${BUILD_GCC_TOOLCHAIN}
        -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/cangjie-demangler
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
    set(CANGJIE_DEMANGLER_CONFIGURE_COMMAND
        ${CMAKE_COMMAND} <SOURCE_DIR> -G${CMAKE_GENERATOR} ${CANGJIE_DEMANGLER_CMAKE_ARGS})
    ExternalProject_Add(
        cangjie-demangler
        SOURCE_DIR ${CMAKE_SOURCE_DIR}/demangler
        CONFIGURE_COMMAND ${CANGJIE_DEMANGLER_CONFIGURE_COMMAND}
        BUILD_COMMAND ${CMAKE_COMMAND} --build .
        INSTALL_COMMAND ${CMAKE_COMMAND} --install .)
endif()

if(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    add_subdirectory(third_party/boundscheck)
    install(
        TARGETS boundscheck
        RUNTIME
            DESTINATION
                runtime/lib/${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND}${SANITIZER_SUBPATH} # RUNTIME includes DLLs. This takes effect on Windows.
        LIBRARY
            DESTINATION
                runtime/lib/${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND}${SANITIZER_SUBPATH} # LIBRARY includes shared libraries except DLLs.
    )
    install(TARGETS boundscheck-static DESTINATION lib/${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND}${SANITIZER_SUBPATH})
    set(LIB_FOLDER_NAME lib)
    if(WIN32)
        set(LIB_FOLDER_NAME bin)
    endif()
    install(
        FILES ${CMAKE_BINARY_DIR}/${LIB_FOLDER_NAME}/libboundscheck${CMAKE_SHARED_LIBRARY_SUFFIX}
        DESTINATION runtime/lib/${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND}${SANITIZER_SUBPATH}
        RENAME libsecurec${CMAKE_SHARED_LIBRARY_SUFFIX}
    )
    if(WIN32 AND CANGJIE_BUILD_CJDB)
        install(TARGETS boundscheck RUNTIME DESTINATION third_party/llvm/bin)
    endif()
    install(FILES include/cangjie/MetaTransformation/MetaTransform.h DESTINATION include/)
endif()

add_subdirectory(third_party)
if(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
add_subdirectory(schema)
add_subdirectory(src)
endif()
if(CANGJIE_BUILD_CJC)
    add_subdirectory(utils)
endif()

find_package(GTest)
if(NOT GTest_FOUND)
    message(WARNING "GTest is not found. Unittests are disabled.")
endif()

if(CANGJIE_BUILD_CJC AND CANGJIE_BUILD_TESTS AND GTest_FOUND)
    enable_testing()
    add_subdirectory(unittests)
endif()

install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/cangjie DESTINATION include/)
