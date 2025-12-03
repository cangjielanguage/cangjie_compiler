// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares some utility functions for interpreter module.
 */

#ifndef CANGJIE_CHIR_INTERRETER_UTILS_H
#define CANGJIE_CHIR_INTERRETER_UTILS_H

#include "cangjie/CHIR/IR/Expression/Terminator.h"
#include "cangjie/CHIR/Interpreter/BCHIR.h"
#include "cangjie/CHIR/Interpreter/InterpreterValue.h"
#include "cangjie/CHIR/Interpreter/OpCodes.h"
#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/IR/Type/Type.h"

namespace Cangjie::CHIR::Interpreter {

OpCode PrimitiveTypeKind2OpCode(Type::TypeKind kind);
OpCode UnExprKind2OpCode(Cangjie::CHIR::ExprKind exprKind);
OpCode BinExprKind2OpCode(Cangjie::CHIR::ExprKind exprKind);
OpCode BinExprKindWitException2OpCode(Cangjie::CHIR::ExprKind exprKind);
IVal ByteCodeToIval(const Bchir::Definition& def, const Bchir& bchir, Bchir& topBchir);

template <bool OmitFirstArg = false>
std::string MangleMethodName(const std::string& methodName, const FuncType& funcTy)
{
    // T0D0: instead we can change SVTable so that the key is pair<std::string, Type>
    std::string res = methodName + "(";
    size_t start = 0;
    if constexpr (OmitFirstArg) {
        start = 1;
    }
    auto paramTys = funcTy.GetParamTypes();
    for (size_t i = start; i < paramTys.size(); i++) {
        res += paramTys[i]->ToString() + " ";
    }
    res += ")";
    return res;
}

}

#endif // CANGJIE_CHIR_INTERRETER_BCHIR_H
