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

std::string NameGenerator::GenerateDeleteCjObjectName(const Decl& target)
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

std::vector<std::string> NameGenerator::GetObjCDeclSelectorComponents(const FuncDecl& target)
{
    auto fullName = GetObjCDeclName(target);
    std::vector<std::string> result;
    size_t pos = 0;
    // split fullName by ':', excluding ':'
    while (pos != std::string::npos && pos < fullName.size()) {
        auto newPos = fullName.find_first_of(':', pos);
        if (newPos == std::string::npos) {
            result.push_back(fullName.substr(pos));
            break;
        }
        result.push_back(fullName.substr(pos, newPos - pos));
        newPos++;
        pos = newPos;
    }
    size_t actualArgumentSize = 0;
    for (auto&& paramList : target.funcBody->paramLists) {
        actualArgumentSize += paramList->params.size();
    }
    // the normal situation: @ForeignName is exactly suited for the task
    if ((actualArgumentSize == 0 && result.size() == 1) || actualArgumentSize == result.size()) {
        return result;
    }
    // too many components, trim them to be compatible. It can happen with excess : symbols
    while (actualArgumentSize < result.size()) {
        result.pop_back();
    }
    // too few components, use argument names
    if (actualArgumentSize > result.size()) {
        // we actually want to add last argument to names to the end
        // of the result, not first ones
        std::vector<std::string> namedTail;
        auto&& paramLists = target.funcBody->paramLists;
        for (auto plIt = std::rbegin(paramLists); plIt != std::rend(paramLists); ++plIt) {
            for (auto pIt = std::rbegin((*plIt)->params); pIt != std::rend((*plIt)->params); ++pIt) {
                namedTail.push_back((*pIt)->identifier.Val());
                if (namedTail.size() + result.size() == actualArgumentSize) {
                    result.insert(std::end(result), std::rbegin(namedTail), std::rend(namedTail));
                    return result;
                }
            }
        }
    }
    return result;
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
