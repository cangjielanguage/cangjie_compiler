// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CodeGen/EmitPackageIR.h"

#include "llvm/IR/Verifier.h"

#include "Base/CGTypes/CGEnumType.h"
#include "Base/CGTypes/CGPrimitiveType.h"
#include "CGModule.h"
#include "CJNative/CGTypes/CGExtensionDef.h"
#include "CJNative/CGTypes/EnumCtorTIOrTTGenerator.h"
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
#include "CJNative/CHIRSplitter.h"
#include "CJNative/CJNativeReflectionInfo.h"
#endif
#include "DIBuilder.h"
#include "EmitFunctionIR.h"
#include "EmitGlobalVariableIR.h"
#include "IRBuilder.h"
#include "IRGenerator.h"
#include "Utils/CGCommonDef.h"
#include "Utils/CGUtils.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/CHIR/Value.h"
#include "cangjie/Frontend/CompilerInstance.h"
#include "cangjie/FrontendTool/IncrementalCompilerInstance.h"
#include "cangjie/Utils/ProfileRecorder.h"
#include "cangjie/Utils/TaskQueue.h"

namespace Cangjie::CodeGen {
void EmitMain(CGModule& cgMod);
void CreatePackageInitResetFunction(CGModule& cgMod);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
void RegisterExceptionRaiser(CGModule& cgMod);
void InitializeCjStringLiteral(const CGModule& cgMod);
void GenerateBinarySectionInfo(const CGModule& cgMod);
void CreateLLVMUsedGVs(const CGModule& cgMod);
void ReplaceFunction(CGModule& cgMod);
void InlineFunction(CGModule& cgMod);
#endif

namespace {
#ifdef CJ_SDK_VERSION
const std::string CANGJIE_SDK_VERSION = CJ_SDK_VERSION;
#else
const std::string CANGJIE_SDK_VERSION = "";
#endif

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
// Encapsulate repeated IR dump logic used in GenSubCHIRPackage
static inline void DumpIRIfNeeded(const CGModule& cgMod, const std::string& suffix, size_t& subDirNum)
{
    const auto& cgPkgCtx = cgMod.GetCGContext().GetCGPkgContext();
    const auto& options = cgPkgCtx.GetGlobalOptions();
    if (!options.NeedDumpIRToFile()) {
        return;
    }
    const auto& dumpPath = GenDumpPath(options.output, cgPkgCtx.GetCurrentPkgName(),
        std::to_string(subDirNum) + "_" + suffix, cgMod.GetLLVMModule()->getSourceFileName());
    DumpIR(*cgMod.GetLLVMModule(), dumpPath);
    subDirNum++;
}

void GenerateExtensionDefs(CGModule& cgMod)
{
    for (auto& def : cgMod.GetAllCGExtensionDefs()) {
        def->Emit();
        // NOTE: Class with 'Object' parent or extend with non-inherited will not generate extensionDef.
    }

    std::vector<llvm::Constant*> content;
    for (auto def : cgMod.GetNonExternalExtensionDefs()) {
        (void)content.emplace_back(def);
    }

    if (!content.empty()) {
        auto extensionDefPrtTy = CGType::GetOrCreateExtensionDefPtrType(cgMod.GetLLVMContext());
        content.emplace_back(llvm::ConstantPointerNull::get(extensionDefPrtTy)); // indicates the end of array
        auto extensionDefsArrayTy = llvm::ArrayType::get(extensionDefPrtTy, content.size());
        auto allDefsVar = llvm::cast<llvm::GlobalVariable>(
            cgMod.GetLLVMModule()->getOrInsertGlobal("NonExternalExtensionDefs", extensionDefsArrayTy));
        allDefsVar->setInitializer(llvm::ConstantArray::get(extensionDefsArrayTy, content));
        allDefsVar->setLinkage(llvm::GlobalValue::PrivateLinkage);
        allDefsVar->addAttribute("InnerTypeExtensions");
        cgMod.GetCGContext().AddLLVMUsedVars(allDefsVar->getName().str());
    }

    std::vector<llvm::Constant*> content2;
    for (auto def : cgMod.GetExternalExtensionDefs()) {
        (void)content2.emplace_back(def);
    }

    if (!content2.empty()) {
        auto extensionDefPrtTy = CGType::GetOrCreateExtensionDefPtrType(cgMod.GetLLVMContext());
        auto extensionDefsArrayTy = llvm::ArrayType::get(extensionDefPrtTy, content2.size());
        auto allDefsVar = llvm::cast<llvm::GlobalVariable>(
            cgMod.GetLLVMModule()->getOrInsertGlobal("ExternalExtensionDefs", extensionDefsArrayTy));
        allDefsVar->setInitializer(llvm::ConstantArray::get(extensionDefsArrayTy, content2));
        allDefsVar->setLinkage(llvm::GlobalValue::PrivateLinkage);
        allDefsVar->addAttribute("OuterTypeExtensions");
        cgMod.GetCGContext().AddLLVMUsedVars(allDefsVar->getName().str());
    }
}

std::vector<llvm::Constant*> TopologicalSortStaticGIs(CGContext& cgCtx, std::vector<llvm::Constant*> content)
{
    auto indegree = cgCtx.GetIndegreeOfTypes();
    auto& partialOrder = cgCtx.GetDependentPartialOrderOfTypes();
    std::queue<llvm::Constant*> q;
    std::vector<llvm::Constant*> res;
    for (auto c : content) {
        if (auto it = indegree.find(c); it == indegree.end() || it->second == 0) {
            q.push(c);
        }
    }
    while (!q.empty()) {
        llvm::Constant* u = q.front();
        q.pop();
        res.emplace_back(u);
        for (auto c : content) {
            if (auto it = partialOrder.find(CGContext::PartialOrderPair{u, c}); it != partialOrder.end()) {
                if (auto& i = indegree.at(c); --i == 0) {
                    q.push(c);
                }
            }
        }
    }
    CJC_ASSERT(content.size() == res.size());
    return res;
}

void GenerateStaticGIs(CGModule& cgMod)
{
    std::vector<llvm::Constant*> content;
    for (auto staticGI : cgMod.GetAliveStaticGIs()) {
        if (staticGI->getNumUses() > 0 || !staticGI->isLocalLinkage(staticGI->getLinkage())) {
            CJC_ASSERT(staticGI->hasInitializer());
            (void)content.emplace_back(staticGI);
        }
    }

    for (auto& staticGIName : cgMod.GetCGContext().GetReflectGeneratedStaticGINames()) {
        if (auto staticGI = cgMod.GetLLVMModule()->getNamedGlobal(staticGIName)) {
            CJC_ASSERT(staticGI->hasInitializer());
            if (find(content.begin(), content.end(), staticGI) == content.end()) {
                (void)content.emplace_back(staticGI);
            }
        }
    }

    if (content.empty()) {
        return;
    }

    content = TopologicalSortStaticGIs(cgMod.GetCGContext(), content);
    auto tiPtrType = CGType::GetOrCreateTypeInfoPtrType(cgMod.GetLLVMContext());
    auto staticGIArrayType = llvm::ArrayType::get(tiPtrType, content.size());
    auto staticGIs = llvm::cast<llvm::GlobalVariable>(
        cgMod.GetLLVMModule()->getOrInsertGlobal("StaticGenericTIs", staticGIArrayType));

    staticGIs->setInitializer(llvm::ConstantArray::get(staticGIArrayType, content));
    staticGIs->setLinkage(llvm::GlobalValue::PrivateLinkage);
    staticGIs->addAttribute("CFileStaticGenericTI");
    cgMod.GetCGContext().AddLLVMUsedVars(staticGIs->getName().str());
}
#endif

void KeepSomeTypesManually(CGModule& cgMod)
{
    // 1) For BaseGen, if a signature of a type has only been used in metadata,
    // 2) For LLVM IR, if an `ObjLayout.xxx` has only been used in metadata,
    // ensure that the type will not be automatically cleared by LLVM. This method
    // is to build an internal function and apply for this type of memory in the
    // unreachable block of the function. NOTE that this function will never be
    // carried out.
    auto& cgCtx = cgMod.GetCGContext();
    auto& llvmCtx = cgCtx.GetLLVMContext();
    auto module = cgMod.GetLLVMModule();

    auto functionType = llvm::FunctionType::get(llvm::Type::getVoidTy(llvmCtx), {}, false);
    auto function = llvm::cast<llvm::Function>(
        module->getOrInsertFunction(FOR_KEEPING_SOME_TYPES_FUNC_NAME, functionType).getCallee());
    function->addFnAttr(llvm::Attribute::NoInline);
    function->addFnAttr(llvm::Attribute::OptimizeNone);
    AddLinkageTypeMetadata(*function, llvm::GlobalValue::PrivateLinkage, false);
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    cgCtx.AddLLVMUsedVars(function->getName().str());
#endif
    auto entryBB = llvm::BasicBlock::Create(llvmCtx, "entry.unreachable", function);
    IRBuilder2 irBuilder(cgMod, entryBB);

    for (auto& structTypeName : cgCtx.GetGeneratedStructType()) {
        auto structType = llvm::StructType::getTypeByName(cgMod.GetLLVMContext(), structTypeName);
        if (structType && structType->isSized()) {
            (void)StaticCast<LLVMIRBuilder2*>(&irBuilder)->CreateAlloca(structType);
        }
    }
    (void)irBuilder.CreateRetVoid();
}

void RecordCodeInfoInCodeGen(const std::string& suffix, const CGModule& cgMod)
{
    auto& options = cgMod.GetCGContext().GetCompileOptions();
    if (!options.enableTimer && !options.enableMemoryCollect) {
        return;
    }

    size_t allInstantiated = 0;
    size_t curPkgInstantiated = 0;
    size_t importedPkgInstantiated = 0;
    size_t funcNum = 0;
    for (auto& function : cgMod.GetLLVMModule()->functions()) {
        if (function.isDeclaration()) {
            continue;
        }
        funcNum++;
        auto count = function.getInstructionCount();
        if (function.hasFnAttribute(GENERIC_DECL_IN_IMPORTED_PKG_ATTR)) {
            importedPkgInstantiated += count;
            allInstantiated += count;
        } else if (function.hasFnAttribute(GENERIC_DECL_IN_CURRENT_PKG_ATTR)) {
            curPkgInstantiated += count;
            allInstantiated += count;
        }
    }
    std::string suffixWithModuleName = suffix + "(" + cgMod.GetLLVMModule()->getSourceFileName() + ")";
    static std::mutex profileMutex;
    std::lock_guard g{profileMutex};
    Utils::ProfileRecorder::RecordCodeInfo(
        "all generic ins llvm ir after " + suffixWithModuleName, static_cast<int64_t>(allInstantiated));
    Utils::ProfileRecorder::RecordCodeInfo(
        "imported generic ins llvm ir after " + suffixWithModuleName, static_cast<int64_t>(importedPkgInstantiated));
    Utils::ProfileRecorder::RecordCodeInfo(
        "cur pkg generic ins llvm ir after " + suffixWithModuleName, static_cast<int64_t>(curPkgInstantiated));
    Utils::ProfileRecorder::RecordCodeInfo(
        "llvm ir after " + suffixWithModuleName, cgMod.GetLLVMModule()->getInstructionCount());
    Utils::ProfileRecorder::RecordCodeInfo(
        "llvm global func after " + suffixWithModuleName, static_cast<int64_t>(funcNum));
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
void TransformFFIs(const CGModule& cgMod)
{
    std::vector<llvm::Function*> cFuncs;
    for (auto& func : cgMod.GetLLVMModule()->getFunctionList()) {
        if (func.hasFnAttribute(CodeGen::CFUNC_ATTR)) {
            func.removeFnAttr(CodeGen::CFUNC_ATTR);
            cFuncs.emplace_back(&func);
        }
    }
}

void SpecifyPackageInitFunc(const CGModule& cgMod)
{
    auto& llvmCtx = cgMod.GetLLVMContext();
    auto pkgInitFuncName = cgMod.GetCGContext().GetCHIRPackage().GetPackageInitFunc()->GetIdentifierWithoutPrefix();
    cgMod.GetLLVMModule()
        ->getOrInsertNamedMetadata("pkg_init_func")
        ->addOperand(llvm::MDTuple::get(llvmCtx, llvm::MDString::get(llvmCtx, pkgInitFuncName)));
    cgMod.GetCGContext().AddLLVMUsedVars(pkgInitFuncName);
}

void SetSymbolLinkageType(const CGModule& cgMod)
{
    auto llvmModule = cgMod.GetLLVMModule();
    for (auto& globalObject : llvmModule->global_objects()) {
        if (!globalObject.hasMetadata("LinkageType")) {
            continue;
        }

        auto linkageType = GetLinkageTypeOfGlobalObject(globalObject);
        // 1. Clear LinkageType metadata
        globalObject.setMetadata("LinkageType", nullptr);
        if (globalObject.isDeclaration()) {
            continue;
        }
        // 2. Set linkageType
        if (llvm::GlobalValue::isWeakODRLinkage(linkageType) || llvm::GlobalValue::isLinkOnceLinkage(linkageType)) {
            globalObject.setLinkage(linkageType);
        } else if (llvm::GlobalValue::isLocalLinkage(linkageType)) {
            cgMod.GetCGContext().AddLocalizedSymbol(globalObject.getName().str());
        }
    }
}

/*
 * @brief generate TypeInfo or TypeTemplate for every CustomDef in sub package.
 */
void EmitTIOrTTForCustomDefs(CGModule& cgMod)
{
    auto& subCHIRPkg = cgMod.GetCGContext().GetSubCHIRPackage();
    for (auto customDef : subCHIRPkg.chirCustomDefs) {
        if (!customDef->IsExtend()) {
            auto cgType = CGType::GetOrCreate(cgMod, customDef->GetType());
            if (cgType->IsConcrete()) {
                cgType->GetOrCreateTypeInfo();
            }
            if (customDef->IsGenericDef()) {
                cgType->GetOrCreateTypeTemplate();
            }
        }
        if (customDef->TestAttr(CHIR::Attribute::IMPORTED) || customDef->TestAttr(CHIR::Attribute::NON_RECOMPILE)) {
            continue;
        }
        switch (customDef->GetCustomKind()) {
            case CHIR::CustomDefKind::TYPE_ENUM: {
                auto chirEnumType = StaticCast<CHIR::EnumDef*>(customDef)->GetType();
                auto cgEnumType = StaticCast<CGEnumType*>(CGType::GetOrCreate(cgMod, chirEnumType));
                if (!cgEnumType->IsTrivial() && !cgEnumType->IsZeroSizeEnum()) {
                    const auto& ctors = chirEnumType->GetConstructorInfos(cgMod.GetCGContext().GetCHIRBuilder());
                    for (auto ctorIndex = 0U; ctorIndex < ctors.size(); ++ctorIndex) {
                        EnumCtorTIOrTTGenerator(cgMod, *chirEnumType, ctorIndex).Emit();
                    }
                }
                break;
            }
            case CHIR::CustomDefKind::TYPE_CLASS:
            case CHIR::CustomDefKind::TYPE_STRUCT:
            case CHIR::CustomDefKind::TYPE_EXTEND:
                break;
            default:
                CJC_ASSERT(false && "Should not reach here.");
                return;
        }
    }
}

void EmitCJSDKVersion(const CGModule& cgMod)
{
    auto& llvmCtx = cgMod.GetLLVMContext();
    auto linkageType = llvm::GlobalValue::LinkageTypes::PrivateLinkage;
    auto strConstant = llvm::ConstantDataArray::getString(llvmCtx, CANGJIE_SDK_VERSION);
    auto cjSdkVersion = llvm::cast<llvm::GlobalVariable>(
        cgMod.GetLLVMModule()->getOrInsertGlobal("cj.sdk.version", strConstant->getType()));
    cjSdkVersion->setInitializer(strConstant);
    cjSdkVersion->setAlignment(llvm::Align(1));
    cjSdkVersion->setLinkage(linkageType);
    cgMod.GetCGContext().AddLLVMUsedVars(cjSdkVersion->getName().str());
}

void GenSubCHIRPackage(CGModule& cgMod)
{
    auto& subCHIRPkg = cgMod.GetCGContext().GetSubCHIRPackage();
    EmitTIOrTTForCustomDefs(cgMod);
    EmitGlobalVariableIR(cgMod, std::vector<CHIR::GlobalVar*>(subCHIRPkg.chirGVs.begin(), subCHIRPkg.chirGVs.end()));
    EmitFunctionIR(cgMod, std::vector<CHIR::Func*>(subCHIRPkg.chirFuncs.begin(), subCHIRPkg.chirFuncs.end()));
    EmitImportedCFuncIR(cgMod,
        std::vector<CHIR::ImportedFunc*>(subCHIRPkg.chirImportedCFuncs.begin(), subCHIRPkg.chirImportedCFuncs.end()));
    if (subCHIRPkg.mainModule) {
        EmitCJSDKVersion(cgMod);
        EmitMain(cgMod);
        RegisterExceptionRaiser(cgMod);
        CreatePackageInitResetFunction(cgMod);
        SpecifyPackageInitFunc(cgMod);
    }
    size_t subDirNum = 0; // for sub-directory naming, starts from 0, increments after each dump.
    DumpIRIfNeeded(cgMod, "TranslateCHIRNode", subDirNum);
    GenerateExtensionDefs(cgMod);
    DumpIRIfNeeded(cgMod, "GenExtensionDefs", subDirNum);
    cgMod.GenTypeTemplate();
    DumpIRIfNeeded(cgMod, "GenTypeTemplates", subDirNum);
    cgMod.GenTypeInfo();
    DumpIRIfNeeded(cgMod, "GenTypeInfos", subDirNum);
    ReplaceFunction(cgMod);
    cgMod.Opt();
    InlineFunction(cgMod);
    DumpIRIfNeeded(cgMod, "ReplaceAndInlineFunc", subDirNum);
    CJNativeReflectionInfo(cgMod, subCHIRPkg).Gen();
    cgMod.GenTypeInfo(); // for reflect generated typeinfo
    DumpIRIfNeeded(cgMod, "GenReflectionInfo", subDirNum);
    cgMod.diBuilder->Finalize();
    TransformFFIs(cgMod);
    DumpIRIfNeeded(cgMod, "TransformFFIs", subDirNum);
    InitializeCjStringLiteral(cgMod);
    DumpIRIfNeeded(cgMod, "InitCJStringLiteral", subDirNum);
    GenerateBinarySectionInfo(cgMod);
    KeepSomeTypesManually(cgMod);
    DumpIRIfNeeded(cgMod, "GenBinarySectionInfo", subDirNum);
    cgMod.EraseUselessInstsAndDeclarations();
    DumpIRIfNeeded(cgMod, "EraseUselessInstsAndDecls", subDirNum);
    RecordCodeInfoInCodeGen("CodeGen EmitIR", cgMod);
    cgMod.GenIncremental();
    SetSymbolLinkageType(cgMod);
    DumpIRIfNeeded(cgMod, "GenIncremental", subDirNum);
    cgMod.EraseUselessGVAndFunctions();
    DumpIRIfNeeded(cgMod, "EraseUselessGVAndFuncs", subDirNum);
    GenerateStaticGIs(cgMod);
    DumpIRIfNeeded(cgMod, "GenStaticGIs", subDirNum);
    CreateLLVMUsedGVs(cgMod);
    // Incremental compilation generates invalid IRs, which need to be deleted by EraseUselessGVAndFunctions.
    // Therefore, the verification is performed after incremental compilation.
    auto success = cgMod.Verify();
#ifndef NDEBUG
    if (!success) {
        auto& cgPkgCtx = cgMod.GetCGContext().GetCGPkgContext();
        std::string path = cgPkgCtx.GetCurrentPkgName() + "/brokenIR/" + cgMod.GetLLVMModule()->getSourceFileName();
        DumpIR(*cgMod.GetLLVMModule(), path);
        InternalError("Broken llvm ir! The result is saved in " + path);
    }
#endif
    DumpIRIfNeeded(cgMod, "Final", subDirNum);
    if (!success) {
        InternalError("Broken llvm ir!");
    }
    RecordCodeInfoInCodeGen("CodeGen stage", cgMod);
    }
#endif
} // namespace

class PackageGeneratorImpl : public IRGeneratorImpl {
public:
    PackageGeneratorImpl(CHIR::CHIRBuilder& chirBuilder, const CHIRData& chirData, const GlobalOptions& options,
        bool enableIncrement, const CachedMangleMap& cachedMangleMap)
        : cgPkgCtx(chirBuilder, chirData, options, enableIncrement, cachedMangleMap)
    {
    }

