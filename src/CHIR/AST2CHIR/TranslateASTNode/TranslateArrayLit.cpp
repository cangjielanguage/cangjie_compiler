// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"
#include "cangjie/CHIR/Expression/Terminator.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

namespace {
bool IsArrayEleTypePrimitive(const CHIR::Type& type)
{
    if (type.IsPrimitive() || type.IsVArray()) {
        return true;
    }
    if (type.IsStructArray()) {
        return IsArrayEleTypePrimitive(*StaticCast<StructType*>(&type)->GetGenericArgs()[0]);
    }
    if (type.IsTuple()) {
        auto eleTys = StaticCast<TupleType*>(&type)->GetElementTypes();
        CJC_ASSERT(eleTys.size() > 0);
        return std::all_of(
            eleTys.begin(), eleTys.end(), [](const Ptr<Type>& type) { return IsArrayEleTypePrimitive(*type); });
    }
    return false;
}
} // namespace

Ptr<Value> Translator::TranslateStructArray(const AST::ArrayLit& array)
{
    auto loc = TranslateLocation(array);

    std::vector<Value*> elements;
    auto arrayTy = StaticCast<StructType*>(chirTy.TranslateType(*array.ty));
    CJC_ASSERT(arrayTy->IsStructArray());
    auto eleTy = arrayTy->GetGenericArgs()[0];
    auto elementSize =
        CreateAndAppendConstantExpression<IntLiteral>(builder.GetInt64Ty(), *currentBlock, array.children.size())
            ->GetResult();
    auto rawArrayType = builder.GetType<RefType>(builder.GetType<RawArrayType>(eleTy, 1u));
    auto rawArrayRef = TryCreate<RawArrayAllocate>(currentBlock, loc, rawArrayType, eleTy, elementSize)->GetResult();
    // in cjdb, if the arrayLit is nested,e.g. [[1,2]]
    // the outer RawArrayAllocate must be generated earlier than inner RawArrayAllocate,
    // then the location of outer's RawArrayAllocate will come before inner's.
#ifdef CANGJIE_CODEGEN_CJNATIVE_BACKEND
    if (IsArrayEleTypePrimitive(*eleTy)) {
        for (auto& child : array.children) {
            auto element = TranslateExprArg(*child, *eleTy);
            if (child->TestAttr(AST::Attribute::NO_REFLECT_INFO)) {
                element->EnableAttr(Attribute::NO_REFLECT_INFO);
            }
            elements.push_back(element);
        }
        CreateAndAppendExpression<RawArrayLiteralInit>(builder.GetUnitTy(), rawArrayRef, elements, currentBlock);
    } else {
        // as for cjnative, if element's type is non-primitive type,
        // too many variables (aka `ele` below) before `RawArrayLiteralInit` may cause
        // `active variable analysis` stroke in backend (behind codegen), so here we use `GetElementRef + Store`
        // instead of `RawArrayLiteralInit`, thus the variable (`ele`) is used (by `Store`) as soon as
        // it is created and is not used after.
        for (size_t i = 0; i < array.children.size(); ++i) {
            auto& child = array.children[i];
            auto ele = TranslateExprArg(*child, *eleTy, false);
            if (child->TestAttr(AST::Attribute::NO_REFLECT_INFO)) {
                ele->EnableAttr(Attribute::NO_REFLECT_INFO);
            }
            CreateAndAppendExpression<StoreElementRef>(
                builder.GetUnitTy(), ele, rawArrayRef, std::vector<uint64_t>({static_cast<uint64_t>(i)}), currentBlock);
        }
    }
#endif
    auto initFn = GetSymbolTable(*array.initFunc);
    auto result =
        CreateAndAppendExpression<Allocate>(builder.GetType<RefType>(arrayTy), arrayTy, currentBlock)->GetResult();
    auto intExpr = CreateAndAppendConstantExpression<IntLiteral>(builder.GetInt64Ty(), *currentBlock, 0UL);
    std::vector<Value*> args = {result, rawArrayRef, intExpr->GetResult(), elementSize};
    // what are the initFn here all normal constructor or the arrayInitByFunc/arrayInitByCollection
    // check the thisType and instParentCustomDefTy
    std::vector<Type*> instParamTys;
    for (auto arg : args) {
        instParamTys.emplace_back(arg->GetType());
    }
    auto instantiedFuncTy = builder.GetType<FuncType>(instParamTys, builder.GetVoidTy());
    GenerateFuncCall(*initFn, instantiedFuncTy, {}, result->GetType(), args, loc);
    result = CreateAndAppendExpression<Load>(arrayTy, result, currentBlock)->GetResult();
    return result;
}

Ptr<Value> Translator::TranslateVArray(const AST::ArrayLit& array)
{
    auto loc = TranslateLocation(array);
    std::vector<Value*> elements;
    auto arrayTy = chirTy.TranslateType(*array.ty);
    CJC_ASSERT(arrayTy->IsVArray());
    auto eleTy = StaticCast<VArrayType*>(arrayTy)->GetElementType();
    for (auto& child : array.children) {
        elements.push_back(TranslateExprArg(*child, *eleTy));
    }
    return CreateAndAppendExpression<VArray>(loc, arrayTy, elements, currentBlock)->GetResult();
}

Ptr<Value> Translator::Visit(const AST::ArrayLit& array)
{
    auto ty = array.ty;
    // VArray
    if (ty->kind == AST::TypeKind::TYPE_VARRAY) {
        return TranslateVArray(array);
    }

    // Array
    if (ty->IsStructArray()) {
        return TranslateStructArray(array);
    }
    InternalError("Certainly won't come here in translating arrayLit.");
    return nullptr;
}
