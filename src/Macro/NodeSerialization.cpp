// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/*
 * @file
 *
 * This file implements the NodeWriter.
 */

#include "cangjie/Macro/NodeSerialization.h"

#include "cangjie/AST/ASTCasting.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/Macro/TokenSerialization.h"
#include "flatbuffers/NodeFormat_generated.h"

using namespace Cangjie;
using namespace AST;
using namespace Meta;
using namespace NodeSerialization;
using namespace Utils;
using namespace TokenSerialization;

NodeFormat::Position NodeWriter::FlatPosCreateHelper(const Position& pos) const
{
    return NodeFormat::Position(pos.fileID, pos.line, pos.column);
}

flatbuffers::Offset<flatbuffers::Vector<const NodeFormat::Position*>> NodeWriter::CreatePositionVector(
    const std::vector<Cangjie::Position>& positions)
{
    std::function<void(size_t, NodeFormat::Position*)> filler =
        [&positions, this](size_t i, NodeFormat::Position* p) -> void { *p = FlatPosCreateHelper(positions[i]); };
    return builder.CreateVectorOfStructs(positions.size(), filler);
}

flatbuffers::Offset<NodeFormat::PackageSpec> NodeWriter::SerializePackageSpec(AstPackageSpec packageSpec)
{
    if (packageSpec == nullptr) {
        return flatbuffers::Offset<NodeFormat::PackageSpec>();
    }
    auto paNodeBase = SerializeNodeBase(packageSpec);
    auto macroPos =
        NodeFormat::Position(packageSpec->macroPos.fileID, packageSpec->macroPos.line, packageSpec->macroPos.column);
    auto packagePos = NodeFormat::Position(
        packageSpec->packagePos.fileID, packageSpec->packagePos.line, packageSpec->packagePos.column);
    auto prefixPaths = builder.CreateVectorOfStrings(packageSpec->prefixPaths);
    auto prefixPoses = CreatePositionVector(packageSpec->prefixPoses);
    auto prefixDotPoses = CreatePositionVector(packageSpec->prefixDotPoses);
    auto packageName = builder.CreateString(packageSpec->packageName.Val());
    auto packageNamePos = NodeFormat::Position(packageSpec->packageName.Begin().fileID,
        packageSpec->packageName.Begin().line, packageSpec->packageName.Begin().column);
    auto accessible = packageSpec->modifier == nullptr            ? NodeFormat::AccessibleKind_ACCESSIBLE_PUBLIC
        : packageSpec->modifier->modifier == TokenKind::INTERNAL  ? NodeFormat::AccessibleKind_ACCESSIBLE_INTERNAL
        : packageSpec->modifier->modifier == TokenKind::PROTECTED ? NodeFormat::AccessibleKind_ACCESSIBLE_PROTECTED
                                                                  : NodeFormat::AccessibleKind_ACCESSIBLE_PUBLIC;
    return NodeFormat::CreatePackageSpec(builder, paNodeBase, &macroPos, &packagePos, prefixPaths, prefixPoses,
        prefixDotPoses, packageName, &packageNamePos, accessible, packageSpec->hasMacro);
}

flatbuffers::Offset<NodeFormat::ImportContent> NodeWriter::SerializeImportContent(const AST::ImportContent& content)
{
    auto icNodeBase = SerializeNodeBase(&content);
    auto kind = content.kind == ImportKind::IMPORT_ALIAS ? NodeFormat::ImportKind::ImportKind_IMPORT_ALIAS
        : content.kind == ImportKind::IMPORT_ALL         ? NodeFormat::ImportKind_IMPORT_ALL
        : content.kind == ImportKind::IMPORT_MULTI       ? NodeFormat::ImportKind_IMPORT_MULTI
                                                         : NodeFormat::ImportKind_IMPORT_SINGLE;
    auto prefixPaths = builder.CreateVectorOfStrings(content.prefixPaths);
    auto prefixPoses = CreatePositionVector(content.prefixPoses);
    auto prefixDotPoses = CreatePositionVector(content.prefixDotPoses);
    auto ident = builder.CreateString(content.identifier.Val());
    auto identPos = NodeFormat::Position(
        content.identifier.Begin().fileID, content.identifier.Begin().line, content.identifier.Begin().column);
    auto asPos = NodeFormat::Position(content.asPos.fileID, content.asPos.line, content.asPos.column);
    auto asIdent = builder.CreateString(content.aliasName.Val());
    auto asIdentPos = NodeFormat::Position(
        content.aliasName.Begin().fileID, content.aliasName.Begin().line, content.aliasName.Begin().column);
    auto leftCurlPos =
        NodeFormat::Position(content.leftCurlPos.fileID, content.leftCurlPos.line, content.leftCurlPos.column);
    std::vector<flatbuffers::Offset<NodeFormat::ImportContent>> itemsVec;
    for (const auto& item : content.items) {
        itemsVec.emplace_back(SerializeImportContent(item));
    }
    auto items = builder.CreateVector(itemsVec);
    auto commaPoses = CreatePositionVector(content.commaPoses);
    auto rightCurlPos =
        NodeFormat::Position(content.rightCurlPos.fileID, content.rightCurlPos.line, content.rightCurlPos.column);

    return NodeFormat::CreateImportContent(builder, icNodeBase, kind, prefixPaths, prefixPoses, prefixDotPoses, ident,
        &identPos, &asPos, asIdent, &asIdentPos, &leftCurlPos, items, commaPoses, &rightCurlPos);
}

flatbuffers::Offset<NodeFormat::ImportSpec> NodeWriter::SerializeImportSpec(AstImportSpec importSpec)
{
    if (importSpec == nullptr) {
        return flatbuffers::Offset<NodeFormat::ImportSpec>();
    }
    auto imNodeBase = SerializeNodeBase(importSpec);
    auto reExport = !importSpec->modifier                        ? NodeFormat::ReExportKind_REEXPORT_PRIVATE
        : importSpec->modifier->modifier == TokenKind::PUBLIC    ? NodeFormat::ReExportKind_REEXPORT_PUBLIC
        : importSpec->modifier->modifier == TokenKind::PROTECTED ? NodeFormat::ReExportKind_REEXPORT_PROTECTED
        : importSpec->modifier->modifier == TokenKind::INTERNAL  ? NodeFormat::ReExportKind_REEXPORT_INTERNAL
                                                                 : NodeFormat::ReExportKind_REEXPORT_PRIVATE;
    auto importPos =
        NodeFormat::Position(importSpec->importPos.fileID, importSpec->importPos.line, importSpec->importPos.column);
    auto content = SerializeImportContent(importSpec->content);
    return NodeFormat::CreateImportSpec(builder, imNodeBase, reExport, &importPos, content);
}

flatbuffers::Offset<NodeFormat::File> NodeWriter::SerializeFile(AstFile file)
{
    if (file == nullptr) {
        return flatbuffers::Offset<NodeFormat::File>();
    }
    auto fNodeBase = SerializeNodeBase(file);
    auto fileName = builder.CreateString(file->fileName);
    auto filePath = builder.CreateString(file->filePath);
    auto package = SerializePackageSpec(file->package.get());
    std::vector<flatbuffers::Offset<NodeFormat::ImportSpec>> importsVec;
    for (auto& import : file->imports) {
        if (import->TestAttr(Attribute::COMPILER_ADD)) {
            continue;
        }
        auto res = NodeWriter::SerializeImportSpec(import.get());
        importsVec.push_back(res);
    }
    auto imports = builder.CreateVector(importsVec);
    auto decls = FlatVectorCreateHelper<NodeFormat::Decl, Decl, AstDecl>(file->decls, &NodeWriter::SerializeDecl);
    return NodeFormat::CreateFile(builder, fNodeBase, fileName, filePath, package, imports, decls);
}

flatbuffers::Offset<NodeFormat::MatchCase> NodeWriter::SerializeMatchCase(AstMatchCase matchcase)
{
    if (matchcase == nullptr) {
        return flatbuffers::Offset<NodeFormat::MatchCase>();
    }
    auto mcNodeBase = SerializeNodeBase(matchcase);
    auto fbPatterns = FlatVectorCreateHelper<NodeFormat::Pattern, Pattern, AstPattern>(
        matchcase->patterns, &NodeWriter::SerializePattern);
    auto bitOrPosVector = CreatePositionVector(matchcase->bitOrPosVector);
    auto fbPatternGuard = SerializeExpr(matchcase->patternGuard.get());
    auto ifPos = NodeFormat::Position(matchcase->wherePos.fileID, matchcase->wherePos.line, matchcase->wherePos.column);
    auto arrowPos =
        NodeFormat::Position(matchcase->arrowPos.fileID, matchcase->arrowPos.line, matchcase->arrowPos.column);
    auto fbexprOrdecls = SerializeBlock(matchcase->exprOrDecls.get());
    return NodeFormat::CreateMatchCase(
        builder, mcNodeBase, fbPatterns, fbPatternGuard, &ifPos, &arrowPos, fbexprOrdecls, bitOrPosVector);
}

