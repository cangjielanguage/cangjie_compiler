// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares auxiliary methods for java interop implementation
 */
#ifndef CANGJIE_SEMA_NATIVE_FFI_JAVA_AFTER_TYPE_CHECK_UTILS
#define CANGJIE_SEMA_NATIVE_FFI_JAVA_AFTER_TYPE_CHECK_UTILS

#include "cangjie/AST/Node.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeManager.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "NativeFFI/Utils.h"

namespace Cangjie::Interop::Java {
using namespace AST;
using namespace Cangjie::Native::FFI;

class Utils final {
public:
    Utils(ImportManager& importManager, TypeManager& typeManager);

    // Ty of `Option<ty>`
    ModalTy GetOptionTy(ModalTy ty);
    Ptr<EnumDecl> GetOptionDecl();

    // `Option<ty>.None`
    OwnedPtr<Expr> CreateOptionNoneRef(ModalTy ty);

    // `Option<ty>.Some(expr)`
    OwnedPtr<Expr> CreateOptionSomeCall(OwnedPtr<Expr> expr, ModalTy ty);

    // `Option<ty>.Some`
    OwnedPtr<Expr> CreateOptionSomeRef(ModalTy ty);

    // Decl of `java.lang.JObject`
    Ptr<ClassLikeDecl> GetJObjectDecl();

    // Decl of `java.lang.JString`
    Ptr<ClassLikeDecl> GetJStringDecl();

    // Decl of String
    StructDecl& GetStringDecl();

    std::string GetJavaObjectTypeName(const Ty& ty);

    OwnedPtr<Expr> CreateOptionMatch(OwnedPtr<Expr> selector, std::function<OwnedPtr<Expr>(VarDecl&)> someBranch,
        std::function<OwnedPtr<Expr>()> noneBranch, ModalTy ty);

    /**
     * Creates native @C func
     * ```cangjie
     * public @C func ${name} (${params}): retTy {
     *     ${nodes}
     * }
     * ```
     */
    OwnedPtr<FuncDecl> CreateNativeFunc(const std::string& name,
        std::vector<OwnedPtr<FuncParam>>&& params, Ptr<Ty> retTy, std::vector<OwnedPtr<Node>>&& nodes,
        File& curFile, std::string& moduleName, std::string& fullPackageName) const;

    OwnedPtr<AST::CallExpr> CreateZeroValue(Ptr<AST::Ty> ty, AST::File& curFile) const;

private:
    Ptr<Decl> GetOptionSomeDecl();
    Ptr<Decl> GetOptionNoneDecl();

