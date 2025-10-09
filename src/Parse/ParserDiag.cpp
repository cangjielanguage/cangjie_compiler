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

#include <utility>

#include "ParserImpl.h"

#include "cangjie/AST/Match.h"

namespace Cangjie {
using namespace AST;
static inline bool IsKeyWord(const Token& t)
{
    return ((t.kind >= TokenKind::INT8 && t.kind < TokenKind::IDENTIFIER) && t.kind != TokenKind::DOLLAR &&
               t.kind != TokenKind::UPPERBOUND) ||
        t.kind == TokenKind::AS || t.kind == TokenKind::IS;
}

std::string ConvertToken(const Token& t)
{
    if (t.kind == TokenKind::END) {
        return "'<EOF>'";
    }
    if (t.kind == TokenKind::NL) {
        return "'<NL>'";
    }
    if (IsKeyWord(t)) {
        return std::string{"keyword '"} + TOKENS[static_cast<size_t>(t.kind)] + "'";
    }
    if (t.kind == TokenKind::INTEGER_LITERAL || t.kind == TokenKind::FLOAT_LITERAL) {
        return std::string{"literal '"} + t.Value() + "'";
    }
    if (Utils::In(t.kind, {TokenKind::BOOL_LITERAL, TokenKind::IDENTIFIER, TokenKind::PACKAGE_IDENTIFIER})) {
        return std::string{"'"} + t.Value() + "'";
    }
    return std::string{"'"} + TOKENS[static_cast<size_t>(t.kind)] + "'";
}

static Ptr<Node> TraceBackNode(std::vector<Ptr<Node>>& chainAST, std::vector<ASTKind> kinds)
{
    auto iter = std::find_if(chainAST.rbegin(), chainAST.rend(),
        [&kinds](auto node) { return std::find(kinds.begin(), kinds.end(), node->astKind) != kinds.end(); });
    if (iter != chainAST.rend()) {
        return *iter;
    }
    return nullptr;
}

void ParserImpl::DiagExpectedIdentifierClassDecl(Ptr<AST::Node> node)
{
    DiagExpectedIdentifier(
        MakeRange(node->begin, node->begin + std::string("class").size()), "a class name", "after keyword 'class'");
}

void ParserImpl::DiagExpectedIdentifierInterfaceDecl(Ptr<AST::Node> node)
{
    DiagExpectedIdentifier(MakeRange(node->begin, node->begin + std::string("interface").size()), "a interface name",
        "after keyword 'interface'");
}

void ParserImpl::DiagExpectedIdentifierStructDecl(Ptr<AST::Node> node)
{
    DiagExpectedIdentifier(
        MakeRange(node->begin, node->begin + std::string("struct").size()), "a struct name", "after keyword 'struct'");
}

void ParserImpl::DiagExpectedIdentifierTypeAliasDecl(Ptr<AST::Node> node)
{
    DiagExpectedIdentifier(
        MakeRange(node->begin, node->begin + std::string("type").size()), "a type name", "after keyword 'type'");
}

void ParserImpl::DiagExpectedIdentifierMacroExpandDecl(Ptr<AST::Node> node)
{
    auto med = StaticAs<ASTKind::MACRO_EXPAND_DECL>(node);
    auto range = MakeRange(
        med->invocation.fullNameDotPos.back(), med->invocation.fullNameDotPos.back() + std::string(".").size());
    DiagExpectedIdentifier(range, "a type name", "after '.' in qualified name");
}

void ParserImpl::DiagExpectedIdentifierMacroExpandExpr(Ptr<AST::Node> node)
{
    auto mee = StaticAs<ASTKind::MACRO_EXPAND_EXPR>(node);
    auto range = MakeRange(
        mee->invocation.fullNameDotPos.back(), mee->invocation.fullNameDotPos.back() + std::string(".").size());
    DiagExpectedIdentifier(range, "a type name", "after '.' in qualified name");
}

void ParserImpl::DiagExpectedIdentifierFuncDecl(Ptr<Node> node)
{
    DiagExpectedIdentifier(
        MakeRange(node->begin, node->begin + std::string("func").size()), "a func name", "after keyword 'func'");
}

void ParserImpl::DiagExpectedIdentifierFuncBody(Ptr<Node> node)
{
    auto fb = StaticAs<ASTKind::FUNC_BODY>(node);
    DiagExpectedIdentifier(MakeRange(fb->colonPos, fb->colonPos + std::string(":").size()), "a type name",
        "after ':' in function declaration");
}

void ParserImpl::DiagExpectedIdentifierMacroDecl(Ptr<Node> node)
{
    DiagExpectedIdentifier(
        MakeRange(node->begin, node->begin + std::string("macro").size()), "a macro name", "after keyword 'macro'");
}

void ParserImpl::DiagExpectedIdentifierMemberAccess(Ptr<Node> node)
{
    auto ma = StaticAs<ASTKind::MEMBER_ACCESS>(node);
    DiagExpectedIdentifier(
        MakeRange(ma->dotPos, ma->dotPos + std::string(".").size()), "a member name", "after '.' in qualified name");
}

void ParserImpl::DiagExpectedIdentifierQualifiedType(Ptr<Node> node)
{
    auto qt = StaticAs<ASTKind::QUALIFIED_TYPE>(node);
    DiagExpectedIdentifier(
        MakeRange(qt->dotPos, qt->dotPos + std::string(".").size()), "a type name", "after '.' in qualified name");
}

void ParserImpl::DiagExpectedIdentifierFuncParam(Ptr<Node>)
{
    // Get func param list node.
    auto pNode = TraceBackNode(chainedAST, {ASTKind::FUNC_PARAM_LIST});
    if (!pNode) {
        DiagExpectedIdentifier(MakeRange(lastToken.Begin(), lastToken.End()));
        return;
    }
    auto fpl = StaticAs<ASTKind::FUNC_PARAM_LIST>(pNode);
    if (fpl->params.empty()) {
        DiagExpectedIdentifier(MakeRange(fpl->leftParenPos, fpl->leftParenPos + std::string("(").size()),
            "a argument name", "after '(' in parameter list");
    } else if (!fpl->params.back()->commaPos.IsZero()) {
        DiagExpectedIdentifier(
            MakeRange(fpl->params.back()->commaPos, fpl->params.back()->commaPos + std::string("(").size()),
            "a argument name", "after ',' in parameter list");
    } else {
        DiagExpectedIdentifier(MakeRange(lastToken.Begin(), lastToken.End()));
    }
}

void ParserImpl::DiagExpectedIdentifierRefType(Ptr<Node>)
{
    const size_t toGenericLen = 2;
    if (chainedAST.size() <= toGenericLen) {
        DiagExpectedIdentifier(MakeRange(lastToken.Begin(), lastToken.End()));
        return;
    }
    auto iter = chainedAST.rbegin();
    std::advance(iter, 1);
    if ((*iter)->astKind != ASTKind::GENERIC_CONSTRAINT) {
        DiagExpectedIdentifier(MakeRange(lastToken.Begin(), lastToken.End()));
        return;
    }

    auto gc = StaticAs<ASTKind::GENERIC_CONSTRAINT>(*iter);
    if (!gc->wherePos.IsZero()) {
        DiagExpectedIdentifier(MakeRange(gc->wherePos, gc->wherePos + std::string("where").size()),
            "a generic type name", "after 'where' in generic constraint");
    } else if (lastToken == "," && !lastToken.Begin().IsZero()) {
        DiagExpectedIdentifier(MakeRange(lastToken.Begin(), lastToken.Begin() + std::string(",").size()),
            "a generic type name", "after ',' in generic constraint");
    } else {
        CJC_ABORT();
    }
}

void ParserImpl::DiagExpectedIdentifierPropDecl(Ptr<Node> node)
{
    auto pd = StaticAs<ASTKind::PROP_DECL>(node);
    DiagExpectedIdentifier(MakeRange(pd->keywordPos, pd->keywordPos + std::string("prop").size()), "a property name",
        "after keyword 'prop' in property definition");
}

void ParserImpl::DiagExpectedIdentifierPackageSpec(Ptr<Node> node)
{
    auto ps = StaticAs<ASTKind::PACKAGE_SPEC>(node);
    if (ps->prefixDotPoses.empty()) {
        DiagExpectedIdentifier(MakeRange(ps->packagePos, ps->packagePos + std::string("package").size()),
            "a package name", "after keyword 'package'");
    } else {
        DiagExpectedIdentifier(
            MakeRange(ps->prefixDotPoses.back(), ps->prefixDotPoses.back() + std::string(".").size()), "a package name",
            "after '.' in qualified name");
    }
}

void ParserImpl::DiagExpectedIdentifierImportContent(Ptr<Node> node)
{
    auto ic = StaticAs<ASTKind::IMPORT_CONTENT>(node);
    if (lastToken.kind == TokenKind::IMPORT) {
        DiagExpectedIdentifier(
            MakeRange(lastToken.Begin(), lastToken.End()), "a package name", "after keyword 'import'");
    } else if (!ic->prefixDotPoses.empty()) {
        DiagExpectedIdentifier(
            MakeRange(ic->prefixDotPoses.back(), ic->prefixDotPoses.back() + std::string(".").size()),
            "a '*' or identifier", "after '.' in qualified name");
    } else {
        DiagExpectedIdentifier(MakeRange(lastToken.Begin(), lastToken.End()));
    }
}

void ParserImpl::DiagExpectedIdentifierImportSpec(Ptr<Node> node)
{
    auto is = StaticAs<ASTKind::IMPORT_SPEC>(node);
    DiagExpectedIdentifier(MakeRange(is->importPos, is->importPos + std::string("import").size()), "a package name",
        "after keyword 'import'");
}

void ParserImpl::DiagExpectedIdentifierFeatureDirective(Ptr<Node> node)
{
    auto fs = StaticAs<ASTKind::FEATURES_DIRECTIVE>(node);
    if (fs->content.empty()) {
        if (lastToken.kind == TokenKind::DOT) { std::swap(lastToken, lookahead); }
        DiagExpectedIdentifier(MakeRange(fs->begin, fs->begin + std::string("features").size()),
            "a feature name", "after keyword 'features'", false);
    } else if (IsRawIdentifier(lastToken.Value())) {
        auto prevRange = fs->content.size() == 1 ? MakeRange(fs->begin, fs->begin + std::string("features").size()) :
            MakeRange(fs->content[fs->content.size() - 2].begin, fs->content[fs->content.size() - 2].end);
        std::swap(lastToken, lookahead);
        DiagExpectedIdentifier(prevRange, "an identifier", "after this", false);
        std::swap(lastToken, lookahead);
    } else if (lastToken.kind == TokenKind::COMMA) {
        std::stringstream ss;
        ss << "after '"  << fs->content[fs->content.size() - 1].ToString() << lastToken.Value() << "'";
        DiagExpectedIdentifier(MakeRange(fs->content[fs->content.size() - 1].begin, lastToken.End()), "an identifier",
            ss.str(), false);
    } else if (lastToken.kind == TokenKind::DOT) {
        DiagExpectedIdentifier(MakeRange(lastToken.Begin(), lastToken.End()), "an identifier",
            "after '.' in qualified name", false);
    } else  {
        DiagExpectedIdentifier(MakeRange(fs->content[fs->content.size() - 1].begin, lastToken.End()), "';' or '<NL>'",
            "after this", false);
    }
}

void ParserImpl::DiagExpectedIdentifierGenericConstraint(Ptr<Node> node)
{
    auto gc = StaticAs<ASTKind::GENERIC_CONSTRAINT>(node);
    if (gc->bitAndPos.empty()) {
        DiagExpectedIdentifier(MakeRange(gc->operatorPos, gc->operatorPos + std::string("<:").size()), "a type name",
            "after '<:' in generic constraint");
    } else {
        DiagExpectedIdentifier(MakeRange(gc->bitAndPos.back(), gc->bitAndPos.back() + std::string("&").size()),
            "a type name", "after '&' in generic constraint");
    }
}

void ParserImpl::DiagExpectedIdentifierGeneric(Ptr<Node> node)
{
    auto ge = StaticAs<ASTKind::GENERIC>(node);
    if (ge->typeParameters.empty()) {
        DiagExpectedIdentifier(MakeRange(ge->leftAnglePos, ge->leftAnglePos + std::string("<").size()),
            "a generic type name", "after '<' in generic");
    } else {
        auto range = MakeRange(
            ge->typeParameters.back()->commaPos, ge->typeParameters.back()->commaPos + std::string(",").size());
        DiagExpectedIdentifier(range, "a generic type name", "after ',' in generic");
    }
}

void ParserImpl::DiagExpectedIdentifierEnumDecl(Ptr<Node> node)
{
    auto ed = StaticAs<ASTKind::ENUM_DECL>(node);
    if (ed->identifier.ZeroPos()) {
        DiagExpectedIdentifier(
            MakeRange(ed->begin, ed->begin + std::string("enum").size()), "a enum name", "after keyword 'enum'");
    } else if (ed->constructors.empty()) {
        if (lastToken.kind == TokenKind::ELLIPSIS) {
            auto db = ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_name, lastToken, "an enum name",
                "at beginning of enum body", ConvertToken(lastToken));
            db.AddMainHintArguments("an enum constructor name");
            db.AddNote("ellipse cannot be the only constructor of an enum decl");
            return;
        }
        DiagExpectedIdentifier(MakeRange(ed->leftCurlPos, ed->leftCurlPos + std::string("{").size()),
            "a enum value name", "at beginning of enum body");
    } else if (!ed->bitOrPosVector.empty()) {
        DiagExpectedIdentifier(
            MakeRange(ed->bitOrPosVector.back(), ed->bitOrPosVector.back() + std::string("|").size()),
            "a enum value name", "after '|' in enum body");
    } else {
        CJC_ABORT();
    }
}

