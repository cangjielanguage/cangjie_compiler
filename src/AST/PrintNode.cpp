// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements AST and related data print apis.
 */

#include "cangjie/AST/PrintNode.h"

#include <iostream>
#include <string>

#include "cangjie/AST/ASTCasting.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/Utils/Utils.h"

namespace Cangjie {
using namespace AST;

using namespace Utils;
using namespace Meta;

namespace {
const unsigned ONE_INDENT = 1u;
const unsigned TWO_INDENT = 2u;
const unsigned THREE_INDENT = 3u;
#define UNKNOWN_TY (ANSI_COLOR_RED + "unknown" + ANSI_COLOR_RESET)

void PrintModifiers(const Decl& decl, unsigned indent, std::ostream& stream = std::cout)
{
    std::string str = "Modifiers:";
    for (auto& i : decl.modifiers) {
        str += " " + std::string(TOKENS[static_cast<int>(i.modifier)]);
    }
    if (!decl.modifiers.empty()) {
        PrintIndent(stream, indent + ONE_INDENT, str);
    }
}

void PrintTarget(unsigned indent, const Decl& target, std::string addition = "target", std::ostream& stream = std::cout)
{
    if (target.mangledName.empty()) {
        PrintIndent(stream, indent, addition + ": ptr:", &target);
    } else {
        PrintIndent(stream, indent, addition + ": mangledName:", "\"" + target.mangledName + "\"");
    }
}

void PrintBasic(unsigned indent, const Node& node, std::ostream& stream = std::cout)
{
    // Basic info:
    std::string filePath = node.curFile ? node.curFile->filePath : "not in file";
    PrintIndent(stream, indent, "curFile:", filePath);
    PrintIndent(stream, indent, "position:", node.begin.ToString(), node.end.ToString());
    PrintIndent(stream, indent, "scopeName:", "\"" + node.scopeName + "\"");
    PrintIndent(stream, indent, "ty:", node.ty->String());
    PrintIndent(stream, indent, "ptr:", &node);
    const auto& fullPkgName = node.GetFullPackageName();
    if (!fullPkgName.empty()) {
        PrintIndent(stream, indent, "fullPackageName:", fullPkgName);
    } else {
        PrintIndent(stream, indent, "fullPackageName is empty");
    }
    PrintIndent(stream, indent, "attributes:", node.GetAttrs().ToString());
    if (!node.exportId.empty()) {
        PrintIndent(stream, indent, "exportId:", "\"" + node.exportId + "\"");
    }
    if (auto d = DynamicCast<Decl>(&node)) {
        PrintIndent(
            stream, indent, "linkage:", static_cast<int>(d->linkage), ", isConst:", static_cast<int>(d->IsConst()));
        if (!d->mangledName.empty()) {
            PrintIndent(indent, "mangledName:", "\"" + d->mangledName + "\"");
        } else {
            PrintIndent(indent, "ptr:", &node);
        }

        if (d->annotationsArray) {
            PrintNode(d->annotationsArray.get(), indent, "annotationsArray", stream);
        }
        if (d->outerDecl) {
            PrintTarget(indent, *d->outerDecl, "outerDecl", stream);
        }
    }
    if (!node.comments.IsEmpty()) {
        PrintIndent(stream, indent, "comments: " + node.comments.ToString());
    }
}

void PrintIndentTokens(unsigned indent, const std::vector<Token>& args, std::ostream& stream = std::cout)
{
    std::stringstream ss;
    PrintIndent(stream, indent, "pos: ", args.front().Begin().ToString(), args.back().End().ToString());
    for (auto& it : args) {
        if (it.kind == TokenKind::NL) {
            ss << ";";
        } else {
            ss << it.Value();
        }
    }
    PrintIndent(stream, indent, "\"" + ss.str() + "\"");
}

void PrintMacroInvocation(unsigned indent, const MacroInvocation& invocation, std::ostream& stream = std::cout)
{
    if (invocation.attrs.empty()) {
        PrintIndent(stream, indent + ONE_INDENT, "// no attributes");
    } else {
        PrintIndent(stream, indent + ONE_INDENT, "attrs {");
        PrintIndentTokens(indent + TWO_INDENT, invocation.attrs, stream);
        PrintIndent(stream, indent + ONE_INDENT, "}");
    }
    if (invocation.args.empty()) {
        PrintIndent(stream, indent + ONE_INDENT, "// no arguments");
    } else {
        PrintIndent(stream, indent + ONE_INDENT, "args {");
        PrintIndentTokens(indent + TWO_INDENT, invocation.args, stream);
        PrintIndent(stream, indent + ONE_INDENT, "}");
    }
    if (invocation.decl) {
        PrintNode(invocation.decl, indent + ONE_INDENT, "decl", stream);
    }
}

void PrintPackage(unsigned indent, const Package& package, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "Package:", package.fullPackageName, "{");

    PrintIndent(stream, indent + ONE_INDENT, "noSubPkg:", package.noSubPkg);

    for (auto& it : package.files) {
        PrintNode(it.get(), indent + ONE_INDENT, "", stream);
    }

    PrintIndent(stream, indent + ONE_INDENT, "genericInstantiatedDecls {");
    for (auto& it : package.genericInstantiatedDecls) {
        PrintNode(it.get(), indent + TWO_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "}");

    PrintIndent(stream, indent + ONE_INDENT, "srcImportedNonGenericDecls {");
    for (auto& it : package.srcImportedNonGenericDecls) {
        PrintNode(it.get(), indent + TWO_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "}");

    PrintIndent(stream, indent + ONE_INDENT, "inlineFuncDecls {");
    for (auto& it : package.inlineFuncDecls) {
        PrintNode(it.get(), indent + TWO_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "}");

    PrintIndent(stream, indent, "}");
}

void PrintFile(unsigned indent, const File& file, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "File:", file.fileName, "{");
    PrintNode(file.feature.get(), indent + ONE_INDENT, "features", stream);
    PrintNode(file.package.get(), indent + ONE_INDENT, "package", stream);
    for (auto& it : file.imports) {
        PrintNode(it.get(), indent + ONE_INDENT, "", stream);
    }
    for (auto& it : file.decls) {
        if (it->identifier == MAIN_INVOKE) {
            continue;
        }
        PrintNode(it.get(), indent + ONE_INDENT, "", stream);
    }
    for (auto& it : file.exportedInternalDecls) {
        PrintNode(it.get(), indent + ONE_INDENT, "exportedInternalDecls", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "originalMacroCallNodes [");
    for (auto& macroNode : file.originalMacroCallNodes) {
        PrintNode(macroNode.get(), indent + TWO_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");
    PrintIndent(stream, indent, "}");
}

void PrintMacroExpandParam(unsigned indent, const AST::MacroExpandParam& macroExpand, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "MacroExpandParam:", macroExpand.invocation.fullName, "{");
    PrintMacroInvocation(indent, macroExpand.invocation, stream);
    PrintIndent(stream, indent, "}");
}

void PrintGenericParamDecl(unsigned indent, const AST::GenericParamDecl& gpd, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "GenericParamDecl:", gpd.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, gpd, stream);
    PrintIndent(stream, indent, "}");
}

void PrintAnnotation(unsigned indent, const Annotation& anno, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "Annotation:", anno.identifier, "{");
    PrintBasic(indent + ONE_INDENT, anno, stream);
    PrintIndent(stream, indent + ONE_INDENT, "args [");
    for (auto& arg : anno.args) {
        PrintNode(arg.get(), indent + TWO_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");
    PrintIndent(stream, indent + ONE_INDENT, "adAnnotation:", anno.adAnnotation);
    PrintNode(anno.condExpr.get(), indent + ONE_INDENT, "condExpr", stream);
    PrintIndent(stream, indent + ONE_INDENT, "target:", anno.target);
    PrintNode(anno.baseExpr.get(), indent + ONE_INDENT, "baseExpr", stream);
    PrintIndent(stream, indent, "}");
}

void PrintAnnotations(const Decl& decl, unsigned indent, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "annotations [");
    for (auto& anno : decl.annotations) {
        PrintAnnotation(indent + ONE_INDENT, *anno.get(), stream);
    }
    PrintIndent(stream, indent, "]");
}

void PrintFuncParam(unsigned indent, const FuncParam& param, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "FuncParam:", param.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, param, stream);
    PrintAnnotations(param, indent + ONE_INDENT, stream);
    PrintNode(param.type.get(), indent + ONE_INDENT, "type", stream);
    PrintNode(param.assignment.get(), indent + ONE_INDENT, "assignment", stream);
    if (param.desugarDecl) {
        PrintNode(param.desugarDecl.get(), indent + ONE_INDENT, "desugarDecl", stream);
    }
    PrintIndent(stream, indent, "}");
}

void PrintFuncParamList(unsigned indent, const FuncParamList& paramList, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "FuncParamList {");
    if (paramList.params.empty()) {
        PrintIndent(stream, indent + ONE_INDENT, "// no Params");
    } else {
        for (auto& it : paramList.params) {
            PrintNode(it.get(), indent + ONE_INDENT, "", stream);
        }
    }
    PrintIndent(stream, indent, "}");
}

void PrintMacroDecl(unsigned indent, const MacroDecl& macroDecl, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "MacroDecl:", macroDecl.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, macroDecl, stream);
    PrintModifiers(macroDecl, indent, stream);
    if (macroDecl.desugarDecl) {
        PrintIndent(stream, indent + ONE_INDENT, "// desugar decl");
        PrintNode(macroDecl.desugarDecl.get(), indent + ONE_INDENT, "", stream);
    } else {
        PrintIndent(stream, indent + ONE_INDENT, "// macro body");
        PrintNode(macroDecl.funcBody.get(), indent + ONE_INDENT, "", stream);
    }
    PrintIndent(stream, indent, "}");
}

void PrintFuncDecl(unsigned indent, const FuncDecl& funcDecl, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "FuncDecl:", funcDecl.identifier.Val(), funcDecl.identifier.Begin(),
        funcDecl.identifier.End(), "{");
    PrintBasic(indent + ONE_INDENT, funcDecl, stream);
    if (funcDecl.TestAttr(Attribute::CONSTRUCTOR) && funcDecl.constructorCall == ConstructorCall::SUPER) {
        if (auto ce = DynamicCast<CallExpr*>(funcDecl.funcBody->body->body.begin()->get()); ce) {
            if (ce->TestAttr(Attribute::COMPILER_ADD)) {
                PrintIndent(stream, indent + ONE_INDENT, "super: implicitSuper");
            } else {
                PrintIndent(stream, indent + ONE_INDENT, "super: explicitSuper");
            }
        }
    }
    PrintAnnotations(funcDecl, indent + ONE_INDENT, stream);
    PrintModifiers(funcDecl, indent, stream);
    PrintNode(funcDecl.funcBody.get(), indent + ONE_INDENT, "", stream);
    PrintIndent(stream, indent, "}");
}

