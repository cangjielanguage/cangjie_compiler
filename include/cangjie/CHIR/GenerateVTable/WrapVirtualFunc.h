// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_WRAP_VIRTUAL_FUNC_H
#define CANGJIE_CHIR_WRAP_VIRTUAL_FUNC_H

#include <vector>

#include "cangjie/IncrementalCompilation/CompilationCache.h"
#include "cangjie/IncrementalCompilation/IncrementalScopeAnalysis.h"
#include "cangjie/CHIR/UserDefinedType.h"
#include "cangjie/CHIR/Type/CustomTypeDef.h"

namespace Cangjie::CHIR {
class WrapVirtualFunc {
public:
    WrapVirtualFunc(CHIRBuilder& builder,
        const CompilationCache& increCachedInfo, const IncreKind incrementalKind, const bool targetIsWin);
    /**
    * @brief wrap virtual function
    *
    * @param customTypeDef wrap virtual function in this def's vtable
    */
    void CheckAndWrap(CustomTypeDef& customTypeDef);
    VirtualWrapperDepMap&& GetCurVirtFuncWrapDep();
    VirtualWrapperDepMap&& GetDelVirtFuncWrapForIncr();

private:
    struct WrapperFuncGenericTable {
        std::vector<GenericType*> funcGenericTypeParams;
        std::unordered_map<const GenericType*, Type*> replaceTable;
        std::unordered_map<const GenericType*, Type*> inverseReplaceTable;
    };
    FuncBase* CreateVirtualWrapperIfNeeded(const VirtualMethodInfo& funcInfo,
        const VirtualMethodInfo& parentFuncInfo, Type& selfTy, CustomTypeDef& customTypeDef, const ClassType& parentTy);
    void CreateWrapperFuncBody(Func& wrapperFunc,
        const VirtualMethodInfo& childFuncInfo, Type& selfTy, WrapVirtualFunc::WrapperFuncGenericTable& genericTable);
    WrapperFuncGenericTable GetReplaceTableForVirtualFunc(
        const ClassType& parentTy, const std::string& funcIdentifier, const VirtualMethodInfo& parentFuncInfo);
    FuncType* RemoveThisArg(FuncType* funcTy);
    void HandleVirtualFuncWrapperForIncrCompilation(const FuncBase* wrapper, const FuncBase& curFunc);
    FuncType* GetWrapperFuncType(FuncType& parentFuncTyWithoutThisArg,
        Type& selfTy, const std::unordered_map<const GenericType*, Type*>& replaceTable, bool isStatic);

private:
    CHIRBuilder& builder;
    const CompilationCache& increCachedInfo;
    const IncreKind incrementalKind;
    const bool targetIsWin;

    std::unordered_map<std::string, FuncBase*> wrapperCache;
    VirtualWrapperDepMap curVirtFuncWrapDep;
    VirtualWrapperDepMap delVirtFuncWrapForIncr;
};
}

#endif