void ParserImpl::DiagExpectedIdentifier(
    const Range& range, const std::string& expectedName, const std::string& afterName, const bool callHelp)
{
    auto foundName = ConvertToken(lookahead);
    auto expectedMsg = expectedName;
    auto otherHintMsg = afterName;
    auto otherHintRange = range;

    if (lookahead.kind == TokenKind::WILDCARD) {
        auto ahead = lexer->LookAheadSkipNL(1);
        if (!ahead.empty()) {
            if (lookahead.Begin().line == ahead.front().Begin().line &&
                lookahead.Begin().column + 1 == ahead.front().Begin().column) {
                expectedMsg = "identifier";
                otherHintMsg = "expected a Unicode XID_Continue after '_'";
                otherHintRange = MakeRange(ahead.front().Begin(), ahead.front().Begin() + 1);
            }
        }
    }
    auto builder =
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_name, lookahead, expectedMsg, afterName, foundName);
    builder.AddMainHintArguments(expectedMsg);
    builder.AddHint(otherHintRange, otherHintMsg);

    if (IsKeyWord(lookahead) && callHelp) {
        auto helpMes = std::string("you could escape keyword as ") + expectedName + " using '`'";
        auto help = DiagHelp(helpMes);
        help.AddSubstitution(lookahead, "`" + lookahead.Value() + "`");
        builder.AddHelp(help);
    }
}

void ParserImpl::DiagNameLengthOverflow(const Range& r, const std::string& tar, size_t maxLength, size_t realLength)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_package_name_length_overflow, r, tar);

    auto note = SubDiagnostic(
        "the maximum length is " + std::to_string(maxLength) + ", the real length is " + std::to_string(realLength));
    builder.AddNote(note);
}

static const std::unordered_map<ASTKind, std::pair<std::string, std::string>> KIND_TO_STR = {
    {ASTKind::VAR_DECL, {"var", "variable declaration"}},
    {ASTKind::VAR_WITH_PATTERN_DECL, {"var", "variable declaration"}},
    {ASTKind::FUNC_DECL, {"func", "function declaration"}},
    {ASTKind::MAIN_DECL, {"main", "main function"}},
    {ASTKind::MACRO_DECL, {"macro", "macro declaration"}},
    {ASTKind::CLASS_DECL, {"class", "class declaration"}},
    {ASTKind::EXTEND_DECL, {"extend", "extend declaration"}},
    {ASTKind::INTERFACE_DECL, {"interface", "interface declaration"}},
    {ASTKind::STRUCT_DECL, {"struct", "struct declaration"}},
    {ASTKind::ENUM_DECL, {"enum", "enum declaration"}},
    {ASTKind::TYPE_ALIAS_DECL, {"type", "type alias declaration"}},
    {ASTKind::PROP_DECL, {"prop", "property declaration"}},
    {ASTKind::INTERPOLATION_EXPR, {"interpolation", "string interpolation"}},
    {ASTKind::PACKAGE_SPEC, {"package", "package declaration"}},
    {ASTKind::FEATURES_DIRECTIVE, {"features", "features declarations"}},
};

