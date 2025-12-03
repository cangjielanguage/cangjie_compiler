// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the CHIRPrinter class in CHIR.
 */

#ifndef CANGJIE_CHIR_CHIRPRINTER_H
#define CANGJIE_CHIR_CHIRPRINTER_H

#include "cangjie/CHIR/CHIR.h"

#include <iostream>

namespace Cangjie::CHIR {
class Type;
class Expression;
class Func;
class If;
class Loop;
class ForIn;
class Value;
class Block;
class BlockGroup;
class Package;

class CHIRPrinter {
public:
    static void PrintCFG(const Func& func, const std::string& path);
    static void PrintPackage(const Package& package, std::ostream& os = std::cout);
    static void PrintPackage(const Package& package, const std::string& fullPath);
    static void PrintCHIRSerializeInfo(ToCHIR::Phase phase, const std::string& path);

public:
    CHIRPrinter(std::ostream& os) : os(os)
    {
    }
    ~CHIRPrinter() = default;
    /*
     * @brief Returns the output stream of the printer.
     *
     */
    std::ostream& GetStream() const
    {
        return os;
    }
private:
    /** @brief The output stream for the printer.*/
    std::ostream& os;
};
} // namespace Cangjie::CHIR
#endif // CANGJIE_CHIR_CHIRPRINTER_H