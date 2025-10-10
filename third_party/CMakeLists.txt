# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

include(ExternalProject)

find_program(SSH_EXECUTABLE ssh)
# Don't check known_host to get rid of interactive operations in cmake
if(CMAKE_HOST_WIN32)
    set(GIT_ARGS "ssh.variant=ssh")
else()
    set(GIT_ARGS "core.sshCommand=${SSH_EXECUTABLE}\\ -o\\ StrictHostKeyChecking=no" "ssh.variant=ssh")
endif()

set(THIRD_PARTY_LLVM "third_party/llvm")

set(LLVM_TAG master)
set(RUNTIME_TAG main)

set(GCC_TOOLCHAIN_FLAG "")
if(MINGW)
    # There will be lots of warnings when compiling backend with MinGW toolchain, temporarily suppress them.
    set(GCC_TOOLCHAIN_FLAG "${GCC_TOOLCHAIN_FLAG} -w")
endif()
if(BUILD_GCC_TOOLCHAIN)
    message(STATUS "Set GCC toolchain: ${BUILD_GCC_TOOLCHAIN}")
    set(GCC_TOOLCHAIN_FLAG "${GCC_TOOLCHAIN_FLAG} --gcc-toolchain=\"${BUILD_GCC_TOOLCHAIN}\"")
endif()

# Handle third-party dependencies
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")
include(Flatbuffer)

if(CANGJIE_BUILD_CJDB)
    include(Xml2)
endif()

set(LLVM_LIB_SUFFIX .a)
set(LLVM_LIB_NAMES
    LLVMLinker
    LLVMIRReader
    LLVMAsmParser
    LLVMTransformUtils
    LLVMBitWriter
    LLVMAnalysis
    LLVMProfileData
    LLVMDebugInfoDWARF
    LLVMDebugInfoMSF
    LLVMObject
    LLVMTextAPI
    LLVMMCParser
    LLVMMC
    LLVMDebugInfoCodeView
    LLVMBitReader
    LLVMCore
    LLVMRemarks
    LLVMBitstreamReader
    LLVMBinaryFormat
    LLVMSupport
    LLVMDemangle)
list(TRANSFORM LLVM_LIB_NAMES PREPEND "lib")
list(TRANSFORM LLVM_LIB_NAMES APPEND "${LLVM_LIB_SUFFIX}")

set(LLVM_DEP_LIB_NAMES
    LLVMBinaryFormat
    LLVMRemarks
    LLVMBitstreamReader
    LLVMSupport
    LLVMDemangle)
list(TRANSFORM LLVM_DEP_LIB_NAMES PREPEND "lib")
list(TRANSFORM LLVM_DEP_LIB_NAMES APPEND "${LLVM_LIB_SUFFIX}")
set(LLVM_LINK_FLAGS -lpthread -lm)

if(CANGJIE_BUILD_CJDB AND NOT WIN32)
    list(APPEND LLVM_LINK_FLAGS -lncurses)
    if(NOT DARWIN)
        list(APPEND LLVM_LINK_FLAGS -ltinfo)
    endif()
endif()

