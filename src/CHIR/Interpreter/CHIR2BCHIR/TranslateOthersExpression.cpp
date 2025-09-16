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
#include "cangjie/CHIR/Interpreter/Utils.h"
#include "cangjie/CHIR/Type/Type.h"
#include "cangjie/CHIR/Utils.h"

using namespace Cangjie::CHIR;
using namespace Interpreter;

void CHIR2BCHIR::TranslateOthersExpression(Context& ctx, const Expression& expr)
{
    switch (expr.GetExprKind()) {
        case ExprKind::DEBUGEXPR: {
            CJC_ASSERT(false);
            break;
        }
        case ExprKind::CONSTANT: {
            // Nothing to be done here. The literal was already encoded because it is the argument 0
            // of expr.
            break;
        }
        case ExprKind::TUPLE: {
            CJC_ASSERT(expr.GetNumOfOperands() > 0);
            CJC_ASSERT(expr.GetNumOfOperands() <= static_cast<size_t>(Bchir::BYTECODE_CONTENT_MAX));
            PushOpCodeWithAnnotations(ctx, OpCode::TUPLE, expr, static_cast<unsigned>(expr.GetNumOfOperands()));
            break;
        }
        case ExprKind::FIELD: {
            TranslateField(ctx, expr);
            break;
        }
        case ExprKind::APPLY: {
            CJC_ASSERT(expr.GetNumOfOperands() > 0);
            CJC_ASSERT(expr.GetNumOfOperands() <= static_cast<size_t>(Bchir::BYTECODE_CONTENT_MAX));
            TranslateApplyExpression(ctx, *StaticCast<const Apply*>(&expr));
            break;
        }
        case ExprKind::INVOKE: {
            TranslateInvoke(ctx, expr);
            break;
        }
        case ExprKind::INSTANCEOF: {
            CJC_ASSERT(expr.GetNumOfOperands() == 1);
            auto instanceOfExpr = StaticCast<const InstanceOf*>(&expr);
            TranslateInstanceOf(ctx, *instanceOfExpr);
            break;
        }
        case ExprKind::TYPECAST: {
            TranslateTypecast(ctx, expr);
            break;
        }
        case ExprKind::INTRINSIC: {
            TranslateIntrinsicExpression(ctx, *StaticCast<const Intrinsic*>(&expr));
            break;
        }
        case ExprKind::GET_EXCEPTION: {
            PushOpCodeWithAnnotations(ctx, OpCode::GET_EXCEPTION, expr);
            break;
        }
        case ExprKind::RAW_ARRAY_ALLOCATE: {
            PushOpCodeWithAnnotations(ctx, OpCode::ALLOCATE_RAW_ARRAY, expr);
            break;
        }
        case ExprKind::RAW_ARRAY_LITERAL_INIT: {
            CJC_ASSERT(expr.GetNumOfOperands() > 0); // array + arguments
            PushOpCodeWithAnnotations(
                ctx, OpCode::RAW_ARRAY_LITERAL_INIT, expr, static_cast<unsigned>(expr.GetNumOfOperands() - 1));
            break;
        }
        case ExprKind::RAW_ARRAY_INIT_BY_VALUE: {
            PushOpCodeWithAnnotations(ctx, OpCode::RAW_ARRAY_INIT_BY_VALUE, expr);
            break;
        }
        case ExprKind::VARRAY: {
            CJC_ASSERT(expr.GetNumOfOperands() < static_cast<size_t>(Bchir::BYTECODE_CONTENT_MAX));
            auto vArraySize = static_cast<Bchir::ByteCodeContent>(expr.GetNumOfOperands());
            PushOpCodeWithAnnotations(ctx, OpCode::VARRAY, expr, vArraySize);
            break;
        }
        case ExprKind::BOX: {
            CJC_ASSERT(expr.GetNumOfOperands() == 1);
            auto boxExpr = StaticCast<const Box*>(&expr);
            TranslateBox(ctx, *boxExpr);
            break;
        }
        case ExprKind::UNBOX: {
            CJC_ASSERT(expr.GetNumOfOperands() == 1);
            PushOpCodeWithAnnotations(ctx, OpCode::UNBOX, expr);
            break;
        }
        case ExprKind::UNBOX_TO_REF: {
            CJC_ASSERT(expr.GetNumOfOperands() == 1);
            PushOpCodeWithAnnotations(ctx, OpCode::UNBOX_REF, expr);
            break;
        }
        case ExprKind::VARRAY_BUILDER:
        case ExprKind::SPAWN:
        case ExprKind::INVOKESTATIC:
        case ExprKind::GET_RTTI:
        case ExprKind::GET_RTTI_STATIC:
        case ExprKind::TRANSFORM_TO_CONCRETE:
        case ExprKind::TRANSFORM_TO_GENERIC: {
            // We currently don't support these operations. If they are reached during interpretation
            // the interpreter will terminate with exception.
            PushOpCodeWithAnnotations(ctx, OpCode::ABORT, expr);
            break;
        }
        default: {
            // unreachable
            CJC_ASSERT(false);
            PushOpCodeWithAnnotations(ctx, OpCode::ABORT, expr);
        }
    }
}

