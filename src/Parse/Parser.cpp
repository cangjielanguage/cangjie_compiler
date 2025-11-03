// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Parser.
 */

#include "ParserImpl.h"

#include "cangjie/AST/Clone.h"
#include "cangjie/AST/Create.h"
#include "cangjie/AST/Match.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/Lex/Token.h"
#include "cangjie/Utils/FileUtil.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie;
using namespace Cangjie::AST;

Parser::Parser(unsigned int fileID, const std::string& input, DiagnosticEngine& diag, SourceManager& sm,
    bool attachComment, bool parseDeclFile)
    : impl{new ParserImpl{fileID, input, diag, sm, attachComment, parseDeclFile}}
{
}

Parser::Parser(const std::string& input, DiagnosticEngine& diag, SourceManager& sm, const Position& pos,
    bool attachComment, bool parseDeclFile)
    : impl{new ParserImpl{input, diag, sm, pos, attachComment, parseDeclFile}}
{
}

Parser::Parser(const std::vector<Token>& inputTokens, DiagnosticEngine& diag, SourceManager& sm, bool attachComment,
    bool parseDeclFile)
    : impl{new ParserImpl{inputTokens, diag, sm, attachComment, parseDeclFile}}
{
}

uint8_t ParserImpl::Precedence(TokenKind kind) const
{
    auto i = static_cast<uint8_t>(kind);
    CJC_ASSERT(i < NUM_TOKENS);
    return TOKEN_TO_OPERATOR_PRECEDENCE[i];
}

bool ParserImpl::ParsePackageHeaderEnd()
{
    bool seeingFileEnd = false;
    if (newlineSkipped) {
        return seeingFileEnd;
    }
    if (Seeing(TokenKind::END)) {
        seeingFileEnd = true;
    } else if (!Skip(TokenKind::SEMI)) {
        DiagExpectSemiOrNewline();
    }
    return seeingFileEnd;
}

void ParserImpl::CheckAndHandleUnexpectedTopLevelDeclAfterFeatures()
{
    if (!SeeingPackage() && !SeeingMacroPackage() && !SeeingImport() && !Seeing(TokenKind::SEMI) &&
        !SeeingDecl() && !SeeingMacroCallDecl() && !SeeingInitializer() && !SeeingFinalizer() &&
        !Seeing(TokenKind::END)) {
        DiagAndSuggestKeywordForExpectedDeclaration({"features", "macro", "package", "import", "func", "let", "var",
            "const", "enum", "type", "struct", "class", "interface", "extend", "main"});
        auto consumeTarget = [this]() {
            return SeeingPackage() || SeeingMacroPackage() || Seeing(TokenKind::SEMI) || SeeingImport() ||
                SeeingDecl() || SeeingMacroCallDecl() || SeeingInitializer() || SeeingFinalizer();
        };
        ConsumeUntilAny(consumeTarget, false);
    }
}

void ParserImpl::CheckExpectedTopLevelDeclWhenNoPackage()
{
    bool maybeConstDecl = lastToken.kind == TokenKind::CONST;
    bool maybeForeignBlock = lastToken.kind == TokenKind::FOREIGN && Seeing(TokenKind::LCURL);
    if (!SeeingImport() && !Seeing(TokenKind::SEMI) && !SeeingDecl() && !SeeingMacroCallDecl() && !maybeConstDecl &&
        !maybeForeignBlock && !SeeingInitializer() && !SeeingFinalizer() && !Seeing(TokenKind::END)) {
        DiagAndSuggestKeywordForExpectedDeclaration({"macro", "package", "import", "func", "let", "var", "const",
            "enum", "type", "struct", "class", "interface", "extend", "main"});
    }
}

size_t ParserImpl::GetLineNum() const
{
    std::unordered_set<int64_t> lines;
    for (auto& item : allTokensInOneFile) {
        for (int64_t i = item.first; i <= item.second; ++i) {
            lines.emplace(i);
        }
    }
    return lines.size();
}

