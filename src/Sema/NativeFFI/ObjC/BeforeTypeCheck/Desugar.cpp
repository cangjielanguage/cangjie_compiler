// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "Desugar.h"
#include "NativeFFI/Utils.h"
#include "cangjie/AST/AttributePack.h"
#include "cangjie/AST/Node.h"
#include "cangjie/AST/Utils.h"
#include "cangjie/AST/ASTCasting.h"
#include "cangjie/Utils/CastingTemplate.h"
#include "cangjie/Utils/SafePointer.h"

using namespace Cangjie::AST;
using namespace Cangjie::Native::FFI;

namespace Cangjie::Interop::ObjC {
namespace {
constexpr std::string_view INIT_PARAM_IDENT = "str";

void AddFuncDeclToClass(FuncDecl& decl, ClassDecl& target)
{
    decl.curFile = target.curFile;
    decl.identifier.SetPos(target.body->end, target.body->end);
    decl.begin = target.body->end;
    decl.end = target.body->end;
    decl.EnableAttr(Attribute::IN_CLASSLIKE);
}

OwnedPtr<Decl> CreateNSStringFromStringCtorDecl(ClassDecl& target)
{
    auto param = CreateFuncParam(std::string(INIT_PARAM_IDENT), CreateRefType(STD_LIB_STRING));

    std::vector<OwnedPtr<FuncParam>> ctorParams;
    ctorParams.emplace_back(std::move(param));
    auto paramList = CreateFuncParamList(std::move(ctorParams));

    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.emplace_back(std::move(paramList));

    auto block = CreateBlock({}, {});
    block->EnableAttr(Attribute::IS_CHECK_VISITED);

    auto ctorBody = CreateFuncBody(std::move(paramLists), nullptr, std::move(block));
    auto ctorDecl = CreateFuncDecl(std::string(INIT_IDENT), std::move(ctorBody), {});
    ctorDecl->EnableAttr(Attribute::PUBLIC, Attribute::CONSTRUCTOR);

    AddFuncDeclToClass(*ctorDecl, target);

    return ctorDecl;
}

OwnedPtr<Decl> CreateNSObjectToStringDecl(ClassDecl& target)
{
    std::vector<OwnedPtr<FuncParam>> funcParams;
    auto paramList = CreateFuncParamList(std::move(funcParams));

    std::vector<OwnedPtr<FuncParamList>> paramLists;
    paramLists.emplace_back(std::move(paramList));

    auto block = CreateBlock({}, {});
    block->EnableAttr(Attribute::IS_CHECK_VISITED);

    auto funcBody = CreateFuncBody(std::move(paramLists), CreateRefType(STD_LIB_STRING), std::move(block));
    auto funcDecl = CreateFuncDecl(std::string(TOSTRING_METHOD_IDENT), std::move(funcBody), {});
    funcDecl->EnableAttr(Attribute::PUBLIC);

    AddFuncDeclToClass(*funcDecl, target);

    return funcDecl;
}

void InsertNSStringCtor(ClassDecl& classDecl)
{
    auto ctorDecl = CreateNSStringFromStringCtorDecl(classDecl);
    classDecl.body->decls.push_back(std::move(ctorDecl));
}

void InsertNSObjectToString(ClassDecl& classDecl)
{
    auto funcDecl = CreateNSObjectToStringDecl(classDecl);
    classDecl.body->decls.push_back(std::move(funcDecl));
}

void DesugarMirrorClass(ClassDecl& classDecl)
{
    InsertMirrorVarProp(classDecl, Attribute::OBJ_C_MIRROR);

    auto foreignName = GetObjCMirrorForeignName(classDecl);
    if (foreignName == NSSTRING_CLASS_IDENT) {
        InsertNSStringCtor(classDecl);
    } else if (foreignName == NSOBJECT_CLASS_IDENT) {
        InsertNSObjectToString(classDecl);
    }
}
}

void PrepareTypeCheck(Package& pkg)
{
    for (auto& file : pkg.files) {
        for (auto& decl : file->decls) {
            if (!decl->TestAttr(Attribute::OBJ_C_MIRROR)) {
                continue;
            }

            if (auto cd = DynamicCast<ClassDecl*>(decl.get())) {
                DesugarMirrorClass(*cd);
            }
        }
    }
}
} // Cangjie::Interop::ObjC