void CHIR2BCHIR::TranslateField(Context& ctx, const Expression& expr)
{
    auto fieldExpr = StaticCast<const Field*>(&expr);
    auto indexes = fieldExpr->GetPath();
    CJC_ASSERT(indexes.size() > 0);
    if (indexes.size() == 1) {
        PushOpCodeWithAnnotations(ctx, OpCode::FIELD, expr, static_cast<unsigned>(indexes[0]));
    } else {
        CJC_ASSERT(fieldExpr->GetOperands()[0]->GetType()->IsStruct() ||
            fieldExpr->GetOperands()[0]->GetType()->IsEnum() || fieldExpr->GetOperands()[0]->GetType()->IsTuple());
        PushOpCodeWithAnnotations(ctx, OpCode::FIELD_TPL, expr, static_cast<Bchir::ByteCodeContent>(indexes.size()));
        for (auto i : indexes) {
            ctx.def.Push(static_cast<Bchir::ByteCodeContent>(i));
        }
    }
}

void CHIR2BCHIR::TranslateInvoke(Context& ctx, const Expression& expr)
{
    CJC_ASSERT(expr.GetNumOfOperands() > 0);
    CJC_ASSERT(expr.GetNumOfOperands() <= static_cast<size_t>(Bchir::BYTECODE_CONTENT_MAX));
    auto invokeExpr = StaticCast<const Invoke*>(&expr);
    auto idx = ctx.def.NextIndex();
    // we dont store mangled name here
    PushOpCodeWithAnnotations<false, true>(
        ctx, OpCode::INVOKE, expr, static_cast<unsigned>(expr.GetNumOfOperands()), 0);
    auto methodName = MangleMethodName<true>(invokeExpr->GetMethodName(), *invokeExpr->GetMethodType());
    ctx.def.AddMangledNameAnnotation(idx, methodName);
}

void CHIR2BCHIR::TranslateTypecast(Context& ctx, const Expression& expr)
{
    CJC_ASSERT(expr.GetNumOfOperands() == 1);
    auto typeCastExpr = StaticCast<const TypeCast*>(&expr);
    auto srcTy = typeCastExpr->GetSourceTy();
    auto dstTy = typeCastExpr->GetTargetTy();
    if (srcTy->IsPrimitive() && dstTy->IsPrimitive()) {
        auto srcTyIdx = srcTy->GetTypeKind();
        auto dstTyIdx = dstTy->GetTypeKind();
        auto overflowStrat = static_cast<Bchir::ByteCodeContent>(typeCastExpr->GetOverflowStrategy());
        PushOpCodeWithAnnotations(ctx, OpCode::TYPECAST, expr, srcTyIdx, dstTyIdx, overflowStrat);
    } else {
        CJC_ASSERT((!srcTy->IsPrimitive() && !dstTy->IsPrimitive()) ||
            (srcTy->IsEnum() && IsEnumSelectorType(*dstTy)) || (IsEnumSelectorType(*srcTy) && dstTy->IsEnum()));
    }
}

void CHIR2BCHIR::TranslateInstanceOf(Context& ctx, const InstanceOf& expr)
{
    auto opIdx = ctx.def.Size();
    CJC_ASSERT(opIdx <= static_cast<size_t>(Bchir::BYTECODE_CONTENT_MAX));
    PushOpCodeWithAnnotations<false>(ctx, OpCode::INSTANCEOF, expr);
    ctx.def.Push(0); // dummy value, this will be resolved during linking
    if (expr.GetType()->IsRef()) {
        auto refTy = StaticCast<const RefType*>(expr.GetType());
        auto classTy = StaticCast<const ClassType*>(refTy->GetBaseType());
        auto classDef = classTy->GetClassDef();
        ctx.def.AddMangledNameAnnotation(static_cast<unsigned>(opIdx), classDef->GetIdentifierWithoutPrefix());
    } else if (expr.GetType()->IsPrimitive()) {
        auto primitiveClassName = expr.GetType()->ToString();
        ctx.def.AddMangledNameAnnotation(static_cast<Bchir::ByteCodeIndex>(opIdx), primitiveClassName);
    } else {
        auto customTy = StaticCast<const CustomType*>(expr.GetType());
        auto customDef = customTy->GetCustomTypeDef();
        ctx.def.AddMangledNameAnnotation(
            static_cast<Bchir::ByteCodeIndex>(opIdx), customDef->GetIdentifierWithoutPrefix());
    }
}