flatbuffers::Offset<NodeFormat::MatchCaseOther> NodeWriter::SerializeMatchCaseOther(AstMatchCaseOther matchcaseother)
{
    if (matchcaseother == nullptr) {
        return flatbuffers::Offset<NodeFormat::MatchCaseOther>();
    }
    auto mcNodeBase = SerializeNodeBase(matchcaseother);
    auto fbExpr = SerializeExpr(matchcaseother->matchExpr.get()); // flatbuffers::Offset<NodeFormat::Expr>
    auto arrowPos = NodeFormat::Position(
        matchcaseother->arrowPos.fileID, matchcaseother->arrowPos.line, matchcaseother->arrowPos.column);
    auto fbExprOrdecls = SerializeBlock(matchcaseother->exprOrDecls.get());
    return NodeFormat::CreateMatchCaseOther(builder, mcNodeBase, fbExpr, &arrowPos, fbExprOrdecls);
}

flatbuffers::Offset<NodeFormat::Pattern> NodeWriter::SerializePattern(AstPattern pattern)
{
    if (pattern == nullptr) {
        return flatbuffers::Offset<NodeFormat::Pattern>();
    }
    static std::unordered_map<AST::ASTKind, std::function<NodeFormatPattern(NodeWriter & nw, AstPattern pattern)>>
        serializePatternMap = {
            {ASTKind::CONST_PATTERN,
                [](NodeWriter& nw, AstPattern pattern) { return nw.SerializeConstPattern(pattern); }},
            {ASTKind::VAR_PATTERN, [](NodeWriter& nw, AstPattern pattern) { return nw.SerializeVarPattern(pattern); }},
            {ASTKind::TUPLE_PATTERN,
                [](NodeWriter& nw, AstPattern pattern) { return nw.SerializeTuplePattern(pattern); }},
            {ASTKind::TYPE_PATTERN,
                [](NodeWriter& nw, AstPattern pattern) { return nw.SerializeTypePattern(pattern); }},
            {ASTKind::ENUM_PATTERN,
                [](NodeWriter& nw, AstPattern pattern) { return nw.SerializeEnumPattern(pattern); }},
            {ASTKind::VAR_OR_ENUM_PATTERN,
                [](NodeWriter& nw, AstPattern pattern) { return nw.SerializeVarOrEnumPattern(pattern); }},
            {ASTKind::EXCEPT_TYPE_PATTERN,
                [](NodeWriter& nw, AstPattern pattern) { return nw.SerializeExceptTypePattern(pattern); }},
            {ASTKind::COMMAND_TYPE_PATTERN,
                [](NodeWriter& nw, AstPattern pattern) { return nw.SerializeCommandTypePattern(pattern); }},
            {ASTKind::WILDCARD_PATTERN,
                [](NodeWriter& nw, AstPattern pattern) { return nw.SerializeWildcardPattern(pattern); }},
        };
    auto serializeFunc = serializePatternMap.find(pattern->astKind);
    if (serializeFunc != serializePatternMap.end()) {
        return serializeFunc->second(*this, pattern);
    }
    Errorln("Unkown Pattern in libast Serialization.");
    return flatbuffers::Offset<NodeFormat::Pattern>();
}

flatbuffers::Offset<NodeFormat::Pattern> NodeWriter::SerializeConstPattern(AstPattern pattern)
{
    auto constPattern = RawStaticCast<const ConstPattern*>(pattern);
    auto fbNodeBase = SerializeNodeBase(constPattern);
    auto fbLiteral = SerializeExpr(constPattern->literal.get());
    auto fbCallExpr = SerializeCallExpr(constPattern->operatorCallExpr.get());
    auto fbConstPattern = NodeFormat::CreateConstPattern(builder, fbNodeBase, fbLiteral, fbCallExpr);
    return NodeFormat::CreatePattern(builder, fbNodeBase, NodeFormat::AnyPattern_CONST_PATTERN, fbConstPattern.Union());
}

flatbuffers::Offset<NodeFormat::Pattern> NodeWriter::SerializeWildcardPattern(AstPattern pattern)
{
    auto wildcardPattern = RawStaticCast<const WildcardPattern*>(pattern);
    auto fbNodeBase = SerializeNodeBase(wildcardPattern);
    auto fbConstPattern = NodeFormat::CreateWildcardPattern(builder, fbNodeBase);
    return NodeFormat::CreatePattern(
        builder, fbNodeBase, NodeFormat::AnyPattern_WILDCARD_PATTERN, fbConstPattern.Union());
}

flatbuffers::Offset<NodeFormat::Pattern> NodeWriter::SerializeVarPattern(AstPattern pattern)
{
    auto varPattern = RawStaticCast<const VarPattern*>(pattern);
    auto fbNodeBase = SerializeNodeBase(varPattern);
    auto fbVarDecl = SerializeVarDecl(varPattern->varDecl.get());
    auto fbVarPattern = NodeFormat::CreateVarPattern(builder, fbNodeBase, fbVarDecl);
    return NodeFormat::CreatePattern(builder, fbNodeBase, NodeFormat::AnyPattern_VAR_PATTERN, fbVarPattern.Union());
}

flatbuffers::Offset<NodeFormat::Pattern> NodeWriter::SerializeTypePattern(AstPattern pattern)
{
    auto typePattern = RawStaticCast<const TypePattern*>(pattern);
    auto fbNodeBase = SerializeNodeBase(typePattern);
    auto fbPattern = SerializePattern(typePattern->pattern.get());
    auto colonPos = FlatPosCreateHelper(typePattern->colonPos);
    auto fbType = SerializeType(typePattern->type.get());
    auto fbTypePattern = NodeFormat::CreateTypePattern(builder, fbNodeBase, fbPattern, &colonPos, fbType);
    return NodeFormat::CreatePattern(builder, fbNodeBase, NodeFormat::AnyPattern_TYPE_PATTERN, fbTypePattern.Union());
}

flatbuffers::Offset<NodeFormat::EnumPattern> NodeWriter::SerializeEnumPattern(const EnumPattern* enumPattern)
{
    if (enumPattern == nullptr) {
        return flatbuffers::Offset<NodeFormat::EnumPattern>();
    }
    auto fbNodeBase = SerializeNodeBase(enumPattern);
    auto fbConstructor = SerializeExpr(enumPattern->constructor.get());
    auto leftParenPos = FlatPosCreateHelper(enumPattern->leftParenPos);
    auto fbPatterns = FlatVectorCreateHelper<NodeFormat::Pattern, Pattern, AstPattern>(
        enumPattern->patterns, &NodeWriter::SerializePattern);
    auto commaPosVector = CreatePositionVector(enumPattern->commaPosVector);
    auto rightParenPos = FlatPosCreateHelper(enumPattern->rightParenPos);
    return NodeFormat::CreateEnumPattern(
        builder, fbNodeBase, fbConstructor, &leftParenPos, fbPatterns, &rightParenPos, commaPosVector);
}

flatbuffers::Offset<NodeFormat::Pattern> NodeWriter::SerializeEnumPattern(const Pattern* pattern)
{
    auto enumPattern = RawStaticCast<const EnumPattern*>(pattern);
    auto fbEnumPattern = SerializeEnumPattern(enumPattern);
    return NodeFormat::CreatePattern(
        builder, SerializeNodeBase(enumPattern), NodeFormat::AnyPattern_ENUM_PATTERN, fbEnumPattern.Union());
}

flatbuffers::Offset<NodeFormat::Pattern> NodeWriter::SerializeVarOrEnumPattern(AstPattern pattern)
{
    auto varOrEnumPattern = RawStaticCast<const VarOrEnumPattern*>(pattern);
    auto fbNodeBase = SerializeNodeBase(varOrEnumPattern);
    auto fbIdentifier = builder.CreateString(varOrEnumPattern->identifier.GetRawText());
    auto fbPattern = SerializePattern(varOrEnumPattern->pattern.get());
    auto fbVarOrEnumPattern = NodeFormat::CreateVarOrEnumPattern(builder, fbNodeBase, fbIdentifier, fbPattern);
    return NodeFormat::CreatePattern(
        builder, fbNodeBase, NodeFormat::AnyPattern_VAR_OR_ENUM_PATTERN, fbVarOrEnumPattern.Union());
}

flatbuffers::Offset<NodeFormat::Pattern> NodeWriter::SerializeTuplePattern(AstPattern pattern)
{
    auto tuplePattern = RawStaticCast<const TuplePattern*>(pattern);
    auto fbNodeBase = SerializeNodeBase(tuplePattern);
    auto leftParenPos = FlatPosCreateHelper(tuplePattern->leftBracePos);
    auto fbPatterns = FlatVectorCreateHelper<NodeFormat::Pattern, Pattern, AstPattern>(
        tuplePattern->patterns, &NodeWriter::SerializePattern);
    auto commaPosVector = CreatePositionVector(tuplePattern->commaPosVector);
    auto rightParenPos = FlatPosCreateHelper(tuplePattern->rightBracePos);
    auto fbTuplePattern =
        NodeFormat::CreateTuplePattern(builder, fbNodeBase, &leftParenPos, fbPatterns, &rightParenPos, commaPosVector);
    return NodeFormat::CreatePattern(builder, fbNodeBase, NodeFormat::AnyPattern_TUPLE_PATTERN, fbTuplePattern.Union());
}