void PrintPrimaryCtorDecl(unsigned indent, const PrimaryCtorDecl& primaryCtor, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "PrimaryCtorDecl:", primaryCtor.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, primaryCtor, stream);
    PrintAnnotations(primaryCtor, indent + ONE_INDENT, stream);
    PrintModifiers(primaryCtor, indent, stream);
    PrintNode(primaryCtor.funcBody.get(), indent + ONE_INDENT, "", stream);
    PrintIndent(stream, indent, "}");
}

void PrintMainDecl(unsigned indent, const MainDecl& mainDecl, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "MainDecl:", mainDecl.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, mainDecl, stream);
    if (mainDecl.desugarDecl) {
        PrintNode(mainDecl.desugarDecl.get(), indent + ONE_INDENT, "desugarDecl", stream);
    } else {
        PrintNode(mainDecl.funcBody.get(), indent + ONE_INDENT, "funcBody", stream);
    }
    PrintIndent(stream, indent, "}");
}

void PrintGeneric(unsigned indent, const Generic& generic, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "typeParameters {");
    for (auto& it : generic.typeParameters) {
        PrintNode(it.get(), indent + ONE_INDENT, "", stream);
    }
    PrintIndent(stream, indent, "}");
    PrintIndent(stream, indent, "genericConstraints {");
    for (auto& it : generic.genericConstraints) {
        PrintNode(it.get(), indent + ONE_INDENT, "", stream);
    }
    PrintIndent(stream, indent, "}");
}

void PrintFuncBody(unsigned indent, const FuncBody& body, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "FuncBody {");
    PrintBasic(indent + ONE_INDENT, body, stream);
    if (body.generic) {
        PrintGeneric(indent + ONE_INDENT, *body.generic, stream);
    }
    for (auto& it : body.paramLists) {
        PrintNode(it.get(), indent + ONE_INDENT, "", stream);
    }
    if (!body.retType) {
        PrintIndent(stream, indent + ONE_INDENT, "// no return type");
    } else {
        PrintIndent(stream, indent + ONE_INDENT, "// return type");
        PrintNode(body.retType.get(), indent + ONE_INDENT, "", stream);
    }
    PrintNode(body.body.get(), indent + ONE_INDENT, "body", stream);
    PrintIndent(stream, indent, "}");
}

void PrintPropDecl(unsigned indent, const PropDecl& propDecl, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "PropDecl:", propDecl.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, propDecl, stream);
    PrintModifiers(propDecl, indent, stream);
    PrintNode(propDecl.type.get(), indent + ONE_INDENT, "type", stream);
    for (auto& propMemberDecl : propDecl.setters) {
        PrintNode(propMemberDecl.get(), indent + ONE_INDENT, "", stream);
    }
    for (auto& propMemberDecl : propDecl.getters) {
        PrintNode(propMemberDecl.get(), indent + ONE_INDENT, "", stream);
    }
    PrintIndent(stream, indent, "}");
}

void PrintMacroExpandDecl(unsigned indent, const AST::MacroExpandDecl& macroExpand, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "MacroExpand:", macroExpand.invocation.fullName, "{");
    PrintBasic(indent + ONE_INDENT, macroExpand, stream);
    PrintMacroInvocation(indent, macroExpand.invocation, stream);
    PrintIndent(stream, indent, "}");
}

void PrintVarWithPatternDecl(
    unsigned indent, const VarWithPatternDecl& varWithPatternDecl, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "VarWithPatternDecl {");
    PrintBasic(indent + ONE_INDENT, varWithPatternDecl, stream);
    if (varWithPatternDecl.isConst) {
        PrintIndent(stream, indent + ONE_INDENT, "Const");
    } else {
        PrintIndent(stream, indent + ONE_INDENT, varWithPatternDecl.isVar ? "Var" : "Let");
    }
    PrintNode(varWithPatternDecl.irrefutablePattern.get(), indent + ONE_INDENT, "", stream);
    PrintNode(varWithPatternDecl.type.get(), indent + ONE_INDENT, "type", stream);
    PrintNode(varWithPatternDecl.initializer.get(), indent + ONE_INDENT, "initializer", stream);
    PrintIndent(stream, indent, "}");
}

void PrintVarDecl(unsigned indent, const VarDecl& varDecl, std::ostream& stream = std::cout)
{
    std::string key = varDecl.isConst ? "const " : varDecl.isVar ? "var " : "let ";
    PrintIndent(stream, indent, "VarDecl:", key + varDecl.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, varDecl, stream);
    PrintAnnotations(varDecl, indent + ONE_INDENT, stream);
    PrintModifiers(varDecl, indent, stream);
    PrintNode(varDecl.type.get(), indent + ONE_INDENT, "type", stream);
    PrintNode(varDecl.initializer.get(), indent + ONE_INDENT, "initializer", stream);
    PrintIndent(stream, indent, "}");
}

void PrintBasicpeAliasDecl(unsigned indent, const TypeAliasDecl& alias, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "TypeAliasDecl", alias.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, alias, stream);
    if (alias.generic) {
        PrintGeneric(indent + ONE_INDENT, *alias.generic, stream);
    }
    PrintNode(alias.type.get(), indent + ONE_INDENT, "type", stream);
    PrintIndent(stream, indent, "}");
}

void PrintClassDecl(unsigned indent, const ClassDecl& classDecl, std::ostream& stream = std::cout)
{
    if (auto attr = HasJavaAttr(classDecl); attr) {
        std::string argStr = attr.value() == Attribute::JAVA_APP ? "app" : "ext";
        PrintIndent(stream, indent, "@Java[\"" + argStr + "\"]");
    }
    PrintIndent(stream, indent, "ClassDecl:", classDecl.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, classDecl, stream);
    PrintAnnotations(classDecl, indent + ONE_INDENT, stream);
    PrintModifiers(classDecl, indent, stream);
    if (classDecl.generic) {
        PrintGeneric(indent + ONE_INDENT, *classDecl.generic, stream);
    }
    if (!classDecl.inheritedTypes.empty()) {
        PrintIndent(stream, indent + ONE_INDENT, "inheritedTypes [");
        for (auto& it : classDecl.inheritedTypes) {
            PrintNode(it.get(), indent + TWO_INDENT, "", stream);
        }
        PrintIndent(stream, indent + ONE_INDENT, "]");
    }
    PrintNode(classDecl.body.get(), indent + ONE_INDENT, "", stream);
    PrintIndent(stream, indent, "}");
}

