// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Utils.h"
#include "NativeFFI/Utils.h"
#include "TypeCheckUtil.h"

#include "Desugar/AfterTypeCheck.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/AST/Match.h"
#include "cangjie/Utils/ConstantsUtils.h"
#include "JavaDesugarManager.h"
#include "../../../InheritanceChecker/StructInheritanceChecker.h"
#include "cangjie/AST/Utils.h"


namespace {
using namespace Cangjie;

std::string NormalizeJavaSignature(const std::string& sig)
{
    std::string normalized = sig;
    std::replace(normalized.begin(), normalized.end(), '.', '/');
    return normalized;
}

/**
 * @brief Generates a synthetic function stub based on an existing function declaration.
 *
 * This function creates a clone of the provided function declaration (fd),
 * replaces its outerDecl to synthetic class, and then inserts the
 * modified function declaration into the specified synthetic class declaration.
 *
 * @param synthetic The class declaration where the cloned function stub will be inserted.
 * @param fd The original function declaration that will be cloned and modified.
 */
void GenerateSyntheticClassFuncStub(ClassDecl& synthetic, FuncDecl& fd)
{
    OwnedPtr<FuncDecl> funcStub = ASTCloner::Clone(Ptr(&fd));
    funcStub->DisableAttr(Attribute::DEFAULT);

    // remove foreign anno from cloned func decl
    for (auto it = funcStub->annotations.begin(); it != funcStub->annotations.end(); ++it) {
        if ((*it)->kind == AnnotationKind::FOREIGN_NAME) {
            funcStub->annotations.erase(it);
            break;
        }
    }

    funcStub->outerDecl = Ptr(&synthetic);
    synthetic.body->decls.emplace_back(std::move(funcStub));
}

void GenerateSyntheticClassPropStub(ClassDecl& synthetic, PropDecl& fd)
{
    CJC_ASSERT(&synthetic);
    CJC_ASSERT(&fd);
    // TODO:
}

void GenerateSyntheticClassAbstractMemberImplStubs(ClassDecl& synthetic, const MemberMap& members)
{
    for (const auto& idMemberSignature : members) {
        const auto& signature = idMemberSignature.second;

        // only abstract functions must be inside synthetic class
        if (!signature.decl->TestAnyAttr(Attribute::ABSTRACT)) {
            continue;
        }

        // JObject already has implementation of java ref getter
        if (Interop::Java::IsJavaRefGetter(*signature.decl)) {
            continue;
        }

        switch (signature.decl->astKind) {
            case ASTKind::FUNC_DECL:
                GenerateSyntheticClassFuncStub(synthetic, *StaticAs<ASTKind::FUNC_DECL>(signature.decl));
                break;
            case ASTKind::PROP_DECL:
                GenerateSyntheticClassPropStub(synthetic, *StaticAs<ASTKind::PROP_DECL>(signature.decl));
                break;
            default:
                continue;
        }
    }
}
}

