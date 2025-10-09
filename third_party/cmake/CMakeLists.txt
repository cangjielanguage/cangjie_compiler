# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

cmake_minimum_required(VERSION 3.16.5)
project(boundscheck)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(SECURE_CFLAG_FOR_SHARED_LIBRARY "-fstack-protector-all")

# we are not expect libboundscheck to be instrumented by asan or hwasan
# otherwise it will generate false positive everywhere
if(CANGJIE_SANITIZER_SUPPORT_ENABLED)
    get_directory_property(boundscheck_COMPILE_OPTIONS COMPILE_OPTIONS)
    list(REMOVE_ITEM boundscheck_COMPILE_OPTIONS "-fsanitize=address" "-fsanitize=hwaddress")
    set_directory_properties(PROPERTIES COMPILE_OPTIONS "${boundscheck_COMPILE_OPTIONS}")
endif()

if (MINGW)
    set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
    set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
    set(WARNING_FLAGS "-w")
    set(SECURE_CFLAG_FOR_SHARED_LIBRARY "-static ${SECURE_CFLAG_FOR_SHARED_LIBRARY}")
else()
    if(NOT DARWIN)
        set(SECURE_LDFLAG_FOR_SHARED_LIBRARY "-Wl,-z,relro,-z,now,-z,noexecstack")
    endif()
endif()

set(STRIP_FLAG "-s")

set(CMAKE_C_FLAGS "${SECURE_CFLAG_FOR_SHARED_LIBRARY} ${WARNING_FLAGS}")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g")
set(CMAKE_C_FLAGS_RELEASE "-D_FORTIFY_SOURCE=2 -O2")
set(CMAKE_C_FLAGS_DEBUG "-O0 -g")

if(CLANG_TARGET_TRIPLE)
    # We add --target option for clang only since gcc does not support --target option.
    # In case of gcc, cross compilation requires a target-specific gcc (a cross compiler).
    add_compile_options(--target=${CLANG_TARGET_TRIPLE})
    add_link_options(--target=${CLANG_TARGET_TRIPLE})
endif()

if(CANGJIE_CMAKE_SYSROOT)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --sysroot=${CANGJIE_CMAKE_SYSROOT}")
endif()

if(CANGJIE_ENABLE_HWASAN)
    add_compile_options(-shared-libsan -fsanitize=hwaddress -fno-emulated-tls -mllvm -hwasan-globals=0 -fno-lto -fno-whole-program-vtables)
    add_link_options(-shared-libsan -fsanitize=hwaddress -fno-emulated-tls -mllvm -hwasan-globals=0 -fno-lto -fno-whole-program-vtables)
endif()

if (MINGW)
    # MSVCRT already provides most of secure functions,
    # so only files listed below need to be compiled from boundscheck library,
    # to avoid "multiple definition" error.
    # Refer to offiical manual of boundscheck library for more information.
    list(APPEND SRC_FILE src/memset_s.c src/snprintf_s.c src/vsnprintf_s.c src/secureprintoutput_a.c)
else()
    aux_source_directory(./src SRC_FILE)
endif()

# Build shared library.
add_library(boundscheck SHARED ${SRC_FILE})
add_library(boundscheck-static STATIC ${SRC_FILE})
target_include_directories(boundscheck PRIVATE ./include)
target_include_directories(boundscheck-static PRIVATE ./include)
target_link_options(boundscheck PRIVATE ${SECURE_LDFLAG_FOR_SHARED_LIBRARY} ${STRIP_FLAG})

if (EXPORT_boundscheck)
    install(DIRECTORY include DESTINATION .)
    install(TARGETS boundscheck RUNTIME DESTINATION lib
                            LIBRARY DESTINATION lib)
    install(TARGETS boundscheck-static ARCHIVE DESTINATION lib)
endif()