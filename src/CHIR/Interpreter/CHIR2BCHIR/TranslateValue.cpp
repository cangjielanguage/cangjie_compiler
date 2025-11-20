// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements a translation from CHIR to BCHIR.
 */

#include "cangjie/CHIR/Interpreter/CHIR2BCHIR.h"

#include <securec.h>

#include "cangjie/CHIR/Interpreter/Utils.h"
#include "cangjie/CHIR/Utils.h"

using namespace Cangjie::CHIR;
using namespace Interpreter;

void CHIR2BCHIR::TranslateValue(Context& ctx, const Value& value)
{
    if (value.IsParameter()) {
        // argument are translated as LVar
        PushOpCodeWithAnnotations(ctx, OpCode::LVAR, value, LVarId(ctx, value));
    } else if (value.IsLocalVar()) {
        // local vars
        PushOpCodeWithAnnotations(ctx, OpCode::LVAR, value, LVarId(ctx, value));
    } else if (value.IsLiteral()) {
        auto literal = StaticCast<const LiteralValue*>(&value);
        TranslateLiteralValue(ctx, *literal);
    } else if (value.IsImportedFunc() &&
        value.GetAttributeInfo().TestAttr(Attribute::FOREIGN)) {
        // this is a syscall and will never be used
        PushOpCodeWithAnnotations(ctx, OpCode::NULLPTR, value);
    } else if (value.IsFuncWithBody()) {
        PushOpCodeWithAnnotations<true>(ctx, OpCode::FUNC, value, UINT32_MAX);
    } else if (Is<GlobalVarBase>(value) || value.IsImportedFunc()) {
        // global vars and imported vars will be resolved during linking
        auto mangledName = value.GetIdentifierWithoutPrefix();
        if (mangledName == CHIR::GV_PKG_INIT_ONCE_FLAG) {
            // This is an hack because $has_applied_pkg_init_func is not a real mangled name. It's
            // not unique amongst packages. T0D0: issue 2079
            auto opIdx = ctx.def.NextIndex();
            PushOpCodeWithAnnotations<false>(ctx, OpCode::GVAR, value, 0u);
            auto fixedMangledName = CHIR::GV_PKG_INIT_ONCE_FLAG + "-" + bchir.packageName;
            ctx.def.AddMangledNameAnnotation(opIdx, fixedMangledName);
        } else {
            PushOpCodeWithAnnotations<true>(ctx, OpCode::GVAR, value, 0u);
        }
    } else {
        CJC_ABORT();
    }
}

void CHIR2BCHIR::TranslateLiteralValue(Context& ctx, const LiteralValue& value)
{
    if (value.IsBoolLiteral()) {
        auto boolVal = StaticCast<const BoolLiteral*>(&value);
        PushOpCodeWithAnnotations(ctx, OpCode::BOOL, value, boolVal->GetVal());
    } else if (value.IsFloatLiteral()) {
        auto floatLit = StaticCast<const FloatLiteral*>(&value);
        TranslateFloatValue(ctx, *floatLit);
    } else if (value.IsIntLiteral()) {
        auto intLit = StaticCast<const IntLiteral*>(&value);
        TranslateIntValue(ctx, *intLit);
    } else if (value.IsNullLiteral()) {
        PushOpCodeWithAnnotations(ctx, OpCode::NULLPTR, value);
    } else if (value.IsRuneLiteral()) {
        auto charLit = StaticCast<const RuneLiteral*>(&value);
        PushOpCodeWithAnnotations(ctx, OpCode::RUNE, value, charLit->GetVal());
    } else if (value.IsStringLiteral()) {
        auto stringLit = StaticCast<const StringLiteral*>(&value);
        PushOpCodeWithAnnotations(ctx, OpCode::STRING, *stringLit, GetStringIdx(stringLit->GetVal()));
    } else if (value.IsUnitLiteral()) {
        PushOpCodeWithAnnotations(ctx, OpCode::UNIT, value);
    } else {
        CJC_ABORT();
    }
}

void CHIR2BCHIR::TranslateIntValue(Context& ctx, const IntLiteral& value)
{
    auto typeKind = value.GetType()->GetTypeKind();
    auto opcode = PrimitiveTypeKind2OpCode(typeKind);
    PushOpCodeWithAnnotations(ctx, opcode, value);
    if (typeKind == Type::TypeKind::TYPE_INT64 || typeKind == Type::TypeKind::TYPE_UINT64 ||
        typeKind == Type::TypeKind::TYPE_INT_NATIVE || typeKind == Type::TypeKind::TYPE_UINT_NATIVE) {
        ctx.def.Push8bytes(value.GetUnsignedVal());
    } else {
        ctx.def.Push(static_cast<unsigned>(value.GetUnsignedVal()));
    }
}

void CHIR2BCHIR::TranslateFloatValue(Context& ctx, const FloatLiteral& value)
{
    auto typeKind = value.GetType()->GetTypeKind();
    auto opcode = PrimitiveTypeKind2OpCode(typeKind);
    PushOpCodeWithAnnotations(ctx, opcode, value);
    if (typeKind == Type::TypeKind::TYPE_FLOAT64) {
        uint64_t tmp{0};
        double d = value.GetVal();
        auto ret = memcpy_s(&tmp, sizeof(tmp), &d, sizeof(d));
        if (ret != EOK) {
            CJC_ABORT();
        } else {
            ctx.def.Push8bytes(tmp);
        }
    } else {
        Bchir::ByteCodeContent tmp{0};
        float d = static_cast<float>(value.GetVal());
        auto ret = memcpy_s(&tmp, sizeof(tmp), &d, sizeof(d));
        if (ret != EOK) {
            CJC_ABORT();
        } else {
            ctx.def.Push(tmp);
        }
    }
    return;
}