// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#include "cangjie/AST/Create.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie::Native::FFI {
constexpr std::string_view NSSTRING_CLASS_IDENT = "NSString";
constexpr std::string_view NSOBJECT_CLASS_IDENT = "NSObject";
constexpr std::string_view TOSTRING_METHOD_IDENT = "toString";
constexpr std::string_view INIT_IDENT = "init";

constexpr std::string_view READ_POINTER_INTRINSIC = "readPointer";
constexpr std::string_view GET_POINTER_ADDRESS_INTRINSIC = "getPointerAddress";

using namespace AST;

OwnedPtr<RefExpr> CreateThisRef(Ptr<Decl> target, ModalTy ty, Ptr<File> curFile);

OwnedPtr<CallExpr> CreateThisCall(
    Decl& target, FuncDecl& baseTarget, Ptr<Ty> funcTy, Ptr<File> curFile, std::vector<OwnedPtr<FuncArg>> args = {});

OwnedPtr<PrimitiveType> CreateUnitType(Ptr<File> curFile);

std::vector<ModalTy> GetParamTys(FuncParamList& params);

OwnedPtr<RefExpr> CreateSuperRef(Ptr<Decl> target, Ptr<Ty> ty);

OwnedPtr<CallExpr> CreateSuperCall(Decl& target, FuncDecl& baseTarget, Ptr<Ty> funcTy);


template <typename Ret = Node, typename... Args> std::vector<OwnedPtr<Ret>> Nodes(OwnedPtr<Args>&&... args)
{
    std::vector<OwnedPtr<Ret>> nodes;
    (nodes.push_back(std::forward<OwnedPtr<Args>>(args)), ...);
    return nodes;
}

namespace Details {

template <typename T> void WrapArg(std::vector<OwnedPtr<FuncArg>>* funcArgs, OwnedPtr<T>&& e)
{
    CJC_ASSERT(e);
    funcArgs->push_back(CreateFuncArg(StaticCast<Expr*>(e.release())));
}

template<> inline void WrapArg<FuncArg>(std::vector<OwnedPtr<FuncArg>>* funcArgs, OwnedPtr<FuncArg>&& e)
{
    CJC_ASSERT(e);
    funcArgs->emplace_back(std::move(e));
}

template<> inline void WrapArg<Expr>(std::vector<OwnedPtr<FuncArg>>* funcArgs, OwnedPtr<Expr>&& e)
{
    CJC_ASSERT(e);
    funcArgs->push_back(CreateFuncArg(std::forward<OwnedPtr<Expr>>(e)));
}

} // namespace Details

template <typename T> OwnedPtr<T> WithinFile(OwnedPtr<T> node, Ptr<File> curFile)
{
    CJC_NULLPTR_CHECK(curFile);
    node->curFile = curFile;
    return node;
}

template <typename... Args> OwnedPtr<CallExpr> CreateCall(Ptr<FuncDecl> fd, Ptr<File> curFile, OwnedPtr<Args>&&... args)
{
    if (!fd) {
        return nullptr;
    }

    std::vector<OwnedPtr<FuncArg>> funcArgs;

    (Details::WrapArg(&funcArgs, std::forward<OwnedPtr<Args>>(args)), ...);

    auto funcTy = StaticCast<FuncTy*>(fd->DataTy());
    return CreateCallExpr(WithinFile(CreateRefExpr(*fd), curFile), std::move(funcArgs), fd, funcTy->retTy,
        CallKind::CALL_DECLARED_FUNCTION);
}

template <typename... Args>
OwnedPtr<CallExpr> CreateMemberCall(OwnedPtr<Expr> receiver, Ptr<FuncDecl> fd, OwnedPtr<Args>&&... args)
{
    CJC_NULLPTR_CHECK(receiver);
    CJC_NULLPTR_CHECK(fd);
    std::vector<OwnedPtr<FuncArg>> funcArgs;

    (Details::WrapArg(&funcArgs, std::forward<OwnedPtr<Args>>(args)), ...);

    auto funcTy = StaticCast<FuncTy*>(fd->DataTy());
    auto ma = CreateMemberAccess(std::move(receiver), *fd);
    CopyBasicInfo(ma->baseExpr, ma);
    return CreateCallExpr(std::move(ma), std::move(funcArgs), fd, funcTy->retTy, CallKind::CALL_DECLARED_FUNCTION);
}

