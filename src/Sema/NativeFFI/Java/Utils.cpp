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
#include "Utils.h"
#include "NativeFFI/Java/AfterTypeCheck/Utils.h"
#include "NativeFFI/Utils.h"
#include "NativeFFI/Java/JavaClassSignature.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Utils/CheckUtils.h"

namespace Cangjie::Native::FFI::Java {
using namespace Cangjie::AST;
using namespace Cangjie::Interop::Java;

ArrayOperationKind GetArrayOperationKind(Decl& decl)
{
    static constexpr std::string_view JAVA_ARRAY_LENGTH = "length";
    CJC_ASSERT(IsJArray(*decl.outerDecl));

    if (Is<PropDecl>(decl) && decl.identifier == JAVA_ARRAY_LENGTH) {
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

const Ptr<ClassDecl> GetSyntheticClass(const ImportManager& importManager, const ClassLikeDecl& cld)
{
    ClassDecl* synthetic =
        importManager.GetImportedDecl<ClassDecl>(cld.fullPackageName, GetMirrorReferenceWrapperNameFromClassLike(cld));

    CJC_NULLPTR_CHECK(synthetic);

    return Ptr(synthetic);
}

std::string GetMirrorReferenceWrapperNameFromClassLike(const ClassLikeDecl& mirrorDecl)
{
    constexpr auto wrapperNameSuffix = "$impl";
    CJC_ASSERT(mirrorDecl.IsInterfaceDecl() || mirrorDecl.IsAbstractClass());
    CJC_ASSERT(mirrorDecl.IsJavaForeign());
    return mirrorDecl.identifier.Val() + wrapperNameSuffix;
}

std::string GetImplRegistryCompanionClassName(const ClassLikeDecl& javaImplDecl)
{
    constexpr auto wrapperNameSuffix = "$reg";
    CJC_ASSERT(IsImpl(javaImplDecl));
    return javaImplDecl.identifier.Val() + wrapperNameSuffix;
}

std::vector<Ptr<AST::ClassLikeDecl>> GetJavaMirrors(AST::File& file)
{
    std::vector<Ptr<ClassLikeDecl>> mirrors;
    for (auto& decl : file.decls) {
        if (decl->IsJavaForeign()) {
            auto mirror = StaticAs<ASTKind::CLASS_LIKE_DECL>(decl.get());
            mirrors.emplace_back(mirror);
        }
    }
    return mirrors;
}

std::vector<Ptr<AST::ClassLikeDecl>> GetJavaMirrors(AST::Package& pkg)
{
    std::vector<Ptr<ClassLikeDecl>> mirrors;
    for (auto& file : pkg.files) {
        auto fileMirrors = GetJavaMirrors(*file);
        std::move(fileMirrors.begin(), fileMirrors.end(), std::back_inserter(mirrors));
    }
    return mirrors;
}

std::vector<Ptr<AST::ClassDecl>> GetJavaImpls(AST::File& file)
{
    std::vector<Ptr<ClassDecl>> impls;
    for (auto& decl : file.decls) {
        if (IsImpl(*decl)) {
            if (auto impl = As<ASTKind::CLASS_DECL>(decl.get())) {
                impls.push_back(impl);
            }
        }
    }
    return impls;
}

std::vector<Ptr<AST::ClassDecl>> GetJavaImpls(AST::Package& pkg)
{
    std::vector<Ptr<ClassDecl>> impls;
    for (auto& file : pkg.files) {
        auto fileImpls = GetJavaImpls(*file);
        std::move(fileImpls.begin(), fileImpls.end(), std::back_inserter(impls));
    }
    return impls;
}

std::vector<Ptr<AST::ClassDecl>> GetJavaImplRegistryCompanions(AST::File& file)
{
    std::vector<Ptr<ClassDecl>> companions;
    for (auto& decl : file.decls) {
        if (IsImplRegistryCompanion(*decl)) {
            auto companion = StaticAs<ASTKind::CLASS_DECL>(decl.get());
            companions.emplace_back(companion);
        }
    }
    return companions;
}

std::vector<Ptr<AST::ClassDecl>> GetJavaImplRegistryCompanions(AST::Package& pkg)
{
    std::vector<Ptr<ClassDecl>> impls;
    for (auto& file : pkg.files) {
        auto fileImpls = GetJavaImplRegistryCompanions(*file);
        std::move(fileImpls.begin(), fileImpls.end(), std::back_inserter(impls));
    }
    return impls;
}

constexpr std::string_view JAVA_IMPL_REGISTRY_COMPANION_REFERENCE_FIELD_NAME_IN_REFERENCE_WRAPPER = "$reg";

bool IsJavaExportRegistryCompanionReferenceField(const AST::Node& node)
{
    if (node.astKind != ASTKind::VAR_DECL) {
        return false;
    }
    auto field = dynamic_cast<const VarDecl*>(&node);
    if (!field) {
        return false;
    }

    if (!field->outerDecl || !IsImplReferenceWrapper(*field->outerDecl)) {
        return false;
    }

    return field->identifier == JAVA_IMPL_REGISTRY_COMPANION_REFERENCE_FIELD_NAME_IN_REFERENCE_WRAPPER;
}

bool IsUserDefinedJavaImplConstructor(const AST::Decl& implMember)
{
    if (!implMember.TestAttr(Attribute::CONSTRUCTOR)) {
        return false;
    }

    // Skip original primary constructors and only consider its FuncDecl counterpart.
    auto ctor = DynamicCast<const FuncDecl*>(&implMember);
    if (!ctor) {
        return false;
    }
    auto hasParams = !ctor->funcBody->paramLists.empty() && !ctor->funcBody->paramLists[0]->params.empty();

    // Primary constructor is desugared as fresh FuncDecl with COMPILER_ADD attribute.
    auto isPrimary = implMember.TestAttr(Attribute::PRIMARY_CONSTRUCTOR, Attribute::COMPILER_ADD);
    // Visit implicit constructors too (compiler added without any parameters).
    auto isImplicit = implMember.TestAttr(Attribute::COMPILER_ADD, Attribute::IMPLICIT_ADD) && !hasParams;
    if (!isPrimary && !isImplicit && implMember.TestAttr(Attribute::COMPILER_ADD)) {
        return false;
    }
    return true;
}

bool IsJavaRefGetter(const Decl& fd)
{
    return fd.astKind == ASTKind::FUNC_DECL && fd.TestAttr(Attribute::COMPILER_ADD) &&
        fd.identifier.Val() == JAVA_REF_GETTER_FUNC_NAME;
}

Ptr<FuncDecl> GetJavaRefGetter(ClassLikeDecl& mirrorLike)
{
    CJC_ASSERT(mirrorLike.IsJavaForeign() || mirrorLike.IsJavaExport());
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

namespace {

/**
 * Returns fully-qualified name of target decl in extend
 */
std::string GetJavaFQNameFromExtendDecl(const ExtendDecl& extendDecl)
{
    auto rt = DynamicCast<const RefType *>(extendDecl.extendedType.get());
    CJC_ASSERT(rt);
    return GetJavaFQName(*rt->ref.target);
}

Ptr<std::string> GetJavaAnnotationForeignName(const Decl& decl)
{
    for (auto& anno : decl.annotations) {
        switch (anno->kind) {
            case AnnotationKind::JAVA_MIRROR:
            case AnnotationKind::JAVA_IMPL:
            case AnnotationKind::JAVA:
                break; // extract name out of 'switch'
            default: continue; // look for next annotation
        }
        if (anno->TestAttr(Attribute::IS_BROKEN)) {
            return nullptr;
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

std::string GetFQNameJoinBy(const std::string& name, std::string_view separator)
{
    auto parts = GetFQNameParts(name);
    return StringJoin(parts.begin(), parts.end(), separator);
}


std::string WrapClassNameAsTypeSignature(const std::string_view classType)
{
    return std::string{"L"} + std::string{classType} + ";";
}

} // namespace

std::string GetJniTypeSignature(const AST::Ty& javaCompatibleTy)
{
    constexpr static std::string_view javaStringJniName = "java/lang/String";
    static std::map<TypeKind, std::string> primitiveTypeMapper = {
        {TypeKind::TYPE_UNIT, "V"},
        {TypeKind::TYPE_BOOLEAN, "Z"},
        {TypeKind::TYPE_INT8, "B"},
        {TypeKind::TYPE_UINT16, "C"},
        {TypeKind::TYPE_INT16, "S"},
        {TypeKind::TYPE_INT32, "I"},
        {TypeKind::TYPE_INT64, "J"},
        {TypeKind::TYPE_FLOAT32, "F"},
        {TypeKind::TYPE_FLOAT64, "D"},
    };

    if (javaCompatibleTy.IsPrimitive()) {
        return primitiveTypeMapper[javaCompatibleTy.kind];
    }

    if (javaCompatibleTy.IsString()) {
        return WrapClassNameAsTypeSignature(javaStringJniName);
    }

    if (javaCompatibleTy.IsCoreOptionType()) {
        CJC_ASSERT_WITH_MSG(!javaCompatibleTy.typeArgs.empty(), "Option type must be generic");
        auto& argTy = *javaCompatibleTy.typeArgs[0];
        return GetJniTypeSignature(argTy);
    }

    if (javaCompatibleTy.IsClassLike()) {
        if (IsJArray(*StaticCast<ClassLikeTy&>(javaCompatibleTy).commonDecl)) {
            return "[" + GetJniTypeSignature(*javaCompatibleTy.typeArgs[0]);
        } else {
            return WrapClassNameAsTypeSignature(
                NormalizeJavaSignature(GetJavaFQName(*StaticCast<ClassLikeTy&>(javaCompatibleTy).commonDecl)));
        }
    }

    if (javaCompatibleTy.IsFunc()) {
        auto& funcTy = StaticCast<FuncTy&>(javaCompatibleTy);
        return GetJniTypeSignature(*funcTy.retTy, funcTy.paramTys);
    }

    CJC_ABORT(); // Unreachable state. This function should be called only on java-compatible types.
    return "";
}

std::string GetJniTypeSignature(
    const AST::Ty& javaCompatibleRetTy, const std::vector<ModalTy>& paramTys, bool withMarker)
{
    std::string signature = "(";
    for (auto paramTy : paramTys) {
        signature.append(GetJniTypeSignature(*paramTy));
    }
    if (withMarker) {
        // Last parameter is marker parameter
        signature.append(
            WrapClassNameAsTypeSignature(JavaClassSignature::GetConstructorMarkerClass().GetJniClassName()));
    }
    signature.append(")");
    signature.append(GetJniTypeSignature(javaCompatibleRetTy));
    return signature;
}

std::string GetJavaFQName(const Decl& decl)
{
    if (auto extendDecl = DynamicCast<const ExtendDecl*>(&decl)) {
        return GetJavaFQNameFromExtendDecl(*extendDecl);
    }
    if (auto classlikeDecl = DynamicCast<const ClassLikeDecl*>(&decl)) {
        auto attr = GetJavaAnnotationForeignName(*classlikeDecl);
        if (attr) {
            return GetFQNameJoinBy(*attr, "$");
        }
    }
    return decl.GetFullPackageName() + "." + decl.identifier;
}

std::string GetJavaFQSourceCodeName(const ClassLikeDecl& decl)
{
    auto attr = GetJavaAnnotationForeignName(decl);
    return attr ? GetFQNameJoinBy(*attr, ".") : (decl.GetFullPackageName() + "." + decl.identifier);
}

std::string GetJavaTopLevelClassName(const ClassLikeDecl& decl)
{
    auto attr = GetJavaAnnotationForeignName(decl);
    if (!attr) {
        return decl.identifier;
    }

    auto leftmost = GetFQNameParts(*attr).front();
    auto topLevelTypeIndex = leftmost.find_last_of('.');
    if (topLevelTypeIndex == std::string::npos) {
        return leftmost;
    }
    return leftmost.substr(topLevelTypeIndex + 1);
}

std::string GetJavaUnqualifiedTypeName(const ClassLikeDecl& decl, const std::string_view nestedSeparator)
{
    auto attr = GetJavaAnnotationForeignName(decl);
    if (!attr) {
        return decl.identifier;
    }

    auto nameParts = GetFQNameParts(*attr);
    if (!nameParts.empty()) {
        auto typeit = nameParts[0].rfind(".");
        if (typeit != std::string::npos) {
            // Omit package name
            nameParts[0] = nameParts[0].substr(typeit + 1);
        }
    }
    // iterator pointing to outermost type (omitting package).
    return StringJoin(nameParts.begin(), nameParts.end(), nestedSeparator);
}

std::string GetJavaPackage(const Decl& decl)
{
    if (IsJArray(decl)) {
        return decl.fullPackageName; // no package info is encoded in annotation argument
    }

    auto pfqname = GetJavaAnnotationForeignName(decl);
    if (!pfqname) {
        return decl.GetFullPackageName();
    }
    auto fqname = *pfqname;

    auto beforeClassNamePos = fqname.rfind(".");
    if (beforeClassNamePos != std::string::npos) {
        fqname.erase(beforeClassNamePos);
        return fqname;
    }
    
    return "";
}

std::string GetJavaMemberName(const Decl& member)
{
    auto foreignNameAnno = GetForeignNameAnnotation(member);
    if (!foreignNameAnno) {
        return member.identifier;
    }
    CJC_ASSERT(foreignNameAnno->args.size() == 1);
    auto litExpr = DynamicCast<LitConstExpr>(foreignNameAnno->args[0]->expr.get());
    CJC_ASSERT(litExpr);
    return litExpr->stringValue;

    return member.identifier;
}

std::string NormalizeJavaSignature(const std::string& sig)
{
    std::string normalized = sig;
    std::replace(normalized.begin(), normalized.end(), '.', '/');
    return normalized;
}

void InjectNodes(std::vector<OwnedPtr<Node>>& into, OwnedPtr<Node> from)
{
    if (auto block = As<ASTKind::BLOCK>(from)) {
        for (auto& node : block->body) {
            into.push_back(std::move(node));
        }
        return;
    }
    into.push_back(std::move(from));
}


} // namespace Cangjie::Native::FFI::Java