flatbuffers::Offset<NodeFormat::Pattern> NodeWriter::SerializeExceptTypePattern(AstPattern pattern)
{
    auto exceptTypePattern = RawStaticCast<const ExceptTypePattern*>(pattern);
    auto fbNodeBase = SerializeNodeBase(exceptTypePattern);
    auto fbPattern = SerializePattern(exceptTypePattern->pattern.get());
    auto patternPos = FlatPosCreateHelper(exceptTypePattern->patternPos);
    auto colonPos = FlatPosCreateHelper(exceptTypePattern->colonPos);
    auto fbTypes =
        FlatVectorCreateHelper<NodeFormat::Type, Type, AstType>(exceptTypePattern->types, &NodeWriter::SerializeType);
    auto bitOrPosVector = CreatePositionVector(exceptTypePattern->bitOrPosVector);
    auto fbExceptTypePattern = NodeFormat::CreateExceptTypePattern(
        builder, fbNodeBase, fbPattern, &patternPos, &colonPos, fbTypes, bitOrPosVector);
    return NodeFormat::CreatePattern(
        builder, fbNodeBase, NodeFormat::AnyPattern_EXCEPT_TYPE_PATTERN, fbExceptTypePattern.Union());
}

flatbuffers::Offset<NodeFormat::Pattern> NodeWriter::SerializeCommandTypePattern(AstPattern pattern)
{
    auto effectTypePattern = RawStaticCast<const CommandTypePattern*>(pattern);
    auto fbNodeBase = SerializeNodeBase(effectTypePattern);
    auto fbPattern = SerializePattern(effectTypePattern->pattern.get());
    auto patternPos = FlatPosCreateHelper(effectTypePattern->patternPos);
    auto colonPos = FlatPosCreateHelper(effectTypePattern->colonPos);
    auto fbTypes =
        FlatVectorCreateHelper<NodeFormat::Type, Type, AstType>(effectTypePattern->types, &NodeWriter::SerializeType);
    auto bitOrPosVector = CreatePositionVector(effectTypePattern->bitOrPosVector);
    auto fbCommandTypePattern = NodeFormat::CreateCommandTypePattern(
        builder, fbNodeBase, fbPattern, &patternPos, &colonPos, fbTypes, bitOrPosVector);
    return NodeFormat::CreatePattern(
        builder, fbNodeBase, NodeFormat::AnyPattern_COMMAND_TYPE_PATTERN, fbCommandTypePattern.Union());
}

flatbuffers::Offset<NodeFormat::FuncParam> NodeWriter::SerializeFuncParam(AstFuncParam funcParam)
{
    if (funcParam == nullptr) {
        return flatbuffers::Offset<NodeFormat::FuncParam>();
    }
    auto fbNodeBase = SerializeNodeBase(funcParam);
    auto fbVarDeclBase = SerializeVarDecl(funcParam.get());
    auto colonPos = FlatPosCreateHelper(funcParam->colonPos);
    auto fbExpr = SerializeExpr((funcParam->assignment).get());
    auto commaPos = FlatPosCreateHelper(funcParam->commaPos);
    auto notMarkPos = FlatPosCreateHelper(funcParam->notMarkPos);
    return NodeFormat::CreateFuncParam(builder, fbNodeBase, fbVarDeclBase, &colonPos, fbExpr, &commaPos,
        funcParam->isNamedParam, funcParam->isMemberParam, &notMarkPos, funcParam->hasLetOrVar);
}

flatbuffers::Offset<NodeFormat::Decl> NodeWriter::SerializeDeclOfFuncParam(const Decl* decl)
{
    auto funcParam = RawStaticCast<const FuncParam*>(decl);
    auto fbFuncParam = SerializeFuncParam(funcParam);
    return NodeFormat::CreateDecl(
        builder, SerializeDeclBase(funcParam), NodeFormat::AnyDecl_FUNC_PARAM, fbFuncParam.Union());
}

flatbuffers::Offset<NodeFormat::Block> NodeWriter::SerializeBlock(AstBlock block)
{
    if (block == nullptr) {
        return flatbuffers::Offset<NodeFormat::Block>();
    }
    auto fbNodeBase = SerializeNodeBase(block);
    auto leftCurPos = FlatPosCreateHelper(block->leftCurlPos);
    auto rightCurPos = FlatPosCreateHelper(block->rightCurlPos);
    auto unsafePos = FlatPosCreateHelper(block->unsafePos);
    std::vector<flatbuffers::Offset<NodeFormat::Node>> vecNode;
    for (auto& node : block->body) {
        auto nPtr = node.get();
        match (*nPtr)(
            [&, this](const Expr& expr) {
                auto fbExpr = SerializeExpr(&expr); // flatbuffers::Offset<NodeFormat::Expr>
                auto nodeBase = SerializeNodeBase(&expr);
                auto fbNode = NodeFormat::CreateNode(builder, nodeBase, NodeFormat::AnyNode_EXPR, fbExpr.Union());
                vecNode.push_back(fbNode);
            },
            [&, this](const Decl& decl) {
                auto fbDecl = SerializeDecl(&decl); // flatbuffers::Offset<NodeFormat::Decl>
                auto nodeBase = SerializeNodeBase(&decl);
                auto fbNode = NodeFormat::CreateNode(builder, nodeBase, NodeFormat::AnyNode_DECL, fbDecl.Union());
                vecNode.push_back(fbNode);
            },
            [&]() { Errorln("Match Nothing in Block!"); });
    }
    auto fbBody = builder.CreateVector(vecNode);
    return NodeFormat::CreateBlock(
        builder, fbNodeBase, &leftCurPos, fbBody, &rightCurPos, block->TestAttr(Attribute::UNSAFE), &unsafePos);
}

flatbuffers::Offset<NodeFormat::FuncBody> NodeWriter::SerializeFuncBody(AstFuncBody funcBody)
{
    if (funcBody == nullptr) {
        return flatbuffers::Offset<NodeFormat::FuncBody>();
    }
    auto fbNodeBaseFnBody = SerializeNodeBase(funcBody);
    auto paramList = (funcBody->paramLists)[0].get(); // a pointer to FuncParamList
    auto fbNodeBasePaList = SerializeNodeBase(paramList);
    auto leftParenPos = FlatPosCreateHelper(paramList->leftParenPos);
    auto rightParenPos = FlatPosCreateHelper(paramList->rightParenPos);
    std::vector<flatbuffers::Offset<NodeFormat::FuncParam>> vecNode;
    for (auto& param : paramList->params) {
        auto nPtr = param.get();
        match (*nPtr)([&, this](const MacroExpandParam& mep) { vecNode.push_back(SerializeMacroExpandParam(&mep)); },
            [&, this]() { vecNode.push_back(SerializeFuncParam(param)); });
    }
    auto fbParamList = NodeFormat::CreateFuncParamList(
        builder, fbNodeBasePaList, &leftParenPos, builder.CreateVector(vecNode), &rightParenPos);
    // Serialize FuncBlock
    auto bodyPtr = (funcBody->body).get();
    auto arrowPos = FlatPosCreateHelper(funcBody->doubleArrowPos);
    auto colonPos = FlatPosCreateHelper(funcBody->colonPos);
    auto fbRetType = SerializeType(funcBody->retType.get());
    auto hasBody = bodyPtr != nullptr;
    auto fbBody = SerializeBlock(bodyPtr);
    auto fbGeneric = SerializeGeneric(funcBody->generic.get());
    return NodeFormat::CreateFuncBody(
        builder, fbNodeBaseFnBody, fbParamList, &arrowPos, &colonPos, fbRetType, hasBody, fbBody, fbGeneric);
}

flatbuffers::Offset<NodeFormat::DeclBase> NodeWriter::SerializeDeclBase(AstDecl decl)
{
    if (decl == nullptr) {
        return flatbuffers::Offset<NodeFormat::DeclBase>();
    }
    // Serialize DeclBase for all Decls
    auto fbNodeBase = SerializeNodeBase(decl);
    auto identifier = builder.CreateString(decl->identifier.GetRawText());
    auto identifierPos = FlatPosCreateHelper(decl->identifier.GetRawPos());
    auto keywordPos = FlatPosCreateHelper(decl->keywordPos);
    auto fbAnnVec = FlatVectorCreateHelper<NodeFormat::Annotation, Annotation, AstAnnotation>(
        decl->annotations, &NodeWriter::SerializeAnnotation);
    std::vector<flatbuffers::Offset<NodeFormat::Modifier>> vecMod;
    auto modifiersVec = SortModifierByPos(decl->modifiers);
    for (auto& mod : modifiersVec) {
        auto fbMod = SerializeModifier(mod);
        vecMod.push_back(fbMod);
    }
    auto fbModVec = builder.CreateVector(vecMod);
    auto generic = SerializeGeneric(decl->generic.get());
    auto isConst = decl->IsConst();
    return NodeFormat::CreateDeclBase(
        builder, fbNodeBase, identifier, &identifierPos, &keywordPos, fbAnnVec, fbModVec, generic, isConst);
}