namespace Cangjie::Interop::Java {

using namespace TypeCheckUtil;
using namespace Cangjie::Native::FFI;

Utils::Utils(ImportManager& importManager, TypeManager& typeManager)
    : importManager(importManager), typeManager(typeManager)
{}

Ptr<Ty> Utils::GetOptionTy(Ptr<Ty> ty)
{
    return typeManager.GetEnumTy(*GetOptionDecl(), {ty});
}

Ptr<EnumDecl> Utils::GetOptionDecl()
{
    static auto decl = importManager.GetCoreDecl<EnumDecl>(STD_LIB_OPTION);
    return decl;
}

Ptr<Decl> Utils::GetOptionSomeDecl()
{
    static auto someDecl = Sema::Desugar::AfterTypeCheck::LookupEnumMember(GetOptionDecl(), OPTION_VALUE_CTOR);
    return someDecl;
}

Ptr<Decl> Utils::GetOptionNoneDecl()
{
    static auto noneDecl = Sema::Desugar::AfterTypeCheck::LookupEnumMember(GetOptionDecl(), OPTION_NONE_CTOR);
    return noneDecl;
}

OwnedPtr<Expr> Utils::CreateOptionSomeRef(Ptr<Ty> ty)
{
    auto someDeclRef = CreateRefExpr(*GetOptionSomeDecl());
    auto optionActualTy = GetOptionTy(ty);
    someDeclRef->ty = typeManager.GetFunctionTy({ty}, optionActualTy);
    return someDeclRef;
}

OwnedPtr<Expr> Utils::CreateOptionNoneRef(Ptr<Ty> ty)
{
    auto noneDeclRef = CreateRefExpr(*GetOptionNoneDecl());
    auto optionActualTy = GetOptionTy(ty);
    noneDeclRef->ty = optionActualTy;
    return noneDeclRef;
}

OwnedPtr<Expr> Utils::CreateOptionSomeCall(OwnedPtr<Expr> expr, Ptr<Ty> ty)
{
    std::vector<OwnedPtr<FuncArg>> someDeclCallArgs {};
    someDeclCallArgs.emplace_back(CreateFuncArg(std::move(expr)));
    auto someDeclCall = CreateCallExpr(CreateOptionSomeRef(ty), std::move(someDeclCallArgs));
    someDeclCall->ty = GetOptionTy(ty);
    someDeclCall->resolvedFunction = As<ASTKind::FUNC_DECL>(GetOptionSomeDecl());
    someDeclCall->callKind = CallKind::CALL_DECLARED_FUNCTION;
    return someDeclCall;
}

Ptr<ClassLikeDecl> Utils::GetJObjectDecl()
{
    return GetJavaLangDecl(INTEROP_JOBJECT_NAME);
}

Ptr<ClassLikeDecl> Utils::GetJStringDecl()
{
    return GetJavaLangDecl(INTEROP_JSTRING_NAME);
}

Ptr<ClassLikeDecl> Utils::GetJavaLangDecl(const std::string& identifier)
{
    auto decl = DynamicCast<ClassLikeDecl>(importManager.GetImportedDecl(INTEROP_JAVA_LANG_PACKAGE, identifier));
    if (decl == nullptr) {
        importManager.GetDiagnosticEngine().DiagnoseRefactor(
            DiagKindRefactor::sema_member_not_imported, DEFAULT_POSITION, INTEROP_JAVA_LANG_PACKAGE + "." + identifier);
        return Ptr<ClassLikeDecl>(nullptr);
    }
    return decl;
}

StructDecl& Utils::GetStringDecl()
{
    return Native::FFI::GetStringDecl(importManager);
}

Ptr<VarDecl> GetJavaRefField(ClassDecl& mirrorLike)
{
    if (mirrorLike.TestAttr(Attribute::JAVA_MIRROR_SUBTYPE)) {
        if (auto superClass = mirrorLike.GetSuperClassDecl();
            superClass && !superClass->ty->IsObject()
            && superClass->TestAnyAttr(Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE)) {
                return GetJavaRefField(*superClass);
        }

        auto superClass = mirrorLike.GetSuperClassDecl();
        if (!superClass || !superClass->TestAnyAttr(Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE)) {
            CJC_ABORT(); // neither mirror or mirror subtype are super type of [mirror]
        }

        return GetJavaRefField(*superClass);
    }

    CJC_ASSERT(mirrorLike.TestAttr(
        Attribute::JAVA_MIRROR) || mirrorLike.TestAttr(Attribute::CJ_MIRROR_JAVA_INTERFACE_FWD));
    CJC_ASSERT(IsJObject(mirrorLike) || IsFwdClass(mirrorLike));
    CJC_ASSERT(mirrorLike.body);

    for (auto& member : mirrorLike.body->decls) {
        if (auto varDecl = As<ASTKind::VAR_DECL>(member); varDecl && varDecl->identifier == JAVA_REF_FIELD_NAME) {
            return varDecl;
        }
    }

    CJC_ABORT(); // Internal error: mirror is not @JavaMirror or weak reference field was not generated!
    return nullptr;
}

// will be removed or changed when interface for JavaImpl will be supported
// now it's kind of stub
Ptr<VarDecl> GetJavaRefField(ClassLikeDecl& mirror)
{
    if (mirror.astKind == AST::ASTKind::CLASS_DECL) {
        if (auto mirrorClass = DynamicCast<ClassDecl*>(&mirror)) {
            return GetJavaRefField(*mirrorClass);
        }
    }
    CJC_ABORT(); // Internal error: mirror is not @JavaMirror or weak reference field was not generated!
    return nullptr;
}

bool IsJavaRefGetter(const Decl& fd)
{
    return fd.astKind == ASTKind::FUNC_DECL &&
        fd.TestAttr(Attribute::COMPILER_ADD) &&
        fd.identifier.Val() == JAVA_REF_GETTER_FUNC_NAME;
}

Ptr<FuncDecl> GetJavaRefGetter(ClassLikeDecl& mirror)
{
    CJC_ASSERT(mirror.TestAnyAttr(Attribute::JAVA_MIRROR_SUBTYPE, Attribute::JAVA_MIRROR));
    const std::function<bool(const Decl& d)>& isDeclJavaRefGetterFunc =
                [](const Decl& d) { return IsJavaRefGetter(d); };

    if (auto cd = DynamicCast<ClassDecl*>(&mirror)) {
        if (!IsJObject(mirror)) {
            return GetJavaRefGetter(*cd->GetSuperClassDecl());
        } else {
            return FindFirstMemberDecl<FuncDecl, ASTKind::FUNC_DECL>(mirror, isDeclJavaRefGetterFunc);
        }
    } else {
        return FindFirstMemberDecl<FuncDecl, ASTKind::FUNC_DECL>(mirror, isDeclJavaRefGetterFunc);
    }
}

OwnedPtr<Expr> CreateJavaRefCall(ClassLikeDecl& mirrorLike, FuncDecl& javaRefGetter, Ptr<File> curFile)
{
    CJC_ASSERT(mirrorLike.TestAnyAttr(
        Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE, Attribute::CJ_MIRROR_JAVA_INTERFACE_FWD));
    auto thisRef = CreateThisRef(&mirrorLike, mirrorLike.ty, curFile);
    return CreateJavaRefCall(std::move(thisRef), javaRefGetter);
}

OwnedPtr<Expr> CreateJavaRefCall(ClassLikeDecl& mirrorLike, VarDecl& javaref, Ptr<File> curFile)
{
    CJC_ASSERT(mirrorLike.astKind == ASTKind::CLASS_DECL);
    CJC_ASSERT(mirrorLike.TestAnyAttr(
        Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE, Attribute::CJ_MIRROR_JAVA_INTERFACE_FWD));
    auto thisRef = CreateThisRef(&mirrorLike, mirrorLike.ty, curFile);
    return CreateJavaRefCall(std::move(thisRef), javaref);
}

OwnedPtr<Expr> CreateJavaRefCall(OwnedPtr<Expr> expr, FuncDecl& javaRefGetter)
{
    // expr ty decl and javaRef outerDecl must be the same

    CJC_ASSERT(expr->ty->IsClassLike());
    CJC_ASSERT(StaticCast<ClassLikeTy*>(expr->ty)->commonDecl->TestAnyAttr(
        Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE));
    auto curFile = expr->curFile;
    CJC_NULLPTR_CHECK(curFile);

    return CreateCallExpr(
        WithinFile(CreateMemberAccess(std::move(expr), javaRefGetter), curFile),
        {},
        &javaRefGetter,
        StaticCast<FuncTy*>(javaRefGetter.ty)->retTy,
        CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<Expr> CreateJavaRefCall(OwnedPtr<Expr> expr, VarDecl& javaref)
{
    // expr ty decl and javaref outerDecl must be the same

    CJC_ASSERT(expr->ty->IsClassLike());
    CJC_ASSERT(StaticCast<ClassLikeTy*>(expr->ty)->commonDecl->TestAnyAttr(
        Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE, Attribute::CJ_MIRROR_JAVA_INTERFACE_FWD));

    auto curFile = expr->curFile;
    CJC_NULLPTR_CHECK(curFile);

    return WithinFile(CreateMemberAccess(std::move(expr), javaref), curFile);
}

OwnedPtr<Expr> CreateJavaRefCall(OwnedPtr<Expr> expr, ClassLikeDecl& mirrorLike)
{
    CJC_ASSERT(mirrorLike.TestAnyAttr(
        Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE, Attribute::CJ_MIRROR_JAVA_INTERFACE_FWD));
    if (auto mirrorLikeClass = As<ASTKind::CLASS_DECL>(&mirrorLike)) {
        return CreateJavaRefCall(std::move(expr), *GetJavaRefField(*mirrorLikeClass));
    }

    // for an interface
    return CreateJavaRefCall(std::move(expr), *GetJavaRefGetter(mirrorLike));
}

OwnedPtr<Expr> CreateJavaRefCall(ClassLikeDecl& mirrorLike, Ptr<File> curFile)
{
    auto thisExpr = CreateThisRef(Ptr(&mirrorLike), mirrorLike.ty, curFile);
    return CreateJavaRefCall(std::move(thisExpr), mirrorLike);
}

OwnedPtr<Expr> CreateJavaRefCall(OwnedPtr<Expr> expr)
{
    CJC_ASSERT(expr->ty->IsClassLike());
    auto classLikeTy = StaticCast<ClassLikeTy*>(expr->ty);
    CJC_ASSERT(classLikeTy->commonDecl);
    return CreateJavaRefCall(std::move(expr), *classLikeTy->commonDecl);
}

bool IsGeneratedJavaMirrorConstructor(const FuncDecl& ctor)
{
    return ctor.TestAttr(Attribute::JAVA_MIRROR) && ctor.TestAttr(Attribute::CONSTRUCTOR) &&
           ctor.TestAttr(Attribute::COMPILER_ADD);
}

Ptr<FuncDecl> GetGeneratedConstructorInMirror(ClassDecl& mirror)
{
    CJC_ASSERT(mirror.TestAttr(Attribute::JAVA_MIRROR));
    Ptr<FuncDecl> generatedCtor;

    for (auto& member : mirror.GetMemberDecls()) {
        if (auto fd = As<ASTKind::FUNC_DECL>(member); fd && IsGeneratedJavaMirrorConstructor(*fd)) {
            generatedCtor = fd;
            break;
        }
    }

    return generatedCtor;
}

Ptr<FuncDecl> GetGeneratedJavaMirrorConstructor(ClassLikeDecl& mirror)
{
    CJC_ASSERT(mirror.astKind == AST::ASTKind::CLASS_DECL);
    if (mirror.TestAttr(Attribute::JAVA_MIRROR_SUBTYPE) && !mirror.TestAttr(Attribute::JAVA_MIRROR)) {
        for (auto& superType : mirror.inheritedTypes) {
            if (superType->ty->kind != TypeKind::TYPE_CLASS) {
                continue;
            }
            auto superTy = static_cast<ClassLikeTy*>(superType->ty.get());
            if (superTy->commonDecl->TestAnyAttr(Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE)) {
                return GetGeneratedJavaMirrorConstructor(*superTy->commonDecl);
            }
        }
        CJC_ABORT(); // impl class must have mirror parent
    }
    CJC_ASSERT(mirror.TestAttr(Attribute::JAVA_MIRROR));
    if (auto mirrorClass = DynamicCast<ClassDecl*>(&mirror)) {
        auto curCtor = GetGeneratedConstructorInMirror(*mirrorClass);
        CJC_ASSERT(curCtor);
        return curCtor;
    }

    CJC_ABORT();
    return nullptr;
}

bool IsGeneratedJavaImplConstructor(const FuncDecl& ctor)
{
    if (!ctor.TestAttr(Attribute::CONSTRUCTOR)) {
        return false;
    }

    auto& plists = ctor.funcBody->paramLists;
    if (plists.empty() || plists[0]->params.empty()) {
        return false;
    }

    return plists[0]->params[0]->identifier == JAVA_IMPL_ENTITY_ARG_NAME_IN_GENERATED_CTOR;
}

std::string GetJavaMemberName(const Decl& decl)
{
    for (auto& anno : decl.annotations) {
        if (anno->kind != AnnotationKind::FOREIGN_NAME) {
            continue;
        }

        CJC_ASSERT(anno->args.size() == 1);

        auto litExpr = DynamicCast<LitConstExpr>(anno->args[0]->expr.get());
        CJC_ASSERT(litExpr);
        return litExpr->stringValue;
    }

    return decl.identifier;
}

bool HasPredefinedJavaName(const ClassLikeDecl& decl)
{
    for (auto& anno : decl.annotations) {
        if (anno->kind != AnnotationKind::JAVA_MIRROR && anno->kind != AnnotationKind::JAVA_IMPL) {
            continue;
        }
        return !anno->args.empty();
    }
    return false;
}

Ptr<std::string> GetJavaMirrorAnnoAttr(const ClassLikeDecl& decl)
{
    for (auto& anno : decl.annotations) {
        if (anno->kind != AnnotationKind::JAVA_MIRROR && anno->kind != AnnotationKind::JAVA_IMPL) {
            continue;
        }

        CJC_ASSERT(anno->args.size() < 2); // It is empty if < 2
        if (anno->args.empty()) {
            break;
        }

        CJC_ASSERT(anno->args[0]->expr->astKind == ASTKind::LIT_CONST_EXPR);
        auto lce = As<ASTKind::LIT_CONST_EXPR>(anno->args[0]->expr.get());
        CJC_ASSERT(lce);
        return &lce->stringValue;
    }
    return nullptr;
}

namespace {

template <typename I>
std::string StringJoin(I begin, I end, std::string_view separator)
{
    std::string res;
    for (auto it = begin; it != end; ++it) {
        if (it != begin) {
            res += separator;
        }
        res += *it;
    }
    return res;
}

std::vector<std::string> GetFQNameParts(std::string_view name)
{
    std::vector<std::string> res;
    res.push_back("");

    bool wasBackslash = false;
    for (std::size_t i = 0; i < name.length(); ++i) {
        switch (name[i]) {
            case '\\':
                if (wasBackslash) {
                    res.back() += '\\';
                }
                wasBackslash = true;
                continue;
            case '$':
                if (wasBackslash) {
                    res.back() += '$';
                } else {
                    res.push_back("");
                }
                wasBackslash = false;
                continue;
            default:
                if (wasBackslash) {
                    res.back() += '\\';
                }
                res.back() += name[i];
                wasBackslash = false;
                continue;
        }
    }
    if (wasBackslash) {
        res.back() += '\\';
    }

    return res;
}

std::string GetFQNameJoinBy(const std::string& name, std::string_view separator)
{
    auto parts = GetFQNameParts(name);
    return StringJoin(parts.begin(), parts.end(), separator);
}

}

std::string GetJavaFQNameFromExtendDecl(const ExtendDecl& extendDecl)
{
    auto rt = DynamicCast<const RefType *>(extendDecl.extendedType.get());
    CJC_ASSERT(rt);
    return GetJavaFQName(*rt->ref.target);
}

std::string GetJavaFQName(const Decl& decl, const std::string* genericActualName)
{
    if (auto extendDecl = DynamicCast<const ExtendDecl*>(&decl)) {
        return GetJavaFQNameFromExtendDecl(*extendDecl);
    }
    if (auto classlikeDecl = DynamicCast<const ClassLikeDecl*>(&decl)) {
        auto attr = GetJavaMirrorAnnoAttr(*classlikeDecl);
        if (attr) {
            return GetFQNameJoinBy(*attr, "$");
        }
    }
    if (genericActualName) {
        return decl.GetFullPackageName() + "." + *genericActualName;
    } else {
        return decl.GetFullPackageName() + "." + decl.identifier;
    }
}

std::string GetJavaFQSourceCodeName(const ClassLikeDecl& decl)
{
    auto attr = GetJavaMirrorAnnoAttr(decl);
    return attr ? GetFQNameJoinBy(*attr, ".") : (decl.GetFullPackageName() + "." + decl.identifier);
}

DestructedJavaClassName DestructJavaClassName(const ClassLikeDecl& decl)
{
    auto attr = GetJavaMirrorAnnoAttr(decl);
    if (!attr) {
        return {decl.GetFullPackageName(), decl.identifier, decl.identifier};
    }

    auto parts = GetFQNameParts(*attr);
    CJC_ASSERT(parts.size() > 0);
    auto ind = parts[0].find_last_of('.');
    if (ind == std::string::npos) {
        return {
            .packageName=std::nullopt,
            .topLevelClassName=parts[0],
            .fullClassName=StringJoin(parts.begin(), parts.end(), ".")
        };
    }
    auto package = parts[0].substr(0, ind);
    parts[0] = parts[0].substr(ind + 1);
    return {
        .packageName=package,
        .topLevelClassName=parts[0],
        .fullClassName=StringJoin(parts.begin(), parts.end(), ".")
    };
}

ArrayOperationKind GetArrayOperationKind(Decl& decl)
{
    CJC_ASSERT(IsJArray(*decl.outerDecl));

    if (Is<PropDecl>(decl) && decl.identifier == "length") {
        return ArrayOperationKind::GET_LENGTH;
    }

    if (auto funcDecl = As<ASTKind::FUNC_DECL>(&decl); funcDecl && funcDecl->identifier == "[]") {
        auto paramsNumber = funcDecl->funcBody->paramLists[0]->params.size();
        if (paramsNumber == 1) {
            return ArrayOperationKind::GET;
        } else if (paramsNumber == 2) {
            return ArrayOperationKind::SET;
        }
    }

    if (auto funcDecl = As<ASTKind::FUNC_DECL>(&decl)) {
        if (funcDecl->identifier == JAVA_ARRAY_GET_FOR_REF_TYPES) {
            // Special version of get operation for object-based types
            return ArrayOperationKind::GET;
        }
        if (funcDecl->identifier == JAVA_ARRAY_SET_FOR_REF_TYPES) {
            // Special version of set operation for object-based types
            return ArrayOperationKind::SET;
        }
    }

    CJC_ABORT();

    return ArrayOperationKind::GET;
}

std::string GetJavaPackage(const Decl& decl)
{
    for (auto& anno : decl.annotations) {
        if (anno->kind != AnnotationKind::JAVA_MIRROR && anno->kind != AnnotationKind::JAVA_IMPL) {
            continue;
        }

        CJC_ASSERT(anno->args.size() < 2);
        if (anno->args.empty()) {
            break;
        }

        CJC_ASSERT(anno->args[0]->expr->astKind == ASTKind::LIT_CONST_EXPR);
        auto lce = As<ASTKind::LIT_CONST_EXPR>(anno->args[0]->expr.get());
        CJC_ASSERT(lce);
        std::string fqname = lce->stringValue;
        auto beforeClassNamePos = fqname.rfind(".");
        if (beforeClassNamePos != std::string::npos) {
            fqname.erase(beforeClassNamePos);
        } else {
            return "";
        }
        return fqname;
    }

    return decl.GetFullPackageName();
}

void MangleJNIName(std::string& name)
{
    size_t start_pos = 0;
    while ((start_pos = name.find("_", start_pos)) != std::string::npos) {
        name.replace(start_pos, 1, "_1");
        start_pos += 2;
    }

    std::replace(name.begin(), name.end(), '.', '_');
}

std::string Utils::GetJavaObjectTypeName(const Ty& ty)
{
    if (ty.IsCoreOptionType()) {
        return GetJavaObjectTypeName(*ty.typeArgs[0]);
    }
    if (ty.kind == TypeKind::TYPE_BOOLEAN) {
        return "java.lang.Boolean";
    }
    if (ty.kind == TypeKind::TYPE_INT8) {
        return "java.lang.Byte";
    }
    if (ty.kind == TypeKind::TYPE_UINT16) {
        return "java.lang.Character";
    }
    if (ty.kind == TypeKind::TYPE_INT16) {
        return "java.lang.Short";
    }
    if (ty.kind == TypeKind::TYPE_INT32) {
        return "java.lang.Integer";
    }
    if (ty.kind == TypeKind::TYPE_INT64) {
        return "java.lang.Long";
    }
    if (ty.kind == TypeKind::TYPE_FLOAT32) {
        return "java.lang.Float";
    }
    if (ty.kind == TypeKind::TYPE_FLOAT64) {
        return "java.lang.Double";
    }

    if (ty.kind == TypeKind::TYPE_CLASS || ty.kind == TypeKind::TYPE_INTERFACE) {
        auto& cldecl = *StaticCast<ClassLikeTy&>(ty).commonDecl;
        if (IsJArray(cldecl)) {
            return GetJavaObjectTypeName(*ty.typeArgs[0]) + "[]";
        }
        return GetJavaFQName(cldecl);
    }

    if (ty.IsString()) {
        return GetJavaFQName(*GetJStringDecl());
    }

    return "unknown type";
}

/*
 * Call only when the type is a class or interface.
 * such as: turn class 'Integer' to 'java/lang/Integer'
 */
std::string Utils::GetJavaClassNormalizeSignature(const Ty& cjtype) const
{
    CJC_ASSERT(!IsJArray(cjtype));
    return NormalizeJavaSignature(GetJavaFQName(*StaticCast<ClassLikeTy&>(cjtype).commonDecl));
}

/*
 * Should be called only on java compatible type or a function type over java compatible types
 */
std::string Utils::GetJavaTypeSignature(const Ty& cjtype)
{
    std::string jsig;

    switch (cjtype.kind) {
        case TypeKind::TYPE_UNIT: jsig = "V"; break;
        case TypeKind::TYPE_BOOLEAN: jsig = "Z"; break;
        case TypeKind::TYPE_INT8: jsig = "B"; break;
        case TypeKind::TYPE_UINT16: jsig = "C"; break;
        case TypeKind::TYPE_INT16: jsig = "S"; break;
        case TypeKind::TYPE_INT32: jsig = "I"; break;
        case TypeKind::TYPE_INT64: jsig = "J"; break;
        case TypeKind::TYPE_FLOAT32: jsig = "F"; break;
        case TypeKind::TYPE_FLOAT64: jsig = "D"; break;

        case TypeKind::TYPE_STRUCT: {
            if (cjtype.IsString()) {
                jsig = "L" + NormalizeJavaSignature(GetJavaFQName(*GetJStringDecl())) + ";";
                break;
            }
            if (!cjtype.IsStructArray()) { break; }
            [[fallthrough]]; // for Array<T> - fallback
        }
        case TypeKind::TYPE_ARRAY: jsig = "[" + GetJavaTypeSignature(*cjtype.typeArgs[0]); break;
        case TypeKind::TYPE_ENUM: {
            if (!cjtype.IsCoreOptionType()) {
                break;
            };
            auto& argTy = *cjtype.typeArgs[0];
            if (IsJArray(argTy)) {
                jsig = GetJavaTypeSignature(argTy);
            } else {
                jsig = "L" + NormalizeJavaSignature(GetJavaObjectTypeName(argTy)) + ";";
            }
            break;
        }
        case TypeKind::TYPE_CLASS:
        case TypeKind::TYPE_INTERFACE:
            if (IsJArray(*StaticCast<ClassLikeTy&>(cjtype).commonDecl)) {
                jsig = "[" + GetJavaTypeSignature(*cjtype.typeArgs[0]);
            } else {
                jsig = "L" + NormalizeJavaSignature(
                    GetJavaFQName(*StaticCast<ClassLikeTy&>(cjtype).commonDecl)
                ) + ";";
            }
            break;
        case TypeKind::TYPE_FUNC: {
            auto& funcTy = *StaticCast<FuncTy>(&cjtype);
            jsig = "(";
            for (auto paramTy : funcTy.paramTys) {
                jsig.append(GetJavaTypeSignature(*paramTy));
            }
            jsig.append(")");
            jsig.append(GetJavaTypeSignature(*funcTy.retTy));
            break;
        }
        default: CJC_ABORT(); break; // method must be called only on java-compatible types
    }

    return jsig;
}

std::string Utils::GetJavaTypeSignature(Ty& retTy, const std::vector<Ptr<Ty>>& params)
{
    return GetJavaTypeSignature(*typeManager.GetFunctionTy(params, &retTy));
}

std::string GetMangledJniInitCjObjectFuncName(const BaseMangler& mangler,
                                              const std::vector<OwnedPtr<FuncParam>>& params, bool isGeneratedCtor)
{
    std::string name("initCJObject");

    // the first parameter is added in generated constructor, it should be skipped in mangling
    size_t toSkip = isGeneratedCtor ? 1 : 0;
    for (auto& param : params) {
        if (toSkip > 0) {
            toSkip--;
            continue;
        }
        auto mangledParam = mangler.MangleType(*param->ty);
        std::replace(mangledParam.begin(), mangledParam.end(), '.', '_');
        name += mangledParam;
    }

    return name;
}

std::string GetMangledJniInitCjObjectFuncNameForEnum(
    const BaseMangler& mangler, const std::vector<OwnedPtr<FuncParam>>& params, const std::string funcName)
{
    std::string name = funcName + "initCJObject";

    // the first parameter is added in generated constructor, it should be skipped in mangling
    for (auto& param : params) {
        auto mangledParam = mangler.MangleType(*param->ty);
        std::replace(mangledParam.begin(), mangledParam.end(), '.', '_');
        name += mangledParam;
    }

    return name;
}

bool IsMirror(const Ty& ty)
{
    auto classLikeTy = DynamicCast<ClassLikeTy*>(&ty);
    return classLikeTy && classLikeTy->commonDecl && IsMirror(*classLikeTy->commonDecl);
}

bool IsImpl(const Ty& ty)
{
    auto classLikeTy = DynamicCast<ClassLikeTy*>(&ty);
    return classLikeTy && classLikeTy->commonDecl && IsImpl(*classLikeTy->commonDecl);
}

bool IsCJMappingInterface(const Ty& ty)
{
    auto interfaceTy = DynamicCast<InterfaceTy*>(&ty);
    return interfaceTy && interfaceTy->decl && IsCJMapping(*interfaceTy->decl);
}

bool IsCJMapping(const Ty& ty)
{
    // currently only support struct type, enum type, class type.
    if (auto structTy = DynamicCast<StructTy*>(&ty)) {
        return structTy->decl && IsCJMapping(*structTy->decl);
    } 
    
    if (auto enumTy = DynamicCast<EnumTy*>(&ty)) {
        return enumTy->decl && IsCJMapping(*enumTy->decl);
    }

    if (auto classTy = DynamicCast<ClassTy*>(&ty)) {
        return classTy && classTy->decl && IsCJMapping(*classTy->decl);
    }

    return false;
}

bool IsCJMappingGeneric(const Decl& decl) {
    auto classDecl = DynamicCast<ClassDecl*>(&decl);
    if (classDecl && !classDecl->TestAnyAttr(AST::Attribute::ABSTRACT, AST::Attribute::OPEN) &&
        classDecl->ty->HasGeneric()) {
        return true;
    }

    auto structDecl = DynamicCast<StructDecl*>(&decl);
    if (structDecl && structDecl->ty->HasGeneric()) {
        return true;
    }

    auto enumDecl = DynamicCast<EnumDecl*>(&decl);
    if (enumDecl && enumDecl->ty->HasGeneric()) {
        return true;
    }

    auto interfaceDecl = DynamicCast<InterfaceDecl*>(&decl);
    if (interfaceDecl && interfaceDecl->ty->HasGeneric()) {
        return true;
    }
    
    return false;
}

void SplitAndTrim(std::string str, std::vector<std::string>& types)
{
    size_t pos = str.find(',');
    if (pos == std::string::npos) {
        types.push_back(str);
        return;
    }
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        types.push_back(token);
    }
}

std::string JoinVector(const std::vector<std::string>& vec, const std::string& delimiter)
{
    std::string result;
    for (size_t i = 0; i < vec.size(); ++i) {
        result += vec[i];
        if (i != vec.size() - 1) {
            result += delimiter;
        }
    }
    return result;
}

const Ptr<ClassDecl> GetSyntheticClass(const ImportManager& importManager, const ClassLikeDecl& cld)
{
    ClassDecl* synthetic = importManager.GetImportedDecl<ClassDecl>(
        cld.fullPackageName, GetSyntheticNameFromClassLike(cld));

    CJC_NULLPTR_CHECK(synthetic);

    return Ptr(synthetic);
}

std::string ReplaceClassName(std::string& classTypeSignature, std::string newSegment)
{
    bool hasSemicolon = (!classTypeSignature.empty() && classTypeSignature.back() == ';');
    
    std::string base = hasSemicolon ? classTypeSignature.substr(0, classTypeSignature.length() - 1) : classTypeSignature;
    
    size_t lastSlash = classTypeSignature.rfind('/');
    if (lastSlash != std::string::npos) {
        base = base.substr(0, lastSlash + 1) + newSegment;
    } else {
        base = newSegment;
    }
    
    return hasSemicolon ? base + ";" : base;
}

OwnedPtr<Expr> CreateMirrorConstructorCall(
    const ImportManager& importManager, OwnedPtr<Expr> entity, Ptr<Ty> mirrorTy)
{
    auto curFile = entity->curFile;
    auto classLikeTy = DynamicCast<ClassLikeTy*>(mirrorTy.get());
    if (!classLikeTy) {
        return nullptr;
    }
    if (auto decl = classLikeTy->commonDecl; decl && decl->TestAttr(Attribute::JAVA_MIRROR)) {
        Ptr<FuncDecl> mirrorCtor;

        if (decl->astKind == ASTKind::INTERFACE_DECL ||
            (decl->astKind == ASTKind::CLASS_DECL && decl->TestAttr(Attribute::ABSTRACT))) {
            Ptr<ClassDecl> synthetic = GetSyntheticClass(importManager, *decl);
            mirrorCtor = GetGeneratedConstructorInMirror(*synthetic);
            CJC_ASSERT(mirrorCtor);
        } else if (decl->astKind == AST::ASTKind::CLASS_DECL) {
            auto cld = As<ASTKind::CLASS_LIKE_DECL>(decl);
            CJC_ASSERT(cld);
            mirrorCtor = GetGeneratedJavaMirrorConstructor(*cld);
        } else {
            CJC_ABORT();
        }

        auto ctorCall = CreateCall(mirrorCtor, curFile, std::move(entity));
        if (ctorCall) {
            ctorCall->callKind = CallKind::CALL_OBJECT_CREATION;
            ctorCall->ty = mirrorTy;
        }
        return ctorCall;
    }
    CJC_ABORT();
    return nullptr;
}

bool IsSynthetic(const Node& node)
{
    return node.TestAttr(Attribute::JAVA_MIRROR_SYNTHETIC_WRAPPER);
}

OwnedPtr<Expr> Utils::CreateOptionMatch(
    OwnedPtr<Expr> selector,
    std::function<OwnedPtr<Expr>(VarDecl&)> someBranch,
    std::function<OwnedPtr<Expr>()> noneBranch,
    Ptr<Ty> ty)
{
    auto curFile = selector->curFile;
    CJC_NULLPTR_CHECK(curFile);

    auto& optTy = *selector->ty;
    CJC_ASSERT(optTy.IsCoreOptionType());
    auto optArgTy = optTy.typeArgs[0];

    auto vp = CreateVarPattern(V_COMPILER, optArgTy);
    vp->curFile = curFile;
    vp->varDecl->curFile = curFile;
    auto& someArgVar = *vp->varDecl;

    auto somePattern = MakeOwnedNode<EnumPattern>();
    somePattern->ty = selector->ty;
    somePattern->constructor = CreateOptionSomeRef(optArgTy);
    somePattern->patterns.emplace_back(std::move(vp));
    somePattern->curFile = curFile;
    auto caseSome = CreateMatchCase(std::move(somePattern), someBranch(someArgVar));

    // `case None => Java_CFFI_JavaEntityJobjectNull()`
    auto nonePattern = MakeOwnedNode<EnumPattern>();
    nonePattern->constructor = CreateOptionNoneRef(optArgTy);
    nonePattern->ty = nonePattern->constructor->ty;
    nonePattern->curFile = curFile;
    auto caseNone = CreateMatchCase(std::move(nonePattern), noneBranch());

    return WithinFile(CreateMatchExpr(
        std::move(selector),
        Nodes<MatchCase>(std::move(caseSome), std::move(caseNone)), ty), curFile);
}

bool IsJArray(const Decl& decl)
{
    if (!Is<ClassLikeDecl>(decl)) {
        return false;
    }
    auto classLikeDecl = StaticCast<const ClassLikeDecl*>(&decl);
    if (!decl.TestAttr(Attribute::JAVA_MIRROR)) {
        return false;
    }

    const auto packageName = INTEROP_JAVA_LANG_PACKAGE;
    if (decl.identifier.Val() != INTEROP_JARRAY_NAME || decl.fullPackageName != packageName) {
        return false;
    }

    auto attr = GetJavaMirrorAnnoAttr(*classLikeDecl);
    if (!attr) {
        return false;
    }

    return *attr == "[]";
}

bool IsJArray(const Ty& ty)
{
    auto classLikeTy = DynamicCast<ClassLikeTy>(&ty);
    if (!classLikeTy || !classLikeTy->commonDecl) {
        return false;
    }

    return IsJArray(*classLikeTy->commonDecl);
}

Ptr<FuncDecl> InteropLibBridge::FindGetTypeForTypeParamDecl(File& file) const
{
    for (auto& decl : file.curPackage->files[0]->decls) {
        if (decl->identifier.Val() == GET_TYPE_FOR_TYPE_PARAMETER_FUNC_NAME) {
            return As<ASTKind::FUNC_DECL>(decl.get());
        }
    }
    CJC_ABORT();
    return nullptr;
}

Ptr<FuncDecl> InteropLibBridge::FindCStringToStringDecl()
{
    for (auto& extend : typeManager.GetAllExtendsByTy(*TypeManager::GetCStringTy())) {
        for (auto& ex : extend->GetMemberDecls()) {
            if (ex->identifier == "toString") {
                return As<ASTKind::FUNC_DECL>(ex.get());
            }
        }
    }
    CJC_ABORT();
    return nullptr;
}

Ptr<FuncDecl> InteropLibBridge::FindStringEqualsDecl()
{
    static auto& strDecl = utils.GetStringDecl();
    for (auto& decl : strDecl.GetMemberDeclPtrs()) {
        if (decl->identifier == "==") {
            return As<ASTKind::FUNC_DECL>(decl.get());
        }
    }
    CJC_ABORT();
    return nullptr;
}

Ptr<FuncDecl> InteropLibBridge::FindStringStartsWithDecl()
{
    static auto& strDecl = utils.GetStringDecl();
    for (auto& decl : strDecl.GetMemberDeclPtrs()) {
        if (decl->identifier == "startsWith") {
            return As<ASTKind::FUNC_DECL>(decl.get());
        }
    }
    CJC_ABORT();
    return nullptr;
}

OwnedPtr<Expr> InteropLibBridge::CreateGetTypeForTypeParameterCall(const Ptr<GenericParamDecl> genericParam) const
{
    static auto getTypeForTypeParamDecl = FindGetTypeForTypeParamDecl(*genericParam->curFile);

    std::vector<OwnedPtr<FuncArg>> args;
    auto refExpr = WithinFile(CreateRefExpr(*getTypeForTypeParamDecl), genericParam->curFile);
    refExpr->instTys.push_back(genericParam->ty);

    auto callExpr = CreateCallExpr(std::move(refExpr), std::move(args), getTypeForTypeParamDecl,
        getTypeForTypeParamDecl->funcBody->retType->ty, CallKind::CALL_INTRINSIC_FUNCTION);
    callExpr->curFile = genericParam->curFile;

    return callExpr;
}

OwnedPtr<MatchExpr> InteropLibBridge::CreateMatchByTypeArgument(
    const Ptr<GenericParamDecl> genericParam,
    std::map<std::string, OwnedPtr<Expr>> typeToCaseMap, Ptr<Ty> retTy, OwnedPtr<Expr> defaultCase)
{
    static auto strTy = utils.GetStringDecl().ty;
    static auto cStrToStringDecl = FindCStringToStringDecl();
    static auto strEqualsDecl = FindStringEqualsDecl();
    static auto strStartsWithDecl = FindStringStartsWithDecl();
    static auto boolTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);

    auto file = genericParam->curFile;
    auto cStrToStringCall = MakeOwned<CallExpr>();
    auto cStrToStringMa = CreateMemberAccess(CreateGetTypeForTypeParameterCall(genericParam), "toString");
    cStrToStringMa->ty = cStrToStringDecl->ty;
    cStrToStringMa->target = cStrToStringDecl;
    cStrToStringMa->callOrPattern = cStrToStringCall;

    std::vector<OwnedPtr<FuncArg>> cStrToStringCallArgs;
    cStrToStringCall->ty = strTy;
    cStrToStringCall->resolvedFunction = cStrToStringDecl;
    cStrToStringCall->baseFunc = std::move(cStrToStringMa);
    cStrToStringCall->args = std::move(cStrToStringCallArgs);
    cStrToStringCall->callKind = CallKind::CALL_DECLARED_FUNCTION;
    cStrToStringCall->curFile = file;

    std::vector<OwnedPtr<MatchCase>> cases;

    for (auto & [typeDesc, expr] : typeToCaseMap) {
        auto isOption = typeDesc.rfind(std::string(CORE_PACKAGE_NAME) + ":" + OPTION_NAME, 0) == 0; // starts_with actually
        auto caseCall = MakeOwned<CallExpr>();
        auto caseMa = CreateMemberAccess(ASTCloner::Clone(cStrToStringCall.get()), isOption ? "startsWith" : "==");
        caseMa->ty = isOption ? strStartsWithDecl->ty : strEqualsDecl->ty;
        caseMa->target = isOption ? strStartsWithDecl : strEqualsDecl;
        caseMa->callOrPattern = caseCall;

        std::vector<OwnedPtr<FuncArg>> caseCallArgs;
        caseCallArgs.emplace_back(CreateFuncArg(CreateLitConstExpr(LitConstKind::STRING, typeDesc, strTy)));
        caseCall->ty = boolTy;
        caseCall->resolvedFunction = isOption ? strStartsWithDecl : strEqualsDecl;
        caseCall->baseFunc = std::move(caseMa);
        caseCall->args = std::move(caseCallArgs);
        caseCall->callKind = CallKind::CALL_DECLARED_FUNCTION;
        caseCall->curFile = file;

        OwnedPtr<ConstPattern> patternForType = MakeOwned<ConstPattern>();
        patternForType->ty = strTy;
        patternForType->literal = CreateLitConstExpr(LitConstKind::STRING, typeDesc, strTy);
        patternForType->operatorCallExpr = std::move(caseCall);

        cases.emplace_back(CreateMatchCase(std::move(patternForType), std::move(expr)));
    }

    cases.emplace_back(CreateMatchCase(MakeOwned<WildcardPattern>(), std::move(defaultCase)));

    auto matchExpr = CreateMatchExpr(std::move(cStrToStringCall), std::move(cases), retTy);
    matchExpr->curFile = genericParam->curFile;

    return std::move(matchExpr);
}

void GenerateSyntheticClassMemberStubs(
    ClassDecl& synthetic,
    const MemberMap& interfaceMembers,
    const MemberMap& instanceMembers)
{
    GenerateSyntheticClassAbstractMemberImplStubs(synthetic, interfaceMembers);
    GenerateSyntheticClassAbstractMemberImplStubs(synthetic, instanceMembers);
}

namespace {

constexpr auto NATIVE_CONSTRUCTOR_MARKER_CLASS_NAME = "$$NativeConstructorMarker";
constexpr auto NATIVE_CONSTRUCTOR_MARKER_PACKAGE_NAME = "cangjie.lang.internal";

} // namespace

std::string GetConstructorMarkerFQName()
{
    std::string res;
    res += NATIVE_CONSTRUCTOR_MARKER_PACKAGE_NAME;
    res += ".";
    res += NATIVE_CONSTRUCTOR_MARKER_CLASS_NAME;
    return res;
}

std::string GetConstructorMarkerClassName()
{
    return NATIVE_CONSTRUCTOR_MARKER_CLASS_NAME;
}

OwnedPtr<ClassDecl> CreateConstructorMarkerClassDecl()
{
    OwnedPtr<ClassDecl> markerClassDecl = MakeOwned<ClassDecl>();
    markerClassDecl->identifier = std::string(NATIVE_CONSTRUCTOR_MARKER_CLASS_NAME);
    markerClassDecl->fullPackageName = NATIVE_CONSTRUCTOR_MARKER_PACKAGE_NAME;
    return markerClassDecl;
}
}
