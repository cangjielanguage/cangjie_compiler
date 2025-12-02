// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_REPLACE_SRC_CODE_IMPORTED_VAL_H
#define CANGJIE_CHIR_REPLACE_SRC_CODE_IMPORTED_VAL_H

#include "cangjie/CHIR/IR/CHIRBuilder.h"
#include "cangjie/CHIR/IR/Package.h"

namespace Cangjie::CHIR {
class ReplaceSrcCodeImportedVal {
public:
    ReplaceSrcCodeImportedVal(
        Package& package, std::unordered_map<std::string, FuncBase*>& implicitFuncs, CHIRBuilder& builder);
    
    /**
     * Replace source code imported functions and variables with imported symbols.
     * 
     * This method performs the following transformations:
     * 1. Creates ImportedFunc and ImportedVar symbols for source code imported functions and variables
     * 2. Removes useless definitions created during closure conversion (useless classes and lambdas)
     * 3. Replaces all usages of source code imported functions/variables with their imported symbols
     * 4. Updates method and static variable pointers in custom type definitions and their vtables
     * 5. Removes the original source code imported functions and variables from the package
     * 
     * @param srcCodeImportedFuncs Set of functions imported from source code to be replaced
     * @param srcCodeImportedVars Set of global variables imported from source code to be replaced
     * @param uselessClasses Set of class definitions that are useless and should be removed
     * @param uselessLambda Set of lambda functions that are useless and should be removed
     */
    void Run(const std::unordered_set<Func*>& srcCodeImportedFuncs,
        const std::unordered_set<GlobalVar*>& srcCodeImportedVars,
        const std::unordered_set<ClassDef*>& uselessClasses,
        const std::unordered_set<Func*>& uselessLambda);

private:
    void CreateSrcImpotedValueSymbol(const std::unordered_set<Func*>& srcCodeImportedFuncs,
        const std::unordered_set<GlobalVar*>& srcCodeImportedVars);
    void CreateSrcImportedFuncSymbol(Func& fn);
    void CreateSrcImportedVarSymbol(GlobalVar& gv);
    std::unordered_set<Func*> RemoveUselessDefFromCC(
        const std::unordered_set<ClassDef*>& uselessClasses, const std::unordered_set<Func*>& uselessLambda);
    void ReplaceSrcCodeImportedFuncUsers(std::unordered_set<Func*>& toBeRemovedFuncs,
        std::unordered_map<CustomTypeDef*, std::unordered_map<Value*, Value*>>& replaceTable);
    void ReplaceSrcCodeImportedVarUsers(
        std::unordered_set<Func*>& toBeRemovedFuncs, std::unordered_set<GlobalVar*>& toBeRemovedVars,
        std::unordered_map<CustomTypeDef*, std::unordered_map<Value*, Value*>>& replaceTable);

private:
    Package& package;
    std::unordered_map<std::string, FuncBase*>& implicitFuncs;
    CHIRBuilder& builder;

    std::unordered_map<Func*, ImportedFunc*> srcCodeImportedFuncMap;
    std::unordered_map<GlobalVar*, ImportedVar*> srcCodeImportedVarMap;
};
} // namespace Cangjie::CHIR
#endif // CANGJIE_CHIR_REPLACE_SRC_CODE_IMPORTED_VAL_H