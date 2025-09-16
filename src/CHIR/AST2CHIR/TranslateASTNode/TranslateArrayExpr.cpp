// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"
#include "cangjie/CHIR/Expression/Terminator.h"
#include "cangjie/CHIR/Type/Type.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;
using namespace Cangjie::AST;

constexpr static int ARGS_NUM_TWO = 2;

Ptr<Value> Translator::Visit(const AST::ArrayExpr& array)
{
    if (array.isValueArray) {
        CJC_ASSERT(array.args.size() == 1);
        CJC_ASSERT(!array.ty->typeArgs.empty());

        if (array.args[0]->ty->IsFunc()) {
            // Case A: "VArray<Int64, $5>({i => i})"
            return InitVArrayByLambda(array);
        } else {
            // Case B: "VArray<Int64, $5>(repeat: 0)"
            return InitVArrayByItem(array);
        }
    }

    CJC_ASSERT(array.ty->IsArray());

    // Case A: "RawArray<T>()" which initialize an empty array.
    if (array.args.empty()) {
        auto loc = TranslateLocation(array);
        auto arrayTy = chirTy.TranslateType(*array.ty);
        CJC_ASSERT(arrayTy->IsRef());
        auto eleTy = StaticCast<RawArrayType*>(StaticCast<CHIR::RefType*>(arrayTy)->GetBaseType())->GetElementType();
        auto sizeVal = CreateAndAppendConstantExpression<IntLiteral>(
            builder.GetInt64Ty(), *currentBlock, 0UL)->GetResult();
        auto rawArrayRef = TryCreate<RawArrayAllocate>(currentBlock, loc, arrayTy, eleTy, sizeVal)->GetResult();
        CreateAndAppendExpression<RawArrayLiteralInit>(
            builder.GetUnitTy(), rawArrayRef, std::vector<Value*>{}, currentBlock);
        return rawArrayRef;
    }

    // Case B: "RawArray<T>(collection)" which initialize an sized array with the `collection` content.
    if (array.args.size() == 1) {
        return InitArrayByCollection(array);
    }

    // Case C: "var x = RawArray<T>(size, val)" which initialize an sized array where the element is with `val`.
    CJC_ASSERT(array.args.size() == ARGS_NUM_TWO);
    if (array.initFunc == nullptr) {
        return InitArrayByItem(array);
    }

    // Case D: "var x = RawArray<T>(size, initFunc)" which initialize an sized array by specified init func.
    return InitArrayByLambda(array);
}

Expression* Translator::GenerateFuncCall(Value& callee, const FuncType* instantiedFuncTy,
    const std::vector<Type*> calleeInstTypeArgs, Type* thisTy,
    const std::vector<Value*>& args, DebugLocation loc)
{
    auto instantiatedParamTys = instantiedFuncTy->GetParamTypes();
    auto instantiatedRetTy = instantiedFuncTy->GetReturnType();

    // Step 1: for the func args, cast it to the corresponding func param type if necessary
    std::vector<Value*> castedArgs;
    // Except for the func with variant param length, args number should equal to params number
    CJC_ASSERT(args.size() == instantiatedParamTys.size() || instantiedFuncTy->HasVarArg());
    size_t i = 0;
    for (; i < instantiatedParamTys.size(); ++i) {
        auto castedArg = TypeCastOrBoxIfNeeded(*args[i], *instantiatedParamTys[i], INVALID_LOCATION);
        castedArgs.emplace_back(castedArg);
    }
    for (; i < args.size(); ++i) {
        castedArgs.emplace_back(args[i]);
    }

    // Step 2: create the func call (might be a `Apply` or `ApplyWithException`) and set
    // its instantiated type info
    // we should make the instantiated type info as a forced input of the constructor of `Apply`
    return TryCreate<Apply>(currentBlock, loc, instantiatedRetTy, &callee, FuncCallContext{
        .args = castedArgs,
        .instTypeArgs = calleeInstTypeArgs,
        .thisType = thisTy});
}