OwnedPtr<File> ParserImpl::ParseTopLevel()
{
    OwnedPtr<File> ret = MakeOwned<File>();
    currentFile = ret.get();
    Peek();
    // Set the begin of file at (fileID, 1, 1).
    ret->begin = {lexer->GetFileID(), 1, 1};
    Source& source = sourceManager.GetSource(ret->begin.fileID);
    ret->fileName = FileUtil::GetFileName(source.path);
    ret->filePath = source.path;
    ret->fileHash = source.fileHash;
    SkipBlank(TokenKind::NL);
     /**
     * preamble
     *  : featureSpec? packageHeader? importSpec*
     *  ;
    */
    // Parse features in TopLevel
    if (SeeingFeatures()) {
        ParseFeatureDirective(ret->feature);
    } else {
        CheckAndHandleUnexpectedTopLevelDeclAfterFeatures();
    }
    PtrVector<Annotation> annos;
    if (SeeingBuiltinAnnotation()) {
        ParseAnnotations(annos);
    }
    // mark a reset point
    ParserScope scope(*this);
    std::set<Modifier> modifiers;
    ParseModifiers(modifiers);
    if (SeeingPackage() || SeeingMacroPackage()) {
        ret->package = ParsePackageHeader(std::move(modifiers));
        /**
         * packageHeader
         *     : packageModifier? PACKAGE NL* (packageName NL* DOT NL*)* packageName end+
         *     ;
         */
        if (ParsePackageHeaderEnd()) {
            // seeing end of file
            ret->end = lastToken.End();
            ret->curFile = ret;
            return ret;
        }
        SkipBlank(TokenKind::SEMI);
        if (!annos.empty()) {
            DiagUnexpectedAnnoOn(*annos.front(), ret->package->packagePos, annos.front()->identifier, "package");
            annos.clear();
        }
    } else {
        CheckExpectedTopLevelDeclWhenNoPackage();
        scope.ResetParserScope();
    }
    // Parse importSpec in TopLevel.
    ParseImportSpecInTop(ret->imports, annos);
    if (!scanDepPkg) {
        ParseTopLevelDecls(*ret, annos);
    }
    ret->end = lookahead.End();
    if (ret->end.IsZero()) {
        ret->end = ret->begin;
    }
    if (enableAttachComment) {
        AttachCommentToFile(ret.get());
    }
    commentsMap.emplace(std::make_pair(ret->begin.fileID, lexer->GetComments()));
    // Assign this->currentFile to curFile of every node except `Package` and `MacroExpandExpr's Modifier`
    AssignCurFile(ret);
    return ret;
}

void ParserImpl::AssignCurFile(const OwnedPtr<AST::File>& file) const
{
    file->curFile = file.get();
    auto visitPre = [&file](Ptr<Node> curNode) -> VisitAction {
        curNode->curFile = file.get();
        return VisitAction::WALK_CHILDREN;
    };

    Walker curFileAssignWalker(file.get(), visitPre);
    curFileAssignWalker.Walk();
}

void ParserImpl::ParseTopLevelDecls(File& file, PtrVector<Annotation>& annos)
{
    int toplevelCount = 0;
    while (true) {
        if (!newlineSkipped) {
            if (Seeing(TokenKind::END)) {
                break;
            }
        }
        if (toplevelCount >= 1) {
            /**
             * translationUnit
             *     : NL* preamble end* (topLevelObject (end+ topLevelObject?)*)? EOF
             *     ;
             * */
            if (!newlineSkipped && !Skip(TokenKind::SEMI)) {
                DiagExpectSemiOrNewline();
            }
            SkipBlank(TokenKind::SEMI);
        }
        if (DetectPrematureEnd()) {
            break;
        }
        ParseAnnotations(annos);
        ParseTopLevelDecl(file, annos);
        annos.clear();
        bracketsStack.clear();
        toplevelCount++;
    }
}

void ParserImpl::ParseTopLevelDecl(File& file, PtrVector<Annotation>& annos)
{
    // Foreign block.
    std::set<Modifier> modifiers;
    ParseModifiers(modifiers);
    if (HasModifier(modifiers, TokenKind::FOREIGN)) {
        auto decls = ParseForeignDecls(modifiers, annos);
        if (!decls.empty()) {
            std::move(std::begin(decls), std::end(decls), std::back_inserter(file.decls));
        }
        return;
    }

    auto decl = ParseDecl(ScopeKind::TOPLEVEL, modifiers, std::move(annos));
    if (currentFile && currentFile->package) {
        if (HasModifier(decl->modifiers, TokenKind::PUBLIC) && currentFile->package->hasMacro &&
            decl->astKind != AST::ASTKind::MACRO_DECL) {
            DiagDeclarationInMacroPackage(decl);
        }
    }
    file.decls.emplace_back(std::move(decl));
}

