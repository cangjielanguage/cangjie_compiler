// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CODEGEN_PACKAGE_CONTEXT_H
#define CANGJIE_CODEGEN_PACKAGE_CONTEXT_H

#include <mutex>

#include "llvm/IR/Module.h"

#include "cangjie/CHIR/IR/CHIRBuilder.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/IncrementalCompilation/CachedMangleMap.h"
#include "cangjie/Option/Option.h"

namespace Cangjie {
namespace CodeGen {
class CGModule;
template <typename ObjectType> class ObjectLocker {
public:
    // Locks the associated object and calls the given function.
    template <typename Func> decltype(auto) Do(Func&& func)
    {
        std::unique_lock<std::mutex> lock(this->locker);
        return func(object);
    }

private:
    std::mutex locker;
    ObjectType object;
};

class CGPkgContext {
public:
    CGPkgContext(CHIR::CHIRBuilder& chirBuilder, const CHIRData& chirData, const GlobalOptions& options,
        bool enableIncrement, const CachedMangleMap& cachedMangleMap);
    ~CGPkgContext();
    void Clear();

    CHIR::CHIRBuilder& GetCHIRBuilder() const
    {
        return chirBuilder;
    }

    const CHIR::Package& GetCHIRPackage() const;

    std::string GetCurrentPkgName() const;

    const GlobalOptions& GetGlobalOptions() const
    {
        return options;
    }

    CHIR::FuncBase* GetImplicitUsedFunc(const std::string& funcMangledName);

    const CachedMangleMap& GetCachedMangleMap() const
    {
        return correctedCachedMangleMap;
    }

    bool IsIncrementEnabled() const
    {
        return enableIncrement;
    }

    bool IsCGParallelEnabled() const
    {
        return cgMods.size() > 1;
    }

    bool IsLineInfoEnabled() const
    {
        return options.enableCompileDebug || options.enableCoverage || options.displayLineInfo;
    }

    void AddCGModule(std::unique_ptr<CGModule>& cgMod);
    const std::vector<std::unique_ptr<CGModule>>& GetCGModules();
    std::vector<std::unique_ptr<llvm::Module>> ReleaseLLVMModules();

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    void AddLocalizedSymbol(const std::string& symName);
    const std::set<std::string>& GetLocalizedSymbols();
#endif

    CHIR::Value* FindCHIRGlobalValue(const std::string& mangledName);

    CHIR::CHIRBuilder& chirBuilder;

private:
    const CHIRData& chirData;
    const GlobalOptions& options;
    const bool enableIncrement;
    CachedMangleMap correctedCachedMangleMap;

    std::vector<std::unique_ptr<CGModule>> cgMods;
    // Container that support quick search for target global chirValue.
    ObjectLocker<std::unordered_map<std::string, CHIR::Value*>> quickCHIRValues;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    // The symbols, which need to be changed linkageType after the link.
    ObjectLocker<std::set<std::string>> localizedSymbols;
#endif
};
} // namespace CodeGen
} // namespace Cangjie
#endif