static const std::unordered_map<ScopeKind, std::pair<std::string, std::string>> SCOPE_TO_DECL = {
    {ScopeKind::TOPLEVEL, {"", "'top-level' scope"}},
    {ScopeKind::MACRO_BODY, {"macro", "macro body"}},
    {ScopeKind::CLASS_BODY, {"class", "class body"}},
    {ScopeKind::EXTEND_BODY, {"extend", "extend body"}},
    {ScopeKind::INTERFACE_BODY, {"interface", "interface body"}},
    {ScopeKind::STRUCT_BODY, {"struct", "struct body"}},
    {ScopeKind::ENUM_BODY, {"enum", "enum body"}},
    {ScopeKind::ENUM_CONSTRUCTOR, {"enum", "enum body"}},
    {ScopeKind::FUNC_BODY, {"func", "function body"}},
    {ScopeKind::PROP_MEMBER_SETTER_BODY, {"setter", "setter body"}},
    {ScopeKind::PROP_MEMBER_GETTER_BODY, {"getter", "getter body"}},
};

static std::pair<Range, std::string> GetSignature(Node& node)
{
    if (node.astKind == ASTKind::FUNC_DECL && node.TestAttr(Attribute::CONSTRUCTOR)) {
        return {MakeRange(node.begin, node.begin + std::string("init").size()), "constructor"};
    }
    if (node.astKind == ASTKind::FUNC_DECL && node.TestAttr(Attribute::FINALIZER)) {
        return {MakeRange(node.begin, node.begin + std::string("~init").size()), "finalizer"};
    }
    if (KIND_TO_STR.count(node.astKind) > 0) {
        if (node.astKind == ASTKind::VAR_DECL) {
            auto vd = StaticAs<ASTKind::VAR_DECL>(&node);
            if (vd && vd->isConst) {
                return {MakeRange(node.begin, node.begin + std::string("const").size()),
                    KIND_TO_STR.at(node.astKind).second};
            }
        }
        return {MakeRange(node.begin, node.begin + KIND_TO_STR.at(node.astKind).first.size()),
            KIND_TO_STR.at(node.astKind).second};
    }

    if (node.astKind == ASTKind::PRIMARY_CTOR_DECL) {
        auto pcd = StaticAs<ASTKind::PRIMARY_CTOR_DECL>(&node);
        return {MakeRange(node.begin, node.begin + pcd->identifier.Length()), "primary constructor"};
    }

    CJC_ABORT();
    return {MakeRange(DEFAULT_POSITION, node.begin + DEFAULT_POSITION), "declaration"};
}

// If if is only block node like `unsafe {..}`, which return a block. Otherwise return function and primary constructor
// or other thing.
static std::pair<bool, Ptr<Node>> IsOnlyBlockNode(std::vector<Ptr<Node>>& chain)
{
    if (chain.size() <= 1) {
        return {false, nullptr};
    }
    std::vector<Ptr<Node>> targetNodes;
    for (auto begin = chain.rbegin() + 1; begin != chain.rend(); ++begin) {
        if (KIND_TO_STR.count((*begin)->astKind) > 0 || (*begin)->astKind == ASTKind::PRIMARY_CTOR_DECL ||
            (*begin)->astKind == ASTKind::BLOCK) {
            targetNodes.push_back(*begin);
        }
    }
    if (targetNodes.empty()) {
        return {false, nullptr};
    }

    if (targetNodes[0]->astKind == ASTKind::BLOCK && targetNodes.size() > 1) {
        if (targetNodes[1]->astKind != ASTKind::FUNC_DECL && targetNodes[1]->astKind != ASTKind::PRIMARY_CTOR_DECL &&
            targetNodes[1]->astKind != ASTKind::MAIN_DECL && targetNodes[1]->astKind != ASTKind::MACRO_DECL &&
            targetNodes[1]->astKind != ASTKind::INTERPOLATION_EXPR) {
            return {true, targetNodes[0]};
        } else {
            return {false, targetNodes[1]};
        }
    }

    return {false, targetNodes[0]};
}

static std::pair<Range, std::pair<std::string, std::string>> TraceScopeInChain(std::vector<Ptr<Node>> chain)
{
    auto nodePair = IsOnlyBlockNode(chain);
    if (nodePair.second == nullptr) {
        return {MakeRange(DEFAULT_POSITION, DEFAULT_POSITION), {"'top-level' scope", "'top-level' scope"}};
    }
    if (nodePair.first || nodePair.second->astKind == ASTKind::BLOCK) {
        auto block = StaticAs<ASTKind::BLOCK>(nodePair.second);
        return {MakeRange(block->leftCurlPos, block->leftCurlPos + std::string{"{"}.size()), {"block", "block"}};
    }

    auto tar = nodePair.second;
    if (tar->astKind == ASTKind::PRIMARY_CTOR_DECL) {
        auto pcd = StaticAs<ASTKind::PRIMARY_CTOR_DECL>(tar);
        return {MakeRange(pcd->identifier), {"primary constructor", "primary constructor"}};
    }

    if (tar->astKind == ASTKind::INTERPOLATION_EXPR) {
        return {MakeRange(tar->begin, "${"), {"string interpolation", "string interpolation"}};
    }

    if (tar->astKind == ASTKind::FUNC_DECL) {
        auto fd = StaticAs<ASTKind::FUNC_DECL>(tar);
        if (fd->isGetter) {
            return {
                MakeRange(tar->begin, tar->begin + std::string("get").size()), std::make_pair("getter", "getter body")};
        }
        if (fd->isSetter) {
            return {
                MakeRange(tar->begin, tar->begin + std::string("set").size()), std::make_pair("setter", "setter body")};
        }
    }

    if (KIND_TO_STR.count(tar->astKind) > 0) {
        auto iden = KIND_TO_STR.at(tar->astKind).first;
        auto end = tar->begin + iden.size();
        iden = iden == "func" ? "function" : iden;
        iden = iden == "main" ? "main function" : iden;
        return {MakeRange(tar->begin, end), {iden, iden + " body"}};
    }

    return {MakeRange(DEFAULT_POSITION, DEFAULT_POSITION), {"'top-level' scope", "'top-level' scope"}};
}

void ParserImpl::DiagIllegalModifierInScope(const AST::Modifier& mod)
{
    CJC_ASSERT(!chainedAST.empty());
    auto node = chainedAST.back();

    auto scopeInfo = TraceScopeInChain(chainedAST);
    std::pair<Position, Position> hintPos{};
    std::string hintMes{};

    bool needSecondHint{false};
    if (node->astKind == ASTKind::FUNC_PARAM || node->astKind == ASTKind::VAR_DECL ||
        node->astKind == ASTKind::FUNC_DECL || node->astKind == ASTKind::PROP_DECL) {
        needSecondHint = true;
    }
    if (KIND_TO_STR.count(node->astKind) > 0) {
        hintPos.first = node->begin;
        if (node->astKind == ASTKind::PACKAGE_SPEC) {
            hintPos.first = StaticAs<ASTKind::PACKAGE_SPEC>(node)->packagePos;
        }
        hintPos.second = hintPos.first + KIND_TO_STR.at(node->astKind).first.size();
        if (node->TestAttr(Attribute::CONSTRUCTOR)) {
            hintMes = "constructor";
        } else if (node->TestAttr(Attribute::FINALIZER)) {
            hintMes = "finalizer";
        } else {
            hintMes = KIND_TO_STR.at(node->astKind).second;
        }
    } else if (node->astKind == ASTKind::FUNC_PARAM) {
        hintPos.first = node->begin;
        hintPos.second = node->begin + std::string("var").size();
        hintMes = "variable declaration";
    } else if (node->astKind == ASTKind::PRIMARY_CTOR_DECL) {
        auto pcd = StaticAs<ASTKind::PRIMARY_CTOR_DECL>(node);
        hintPos.first = pcd->identifier.Begin();
        hintPos.second = pcd->identifier.End();
        hintMes = "primary constructor";
    } else {
        CJC_ABORT();
    }

    auto modStr = TOKENS[static_cast<int>(mod.modifier)];
    auto secondAgr = needSecondHint ? " in " + scopeInfo.second.second : "";
    auto builder =
        ParseDiagnoseRefactor(DiagKindRefactor::parse_illegal_modifier_in_scope, mod, modStr, hintMes, secondAgr);

    if (scopeInfo.first.IsDefault() && needSecondHint) {
        hintMes += " in " + scopeInfo.second.first;
    }
    builder.AddHint(MakeRange(hintPos.first, hintPos.second), hintMes);

    if (!scopeInfo.first.IsDefault()) {
        builder.AddHint(scopeInfo.first, scopeInfo.second.first);
    }
}