OwnedPtr<PackageSpec> ParserImpl::ParsePackageHeader(std::set<Modifier>&& modifiers)
{
    auto packageHeader = MakeOwned<PackageSpec>();

    if (!modifiers.empty()) {
        const auto firstModifier = SortModifierByPos(modifiers)[0];
        auto modifier = MakeOwned<Modifier>(firstModifier->modifier, firstModifier->begin);
        packageHeader->modifier = std::move(modifier);
    }
    packageHeader->EnableAttr(GetModifierAttr(packageHeader->modifier, ASTKind::PACKAGE_SPEC));
    if (Skip(TokenKind::MACRO)) {
        packageHeader->macroPos = lastToken.Begin();
        packageHeader->hasMacro = true;
    }

    Skip(TokenKind::PACKAGE);
    packageHeader->begin = packageHeader->modifier ? packageHeader->modifier->begin
        : packageHeader->hasMacro                  ? packageHeader->macroPos
                                                   : lastToken.Begin();
    packageHeader->packagePos = lastToken.Begin();

    ChainScope cs(*this, packageHeader.get());
    SrcIdentifier curIdent;
    bool skipDc{false};
    do {
        skipDc = false;
        if (!curIdent.Empty()) {
            packageHeader->prefixPaths.emplace_back(curIdent.Val());
            packageHeader->prefixPoses.emplace_back(curIdent.Begin());
            packageHeader->prefixDotPoses.emplace_back(lastToken.Begin());
        }
        curIdent = ExpectPackageIdentWithPos(*packageHeader);
        if (curIdent == INVALID_IDENTIFIER || curIdent.IsRaw()) {
            ConsumeUntilAny({TokenKind::NL});
            packageHeader->EnableAttr(Attribute::IS_BROKEN);
            if (curIdent.IsRaw()) {
                // If isRawId is true, curIdentPos has been added 1, so we decrease 1 here to get the original position.
                ParseDiagnoseRefactor(DiagKindRefactor::parse_package_name_has_backtick,
                    MakeRange(curIdent.Begin() - 1, curIdent.End() + 1));
            }
            break;
        }
        if (packageHeader->prefixPaths.empty() && Skip(TokenKind::DOUBLE_COLON)) {
            packageHeader->hasDoubleColon = true;
            skipDc = true;
        }
    } while (skipDc || Skip(TokenKind::DOT));
    packageHeader->packageName = std::move(curIdent);
    packageHeader->end = curIdent.End();
    (void)CheckDeclModifiers(modifiers, ScopeKind::TOPLEVEL, DefKind::PACKAGE);
    auto fullPackageName = packageHeader->GetPackageName();
    if (!packageHeader->TestAttr(Attribute::IS_BROKEN) && fullPackageName.size() > PACKAGE_NAME_LEN_LIMIT) {
        auto packageNameBeginPos =
            packageHeader->prefixPaths.empty() ? curIdent.Begin() : packageHeader->prefixPoses[0];
        DiagNameLengthOverflow(MakeRange(packageNameBeginPos, curIdent.End()), fullPackageName, PACKAGE_NAME_LEN_LIMIT,
            fullPackageName.size());
    }

    if (!packageHeader->prefixPaths.empty()) {
        // NOTE: will only used for 'std' module.
        this->moduleName = packageHeader->prefixPaths[0];
    }
    return packageHeader;
}

std::vector<OwnedPtr<Decl>> ParserImpl::ParseForeignDecls(
    const std::set<Modifier>& modifiers, PtrVector<Annotation>& annos)
{
    std::vector<OwnedPtr<Decl>> ret;
    if (!Skip(TokenKind::LCURL)) {
        // Single foreign declaration.
        auto decl = ParseDecl(ScopeKind::TOPLEVEL, modifiers, std::move(annos));
        ret.emplace_back(std::move(decl));
        return ret;
    }
    CheckNoDeprecatedAnno(annos, "foreign block");
    Position leftCurlyBracePos = lastToken.Begin();
    SkipBlank(TokenKind::SEMI);
    inForeignBlock = true;
    for (;;) {
        if (Skip(TokenKind::RCURL)) {
            break;
        }
        if (DetectPrematureEnd()) {
            DiagExpectedRightDelimiter("{", leftCurlyBracePos);
            break;
        }
        auto clonedMods = modifiers;
        // Copy annos here, because ParseDecl(xxx) will move annos.
        PtrVector<Annotation> clonedAnnos;
        for (auto& it : annos) {
            (void)clonedAnnos.emplace_back(ASTCloner::Clone(it.get()));
        }
        ParseAnnotations(clonedAnnos);
        auto decl = ParseDecl(ScopeKind::TOPLEVEL, clonedMods, std::move(clonedAnnos));

        ret.emplace_back(std::move(decl));
        SkipBlank(TokenKind::SEMI);
    }
    inForeignBlock = false;
    return ret;
}

