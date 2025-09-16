// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements a linker for the BCHIR Interpreter.
 */

#include <queue>

#include "cangjie/CHIR/Interpreter/BCHIRPrinter.h"
#include "cangjie/CHIR/Interpreter/Utils.h"
#include "cangjie/Mangle/BaseMangler.h"
#include "cangjie/CHIR/Interpreter/BCHIRLinker.h"

using namespace Cangjie::CHIR::Interpreter;

template <bool ForConstEval>
std::unordered_map<Bchir::ByteCodeIndex, IVal> BCHIRLinker::Run(
    std::vector<Bchir>& packages, const GlobalOptions& options)
{
    std::unordered_map<Bchir::ByteCodeIndex, IVal> gvarId2InitIVal;

    // Jump over the dummy function definition
    topDef.Push(OpCode::JUMP);
    // Store the index to set the jump target later
    auto targetJumpIdx = topDef.NextIndex();
    topDef.Push(0); // 0 is just a dummy value. The real value will be set below with `targetJumpIdx`.
    // Generate dummy function of the form FRAME :: 0 :: ABORT
    // A call to this function will be generated when a mangled name is not found in mName2FuncBodyIdx
    GenerateDummyAbortFunction();
    // Set the jump target. The interpretation of the top-level definitions should start at `topDef.NextIndex()`.
    topDef.Set(targetJumpIdx, topDef.NextIndex());

    // First traversal to link global vars and set main mangled name
    for (size_t i = 0; i < packages.size(); ++i) {
        const auto& bchir = packages[i];
        if constexpr (ForConstEval) {
            LinkAndInitGlobalVars(bchir, gvarId2InitIVal, i == packages.size() - 1);
        }
        if (bchir.GetMainMangledName() != "") {
            // always get the main from the most recent package
            topBchir.SetMainMangledName(bchir.GetMainMangledName());
            topBchir.SetMainExpectedArgs(bchir.GetMainExpectedArgs());
        }
    }

    // Jump over all the definitions
    topDef.Push(OpCode::JUMP);
    // Store the index to set the jump target later
    targetJumpIdx = topDef.NextIndex();
    topDef.Push(0); // 0 is just a dummy value. The real value will be set below with `targetJumpIdx`.
    // Second traversal to link functions
    LinkFunctions(packages);

    // Set the jump target. The interpretation of the top-level definitions should start at `topDef.NextIndex()`.
    topDef.Set(targetJumpIdx, topDef.NextIndex());

    // BEGIN of top-level initialization
    if constexpr (ForConstEval) {
        // When running const-evaluation we initialize some variables manually instead of relying on linked BCHIR
        // to do that.
        GenerateCallsToConstInitFunctions(packages.back().initFuncsForConsts);
    }

    // End of top-level initialization
    topDef.Push(OpCode::EXIT);

    // we use this to reserve global env space
    topBchir.SetNumGlobalVars(gvarId);

    // third traversal to link classes
    for (const auto& bchir : packages) {
        LinkClasses(bchir);
    }

    topBchir.LinkDefaultFunctions(mName2FuncBodyIdx);
    // Used for filename when debugging both below and when running interpreter
    topBchir.packageName = packages.back().packageName;

    auto printBchirOption =
        ForConstEval ? GlobalOptions::PrintBCHIROption::CE_LINKED : GlobalOptions::PrintBCHIROption::LINKED;
    if (options.PrintBchir(printBchirOption)) {
        auto stageName = ForConstEval ? "ce-linked" : "linked";
        auto bchirFile = CHIR::Interpreter::BCHIRPrinter::GetBCHIROutputFile(
            options, topBchir.packageName, stageName);
        auto bchirPrinter = CHIR::Interpreter::BCHIRPrinter(bchirFile, topBchir);
        bchirPrinter.PrintAll("Linked bytecode");
    }
    return gvarId2InitIVal;
}

template std::unordered_map<Bchir::ByteCodeIndex, IVal> BCHIRLinker::Run<true>(
    std::vector<Bchir>& packages, const GlobalOptions& options);
template std::unordered_map<Bchir::ByteCodeIndex, IVal> BCHIRLinker::Run<false>(
    std::vector<Bchir>& packages, const GlobalOptions& options);