void ParserImpl::DiagConflictedModifier(const Modifier& resMod, const Modifier& tarMod)
{
    if (chainedAST.empty()) {
        InternalError("The chainAST is empty in DiagConflictedModifier");
        return;
    }
    auto node = chainedAST.back();

    auto resStr = std::string(TOKENS[static_cast<int>(resMod.modifier)]);
    auto tarStr = std::string(TOKENS[static_cast<int>(tarMod.modifier)]);

    auto resRange = MakeRange(resMod.begin, resMod.begin + resStr.size());
    auto tarRange = MakeRange(tarMod.begin, tarMod.begin + tarStr.size());

    std::string hintN1;
    std::string hintN2;
    if (KIND_TO_STR.count(node->astKind) != 0) {
        hintN1 = KIND_TO_STR.at(node->astKind).first;
        hintN2 = KIND_TO_STR.at(node->astKind).second;
    } else if (node->astKind == ASTKind::PRIMARY_CTOR_DECL) {
        auto pcd = StaticAs<ASTKind::PRIMARY_CTOR_DECL>(node);
        hintN1 = pcd->identifier;
        hintN2 = "constructor";
    } else if (node->astKind == ASTKind::FUNC_PARAM) {
        hintN1 = "var";
        hintN2 = "variable declaration";
    } else {
        CJC_ABORT(); // Unexpect
    }

    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_conflict_modifier, tarRange, tarStr, resStr, hintN2);

    builder.AddHint(resRange);
    if (node->IsDecl()) {
        auto decl = RawStaticCast<const Decl *>(node);
        if (decl->astKind == ASTKind::VAR_DECL && decl->IsConst()) {
            builder.AddHint(MakeRange(decl->identifier), hintN2);
        } else {
            builder.AddHint(MakeRange(decl->keywordPos, decl->keywordPos + hintN1.size()), hintN2);
        }
    } else if (node->astKind == ASTKind::PACKAGE_SPEC) {
        auto pkgPos = StaticAs<ASTKind::PACKAGE_SPEC>(node)->packagePos;
        builder.AddHint(MakeRange(pkgPos, pkgPos + hintN1.size()), hintN2);
    } else {
        builder.AddHint(MakeRange(node->begin, node->begin + hintN1.size()), hintN2);
    }
}

std::string ParserImpl::GetSingleLineContent(const Position& begin, const Position& end) const
{
    if (begin.line == end.line) {
        return sourceManager.GetContentBetween(begin.fileID, begin, end);
    } else {
        return sourceManager.GetContentBetween(
            begin.fileID, begin, Position(begin.line, std::numeric_limits<int>::max()));
    }
}

void ParserImpl::DiagExpectNoModifierBefore(const Modifier& mod, const std::string& str)
{
    std::string modStr = TOKEN_KIND_VALUES[static_cast<int>(mod.modifier)];
    auto builder =
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_no_modifier, MakeRange(mod.begin, mod.end), str, modStr);

    auto tokens = lexer->LookAheadSkipNL(1);
    builder.AddHint(MakeRange(lookahead.Begin(), tokens.begin()->End()), str);
}

void ParserImpl::DiagUnexpectedAnnoOn(
    const Annotation& anno, const Position& onPos, const std::string& annoStr, const std::string& onStr)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_unexpected_anno_on, anno, "@" + annoStr, onStr);
    builder.AddHint(MakeRange(onPos, onStr), onStr);
}

void ParserImpl::DiagUnexpectedAnnoOnKind(
    const Annotation& anno, const Position& onPos, const std::string& annoStr, ASTKind kind)
{
    CJC_ASSERT(KIND_TO_STR.count(kind) != 0);
    auto keyword = KIND_TO_STR.at(kind).first;
    DiagUnexpectedAnnoOn(anno, onPos, annoStr, keyword);
}

void ParserImpl::DiagUnexpectedWhenON(PtrVector<Annotation>& annos)
{
    auto it = std::find_if(annos.begin(), annos.end(), [](auto& anno) { return anno->kind == AnnotationKind::WHEN; });
    CJC_ASSERT(it != annos.end());
    DiagUnexpectedAnnoOn(**it, lookahead.Begin(), it->get()->identifier, lookahead.Value());
}

void PrepareExpectNoModifierArg(const Node& node, std::string& arg, Range& range)
{
    switch (node.astKind) {
        case ASTKind::EXTEND_DECL: {
            arg = "extend declaration";
            range = MakeRange(node.begin, node.begin + std::string("extend").size());
            break;
        }
        case ASTKind::FUNC_DECL: {
            auto fd = StaticAs<ASTKind::FUNC_DECL>(&node);
            if (fd->isGetter) {
                arg = "getter function";
                range = MakeRange(node.begin, node.begin + std::string("get").size());
            } else if (fd->isSetter) {
                arg = "setter function";
                range = MakeRange(node.begin, node.begin + std::string("set").size());
            } else if (fd->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
                arg = "enum constructor";
                range = MakeRange(fd->identifier);
            } else {
                CJC_ABORT();
            }
            break;
        }
        case ASTKind::FUNC_PARAM: {
            arg = "non-member variable parameter";
            range = MakeRange(node.begin, node.end);
            break;
        }
        case ASTKind::VAR_DECL: {
            auto vd = StaticAs<ASTKind::VAR_DECL>(&node);
            if (vd->TestAttr(Attribute::ENUM_CONSTRUCTOR)) {
                arg = "enum constructor";
                range = MakeRange(vd->identifier);
            } else {
                CJC_ABORT();
            }
            break;
        }
        default: {
            break;
        }
    }
}

void ParserImpl::DiagExpectNoModifier(const Modifier& mod)
{
    CJC_ASSERT(!chainedAST.empty());
    auto node = chainedAST.back();
    std::string arg1{};
    auto range = MakeRange(DEFAULT_POSITION, DEFAULT_POSITION);
    PrepareExpectNoModifierArg(*node, arg1, range);
    auto modStr = mod.ToString();
    auto modRange = MakeRange(mod.begin, mod.begin + modStr.size());
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_no_modifier, modRange, arg1, modStr);
    builder.AddHint(range, arg1);
    if (Is<FuncParam>(node)) {
        auto content = GetSingleLineContent(node->begin, node->end);
        auto help = DiagHelp("if you want an immutable member variable, use 'let'");
        help.AddSubstitution(MakeRange(node->begin, node->end), "let " + content);
        builder.AddHelp(help);

        help = DiagHelp("or 'var' to get a mutable member variable");
        help.AddSubstitution(MakeRange(node->begin, node->end), "var " + content);
        builder.AddHelp(help);
    }
}

void ParserImpl::DiagUnexpectedDeclInScope(ScopeKind sk)
{
    CJC_ASSERT(!chainedAST.empty() && "The chainAST is empty in DiagIllegalModifierInScope");
    auto node = chainedAST.back();

    auto tar = GetSignature(*node);

    auto res = TraceScopeInChain(chainedAST);

    std::pair<std::string, std::string> arg;
    // It can not trace the scope if the parser is from macro replace.
    if (res.first.IsDefault() && sk != ScopeKind::TOPLEVEL) {
        CJC_ASSERT(SCOPE_TO_DECL.find(sk) != SCOPE_TO_DECL.end());
        arg = SCOPE_TO_DECL.at(sk);
    } else {
        arg = res.second;
    }

    auto builder = ParseDiagnoseRefactor(
        DiagKindRefactor::parse_unexpected_declaration_in_scope, tar.first, tar.second, arg.second);

    if (!res.first.IsDefault()) {
        builder.AddHint(res.first, arg.first);
        builder.AddMainHintArguments(tar.second);
    } else {
        builder.AddMainHintArguments(tar.second + " in " + arg.second);
    }

    node->EnableAttr(Attribute::IS_BROKEN);
}

