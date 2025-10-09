// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements feature parsing
 */

#include "ParserImpl.h"

using namespace Cangjie;
using namespace Cangjie::AST;

/**
 * featureId
 *      : Identifier (DOT Identifier)*
 *      ;
 *
 * featuresDirective
 *      : FEATURES NL* featureId
 *      (COMMA NL* featureId)*
 *      end+;
 */
void ParserImpl::ParseFeatureDirective(OwnedPtr<FeaturesDirective>& features)
{
    Skip(TokenKind::FEATURES);
    features = MakeOwned<FeaturesDirective>();
    features->begin = lastToken.Begin();
    bool hasError {false};
    while (!Seeing(TokenKind::END)) {
        if (!ParseFeatureId(features)) {
            DiagExpectedIdentifierFeatureDirective(features);
            hasError = true;
            break;
        }
        if (newlineSkipped || Seeing(TokenKind::SEMI) || Seeing(TokenKind::END)) { break; }
        if (!Skip(TokenKind::COMMA)) {
            DiagExpectedIdentifierFeatureDirective(features);
            hasError = true;
            break;
        }
        features->commaPoses.emplace_back(lastToken.Begin());
    }
    if (hasError) {
        features->EnableAttr(Attribute::IS_BROKEN);
        if (!newlineSkipped) {
            ConsumeUntilAny({TokenKind::NL, TokenKind::SEMI, TokenKind::END});
        }
    }
    features->end = features->content.empty() ? lastToken.End() : features->content.back().end;
}

bool ParserImpl::ParseFeatureId(OwnedPtr<FeaturesDirective>& features)
{
    FeatureId content;
    bool firstIter{true};
    bool noError{true};
    while (Skip(TokenKind::IDENTIFIER) || Skip(TokenKind::DOT)) {
        if (firstIter) {
            content.begin = lastToken.Begin();
            content.end = lastToken.End();
            if (lastToken.kind != TokenKind::IDENTIFIER) {
                content.EnableAttr(Attribute::IS_BROKEN);
                noError = false;
                break;
            }
            firstIter = false;
        }
        std::string current = lastToken.Value();
        if (lastToken.kind == TokenKind::IDENTIFIER) {
            content.identifiers.emplace_back(Identifier(current, lastToken.Begin(), lastToken.End()));
        } else {
            content.dotPoses.emplace_back(lastToken.Begin());
        }
        content.end = lastToken.End();
        if (Seeing(lastToken.kind) || IsRawIdentifier(current)) {
            content.EnableAttr(Attribute::IS_BROKEN);
            noError = false;
            break;
        }
    }
    if (lastToken.kind != TokenKind::IDENTIFIER) { noError = false; }
    if (!content.identifiers.empty()) {
        features->content.emplace_back(content);
    }
    return noError;
}