template <typename... Args>
OwnedPtr<CallExpr> CreateMemberCall(
    [[maybe_unused]] TypeManager& typeManager, OwnedPtr<Expr> receiver, VarDecl& vd, OwnedPtr<Args>&&... args)
{
    CJC_NULLPTR_CHECK(receiver);
    CJC_ASSERT(vd.GetTy()->IsFunc() || vd.GetTy()->IsCFunc());
    std::vector<OwnedPtr<FuncArg>> funcArgs;

    (Details::WrapArg(&funcArgs, std::forward<OwnedPtr<Args>>(args)), ...);

    auto funcTy = StaticCast<FuncTy>(vd.DataTy());

    // Ensure formal parameters count is the same as passed arguments.
    CJC_ASSERT(funcTy->paramTys.size() == funcArgs.size());
#ifdef NDEBUG
    // Ensure passed arguments are subtypes of expected types.
    for (auto [expectedTy, actualParam] = std::tuple{funcTy->paramTys.begin(), funcArgs.begin()};
         expectedTy != funcTy->paramTys.end();
         expectedTy++, actualParam++) {
             CJC_ASSERT(typeManager.IsSubtype((*actualParam)->GetTy(), *expectedTy));
        }
#endif // NDEBUG

        auto ma = CreateMemberAccess(std::move(receiver), vd);
        if (vd.GetTy()->IsCFunc()) {
            ma->EnableAttr(Attribute::UNSAFE);
        }
        CopyBasicInfo(ma->baseExpr, ma);
        auto ca =
            CreateCallExpr(std::move(ma), std::move(funcArgs), nullptr, funcTy->retTy, CallKind::CALL_FUNCTION_PTR);
        if (vd.GetTy()->IsCFunc()) {
            ca->EnableAttr(Attribute::UNSAFE);
        }
        return ca;
}

OwnedPtr<Type> CreateType(ModalTy ty);
OwnedPtr<Type> CreateFuncType(Ptr<FuncTy> ty);

OwnedPtr<Expr> CreateBoolMatch(
    OwnedPtr<Expr> selector, OwnedPtr<Expr> trueBranch, OwnedPtr<Expr> falseBranch, ModalTy ty);

StructDecl& GetStringDecl(const ImportManager& importManager);
FuncDecl& GetReadPointerIntrinsicDecl(const ImportManager& importManager);
FuncDecl& GetGetPointerAddressIntrinsicDecl(const ImportManager& importManager);

OwnedPtr<Expr> CreateReadPointerCall(const ImportManager& importManager, TypeManager& typeManager, Ptr<Expr> ptr);
OwnedPtr<Expr> CreateGetPointerAddressCall(const ImportManager& importManager, TypeManager& typeManager, Ptr<Expr> ptr);
OwnedPtr<Expr> CreateIsPtrNullCheckCall(const ImportManager& importManager, TypeManager& typeManager, Ptr<Expr> ptr);

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

/**
 * Returns lambda called in-place.
 * @see WrapReturningLambdaCall(TypeManager&, std::vector<OwnedPtr<Node>>) overload.
 */
OwnedPtr<CallExpr> WrapReturningLambdaCall(TypeManager& typeManager, OwnedPtr<Block> nodes);

/**
 * Returns lambda expression of nodes returning `Unit`.
 */
OwnedPtr<LambdaExpr> WrapUnitLambdaExpr(TypeManager& typeManager, std::vector<OwnedPtr<Node>> nodes,
    std::vector<OwnedPtr<FuncParam>> lambdaParams = {});