void ParserImpl::DiagExpectedMoreFieldInTuplePattern()
{
    if (chainedAST.empty() || chainedAST.back()->astKind != ASTKind::TUPLE_PATTERN) {
        InternalError("The chainAST is empty in DiagExpectedMoreFieldInTuplePattern");
        return;
    }
    auto tp = StaticAs<ASTKind::TUPLE_PATTERN>(chainedAST.back());
    ParseDiagnoseRefactor(
        DiagKindRefactor::parse_tuple_pattern_expected_more_field, MakeRange(tp->leftBracePos, tp->rightBracePos + 1));
}

void ParserImpl::DiagExpectedTypeNameAfterAs(const Token& tok)
{
    if (tok.kind != TokenKind::AS && tok.kind != TokenKind::IS) {
        return;
    }
    auto builder = ParseDiagnoseRefactor(
        DiagKindRefactor::parse_expected_type, lookahead, ConvertToken(tok), ConvertToken(lookahead));
    builder.AddHint(tok, ConvertToken(tok));
}

void ParserImpl::DiagExpectedTypeName()
{
    auto builder = ParseDiagnoseRefactor(
        DiagKindRefactor::parse_expected_type, lookahead, ConvertToken(lastToken), ConvertToken(lookahead));
    builder.AddHint(lastToken, ConvertToken(lastToken));
}

void ParserImpl::DiagInvalidLeftHandExpr(const Expr& expr, const Token& tok)
{
    if (expr.TestAttr(Attribute::IS_BROKEN)) {
        return;
    }
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_invalid_left_hand_expr, tok, tok.Value());
    builder.AddHint(expr);
}

const std::unordered_map<TokenKind, std::string_view> NONE_ASSO_OP = {
    {TokenKind::ASSIGN, "assignment"}, // Means assignment category, including '=', '**=', '*=' etc.
    {TokenKind::RANGEOP, "range"},     // '..', '..='.
    {TokenKind::EQUAL, "equality"},    // '==', '!='.
    {TokenKind::LT, "comparison"}      // '<', '<=', '>', 'is', 'as' etc.
};

bool ParserImpl::IsNoneAssociative(const Token& tok) const
{
    return std::any_of(NONE_ASSO_OP.begin(), NONE_ASSO_OP.end(),
        [&tok, this](auto& kind) { return Precedence(kind.first) == Precedence(tok.kind); });
}

void ParserImpl::DiagNoneAssociativeOp(const Token& preT, const Token& tok)
{
    auto iter = std::find_if(NONE_ASSO_OP.begin(), NONE_ASSO_OP.end(),
        [&tok, this](auto& asso) { return Precedence(tok.kind) == Precedence(asso.first); });
    CJC_ASSERT(iter != NONE_ASSO_OP.end());

    auto builder =
        ParseDiagnoseRefactor(DiagKindRefactor::parse_chained_none_associative, tok, std::string{iter->second});
    builder.AddHint(preT);
}

void ParserImpl::DiagInvalidInheritType(const Type& type)
{
    auto typeName = sourceManager.GetContentBetween(type.begin.fileID, type.begin, type.end);
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_invalid_super_declaration, type, typeName);

    builder.AddNote("only class types and interface types can be subtyped");
}

void ParserImpl::DiagExpectedExpression()
{
    ParseDiagnoseRefactor(
        DiagKindRefactor::parse_expected_expression, lookahead, ConvertToken(lastToken), ConvertToken(lookahead));
}

void ParserImpl::DiagExpectedRightDelimiter(const std::string& del, const Position& pos)
{
    static std::map<std::string, std::string> delMap = {{"{", "}"}, {"(", ")"}, {"[", "]"}, {"<", ">"}};
    auto builder = lookahead.kind == TokenKind::NL
        ? ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_right_delimiter, lastToken, del)
        : ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_right_delimiter, lastToken.End(), del);

    builder.AddMainHintArguments(delMap[del]);
    builder.AddHint(pos, del);
}

void ParserImpl::DiagInvalidIncreExpr(const Expr& expr)
{
    auto name = lookahead.kind == TokenKind::INCR ? "increment" : "decrement";
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_invalid_incre_expr, lookahead, name);
    builder.AddHint(expr);
}

void ParserImpl::DiagInvalidMacroExpandExpr(const Token& tok, const MacroExpandExpr& expr)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_expression, *expr.invocation.decl,
        ConvertToken(tok), std::string{"a declaration"});
    builder.AddHint(tok);
}

void ParserImpl::DiagUnrecognizedNodeAfterMacro(const Token& tok, const MacroExpandExpr& expr)
{
    auto builder =
        ParseDiagnoseRefactor(DiagKindRefactor::parse_unrecognized_token_after_macro_node, tok, ConvertToken(tok));
    builder.AddHint(*expr.invocation.decl);
}

void ParserImpl::DiagChainedAsExpr(Expr& expr, const Token& tok)
{
    Position pos;
    if (expr.astKind == ASTKind::AS_EXPR) {
        auto isE = StaticAs<ASTKind::AS_EXPR>(&expr);
        pos = isE->asPos;
    }
    if (expr.astKind == ASTKind::IS_EXPR) {
        auto isE = StaticAs<ASTKind::IS_EXPR>(&expr);
        pos = isE->isPos;
    }
    auto builder =
        ParseDiagnoseRefactor(DiagKindRefactor::parse_chained_none_associative, tok, std::string{"`is` and `as`"});
    builder.AddHint(MakeRange(pos, pos + std::string("is").size()));
}

void ParserImpl::DiagExpecetedOpeOrEnd()
{
    ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_operator_or_end, lookahead, ConvertToken(lookahead));
}

void ParserImpl::DiagExpectedNoNewLine()
{
    auto old = skipNL;
    skipNL = false;
    Next(); // Skip '-'/'!' operator.
    auto newline = Peek();
    auto builder =
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_no_newline_after, newline, ConvertToken(lastToken));
    builder.AddHint(lastToken, ConvertToken(lastToken));
    skipNL = old;
}

void ParserImpl::DiagCannotHaveAssignmentInInit(const Expr& expr)
{
    ParseDiagnoseRefactor(DiagKindRefactor::parse_cannot_have_assi_in_init, expr);
}

void ParserImpl::DiagOrPattern()
{
    ParseDiagnoseRefactor(DiagKindRefactor::parse_illegal_or_pattern, lookahead.Begin());
}

void ParserImpl::DiagDeclarationInMacroPackage(const OwnedPtr<Decl>& decl)
{
    CJC_ASSERT(KIND_TO_STR.count(decl->astKind) != 0);
    auto typeKeyword = KIND_TO_STR.at(decl->astKind).first;
    auto publicModifier = std::find_if(decl->modifiers.begin(), decl->modifiers.end(),
        [](const auto& mod) { return mod.modifier == TokenKind::PUBLIC; });
    CJC_ASSERT(publicModifier != decl->modifiers.end());
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_macro_decl_in_macro_package,
        MakeRange(publicModifier->begin, publicModifier->begin + std::string("public").size()), typeKeyword);
    builder.AddHint(
        MakeRange(currentFile->package->macroPos, currentFile->package->packagePos + std::string("package").size()));
    SubDiagnostic note{
        "Declarations in 'macro package' cannot be accessed in other packages, except macro declarations."};
    builder.AddNote(note);
}

void ParserImpl::DiagExpectPublicBeforeMacroCall(const OwnedPtr<MacroDecl>& md)
{
    ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_public_before_macro_decl,
        MakeRange(md->keywordPos, md->keywordPos + std::string("macro").size()));
}

void ParserImpl::DiagExpectMacroParamType(const Type& type)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_macro_illegal_ret_type, type);
    builder.AddMainHintArguments(ConvertToken(lastToken));
}

void ParserImpl::DiagMacroUnexpectNamedParam(const OwnedPtr<FuncParam>& param)
{
    auto builder = ParseDiagnoseRefactor(
        DiagKindRefactor::parse_macro_illegal_named_param, MakeRange(param->notMarkPos, param->notMarkPos + 1));
    builder.AddHint(MakeRange(param->begin, param->identifier), param->identifier.Val());
}

