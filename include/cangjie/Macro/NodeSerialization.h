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

#include "flatbuffers/NodeFormat_generated.h"

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

using NodeFormatDecl = flatbuffers::Offset<NodeFormat::Decl>;
using NodeFormatExpr = flatbuffers::Offset<NodeFormat::Expr>;
using NodeFormatType = flatbuffers::Offset<NodeFormat::Type>;
using NodeFormatPattern = flatbuffers::Offset<NodeFormat::Pattern>;

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
    flatbuffers::Offset<NodeFormat::DeclBase> emptyDeclBase = flatbuffers::Offset<NodeFormat::DeclBase>();
    flatbuffers::Offset<NodeFormat::NodeBase> emptyNodeBase = flatbuffers::Offset<NodeFormat::NodeBase>();
    flatbuffers::Offset<NodeFormat::TypeBase> emptyTypeBase = flatbuffers::Offset<NodeFormat::TypeBase>();
    template <typename K, typename U, typename V>
    flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<K>>> FlatVectorCreateHelper(
        const std::vector<OwnedPtr<U>>& input, flatbuffers::Offset<K> (NodeWriter::*funcPtr)(V));
    NodeFormat::Position FlatPosCreateHelper(const Position& pos) const;
    flatbuffers::Offset<flatbuffers::Vector<const NodeFormat::Position*>> CreatePositionVector(
        const std::vector<Cangjie::Position>& positions);
    flatbuffers::FlatBufferBuilder builder; // FlatBufferBuilder which contains the buffer it grows.
    flatbuffers::Offset<NodeFormat::MacroInvocation> MacroInvocationCreateHelper(
        const AST::MacroInvocation& macroInvocation);
    std::vector<flatbuffers::Offset<NodeFormat::Token>> TokensVectorCreateHelper(
        std::vector<Cangjie::Token> tokenVector);
    flatbuffers::Offset<NodeFormat::NodeBase> SerializeNodeBase(AstNode node);

    flatbuffers::Offset<NodeFormat::Pattern> SerializePattern(AstPattern pattern);
    flatbuffers::Offset<NodeFormat::Pattern> SerializeConstPattern(AstPattern pattern);
    flatbuffers::Offset<NodeFormat::Pattern> SerializeWildcardPattern(AstPattern pattern);
    flatbuffers::Offset<NodeFormat::Pattern> SerializeVarPattern(AstPattern pattern);
    flatbuffers::Offset<NodeFormat::Pattern> SerializeExceptTypePattern(AstPattern pattern);
    flatbuffers::Offset<NodeFormat::Pattern> SerializeCommandTypePattern(AstPattern pattern);
    flatbuffers::Offset<NodeFormat::Pattern> SerializeResumptionTypePattern(AstPattern pattern);
    flatbuffers::Offset<NodeFormat::Pattern> SerializeTypePattern(AstPattern pattern);
    flatbuffers::Offset<NodeFormat::Pattern> SerializeEnumPattern(const AST::Pattern* pattern);
    flatbuffers::Offset<NodeFormat::Pattern> SerializeMultiEnumPattern(AstPattern pattern);
    flatbuffers::Offset<NodeFormat::Pattern> SerializeVarOrEnumPattern(AstPattern pattern);
    flatbuffers::Offset<NodeFormat::Pattern> SerializeTuplePattern(AstPattern pattern);
    flatbuffers::Offset<NodeFormat::EnumPattern> SerializeEnumPattern(const AST::EnumPattern* enumPattern);
    flatbuffers::Offset<NodeFormat::MatchCase> SerializeMatchCase(AstMatchCase matchcase);
    flatbuffers::Offset<NodeFormat::MatchCaseOther> SerializeMatchCaseOther(AstMatchCaseOther matchcaseother);

    flatbuffers::Offset<NodeFormat::Expr> SerializeExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeWildcardExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeBinaryExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeIsExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeAsExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeLitConstExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeUnaryExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeParenExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeCallExpr(const AST::Expr* expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeRefExpr(const AST::Expr* expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeReturnExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeAssignExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeMemberAccess(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeLambdaExpr(const AST::Expr* expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeTrailingClosureExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeTypeConvExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeTryExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeMatchExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeTokenPart(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeQuoteExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeThrowExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializePerformExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeResumeExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeForInExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeIfExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeLetPatternDestructor(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeBlockExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeWhileExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeDoWhileExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeJumpExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeIncOrDecExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializePrimitiveTypeExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeSpawnExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeSynchronizedExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeArrayLit(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeTupleLit(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeSubscriptExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeRangeExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::CallExpr> SerializeCallExpr(const AST::CallExpr* callExpr);
    flatbuffers::Offset<NodeFormat::LambdaExpr> SerializeLambdaExpr(const AST::LambdaExpr* lambdaExpr);
    flatbuffers::Offset<NodeFormat::Block> SerializeBlock(AstBlock block);
    flatbuffers::Offset<NodeFormat::FuncArg> SerializeFuncArg(AstFuncArg funcArg);
    flatbuffers::Offset<NodeFormat::RefExpr> SerializeRefExpr(const AST::RefExpr* refExpr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeOptionalExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeOptionalChainExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeMacroExpandExpr(AstExpr expr);
    flatbuffers::Offset<NodeFormat::Expr> SerializeArrayExpr(AstExpr expr);

    flatbuffers::Offset<NodeFormat::TypeBase> SerializeTypeBase(AstType type);
    flatbuffers::Offset<NodeFormat::Type> SerializeType(AstType type);
    flatbuffers::Offset<NodeFormat::Type> SerializeRefType(const AST::Type* type);
    flatbuffers::Offset<NodeFormat::Type> SerializePrimitiveType(const AST::Type* type);
    flatbuffers::Offset<NodeFormat::Type> SerializeArrayType(AstType type);
    flatbuffers::Offset<NodeFormat::Type> SerializeFuncType(AstType type);
    flatbuffers::Offset<NodeFormat::Type> SerializeThisType(AstType type);
    flatbuffers::Offset<NodeFormat::Type> SerializeParenType(AstType type);
    flatbuffers::Offset<NodeFormat::Type> SerializeQualifiedType(AstType type);
    flatbuffers::Offset<NodeFormat::Type> SerializeOptionType(AstType type);
    flatbuffers::Offset<NodeFormat::Type> SerializeTupleType(AstType type);
    flatbuffers::Offset<NodeFormat::RefType> SerializeRefType(const AST::RefType* refType);
    flatbuffers::Offset<NodeFormat::PrimitiveType> SerializePrimitiveType(const AST::PrimitiveType* primitiveType);
    flatbuffers::Offset<NodeFormat::Type> SerializeVArrayType(AstType type);
    flatbuffers::Offset<NodeFormat::Type> SerializeConstantType(AstType type);

    flatbuffers::Offset<NodeFormat::Annotation> SerializeAnnotation(AstAnnotation annotation);
    flatbuffers::Offset<NodeFormat::Modifier> SerializeModifier(AstModifier modifier);
    flatbuffers::Offset<NodeFormat::DeclBase> SerializeDeclBase(AstDecl decl);
    flatbuffers::Offset<NodeFormat::Decl> SerializeDecl(AstDecl decl);
    flatbuffers::Offset<NodeFormat::Decl> SerializeVarWithPatternDecl(AstDecl decl);
    flatbuffers::Offset<NodeFormat::Decl> SerializeVarDecl(const AST::Decl* decl);
    flatbuffers::Offset<NodeFormat::Decl> SerializeMainDecl(const AST::Decl* decl);
    flatbuffers::Offset<NodeFormat::Decl> SerializeFuncDecl(const AST::Decl* decl);
    flatbuffers::Offset<NodeFormat::Decl> SerializeMacroDecl(const AST::Decl* decl);
    flatbuffers::Offset<NodeFormat::Decl> SerializeMacroExpandDecl(const AST::Decl* decl);
    flatbuffers::Offset<NodeFormat::Decl> SerializeStructDecl(AstDecl decl);
    flatbuffers::Offset<NodeFormat::Decl> SerializePropDecl(AstDecl decl);
    flatbuffers::Offset<NodeFormat::Decl> SerializeTypeAliasDecl(AstDecl decl);
    flatbuffers::Offset<NodeFormat::Decl> SerializeExtendDecl(AstDecl decl);
    flatbuffers::Offset<NodeFormat::Decl> SerializeClassDecl(AstDecl decl);
    flatbuffers::Offset<NodeFormat::Decl> SerializeInterfaceDecl(AstDecl decl);
    flatbuffers::Offset<NodeFormat::Decl> SerializeEnumDecl(AstDecl decl);
    flatbuffers::Offset<NodeFormat::Decl> SerializePrimaryCtorDecl(AstDecl decl);
    flatbuffers::Offset<NodeFormat::VarDecl> SerializeVarDecl(const AST::VarDecl* varDecl);
    flatbuffers::Offset<NodeFormat::MainDecl> SerializeMainDecl(const AST::MainDecl* mainDecl);
    flatbuffers::Offset<NodeFormat::FuncDecl> SerializeFuncDecl(const AST::FuncDecl* funcDecl);
    flatbuffers::Offset<NodeFormat::MacroDecl> SerializeMacroDecl(const AST::MacroDecl* macroDecl);
    flatbuffers::Offset<NodeFormat::MacroExpandDecl> SerializeMacroExpandDecl(
        const AST::MacroExpandDecl* macroExpandDecl);
    flatbuffers::Offset<NodeFormat::Decl> SerializeDeclOfFuncParam(const AST::Decl* decl);
    flatbuffers::Offset<NodeFormat::Decl> SerializeDeclOfMacroExpandParam(const AST::Decl* decl); 
    flatbuffers::Offset<NodeFormat::FuncBody> SerializeFuncBody(AstFuncBody funcBody);
    flatbuffers::Offset<NodeFormat::FuncParam> SerializeFuncParam(AstFuncParam funcParam);
    flatbuffers::Offset<NodeFormat::FuncParam> SerializeMacroExpandParam(AstMacroExpandParam mep);
    flatbuffers::Offset<NodeFormat::StructBody> SerializeStructBody(AstStructBody structBody);
    flatbuffers::Offset<NodeFormat::InterfaceBody> SerializeInterfaceBody(AstInterfaceBody interfaceBody);
    flatbuffers::Offset<NodeFormat::ClassBody> SerializeClassBody(AstClassBody classBody);
    flatbuffers::Offset<NodeFormat::Generic> SerializeGeneric(AstGeneric generic);
    flatbuffers::Offset<NodeFormat::GenericParamDecl> SerializeGenericParamDecl(AstGenericParamDecl genericParamDecl);
    flatbuffers::Offset<NodeFormat::GenericConstraint> SerializeGenericConstraint(
        AstGenericConstraint genericConstraint);

    flatbuffers::Offset<NodeFormat::File> SerializeFile(AstFile file);
    flatbuffers::Offset<NodeFormat::ImportSpec> SerializeImportSpec(AstImportSpec importSpec);
    flatbuffers::Offset<NodeFormat::ImportContent> SerializeImportContent(const AST::ImportContent& content);
    flatbuffers::Offset<NodeFormat::PackageSpec> SerializePackageSpec(AstPackageSpec packageSpec);
    flatbuffers::Offset<NodeFormat::FeaturesDirective> SerializeFeaturesDirective(AstFeaturesDirective featureDirective);
    flatbuffers::Offset<NodeFormat::FeatureId> SerializeFeatureId(const AST::FeatureId& featureId);
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
