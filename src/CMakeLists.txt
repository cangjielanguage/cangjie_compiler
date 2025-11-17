# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

# This if branch is used by --product=libs, which just builds std lib.
# Standard library cangjie-std-ast need these targets.

if(CANGJIE_ENABLE_COMPILER_TSAN)
    add_compile_options("${TSAN_FLAGS}")
    add_link_options("${TSAN_FLAGS}")
endif()

if(IOS)
    get_directory_property(SRC_COMPILE_OPTIONS COMPILE_OPTIONS)
    list(REMOVE_ITEM SRC_COMPILE_OPTIONS "--target=${TRIPLE}")
    set_directory_properties(PROPERTIES COMPILE_OPTIONS "${SRC_COMPILE_OPTIONS}")
    add_compile_options(--target=${CXX_COMPATIABLE_TRIPLE})
endif()

set(CJC_EXTRA_WARNINGS -Woverloaded-virtual -Wvla -Wconversion -Wfloat-equal -Wunused -Wdelete-non-virtual-dtor -Wcast-qual -Wextra -Wformat=2 -Wno-missing-field-initializers -Wno-format-nonliteral)
if(${CMAKE_SYSTEM_NAME} EQUAL Darwin)
    # mac uses llvm headers for std, and llvm headers have the following warning(s)
    set(CJC_EXTRA_WARNINGS ${CJC_EXTRA_WARNINGS} -Wno-float-equal)
    # mac headers have warning on undefed macro(s) (FFI_GO_CLOSURES), so use -Wno-undef
    set(CJC_EXTRA_WARNINGS ${CJC_EXTRA_WARNINGS} -Wno-undef)
endif()
if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang" OR ${CMAKE_CXX_COMPILER_ID} STREQUAL "AppleClang")
    # g++ on some platform has harsher check on -Werror=shadow, in that it does not allow function parameters to share the same name with class members, which is actually bad in constructor. So we disable this on some platform.
    set(CJC_EXTRA_WARNINGS ${CJC_EXTRA_WARNINGS} -Wshadow)
endif()
set(CJC_EXTRA_WARNINGS ${CJC_EXTRA_WARNINGS} -Wno-switch-default)

# when including llvm header files, the following warnings have to be excluded
set(CJC_WITH_LLVM_EXTRA_WARNINGS ${CJC_EXTRA_WARNINGS} -Wno-sign-conversion -Wno-unused-parameter -Wno-float-conversion -Wno-shadow)
include(CheckCXXCompilerFlag)
if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang" OR ${CMAKE_CXX_COMPILER_ID} STREQUAL "AppleClang")
    # gcc does not recognise these options
    set(CJC_WITH_LLVM_EXTRA_WARNINGS ${CJC_WITH_LLVM_EXTRA_WARNINGS} -Wno-implicit-int-conversion -Wno-shorten-64-to-32 -Wno-implicit-int-float-conversion)
    check_cxx_compiler_flag("-Wno-unnecessary-virtual-specifier" COMPILER_SUPPORTS_UNNECESSARY_VIRTUAL)
    if(COMPILER_SUPPORTS_UNNECESSARY_VIRTUAL)
        set(CJC_WITH_LLVM_EXTRA_WARNINGS ${CJC_WITH_LLVM_EXTRA_WARNINGS} -Wno-unnecessary-virtual-specifier)
    endif()
endif()