void PrintInterfaceDecl(unsigned indent, const InterfaceDecl& interfaceDecl, std::ostream& stream = std::cout)
{
    if (auto attr = HasJavaAttr(interfaceDecl); attr) {
        PrintIndent(stream, indent, "@Java[\"", attr.value() == Attribute::JAVA_APP ? "app" : "ext", "\"]");
    }
    PrintIndent(stream, indent, "InterfaceDecl:", interfaceDecl.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, interfaceDecl, stream);
    PrintModifiers(interfaceDecl, indent, stream);
    if (interfaceDecl.generic) {
        PrintGeneric(indent + ONE_INDENT, *interfaceDecl.generic, stream);
    }
    if (!interfaceDecl.inheritedTypes.empty()) {
        PrintIndent(stream, indent + ONE_INDENT, "inheritedTypes: [");
        for (auto& it : interfaceDecl.inheritedTypes) {
            PrintNode(it.get(), indent + TWO_INDENT, "", stream);
        }
        PrintIndent(stream, indent + ONE_INDENT, "]");
    }
    PrintNode(interfaceDecl.body.get(), indent + ONE_INDENT, "", stream);
    PrintIndent(stream, indent, "}");
}

void PrintEnumDecl(unsigned indent, const EnumDecl& enumDecl, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "EnumDecl:", enumDecl.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, enumDecl, stream);
    PrintModifiers(enumDecl, indent, stream);
    if (enumDecl.generic) {
        PrintGeneric(indent + ONE_INDENT, *enumDecl.generic, stream);
    }
    if (!enumDecl.inheritedTypes.empty()) {
        PrintIndent(stream, indent + ONE_INDENT, "inheritedTypes [");
        for (auto& it : enumDecl.inheritedTypes) {
            PrintNode(it.get(), indent + TWO_INDENT, "", stream);
        }
        PrintIndent(stream, indent + ONE_INDENT, "]");
    }
    PrintIndent(stream, indent + ONE_INDENT, "constructors [");
    for (auto& it : enumDecl.constructors) {
        PrintNode(it.get(), indent + TWO_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");
    PrintIndent(stream, indent + ONE_INDENT, "members [");
    for (auto& it : enumDecl.members) {
        PrintNode(it.get(), indent + TWO_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");
    PrintIndent(stream, indent, "}");
}

void PrintStructDecl(unsigned indent, const StructDecl& decl, std::ostream& stream = std::cout)
{
    if (decl.TestAttr(Attribute::C)) {
        PrintIndent(stream, indent, "@C");
    }
    PrintIndent(stream, indent, "StructDecl:", decl.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, decl, stream);
    PrintAnnotations(decl, indent + ONE_INDENT, stream);
    PrintModifiers(decl, indent, stream);
    if (decl.generic) {
        PrintGeneric(indent + ONE_INDENT, *decl.generic, stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "StructBody {");
    PrintNode(decl.body.get(), indent + TWO_INDENT, "", stream);
    PrintIndent(stream, indent + ONE_INDENT, "}");
    PrintIndent(stream, indent, "}");
}

void PrintExtendDecl(unsigned indent, const ExtendDecl& ed, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "ExtendDecl {");
    PrintBasic(indent + ONE_INDENT, ed, stream);
    PrintNode(ed.extendedType.get(), indent + ONE_INDENT, "extendedType", stream);
    PrintIndent(stream, indent + ONE_INDENT, "inheritedTypes [");
    for (auto& interface : ed.inheritedTypes) {
        PrintNode(interface.get(), indent + TWO_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");
    if (ed.generic) {
        PrintGeneric(indent + ONE_INDENT, *ed.generic, stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "extendMembers [");
    for (auto& it : ed.members) {
        PrintNode(it.get(), indent + TWO_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");
    PrintIndent(stream, indent, "}");
}

void PrintIfExpr(unsigned indent, const IfExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "IfExpr {");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintNode(expr.condExpr.get(), indent + ONE_INDENT, "condExpr", stream);
    PrintNode(expr.thenBody.get(), indent + ONE_INDENT, "thenBody", stream);
    if (expr.hasElse) {
        PrintNode(expr.elseBody.get(), indent + ONE_INDENT, "elseBody", stream);
    }
    PrintIndent(stream, indent, "}");
}

void PrintMatchCase(unsigned indent, const MatchCase& mc, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "MatchCase: {");
    PrintBasic(indent + ONE_INDENT, mc, stream);
    PrintIndent(stream, indent + ONE_INDENT, "patterns {");
    for (auto& it : mc.patterns) {
        PrintNode(it.get(), indent + TWO_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "}");
    PrintNode(mc.patternGuard.get(), indent + ONE_INDENT, "patternGuard", stream);
    PrintIndent(stream, indent + ONE_INDENT, "exprOrDecls [");
    for (auto& it : mc.exprOrDecls->body) {
        PrintNode(it.get(), indent + TWO_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");

    PrintIndent(stream, indent, "}");
}

void PrintMatchExpr(unsigned indent, const MatchExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "MatchExpr:", "{");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    if (expr.matchMode) {
        PrintIndent(stream, indent + ONE_INDENT, "selector: {");
        PrintNode(expr.selector.get(), indent + TWO_INDENT, "", stream);
        PrintIndent(stream, indent + ONE_INDENT, "}");

        PrintIndent(stream, indent + ONE_INDENT, "matchCases: [");
        for (auto& matchCase : expr.matchCases) {
            PrintMatchCase(indent + TWO_INDENT, *StaticCast<MatchCase*>(matchCase.get()), stream);
        }
        PrintIndent(stream, indent + ONE_INDENT, "]");
    } else {
        for (auto& matchCaseOther : expr.matchCaseOthers) {
            PrintIndent(stream, indent + ONE_INDENT, "matchCases: {");
            PrintNode(matchCaseOther->matchExpr.get(), indent + TWO_INDENT, "", stream);
            PrintIndent(stream, indent + ONE_INDENT, "}");

            PrintIndent(stream, indent + ONE_INDENT, "exprOrDecls: [");
            for (auto& it : matchCaseOther->exprOrDecls->body) {
                PrintNode(it.get(), indent + ONE_INDENT, "", stream);
            }
            PrintIndent(stream, indent + ONE_INDENT, "]");
        }
    }
    PrintIndent(stream, indent, "}");
}

void PrintTryExpr(unsigned indent, const TryExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "TryExpr", "{");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    if (!expr.resourceSpec.empty()) {
        PrintIndent(stream, indent + ONE_INDENT, "ResourceSpec", "{");
        for (auto& i : expr.resourceSpec) {
            PrintNode(i.get(), indent + TWO_INDENT, "", stream);
        }
        PrintIndent(stream, indent + ONE_INDENT, "}");
    }
    PrintIndent(stream, indent + ONE_INDENT, "TryBlock", "{");
    PrintNode(expr.tryBlock.get(), indent + TWO_INDENT, "", stream);
    PrintIndent(stream, indent + ONE_INDENT, "}");
    for (unsigned int i = 0; i < expr.catchBlocks.size(); i++) {
        PrintIndent(stream, indent + ONE_INDENT, "Catch", "{");
        PrintIndent(stream, indent + TWO_INDENT, "CatchPattern", "{");
        PrintNode(expr.catchPatterns[i].get(), indent + THREE_INDENT, "", stream);
        PrintIndent(stream, indent + TWO_INDENT, "}");
        PrintIndent(stream, indent + TWO_INDENT, "CatchBlock", "{");
        PrintNode(expr.catchBlocks[i].get(), indent + THREE_INDENT, "", stream);
        PrintIndent(stream, indent + TWO_INDENT, "}");
        PrintIndent(stream, indent + ONE_INDENT, "}");
    }
    for (const auto& handler : expr.handlers) {
        PrintIndent(stream, indent + ONE_INDENT, "Handle", "{");
        PrintIndent(stream, indent + TWO_INDENT, "HandlePattern", "{");
        PrintIndent(stream, indent + THREE_INDENT, "CommandTypePattern:");
        PrintNode(handler.commandPattern.get(), indent + THREE_INDENT, "", stream);
        PrintIndent(stream, indent + TWO_INDENT, "}");
        PrintIndent(stream, indent + TWO_INDENT, "HandleBlock", "{");
        PrintNode(handler.block.get(), indent + THREE_INDENT, "", stream);
        PrintIndent(stream, indent + TWO_INDENT, "}");
        PrintIndent(stream, indent + TWO_INDENT, "DesugaredLambda", "{");
        PrintNode(handler.desugaredLambda.get(), indent + THREE_INDENT, "", stream);
        PrintIndent(stream, indent + TWO_INDENT, "}");
        PrintIndent(stream, indent + ONE_INDENT, "}");
    }
    PrintIndent(stream, indent + ONE_INDENT, "FinallyBlock", "{");
    PrintNode(expr.finallyBlock.get(), indent + TWO_INDENT, "", stream);
    PrintIndent(stream, indent + ONE_INDENT, "}");
    if (!expr.handlers.empty()) {
        PrintIndent(stream, indent + ONE_INDENT, "TryLambda", "{");
        PrintNode(expr.tryLambda.get(), indent + TWO_INDENT, "", stream);
        PrintIndent(stream, indent + ONE_INDENT, "}");
        PrintIndent(stream, indent + ONE_INDENT, "FinallyLambda", "{");
        PrintNode(expr.finallyLambda.get(), indent + TWO_INDENT, "", stream);
        PrintIndent(stream, indent + ONE_INDENT, "}");
    }
    PrintIndent(stream, indent, "}");
}

void PrintThrowExpr(unsigned indent, const ThrowExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "ThrowExpr", "{");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintNode(expr.expr.get(), indent + ONE_INDENT, "expr", stream);
    PrintIndent(stream, indent, "}");
}

void PrintPerformExpr(unsigned indent, const PerformExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "PerformExpr", "{");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintNode(expr.expr.get(), indent + ONE_INDENT, "PerformExpr", stream);
    PrintIndent(stream, indent, "}");
}

void PrintResumeExpr(unsigned indent, const ResumeExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "ResumeExpr", "{");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintNode(expr.withExpr.get(), indent + ONE_INDENT, "WithExpr", stream);
    PrintNode(expr.throwingExpr.get(), indent + ONE_INDENT, "ThrowingExpr", stream);
    PrintIndent(stream, indent, "}");
}

void PrintReturnExpr(unsigned indent, const ReturnExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "ReturnExpr", "{");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintNode(expr.expr.get(), indent + ONE_INDENT, "expr", stream);
    PrintIndent(stream, indent, "}");
}

void PrintJumpExpr(unsigned indent, const JumpExpr& expr, std::ostream& stream = std::cout)
{
    std::string str1 = expr.isBreak ? "Break" : "Continue";
    PrintIndent(stream, indent, "JumpExpr:", str1);
    PrintBasic(indent + ONE_INDENT, expr, stream);
}

void PrintForInExpr(unsigned indent, const ForInExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "ForInExpr {");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintIndent(stream, indent + ONE_INDENT, "pattern", ":");
    PrintNode(expr.pattern.get(), indent + TWO_INDENT, "", stream);
    PrintIndent(stream, indent + ONE_INDENT, "inExpression", ":");
    PrintNode(expr.inExpression.get(), indent + TWO_INDENT, "", stream);
    PrintIndent(stream, indent + ONE_INDENT, "patternGuard", ":");
    PrintNode(expr.patternGuard.get(), indent + TWO_INDENT, "", stream);
    PrintNode(expr.body.get(), indent + ONE_INDENT, "body", stream);
    PrintIndent(stream, indent, "}");
}

void PrintWhileExpr(unsigned indent, const WhileExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "WhileExpr {");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintIndent(stream, indent + ONE_INDENT, "condExpr", "{");
    PrintNode(expr.condExpr.get(), indent + TWO_INDENT, "condition expr", stream);
    PrintIndent(stream, indent + ONE_INDENT, "}");
    PrintNode(expr.body.get(), indent + ONE_INDENT, "While-Body", stream);
    PrintIndent(stream, indent, "}");
}

void PrintDoWhileExpr(unsigned indent, const DoWhileExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "DoWhileExpr {");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintNode(expr.body.get(), indent + ONE_INDENT, "DoWhile-Body", stream);
    PrintIndent(stream, indent + ONE_INDENT, "condExpr", "{");
    PrintNode(expr.condExpr.get(), indent + TWO_INDENT, "", stream);
    PrintIndent(stream, indent + ONE_INDENT, "}");
    PrintIndent(stream, indent, "}");
}