flatbuffers::Offset<NodeFormat::TypeBase> NodeWriter::SerializeTypeBase(AstType type)
{
    if (type == nullptr) {
        return flatbuffers::Offset<NodeFormat::TypeBase>();
    }
    auto fbBase = SerializeNodeBase(type);
    auto commaPos = FlatPosCreateHelper(type->commaPos);
    auto bitAndPos = FlatPosCreateHelper(type->bitAndPos);
    auto typeParameterName = builder.CreateString(type->GetTypeParameterNameRawText());
    auto colonPos = FlatPosCreateHelper(type->colonPos);
    auto typePos = FlatPosCreateHelper(type->typePos);
    return NodeFormat::CreateTypeBase(builder, fbBase, &commaPos, typeParameterName, &colonPos, &typePos, &bitAndPos);
}

flatbuffers::Offset<NodeFormat::RefType> NodeWriter::SerializeRefType(const RefType* refType)
{
    if (refType == nullptr) {
        return flatbuffers::Offset<NodeFormat::RefType>();
    }
    auto fbTypeBase = SerializeTypeBase(refType);
    auto ref = refType->ref;
    auto identifier = builder.CreateString(ref.identifier.GetRawText());
    auto identifierPos = FlatPosCreateHelper(ref.identifier.GetRawPos());
    auto fbRef = NodeFormat::CreateReference(builder, identifier, &identifierPos);
    auto leftAnglePos = FlatPosCreateHelper(refType->leftAnglePos);
    auto fbTypeVec =
        FlatVectorCreateHelper<NodeFormat::Type, Type, AstType>(refType->typeArguments, &NodeWriter::SerializeType);
    auto rightAnglePos = FlatPosCreateHelper(refType->rightAnglePos);
    return NodeFormat::CreateRefType(builder, fbTypeBase, fbRef, &leftAnglePos, fbTypeVec, &rightAnglePos);
}
flatbuffers::Offset<NodeFormat::Type> NodeWriter::SerializeRefType(const Type* type)
{
    auto refType = RawStaticCast<const RefType*>(type);
    auto fbRefType = SerializeRefType(refType);
    return NodeFormat::CreateType(builder, SerializeTypeBase(refType), NodeFormat::AnyType_REF_TYPE, fbRefType.Union());
}

flatbuffers::Offset<NodeFormat::PrimitiveType> NodeWriter::SerializePrimitiveType(const PrimitiveType* primitiveType)
{
    if (primitiveType == nullptr) {
        return flatbuffers::Offset<NodeFormat::PrimitiveType>();
    }
    auto fbTypeBase = SerializeTypeBase(primitiveType);
    auto fbTypeStr = builder.CreateString(primitiveType->str);
    auto typeKind = static_cast<uint16_t>(primitiveType->kind);
    return NodeFormat::CreatePrimitiveType(builder, fbTypeBase, fbTypeStr, typeKind);
}

flatbuffers::Offset<NodeFormat::Type> NodeWriter::SerializePrimitiveType(const Type* type)
{
    auto primitiveType = RawStaticCast<const PrimitiveType*>(type);
    auto fbPrimType = SerializePrimitiveType(primitiveType);
    return NodeFormat::CreateType(
        builder, SerializeTypeBase(primitiveType), NodeFormat::AnyType_PRIMITIVE_TYPE, fbPrimType.Union());
}

flatbuffers::Offset<NodeFormat::Type> NodeWriter::SerializeThisType(AstType type)
{
    auto thisType = RawStaticCast<const ThisType*>(type);
    auto fbTypeBase = SerializeTypeBase(thisType);
    auto fbThisType = NodeFormat::CreateThisType(builder, fbTypeBase);
    return NodeFormat::CreateType(builder, fbTypeBase, NodeFormat::AnyType_THIS_TYPE, fbThisType.Union());
}

flatbuffers::Offset<NodeFormat::Type> NodeWriter::SerializeParenType(AstType type)
{
    auto parenType = RawStaticCast<const ParenType*>(type);
    auto fbTypeBase = SerializeTypeBase(parenType);
    auto leftParenPos = FlatPosCreateHelper(parenType->leftParenPos);
    auto ptype = SerializeType(parenType->type.get());
    auto rightParenPos = FlatPosCreateHelper(parenType->rightParenPos);
    auto fbParenType = NodeFormat::CreateParenType(builder, fbTypeBase, &leftParenPos, ptype, &rightParenPos);
    return NodeFormat::CreateType(builder, fbTypeBase, NodeFormat::AnyType_PAREN_TYPE, fbParenType.Union());
}

flatbuffers::Offset<NodeFormat::Type> NodeWriter::SerializeQualifiedType(AstType type)
{
    auto qualifiedType = RawStaticCast<const QualifiedType*>(type);
    auto fbTypeBase = SerializeTypeBase(qualifiedType);
    auto baseType = SerializeType(qualifiedType->baseType.get());
    auto dotPos = FlatPosCreateHelper(qualifiedType->dotPos);
    auto fieldStr = builder.CreateString(qualifiedType->field.GetRawText());
    auto fieldPos = FlatPosCreateHelper(qualifiedType->field.GetRawPos());
    auto leftAnglePos = FlatPosCreateHelper(qualifiedType->leftAnglePos);
    auto typeArguments = FlatVectorCreateHelper<NodeFormat::Type, Type, AstType>(
        qualifiedType->typeArguments, &NodeWriter::SerializeType);
    auto rightAnglePos = FlatPosCreateHelper(qualifiedType->rightAnglePos);
    auto fbQualifiedType = NodeFormat::CreateQualifiedType(
        builder, fbTypeBase, baseType, &dotPos, fieldStr, &fieldPos, &leftAnglePos, typeArguments, &rightAnglePos);
    return NodeFormat::CreateType(builder, fbTypeBase, NodeFormat::AnyType_QUALIFIED_TYPE, fbQualifiedType.Union());
}

flatbuffers::Offset<NodeFormat::Type> NodeWriter::SerializeOptionType(AstType type)
{
    auto optionType = RawStaticCast<const OptionType*>(type);
    auto fbTypeBase = SerializeTypeBase(optionType);
    auto componentType = SerializeType(optionType->componentType.get());
    auto questVector = CreatePositionVector(optionType->questVector);
    auto fbOptionType = NodeFormat::CreateOptionType(
        builder, fbTypeBase, componentType, static_cast<int32_t>(optionType->questNum), questVector);
    return NodeFormat::CreateType(builder, fbTypeBase, NodeFormat::AnyType_OPTION_TYPE, fbOptionType.Union());
}

flatbuffers::Offset<NodeFormat::Type> NodeWriter::SerializeTupleType(AstType type)
{
    auto tupleType = RawStaticCast<const TupleType*>(type);
    auto fbTypeBase = SerializeTypeBase(tupleType);
    auto fieldTypes =
        FlatVectorCreateHelper<NodeFormat::Type, Type, AstType>(tupleType->fieldTypes, &NodeWriter::SerializeType);
    auto leftParenPos = FlatPosCreateHelper(tupleType->leftParenPos);
    auto rightParenPos = FlatPosCreateHelper(tupleType->rightParenPos);
    auto commaPosVector = CreatePositionVector(tupleType->commaPosVector);
    if (tupleType->commaPosVector.size() == 0) {
        Errorln("Legacy tuple type syntax no longer allowed after version 0.28.4, use ',' instead.");
    }
    auto fbTupleType =
        NodeFormat::CreateTupleType(builder, fbTypeBase, fieldTypes, &leftParenPos, &rightParenPos, commaPosVector);
    return NodeFormat::CreateType(builder, fbTypeBase, NodeFormat::AnyType_TUPLE_TYPE, fbTupleType.Union());
}

flatbuffers::Offset<NodeFormat::Type> NodeWriter::SerializeFuncType(AstType type)
{
    auto funcType = RawStaticCast<const FuncType*>(type);
    auto fbTypeBase = SerializeTypeBase(funcType);
    auto leftParenPos = FlatPosCreateHelper(funcType->leftParenPos);
    auto fbTypeVec =
        FlatVectorCreateHelper<NodeFormat::Type, Type, AstType>(funcType->paramTypes, &NodeWriter::SerializeType);
    auto rightParenPos = FlatPosCreateHelper(funcType->rightParenPos);
    auto arrowPos = FlatPosCreateHelper(funcType->arrowPos);
    auto fbRetType = SerializeType(funcType->retType.get());
    auto fbFuncType = NodeFormat::CreateFuncType(
        builder, fbTypeBase, &leftParenPos, fbTypeVec, &rightParenPos, &arrowPos, fbRetType, funcType->isC);
    return NodeFormat::CreateType(builder, fbTypeBase, NodeFormat::AnyType_FUNC_TYPE, fbFuncType.Union());
}

flatbuffers::Offset<NodeFormat::Type> NodeWriter::SerializeVArrayType(AstType type)
{
    auto vArrayType = RawStaticCast<const VArrayType*>(type);
    auto fbTypeBase = SerializeTypeBase(vArrayType);
    auto varrayPos = FlatPosCreateHelper(vArrayType->varrayPos);
    auto leftAnglePos = FlatPosCreateHelper(vArrayType->leftAnglePos);
    auto typeArgument = SerializeType(vArrayType->typeArgument.get());
    auto constantType = SerializeType(vArrayType->constantType.get());
    auto rightAnglePos = FlatPosCreateHelper(vArrayType->rightAnglePos);
    auto fbVArrayType = NodeFormat::CreateVArrayType(
        builder, fbTypeBase, &varrayPos, &leftAnglePos, typeArgument, constantType, &rightAnglePos);
    return NodeFormat::CreateType(builder, fbTypeBase, NodeFormat::AnyType_VARRAY_TYPE, fbVArrayType.Union());
}

