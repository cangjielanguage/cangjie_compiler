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
    return Native::FFI::GetOptionDecl(importManager);
}

Ptr<Decl> Utils::GetOptionSomeDecl()
{
    return Native::FFI::GetOptionSomeDecl(importManager);
}

Ptr<Decl> Utils::GetOptionNoneDecl()
{
    return Native::FFI::GetOptionNoneDecl(importManager);
}

OwnedPtr<Expr> Utils::CreateOptionSomeRef(ModalTy ty)
{
    return Native::FFI::CreateOptionSomeRef(importManager, typeManager, ty);
}

OwnedPtr<Expr> Utils::CreateOptionNoneRef(ModalTy ty)
{
    return Native::FFI::CreateOptionNoneRef(importManager, typeManager, ty);
}

OwnedPtr<Expr> Utils::CreateOptionSomeCall(OwnedPtr<Expr> expr, ModalTy ty)
{
    return Native::FFI::CreateOptionSomeCall(importManager, typeManager, std::move(expr), ty);
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
    if ((mirrorLike.IsJavaForeign() || mirrorLike.IsJavaExport()) && !IsJObject(mirrorLike)) {
        auto superClass = mirrorLike.GetSuperClassDecl();
        if (!superClass) {
            CJC_ABORT();
        }

        if (!superClass->IsJavaForeign() && !superClass->IsJavaExport()) {
            CJC_ABORT(); // neither mirror or mirror subtype are super type of [mirror]
        }

        return GetJavaRefField(*superClass);
    }

    CJC_ASSERT(mirrorLike.IsJavaForeign());
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
    CJC_ASSERT(mirrorLike.IsJavaForeign() || mirrorLike.IsJavaExport());
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

bool IsGeneratedJavaMirrorConstructor(const FuncDecl& ctor)
{
    if (ctor.outerDecl == nullptr) {
        return false;
    }
    return ctor.outerDecl->IsJavaForeign() && ctor.TestAttr(Attribute::CONSTRUCTOR) &&
        ctor.TestAttr(Attribute::COMPILER_ADD);
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
    CJC_ASSERT(mirror.IsJavaForeign());
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
    if (mirrorLike.IsJavaExport()) {
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
    CJC_ASSERT(mirrorLike.IsJavaForeign());
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

bool HasPredefinedJavaName(const ClassLikeDecl& decl)
{
    for (auto& anno : decl.annotations) {
        switch (anno->kind) {
            case AnnotationKind::JAVA:
            case AnnotationKind::JAVA_MIRROR:
            case AnnotationKind::JAVA_IMPL: break;
            default: continue;
        }
        return !anno->args.empty();
    }
    return false;
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
std::string GetJavaJniClassName(const Ty& cjtype)
{
    if (IsJArray(cjtype)) {
        // arrays use full signatures and work for primitives
        return GetJniTypeSignature(cjtype);
    }
    return NormalizeJavaSignature(GetJavaFQName(*StaticCast<ClassLikeTy&>(cjtype).commonDecl));
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

OwnedPtr<Expr> CreateMirrorConstructorCall(
    const ImportManager& importManager, OwnedPtr<Expr> entity, Ptr<Ty> mirrorTy)
{
    auto curFile = entity->curFile;
    auto classLikeTy = DynamicCast<ClassLikeTy*>(mirrorTy.get());
    if (!classLikeTy) {
        return nullptr;
    }
    if (auto decl = classLikeTy->commonDecl; decl && decl->IsJavaForeign()) {
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
    if (!decl.IsJavaForeign()) {
        return false;
    }

    const auto packageName = INTEROP_JAVA_LANG_PACKAGE;
    if (decl.identifier.Val() != INTEROP_JARRAY_NAME || decl.fullPackageName != packageName) {
        return false;
    }

    auto attr = GetJavaFQName(*classLikeDecl); // prioritizes foreign name instead of decl name.
    return attr == "[]";
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

} // namespace Cangjie::Interop::Java