OwnedPtr<LambdaExpr> WrapReturningLambdaExpr(
    TypeManager& typeManager, std::vector<OwnedPtr<Node>> nodes, std::vector<OwnedPtr<FuncParam>> lambdaParams = {});

/**
 * Returns trimmed cangjie library name.
 * For a filename in [outputLibPath] matched to "lib{libname}.{ext}" it returns {libname} if [trimmed] = `true`
 * and "lib{libname}.{ext}" if [trimmed] = `false`.
 * For other cases, it returns [fullPackageName]
 */
std::string GetCangjieLibName(
    const std::string& outputLibPath, const std::string& fullPackageName, bool trimmed = true);

std::string GetMangledMethodName(const BaseMangler& mangler, const std::vector<OwnedPtr<FuncParam>>& params,
    const std::string& methodName);

Ptr<Annotation> GetForeignNameAnnotation(const Decl& decl);
Ptr<Annotation> GetAnnotation(const Decl& decl, AnnotationKind annotationKind);

Ptr<std::string> GetSingleArgumentAnnotationValue(const Decl& target, AnnotationKind annotationKind);

bool IsSuperConstructorCall(const CallExpr& call);
bool IsThisConstructorCall(const CallExpr& call);

OwnedPtr<PrimitiveType> GetPrimitiveType(std::string typeName, AST::TypeKind typekind);
OwnedPtr<Type> GetTypeByName(std::string typeStr);
TypeKind GetActualTypeKind(std::string configType);
Ptr<Ty> GetTyByName(std::string typeStr);

void SplitAndTrim(std::string str, std::vector<std::string>& types);

std::string JoinVector(const std::vector<std::string>& vec, const std::string& delimiter = "");

Ptr<Ty> GetInstantyForGenericTy(
    Decl& decl, const std::unordered_map<std::string, Ptr<Ty>>& actualTyArgMap, TypeManager& typeManager);

ClassDecl& GetExceptionDecl(const ImportManager& importManager);
OwnedPtr<ThrowExpr> CreateThrowExceptionCall(const ImportManager& importManager,
    TypeManager& typeManager, const std::string& msg, Ptr<File> curFile);
bool AreParamTypeKindsValid(const FuncDecl& fd, const std::vector<TypeKind>& typeKinds);

/**
 * @brief Sets `outerDecl` and package identity of a cloned stub to match `synthetic`.
 *
 * @param stub The cloned member declaration.
 * @param synthetic The synthetic wrapper class that owns the stub.
 */
void RebindClonedStubToSynthetic(Decl& stub, ClassDecl& synthetic);

OwnedPtr<Expr> CreateZeroValue(const ImportManager& importManager, TypeManager& typeManager, Ty& ty);

// Ty of `Option<ty>`
Ptr<Ty> GetOptionTy(const ImportManager& importManager, TypeManager& typeManager, Ptr<Ty> ty);
Ptr<EnumDecl> GetOptionDecl(const ImportManager& importManager);

Ptr<Decl> GetOptionSomeDecl(const ImportManager& importManager);

Ptr<Decl> GetOptionNoneDecl(const ImportManager& importManager);

// `Option<ty>.None`
OwnedPtr<Expr> CreateOptionNoneRef(const ImportManager& importManager, TypeManager& typeManager, ModalTy ty);

// `Option<ty>.Some(expr)`
OwnedPtr<Expr> CreateOptionSomeCall(
    const ImportManager& importManager, TypeManager& typeManager, OwnedPtr<Expr> expr, ModalTy ty);

// `Option<ty>.Some`
OwnedPtr<Expr> CreateOptionSomeRef(const ImportManager& importManager, TypeManager& typeManager, ModalTy ty);

/**
 * Returns string value of objective-c decl whether it is foreign or exported.
 */
std::string GetObjCDeclKindInfo(const Decl& decl);

} // namespace Cangjie::Native::FFI

#endif // CANGJIE_SEMA_NATIVE_FFI_UTILS