    void EmitIR() override;

    std::vector<std::unique_ptr<llvm::Module>> ReleaseLLVMModules()
    {
        return cgPkgCtx.ReleaseLLVMModules();
    }

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
private:
    bool InitIncrementalGen()
    {
        bool ret = true;
        if (!cgPkgCtx.IsIncrementEnabled()) {
            return ret;
        }

        Utils::ProfileRecorder::Start("EmitIR", "InitIncrementalGen");
        auto& cgMods = cgPkgCtx.GetCGModules();
        size_t threadNum = cgPkgCtx.GetGlobalOptions().codegenDebugMode ? 1 : cgMods.size();
        if (threadNum == 1) {
            for (auto& cgMod : cgMods) {
                ret = ret && cgMod->InitIncrementalGen();
            }
        } else {
            Utils::TaskQueue taskQueueInitIncrementalGen(threadNum);
            std::vector<Cangjie::Utils::TaskResult<bool>> results;
            for (auto& cgMod : cgMods) {
                results.emplace_back(
                    taskQueueInitIncrementalGen.AddTask<bool>([&cgMod]() { return cgMod->InitIncrementalGen(); }));
            }
            taskQueueInitIncrementalGen.RunAndWaitForAllTasksCompleted();
            for (auto& result : results) {
                ret = ret && result.get();
            }
        }
        Utils::ProfileRecorder::Start("EmitIR", "InitIncrementalGen");
        return ret;
    }