void ParserImpl::DiagIllegalFunc(const OwnedPtr<FuncDecl>& funcDecl)
{
    auto builder =
        ParseDiagnoseRefactor(DiagKindRefactor::parse_illegal_function_name, MakeRange(funcDecl->identifier));
    builder.AddHint(MakeRange(funcDecl->keywordPos, funcDecl->keywordPos + std::string("func").size()));
}

void ParserImpl::DiagParseExpectedParenthis(const OwnedPtr<Type>& postType)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_parentheses, lastToken);
    auto help = DiagHelp("should be surrounded by '(' and ')'");
    std::string content = "(" + lastToken.Value() + ")";
    help.AddSubstitution(*postType, content);
    builder.AddHelp(help);
}

void ParserImpl::DiagParseIllegalDeclarationPattern(const OwnedPtr<VarWithPatternDecl>& decl, ScopeKind scopeKind)
{
    std::string patternTypeStr;
    if (decl->irrefutablePattern->astKind == ASTKind::ENUM_PATTERN) {
        patternTypeStr = "enum";
    } else if (decl->irrefutablePattern->astKind == ASTKind::TUPLE_PATTERN) {
        patternTypeStr = "tuple";
    } else if (decl->irrefutablePattern->astKind == ASTKind::WILDCARD_PATTERN) {
        patternTypeStr = "wildcard";
    }
    auto builder = ParseDiagnoseRefactor(
        DiagKindRefactor::parse_illegal_declaration_pattern, *(decl->irrefutablePattern), patternTypeStr);
    auto pNode = TraceBackNode(chainedAST, {ASTKind::CLASS_DECL, ASTKind::STRUCT_DECL});
    if (!pNode) {
        return;
    }
    auto node = StaticCast<AST::Decl*>(pNode);
    if (scopeKind == ScopeKind::CLASS_BODY) {
        builder.AddHint(MakeRange(node->keywordPos, node->keywordPos + std::string("class").size()), "class");
    } else if (scopeKind == ScopeKind::STRUCT_BODY) {
        builder.AddHint(MakeRange(node->keywordPos, node->keywordPos + std::string("struct").size()), "struct");
    }
}

void ParserImpl::DiagThisTypeNotAllow()
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_this_type_not_allow, lookahead);
    SubDiagnostic note =
        SubDiagnostic("'This' type can only be used as the return type of an instance member function in class");
    builder.AddNote(note);
}

void ParserImpl::DiagInvalidUnicodeScalar(const Position& startPos, const std::string& str)
{
    auto prefixSize = std::string_view{"\\u{"}.size();
    auto pos = startPos + prefixSize + 1;
    auto endPos = startPos + str.size();
    auto args = "0x" + str.substr(prefixSize, (str.size() - prefixSize) - 1);
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_invalid_unicode_scalar, MakeRange(pos, endPos), args);

    builder.AddHint(MakeRange(startPos + 1, startPos + prefixSize + 1));
    builder.AddHint(MakeRange(startPos + str.size(), startPos + str.size() + 1));

    auto note = SubDiagnostic{"the valid range of code point is '0x0 ~ 0x7fffffff'"};
    builder.AddNote(note);
}

void ParserImpl::DiagExpectedLiteral(const Position& subPos)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_literal, lookahead, ConvertToken(lookahead));
    builder.AddHint(subPos);
}

void ParserImpl::DiagTypePatternInLetCondExpr(const Pattern& pat, ExprKind ek)
{
    std::string exprName = ek == ExprKind::WHILE_COND_EXPR ? "while-let" : "if-let";
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_type_pattern_in_let_cond, pat, exprName);
    auto note =
        SubDiagnostic{exprName + " expression can only accept constant, wildcard, binding, tuple and enum pattern"};
    builder.AddNote(note);
}

void ParserImpl::DiagExpectedLeftParenAfter(const Position& pos, const std::string& str)
{
    auto builder = ParseDiagnoseRefactor(
        DiagKindRefactor::parse_expected_left_paren_after, lookahead, str, ConvertToken(lookahead));
    builder.AddHint(MakeRange(pos, str));
}

void ParserImpl::DiagMatchCaseExpectedExprOrDecl()
{
    auto builder =
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_expr_or_decl_in, lookahead, ConvertToken(lookahead));
    DiagMatchCase(builder);
}

void ParserImpl::DiagMatchCaseBodyCannotBeEmpty(const Position& pos)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_case_body_cannot_be_empty, pos);
    DiagMatchCase(builder);
}

void ParserImpl::DiagMatchCase(DiagnosticBuilder& builder)
{
    auto node = TraceBackNode(chainedAST, {ASTKind::MATCH_CASE_OTHER, ASTKind::MATCH_CASE});
    if (!node) {
        return;
    }
    if (node->astKind == ASTKind::MATCH_CASE) {
        auto mc = StaticAs<ASTKind::MATCH_CASE>(node);
        builder.AddHint(MakeRange(mc->begin, std::string("case")));
    } else if (node->astKind == ASTKind::MATCH_CASE_OTHER) {
        auto mco = StaticAs<ASTKind::MATCH_CASE_OTHER>(node);
        builder.AddHint(MakeRange(mco->begin, std::string("case")));
    } else {
        CJC_ABORT();
    }
}

void ParserImpl::DiagExpectedCatchOrHandleOrFinallyAfterTry(const TryExpr& te)
{
    auto diagKind = this->enableEH ? DiagKindRefactor::parse_expected_catch_or_handle_or_finally_in_try
                                        : DiagKindRefactor::parse_expected_catch_or_finally_in_try;
    auto builder = ParseDiagnoseRefactor(diagKind, lookahead, ConvertToken(lookahead));
    builder.AddHint(MakeRange(te.begin, te.tryBlock->end));
}

void ParserImpl::DiagExpectedSelectorOrMatchExprBody(const Position& pos)
{
    auto builder = ParseDiagnoseRefactor(
        DiagKindRefactor::parse_selector_or_match_expression_body, lookahead, ConvertToken(lookahead));
    builder.AddHint(MakeRange(pos, "match"));
}

void ParserImpl::DiagRedefinedResourceName(
    const std::pair<std::string, Position>& cur, const std::pair<std::string, Position>& pre)
{
    auto builder = ParseDiagnoseRefactor(
        DiagKindRefactor::parse_redefined_resource_name, MakeRange(cur.second, cur.first), cur.first);
    builder.AddHint(MakeRange(pre.second, pre.first));
}

void ParserImpl::DiagExpectedNoArgumentsInSpawn(
    const std::vector<OwnedPtr<AST::FuncParam>>& params, const Position& pos)
{
    std::string arg = params.size() == 1 ? "argument" : "arguments";
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_no_arguments_in_spawn,
        MakeRange(params.front()->begin, params.back()->end), arg);
    builder.AddMainHintArguments(arg);
    builder.AddHint(MakeRange(pos, "spawn"));
}

void ParserImpl::DiagExpectCharacter(const std::string& expectStr, const std::string& noteStr)
{
    auto builder = ParseDiagnoseRefactor(
        DiagKindRefactor::parse_expected_character, lookahead, expectStr, ConvertToken(lookahead));
    builder.AddMainHintArguments(expectStr);
    if (!noteStr.empty()) {
        builder.AddNote(noteStr);
    }
}

void ParserImpl::DiagExpectCharacter(const Position& pos, const std::string& expectStr, const std::string& noteStr)
{
    auto builder =
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_character, pos, expectStr, ConvertToken(lookahead));
    builder.AddMainHintArguments(expectStr);
    if (!noteStr.empty()) {
        builder.AddNote(noteStr);
    }
}

void ParserImpl::DiagExpectSemiOrNewline()
{
    DiagExpectCharacter("';' or '<NL>'");
}

void ParserImpl::DiagRedundantArrowAfterFunc(const Type& type)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_redundant_arrow_after_func_type, lookahead);
    builder.AddHint(type, "function type");
}

void ParserImpl::DiagInvalidOverloadedOperator()
{
    auto builder =
        ParseDiagnoseRefactor(DiagKindRefactor::parse_invalid_overloaded_operator, lookahead, ConvertToken(lookahead));
    auto note = SubDiagnostic("only operator '+', '-', ... can be overloaded");
    builder.AddNote(note);
}

void ParserImpl::DiagExpectedDeclaration(ScopeKind scopeKind)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_decl, lookahead, ConvertToken(lookahead));
    if (scopeKind == ScopeKind::TOPLEVEL) {
        auto note = SubDiagnostic("only declarations or macro expressions can be used in the top-level");
        builder.AddNote(note);
    }
}