void BCHIRLinker::GenerateCallsToConstInitFunctions(const std::vector<std::string>& constInitFuncs)
{
    for (auto& nm : constInitFuncs) {
        auto funcIdxIt = mName2FuncBodyIdx.find(nm);
        if (funcIdxIt != mName2FuncBodyIdx.end()) {
            topDef.Push(OpCode::FUNC);
            topDef.Push(funcIdxIt->second);
            topDef.Push(OpCode::APPLY);
            topDef.Push(1);            // only has the func arg
            topDef.Push(OpCode::DROP); // always returns a UNIT
        }                              // else the constant and function was removed by some CHIR optimization
    }
}

void BCHIRLinker::GenerateDummyAbortFunction()
{
    dummyAbortFuncIdx = topDef.NextIndex();
    topDef.Push(OpCode::FRAME);
    topDef.AddMangledNameAnnotation(topDef.NextIndex() - 1, "linker_dummy_abort_function");
    topDef.Push(0);
    topDef.Push(OpCode::ABORT);
}

Bchir::ByteCodeContent BCHIRLinker::GetClassId(const std::string& classMangledName)
{
    auto it = mName2ClassId.find(classMangledName);
    if (it != mName2ClassId.end()) {
        return it->second;
    }
    auto thisClassId = classId++;
    mName2ClassId.emplace_hint(it, classMangledName, thisClassId);
    return thisClassId;
}

Bchir::ByteCodeContent BCHIRLinker::GetMethodId(const std::string& methodName)
{
    /* Note that two methods with the same name will have the same ID even if they are from different
    classes (in the same class, method's names/ids are unique). This is however not a problem because the
    invoke always takes into account the class ID and the method ID. Thus the method ID for a class
    ID will still be unique. */
    auto it = mName2MethodId.find(methodName);
    if (it != mName2MethodId.end()) {
        return it->second;
    }
    auto thisMethodId = methodId++;
    mName2MethodId.emplace_hint(it, methodName, thisMethodId);
    return thisMethodId;
}

void BCHIRLinker::LinkClasses(const Bchir& bchir)
{
    for (const auto& p : bchir.GetSClassTable()) {
        auto& mangledName = p.first;
        LinkClass(bchir, mangledName);
    }
}

void BCHIRLinker::LinkClass(const Bchir& bchir, const std::string& mangledName)
{
    auto thisClassIt = mName2ClassId.find(mangledName);
    Bchir::ByteCodeIndex thisClassId = 0;
    if (thisClassIt == mName2ClassId.end()) {
        thisClassId = classId++;
        mName2ClassId.emplace_hint(thisClassIt, mangledName, thisClassId);
    } else {
        // class ID was already generated when linking bytecode
        thisClassId = thisClassIt->second;
        if (topBchir.ClassExists(thisClassId)) {
            // ClassInfo was already generated for this class
            return;
        } // else ClassInfo was not yet generated for this class
    }

    Bchir::ClassInfo classInfo;
    classInfo.mangledName = mangledName;
    // only reached for classes from this package; otherwise it was already encoded and this point is not reached
    auto sClassInfo = bchir.GetSClass(mangledName);
    if (sClassInfo == nullptr) {
        // This can only happen when linking packages for const-eval. The reason is that some packages are not loaded
        // because of CompilerInstance::AddRequiredPackagesForConstEval. At this point we assume that if a package is
        // not loaded then the information about the classes/interfaces in it is not required. If this assumptions is
        // proved incorrect we should load packages that contain super classes/interfaces for the declared
        // classes/interfaces in the current package.
        topBchir.AddClass(thisClassId, std::move(classInfo));
        return;
    }
    for (const auto& superClass : sClassInfo->superClasses) {
        // make sure all the super classes are encoded
        LinkClass(bchir, superClass);
    }

    // append **all** superclasses including indirect ones
    for (const auto& superClass : sClassInfo->superClasses) {
        auto superClassIt = mName2ClassId.find(superClass);
        CJC_ASSERT(superClassIt != mName2ClassId.end());
        auto superId = superClassIt->second;
        classInfo.superClasses.emplace(superId);
        // all the super classes have been encoded before
        auto& superSuperClasses = topBchir.GetClass(superId).superClasses;
        for (auto scit : superSuperClasses) {
            (void)classInfo.superClasses.emplace(scit);
        }
    }

    for (const auto& [methodName, funcMangledName] : sClassInfo->vtable) {
        Bchir::ByteCodeIndex thisMethodId = GetMethodId(methodName);
        auto thisMethdIdxIt = mName2FuncBodyIdx.find(funcMangledName);
        if (thisMethdIdxIt != mName2FuncBodyIdx.end()) {
            (void)classInfo.vtable.emplace(thisMethodId, thisMethdIdxIt->second);
        } else {
            // This can happen because we only load packages that are required for const-eval - see
            // requiredConstEvalDependencies in the RunConstantEvaluation function; However, a class type might appear
            // as an import CHIR type, but the package containing the definition is never loaded - we are currently
            // assuming that those methods won't be used in const contexts.
            (void)classInfo.vtable.emplace(thisMethodId, dummyAbortFuncIdx);
        }
    }

    auto finalizerIdxIt = mName2FuncBodyIdx.find(sClassInfo->finalizer);
    if (sClassInfo->finalizer != "" && finalizerIdxIt != mName2FuncBodyIdx.end()) {
        classInfo.finalizerIdx = finalizerIdxIt->second;
    } else {
        classInfo.finalizerIdx = 0;
    }
    topBchir.AddClass(thisClassId, std::move(classInfo));
}

