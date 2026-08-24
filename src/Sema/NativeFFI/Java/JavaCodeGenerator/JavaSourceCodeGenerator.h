// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares class for java code generation.
 */
#ifndef CANGJIE_SEMA_JAVA_CODE_GENERATOR
#define CANGJIE_SEMA_JAVA_CODE_GENERATOR

#include <set>

#include "AbstractSourceCodeGenerator.h"
#include "NativeFFI/Java/AfterTypeCheck/AfterTypeCheckContext.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Types.h"
#include "cangjie/Mangle/BaseMangler.h"

namespace Cangjie::Interop::Java {
using namespace AST;
using namespace Cangjie::Native::FFI;
using namespace Cangjie::Native::FFI::Java;

class JavaSourceCodeGenerator : public AbstractSourceCodeGenerator {
public:
    JavaSourceCodeGenerator(Decl* decl, const BaseMangler& mangler,
        AfterTypeCheckContext& ctx, const std::optional<std::string>& folderPath, const std::string& outputFileName,
        std::string cjLibName, std::vector<Ptr<ExtendDecl>> extends);

private:
    static const std::string DEFAULT_OUTPUT_DIR;
    static const std::string IGNORE_IMPORT;
    static std::string AddImport(Ptr<Ty> ty, std::set<std::string>* javaImports, const std::string* curPackageName);
    std::string MapCJTypeToJavaType(
        ModalTy ty, std::set<std::string>* javaImports, const std::string* curPackageName, bool isNativeMethod = false);
    std::string MapCJTypeToJavaType(const OwnedPtr<Type>& type, std::set<std::string>* javaImports,
        const std::string* curPackageName, bool isNativeMethod = false);
    std::string MapCJTypeToJavaType(const OwnedPtr<FuncParam>& param, std::set<std::string>* javaImports,
        const std::string* curPackageName, bool isNativeMethod = false);
    static std::string GenerateParams(const std::vector<OwnedPtr<FuncParam>>& params,
        const std::function<std::string(const OwnedPtr<FuncParam>& ptr)>& transform);
    static std::string GenerateParamLists(const std::vector<OwnedPtr<FuncParamList>>& paramLists,
        const std::function<std::string(const OwnedPtr<FuncParam>& ptr)>& transform);

    bool IsMemberExportable(const AST::Decl& member) const;

    Decl* decl;
    std::set<std::string> imports;
    const std::string cjLibName;
    const BaseMangler& mangler;
    AfterTypeCheckContext& ctx;
    std::vector<Ptr<ExtendDecl>> extendDecls;

    std::string GenerateFuncParams(const std::vector<OwnedPtr<FuncParam>>& params, bool isNativeMethod = false);
    std::string GenerateFuncParamLists(
        const std::vector<OwnedPtr<FuncParamList>>& paramLists, bool isNativeMethod = false);
    std::string GenerateFuncParamClasses(const std::vector<OwnedPtr<FuncParamList>>& paramLists);
    void ConstructResult() override;
    void AddClassDeclaration();
    void AddLoadLibrary();
    void AddRegistryIdIdField();
    void AddProperties();
    std::string GenerateConstructorDecl(const FuncDecl& func, bool isForCangjie);
    // Generate super call argument and native declaration.
    std::pair<std::string, std::string> GenNativeSuperArgCall(
        const FuncArg& arg, const FuncDecl& nativeFunc, const std::vector<OwnedPtr<FuncParam>>& params);
    // Generate super call and collection native func declaration.
    std::string GenerateSuperCall(
        const CallExpr& call,
        std::vector<Ptr<FuncDecl>> nativeFuncs,
        const std::vector<OwnedPtr<FuncParam>>& params, std::vector<std::string>& nativeArgs);
    std::string GenerateConstructorSuperCall(const FuncDecl& ctor, std::vector<std::string>& nativeArgs);
    void AddConstructor(const FuncDecl& ctor, const std::string& superCall, bool isForCangjie);
    // Generate constructors and native funcs.
    void AddConstructor(const FuncDecl& ctor);
    void AddConstructors();
    void AddInstanceMethod(const FuncDecl& funcDecl);
    void AddStaticMethod(const FuncDecl& funcDecl);
    void AddMethods();

    void AddEndClassParenthesis();
    void AddNativeInitCJObject(const std::vector<OwnedPtr<Cangjie::AST::FuncParam>>& params, const FuncDecl& ctor);
    void AddNativeDeleteCJObject();
    void AddFinalize();
    void AddHeader();
    void AddHeaderWithPackageName(std::string& curPackageName);
};
} // namespace Cangjie::Interop::Java

#endif // CANGJIE_SEMA_JAVA_CODE_GENERATOR