void ParserImpl::DiagExpectedDeclaration(const Position& pos, const std::string& str)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_decl, pos, str);
}

void ParserImpl::DiagUnExpectedModifierOnDeclaration(const Decl& vd)
{
    auto constModifier = std::find_if(
        vd.modifiers.begin(), vd.modifiers.end(), [](const auto& mod) { return mod.modifier == TokenKind::CONST; });
    CJC_ASSERT(constModifier != vd.modifiers.end());
    auto builder = ParseDiagnoseRefactor(
        DiagKindRefactor::parse_unexpected_const_modifier_on_variable, MakeRange(constModifier->begin, "const"));
    builder.AddHint(MakeRange(vd.keywordPos, "var"));
}

void ParserImpl::DiagConstVariableExpectedInitializer(Decl& vd)
{
    if (parseDeclFile) {
        return;
    }
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_const_expected_initializer, vd.end);
    builder.AddHint(vd);
}

void ParserImpl::DiagConstVariableExpectedStatic(const Token& key)
{
    (void)ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_static_for_const_member_var, key);
}

void ParserImpl::DiagExpectedOneOfTypeOrInitializer(const Decl& vd, const std::string& str)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_one_of_type_or_initializer, vd.end, str);

    builder.AddHint(vd);
}

void ParserImpl::DiagExpectedTypeOrInitializerInPattern(const Decl& vd)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_type_or_init_in_pattern, vd.end);
    builder.AddHint(vd);
}

void ParserImpl::DiagExpectedInitializerForToplevelVar(const Decl& vd)
{
    if (parseDeclFile) {
        return;
    }
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_var_must_be_initialized, vd.end);
    builder.AddHint(vd);
}

void ParserImpl::DiagExpectedIdentifierOrPattern(bool isVar, const Position& pos, bool isConst)
{
    if (isConst) {
        auto builder = ParseDiagnoseRefactor(
            DiagKindRefactor::parse_expected_one_of_identifier_or_pattern, lookahead, "const", ConvertToken(lookahead));
        builder.AddHint(MakeRange(pos, "const"));
        return;
    }
    auto s = isVar ? "var" : "let";
    auto builder = ParseDiagnoseRefactor(
        DiagKindRefactor::parse_expected_one_of_identifier_or_pattern, lookahead, s, ConvertToken(lookahead));

    builder.AddHint(MakeRange(pos, s));
    if (IsKeyWord(lookahead)) {
        auto helpMes = std::string("escape the keyword `" + lookahead.Value() + "` to use it as an identifier");
        auto help = DiagHelp(helpMes);
        help.AddSubstitution(lookahead, "`" + lookahead.Value() + "`");
        builder.AddHelp(help);
    }
}

void ParserImpl::DiagExpectedGetOrSetInProp(const Position& pos)
{
    auto builder =
        ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_get_or_set_in_prop, lookahead, ConvertToken(lookahead));

    builder.AddHint(MakeRange(pos, "prop"));
}

void ParserImpl::DiagDuplicatedGetOrSet(const Node& node, const PropDecl& pd)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_duplicated_get_or_set, lookahead, lookahead.Value());
    builder.AddMainHintArguments(lookahead.Value());
    builder.AddHint(node);

    builder.AddHint(MakeRange(pd.begin, "prop"));
}

void ParserImpl::DiagUnknownPrimaryConstructor(const std::string& str)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_expected_decl, lookahead, ConvertToken(lookahead));
    builder.AddHelp({"probably you want a primary constructor name: '" + str + "'"});
}

void ParserImpl::DiagExpectedName(const std::string& str, const std::string& afterName)
{
    auto builder = ParseDiagnoseRefactor(
        DiagKindRefactor::parse_expected_name, lookahead, str + " name", afterName, ConvertToken(lookahead));

    builder.AddMainHintArguments(str + " name");
}

void ParserImpl::DiagGetOrSetCannotBeGeneric(const std::string& str, const Generic& ge)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_getter_setter_cannot_be_generic, ge, str);

    auto isGetOrSet = [](Ptr<Node> node) {
        if (node->astKind != ASTKind::FUNC_DECL) {
            return false;
        }
        auto fd = StaticAs<ASTKind::FUNC_DECL>(node);
        if (!fd->isSetter && !fd->isGetter) {
            return false;
        }
        return true;
    };
    auto iter = std::find_if(chainedAST.rbegin(), chainedAST.rend(), isGetOrSet);
    CJC_ASSERT(iter != chainedAST.rend() && "cannot find getter or setter");
    auto fd = StaticAs<ASTKind::FUNC_DECL>(*iter);
    builder.AddHint(MakeRange(fd->begin, "get"), str);
}

void ParserImpl::DiagUnexpectedWhere(const Token& token)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_unexpected_where, token);

    auto iter = std::find_if(chainedAST.rbegin(), chainedAST.rend(), [](auto node) {
        return node->astKind == ASTKind::FUNC_DECL || node->astKind == ASTKind::CLASS_DECL ||
            node->astKind == ASTKind::INTERFACE_DECL || node->astKind == ASTKind::ENUM_DECL ||
            node->astKind == ASTKind::STRUCT_DECL || node->astKind == ASTKind::MAIN_DECL ||
            node->astKind == ASTKind::EXTEND_DECL;
    });
    CJC_ASSERT(iter != chainedAST.rend() && "cannot find declaration");
    auto decl = StaticAs<ASTKind::DECL>(*iter);
    auto hintRange =
        decl->astKind == ASTKind::EXTEND_DECL ? MakeRange(decl->keywordPos, "extend") : MakeRange(decl->identifier);
    builder.AddHint(hintRange);
}

void ParserImpl::DiagDuplicatedIntrinsicFunc(const Decl& decl, const Decl& prev)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_duplicated_intrinsic_function, decl, decl.identifier);

    builder.AddHint(prev);
}

void ParserImpl::DiagMissingBody(const std::string& str, const std::string& name, const Position& pos)
{
    if (parseDeclFile) {
        return;
    }
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_missing_body, pos,
        name.empty() ? str : str + name);

    builder.AddMainHintArguments(str);
}

void ParserImpl::DiagDeclCannotInheritTheirSelf(const InheritableDecl& decl, const RefType& rt)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_decl_cannot_inherit_their_self, rt, rt.ref.identifier);

    builder.AddHint(MakeRange(decl.identifier), rt.ref.identifier);
}

void ParserImpl::DiagUnsafeWillBeIgnored(const Modifier& mod)
{
    auto builder =
        ParseDiagnoseRefactor(DiagKindRefactor::parse_unsafe_will_be_ignored, mod, Triple::BackendToString(backend));

    builder.AddMainHintArguments(Triple::BackendToString(backend));
}

void ParserImpl::DiagDuplicatedModifier(const Modifier& mod, const Modifier& pre)
{
    auto builder = ParseDiagnoseRefactor(
        DiagKindRefactor::parse_duplicate_modifier, mod, TOKEN_KIND_VALUES[static_cast<int>(mod.modifier)]);

    builder.AddHint(pre);
}

void ParserImpl::DiagNamedParameterAfterUnnamed(const FuncParam& param, const FuncParam& pre)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_named_parameter_after_unnamed, param);

    builder.AddHint(pre);
}

void ParserImpl::DiagMemberParameterAfterRegular(const FuncParam& param, const FuncParam& pre)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_member_parameter_after_regular, param);

    builder.AddHint(pre);
}

void ParserImpl::DiagUnexpectedColonInRange(const RangeExpr& re)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_unexpected_colon_in_range, lookahead);
    builder.AddHint(re);

    builder.AddNote("range expression in index access cannot have step if it is without start or end");
}

void ParserImpl::DiagDuplicatedAttiValue(const std::string& attr, const Position& pos, const Position& pre)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_duplicated_attr_value, MakeRange(pos, attr), attr);
    builder.AddHint(MakeRange(pre, attr));
}

void ParserImpl::DiagDuplicatedItem(const std::string& name, const std::string& sourceName, const Position& errPos,
    const Position& prePos, const std::string& attachment)
{
    auto builder = ParseDiagnoseRefactor(
        DiagKindRefactor::parse_duplicated_item, MakeRange(errPos, sourceName), name, sourceName, attachment);
    builder.AddMainHintArguments(name);
    builder.AddHint(MakeRange(prePos, sourceName));
}

