// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file generate VTable in CHIR and update ir
 */

#ifndef CANGJIE_CHIR_GENERATE_VTABLE_H
#define CANGJIE_CHIR_GENERATE_VTABLE_H

#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/IncrementalCompilation/IncrementalScopeAnalysis.h"
#include "cangjie/Option/Option.h"

namespace Cangjie {
namespace CHIR {
class GenerateVTable {
public:
    GenerateVTable(Package& pkg, const std::vector<CustomTypeDef*>& defs, CHIRBuilder& b, const GlobalOptions& opts);

    /**
     * @brief Create VTable.
     */
    void CreateVTable();

    /**
     * @brief Update VTable for operator func.
     */
    void UpdateOperatorVirFunc();

    /**
     * @brief Create wrapper func for virtual method.
     *
     * @param kind The result of incremental compilation.
     * @param increCachedInfo Cache info of incremental compilation.
     * @param curVirtFuncWrapDep Dependency info, used by incremental compilation.
     * @param delVirtFuncWrapForIncr Dependency info, used by incremental compilation.
     */
    void CreateVirtualFuncWrapper(const IncreKind& kind, const CompilationCache& increCachedInfo,
        VirtualWrapperDepMap& curVirtFuncWrapDep, VirtualWrapperDepMap& delVirtFuncWrapForIncr);

    void SetSrcFuncType() const;

    /**
     * @brief Create wrapper func for mut method.
     */
    void CreateMutFuncWrapper();

    /**
     * @brief Update Invoke and InvokeStatic after computing virtual func offset.
     */
    void UpdateFuncCall();

private:
    FuncBase* GetMutFuncWrapper(const Type& thisType, const std::vector<Value*>& args,
        const std::vector<Type*>& instTypeArgs, Type& retType, const FuncBase& callee);

    const Package& package;
    const std::vector<CustomTypeDef*>& candidateDefs;
    CHIRBuilder& builder;
    const GlobalOptions& opts;
    std::unordered_map<std::string, FuncBase*> mutFuncWrappers;
};
} // namespace CHIR
} // namespace Cangjie
#endif