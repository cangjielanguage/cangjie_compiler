# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

message(STATUS "Configuring xml2 library...")

set(XML2_BUILD_DIR ${CMAKE_BINARY_DIR}/third_party/xml2-build)
set(XML2_INSTALL_DIR ${CMAKE_BINARY_DIR}/third_party/xml2)
file(MAKE_DIRECTORY ${XML2_BUILD_DIR})

set(CMAKE_SHARED_LINKER_FLAGS "${LINK_FLAGS} ${LINK_FLAGS_BUILD_ID} ${STRIP_FLAG}")
set(XML2_SOURCE_DIR ${CMAKE_BINARY_DIR}/third_party/libxml2)
if(NOT EXISTS ${CANGJIE_XML2_SOURCE_DIR})
        if(NOT EXISTS ${XML2_SOURCE_DIR})
            execute_process(COMMAND git clone --branch OpenHarmony-v6.0-Release --depth=1 https://gitcode.com/openharmony/third_party_libxml2.git ${XML2_SOURCE_DIR})
    endif()
    message(STATUS "Uncompressing libxml2...")
    execute_process(COMMAND tar -C ${CMAKE_SOURCE_DIR}/third_party -xf ${XML2_SOURCE_DIR}/libxml2-2.14.0.tar.xz)
endif()

execute_process(
    COMMAND
        ${CMAKE_COMMAND}
        -G Ninja
        -DLIBXML2_WITH_PYTHON=OFF
        -DLIBXML2_WITH_ICONV=OFF
        -DLIBXML2_WITH_LZMA=OFF
        -DLIBXML2_WITH_ZLIB=OFF
        -DCUSTOM_WARNING_SETTINGS=-Wno-error
        -DCANGJIE_TARGET_TOOLCHAIN=${CANGJIE_TARGET_TOOLCHAIN}
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_SYSROOT=${CANGJIE_TARGET_SYSROOT}
        -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS}
        -DBUILD_SHARED_LIBS=ON
        -DCMAKE_INSTALL_PREFIX=${XML2_INSTALL_DIR}
        ${CANGJIE_XML2_SOURCE_DIR}
    WORKING_DIRECTORY ${XML2_BUILD_DIR}
    RESULT_VARIABLE config_result
    OUTPUT_VARIABLE config_stdout
    ERROR_VARIABLE config_stderr)
if(NOT ${config_result} STREQUAL "0")
    message(STATUS "${config_stdout}")
    message(STATUS "${config_stderr}")
    message(FATAL_ERROR "Configuring xml2 Failed!")
endif()

message(STATUS "Building xml2 libraries...")
execute_process(
    COMMAND ${CMAKE_COMMAND} --build .
    WORKING_DIRECTORY ${XML2_BUILD_DIR}
    RESULT_VARIABLE build_result
    OUTPUT_VARIABLE build_stdout
    ERROR_VARIABLE build_stderr)
if(NOT ${build_result} STREQUAL "0")
    message(STATUS "${config_stdout}")
    message(STATUS "${config_stderr}")
    message(STATUS "${build_stdout}")
    message(STATUS "${build_stderr}")
    message(FATAL_ERROR "Building xml2 Failed!")
endif()

message(STATUS "Installing xml2 libraries to Cangjie library source...")
execute_process(
    COMMAND ${CMAKE_COMMAND} --install .
    WORKING_DIRECTORY ${XML2_BUILD_DIR}
    RESULT_VARIABLE install_result
    OUTPUT_VARIABLE install_stdout
    ERROR_VARIABLE install_stderr)
if(NOT ${install_result} STREQUAL "0")
    message(STATUS "${config_stdout}")
    message(STATUS "${config_stderr}")
    message(STATUS "${build_stdout}")
    message(STATUS "${build_stderr}")
    message(STATUS "${install_stdout}")
    message(STATUS "${install_stderr}")
    message(FATAL_ERROR "Installing xml2 Failed!")
endif()