void ParserImpl::DiagUnrecognizedAttrInAnno(
    const Annotation& anno, const std::string& attrName, const Position& pos, const std::set<std::string>& acs)
{
    CJC_ASSERT(!acs.empty() && "accepted attributes cannot be empty");
    std::string str{"'" + *acs.begin() + "'"};
    auto i = acs.begin();
    auto j = acs.end();
    ++i;
    --j;
    for (; i != acs.end(); ++i) {
        auto temp = i == j ? " or " : ", ";
        str += temp;
        str += "'" + *i + "'";
    }
    auto builder = ParseDiagnoseRefactor(
        DiagKindRefactor::parse_unrecognized_attr_in_anno, MakeRange(pos, attrName), attrName, anno.identifier);
    builder.AddHint(anno);
    builder.AddNote("annotation '@" + anno.identifier + "' only accept " + str + " as attribute");
}

void ParserImpl::DiagDuplicatedAnno(const Annotation& anno, const Annotation& pre)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_duplicated_annotation, anno, "@" + anno.identifier);
    builder.AddHint(pre);
}

void ParserImpl::DiagExpectedLsquareAfter(const Node& node, const std::string& aName, const std::string& note)
{
    auto builder = ParseDiagnoseRefactor(
        DiagKindRefactor::parse_expected_lsquare_after, lookahead, aName, ConvertToken(lookahead));
    builder.AddNote(note);
    builder.AddHint(node);
}

void ParserImpl::DiagUnrecognizedExprInWhen(const Expr& expr, const Annotation& when)
{
    auto str = sourceManager.GetContentBetween(expr.begin.fileID, expr.begin, expr.end);
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_unrecognized_expression_in_when, expr, str);
    builder.AddHint(when);
    builder.AddNote("annotation '@When' can only support unary, binary and ref expression");
}

void ParserImpl::DiagDeprecatedArgumentNotLitConst(const Node& node)
{
    ParseDiagnoseRefactor(DiagKindRefactor::parse_deprecated_arguments_must_be_lit_const_expr, node);
}

void ParserImpl::DiagDeprecatedArgumentDuplicated(
    const Node& node,
    const std::string& parameterName
)
{
    ParseDiagnoseRefactor(DiagKindRefactor::parse_deprecated_argument_duplication, node, parameterName);
}

void ParserImpl::DiagDeprecatedWrongArgumentType(
    const Node& node,
    const std::string& paramName,
    const std::string& expectedType
)
{
    ParseDiagnoseRefactor(DiagKindRefactor::parse_deprecated_wrong_argument, node, paramName, expectedType);
}

void ParserImpl::DiagDeprecatedUnknownArgument(
    const Node& arg,
    const std::string& name
)
{
    ParseDiagnoseRefactor(DiagKindRefactor::parse_deprecated_unknown_argument, arg, name);
}

void ParserImpl::DiagDeprecatedEmptyStringArgument(
    const Node& node,
    const std::string& paramName
)
{
    ParseDiagnoseRefactor(DiagKindRefactor::parse_deprecated_empty_string_argument, node, paramName);
}

void ParserImpl::DiagDeprecatedInvalidTarget(
    const Node& node,
    const std::string& invalidTarget
)
{
    ParseDiagnoseRefactor(DiagKindRefactor::parse_deprecated_invalid_target, node, invalidTarget);
}

void ParserImpl::DiagAnnotationExpectsOneArgument(const AST::Annotation& node, const std::string& annotationName)
{
    ParseDiagnoseRefactor(
        DiagKindRefactor::parse_annotation_one_argument,
        node, annotationName, "");
}

void ParserImpl::DiagAnnotationExpectsOneArgument(const Annotation& node, const std::string& annotationName,
                                                  const std::string& argInfo)
{
    const Node* diagNode = node.args.size() == 1 ? StaticCast<Node*>(node.args[0].get().get()) : &node;
    auto dbuilder = ParseDiagnoseRefactor(DiagKindRefactor::parse_annotation_one_argument, *diagNode,
        annotationName, " " + argInfo);

    switch (node.args.size()) {
        case 1:
            dbuilder.AddMainHintArguments(argInfo);
            break;
        default:
            dbuilder.AddMainHintArguments("single " + argInfo + " for " + annotationName);
            break;
    }
}

void ParserImpl::DiagAnnotationMoreThanOneArgs(const Annotation& node, const std::string& annotationName)
{
    CJC_ASSERT(node.args.size() > 1);
    auto dbuilder = ParseDiagnoseRefactor(
        DiagKindRefactor::parse_annotation_max_one_argument,
        node, annotationName, "");
    dbuilder.AddMainHintArguments("single argument for " + annotationName);
}

void ParserImpl::DiagAnnotationMoreThanOneArgs(const Annotation& node, const std::string& annotationName,
                                               const std::string& argInfo)
{
    const Node* diagNode = node.args.size() == 1 ? StaticCast<Node*>(node.args[0].get().get()) : &node;
    auto dbuilder = ParseDiagnoseRefactor(DiagKindRefactor::parse_annotation_max_one_argument,
        *diagNode, annotationName, " " + argInfo);
    if (node.args.size() == 1) {
        dbuilder.AddMainHintArguments(argInfo);
    } else {
        dbuilder.AddMainHintArguments("single " + argInfo + " for " + annotationName);
    }
}

void ParserImpl::DiagUnexpectedTypeIn(
    const Type& type, const Position& inPos, const std::string& inStr, const std::string& noteStr)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_unexpected_type_in, type, inStr);
    builder.AddHint(MakeRange(inPos, inStr), inStr);
    builder.AddNote(noteStr);
}

void ParserImpl::DiagExpectedIdentifierWithNode(Ptr<AST::Node> node)
{
    if (auto iter = diagExpectedIdentifierMap.find(node->astKind); iter != diagExpectedIdentifierMap.end()) {
        iter->second(this, node);
    } else {
        auto range = MakeRange(lastToken.Begin(), lastToken.End());
        DiagExpectedIdentifier(range);
    }
    node->EnableAttr(Attribute::HAS_BROKEN);
}

void ParserImpl::DiagImportingByPackageNameIsNotSupported(const Position& expectPos)
{
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_importing_by_package_name_is_not_supported, expectPos);
    builder.AddNote("importing by package name is not supported.");
}

void ParserImpl::DiagVArrayTypeArgMismatch(const Range& range, const std::string& note)
{
    (void)ParseDiagnoseRefactor(DiagKindRefactor::parse_varray_type_args_mismatch, range, note);
    ConsumeUntil(TokenKind::NL);
}

void ParserImpl::DiagRedundantModifiers(const Modifier& modifier)
{
    // NOTE: for now, only interface member decls will enter this method.
    CJC_ASSERT(!chainedAST.empty());
    auto node = TraceBackNode(chainedAST, {ASTKind::INTERFACE_DECL});
    if (!node) {
        // When parsing macro expand nodes, the decl of outer scope may not existed.
        return;
    }

    CJC_ASSERT(KIND_TO_STR.count(node->astKind) != 0);
    auto kindStr = KIND_TO_STR.at(node->astKind);
    auto scopeRange = MakeRange(node->begin, node->begin + kindStr.first.size());

    auto modStr = std::string(TOKENS[static_cast<int>(modifier.modifier)]);
    auto modRange = MakeRange(modifier.begin, modifier.begin + modStr.size());
    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_redundant_modifier, modRange, modStr);
    builder.AddMainHintArguments("interface member", modStr);
    builder.AddHint(scopeRange);
}

void ParserImpl::DiagRedundantModifiers(const Modifier& lowerModifier, const Modifier& higherModifier)
{
    auto resStr = std::string(TOKENS[static_cast<int>(lowerModifier.modifier)]);
    auto tarStr = std::string(TOKENS[static_cast<int>(higherModifier.modifier)]);

    auto resRange = MakeRange(lowerModifier.begin, lowerModifier.begin + resStr.size());
    auto tarRange = MakeRange(higherModifier.begin, higherModifier.begin + tarStr.size());

    auto builder = ParseDiagnoseRefactor(DiagKindRefactor::parse_redundant_modifier, resRange, resStr);
    builder.AddMainHintArguments("'" + tarStr + "'", resStr);
    builder.AddHint(tarRange);
}

} // namespace Cangjie