flatbuffers::Offset<NodeFormat::Type> NodeWriter::SerializeConstantType(AstType type)
{
    auto constantType = RawStaticCast<const ConstantType*>(type);
    auto fbTypeBase = SerializeTypeBase(constantType);
    auto constantExpr = SerializeExpr(constantType->constantExpr.get());
    auto dollarPos = FlatPosCreateHelper(constantType->dollarPos);
    auto fbConstantType = NodeFormat::CreateConstantType(builder, fbTypeBase, constantExpr, &dollarPos);
    return NodeFormat::CreateType(builder, fbTypeBase, NodeFormat::AnyType_CONSTANT_TYPE, fbConstantType.Union());
}

flatbuffers::Offset<NodeFormat::Type> NodeWriter::SerializeType(AstType type)
{
    if (type == nullptr) {
        return flatbuffers::Offset<NodeFormat::Type>();
    }
    static std::unordered_map<AST::ASTKind, std::function<NodeFormatType(NodeWriter & nw, AstType type)>>
        serializeTypeMap = {
            {ASTKind::REF_TYPE, [](NodeWriter& nw, AstType type) { return nw.SerializeRefType(type); }},
            {ASTKind::PRIMITIVE_TYPE, [](NodeWriter& nw, AstType type) { return nw.SerializePrimitiveType(type); }},
            {ASTKind::FUNC_TYPE, [](NodeWriter& nw, AstType type) { return nw.SerializeFuncType(type); }},
            {ASTKind::THIS_TYPE, [](NodeWriter& nw, AstType type) { return nw.SerializeThisType(type); }},
            {ASTKind::PAREN_TYPE, [](NodeWriter& nw, AstType type) { return nw.SerializeParenType(type); }},
            {ASTKind::QUALIFIED_TYPE, [](NodeWriter& nw, AstType type) { return nw.SerializeQualifiedType(type); }},
            {ASTKind::OPTION_TYPE, [](NodeWriter& nw, AstType type) { return nw.SerializeOptionType(type); }},
            {ASTKind::TUPLE_TYPE, [](NodeWriter& nw, AstType type) { return nw.SerializeTupleType(type); }},
            {ASTKind::VARRAY_TYPE, [](NodeWriter& nw, AstType type) { return nw.SerializeVArrayType(type); }},
            {ASTKind::CONSTANT_TYPE, [](NodeWriter& nw, AstType type) { return nw.SerializeConstantType(type); }},
        };
    // Serialize Type.
    auto serializeFunc = serializeTypeMap.find(type->astKind);
    if (serializeFunc != serializeTypeMap.end()) {
        return serializeFunc->second(*this, type);
    }
    Errorln("Type Not Supported in Libast Yet\n");
    return flatbuffers::Offset<NodeFormat::Type>();
}

flatbuffers::Offset<NodeFormat::VarDecl> NodeWriter::SerializeVarDecl(const VarDecl* varDecl)
{
    if (varDecl == nullptr) {
        return flatbuffers::Offset<NodeFormat::VarDecl>();
    }
    auto fbDeclBase = SerializeDeclBase(varDecl);
    auto typePtr = varDecl->type.get();
    auto fbType = SerializeType(typePtr);
    auto colonPos = FlatPosCreateHelper(varDecl->colonPos);
    auto initializer = SerializeExpr(varDecl->initializer.get());
    auto assignPos = FlatPosCreateHelper(varDecl->assignPos);
    auto isVar = varDecl->isVar;
    auto isEnumConstruct = varDecl->TestAttr(Attribute::ENUM_CONSTRUCTOR);
    // Situations where keywords are not required:
    // case1: the varDecl is in pattern
    auto emptyKeyword = varDecl->parentPattern == nullptr ? false : true;
    // case2: the varDecl is in tryWithResource
    if (varDecl->parentPattern == nullptr) {
        emptyKeyword = varDecl->isResourceVar;
    }
    return NodeFormat::CreateVarDecl(
        builder, fbDeclBase, fbType, &colonPos, initializer, &assignPos, isVar, isEnumConstruct, emptyKeyword);
}

flatbuffers::Offset<NodeFormat::Decl> NodeWriter::SerializeVarDecl(const Decl* decl)
{
    auto varDecl = RawStaticCast<const VarDecl*>(decl);
    auto fbVarDecl = SerializeVarDecl(varDecl);
    auto fbDeclBase = SerializeDeclBase(varDecl);
    return NodeFormat::CreateDecl(builder, fbDeclBase, NodeFormat::AnyDecl_VAR_DECL, fbVarDecl.Union());
}

flatbuffers::Offset<NodeFormat::FuncDecl> NodeWriter::SerializeFuncDecl(const FuncDecl* funcDecl)
{
    if (funcDecl == nullptr) {
        return flatbuffers::Offset<NodeFormat::FuncDecl>();
    }
    // Serialize FuncDecl.
    auto fbDeclBase = SerializeDeclBase(funcDecl);
    auto leftParenPos = FlatPosCreateHelper(funcDecl->leftParenPos);
    auto rightParenPos = FlatPosCreateHelper(funcDecl->rightParenPos);
    auto fbFuncBody = SerializeFuncBody(funcDecl->funcBody.get());
    auto isEnumConstruct = funcDecl->TestAttr(Attribute::ENUM_CONSTRUCTOR);
    return NodeFormat::CreateFuncDecl(builder, fbDeclBase, &leftParenPos, &rightParenPos, fbFuncBody,
        funcDecl->isSetter, funcDecl->isGetter, static_cast<int>(funcDecl->op), isEnumConstruct);
}

flatbuffers::Offset<NodeFormat::Decl> NodeWriter::SerializeFuncDecl(const Decl* decl)
{
    auto funcDecl = RawStaticCast<const FuncDecl*>(decl);
    auto fbFuncDecl = SerializeFuncDecl(funcDecl);
    auto fbDeclBase = SerializeDeclBase(funcDecl);
    return NodeFormat::CreateDecl(builder, fbDeclBase, NodeFormat::AnyDecl_FUNC_DECL, fbFuncDecl.Union());
}

flatbuffers::Offset<NodeFormat::MainDecl> NodeWriter::SerializeMainDecl(const MainDecl* mainDecl)
{
    if (mainDecl == nullptr) {
        return flatbuffers::Offset<NodeFormat::MainDecl>();
    }
    // Serialize MainDecl.
    auto fbDeclBase = SerializeDeclBase(mainDecl);
    auto fbFuncBody = SerializeFuncBody(mainDecl->funcBody.get());
    return NodeFormat::CreateMainDecl(builder, fbDeclBase, fbFuncBody);
}

flatbuffers::Offset<NodeFormat::Decl> NodeWriter::SerializeMainDecl(const Decl* decl)
{
    auto mainDecl = RawStaticCast<const MainDecl*>(decl);
    auto fbMainDecl = SerializeMainDecl(mainDecl);
    auto fbDeclBase = SerializeDeclBase(mainDecl);
    return NodeFormat::CreateDecl(builder, fbDeclBase, NodeFormat::AnyDecl_MAIN_DECL, fbMainDecl.Union());
}

flatbuffers::Offset<NodeFormat::MacroDecl> NodeWriter::SerializeMacroDecl(const MacroDecl* macroDecl)
{
    if (macroDecl == nullptr) {
        return flatbuffers::Offset<NodeFormat::MacroDecl>();
    }
    // Serialize MacroDecl.
    auto fbDeclBase = SerializeDeclBase(macroDecl);
    auto leftParenPos = FlatPosCreateHelper(macroDecl->leftParenPos);
    auto rightParenPos = FlatPosCreateHelper(macroDecl->rightParenPos);
    auto fbFuncBody = SerializeFuncBody(macroDecl->funcBody.get());
    return NodeFormat::CreateMacroDecl(builder, fbDeclBase, &leftParenPos, &rightParenPos, fbFuncBody);
}

flatbuffers::Offset<NodeFormat::Decl> NodeWriter::SerializeMacroDecl(const Decl* decl)
{
    auto macroDecl = RawStaticCast<const MacroDecl*>(decl);
    auto fbMacroDecl = SerializeMacroDecl(macroDecl);
    auto fbDeclBase = SerializeDeclBase(macroDecl);
    return NodeFormat::CreateDecl(builder, fbDeclBase, NodeFormat::AnyDecl_MACRO_DECL, fbMacroDecl.Union());
}

flatbuffers::Offset<NodeFormat::GenericParamDecl> NodeWriter::SerializeGenericParamDecl(
    AstGenericParamDecl genericParamDecl)
{
    if (genericParamDecl == nullptr) {
        return flatbuffers::Offset<NodeFormat::GenericParamDecl>();
    }
    auto fbBase = SerializeDeclBase(genericParamDecl);
    auto commaPos = FlatPosCreateHelper(genericParamDecl->commaPos);
    return NodeFormat::CreateGenericParamDecl(builder, fbBase, &commaPos);
}

