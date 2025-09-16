// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

Ptr<LiteralValue> Translator::TranslateLitConstant(const AST::LitConstExpr& expr, AST::Ty& realTy)
{
    auto chirTyToTrans = TranslateType(realTy);
    switch (realTy.kind) {
        case AST::TypeKind::TYPE_FLOAT16:
        case AST::TypeKind::TYPE_FLOAT64:
        case AST::TypeKind::TYPE_IDEAL_FLOAT: {
            auto value = static_cast<double>(expr.constNumValue.asFloat.value);
            return builder.CreateLiteralValue<FloatLiteral>(chirTyToTrans, value);
        }
        case AST::TypeKind::TYPE_FLOAT32: {
            auto stringVal = expr.stringValue;
            stringVal.erase(std::remove(stringVal.begin(), stringVal.end(), '_'), stringVal.end());
            double value = static_cast<float>(strtold(stringVal.c_str(), nullptr));
            return builder.CreateLiteralValue<FloatLiteral>(chirTyToTrans, value);
        }
        case AST::TypeKind::TYPE_UINT8:
        case AST::TypeKind::TYPE_UINT16:
        case AST::TypeKind::TYPE_UINT32:
        case AST::TypeKind::TYPE_UINT64:
        case AST::TypeKind::TYPE_UINT_NATIVE: {
            return builder.CreateLiteralValue<IntLiteral>(chirTyToTrans, expr.constNumValue.asInt.Uint64());
        }
        case AST::TypeKind::TYPE_INT8:
        case AST::TypeKind::TYPE_INT16:
        case AST::TypeKind::TYPE_INT32:
        case AST::TypeKind::TYPE_INT64:
        case AST::TypeKind::TYPE_INT_NATIVE:
        case AST::TypeKind::TYPE_IDEAL_INT: {
            return builder.CreateLiteralValue<IntLiteral>(
                chirTyToTrans, static_cast<uint64_t>(expr.constNumValue.asInt.Int64()));
        }
        case AST::TypeKind::TYPE_RUNE: {
            return builder.CreateLiteralValue<RuneLiteral>(chirTyToTrans, expr.codepoint[0]);
        }
        case AST::TypeKind::TYPE_BOOLEAN: {
            return builder.CreateLiteralValue<BoolLiteral>(chirTyToTrans, expr.stringValue == "true");
        }
        case AST::TypeKind::TYPE_STRUCT: {
            CJC_ASSERT(expr.kind == AST::LitConstKind::STRING);
            return builder.CreateLiteralValue<StringLiteral>(chirTyToTrans, expr.stringValue);
        }
        // Unit literal is handled in TranslateExprArg etc. functions
        case AST::TypeKind::TYPE_UNIT:
        default: {
            CJC_ABORT();
            return nullptr;
        }
    }
}

Ptr<Constant> Translator::TranslateLitConstant(const AST::LitConstExpr& expr, AST::Ty& realTy, Ptr<Block> block)
{
    auto loc = TranslateLocation(expr);
    auto chirTyToTrans = TranslateType(realTy);
    switch (realTy.kind) {
        case AST::TypeKind::TYPE_UNIT: {
            return nullptr;
        }
        case AST::TypeKind::TYPE_FLOAT16:
        case AST::TypeKind::TYPE_FLOAT64:
        case AST::TypeKind::TYPE_IDEAL_FLOAT: {
            auto value = static_cast<double>(expr.constNumValue.asFloat.value);
            return CreateAndAppendConstantExpression<FloatLiteral>(loc, chirTyToTrans, *block, value);
        }
        case AST::TypeKind::TYPE_FLOAT32: {
            auto stringVal = expr.stringValue;
            stringVal.erase(std::remove(stringVal.begin(), stringVal.end(), '_'), stringVal.end());
            double value = static_cast<float>(strtold(stringVal.c_str(), nullptr));
            return CreateAndAppendConstantExpression<FloatLiteral>(loc, chirTyToTrans, *block, value);
        }
        case AST::TypeKind::TYPE_UINT8:
        case AST::TypeKind::TYPE_UINT16:
        case AST::TypeKind::TYPE_UINT32:
        case AST::TypeKind::TYPE_UINT64:
        case AST::TypeKind::TYPE_UINT_NATIVE: {
            uint64_t value = expr.constNumValue.asInt.Uint64();
            return CreateAndAppendConstantExpression<IntLiteral>(loc, chirTyToTrans, *block, value);
        }
        case AST::TypeKind::TYPE_INT8:
        case AST::TypeKind::TYPE_INT16:
        case AST::TypeKind::TYPE_INT32:
        case AST::TypeKind::TYPE_INT64:
        case AST::TypeKind::TYPE_INT_NATIVE:
        case AST::TypeKind::TYPE_IDEAL_INT: {
            int64_t value = expr.constNumValue.asInt.Int64();
            return CreateAndAppendConstantExpression<IntLiteral>(
                loc, chirTyToTrans, *block, static_cast<uint64_t>(value));
        }
        case AST::TypeKind::TYPE_RUNE: {
            auto value = expr.codepoint[0];
            return CreateAndAppendConstantExpression<RuneLiteral>(loc, chirTyToTrans, *block, value);
        }
        case AST::TypeKind::TYPE_BOOLEAN: {
            auto value = expr.stringValue == "true";
            return CreateAndAppendConstantExpression<BoolLiteral>(loc, chirTyToTrans, *block, value);
        }
        case AST::TypeKind::TYPE_STRUCT: {
            CJC_ASSERT(expr.kind == AST::LitConstKind::STRING);
            auto value = expr.stringValue;
            return CreateAndAppendConstantExpression<StringLiteral>(loc, chirTyToTrans, *block, value);
        }
        default: {
            CJC_ABORT();
            return nullptr;
        }
    }
}

Ptr<Value> Translator::Visit(const AST::LitConstExpr& expr)
{
    Constant* c = TranslateLitConstant(expr, *expr.ty, currentBlock);
    if (!c) {
        return nullptr;
    }
    return c->GetResult();
}
