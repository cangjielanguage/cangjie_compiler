// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the AST Serialization related definitions.
 */

#ifndef CANGJIE_MODULES_ASTSERIALIZE_UTILS_H
#define CANGJIE_MODULES_ASTSERIALIZE_UTILS_H

#include "flatbuffers/ModuleFormat_generated.h"

#include "cangjie/AST/Node.h"

namespace Cangjie {
using TTypeOffset = flatbuffers::Offset<PackageFormat::SemaTy>;
using TDeclOffset = flatbuffers::Offset<PackageFormat::Decl>;
using TExprOffset = flatbuffers::Offset<PackageFormat::Expr>;
using TAnnoOffset = flatbuffers::Offset<PackageFormat::Anno>;
using TAnnoArgOffset = flatbuffers::Offset<PackageFormat::AnnoArg>;
using TPatternOffset = flatbuffers::Offset<PackageFormat::Pattern>;
using TFullIdOffset = flatbuffers::Offset<PackageFormat::FullId>;
using TImportsOffset = flatbuffers::Offset<PackageFormat::Imports>;
using TFileInfoOffset = flatbuffers::Offset<PackageFormat::FileInfo>;
using TImportSpecOffset = flatbuffers::Offset<PackageFormat::ImportSpec>;
using TFuncBodyOffset = flatbuffers::Offset<PackageFormat::FuncBody>;
template <typename T> using TVectorOffset = flatbuffers::Offset<flatbuffers::Vector<T>>;
using TPosition = PackageFormat::Position;
using TDeclHash = PackageFormat::DeclHash;
using PackageIndex = int32_t;

constexpr PackageIndex INVALID_PACKAGE_INDEX = -1L;
constexpr PackageIndex CURRENT_PKG_INDEX = -2L;
constexpr PackageIndex PKG_REFERENCE_INDEX = -3L;

extern const std::unordered_map<AST::AccessLevel, PackageFormat::AccessLevel> ACCESS_LEVEL_MAP;
extern const std::unordered_map<PackageFormat::AccessLevel, AST::AccessLevel> ACCESS_LEVEL_RMAP;
extern const std::unordered_map<TokenKind, PackageFormat::AccessModifier> ACCESS_MODIFIER_MAP;
extern const std::unordered_map<PackageFormat::AccessModifier, std::pair<TokenKind, AST::Attribute>>
    ACCESS_MODIFIER_RMAP;
extern const std::unordered_map<AST::BuiltInType, PackageFormat::BuiltInType> BUILTIN_TYPE_MAP;
extern const std::unordered_map<PackageFormat::BuiltInType, AST::BuiltInType> BUILTIN_TYPE_RMAP;
extern const std::unordered_map<OverflowStrategy, PackageFormat::OverflowPolicy> STRATEGY_MAP;
extern const std::unordered_map<PackageFormat::OverflowPolicy, OverflowStrategy> STRATEGY_RMAP;
extern const std::unordered_map<AST::TypeKind, PackageFormat::TypeKind> TYPE_KIND_MAP;
extern const std::unordered_map<PackageFormat::TypeKind, AST::TypeKind> TYPE_KIND_RMAP;
extern const std::unordered_map<TokenKind, PackageFormat::OperatorKind> OP_KIND_MAP;
extern const std::unordered_map<PackageFormat::OperatorKind, TokenKind> OP_KIND_RMAP;
extern const std::unordered_map<AST::CallKind, PackageFormat::CallKind> CALL_KIND_MAP;
extern const std::unordered_map<PackageFormat::CallKind, AST::CallKind> CALL_KIND_RMAP;
extern const std::unordered_map<AST::LitConstKind, PackageFormat::LitConstKind> LIT_CONST_KIND_MAP;
extern const std::unordered_map<PackageFormat::LitConstKind, AST::LitConstKind> LIT_CONST_KIND_RMAP;
extern const std::unordered_map<AST::StringKind, PackageFormat::StringKind> STRING_KIND_MAP;
extern const std::unordered_map<PackageFormat::StringKind, AST::StringKind> STRING_KIND_RMAP;
extern const std::unordered_map<AST::ForInKind, PackageFormat::ForInKind> FOR_IN_KIND_MAP;
extern const std::unordered_map<PackageFormat::ForInKind, AST::ForInKind> FOR_IN_KIND_RMAP;

inline uint32_t GetFileIndexInPkg(const AST::Decl& decl)
{
    if (!decl.curFile || !decl.curFile->curPackage) {
        return 0;
    }
    auto pkg = decl.curFile->curPackage;
    for (uint32_t i = 0; i < pkg->files.size(); ++i) {
        if (pkg->files[i].get() == decl.curFile) {
            return i;
        }
    }
    return 0;
}

inline bool IsFullInstantiatedDecl(const AST::Decl& decl)
{
    return decl.TestAttr(AST::Attribute::GENERIC_INSTANTIATED) && !decl.TestAttr(AST::Attribute::GENERIC);
}

inline bool IsExportedMemberFunc(const AST::FuncDecl& fd)
{
    return fd.linkage != Linkage::INTERNAL;
}
} // namespace Cangjie

#endif