flatbuffers::Offset<NodeFormat::GenericConstraint> NodeWriter::SerializeGenericConstraint(
    AstGenericConstraint genericConstraint)
{
    if (genericConstraint == nullptr) {
        return flatbuffers::Offset<NodeFormat::GenericConstraint>();
    }
    auto fbBase = SerializeNodeBase(genericConstraint);
    auto wherePos = FlatPosCreateHelper(genericConstraint->wherePos);
    auto fbRefType = SerializeRefType(genericConstraint->type.get());
    auto operatorPos = FlatPosCreateHelper(genericConstraint->operatorPos);
    auto fbUpperBounds = FlatVectorCreateHelper<NodeFormat::Type, Type, AstType>(
        genericConstraint->upperBounds, &NodeWriter::SerializeType);
    auto bitAndPos = CreatePositionVector(genericConstraint->bitAndPos);
    auto commaPos = FlatPosCreateHelper(genericConstraint->commaPos);
    return NodeFormat::CreateGenericConstraint(
        builder, fbBase, &wherePos, fbRefType, &operatorPos, fbUpperBounds, bitAndPos, &commaPos);
}

flatbuffers::Offset<NodeFormat::Generic> NodeWriter::SerializeGeneric(AstGeneric generic)
{
    if (generic == nullptr) {
        return flatbuffers::Offset<NodeFormat::Generic>();
    }
    auto leftAnglePos = FlatPosCreateHelper(generic->leftAnglePos);
    auto fbTypeParameters = FlatVectorCreateHelper<NodeFormat::GenericParamDecl, GenericParamDecl, AstGenericParamDecl>(
        generic->typeParameters, &NodeWriter::SerializeGenericParamDecl);
    auto rightAnglePos = FlatPosCreateHelper(generic->rightAnglePos);
    auto fbGenericConstraints =
        FlatVectorCreateHelper<NodeFormat::GenericConstraint, GenericConstraint, AstGenericConstraint>(
            generic->genericConstraints, &NodeWriter::SerializeGenericConstraint);
    return NodeFormat::CreateGeneric(builder, &leftAnglePos, fbTypeParameters, &rightAnglePos, fbGenericConstraints);
}

flatbuffers::Offset<NodeFormat::ClassBody> NodeWriter::SerializeClassBody(AstClassBody classBody)
{
    if (classBody == nullptr) {
        return flatbuffers::Offset<NodeFormat::ClassBody>();
    }
    // Serialize ClassBody
    auto fbBase = SerializeNodeBase(classBody);
    auto leftCurlPos = FlatPosCreateHelper(classBody->leftCurlPos);
    auto fbDecls =
        FlatVectorCreateHelper<NodeFormat::Decl, Decl, AstDecl>(classBody->decls, &NodeWriter::SerializeDecl);
    auto rightCurlPos = FlatPosCreateHelper(classBody->rightCurlPos);
    return NodeFormat::CreateClassBody(builder, fbBase, &leftCurlPos, fbDecls, &rightCurlPos);
}

flatbuffers::Offset<NodeFormat::Decl> NodeWriter::SerializeClassDecl(AstDecl decl)
{
    auto classDecl = RawStaticCast<const ClassDecl*>(decl);
    auto fbBase = SerializeDeclBase(classDecl);
    auto upperBoundPos = FlatPosCreateHelper(classDecl->upperBoundPos);
    auto fbSuperTypes =
        FlatVectorCreateHelper<NodeFormat::Type, Type, AstType>(classDecl->inheritedTypes, &NodeWriter::SerializeType);
    auto fbBody = SerializeClassBody(classDecl->body.get());
    auto fbClassDecl = NodeFormat::CreateClassDecl(builder, fbBase, &upperBoundPos, fbSuperTypes, fbBody);
    return NodeFormat::CreateDecl(builder, fbBase, NodeFormat::AnyDecl_CLASS_DECL, fbClassDecl.Union());
}

flatbuffers::Offset<NodeFormat::InterfaceBody> NodeWriter::SerializeInterfaceBody(AstInterfaceBody interfaceBody)
{
    if (interfaceBody == nullptr) {
        return flatbuffers::Offset<NodeFormat::InterfaceBody>();
    }
    auto fbBase = SerializeNodeBase(interfaceBody);
    auto leftCurlPos = FlatPosCreateHelper(interfaceBody->leftCurlPos);
    // Serialize vector of decl
    auto fbDecls =
        FlatVectorCreateHelper<NodeFormat::Decl, Decl, AstDecl>(interfaceBody->decls, &NodeWriter::SerializeDecl);
    auto rightCurlPos = FlatPosCreateHelper(interfaceBody->rightCurlPos);
    return NodeFormat::CreateInterfaceBody(builder, fbBase, &leftCurlPos, fbDecls, &rightCurlPos);
}

flatbuffers::Offset<NodeFormat::Decl> NodeWriter::SerializeInterfaceDecl(AstDecl decl)
{
    auto interfaceDecl = RawStaticCast<const InterfaceDecl*>(decl);
    auto fbBase = SerializeDeclBase(interfaceDecl);
    auto upperBoundPos = FlatPosCreateHelper(interfaceDecl->upperBoundPos);
    auto fbSuperTypes = FlatVectorCreateHelper<NodeFormat::Type, Type, AstType>(
        interfaceDecl->inheritedTypes, &NodeWriter::SerializeType);
    auto fbBody = SerializeInterfaceBody(interfaceDecl->body.get());
    auto fbInterfaceDecl = NodeFormat::CreateInterfaceDecl(builder, fbBase, &upperBoundPos, fbSuperTypes, fbBody);
    return NodeFormat::CreateDecl(builder, fbBase, NodeFormat::AnyDecl_INTERFACE_DECL, fbInterfaceDecl.Union());
}

flatbuffers::Offset<NodeFormat::Decl> NodeWriter::SerializePrimaryCtorDecl(AstDecl decl)
{
    auto primaryCtorDecl = RawStaticCast<const PrimaryCtorDecl*>(decl);
    auto fbBase = SerializeDeclBase(primaryCtorDecl);
    auto fbFuncBody = SerializeFuncBody(primaryCtorDecl->funcBody.get());
    auto fbPrimaryCtorDecl = NodeFormat::CreatePrimaryCtorDecl(builder, fbBase, fbFuncBody);
    return NodeFormat::CreateDecl(builder, fbBase, NodeFormat::AnyDecl_PRIMARY_CTOR_DECL, fbPrimaryCtorDecl.Union());
}

flatbuffers::Offset<NodeFormat::Decl> NodeWriter::SerializeDecl(AstDecl decl)
{
    if (decl == nullptr) {
        return flatbuffers::Offset<NodeFormat::Decl>();
    }
    static std::unordered_map<AST::ASTKind, std::function<NodeFormatDecl(NodeWriter & nw, AstDecl decl)>>
        serializeDeclMap = {
            {ASTKind::VAR_DECL, [](NodeWriter& nw, AstDecl decl) { return nw.SerializeVarDecl(decl); }},
            {ASTKind::FUNC_DECL, [](NodeWriter& nw, AstDecl decl) { return nw.SerializeFuncDecl(decl); }},
            {ASTKind::MAIN_DECL, [](NodeWriter& nw, AstDecl decl) { return nw.SerializeMainDecl(decl); }},
            {ASTKind::STRUCT_DECL, [](NodeWriter& nw, AstDecl decl) { return nw.SerializeStructDecl(decl); }},
            {ASTKind::CLASS_DECL, [](NodeWriter& nw, AstDecl decl) { return nw.SerializeClassDecl(decl); }},
            {ASTKind::INTERFACE_DECL, [](NodeWriter& nw, AstDecl decl) { return nw.SerializeInterfaceDecl(decl); }},
            {ASTKind::PRIMARY_CTOR_DECL,
                [](NodeWriter& nw, AstDecl decl) { return nw.SerializePrimaryCtorDecl(decl); }},
            {ASTKind::PROP_DECL, [](NodeWriter& nw, AstDecl decl) { return nw.SerializePropDecl(decl); }},
            {ASTKind::ENUM_DECL, [](NodeWriter& nw, AstDecl decl) { return nw.SerializeEnumDecl(decl); }},
            {ASTKind::TYPE_ALIAS_DECL, [](NodeWriter& nw, AstDecl decl) { return nw.SerializeTypeAliasDecl(decl); }},
            {ASTKind::EXTEND_DECL, [](NodeWriter& nw, AstDecl decl) { return nw.SerializeExtendDecl(decl); }},
            {ASTKind::MACRO_DECL, [](NodeWriter& nw, AstDecl decl) { return nw.SerializeMacroDecl(decl); }},
            {ASTKind::MACRO_EXPAND_DECL,
                [](NodeWriter& nw, AstDecl decl) { return nw.SerializeMacroExpandDecl(decl); }},
            {ASTKind::FUNC_PARAM, [](NodeWriter& nw, AstDecl decl) { return nw.SerializeDeclOfFuncParam(decl); }},
            {ASTKind::VAR_WITH_PATTERN_DECL,
                [](NodeWriter& nw, AstDecl decl) { return nw.SerializeVarWithPatternDecl(decl); }},
        };
    // Serialize Decl.
    auto serializeFunc = serializeDeclMap.find(decl->astKind);
    if (serializeFunc != serializeDeclMap.end()) {
        return serializeFunc->second(*this, decl);
    }
    Errorln("Decl Not Supported in Libast Yet\n");
    return flatbuffers::Offset<NodeFormat::Decl>();
}

