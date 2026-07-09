// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/*
 * @file
 *
 * This file declares the NodeWriter, which serializes AST for LibAST.
 */

#ifndef CANGJIE_MODULES_NODESERIALIZATION_H
#define CANGJIE_MODULES_NODESERIALIZATION_H

#include <cstdint>
#include <string>
#include <vector>

#include "flatbuffers/StdAstFormat_generated.h"

#include "cangjie/AST/Node.h"
#include "cangjie/Basic/DiagnosticEngine.h"

using AstNode = Ptr<const Cangjie::AST::Node>;
using AstExpr = Ptr<const Cangjie::AST::Expr>;
using AstCallExpr = Ptr<const Cangjie::AST::CallExpr>;
using AstLambdaExpr = Ptr<const Cangjie::AST::LambdaExpr>;
using AstType = Ptr<const Cangjie::AST::Type>;
using AstRefType = Ptr<const Cangjie::AST::RefType>;
using AstRefExpr = Ptr<const Cangjie::AST::RefExpr>;
using AstPrimitiveType = Ptr<const Cangjie::AST::PrimitiveType>;
using AstAnnotation = Ptr<const Cangjie::AST::Annotation>;
using AstModifier = Ptr<const Cangjie::AST::Modifier>;
using AstDecl = Ptr<const Cangjie::AST::Decl>;
using AstVarDecl = Ptr<const Cangjie::AST::VarDecl>;
using AstMainDecl = Ptr<const Cangjie::AST::MainDecl>;
using AstFuncDecl = Ptr<const Cangjie::AST::FuncDecl>;
using AstMacroDecl = Ptr<const Cangjie::AST::MacroDecl>;
using AstMacroExpandDecl = Ptr<const Cangjie::AST::MacroExpandDecl>;
using AstFuncArg = Ptr<const Cangjie::AST::FuncArg>;
using AstFuncBody = Ptr<const Cangjie::AST::FuncBody>;
using AstFuncParam = Ptr<const Cangjie::AST::FuncParam>;
using AstMacroExpandParam = Ptr<const Cangjie::AST::MacroExpandParam>;
using AstBlock = Ptr<const Cangjie::AST::Block>;
using AstStructBody = Ptr<const Cangjie::AST::StructBody>;
using AstInterfaceBody = Ptr<const Cangjie::AST::InterfaceBody>;
using AstClassBody = Ptr<const Cangjie::AST::ClassBody>;
using AstGeneric = Ptr<const Cangjie::AST::Generic>;
using AstGenericParamDecl = Ptr<const Cangjie::AST::GenericParamDecl>;
using AstGenericConstraint = Ptr<const Cangjie::AST::GenericConstraint>;
using AstPattern = Ptr<const Cangjie::AST::Pattern>;
using AstEnumPattern = Ptr<const Cangjie::AST::EnumPattern>;
using AstMatchCase = Ptr<const Cangjie::AST::MatchCase>;
using AstMatchCaseOther = Ptr<const Cangjie::AST::MatchCaseOther>;
using AstFile = Ptr<const Cangjie::AST::File>;
using AstFeaturesDirective = Ptr<const Cangjie::AST::FeaturesDirective>;
using AstPackageSpec = Ptr<const Cangjie::AST::PackageSpec>;
using AstImportSpec = Ptr<const Cangjie::AST::ImportSpec>;

using ASTFormatDecl = flatbuffers::Offset<ASTFormat::Decl>;
using ASTFormatExpr = flatbuffers::Offset<ASTFormat::Expr>;
using ASTFormatType = flatbuffers::Offset<ASTFormat::Type>;
using ASTFormatPattern = flatbuffers::Offset<ASTFormat::Pattern>;

