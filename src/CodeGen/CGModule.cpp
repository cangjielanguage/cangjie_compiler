// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "CGModule.h"

#include "llvm/IR/Attributes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Host.h"

#include "Base/CGTypes/CGFunctionType.h"
#include "Base/CGTypes/CGType.h"
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
#include "CJNative/CGTypes/CGExtensionDef.h"
#include "CJNative/CJNativeCGCFFI.h"
#endif
#include "DIBuilder.h"
#include "IRAttribute.h"
#include "IRBuilder.h"
#include "IncrementalGen/IncrementalGen.h"
#include "Utils/CGCommonDef.h"
#include "cangjie/CHIR/AttributeInfo.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Value.h"
#include "cangjie/Utils/ProfileRecorder.h"

namespace Cangjie::CodeGen {
inline std::string CGModule::GetDataLayoutString(const Triple::Info& target)
{
    if (target.IsMacOS() && target.arch == Triple::ArchType::AARCH64) {
        return "e-m:o-i64:64-i128:128-n32:64-S128";
    }
    if (target.arch == Triple::ArchType::ARM32) {
        return "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64";
    }
    return target.IsMinGW() ? "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
                            : "e-m:e-i64:64-f80:128-n8:16:32:64-S128";
}

inline std::string CGModule::GetTargetTripleString(const Triple::Info& target)
{
    if (target.IsMacOS()) {
        if (target.arch == Triple::ArchType::AARCH64) {
            return "arm64-apple-macosx12.0.0";
        }
        return target.ArchToString() + "-apple-macosx12.0.0";
    } else if (target.arch == Triple::ArchType::ARM32 && target.os == Triple::OSType::LINUX) {
        return "armv7a-linux-gnu";
    } else {
        return llvm::sys::getDefaultTargetTriple();
    }
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
CGModule::CGModule(SubCHIRPackage& subCHIRPackage, CGPkgContext& cgPkgCtx)
    : cgCtx(std::make_unique<CGContext>(subCHIRPackage, cgPkgCtx)),
      module(std::make_unique<llvm::Module>(
          std::to_string(subCHIRPackage.subCHIRPackageIdx) + '-' + cgPkgCtx.GetCurrentPkgName(),
          cgCtx->GetLLVMContext())),
      commonBitMap(std::make_unique<llvm::CommonBitmap>(*module))
#endif
{
    // This flag is used to specify that the generated bc is cangjie bc in LTO mode.
    module->addModuleFlag(llvm::Module::Warning, "CJBC", static_cast<uint32_t>(1));
    // This flag is used to specify that the generated bc has not been opt-optimized.
    module->addModuleFlag(llvm::Module::Warning, "Cangjie_OPT", static_cast<uint32_t>(0));
    // LLVM BE requires package name to distinguish difference instantiations of generic functions between packages.
    if (cgCtx->GetCompileOptions().target.IsMacOS()) {
        auto identifier = cgCtx->GetCurrentPkgName();
        std::replace(identifier.begin(), identifier.end(), '/', '.');
        module->addModuleFlag(
            llvm::Module::Warning, "Cangjie_PACKAGE_ID", llvm::MDString::get(module->getContext(), identifier));
    }

    const auto& options = cgCtx->GetCompileOptions();
    module->setDataLayout(CGModule::GetDataLayoutString(options.target));
    module->setTargetTriple(CGModule::GetTargetTripleString(options.target));
    InitDebugInfo();
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (options.target.os == Triple::OSType::WINDOWS && options.target.arch == Triple::ArchType::X86_64) {
        cffi = std::make_unique<WindowsAmd64CJNativeCGCFFI>(*this);
    } else if (Utils::In(options.target.os, {Triple::OSType::LINUX, Triple::OSType::DARWIN}) &&
        options.target.arch == Triple::ArchType::X86_64) {
        cffi = std::make_unique<LinuxAmd64CJNativeCGCFFI>(*this);
    } else if (options.target.arch == Triple::ArchType::AARCH64 &&
        Utils::In(options.target.os, {Triple::OSType::LINUX})) {
        cffi = std::make_unique<LinuxAarch64CJNativeCGCFFI>(*this);
#ifdef __APPLE__
    } else if (options.target.arch == Triple::ArchType::AARCH64 && options.target.os == Triple::OSType::DARWIN) {
        cffi = std::make_unique<MacAArch64CJNativeCGCFFI>(*this);
    } else if (options.target.arch == Triple::ArchType::AARCH64 && options.target.os == Triple::OSType::IOS) {
        cffi = std::make_unique<MacAArch64CJNativeCGCFFI>(*this);
#endif
    } else if (options.target.arch == Triple::ArchType::ARM32) {
        cffi = std::make_unique<LinuxOhosArm32CJNativeCGCFFI>(*this);
    } else {
        // Rollback to linux x86_64 abi.
        cffi = std::make_unique<LinuxAmd64CJNativeCGCFFI>(*this);
    }
#endif
}

CGModule::~CGModule()
{
    diBuilder = nullptr;
    incrementalGen = nullptr;
    if (module) {
        auto context = &module->getContext();
        // Then, release `module`:
        module = nullptr;
        // Finally, release llvm context
        delete context;
        context = nullptr;
    }
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
void CGModule::GenTypeInfo()
{
    while (!delayGenTypeInfo.empty()) {
        auto cgType = delayGenTypeInfo.top();
        delayGenTypeInfo.pop();
        cgType->GenTypeInfo();
    }
}
void CGModule::GenTypeTemplate()
{
    while (!delayGenTypeTemplate.empty()) {
        auto cgType = delayGenTypeTemplate.top();
        delayGenTypeTemplate.pop();
        cgType->GenTypeTemplate();
    }
}

bool CGModule::InitIncrementalGen()
{
    CJC_ASSERT(cgCtx->GetCGPkgContext().IsIncrementEnabled());
    incrementalGen = std::make_unique<IncrementalGen>(cgCtx->IsCGParallelEnabled());
    auto cachePath =
        cgCtx->GetCompileOptions().GenerateCachedPathNameForCodeGen(module->getSourceFileName(), "_cache.bc");
    return incrementalGen->Init(cachePath, cgCtx->GetLLVMContext());
}

void CGModule::GenIncremental()
{
    auto& option = cgCtx->GetCompileOptions();
    GenCodeGenAddedMetadata();
    if (cgCtx->GetCGPkgContext().IsIncrementEnabled()) {
        CJC_NULLPTR_CHECK(incrementalGen);
        auto& llvmStdout = llvm::outs();
        if (llvm::verifyModule(*module, &llvmStdout)) {
            InternalError("Incremental part failed to be compiled");
        }
        if (option.NeedDumpIRToFile()) {
            const auto& dp = GenDumpPath(
                option.output, cgCtx->GetCurrentPkgName(), "Incre", module->getSourceFileName(), "incre.ll");
            DumpIR(*module, dp);
        }
        auto newModule = incrementalGen->LinkModules(ReleaseLLVMModule(), cgCtx->GetCachedMangleMap());
        for (auto name: incrementalGen->GetIncrLLVMUsedNames()) {
            GetCGContext().AddLLVMUsedVars(name);
        }
        for (auto name : incrementalGen->GetIncrCachedStaticGINames()) {
            GetCGContext().RegisterStaticGIName(name);
        }
        ResetLLVMModule(newModule);
    }

    if (option.enIncrementalCompilation) {
        // Save .bc for the potential incremental compilation before deeper erasure.
        auto cachePath = option.GenerateCachedPathNameForCodeGen(module->getSourceFileName(), "_cache.bc");
        CodeGen::SaveToBitcodeFile(*module, cachePath);
        if (auto namedMD = module->getNamedMetadata("CodeGenAddedForIncr"); namedMD) {
            namedMD->eraseFromParent();
        }
        if (auto namedMD = module->getNamedMetadata("StaticGenericTIsForIncr"); namedMD) {
            namedMD->eraseFromParent();
        }
    }
}

void CGModule::GenCodeGenAddedMetadata() const
{
    auto codegenAddedNamedMD = GetLLVMModule()->getOrInsertNamedMetadata("CodeGenAddedForIncr");
    auto codegenAddedFuncsOrVars = GetCGContext().GetCodeGenAddedFuncsOrVars();
    for (auto& kv : codegenAddedFuncsOrVars) {
        std::vector<llvm::Metadata*> ops;
        ops.push_back(llvm::MDString::get(GetLLVMContext(), kv.first));
        for (auto& name : kv.second) {
            ops.push_back(llvm::MDString::get(GetLLVMContext(), name));
        }
        auto md = llvm::MDTuple::get(GetLLVMContext(), ops);
        codegenAddedNamedMD->addOperand(md);
    }
    auto staticGINames = GetCGContext().GetStaticGINames();
    auto staticGINamedMD = GetLLVMModule()->getOrInsertNamedMetadata("StaticGenericTIsForIncr");
    for (auto name : staticGINames) {
        staticGINamedMD->addOperand(llvm::MDTuple::get(GetLLVMContext(), llvm::MDString::get(GetLLVMContext(), name)));
    }
}
#endif

namespace {

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
inline void SetCFFIStub(const CHIR::Value& func, llvm::Function* function)
{
    CJC_NULLPTR_CHECK(function);
    if (func.IsFuncWithBody()) {
        const CHIR::Func& funcNode = VirtualCast<const CHIR::Func&>(func);
        if (funcNode.IsCFunc()) {
            if (!funcNode.IsCFFIWrapper()) {
                function->addFnAttr(C2CJ_ATTR);
            } else if (funcNode.GetIdentifier().find(':') != std::string::npos) {
                function->addFnAttr("pkg_c_wrapper");
            } else {
                function->addFnAttr(CJSTUB_ATTR);
            }
        }
    }
}
#endif

void AddToGenericParamMapUnderExtendScope(const llvm::Function* function, std::unique_ptr<CGFunction>& cgFunc,
    const CHIR::FuncBase& f, std::optional<size_t> outerTIIdx)
{
    CJC_NULLPTR_CHECK(function);
    auto extendDef = StaticCast<CHIR::ExtendDef*>(f.GetParentCustomTypeDef());
    auto extendedType = extendDef->GetExtendedType();
    for (auto gt : extendDef->GetGenericTypeParams()) {
        std::vector<size_t> path;
        CGExtensionDef::FoundGenericTypeAndCollectPath(*extendedType, *gt, path);
        auto outerTi = function->getArg(static_cast<unsigned>(outerTIIdx.value()));
        (void)cgFunc->genericParamsMap.emplace(gt, [outerTi, path, extendedType](IRBuilder2& irBuilder) {
            auto entryTypeArgs = irBuilder.GetTypeArgsFromTypeInfo(outerTi);
            std::unordered_map<const CHIR::Type*, CGExtensionDef::InnerTiInfo> map;
            std::queue<size_t> remainPath;
            for (auto idx : path) {
                remainPath.push(idx);
            }
            return CGExtensionDef::GetTypeInfoWithPath(
                irBuilder, *extendedType, entryTypeArgs, std::move(remainPath), map);
        });
    }
}

void AddToGenericParamMapWithOuterTI(const llvm::Function* function, std::unique_ptr<CGFunction>& cgFunc,
    const CHIR::Value& func, std::optional<size_t> outerTIIdx)
{
    CJC_NULLPTR_CHECK(function);
    uint32_t idx = 0;
    auto outerTiArg = function->getArg(static_cast<unsigned>(outerTIIdx.value()));
    outerTiArg->setName("outerTI");
    auto outerDef = VirtualCast<const CHIR::FuncBase&>(func).GetParentCustomTypeDef();
    CJC_ASSERT(outerDef);
    if (!outerDef->GetGenericTypeParams().empty()) {
        auto outerType = outerDef->IsExtend() ? dynamic_cast<const CHIR::ExtendDef*>(outerDef)->GetExtendedType()
                                              : outerDef->GetType();
        (void)cgFunc->genericParamsMap.emplace(
            outerType, [outerTiArg]([[maybe_unused]] IRBuilder2& irBuilder) { return outerTiArg; });
    }
    for (auto gt : outerDef->GetGenericTypeParams()) {
        (void)cgFunc->genericParamsMap.emplace(gt, [function, outerTIIdx, idx](IRBuilder2& irBuilder) {
            auto outerTI = function->getArg(static_cast<unsigned>(outerTIIdx.value()));
            auto tiPtrType = CGType::GetOrCreateTypeInfoPtrType(irBuilder.GetLLVMContext());
            auto typeArgs = irBuilder.GetTypeArgsFromTypeInfo(outerTI);
            auto tiAddr = irBuilder.CreateConstInBoundsGEP1_32(tiPtrType, typeArgs, idx);
            return irBuilder.LLVMIRBuilder2::CreateLoad(tiPtrType, tiAddr);
        });
        ++idx;
    }
}
} // namespace

CGFunction* CGModule::GetOrInsertCGFunction(const CHIR::Value* func, bool forWrapper)
{
    CJC_NULLPTR_CHECK(func);
    if (auto it = valueMapping.find(func); it != valueMapping.end()) {
        return dynamic_cast<CGFunction*>(it->second);
    }
    auto cgFuncType = StaticCast<CGFunctionType*>(CGType::GetOrCreateWithNode(*this, func, true, forWrapper));
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    auto chirFuncTy = StaticCast<CHIR::FuncType*>(func->GetType());
    CJC_NULLPTR_CHECK(chirFuncTy);
    llvm::FunctionType* functionType =
        chirFuncTy->IsCFunc() ? cffi->GetCFuncType(*chirFuncTy) : cgFuncType->GetLLVMFunctionType();
#endif
    auto function = llvm::cast<llvm::Function>(
        module->getOrInsertFunction(func->GetIdentifierWithoutPrefix(), functionType).getCallee());
    if (forWrapper) {
        function->addFnAttr("wrapper");
    }
    if (func->TestAttr(CHIR::Attribute::NO_INLINE)) {
        function->addFnAttr(llvm::Attribute::NoInline);
    }
    if (cgFuncType->HasBasePtr()) {
        AddFnAttr(function, llvm::Attribute::get(module->getContext(), STRUCT_MUT_FUNC_ATTR)); // Deprecated
        AddFnAttr(function, llvm::Attribute::get(module->getContext(), THIS_PARAM_HAS_BP));
    }

    auto chirFunc = VirtualCast<const CHIR::FuncBase*>(func);
    auto chirLinkage = chirFunc->Get<CHIR::LinkTypeInfo>();
    if (func->IsFuncWithBody()) {
        auto chirFunc = VirtualCast<const CHIR::Func*>(func);
        auto chirLinkage = chirFunc->Get<CHIR::LinkTypeInfo>();
        bool markByMD = cgCtx->IsCGParallelEnabled() && !IsCHIRWrapper(func->GetIdentifierWithoutPrefix());
        AddLinkageTypeMetadata(*function, CHIRLinkage2LLVMLinkage(chirLinkage), markByMD);
    } else if (chirLinkage == Linkage::EXTERNAL_WEAK) {
        // Import function in if branch of @IfAvailable need to mark as EXTERNAL_WEAK.
        function->setLinkage(llvm::GlobalValue::ExternalWeakLinkage);
    }

    auto cgFunc = std::make_unique<CGFunction>(function, cgFuncType, func, *this);

    for (auto& [gt, idx] : cgFuncType->GetGenericParamIndicesMap()) {
        function->getArg(static_cast<unsigned>(idx))->setName("ti." + gt->GetSrcCodeIdentifier());
        auto index = idx;
        (void)cgFunc->genericParamsMap.emplace(
            gt, [function, index](IRBuilder2&) { return function->getArg(static_cast<unsigned>(index)); });
    }

    auto outerTIIdx = cgFuncType->GetOuterTypeInfoIndex();

    // If this is a function defined within `Extend<T, ..., K>` scope,  we need to
    // add the `T`, ..., `K` into the genericParamsMap of this function.
    if (auto f = DynamicCast<const CHIR::FuncBase*>(func); f && f->IsInExtend()) {
        CJC_ASSERT(outerTIIdx.has_value());
        AddToGenericParamMapUnderExtendScope(function, cgFunc, *f, outerTIIdx);
    }

    if (outerTIIdx.has_value()) {
        AddToGenericParamMapWithOuterTI(function, cgFunc, *func, outerTIIdx);
    }

    if (auto thisTIIdx = cgFuncType->GetThisTypeInfoIndex()) {
        auto thisTiArg = function->getArg(static_cast<unsigned>(thisTIIdx.value()));
        thisTiArg->setName("thisTI");
    }

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    SetCFFIStub(*func, function);
    if (chirFuncTy->IsCFunc()) {
        cffi->AddFunctionAttr(*chirFuncTy, *function);
    } else {
        SetGCCangjie(function);
        auto chirParamTypes = dynamic_cast<CHIR::FuncType*>(func->GetType())->GetParamTypes();
        for (size_t idx = 0; idx < chirParamTypes.size(); ++idx) {
            if (chirParamTypes[idx]->IsStruct()) {
                cgFunc->GetArgByIndexFromCHIR(idx)->addAttr(llvm::Attribute::ReadOnly);
                cgFunc->GetArgByIndexFromCHIR(idx)->addAttr(llvm::Attribute::NoCapture);
            }
        }
    }
    if (func->IsFuncWithBody()) {
        if (!VirtualCast<const CHIR::Func*>(func)->IsCFFIWrapper()) {
            SetGCCangjie(function);
        }
    }
    if (func->TestAttr(CHIR::Attribute::NO_SIDE_EFFECT)) {
        function->addFnAttr(llvm::Attribute::NoUnwind);
        function->addFnAttr(llvm::Attribute::ReadOnly);
        function->addFnAttr(llvm::Attribute::WillReturn);
    }
#endif
    auto ret = cgFunc.get();
    (void)functions.emplace(function, ret);
    SetOrUpdateMappedCGValue(func, std::move(cgFunc));
    return ret;
}

CGValue* CGModule::GetOrInsertGlobalVariable(const CHIR::Value* chirGV)
{
    CJC_ASSERT(chirGV && "chirGV is null");
    if (auto it = valueMapping.find(chirGV); it != valueMapping.end()) {
        return it->second;
    }
    auto cgVarType = CGType::GetOrCreate(*this, chirGV->GetType());
    llvm::GlobalVariable* gv = llvm::cast<llvm::GlobalVariable>(module->getOrInsertGlobal(
        chirGV->GetIdentifierWithoutPrefix(), cgVarType->GetPointerElementType()->GetLLVMType()));
    if (chirGV->IsGlobalVarInCurPackage()) {
        auto chirLinkage = chirGV->Get<CHIR::LinkTypeInfo>();
        AddLinkageTypeMetadata(*gv, CHIRLinkage2LLVMLinkage(chirLinkage), cgCtx->IsCGParallelEnabled());
    }
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    gv->addAttribute(CJGLOBAL_VALUE_ATTR);
    if (cgCtx->GetCompileOptions().EnableAsan() || cgCtx->GetCompileOptions().EnableHwAsan()) {
        gv->addAttribute("address_sanitize_global");
    }
    if (chirGV->TestAttr(CHIR::Attribute::NON_RECOMPILE)) {
        gv->addAttribute("nonRecompile");
    }
#endif
    auto cgVar = std::make_unique<CGValue>(gv, cgVarType);
    auto ret = cgVar.get();
    SetOrUpdateMappedCGValue(chirGV, std::move(cgVar));
    return ret;
}

CGValue* CGModule::GetMappedCGValue(const CHIR::Value* chirValue)
{
    CJC_NULLPTR_CHECK(chirValue);
    if (auto cgValue = GetMappedValue(chirValue); cgValue) {
        return cgValue;
    }
    if (chirValue->IsImportedFunc()) {
        auto importedFunc = DynamicCast<const CHIR::ImportedFunc*>(chirValue);
        return GetOrInsertCGFunction(importedFunc);
    } else if (chirValue->IsFuncWithBody()) {
        auto func = DynamicCast<const CHIR::FuncBase*>(chirValue);
        return GetOrInsertCGFunction(func);
    } else if (chirValue->IsImportedVar()) {
        auto importedVar = DynamicCast<const CHIR::ImportedVar*>(chirValue);
        return GetOrInsertGlobalVariable(importedVar);
    } else if (chirValue->IsGlobalVarInCurPackage()) {
        auto chirGV = DynamicCast<const CHIR::GlobalVarBase*>(chirValue);
        return GetOrInsertGlobalVariable(chirGV);
    } else {
        CJC_ASSERT(false && "Should not reach here: value not dominate all uses in CHIR.");
        return nullptr;
    }
}

llvm::Value* CGModule::GenerateUnitTypeValue()
{
    auto unitVal = module->getGlobalVariable(UNIT_VAL_STR);
    if (unitVal == nullptr) {
        llvm::Type* unitType = CGType::GetUnitCGType(*this)->GetLLVMType();
        auto gv = module->getOrInsertGlobal(UNIT_VAL_STR, unitType);
        unitVal = llvm::cast<llvm::GlobalVariable>(gv);
        CJC_NULLPTR_CHECK(unitVal);
        unitVal->addAttribute(CJGLOBAL_VALUE_ATTR);
        unitVal->setInitializer(llvm::UndefValue::get(unitType));
        AddLinkageTypeMetadata(*unitVal,
            cgCtx->GetCompileOptions().target.IsMinGW() ? llvm::GlobalValue::PrivateLinkage
                                                        : llvm::GlobalValue::WeakODRLinkage,
            false);
    }
    return unitVal;
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
llvm::Constant* CGModule::GetExceptionIntrinsicPersonality()
{
    auto funcName = "__cj_personality_v0$";
    // MachO does not support "too many personalities for compact unwind to decode". __cj_personality_v0$"
    // It is defined by runtime on macOS and declaration is all we need here.
    if (cgCtx->GetCompileOptions().target.IsMacOS()) {
        auto retTy = llvm::Type::getInt32Ty(module->getContext());
        return GetOrInsertFunction(funcName, llvm::FunctionType::get(retTy, true));
    }
    llvm::Function* func = module->getFunction(funcName);
    if (!func) {
        auto& llvmCtx = module->getContext();
        auto retTy = llvm::Type::getInt32Ty(llvmCtx);
        auto functionCallee = module->getOrInsertFunction(funcName, llvm::FunctionType::get(retTy, true));
        func = llvm::cast<llvm::Function>(functionCallee.getCallee());
        AddLinkageTypeMetadata(*func, llvm::GlobalValue::PrivateLinkage, false);
        auto entryBB = llvm::BasicBlock::Create(llvmCtx, "entry", func);
        IRBuilder2 irBuilder(*this, entryBB);
        (void)irBuilder.CreateRet(llvm::ConstantInt::get(retTy, 0));
    }
    return func;
}
#endif

void CGModule::Opt()
{
    for (auto& [function, cgFunction] : std::as_const(functions)) {
        if (function == nullptr || cgFunction->GetRawValue() == nullptr) {
            continue;
        }
        cgFunction->Opt();
    }
}

bool CGModule::Verify() const
{
    bool wellFormed = true;
    if (llvm::verifyModule(*module, &llvm::outs())) {
        wellFormed = false;
    }
    return wellFormed;
}

void CGModule::InitDebugInfo()
{
    module->addModuleFlag(
        llvm::Module::Warning, "Debug Info Version", static_cast<uint32_t>(llvm::DEBUG_METADATA_VERSION));
    module->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 4); // Use dwarf 4 format.
    diBuilder = std::make_unique<DIBuilder>(*this);
    diBuilder->CreateCompileUnit(module->getSourceFileName());      // Create artificial CompileUnit.
    diBuilder->CreateNameSpace(GetCGContext().GetCurrentPkgName()); // Create current package as namespace.
}

llvm::Constant* CGModule::GenerateTypeNameConstantString(const std::string& typeMangledName, bool isWeakODR) const
{
    auto linkageType =
        isWeakODR ? llvm::GlobalValue::LinkageTypes::WeakODRLinkage : llvm::GlobalValue::LinkageTypes::PrivateLinkage;
    auto strConstant = llvm::ConstantDataArray::getString(module->getContext(), typeMangledName);
    auto globalValue = module->getOrInsertGlobal(typeMangledName + ".name", strConstant->getType());
    auto klassName = llvm::cast<llvm::GlobalVariable>(globalValue);
    klassName->setInitializer(strConstant);
    klassName->setAlignment(llvm::Align(1));
    klassName->addAttribute(CJTYPE_NAME_ATTR);
    AddLinkageTypeMetadata(*klassName, linkageType, false);
    return llvm::ConstantExpr::getBitCast(klassName, llvm::Type::getInt8PtrTy(module->getContext()));
}

llvm::GlobalVariable* CGModule::GeneratePrivateUnnamedAddrConstant(
    const std::string& name, llvm::Constant* constVal) const
{
    auto type = constVal->getType();
    auto res = llvm::cast<llvm::GlobalVariable>(module->getOrInsertGlobal(name, type));
    res->setConstant(true);
    res->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    AddLinkageTypeMetadata(*res, llvm::GlobalValue::PrivateLinkage, false);
    auto& layOut = module->getDataLayout();
    auto align = llvm::MaybeAlign(layOut.getPrefTypeAlignment(type));
    res->setAlignment(align);
    res->setInitializer(constVal);
    return res;
}

llvm::GlobalVariable* CGModule::GetOrCreateGlobalVariable(
    llvm::Constant* constVal, const std::string& gvContent, bool isTuple)
{
    std::string name = isTuple ? GetConstantTupleName(gvContent) : GetConstantArrayName(gvContent);
    if (auto gv = module->getGlobalVariable(name, true); gv) {
        return gv;
    } else {
        return GeneratePrivateUnnamedAddrConstant(name, constVal);
    }
}

llvm::Function* CGModule::GetOrInsertFunction(const std::string& funcMangledName, llvm::FunctionType* funcType) const
{
    auto functionCallee = module->getOrInsertFunction(funcMangledName, funcType);
    auto callee = functionCallee.getCallee();
    CJC_ASSERT(llvm::isa<llvm::Function>(callee));
    return llvm::cast<llvm::Function>(callee);
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
std::string CGModule::GetEnumCaseClassName(const CHIR::EnumType& chirEnumType, std::size_t ctorIndex)
{
    auto enumDef = chirEnumType.GetEnumDef();
    auto name =
        enumDef->GetPackageName() + ':' + GetCustomTypeDefIdentifier(*enumDef) + ':' + std::to_string(ctorIndex);
    if (!CGType::GetOrCreate(*this, &chirEnumType)->IsDynamicGI() && !chirEnumType.GetTypeArgs().empty()) {
        name = name + '<' + GetTypeArgsQualifiedName(chirEnumType.GetTypeArgs()) + '>';
    }
    return name;
}

llvm::GlobalVariable* CGModule::GetOrCreateEnumCtorTIOrTT(const CHIR::EnumType& chirEnumType, std::size_t ctorIndex)
{
    auto isTT = CGType::GetOrCreate(*this, &chirEnumType)->IsDynamicGI();
    auto className = GetEnumCaseClassName(chirEnumType, ctorIndex) + (isTT ? ".tt" : ".ti");
    auto gvType = isTT ? CGType::GetOrCreateTypeTemplateType(cgCtx->GetLLVMContext())
                       : CGType::GetOrCreateTypeInfoType(cgCtx->GetLLVMContext());
    auto gvOfEnumCtorTIOrTT = GetLLVMModule()->getGlobalVariable(className, true);
    if (!gvOfEnumCtorTIOrTT) {
        gvOfEnumCtorTIOrTT = llvm::cast<llvm::GlobalVariable>(GetLLVMModule()->getOrInsertGlobal(className, gvType));
        EnumCtorTIOrTTGenerator(*this, chirEnumType, ctorIndex).Emit();
    }
    return gvOfEnumCtorTIOrTT;
}
#endif

std::vector<llvm::Constant*> CGModule::InitRawArrayUInt8Constants() const
{
    auto& ctx = GetLLVMContext();
    auto int32Ty = llvm::Type::getInt32Ty(ctx);
    auto int32PtrTy = llvm::Type::getInt32PtrTy(ctx);
    auto int8Ty = llvm::Type::getInt8Ty(ctx);
    auto int8PtrTy = llvm::Type::getInt8PtrTy(ctx);
    auto int16Ty = llvm::Type::getInt16Ty(ctx);

    auto typeTemplateType = CGType::GetOrCreateTypeTemplateType(ctx);
    auto typeInfoType = CGType::GetOrCreateTypeInfoType(ctx);
    auto ttName = "RawArray.tt";
    auto memberTiName = "UInt8.ti";
    // When --apc, in some subpackage, UInt8.ti has not been generated, so we add relatedType here.
    if (module->getGlobalVariable(memberTiName, true) == nullptr) {
        auto gvUInt8Ti = llvm::cast<llvm::GlobalVariable>(module->getOrInsertGlobal(memberTiName, typeInfoType));
        auto metaUInt8Ti = llvm::MDTuple::get(ctx, {llvm::MDString::get(ctx, "UInt8.Type")});
        gvUInt8Ti->setMetadata(GC_TYPE_META_NAME, metaUInt8Ti);
    }

    auto constantInt8Zero = llvm::ConstantInt::get(int8Ty, 0);
    auto constantInt16Zero = llvm::ConstantInt::get(int16Ty, 0);
    auto constantNullPtrInt8Ptr = llvm::ConstantPointerNull::get(int8PtrTy);
    std::vector<llvm::Constant*> typeInfoVec(TYPE_INFO_FIELDS_NUM);
    typeInfoVec[static_cast<size_t>(TYPEINFO_NAME)] = GenerateTypeNameConstantString("RawArray<UInt8>", false);
    typeInfoVec[static_cast<size_t>(TYPEINFO_TYPE_KIND)] =
        llvm::ConstantInt::get(int8Ty, static_cast<size_t>(UGTypeKind::UG_RAWARRAY));
    typeInfoVec[static_cast<size_t>(TYPEINFO_FIELDS_NUM)] = constantInt16Zero;
    typeInfoVec[static_cast<size_t>(TYPEINFO_FIELDS)] = constantNullPtrInt8Ptr;
    typeInfoVec[static_cast<size_t>(TYPEINFO_SIZE)] = llvm::ConstantInt::get(int32Ty, 0);
    typeInfoVec[static_cast<size_t>(TYPEINFO_UUID)] = llvm::Constant::getNullValue(int32Ty);
    typeInfoVec[static_cast<size_t>(TYPEINFO_ALIGN)] = constantInt8Zero;
    typeInfoVec[static_cast<size_t>(TYPEINFO_SOURCE_GENERIC)] =
        llvm::ConstantExpr::getBitCast(GetLLVMModule()->getOrInsertGlobal(ttName, typeTemplateType), int8PtrTy);
    typeInfoVec[static_cast<size_t>(TYPEINFO_TYPE_ARGS_NUM)] = constantInt8Zero;
    typeInfoVec[static_cast<size_t>(TYPEINFO_INHERITED_CLASS_NUM)] =
        llvm::ConstantInt::get(int16Ty, INHERITED_CLASS_NUM_FE_FLAG);
    typeInfoVec[static_cast<size_t>(TYPEINFO_OFFSETS)] = llvm::ConstantPointerNull::get(int32PtrTy);
    typeInfoVec[static_cast<size_t>(TYPEINFO_TYPE_ARGS)] = constantNullPtrInt8Ptr;
    typeInfoVec[static_cast<size_t>(TYPEINFO_SUPER)] =
        llvm::cast<llvm::GlobalVariable>(GetLLVMModule()->getOrInsertGlobal(memberTiName, typeInfoType));
    typeInfoVec[static_cast<size_t>(TYPEINFO_EXTENSIONDEF_PTR)] =
        llvm::ConstantPointerNull::get(CGType::GetOrCreateExtensionDefPtrType(ctx)->getPointerTo());
    typeInfoVec[static_cast<size_t>(TYPEINFO_MTABLE)] = constantNullPtrInt8Ptr;
    typeInfoVec[static_cast<size_t>(TYPEINFO_REFLECTION)] = constantNullPtrInt8Ptr;
    typeInfoVec[static_cast<size_t>(TYPEINFO_GC_TIB)] =
        llvm::ConstantPointerNull::get(CGType::GetBitMapType(ctx)->getPointerTo());
    typeInfoVec[static_cast<size_t>(TYPEINFO_FLAG)] = constantInt8Zero;

    return typeInfoVec;
}

bool CGModule::IsLinkNameUsedInMeta(const std::string& linkageName)
{
    if (linkNameUsedInMeta.empty()) {
        std::vector<std::string> metaTypes = {METADATA_PKG, METADATA_TYPES, METADATA_TYPETEMPLATES,
            METADATA_PRIMITIVE_TYPES, METADATA_PRIMITIVE_TYPETEMPLATES, METADATA_GLOBAL_VAR, METADATA_FUNCTIONS};
        std::for_each(metaTypes.begin(), metaTypes.end(), [this](auto& type) {
            CollectLinkNameUsedInMeta(this->module->getNamedMetadata(type), this->linkNameUsedInMeta);
        });
    }
    return linkNameUsedInMeta.find(linkageName) != linkNameUsedInMeta.end();
}

const std::vector<std::unique_ptr<CGExtensionDef>>& CGModule::GetAllCGExtensionDefs()
{
    auto& subCHIRPkg = GetCGContext().GetSubCHIRPackage();
    std::unordered_set<CHIR::CustomTypeDef*> traversedCustomTypeDefs;

    for (auto customDef : subCHIRPkg.chirCustomDefs) {
        if (!traversedCustomTypeDefs.emplace(customDef).second) {
            // It indicates this customDef has been traversed.
            CJC_ASSERT(false && "Duplicated customDef provided by CHIR.");
            continue;
        }
        switch (customDef->GetCustomKind()) {
            case CHIR::CustomDefKind::TYPE_CLASS: {
                // Core.Object is a special case, and doesn't need to generate extensionDef for it.
                auto classDef = dynamic_cast<CHIR::ClassDef*>(customDef);
                if (!IsCoreObject(*classDef) && !IsClosureConversionEnvClass(*classDef)) {
                    (void)cgExtensionDefs.emplace_back(std::make_unique<CGExtensionDef>(*this, *customDef));
                }
                break;
            }
            case CHIR::CustomDefKind::TYPE_ENUM:
            case CHIR::CustomDefKind::TYPE_STRUCT: {
                (void)cgExtensionDefs.emplace_back(std::make_unique<CGExtensionDef>(*this, *customDef));
                break;
            }
            case CHIR::CustomDefKind::TYPE_EXTEND: {
                auto extendedType = dynamic_cast<CHIR::ExtendDef*>(customDef)->GetExtendedType();
                // No need to collect extensions for nominals, since we will traverse
                // these extensions while handling Class/Enum/Struct definitions.
                if (!extendedType->IsNominal() ||
                    dynamic_cast<CHIR::CustomType*>(extendedType)->GetCustomTypeDef()
                        ->TestAttr(CHIR::Attribute::IMPORTED)) {
                    (void)cgExtensionDefs.emplace_back(std::make_unique<CGExtensionDef>(*this, *customDef));
                }
                break;
            }
            default:
                CJC_ASSERT(false && "Should not reach here.");
                return cgExtensionDefs;
        }
    }
    return cgExtensionDefs;
}
} // namespace Cangjie::CodeGen
