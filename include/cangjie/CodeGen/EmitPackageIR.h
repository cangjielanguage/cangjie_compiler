// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_EMITPACKAGEIR_H
#define CANGJIE_EMITPACKAGEIR_H

#include "llvm/IR/Module.h"

#include "cangjie/CHIR/IR/CHIRBuilder.h"
#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/FrontendTool/IncrementalCompilerInstance.h"
#include "cangjie/Option/Option.h"

namespace Cangjie::CodeGen {
/**
 * @brief This function generates the package modules.
 *        Note that after using llvm::Module, call the ClearPackageModules to clear the memory.
 *
 * @param chirBuilder A CHIRBuilder of CHIR.
 * @param chirData CHIRData of a complete package.
 * @param options GlobalOptions to compile a package.
 * @param compilerInstance DefaultCompilerInstance.
 * @param enableIncrement A falg, indicating whether incremental compilation is enabled.
 * @return A vector of std::unique_ptr<llvm::Module>. If --aggressive-parallel-compile is enabled,
 *         multiple llvm::Modules are returned. Otherwise, only one llvm::Module is returned.
 */
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
std::vector<std::unique_ptr<llvm::Module>> GenPackageModules(CHIR::CHIRBuilder& chirBuilder, const CHIRData& chirData,
    const GlobalOptions& options, DefaultCompilerInstance& compilerInstance, bool enableIncrement);
#endif

/**
 * @brief Save the LLVM module to the specified Bitcode file path
 *
 * @param module A llvm::Module to be cached.
 * @param bcFilePath CHIRData of a complete package
 * @return If the saving is successful, true is returned. Otherwise, false is returned.
 */
bool SavePackageModule(const llvm::Module& module, const std::string& bcFilePath);

/**
 * @brief Clear and release all modules. It ensures that all resources are properly released and cleaned up.
 *
 * @param packageModules A vector of unique pointers to LLVM modules to be cleared.
 */
void ClearPackageModules(std::vector<std::unique_ptr<llvm::Module>>& packageModules);
} // namespace Cangjie::CodeGen

#endif // CANGJIE_EMITPACKAGEIR_H