flatbuffers::Offset<NodeFormat::StructBody> NodeWriter::SerializeStructBody(AstStructBody structBody)
{
    if (structBody == nullptr) {
        return flatbuffers::Offset<NodeFormat::StructBody>();
    }
    auto fbNodeBase = SerializeNodeBase(structBody);
    auto leftCurlPos = FlatPosCreateHelper(structBody->leftCurlPos);
    auto fbDecls =
        FlatVectorCreateHelper<NodeFormat::Decl, Decl, AstDecl>(structBody->decls, &NodeWriter::SerializeDecl);
    auto rightCurlPos = FlatPosCreateHelper(structBody->rightCurlPos);
    return NodeFormat::CreateStructBody(builder, fbNodeBase, &leftCurlPos, fbDecls, &rightCurlPos);
}

flatbuffers::Offset<NodeFormat::Decl> NodeWriter::SerializeStructDecl(AstDecl decl)
{
    auto structDecl = RawStaticCast<const StructDecl*>(decl);
    auto fbDeclBase = SerializeDeclBase(structDecl);
    auto structBodyPtr = structDecl->body.get();
    auto fbStructBody = SerializeStructBody(structBodyPtr);
    auto upperBoundPos = FlatPosCreateHelper(structDecl->upperBoundPos);
    auto fbSuperTypes =
        FlatVectorCreateHelper<NodeFormat::Type, Type, AstType>(structDecl->inheritedTypes, &NodeWriter::SerializeType);
    auto fbStructDecl = NodeFormat::CreateStructDecl(builder, fbDeclBase, fbStructBody, &upperBoundPos, fbSuperTypes);
    return NodeFormat::CreateDecl(builder, fbDeclBase, NodeFormat::AnyDecl_STRUCT_DECL, fbStructDecl.Union());
}

flatbuffers::Offset<NodeFormat::Decl> NodeWriter::SerializeVarWithPatternDecl(AstDecl decl)
{
    auto varWithPatternDecl = RawStaticCast<const VarWithPatternDecl*>(decl);
    auto fbDeclBase = SerializeDeclBase(varWithPatternDecl);
    auto type = SerializeType(varWithPatternDecl->type.get());
    auto colonPos = FlatPosCreateHelper(varWithPatternDecl->colonPos);
    auto initializer = SerializeExpr(varWithPatternDecl->initializer.get());
    auto pattern = SerializePattern(varWithPatternDecl->irrefutablePattern.get());
    auto assignPos = FlatPosCreateHelper(varWithPatternDecl->assignPos);
    auto isVar = varWithPatternDecl->isVar;
    auto fbVarWithPatternDecl = NodeFormat::CreateVarWithPatternDecl(
        builder, fbDeclBase, type, &colonPos, initializer, pattern, &assignPos, isVar);
    return NodeFormat::CreateDecl(
        builder, fbDeclBase, NodeFormat::AnyDecl_VAR_WITH_PATTERN_DECL, fbVarWithPatternDecl.Union());
}

flatbuffers::Offset<NodeFormat::Decl> NodeWriter::SerializeEnumDecl(AstDecl decl)
{
    auto enumDecl = RawStaticCast<const EnumDecl*>(decl);
    auto fbDeclBase = SerializeDeclBase(enumDecl);
    auto leftCurlPos = FlatPosCreateHelper(enumDecl->leftCurlPos);
    auto constructors =
        FlatVectorCreateHelper<NodeFormat::Decl, Decl, AstDecl>(enumDecl->constructors, &NodeWriter::SerializeDecl);
    auto bitOrPosVector = CreatePositionVector(enumDecl->bitOrPosVector);
    auto members =
        FlatVectorCreateHelper<NodeFormat::Decl, Decl, AstDecl>(enumDecl->members, &NodeWriter::SerializeDecl);
    auto rightCurlPos = FlatPosCreateHelper(enumDecl->rightCurlPos);
    auto upperBoundPos = FlatPosCreateHelper(enumDecl->upperBoundPos);
    auto ellipsisPos = FlatPosCreateHelper(enumDecl->ellipsisPos);
    auto fbSuperTypes =
        FlatVectorCreateHelper<NodeFormat::Type, Type, AstType>(enumDecl->inheritedTypes, &NodeWriter::SerializeType);
    auto fbEnumDecl =
        NodeFormat::CreateEnumDecl(builder, fbDeclBase, enumDecl->hasArguments, &leftCurlPos, constructors,
            bitOrPosVector, members, &rightCurlPos, &upperBoundPos, fbSuperTypes, enumDecl->hasEllipsis, &ellipsisPos);
    return NodeFormat::CreateDecl(builder, fbDeclBase, NodeFormat::AnyDecl_ENUM_DECL, fbEnumDecl.Union());
}

flatbuffers::Offset<NodeFormat::Decl> NodeWriter::SerializePropDecl(AstDecl decl)
{
    auto propDecl = RawStaticCast<const PropDecl*>(decl);
    auto fbVarDecl = SerializeVarDecl(propDecl);
    auto fbDeclBase = SerializeDeclBase(propDecl);
    auto colonPos = FlatPosCreateHelper(propDecl->colonPos);
    auto leftCurlPos = FlatPosCreateHelper(propDecl->leftCurlPos);
    auto fbGetters = FlatVectorCreateHelper<NodeFormat::FuncDecl, FuncDecl, const FuncDecl*>(
        propDecl->getters, &NodeWriter::SerializeFuncDecl);
    auto fbSetters = FlatVectorCreateHelper<NodeFormat::FuncDecl, FuncDecl, const FuncDecl*>(
        propDecl->setters, &NodeWriter::SerializeFuncDecl);
    auto rightCurlPos = FlatPosCreateHelper(propDecl->rightCurlPos);
    auto fbPropDecl =
        NodeFormat::CreatePropDecl(builder, fbVarDecl, &colonPos, &leftCurlPos, fbGetters, fbSetters, &rightCurlPos);
    return NodeFormat::CreateDecl(builder, fbDeclBase, NodeFormat::AnyDecl_PROP_DECL, fbPropDecl.Union());
}

flatbuffers::Offset<NodeFormat::Decl> NodeWriter::SerializeTypeAliasDecl(AstDecl decl)
{
    auto typeAliasDecl = RawStaticCast<const TypeAliasDecl*>(decl);
    auto fbDeclBase = SerializeDeclBase(typeAliasDecl);
    auto assignPos = FlatPosCreateHelper(typeAliasDecl->assignPos);
    auto type = SerializeType(typeAliasDecl->type.get());
    auto fbTypeAliasDecl = NodeFormat::CreateTypeAliasDecl(builder, fbDeclBase, &assignPos, type);
    return NodeFormat::CreateDecl(builder, fbDeclBase, NodeFormat::AnyDecl_TYPE_ALIAS_DECL, fbTypeAliasDecl.Union());
}

flatbuffers::Offset<NodeFormat::Decl> NodeWriter::SerializeExtendDecl(AstDecl decl)
{
    auto extendDecl = RawStaticCast<const ExtendDecl*>(decl);
    auto fbDeclBase = SerializeDeclBase(extendDecl);
    auto extendedType = SerializeType(extendDecl->extendedType.get());
    auto upperBoundPos = FlatPosCreateHelper(extendDecl->upperBoundPos);
    auto interfaces =
        FlatVectorCreateHelper<NodeFormat::Type, Type, AstType>(extendDecl->inheritedTypes, &NodeWriter::SerializeType);
    auto wherePos = FlatPosCreateHelper(extendDecl->wherePos);
    auto leftCurlPos = FlatPosCreateHelper(extendDecl->leftCurlPos);
    auto members =
        FlatVectorCreateHelper<NodeFormat::Decl, Decl, AstDecl>(extendDecl->members, &NodeWriter::SerializeDecl);
    auto rightCurlPos = FlatPosCreateHelper(extendDecl->rightCurlPos);
    auto fbExtendDecl = NodeFormat::CreateExtendDecl(
        builder, fbDeclBase, extendedType, &upperBoundPos, interfaces, &wherePos, &leftCurlPos, members, &rightCurlPos);
    return NodeFormat::CreateDecl(builder, fbDeclBase, NodeFormat::AnyDecl_EXTEND_DECL, fbExtendDecl.Union());
}

flatbuffers::Offset<NodeFormat::Decl> NodeWriter::SerializeMacroExpandDecl(const Decl* decl)
{
    auto macroExpandDecl = RawStaticCast<const MacroExpandDecl*>(decl);
    auto fbMacroExpandDecl = SerializeMacroExpandDecl(macroExpandDecl);
    return NodeFormat::CreateDecl(
        builder, SerializeDeclBase(macroExpandDecl), NodeFormat::AnyDecl_MACRO_EXPAND_DECL, fbMacroExpandDecl.Union());
}

flatbuffers::Offset<NodeFormat::MacroExpandDecl> NodeWriter::SerializeMacroExpandDecl(
    const MacroExpandDecl* macroExpandDecl)
{
    if (macroExpandDecl == nullptr) {
        return flatbuffers::Offset<NodeFormat::MacroExpandDecl>();
    }
    auto base = SerializeDeclBase(macroExpandDecl);
    auto invocation = MacroInvocationCreateHelper(macroExpandDecl->invocation);
    return NodeFormat::CreateMacroExpandDecl(builder, base, invocation);
}

