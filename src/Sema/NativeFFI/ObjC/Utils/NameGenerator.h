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
    std::string GenerateInitCjObjectName(const AST::FuncDecl& target);
    std::string GenerateDeleteCjObjectName(const AST::ClassDecl& target);
    std::string GenerateMethodWrapperName(const AST::FuncDecl& target);
    std::string GeneratePropGetterWrapperName(const AST::PropDecl& target);
    std::string GetPropSetterWrapperName(const AST::PropDecl& target);
    std::string GetFieldGetterWrapperName(const AST::VarDecl& target);
    std::string GetFieldSetterWrapperName(const AST::VarDecl& target);

    /**
     * Returns name declared in @ForeignName or target.identifier if no foreign name specified
     */
    std::string GetObjCDeclName(const AST::Decl& target);

    /**
     * Returns name declared in @ObjC attribute or {target.fullPackageName}.{target.identifier}
     * if no name in attribute specified
     */
    std::string GetObjCFullDeclName(const AST::Decl& target);

private:
    Ptr<std::string> GetUserDefinedObjCName(const AST::Decl& target);
    const BaseMangler& mangler;
};
} // namespace Cangjie::Interop::ObjC

#endif // CANGJIE_SEMA_OBJ_C_UTILS_NAME_GENERATOR_H