void PrintAssignExpr(unsigned indent, const AssignExpr& expr, std::ostream& stream = std::cout)
{
    const std::unordered_map<TokenKind, std::string> op2str = {
        {TokenKind::ASSIGN, "'='"},
        {TokenKind::ADD_ASSIGN, "'+='"},
        {TokenKind::SUB_ASSIGN, "'-='"},
        {TokenKind::MUL_ASSIGN, "'*='"},
        {TokenKind::EXP_ASSIGN, "'**='"},
        {TokenKind::DIV_ASSIGN, "'/='"},
        {TokenKind::MOD_ASSIGN, "'%%='"},
        {TokenKind::AND_ASSIGN, "'&&='"},
        {TokenKind::OR_ASSIGN, "'||='"},
        {TokenKind::BITAND_ASSIGN, "'&='"},
        {TokenKind::BITOR_ASSIGN, "'|='"},
        {TokenKind::BITXOR_ASSIGN, "'^='"},
        {TokenKind::LSHIFT_ASSIGN, "'<<='"},
        {TokenKind::RSHIFT_ASSIGN, "'>>='"},
    };
    PrintIndent(stream, indent, "AssignExpr", "{");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    std::string str1;
    auto result = op2str.find(expr.op);
    if (result != op2str.end()) {
        str1 = result->second;
    }
    PrintIndent(stream, indent + ONE_INDENT, str1);
    PrintNode(expr.leftValue.get(), indent + ONE_INDENT, "leftValue", stream);
    PrintNode(expr.rightExpr.get(), indent + ONE_INDENT, "rightExpr", stream);
    if (expr.overflowStrategy != OverflowStrategy::NA) {
        PrintIndent(stream, indent + ONE_INDENT, "overflowStrategy:", OverflowStrategyName(expr.overflowStrategy));
    }
    PrintIndent(stream, indent, "}");
}

void PrintIncOrDecExpr(unsigned indent, const IncOrDecExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "IncOrDecExpr", "{");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    std::string str1 = (expr.op == TokenKind::INCR) ? "++" : "--";
    PrintIndent(stream, indent + ONE_INDENT, str1);
    PrintNode(expr.expr.get(), indent + ONE_INDENT, "", stream);
    if (expr.overflowStrategy != OverflowStrategy::NA) {
        PrintIndent(stream, indent + ONE_INDENT, "overflowStrategy:", OverflowStrategyName(expr.overflowStrategy));
    }
    PrintIndent(stream, indent, "}");
}

void PrintUnaryExpr(unsigned indent, const UnaryExpr& unaryExpr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "UnaryExpr", "{", TOKENS[static_cast<int>(unaryExpr.op)]);
    PrintBasic(indent + ONE_INDENT, unaryExpr, stream);
    PrintNode(unaryExpr.expr.get(), indent + ONE_INDENT, "", stream);
    if (unaryExpr.overflowStrategy != OverflowStrategy::NA) {
        PrintIndent(stream, indent + ONE_INDENT, "overflowStrategy:", OverflowStrategyName(unaryExpr.overflowStrategy));
    }
    PrintIndent(stream, indent, "}");
}

void PrintBinaryExpr(unsigned indent, const BinaryExpr& binaryExpr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "BinaryExpr ", TOKENS[static_cast<int>(binaryExpr.op)], "{");
    PrintBasic(indent + ONE_INDENT, binaryExpr, stream);
    PrintNode(binaryExpr.leftExpr.get(), indent + ONE_INDENT, "leftExpr:", stream);
    PrintNode(binaryExpr.rightExpr.get(), indent + ONE_INDENT, "rightExpr:", stream);
    if (binaryExpr.overflowStrategy != OverflowStrategy::NA) {
        PrintIndent(
            stream, indent + ONE_INDENT, "overflowStrategy:", OverflowStrategyName(binaryExpr.overflowStrategy));
    }
    PrintIndent(stream, indent, "}");
}

void PrintRangeExpr(unsigned indent, const RangeExpr& rangeExpr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "RangeExpr {");
    PrintBasic(indent + ONE_INDENT, rangeExpr, stream);
    PrintIndent(stream, indent + ONE_INDENT, "Start:");
    PrintNode(rangeExpr.startExpr.get(), indent + TWO_INDENT, "", stream);
    PrintIndent(stream, indent + ONE_INDENT, "Stop:");
    PrintNode(rangeExpr.stopExpr.get(), indent + TWO_INDENT, "", stream);
    PrintIndent(stream, indent + ONE_INDENT, "Step:");
    PrintNode(rangeExpr.stepExpr.get(), indent + TWO_INDENT, "", stream);
    PrintIndent(stream, indent, "}");
}