    Ptr<ClassLikeDecl> GetJavaLangDecl(const std::string& identifier);

private:
    ImportManager& importManager;
    TypeManager& typeManager;
};

/**
 * Returns javaref field of java mirror for the passed @JavaMirror/@JavaImpl declaration
 */
Ptr<VarDecl> GetJavaRefField(ClassLikeDecl& mirror);

/**
 * expr.javaRefGetter()
 */
OwnedPtr<Expr> CreateJavaRefCall(OwnedPtr<Expr> expr, FuncDecl& javaRefGetter);

/**
 * expr.javaref
 */
OwnedPtr<Expr> CreateJavaRefCall(OwnedPtr<Expr> expr, VarDecl& javaref);

/**
 * if mirrorLike is abstract class or interface, then:
 *   expr.getJavaRef()
 * else:
 *   expr.javaref
 */
OwnedPtr<Expr> CreateJavaRefCall(OwnedPtr<Expr> expr, ClassLikeDecl& mirrorLike);

/**
 * if mirrorLike is abstract class or interface, then:
 *   this.getJavaRef()
 * else:
 *   this.javaref,
 * where this is on mirrorLike
 */
OwnedPtr<Expr> CreateJavaRefCall(ClassLikeDecl& mirrorLike, Ptr<File> curFile);

/**
 * Acts like `CreateJavaRefCall(expr, *StaticCast<ClassLikeTy*>(expr->GetTy()))->commonDecl`
 */
OwnedPtr<Expr> CreateJavaRefCall(OwnedPtr<Expr> expr);

/**
 * Is generated constructor of java mirror.
 * Java mirror class can have several generated constructors as JString has.
 */
bool IsGeneratedJavaMirrorConstructor(const FuncDecl& ctor);

/**
 * Is generated wrapping constructor of java mirror of kind: init(Java_CFFI_JavaEntity)
 */
bool IsWrappingConstructorOfJavaMirror(const FuncDecl& ctor);

bool IsGeneratedJavaImplConstructor(const FuncDecl& ctor);

/**
 * Recursively searches generated constructor of @JavaMirror in passed @JavaMirror/@JavaImpl `mirrorLike`
 */
Ptr<FuncDecl> GetJavaMirrorWrappingConstructor(ClassLikeDecl& mirrorLike);

/**
 * Searches generated wrapping constructor of @JavaMirror
 */
Ptr<FuncDecl> GetJavaMirrorWrappingConstructor(ClassDecl& mirror);

std::string GetJavaJniClassName(const Ty& cjtype);

/**
 * Returns true if has java fully-qualified name defined in annotation as a string literal
 */
bool HasPredefinedJavaName(const ClassLikeDecl& decl);

/**
 * Performs mangling of `javaTy` with `mangler`. If `javaTy` is a mirrror or impl, then it returns `jobjectTy`
 */
std::string GetMangledJniInitCjObjectFuncName(const BaseMangler& mangler,
    const std::vector<OwnedPtr<FuncParam>>& params, bool isGeneratedCtor);
std::string GetMangledJniInitCjObjectFuncName(const BaseMangler& mangler, const std::vector<Ptr<Ty>>& types);

std::string GetMangledJniInitCjObjectFuncNameForEnum(
    const BaseMangler& mangler, const std::vector<OwnedPtr<FuncParam>>& params, const std::string funcName);

/**
 * Creates call of generated constructor (accepting java entity)
 * mirrorTy(entity)
 */
OwnedPtr<Expr> CreateMirrorConstructorCall(
    const ImportManager& importManager, OwnedPtr<Expr> entity, Ptr<Ty> mirrorTy);

bool IsJArray(const Decl& decl);
bool IsJArray(const Ty& ty);
bool IsOptionOfString(Ptr<Ty> ty);

bool IsMirror(const Ty& ty);

bool IsImpl(const Ty& ty);

template <typename Ret = Node, typename... Args>
std::vector<OwnedPtr<Ret>> Nodes(OwnedPtr<Args>&&... args)
{
    std::vector<OwnedPtr<Ret>> nodes;
    (nodes.push_back(std::forward<OwnedPtr<Args>>(args)), ...);
    return nodes;
}

namespace Details {

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

} // namespace Details

template <typename... Args>
OwnedPtr<CallExpr> CreateCall(Ptr<FuncDecl> fd, Ptr<File> curFile, OwnedPtr<Args>&&... args)
{
    if (!fd) {
        return nullptr;
    }

    std::vector<OwnedPtr<FuncArg>> funcArgs;

    (Details::WrapArg(&funcArgs, std::forward<OwnedPtr<Args>>(args)), ...);

    auto funcTy = RawStaticCast<FuncTy*>(fd->DataTy());

    return CreateCallExpr(WithinFile(CreateRefExpr(*fd), curFile), std::move(funcArgs), fd, funcTy->retTy,
                          CallKind::CALL_DECLARED_FUNCTION);
}

Ptr<VarDecl> GetJavaRefField(ClassDecl& mirrorLike);

template<typename MemberDecl, ASTKind MemberKind>
Ptr<MemberDecl> FindFirstMemberDecl(
    const Decl& sourceDecl,
    const std::function<bool(const Decl&)>& memberPredicate
)
{
    for (auto& member : sourceDecl.GetMemberDecls()) {
        if (memberPredicate(*member)) {
            auto ptr = As<MemberKind>(member);
            CJC_ASSERT(ptr);
            return Ptr(ptr);
        }
    }
    CJC_ABORT();
    return nullptr;
}

/**
 * Returns generated constructor of registry companion class.
 * This constructor
 */
Ptr<AST::FuncDecl> GetJavaImplRegistryCompanionConstructor(AST::ClassDecl& companion);

} // namespace Cangjie::Interop::Java

#endif // CANGJIE_SEMA_NATIVE_FFI_JAVA_AFTER_TYPE_CHECK_UTILS
