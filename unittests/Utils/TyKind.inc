// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

DEFINE_TY(AST::ArrayTy, AST::TypeKind::TYPE_ARRAY);
DEFINE_TY(AST::PointerTy, AST::TypeKind::TYPE_POINTER);
DEFINE_TY(AST::CStringTy, AST::TypeKind::TYPE_CSTRING);
DEFINE_TY(AST::TupleTy, AST::TypeKind::TYPE_TUPLE);
DEFINE_TY(AST::FuncTy, AST::TypeKind::TYPE_FUNC);
DEFINE_TY(AST::UnionTy, AST::TypeKind::TYPE_UNION);
DEFINE_TY(AST::IntersectionTy, AST::TypeKind::TYPE_INTERSECTION);
DEFINE_TY(AST::InterfaceTy, AST::TypeKind::TYPE_INTERFACE);
DEFINE_TY(AST::ClassTy, AST::TypeKind::TYPE_CLASS);
DEFINE_TY(AST::EnumTy, AST::TypeKind::TYPE_ENUM);
DEFINE_TY(AST::StructTy, AST::TypeKind::TYPE_STRUCT);
DEFINE_TY(AST::TypeAliasTy, AST::TypeKind::TYPE);
DEFINE_TY(AST::GenericsTy, AST::TypeKind::TYPE_GENERICS);
