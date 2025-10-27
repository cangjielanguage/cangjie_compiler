// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares core support for java mirror and mirror subtype
 */
#ifndef CANGJIE_SEMA_NATIVE_FFI_JAVA_DESUGAR_INTEROP_MANAGER
#define CANGJIE_SEMA_NATIVE_FFI_JAVA_DESUGAR_INTEROP_MANAGER

#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/Modules/ImportManager.h"
#include "cangjie/Sema/TypeManager.h"

namespace Cangjie::Interop::Java {
using namespace AST;

class JavaInteropManager {
public:
    JavaInteropManager(ImportManager& importManager, TypeManager& typeManager, DiagnosticEngine& diag,
        const BaseMangler& mangler, const std::optional<std::string>& javagenOutputPath, const std::string outputPath,
        GlobalOptions::InteropLanguage& targetInteropLanguage)
        : importManager(importManager),
          typeManager(typeManager),
          diag(diag),
          mangler(mangler),
          javagenOutputPath(javagenOutputPath),
          outputPath(outputPath),
          targetInteropLanguage(targetInteropLanguage)
    {
    }

    void CheckImplRedefinition(Package& package);
    void CheckInheritance(ClassLikeDecl& decl) const;
    void CheckTypes(File& file);
    void CheckTypes(ClassLikeDecl& classLikeDecl);
    void CheckJavaMirrorTypes(ClassLikeDecl& decl);
    void CheckJavaImplTypes(ClassLikeDecl& decl);
    void CheckCJMappingType(Decl& decl);
    void CheckCJMappingDeclSupportRange(Decl& decl);
    void DesugarPackage(Package& pkg);

private:
    void CheckUsageOfJavaTypes(Decl& decl);

private:
    void CheckNonJavaSuperType(ClassLikeDecl& decl) const;
    void CheckJavaMirrorSubtypeAttrClassLikeDecl(ClassLikeDecl& decl) const;
    void CheckExtendDecl(ExtendDecl& decl) const;
    void CheckGenericsInstantiation(Decl& file);

    ImportManager& importManager;
    TypeManager& typeManager;
    DiagnosticEngine& diag;
    const BaseMangler& mangler;
    const std::optional<std::string>& javagenOutputPath;
    /**
     * Name of output cangjie library
     */
    const std::string outputPath;
    /**
     * Flag that informs on presence of any @JavaMirror- or @JavaImpl-annotated entities in the compilation package
     */
    bool hasMirrorOrImpl = false;
    GlobalOptions::InteropLanguage& targetInteropLanguage;
};
} // namespace Cangjie::Interop::Java

#endif // CANGJIE_SEMA_NATIVE_FFI_JAVA_DESUGAR_INTEROP_MANAGER