void ParserImpl::CheckNoDeprecatedAnno(const PtrVector<Annotation>& annos, const std::string& invalidTarget)
{
    for (auto& anno : annos) {
        if (anno->kind == AnnotationKind::DEPRECATED) {
            DiagDeprecatedInvalidTarget(*anno, invalidTarget);
        }
    }
}

void ParserImpl::CheckDeprecationOfFuncParam(const FuncParam& param)
{
    for (auto& anno : param.annotations) {
        if (anno->kind != AnnotationKind::DEPRECATED) {
            continue;
        }

        if (!param.isNamedParam) {
            DiagDeprecatedInvalidTarget(*anno, "Not named parameter");
        }

        bool hasDefaultValue = param.assignment;
        if (!hasDefaultValue) {
            DiagDeprecatedInvalidTarget(*anno, "Parameter without default value");
        }
    }
}

OwnedPtr<Decl> ParserImpl::ParsePropDecl(
    ScopeKind scopeKind, const std::set<Modifier>& modifiers, std::vector<OwnedPtr<AST::Annotation>> annos)
{
    Next();
    OwnedPtr<PropDecl> ret = MakeOwned<PropDecl>();
    ret->begin = lookahead.Begin();
    ret->keywordPos = lookahead.Begin();
    ChainScope cs(*this, ret.get());
    CheckDeclarationInScope(scopeKind, DefKind::PROPERTY);
    ret->identifier = ExpectIdentifierWithPos(*ret);
    if (ret->identifier == INVALID_IDENTIFIER) {
        ret->EnableAttr(Attribute::IS_BROKEN);
        ret->end = lastToken.End();
        ConsumeUntilDecl(TokenKind::RCURL);
        return ret;
    }
    ret->end = lookahead.End();
    // PropDecl must declare type explicitly, otherwise we report error during Parsing.
    if (Skip(TokenKind::COLON)) {
        ret->colonPos = lastToken.Begin();
        ret->type = ParseType();
    } else {
        ret->type = MakeOwned<InvalidType>(lookahead.Begin());
        DiagExpectCharacter("':'", "property declaration must declare type explicitly");
    }
    auto attrs = CheckDeclModifiers(modifiers, scopeKind, DefKind::PROPERTY);
    std::for_each(attrs.begin(), attrs.end(), [&](auto&& it) { ret->EnableAttr(it); });
    if (ret->TestAttr(Attribute::MUT)) {
        ret->isVar = true;
    }
    ret->modifiers.insert(modifiers.begin(), modifiers.end());
    auto inClassLikeScope = scopeKind == ScopeKind::CLASS_BODY || scopeKind == ScopeKind::INTERFACE_BODY;
    if (Skip(TokenKind::LCURL)) {
        ParsePropBody(modifiers, *ret);
    } else if (inClassLikeScope || scopeKind == ScopeKind::UNKNOWN_SCOPE || ret->TestAttr(Attribute::COMMON)) {
        if (CanBeAbstract(*ret, scopeKind)) {
            ret->EnableAttr(Attribute::ABSTRACT);
        }
        if (ret->type) {
            ret->end = ret->type->end;
        }
    } else {
        DiagMissingPropertyBody(*ret);
        ret->end = lastToken.End();
    }
    ret->annotations = std::move(annos);
    SetDefaultFunc(scopeKind, *ret);
    return ret;
}