    void GenSubCHIRPackages()
    {
        Utils::ProfileRecorder::Start("EmitIR", "GenSubCHIRPackages");
        auto& cgMods = cgPkgCtx.GetCGModules();
        auto& globalOptions = cgPkgCtx.GetGlobalOptions();
        if (globalOptions.NeedDumpIRToFile()) {
            ClearOldIRDumpFiles(globalOptions.output, cgPkgCtx.GetCurrentPkgName());
        }
        size_t threadNum = cgPkgCtx.GetGlobalOptions().codegenDebugMode ? 1 : cgMods.size();
        if (threadNum == 1) {
            for (auto& cgMod : cgMods) {
                GenSubCHIRPackage(*cgMod);
            }
        } else {
            Utils::TaskQueue taskQueueCHIRIR2LLVMIR(threadNum);
            for (auto& cgMod : cgMods) {
                taskQueueCHIRIR2LLVMIR.AddTask<void>([&cgMod]() { GenSubCHIRPackage(*cgMod); });
            }
            taskQueueCHIRIR2LLVMIR.RunAndWaitForAllTasksCompleted();
        }
        if (cgPkgCtx.GetGlobalOptions().NeedDumpIRToScreen()) {
            for (auto& cgMod : cgPkgCtx.GetCGModules()) {
                DumpIR(*cgMod->GetLLVMModule());
            }
        }
        Utils::ProfileRecorder::Stop("EmitIR", "GenSubCHIRPackages");
    }
#endif

private:
    CGPkgContext cgPkgCtx;
};


// Limit values[0] from values[1] to values[2], values' ty must be Integer.
// The basic block is automatically inserted after the current insert block.
llvm::Value* SaturatingIntegerValue(IRBuilder2& irBuilder, const std::vector<llvm::Value*>& values, llvm::Type* type)
{
    CJC_ASSERT(values.size() == 3); // 3 elements: src, min, max
    CJC_ASSERT(type->isIntegerTy());

    auto [minBb, maxBb, checkOverBb] =
        Vec2Tuple<3>(irBuilder.CreateAndInsertBasicBlocks({"min.bb", "max.bb", "check.over"}));

    const int srcIndex = 0;
    const int minIndex = 1;
    const int maxIndex = 2;
    const int branchNum = 3;
    auto previousBB = irBuilder.GetInsertBlock();
    auto downOverflow = irBuilder.CreateICmpSLT(values[srcIndex], values[minIndex]);
    auto upOverflow = irBuilder.CreateICmpSGT(values[srcIndex], values[maxIndex]);
    auto overflow = irBuilder.CreateOr(downOverflow, upOverflow);
    (void)irBuilder.CreateCondBr(overflow, minBb, checkOverBb);
    irBuilder.SetInsertPoint(minBb);
    (void)irBuilder.CreateCondBr(downOverflow, checkOverBb, maxBb);
    irBuilder.SetInsertPoint(maxBb);
    (void)irBuilder.CreateBr(checkOverBb);
    irBuilder.SetInsertPoint(checkOverBb);
    auto result = irBuilder.CreatePHI(type, branchNum, "result"); // 3 branches for phi.
    result->addIncoming(values[srcIndex], previousBB);
    result->addIncoming(values[minIndex], minBb);
    result->addIncoming(values[maxIndex], maxBb);
    return result;
}

llvm::Value* GenerateMainRetVal(IRBuilder2& irBuilder, llvm::Value* userMainRetVal)
{
    auto i32Ty = llvm::Type::getInt32Ty(irBuilder.GetLLVMContext());
    if (userMainRetVal->getType()->isVoidTy()) {
        return llvm::ConstantInt::get(i32Ty, 0);
    } else {
        auto i64Ty = llvm::Type::getInt64Ty(irBuilder.GetLLVMContext());
        auto minValue = irBuilder.getInt64(static_cast<uint64_t>(std::numeric_limits<int32_t>::min()));
        auto maxValue = irBuilder.getInt64(static_cast<uint64_t>(std::numeric_limits<int32_t>::max()));
        auto result = SaturatingIntegerValue(irBuilder, {userMainRetVal, minValue, maxValue}, i64Ty);
        return irBuilder.CreateTrunc(result, i32Ty);
    }
}

llvm::Function* CreateMainFunc(const CGModule& cgMod)
{
    auto& context = cgMod.GetCGContext().GetLLVMContext();
    auto i32Ty = llvm::Type::getInt32Ty(context);
    auto i8PtrPtrTy = llvm::Type::getInt8PtrTy(context)->getPointerTo();
    // Create @main func.
    auto module = cgMod.GetLLVMModule();
    auto mainFunc = module->getFunction("main");
    CJC_ASSERT(!mainFunc && "The main function is generated repeatedly.");
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    auto voidType = llvm::Type::getVoidTy(context);
    auto mainFuncType = llvm::FunctionType::get(voidType, {i32Ty, i8PtrPtrTy}, false);
    mainFunc = llvm::cast<llvm::Function>(module->getOrInsertFunction("main", mainFuncType).getCallee());
#endif
    NameFunctionParam(mainFunc, {"argc", "argv"});
    return mainFunc;
}

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
void PackageGeneratorImpl::EmitIR()
{
    Utils::ProfileRecorder recorder("CodeGen", "EmitIR");

    // Splitting CHIRPackage into n subCHIRPackages and Construct cgMods.
    CHIRSplitter chirSplitter(cgPkgCtx);
    auto subCHIRPkgs = chirSplitter.SplitCHIRPackage();
    for (auto& subCHIRPkg : subCHIRPkgs) {
        auto cgMod = std::make_unique<CGModule>(subCHIRPkg, cgPkgCtx);
        cgPkgCtx.AddCGModule(cgMod);
    }

    // Reads the buffered bitcode.
    if (!InitIncrementalGen()) {
        return;
    }

    // Translate CHIR to LLVM IR
    GenSubCHIRPackages();

    auto localizedSymbols = cgPkgCtx.GetLocalizedSymbols();
    const_cast<GlobalOptions&>(cgPkgCtx.GetGlobalOptions()).symbolsNeedLocalized =
        std::vector<std::string>(localizedSymbols.begin(), localizedSymbols.end());

    cgPkgCtx.Clear();
}
#endif

#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
std::vector<std::unique_ptr<llvm::Module>> GenPackageModules(CHIR::CHIRBuilder& chirBuilder, const CHIRData& chirData,
    const GlobalOptions& options, DefaultCompilerInstance& compilerInstance, bool enableIncrement)
{
    CachedMangleMap cachedMangleMap;
    if (enableIncrement) {
        cachedMangleMap = StaticCast<Cangjie::IncrementalCompilerInstance&>(compilerInstance).cacheMangles;
    }
    auto temp = PackageGeneratorImpl(chirBuilder, chirData, options, enableIncrement, cachedMangleMap);
    temp.EmitIR();
    return temp.ReleaseLLVMModules();
}
#endif

bool SavePackageModule(const llvm::Module& module, const std::string& bcFilePath)
{
    return SaveToBitcodeFile(module, bcFilePath);
}

void ClearPackageModules(std::vector<std::unique_ptr<llvm::Module>>& packageModules)
{
    std::unordered_set<llvm::LLVMContext*> contexts;
    std::for_each(packageModules.begin(), packageModules.end(), [&contexts](std::unique_ptr<llvm::Module>& module) {
        if (module) {
            contexts.insert(&module->getContext());
        }
    });
    // Release module first, it will automatically unregistered from context. Then can safely release llvm context.
    packageModules.clear();
    std::for_each(contexts.begin(), contexts.end(), [](llvm::LLVMContext* llvmCtx) {
        CJC_NULLPTR_CHECK(llvmCtx);
        delete llvmCtx;
    });
}
} // namespace Cangjie::CodeGen
