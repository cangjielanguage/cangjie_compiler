// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

#include "cangjie/CHIR/Utils/Utils.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

void Translator::HandleVarWithVarPattern(
    const AST::VarPattern& pattern, const Ptr<Value>& initNode, bool isLocalPattern)
{
    auto varDecl = pattern.varDecl.get();
    Ptr<Value> leftValue = TranslateLeftValueOfVarDecl(*varDecl, initNode == nullptr, isLocalPattern);
    if (initNode != nullptr) {
        StoreRValueToLValue(*varDecl, *initNode, leftValue);
        if (leftValue->IsGlobalVarInCurPackage()) {
            CJC_ASSERT(GetTopLevelFunc(*initNode));
            VirtualCast<GlobalVar*>(leftValue)->SetInitFunc(*GetTopLevelFunc(*initNode));
        }
    }
    SetSymbolTable(*varDecl, *leftValue);
}

void Translator::HandleVarWithTupleAndEnumPattern(const AST::Pattern& pattern,
    const std::vector<OwnedPtr<AST::Pattern>>& subPatterns, const Ptr<Value>& initNode, bool isLocalPattern)
{
    auto leftType = TranslateType(*(pattern.ty));
    Ptr<Value> leftValue = nullptr;
    if (initNode != nullptr) {
        leftValue = GetDerefedValue(initNode);
        leftValue = TypeCastOrBoxIfNeeded(*initNode, *leftType, leftValue->GetDebugLocation());
        SetSymbolTable(pattern, *leftValue, isLocalPattern);
    }
    bool isEnumPattern{pattern.astKind == AST::ASTKind::ENUM_PATTERN};
    for (size_t i = 0; i < subPatterns.size(); i++) {
        Ptr<Value> rVal = nullptr;
        if (leftValue != nullptr) {
            auto fieldType = TranslateType(*(subPatterns[i]->ty));
            auto fieldIndex = isEnumPattern ? i + 1 : i; // add 1 to index when base type is enum
            std::vector<uint64_t> path{fieldIndex};
            auto baseValue = isEnumPattern ? CastEnumValueToConstructorTupleType(
                leftValue, StaticCast<AST::EnumPattern&>(pattern)) : leftValue;
            rVal = CreateAndAppendExpression<Field>(fieldType, baseValue, std::move(path), currentBlock)->GetResult();
        }
        FlattenVarWithPatternDecl(*subPatterns[i], rVal, isLocalPattern);
    }
}

void Translator::FlattenVarWithPatternDecl(const AST::Pattern& pattern, const Ptr<Value>& target, bool isLocalPattern)
{
    switch (pattern.astKind) {
        case AST::ASTKind::VAR_PATTERN: {
            auto varPattern = StaticCast<const AST::VarPattern*>(&pattern);
            HandleVarWithVarPattern(*varPattern, target, isLocalPattern);
            break;
        }
        case AST::ASTKind::TUPLE_PATTERN: {
            auto tuplePattern = StaticCast<const AST::TuplePattern*>(&pattern);
            HandleVarWithTupleAndEnumPattern(*tuplePattern, tuplePattern->patterns, target, isLocalPattern);
            break;
        }
        case AST::ASTKind::ENUM_PATTERN: {
            auto enumPattern = StaticCast<const AST::EnumPattern*>(&pattern);
            HandleVarWithTupleAndEnumPattern(*enumPattern, enumPattern->patterns, target, isLocalPattern);
            break;
        }
        case AST::ASTKind::WILDCARD_PATTERN: {
            break;
        }
        default: {
            Errorln("decl with unsupported pattern");
            CJC_ABORT();
        }
    }
}

Ptr<Value> Translator::Visit(const AST::VarWithPatternDecl& patternDecl)
{
    CJC_ASSERT(!patternDecl.TestAttr(AST::Attribute::GLOBAL));

    // The local const var is lifted as global variable, thus not handled here
    if (localConstVars.HasElement(&patternDecl)) {
        return nullptr;
    }

    Ptr<Value> initNode = nullptr;
    if (patternDecl.initializer.get()) {
        initNode = TranslateExprArg(*patternDecl.initializer);
        initNode->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
    }
    auto pattern = patternDecl.irrefutablePattern.get();
    FlattenVarWithPatternDecl(*pattern, initNode, true);

    return nullptr;
}