void ParserImpl::CheckClassLikePropAbstractness(AST::PropDecl& prop)
{
    CJC_NULLPTR_CHECK(prop.outerDecl);
    if (prop.outerDecl->TestAnyAttr(Attribute::JAVA_MIRROR)) {
        prop.DisableAttr(Attribute::ABSTRACT);
        return;
    }
    bool isJavaMirrorOrJavaMirrorSubtype =
        prop.outerDecl->TestAnyAttr(Attribute::JAVA_MIRROR, Attribute::JAVA_MIRROR_SUBTYPE);
    bool isCommon = prop.TestAttr(Attribute::COMMON);
    auto outerModifiers = prop.outerDecl->modifiers;
    bool inAbstract = HasModifier(outerModifiers, TokenKind::ABSTRACT);
    bool inCJMP = HasModifier(outerModifiers, TokenKind::PLATFORM) || HasModifier(outerModifiers, TokenKind::COMMON);
    bool inAbstractCJMP = inAbstract && inCJMP;
    bool inClass = prop.outerDecl->astKind == ASTKind::CLASS_DECL;
    bool inAbstractCJMPClass = inAbstractCJMP && inClass;
    bool inObjCMirror = prop.outerDecl->TestAttr(Attribute::OBJ_C_MIRROR);

    if (HasModifier(prop.modifiers, TokenKind::ABSTRACT) && !isCommon && !inAbstractCJMP &&
        !isJavaMirrorOrJavaMirrorSubtype) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_explicitly_abstract_only_for_cjmp_abstract_class,
            lastToken.End(), "property");
    }

    if (inObjCMirror) {
        prop.DisableAttr(Attribute::ABSTRACT);
        return;
    }


    if (inAbstractCJMPClass && prop.TestAttr(Attribute::ABSTRACT) &&
        !HasModifier(prop.modifiers, TokenKind::ABSTRACT)) {
        prop.DisableAttr(Attribute::ABSTRACT);
        return;
    }

    // `abstract` modifier valid only in COMMON ABSTRACT class
    if (HasModifier(prop.modifiers, TokenKind::ABSTRACT) && !inAbstractCJMPClass) {
        Ptr<const Modifier> abstractMod = nullptr;
        for (auto& modifier : prop.modifiers) {
            if (modifier.modifier == TokenKind::ABSTRACT) {
                abstractMod = Ptr(&modifier);
            }
        }
        CJC_NULLPTR_CHECK(abstractMod);
        ChainScope cs(*this, Ptr(&prop));
        DiagIllegalModifierInScope(*abstractMod);
    }
}

void ParserImpl::DiagMissingPropertyBody(AST::PropDecl& prop)
{
    if (parseDeclFile) {
        return;
    }
    auto builder =
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_character, lookahead, "'{'", ConvertToken(lookahead));
    builder.AddMainHintArguments("'{'");
    builder.AddNote("property declaration must have body");
    prop.EnableAttr(Attribute::HAS_BROKEN);
}

void ParserImpl::ParseExtendedType(ExtendDecl& extendDecl)
{
    if (Skip(TokenKind::LT)) {
        extendDecl.EnableAttr(Attribute::GENERIC);
        extendDecl.generic = ParseGeneric();
    }
    extendDecl.extendedType = ParseType();
    if (extendDecl.extendedType->astKind == ASTKind::PAREN_TYPE) {
        DiagUnexpectedTypeIn(*extendDecl.extendedType.get(), extendDecl.keywordPos, "extend",
            "Extend declaration cannot extend paren type");
    }
}

void ParserImpl::ParseExtendBody(ExtendDecl& ed)
{
    ed.bodyScope = MakeOwned<DummyBody>();
    if (!Skip(TokenKind::LCURL)) {
        DiagExpectCharacter("'{'", "extend declaration must have body");
        // Do not need to do the folowing parsing
        ed.EnableAttr(Attribute::HAS_BROKEN);
        return;
    } else {
        ed.leftCurlPos = lookahead.Begin();
        ed.bodyScope->begin = lookahead.Begin();
    }
    SkipBlank(TokenKind::SEMI);
    // Parse extend members.
    while (true) {
        SkipBlank(TokenKind::SEMI);
        if (Skip(TokenKind::RCURL)) {
            ed.rightCurlPos = lastToken.Begin();
            break;
        }
        if (DetectPrematureEnd()) {
            ed.EnableAttr(Attribute::IS_BROKEN);
            DiagExpectedRightDelimiter("{", ed.leftCurlPos);
            break;
        }
        auto decl = ParseDecl(ScopeKind::EXTEND_BODY);
        if (decl->IsInvalid()) {
            continue;
        }
        SetMemberParentInheritableDecl(ed, decl);
        ed.members.emplace_back(std::move(decl));
    }
}

