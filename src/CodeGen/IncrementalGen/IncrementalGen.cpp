// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file introduces methods to support incremental compilation in CodeGen.
 *
 */

#include "IncrementalGen/IncrementalGen.h"

#include <queue>
#include <unordered_set>

#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "Utils/CGCommonDef.h"
#include "Utils/Constants.h"
#include "cangjie/Utils/FileUtil.h"
#ifdef _WIN32
#include "cangjie/Basic/StringConvertor.h"
#endif

using namespace Cangjie;
using namespace Cangjie::CodeGen;

namespace {
const size_t SMALL_VECTOR_SIZE = 8;

// The bodies of functions that used to initialize global variables or
// to keep some type declarations should not be directly replaced as
// a whole, but should be replaced from the basic block level.
inline bool AllowFunctionBodyBeReplaced(const llvm::Function& func)
{
    auto funcName = func.getName();
    return !funcName.equals(FOR_KEEPING_SOME_TYPES_FUNC_NAME);
}

inline bool IsFileInitFuncName(const std::string& funcName)
{
    return funcName.find(FILE_GV_INIT_PREFIX) == 0;
}

llvm::GlobalVariable* CopyGlobalVariableTo(
    llvm::Module& destModule, const std::string& name, const llvm::GlobalVariable& gv)
{
    auto newGV = llvm::cast<llvm::GlobalVariable>(destModule.getOrInsertGlobal(name, gv.getValueType()));
    // All the accompanying information of new global variable needs to be consistent with the old.
    newGV->setLinkage(gv.getLinkage());
    newGV->setAlignment(gv.getAlign());
    newGV->setUnnamedAddr(gv.getUnnamedAddr());
    newGV->setConstant(gv.isConstant());
    newGV->setVisibility(gv.getVisibility());
    newGV->setAttributes(gv.getAttributes());
    newGV->setThreadLocalMode(gv.getThreadLocalMode());
    newGV->copyAttributesFrom(&gv);
    return newGV;
}

void UpdateCompileUnitWith(const llvm::Function& function, llvm::DICompileUnit& newCU)
{
    if (auto oriSP = function.getSubprogram(); oriSP) {
        auto sp = function.getSubprogram();
        sp->replaceUnit(&newCU);
    }
}

void DeleteFuncBody(llvm::Function* func)
{
    CJC_NULLPTR_CHECK(func);
    for (llvm::BasicBlock& bb : *func) {
        bb.dropAllReferences();
    }
    while (func->begin() != func->end()) {
        (void)func->begin()->eraseFromParent();
    }
    func->clearMetadata();
}

bool IsGVForExtensionDefs(const llvm::GlobalVariable& gv)
{
    return gv.hasAttribute("InnerTypeExtensions") || gv.hasAttribute("OuterTypeExtensions");
}
} // namespace

IncrementalGen::IncrementalGen(bool cgParallelEnabled) : cgParallelEnabled(cgParallelEnabled)
{
}

bool IncrementalGen::Init(const std::string& cachedIRPath, llvm::LLVMContext& llvmContext)
{
    if (FileUtil::FileExist(cachedIRPath)) {
        llvm::SMDiagnostic err;
#ifdef _WIN32
        std::optional<std::string> tempPath = StringConvertor::NormalizeStringToUTF8(cachedIRPath);
        CJC_ASSERT(tempPath.has_value() && "Incorrect file name encoding.");
        injectedModule = llvm::parseIRFile(tempPath.value(), err, llvmContext);
#else
        injectedModule = llvm::parseIRFile(cachedIRPath, err, llvmContext);
#endif
        if (injectedModule == nullptr) {
            Errorln("Illegal bitcode cache detected during incremental compilation.");
            return false;
        }
        return true;
    } else {
        Errorln("the cached bitcode file is lost.");
        return false;
    }
}

void IncrementalGen::InitCodeGenAddedCachedMap()
{
    auto mds = injectedModule->getNamedMetadata("CodeGenAddedForIncr");
    for (size_t i = 0; i < mds->getNumOperands(); i++) {
        auto md = mds->getOperand(i);
        auto key = llvm::dyn_cast_or_null<llvm::MDString>(md->getOperand(0))->getString().str();
        for (size_t j = 1; j < md->getNumOperands(); j++) {
            codegenAddedCachedMap[key].insert(
                llvm::dyn_cast_or_null<llvm::MDString>(md->getOperand(j))->getString().str());
        }
    }
}

std::vector<std::string> IncrementalGen::GetIncrLLVMUsedNames()
{
    return llvmUsedGVNames;
}