set(LLVM_INSTALL_PATTERN
        PATTERN "include" EXCLUDE
        PATTERN "libexec" EXCLUDE
        PATTERN "share" EXCLUDE
        PATTERN "cmake" EXCLUDE
        PATTERN "VERSION.txt" EXCLUDE
        PATTERN "llvm-config" EXCLUDE
        PATTERN "lib/lib*.a" EXCLUDE
        PATTERN "lib/liblldbIntelFeatures*" EXCLUDE
        PATTERN "lib/linux" EXCLUDE
        PATTERN "bin" EXCLUDE
)
# Only the following binaries are needed. The other unused llvm binaries will not be installed.
set(LLVM_BIN_INSTALL_PATTERN
        # For cjc general compilation
        REGEX "bin/(opt|llc)" PERMISSIONS OWNER_EXECUTE OWNER_READ GROUP_EXECUTE GROUP_READ
        # For cjc general compilation (e.g. OHOS) and LTO and lldb related binaries
        REGEX "bin/(lld|ld64.lld|ld.lld|lld-link)" PERMISSIONS OWNER_EXECUTE OWNER_READ GROUP_EXECUTE GROUP_READ
        # For cjc general compilation (e.g. OHOS)
        REGEX "bin/(llvm-ar)" PERMISSIONS OWNER_EXECUTE OWNER_READ GROUP_EXECUTE GROUP_READ
        # For LTO
        REGEX "bin/(llvm-link|llvm-lto|llvm-lto2)" PERMISSIONS OWNER_EXECUTE OWNER_READ GROUP_EXECUTE GROUP_READ
        # For cjc --coverage feature
        REGEX "bin/(llvm-cov)" PERMISSIONS OWNER_EXECUTE OWNER_READ GROUP_EXECUTE GROUP_READ
        # For PGO
        REGEX "bin/(llvm-profdata|llvm-profgen)" PERMISSIONS OWNER_EXECUTE OWNER_READ GROUP_EXECUTE GROUP_READ
        # For compiler developer debugging
        REGEX "bin/(llvm-as|llvm-dis)" PERMISSIONS OWNER_EXECUTE OWNER_READ GROUP_EXECUTE GROUP_READ
        REGEX "bin/(lli)" PERMISSIONS OWNER_EXECUTE OWNER_READ GROUP_EXECUTE GROUP_READ
        REGEX "bin/(llvm-addr2line|llvm-symbolizer)" PERMISSIONS OWNER_EXECUTE OWNER_READ GROUP_EXECUTE GROUP_READ
        REGEX "bin/(llvm-otool|llvm-objdump)" PERMISSIONS OWNER_EXECUTE OWNER_READ GROUP_EXECUTE GROUP_READ
        REGEX "bin/(llvm-objcopy)" PERMISSIONS OWNER_EXECUTE OWNER_READ GROUP_EXECUTE GROUP_READ
        # For Windows
        REGEX "bin/(libclang\\.dll|libclang-cpp\\.dll)"
        REGEX "bin/(libgcc_s_seh-1\\.dll|libssp-0\\.dll|libstdc\\+\\+-6\\.dll|libwinpthread-1\\.dll)"
        REGEX "bin/(libLTO\\.dll|libLLVM-15\\.dll|libLLVM-Foundation-15\\.dll)"
)
set(LIB_PROFILE profile)
set(LIB_FUZZER_NOMAIN fuzzer_no_main)
set(LIB_PROFILE_NAME libclang_rt-${LIB_PROFILE}.a)
if(MACOS)
    set(LIB_BUILTINS osx)
    set(LIB_BUILTINS_NAME libclang_rt.osx.a)
elseif(IOS)
    if(IOS_PLATFORM MATCHES "SIMULATOR")
        set(LIB_BUILTINS iossim)
        set(LIB_BUILTINS_NAME libclang_rt.iossim.a)
        set(LIB_PROFILE profile_iossim)
        set(LIB_PROFILE_NAME libclang_rt.profile_iossim.a)
    else()
        set(LIB_BUILTINS ios)
        set(LIB_BUILTINS_NAME libclang_rt.ios.a)
        set(LIB_PROFILE profile_ios)
        set(LIB_PROFILE_NAME libclang_rt.profile_ios.a)
    endif()
else()
    set(LIB_BUILTINS builtins)
    set(LIB_BUILTINS_NAME libclang_rt-${LIB_BUILTINS}.a)
endif()
# Keep the same name with standard libfuzzer: libclang_rt.fuzz_no_main.a
set(LIB_FUZZER_NOMAIN_NAME libclang_rt.${LIB_FUZZER_NOMAIN}.a)

set(CJNATIVE_BACKEND "cjnative")
string(TOLOWER ${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND} output_lib_dir)

install(DIRECTORY DESTINATION modules/${output_lib_dir}${SANITIZER_SUBPATH})

