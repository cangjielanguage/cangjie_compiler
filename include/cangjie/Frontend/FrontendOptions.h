// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the FrontendOptions, which parses frontend arguments.
 */

#ifndef CANGJIE_FRONTEND_FRONTENDOPTIONS_H
#define CANGJIE_FRONTEND_FRONTENDOPTIONS_H

#include <string>

#include "cangjie/Option/Option.h"

namespace Cangjie {
class FrontendOptions : public GlobalOptions {
public:
    virtual ~FrontendOptions() { }

    enum class DumpAction {
        NO_ACTION,        /**< No specific action. */
        DUMP_TOKENS,      /**< Dump tokens. */
        DUMP_SYMBOLS,     /**< Dump symbols after semantic check. */
        TYPE_CHECK,       /**< Parse ast and do typecheck. */
        DUMP_DEP_PKG,      /**< Dump dependent packages of current package. */
        DESERIALIZE_CHIR, /**< Deserialize Chir. */
    };

    FrontendOptions() : dumpAction(DumpAction::NO_ACTION) {}

    /**
     * Indicates the dump action the user requested that the frontend execute.
     */
    DumpAction dumpAction = DumpAction::NO_ACTION;

protected:
    virtual std::optional<bool> ParseOption(OptionArgInstance& arg) override;
};
} // namespace Cangjie
#endif // CANGJIE_FRONTEND_FRONTENDOPTIONS_H
