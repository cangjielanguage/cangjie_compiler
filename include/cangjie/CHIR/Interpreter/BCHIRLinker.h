// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the linker for BCHIR.
 */

#ifndef CANGJIE_CHIR_INTERRETER_BCHIRLINKER_H
#define CANGJIE_CHIR_INTERRETER_BCHIRLINKER_H

#include "cangjie/CHIR/Interpreter/BCHIR.h"
#include "cangjie/CHIR/Interpreter/InterpreterValueUtils.h"
#include "cangjie/Option/Option.h"

namespace Cangjie::CHIR::Interpreter {

class BCHIRLinker {
public:
    BCHIRLinker(Bchir& topBchir) : topBchir(topBchir), topDef(topBchir.linkedByteCode)
    {
    }

    /** @brief Link the provided packages in topBchir.
     *
     * @returns the value of the global vars that need to be initialized manually. ATM the return map is empty
     * if `ForConstEval == false`. */
    template <bool ForConstEval = false>
    std::unordered_map<Bchir::ByteCodeIndex, IVal> Run(std::vector<Bchir>& packages, const GlobalOptions& options);

    int GetGVARId(const std::string& name) const;

private:
    Bchir& topBchir;
    Bchir::Definition& topDef;
    /** @brief memoization table for the index in bchir.fileNames */
    std::unordered_map<std::string, Bchir::ByteCodeContent> fileName2IndexMemoization;
    /** @brief memoization table for the index in bchir.types */
    std::unordered_map<const CHIR::Type*, Bchir::ByteCodeContent> type2IndexMemoization;
    /** @brief memoization table for the index in bchir.strings */
    std::unordered_map<std::string, Bchir::ByteCodeContent> strings2IndexMemoization;

    /** @brief location in the bytecode of function's mangled names */
    std::unordered_map<std::string, Bchir::ByteCodeIndex> mName2FuncBodyIdx;
    /** @brief functions that have not yet been encoded, and set of locations that need to be updated when they are
     * encoded */
    std::unordered_map<std::string, std::vector<Bchir::ByteCodeIndex>> mName2FuncBodyIdxPlaceHolder;
    Bchir::ByteCodeContent gvarId{0};
    std::unordered_map<std::string, Bchir::ByteCodeContent> mName2GvarId;
    Bchir::ByteCodeContent classId{0};
    std::unordered_map<std::string, Bchir::ByteCodeContent> mName2ClassId;

    Bchir::ByteCodeContent methodId{0};
    std::unordered_map<std::string, Bchir::ByteCodeContent> mName2MethodId;

    /** Index of a dummy function of the form FRAME :: 0 :: ABORT */
    Bchir::ByteCodeIndex dummyAbortFuncIdx{Bchir::BYTECODE_CONTENT_MAX};

    void LinkClasses(const Bchir& bchir);
    void LinkClass(const Bchir& bchir, const std::string& mangledName);
    void LinkAndInitGlobalVars(
        const Bchir& bchir, std::unordered_map<Bchir::ByteCodeIndex, IVal>& gvarId2InitIVal, bool isLast);
    /** @brief Generates a dummy function that simply aborts interpretation. */
    void GenerateDummyAbortFunction();
    void LinkFunctions(const std::vector<Bchir>& packages);
    void GenerateCallsToConstInitFunctions(const std::vector<std::string>& constInitFuncs);
    /** @brief Traverse currentDef and append it to topBCHIR */
    void TraverseAndLink(const Bchir& bchir, const Bchir::Definition& currentDef,
        const std::vector<Bchir::ByteCodeContent>& fileMap, const std::vector<Bchir::ByteCodeContent>& typeMap,
        const std::vector<Bchir::ByteCodeContent>& stringMap);

    void AddPosition(const std::unordered_map<Bchir::ByteCodeIndex, Bchir::CodePosition>& positions,
        const std::vector<Bchir::ByteCodeContent>& fileMap, Bchir::ByteCodeIndex curr);
    void AddMangledName(
        const std::unordered_map<Bchir::ByteCodeIndex, std::string>& mangledNames, Bchir::ByteCodeIndex curr);

    /** @brief returns a map from that maps unliked files indexes to linked files indexes */
    std::vector<Bchir::ByteCodeContent> UnlinkedFiles2LinkedFiles(const std::vector<std::string>& fileNames);
    /** @brief returns a map from that maps unliked types indexes to linked types indexes */
    std::vector<Bchir::ByteCodeContent> UnlinkedTypes2LinkedTypes(const std::vector<CHIR::Type*>& types);
    /** @brief returns a map from that maps unliked strings indexes to strings types indexes */
    std::vector<Bchir::ByteCodeContent> UnlinkedStrings2LinkedStrings(const std::vector<std::string>& strings);

    Bchir::ByteCodeIndex FreshGVarId();

    /** @brief get the class id of class `classMangledName` */
    Bchir::ByteCodeContent GetClassId(const std::string& classMangledName);
    Bchir::ByteCodeContent GetMethodId(const std::string& methodName);

    /** @brief add `idx` to the set of locations that need to be set once function with mangled name `nName` is encoded
     */
    void AddToMName2FuncBodyIdxPlaceHolder(const std::string& mName, Bchir::ByteCodeIndex idx);
    /** @brief resolve all the indexes from `mName2FuncBodyIdxPlaceHolder` */
    void ResolveMName2FuncBodyIdxPlaceHolder(const std::string& mName, Bchir::ByteCodeIndex idx);
};

} // namespace Cangjie::CHIR::Interpreter

#endif // CANGJIE_CHIR_INTERRETER_BCHIRLINKER_H