namespace NodeSerialization {
const size_t INITIAL_FILE_SIZE = 65536;

using namespace Cangjie;
class NodeWriter {
public:
    NodeWriter(Ptr<AST::Node> nodePtr) : nodePtr(nodePtr), builder(INITIAL_FILE_SIZE)
    {
    }
    ~NodeWriter()
    {
    }
    uint8_t* ExportNode(); // uint8_t* -> unsafePtr in CangJie
private:
    std::vector<uint8_t> bufferData;
    Ptr<AST::Node> nodePtr = nullptr; // nodePtr is the AST node to be serialized
    flatbuffers::Offset<ASTFormat::DeclBase> emptyDeclBase = flatbuffers::Offset<ASTFormat::DeclBase>();
    flatbuffers::Offset<ASTFormat::NodeBase> emptyNodeBase = flatbuffers::Offset<ASTFormat::NodeBase>();
    flatbuffers::Offset<ASTFormat::TypeBase> emptyTypeBase = flatbuffers::Offset<ASTFormat::TypeBase>();
    template <typename K, typename U, typename V>
    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<K>>> FlatVectorCreateHelper(
        const std::vector<OwnedPtr<U>>& input, flatbuffers::Offset<K> (NodeWriter::*funcPtr)(V));
    ASTFormat::Position FlatPosCreateHelper(const Position& pos) const;
    flatbuffers::Offset<flatbuffers::Vector<const ASTFormat::Position*>> CreatePositionVector(
        const std::vector<Cangjie::Position>& positions);
    flatbuffers::FlatBufferBuilder builder; // FlatBufferBuilder which contains the buffer it grows.
    flatbuffers::Offset<ASTFormat::MacroInvocation> MacroInvocationCreateHelper(
        const AST::MacroInvocation& macroInvocation);
    std::vector<flatbuffers::Offset<ASTFormat::Token>> TokensVectorCreateHelper(
        std::vector<Cangjie::Token> tokenVector);
    flatbuffers::Offset<ASTFormat::NodeBase> SerializeNodeBase(AstNode node);

    flatbuffers::Offset<ASTFormat::Pattern> SerializePattern(AstPattern pattern);
    flatbuffers::Offset<ASTFormat::Pattern> SerializeConstPattern(AstPattern pattern);
    flatbuffers::Offset<ASTFormat::Pattern> SerializeWildcardPattern(AstPattern pattern);
    flatbuffers::Offset<ASTFormat::Pattern> SerializeVarPattern(AstPattern pattern);
    flatbuffers::Offset<ASTFormat::Pattern> SerializeExceptTypePattern(AstPattern pattern);
    flatbuffers::Offset<ASTFormat::Pattern> SerializeCommandTypePattern(AstPattern pattern);
    flatbuffers::Offset<ASTFormat::Pattern> SerializeResumptionTypePattern(AstPattern pattern);
    flatbuffers::Offset<ASTFormat::Pattern> SerializeTypePattern(AstPattern pattern);
    flatbuffers::Offset<ASTFormat::Pattern> SerializeEnumPattern(const AST::Pattern* pattern);
    flatbuffers::Offset<ASTFormat::Pattern> SerializeMultiEnumPattern(AstPattern pattern);
    flatbuffers::Offset<ASTFormat::Pattern> SerializeVarOrEnumPattern(AstPattern pattern);
    flatbuffers::Offset<ASTFormat::Pattern> SerializeTuplePattern(AstPattern pattern);
    flatbuffers::Offset<ASTFormat::EnumPattern> SerializeEnumPattern(const AST::EnumPattern* enumPattern);
    flatbuffers::Offset<ASTFormat::MatchCase> SerializeMatchCase(AstMatchCase matchcase);
    flatbuffers::Offset<ASTFormat::MatchCaseOther> SerializeMatchCaseOther(AstMatchCaseOther matchcaseother);

