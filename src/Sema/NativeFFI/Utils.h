// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares auxiliary methods for Cangjie Native FFI implementation with different targets
 */
#ifndef CANGJIE_SEMA_NATIVE_FFI_UTILS
#define CANGJIE_SEMA_NATIVE_FFI_UTILS

#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Utils.h"

namespace Cangjie::Native::FFI {
using namespace AST;

enum class ArrayOperationKind: uint8_t {
    CREATE,
    GET,
    SET,
    GET_LENGTH
};

OwnedPtr<RefExpr> CreateThisRef(Ptr<Decl> target, Ptr<Ty> ty, Ptr<File> curFile);

OwnedPtr<CallExpr> CreateThisCall(Decl& target, FuncDecl& baseTarget, Ptr<Ty> funcTy, Ptr<File> curFile);

OwnedPtr<PrimitiveType> CreateUnitType(Ptr<File> curFile);

std::vector<Ptr<Ty>> GetParamTys(FuncParamList& params);

OwnedPtr<RefExpr> CreateSuperRef(Ptr<Decl> target, Ptr<Ty> ty);

OwnedPtr<CallExpr> CreateSuperCall(Decl& target, FuncDecl& baseTarget, Ptr<Ty> funcTy);

ArrayOperationKind GetArrayOperationKind(Decl& decl);

template <typename Ret = Node, typename... Args>
std::vector<OwnedPtr<Ret>> Nodes(OwnedPtr<Args>&&... args)
{
    std::vector<OwnedPtr<Ret>> nodes;
    (nodes.push_back(std::forward<OwnedPtr<Args>>(args)), ...);
    return nodes;
}

namespace details {

template <typename T>
void WrapArg(std::vector<OwnedPtr<FuncArg>>* funcArgs, OwnedPtr<T>&& e)
{
    CJC_ASSERT(e);
    if (auto ptr = As<ASTKind::FUNC_ARG>(e.get())) {
        funcArgs->emplace_back(ptr);
    } else {
        funcArgs->push_back(CreateFuncArg(std::forward<OwnedPtr<T>>(e)));
    }
}

} // namespace details

template <typename T>
OwnedPtr<T> WithinFile(OwnedPtr<T> node, Ptr<File> curFile)
{
    CJC_NULLPTR_CHECK(curFile);
    node->curFile = curFile;
    return node;
}

template <typename... Args>
OwnedPtr<CallExpr> CreateCall(Ptr<FuncDecl> fd, Ptr<File> curFile, OwnedPtr<Args>&&... args)
{
    if (!fd) {
        return nullptr;
    }

    std::vector<OwnedPtr<FuncArg>> funcArgs;

    (details::WrapArg(&funcArgs, std::forward<OwnedPtr<Args>>(args)), ...);

    auto funcTy = StaticCast<FuncTy*>(fd->ty);

    return CreateCallExpr(WithinFile(CreateRefExpr(*fd), curFile), std::move(funcArgs), fd, funcTy->retTy,
                          CallKind::CALL_DECLARED_FUNCTION);
}

template <typename... Args>
OwnedPtr<CallExpr> CreateMemberCall(OwnedPtr<Expr> receiver, Ptr<FuncDecl> fd, OwnedPtr<Args>&&... args) {
    CJC_NULLPTR_CHECK(receiver);
    CJC_NULLPTR_CHECK(fd);
    std::vector<OwnedPtr<FuncArg>> funcArgs;

    (details::WrapArg(&funcArgs, std::forward<OwnedPtr<Args>>(args)), ...);

    auto funcTy = StaticCast<FuncTy*>(fd->ty);
    auto ma = CreateMemberAccess(std::move(receiver), *fd);
    CopyBasicInfo(ma->baseExpr, ma);
    return CreateCallExpr(std::move(ma), std::move(funcArgs), fd, funcTy->retTy,
                          CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<Type> CreateType(Ptr<Ty> ty);
OwnedPtr<Type> CreateFuncType(Ptr<FuncTy> ty);

OwnedPtr<Expr> CreateBoolMatch(OwnedPtr<Expr> selector, OwnedPtr<Expr> trueBranch, OwnedPtr<Expr> falseBranch,
    Ptr<Ty> ty);

StructDecl& GetStringDecl(const ImportManager& importManager);

/**
 * Returns synthetic lambda call that includes nodes. The result of the call expr is the last node:
 *
 * {
 *     node1;
 *     node2;
 *     ...
 *     return noden;
 * }()
 */

OwnedPtr<CallExpr> WrapReturningLambdaCall(TypeManager& typeManager, std::vector<OwnedPtr<Node>> nodes);
OwnedPtr<LambdaExpr> WrapReturningLambdaExpr(TypeManager& typeManager, std::vector<OwnedPtr<Node>> nodes, std::vector<OwnedPtr<FuncParam>> lambdaParams = {});

/**
 * Returns trimmed cangjie library name.
 * For a filename in [outputLibPath] matched to "lib{libname}.{ext}" it returns {libname} if [trimmed] = `true`
 * and "lib{libname}.{ext}" if [trimmed] = `false`.
 * For other cases, it returns [fullPackageName]
 */
std::string GetCangjieLibName(const std::string& outputLibPath, const std::string& fullPackageName,
    bool trimmed = true);

std::string GetMangledMethodName(const BaseMangler& mangler, const std::vector<OwnedPtr<FuncParam>>& params,
    const std::string& methodName);

Ptr<Annotation> GetForeignNameAnnotation(const Decl& decl);
Ptr<Annotation> GetAnnotation(const Decl& decl, AnnotationKind annotationKind);

Ptr<std::string> GetSingleArgumentAnnotationValue(const Decl& target, AnnotationKind annotationKind);

bool IsSuperConstructorCall(const CallExpr& call);

} // namespace Cangjie::Interop::Java

#endif // CANGJIE_SEMA_NATIVE_FFI_UTILS
