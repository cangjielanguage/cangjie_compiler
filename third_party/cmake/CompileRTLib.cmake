# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

# compile_rtlib compiles libclang_rt-profile.a and libclang_rt-builtins.a for the target platforms
function(compile_rtlib target_name)

    set(oneValueArgs INSTALL_PREFIX)
    cmake_parse_arguments(
        COMPILE_RTLIB
        "${options}"
        "${oneValueArgs}"
        ""
        ${ARGN})

    set(LLVM_RTLIBS libclang_rt.profile-${CMAKE_SYSTEM_PROCESSOR}.a)
    if(NOT WIN32)
        list(APPEND LLVM_RTLIBS libclang_rt.fuzzer_no_main-${CMAKE_SYSTEM_PROCESSOR}.a)
        set(CANGJIE_LIBFUZZER_BUILD_ENABLED ON)
    else()
        set(CANGJIE_LIBFUZZER_BUILD_ENABLED OFF)
    endif()
    if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64")
        list(APPEND LLVM_RTLIBS libclang_rt.builtins-${CMAKE_SYSTEM_PROCESSOR}.a)
    endif()

    set(RTLIBS_TARGET ${TRIPLE})
    if(CANGJIE_ASAN_SUPPORT)
        set(SANITIZER_BUILD_TARGET -DCOMPILER_RT_SANITIZERS_TO_BUILD=asan)
        list(APPEND LLVM_RTLIBS libclang_rt.asan-${CMAKE_SYSTEM_PROCESSOR}.a)
    elseif(CANGJIE_TSAN_SUPPORT)
        set(SANITIZER_BUILD_TARGET -DCOMPILER_RT_SANITIZERS_TO_BUILD=tsan)
        list(APPEND LLVM_RTLIBS libclang_rt.tsan-${CMAKE_SYSTEM_PROCESSOR}.a)
    elseif(CANGJIE_HWASAN_SUPPORT)
        list(APPEND LLVM_RTLIBS libclang_rt.hwasan-${CMAKE_SYSTEM_PROCESSOR}.a)
    endif()

    # Setting flags for cross-compiling
    set(RTLIBS_C_FLAGS
        "${C_OTHER_FLAGS} -pipe -fno-common -fno-strict-aliasing -fstack-protector-all -O3 -U_FORTIFY_SOURCE --sysroot=${CMAKE_SYSROOT} --prefix=${CANGJIE_TARGET_TOOLCHAIN}"
    )
    set(RTLIBS_CXX_FLAGS
        "${C_OTHER_FLAGS} -pipe -fno-common -fno-strict-aliasing -fstack-protector-all -O3 -U_FORTIFY_SOURCE --sysroot=${CMAKE_SYSROOT} --prefix=${CANGJIE_TARGET_TOOLCHAIN}"
    )

    # The -fPIE option is not provided on Windows.
    # Address space layout randomization (ASLR) is enabled by MinGW ld by default.
    if(NOT WIN32)
        set(RTLIBS_C_FLAGS "${RTLIBS_C_FLAGS} -fPIE")
        set(RTLIBS_CXX_FLAGS "${RTLIBS_CXX_FLAGS} -fPIE")
    endif()

    if(MINGW OR (CMAKE_CROSSCOMPILING AND NOT OHOS))
        set(CLANG_C_COMPILER ${TRIPLE}-gcc)
        set(CLANG_CXX_COMPILER ${TRIPLE}-g++)
    else()
        set(CLANG_C_COMPILER clang)
        set(CLANG_CXX_COMPILER clang++)
    endif()

    # Use clang which the current cmake uses instead of system installed clang
    if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        set(CLANG_C_COMPILER ${CMAKE_C_COMPILER})
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(CLANG_CXX_COMPILER ${CMAKE_CXX_COMPILER})
    endif()

    # Finding full paths of these pre-installed llvm tools
    find_program(RTLIBS_AR_PATH "llvm-ar")
    find_program(RTLIBS_NM_PATH "llvm-nm")
    find_program(RTLIBS_RANLIB_PATH "llvm-ranlib")

    # sanitizer version REQUIRES llvm-config, otherwise will fail
    if (CANGJIE_SANITIZER_SUPPORT_ENABLED)
        find_program(RTLIBS_CONFIG_PATH "llvm-config" REQUIRED)
    else()
        find_program(RTLIBS_CONFIG_PATH "llvm-config")
    endif()

    set(LLVM_CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=${COMPILE_RTLIB_INSTALL_PREFIX}
        -DCMAKE_BUILD_TYPE=Release
        -DCOMPILER_RT_BUILD_BUILTINS=ON
        -DCOMPILER_RT_BUILD_PROFILE=ON
        -DCOMPILER_RT_BUILD_SANITIZERS=${CANGJIE_SANITIZER_SUPPORT_ENABLED}
        ${SANITIZER_BUILD_TARGET}
        -DCOMPILER_RT_BUILD_XRAY=OFF
        -DCOMPILER_RT_BUILD_LIBFUZZER=${CANGJIE_LIBFUZZER_BUILD_ENABLED}
        -DCOMPILER_RT_BUILD_MEMPROF=OFF
        -DCOMPILER_RT_BUILD_CRT=OFF
        -DCOMPILER_RT_USE_LIBCXX=OFF
        -DCMAKE_C_COMPILER=${CLANG_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CLANG_CXX_COMPILER}
        -DCMAKE_AR=${RTLIBS_AR_PATH}
        -DCMAKE_NM=${RTLIBS_NM_PATH}
        -DCMAKE_RANLIB=${RTLIBS_RANLIB_PATH}
        -DLLVM_CONFIG_PATH=${RTLIBS_CONFIG_PATH}
        -DCMAKE_C_COMPILER_TARGET=${RTLIBS_TARGET}
        -DCMAKE_CXX_COMPILER_TARGET=${RTLIBS_TARGET}
        -DCMAKE_ASM_COMPILER_TARGET=${RTLIBS_TARGET}
        -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON
        -DCMAKE_C_FLAGS=${RTLIBS_C_FLAGS}
        -DCMAKE_CXX_FLAGS=${RTLIBS_CXX_FLAGS})

    if(MINGW)
        list(APPEND LLVM_CMAKE_ARGS -DCMAKE_SYSTEM_NAME=Windows)
        list(APPEND LLVM_CMAKE_ARGS -DLLVM_HOST_TRIPLE=x86_64-w64-mingw32)
    endif()

    # For Jenkins, which download source files first
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/llvm-project)
        ExternalProject_Add(
            ${target_name}
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/llvm-project
            BUILD_BYPRODUCTS ${LLVM_RTLIBS}
            SOURCE_SUBDIR compiler-rt
            CMAKE_ARGS ${LLVM_CMAKE_ARGS})
    else()
        if(CANGJIE_USE_OH_LLVM_REPO)
            set(REPOSITORY_PATH "https://gitcode.com/openharmony/third_party_llvm-project.git")
            set(LLVM_TAG master)
        else()
            set(REPOSITORY_PATH "https://gitcode.com/Cangjie/llvm-project.git")
            set(LLVM_TAG dev)
        endif()
        set(LLVM_REPO_DOWNLOAD_ARGS
            DOWNLOAD_COMMAND git clone -b ${LLVM_TAG} --depth=1 ${REPOSITORY_PATH} <SOURCE_DIR>)
        if(CANGJIE_USE_OH_LLVM_REPO)
            set(APPLY_LLVM_PATCH_COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyLLVMPatch.cmake" <SOURCE_DIR> ${CMAKE_SOURCE_DIR}/third_party/llvmPatch.diff)
            set(LLVM_REPO_DOWNLOAD_ARGS
                GIT_REPOSITORY ${REPOSITORY_PATH}
                GIT_TAG ${LLVM_TAG}
                GIT_PROGRESS ON
                GIT_CONFIG ${GIT_ARGS}
                GIT_SHALLOW OFF)
        endif()
        ExternalProject_Add(
            ${target_name}
            ${LLVM_REPO_DOWNLOAD_ARGS}
            BUILD_BYPRODUCTS ${LLVM_RTLIBS}
            SOURCE_SUBDIR compiler-rt
            CMAKE_ARGS ${LLVM_CMAKE_ARGS})
    endif()
endfunction(compile_rtlib)