void PrintSubscriptExpr(unsigned indent, const SubscriptExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "SubscriptExpr {");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintNode(expr.baseExpr.get(), indent + ONE_INDENT, "baseExpr", stream);
    PrintIndent(stream, indent + ONE_INDENT, "indexExprs [");
    for (auto& it : expr.indexExprs) {
        PrintNode(it.get(), indent + TWO_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");
    PrintIndent(stream, indent, "}");
}

void PrintMemberAccess(unsigned indent, const MemberAccess& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "MemberAccess", "{");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintNode(expr.baseExpr.get(), indent + ONE_INDENT, "baseExpr", stream);
    PrintIndent(stream, indent + ONE_INDENT, "field:", expr.field.Val());
    if (expr.typeArguments.empty()) {
        PrintIndent(stream, indent + ONE_INDENT, "// no type arguments");
    } else {
        PrintIndent(stream, indent + ONE_INDENT, "typeArguments [");
        for (auto& it : expr.typeArguments) {
            PrintNode(it.get(), indent + TWO_INDENT, "", stream);
        }
        PrintIndent(stream, indent + ONE_INDENT, "]");
    }
    if (expr.target) {
        PrintTarget(indent + ONE_INDENT, *expr.target, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "targets [");
    for (auto t : expr.targets) {
        PrintTarget(indent + TWO_INDENT, *t, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");
    PrintIndent(stream, indent, "}");
}

void PrintOptionalExpr(unsigned indent, const OptionalExpr& optionalExpr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "OptionalExpr", "{");
    PrintBasic(indent + ONE_INDENT, optionalExpr, stream);
    PrintIndent(stream, indent + ONE_INDENT, "BaseExpr:");
    PrintNode(optionalExpr.baseExpr.get(), indent + ONE_INDENT, "", stream);
    PrintIndent(stream, indent, "}");
}

void PrintOptionalChainExpr(
    unsigned indent, const OptionalChainExpr& optionalChainExpr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "OptionalChainExpr", "{");
    PrintBasic(indent + ONE_INDENT, optionalChainExpr, stream);
    PrintIndent(stream, indent + ONE_INDENT, "Expr:");
    PrintNode(optionalChainExpr.expr.get(), indent + ONE_INDENT, "", stream);
    PrintIndent(stream, indent, "}");
}

void PrintLetPatternDestructor(unsigned indent, const LetPatternDestructor& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "LetPattern", "{");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintIndent(stream, indent + ONE_INDENT, "patterns [");
    for (auto& p : expr.patterns) {
        PrintIndent(stream, indent + TWO_INDENT, "|", "{");
        PrintNode(p.get(), indent + THREE_INDENT, "", stream);
        PrintIndent(stream, indent + TWO_INDENT, "}");
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");
    PrintIndent(stream, indent + ONE_INDENT, "Initializer:", "{");
    PrintNode(expr.initializer.get(), indent + TWO_INDENT, "", stream);
    PrintIndent(stream, indent + ONE_INDENT, "}");
    PrintIndent(stream, indent, "}");
}

void PrintFuncArg(unsigned indent, const FuncArg& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "FuncArg", "{");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    if (!expr.name.Empty()) {
        PrintIndent(stream, indent + ONE_INDENT, "ArgName:", expr.name.Val());
    }
    if (expr.withInout) {
        PrintIndent(stream, indent + ONE_INDENT, "inout");
    }
    PrintNode(expr.expr.get(), indent + ONE_INDENT, "", stream);
    PrintIndent(stream, indent, "}");
}

void PrintCallExpr(unsigned indent, const CallExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "CallExpr {");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintIndent(stream, indent + ONE_INDENT, "BaseFunc {");
    PrintNode(expr.baseFunc.get(), indent + TWO_INDENT, "", stream);
    PrintIndent(stream, indent + ONE_INDENT, "}");
    if (expr.args.empty()) {
        PrintIndent(stream, indent + ONE_INDENT, "// no arguments");
    } else {
        PrintIndent(stream, indent + ONE_INDENT, "arguments [");
        for (auto& it : expr.args) {
            PrintNode(it.get(), indent + TWO_INDENT, "", stream);
        }
        PrintIndent(stream, indent + ONE_INDENT, "]");
    }
    if (expr.resolvedFunction) {
        PrintTarget(indent + ONE_INDENT, *expr.resolvedFunction, "resolvedFunction", stream);
    }
    PrintIndent(stream, indent, "}");
}

void PrintParenExpr(unsigned indent, const ParenExpr& parenExpr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "ParenExpr {");
    PrintBasic(indent + ONE_INDENT, parenExpr, stream);
    PrintNode(parenExpr.expr.get(), indent + ONE_INDENT, "", stream);
    PrintIndent(stream, indent, "}");
}

void PrintLambdaExpr(unsigned indent, const LambdaExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "LambdaExpr {");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintIndent(stream, indent + ONE_INDENT, "mangledName:", expr.mangledName);
    PrintNode(expr.funcBody.get(), indent + ONE_INDENT, "LambdaExpr-Body", stream);
    PrintIndent(stream, indent, "}");
}

void PrintLitConstExpr(unsigned indent, const LitConstExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "LitConstExpr:", expr.LitKindToString(),
        "\"" + StringConvertor::Recover(expr.stringValue) + "\"", "{");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    if (expr.siExpr) {
        PrintNode(expr.siExpr.get(), indent + ONE_INDENT, "siExpr", stream);
    }
    PrintIndent(stream, indent, "}");
}

void PrintInterpolationExpr(unsigned indent, const InterpolationExpr& ie, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "InterpolationExpr:", "\"" + ie.rawString + "\"", "{");
    PrintBasic(indent + ONE_INDENT, ie, stream);
    PrintNode(ie.block.get(), indent + ONE_INDENT, "block", stream);
    PrintIndent(stream, indent, "}");
}

void PrintStrInterpolationExpr(unsigned indent, const StrInterpolationExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "StrInterpolationExpr:", "\"" + expr.rawString + "\"", "{");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintIndent(stream, indent + ONE_INDENT, "strPartExprs [");
    for (auto& sp : expr.strPartExprs) {
        PrintNode(sp.get(), indent + TWO_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");
    PrintIndent(stream, indent, "}");
}

void PrintArrayLit(unsigned indent, const ArrayLit& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "ArrayLiteral", "{");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    if (expr.children.empty()) {
        PrintIndent(stream, indent + ONE_INDENT, "// no members");
    }
    PrintIndent(stream, indent + ONE_INDENT, "children [");
    for (auto& it : expr.children) {
        PrintNode(it.get(), indent + TWO_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");
    PrintIndent(stream, indent, "}");
}

void PrintArrayExpr(unsigned indent, const ArrayExpr& expr, std::ostream& stream = std::cout)
{
    auto typeName = expr.isValueArray ? "VArrayExpr" : "ArrayExpr";
    PrintIndent(stream, indent, typeName, "{");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintIndent(stream, indent + ONE_INDENT, "args [");
    for (size_t i = 0; i < expr.args.size(); ++i) {
        PrintNode(expr.args[i].get(), indent + TWO_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");
    PrintIndent(stream, indent, "}");
}

void PrintTupleLit(unsigned indent, const TupleLit& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "TupleLiteral", "{");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    for (auto& it : expr.children) {
        PrintNode(it.get(), indent + ONE_INDENT, "", stream);
    }
    PrintIndent(stream, indent, "}");
}

void PrintBasicpeConvExpr(unsigned indent, const TypeConvExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "TypeConvExpr", "{");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintIndent(stream, indent + ONE_INDENT, "// Convert Type:");
    PrintNode(expr.type.get(), indent + TWO_INDENT, "", stream);
    PrintIndent(stream, indent + ONE_INDENT, "// Expr:");
    PrintNode(expr.expr.get(), indent + TWO_INDENT, "", stream);
    PrintIndent(stream, indent, "}");
}

void PrintRefExpr(unsigned indent, const RefExpr& refExpr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "RefExpr:", refExpr.ref.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, refExpr, stream);
    if (refExpr.typeArguments.empty()) {
        PrintIndent(stream, indent + ONE_INDENT, "// no type arguments");
    } else {
        PrintIndent(stream, indent + ONE_INDENT, "typeArguments [");
        for (auto& it : refExpr.typeArguments) {
            PrintNode(it.get(), indent + TWO_INDENT, "", stream);
        }
        PrintIndent(stream, indent + ONE_INDENT, "]");
    }
    if (refExpr.ref.target) {
        PrintTarget(indent + ONE_INDENT, *refExpr.ref.target, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "targets [");
    for (auto t : refExpr.ref.targets) {
        PrintTarget(indent + TWO_INDENT, *t, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");
    PrintIndent(stream, indent, "}");
}

void PrintSpawnExpr(unsigned indent, const SpawnExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "SpawnExpr {");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    if (expr.futureObj) {
        PrintNode(expr.futureObj.get(), indent + ONE_INDENT, "", stream);
    }
    if (expr.arg) {
        PrintNode(expr.arg.get(), indent + ONE_INDENT, "", stream);
    }
    if (expr.task) {
        PrintNode(expr.task.get(), indent + ONE_INDENT, "", stream);
    }
    PrintIndent(stream, indent, "}");
}

void PrintSynchronizedExpr(unsigned indent, const SynchronizedExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "SynchronizedExpr {");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintNode(expr.mutex.get(), indent + TWO_INDENT, "mutex", stream);
    PrintNode(expr.body.get(), indent + TWO_INDENT, "body", stream);
    PrintIndent(stream, indent, "}");
}

void PrintIfAvailableExpr(unsigned indent, const IfAvailableExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "IfAvailableExpr {");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintNode(expr.GetArg(), indent + TWO_INDENT, "arg", stream);
    PrintNode(expr.GetLambda1(), indent + TWO_INDENT, "lambda1", stream);
    PrintNode(expr.GetLambda2(), indent + TWO_INDENT, "lamdba2", stream);
    PrintIndent(stream, indent, "}");
}

void PrintBlock(unsigned indent, const Block& block, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "Block: {");
    PrintBasic(indent + ONE_INDENT, block, stream);
    if (block.body.empty()) {
        PrintIndent(stream, indent + ONE_INDENT, "// no block");
    } else {
        for (auto& it : block.body) {
            PrintNode(it.get(), indent + ONE_INDENT, "", stream);
        }
    }
    PrintIndent(stream, indent, "}");
}