if(NOT CANGJIE_BUILD_CJC AND CANGJIE_BUILD_STD_SUPPORT)
    add_subdirectory(AST)
    add_subdirectory(Driver)
    add_subdirectory(Parse)
    add_subdirectory(Macro)
    add_subdirectory(ConditionalCompilation)
    add_subdirectory(Lex)
    add_subdirectory(Utils)
    add_subdirectory(Basic)
    add_subdirectory(Option)
    if (CANGJIE_CODEGEN_CJNATIVE_BACKEND)
        add_subdirectory(MetaTransformation)
    endif()

    set(AST_FFI_OBJECTS
        $<TARGET_OBJECTS:CangjieASTCommon>
        $<TARGET_OBJECTS:CangjieParseFFI>
        $<TARGET_OBJECTS:CangjieLex>
        $<TARGET_OBJECTS:CangjieBasic>
        $<TARGET_OBJECTS:CangjieOption>
        $<TARGET_OBJECTS:CangjieAstSerialization>
        $<TARGET_OBJECTS:CangjieCommonUtilFFI>
        $<TARGET_OBJECTS:CangjieUnicodeUtils>
        $<TARGET_OBJECTS:CangjieTempFilesUtil>)
    add_library(cangjie-ast-support STATIC ${AST_FFI_OBJECTS})
    string(TOLOWER ${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND} output_lib_dir)
    install(TARGETS cangjie-ast-support DESTINATION lib/${output_lib_dir}${SANITIZER_SUBPATH})
    return()
endif()

set(CANGJIE_SRC_COMMON_OBJECTS_LIST
    $<TARGET_OBJECTS:CangjieASTCommon>
    $<TARGET_OBJECTS:CangjieASTExtra>
    $<TARGET_OBJECTS:CangjieDriver>
    $<TARGET_OBJECTS:CangjieFrontend>
    $<TARGET_OBJECTS:CangjieFrontendTool>
    $<TARGET_OBJECTS:CangjieMacro>
    $<TARGET_OBJECTS:CangjieAstSerialization>
    $<TARGET_OBJECTS:CangjieSema>
    $<TARGET_OBJECTS:CangjieModules>
    $<TARGET_OBJECTS:CangjieConditionalCompilation>
    $<TARGET_OBJECTS:CangjieParse>
    $<TARGET_OBJECTS:CangjieLex>
    $<TARGET_OBJECTS:CangjieUnicodeUtils>
    $<TARGET_OBJECTS:CangjieIncrementalCompilation>
    $<TARGET_OBJECTS:CangjieCommonUtil>
    $<TARGET_OBJECTS:CangjieProfileUtils>
    $<TARGET_OBJECTS:CangjieBasic>
    $<TARGET_OBJECTS:CangjieMangle>)

