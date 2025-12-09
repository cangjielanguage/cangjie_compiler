// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CGMODULE_H
#define CANGJIE_CGMODULE_H

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/CJStructTypeGCInfo.h"
#include "llvm/Support/Host.h"
#include "llvm/Transforms/Utils/Local.h"

#include "Base/CGTypes/CGArrayType.h"
#include "CGContext.h"
#include "cangjie/Basic/UGTypeKind.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Value.h"
#include "cangjie/Option/Option.h"

namespace Cangjie {
namespace CHIR {
class GlobalVar;
class Field;
} // namespace CHIR

namespace CodeGen {
class CGType;
class CGFunctionType;
class DIBuilder;
class IncrementalGen;
class CGValue {
public:
    CGValue() = default;
    virtual ~CGValue() = default;
    explicit CGValue(llvm::Value* rawVal, const CGType* cgType, bool isSRetArg = false)
        : rawVal(rawVal), cgType(cgType), isSRetArg(isSRetArg)
    {
    }

    virtual llvm::LLVMContext& GetLLVMContext()
    {
        return rawVal->getContext();
    }

    inline llvm::Value* GetRawValue() const
    {
        return rawVal;
    }

    // It is a sugar to get `rawVal` which could simplify coding process.
    inline llvm::Value* operator*() const
    {
        return rawVal;
    }

    inline const CGType* GetCGType() const
    {
        return cgType;
    }

    inline const llvm::Type* GetLLVMType() const
    {
        return cgType->GetLLVMType();
    }

    bool IsSRetArg() const
    {
        return isSRetArg;
    }

private:
    llvm::Value* rawVal;
    const CGType* cgType;
    bool isSRetArg;
};

class CGModule {
    friend class DIBuilder;

public:
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    explicit CGModule(SubCHIRPackage& subCHIRPackage, CGPkgContext& cgPkgCtx);
#endif

    ~CGModule();

    inline CGContext& GetCGContext() const
    {
        CJC_ASSERT(cgCtx && "Should not be null.");
        return *cgCtx;
    }

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    CGCFFI& GetCGCFFI() const
    {
        CJC_ASSERT(cffi && "Should not be null.");
        return *cffi;
    }
    CGValue* CreateGhostCFuncArgValue(llvm::Value& llvmValue, const CGType& ty)
    {
        auto value = std::make_unique<CGValue>(&llvmValue, &ty);
        return ghostCFuncArgValues.emplace_back(std::move(value)).get();
    }
#endif

    inline llvm::Module* GetLLVMModule() const
    {
        CJC_ASSERT(module && "Should not be null.");
        return module.get();
    }

    llvm::Module* ReleaseLLVMModule()
    {
        return module.release();
    }

    void ResetLLVMModule(llvm::Module* newModule)
    {
        module.reset(newModule);
    }

    llvm::LLVMContext& GetLLVMContext() const
    {
        return cgCtx->GetLLVMContext();
    }

    llvm::Constant* GetOrInsertBitMap(const std::string& bitStr, const std::string& name = "")
    {
        return commonBitMap->getOrInsertBitMap(bitStr, llvm::Type::getInt8PtrTy(cgCtx->GetLLVMContext()), name);
    }

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    bool InitIncrementalGen();
    void GenIncremental();
    void GenCodeGenAddedMetadata() const;
#endif
    CGFunction* GetOrInsertCGFunction(const CHIR::Value* func, bool forWrapper = false);
    CGValue* GetOrInsertGlobalVariable(const CHIR::Value* chirGV);
    llvm::GlobalVariable* GetOrCreateGlobalVariable(
        llvm::Constant* constVal, const std::string& gvContent, bool isTuple);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    llvm::GlobalVariable* GetOrCreateEnumCtorTIOrTT(const CHIR::EnumType& chirEnumType, std::size_t ctorIndex);
    std::string GetEnumCaseClassName(const CHIR::EnumType& chirEnumType, std::size_t ctorIndex);
    llvm::Function* GetOrInsertFunction(const std::string& funcMangledName, llvm::FunctionType* funcType) const;
#endif

    llvm::BasicBlock* GetMappedBB(const Cangjie::CHIR::Block* chirBB) const
    {
        if (auto it = bbMapping.find(chirBB); it != bbMapping.end()) {
            return it->second;
        }
        return nullptr;
    }