std::vector<std::string> IncrementalGen::GetIncrCachedStaticGINames()
{
    return staticGINames;
}

llvm::Module* IncrementalGen::LinkModules(llvm::Module* incremental, const CachedMangleMap& cachedMangles)
{
    CJC_ASSERT(incremental != nullptr && injectedModule.get());
    incrementalModule.reset(incremental);
    InitCodeGenAddedCachedMap();
    for (auto& func : incrementalModule->functions()) {
        if (func.getSubprogram()) {
            func.getSubprogram()->replaceUnit(nullptr);
        }
    }

    // Step 0: Update changed decls from incremental module.
    UpdateCachedDeclsFromInjectedModule(cachedMangles);
    CopyDeclarationsToInjectedModule();
    // Step 1: Fill valueMap.
    // valueMap.first: value in incremental module; valueMap.second: value in injected module.
    llvm::ValueToValueMapTy valueMap;
    FillValueMap(valueMap);
    // Step 2: Update injected module for the constant initializations of global variables.
    UpdateInitializationsOfGlobalVariables(valueMap);
    // Step 3: Update injected module for the function definitions.
    UpdateDefinitionsOfFunction(valueMap);
    // Step 4: Update the bodies of functions that used to keep some type declarations.
    UpdateBodyOfKeepTypesFunction(valueMap);
    // Step 5: Collect and erase useless functions in injected module.
    CollectUselessFunctions();
    EraseUselessFunctions();
    // Step 6: Update named metadata in injected module.
    UpdateReflectionMetadata();
    UpdateCodeGenAddedMetadata();
    UpdateIncrLLVMUsedNames();

    return injectedModule.release();
}

void IncrementalGen::UpdateCachedDeclsFromInjectedModule(const CachedMangleMap& cachedMangles)
{
    for (auto& name : std::as_const(cachedMangles.incrRemovedDecls)) {
        if (auto oldF = injectedModule->getFunction(name)) {
            oldF->setName(name + "$useless$");
            CollectUselessDefinitions(oldF);
            if (name.find("macroCall_c_") == 0 || name.find("macroCall_a_") == 0) {
                auto nameWrapper = name + "$real";
                auto oldFWrapper = injectedModule->getFunction(nameWrapper);
                oldFWrapper->setName(nameWrapper + "$useless$");
                CollectUselessDefinitions(oldFWrapper);
            }
        } else if (auto oldV = injectedModule->getNamedGlobal(name)) {
            oldV->setName(name + "$useless$");
            CollectUselessDefinitions(oldV);
        }

        if (codegenAddedCachedMap.find(name) != codegenAddedCachedMap.end()) {
            for (auto& codegenAddedName : codegenAddedCachedMap[name]) {
                if (auto old = injectedModule->getNamedGlobal(codegenAddedName)) {
                    old->setName(name + "$useless$");
                    CollectUselessDefinitions(old);
                }
            }
            codegenAddedCachedMap.erase(name);
        }
    }

    for (auto& name : std::as_const(cachedMangles.newExternalDecls)) {
        if (auto oldF = injectedModule->getFunction(name)) {
            AddLinkageTypeMetadata(*oldF, llvm::Function::ExternalLinkage, cgParallelEnabled);
        } else if (auto oldV = injectedModule->getNamedGlobal(name)) {
            AddLinkageTypeMetadata(*oldV, llvm::GlobalVariable::ExternalLinkage, cgParallelEnabled);
        }
    }

    for (auto& name : std::as_const(cachedMangles.importedInlineDecls)) {
        if (auto oldF = injectedModule->getFunction(name)) {
            DeleteFuncBody(oldF);
            AddLinkageTypeMetadata(*oldF, llvm::Function::ExternalLinkage, cgParallelEnabled);
            oldF->setDSOLocal(false);
            oldF->setPersonalityFn(nullptr);
        } else if (auto oldV = injectedModule->getNamedGlobal(name)) {
            AddLinkageTypeMetadata(*oldV, llvm::GlobalVariable::ExternalLinkage, cgParallelEnabled);
            oldV->setInitializer(nullptr);
            oldV->setDSOLocal(false);
        }
    }
}