Ptr<Value> Translator::InitArrayByLambda(const AST::ArrayExpr& array)
{
    CJC_ASSERT(array.args.size() == ARGS_NUM_TWO && array.initFunc != nullptr);

    auto loc = TranslateLocation(array);
    auto arrayTy = chirTy.TranslateType(*array.ty);
    CJC_ASSERT(arrayTy->IsRef());
    auto eleTy = StaticCast<RawArrayType*>(StaticCast<CHIR::RefType*>(arrayTy)->GetBaseType())->GetElementType();
    auto sizeVal = TranslateExprArg(*array.args[0]);
    auto initFn = GetSymbolTable(*array.initFunc);
    auto rawArrayExpr = CreateAndAppendExpression<RawArrayAllocate>(loc, arrayTy, eleTy, sizeVal, currentBlock);
    auto rawArrayRef = rawArrayExpr->GetResult();

    std::vector<Type*> instantiatedTypeArgs;
    // if array init func is generic decl, then we will create `Apply` expr like: `Apply(init<xxx>, args)`
    // if array init func is instantiated decl, then we will create `Apply` expr like: `Apply(init, args)`
    if (array.initFunc->TestAttr(AST::Attribute::GENERIC)) {
        for (auto ty : array.ty->typeArgs) {
            instantiatedTypeArgs.emplace_back(chirTy.TranslateType(*ty));
        }
    }
    auto userInitFn = TranslateExprArg(*array.args[1]);
    // what are the initFn here all normal constructor or the arrayInitByFunc/arrayInitByCollection
    // check the thisType and instParentCustomDefTy
    std::vector<Type*> instParamTys;
    instParamTys.emplace_back(rawArrayRef->GetType());
    instParamTys.emplace_back(userInitFn->GetType());
    auto instantiedFuncTy = builder.GetType<FuncType>(instParamTys, arrayTy);
    GenerateFuncCall(*initFn, instantiedFuncTy, instantiatedTypeArgs, rawArrayRef->GetType(),
        std::vector<Value*>{rawArrayRef, userInitFn}, loc);

    return rawArrayRef;
}

Ptr<Value> Translator::InitArrayByItem(const AST::ArrayExpr& array)
{
    CJC_ASSERT(array.args.size() == ARGS_NUM_TWO && array.initFunc == nullptr);

    auto loc = TranslateLocation(array);
    auto arrayTy = chirTy.TranslateType(*array.ty);
    CJC_ASSERT(arrayTy->IsRef());
    auto eleTy = StaticCast<RawArrayType*>(StaticCast<CHIR::RefType*>(arrayTy)->GetBaseType())->GetElementType();

    auto sizeVal = TranslateExprArg(*array.args[0]);
    auto initVal = TranslateExprArg(*array.args[1]);
    auto rawArrayRef =
        CreateAndAppendExpression<RawArrayAllocate>(loc, arrayTy, eleTy, sizeVal, currentBlock)->GetResult();
    CreateAndAppendExpression<RawArrayInitByValue>(
        loc, builder.GetUnitTy(), rawArrayRef, sizeVal, initVal, currentBlock);
    return rawArrayRef;
}