void BCHIRLinker::LinkAndInitGlobalVars(
    const Bchir& bchir, std::unordered_map<Bchir::ByteCodeIndex, IVal>& gvarId2InitIVal, bool isLast)
{
    for (auto& p : bchir.GetGlobalVars()) {
        auto& name = p.first;
        CJC_ASSERT(name.size() > 0);
        if (mName2GvarId.find(name) != mName2GvarId.end()) {
            continue;
        }
        auto& def = p.second;
        auto id = FreshGVarId();
        mName2GvarId.emplace(name, id);
        auto initIVal = Interpreter::ByteCodeToIval(def, bchir, topBchir);

        CJC_ASSERT(isLast);
        gvarId2InitIVal.emplace(id, initIVal);
    }
}

std::vector<Bchir::ByteCodeContent> BCHIRLinker::UnlinkedFiles2LinkedFiles(const std::vector<std::string>& fileNames)
{
    std::vector<Bchir::ByteCodeContent> fileMap;
    for (auto& fileName : fileNames) {
        auto fileIdxIt = fileName2IndexMemoization.find(fileName);
        if (fileIdxIt == fileName2IndexMemoization.end()) {
            fileMap.emplace_back(static_cast<Bchir::ByteCodeContent>(topBchir.AddFileName(fileName)));
        } else {
            fileMap.emplace_back(fileIdxIt->second);
        }
    }
    return fileMap;
}

std::vector<Bchir::ByteCodeContent> BCHIRLinker::UnlinkedTypes2LinkedTypes(const std::vector<CHIR::Type*>& types)
{
    std::vector<Bchir::ByteCodeContent> typeMap;
    for (auto ty : types) {
        auto typeIdxIt = type2IndexMemoization.find(ty);
        if (typeIdxIt == type2IndexMemoization.end()) {
            typeMap.emplace_back(static_cast<Bchir::ByteCodeContent>(topBchir.AddType(*ty)));
        } else {
            typeMap.emplace_back(typeIdxIt->second);
        }
    }
    return typeMap;
}

std::vector<Bchir::ByteCodeContent> BCHIRLinker::UnlinkedStrings2LinkedStrings(const std::vector<std::string>& strings)
{
    std::vector<Bchir::ByteCodeContent> stringMap;
    for (auto& str : strings) {
        auto stringIdxIt = strings2IndexMemoization.find(str);
        if (stringIdxIt == strings2IndexMemoization.end()) {
            stringMap.emplace_back(static_cast<Bchir::ByteCodeContent>(topBchir.AddString(str)));
        } else {
            stringMap.emplace_back(stringIdxIt->second);
        }
    }
    return stringMap;
}

void BCHIRLinker::LinkFunctions(const std::vector<Bchir>& packages)
{
    // we traverse the bytecode each function definition
    // and append that to topDef while adjusting some indexes
    // and resolving GVAR ids and class ids
    for (const auto& bchir : packages) {
        auto fileMap = UnlinkedFiles2LinkedFiles(bchir.GetFileNames());
        auto typeMap = UnlinkedTypes2LinkedTypes(bchir.GetTypes());
        auto stringMap = UnlinkedStrings2LinkedStrings(bchir.GetStrings());
        for (auto& p : bchir.GetFunctions()) {
            auto& mangledName = p.first;
            auto& defPtr = p.second;

            auto bodyIdx = topDef.NextIndex();
            ResolveMName2FuncBodyIdxPlaceHolder(mangledName, bodyIdx);

            // reserving space for a frame
            topDef.Push(OpCode::FRAME);
            topDef.AddMangledNameAnnotation(topDef.NextIndex() - 1, mangledName);
            topDef.Push(defPtr.GetNumLVars());

            // encode the body
            TraverseAndLink(bchir, defPtr, fileMap, typeMap, stringMap);
        }
    }
}

