// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * codegen will create extension def according to CHIR's vtable, in order not to create duplicate
 * extension def, codegen won't visit vtable from imported CustomTypeDef, these vtables are assumed that
 * must be created in imported package. but there is a special case:
 * ================ package A ================
 * public interface I {}
 * open public class A {}
 *
 * ================ package B ================
 * import package A
 * public class B <: A {} // extension def B_ed_A will be created in codegen
 *
 * ================ package C ================
 * import package A
 * extend A <: I {} // extension def A_ed_I will be created in codegen
 *
 * ================ package D ================
 * import package A, B, C
 * // extension def B_ed_I is needed, but there isn't in imported packages
 *
 * so we need to create extension def B_ed_I in current package, in order to deal with this case,
 * a compiler added extend def is needed:
 * [COMPILER_ADD] extend B <: I {}
 * this def is create in current package, so extension def B_ed_I will be created in codegen
 */

#ifndef CANGJIE_CHIR_CREATE_EXTEND_DEF_FOR_IMPORTED_PARENT_H
#define CANGJIE_CHIR_CREATE_EXTEND_DEF_FOR_IMPORTED_PARENT_H

#include "cangjie/CHIR/IR/CHIRBuilder.h"
#include "cangjie/CHIR/IR/Package.h"

namespace Cangjie::CHIR {
class CreateExtendDefForImportedParent {
public:
    CreateExtendDefForImportedParent(Package& package, CHIRBuilder& builder);
    /**
     * Create compiler-added extend definitions for imported parent types.
     * 
     * This method handles the case where an imported class needs to extend an interface
     * that was extended in another package. Since codegen doesn't visit vtables from
     * imported CustomTypeDef to avoid duplicates, we need to create extension definitions
     * in the current package for such cases.
     * 
     * For example:
     * - Package A: interface I, class A
     * - Package B: class B <: A (creates B_ed_A in codegen)
     * - Package C: extend A <: I (creates A_ed_I in codegen)
     * - Package D: imports A, B, C (needs B_ed_I, which doesn't exist)
     * 
     * This method will create [COMPILER_ADD] extend B <: I {} so that B_ed_I can be
     * generated in codegen.
     * 
     * The method iterates through all imported custom type definitions, and for each
     * non-extend definition, checks if any parent type in its vtable comes from an
     * extend definition. If so, it creates a new compiler-added extend definition.
     */
    void Run();

private:
    void CreateNewExtendDef(CustomTypeDef& curDef, ClassType& parentType, std::vector<VirtualMethodInfo>& virtualMethods);
    
    Package& package;
    CHIRBuilder& builder;
};
}
#endif