void CHIR2BCHIR::TranslateBox(Context& ctx, const Box& expr)
{
    auto opIdx = ctx.def.Size();
    CJC_ASSERT(opIdx <= static_cast<size_t>(Bchir::BYTECODE_CONTENT_MAX));
    PushOpCodeWithAnnotations<false>(ctx, OpCode::BOX, expr, 0);
    auto ty = expr.GetSourceTy();
    if (ty->IsStruct()) {
        auto structTy = StaticCast<const StructType*>(ty);
        auto structDef = structTy->GetStructDef();
        ctx.def.AddMangledNameAnnotation(
            static_cast<Bchir::ByteCodeIndex>(opIdx), structDef->GetIdentifierWithoutPrefix());
    } else if (ty->IsEnum()) {
        auto enumTy = StaticCast<const EnumType*>(ty);
        auto enumDef = enumTy->GetEnumDef();
        ctx.def.AddMangledNameAnnotation(
            static_cast<Bchir::ByteCodeIndex>(opIdx), enumDef->GetIdentifierWithoutPrefix());
    } else { // this is a primitive type
        CJC_ASSERT(ty->IsPrimitive());
        ctx.def.AddMangledNameAnnotation(static_cast<Bchir::ByteCodeIndex>(opIdx), ty->ToString());
    }
}

void CHIR2BCHIR::TranslateCApplyExpression(Context& ctx, const Apply& apply, const Cangjie::CHIR::FuncType& funcTy)
{
    // bchir :: CAPPLY
    // :: CFUNC_NUMBER_OF_ARGS :: CFUNC_RESULT_TY :: CFUNC_ARG1_TY :: ... :: CFUNC_ARGN_TY
    // The number of funcTy and GetNumOfOperands
    size_t numberArgs = apply.GetNumOfOperands() - 1; // remove param 0 func;
    CJC_ASSERT(numberArgs == funcTy.GetParamTypes().size());
    CJC_ASSERT(numberArgs <= static_cast<size_t>(Bchir::BYTECODE_CONTENT_MAX));
    PushOpCodeWithAnnotations<false, true>(
        ctx, OpCode::CAPPLY, apply, static_cast<unsigned>(funcTy.GetParamTypes().size()));
    auto addTyAnnotation = [this, &ctx](CHIR::Type& type) { ctx.def.Push(GetTypeIdx(type)); };
    addTyAnnotation(*funcTy.GetReturnType());
    for (auto ty : funcTy.GetParamTypes()) {
        addTyAnnotation(*ty);
    }
}

void CHIR2BCHIR::TranslateApplyExpression(Context& ctx, const Apply& apply)
{
    auto operands = apply.GetOperands();
    auto funcExpr = operands[0];
    auto funcTy = funcExpr->GetType();
    if ((funcExpr->IsImportedFunc() && funcExpr->GetAttributeInfo().TestAttr(Attribute::FOREIGN)) ||
        funcExpr->GetSrcCodeIdentifier() == "std.core:CJ_CORE_CanUseSIMD") {
        // This is an hack. These functions should be intrinsic in CHIR 2.0. For the time being
        // we simply translate them as INTRINSIC1.
        auto it = syscall2IntrinsicKind.find(funcExpr->GetSrcCodeIdentifier());
        if (it != syscall2IntrinsicKind.end()) {
            // We use INTRINSIC1 instead of INTRINSIC0 so that we know that the dummy function node
            // needs to be popped from the argument stack. Revert changes once these functions are marked
            // as intrinsic in CHIR 2.0.
            PushOpCodeWithAnnotations<false, true>(ctx, OpCode::INTRINSIC1, apply, it->second, UINT32_MAX);
            return;
        }
        // bchir :: SYSCALL :: syscallName_STRING_IDX :: NUMBER_OF_ARGS
        // :: ANNOTATION_RESULT_TY :: ANNOTATION_ARG1_TY :: ... :: ANNOTATION_ARGN_TY
        auto strIdx = GetStringIdx(funcExpr->GetSrcCodeIdentifier());
        auto numberArgs = apply.GetNumOfOperands() - 1;
        CJC_ASSERT(numberArgs <= static_cast<size_t>(Bchir::BYTECODE_CONTENT_MAX));
        PushOpCodeWithAnnotations<false, true>(
            ctx, OpCode::SYSCALL, apply, strIdx, static_cast<Bchir::ByteCodeContent>(numberArgs));
        auto addTyAnnotation = [this, &ctx](CHIR::Type& type) { ctx.def.Push(GetTypeIdx(type)); };
        addTyAnnotation(*apply.GetResult()->GetType());
        // skip the first operand which is the function
        for (size_t i = 1; i < operands.size(); ++i) {
            addTyAnnotation(*operands[i]->GetType());
        }
        return;
    } else if (StaticCast<const FuncType&>(*funcTy).IsCFunc()) {
        TranslateCApplyExpression(ctx, apply, StaticCast<const FuncType&>(*funcTy));
        return;
    }
    PushOpCodeWithAnnotations<false, true>(ctx, OpCode::APPLY, apply, static_cast<unsigned>(apply.GetNumOfOperands()));
}

