// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "CGPkgContext.h"

#include "CGModule.h"
#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/Utils/ProfileRecorder.h"

namespace Cangjie::CodeGen {
CGPkgContext::CGPkgContext(CHIR::CHIRBuilder& chirBuilder, const CHIRData& chirData, const GlobalOptions& options,
    bool enableIncrement, const CachedMangleMap& cachedMangleMap)
    : chirBuilder(chirBuilder), chirData(chirData), options(options), enableIncrement(enableIncrement)
{
    cachedMangleMap.Dump();
    correctedCachedMangleMap.importedInlineDecls = cachedMangleMap.importedInlineDecls;
    correctedCachedMangleMap.newExternalDecls = cachedMangleMap.newExternalDecls;
    for (auto& incrRemovedDecl : cachedMangleMap.incrRemovedDecls) {
        if (FindCHIRGlobalValue(incrRemovedDecl)) {
            continue;
        }
        correctedCachedMangleMap.incrRemovedDecls.emplace(incrRemovedDecl);
    }
    correctedCachedMangleMap.Dump();
}

CGPkgContext::~CGPkgContext() = default;

void CGPkgContext::Clear()
{
    for (auto& cgMod : cgMods) {
        cgMod->GetCGContext().Clear();
    }
    correctedCachedMangleMap.Clear();
    quickCHIRValues.Do([](std::unordered_map<std::string, CHIR::Value*>& object) { object.clear(); });
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    localizedSymbols.Do([](std::set<std::string>& object) { object.clear(); });
#endif
}

std::string CGPkgContext::GetCurrentPkgName() const
{
    auto curPackage = chirData.GetCurrentCHIRPackage();
    CJC_NULLPTR_CHECK(curPackage);
    return curPackage->GetName();
}

CHIR::FuncBase* CGPkgContext::GetImplicitUsedFunc(const std::string& funcMangledName)
{
    auto funcs = chirData.GetImplicitFuncs();
    auto it = funcs.find(funcMangledName);
    CJC_ASSERT(it != funcs.end());
    return it->second;
}

void CGPkgContext::AddCGModule(std::unique_ptr<CGModule>& cgMod)
{
    cgMods.emplace_back(std::move(cgMod));
}
const std::vector<std::unique_ptr<CGModule>>& CGPkgContext::GetCGModules()
{
    return cgMods;
}
std::vector<std::unique_ptr<llvm::Module>> CGPkgContext::ReleaseLLVMModules()
{
    std::vector<std::unique_ptr<llvm::Module>> llvmModules;
    std::for_each(cgMods.begin(), cgMods.end(), [&llvmModules](std::unique_ptr<CGModule>& cgMod) {
        llvmModules.emplace_back(cgMod->ReleaseLLVMModule());
        cgMod->GetCGContext().Clear();
    });
    cgMods.clear();
    return llvmModules;
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
void CGPkgContext::AddLocalizedSymbol(const std::string& symName)
{
    localizedSymbols.Do([&symName](std::set<std::string>& object) { object.emplace(symName); });
}
const std::set<std::string>& CGPkgContext::GetLocalizedSymbols()
{
    return localizedSymbols.Do(
        [](const std::set<std::string>& object) -> const std::set<std::string>& { return object; });
}
#endif

CHIR::Value* CGPkgContext::FindCHIRGlobalValue(const std::string& mangledName)
{
    const CHIR::Package& capturedChirPkg = GetCHIRPackage();
    return quickCHIRValues.Do(
        [&capturedChirPkg, &mangledName](std::unordered_map<std::string, CHIR::Value*>& object) -> CHIR::Value* {
            if (object.empty()) {
                object.reserve(capturedChirPkg.GetGlobalFuncs().size() + capturedChirPkg.GetGlobalVars().size() +
                    capturedChirPkg.GetImportedVarAndFuncs().size());
                for (auto chirFunc : capturedChirPkg.GetGlobalFuncs()) {
                    object.emplace(chirFunc->GetIdentifierWithoutPrefix(), chirFunc);
                }
                for (auto chirGv : capturedChirPkg.GetGlobalVars()) {
                    object.emplace(chirGv->GetIdentifierWithoutPrefix(), chirGv);
                }
                for (auto importedValue : capturedChirPkg.GetImportedVarAndFuncs()) {
                    object.emplace(importedValue->GetIdentifierWithoutPrefix(), importedValue);
                }
            }

            if (auto target = object.find(mangledName); target != object.end()) {
                return target->second;
            } else {
                return nullptr;
            }
        });
}

const CHIR::Package& CGPkgContext::GetCHIRPackage() const
{
    auto chirPackage = chirData.GetCurrentCHIRPackage();
    CJC_NULLPTR_CHECK(chirPackage);
    return *chirPackage;
}
} // namespace Cangjie::CodeGen
