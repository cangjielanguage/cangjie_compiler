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
#ifndef CANGJIE_SEMA_NATIVE_FFI_JAVA_UTILS
#define CANGJIE_SEMA_NATIVE_FFI_JAVA_UTILS

#include "cangjie/AST/Node.h"
#include "cangjie/Modules/ImportManager.h"

namespace Cangjie::Native::FFI::Java {
    
enum class ArrayOperationKind : uint8_t { CREATE, GET, SET, GET_LENGTH };
    
constexpr std::string_view JAVA_ARRAY_GET_FOR_REF_TYPES = "$javaarrayget";
constexpr std::string_view JAVA_ARRAY_SET_FOR_REF_TYPES = "$javaarrayset";

ArrayOperationKind GetArrayOperationKind(AST::Decl& decl);

const Ptr<AST::ClassDecl> GetSyntheticClass(const ImportManager& importManager, const AST::ClassLikeDecl& cld);

std::string GetMirrorReferenceWrapperNameFromClassLike(const AST::ClassLikeDecl &mirror);

std::string GetImplRegistryCompanionClassName(const AST::ClassLikeDecl& javaImplDecl);

std::vector<Ptr<AST::ClassLikeDecl>> GetJavaMirrors(AST::File& file);

std::vector<Ptr<AST::ClassLikeDecl>> GetJavaMirrors(AST::Package& pkg);

std::vector<Ptr<AST::ClassDecl>> GetJavaImpls(AST::File& file);

std::vector<Ptr<AST::ClassDecl>> GetJavaImpls(AST::Package& pkg);

std::vector<Ptr<AST::ClassDecl>> GetJavaImplRegistryCompanions(AST::File& file);

std::vector<Ptr<AST::ClassDecl>> GetJavaImplRegistryCompanions(AST::Package& pkg);

bool IsJavaExportRegistryCompanionReferenceField(const AST::Node& node);

bool IsUserDefinedJavaImplConstructor(const AST::Decl& implMember);

/**
 * for interfaces and abstract classes
 */
Ptr<AST::FuncDecl> GetJavaRefGetter(AST::ClassLikeDecl& mirrorLike);

bool IsJavaRefGetter(const AST::Decl& fd);

/**
 * Returns JNI signature of `javaCompatibleTy` type.
 * An example of signatures:
 * - `B` for `Int8`
 * - `Ljava/lang/Object` for `java.lang.JObject`
 * - `[[Ljava/lang/String` for `java.lang.JArray<java.lang.JString>`
 * - `(Ljava/lang/String;I)Ljava/lang/Object;` for function (java.lang.JString, Int32) -> java.lang.JObject
 * NOTE: fully-qualified names are wrapped with 'L'-prefix and ';'-suffix.
 */
std::string GetJniTypeSignature(const AST::Ty& javaCompatibleTy);
std::string GetJniTypeSignature(
    const AST::Ty& javaCompatibleRetTy, const std::vector<AST::ModalTy>& paramTys, bool withMarker = false);

/**
 * Returns fully-qualified name of the decl or fq-name specified in @JavaMirror/@JavaImpl as attribute,
 * which is used as target class name for JNI calls
 */
std::string GetJavaFQName(const AST::Decl& decl);

/**
 * Returns fully-qualified name of the decl or fq-name specified in @JavaMirror as attribute,
 * which is suitable for using Java source code:
 * - For specifying nested class '.' is used
 */
std::string GetJavaFQSourceCodeName(const AST::ClassLikeDecl& decl);

std::string GetJavaTopLevelClassName(const AST::ClassLikeDecl& decl);

std::string GetJavaUnqualifiedTypeName(const AST::ClassLikeDecl& decl,
    const std::string_view nestedSeparator = "$");

/**
 * Returns package of the decl or package specified in @JavaMirror/@JavaImpl as parameter (omitting class name)
 */
std::string GetJavaPackage(const AST::Decl& decl);

/**
 * Returns name of corresponding Java method or field with respect to @ForeignName annotation
 */
std::string GetJavaMemberName(const AST::Decl& member);

std::string NormalizeJavaSignature(const std::string& sig);

void InjectNodes(std::vector<OwnedPtr<AST::Node>>& into, OwnedPtr<AST::Node> from);

} // namespace Cangjie::Native::FFI::Java

#endif // CANGJIE_SEMA_NATIVE_FFI_JAVA_UTILS
