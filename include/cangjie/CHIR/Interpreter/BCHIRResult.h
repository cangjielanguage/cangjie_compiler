// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares interpreter values.
 */

#ifndef CANGJIE_CHIR_INTERRETER_BCHIRRESULT_H
#define CANGJIE_CHIR_INTERRETER_BCHIRRESULT_H

#include "cangjie/CHIR/Interpreter/InterpreterValueUtils.h"
#include <variant>

namespace Cangjie::CHIR::Interpreter {

// possible outcomes of interpretation
struct INotRun {
};

struct IException {
    IVal ptr;
};
struct ISuccess {
    IVal val;
};

using IResult = std::variant<INotRun, IException, ISuccess>;

} // namespace Cangjie::CHIR::Interpreter

#endif // CANGJIE_CHIR_INTERRETER_BCHIRRESULT_H