    CGValue* GetMappedValue(const Cangjie::CHIR::Value* chirValue) const
    {
        if (auto it = valueMapping.find(chirValue); it != valueMapping.end()) {
            return it->second;
        }
        return nullptr;
    }

    CGValue* GetMappedCGValue(const Cangjie::CHIR::Value* chirValue);

    // It is a sugar for `GetMappedCGValue` which could simplify coding process.
    inline CGValue* operator|(const Cangjie::CHIR::Value* chirValue)
    {
        return this->GetMappedCGValue(chirValue);
    }

    inline void SetOrUpdateMappedBB(const Cangjie::CHIR::Block* chirBB, llvm::BasicBlock* bb)
    {
        bbMapping[chirBB] = bb;
    }

    inline void SetOrUpdateMappedCGValue(const Cangjie::CHIR::Value* chirValue, std::unique_ptr<CGValue> value)
    {
        valueMapping[chirValue] = value.get();
        allocatedCGValues.emplace_back(std::move(value));
    }

    inline void SetValuesToLoad(const Cangjie::CHIR::Value* chirValue, llvm::Type* type, llvm::Value* value)
    {
        valuesToLoad[chirValue] = std::make_pair(type, value);
    }

    std::optional<std::pair<llvm::Type*, llvm::Value*>> GetValuesToLoad(const Cangjie::CHIR::Value* chirValue)
    {
        auto it = valuesToLoad.find(chirValue);
        return it != valuesToLoad.end() ? std::optional(it->second) : std::nullopt;
    }

    inline void SetBoxedValuesToLoad(const Cangjie::CHIR::Value* chirValue, llvm::Type* type, llvm::Value* value)
    {
        boxedValuesToLoad[chirValue] = std::make_pair(type, value);
    }

    std::optional<std::pair<llvm::Type*, llvm::Value*>> GetBoxedValuesToLoad(const Cangjie::CHIR::Value* chirValue)
    {
        auto it = boxedValuesToLoad.find(chirValue);
        return it != boxedValuesToLoad.end() ? std::optional(it->second) : std::nullopt;
    }

    void EraseUselessInstsAndDeclarations();
    void EraseUselessGVAndFunctions();
    void Opt();

    /**
     * Check the module for errors.
     * @return false if anything is broken.
     */
    bool Verify() const;

    void InitDebugInfo();

    std::unique_ptr<DIBuilder> diBuilder;

    llvm::Constant* GenerateTypeNameConstantString(const std::string& typeMangledName, bool isWeakODR, const std::string& qualifiedName = "") const;

    llvm::Value* GenerateUnitTypeValue();

    llvm::Constant* GetExceptionIntrinsicPersonality();

    size_t GetTypeSize(llvm::Type* codeGenType) const
    {
        return module->getDataLayout().getTypeAllocSize(codeGenType);
    }

    std::vector<llvm::Constant*> InitRawArrayUInt8Constants() const;

    llvm::GlobalVariable* GeneratePrivateUnnamedAddrConstant(const std::string& name, llvm::Constant* constVal) const;

    inline void ClearLinkNameUsedInMeta()
    {
        linkNameUsedInMeta.clear();
    }
    bool IsLinkNameUsedInMeta(const std::string& linkageName);

    inline static std::string GetDataLayoutString(const Triple::Info& target);
    inline static std::string GetTargetTripleString(const Triple::Info& target);

    void AddNonExternalExtensionDef(llvm::GlobalVariable* extensionDef)
    {
        (void)nonExternalExtensionDefs.emplace_back(extensionDef);
    }

    std::vector<llvm::GlobalVariable*> GetNonExternalExtensionDefs() const
    {
        return nonExternalExtensionDefs;
    }

    void AddExternalExtensionDef(llvm::GlobalVariable* extensionDef)
    {
        (void)externalExtensionDefs.emplace_back(extensionDef);
    }

    std::vector<llvm::GlobalVariable*> GetExternalExtensionDefs() const
    {
        return externalExtensionDefs;
    }

    size_t GetNonExternalExtensionDefsNum() const
    {
        return nonExternalExtensionDefs.size();
    }

    const std::vector<std::unique_ptr<CGExtensionDef>>& GetAllCGExtensionDefs();