void BCHIRLinker::AddPosition(const std::unordered_map<Bchir::ByteCodeIndex, Bchir::CodePosition>& positions,
    const std::vector<Bchir::ByteCodeContent>& fileMap, Bchir::ByteCodeIndex curr)
{
    auto positionIt = positions.find(curr);
    if (positionIt != positions.end()) {
        auto position = positionIt->second;
        position.fileID = fileMap[position.fileID];
        topDef.AddCodePositionAnnotation(topDef.NextIndex() - 1, position);
    }
}

void BCHIRLinker::AddMangledName(
    const std::unordered_map<Bchir::ByteCodeIndex, std::string>& mangledNames, Bchir::ByteCodeIndex curr)
{
    auto mangled = mangledNames.find(curr);
    if (mangled != mangledNames.end()) {
        topDef.AddMangledNameAnnotation(topDef.NextIndex() - 1, mangled->second);
    }
}

void BCHIRLinker::TraverseAndLink(const Bchir& bchir, const Bchir::Definition& currentDef,
    const std::vector<Bchir::ByteCodeContent>& fileMap, const std::vector<Bchir::ByteCodeContent>& typeMap,
    const std::vector<Bchir::ByteCodeContent>& stringMap)
{
    const auto offset = topDef.NextIndex();
    Bchir::ByteCodeIndex curr{0};
    auto& positions = currentDef.GetCodePositionsAnnotations();
    auto& mangledNames = currentDef.GetMangledNamesAnnotations();
    while (curr < currentDef.NextIndex()) {
        auto op = static_cast<OpCode>(currentDef.Get(curr));
        CJC_ASSERT(op <= OpCode::INVALID);
        // pushing the opcode
        topDef.Push(op);
        // propagate code positions for call stack printing purposes
        AddPosition(positions, fileMap, curr);
#ifndef NDEBUG
        // propagate all the mangled names for debugging purposes
        AddMangledName(mangledNames, curr);
#endif
        auto next = curr + GetOpCodeArgSize(op) + 1;
        // pushing anything else
        switch (op) {
            case OpCode::GVAR: {
                auto& mgl = currentDef.GetMangledNameAnnotation(curr);
                auto gVarIt = mName2GvarId.find(mgl);
                if (gVarIt != mName2GvarId.end()) {
                    topDef.Push(mName2GvarId[mgl]);
                    break;
                } // else we treat it as a function
                topDef.SetOp(topDef.NextIndex() - 1, OpCode::FUNC);
                [[fallthrough]];
            }
            case OpCode::FUNC: {
#ifdef NDEBUG
                AddMangledName(mangledNames, curr);
                // #else debug AddMangledName is performed for all the operations anyway
#endif
                auto& mgl = currentDef.GetMangledNameAnnotation(curr);
                auto it = mName2FuncBodyIdx.find(mgl);
                if (it != mName2FuncBodyIdx.end()) {
                    topDef.Push(it->second);
                } else {
                    AddToMName2FuncBodyIdxPlaceHolder(mgl, topDef.NextIndex());
                    // Most of the time `dummyAbortFuncIdx` will be modified when function is visited, otherwise
                    // something went wrong and the interpretation will abort.
                    topDef.Push(dummyAbortFuncIdx);
                }
                break;
            }
            case OpCode::STRING: {
                topDef.Push(stringMap[currentDef.Get(curr + 1)]);
                break;
            }
            case OpCode::ALLOCATE_CLASS: {
                auto& mgl = currentDef.GetMangledNameAnnotation(curr);
                auto thisClassId = GetClassId(mgl);
                topDef.Push(thisClassId);
                topDef.Push(currentDef.Get(curr + Bchir::FLAG_TWO));
                break;
            }
            case OpCode::ALLOCATE_CLASS_EXC: {
                auto& mgl = currentDef.GetMangledNameAnnotation(curr);
                auto thisClassId = GetClassId(mgl);
                topDef.Push(thisClassId);
                topDef.Push(currentDef.Get(curr + Bchir::FLAG_TWO));
                topDef.Push(currentDef.Get(curr + Bchir::FLAG_THREE) + offset);
                break;
            }
            case OpCode::ALLOCATE_STRUCT_EXC: {
                topDef.Push(currentDef.Get(curr + 1));                        // number of fields
                topDef.Push(currentDef.Get(curr + Bchir::FLAG_TWO) + offset); // jump target for when exception
                break;
            }
            case OpCode::ALLOCATE_EXC: {
                topDef.Push(currentDef.Get(curr + 1) + offset); // jump target for when exception
                break;
            }
            case OpCode::BOX:
            case OpCode::INSTANCEOF: {
                auto& mgl = currentDef.GetMangledNameAnnotation(curr);
                auto thisClassId = GetClassId(mgl);
                // Create class info if the type doesn't have a class table
                // (e.g. enums). We don't do this for classes with a class
                // table because we need to finish converting their methods
                // to BCHIR before creating their vtables.
                if (bchir.GetSClass(mgl) == nullptr) {
                    LinkClass(bchir, mgl);
                }
                topDef.Push(thisClassId);
                break;
            }
            case OpCode::INVOKE_EXC: {
                topDef.Push(currentDef.Get(curr + 1)); // number of arguments
                auto& mgl = currentDef.GetMangledNameAnnotation(curr);
                topDef.Push(GetMethodId(mgl));                                  // method ID
                topDef.Push(currentDef.Get(curr + Bchir::FLAG_THREE) + offset); // jump target for when exception
                break;
            }
            case OpCode::INVOKE: {
                topDef.Push(currentDef.Get(curr + 1));
                auto& mgl = currentDef.GetMangledNameAnnotation(curr);
                topDef.Push(GetMethodId(mgl));
                break;
            }
            case OpCode::JUMP: {
                auto nextIdx = currentDef.Get(curr + 1) + offset;
                topDef.Push(nextIdx);
                break;
            }
            case OpCode::BRANCH: {
                auto trueIdx = currentDef.Get(curr + 1) + offset;
                topDef.Push(trueIdx);
                auto falseIdx = currentDef.Get(curr + Bchir::FLAG_TWO) + offset;
                topDef.Push(falseIdx);
                break;
            }
            case OpCode::INTRINSIC1: {
                topDef.Push(currentDef.Get(curr + 1));                  // intrinsic kind
                auto oldTyIdx = currentDef.Get(curr + Bchir::FLAG_TWO); // type
                Bchir::ByteCodeContent newTyIdx = Bchir::BYTECODE_CONTENT_MAX;
                if (oldTyIdx != Bchir::BYTECODE_CONTENT_MAX) {
                    newTyIdx = typeMap[oldTyIdx];
                }
                topDef.Push(newTyIdx);
                break;
            }
            case OpCode::SYSCALL: {
                auto nameId = stringMap[currentDef.Get(curr + 1)];
                topDef.Push(nameId);
                auto numberOfArgs = currentDef.Get(curr + Bchir::FLAG_TWO);
                topDef.Push(numberOfArgs);
                auto resTyId = typeMap[currentDef.Get(curr + Bchir::FLAG_THREE)];
                topDef.Push(resTyId);
                for (Bchir::ByteCodeIndex j = 0; j < numberOfArgs; ++j) {
                    auto tyId = typeMap[currentDef.Get(curr + Bchir::FLAG_TWO + Bchir::FLAG_TWO + j)];
                    topDef.Push(static_cast<Bchir::ByteCodeContent>(tyId));
                }
                next = curr + GetOpCodeArgSize(op) + 1 + numberOfArgs + 1;
                break;
            }
            case OpCode::SWITCH: {
                auto casesIdx = curr + Bchir::FLAG_TWO;
                auto cases = currentDef.Get(casesIdx);
                auto firstJump = casesIdx + 1 + cases * Bchir::FLAG_TWO;
                topDef.Push(currentDef.Get(curr + 1));
                topDef.Push(currentDef.Get(curr + Bchir::FLAG_TWO));
                for (size_t j = casesIdx + 1; j < firstJump; j += Bchir::FLAG_TWO) {
                    topDef.Push8bytes(currentDef.Get8bytes(static_cast<Bchir::ByteCodeIndex>(j)));
                }
                for (size_t j = firstJump; j < firstJump + cases + 1; ++j) {
                    topDef.Push(currentDef.Get(static_cast<Bchir::ByteCodeIndex>(j)) + offset);
                }
                next = curr + GetOpCodeArgSize(op) + /* type and number of cases */
                    cases * Bchir::FLAG_TWO +        /* cases (8 bytes each) */
                    1 +                              /* default target */
                    cases +                          /* other targets */
                    1;
                break;
            }
            case OpCode::APPLY_EXC: {
                topDef.Push(currentDef.Get(curr + 1));                        // number of arguments
                topDef.Push(currentDef.Get(curr + Bchir::FLAG_TWO) + offset); // jump target for when exception
                break;
            }
            case OpCode::UN_NEG_EXC:
            case OpCode::BIN_ADD_EXC:
            case OpCode::BIN_SUB_EXC:
            case OpCode::BIN_MUL_EXC:
            case OpCode::BIN_DIV_EXC:
            case OpCode::BIN_MOD_EXC:
            case OpCode::BIN_EXP_EXC: {
                topDef.Push(currentDef.Get(curr + 1));                          // type kind
                topDef.Push(currentDef.Get(curr + Bchir::FLAG_TWO));            // overflow strategy
                topDef.Push(currentDef.Get(curr + Bchir::FLAG_THREE) + offset); // jump target for when exception
                break;
            }
            case OpCode::BIN_RSHIFT_EXC:
            case OpCode::BIN_LSHIFT_EXC: {
                topDef.Push(currentDef.Get(curr + 1));                         // type kind
                topDef.Push(currentDef.Get(curr + Bchir::FLAG_TWO));           // overflow strategy
                topDef.Push(currentDef.Get(curr + Bchir::FLAG_THREE));         // type kind for rhs
                topDef.Push(currentDef.Get(curr + Bchir::FLAG_FOUR) + offset); // jump target for when exception
                break;
            }
            case OpCode::CAPPLY: {
                auto argsSize = currentDef.Get(curr + 1);
                topDef.Push(argsSize); // num of Args
                next = curr + argsSize + Bchir::FLAG_THREE;
                for (auto j = curr + 2; j < next; ++j) {
                    auto oldTyIdx = currentDef.Get(j); // type
                    Bchir::ByteCodeContent newTyIdx = Bchir::BYTECODE_CONTENT_MAX;
                    newTyIdx = typeMap[oldTyIdx];
                    topDef.Push(newTyIdx);
                }
                break;
            }
            case OpCode::STOREINREF:
            case OpCode::GETREF: {
                auto pathSize = currentDef.Get(curr + 1);
                next = curr + GetOpCodeArgSize(op) + pathSize + 1;
                for (auto j = curr + 1; j < next; ++j) {
                    topDef.Push(currentDef.Get(j));
                }
                break;
            }
            case OpCode::FIELD_TPL: {
                auto pathSize = currentDef.Get(curr + 1);
                next = curr + GetOpCodeArgSize(op) + pathSize + 1;
                for (auto j = curr + 1; j < next; ++j) {
                    topDef.Push(currentDef.Get(j));
                }
                break;
            }
            default: {
                for (auto j = curr + 1; j < next; ++j) {
                    topDef.Push(currentDef.Get(j));
                }
            }
        }
        curr = next;
    }
}