CHIR::Type* Translator::GetExactParentType(
    Type& fuzzyParentType, const AST::FuncDecl& resolvedFunction, FuncType& funcType,
    std::vector<Type*>& funcInstTypeArgs, bool checkAbstractMethod, [[maybe_unused]] bool report)
{
    if (fuzzyParentType.IsNothing()) {
        return &fuzzyParentType;
    }
    auto outerDecl = resolvedFunction.outerDecl;
    CJC_NULLPTR_CHECK(outerDecl);
    if (outerDecl->TestAttr(AST::Attribute::GENERIC_INSTANTIATED)) {
        Type* parentTy = nullptr;
        if (outerDecl->astKind == AST::ASTKind::EXTEND_DECL) {
            parentTy = TranslateType(*StaticCast<AST::ExtendDecl*>(outerDecl)->extendedType->ty);
        } else {
            parentTy = TranslateType(*outerDecl->ty);
        }
        return parentTy->StripAllRefs();
    }
    
    auto funcName = resolvedFunction.identifier.Val();
    auto isStatic = resolvedFunction.TestAttr(AST::Attribute::STATIC);
    CHIR::Type* result = nullptr;
    if (auto genericTy = DynamicCast<GenericType*>(&fuzzyParentType)) {
        auto& upperBounds = genericTy->GetUpperBounds();
        CJC_ASSERT(!upperBounds.empty());
        for (auto upperBound : upperBounds) {
            ClassType* upperClassType = StaticCast<ClassType*>(StaticCast<RefType*>(upperBound)->GetBaseType());
            result = GetExactParentType(
                *upperClassType, resolvedFunction, funcType, funcInstTypeArgs, checkAbstractMethod, false);
            if (result != nullptr) {
                break;
            }
        }
    } else if (auto classTy = DynamicCast<CustomType*>(&fuzzyParentType)) {
        result =
            classTy->GetExactParentType(funcName, funcType, isStatic, funcInstTypeArgs, builder, checkAbstractMethod);
    } else {
        std::unordered_map<const GenericType*, Type*> replaceTable;
        auto classInstArgs = fuzzyParentType.GetTypeArgs();
        auto extendDefs = fuzzyParentType.GetExtends(&builder);
        CJC_ASSERT(!extendDefs.empty());
        // extend def
        for (auto ex : extendDefs) {
            auto classGenericArgs = ex->GetExtendedType()->GetTypeArgs();
            CJC_ASSERT(classInstArgs.size() == classGenericArgs.size());
            for (size_t i = 0; i < classInstArgs.size(); ++i) {
                if (auto genericTy1 = DynamicCast<GenericType*>(classGenericArgs[i])) {
                    replaceTable.emplace(genericTy1, classInstArgs[i]);
                }
            }
            auto [func, done] = ex->GetExpectedFunc(
                funcName, funcType, isStatic, replaceTable, funcInstTypeArgs, builder, checkAbstractMethod);
            // when it's done, don't need to check `func` is nullptr, it can't be
            if (done && func->Get<WrappedRawMethod>() == nullptr) {
                return ReplaceRawGenericArgType(*ex->GetExtendedType(), replaceTable, builder);
            }
        }
        // extend def's super interface
        for (auto ex : extendDefs) {
            for (auto ty : ex->GetImplementedInterfaceTys()) {
                result = ty->GetExactParentType(
                    funcName, funcType, isStatic, funcInstTypeArgs, builder, checkAbstractMethod);
                if (result != nullptr) {
                    return result;
                }
            }
        }
    }

#ifndef NDEBUG
    if (report && result == nullptr) {
        const std::string msg = "can't find '" + funcName + "': " + funcType.ToString() + ", in " +
            fuzzyParentType.ToString() + " related CustomTypeDef";
        Errorln(msg);
        for (auto tmp : builder.GetCurPackage()->GetClasses()) {
            std::cout << tmp->ToString() << std::endl;
        }
        for (auto tmp : builder.GetCurPackage()->GetStructs()) {
            std::cout << tmp->ToString() << std::endl;
        }
        for (auto tmp : builder.GetCurPackage()->GetEnums()) {
            std::cout << tmp->ToString() << std::endl;
        }
        for (auto tmp : builder.GetCurPackage()->GetExtends()) {
            std::cout << tmp->ToString() << std::endl;
        }
        for (auto tmp : builder.GetCurPackage()->GetImportedClasses()) {
            std::cout << tmp->ToString() << std::endl;
        }
        for (auto tmp : builder.GetCurPackage()->GetImportedStructs()) {
            std::cout << tmp->ToString() << std::endl;
        }
        for (auto tmp : builder.GetCurPackage()->GetImportedEnums()) {
            std::cout << tmp->ToString() << std::endl;
        }
        for (auto tmp : builder.GetCurPackage()->GetImportedExtends()) {
            std::cout << tmp->ToString() << std::endl;
        }
        CJC_ABORT();
    }
#endif
    return result;
}

std::vector<VTableSearchRes> Translator::GetFuncIndexInVTable(
    Type& root, const FuncCallType& funcCallType, bool isStatic, [[maybe_unused]] bool report)
{
    auto result = CHIR::GetFuncIndexInVTable(root, funcCallType, isStatic, builder);
#ifndef NDEBUG
    if (report && result.empty()) {
        const std::string msg = "can't find '" + funcCallType.funcName + "': " +
            funcCallType.funcType->ToString() + ", in " + root.ToString() + " vtable.";
        Errorln(msg);
        for (auto tmp : builder.GetCurPackage()->GetClasses()) {
            std::cout << tmp->ToString() << std::endl;
        }
        for (auto tmp : builder.GetCurPackage()->GetStructs()) {
            std::cout << tmp->ToString() << std::endl;
        }
        for (auto tmp : builder.GetCurPackage()->GetEnums()) {
            std::cout << tmp->ToString() << std::endl;
        }
        for (auto tmp : builder.GetCurPackage()->GetExtends()) {
            std::cout << tmp->ToString() << std::endl;
        }
        CJC_ABORT();
    }
#endif
    return result;
}

