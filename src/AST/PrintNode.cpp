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

#if !defined(NDEBUG) || defined(CMAKE_ENABLE_ASSERT)
#include <iostream>
#include <string>

#include "cangjie/AST/ASTCasting.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/Basic/Match.h"
#include "cangjie/Basic/Print.h"
#include "cangjie/Basic/StringConvertor.h"
#include "cangjie/Utils/Utils.h"
#endif

namespace Cangjie {
using namespace AST;
// Enable printNode for debug and CI version.
#if !defined(NDEBUG) || defined(CMAKE_ENABLE_ASSERT)
using namespace Utils;
using namespace Meta;

namespace {
const unsigned ONE_INDENT = 1u;
const unsigned TWO_INDENT = 2u;
const unsigned THREE_INDENT = 3u;
#define UNKNOWN_TY (ANSI_COLOR_RED + "unknown" + ANSI_COLOR_RESET)

void PrintModifiers(const Decl& decl, unsigned indent)
{
    std::string str = "Modifiers:";
    for (auto& i : decl.modifiers) {
        str += " " + std::string(TOKENS[static_cast<int>(i.modifier)]);
    }
    if (!decl.modifiers.empty()) {
        PrintIndent(indent + ONE_INDENT, str);
    }
}

void PrintTarget(unsigned indent, const Decl& target, std::string addition = "target")
{
    if (target.mangledName.empty()) {
        PrintIndent(indent, addition + ": ptr:", &target);
    } else {
        PrintIndent(indent, addition + ": mangledName:", "\"" + target.exportId + "\"");
    }
}

void PrintBasic(unsigned indent, const Node& node)
{
    // Basic info:
    std::string filePath = node.curFile ? node.curFile->filePath : "not in file";
    PrintIndent(indent, "curFile:", filePath);
    PrintIndent(indent, "position:", node.begin.ToString(), node.end.ToString());
    PrintIndent(indent, "scopeName:", "\"" + node.scopeName + "\"");
    PrintIndent(indent, "ty:", node.ty->String());
    auto fullPkgName = node.GetFullPackageName();
    if (!fullPkgName.empty()) {
        PrintIndent(indent, "fullPackageName:", fullPkgName);
    } else {
        PrintIndent(indent, "fullPackageName is empty");
    }
    PrintIndent(indent, "attributes:", node.GetAttrs().ToString());
    if (!node.exportId.empty()) {
        PrintIndent(indent, "exportId:", "\"" + node.exportId + "\"");
    }
    if (auto d = DynamicCast<Decl>(&node)) {
        PrintIndent(indent, "linkage:", static_cast<int>(d->linkage), ", isConst:", static_cast<int>(d->IsConst()));
        if (!d->mangledName.empty()) {
            PrintIndent(indent, "mangledName:", "\"" + d->mangledName + "\"");
        }
        if (d->annotationsArray) {
            PrintNode(d->annotationsArray.get(), indent, "annotationsArray");
        }
        if (d->outerDecl) {
            PrintTarget(indent, *d->outerDecl, "outerDecl");
        }
    }
    if (!node.comments.IsEmpty()) {
        PrintIndent(indent, "comments: " + node.comments.ToString());
    }
}

void PrintIndentTokens(unsigned indent, const std::vector<Token>& args)
{
    std::stringstream ss;
    PrintIndent(indent, "pos: ", args.front().Begin().ToString(), args.back().End().ToString());
    for (auto& it : args) {
        if (it.kind == TokenKind::NL) {
            ss << ";";
        } else {
            ss << it.Value();
        }
    }
    PrintIndent(indent, "\"" + ss.str() + "\"");
}

void PrintMacroInvocation(unsigned indent, const MacroInvocation& invocation)
{
    if (invocation.attrs.empty()) {
        PrintIndent(indent + ONE_INDENT, "// no attributes");
    } else {
        PrintIndent(indent + ONE_INDENT, "attrs {");
        PrintIndentTokens(indent + TWO_INDENT, invocation.attrs);
        PrintIndent(indent + ONE_INDENT, "}");
    }
    if (invocation.args.empty()) {
        PrintIndent(indent + ONE_INDENT, "// no arguments");
    } else {
        PrintIndent(indent + ONE_INDENT, "args {");
        PrintIndentTokens(indent + TWO_INDENT, invocation.args);
        PrintIndent(indent + ONE_INDENT, "}");
    }
    if (invocation.decl) {
        PrintNode(invocation.decl, indent + ONE_INDENT, "decl");
    }
}

void PrintPackage(unsigned indent, const Package& package)
{
    PrintIndent(indent, "Package:", package.fullPackageName, "{");

    PrintIndent(indent + ONE_INDENT, "noSubPkg:", package.noSubPkg);

    for (auto& it : package.files) {
        PrintNode(it.get(), indent + ONE_INDENT);
    }

    PrintIndent(indent + ONE_INDENT, "genericInstantiatedDecls {");
    for (auto& it : package.genericInstantiatedDecls) {
        PrintNode(it.get(), indent + TWO_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "}");

    PrintIndent(indent + ONE_INDENT, "srcImportedNonGenericDecls {");
    for (auto& it : package.srcImportedNonGenericDecls) {
        PrintNode(it.get(), indent + TWO_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "}");

    PrintIndent(indent + ONE_INDENT, "inlineFuncDecls {");
    for (auto& it : package.inlineFuncDecls) {
        PrintNode(it.get(), indent + TWO_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "}");

    PrintIndent(indent, "}");
}

void PrintFile(unsigned indent, const File& file)
{
    PrintIndent(indent, "File:", file.fileName, "{");
    PrintNode(file.package.get(), indent + ONE_INDENT, "package");
    for (auto& it : file.imports) {
        PrintNode(it.get(), indent + ONE_INDENT);
    }
    for (auto& it : file.decls) {
        if (it->identifier == MAIN_INVOKE) {
            continue;
        }
        PrintNode(it.get(), indent + ONE_INDENT);
    }
    for (auto& it : file.exportedInternalDecls) {
        PrintNode(it.get(), indent + ONE_INDENT, "exportedInternalDecls");
    }
    PrintIndent(indent + ONE_INDENT, "originalMacroCallNodes [");
    for (auto& macroNode : file.originalMacroCallNodes) {
        PrintNode(macroNode.get(), indent + TWO_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "]");
    PrintIndent(indent, "}");
}

void PrintMacroExpandParam(unsigned indent, const AST::MacroExpandParam& macroExpand)
{
    PrintIndent(indent, "MacroExpandParam:", macroExpand.invocation.fullName, "{");
    PrintMacroInvocation(indent, macroExpand.invocation);
    PrintIndent(indent, "}");
}

void PrintGenericParamDecl(unsigned indent, const AST::GenericParamDecl& gpd)
{
    PrintIndent(indent, "GenericParamDecl:", gpd.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, gpd);
    PrintIndent(indent, "}");
}

void PrintAnnotation(unsigned indent, const Annotation& anno)
{
    PrintIndent(indent, "Annotation:", anno.identifier, "{");
    PrintBasic(indent + ONE_INDENT, anno);
    PrintIndent(indent + ONE_INDENT, "args [");
    for (auto& arg : anno.args) {
        PrintNode(arg.get(), indent + TWO_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "]");
    PrintIndent(indent + ONE_INDENT, "adAnnotation:", anno.adAnnotation);
    PrintNode(anno.condExpr.get(), indent + ONE_INDENT, "condExpr");
    PrintIndent(indent + ONE_INDENT, "target:", anno.target);
    PrintNode(anno.baseExpr.get(), indent + ONE_INDENT, "baseExpr");
    PrintIndent(indent, "}");
}

void PrintAnnotations(const Decl& decl, unsigned indent)
{
    PrintIndent(indent, "annotations [");
    for (auto& anno : decl.annotations) {
        PrintAnnotation(indent + ONE_INDENT, *anno.get());
    }
    PrintIndent(indent, "]");
}

void PrintFuncParam(unsigned indent, const FuncParam& param)
{
    PrintIndent(indent, "FuncParam:", param.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, param);
    PrintAnnotations(param, indent + ONE_INDENT);
    PrintNode(param.type.get(), indent + ONE_INDENT, "type");
    PrintNode(param.assignment.get(), indent + ONE_INDENT, "assignment");
    if (param.desugarDecl) {
        PrintNode(param.desugarDecl.get(), indent + ONE_INDENT, "desugarDecl");
    }
    PrintIndent(indent, "}");
}

void PrintFuncParamList(unsigned indent, const FuncParamList& paramList)
{
    PrintIndent(indent, "FuncParamList {");
    if (paramList.params.empty()) {
        PrintIndent(indent + ONE_INDENT, "// no Params");
    } else {
        for (auto& it : paramList.params) {
            PrintNode(it.get(), indent + ONE_INDENT);
        }
    }
    PrintIndent(indent, "}");
}

void PrintMacroDecl(unsigned indent, const MacroDecl& macroDecl)
{
    PrintIndent(indent, "MacroDecl:", macroDecl.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, macroDecl);
    PrintModifiers(macroDecl, indent);
    if (macroDecl.desugarDecl) {
        PrintIndent(indent + ONE_INDENT, "// desugar decl");
        PrintNode(macroDecl.desugarDecl.get(), indent + ONE_INDENT);
    } else {
        PrintIndent(indent + ONE_INDENT, "// macro body");
        PrintNode(macroDecl.funcBody.get(), indent + ONE_INDENT);
    }
    PrintIndent(indent, "}");
}

void PrintFuncDecl(unsigned indent, const FuncDecl& funcDecl)
{
    PrintIndent(indent, "FuncDecl:", funcDecl.identifier.Val(), funcDecl.identifier.Begin(),
        funcDecl.identifier.End(), "{");
    PrintBasic(indent + ONE_INDENT, funcDecl);
    if (funcDecl.TestAttr(Attribute::CONSTRUCTOR) && funcDecl.constructorCall == ConstructorCall::SUPER) {
        if (auto ce = DynamicCast<CallExpr*>(funcDecl.funcBody->body->body.begin()->get()); ce) {
            if (ce->TestAttr(Attribute::COMPILER_ADD)) {
                PrintIndent(indent + ONE_INDENT, "super: implicitSuper");
            } else {
                PrintIndent(indent + ONE_INDENT, "super: explicitSuper");
            }
        }
    }
    PrintAnnotations(funcDecl, indent + ONE_INDENT);
    PrintModifiers(funcDecl, indent);
    PrintNode(funcDecl.funcBody.get(), indent + ONE_INDENT);
    PrintIndent(indent, "}");
}

void PrintPrimaryCtorDecl(unsigned indent, const PrimaryCtorDecl& primaryCtor)
{
    PrintIndent(indent, "PrimaryCtorDecl:", primaryCtor.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, primaryCtor);
    PrintAnnotations(primaryCtor, indent + ONE_INDENT);
    PrintModifiers(primaryCtor, indent);
    PrintNode(primaryCtor.funcBody.get(), indent + ONE_INDENT);
    PrintIndent(indent, "}");
}

void PrintMainDecl(unsigned indent, const MainDecl& mainDecl)
{
    PrintIndent(indent, "MainDecl:", mainDecl.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, mainDecl);
    if (mainDecl.desugarDecl) {
        PrintNode(mainDecl.desugarDecl.get(), indent + ONE_INDENT, "desugarDecl");
    } else {
        PrintNode(mainDecl.funcBody.get(), indent + ONE_INDENT, "funcBody");
    }
    PrintIndent(indent, "}");
}

void PrintGeneric(unsigned indent, const Generic& generic)
{
    PrintIndent(indent, "typeParameters {");
    for (auto& it : generic.typeParameters) {
        PrintNode(it.get(), indent + ONE_INDENT);
    }
    PrintIndent(indent, "}");
    PrintIndent(indent, "genericConstraints {");
    for (auto& it : generic.genericConstraints) {
        PrintNode(it.get(), indent + ONE_INDENT);
    }
    PrintIndent(indent, "}");
}

void PrintFuncBody(unsigned indent, const FuncBody& body)
{
    PrintIndent(indent, "FuncBody {");
    PrintBasic(indent + ONE_INDENT, body);
    if (body.generic) {
        PrintGeneric(indent + ONE_INDENT, *body.generic);
    }
    for (auto& it : body.paramLists) {
        PrintNode(it.get(), indent + ONE_INDENT);
    }
    if (!body.retType) {
        PrintIndent(indent + ONE_INDENT, "// no return type");
    } else {
        PrintIndent(indent + ONE_INDENT, "// return type");
        PrintNode(body.retType.get(), indent + ONE_INDENT);
    }
    PrintNode(body.body.get(), indent + ONE_INDENT, "body");
    PrintIndent(indent, "}");
}

void PrintPropDecl(unsigned indent, const PropDecl& propDecl)
{
    PrintIndent(indent, "PropDecl:", propDecl.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, propDecl);
    PrintModifiers(propDecl, indent);
    PrintNode(propDecl.type.get(), indent + ONE_INDENT, "type");
    for (auto& propMemberDecl : propDecl.setters) {
        PrintNode(propMemberDecl.get(), indent + ONE_INDENT);
    }
    for (auto& propMemberDecl : propDecl.getters) {
        PrintNode(propMemberDecl.get(), indent + ONE_INDENT);
    }
    PrintIndent(indent, "}");
}

void PrintMacroExpandDecl(unsigned indent, const AST::MacroExpandDecl& macroExpand)
{
    PrintIndent(indent, "MacroExpand:", macroExpand.invocation.fullName, "{");
    PrintBasic(indent + ONE_INDENT, macroExpand);
    PrintMacroInvocation(indent, macroExpand.invocation);
    PrintIndent(indent, "}");
}

void PrintVarWithPatternDecl(unsigned indent, const VarWithPatternDecl& varWithPatternDecl)
{
    PrintIndent(indent, "VarWithPatternDecl {");
    PrintBasic(indent + ONE_INDENT, varWithPatternDecl);
    if (varWithPatternDecl.isConst) {
        PrintIndent(indent + ONE_INDENT, "Const");
    } else {
        PrintIndent(indent + ONE_INDENT, varWithPatternDecl.isVar ? "Var" : "Let");
    }
    PrintNode(varWithPatternDecl.irrefutablePattern.get(), indent + ONE_INDENT);
    PrintNode(varWithPatternDecl.type.get(), indent + ONE_INDENT, "type");
    PrintNode(varWithPatternDecl.initializer.get(), indent + ONE_INDENT, "initializer");
    PrintIndent(indent, "}");
}

void PrintVarDecl(unsigned indent, const VarDecl& varDecl)
{
    std::string key = varDecl.isConst ? "const " : varDecl.isVar ? "var " : "let ";
    PrintIndent(indent, "VarDecl:", key + varDecl.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, varDecl);
    PrintAnnotations(varDecl, indent + ONE_INDENT);
    PrintModifiers(varDecl, indent);
    PrintNode(varDecl.type.get(), indent + ONE_INDENT, "type");
    PrintNode(varDecl.initializer.get(), indent + ONE_INDENT, "initializer");
    PrintIndent(indent, "}");
}

void PrintBasicpeAliasDecl(unsigned indent, const TypeAliasDecl& alias)
{
    PrintIndent(indent, "TypeAliasDecl", alias.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, alias);
    if (alias.generic) {
        PrintGeneric(indent + ONE_INDENT, *alias.generic);
    }
    PrintNode(alias.type.get(), indent + ONE_INDENT, "type");
    PrintIndent(indent, "}");
}

void PrintClassDecl(unsigned indent, const ClassDecl& classDecl)
{
    if (auto attr = HasJavaAttr(classDecl); attr) {
        std::string argStr = attr.value() == Attribute::JAVA_APP ? "app" : "ext";
        PrintIndent(indent, "@Java[\"" + argStr + "\"]");
    }
    PrintIndent(indent, "ClassDecl:", classDecl.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, classDecl);
    PrintAnnotations(classDecl, indent + ONE_INDENT);
    PrintModifiers(classDecl, indent);
    if (classDecl.generic) {
        PrintGeneric(indent + ONE_INDENT, *classDecl.generic);
    }
    if (!classDecl.inheritedTypes.empty()) {
        PrintIndent(indent + ONE_INDENT, "inheritedTypes [");
        for (auto& it : classDecl.inheritedTypes) {
            PrintNode(it.get(), indent + TWO_INDENT);
        }
        PrintIndent(indent + ONE_INDENT, "]");
    }
    PrintNode(classDecl.body.get(), indent + ONE_INDENT);
    PrintIndent(indent, "}");
}

void PrintInterfaceDecl(unsigned indent, const InterfaceDecl& interfaceDecl)
{
    if (auto attr = HasJavaAttr(interfaceDecl); attr) {
        PrintIndent(indent, "@Java[\"", attr.value() == Attribute::JAVA_APP ? "app" : "ext", "\"]");
    }
    PrintIndent(indent, "InterfaceDecl:", interfaceDecl.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, interfaceDecl);
    PrintModifiers(interfaceDecl, indent);
    if (interfaceDecl.generic) {
        PrintGeneric(indent + ONE_INDENT, *interfaceDecl.generic);
    }
    if (!interfaceDecl.inheritedTypes.empty()) {
        PrintIndent(indent + ONE_INDENT, "inheritedTypes: [");
        for (auto& it : interfaceDecl.inheritedTypes) {
            PrintNode(it.get(), indent + TWO_INDENT);
        }
        PrintIndent(indent + ONE_INDENT, "]");
    }
    PrintNode(interfaceDecl.body.get(), indent + ONE_INDENT);
    PrintIndent(indent, "}");
}

void PrintEnumDecl(unsigned indent, const EnumDecl& enumDecl)
{
    PrintIndent(indent, "EnumDecl:", enumDecl.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, enumDecl);
    PrintModifiers(enumDecl, indent);
    if (enumDecl.generic) {
        PrintGeneric(indent + ONE_INDENT, *enumDecl.generic);
    }
    if (!enumDecl.inheritedTypes.empty()) {
        PrintIndent(indent + ONE_INDENT, "inheritedTypes [");
        for (auto& it : enumDecl.inheritedTypes) {
            PrintNode(it.get(), indent + TWO_INDENT);
        }
        PrintIndent(indent + ONE_INDENT, "]");
    }
    PrintIndent(indent + ONE_INDENT, "constructors [");
    for (auto& it : enumDecl.constructors) {
        PrintNode(it.get(), indent + TWO_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "]");
    PrintIndent(indent + ONE_INDENT, "members [");
    for (auto& it : enumDecl.members) {
        PrintNode(it.get(), indent + TWO_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "]");
    PrintIndent(indent, "}");
}

void PrintStructDecl(unsigned indent, const StructDecl& decl)
{
    if (decl.TestAttr(Attribute::C)) {
        PrintIndent(indent, "@C");
    }
    PrintIndent(indent, "StructDecl:", decl.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, decl);
    PrintAnnotations(decl, indent + ONE_INDENT);
    PrintModifiers(decl, indent);
    if (decl.generic) {
        PrintGeneric(indent + ONE_INDENT, *decl.generic);
    }
    PrintIndent(indent + ONE_INDENT, "StructBody {");
    PrintNode(decl.body.get(), indent + TWO_INDENT);
    PrintIndent(indent + ONE_INDENT, "}");
    PrintIndent(indent, "}");
}

void PrintExtendDecl(unsigned indent, const ExtendDecl& ed)
{
    PrintIndent(indent, "ExtendDecl {");
    PrintBasic(indent + ONE_INDENT, ed);
    PrintNode(ed.extendedType.get(), indent + ONE_INDENT, "extendedType");
    PrintIndent(indent + ONE_INDENT, "inheritedTypes [");
    for (auto& interface : ed.inheritedTypes) {
        PrintNode(interface.get(), indent + TWO_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "]");
    if (ed.generic) {
        PrintGeneric(indent + ONE_INDENT, *ed.generic);
    }
    PrintIndent(indent + ONE_INDENT, "extendMembers [");
    for (auto& it : ed.members) {
        PrintNode(it.get(), indent + TWO_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "]");
    PrintIndent(indent, "}");
}

void PrintIfExpr(unsigned indent, const IfExpr& expr)
{
    PrintIndent(indent, "IfExpr {");
    PrintBasic(indent + ONE_INDENT, expr);
    PrintNode(expr.condExpr.get(), indent + ONE_INDENT, "condExpr");
    PrintNode(expr.thenBody.get(), indent + ONE_INDENT, "thenBody");
    if (expr.hasElse) {
        PrintNode(expr.elseBody.get(), indent + ONE_INDENT, "elseBody");
    }
    PrintIndent(indent, "}");
}

void PrintMatchCase(unsigned indent, const MatchCase& mc)
{
    PrintIndent(indent, "MatchCase: {");
    PrintBasic(indent + ONE_INDENT, mc);
    PrintIndent(indent + ONE_INDENT, "patterns {");
    for (auto& it : mc.patterns) {
        PrintNode(it.get(), indent + TWO_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "}");
    PrintNode(mc.patternGuard.get(), indent + ONE_INDENT, "patternGuard");
    PrintIndent(indent + ONE_INDENT, "exprOrDecls [");
    for (auto& it : mc.exprOrDecls->body) {
        PrintNode(it.get(), indent + TWO_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "]");

    PrintIndent(indent, "}");
}

void PrintMatchExpr(unsigned indent, const MatchExpr& expr)
{
    PrintIndent(indent, "MatchExpr:", "{");
    PrintBasic(indent + ONE_INDENT, expr);
    if (expr.matchMode) {
        PrintIndent(indent + ONE_INDENT, "selector: {");
        PrintNode(expr.selector.get(), indent + TWO_INDENT);
        PrintIndent(indent + ONE_INDENT, "}");

        PrintIndent(indent + ONE_INDENT, "matchCases: [");
        for (auto& matchCase : expr.matchCases) {
            PrintMatchCase(indent + TWO_INDENT, *StaticCast<MatchCase*>(matchCase.get()));
        }
        PrintIndent(indent + ONE_INDENT, "]");
    } else {
        for (auto& matchCaseOther : expr.matchCaseOthers) {
            PrintIndent(indent + ONE_INDENT, "matchCases: {");
            PrintNode(matchCaseOther->matchExpr.get(), indent + TWO_INDENT);
            PrintIndent(indent + ONE_INDENT, "}");

            PrintIndent(indent + ONE_INDENT, "exprOrDecls: [");
            for (auto& it : matchCaseOther->exprOrDecls->body) {
                PrintNode(it.get(), indent + ONE_INDENT);
            }
            PrintIndent(indent + ONE_INDENT, "]");
        }
    }
    PrintIndent(indent, "}");
}

void PrintTryExpr(unsigned indent, const TryExpr& expr)
{
    PrintIndent(indent, "TryExpr", "{");
    PrintBasic(indent + ONE_INDENT, expr);
    if (!expr.resourceSpec.empty()) {
        PrintIndent(indent + ONE_INDENT, "ResourceSpec", "{");
        for (auto& i : expr.resourceSpec) {
            PrintNode(i.get(), indent + TWO_INDENT);
        }
        PrintIndent(indent + ONE_INDENT, "}");
    }
    PrintIndent(indent + ONE_INDENT, "TryBlock", "{");
    PrintNode(expr.tryBlock.get(), indent + TWO_INDENT);
    PrintIndent(indent + ONE_INDENT, "}");
    for (unsigned int i = 0; i < expr.catchBlocks.size(); i++) {
        PrintIndent(indent + ONE_INDENT, "Catch", "{");
        PrintIndent(indent + TWO_INDENT, "CatchPattern", "{");
        PrintNode(expr.catchPatterns[i].get(), indent + THREE_INDENT);
        PrintIndent(indent + TWO_INDENT, "}");
        PrintIndent(indent + TWO_INDENT, "CatchBlock", "{");
        PrintNode(expr.catchBlocks[i].get(), indent + THREE_INDENT);
        PrintIndent(indent + TWO_INDENT, "}");
        PrintIndent(indent + ONE_INDENT, "}");
    }
    PrintIndent(indent + ONE_INDENT, "FinallyBlock", "{");
    PrintNode(expr.finallyBlock.get(), indent + TWO_INDENT);
    PrintIndent(indent + ONE_INDENT, "}");
    PrintIndent(indent, "}");
}

void PrintThrowExpr(unsigned indent, const ThrowExpr& expr)
{
    PrintIndent(indent, "ThrowExpr", "{");
    PrintBasic(indent + ONE_INDENT, expr);
    PrintNode(expr.expr.get(), indent + ONE_INDENT, "expr");
    PrintIndent(indent, "}");
}

void PrintReturnExpr(unsigned indent, const ReturnExpr& expr)
{
    PrintIndent(indent, "ReturnExpr", "{");
    PrintBasic(indent + ONE_INDENT, expr);
    PrintNode(expr.expr.get(), indent + ONE_INDENT, "expr");
    PrintIndent(indent, "}");
}

void PrintJumpExpr(unsigned indent, const JumpExpr& expr)
{
    std::string str1 = expr.isBreak ? "Break" : "Continue";
    PrintIndent(indent, "JumpExpr:", str1);
    PrintBasic(indent + ONE_INDENT, expr);
}

void PrintForInExpr(unsigned indent, const ForInExpr& expr)
{
    PrintIndent(indent, "ForInExpr {");
    PrintBasic(indent + ONE_INDENT, expr);
    PrintIndent(indent + ONE_INDENT, "pattern", ":");
    PrintNode(expr.pattern.get(), indent + TWO_INDENT);
    PrintIndent(indent + ONE_INDENT, "inExpression", ":");
    PrintNode(expr.inExpression.get(), indent + TWO_INDENT);
    PrintIndent(indent + ONE_INDENT, "patternGuard", ":");
    PrintNode(expr.patternGuard.get(), indent + TWO_INDENT);
    PrintNode(expr.body.get(), indent + ONE_INDENT, "body");
    PrintIndent(indent, "}");
}

void PrintWhileExpr(unsigned indent, const WhileExpr& expr)
{
    PrintIndent(indent, "WhileExpr {");
    PrintBasic(indent + ONE_INDENT, expr);
    PrintIndent(indent + ONE_INDENT, "condExpr", "{");
    PrintNode(expr.condExpr.get(), indent + TWO_INDENT, "condition expr");
    PrintIndent(indent + ONE_INDENT, "}");
    PrintNode(expr.body.get(), indent + ONE_INDENT, "While-Body");
    PrintIndent(indent, "}");
}

void PrintDoWhileExpr(unsigned indent, const DoWhileExpr& expr)
{
    PrintIndent(indent, "DoWhileExpr {");
    PrintBasic(indent + ONE_INDENT, expr);
    PrintNode(expr.body.get(), indent + ONE_INDENT, "DoWhile-Body");
    PrintIndent(indent + ONE_INDENT, "condExpr", "{");
    PrintNode(expr.condExpr.get(), indent + TWO_INDENT);
    PrintIndent(indent + ONE_INDENT, "}");
    PrintIndent(indent, "}");
}

void PrintAssignExpr(unsigned indent, const AssignExpr& expr)
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
    PrintIndent(indent, "AssignExpr", "{");
    PrintBasic(indent + ONE_INDENT, expr);
    std::string str1;
    auto result = op2str.find(expr.op);
    if (result != op2str.end()) {
        str1 = result->second;
    }
    PrintIndent(indent + ONE_INDENT, str1);
    PrintNode(expr.leftValue.get(), indent + ONE_INDENT, "leftValue");
    PrintNode(expr.rightExpr.get(), indent + ONE_INDENT, "rightExpr");
    if (expr.overflowStrategy != OverflowStrategy::NA) {
        PrintIndent(indent + ONE_INDENT, "overflowStrategy:", OverflowStrategyName(expr.overflowStrategy));
    }
    PrintIndent(indent, "}");
}

void PrintIncOrDecExpr(unsigned indent, const IncOrDecExpr& expr)
{
    PrintIndent(indent, "IncOrDecExpr", "{");
    PrintBasic(indent + ONE_INDENT, expr);
    std::string str1 = (expr.op == TokenKind::INCR) ? "++" : "--";
    PrintIndent(indent + ONE_INDENT, str1);
    PrintNode(expr.expr.get(), indent + ONE_INDENT);
    if (expr.overflowStrategy != OverflowStrategy::NA) {
        PrintIndent(indent + ONE_INDENT, "overflowStrategy:", OverflowStrategyName(expr.overflowStrategy));
    }
    PrintIndent(indent, "}");
}

void PrintUnaryExpr(unsigned indent, const UnaryExpr& unaryExpr)
{
    PrintIndent(indent, "UnaryExpr", "{", TOKENS[static_cast<int>(unaryExpr.op)]);
    PrintBasic(indent + ONE_INDENT, unaryExpr);
    PrintNode(unaryExpr.expr.get(), indent + ONE_INDENT);
    if (unaryExpr.overflowStrategy != OverflowStrategy::NA) {
        PrintIndent(indent + ONE_INDENT, "overflowStrategy:", OverflowStrategyName(unaryExpr.overflowStrategy));
    }
    PrintIndent(indent, "}");
}

void PrintBinaryExpr(unsigned indent, const BinaryExpr& binaryExpr)
{
    PrintIndent(indent, "BinaryExpr ", TOKENS[static_cast<int>(binaryExpr.op)], "{");
    PrintBasic(indent + ONE_INDENT, binaryExpr);
    PrintNode(binaryExpr.leftExpr.get(), indent + ONE_INDENT, "leftExpr:");
    PrintNode(binaryExpr.rightExpr.get(), indent + ONE_INDENT, "rightExpr:");
    if (binaryExpr.overflowStrategy != OverflowStrategy::NA) {
        PrintIndent(indent + ONE_INDENT, "overflowStrategy:", OverflowStrategyName(binaryExpr.overflowStrategy));
    }
    PrintIndent(indent, "}");
}

void PrintRangeExpr(unsigned indent, const RangeExpr& rangeExpr)
{
    PrintIndent(indent, "RangeExpr {");
    PrintBasic(indent + ONE_INDENT, rangeExpr);
    PrintIndent(indent + ONE_INDENT, "Start:");
    PrintNode(rangeExpr.startExpr.get(), indent + TWO_INDENT);
    PrintIndent(indent + ONE_INDENT, "Stop:");
    PrintNode(rangeExpr.stopExpr.get(), indent + TWO_INDENT);
    PrintIndent(indent + ONE_INDENT, "Step:");
    PrintNode(rangeExpr.stepExpr.get(), indent + TWO_INDENT);
    PrintIndent(indent, "}");
}

void PrintSubscriptExpr(unsigned indent, const SubscriptExpr& expr)
{
    PrintIndent(indent, "SubscriptExpr {");
    PrintBasic(indent + ONE_INDENT, expr);
    PrintNode(expr.baseExpr.get(), indent + ONE_INDENT, "baseExpr");
    PrintIndent(indent + ONE_INDENT, "indexExprs [");
    for (auto& it : expr.indexExprs) {
        PrintNode(it.get(), indent + TWO_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "]");
    PrintIndent(indent, "}");
}

void PrintMemberAccess(unsigned indent, const MemberAccess& expr)
{
    PrintIndent(indent, "MemberAccess", "{");
    PrintBasic(indent + ONE_INDENT, expr);
    PrintNode(expr.baseExpr.get(), indent + ONE_INDENT, "baseExpr");
    PrintIndent(indent + ONE_INDENT, "field:", expr.field.Val());
    if (expr.typeArguments.empty()) {
        PrintIndent(indent + ONE_INDENT, "// no type arguments");
    } else {
        PrintIndent(indent + ONE_INDENT, "typeArguments [");
        for (auto& it : expr.typeArguments) {
            PrintNode(it.get(), indent + TWO_INDENT);
        }
        PrintIndent(indent + ONE_INDENT, "]");
    }
    if (expr.target) {
        PrintTarget(indent + ONE_INDENT, *expr.target);
    }
    PrintIndent(indent + ONE_INDENT, "targets [");
    for (auto t : expr.targets) {
        PrintTarget(indent + TWO_INDENT, *t);
    }
    PrintIndent(indent + ONE_INDENT, "]");
    PrintIndent(indent, "}");
}

void PrintOptionalExpr(unsigned indent, const OptionalExpr& optionalExpr)
{
    PrintIndent(indent, "OptionalExpr", "{");
    PrintBasic(indent + ONE_INDENT, optionalExpr);
    PrintIndent(indent + ONE_INDENT, "BaseExpr:");
    PrintNode(optionalExpr.baseExpr.get(), indent + ONE_INDENT);
    PrintIndent(indent, "}");
}

void PrintOptionalChainExpr(unsigned indent, const OptionalChainExpr& optionalChainExpr)
{
    PrintIndent(indent, "OptionalChainExpr", "{");
    PrintBasic(indent + ONE_INDENT, optionalChainExpr);
    PrintIndent(indent + ONE_INDENT, "Expr:");
    PrintNode(optionalChainExpr.expr.get(), indent + ONE_INDENT);
    PrintIndent(indent, "}");
}

void PrintLetPatternDestructor(unsigned indent, const LetPatternDestructor& expr)
{
    PrintIndent(indent, "LetPattern", "{");
    PrintBasic(indent + ONE_INDENT, expr);
    PrintIndent(indent + ONE_INDENT, "patterns [");
    for (auto& p : expr.patterns) {
        PrintIndent(indent + TWO_INDENT, "|", "{");
        PrintNode(p.get(), indent + THREE_INDENT);
        PrintIndent(indent + TWO_INDENT, "}");
    }
    PrintIndent(indent + ONE_INDENT, "]");
    PrintIndent(indent + ONE_INDENT, "Initializer:", "{");
    PrintNode(expr.initializer.get(), indent + TWO_INDENT);
    PrintIndent(indent + ONE_INDENT, "}");
    PrintIndent(indent, "}");
}

void PrintFuncArg(unsigned indent, const FuncArg& expr)
{
    PrintIndent(indent, "FuncArg", "{");
    PrintBasic(indent + ONE_INDENT, expr);
    if (!expr.name.Empty()) {
        PrintIndent(indent + ONE_INDENT, "ArgName:", expr.name.Val());
    }
    if (expr.withInout) {
        PrintIndent(indent + ONE_INDENT, "inout");
    }
    PrintNode(expr.expr.get(), indent + ONE_INDENT);
    PrintIndent(indent, "}");
}

void PrintCallExpr(unsigned indent, const CallExpr& expr)
{
    PrintIndent(indent, "CallExpr {");
    PrintBasic(indent + ONE_INDENT, expr);
    PrintIndent(indent + ONE_INDENT, "BaseFunc {");
    PrintNode(expr.baseFunc.get(), indent + TWO_INDENT);
    PrintIndent(indent + ONE_INDENT, "}");
    if (expr.args.empty()) {
        PrintIndent(indent + ONE_INDENT, "// no arguments");
    } else {
        PrintIndent(indent + ONE_INDENT, "arguments [");
        for (auto& it : expr.args) {
            PrintNode(it.get(), indent + TWO_INDENT);
        }
        PrintIndent(indent + ONE_INDENT, "]");
    }
    if (expr.resolvedFunction) {
        PrintTarget(indent + ONE_INDENT, *expr.resolvedFunction, "resolvedFunction");
    }
    PrintIndent(indent, "}");
}

void PrintParenExpr(unsigned indent, const ParenExpr& parenExpr)
{
    PrintIndent(indent, "ParenExpr {");
    PrintBasic(indent + ONE_INDENT, parenExpr);
    PrintNode(parenExpr.expr.get(), indent + ONE_INDENT);
    PrintIndent(indent, "}");
}

void PrintLambdaExpr(unsigned indent, const LambdaExpr& expr)
{
    PrintIndent(indent, "LambdaExpr {");
    PrintBasic(indent + ONE_INDENT, expr);
    PrintIndent(indent + ONE_INDENT, "mangledName:", expr.mangledName);
    PrintNode(expr.funcBody.get(), indent + ONE_INDENT, "LambdaExpr-Body");
    PrintIndent(indent, "}");
}

void PrintLitConstExpr(unsigned indent, const LitConstExpr& expr)
{
    PrintIndent(
        indent, "LitConstExpr:", expr.LitKindToString(), "\"" + StringConvertor::Recover(expr.stringValue) + "\"", "{");
    PrintBasic(indent + ONE_INDENT, expr);
    if (expr.siExpr) {
        PrintNode(expr.siExpr.get(), indent + ONE_INDENT, "siExpr");
    }
    PrintIndent(indent, "}");
}

void PrintInterpolationExpr(unsigned indent, const InterpolationExpr& ie)
{
    PrintIndent(indent, "InterpolationExpr:", "\"" + ie.rawString + "\"", "{");
    PrintBasic(indent + ONE_INDENT, ie);
    PrintNode(ie.block.get(), indent + ONE_INDENT, "block");
    PrintIndent(indent, "}");
}

void PrintStrInterpolationExpr(unsigned indent, const StrInterpolationExpr& expr)
{
    PrintIndent(indent, "StrInterpolationExpr:", "\"" + expr.rawString + "\"", "{");
    PrintBasic(indent + ONE_INDENT, expr);
    PrintIndent(indent + ONE_INDENT, "strPartExprs [");
    for (auto& sp : expr.strPartExprs) {
        PrintNode(sp.get(), indent + TWO_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "]");
    PrintIndent(indent, "}");
}

void PrintArrayLit(unsigned indent, const ArrayLit& expr)
{
    PrintIndent(indent, "ArrayLiteral", "{");
    PrintBasic(indent + ONE_INDENT, expr);
    if (expr.children.empty()) {
        PrintIndent(indent + ONE_INDENT, "// no members");
    }
    PrintIndent(indent + ONE_INDENT, "children [");
    for (auto& it : expr.children) {
        PrintNode(it.get(), indent + TWO_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "]");
    PrintIndent(indent, "}");
}

void PrintArrayExpr(unsigned indent, const ArrayExpr& expr)
{
    auto typeName = expr.isValueArray ? "VArrayExpr" : "ArrayExpr";
    PrintIndent(indent, typeName, "{");
    PrintBasic(indent + ONE_INDENT, expr);
    PrintIndent(indent + ONE_INDENT, "args [");
    for (size_t i = 0; i < expr.args.size(); ++i) {
        PrintNode(expr.args[i].get(), indent + TWO_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "]");
    PrintIndent(indent, "}");
}

void PrintTupleLit(unsigned indent, const TupleLit& expr)
{
    PrintIndent(indent, "TupleLiteral", "{");
    PrintBasic(indent + ONE_INDENT, expr);
    for (auto& it : expr.children) {
        PrintNode(it.get(), indent + ONE_INDENT);
    }
    PrintIndent(indent, "}");
}

void PrintBasicpeConvExpr(unsigned indent, const TypeConvExpr& expr)
{
    PrintIndent(indent, "TypeConvExpr", "{");
    PrintBasic(indent + ONE_INDENT, expr);
    PrintIndent(indent + ONE_INDENT, "// Convert Type:");
    PrintNode(expr.type.get(), indent + TWO_INDENT);
    PrintIndent(indent + ONE_INDENT, "// Expr:");
    PrintNode(expr.expr.get(), indent + TWO_INDENT);
    PrintIndent(indent, "}");
}

void PrintRefExpr(unsigned indent, const RefExpr& refExpr)
{
    PrintIndent(indent, "RefExpr:", refExpr.ref.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, refExpr);
    if (refExpr.typeArguments.empty()) {
        PrintIndent(indent + ONE_INDENT, "// no type arguments");
    } else {
        PrintIndent(indent + ONE_INDENT, "typeArguments [");
        for (auto& it : refExpr.typeArguments) {
            PrintNode(it.get(), indent + TWO_INDENT);
        }
        PrintIndent(indent + ONE_INDENT, "]");
    }
    if (refExpr.ref.target) {
        PrintTarget(indent + ONE_INDENT, *refExpr.ref.target);
    }
    PrintIndent(indent + ONE_INDENT, "targets [");
    for (auto t : refExpr.ref.targets) {
        PrintTarget(indent + TWO_INDENT, *t);
    }
    PrintIndent(indent + ONE_INDENT, "]");
    PrintIndent(indent, "}");
}

void PrintSpawnExpr(unsigned indent, const SpawnExpr& expr)
{
    PrintIndent(indent, "SpawnExpr {");
    PrintBasic(indent + ONE_INDENT, expr);
    if (expr.futureObj) {
        PrintNode(expr.futureObj.get(), indent + ONE_INDENT);
    }
    if (expr.arg) {
        PrintNode(expr.arg.get(), indent + ONE_INDENT);
    }
    if (expr.task) {
        PrintNode(expr.task.get(), indent + ONE_INDENT);
    }
    PrintIndent(indent, "}");
}

void PrintSynchronizedExpr(unsigned indent, const SynchronizedExpr& expr)
{
    PrintIndent(indent, "SynchronizedExpr {");
    PrintBasic(indent + ONE_INDENT, expr);
    PrintNode(expr.mutex.get(), indent + TWO_INDENT, "mutex");
    PrintNode(expr.body.get(), indent + TWO_INDENT, "body");
    PrintIndent(indent, "}");
}

void PrintIfAvailableExpr(unsigned indent, const IfAvailableExpr& expr)
{
    PrintIndent(indent, "IfAvailableExpr {");
    PrintBasic(indent + ONE_INDENT, expr);
    PrintNode(expr.GetArg(), indent + TWO_INDENT, "arg");
    PrintNode(expr.GetLambda1(), indent + TWO_INDENT, "lambda1");
    PrintNode(expr.GetLambda2(), indent + TWO_INDENT, "lamdba2");
    PrintIndent(indent, "}");
}

void PrintBlock(unsigned indent, const Block& block)
{
    PrintIndent(indent, "Block: {");
    PrintBasic(indent + ONE_INDENT, block);
    if (block.body.empty()) {
        PrintIndent(indent + ONE_INDENT, "// no block");
    } else {
        for (auto& it : block.body) {
            PrintNode(it.get(), indent + ONE_INDENT);
        }
    }
    PrintIndent(indent, "}");
}

void PrintInterfaceBody(unsigned indent, const InterfaceBody& body)
{
    PrintIndent(indent, "InterfaceBody {");
    for (auto& it : body.decls) {
        PrintNode(it.get(), indent + ONE_INDENT);
    }
    PrintIndent(indent, "}");
}

void PrintClassBody(unsigned indent, const ClassBody& body)
{
    PrintIndent(indent, "ClassBody {");
    for (auto& it : body.decls) {
        PrintNode(it.get(), indent + ONE_INDENT);
    }
    PrintIndent(indent, "}");
}

void PrintStructBody(unsigned indent, const StructBody& body)
{
    for (auto& it : body.decls) {
        PrintNode(it.get(), indent);
    }
}

void PrintIsExpr(unsigned indent, const IsExpr& isExpr)
{
    PrintIndent(indent, "IsExpr : ", "{");
    PrintBasic(indent + ONE_INDENT, isExpr);
    PrintNode(isExpr.leftExpr.get(), indent + ONE_INDENT, "leftExpr");
    PrintNode(isExpr.isType.get(), indent + ONE_INDENT, "isType");
    PrintIndent(indent, "}");
}

void PrintAsExpr(unsigned indent, const AsExpr& asExpr)
{
    PrintIndent(indent, "AsExpr: {");
    PrintBasic(indent + ONE_INDENT, asExpr);
    PrintNode(asExpr.leftExpr.get(), indent + ONE_INDENT, "leftExpr");
    PrintNode(asExpr.asType.get(), indent + ONE_INDENT, "asType");
    PrintIndent(indent, "}");
}

void PrintTokenPart(unsigned indent, const AST::TokenPart& tokenPart)
{
    PrintIndentTokens(indent + ONE_INDENT, tokenPart.tokens);
}

void PrintQuoteExpr(unsigned indent, const AST::QuoteExpr& qe)
{
    PrintIndent(indent, "QuoteExpr {");
    for (auto& expr : qe.exprs) {
        if (expr->astKind == ASTKind::TOKEN_PART) {
            PrintIndent(indent + ONE_INDENT, "TokenPart", "(");
        } else {
            PrintIndent(indent + ONE_INDENT, "DollarExpr", "(");
        }
        PrintNode(expr.get(), indent + TWO_INDENT);
        PrintIndent(indent + ONE_INDENT, ")");
    }
    PrintIndent(indent, "}");
}

void PrintMacroExpandExpr(unsigned indent, const AST::MacroExpandExpr& expr)
{
    PrintIndent(indent, "MacroExpand:", expr.invocation.fullName, "{");
    PrintBasic(indent + ONE_INDENT, expr);
    PrintMacroInvocation(indent, expr.invocation);
    PrintIndent(indent, "}");
}

void PrintRefType(unsigned indent, const RefType& refType)
{
    PrintIndent(indent, "RefType:", refType.ref.identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, refType);
    if (refType.typeArguments.empty()) {
        PrintIndent(indent + ONE_INDENT, "// no type arguments");
    } else {
        PrintIndent(indent + ONE_INDENT, "typeArguments [");
        for (auto& it : refType.typeArguments) {
            PrintNode(it.get(), indent + TWO_INDENT);
        }
        PrintIndent(indent + ONE_INDENT, "]");
    }
    if (refType.ref.target) {
        PrintTarget(indent + ONE_INDENT, *refType.ref.target);
    }
    PrintIndent(indent + ONE_INDENT, "targets [");
    for (auto t : refType.ref.targets) {
        PrintTarget(indent + TWO_INDENT, *t);
    }
    PrintIndent(indent + ONE_INDENT, "]");
    PrintIndent(indent, "}");
}

void PrintParenType(unsigned indent, const ParenType& type)
{
    PrintIndent(indent, "ParenType {");
    PrintBasic(indent + ONE_INDENT, type);
    PrintNode(type.type.get(), indent + ONE_INDENT);
    PrintIndent(indent, "}");
}

void PrintFuncType(unsigned indent, const FuncType& type)
{
    PrintIndent(indent, "FuncType {");
    PrintBasic(indent + ONE_INDENT, type);
    PrintIndent(indent + ONE_INDENT, "paramTypes [");
    for (auto& paramType : type.paramTypes) {
        PrintNode(paramType.get(), indent + TWO_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "]");
    PrintNode(type.retType.get(), indent + ONE_INDENT, "retType");
    PrintIndent(indent + ONE_INDENT, "IsCFuncType:", type.isC);
    PrintIndent(indent, "}");
}

void PrintTupleType(unsigned indent, const TupleType& type)
{
    PrintIndent(indent, "TupleType {");
    PrintBasic(indent + ONE_INDENT, type);
    PrintIndent(indent + ONE_INDENT, "fieldTypes [");
    for (auto& it : type.fieldTypes) {
        PrintNode(it.get(), indent + TWO_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "]");
    PrintIndent(indent, "}");
}

void PrintThisType(unsigned indent, const ThisType& type)
{
    PrintIndent(indent, "ThisType {");
    PrintBasic(indent + ONE_INDENT, type);
    PrintIndent(indent, "}");
}

void PrintPrimitiveType(unsigned indent, const PrimitiveType& type)
{
    PrintIndent(indent, "PrimitiveType:", type.str, "{");
    PrintBasic(indent + ONE_INDENT, type);
    PrintIndent(indent, "}");
}

void PrintOptionType(unsigned indent, const OptionType& type)
{
    PrintIndent(indent, "OptionType {");
    PrintBasic(indent + ONE_INDENT, type);
    PrintIndent(indent + ONE_INDENT, "BaseType:");
    PrintNode(type.componentType.get(), indent + TWO_INDENT);
    PrintIndent(indent + ONE_INDENT, "QuestNums:", type.questNum);
    PrintIndent(indent, "}");
}

void PrintVArrayType(unsigned indent, const VArrayType& type)
{
    PrintIndent(indent, "VArrayType {");
    PrintBasic(indent + ONE_INDENT, type);
    PrintIndent(indent + ONE_INDENT, "typeArguments:");
    PrintNode(type.typeArgument.get(), indent + TWO_INDENT);
    PrintIndent(indent, "}");
}

void PrintQualifiedType(unsigned indent, const QualifiedType& type)
{
    PrintIndent(indent, "QualifiedType {");
    PrintBasic(indent + ONE_INDENT, type);
    PrintNode(type.baseType.get(), indent + ONE_INDENT, "baseType");
    PrintIndent(indent + ONE_INDENT, "field:", type.field.Val());
    if (type.target) {
        PrintTarget(indent + ONE_INDENT, *type.target);
    }
    PrintIndent(indent, "}");
}

void PrintGenericConstraint(unsigned indent, const GenericConstraint& generic)
{
    PrintIndent(indent, "GenericConstraint {");
    PrintNode(generic.type.get(), indent + ONE_INDENT);
    PrintIndent(indent + ONE_INDENT, "upperBounds {");
    for (auto& upperBound : generic.upperBounds) {
        PrintNode(upperBound.get(), indent + TWO_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "}");
    PrintIndent(indent, "}");
}

void PrintConstPattern(unsigned indent, const ConstPattern& constPattern)
{
    PrintIndent(indent, "ConstPattern {");
    PrintNode(constPattern.literal.get(), indent + ONE_INDENT);
    PrintIndent(indent, "}");
}

void PrintTuplePattern(unsigned indent, const TuplePattern& tuplePattern)
{
    PrintIndent(indent, "TuplePattern {");
    PrintIndent(indent + ONE_INDENT, "patterns [");
    for (auto& pattern : tuplePattern.patterns) {
        PrintNode(pattern.get(), indent + ONE_INDENT);
    }
    PrintIndent(indent + ONE_INDENT, "]");
    PrintIndent(indent, "}");
}

void PrintBasicpePattern(unsigned indent, const TypePattern& typePattern)
{
    PrintIndent(indent, "TypePattern {");
    PrintBasic(indent + ONE_INDENT, typePattern);
    PrintNode(typePattern.pattern.get(), indent + ONE_INDENT, "pattern");
    PrintNode(typePattern.type.get(), indent + ONE_INDENT, "type");
    if (typePattern.desugarVarPattern.get()) {
        PrintNode(typePattern.desugarVarPattern.get(), indent + ONE_INDENT, "// desugarVarPattern");
    }
    PrintIndent(indent, "}");
}

void PrintVarPattern(unsigned indent, const VarPattern& varPattern)
{
    PrintIndent(indent, "VarPattern:", varPattern.varDecl->identifier.Val(), "{");
    PrintBasic(indent + ONE_INDENT, varPattern);
    PrintNode(varPattern.varDecl, indent + ONE_INDENT, "varDecl");
    PrintIndent(indent, "}");
}

void PrintEnumPattern(unsigned indent, const EnumPattern& enumPattern)
{
    PrintIndent(indent, "EnumPattern {");
    PrintNode(enumPattern.constructor.get(), indent + ONE_INDENT);
    for (auto& pattern : enumPattern.patterns) {
        PrintNode(pattern.get(), indent + ONE_INDENT);
    }
    PrintIndent(indent, "}");
}

void PrintExceptTypePattern(unsigned indent, const ExceptTypePattern& exceptTypePattern)
{
    PrintIndent(indent, "ExceptTypePattern {");
    PrintNode(exceptTypePattern.pattern.get(), indent + ONE_INDENT);
    for (auto& type : exceptTypePattern.types) {
        PrintNode(type.get(), indent + ONE_INDENT);
    }
    PrintIndent(indent, "}");
}

void PrintVarOrEnumPattern(unsigned indent, const VarOrEnumPattern& ve)
{
    PrintIndent(indent, "VarOrEnumPattern {");
    PrintIndent(indent + ONE_INDENT, ve.identifier);
    if (ve.pattern) {
        PrintIndent(indent + ONE_INDENT, "Subpatterns {");
        PrintNode(ve.pattern, indent + TWO_INDENT);
        PrintIndent(indent + ONE_INDENT, "}");
    }
    PrintIndent(indent, "}");
}

void PrintPackageSpec(unsigned indent, const PackageSpec& package)
{
    PrintIndent(indent, "PackageSpec:", package.packageName.Val(), "{");
    if (!package.prefixPaths.empty()) {
        auto prefixPath = JoinStrings(package.prefixPaths, ".");
        PrintIndent(indent + ONE_INDENT, "prefixPath:", std::move(prefixPath));
    }
    if (package.modifier) {
        PrintIndent(indent + ONE_INDENT, "modifier:", TOKENS[static_cast<int>(package.modifier->modifier)]);
    }
    PrintIndent(indent, "}");
}

void PrintImportSpec(unsigned indent, const ImportSpec& import)
{
    if (import.content.kind != ImportKind::IMPORT_MULTI) {
        PrintIndent(indent, "ImportSpec:", import.content.identifier.Val(), "{");
        if (import.content.kind == ImportKind::IMPORT_ALIAS) {
            PrintIndent(indent + ONE_INDENT, "aliasName:", import.content.aliasName.Val());
        }
        if (!import.content.prefixPaths.empty()) {
            PrintIndent(indent + ONE_INDENT, "prefixPaths:", JoinStrings(import.content.prefixPaths, "."));
        }
        if (import.modifier) {
            PrintIndent(indent + ONE_INDENT, "modifier:", TOKENS[static_cast<int>(import.modifier->modifier)]);
        }
        PrintIndent(indent + ONE_INDENT, "isDecl:", import.content.isDecl);
        PrintIndent(indent, "}");
    } else {
        std::string commonPrefix = JoinStrings(import.content.prefixPaths, ".");
        for (const auto& item : import.content.items) {
            PrintIndent(indent, "ImportSpec:", item.identifier.Val(), "{");
            if (item.kind == ImportKind::IMPORT_ALIAS) {
                PrintIndent(indent + ONE_INDENT, "aliasName:", item.aliasName.Val());
            }
            std::stringstream ss;
            ss << commonPrefix;
            if (!commonPrefix.empty() && !item.prefixPaths.empty()) {
                ss << "." << JoinStrings(item.prefixPaths, ".");
            } else if (!item.prefixPaths.empty()) {
                ss << JoinStrings(item.prefixPaths, ".");
            }
            if (ss.rdbuf()->in_avail() != 0) {
                PrintIndent(indent + ONE_INDENT, "prefixPaths:", ss.str());
            }
            if (import.modifier) {
                PrintIndent(indent + ONE_INDENT, "modifier:", TOKENS[static_cast<int>(import.modifier->modifier)]);
            }
            PrintIndent(indent, "}");
        }
    }
}
} // namespace
#endif

void PrintNode([[maybe_unused]] Ptr<const Node> node, [[maybe_unused]] unsigned indent,
    [[maybe_unused]] const std::string addition)
{
#if !defined(NDEBUG) || defined(CMAKE_ENABLE_ASSERT) // Enable printNode for debug and CI version.
    if (!addition.empty()) {
        PrintIndent(indent, "//", node ? addition : addition + " nullptr");
    }
    if (!node) {
        return;
    }
    if (node->IsExpr()) {
        auto expr = RawStaticCast<const Expr*>(node);
        if (expr->desugarExpr) {
            return PrintNode(expr->desugarExpr.get(), indent, addition);
        }
    }
    match (*node)([&indent](const Package& package) { PrintPackage(indent, package); },
        [&indent](const File& file) { PrintFile(indent, file); },
        // ----------- Decls --------------------
        [&indent](const GenericParamDecl& gpd) { PrintGenericParamDecl(indent, gpd); },
        [&indent](const FuncParam& param) { PrintFuncParam(indent, param); },
        [&indent](const MacroExpandParam& macroExpand) { PrintMacroExpandParam(indent, macroExpand); },
        [&indent](const FuncParamList& paramList) { PrintFuncParamList(indent, paramList); },
        [&indent](const MainDecl& mainDecl) { PrintMainDecl(indent, mainDecl); },
        [&indent](const FuncDecl& funcDecl) { PrintFuncDecl(indent, funcDecl); },
        [&indent](const MacroDecl& macroDecl) { PrintMacroDecl(indent, macroDecl); },
        [&indent](const FuncBody& body) { PrintFuncBody(indent, body); },
        [&indent](const PropDecl& propDecl) { PrintPropDecl(indent, propDecl); },
        [&indent](const MacroExpandDecl& macroExpand) { PrintMacroExpandDecl(indent, macroExpand); },
        [&indent](const VarWithPatternDecl& vwpd) { PrintVarWithPatternDecl(indent, vwpd); },
        [&indent](const VarDecl& varDecl) { PrintVarDecl(indent, varDecl); },
        [&indent](const TypeAliasDecl& alias) { PrintBasicpeAliasDecl(indent, alias); },
        [&indent](const ClassDecl& classDecl) { PrintClassDecl(indent, classDecl); },
        [&indent](const InterfaceDecl& interfaceDecl) { PrintInterfaceDecl(indent, interfaceDecl); },
        [&indent](const EnumDecl& enumDecl) { PrintEnumDecl(indent, enumDecl); },
        [&indent](const StructDecl& decl) { PrintStructDecl(indent, decl); },
        [&indent](const ExtendDecl& ed) { PrintExtendDecl(indent, ed); },
        [&indent](const PrimaryCtorDecl& pc) { PrintPrimaryCtorDecl(indent, pc); },
        // -----------Exprs----------------------
        [&indent](const IfExpr& expr) { PrintIfExpr(indent, expr); },
        [&indent](const MatchExpr& expr) { PrintMatchExpr(indent, expr); },
        [&indent](const TryExpr& expr) { PrintTryExpr(indent, expr); },
        [&indent](const ThrowExpr& expr) { PrintThrowExpr(indent, expr); },
        [&indent](const ReturnExpr& expr) { PrintReturnExpr(indent, expr); },
        [&indent](const JumpExpr& expr) { PrintJumpExpr(indent, expr); },
        [&indent](const ForInExpr& expr) { PrintForInExpr(indent, expr); },
        [&indent](const WhileExpr& expr) { PrintWhileExpr(indent, expr); },
        [&indent](const DoWhileExpr& expr) { PrintDoWhileExpr(indent, expr); },
        [&indent](const AssignExpr& expr) { PrintAssignExpr(indent, expr); },
        [&indent](const IncOrDecExpr& expr) { PrintIncOrDecExpr(indent, expr); },
        [&indent](const UnaryExpr& unaryExpr) { PrintUnaryExpr(indent, unaryExpr); },
        [&indent](const BinaryExpr& binaryExpr) { PrintBinaryExpr(indent, binaryExpr); },
        [&indent](const RangeExpr& rangeExpr) { PrintRangeExpr(indent, rangeExpr); },
        [&indent](const SubscriptExpr& expr) { PrintSubscriptExpr(indent, expr); },
        [&indent](const MemberAccess& expr) { PrintMemberAccess(indent, expr); },
        [&indent](const FuncArg& expr) { PrintFuncArg(indent, expr); },
        [&indent](const CallExpr& expr) { PrintCallExpr(indent, expr); },
        [&indent](const ParenExpr& parenExpr) { PrintParenExpr(indent, parenExpr); },
        [&indent](const LambdaExpr& expr) { PrintLambdaExpr(indent, expr); },
        [&indent](const LitConstExpr& expr) { PrintLitConstExpr(indent, expr); },
        [&indent](const InterpolationExpr& expr) { PrintInterpolationExpr(indent, expr); },
        [&indent](const StrInterpolationExpr& expr) { PrintStrInterpolationExpr(indent, expr); },
        [&indent](const ArrayLit& expr) { PrintArrayLit(indent, expr); },
        [&indent](const ArrayExpr& expr) { PrintArrayExpr(indent, expr); },
        [&indent](const TupleLit& expr) { PrintTupleLit(indent, expr); },
        [&indent](const TypeConvExpr& expr) { PrintBasicpeConvExpr(indent, expr); },
        [&indent](const RefExpr& refExpr) { PrintRefExpr(indent, refExpr); },
        [&indent](const OptionalExpr& expr) { PrintOptionalExpr(indent, expr); },
        [&indent](const OptionalChainExpr& expr) { PrintOptionalChainExpr(indent, expr); },
        [&indent](const LetPatternDestructor& expr) { PrintLetPatternDestructor(indent, expr); },
        [&indent](const PrimitiveTypeExpr& pte) { PrintIndent(indent, "PrimitiveTypeExpr: " + pte.ty->String()); },
        [&indent](const SpawnExpr& expr) { PrintSpawnExpr(indent, expr); },
        [&indent](const SynchronizedExpr& expr) { PrintSynchronizedExpr(indent, expr); },
        [&indent](const InvalidExpr& /* expr */) { PrintIndent(indent, "InvalidExpr: Need to be fixed!"); },
        [&indent](const Block& block) { PrintBlock(indent, block); },
        [&indent](const InterfaceBody& body) { PrintInterfaceBody(indent, body); },
        [&indent](const ClassBody& body) { PrintClassBody(indent, body); },
        [&indent](const StructBody& body) { PrintStructBody(indent, body); },
        [&indent](const IsExpr& isExpr) { PrintIsExpr(indent, isExpr); },
        [&indent](const AsExpr& asExpr) { PrintAsExpr(indent, asExpr); },
        [&indent](const TokenPart& tokenPart) { PrintTokenPart(indent, tokenPart); },
        [&indent](const QuoteExpr& quoteExpr) { PrintQuoteExpr(indent, quoteExpr); },
        [&indent](const MacroExpandExpr& macroExpr) { PrintMacroExpandExpr(indent, macroExpr); },
        [&indent](const WildcardExpr& /* WildcardExpr */) { PrintIndent(indent, "WildcardExpr:", "_"); },
        [&indent](const IfAvailableExpr& expr) { PrintIfAvailableExpr(indent, expr); },
        // ----------- Type --------------------
        [&indent](const RefType& refType) { PrintRefType(indent, refType); },
        [&indent](const ParenType& type) { PrintParenType(indent, type); },
        [&indent](const FuncType& type) { PrintFuncType(indent, type); },
        [&indent](const TupleType& type) { PrintTupleType(indent, type); },
        [&indent](const ThisType& type) { PrintThisType(indent, type); },
        [&indent](const PrimitiveType& type) { PrintPrimitiveType(indent, type); },
        [&indent](const OptionType& type) { PrintOptionType(indent, type); },
        [&indent](const VArrayType& type) { PrintVArrayType(indent, type); },
        [&indent](const QualifiedType& type) { PrintQualifiedType(indent, type); },
        [&indent](const GenericConstraint& generic) { PrintGenericConstraint(indent, generic); },
        [&indent](const InvalidType&) { PrintIndent(indent, "InvalidType: Need to be fixed!"); },
        // -----------pattern----------------------
        [&indent](const ConstPattern& constPattern) { PrintConstPattern(indent, constPattern); },
        [&indent](const WildcardPattern& /* WildcardPattern */) { PrintIndent(indent, "WildcardPattern:", "_"); },
        [&indent](const VarPattern& varPattern) { PrintVarPattern(indent, varPattern); },
        [&indent](const TuplePattern& tuplePattern) { PrintTuplePattern(indent, tuplePattern); },
        [&indent](const TypePattern& typePattern) { PrintBasicpePattern(indent, typePattern); },
        [&indent](const EnumPattern& enumPattern) { PrintEnumPattern(indent, enumPattern); },
        [&indent](const ExceptTypePattern& exceptTypePattern) { PrintExceptTypePattern(indent, exceptTypePattern); },
        [indent](const VarOrEnumPattern& ve) { PrintVarOrEnumPattern(indent, ve); },
        // ----------- package----------------------
        [&indent](const PackageSpec& package) { PrintPackageSpec(indent, package); },
        [&indent](const ImportSpec& import) {
            if (import.IsImportMulti()) {
                return;
            }
            PrintImportSpec(indent, import);
        },
        // -----------no match----------------------
        [&indent, &node]() {
            PrintIndent(indent, "UnknowNode:", ASTKIND_TO_STR.at(node->astKind), "{");
            PrintBasic(indent + ONE_INDENT, *node);
            PrintIndent(indent, "}");
        });
#endif
}

void PrintNode(Ptr<const AST::Node> node)
{
    PrintNode(node, 0, "");
}
} // namespace Cangjie