void IncrementalGen::CopyDeclarationsToInjectedModule()
{
    // Copy global variable declarations to injected module from incremental module.
    for (auto& gv : incrementalModule->globals()) {
        auto gvName = gv.getName();
        if (IsGVForExtensionDefs(gv)) {
            size_t idx = 0U;
            std::string gvNameStr = gvName.str();
            auto found = injectedModule->getNamedGlobal(gvNameStr + std::to_string(idx));
            while (found) {
                ++idx;
                found = injectedModule->getNamedGlobal(gvNameStr + std::to_string(idx));
            }
            found = llvm::cast<llvm::GlobalVariable>(
                injectedModule->getOrInsertGlobal(gvNameStr + std::to_string(idx), gv.getValueType()));
            gv.setName(found->getName());
            (void)CopyGlobalVariableTo(*injectedModule, found->getName().str(), gv);
            continue;
        }
        auto injectedGv = injectedModule->getNamedGlobal(gvName);
        if (injectedGv &&
            (injectedGv->getType() != gv.getType() ||
                (injectedGv->isConstant() != gv.isConstant() && !gv.isDeclaration()))) {
            injectedGv->setName(gvName + "$useless$");
            injectedGv = nullptr;
        }
        if (injectedGv == nullptr) {
            (void)CopyGlobalVariableTo(*injectedModule, gvName.str(), gv);
        }
    }
    // Copy function declarations to injected module from incremental module.
    for (auto& func : *incrementalModule) {
        auto funcName = func.getName();
        auto injectedFunc = injectedModule->getFunction(funcName);
        if (injectedFunc && injectedFunc->getFunctionType() != func.getFunctionType()) {
            injectedFunc->setName(funcName + "$useless$");
            CollectUselessDefinitions(injectedFunc);
            injectedFunc = nullptr;
        }
        if (injectedFunc == nullptr) {
            auto newFunc = llvm::Function::Create(
                func.getFunctionType(), func.getLinkage(), func.getAddressSpace(), funcName, injectedModule.get());
            newFunc->copyAttributesFrom(&func);
        }
    }
}

void IncrementalGen::FillValueMap(llvm::ValueToValueMapTy& valueMap)
{
    for (llvm::Function& funcNew : *incrementalModule) {
        if (auto funcOld = injectedModule->getFunction(funcNew.getName()); funcOld) {
            valueMap[&funcNew] = funcOld;
            if (funcOld->arg_size() != funcNew.arg_size()) {
                continue;
            }
            auto destIt = funcOld->arg_begin();
            for (const llvm::Argument& i : funcNew.args()) {
                destIt->setName(i.getName());
                valueMap[&i] = &*destIt;
                ++destIt;
            }
        }
    }
    for (llvm::GlobalVariable& gv : incrementalModule->globals()) {
        if (auto existedGVInBase = injectedModule->getNamedGlobal(gv.getName()); existedGVInBase) {
            valueMap[&gv] = existedGVInBase;
        }
    }
}

void IncrementalGen::UpdateInitializationsOfGlobalVariables(llvm::ValueToValueMapTy& valueMap)
{
    llvm::DIBuilder diBuilder(*injectedModule);
    auto compileUnitToBeUpdated =
        llvm::cast<llvm::DICompileUnit>(injectedModule->getNamedMetadata("llvm.dbg.cu")->getOperand(0));
    auto currentGlobalVars = compileUnitToBeUpdated->getGlobalVariables();
    auto newGlobalVars = currentGlobalVars ? currentGlobalVars->clone() : diBuilder.getOrCreateArray({})->clone();
    for (auto& gv : incrementalModule->globals()) {
        if (gv.hasAttribute("nonRecompile")) {
            gv.setAttributes(gv.getAttributes().removeAttribute(incrementalModule->getContext(), "nonRecompile"));
            continue;
        }
        auto gvToBeUpdated = injectedModule->getNamedGlobal(gv.getName());
        CJC_NULLPTR_CHECK(gvToBeUpdated);
        if (IsGVForExtensionDefs(gv)) {
            gvToBeUpdated->setInitializer(llvm::MapValue(gv.getInitializer(), valueMap));
            continue;
        }
        // 1. Update metadata
        gvToBeUpdated->clearMetadata();
        llvm::SmallVector<std::pair<unsigned, llvm::MDNode*>, SMALL_VECTOR_SIZE> metadatas;
        gv.getAllMetadata(metadatas);
        for (auto md : metadatas) {
            gvToBeUpdated->setMetadata(md.first, llvm::MapMetadata(md.second, valueMap));
        }
        if (gv.getMetadata(llvm::LLVMContext::MD_dbg)) {
            auto gvToBeUpdatedDbgInfo = gvToBeUpdated->getMetadata(llvm::LLVMContext::MD_dbg);
            newGlobalVars->push_back(gvToBeUpdatedDbgInfo);
        }
        // 2. Update initializer
        if (!gv.isDeclaration()) {
            gvToBeUpdated->setInitializer(llvm::MapValue(gv.getInitializer(), valueMap));
            gvToBeUpdated->setLinkage(gv.getLinkage());
        }
    }
    auto permanentNode = llvm::MDNode::replaceWithPermanent(newGlobalVars->clone());
    compileUnitToBeUpdated->replaceGlobalVariables(permanentNode);
}