Bchir::ByteCodeContent BCHIRLinker::FreshGVarId()
{
    return gvarId++;
}

void BCHIRLinker::AddToMName2FuncBodyIdxPlaceHolder(const std::string& mName, Bchir::ByteCodeIndex idx)
{
    auto it = mName2FuncBodyIdxPlaceHolder.find(mName);
    if (it == mName2FuncBodyIdxPlaceHolder.end()) {
        std::vector<Bchir::ByteCodeIndex> vec{idx};
        mName2FuncBodyIdxPlaceHolder.emplace(mName, vec);
    } else {
        it->second.emplace_back(idx);
    }
}

void BCHIRLinker::ResolveMName2FuncBodyIdxPlaceHolder(const std::string& mName, Bchir::ByteCodeIndex idx)
{
    mName2FuncBodyIdx.emplace(mName, idx);
    auto it = mName2FuncBodyIdxPlaceHolder.find(mName);
    if (it != mName2FuncBodyIdxPlaceHolder.end()) {
        for (auto ph : it->second) {
            topDef.Set(ph, idx);
        }
        mName2FuncBodyIdxPlaceHolder.erase(mName);
    }
}

int BCHIRLinker::GetGVARId(const std::string& name) const
{
    auto it = mName2GvarId.find(name);
    if (it != mName2GvarId.end()) {
        return static_cast<int>(it->second);
    }
    return -1;
}