void PrintInterfaceBody(unsigned indent, const InterfaceBody& body, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "InterfaceBody {");
    for (auto& it : body.decls) {
        PrintNode(it.get(), indent + ONE_INDENT, "", stream);
    }
    PrintIndent(stream, indent, "}");
}

void PrintClassBody(unsigned indent, const ClassBody& body, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "ClassBody {");
    for (auto& it : body.decls) {
        PrintNode(it.get(), indent + ONE_INDENT, "", stream);
    }
    PrintIndent(stream, indent, "}");
}

void PrintStructBody(unsigned indent, const StructBody& body, std::ostream& stream = std::cout)
{
    for (auto& it : body.decls) {
        PrintNode(it.get(), indent, "", stream);
    }
}

void PrintIsExpr(unsigned indent, const IsExpr& isExpr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "IsExpr : ", "{");
    PrintBasic(indent + ONE_INDENT, isExpr, stream);
    PrintNode(isExpr.leftExpr.get(), indent + ONE_INDENT, "leftExpr", stream);
    PrintNode(isExpr.isType.get(), indent + ONE_INDENT, "isType", stream);
    PrintIndent(stream, indent, "}");
}

void PrintAsExpr(unsigned indent, const AsExpr& asExpr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "AsExpr: {");
    PrintBasic(indent + ONE_INDENT, asExpr, stream);
    PrintNode(asExpr.leftExpr.get(), indent + ONE_INDENT, "leftExpr", stream);
    PrintNode(asExpr.asType.get(), indent + ONE_INDENT, "asType", stream);
    PrintIndent(stream, indent, "}");
}

void PrintTokenPart(unsigned indent, const AST::TokenPart& tokenPart, std::ostream& stream = std::cout)
{
    PrintIndentTokens(indent + ONE_INDENT, tokenPart.tokens, stream);
}

void PrintQuoteExpr(unsigned indent, const AST::QuoteExpr& qe, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "QuoteExpr {");
    for (auto& expr : qe.exprs) {
        if (expr->astKind == ASTKind::TOKEN_PART) {
            PrintIndent(stream, indent + ONE_INDENT, "TokenPart", "(");
        } else {
            PrintIndent(stream, indent + ONE_INDENT, "DollarExpr", "(");
        }
        PrintNode(expr.get(), indent + TWO_INDENT, "", stream);
        PrintIndent(stream, indent + ONE_INDENT, ")");
    }
    PrintIndent(stream, indent, "}");
}

void PrintMacroExpandExpr(unsigned indent, const AST::MacroExpandExpr& expr, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "MacroExpand:", expr.invocation.fullName, "{");
    PrintBasic(indent + ONE_INDENT, expr, stream);
    PrintMacroInvocation(indent, expr.invocation, stream);
    PrintIndent(stream, indent, "}");
}

void PrintRefType(unsigned indent, const RefType& refType, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "RefType:", refType.ref.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, refType, stream);
    if (refType.typeArguments.empty()) {
        PrintIndent(stream, indent + ONE_INDENT, "// no type arguments");
    } else {
        PrintIndent(stream, indent + ONE_INDENT, "typeArguments [");
        for (auto& it : refType.typeArguments) {
            PrintNode(it.get(), indent + TWO_INDENT, "", stream);
        }
        PrintIndent(stream, indent + ONE_INDENT, "]");
    }
    if (refType.ref.target) {
        PrintTarget(indent + ONE_INDENT, *refType.ref.target, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "targets [");
    for (auto t : refType.ref.targets) {
        PrintTarget(indent + TWO_INDENT, *t, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");
    PrintIndent(stream, indent, "}");
}

void PrintParenType(unsigned indent, const ParenType& type, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "ParenType {");
    PrintBasic(indent + ONE_INDENT, type, stream);
    PrintNode(type.type.get(), indent + ONE_INDENT, "", stream);
    PrintIndent(stream, indent, "}");
}

void PrintFuncType(unsigned indent, const FuncType& type, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "FuncType {");
    PrintBasic(indent + ONE_INDENT, type, stream);
    PrintIndent(stream, indent + ONE_INDENT, "paramTypes [");
    for (auto& paramType : type.paramTypes) {
        PrintNode(paramType.get(), indent + TWO_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");
    PrintNode(type.retType.get(), indent + ONE_INDENT, "retType", stream);
    PrintIndent(stream, indent + ONE_INDENT, "IsCFuncType:", type.isC);
    PrintIndent(stream, indent, "}");
}

void PrintTupleType(unsigned indent, const TupleType& type, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "TupleType {");
    PrintBasic(indent + ONE_INDENT, type, stream);
    PrintIndent(stream, indent + ONE_INDENT, "fieldTypes [");
    for (auto& it : type.fieldTypes) {
        PrintNode(it.get(), indent + TWO_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");
    PrintIndent(stream, indent, "}");
}

void PrintThisType(unsigned indent, const ThisType& type, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "ThisType {");
    PrintBasic(indent + ONE_INDENT, type, stream);
    PrintIndent(stream, indent, "}");
}

void PrintPrimitiveType(unsigned indent, const PrimitiveType& type, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "PrimitiveType:", type.str, "{");
    PrintBasic(indent + ONE_INDENT, type, stream);
    PrintIndent(stream, indent, "}");
}

void PrintOptionType(unsigned indent, const OptionType& type, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "OptionType {");
    PrintBasic(indent + ONE_INDENT, type, stream);
    PrintIndent(stream, indent + ONE_INDENT, "BaseType:");
    PrintNode(type.componentType.get(), indent + TWO_INDENT, "", stream);
    PrintIndent(stream, indent + ONE_INDENT, "QuestNums:", type.questNum);
    PrintIndent(stream, indent, "}");
}

void PrintVArrayType(unsigned indent, const VArrayType& type, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "VArrayType {");
    PrintBasic(indent + ONE_INDENT, type, stream);
    PrintIndent(stream, indent + ONE_INDENT, "typeArguments:");
    PrintNode(type.typeArgument.get(), indent + TWO_INDENT, "", stream);
    PrintIndent(stream, indent, "}");
}

void PrintQualifiedType(unsigned indent, const QualifiedType& type, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "QualifiedType {");
    PrintBasic(indent + ONE_INDENT, type, stream);
    PrintNode(type.baseType.get(), indent + ONE_INDENT, "baseType", stream);
    PrintIndent(stream, indent + ONE_INDENT, "field:", type.field.Val());
    if (type.target) {
        PrintTarget(indent + ONE_INDENT, *type.target, "", stream);
    }
    PrintIndent(stream, indent, "}");
}

void PrintGenericConstraint(unsigned indent, const GenericConstraint& generic, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "GenericConstraint {");
    PrintNode(generic.type.get(), indent + ONE_INDENT, "", stream);
    PrintIndent(stream, indent + ONE_INDENT, "upperBounds {");
    for (auto& upperBound : generic.upperBounds) {
        PrintNode(upperBound.get(), indent + TWO_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "}");
    PrintIndent(stream, indent, "}");
}

void PrintConstPattern(unsigned indent, const ConstPattern& constPattern, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "ConstPattern {");
    PrintNode(constPattern.literal.get(), indent + ONE_INDENT, "", stream);
    PrintIndent(stream, indent, "}");
}

void PrintTuplePattern(unsigned indent, const TuplePattern& tuplePattern, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "TuplePattern {");
    PrintIndent(stream, indent + ONE_INDENT, "patterns [");
    for (auto& pattern : tuplePattern.patterns) {
        PrintNode(pattern.get(), indent + ONE_INDENT, "", stream);
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");
    PrintIndent(stream, indent, "}");
}