void IncrementalGen::UpdateDefinitionsOfFunction(llvm::ValueToValueMapTy& valueMap)
{
    for (auto& funcToBeInjected : *incrementalModule) {
        if (funcToBeInjected.isDeclaration()) {
            continue; // Declaration has no function body.
        }
        if (!AllowFunctionBodyBeReplaced(funcToBeInjected)) {
            continue;
        }

        auto funcName = funcToBeInjected.getName().str();
        auto funcToBeUpdated = injectedModule->getFunction(funcName);
        DeleteFuncBody(funcToBeUpdated);

        llvm::SmallVector<llvm::ReturnInst*, SMALL_VECTOR_SIZE> returns;
        llvm::CloneFunctionInto(
            funcToBeUpdated, &funcToBeInjected, valueMap, llvm::CloneFunctionChangeType::DifferentModule, returns);
        funcToBeUpdated->setLinkage(funcToBeInjected.getLinkage());

        UpdateCompileUnitWith(*funcToBeUpdated, **injectedModule->debug_compile_units_begin());
    }
}

void IncrementalGen::UpdateBodyOfKeepTypesFunction(llvm::ValueToValueMapTy& valueMap)
{
    auto funcToKeepTypes = injectedModule->getFunction(FOR_KEEPING_SOME_TYPES_FUNC_NAME);
    for (const auto& bb : incrementalModule->getFunction(FOR_KEEPING_SOME_TYPES_FUNC_NAME)->getBasicBlockList()) {
        (void)llvm::CloneBasicBlock(&bb, valueMap, "", funcToKeepTypes);
    }
}

void IncrementalGen::CollectUselessDefinitions(llvm::GlobalObject* uselessDefinition)
{
    if (uselessDefinition == nullptr) {
        return;
    }
    (void)uselessDefinitions.insert(uselessDefinition);
}

void IncrementalGen::CollectUselessFunctions()
{
    auto collectCallerDef = [](auto& q, auto item) {
        // When erasing a definition, we also need to erase the definitions which use current definition.
        for (auto it : item->users()) {
            // The users only can be either of two types: 1).Instruction, 2). Constant
            // If user's type is Instruction, then we need to collect and erase the func where the instruction lives.
            // If user's type is Constant, then we need to collect and erase itself.
            if (auto inst = llvm::dyn_cast<llvm::Instruction>(it); inst) {
                auto callInst = llvm::dyn_cast<llvm::CallInst>(it);
                // In the case where the init function for a global variable is deleted during incremental mode,
                // we should only remove the call to that global variable's initialization function,
                // and not proceed to handle the function in which the call is made.
                if (callInst && IsFileInitFuncName(callInst->getFunction()->getName().str())) {
                    callInst->eraseFromParent();
                    break;
                } else {
                    q.push(inst->getFunction());
                }
            } else {
                q.push(it);
            }
        }
    };
    std::unordered_set<llvm::Value*> removed;
    for (auto item : uselessDefinitions) {
        std::queue<llvm::Value*> q;
        std::list<llvm::Value*> willBeRemoved;
        q.push(item);
        while (!q.empty()) {
            auto current = q.front();
            q.pop();
            if (auto [_, success] = removed.emplace(current); !success) {
                continue;
            }
            willBeRemoved.emplace_front(current);
            collectCallerDef(q, current);
        }
        // Release GlobalObject's user before releasing the GlobalObject
        for (auto object : std::as_const(willBeRemoved)) {
            if (auto def = llvm::dyn_cast<llvm::GlobalObject>(object); def) {
                deferErase.emplace(def);
                def->clearMetadata();
                // For those functions need to be erased, they may have invoking relationships,
                // Firstly, we erase their bodies,
                // Secondly, we erase the declarations to avoid unexpected pointers double-free.
                if (auto func = llvm::dyn_cast<llvm::Function>(object)) {
                    DeleteFuncBody(func);
                } else if (auto gv = llvm::dyn_cast<llvm::GlobalVariable>(object)) {
                    gv->setInitializer(nullptr);
                }
            }
        }
    }
    uselessDefinitions.clear();
}

