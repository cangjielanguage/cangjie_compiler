// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements imports parsing.
 */

#include "ParserImpl.h"

#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Match.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace Cangjie::AST;
using namespace Cangjie::Utils;

void ParserImpl::ParseCommonImportSpec(PtrVector<ImportSpec>& imports, PtrVector<Annotation>& annos)
{
    while (SeeingImport() || (SeeingBuiltinAnnotation())) {
        if (SeeingBuiltinAnnotation()) {
            ParseAnnotations(annos);
        }
        if (!SeeingImport()) {
            break;
        }
        ParseImportSpec(imports, annos);
        annos.clear();
        /**
         * importList
         *     : (FROM NL* Identifier)? NL* IMPORT NL* importAllOrSpecified
         *     (NL* COMMA NL* importAllOrSpecified)* end+
         *     ;
         * */
        if (!newlineSkipped) {
            if (Seeing(TokenKind::END)) {
                break;
            }
            if (!Skip(TokenKind::SEMI)) {
                DiagExpectSemiOrNewline();
                ConsumeUntilAny({TokenKind::NL, TokenKind::SEMI});
            }
        }
        SkipBlank(TokenKind::SEMI);
    }
}

void ParserImpl::CheckImportSpec(PtrVector<ImportSpec>& imports)
{
    for (auto& import : imports) {
        if (import->TestAttr(Attribute::COMPILER_ADD)) {
            continue;
        }

        const auto& content = import->content;
        if (content.kind != ImportKind::IMPORT_MULTI) {
            auto fullPackageName = content.GetImportedPackageName();
            if (!content.TestAttr(Attribute::IS_BROKEN) && fullPackageName.size() > PACKAGE_NAME_LEN_LIMIT) {
                auto packageNameBeginPos =
                    content.prefixPaths.empty() ? content.identifier.Begin() : content.prefixPoses[0];
                DiagNameLengthOverflow(MakeRange(packageNameBeginPos, content.end), fullPackageName,
                    PACKAGE_NAME_LEN_LIMIT, fullPackageName.size());
            }
            continue;
        }
        auto commonPrefix = content.GetImportedPackageName();
        for (const auto& item : content.items) {
            auto suffix = item.GetImportedPackageName();
            auto fullPackageName = commonPrefix.empty() ? suffix : commonPrefix + "." + suffix;
            if (!item.TestAttr(Attribute::IS_BROKEN) && fullPackageName.size() > PACKAGE_NAME_LEN_LIMIT) {
                auto packageNameBeginPos = content.prefixPaths.empty()
                    ? item.prefixPaths.empty() ? item.identifier.Begin() : item.prefixPoses[0]
                    : content.prefixPoses[0];
                DiagNameLengthOverflow(MakeRange(packageNameBeginPos, content.end), fullPackageName,
                    PACKAGE_NAME_LEN_LIMIT, fullPackageName.size());
            }
        }
    }
}

void ParserImpl::ParseImportSpecInTop(PtrVector<ImportSpec>& imports, PtrVector<Annotation>& annos)
{
    ParseCommonImportSpec(imports, annos);
    CheckImportSpec(imports);
}

void ParserImpl::ParseImportSpec(PtrVector<ImportSpec>& imports, const PtrVector<Annotation>& annos)
{
    auto importSpec = MakeOwned<ImportSpec>();
    if (SeeingAny({TokenKind::PUBLIC, TokenKind::PROTECTED, TokenKind::INTERNAL, TokenKind::PRIVATE})) {
        Next();
        auto modifier = MakeOwned<Modifier>(lastToken.kind, lastToken.Begin());
        modifier->end = lastToken.End();
        importSpec->modifier = std::move(modifier);
    }
    Skip(TokenKind::IMPORT);
    importSpec->EnableAttr(GetModifierAttr(importSpec->modifier, ASTKind::IMPORT_SPEC));
    importSpec->begin = importSpec->modifier ? importSpec->modifier->begin : lastToken.Begin();
    importSpec->importPos = lastToken.Begin();
    ParseImportContent(importSpec->content);
    importSpec->end = importSpec->content.end;

    // Check allowed annotation on import.
    std::for_each(annos.begin(), annos.end(), [&, this](const Ptr<Annotation>& anno) {
        if (NotIn(anno->kind, {AnnotationKind::WHEN})) {
            DiagUnexpectedAnnoOn(*anno, importSpec->importPos, anno->identifier, "import");
        }
    });

    importSpec->annotations = ASTCloner::CloneVector(annos);

    if (auto& import = imports.emplace_back(std::move(importSpec)); import->IsImportMulti()) {
        DesugarImportMulti(imports, *import);
    }
}

