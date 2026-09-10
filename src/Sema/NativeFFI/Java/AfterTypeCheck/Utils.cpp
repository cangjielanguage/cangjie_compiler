// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Utils.h"
#include "NativeFFI/Java/AfterTypeCheck/InteropLibBridge.h"
#include "NativeFFI/Utils.h"
#include "NativeFFI/Java/Utils.h"
#include "TypeCheckUtil.h"

#include "Desugar/AfterTypeCheck.h"
#include "JavaDesugarManager.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/ConstantsUtils.h"
#include <string_view>

namespace Cangjie::Interop::Java {
using namespace TypeCheckUtil;
using namespace Cangjie::Native::FFI;
using namespace Cangjie::Native::FFI::Java;

namespace {
using namespace Cangjie;
using namespace AST;

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
} // namespace

Utils::Utils(ImportManager& importManager, TypeManager& typeManager)
    : importManager(importManager), typeManager(typeManager)
{
}

ModalTy Utils::GetOptionTy(ModalTy ty)
{
    return ModalTy{typeManager.GetEnumTy(*GetOptionDecl(), {ty.Ty()})};
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

OwnedPtr<Expr> Utils::CreateOptionSomeRef(ModalTy ty)
{
    auto someDeclRef = CreateRefExpr(*GetOptionSomeDecl());
    auto optionActualTy = GetOptionTy(ty);
    someDeclRef->SetTy({typeManager.GetFunctionTy({{ty}}, {optionActualTy})});
    return someDeclRef;
}

OwnedPtr<Expr> Utils::CreateOptionNoneRef(ModalTy ty)
{
    auto noneDeclRef = CreateRefExpr(*GetOptionNoneDecl());
    auto optionActualTy = GetOptionTy(ty);
    noneDeclRef->SetTy({optionActualTy});
    return noneDeclRef;
}

OwnedPtr<Expr> Utils::CreateOptionSomeCall(OwnedPtr<Expr> expr, ModalTy ty)
{
    std::vector<OwnedPtr<FuncArg>> someDeclCallArgs{};
    someDeclCallArgs.emplace_back(CreateFuncArg(std::move(expr)));
    auto someDeclCall = CreateCallExpr(CreateOptionSomeRef(ty), std::move(someDeclCallArgs));
    someDeclCall->SetTy(GetOptionTy(ty));
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
    CJC_ASSERT(!mirrorLike.IsInterfaceDecl()); // no field in interface
    if ((mirrorLike.IsJavaMirror() || mirrorLike.IsJavaImpl()) && !IsJObject(mirrorLike)) {
        auto superClass = mirrorLike.GetSuperClassDecl();
        if (!superClass) {
            CJC_ABORT();
        }

        if (!superClass->IsJavaMirror() && !superClass->IsJavaImpl()) {
            CJC_ABORT(); // neither mirror or mirror subtype are super type of [mirror]
        }

        return GetJavaRefField(*superClass);
    }

    CJC_ASSERT(mirrorLike.IsJavaMirror());
    CJC_ASSERT(IsJObject(mirrorLike));
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
    return fd.astKind == ASTKind::FUNC_DECL && fd.TestAttr(Attribute::COMPILER_ADD) &&
        fd.identifier.Val() == JAVA_REF_GETTER_FUNC_NAME;
}

Ptr<FuncDecl> GetJavaRefGetter(ClassLikeDecl& mirrorLike)
{
    CJC_ASSERT(mirrorLike.IsJavaMirror() || mirrorLike.IsJavaImpl());
    const std::function<bool(const Decl& d)>& isDeclJavaRefGetterFunc = [](const Decl& d) {
        return IsJavaRefGetter(d);
    };

    if (auto cd = DynamicCast<ClassDecl*>(&mirrorLike)) {
        if (!IsJObject(mirrorLike)) {
            return GetJavaRefGetter(*cd->GetSuperClassDecl());
        } else {
            return FindFirstMemberDecl<FuncDecl, ASTKind::FUNC_DECL>(mirrorLike, isDeclJavaRefGetterFunc);
        }
    } else {
        return FindFirstMemberDecl<FuncDecl, ASTKind::FUNC_DECL>(mirrorLike, isDeclJavaRefGetterFunc);
    }
}

OwnedPtr<Expr> CreateJavaRefCall(OwnedPtr<Expr> expr, FuncDecl& javaRefGetter)
{
    // expr ty decl and javaRef outerDecl must be the same
    CJC_ASSERT(expr->GetTy());
    CJC_ASSERT(expr->GetTy()->IsClassLike());
    CJC_ASSERT(IsMirror(*expr->GetTy()) || IsImpl(*expr->GetTy()));
    auto curFile = expr->curFile;
    CJC_NULLPTR_CHECK(curFile);

    return CreateCallExpr(WithinFile(CreateMemberAccess(std::move(expr), javaRefGetter), curFile), {}, &javaRefGetter,
        StaticCast<FuncTy*>(javaRefGetter.DataTy())->retTy, CallKind::CALL_DECLARED_FUNCTION);
}

OwnedPtr<Expr> CreateJavaRefCall(OwnedPtr<Expr> expr, VarDecl& javaref)
{
    // expr ty decl and javaref outerDecl must be the same

    CJC_ASSERT(expr->GetTy()->IsClassLike());
    CJC_ASSERT(IsMirror(*expr->GetTy()) || IsImpl(*expr->GetTy()));

    auto curFile = expr->curFile;
    CJC_NULLPTR_CHECK(curFile);

    return WithinFile(CreateMemberAccess(std::move(expr), javaref), curFile);
}

OwnedPtr<Expr> CreateJavaRefCall(OwnedPtr<Expr> expr, ClassLikeDecl& mirrorLike)
{
    CJC_ASSERT(mirrorLike.IsJavaMirror() || mirrorLike.IsJavaImpl());
    if (auto mirrorLikeClass = As<ASTKind::CLASS_DECL>(&mirrorLike)) {
        return CreateJavaRefCall(std::move(expr), *GetJavaRefField(*mirrorLikeClass));
    }

    // for an interface
    return CreateJavaRefCall(std::move(expr), *GetJavaRefGetter(mirrorLike));
}

OwnedPtr<Expr> CreateJavaRefCall(ClassLikeDecl& mirrorLike, Ptr<File> curFile)
{
    auto thisExpr = CreateThisRef(Ptr(&mirrorLike), mirrorLike.DataTy(), curFile);
    return CreateJavaRefCall(std::move(thisExpr), mirrorLike);
}

OwnedPtr<Expr> CreateJavaRefCall(OwnedPtr<Expr> expr)
{
    CJC_ASSERT(expr->DataTy()->IsClassLike());
    auto classLikeTy = StaticCast<ClassLikeTy*>(expr->DataTy());
    CJC_ASSERT(classLikeTy->commonDecl);
    return CreateJavaRefCall(std::move(expr), *classLikeTy->commonDecl);
}

bool IsWrappingConstructorOfJavaMirror(const FuncDecl& ctor)
{
    if (!ctor.TestAttr(Attribute::CONSTRUCTOR, Attribute::COMPILER_ADD)) {
        return false;
    }
    if (ctor.funcBody->paramLists.empty()) {
        return false;
    }
    auto& params = ctor.funcBody->paramLists.front()->params;
    if (params.size() != 1) {
        return false;
    }
    auto& javaEntityParam = *params.front();
    return InteropLibBridge::IsJavaEntityTy(*javaEntityParam.GetTy());
}

Ptr<FuncDecl> GetJavaMirrorWrappingConstructor(ClassDecl& mirror)
{
    CJC_ASSERT(mirror.IsJavaMirror());
    Ptr<FuncDecl> generatedCtor;

    for (auto& member : mirror.GetMemberDecls()) {
        if (auto fd = As<ASTKind::FUNC_DECL>(member); fd && IsWrappingConstructorOfJavaMirror(*fd)) {
            generatedCtor = fd;
            break;
        }
    }

    return generatedCtor;
}

Ptr<FuncDecl> GetJavaMirrorWrappingConstructor(ClassLikeDecl& mirrorLike)
{
    CJC_ASSERT(mirrorLike.astKind == AST::ASTKind::CLASS_DECL);
    if (mirrorLike.IsJavaImpl()) {
        for (auto& superType : mirrorLike.inheritedTypes) {
            if (superType->TyKind() != TypeKind::TYPE_CLASS) {
                continue;
            }
            auto superTy = StaticCast<ClassLikeTy*>(superType->GetTy().get());
            if (IsMirror(*superTy) || IsImpl(*superTy)) {
                return GetJavaMirrorWrappingConstructor(*superTy->commonDecl);
            }
        }
        CJC_ABORT(); // impl class must have mirror parent
    }
    CJC_ASSERT(mirrorLike.IsJavaMirror());
    if (auto mirrorClass = As<ASTKind::CLASS_DECL>(&mirrorLike)) {
        auto curCtor = GetJavaMirrorWrappingConstructor(*mirrorClass);
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

} // namespace

std::string GetJavaFQNameFromExtendDecl(const ExtendDecl& extendDecl)
{
    auto rt = DynamicCast<const RefType *>(extendDecl.extendedType.get());
    CJC_ASSERT(rt);
    return GetJavaFQName(*rt->ref.target);
}

std::string GetJavaFQName(const Decl& decl)
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
    return decl.GetFullPackageName() + "." + decl.identifier;
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
        return {.packageName = std::nullopt,
            .topLevelClassName = parts[0],
            .fullClassName = StringJoin(parts.begin(), parts.end(), ".")};
    }
    auto package = parts[0].substr(0, ind);
    parts[0] = parts[0].substr(ind + 1);
    return {.packageName = package,
        .topLevelClassName = parts[0],
        .fullClassName = StringJoin(parts.begin(), parts.end(), ".")};
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
            // Array "get" has one parameter: index.
            return ArrayOperationKind::GET;
        } else if (paramsNumber == 2) {
            // Array "set" has two parameters: index and value to be set.
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

        // @JavaMirror or @JavaImpl annotations could accept optional string literal argument with fully-qualified name.
        CJC_ASSERT_WITH_MSG(anno->args.size() < 2, "@JavaMirror or @JavaImpl could accept maximum one argument");
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

std::string Utils::GetJavaObjectTypeName(const Ty& ty)
{
    if (ty.IsCoreOptionType()) {
        return GetJavaObjectTypeName(*ty.TyArg(0));
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
            CJC_ASSERT_WITH_MSG(!ty.TyArgs().empty(), "JArray type must be generic");
            return GetJavaObjectTypeName(*ty.TyArg(0)) + "[]";
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
        case TypeKind::TYPE_UNIT:
            jsig = "V";
            break;
        case TypeKind::TYPE_BOOLEAN:
            jsig = "Z";
            break;
        case TypeKind::TYPE_INT8:
            jsig = "B";
            break;
        case TypeKind::TYPE_UINT16:
            jsig = "C";
            break;
        case TypeKind::TYPE_INT16:
            jsig = "S";
            break;
        case TypeKind::TYPE_INT32:
            jsig = "I";
            break;
        case TypeKind::TYPE_INT64:
            jsig = "J";
            break;
        case TypeKind::TYPE_FLOAT32:
            jsig = "F";
            break;
        case TypeKind::TYPE_FLOAT64:
            jsig = "D";
            break;

        case TypeKind::TYPE_STRUCT: {
            if (cjtype.IsString()) {
                jsig = "L" + NormalizeJavaSignature(GetJavaFQName(*GetJStringDecl())) + ";";
                break;
            }
            if (!cjtype.IsStructArray()) {
                break;
            }
            [[fallthrough]]; // for Array<T> - fallback
        }
        case TypeKind::TYPE_ARRAY:
            jsig = "[" + GetJavaTypeSignature(*cjtype.TyArg(0));
            break;
        case TypeKind::TYPE_ENUM: {
            if (!cjtype.IsCoreOptionType()) {
                break;
            };
            CJC_ASSERT_WITH_MSG(!cjtype.TyArgs().empty(), "Option type must be generic");
            auto& argTy = *cjtype.TyArg(0);
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
                jsig = "[" + GetJavaTypeSignature(*cjtype.TyArg(0));
            } else {
                jsig = "L" + NormalizeJavaSignature(GetJavaFQName(*StaticCast<ClassLikeTy&>(cjtype).commonDecl)) + ";";
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
        default:
            CJC_ABORT();
            break; // method must be called only on java-compatible types
    }

    return jsig;
}

std::string Utils::GetJavaTypeSignature(Ty& retTy, const std::vector<ModalTy>& params)
{
    return GetJavaTypeSignature(*typeManager.GetFunctionTy(params, {&retTy}));
}

OwnedPtr<CallExpr> Utils::CreateZeroValue(Ptr<Ty> ty, File& curFile) const
{
    static constexpr std::string_view ZERO_VALUE_INTRINSIC_NAME = "zeroValue";
    static auto zeroValueDecl = importManager.GetCoreDecl<FuncDecl>(std::string(ZERO_VALUE_INTRINSIC_NAME));
    CJC_ASSERT(zeroValueDecl);
    auto zeroValueCall = MakeOwned<CallExpr>();

    auto fdRef = WithinFile(CreateRefExpr(*zeroValueDecl), &curFile);
    fdRef->instTys.push_back(ty);
    auto refTy = typeManager.GetInstantiatedTy(zeroValueDecl->GetTy(),
        GenerateTypeMapping(*zeroValueDecl, fdRef->instTys));
    fdRef->SetTy(refTy);

    zeroValueCall->baseFunc = std::move(fdRef);
    zeroValueCall->SetTy(ty);
    zeroValueCall->callKind = CallKind::CALL_INTRINSIC_FUNCTION;
    zeroValueCall->resolvedFunction = zeroValueDecl;
    zeroValueCall->curFile = &curFile;
    return zeroValueCall;
}

std::string GetMangledJniInitCjObjectFuncName(
    const BaseMangler& mangler, const std::vector<OwnedPtr<FuncParam>>& params, bool isGeneratedCtor)
{
    std::string name("initCJObject");

    // the first parameter is added in generated constructor, it should be skipped in mangling
    size_t toSkip = isGeneratedCtor ? 1 : 0;
    for (auto& param : params) {
        if (toSkip > 0) {
            toSkip--;
            continue;
        }
        auto mangledParam = mangler.MangleType(*param->GetTy());
        std::replace(mangledParam.begin(), mangledParam.end(), '.', '_');
        name += mangledParam;
    }

    return name;
}

std::string GetMangledJniInitCjObjectFuncName(const BaseMangler& mangler, const std::vector<Ptr<Ty>>& types)
{
    std::string name("initCJObject");

    for (auto& ty : types) {
        auto mangledParam = mangler.MangleType(*ty);
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
        auto mangledParam = mangler.MangleType(*param->GetTy());
        std::replace(mangledParam.begin(), mangledParam.end(), '.', '_');
        name += mangledParam;
    }

    return name;
}

bool IsOptionOfString(Ptr<Ty> ty)
{
    return ty->IsCoreOptionType() && ty->typeArgs.size() > 0 && ty->typeArgs[0]->IsString();
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

std::string ReplaceClassName(std::string& classTypeSignature, std::string newSegment)
{
    bool hasSemicolon = (!classTypeSignature.empty() && classTypeSignature.back() == ';');

    std::string base = hasSemicolon
        ? classTypeSignature.substr(0, classTypeSignature.length() - 1)
        : classTypeSignature;

    size_t lastSlash = classTypeSignature.rfind('/');
    if (lastSlash != std::string::npos) {
        base = base.substr(0, lastSlash + 1) + newSegment;
    } else {
        base = newSegment;
    }

    return hasSemicolon ? base + ";" : base;
}

std::string NormalizeJavaSignature(const std::string& sig)
{
    std::string normalized = sig;
    std::replace(normalized.begin(), normalized.end(), '.', '/');
    return normalized;
}

OwnedPtr<Expr> CreateMirrorConstructorCall(
    const ImportManager& importManager, OwnedPtr<Expr> entity, Ptr<Ty> mirrorTy)
{
    auto curFile = entity->curFile;
    auto classLikeTy = DynamicCast<ClassLikeTy*>(mirrorTy.get());
    if (!classLikeTy) {
        return nullptr;
    }
    if (auto decl = classLikeTy->commonDecl; decl && decl->IsJavaMirror()) {
        Ptr<FuncDecl> mirrorCtor;

        if (decl->astKind == ASTKind::INTERFACE_DECL ||
            (decl->astKind == ASTKind::CLASS_DECL && decl->TestAttr(Attribute::ABSTRACT))) {
            Ptr<ClassDecl> synthetic = GetSyntheticClass(importManager, *decl);
            mirrorCtor = GetJavaMirrorWrappingConstructor(*synthetic);
            CJC_ASSERT(mirrorCtor);
        } else if (decl->astKind == AST::ASTKind::CLASS_DECL) {
            auto cld = As<ASTKind::CLASS_LIKE_DECL>(decl);
            CJC_ASSERT(cld);
            mirrorCtor = GetJavaMirrorWrappingConstructor(*cld);
        } else {
            CJC_ABORT();
        }

        auto ctorCall = CreateCall(mirrorCtor, curFile, std::move(entity));
        if (ctorCall) {
            ctorCall->callKind = CallKind::CALL_OBJECT_CREATION;
            ctorCall->SetTy(mirrorTy);
        }
        return ctorCall;
    }
    CJC_ABORT();
    return nullptr;
}

OwnedPtr<Expr> Utils::CreateOptionMatch(OwnedPtr<Expr> selector, std::function<OwnedPtr<Expr>(VarDecl&)> someBranch,
    std::function<OwnedPtr<Expr>()> noneBranch, ModalTy ty)
{
    auto curFile = selector->curFile;
    CJC_NULLPTR_CHECK(curFile);

    auto& optTy = *selector->GetTy();
    CJC_ASSERT(optTy.IsCoreOptionType());
    CJC_ASSERT_WITH_MSG(!optTy.TyArgs().empty(), "Option type must be generic");
    ModalTy optArgTy{optTy.TyArg(0)};

    auto vp = CreateVarPattern(V_COMPILER, optArgTy);
    vp->curFile = curFile;
    vp->varDecl->curFile = curFile;
    auto& someArgVar = *vp->varDecl;

    auto somePattern = MakeOwnedNode<EnumPattern>();
    somePattern->SetTy(selector->GetTy());
    somePattern->constructor = CreateOptionSomeRef(optArgTy);
    somePattern->patterns.emplace_back(std::move(vp));
    somePattern->curFile = curFile;
    auto caseSome = CreateMatchCase(std::move(somePattern), someBranch(someArgVar));

    // `case None => Java_CFFI_JavaEntityJobjectNull()`
    auto nonePattern = MakeOwnedNode<EnumPattern>();
    nonePattern->constructor = CreateOptionNoneRef(optArgTy);
    nonePattern->SetTy(nonePattern->constructor->GetTy());
    nonePattern->curFile = curFile;
    auto caseNone = CreateMatchCase(std::move(nonePattern), noneBranch());

    return WithinFile(
        CreateMatchExpr(std::move(selector), Nodes<MatchCase>(std::move(caseSome), std::move(caseNone)), {ty}),
        curFile);
}

OwnedPtr<FuncDecl> Utils::CreateNativeFunc(const std::string& name,
    std::vector<OwnedPtr<FuncParam>>&& params, Ptr<Ty> retTy, std::vector<OwnedPtr<Node>>&& nodes,
    File& curFile, std::string& moduleName, std::string& fullPackageName) const
{
    CJC_ASSERT(retTy->IsMetCType());
    for (auto& param : params) {
        CJC_ASSERT(param->GetTy()->IsMetCType());
    }

    auto block = MakeOwned<Block>();
    block->EnableAttr(Attribute::COMPILER_ADD);
    block->SetTy(retTy);
    block->curFile = &curFile;
    std::move(nodes.begin(), nodes.end(), std::back_inserter(block->body));

    std::vector<ModalTy> funcTyParams;
    for (auto& param : params) {
        funcTyParams.push_back(param->GetTy());
    }

    auto funcBody = CreateFuncBody({}, nullptr, std::move(block), retTy);
    funcBody->curFile = &curFile;
    funcBody->paramLists.emplace_back(CreateFuncParamList(std::move(params)));

    auto funcTy = typeManager.GetFunctionTy(funcTyParams, retTy, {.isC = true});
    auto fdecl = CreateFuncDecl(name, std::move(funcBody), {funcTy});
    fdecl->funcBody->funcDecl = fdecl.get();
    fdecl->EnableAttr(Attribute::C);
    fdecl->EnableAttr(Attribute::GLOBAL);
    fdecl->EnableAttr(Attribute::PUBLIC);
    fdecl->EnableAttr(Attribute::NO_MANGLE);
    fdecl->EnableAttr(Attribute::UNSAFE);
    fdecl->curFile = &curFile;
    fdecl->moduleName = moduleName;
    fdecl->fullPackageName = fullPackageName;

    return fdecl;
}

bool IsJArray(const Decl& decl)
{
    if (!Is<ClassLikeDecl>(decl)) {
        return false;
    }
    auto classLikeDecl = StaticCast<const ClassLikeDecl*>(&decl);
    if (!decl.IsJavaMirror()) {
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
    refExpr->instTys.push_back(genericParam->DataTy());

    auto callExpr = CreateCallExpr(std::move(refExpr), std::move(args), getTypeForTypeParamDecl,
        getTypeForTypeParamDecl->funcBody->retType->GetTy(), CallKind::CALL_INTRINSIC_FUNCTION);
    callExpr->curFile = genericParam->curFile;

    return callExpr;
}

OwnedPtr<MatchExpr> InteropLibBridge::CreateMatchByTypeArgument(const Ptr<GenericParamDecl> genericParam,
    std::map<std::string, OwnedPtr<Expr>> typeToCaseMap, ModalTy retTy, OwnedPtr<Expr> defaultCase)
{
    static auto strTy = utils.GetStringDecl().GetTy();
    static auto cStrToStringDecl = FindCStringToStringDecl();
    static auto strEqualsDecl = FindStringEqualsDecl();
    static auto strStartsWithDecl = FindStringStartsWithDecl();
    static const auto boolTy = TypeManager::GetPrimitiveTy(TypeKind::TYPE_BOOLEAN);

    auto file = genericParam->curFile;
    auto cStrToStringCall = MakeOwned<CallExpr>();
    auto cStrToStringMa = CreateMemberAccess(CreateGetTypeForTypeParameterCall(genericParam), "toString");
    cStrToStringMa->SetTy(cStrToStringDecl->GetTy());
    cStrToStringMa->target = cStrToStringDecl;
    cStrToStringMa->callOrPattern = cStrToStringCall;

    std::vector<OwnedPtr<FuncArg>> cStrToStringCallArgs;
    cStrToStringCall->SetTy(strTy);
    cStrToStringCall->resolvedFunction = cStrToStringDecl;
    cStrToStringCall->baseFunc = std::move(cStrToStringMa);
    cStrToStringCall->args = std::move(cStrToStringCallArgs);
    cStrToStringCall->callKind = CallKind::CALL_DECLARED_FUNCTION;
    cStrToStringCall->curFile = file;

    std::vector<OwnedPtr<MatchCase>> cases;

    for (auto& [typeDesc, expr] : typeToCaseMap) {
        auto isOption =
            typeDesc.rfind(std::string(CORE_PACKAGE_NAME) + ":" + OPTION_NAME, 0) == 0; // starts_with actually
        auto caseCall = MakeOwned<CallExpr>();
        auto caseMa = CreateMemberAccess(ASTCloner::Clone(cStrToStringCall.get()), isOption ? "startsWith" : "==");
        caseMa->SetTy(isOption ? strStartsWithDecl->GetTy() : strEqualsDecl->GetTy());
        caseMa->target = isOption ? strStartsWithDecl : strEqualsDecl;
        caseMa->callOrPattern = caseCall;

        std::vector<OwnedPtr<FuncArg>> caseCallArgs;
        caseCallArgs.emplace_back(CreateFuncArg(CreateLitConstExpr(LitConstKind::STRING, typeDesc, strTy)));
        caseCall->SetTy({boolTy});
        caseCall->resolvedFunction = isOption ? strStartsWithDecl : strEqualsDecl;
        caseCall->baseFunc = std::move(caseMa);
        caseCall->args = std::move(caseCallArgs);
        caseCall->callKind = CallKind::CALL_DECLARED_FUNCTION;
        caseCall->curFile = file;

        OwnedPtr<ConstPattern> patternForType = MakeOwned<ConstPattern>();
        patternForType->SetTy(strTy);
        patternForType->literal = CreateLitConstExpr(LitConstKind::STRING, typeDesc, strTy);
        patternForType->operatorCallExpr = std::move(caseCall);

        cases.emplace_back(CreateMatchCase(std::move(patternForType), std::move(expr)));
    }

    cases.emplace_back(CreateMatchCase(MakeOwned<WildcardPattern>(), std::move(defaultCase)));

    auto matchExpr = CreateMatchExpr(std::move(cStrToStringCall), std::move(cases), retTy);
    matchExpr->curFile = genericParam->curFile;

    return std::move(matchExpr);
}

namespace {

constexpr auto NATIVE_CONSTRUCTOR_MARKER_CLASS_NAME = "$$NativeConstructorMarker";
constexpr auto NATIVE_CONSTRUCTOR_MARKER_PACKAGE_NAME = "cangjie.lang.internal";

} // namespace

Ptr<FuncDecl> GetJavaImplRegistryCompanionConstructor(AST::ClassDecl& companion)
{
    CJC_ASSERT(IsImplRegistryCompanion(companion));

    for (auto member : companion.GetMemberDeclPtrs()) {
        if (!member->TestAttr(Attribute::CONSTRUCTOR)) {
            continue;
        }
        if (auto ctor = As<ASTKind::FUNC_DECL>(member)) {
            return ctor;
        }
    }

    CJC_ABORT_WITH_MSG("Bad registry companion");
    return nullptr;
}

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

void GenerateSyntheticClassMemberStubs(ClassDecl& synthetic, const MemberMap& members)
{
    for (const auto& idMemberSignature : members) {
        const auto& signature = idMemberSignature.second;

        // only abstract functions must be inside synthetic class
        if (!signature.decl->TestAttr(Attribute::ABSTRACT)) {
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
            default:
                continue;
        }
    }
}
} // namespace Cangjie::Interop::Java