OwnedPtr<Node> ParserImpl::ParseExprOrDecl(ScopeKind sk)
{
    if (SeeingKeywordAndOperater()) {
        return ParseExpr();
    }
    if (SeeingMacroCall()) {
        if (curMacroCall || enableCustomAnno) {
            if (SeeingIfAvailable()) {
                return ParseIfAvailable();
            }
            return ParseDecl(sk);
        }
        OwnedPtr<Expr> expr = ParseMacroCall<MacroExpandExpr>(sk);
        return ParseExpr(Token{TokenKind::DOT}, std::move(expr));
    } else if (SeeingMacroCallDecl()) {
        return ParseMacroCall<MacroExpandDecl>(sk);
    } else if (SeeingDecl()) {
        return ParseDecl(sk);
    } else if (SeeingExpr()) {
        return ParseExpr();
    }
    return MakeInvalid<Node>(lookahead.Begin());
}

/**
 * expressionOrDeclarations
 *     : end* (expressionOrDeclaration (end+ expressionOrDeclaration?)*)?
 *     ;
 * */
OwnedPtr<Block> ParserImpl::ParseExpressionOrDeclarations(ScopeKind sk)
{
    OwnedPtr<Block> result = MakeOwned<Block>();
    result->begin = lastToken.Begin();
    result->leftCurlPos = lastToken.Begin();
    ChainScope cs(*this, result.get());
    SkipBlank(TokenKind::SEMI);
    for (;;) {
        bool hasNLorSEMI = SkipNLOrSemi();
        if (Skip(TokenKind::RCURL)) {
            result->rightCurlPos = lastToken.Begin();
            break;
        }
        if (DetectPrematureEnd()) {
            Position resPos = result->begin;
            // The distance from chainedAST.end() to LambdaExpr in ChainScope is 3.
            const static ptrdiff_t toLambdaLen = 3;
            if (chainedAST.size() >= toLambdaLen &&
                (*(chainedAST.end() - toLambdaLen))->astKind == ASTKind::LAMBDA_EXPR) {
                auto le = StaticAs<ASTKind::LAMBDA_EXPR>(*(chainedAST.end() - toLambdaLen));
                resPos = le->begin;
            }
            DiagExpectedRightDelimiter("{", resPos);
            break;
        }
        if (!result->body.empty() && !hasNLorSEMI) {
            if (DiagForBlock(*result)) {
                result->end = lookahead.End();
                return result;
            }
            ConsumeUntilDeclOrNL(TokenKind::RCURL);
            SkipBlank(TokenKind::NL);
        }
        if (SeeingMacroCallDecl() || SeeingDecl() || SeeingExpr()) {
            auto node = ParseExprOrDecl(sk);
            result->body.emplace_back(std::move(node));
        } else {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_expr_or_decl_in, lookahead, ConvertToken(lookahead));
            ConsumeUntilAny({TokenKind::RCURL, TokenKind::NL}, false);
        }
    }
    if (Seeing(TokenKind::SEMI)) {
        result->hasSemi = true;
        result->semiPos = lookahead.Begin();
    }
    result->end = lastToken.End();
    return result;
}

bool ParserImpl::DiagForBlock(const Block& block)
{ // Close the block.
    if (SeeingAny({TokenKind::RPAREN, TokenKind::RSQUARE, TokenKind::GT})) {
        if (CanMatchBracketInStack()) {
            DiagExpectedRightDelimiter("{", block.begin);
            return true; // early return.
        } else {
            ParseDiagnoseRefactor(DiagKindRefactor::parse_unmatched_right_delimiter, lookahead, lookahead.Value());
        }
    } else {
        DiagExpectSemiOrNewline();
    }
    return false;
}

OwnedPtr<Block> ParserImpl::ParseBlock(ScopeKind scopeKind)
{
    if (!Skip(TokenKind::LCURL)) {
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_left_brace, lookahead, ConvertToken(lookahead));
        return MakeInvalid<Block>(lookahead.Begin());
    }
    return ParseExpressionOrDeclarations(scopeKind);
}

DiagnosticEngine& ParserImpl::GetDiagnosticEngine() const
{
    return this->diag;
}
std::string ParserImpl::GetPrimaryDeclIdentRawValue() const
{
    return curPrimaryDeclIdentIsRaw ? "`" + curPrimaryDeclIdent + "`" : curPrimaryDeclIdent;
}
void ParserImpl::SetPrimaryDecl(const std::string& decl, bool isRawId)
{
    lastPrimaryDeclIdent = curPrimaryDeclIdent;
    lastPrimaryDeclIdentIsRaw = curPrimaryDeclIdentIsRaw;
    curPrimaryDeclIdent = decl;
    curPrimaryDeclIdentIsRaw = isRawId;
}
