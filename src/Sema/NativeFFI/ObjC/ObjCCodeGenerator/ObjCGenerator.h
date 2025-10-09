// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares class for Objective-C code generation.
 */

#ifndef CANGJIE_SEMA_OBJC_GENERATOR_H
#define CANGJIE_SEMA_OBJC_GENERATOR_H

#include <fstream>
#include <string_view>
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Types.h"
#include "NativeFFI/ObjC/AfterTypeCheck/Interop/Context.h"

namespace Cangjie::Interop::ObjC {
using namespace AST;

enum class ObjCFunctionType { STATIC, INSTANCE };
enum class GenerationTarget { HEADER, SOURCE, BOTH };
enum class FunctionListFormat { DECLARATION, STATIC_REF, CANGJIE_DECL };
enum class OptionalBlockOp { OPEN, CLOSE, NONE };
using ArgsList = std::vector<std::pair<std::string, std::string>>;

class ObjCGenerator {
public:
    ObjCGenerator(InteropContext& ctx, Ptr<ClassDecl> decl, const std::string& outputFilePath,
        const std::string& cjLibOutputPath);
    void Generate();

private:
    std::string res;
    std::string resSource;
    const std::string& outputFilePath;
    const std::string& cjLibOutputPath;
    size_t currentBlockIndent = 0;
    Ptr<AST::ClassDecl> classDecl;
    InteropContext& ctx;

    void OpenBlock();
    void CloseBlock(bool newLineBefore, bool newLineAfter);
    void AddWithIndent(const std::string& s, const GenerationTarget target = GenerationTarget::HEADER,
        const OptionalBlockOp bOp = OptionalBlockOp::NONE);

    std::string GenerateReturn(const std::string& statement) const;
    std::string GenerateAssignment(const std::string& lhs, const std::string& rhs) const;
    std::string GenerateIfStatement(const std::string& lhs, const std::string& rhs, const std::string& op) const;
    std::string GenerateObjCCall(const std::string& lhs, const std::string& rhs) const;
    std::string GenerateCCall(
        const std::string& funcName, const std::vector<std::string> args = std::vector<std::string>()) const;
    std::string GenerateDefaultFunctionImplementation(const std::string& name, const Ty& retTy,
        const ArgsList args = ArgsList(), const ObjCFunctionType = ObjCFunctionType::INSTANCE) const;
    std::string GenerateFunctionDeclaration(
        const ObjCFunctionType type, const std::string& returnType, const std::string& name) const;
    std::string GeneratePropertyDeclaration(const ObjCFunctionType staticType, const std::string& mode,
        const std::string& type, const std::string& name) const;
    std::string GenerateImport(const std::string& name);
    std::string GenerateStaticFunctionReference(
        const std::string& funcName, const std::string& retType, const std::string& argTypes) const;
    std::string GenerateStaticReference(const std::string& name, const std::string& type,
        const std::string& defaultValue) const;
    std::string GenerateFuncParamLists(const std::vector<OwnedPtr<AST::FuncParamList>>& paramLists,
        FunctionListFormat format = FunctionListFormat::DECLARATION,
        const ObjCFunctionType type = ObjCFunctionType::INSTANCE);
    std::string MapCJTypeToObjCType(const OwnedPtr<AST::Type>& type);
    std::string MapCJTypeToObjCType(const OwnedPtr<AST::FuncParam>& param);

    ArgsList ConvertParamsListToArgsList(
        const std::vector<OwnedPtr<AST::FuncParamList>>& paramLists, bool withRegistryId);
    std::vector<std::string> ConvertParamsListToCallableParamsString(
        std::vector<OwnedPtr<FuncParamList>>& paramLists, bool withSelf) const;
    std::string GenerateSetterParamLists(const std::string& type) const;

    void GenerateStaticFunctionsReferences();
    void GenerateFunctionSymbolsInitialization();
    void GenerateFunctionSymInit(const std::string& fName);
    void GenerateInterfaceDecl();
    void AddProperties();
    void AddConstructors();
    void AddMethods();
    void WriteToFile();
};
} // namespace Cangjie::Interop::ObjC

#endif // CANGJIE_SEMA_OBJC_GENERATOR_H