    std::vector<llvm::GlobalVariable*> GetAliveStaticGIs() const
    {
        std::vector<llvm::GlobalVariable*> staticGIs;
        for (auto& staticGIName : cgCtx->GetStaticGINames()) {
            if (auto gi = module->getNamedGlobal(staticGIName)) {
                (void)staticGIs.emplace_back(gi);
            }
        }
        return staticGIs;
    }

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    void GenTypeInfo();
    void GenTypeTemplate();

    void DelayGenTypeInfo(CGType* cgType)
    {
        delayGenTypeInfo.push(cgType);
    }
    void DelayGenTypeTemplate(CGType* cgType)
    {
        delayGenTypeTemplate.push(cgType);
    }
#endif
private:
    std::unique_ptr<CGContext> cgCtx;
    std::unique_ptr<llvm::Module> module;
    std::unique_ptr<llvm::CommonBitmap> commonBitMap;
    std::unique_ptr<IncrementalGen> incrementalGen;
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    std::unique_ptr<CGCFFI> cffi;
    std::vector<std::unique_ptr<CGValue>> ghostCFuncArgValues;
#endif

    std::unordered_map<llvm::Function*, CGFunction*> functions;
    std::unordered_map<const Cangjie::CHIR::Block*, llvm::BasicBlock*> bbMapping;
    std::unordered_map<const Cangjie::CHIR::Value*, CGValue*> valueMapping;
    std::unordered_map<const Cangjie::CHIR::Value*, std::pair<llvm::Type*, llvm::Value*>> valuesToLoad;
    std::unordered_map<const Cangjie::CHIR::Value*, std::pair<llvm::Type*, llvm::Value*>> boxedValuesToLoad;
    std::unordered_set<std::string> linkNameUsedInMeta;
    std::vector<std::unique_ptr<CGValue>> allocatedCGValues;
    std::vector<llvm::GlobalVariable*> externalExtensionDefs;
    std::vector<llvm::GlobalVariable*> nonExternalExtensionDefs;
    std::vector<std::unique_ptr<CGExtensionDef>> cgExtensionDefs;
    std::stack<CGType*> delayGenTypeInfo;
    std::stack<CGType*> delayGenTypeTemplate;

    void MergeUselessBBIntoPreds(llvm::Function* function) const;
    void EraseUnusedFuncs(const std::function<bool(const llvm::GlobalObject&)> extraCond);
    void EraseUnusedGVs(const std::function<bool(const llvm::GlobalObject&)> extraCond);
    bool CheckUnusedGV(const llvm::GlobalVariable* var, const std::set<std::string>& llvmUsed);
};

class CGFunction : public CGValue {
    friend class IRBuilder2;

public:
    explicit CGFunction(
        llvm::Function* function, const CGFunctionType* cgType, const CHIR::Value* chirFunc, CGModule& cgMod);

    bool operator==(CGFunction* that)
    {
        return GetRawValue()->getName() == that->GetRawValue()->getName();
    }

    const CHIR::Value& GetOriginal() const
    {
        return chirFunc;
    }

    inline llvm::Function* GetRawFunction() const
    {
        return llvm::cast<llvm::Function>(GetRawValue());
    }

    inline const CGFunctionType* GetCGFunctionType() const
    {
        return dynamic_cast<const CGFunctionType*>(GetCGType());
    }

    bool IsSRet() const
    {
        return isStructRet;
    }

    llvm::Argument* GetArgByIndexFromCHIR(size_t idx) const;

    void Opt() const;

    std::unordered_map<const CHIR::Type*, std::function<llvm::Value*(IRBuilder2&)>> genericParamsMap;

    void DumpIR() const;

    static void RemoveUnreachableBlocks(llvm::Function& function);
    llvm::Function* GetWrapperFunction() const
    {
        return wrapperF;
    }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    static void EraseReplaceableAlloca(const CGModule& cgModule, llvm::Function& function);
    static void AddZeroInitForStructWithRefField(CGModule& cgModule, llvm::Function& function);
#endif

private:
    CGModule& cgMod;
    const bool isStructRet;
    llvm::Function* wrapperF{nullptr};
    const CHIR::Value& chirFunc;
};
} // namespace CodeGen
} // namespace Cangjie

#endif // CANGJIE_CGMODULE_H