flatbuffers::Offset<NodeFormat::FuncParam> NodeWriter::SerializeMacroExpandParam(AstMacroExpandParam mep)
{
    auto macroExpandParam = RawStaticCast<const MacroExpandParam*>(mep);
    auto base = SerializeFuncParam(macroExpandParam);
    auto fbNodeBase = SerializeNodeBase(mep);
    auto fbVarDeclBase = SerializeVarDecl(mep.get());
    auto colonPos = FlatPosCreateHelper(mep->colonPos);
    auto fbExpr = SerializeExpr((mep->assignment).get());
    auto commaPos = FlatPosCreateHelper(mep->commaPos);
    auto notMarkPos = FlatPosCreateHelper(mep->notMarkPos);
    auto invocation = MacroInvocationCreateHelper(mep->invocation);
    auto mepNode = NodeFormat::CreateMacroExpandParam(builder, base, invocation);

    return NodeFormat::CreateFuncParam(builder, fbNodeBase, fbVarDeclBase, &colonPos, fbExpr, &commaPos,
        mep->isNamedParam, mep->isMemberParam, &notMarkPos, mep->hasLetOrVar, NodeFormat::MacroParam_MACRO_EXPAND_PARAM,
        mepNode.Union());
}

flatbuffers::Offset<NodeFormat::Annotation> NodeWriter::SerializeAnnotation(AstAnnotation annotation)
{
    if (annotation == nullptr) {
        return flatbuffers::Offset<NodeFormat::Annotation>();
    }
    auto base = SerializeNodeBase(annotation);
    auto isCompileTimeVisible = annotation->isCompileTimeVisible;
    auto kind = static_cast<uint16_t>(annotation->kind);
    auto identPos = FlatPosCreateHelper(annotation->identifier.Begin());
    auto identifier = builder.CreateString(annotation->identifier.Val());
    auto lsquarePos = FlatPosCreateHelper(annotation->lsquarePos);
    auto fbArgs = FlatVectorCreateHelper<NodeFormat::FuncArg, FuncArg, AstFuncArg>(
        annotation->args, &NodeWriter::SerializeFuncArg);
    auto rsquarePos = FlatPosCreateHelper(annotation->rsquarePos);
    auto overlowStrategy = builder.CreateString(OverflowStrategyName(annotation->overflowStrategy));
    std::vector<flatbuffers::Offset<flatbuffers::String>> vecAttrs;
    for (auto& item : annotation->attrs) {
        auto fbItem = builder.CreateString(item);
        vecAttrs.push_back(fbItem);
    }
    auto fbAttrs = builder.CreateVector(vecAttrs);
    auto condExpr = SerializeExpr(annotation->condExpr.get());
    return NodeFormat::CreateAnnotation(builder, base, kind, &identPos, identifier, fbArgs, overlowStrategy, fbAttrs,
        condExpr, &lsquarePos, &rsquarePos, isCompileTimeVisible);
}

flatbuffers::Offset<NodeFormat::Modifier> NodeWriter::SerializeModifier(AstModifier modifier)
{
    if (modifier == nullptr) {
        return flatbuffers::Offset<NodeFormat::Modifier>();
    }
    auto base = SerializeNodeBase(modifier);
    auto kind = static_cast<uint16_t>(modifier->modifier);
    auto isExplicit = modifier->isExplicit;
    return NodeFormat::CreateModifier(builder, base, kind, isExplicit);
}

std::vector<flatbuffers::Offset<NodeFormat::Token>> NodeWriter::TokensVectorCreateHelper(
    std::vector<Cangjie::Token> tokenVector)
{
    std::vector<flatbuffers::Offset<NodeFormat::Token>> vecToken;
    for (auto& token : tokenVector) {
        auto kind = static_cast<uint16_t>(token.kind);
        auto value = builder.CreateString(token.Value());
        auto pos = FlatPosCreateHelper(token.Begin());
        vecToken.push_back(NodeFormat::CreateToken(builder, kind, value, &pos));
    }
    return vecToken;
}

flatbuffers::Offset<NodeFormat::MacroInvocation> NodeWriter::MacroInvocationCreateHelper(
    const MacroInvocation& macroInvocation)
{
    auto fullName = builder.CreateString(macroInvocation.fullName);
    auto identifier = builder.CreateString(macroInvocation.identifier);

    auto identifierPos = FlatPosCreateHelper(macroInvocation.identifierPos);
    auto leftSquarePos = FlatPosCreateHelper(macroInvocation.leftSquarePos);
    auto rightSquarePos = FlatPosCreateHelper(macroInvocation.rightSquarePos);
    auto leftParenPos = FlatPosCreateHelper(macroInvocation.leftParenPos);
    auto rightParenPos = FlatPosCreateHelper(macroInvocation.rightParenPos);
    auto atPos = FlatPosCreateHelper(macroInvocation.atPos);
    auto attrsTokens = TokensVectorCreateHelper(macroInvocation.attrs);
    auto attrs = builder.CreateVector(attrsTokens);
    auto argsTokens = TokensVectorCreateHelper(macroInvocation.args);
    auto args = builder.CreateVector(argsTokens);

    auto decl = SerializeDecl(macroInvocation.decl.get());
    auto hasParenthesis = macroInvocation.hasParenthesis;
    auto isCompileTimeVisible = macroInvocation.isCompileTimeVisible;
    return NodeFormat::CreateMacroInvocation(builder, fullName, identifier, &identifierPos, &leftSquarePos,
        &rightSquarePos, &leftParenPos, &rightParenPos, &atPos, attrs, args, decl, hasParenthesis,
        isCompileTimeVisible);
}

uint8_t* NodeWriter::ExportNode()
{
    // Match the nodePtr to Expr or Decl.
    match (*nodePtr)(
        [&, this](const Expr& expr) {
            // Serialize Expr.
            auto fbExpr = SerializeExpr(&expr); // flatbuffers::Offset<NodeFormat::Expr>
            auto nodeBase = SerializeNodeBase(&expr);
            auto fbNode = NodeFormat::CreateNode(builder, nodeBase, NodeFormat::AnyNode_EXPR, fbExpr.Union());
            builder.Finish(fbNode);
        },
        [&, this](const Pattern& pattern) {
            // Serialize Pattern.
            auto fbPattern = SerializePattern(&pattern);
            auto nodeBase = SerializeNodeBase(&pattern);
            auto fbNode = NodeFormat::CreateNode(builder, nodeBase, NodeFormat::AnyNode_PATTERN, fbPattern.Union());
            builder.Finish(fbNode);
        },
        [&, this](const Decl& decl) {
            // Serialize Decl.
            auto fbDecl = SerializeDecl(&decl);
            auto nodeBase = SerializeNodeBase(&decl);
            auto fbNode = NodeFormat::CreateNode(builder, nodeBase, NodeFormat::AnyNode_DECL, fbDecl.Union());
            builder.Finish(fbNode);
        },
        [&, this](const Type& ty) {
            // Serialize Type.
            auto fbType = SerializeType(&ty);
            auto nodeBase = SerializeNodeBase(&ty);
            auto fbNode = NodeFormat::CreateNode(builder, nodeBase, NodeFormat::AnyNode_TYPE, fbType.Union());
            builder.Finish(fbNode);
        },
        [&, this](const File& file) {
            // Serialize File.
            auto fbFile = SerializeFile(&file);
            auto nodeBase = SerializeNodeBase(&file);
            auto fbNode = NodeFormat::CreateNode(builder, nodeBase, NodeFormat::AnyNode_FILE, fbFile.Union());
            builder.Finish(fbNode);
        },
        [&, this](const Annotation& annotation) {
            // Serialize Annotation.
            auto anno = SerializeAnnotation(&annotation);
            auto nodeBase = SerializeNodeBase(&annotation);
            auto fbNode = NodeFormat::CreateNode(builder, nodeBase, NodeFormat::AnyNode_ANNOTATION, anno.Union());
            builder.Finish(fbNode);
        },
        [&]() {
            // nodePtr not supported
            Errorln("nodePtr not supported type");
        });
    // Serialize data into buffer.
    // Flatbuffer maximum size is 2GB, for 32-bit signed offsets.
    uint32_t length = static_cast<uint32_t>(builder.GetSize());
    bufferData.resize(sizeof(uint32_t) + length); // Add length of buffer in front.
    uint8_t* buf = builder.GetBufferPointer();
    uint32_t bufferSize = length + static_cast<uint32_t>(sizeof(uint32_t));
    uint8_t* pBufferSize = reinterpret_cast<uint8_t*>(&bufferSize);
    (void)std::copy(pBufferSize, pBufferSize + sizeof(uint32_t), bufferData.begin());
    (void)std::copy(
        buf, buf + static_cast<size_t>(length), bufferData.begin() + static_cast<int32_t>(sizeof(uint32_t)));
    uint8_t* rawPtr = (uint8_t*)malloc(bufferData.size());
    if (rawPtr == nullptr) {
        Errorln("Memory Allocation Failed.");
        return rawPtr;
    }
    (void)std::copy_n(bufferData.begin(), bufferData.size(), rawPtr);
    return rawPtr;
}