void IncrementalGen::EraseUselessFunctions()
{
    // Because normal funcs are from chir and the lambdas are calculated by codegen.
    // considering the call graph, we need to erase normal funcs first, and then lambdas.
    // 1. Erase the normal funcs which are useless from deferErase
    for (auto& item : deferErase) {
        item->eraseFromParent();
    }

    // 2. Erase the lambdas which are useless
    bool erased;
    do {
        erased = false;
        auto f = injectedModule->begin();
        while (f != injectedModule->end()) {
            auto cur = f++;
            bool isFromCFFI = (cur->hasFnAttribute(C2CJ_ATTR) || cur->hasFnAttribute(CJSTUB_ATTR));
            if (!cur->hasFnAttribute(FUNC_USED_BY_CLOSURE)) {
                continue;
            }
            cur->removeDeadConstantUsers();
            if (cur->user_empty() && (llvm::Function::isLocalLinkage(cur->getLinkage()) || isFromCFFI)) {
                cur->eraseFromParent();
                erased = true;
            }
        }
    } while (erased);
}

void IncrementalGen::UpdateReflectionMetadata()
{
    std::vector<llvm::MDNode*> metaBoundByTI;
    std::vector<llvm::MDNode*> metaBoundByTT;
    std::vector<llvm::MDNode*> metaBoundByGV;
    for (auto& gv : injectedModule->getGlobalList()) {
        if (gv.isDeclaration()) {
            continue;
        }
        // Collect reflection ti/tt
        if (gv.hasMetadata("Reflection")) {
            if (gv.hasAttribute(TYPE_TEMPLATE_ATTR)) {
                (void)metaBoundByTT.emplace_back(gv.getMetadata("Reflection"));
            } else {
                (void)metaBoundByTI.emplace_back(gv.getMetadata("Reflection"));
            }
        }
        // Collect reflection global variable
        if (gv.hasMetadata("ReflectionGV")) {
            (void)metaBoundByGV.emplace_back(gv.getMetadata("ReflectionGV"));
        }
    }
    auto namedMDForTI = injectedModule->getOrInsertNamedMetadata(METADATA_TYPES);
    namedMDForTI->clearOperands();
    for (auto meta : metaBoundByTI) {
        namedMDForTI->addOperand(meta);
    }

    auto namedMDForTT = injectedModule->getOrInsertNamedMetadata(METADATA_TYPETEMPLATES);
    namedMDForTT->clearOperands();
    for (auto meta : metaBoundByTT) {
        namedMDForTT->addOperand(meta);
    }

    if (auto namedMD = injectedModule->getNamedMetadata(METADATA_GLOBAL_VAR)) {
        namedMD->clearOperands();
        for (auto meta : metaBoundByGV) {
            namedMD->addOperand(meta);
        }
    }

    // Collect reflection function
    std::vector<llvm::MDNode*> metaBoundByF;
    for (auto& f : injectedModule->getFunctionList()) {
        if (f.isDeclaration()) {
            continue;
        }
        if (f.hasMetadata("ReflectionFunc")) {
            (void)metaBoundByF.emplace_back(f.getMetadata("ReflectionFunc"));
        }
    }

    if (auto namedMD = injectedModule->getNamedMetadata(METADATA_FUNCTIONS)) {
        namedMD->clearOperands();
        for (auto meta : metaBoundByF) {
            namedMD->addOperand(meta);
        }
    }
}

void IncrementalGen::UpdateCodeGenAddedMetadata()
{
    if (auto namedMD = injectedModule->getNamedMetadata("CodeGenAddedForIncr"); namedMD) {
        if (auto incrNamedMD = incrementalModule->getNamedMetadata("CodeGenAddedForIncr"); incrNamedMD) {
            for (auto meta : incrNamedMD->operands()) {
                namedMD->addOperand(meta);
            }
        }
    }
    if (auto namedMD = injectedModule->getNamedMetadata("StaticGenericTIsForIncr"); namedMD) {
        if (auto incrNamedMD = incrementalModule->getNamedMetadata("StaticGenericTIsForIncr"); incrNamedMD) {
            for (auto meta : incrNamedMD->operands()) {
                namedMD->addOperand(meta);
            }
        }
        for (size_t i = 0; i < namedMD->getNumOperands(); i++) {
            auto md = namedMD->getOperand(i);
            staticGINames.emplace_back(llvm::dyn_cast_or_null<llvm::MDString>(md->getOperand(0))->getString().str());
        }
    }
}

void IncrementalGen::UpdateIncrLLVMUsedNames()
{
    std::vector<std::string> specialGVs = {"ExternalExtensionDefs", "NonExternalExtensionDefs"};
    for (auto name : specialGVs) {
        if (injectedModule->getNamedGlobal(name) != nullptr) {
            llvmUsedGVNames.emplace_back(name);
        }
    }
}
