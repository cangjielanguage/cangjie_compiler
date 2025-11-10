// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares factory class for names of different Objective-C interop entities.
 */

#ifndef CANGJIE_SEMA_OBJ_C_UTILS_NAME_GENERATOR_H
#define CANGJIE_SEMA_OBJ_C_UTILS_NAME_GENERATOR_H

#include "cangjie/AST/Node.h"
#include "cangjie/Mangle/BaseMangler.h"

namespace Cangjie::Interop::ObjC {
class NameGenerator {
public:
    explicit NameGenerator(const BaseMangler& mangler);
    std::string GenerateInitCjObjectName(const AST::VarDecl& target);
    std::string GenerateInitCjObjectName(const AST::FuncDecl& target);
    std::string GenerateInitCjObjectName(const AST::Decl& target);
    std::string GenerateDeleteCjObjectName(const AST::Decl& target);
    std::string GenerateMethodWrapperName(const AST::FuncDecl& target);
    std::string GeneratePropGetterWrapperName(const AST::PropDecl& target);
    std::string GetPropSetterWrapperName(const AST::PropDecl& target);
    std::string GetFieldGetterWrapperName(const AST::VarDecl& target);
    std::string GetFieldSetterWrapperName(const AST::VarDecl& target);

    /**
     * Returns name declared in @ForeignName or target.identifier if no foreign name specified.
     * The only one exception is function: If it has one param, then target.identifier + ":" will be returned.
     */
    std::string GetObjCDeclName(const AST::Decl& target);

    std::string GetObjCGetterName(const AST::Decl& arget);
    std::string GetObjCSetterName(const AST::Decl& target);
    static std::string MakeSetterName(std::string propName);

    /**
     * Returns name declared in @ForeignName split by ':'
     * or function name followed by argument names if no @ForeignName is specified
     */
    std::vector<std::string> GetObjCDeclSelectorComponents(const AST::FuncDecl& target);

    /**
     * Returns name declared in @ObjC attribute or {target.fullPackageName}.{target.identifier}
     * if no name in attribute specified
     */
    std::string GetObjCFullDeclName(const AST::Decl& target);

    /**
     * Gets the pointer to value of @ForeignName anno or @ObjCMirror/@ObjCImpl annos or returns nullptr.
     */
    Ptr<std::string> GetUserDefinedObjCName(const AST::Decl& target);

private:
    const BaseMangler& mangler;
};
} // namespace Cangjie::Interop::ObjC

#endif // CANGJIE_SEMA_OBJ_C_UTILS_NAME_GENERATOR_H
