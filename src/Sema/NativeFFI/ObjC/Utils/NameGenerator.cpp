// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements factory class for names of different Objective-C interop entities.
 */

#include "ASTFactory.h"
#include "NameGenerator.h"
#include "cangjie/AST/Match.h"
#include "NativeFFI/Utils.h"

using namespace Cangjie::AST;
using namespace Cangjie::Interop::ObjC;
using namespace Cangjie::Native::FFI;

namespace {
constexpr auto WRAPPER_PREFIX = "CJImpl_ObjC_";
constexpr auto DELETE_CJ_OBJECT_SUFFIX = "_deleteCJObject";
constexpr auto WRAPPER_GETTER_SUFFIX = "_get";
constexpr auto WRAPPER_SETTER_SUFFIX = "_set";
} // namespace

NameGenerator::NameGenerator(const BaseMangler& mangler) : mangler(mangler) {
}

std::string NameGenerator::GenerateInitCjObjectName(const FuncDecl& target)
{
    auto& params =  target.funcBody->paramLists[0]->params;
    auto ctorName = GetObjCDeclName(target);
    auto mangledCtorName = GetMangledMethodName(mangler, params, ctorName);
    auto name = GetObjCFullDeclName(*target.outerDecl) + "_" + mangledCtorName;
    std::replace(name.begin(), name.end(), '.', '_');
    std::replace(name.begin(), name.end(), ':', '_');

    return WRAPPER_PREFIX + name;
}

std::string NameGenerator::GenerateDeleteCjObjectName(const ClassDecl& target)
{
    auto name = GetObjCFullDeclName(target);
    std::replace(name.begin(), name.end(), '.', '_');
    std::replace(name.begin(), name.end(), ':', '_');

    return WRAPPER_PREFIX + name + DELETE_CJ_OBJECT_SUFFIX;
}

std::string NameGenerator::GenerateMethodWrapperName(const FuncDecl& target)
{
    auto& params = target.funcBody->paramLists[0]->params;
    auto methodName = GetObjCDeclName(target);
    auto mangledMethodName = GetMangledMethodName(mangler, params, methodName);
    auto outerDeclName = GetObjCFullDeclName(*target.outerDecl);

    auto name = outerDeclName + "." + mangledMethodName;
    std::replace(name.begin(), name.end(), '.', '_');
    std::replace(name.begin(), name.end(), ':', '_');

    return WRAPPER_PREFIX + name;
}

std::string NameGenerator::GeneratePropGetterWrapperName(const PropDecl& target)
{
    CJC_NULLPTR_CHECK(target.outerDecl);
    auto outerDeclName = GetObjCFullDeclName(*target.outerDecl);
    auto name = outerDeclName + "." + GetObjCDeclName(target);
    std::replace(name.begin(), name.end(), '.', '_');
    std::replace(name.begin(), name.end(), ':', '_');

    return WRAPPER_PREFIX + name + WRAPPER_GETTER_SUFFIX;
}

std::string NameGenerator::GetPropSetterWrapperName(const PropDecl& target)
{
    CJC_NULLPTR_CHECK(target.outerDecl);
    auto outerDeclName = GetObjCFullDeclName(*target.outerDecl);
    auto name = outerDeclName + "." + GetObjCDeclName(target);

    std::replace(name.begin(), name.end(), '.', '_');
    std::replace(name.begin(), name.end(), ':', '_');

    return WRAPPER_PREFIX + name + WRAPPER_SETTER_SUFFIX;
}

std::string NameGenerator::GetFieldGetterWrapperName(const VarDecl& target)
{
    CJC_NULLPTR_CHECK(target.outerDecl);
    auto outerDeclName = GetObjCFullDeclName(*target.outerDecl);
    auto name = outerDeclName + "." + GetObjCDeclName(target);

    std::replace(name.begin(), name.end(), '.', '_');
    std::replace(name.begin(), name.end(), ':', '_');

    return WRAPPER_PREFIX + name + WRAPPER_GETTER_SUFFIX;
}

std::string NameGenerator::GetFieldSetterWrapperName(const VarDecl& target)
{
    CJC_NULLPTR_CHECK(target.outerDecl);
    auto outerDeclName = GetObjCFullDeclName(*target.outerDecl);
    auto name = outerDeclName + "." + GetObjCDeclName(target);

    std::replace(name.begin(), name.end(), '.', '_');
    std::replace(name.begin(), name.end(), ':', '_');

    return WRAPPER_PREFIX + name + WRAPPER_SETTER_SUFFIX;
}

Ptr<std::string> NameGenerator::GetUserDefinedObjCName(const Decl& target)
{
    for (auto& anno : target.annotations) {
        if (anno->kind != AnnotationKind::OBJ_C_MIRROR && anno->kind != AnnotationKind::OBJ_C_IMPL &&
            anno->kind != AnnotationKind::FOREIGN_NAME) {
            continue;
        }

        CJC_ASSERT(anno->args.size() < 2);
        if (anno->args.empty()) {
            break;
        }

        CJC_ASSERT(anno->args[0]->expr->astKind == ASTKind::LIT_CONST_EXPR);
        auto lce = As<ASTKind::LIT_CONST_EXPR>(anno->args[0]->expr.get());
        CJC_ASSERT(lce);

        return &lce->stringValue;
    }

    return nullptr;
}

std::string NameGenerator::GetObjCDeclName(const Decl& target)
{
    auto foreignName = GetUserDefinedObjCName(target);
    if (foreignName) {
        return *foreignName;
    }

    return target.identifier;
}

std::string NameGenerator::GetObjCFullDeclName(const Decl& target)
{
    auto name = GetUserDefinedObjCName(target);
    if (name) {
        return *name;
    }

    auto ret = target.fullPackageName + "." + target.identifier;

    return ret;
}