if (CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    set(CANGJIE_SRC_COMMON_OBJECTS_LIST ${CANGJIE_SRC_COMMON_OBJECTS_LIST} $<TARGET_OBJECTS:CangjieMetaTransformation>)
endif()

if (CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    list(APPEND CANGJIE_SRC_COMMON_OBJECTS_LIST $<TARGET_OBJECTS:CangjieCHIRBase>)
    list(APPEND CANGJIE_SRC_COMMON_OBJECTS_LIST $<TARGET_OBJECTS:CangjieCHIRExtra>)
    list(APPEND CANGJIE_SRC_COMMON_OBJECTS_LIST $<TARGET_OBJECTS:CangjieCodeGen>)
    list(APPEND CANGJIE_SRC_COMMON_OBJECTS_LIST $<TARGET_OBJECTS:CangjieCodeGenIncrementalGen>)
else()
    list(APPEND CANGJIE_SRC_COMMON_OBJECTS_LIST $<TARGET_OBJECTS:CangjieCHIRBase>)
    list(APPEND CANGJIE_SRC_COMMON_OBJECTS_LIST $<TARGET_OBJECTS:CangjieCHIRExtra>)
    list(APPEND CANGJIE_SRC_COMMON_OBJECTS_LIST $<TARGET_OBJECTS:CangjieCodeGen>)
endif()

# By using `CangjieOption-Fully-Enabled`, we could still have a fully featured compiler for
# compiling standard library when building for a visible options only version.
if(CANGJIE_VISIBLE_OPTIONS_ONLY)
    set(CANGJIE_SRC_OBJECTS
        ${CANGJIE_SRC_COMMON_OBJECTS_LIST} $<TARGET_OBJECTS:CangjieOption-Fully-Enabled>
        CACHE INTERNAL "")
else()
    set(CANGJIE_SRC_OBJECTS
        ${CANGJIE_SRC_COMMON_OBJECTS_LIST} $<TARGET_OBJECTS:CangjieOption>
        CACHE INTERNAL "")
endif()
add_executable(cjc main.cpp ${CANGJIE_SRC_OBJECTS})

if(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    add_dependencies(cjc cjnative)
    set(CJNATIVE_BACKEND "cjnative")
    string(TOLOWER ${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}_${CJNATIVE_BACKEND} output_lib_dir)
    set(RUNTIME_LIB_DIR ${CMAKE_BINARY_DIR}/lib/${output_lib_dir})
endif()
set_target_properties(cjc PROPERTIES LINK_FLAGS
                                     "${CMAKE_EXE_LINKER_FLAGS} ${SAFE_EXE_LINK_FLAG} ${CJ_RUNTIME_LINK_FLAGS}")

target_link_libraries(
    cjc ${LLVM_LIBS}
    # Required by CangjieBasic
    ${CMAKE_DL_LIBS})

target_link_libraries(cjc boundscheck-static) # depended by LLVMCore in ${LINK_LIBS} and interrupt signal handler
target_include_directories(cjc PRIVATE ${BOUNDSCHECK}/include)

target_link_libraries(cjc ${ffi})

if(MINGW)
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -print-file-name=CRT_glob.o
        OUTPUT_VARIABLE GET_CRT_GLOB_OUTPUT
        ERROR_VARIABLE GET_CRT_GLOB_ERROR
        RESULT_VARIABLE GET_CRT_GLOB_RESULT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT GET_CRT_GLOB_RESULT EQUAL 0)
        message(FATAL_ERROR "Cannot get filepath of CRT_glob.o! Error message is: ${GET_CRT_GLOB_ERROR}")
    endif()
    target_link_libraries(cjc ${GET_CRT_GLOB_OUTPUT})
endif()

if(CMAKE_BUILD_TYPE MATCHES Release AND MINGW)
    # Add static link option for cjc when building MinGW release. This use
    # case is defined in GCC Runtime Libraries Exception and has no GPL issue.
    target_link_options(cjc PRIVATE -static)
endif()
install(TARGETS cjc)

# `cjc-frontend.exe` is a special wrapper executable to run our compiler frontend on windows.
# On windows platform, symbolic link can only be created with command prompt with administrator
# privilege. To be able to build the project with no special privilege, our build strategy is as
# follows:
# 1) On windows Platform && building Release:
#    Use our wrapper executable as `cjc-frontend.exe`. Debug of `cjc.exe` by calling `cjc-frontend.exe`
#    would be unavailable since they are different programs. The size of `cjc-frontend.exe` is small.
#    No special privilege is required.
# 2) On windows Platform && building Debug && without administrator privilege:
#    Simply make a copy of `cjc.exe` and name it `cjc-frontend.exe`. Same debugging experience between
#    `cjc.exe` and `cjc-frontend.exe`. The size of `cjc-frontend.exe` would be the same as `cjc.exe`,
#    which means that much space would be used. Since it is for debugging only, it should be tolerable.
# 3) On windows Platform && building Debug && with administrator privilege / On Linux:
#    Create symbolic link `cjc-frontend(.exe)` pointing to `cjc(.exe)`. Same debugging experience between
#    `cjc(.exe)` and `cjc-frontend(.exe)`. Minimum size usage.
if(WIN32 AND CMAKE_BUILD_TYPE MATCHES Release)
    add_executable(cjc-frontend main-frontend.cpp)
    target_link_options(cjc-frontend PRIVATE -static)
else()
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/bin/cjc-frontend${CMAKE_EXECUTABLE_SUFFIX}
        COMMAND ${CMAKE_COMMAND}
            -D CMAKE_CROSSCOMPILING=${CMAKE_CROSSCOMPILING}
            -D WIN32=${WIN32}
            -D LINK_TARGET=cjc${CMAKE_EXECUTABLE_SUFFIX}
            -D LINK_NAME=cjc-frontend${CMAKE_EXECUTABLE_SUFFIX}
            -D WORKING_DIR=${CMAKE_BINARY_DIR}/bin
            -P "${CMAKE_SOURCE_DIR}/cmake/modules/make-symlink.cmake"
        DEPENDS cjc)
    add_custom_target(
        cjc-frontend ALL
        DEPENDS ${CMAKE_BINARY_DIR}/bin/cjc-frontend${CMAKE_EXECUTABLE_SUFFIX}
        COMMENT "Making cjc-frontend${CMAKE_EXECUTABLE_SUFFIX}")
endif()

if(COMPILER_EXPLORER_RACE_FIX)
    add_compile_definitions(COMPILER_EXPLORER_RACE_FIX)
endif()

if(WIN32 AND CMAKE_BUILD_TYPE MATCHES Release)
    install(TARGETS cjc-frontend)
else()
    install(
        CODE "execute_process(COMMAND ${CMAKE_COMMAND}
        -D CMAKE_CROSSCOMPILING=${CMAKE_CROSSCOMPILING}
        -D WIN32=${WIN32}
        -D LINK_TARGET=cjc${CMAKE_EXECUTABLE_SUFFIX}
        -D LINK_NAME=cjc-frontend${CMAKE_EXECUTABLE_SUFFIX}
        -D WORKING_DIR=\${CMAKE_INSTALL_PREFIX}/bin
        -P ${CMAKE_SOURCE_DIR}/cmake/modules/make-symlink.cmake)")
endif()

if(MINGW AND CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo|MinSizeRelWithDebInfo)$")
    add_custom_command(
        TARGET cjc
        COMMAND ${CMAKE_COMMAND} -E rename cjc.exe cjc-original.exe
        COMMAND objcopy --only-keep-debug --compress-debug-sections cjc-original.exe cjc.debug
        COMMAND objcopy --strip-debug --add-gnu-debuglink=cjc.debug cjc-original.exe cjc.exe
        BYPRODUCTS cjc.debug
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        COMMENT "Extracting debug info from cjc.exe to cjc.debug")
    install(
        FILES ${CMAKE_BINARY_DIR}/bin/cjc.debug
        DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
endif()

# The autosdk, IDE need this library.
set(FRONTEND_SRC_OBJECTS
    $<TARGET_OBJECTS:CangjieASTCommon>
    $<TARGET_OBJECTS:CangjieASTExtra>
    $<TARGET_OBJECTS:CangjieFrontend>
    $<TARGET_OBJECTS:CangjieMacro>
    $<TARGET_OBJECTS:CangjieAstSerialization>
    $<TARGET_OBJECTS:CangjieSema>
    $<TARGET_OBJECTS:CangjieModules>
    $<TARGET_OBJECTS:CangjieParse>
    $<TARGET_OBJECTS:CangjieConditionalCompilation>
    $<TARGET_OBJECTS:CangjieLex>
    $<TARGET_OBJECTS:CangjieUnicodeUtils>
    $<TARGET_OBJECTS:CangjieIncrementalCompilation>
    $<TARGET_OBJECTS:CangjieOption>
    $<TARGET_OBJECTS:CangjieCommonUtil>
    $<TARGET_OBJECTS:CangjieProfileUtils>
    $<TARGET_OBJECTS:CangjieBasic>
    $<TARGET_OBJECTS:CangjieMangle>
    $<TARGET_OBJECTS:CangjieTempFilesUtil>)

if (CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    set(FRONTEND_SRC_OBJECTS ${FRONTEND_SRC_OBJECTS} $<TARGET_OBJECTS:CangjieMetaTransformation>)
endif()

if (CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    list(APPEND FRONTEND_SRC_OBJECTS $<TARGET_OBJECTS:CangjieCHIRBase>)
    list(APPEND FRONTEND_SRC_OBJECTS $<TARGET_OBJECTS:CangjieCHIRExtra>)
else()
    list(APPEND FRONTEND_SRC_OBJECTS $<TARGET_OBJECTS:CangjieCHIRBase>)
    list(APPEND FRONTEND_SRC_OBJECTS $<TARGET_OBJECTS:CangjieCHIRExtra>)
endif()
add_library(cangjie-lsp ${FRONTEND_SRC_OBJECTS})
if(MINGW AND CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo|MinSizeRelWithDebInfo)$")
    set(WIN_DEBUG_USE_STATIC_LIB ON)
endif()
if(NOT WIN_DEBUG_USE_STATIC_LIB)
    add_library(cangjie-lsp-share SHARED ${FRONTEND_SRC_OBJECTS})
    set_target_properties(cangjie-lsp-share PROPERTIES OUTPUT_NAME "cangjie-lsp")
    target_link_libraries(cangjie-lsp-share ${ffi} boundscheck-static)
    if(MINGW)
        target_link_options(cangjie-lsp-share PRIVATE -pthread)
    endif()
endif()

#  We should remove this once the LSPCompilerInstance is out of compiler
if(WIN_DEBUG_USE_STATIC_LIB)
    install(TARGETS cangjie-lsp DESTINATION "tools/lib")
endif()
if(CANGJIE_CODEGEN_CJNATIVE_BACKEND AND NOT WIN_DEBUG_USE_STATIC_LIB)
    install(
        TARGETS cangjie-lsp-share
        RUNTIME DESTINATION tools/bin
        ARCHIVE DESTINATION tools/lib
        LIBRARY DESTINATION tools/lib)
endif()

# LSPMacroServer for lsp
add_executable(LSPMacroServer main-macrosrv.cpp)
if(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    add_dependencies(LSPMacroServer cjnative)
endif()
set_target_properties(LSPMacroServer PROPERTIES LINK_FLAGS
                                    "${CMAKE_EXE_LINKER_FLAGS} ${SAFE_EXE_LINK_FLAG} ${CJ_RUNTIME_LINK_FLAGS}")

target_link_libraries(
    LSPMacroServer ${LLVM_LIBS}
    # Required by CangjieBasic
    ${CMAKE_DL_LIBS})

target_link_libraries(LSPMacroServer boundscheck-static) # depended by LLVMCore in ${LINK_LIBS} and interrupt signal handler
target_include_directories(LSPMacroServer PRIVATE ${BOUNDSCHECK}/include)

add_dependencies(LSPMacroServer CangjieFlatbuffersHeaders)
target_include_directories(LSPMacroServer PRIVATE ${FLATBUFFERS_INCLUDE_DIR})
if(WIN_DEBUG_USE_STATIC_LIB)
    target_link_libraries(LSPMacroServer ${ffi} cangjie-lsp)
else()
    target_link_libraries(LSPMacroServer ${ffi} cangjie-lsp-share)
endif()
# Add the rpath for LSPMacroServer
if(DARWIN)
    set_property(TARGET LSPMacroServer APPEND PROPERTY INSTALL_RPATH "@loader_path/../lib")
else()
    set_property(TARGET LSPMacroServer APPEND PROPERTY INSTALL_RPATH "\$ORIGIN/../lib")
endif()
install(TARGETS LSPMacroServer DESTINATION "tools/bin")

# The chir-dis
add_executable(chir-dis main-chir-dis.cpp)
if(CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    add_dependencies(chir-dis cjnative)
endif()
set_target_properties(chir-dis PROPERTIES LINK_FLAGS
                                    "${CMAKE_EXE_LINKER_FLAGS} ${SAFE_EXE_LINK_FLAG} ${CJ_RUNTIME_LINK_FLAGS}")

target_link_libraries(
    chir-dis ${LLVM_LIBS}
    # Required by CangjieBasic
    ${CMAKE_DL_LIBS})

target_link_libraries(chir-dis boundscheck-static) # Depended by LLVMCore in ${LINK_LIBS} and interrupt signal handler
target_include_directories(chir-dis PRIVATE ${BOUNDSCHECK}/include)

add_dependencies(chir-dis CangjieFlatbuffersHeaders)
target_include_directories(chir-dis PRIVATE ${FLATBUFFERS_INCLUDE_DIR})
if(WIN_DEBUG_USE_STATIC_LIB)
    target_link_libraries(chir-dis ${ffi} cangjie-lsp)
else()
    target_link_libraries(chir-dis ${ffi} cangjie-lsp-share)
endif()
# Add the rpath for chir-dis
if(DARWIN)
    set_property(TARGET chir-dis APPEND PROPERTY INSTALL_RPATH "@loader_path/../lib")
else()
    set_property(TARGET chir-dis APPEND PROPERTY INSTALL_RPATH "\$ORIGIN/../lib")
endif()
install(TARGETS chir-dis DESTINATION "tools/bin")


set(CANGJIE_LIB_FRONTEND
    $<TARGET_OBJECTS:CangjieDriver>
    $<TARGET_OBJECTS:CangjieFrontendTool>)
if (CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    list(APPEND CANGJIE_LIB_FRONTEND $<TARGET_OBJECTS:CangjieCodeGen>)
    list(APPEND CANGJIE_LIB_FRONTEND $<TARGET_OBJECTS:CangjieCodeGenIncrementalGen>)
endif()
if(NOT WIN_DEBUG_USE_STATIC_LIB)
    add_library(cangjie-frontend SHARED ${CANGJIE_LIB_FRONTEND})
else()
    add_library(cangjie-frontend ${CANGJIE_LIB_FRONTEND})
endif()
if (CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    if(NOT WIN_DEBUG_USE_STATIC_LIB)
        target_link_libraries(cangjie-frontend cangjie-lsp-share)
        if(MINGW)
            target_link_libraries(cangjie-frontend -pthread)
        endif() 
    endif()
    if(DARWIN)
        target_link_libraries(cangjie-frontend -lLLVM)
    elseif(MINGW)
	    target_link_libraries(cangjie-frontend -lLLVM-15 -lLLVM-Foundation-15)
    else()
        target_link_libraries(cangjie-frontend -lLLVM-15)
    endif()
    target_link_directories(cangjie-frontend PUBLIC ${CMAKE_BINARY_DIR}/third_party/llvm/lib)
    install(
        TARGETS cangjie-frontend
        RUNTIME DESTINATION tools/bin
        ARCHIVE DESTINATION tools/lib
        LIBRARY DESTINATION tools/lib)
endif()
if(CANGJIE_BUILD_CJDB)
    add_dependencies(lldb cangjie-frontend)
endif()

add_subdirectory(AST)
add_subdirectory(Driver)
add_subdirectory(Parse)
add_subdirectory(Macro)
add_subdirectory(Frontend)
add_subdirectory(FrontendTool)
add_subdirectory(IncrementalCompilation)
add_subdirectory(Lex)
add_subdirectory(ConditionalCompilation)
add_subdirectory(Sema)

add_subdirectory(Modules)
add_subdirectory(Utils)
add_subdirectory(Basic)
add_subdirectory(Option)
add_subdirectory(Mangle)
if (CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    add_subdirectory(MetaTransformation)
endif()

if (CANGJIE_CODEGEN_CJNATIVE_BACKEND)
    add_subdirectory(CHIR)
    add_subdirectory(CodeGen)
endif()

set(AST_FFI_OBJECTS
    $<TARGET_OBJECTS:CangjieASTCommon>
    $<TARGET_OBJECTS:CangjieParseFFI>
    $<TARGET_OBJECTS:CangjieLex>
    $<TARGET_OBJECTS:CangjieBasic>
    $<TARGET_OBJECTS:CangjieOption>
    $<TARGET_OBJECTS:CangjieAstSerialization>
    $<TARGET_OBJECTS:CangjieCommonUtilFFI>
    $<TARGET_OBJECTS:CangjieUnicodeUtils>
    $<TARGET_OBJECTS:CangjieTempFilesUtil>)
add_library(cangjie-ast-support STATIC ${AST_FFI_OBJECTS})
string(TOLOWER ${TARGET_TRIPLE_DIRECTORY_PREFIX}_${CJNATIVE_BACKEND} output_lib_dir)
install(TARGETS cangjie-ast-support DESTINATION lib/${output_lib_dir}${SANITIZER_SUBPATH})