void ParserImpl::DesugarImportMulti(PtrVector<ImportSpec>& imports, ImportSpec& import) const
{
    CJC_ASSERT(import.IsImportMulti());
    // For lsp auto completion, desugar as much as possible
    const auto& items = import.content.items;
    if (std::find_if(items.begin(), items.end(),
            [](auto& it) { return it.TestAnyAttr(Attribute::IS_BROKEN, Attribute::HAS_BROKEN); }) != items.end()) {
        import.EnableAttr(Attribute::HAS_BROKEN);
    }
    for (auto& item : items) {
        auto desugaredImport = MakeOwned<ImportSpec>();
        CopyBasicInfo(&item, desugaredImport.get());

        // Copy modifier
        if (import.modifier) {
            desugaredImport->modifier = MakeOwned<Modifier>(import.modifier->modifier, import.modifier->begin);
            CopyBasicInfo(import.modifier.get(), desugaredImport->modifier.get());
            desugaredImport->modifier->isExplicit = import.modifier->isExplicit;
        }

        desugaredImport->importPos = import.importPos;
        desugaredImport->annotations = ASTCloner::CloneVector(import.annotations);

        CopyBasicInfo(&import.content, &desugaredImport->content);
        desugaredImport->content.end = item.end;
        desugaredImport->content.kind = item.kind;
        desugaredImport->content.prefixPaths = import.content.prefixPaths;
        desugaredImport->content.prefixPoses = import.content.prefixPoses;
        desugaredImport->content.prefixDotPoses = import.content.prefixDotPoses;
        desugaredImport->content.hasDoubleColon = import.content.hasDoubleColon;

        desugaredImport->content.prefixPaths.insert(
            desugaredImport->content.prefixPaths.end(), item.prefixPaths.begin(), item.prefixPaths.end());
        desugaredImport->content.prefixPoses.insert(
            desugaredImport->content.prefixPoses.end(), item.prefixPoses.begin(), item.prefixPoses.end());
        desugaredImport->content.prefixDotPoses.insert(
            desugaredImport->content.prefixDotPoses.end(), item.prefixDotPoses.begin(), item.prefixDotPoses.end());

        desugaredImport->content.identifier = item.identifier;
        desugaredImport->content.asPos = item.asPos;
        desugaredImport->content.aliasName = item.aliasName;

        desugaredImport->EnableAttr(Attribute::COMPILER_ADD, GetModifierAttr(import.modifier, ASTKind::IMPORT_SPEC));
        imports.emplace_back(std::move(desugaredImport));
    }
}

void ParserImpl::ParseImportContent(ImportContent& content)
{
    if (!ParseImportSingle(content)) {
        ParseImportMulti(content);
    }

    // Parse import-alias
    if (content.kind == ImportKind::IMPORT_SINGLE && Skip(TokenKind::AS)) {
        ParseImportAliasPart(content);
    }
}