Ptr<Value> Translator::InitArrayByCollection(const AST::ArrayExpr& array)
{
    auto loc = TranslateLocation(array);
    auto arrayTy = chirTy.TranslateType(*array.ty);
    CJC_ASSERT(arrayTy->IsRef());
    auto eleTy = StaticCast<RawArrayType*>(StaticCast<CHIR::RefType*>(arrayTy)->GetBaseType())->GetElementType();

    auto collection = TranslateExprArg(*array.args[0]);
    auto sizeTy = builder.GetInt64Ty();

    auto sizeGetInstFuncTy = builder.GetType<FuncType>(std::vector<Type*>({collection->GetType()}), sizeTy);
    auto collectionDerefType = StaticCast<RefType*>(collection->GetType())->GetBaseType();
    auto funcName = "$sizeget";
    FuncCallType funcCallType{funcName, sizeGetInstFuncTy};
    auto vtableRes =
        GetFuncIndexInVTable(*collectionDerefType, funcCallType, false)[0];
    InstInvokeCalleeInfo funcInfo{funcName, sizeGetInstFuncTy, vtableRes.originalFuncType, vtableRes.instSrcParentType,
        StaticCast<ClassType*>(vtableRes.instSrcParentType->GetCustomTypeDef()->GetType()), std::vector<Type*>{},
        collectionDerefType, vtableRes.offset};
    Value* sizeVal = GenerateDynmaicDispatchFuncCall(funcInfo, std::vector<Value*>{}, collection)->GetResult();

    // Create the array `RawArrayAllocate(eleTy, collection.size)`
    auto rawArrayRef =
        CreateAndAppendExpression<RawArrayAllocate>(loc, arrayTy, eleTy, sizeVal, currentBlock)->GetResult();

    // Call the `Core::arrayInitByCollection` to set the array element value
    CJC_NULLPTR_CHECK(array.initFunc);
    auto initFn = GetSymbolTable(*array.initFunc);
    // what are the initFn here all normal constructor or the arrayInitByFunc/arrayInitByCollection
    // check the thisType and instParentCustomDefTy
    std::vector<Type*> instParamTys;
    instParamTys.emplace_back(rawArrayRef->GetType());
    instParamTys.emplace_back(collection->GetType());
    auto instantiedFuncTy = builder.GetType<FuncType>(instParamTys, arrayTy);
    // if array init func is generic decl, then we will create `Apply` expr like: `Apply(init<xxx>, args)`
    // if array init func is instantiated decl, then we will create `Apply` expr like: `Apply(init, args)`
    auto instTys = array.initFunc->TestAttr(AST::Attribute::GENERIC) ? std::vector<Type*>{eleTy} : std::vector<Type*>{};
    GenerateFuncCall(*initFn, instantiedFuncTy, instTys, rawArrayRef->GetType(),
        std::vector<Value*>{rawArrayRef, collection}, loc);

    return rawArrayRef;
}

Ptr<Value> Translator::InitVArrayByItem(const AST::ArrayExpr& vArray)
{
    auto loc = TranslateLocation(vArray);
    auto vArrayTy = StaticCast<VArrayType*>(chirTy.TranslateType(*vArray.ty));
    auto eleTy = vArrayTy->GetElementType();

    auto sizeVal =
        CreateAndAppendConstantExpression<IntLiteral>(builder.GetInt64Ty(), *currentBlock, vArrayTy->GetSize())
            ->GetResult();
    auto valArg = vArray.args[0].get();
    auto val = TranslateExprArg(*valArg);

    // todo: optimize if val is constant
    auto fnTy = builder.GetType<FuncType>(std::vector<Type*>({builder.GetInt64Ty()}), eleTy);
    auto nullFn = CreateAndAppendConstantExpression<NullLiteral>(fnTy, *currentBlock)->GetResult();
    return CreateAndAppendExpression<VArrayBuilder>(loc, vArrayTy, sizeVal, val, nullFn, currentBlock)->GetResult();
}

Ptr<Value> Translator::InitVArrayByLambda(const AST::ArrayExpr& vArray)
{
    auto loc = TranslateLocation(vArray);
    auto vArrayTy = StaticCast<VArrayType*>(chirTy.TranslateType(*vArray.ty));
    auto eleTy = vArrayTy->GetElementType();
    auto sizeVal =
        CreateAndAppendConstantExpression<IntLiteral>(builder.GetInt64Ty(), *currentBlock, vArrayTy->GetSize())
            ->GetResult();
    auto initFn = TranslateExprArg(*vArray.args[0]);
    auto nullItem = CreateAndAppendConstantExpression<NullLiteral>(eleTy, *currentBlock)->GetResult();
    return CreateAndAppendExpression<VArrayBuilder>(loc, vArrayTy, sizeVal, nullItem, initFn, currentBlock)
        ->GetResult();
}
