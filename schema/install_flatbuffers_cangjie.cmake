# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.


# Install flatbuffers cangjie runtime for std.ast consumers:
# rewrite package name and expose API as public for cross-package use.
if(NOT DEFINED SRC_DIR OR NOT DEFINED DST_DIR)
    message(FATAL_ERROR "SRC_DIR and DST_DIR must be set")
endif()

file(GLOB _cj_paths "${SRC_DIR}/*.cj")
if(NOT _cj_paths)
    message(FATAL_ERROR "No flatbuffers .cj sources found in: ${SRC_DIR}")
endif()

file(MAKE_DIRECTORY "${DST_DIR}")
foreach(_src ${_cj_paths})
    get_filename_component(_cj_file "${_src}" NAME)
    set(_dst "${DST_DIR}/${_cj_file}")
    file(READ "${_src}" _content)
    string(REPLACE "package std.ast" "package flatbuffers" _content "${_content}")
    string(REPLACE "public public " "public " _content "${_content}")
    string(REGEX REPLACE "(^|\n)class Builder" "\\1public class Builder" _content "${_content}")
    string(REGEX REPLACE "(^|\n)class Table" "\\1public class Table" _content "${_content}")
    string(REGEX REPLACE "(^|\n)class FlatBuffersException" "\\1public class FlatBuffersException" _content "${_content}")
    string(REGEX REPLACE "(^|\n)func " "\\1public func " _content "${_content}")
    string(REGEX REPLACE "(^|\n)const " "\\1public const " _content "${_content}")
    string(REGEX REPLACE "(^|\n)let " "\\1public let " _content "${_content}")
    string(REPLACE "\n    let table: Table" "\n    public let table: Table" _content "${_content}")
    string(REPLACE "\n    var bytes: Array<UInt8>" "\n    public var bytes: Array<UInt8>" _content "${_content}")
    string(REPLACE "\n    var pos: UInt32" "\n    public var pos: UInt32" _content "${_content}")
    string(REGEX REPLACE "(\n    )func " "\\1public func " _content "${_content}")
    string(REGEX REPLACE "(\n    )init\\(" "\\1public init(" _content "${_content}")
    string(REPLACE "public public " "public " _content "${_content}")
    file(WRITE "${_dst}" "${_content}")
endforeach()