bool ParserImpl::ParseImportSingle(ImportContent& content, bool inMultiImport)
{
    SrcIdentifier curIdent;
    bool firstIter{true};
    bool skipDc{false};
    do {
        skipDc = false;
        if (!curIdent.Empty()) {
            content.prefixPaths.emplace_back(curIdent.Val());
            content.prefixPoses.emplace_back(curIdent.Begin());
            CJC_ASSERT(!curIdent.Begin().IsZero());
            content.prefixDotPoses.emplace_back(lastToken.Begin());
        }

        if (!inMultiImport && Skip(TokenKind::LCURL)) {
            if (firstIter) {
                content.begin = lastToken.Begin();
            }
            content.leftCurlPos = lastToken.Begin();
            return false;
        }

        // Parse import-all
        if (!firstIter && Skip(TokenKind::MUL)) {
            curIdent = "*";
            curIdent.SetPos(lastToken.Begin(), lastToken.End());
            break;
        }

        // Parse import-single
        curIdent = ExpectPackageIdentWithPos(content);
        if (curIdent == INVALID_IDENTIFIER) {
            if (inMultiImport) {
                ConsumeUntilAny({TokenKind::NL, TokenKind::COMMA, TokenKind::RCURL});
                if (lastToken.kind == TokenKind::RCURL) {
                    content.rightCurlPos = lastToken.Begin();
                }
            } else {
                ConsumeUntilAny({TokenKind::NL, TokenKind::COMMA});
            }
            content.EnableAttr(Attribute::IS_BROKEN);
            break;
        }
        if (firstIter) {
            content.begin = curIdent.Begin();
            firstIter = false;
        }
        if (content.prefixPaths.empty() && Skip(TokenKind::DOUBLE_COLON)) {
            content.hasDoubleColon = true;
            skipDc = true;
        }
    } while (skipDc || Skip(TokenKind::DOT));

    content.kind = curIdent == "*" ? ImportKind::IMPORT_ALL : ImportKind::IMPORT_SINGLE;

    if (lastToken.kind == TokenKind::PACKAGE_IDENTIFIER) {
        DiagExpectedIdentifier(MakeRange(curIdent.Begin(), curIdent.End()));
        content.EnableAttr(Attribute::IS_BROKEN);
    }
    content.identifier = std::move(curIdent);
    content.end = curIdent.End();

    return true;
}

void ParserImpl::ParseImportMulti(ImportContent& content)
{
    content.kind = ImportKind::IMPORT_MULTI;

    bool firstIter{true};
    do {
        if (!firstIter) {
            bool skippedComma = Skip(TokenKind::COMMA);
            if (skippedComma) {
                content.commaPoses.emplace_back(lastToken.Begin());
            }
            // Import-multi should contain at least one item.
            if (Skip(TokenKind::RCURL)) {
                content.rightCurlPos = lastToken.Begin();
                content.end = lastToken.End();
                return;
            }
            if (!skippedComma) {
                DiagExpectCharacter(lastToken.End(), "','");
                content.EnableAttr(Attribute::IS_BROKEN);
            }
        }
        firstIter = false;

        ImportContent item;
        ParseImportSingle(item, true);
        if (item.kind == ImportKind::IMPORT_SINGLE && Skip(TokenKind::AS)) {
            ParseImportAliasPart(item);
        }
        if (item.TestAttr(Attribute::IS_BROKEN)) {
            content.EnableAttr(Attribute::HAS_BROKEN);
        }
        content.items.emplace_back(std::move(item));
    } while (SeeingAny({TokenKind::COMMA, TokenKind::IDENTIFIER, TokenKind::PACKAGE_IDENTIFIER, TokenKind::AS}));

    if (lastToken.kind != TokenKind::RCURL && !Skip(TokenKind::RCURL)) {
        DiagExpectedRightDelimiter("{", content.leftCurlPos);
        content.EnableAttr(Attribute::IS_BROKEN);
        content.end = lastToken.End();
    } else {
        content.rightCurlPos = lastToken.Begin();
        auto lastItem = content.items.rbegin();
        CJC_ASSERT(lastItem != content.items.rend());
        content.end = lastItem->end;
    }
}

void ParserImpl::ParseImportAliasPart(ImportContent& content)
{
    content.kind = ImportKind::IMPORT_ALIAS;
    content.asPos = lastToken.Begin();
    auto curIdent = ExpectIdentifierWithPos(content);
    if (curIdent == INVALID_IDENTIFIER) {
        ConsumeUntilAny({TokenKind::NL, TokenKind::COMMA});
        content.EnableAttr(Attribute::IS_BROKEN);
    } else {
        content.aliasName = std::move(curIdent);
        content.end = lastToken.End();
    }
}
