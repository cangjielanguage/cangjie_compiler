// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Type/PrivateTypeConverter.h"

#include "cangjie/CHIR/Utils.h"
#include "cangjie/CHIR/Visitor/Visitor.h"
#include <functional>

namespace Cangjie::CHIR {
FuncType* TypeConverter::ConvertFuncParamsAndRetType(const FuncType& input)
{
    std::vector<Type*> newParamTys;
    for (auto oldParamTy : input.GetParamTypes()) {
        newParamTys.emplace_back(ConvertType(*oldParamTy));
    }
    auto newRetTy = ConvertType(*input.GetReturnType());
    return builder.GetType<FuncType>(newParamTys, newRetTy, input.HasVarArg(), input.IsCFunc());
}

Type* TypeConverter::ConvertType(Type& type)
{
    return converter(type);
}

void ExprTypeConverter::VisitExprDefaultImpl(Expression& o)
{
    if (o.result != nullptr) {
        VisitValue(*o.result);
    }
}

void ExprTypeConverter::VisitSubExpression(Allocate& o)
{
    VisitExprDefaultImpl(o);
    o.ty = ConvertType(*o.ty);
}

void ExprTypeConverter::VisitSubExpression(AllocateWithException& o)
{
    VisitExprDefaultImpl(o);
    o.ty = ConvertType(*o.ty);
}

void ExprTypeConverter::VisitSubExpression(InstanceOf& o)
{
    VisitExprDefaultImpl(o);
    o.ty = ConvertType(*o.ty);
}

void ExprTypeConverter::VisitSubExpression(RawArrayAllocate& o)
{
    VisitExprDefaultImpl(o);
    o.elementType = ConvertType(*o.elementType);
}

void ExprTypeConverter::VisitSubExpression(RawArrayAllocateWithException& o)
{
    VisitExprDefaultImpl(o);
    o.elementType = ConvertType(*o.elementType);
}

void ExprTypeConverter::VisitSubExpression(Apply& o)
{
    VisitExprDefaultImpl(o);
    for (auto& ty : o.instantiatedTypeArgs) {
        ty = converter(*ty);
    }
    if (o.thisType != nullptr) {
        o.thisType = ConvertType(*o.thisType);
    }
}

void ExprTypeConverter::VisitSubExpression(ApplyWithException& o)
{
    VisitExprDefaultImpl(o);
    for (auto& ty : o.instantiatedTypeArgs) {
        ty = converter(*ty);
    }
    if (o.thisType != nullptr) {
        o.thisType = ConvertType(*o.thisType);
    }
}

void ExprTypeConverter::VisitSubExpression(Invoke& o)
{
    VisitExprDefaultImpl(o);
    o.virMethodCtx.originalFuncType = ConvertFuncParamsAndRetType(*o.virMethodCtx.originalFuncType);
    o.thisType = ConvertType(*o.thisType);
    for (auto& ty : o.instantiatedTypeArgs) {
        ty = converter(*ty);
    }
}

void ExprTypeConverter::VisitSubExpression(InvokeWithException& o)
{
    VisitExprDefaultImpl(o);
    o.virMethodCtx.originalFuncType = ConvertFuncParamsAndRetType(*o.virMethodCtx.originalFuncType);
    o.thisType = ConvertType(*o.thisType);
    for (auto& ty : o.instantiatedTypeArgs) {
        ty = converter(*ty);
    }
}

void ExprTypeConverter::VisitSubExpression(InvokeStatic& o)
{
    VisitExprDefaultImpl(o);
    o.virMethodCtx.originalFuncType = ConvertFuncParamsAndRetType(*o.virMethodCtx.originalFuncType);
    o.thisType = ConvertType(*o.thisType);
    for (auto& ty : o.instantiatedTypeArgs) {
        ty = converter(*ty);
    }
}

void ExprTypeConverter::VisitSubExpression(InvokeStaticWithException& o)
{
    VisitExprDefaultImpl(o);
    o.virMethodCtx.originalFuncType = ConvertFuncParamsAndRetType(*o.virMethodCtx.originalFuncType);
    o.thisType = ConvertType(*o.thisType);
    for (auto& ty : o.instantiatedTypeArgs) {
        ty = converter(*ty);
    }
}

void ExprTypeConverter::VisitSubExpression(Constant& o)
{
    VisitExprDefaultImpl(o);
    VisitValue(*o.GetValue());
}

void ExprTypeConverter::VisitSubExpression(Intrinsic& o)
{
    VisitExprDefaultImpl(o);
    for (auto& ty : o.instantiatedTypeArgs) {
        ty = converter(*ty);
    }
}

void ExprTypeConverter::VisitSubExpression(IntrinsicWithException& o)
{
    VisitExprDefaultImpl(o);
    for (auto& ty : o.instantiatedTypeArgs) {
        ty = converter(*ty);
    }
}

void ExprTypeConverter::VisitSubExpression(GetInstantiateValue& o)
{
    VisitExprDefaultImpl(o);
    for (auto& ty : o.instantiateTys) {
        ty = ConvertType(*ty);
    }
}

void ExprTypeConverter::VisitSubExpression(Lambda& o)
{
    VisitExprDefaultImpl(o);
    o.funcTy = ConvertFuncParamsAndRetType(*o.funcTy);
    auto postVisit = [this](Expression& o) {
        VisitExpr(o);
        return VisitResult::CONTINUE;
    };
    for (auto& param : o.GetParams()) {
        VisitValue(*param);
    }
    // no need to transfer retValue, because ret Value has been contained in body
    Visitor::Visit(*o.GetBody(), [](Expression&) { return VisitResult::CONTINUE; }, postVisit);
}

void ExprTypeConverter::VisitSubExpression(GetRTTIStatic& o)
{
    VisitExprDefaultImpl(o);
    o.ty = ConvertType(*o.GetRTTIType());
}

void ValueTypeConverter::VisitValueDefaultImpl(Value& o)
{
    o.ty = ConvertType(*o.ty);
}

void ValueTypeConverter::VisitFuncBase(FuncBase& o)
{
    o.ty = ConvertFuncParamsAndRetType(*o.GetFuncType());
    auto srcFuncType = o.Get<OverrideSrcFuncType>();
    if (srcFuncType != nullptr) {
        o.Set<OverrideSrcFuncType>(ConvertFuncParamsAndRetType(*srcFuncType));
    }
    // convert generic type params
    for (auto& genericTypeParam : o.genericTypeParams) {
        genericTypeParam = StaticCast<GenericType*>(ConvertType(*genericTypeParam));
    }
}

void ValueTypeConverter::VisitSubValue(Func& o)
{
    VisitFuncBase(o);
    // convert param types
    for (auto param : o.GetParams()) {
        VisitValue(*param);
    }
}

void ValueTypeConverter::VisitSubValue(ImportedFunc& o)
{
    VisitFuncBase(o);
    // convert param types
    for (auto& param : o.paramInfo) {
        param.type = ConvertType(*param.type);
    }
}

void CustomDefTypeConverter::VisitDefDefaultImpl(CustomTypeDef& o)
{
    // extend def's type is nullptr
    if (o.type != nullptr) {
        o.type = StaticCast<CustomType*>(ConvertType(*o.type));
    }

    for (size_t i = 0; i < o.implementedInterfaceTys.size(); ++i) {
        o.implementedInterfaceTys[i] = StaticCast<ClassType*>(ConvertType(*o.implementedInterfaceTys[i]));
    }
    for (auto& var : o.instanceVars) {
        var.type = ConvertType(*var.type);
    }
    
    std::function<FuncType*(FuncType&)> func1 = [this](FuncType& ty) {
        return ConvertFuncParamsAndRetType(ty);
    };
    std::function<Type*(Type&)> func2 = [this](Type& ty) {
        return ConvertType(ty);
    };
    o.vtable.ConvertPrivateType(func1, func2);
}

void CustomDefTypeConverter::VisitSubDef(StructDef& o)
{
    VisitDefDefaultImpl(o);
}

void CustomDefTypeConverter::VisitSubDef(EnumDef& o)
{
    VisitDefDefaultImpl(o);
    for (auto& ctor : o.ctors) {
        ctor.funcType = ConvertFuncParamsAndRetType(*ctor.funcType);
    }
}

void CustomDefTypeConverter::VisitSubDef(ClassDef& o)
{
    VisitDefDefaultImpl(o);
    if (o.superClassTy != nullptr) {
        o.superClassTy = StaticCast<ClassType*>(ConvertType(*o.superClassTy));
    }
    for (auto& method : o.abstractMethods) {
        method.methodTy = ConvertFuncParamsAndRetType(*StaticCast<FuncType*>(method.methodTy));
        for (auto& param : method.paramInfos) {
            param.type = ConvertType(*param.type);
        }
    }
}

void CustomDefTypeConverter::VisitSubDef(ExtendDef& o)
{
    VisitDefDefaultImpl(o);
    o.extendedType = ConvertType(*o.extendedType);
    for (size_t i = 0; i < o.genericParams.size(); ++i) {
        o.genericParams[i] = StaticCast<GenericType*>(ConvertType(*o.genericParams[i]));
    }
}

void PrivateTypeConverterNoInvokeOriginal::VisitSubExpression(Invoke& o)
{
    VisitExprDefaultImpl(o);
    o.thisType = ConvertType(*o.thisType);
    for (auto& ty : o.instantiatedTypeArgs) {
        ty = ConvertType(*ty);
    }
}

void PrivateTypeConverterNoInvokeOriginal::VisitSubExpression(InvokeWithException& o)
{
    VisitExprDefaultImpl(o);
    o.thisType = ConvertType(*o.thisType);
    for (auto& ty : o.instantiatedTypeArgs) {
        ty = ConvertType(*ty);
    }
}

void PrivateTypeConverterNoInvokeOriginal::VisitSubExpression(InvokeStatic& o)
{
    VisitExprDefaultImpl(o);
    o.thisType = ConvertType(*o.thisType);
    for (auto& ty : o.instantiatedTypeArgs) {
        ty = ConvertType(*ty);
    }
}

void PrivateTypeConverterNoInvokeOriginal::VisitSubExpression(InvokeStaticWithException& o)
{
    VisitExprDefaultImpl(o);
    o.thisType = ConvertType(*o.thisType);
    for (auto& ty : o.instantiatedTypeArgs) {
        ty = ConvertType(*ty);
    }
}

Type* TypeConverterForCC::ConvertType(Type& type)
{
    if (type.IsRef()) {
        return builder.GetType<RefType>(ConvertType(*StaticCast<RefType&>(type).GetBaseType()));
    } else if (type.IsCJFunc()) {
        return funcConverter(type);
    } else {
        return converter(type);
    }
}

void TypeConverterForCC::VisitSubExpression(RawArrayAllocate& o)
{
    VisitExprDefaultImpl(o);
    o.elementType = converter(*o.elementType);
}

void TypeConverterForCC::VisitSubExpression(RawArrayAllocateWithException& o)
{
    VisitExprDefaultImpl(o);
    o.elementType = converter(*o.elementType);
}

void TypeConverterForCC::VisitSubValue(Func& o)
{
    if (o.GetSrcCodeIdentifier() == GENERIC_VIRTUAL_FUNC) {
        auto funcType = o.GetFuncType();
        std::vector<Type*> newParamTys;
        for (auto oldParamTy : funcType->GetParamTypes()) {
            /**
             *  func $GenericVirtualFunc(p: (Int64) -> Unit) {}
             *  we need to convert type of `p` to $AutoEnvGenericBase, not to $AutoEnvInstBase, because
             *  call site may be an `Invoke`, param type in method type is Generic-T,
             *  so we can pass an object with any func type, generic func type or inst func type, both ok.
             *  we don't need to convert to $AutoEnvGenericBase in $InstVirtualFunc, because its arg's type
             *  must be inst func type
            */
            if (oldParamTy->IsCJFunc()) {
                newParamTys.emplace_back(converter(*oldParamTy));
            } else {
                newParamTys.emplace_back(ConvertType(*oldParamTy));
            }
        }
        auto newRetTy = ConvertType(*funcType->GetReturnType());
        o.ty = builder.GetType<FuncType>(newParamTys, newRetTy, funcType->HasVarArg(), funcType->IsCFunc());
        // convert generic type params
        for (auto& genericTypeParam : o.genericTypeParams) {
            genericTypeParam = StaticCast<GenericType*>(ConvertType(*genericTypeParam));
        }

        // convert param types
        for (auto param : o.GetParams()) {
            VisitValue(*param);
        }
    } else {
        ValueTypeConverter::VisitSubValue(o);
    }
}

void TypeConverterForCC::VisitValueDefaultImpl(Value& o)
{
    if (o.IsParameter()) {
        if (auto func = StaticCast<Parameter&>(o).GetOwnerFunc(); func &&
            func->GetSrcCodeIdentifier() == GENERIC_VIRTUAL_FUNC && o.ty->IsCJFunc()) {
            o.ty = converter(*o.ty);
            return;
        }
    }
    ValueTypeConverter::VisitValueDefaultImpl(o);
}
} // namespace Cangjie::CHIR