void PrintBasicpePattern(unsigned indent, const TypePattern& typePattern, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "TypePattern {");
    PrintBasic(indent + ONE_INDENT, typePattern, stream);
    PrintNode(typePattern.pattern.get(), indent + ONE_INDENT, "pattern", stream);
    PrintNode(typePattern.type.get(), indent + ONE_INDENT, "type", stream);
    if (typePattern.desugarVarPattern.get()) {
        PrintNode(typePattern.desugarVarPattern.get(), indent + ONE_INDENT, "// desugarVarPattern", stream);
    }
    PrintIndent(stream, indent, "}");
}

void PrintVarPattern(unsigned indent, const VarPattern& varPattern, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "VarPattern:", varPattern.varDecl->identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, varPattern, stream);
    PrintNode(varPattern.varDecl, indent + ONE_INDENT, "varDecl", stream);
    PrintIndent(stream, indent, "}");
}

void PrintEnumPattern(unsigned indent, const EnumPattern& enumPattern, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "EnumPattern {");
    PrintNode(enumPattern.constructor.get(), indent + ONE_INDENT, "", stream);
    for (auto& pattern : enumPattern.patterns) {
        PrintNode(pattern.get(), indent + ONE_INDENT, "", stream);
    }
    PrintIndent(stream, indent, "}");
}

void PrintExceptTypePattern(
    unsigned indent, const ExceptTypePattern& exceptTypePattern, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "ExceptTypePattern {");
    PrintNode(exceptTypePattern.pattern.get(), indent + ONE_INDENT, "", stream);
    for (auto& type : exceptTypePattern.types) {
        PrintNode(type.get(), indent + ONE_INDENT, "", stream);
    }
    PrintIndent(stream, indent, "}");
}

void PrintVarOrEnumPattern(unsigned indent, const VarOrEnumPattern& ve, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "VarOrEnumPattern {");
    PrintIndent(stream, indent + ONE_INDENT, ve.identifier);
    if (ve.pattern) {
        PrintIndent(stream, indent + ONE_INDENT, "Subpatterns {");
        PrintNode(ve.pattern, indent + TWO_INDENT, "", stream);
        PrintIndent(stream, indent + ONE_INDENT, "}");
    }
    PrintIndent(stream, indent, "}");
}

void PrintCommandTypePattern(
    unsigned indent, const CommandTypePattern& commandTypePattern, std::ostream& stream = std::cout)
{
    PrintNode(commandTypePattern.pattern.get(), indent + ONE_INDENT, "", stream);
    for (auto& type : commandTypePattern.types) {
        PrintNode(type.get(), indent + ONE_INDENT, "", stream);
    }
}

void PrintFeaturesDirective(
    unsigned indent, const FeaturesDirective& featuresDirective, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "FeaturesDirective:", "features", "{");
    PrintIndent(stream, indent + ONE_INDENT, "ids: ", "[");
    if (!featuresDirective.content.empty()) {
        std::stringstream ss;
        for (size_t i = 0; i < featuresDirective.content.size(); i++) {
            ss << featuresDirective.content[i].ToString();
            if (i < featuresDirective.content.size() - 1) { ss << ", "; }
        }
        PrintIndent(stream, indent + TWO_INDENT, ss.str());
    } else {
        PrintIndent(stream, indent + TWO_INDENT, "// no feature arguments");
    }
    PrintIndent(stream, indent + ONE_INDENT, "]");
    PrintIndent(
        stream, indent + ONE_INDENT, "position:", featuresDirective.begin.ToString(), featuresDirective.end.ToString());
    PrintIndent(stream, indent, "}");
}

void PrintPackageSpec(unsigned indent, const PackageSpec& package, std::ostream& stream = std::cout)
{
    PrintIndent(stream, indent, "PackageSpec:", package.packageName.Val(), "{");
    PrintIndent(stream, indent + ONE_INDENT, package.GetPackageName());
    if (!package.prefixPaths.empty()) {
        auto prefixPath = JoinStrings(package.prefixPaths, ".");
        PrintIndent(stream, indent + ONE_INDENT, "prefixPath:", std::move(prefixPath));
    }
    if (package.modifier) {
        PrintIndent(stream, indent + ONE_INDENT, "modifier:", TOKENS[static_cast<int>(package.modifier->modifier)]);
    }
    PrintIndent(stream, indent, "}");
}

void PrintImportSpec(unsigned indent, const ImportSpec& import, std::ostream& stream = std::cout)
{
    if (import.content.kind != ImportKind::IMPORT_MULTI) {
        PrintIndent(stream, indent, "ImportSpec:", import.content.identifier.Val(), "{");
        if (import.content.kind == ImportKind::IMPORT_ALIAS) {
            PrintIndent(stream, indent + ONE_INDENT, "aliasName:", import.content.aliasName.Val());
        }
        if (!import.content.prefixPaths.empty()) {
            PrintIndent(stream, indent + ONE_INDENT, "prefixPaths:", import.content.GetPrefixPath());
        }
        if (import.modifier) {
            PrintIndent(stream, indent + ONE_INDENT, "modifier:", TOKENS[static_cast<int>(import.modifier->modifier)]);
        }
        PrintIndent(stream, indent + ONE_INDENT, "isDecl:", import.content.isDecl);
        PrintIndent(stream, indent, "}");
    } else {
        std::string commonPrefix = import.content.GetPrefixPath();
        for (const auto& item : import.content.items) {
            PrintIndent(stream, indent, "ImportSpec:", item.identifier.Val(), "{");
            if (item.kind == ImportKind::IMPORT_ALIAS) {
                PrintIndent(stream, indent + ONE_INDENT, "aliasName:", item.aliasName.Val());
            }
            PrintIndent(stream, indent + ONE_INDENT, "prefixPaths:", item.GetPrefixPath());
            if (import.modifier) {
                PrintIndent(
                    stream, indent + ONE_INDENT, "modifier:", TOKENS[static_cast<int>(import.modifier->modifier)]);
            }
            PrintIndent(stream, indent, "}");
        }
    }
}
} // namespace

