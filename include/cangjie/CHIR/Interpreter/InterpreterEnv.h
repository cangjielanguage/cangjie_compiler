// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares an environment for the BCHIR interpreter.
 */

#ifndef CANGJIE_CHIR_INTERRETER_INTERPRETERENV_H
#define CANGJIE_CHIR_INTERRETER_INTERPRETERENV_H

#include "cangjie/CHIR/Interpreter/BCHIR.h"
#include "cangjie/CHIR/Interpreter/InterpreterValueUtils.h"

namespace Cangjie::CHIR::Interpreter {

struct Env {
    const static size_t LOCAL_ENV_DEFAULT_SIZE = 1024;
    Env(size_t sizeGlobalEnv) : numberOfGlobals(sizeGlobalEnv)
    {
        local.reserve(LOCAL_ENV_DEFAULT_SIZE);
        global.resize(numberOfGlobals, IInvalid());
    }

    void SetLocal(Bchir::VarIdx var, IVal&& node)
    {
        local[bp + var] = std::move(node);
    }

    void AllocateLocalVarsForFrame(size_t number)
    {
        // this should always be performed when entering the frame
        CJC_ASSERT(local.size() == bp);
        local.resize(local.size() + number);
    }

    void SetGlobal(Bchir::VarIdx var, IVal&& node)
    {
        CJC_ASSERT(var < global.size());
        global[var] = std::move(node);
    }

    const IVal& GetLocal(Bchir::VarIdx var)
    {
        CJC_ASSERT(bp + var < local.size());
        CJC_ASSERT(!std::holds_alternative<IInvalid>(local[bp + var]));
        return local[bp + var];
    }

    IVal& GetGlobal(Bchir::VarIdx var)
    {
        CJC_ASSERT(var < global.size());
        // Global vars are initialized with IInvalid. global[var] can be IInvalid the first time we read it.
        return global[var];
    }

    const IVal& PeekGlobal(Bchir::VarIdx var) const
    {
        CJC_ASSERT(var < global.size());
        // Global vars are initialized with IInvalid. global[var] can be IInvalid the first time we read it.
        return global[var];
    }

    void StartStackFrame()
    {
        bp = local.size();
    }

    /** @brief set base pointer to newBP and clean environment stack after bp. */
    void RestoreStackFrameTo(size_t newBP)
    {
        // we assume that the newBP is the preceeding stack frame of bp
        local.erase(local.begin() + static_cast<std::vector<Interpreter::IVal>::difference_type>(bp), local.end());
        bp = newBP;
    }

    size_t GetBP() const
    {
        return bp;
    }

private:
    size_t numberOfGlobals;
    /** @brief environment for global variables */
    std::vector<IVal> global;
    /** @brief environment for local variables */
    std::vector<IVal> local;
    /** @brief the base pointer in the local environment (an index of `local`) */
    size_t bp{0};
};

} // namespace Cangjie::CHIR::Interpreter

#endif // CANGJIE_CHIR_INTERRETER_INTERPRETERENV_H