# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

# Apply Cangjie package-mapping changes to flatbuffers idl_gen_cangjie.cpp.
# Intended to run as ExternalProject PATCH_COMMAND (cwd = SOURCE_DIR), or with -DFLATBUFFERS_SRC=.
if(DEFINED FLATBUFFERS_SRC)
    set(_src_root "${FLATBUFFERS_SRC}")
else()
    set(_src_root "${CMAKE_CURRENT_SOURCE_DIR}")
endif()
set(_idl "${_src_root}/src/idl_gen_cangjie.cpp")
if(NOT EXISTS "${_idl}")
    message(FATAL_ERROR "idl_gen_cangjie.cpp not found at ${_idl}")
endif()

file(READ "${_idl}" _content)
if(_content MATCHES "MapFbsNamespaceToCangjiePackage")
    message(STATUS "Cangjie package-mapping patch already applied")
    return()
endif()

if(NOT _content MATCHES "code_ \\+= \"package std\\.ast\";")
    message(FATAL_ERROR "Unexpected idl_gen_cangjie.cpp content; cannot apply package-mapping patch")
endif()

string(REPLACE
"        code_ += \"package std.ast\";
        code_ += \"\";"
"        const std::string cj_package = GetCangjiePackageName();
        code_ += \"package \" + cj_package;
        // Generated tables depend on the shared flatbuffers runtime package.
        if (cj_package != \"flatbuffers\") {
            code_ += \"import flatbuffers.*\";
        }
        code_ += \"\";"
_content "${_content}")

string(REPLACE
"    std::string Name(const Definition& def) const { return EscapeKeyword(MakeCamel(def.name, false)); }
};
} // namespace cangjie"
"    std::string Name(const Definition& def) const { return EscapeKeyword(MakeCamel(def.name, false)); }

    // Map .fbs `namespace` to Cangjie package name.
    static std::string MapFbsNamespaceToCangjiePackage(const std::string &fbs_ns)
    {
        if (fbs_ns == \"ASTFormat\") return \"std.ast\";
        if (fbs_ns == \"CHIRFormat\") return \"stdx.chir\";
        if (fbs_ns == \"SyntaxFormat\") return \"stdx.syntax\";
        return fbs_ns;
    }

    std::string GetSchemaNamespaceName() const
    {
        for (auto it = parser_.namespaces_.rbegin(); it != parser_.namespaces_.rend(); ++it) {
            if (*it && !(*it)->components.empty()) {
                return FullNamespace(\".\", **it);
            }
        }
        if (parser_.current_namespace_ && !parser_.current_namespace_->components.empty()) {
            return FullNamespace(\".\", *parser_.current_namespace_);
        }
        return file_name_;
    }

    std::string GetCangjiePackageName() const
    {
        return MapFbsNamespaceToCangjiePackage(GetSchemaNamespaceName());
    }
};
} // namespace cangjie"
_content "${_content}")

if(NOT _content MATCHES "MapFbsNamespaceToCangjiePackage")
    message(FATAL_ERROR "Failed to apply Cangjie package-mapping patch to idl_gen_cangjie.cpp")
endif()

file(WRITE "${_idl}" "${_content}")
message(STATUS "Applied Cangjie package-mapping patch to idl_gen_cangjie.cpp")
