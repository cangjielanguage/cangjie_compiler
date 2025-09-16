// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares a BCHIR printer.
 */

#ifndef CANGJIE_CHIR_INTERRETER_BCHIRPRINTER_H
#define CANGJIE_CHIR_INTERRETER_BCHIRPRINTER_H

#include "cangjie/CHIR/Interpreter/BCHIR.h"
#include "cangjie/CHIR/Interpreter/OpCodes.h"
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/Option/Option.h"

#include <fstream>
#include <iostream>
#include <regex>
#include <vector>

namespace Cangjie::CHIR::Interpreter {
class BCHIRPrinter {
public:
    BCHIRPrinter(std::ostream& os, const Bchir& bchir) : os(os), bchir(bchir)
    {
    }
    // This function is not used by the interpreter directly, but it's
    // necessary when debugging the interpreter runtime
    void Print(std::string header = "");
    // This prints out all relevant information that will be serialized.
    void PrintAll(std::string header = "");
    // Open (and create) a file for writing debug BCHIR information to for the
    // given package and compilation stage.
    static std::fstream GetBCHIROutputFile(
        const GlobalOptions& options, const std::string& fullPackageName, const std::string& stageName);

private:
    std::ostream& os;
    const Bchir& bchir;

    class DefinitionPrinter {
    public:
        DefinitionPrinter(const Bchir& bchir, const Bchir::Definition& def, std::ostream& os)
            : bchir(bchir), os(os), bytecode(def.GetByteCode()), def(def)
        {
        }
        void Print();

    private:
        const std::string LEFT{""};
        const std::string RIGHT{""};
        const std::string ARGSEP{", "};
        const std::string OPSEP{"\n"};
        void PrintOP();
        void PrintOPFloat();
        void PrintOPFloat8bytes();
        void PrintOPRune();
        void PrintOPSwitch();
        void PrintOPBinRshift(OpCode opCode);
        void PrintOPTypeCast();
        void PrintPath();
        void PrintOPIntrinsic(OpCode opCode);
        // PrintOpCode advances index
        void PrintOpCode();
        void PrintAtIndex();
        void PrintAtIndex8bytes();

        // This function is not used by the interpreter directly, but it's
        // necessary when debugging the interpreter runtime
        void PrintTy();
        void Print(size_t argIndex, const std::string& str);

    private:
        const Bchir& bchir;
        std::ostream& os;
        const std::vector<Bchir::ByteCodeContent>& bytecode;
        const Bchir::Definition& def;
        size_t index{0};
    };

    // These methods are intended to print out BCHIR data
    // according to how it is serialized such that
    // we can debug BEP issues better later on.
    // This will need updates when new things are introduced to BCHIR.
    void PrintSClassTable();
    void PrintStrings();
    void PrintTypes();
    void PrintSourceFiles();
    static const std::unordered_map<Cangjie::OverflowStrategy, std::string> OVERFLOW_STRAT2STRING;
};

} // namespace Cangjie::CHIR::Interpreter

#endif // CANGJIE_CHIR_INTERRETER_BCHIRPRINTER_H