    flatbuffers::Offset<ASTFormat::Expr> SerializeExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeWildcardExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeBinaryExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeIsExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeAsExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeLitConstExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeUnaryExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeParenExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeCallExpr(const AST::Expr* expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeRefExpr(const AST::Expr* expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeReturnExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeAssignExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeMemberAccess(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeLambdaExpr(const AST::Expr* expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeTrailingClosureExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeTypeConvExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeTryExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeMatchExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeTokenPart(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeQuoteExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeThrowExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializePerformExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeResumeExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeForInExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeIfExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeLetPatternDestructor(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeBlockExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeWhileExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeDoWhileExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeJumpExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeIncOrDecExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializePrimitiveTypeExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeSpawnExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeSynchronizedExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeArrayLit(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeTupleLit(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeSubscriptExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeRangeExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::CallExpr> SerializeCallExpr(const AST::CallExpr* callExpr);
    flatbuffers::Offset<ASTFormat::LambdaExpr> SerializeLambdaExpr(const AST::LambdaExpr* lambdaExpr);
    flatbuffers::Offset<ASTFormat::Block> SerializeBlock(AstBlock block);
    flatbuffers::Offset<ASTFormat::FuncArg> SerializeFuncArg(AstFuncArg funcArg);
    flatbuffers::Offset<ASTFormat::RefExpr> SerializeRefExpr(const AST::RefExpr* refExpr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeOptionalExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeOptionalChainExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeMacroExpandExpr(AstExpr expr);
    flatbuffers::Offset<ASTFormat::Expr> SerializeArrayExpr(AstExpr expr);

    flatbuffers::Offset<ASTFormat::TypeBase> SerializeTypeBase(AstType type);
    flatbuffers::Offset<ASTFormat::Type> SerializeType(AstType type);
    flatbuffers::Offset<ASTFormat::Type> SerializeRefType(const AST::Type* type);
    flatbuffers::Offset<ASTFormat::Type> SerializePrimitiveType(const AST::Type* type);
    flatbuffers::Offset<ASTFormat::Type> SerializeArrayType(AstType type);
    flatbuffers::Offset<ASTFormat::Type> SerializeFuncType(AstType type);
    flatbuffers::Offset<ASTFormat::Type> SerializeThisType(AstType type);
    flatbuffers::Offset<ASTFormat::Type> SerializeParenType(AstType type);
    flatbuffers::Offset<ASTFormat::Type> SerializeQualifiedType(AstType type);
    flatbuffers::Offset<ASTFormat::Type> SerializeOptionType(AstType type);
    flatbuffers::Offset<ASTFormat::Type> SerializeTupleType(AstType type);
    flatbuffers::Offset<ASTFormat::RefType> SerializeRefType(const AST::RefType* refType);
    flatbuffers::Offset<ASTFormat::PrimitiveType> SerializePrimitiveType(const AST::PrimitiveType* primitiveType);
    flatbuffers::Offset<ASTFormat::Type> SerializeVArrayType(AstType type);
    flatbuffers::Offset<ASTFormat::Type> SerializeConstantType(AstType type);

    flatbuffers::Offset<ASTFormat::Annotation> SerializeAnnotation(AstAnnotation annotation);
    flatbuffers::Offset<ASTFormat::Modifier> SerializeModifier(AstModifier modifier);
    flatbuffers::Offset<ASTFormat::DeclBase> SerializeDeclBase(AstDecl decl);
    flatbuffers::Offset<ASTFormat::Decl> SerializeDecl(AstDecl decl);
    flatbuffers::Offset<ASTFormat::Decl> SerializeVarWithPatternDecl(AstDecl decl);
    flatbuffers::Offset<ASTFormat::Decl> SerializeVarDecl(const AST::Decl* decl);
    flatbuffers::Offset<ASTFormat::Decl> SerializeMainDecl(const AST::Decl* decl);
    flatbuffers::Offset<ASTFormat::Decl> SerializeFuncDecl(const AST::Decl* decl);
    flatbuffers::Offset<ASTFormat::Decl> SerializeMacroDecl(const AST::Decl* decl);
    flatbuffers::Offset<ASTFormat::Decl> SerializeMacroExpandDecl(const AST::Decl* decl);
    flatbuffers::Offset<ASTFormat::Decl> SerializeStructDecl(AstDecl decl);
    flatbuffers::Offset<ASTFormat::Decl> SerializePropDecl(AstDecl decl);
    flatbuffers::Offset<ASTFormat::Decl> SerializeTypeAliasDecl(AstDecl decl);
    flatbuffers::Offset<ASTFormat::Decl> SerializeExtendDecl(AstDecl decl);
    flatbuffers::Offset<ASTFormat::Decl> SerializeClassDecl(AstDecl decl);
    flatbuffers::Offset<ASTFormat::Decl> SerializeInterfaceDecl(AstDecl decl);
    flatbuffers::Offset<ASTFormat::Decl> SerializeEnumDecl(AstDecl decl);
    flatbuffers::Offset<ASTFormat::Decl> SerializePrimaryCtorDecl(AstDecl decl);
    flatbuffers::Offset<ASTFormat::VarDecl> SerializeVarDecl(const AST::VarDecl* varDecl);
    flatbuffers::Offset<ASTFormat::MainDecl> SerializeMainDecl(const AST::MainDecl* mainDecl);
    flatbuffers::Offset<ASTFormat::FuncDecl> SerializeFuncDecl(const AST::FuncDecl* funcDecl);
    flatbuffers::Offset<ASTFormat::MacroDecl> SerializeMacroDecl(const AST::MacroDecl* macroDecl);
    flatbuffers::Offset<ASTFormat::MacroExpandDecl> SerializeMacroExpandDecl(
        const AST::MacroExpandDecl* macroExpandDecl);
    flatbuffers::Offset<ASTFormat::Decl> SerializeDeclOfFuncParam(const AST::Decl* decl);
    flatbuffers::Offset<ASTFormat::Decl> SerializeDeclOfMacroExpandParam(const AST::Decl* decl);
    flatbuffers::Offset<ASTFormat::FuncBody> SerializeFuncBody(AstFuncBody funcBody);
    flatbuffers::Offset<ASTFormat::FuncParam> SerializeFuncParam(AstFuncParam funcParam);
    flatbuffers::Offset<ASTFormat::FuncParam> SerializeMacroExpandParam(AstMacroExpandParam mep);
    flatbuffers::Offset<ASTFormat::StructBody> SerializeStructBody(AstStructBody structBody);
    flatbuffers::Offset<ASTFormat::InterfaceBody> SerializeInterfaceBody(AstInterfaceBody interfaceBody);
    flatbuffers::Offset<ASTFormat::ClassBody> SerializeClassBody(AstClassBody classBody);
    flatbuffers::Offset<ASTFormat::Generic> SerializeGeneric(AstGeneric generic);
    flatbuffers::Offset<ASTFormat::GenericParamDecl> SerializeGenericParamDecl(AstGenericParamDecl genericParamDecl);
    flatbuffers::Offset<ASTFormat::GenericConstraint> SerializeGenericConstraint(
        AstGenericConstraint genericConstraint);

    flatbuffers::Offset<ASTFormat::File> SerializeFile(AstFile file);
    flatbuffers::Offset<ASTFormat::ImportSpec> SerializeImportSpec(AstImportSpec importSpec);
    flatbuffers::Offset<ASTFormat::ImportContent> SerializeImportContent(const AST::ImportContent& content);
    flatbuffers::Offset<ASTFormat::PackageSpec> SerializePackageSpec(AstPackageSpec packageSpec);
    flatbuffers::Offset<ASTFormat::FeaturesDirective> SerializeFeaturesDirective(AstFeaturesDirective featureDirective);
    flatbuffers::Offset<ASTFormat::FeaturesSet> SerializeFeaturesSet(const AST::FeaturesSet& fSet);
    flatbuffers::Offset<ASTFormat::FeatureId> SerializeFeatureId(const AST::FeatureId& featureId);
};

template <typename K, typename U, typename V>
flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<K>>> NodeWriter::FlatVectorCreateHelper(
    const std::vector<OwnedPtr<U>>& input, flatbuffers::Offset<K> (NodeWriter::*funcPtr)(V))
{
    std::vector<flatbuffers::Offset<K>> vecK;
    for (auto& ele : input) {
        auto fbK = std::invoke(funcPtr, this, ele.get());
        vecK.push_back(fbK);
    }
    return builder.CreateVector(vecK);
}

} // namespace NodeSerialization
#endif // CANGJIE_MODULES_NODESERIALIZATION_H