# binary
if(CANGJIE_CODEGEN_CJNATIVE_BACKEND) # i.e. "--compile-backend" option
    include(CompileRTLib)

    if(CANGJIE_USE_OH_LLVM_REPO)
        set(REPOSITORY_PATH "https://gitcode.com/openharmony/third_party_llvm-project.git")
        set(LLVM_TAG master)
    else()
        set(REPOSITORY_PATH "https://gitcode.com/Cangjie/llvm-project.git")
        set(LLVM_TAG dev)
    endif()
    # llvm
    message(STATUS "Set cjnative REPOSITORY_PATH: ${REPOSITORY_PATH}")
    if(CANGJIE_BUILD_CJC)
        set(LLVM_LIB_PATH ${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/lib)
        set(LLVM_LIBS)
        set(LLVM_SANITIZER)
        if(CANGJIE_ENABLE_ASAN OR CANGJIE_ENABLE_ASAN_COV)
            set(LLVM_SANITIZER Address)
        endif()
        foreach(LIB_NAME ${LLVM_LIB_NAMES})
            list(APPEND LLVM_LIBS ${LLVM_LIB_PATH}/${LIB_NAME})
        endforeach()
        set(LLVM_PROJECTS lld|clang|compiler-rt)
        if(CANGJIE_ENABLE_ASAN OR CANGJIE_ENABLE_ASAN_COV)
            set(LLVM_PROJECTS lld)
        endif()
        set(LLVM_CMAKE_C_FLAGS ${GCC_TOOLCHAIN_FLAG})
        set(LLVM_CMAKE_CXX_FLAGS ${GCC_TOOLCHAIN_FLAG})
        if(MINGW)
            set(LLVM_CMAKE_C_FLAGS "${LLVM_CMAKE_C_FLAGS} --unwindlib=libunwind")
            set(LLVM_CMAKE_CXX_FLAGS "${LLVM_CMAKE_CXX_FLAGS} --unwindlib=libunwind")
        endif()
        set(LLVM_CMAKE_EXE_LINKER_FLAGS)
        set(LLVM_CMAKE_SHARED_LINKER_FLAGS)
        string(REPLACE ";" "|" CMAKE_PREFIX_PATH_ALT_SEP "${CMAKE_PREFIX_PATH}")
        set(LLVM_CMAKE_ARGS
            # Hide warnings during configure which are intended for LLVM developers.
            -Wno-dev
            -DLLVM_ENABLE_BACKTRACES:BOOL=OFF
            -DLLVM_ENABLE_PROJECTS=${LLVM_PROJECTS}
            # Disable `libear` generation
            -DLLVM_ENABLE_Z3_SOLVER=OFF
            -DCLANG_ENABLE_STATIC_ANALYZER=OFF
            -DCLANG_ENABLE_ARCMT=OFF
            -DLLVM_ENABLE_LIBXML2=OFF
            -DLLVM_ENABLE_ZLIB=OFF
            -DLLVM_ENABLE_ZSTD=OFF
            # Build both targets in case of cross-compilation.
            -DLLVM_TARGETS_TO_BUILD=ARM|AArch64|X86
            -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}
            -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH_ALT_SEP}
            -DCOMPILER_RT_ENABLE_IOS=OFF
            # Propagate build type and compiler.
            # For now, we can only use release version
            -DCMAKE_BUILD_TYPE=${CANGJIE_LLVM_BUILD_TYPE}
            -DCMAKE_C_COMPILER=${LLVM_BUILD_C_COMPILER}
            -DCMAKE_CXX_COMPILER=${LLVM_BUILD_CXX_COMPILER}
            -DCMAKE_AR=${CMAKE_AR}
            -DCMAKE_RANLIB=${CMAKE_RANLIB}
            -DCMAKE_CXX_STANDARD=17)
        if(CANGJIE_BUILD_CJDB)
            if(NOT WIN32)
                list(APPEND LLVM_CMAKE_ARGS ${LLVM_BUILD_ARG}
                    -DLLVM_ENABLE_TERMINFO=1
                    -DCURSES_NEED_NCURSES=1)
            endif()
            list(APPEND LLVM_CMAKE_ARGS -DLLVM_ENABLE_RTTI=ON)
            # When making debug build, LLDB is debug build but LLVM is still release build. LLDB calls some
            # dump() functions which exist in LLVM debug build only. To prevent undefined references errors,
            # LLVM_ENABLE_DUMP needs to be enabled in LLVM release build for LLDB debug build.
            if(CANGJIE_CJDB_BUILD_TYPE MATCHES "^Debug$")
                list(APPEND LLVM_CMAKE_ARGS -DLLVM_ENABLE_DUMP=ON)
            endif()
        else()
            list(APPEND LLVM_CMAKE_ARGS
                # Hide warnings during configure which are intended for LLVM developers.
                -Wno-dev
                # Bindings is disabled for two reasons. First, we simply do not need it.
                # Second, installation of bindings may require extra privilege on some environments,
                # which may cause building errors.
                -DLLVM_ENABLE_BINDINGS=OFF
                -DLLVM_ENABLE_TERMINFO=OFF
                -DLLVM_ENABLE_LIBEDIT=OFF
                -DLLVM_USE_SANITIZER=${LLVM_SANITIZER}
                -DCMAKE_SKIP_RPATH:BOOL=${CMAKE_SKIP_RPATH})
        endif()
        list(APPEND LLVM_CMAKE_ARGS
            -DCMAKE_C_FLAGS=${LLVM_CMAKE_C_FLAGS}
            -DCMAKE_CXX_FLAGS=${LLVM_CMAKE_CXX_FLAGS})
        foreach(LLVM_BUILD_ARG ${CANGJIE_LLVM_BUILD_ARGS})
            list(APPEND LLVM_CMAKE_ARGS ${LLVM_BUILD_ARG})
        endforeach()
        # Build `libLLVM.so` to link against.
        list(APPEND LLVM_CMAKE_ARGS -DLLVM_BUILD_LLVM_DYLIB=ON)
        list(APPEND LLVM_CMAKE_ARGS -DLLVM_LINK_LLVM_DYLIB=ON)
        if(MINGW)
            list(APPEND LLVM_CMAKE_ARGS -DLLVM_SPLIT_LLVM_DYLIB=ON)
            list(APPEND LLVM_CMAKE_ARGS -DCOMPILER_RT_USE_BUILTINS_LIBRARY=TRUE)
            list(APPEND LLVM_CMAKE_ARGS -DCOMPILER_RT_USE_LLVM_UNWINDER=TRUE)
            list(APPEND LLVM_CMAKE_ARGS -DSANITIZER_CXX_ABI=libc++)
            list(APPEND LLVM_CMAKE_ARGS -DCMAKE_SYSTEM_NAME=Windows)
            list(APPEND LLVM_CMAKE_ARGS -DLLVM_HOST_TRIPLE=x86_64-w64-mingw32)
            set(NATIVE_LLVM_CMAKE_ARGS -Wno-dev|-Wno-deprecated)
            list(APPEND LLVM_CMAKE_ARGS -DCROSS_TOOLCHAIN_FLAGS_LLVM_NATIVE=${NATIVE_LLVM_CMAKE_ARGS})
            set(LLVM_CMAKE_EXE_LINKER_FLAGS "${LLVM_CMAKE_EXE_LINKER_FLAGS} -Wl,--no-insert-timestamp -lunwind")
            set(LLVM_CMAKE_SHARED_LINKER_FLAGS "${LLVM_CMAKE_SHARED_LINKER_FLAGS} -Wl,--no-insert-timestamp -lunwind")
        endif()
        list(APPEND LLVM_CMAKE_ARGS -DCMAKE_EXE_LINKER_FLAGS=${LLVM_CMAKE_EXE_LINKER_FLAGS})
        list(APPEND LLVM_CMAKE_ARGS -DCMAKE_SHARED_LINKER_FLAGS=${LLVM_CMAKE_SHARED_LINKER_FLAGS})
        # ranlib / ar on Windows may fail to create temp files when multiple processes are invoked simultaneously,
        # so the maximum number of parallel linking jobs needs to be limit to 1.
        if(CMAKE_HOST_WIN32)
            list(APPEND LLVM_CMAKE_ARGS -DLLVM_PARALLEL_LINK_JOBS=1)
        endif()
        set(boundscheck_suffix ${CMAKE_SHARED_LIBRARY_SUFFIX})
        set(boundscheck_target_suffix ${CMAKE_SHARED_LIBRARY_SUFFIX})
        if(WIN32)
            set(boundscheck_suffix "-static.a")
            set(boundscheck_target_suffix ".a")
        endif()
        set(APPLY_LLVM_PATCH_COMMAND ${CMAKE_COMMAND} -E echo "")
        if(EXISTS ${CANGJIE_CJNATIVE_SOURCE_DIR})
            if(CANGJIE_USE_OH_LLVM_REPO)
                set(APPLY_LLVM_PATCH_COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyLLVMPatch.cmake" ${CANGJIE_CJNATIVE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/third_party/llvmPatch.diff)
            endif()
            ExternalProject_Add(
                cjnative
                SOURCE_DIR ${CANGJIE_CJNATIVE_SOURCE_DIR}
                BUILD_BYPRODUCTS ${LLVM_LIBS}
                SOURCE_SUBDIR llvm
                PATCH_COMMAND ${APPLY_LLVM_PATCH_COMMAND}
                    COMMAND ${CMAKE_COMMAND} -E make_directory <SOURCE_DIR>/utils/demangle
                    COMMAND ${CMAKE_COMMAND} -E make_directory <SOURCE_DIR>/utils/boundscheck/include
                    COMMAND ${CMAKE_COMMAND} -E make_directory <SOURCE_DIR>/utils/boundscheck/lib/${CMAKE_SYSTEM_PROCESSOR}
                    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/cangjie-demangler/include/CangjieDemangle.h
                            <SOURCE_DIR>/utils/demangle
                    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/cangjie-demangler/lib/libcangjie-demangle.a
                            <SOURCE_DIR>/utils/demangle
                    COMMAND ${CMAKE_COMMAND} -E copy
                            ${BOUNDSCHECK}/include/securec.h
                            ${BOUNDSCHECK}/include/securectype.h
                            <SOURCE_DIR>/utils/boundscheck/include
                    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/lib/libboundscheck${boundscheck_suffix}
                            <SOURCE_DIR>/utils/boundscheck/lib/${CMAKE_SYSTEM_PROCESSOR}/libboundscheck${boundscheck_target_suffix}
                LIST_SEPARATOR |
                CMAKE_ARGS ${LLVM_CMAKE_ARGS}
                USES_TERMINAL_BUILD ON
                DEPENDS cangjie-demangler boundscheck)
        else()
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
                cjnative
                ${LLVM_REPO_DOWNLOAD_ARGS}
                BUILD_BYPRODUCTS ${LLVM_LIBS}
                SOURCE_SUBDIR llvm
                PATCH_COMMAND ${APPLY_LLVM_PATCH_COMMAND}
                    COMMAND ${CMAKE_COMMAND} -E make_directory <SOURCE_DIR>/utils/demangle
                    COMMAND ${CMAKE_COMMAND} -E make_directory <SOURCE_DIR>/utils/boundscheck/include
                    COMMAND ${CMAKE_COMMAND} -E make_directory <SOURCE_DIR>/utils/boundscheck/lib/${CMAKE_SYSTEM_PROCESSOR}
                    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/cangjie-demangler/include/CangjieDemangle.h
                            <SOURCE_DIR>/utils/demangle
                    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/cangjie-demangler/lib/libcangjie-demangle.a
                            <SOURCE_DIR>/utils/demangle
                    COMMAND ${CMAKE_COMMAND} -E copy
                            ${BOUNDSCHECK}/include/securec.h
                            ${BOUNDSCHECK}/include/securectype.h
                            <SOURCE_DIR>/utils/boundscheck/include
                    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/lib/libboundscheck${boundscheck_suffix}
                            <SOURCE_DIR>/utils/boundscheck/lib/${CMAKE_SYSTEM_PROCESSOR}/libboundscheck${boundscheck_target_suffix}
                LIST_SEPARATOR |
                CMAKE_ARGS ${LLVM_CMAKE_ARGS}
                USES_TERMINAL_BUILD ON
                DEPENDS cangjie-demangler boundscheck)
        endif()
        ExternalProject_Get_Property(cjnative SOURCE_DIR)
        add_custom_command(
            TARGET cjnative POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E $<IF:$<VERSION_GREATER_EQUAL:${CMAKE_VERSION},3.17>,rm,remove> -f
                ${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/$<IF:$<BOOL:${MINGW}>,bin,lib>/libRemarks.*
            COMMENT "Removing unnecessary llvm libraries..."
        )
        if(CANGJIE_BUILD_CJDB)
            include(BuildCJDB)
        endif()
    elseif (NOT CANGJIE_SKIP_BUILD_CLANG_RT)
        compile_rtlib(cjnative INSTALL_PREFIX ${CMAKE_BINARY_DIR}/third_party/llvm)
        ExternalProject_Get_Property(cjnative SOURCE_DIR)
    endif()

    if(NOT CANGJIE_SKIP_BUILD_CLANG_RT)
        # Set name and path of the profile library
        set(LIB_RTLIB_ROOT_PATH ${CMAKE_BINARY_DIR}/lib/)
        set(LIB_PROFILE_PATH ${LIB_RTLIB_ROOT_PATH}/${LIB_PROFILE_NAME})
        set(LIB_BUILTINS_PATH ${LIB_RTLIB_ROOT_PATH}/${LIB_BUILTINS_NAME})
        set(LIB_FUZZER_NOMAIN_PATH ${LIB_RTLIB_ROOT_PATH}/${LIB_FUZZER_NOMAIN_NAME})
        if(NOT (CANGJIE_ENABLE_ASAN OR CANGJIE_ENABLE_ASAN_COV))
            set(KEEP_CLANG_CPP_LIBS ${CANGJIE_BUILD_CJDB})
            set(COMMAND_REMOVE_AFTER_EXTRACTION
                COMMAND
                ${CMAKE_COMMAND}
                -P
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RemoveByproductLib.cmake"
                ${CMAKE_BINARY_DIR}
                ${THIRD_PARTY_LLVM}
                ${KEEP_CLANG_CPP_LIBS})
        endif()
        set(CLANG_RT_LIBS)
        # copy the builtin and profile library
        if(NOT OHOS)
            if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64" OR DARWIN)
                list(APPEND CLANG_RT_LIBS ${LIB_BUILTINS_PATH})
            endif()
            list(APPEND CLANG_RT_LIBS ${LIB_PROFILE_PATH})
            if(NOT WIN32 AND NOT IOS)
                list(APPEND CLANG_RT_LIBS ${LIB_FUZZER_NOMAIN_PATH})
            endif()
        endif()
        set(CLANG_RT_TARGET_SUFFIX ${CMAKE_SYSTEM_PROCESSOR}$<$<BOOL:${ANDROID}>:-android>)
        if(CANGJIE_ASAN_SUPPORT)
            list(APPEND CLANG_RT_LIBS ${LIB_RTLIB_ROOT_PATH}/libclang_rt-asan.a)
            set(SANITIZER_COPY_CMD
                COMMAND
                ${CMAKE_COMMAND}
                -P
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ExtractRtlib.cmake"
                ${CMAKE_BINARY_DIR}
                asan
                ${LIB_RTLIB_ROOT_PATH}/libclang_rt-asan.a
                ${CLANG_RT_TARGET_SUFFIX}
                ${CANGJIE_BUILD_CJC}
                ${THIRD_PARTY_LLVM}
                ${IOS})
        elseif(CANGJIE_TSAN_SUPPORT)
            list(APPEND CLANG_RT_LIBS ${LIB_RTLIB_ROOT_PATH}/libclang_rt-tsan.a)
            set(SANITIZER_COPY_CMD
                COMMAND
                ${CMAKE_COMMAND}
                -P
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ExtractRtlib.cmake"
                ${CMAKE_BINARY_DIR}
                tsan_cangjie
                ${LIB_RTLIB_ROOT_PATH}/libclang_rt-tsan.a
                ${CLANG_RT_TARGET_SUFFIX}
                ${CANGJIE_BUILD_CJC}
                ${THIRD_PARTY_LLVM}
                ${IOS})
        elseif(CANGJIE_HWASAN_SUPPORT)
            list(APPEND CLANG_RT_LIBS ${LIB_RTLIB_ROOT_PATH}/libclang_rt-hwasan.a)
            set(SANITIZER_COPY_CMD
                COMMAND
                ${CMAKE_COMMAND}
                -P
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ExtractRtlib.cmake"
                ${CMAKE_BINARY_DIR}
                hwasan
                ${LIB_RTLIB_ROOT_PATH}/libclang_rt-hwasan.a
                ${CMAKE_SYSTEM_PROCESSOR}
                ${CANGJIE_BUILD_CJC}
                ${THIRD_PARTY_LLVM}
                ${IOS})
        endif()
        if(NOT WIN32 AND NOT IOS)
            set(FUZZER_NOMAIN_COPY_CMD
                COMMAND
                ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ExtractRtlib.cmake"
                ${CMAKE_BINARY_DIR}
                ${LIB_FUZZER_NOMAIN}
                ${LIB_FUZZER_NOMAIN_PATH}
                ${CLANG_RT_TARGET_SUFFIX}
                ${CANGJIE_BUILD_CJC}
                ${THIRD_PARTY_LLVM}
                ${IOS})
        endif()
        if(NOT CANGJIE_ENABLE_ASAN AND NOT CANGJIE_ENABLE_ASAN_COV)
            # Generate version info and extract the profile library
            add_custom_command(
                TARGET cjnative
                POST_BUILD
                COMMAND git log -1 --oneline --decorate > ${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/VERSION.txt
                # The command `cmake -P ExtractRtlib.cmake` will run the `file` commands like `file(GLOB ...)`
                # at the building stage instead of the configuration stage.
                COMMAND
                    ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ExtractRtlib.cmake"
                    ${CMAKE_BINARY_DIR}
                    ${LIB_PROFILE}
                    ${LIB_PROFILE_PATH}
                    ${CLANG_RT_TARGET_SUFFIX}
                    ${CANGJIE_BUILD_CJC}
                    ${THIRD_PARTY_LLVM}
                    ${IOS}
                COMMAND
                    ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ExtractRtlib.cmake"
                    ${CMAKE_BINARY_DIR}
                    ${LIB_BUILTINS}
                    ${LIB_BUILTINS_PATH}
                    ${CLANG_RT_TARGET_SUFFIX}
                    ${CANGJIE_BUILD_CJC}
                    ${THIRD_PARTY_LLVM}
                    ${IOS}
                ${FUZZER_NOMAIN_COPY_CMD}
                ${SANITIZER_COPY_CMD}
                ${COMMAND_REMOVE_AFTER_EXTRACTION}
                # copy to runtime/lib for build-binary-tar
                COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/third_party/runtime/lib
                COMMAND ${CMAKE_COMMAND} -E copy ${CLANG_RT_LIBS} ${CMAKE_BINARY_DIR}/third_party/runtime/lib
                WORKING_DIRECTORY ${SOURCE_DIR})
        else()
            # this condition is equals to disable asan rt-lib compile (i.e. NOT CANGJIE_ASAN_SUPPORT/CANGJIE_TSAN_SUPPORT),
            # so just don't add asan compile here

            # When compiling asan version, clang_rt-profile will not be generated even compile-rt
            # project is added. To be able to supply clang_rt-profile for --coverage support, we
            # have to compile clang-rt again without asan settings.
            compile_rtlib(cjnative-rtlib INSTALL_PREFIX ${CMAKE_BINARY_DIR}/third_party/llvm-rtlib)
            set(LIB_BUILTINS_COPY_COMMAND COMMAND
                ${CMAKE_COMMAND} -E copy
                ${CMAKE_BINARY_DIR}/third_party/llvm-rtlib/lib/*/libclang_rt.${LIB_BUILTINS}-${CMAKE_SYSTEM_PROCESSOR}.a
                ${LIB_BUILTINS_PATH}
            )
            if(DARWIN)
                set(LIB_BUILTINS_COPY_COMMAND)
            endif()
            add_custom_command(
                TARGET cjnative-rtlib
                POST_BUILD
                COMMAND
                    ${CMAKE_COMMAND} -E copy
                    ${CMAKE_BINARY_DIR}/third_party/llvm-rtlib/lib/*/libclang_rt.${LIB_PROFILE}-${CMAKE_SYSTEM_PROCESSOR}.a
                    ${LIB_PROFILE_PATH}
                # The command `cmake -P ExtractRtlib.cmake` will run the `file` commands like `file(GLOB ...)`
                # at the building stage instead of the configuration stage.
                ${LIB_BUILTINS_COPY_COMMAND}
                COMMAND
                    ${CMAKE_COMMAND} -E copy
                    ${CMAKE_BINARY_DIR}/third_party/llvm-rtlib/lib/*/libclang_rt.${LIB_FUZZER_NOMAIN}-${CMAKE_SYSTEM_PROCESSOR}.a
                    ${LIB_FUZZER_NOMAIN_PATH}
                ${COMMAND_REMOVE_AFTER_EXTRACTION}
                # copy to runtime/lib for build-binary-tar
                COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/third_party/runtime/lib
                COMMAND ${CMAKE_COMMAND} -E copy ${CLANG_RT_LIBS} ${CMAKE_BINARY_DIR}/third_party/runtime/lib
                WORKING_DIRECTORY ${SOURCE_DIR})
        endif()
        if(CANGJIE_BUILD_CJC)
            install(
                DIRECTORY ${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/
                DESTINATION ${THIRD_PARTY_LLVM}
                ${LLVM_INSTALL_PATTERN})
            install(
                DIRECTORY ${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/bin
                DESTINATION ${THIRD_PARTY_LLVM}
                FILES_MATCHING
                ${LLVM_BIN_INSTALL_PATTERN}
                )
        endif()
        install(FILES ${CLANG_RT_LIBS} DESTINATION lib/${output_lib_dir}${SANITIZER_SUBPATH})

        set(LLVM_INCLUDE_DIRS
            ${CMAKE_BINARY_DIR}/${THIRD_PARTY_LLVM}/include
            PARENT_SCOPE)
        # for other targets to use
        foreach(LIB_NAME ${LLVM_DEP_LIB_NAMES})
            list(APPEND LLVM_LIBS ${LLVM_LIB_PATH}/${LIB_NAME})
        endforeach()
        list(APPEND LLVM_LIBS ${LLVM_LINK_FLAGS})
        set(LLVM_LIBS
            ${LLVM_LIBS}
            PARENT_SCOPE)
    endif()
    if(MINGW)
        include(AddMinGWLibs)
    endif()
    if(OHOS)
        include(AddOHOSLibs)
    endif()

    set(BINARY_TARGET_DEPENDS runtime)
    if(NOT CANGJIE_SKIP_BUILD_CLANG_RT AND NOT OHOS)
        list(APPEND BINARY_TARGET_DEPENDS cjnative)
    endif()
endif()