void PrintNode(Ptr<const Node> node, unsigned indent, const std::string& addition, std::ostream& stream)
{
    if (!addition.empty()) {
        PrintIndent(stream, indent, "//", node ? addition : addition + " nullptr");
    }
    if (!node) {
        return;
    }
    if (node->IsExpr()) {
        auto expr = RawStaticCast<const Expr*>(node);
        if (expr->desugarExpr) {
            return PrintNode(expr->desugarExpr.get(), indent, addition, stream);
        }
    }
    match (*node)([&indent, &stream](const Package& package) { PrintPackage(indent, package, stream); },
        [&indent, &stream](const File& file) { PrintFile(indent, file, stream); },
        // ----------- Decls --------------------
        [&indent, &stream](const GenericParamDecl& gpd) { PrintGenericParamDecl(indent, gpd, stream); },
        [&indent, &stream](const FuncParam& param) { PrintFuncParam(indent, param, stream); },
        [&indent, &stream](const MacroExpandParam& macroExpand) { PrintMacroExpandParam(indent, macroExpand, stream); },
        [&indent, &stream](const FuncParamList& paramList) { PrintFuncParamList(indent, paramList, stream); },
        [&indent, &stream](const MainDecl& mainDecl) { PrintMainDecl(indent, mainDecl, stream); },
        [&indent, &stream](const FuncDecl& funcDecl) { PrintFuncDecl(indent, funcDecl, stream); },
        [&indent, &stream](const MacroDecl& macroDecl) { PrintMacroDecl(indent, macroDecl, stream); },
        [&indent, &stream](const FuncBody& body) { PrintFuncBody(indent, body, stream); },
        [&indent, &stream](const PropDecl& propDecl) { PrintPropDecl(indent, propDecl, stream); },
        [&indent, &stream](const MacroExpandDecl& macroExpand) { PrintMacroExpandDecl(indent, macroExpand, stream); },
        [&indent, &stream](const VarWithPatternDecl& vwpd) { PrintVarWithPatternDecl(indent, vwpd, stream); },
        [&indent, &stream](const VarDecl& varDecl) { PrintVarDecl(indent, varDecl, stream); },
        [&indent, &stream](const TypeAliasDecl& alias) { PrintBasicpeAliasDecl(indent, alias, stream); },
        [&indent, &stream](const ClassDecl& classDecl) { PrintClassDecl(indent, classDecl, stream); },
        [&indent, &stream](const InterfaceDecl& interfaceDecl) { PrintInterfaceDecl(indent, interfaceDecl, stream); },
        [&indent, &stream](const EnumDecl& enumDecl) { PrintEnumDecl(indent, enumDecl, stream); },
        [&indent, &stream](const StructDecl& decl) { PrintStructDecl(indent, decl, stream); },
        [&indent, &stream](const ExtendDecl& ed) { PrintExtendDecl(indent, ed, stream); },
        [&indent, &stream](const PrimaryCtorDecl& pc) { PrintPrimaryCtorDecl(indent, pc, stream); },
        // -----------Exprs----------------------
        [&indent, &stream](const IfExpr& expr) { PrintIfExpr(indent, expr, stream); },
        [&indent, &stream](const MatchExpr& expr) { PrintMatchExpr(indent, expr, stream); },
        [&indent, &stream](const TryExpr& expr) { PrintTryExpr(indent, expr, stream); },
        [&indent, &stream](const ThrowExpr& expr) { PrintThrowExpr(indent, expr, stream); },
        [&indent, &stream](const PerformExpr& expr) { PrintPerformExpr(indent, expr, stream); },
        [&indent, &stream](const ResumeExpr& expr) { PrintResumeExpr(indent, expr, stream); },
        [&indent, &stream](const ReturnExpr& expr) { PrintReturnExpr(indent, expr, stream); },
        [&indent, &stream](const JumpExpr& expr) { PrintJumpExpr(indent, expr, stream); },
        [&indent, &stream](const ForInExpr& expr) { PrintForInExpr(indent, expr, stream); },
        [&indent, &stream](const WhileExpr& expr) { PrintWhileExpr(indent, expr, stream); },
        [&indent, &stream](const DoWhileExpr& expr) { PrintDoWhileExpr(indent, expr, stream); },
        [&indent, &stream](const AssignExpr& expr) { PrintAssignExpr(indent, expr, stream); },
        [&indent, &stream](const IncOrDecExpr& expr) { PrintIncOrDecExpr(indent, expr, stream); },
        [&indent, &stream](const UnaryExpr& unaryExpr) { PrintUnaryExpr(indent, unaryExpr, stream); },
        [&indent, &stream](const BinaryExpr& binaryExpr) { PrintBinaryExpr(indent, binaryExpr, stream); },
        [&indent, &stream](const RangeExpr& rangeExpr) { PrintRangeExpr(indent, rangeExpr, stream); },
        [&indent, &stream](const SubscriptExpr& expr) { PrintSubscriptExpr(indent, expr, stream); },
        [&indent, &stream](const MemberAccess& expr) { PrintMemberAccess(indent, expr, stream); },
        [&indent, &stream](const FuncArg& expr) { PrintFuncArg(indent, expr, stream); },
        [&indent, &stream](const CallExpr& expr) { PrintCallExpr(indent, expr, stream); },
        [&indent, &stream](const ParenExpr& parenExpr) { PrintParenExpr(indent, parenExpr, stream); },
        [&indent, &stream](const LambdaExpr& expr) { PrintLambdaExpr(indent, expr, stream); },
        [&indent, &stream](const LitConstExpr& expr) { PrintLitConstExpr(indent, expr, stream); },
        [&indent, &stream](const InterpolationExpr& expr) { PrintInterpolationExpr(indent, expr, stream); },
        [&indent, &stream](const StrInterpolationExpr& expr) { PrintStrInterpolationExpr(indent, expr, stream); },
        [&indent, &stream](const ArrayLit& expr) { PrintArrayLit(indent, expr, stream); },
        [&indent, &stream](const ArrayExpr& expr) { PrintArrayExpr(indent, expr, stream); },
        [&indent, &stream](const TupleLit& expr) { PrintTupleLit(indent, expr, stream); },
        [&indent, &stream](const TypeConvExpr& expr) { PrintBasicpeConvExpr(indent, expr, stream); },
        [&indent, &stream](const RefExpr& refExpr) { PrintRefExpr(indent, refExpr, stream); },
        [&indent, &stream](const OptionalExpr& expr) { PrintOptionalExpr(indent, expr, stream); },
        [&indent, &stream](const OptionalChainExpr& expr) { PrintOptionalChainExpr(indent, expr, stream); },
        [&indent, &stream](const LetPatternDestructor& expr) { PrintLetPatternDestructor(indent, expr, stream); },
        [&indent, &stream](
            const PrimitiveTypeExpr& pte) { PrintIndent(stream, indent, "PrimitiveTypeExpr: " + pte.ty->String()); },
        [&indent, &stream](const SpawnExpr& expr) { PrintSpawnExpr(indent, expr, stream); },
        [&indent, &stream](const SynchronizedExpr& expr) { PrintSynchronizedExpr(indent, expr, stream); },
        [&indent, &stream](
            const InvalidExpr& /* expr */) { PrintIndent(stream, indent, "InvalidExpr: Need to be fixed!"); },
        [&indent, &stream](const Block& block) { PrintBlock(indent, block, stream); },
        [&indent, &stream](const InterfaceBody& body) { PrintInterfaceBody(indent, body, stream); },
        [&indent, &stream](const ClassBody& body) { PrintClassBody(indent, body, stream); },
        [&indent, &stream](const StructBody& body) { PrintStructBody(indent, body, stream); },
        [&indent, &stream](const IsExpr& isExpr) { PrintIsExpr(indent, isExpr, stream); },
        [&indent, &stream](const AsExpr& asExpr) { PrintAsExpr(indent, asExpr, stream); },
        [&indent, &stream](const TokenPart& tokenPart) { PrintTokenPart(indent, tokenPart, stream); },
        [&indent, &stream](const QuoteExpr& quoteExpr) { PrintQuoteExpr(indent, quoteExpr, stream); },
        [&indent, &stream](const MacroExpandExpr& macroExpr) { PrintMacroExpandExpr(indent, macroExpr, stream); },
        [&indent, &stream](
            const WildcardExpr& /* WildcardExpr */) { PrintIndent(stream, indent, "WildcardExpr:", "_"); },
        [&indent, &stream](const IfAvailableExpr& expr) { PrintIfAvailableExpr(indent, expr, stream); },
        // ----------- Type --------------------
        [&indent, &stream](const RefType& refType) { PrintRefType(indent, refType, stream); },
        [&indent, &stream](const ParenType& type) { PrintParenType(indent, type, stream); },
        [&indent, &stream](const FuncType& type) { PrintFuncType(indent, type, stream); },
        [&indent, &stream](const TupleType& type) { PrintTupleType(indent, type, stream); },
        [&indent, &stream](const ThisType& type) { PrintThisType(indent, type, stream); },
        [&indent, &stream](const PrimitiveType& type) { PrintPrimitiveType(indent, type, stream); },
        [&indent, &stream](const OptionType& type) { PrintOptionType(indent, type, stream); },
        [&indent, &stream](const VArrayType& type) { PrintVArrayType(indent, type, stream); },
        [&indent, &stream](const QualifiedType& type) { PrintQualifiedType(indent, type, stream); },
        [&indent, &stream](const GenericConstraint& generic) { PrintGenericConstraint(indent, generic, stream); },
        [&indent, &stream](const InvalidType&) { PrintIndent(stream, indent, "InvalidType: Need to be fixed!"); },
        // -----------pattern----------------------
        [&indent, &stream](const ConstPattern& constPattern) { PrintConstPattern(indent, constPattern, stream); },
        [&indent, &stream](
            const WildcardPattern& /* WildcardPattern */) { PrintIndent(stream, indent, "WildcardPattern:", "_"); },
        [&indent, &stream](const VarPattern& varPattern) { PrintVarPattern(indent, varPattern, stream); },
        [&indent, &stream](const TuplePattern& tuplePattern) { PrintTuplePattern(indent, tuplePattern, stream); },
        [&indent, &stream](const TypePattern& typePattern) { PrintBasicpePattern(indent, typePattern, stream); },
        [&indent, &stream](const EnumPattern& enumPattern) { PrintEnumPattern(indent, enumPattern, stream); },
        [&indent, &stream](
            const ExceptTypePattern& exceptTypePattern) { PrintExceptTypePattern(indent, exceptTypePattern, stream); },
        [&indent, &stream](
            const CommandTypePattern& cmdTypePattern) { PrintCommandTypePattern(indent, cmdTypePattern, stream); },
        [indent, &stream](const VarOrEnumPattern& ve) { PrintVarOrEnumPattern(indent, ve, stream); },
        // ----------- package----------------------
        [&indent, &stream](const FeaturesDirective& feature) { PrintFeaturesDirective(indent, feature, stream); },
        [&indent, &stream](const PackageSpec& package) { PrintPackageSpec(indent, package, stream); },
        [&indent, &stream](const ImportSpec& import) {
            if (import.IsImportMulti()) {
                return;
            }
            PrintImportSpec(indent, import, stream);
        },
        // -----------no match----------------------
        [&indent, &stream, &node]() {
            PrintIndent(stream, indent, "UnknowNode:", ASTKIND_TO_STR.at(node->astKind), "{");
            PrintBasic(indent + ONE_INDENT, *node, stream);
            PrintIndent(stream, indent, "}");
        });
}

void PrintNode(Ptr<const AST::Node> node, std::ostream& stream = std::cout)
{
    PrintNode(node, 0, "", stream);
}
} // namespace Cangjie
