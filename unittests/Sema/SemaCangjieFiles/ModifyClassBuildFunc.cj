// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.macro package macrodef

macro package macrodefX

import std.ast.*
import std.collection.*

func createClassDeclaration(
    modifiers!: Tokens = Tokens(),
    keyword!: Tokens = quote(class),
    identifier!: Tokens = Tokens(),
    body!: Tokens = Tokens()
): Tokens {
    quote(
        $modifiers $keyword $identifier {
            $body
        }
    )
}

public macro Prop(input: Tokens):Tokens{
    return input
}

public macro Component(input: Tokens): Tokens {
    let classDecl = (parseDecl(input) as ClassDecl).getOrThrow()
    let classModifier = classDecl.modifiers
    let classIdent = classDecl.identifier
    let classBody = classDecl.body

    let classBodyAfterExpand = handleClassBody(classBody.decls)

    createClassDeclaration(
        modifiers: quote($classModifier),
        identifier: quote($classIdent),
        body: quote(
            $classBodyAfterExpand
        )
    )
}

func createMethodDeclaration(
    modifiers!: Tokens = Tokens(),
    identifier!: Tokens = Tokens(),
    generics!: ?Tokens = None,
    body!: Tokens = Tokens()
): Tokens {
    var funcDef = quote(
        $modifiers func $identifier)
    if (let Some(v) <- generics) {
        funcDef += quote($v)
    }
    funcDef += quote(())

    funcDef + quote({
            $body
        }
    )
}

func generateBuildFunc(funcDecl: FuncDecl) {
    var funcBody = funcDecl.block.nodes

    var renDer= Tokens()
    for (each in funcBody) {
        match (each) {
            case value: IfExpr => renDer = renDer.append(Token(TokenKind.NL)).append(quote({=> $value}))
            case _: Node => ()
        }
    }
    createMethodDeclaration(
        modifiers: quote(public),
        identifier: quote(build),
        body: quote($renDer)
    )
}

func handleFuncInClassBody(funcDecl: FuncDecl) {
    // handler build function, rename to build, and expand components
    if (funcDecl.identifier.value == "build") {
        return generateBuildFunc(funcDecl)
    }
    // other functions are inserted without change.
    return quote(
        $funcDecl
    )
}

func handleClassBody(classBody: ArrayList<Decl>) {
    var ret = Tokens()
    for (item in classBody) {
        // handle function
        match (item) {
            case funcDecl: FuncDecl => ret = ret + handleFuncInClassBody(funcDecl)
            case _ =>
                // other declarations are inserted without change.
                ret = ret + quote(
                    $item
                )
        }
    }
    return ret
}