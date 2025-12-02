// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/AST/Utils.h"
#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"
#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/CHIR/Utils/ConstantUtils.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/Mangle/CHIRManglingUtils.h"

using namespace Cangjie::AST;
using namespace Cangjie::CHIR;
using namespace Cangjie;

Translator::LeftValueInfo Translator::TranslateThisOrSuperRefAsLeftValue(const AST::RefExpr& refExpr)
{
    CJC_ASSERT(refExpr.isThis || refExpr.isSuper);
    auto curFunc = GetCurrentFunc();
    CJC_ASSERT(curFunc);
    auto thisParam = GetImplicitThisParam();
    if (refExpr.isSuper) {
        auto superTy = TranslateType(*refExpr.ty);
        auto loc = TranslateLocation(refExpr);
        thisParam = TypeCastOrBoxIfNeeded(*thisParam, *superTy, loc);
    }
    return LeftValueInfo(thisParam, {});
}

Translator::LeftValueInfo Translator::TranslateStructMemberVarRefAsLeftValue(const AST::RefExpr& refExpr) const
{
    auto target = refExpr.ref.target;
    CJC_ASSERT(target->astKind == ASTKind::VAR_DECL);
    CJC_ASSERT(target->outerDecl->astKind == ASTKind::STRUCT_DECL);

    auto implicitThis = GetImplicitThisParam();
    auto name = StaticCast<VarDecl*>(target)->identifier.Val();
    CJC_ASSERT(!name.empty());
    return LeftValueInfo(implicitThis, {name});
}

Translator::LeftValueInfo Translator::TranslateClassMemberVarRefAsLeftValue(const AST::RefExpr& refExpr) const
{
    auto target = refExpr.ref.target;
    CJC_ASSERT(target->astKind == ASTKind::VAR_DECL);
    CJC_ASSERT(target->outerDecl->astKind == ASTKind::CLASS_DECL);

    auto implicitThis = GetImplicitThisParam();
    CJC_ASSERT(implicitThis->GetType()->IsReferenceTypeWithRefDims(1));
    auto name = StaticCast<VarDecl*>(target)->identifier.Val();
    CJC_ASSERT(!name.empty());
    return LeftValueInfo(implicitThis, {name});
}

Translator::LeftValueInfo Translator::TranslateEnumMemberVarRef(const AST::RefExpr& refExpr)
{
    auto target = refExpr.ref.target;
    CJC_ASSERT(target->astKind == ASTKind::VAR_DECL);
    CJC_ASSERT(target->outerDecl->astKind == ASTKind::ENUM_DECL);
    CJC_ASSERT(target->TestAttr(AST::Attribute::ENUM_CONSTRUCTOR));
    auto loc = TranslateLocation(refExpr);

    // polish here
    auto enumTy = StaticCast<EnumTy*>(refExpr.ty);
    auto enumType = StaticCast<EnumType*>(chirTy.TranslateType(*enumTy));
    uint64_t enumId = GetEnumCtorId(*target);
    auto selectorTy = GetSelectorType(*enumTy);
    if (enumTy->decl->hasArguments) {
        std::vector<Value*> args;
        if (selectorTy->IsBoolean()) {
            auto boolExpr = CreateAndAppendConstantExpression<BoolLiteral>(
                selectorTy, *currentBlock, static_cast<bool>(enumId));
            args.emplace_back(boolExpr->GetResult());
        } else {
            auto intExpr = CreateAndAppendConstantExpression<IntLiteral>(selectorTy, *currentBlock, enumId);
            args.emplace_back(intExpr->GetResult());
        }
        auto tupleExpr = CreateAndAppendExpression<Tuple>(loc, enumType, args, currentBlock);
        return LeftValueInfo(tupleExpr->GetResult(), {});
    } else {
        auto intExpr = CreateAndAppendConstantExpression<IntLiteral>(loc, selectorTy, *currentBlock, enumId);
        auto castedIntExpr = TypeCastOrBoxIfNeeded(*intExpr->GetResult(), *enumType, loc);
        return LeftValueInfo(castedIntExpr, {});
    }
}

Translator::LeftValueInfo Translator::TranslateVarRefAsLeftValue(const AST::RefExpr& refExpr)
{
    auto target = refExpr.ref.target;
    CJC_ASSERT(target);
    CJC_ASSERT(target->astKind == AST::ASTKind::VAR_DECL || target->astKind == AST::ASTKind::FUNC_PARAM);

    // Case 1: non static member variable
    if (target->outerDecl != nullptr && !target->TestAttr(AST::Attribute::STATIC)) {
        // Case 2.1: non static member variable in struct
        if (target->outerDecl->astKind == AST::ASTKind::STRUCT_DECL) {
            return TranslateStructMemberVarRefAsLeftValue(refExpr);
        }
        // Case 2.2: non static member variable in class
        if (target->outerDecl->astKind == AST::ASTKind::CLASS_DECL) {
            return TranslateClassMemberVarRefAsLeftValue(refExpr);
        }
        // Case 2.3: case variable in enum
        if (target->outerDecl->astKind == AST::ASTKind::ENUM_DECL) {
            return TranslateEnumMemberVarRef(refExpr);
        }
    }

    // Case 2: global var, static member var, local var, func param
    auto val = GetSymbolTable(*target);
    return LeftValueInfo(val, {});
}

Translator::LeftValueInfo Translator::TranslateRefExprAsLeftValue(const AST::RefExpr& refExpr)
{
    // Case 1: `this` or `super`
    if (refExpr.isThis || refExpr.isSuper) {
        return TranslateThisOrSuperRefAsLeftValue(refExpr);
    }
    // Case 2: variable
    auto target = refExpr.ref.target;
    CJC_ASSERT(target);
    if (target->astKind == AST::ASTKind::VAR_DECL || target->astKind == AST::ASTKind::FUNC_PARAM) {
        return TranslateVarRefAsLeftValue(refExpr);
    }
    CJC_ABORT();
    return LeftValueInfo(nullptr, {});
}

Value* Translator::TranslateThisOrSuperRef(const AST::RefExpr& refExpr)
{
    CJC_ASSERT(refExpr.isThis || refExpr.isSuper);
    auto loc = TranslateLocation(refExpr);
    auto thisLeftValueInfo = TranslateThisOrSuperRefAsLeftValue(refExpr);
    CJC_ASSERT(thisLeftValueInfo.path.empty());
    auto thisLeftValueBase = thisLeftValueInfo.base;
    auto thisLeftValueBaseTy = thisLeftValueInfo.base->GetType();
    CJC_ASSERT(thisLeftValueBaseTy->GetRefDims() <= 1);
    if (thisLeftValueBaseTy->IsValueOrGenericTypeWithRefDims(1)) {
        auto loadThis = CreateAndAppendExpression<Load>(
            loc, StaticCast<RefType*>(thisLeftValueBaseTy)->GetBaseType(), thisLeftValueBase, currentBlock);
        return loadThis->GetResult();
    }
    return thisLeftValueBase;
}

Value* Translator::TranslateVarRef(const AST::RefExpr& refExpr)
{
    auto target = refExpr.ref.target;
    CJC_ASSERT(target);
    CJC_ASSERT(target->astKind == AST::ASTKind::VAR_DECL || target->astKind == AST::ASTKind::FUNC_PARAM);
    auto loc = TranslateLocation(refExpr);

    auto varLeftValueInfo = TranslateVarRefAsLeftValue(refExpr);
    auto varLeftValueBase = varLeftValueInfo.base;
    auto varLeftValueBaseTy = varLeftValueInfo.base->GetType();

    // Case 1 non static member variables
    if (target->outerDecl != nullptr && !target->TestAttr(AST::Attribute::STATIC)) {
        auto path = varLeftValueInfo.path;

        // Case 1.1: non static member variables in struct or class
        if (target->outerDecl->astKind == AST::ASTKind::STRUCT_DECL ||
            target->outerDecl->astKind == AST::ASTKind::CLASS_DECL) {
            auto thisCustomType = StaticCast<CustomType*>(varLeftValueBaseTy->StripAllRefs());
            CJC_ASSERT(varLeftValueBaseTy->IsReferenceTypeWithRefDims(1) ||
                varLeftValueBaseTy->IsValueOrGenericTypeWithRefDims(1) ||
                varLeftValueBaseTy->IsValueOrGenericTypeWithRefDims(0));
            if (varLeftValueBaseTy->IsReferenceTypeWithRefDims(1) ||
                varLeftValueBaseTy->IsValueOrGenericTypeWithRefDims(1)) {
                auto getMemberRef =
                    CreateGetElementRefWithPath(loc, varLeftValueBase, path, currentBlock, *thisCustomType);
                auto memberType = StaticCast<RefType*>(getMemberRef->GetType())->GetBaseType();
                auto loadMemberValue = CreateAndAppendExpression<Load>(loc, memberType, getMemberRef, currentBlock);
                return loadMemberValue->GetResult();
            } else if (varLeftValueBaseTy->IsValueOrGenericTypeWithRefDims(0)) {
                auto memberType = GetInstMemberTypeByName(*thisCustomType, path, builder);
                auto getField =
                    CreateAndAppendExpression<FieldByName>(loc, memberType, varLeftValueBase, path, currentBlock);
                return getField->GetResult();
            }
        }
        // Case 1.2: variable case in enum
        if (target->outerDecl->astKind == AST::ASTKind::ENUM_DECL) {
            CJC_ASSERT(path.empty());
            return varLeftValueBase;
        }
    }
    // Case 2: global var, static member var, local var, func param
    CJC_ASSERT(varLeftValueInfo.path.empty());
    if (varLeftValueBaseTy->IsReferenceTypeWithRefDims(CLASS_REF_DIM) ||
        varLeftValueBaseTy->IsValueOrGenericTypeWithRefDims(1)) {
        auto loadVarRightValue = CreateAndAppendExpression<Load>(
            loc, StaticCast<RefType*>(varLeftValueBaseTy)->GetBaseType(), varLeftValueBase, currentBlock);
        return loadVarRightValue->GetResult();
    }
    CJC_ASSERT(varLeftValueBaseTy->IsReferenceTypeWithRefDims(1) ||
        varLeftValueBaseTy->IsValueOrGenericTypeWithRefDims(0));
    return varLeftValueBase;
}

InvokeCallContext Translator::GenerateInvokeCallContext(const InstCalleeInfo& instFuncType, Value& caller,
    const AST::FuncDecl& callee, const std::vector<Value*>& args, const OverflowStrategy strategy)
{
    auto tempDecl = typeManager.GetTopOverriddenFuncDecl(&callee);
    const AST::FuncDecl* originalFuncDecl = tempDecl ? tempDecl.get() : &callee;
    auto originalFuncType = StaticCast<FuncType*>(TranslateType(*originalFuncDecl->ty));
    if (!originalFuncDecl->TestAttr(AST::Attribute::STATIC)) {
        auto outerDecl = originalFuncDecl->outerDecl;
        CJC_NULLPTR_CHECK(outerDecl);
        auto parentType = GetNominalSymbolTable(*outerDecl)->GetType();
        parentType = AddRefIfFuncIsMutOrClass(*parentType, *originalFuncDecl, builder);
        auto paramTypes = originalFuncType->GetParamTypes();
        paramTypes.insert(paramTypes.begin(), parentType);
        originalFuncType = builder.GetType<FuncType>(paramTypes, originalFuncType->GetReturnType());
    }
    std::vector<GenericType*> originalGenericTypeParams;
    if (originalFuncDecl->TestAttr(AST::Attribute::GENERIC)) {
        for (const auto& genericTy : originalFuncDecl->funcBody->generic->typeParameters) {
            originalGenericTypeParams.emplace_back(StaticCast<GenericType*>(TranslateType(*(genericTy->ty))));
        }
    }
    auto funcName = originalFuncDecl->identifier.Val();
    if (IsOverflowOpCall(*originalFuncDecl)) {
        funcName = OverflowStrategyPrefix(strategy) + funcName;
    }
    
    auto invokeInfo = InvokeCallContext {
        .caller = &caller,
        .funcCallCtx = FuncCallContext {
            .args = args,
            .instTypeArgs = instFuncType.instantiatedTypeArgs,
            .thisType = instFuncType.thisType
        },
        .virMethodCtx = VirMethodContext {
            .srcCodeIdentifier = funcName,
            .originalFuncType = originalFuncType,
            .genericTypeParams = originalGenericTypeParams
        }
    };
    return invokeInfo;
}

Value* Translator::WrapMemberMethodByLambda(
    const AST::FuncDecl& funcDecl, const InstCalleeInfo& instFuncType, Value* thisObj)
{
    // create Lambda
    auto topLevelFunc = currentBlock->GetTopLevelFunc();
    CJC_NULLPTR_CHECK(topLevelFunc);
    auto lambdaMangledName =
        CHIRMangling::GenerateLambdaFuncMangleName(*topLevelFunc, lambdaWrapperIndex++);
    auto lambdaParamTypes = instFuncType.instParamTys;
    if (!funcDecl.TestAttr(AST::Attribute::STATIC)) {
        lambdaParamTypes.erase(lambdaParamTypes.begin());
    }
    auto lambdaType = builder.GetType<FuncType>(lambdaParamTypes, instFuncType.instRetTy);
    Lambda* lambda = CreateAndAppendExpression<Lambda>(
        lambdaType, lambdaType, currentBlock, false, lambdaMangledName, funcDecl.identifier);
    
    // init body
    auto lambdaBlockGroup = builder.CreateBlockGroup(*topLevelFunc);
    lambda->InitBody(*lambdaBlockGroup);
    auto entry = builder.CreateBlock(lambdaBlockGroup);
    lambdaBlockGroup->SetEntryBlock(entry);
    
    // create parameter
    for (auto paramTy : lambdaParamTypes) {
        builder.CreateParameter(paramTy, INVALID_LOCATION, *lambda);
    }

    // create return value
    auto lambdaRetType = instFuncType.instRetTy;
    auto retVal = CreateAndAppendExpression<Allocate>(
        INVALID_LOCATION, builder.GetType<RefType>(lambdaRetType), lambdaRetType, entry)->GetResult();
    lambda->SetReturnValue(*retVal);

    // create lambda body
    auto currentBlockBackup = currentBlock;
    currentBlock = entry;
    auto lambdaArgs = lambda->GetParams();
    std::vector<Value*> args{lambdaArgs.begin(), lambdaArgs.end()};
    Value* ret = nullptr;
    if (instFuncType.isVirtualFuncCall) {
        if (funcDecl.TestAttr(AST::Attribute::STATIC)) {
            // InvokeStatic
            CJC_ASSERT(thisObj == nullptr);
            auto rtti = CreateAndAppendExpression<GetRTTIStatic>(
                builder.GetUnitTy(), instFuncType.thisType->StripAllRefs(), currentBlock)->GetResult();
            auto invokeInfo = GenerateInvokeCallContext(instFuncType, *rtti, funcDecl, args);
            ret = CreateAndAppendExpression<InvokeStatic>(lambdaRetType, invokeInfo, currentBlock)->GetResult();
        } else {
            // Invoke
            CJC_NULLPTR_CHECK(thisObj);
            auto objExpectedType = instFuncType.instParentCustomTy;
            if (objExpectedType->IsReferenceType()) {
                objExpectedType = builder.GetType<RefType>(objExpectedType);
            }
            if (thisObj->GetType() != objExpectedType) {
                thisObj = TypeCastOrBoxIfNeeded(*thisObj, *objExpectedType, INVALID_LOCATION);
            }
            auto invokeInfo = GenerateInvokeCallContext(instFuncType, *thisObj, funcDecl, args);
            ret = CreateAndAppendExpression<Invoke>(lambdaRetType, invokeInfo, currentBlock)->GetResult();
        }
    } else {
        // Apply
        if (thisObj != nullptr) {
            auto objExpectedType = instFuncType.instParentCustomTy;
            if (objExpectedType->IsReferenceType()) {
                objExpectedType = builder.GetType<RefType>(objExpectedType);
            }
            args.insert(args.begin(), TransformThisType(*thisObj, *objExpectedType, *lambda));
        }
        CJC_ASSERT(args.size() == instFuncType.instParamTys.size());
        std::vector<Type*> instTypeArgs(
            instFuncType.instantiatedTypeArgs.begin(), instFuncType.instantiatedTypeArgs.end());
        auto callee = GetSymbolTable(funcDecl);
        auto funcType = builder.GetType<FuncType>(instFuncType.instParamTys, instFuncType.instRetTy);
        auto wrapperFunc = GetWrapperFuncFromMemberAccess(*instFuncType.instParentCustomTy->StripAllRefs(),
            callee->GetSrcCodeIdentifier(), *funcType, callee->TestAttr(Attribute::STATIC), instTypeArgs);
        if (wrapperFunc != nullptr) {
            callee = wrapperFunc;
        }
        auto funcCallContext = FuncCallContext {
            .args = args,
            .instTypeArgs = instTypeArgs,
            .thisType = instFuncType.thisType
        };
        ret = CreateAndAppendExpression<Apply>(
            instFuncType.instRetTy, callee, funcCallContext, currentBlock)->GetResult();
    }
    CreateWrappedStore(ret, retVal, currentBlock);
    CreateAndAppendTerminator<Exit>(currentBlock);
    currentBlock = currentBlockBackup;
    return lambda->GetResult();
}

Value* Translator::TranslateMemberFuncRef(const AST::RefExpr& refExpr)
{
    auto target = refExpr.ref.target;
    CJC_ASSERT(target->astKind == ASTKind::FUNC_DECL);
    Value* thisObj = nullptr;
    if (!target->TestAttr(AST::Attribute::STATIC)) {
        thisObj = GetImplicitThisParam();
    }
    auto instFuncType = GetInstCalleeInfoFromRefExpr(refExpr);
    if (!instFuncType.isVirtualFuncCall && target->TestAttr(AST::Attribute::STATIC)) {
        // just a shotcut, we can use `GetInstantiateValue` instead of `Lambda`
        auto targetFunc = GetSymbolTable(*target);
        return TranslateGlobalOrLocalFuncRef(refExpr, *targetFunc);
    } else {
        return WrapMemberMethodByLambda(*StaticCast<FuncDecl*>(target), instFuncType, thisObj);
    }
}

Value* Translator::TranslateGlobalOrLocalFuncRef(const AST::RefExpr& refExpr, Value& originalFunc)
{
    auto outerDeclaredTypes = GetOutDefDeclaredTypes(originalFunc);
    // 1. get inst types of outer custom type from current func's parent type
    if (originalFunc.IsFunc() && VirtualCast<FuncBase*>(&originalFunc)->IsMemberFunc() &&
        GetCurrentFunc() && GetCurrentFunc()->GetParentCustomTypeDef() != nullptr) {
        /* orginalFunc may be defined in interface, try to get inst Types from current custom type
            interface I<T, U, V, W> {
                static func foo<T2, T3>() {
                    1
                }
            }
            class B<T> <: I<Int32, T, Int64, T> {
                static public func me<T2, U2>() {
                    let a = foo<T, T2>
                    a()
                }
            }
            we shuold get (Int32, T, Int64, T) for foo's class inst args.
            1. get (T, U, V, W) from visiable generic types of I
            2. replace with inst types get from me' custom type B<T>, we got results (Int32, T, Int64, T).
        */
        auto originCustomDef = VirtualCast<FuncBase*>(&originalFunc)->GetParentCustomTypeDef();
        auto curFunc = GetCurrentFunc();
        CJC_NULLPTR_CHECK(curFunc);
        auto parentFuncCustomDef = curFunc->GetParentCustomTypeDef();
        CJC_ASSERT(originCustomDef && parentFuncCustomDef);
        if (originCustomDef != parentFuncCustomDef) {
            std::unordered_map<const GenericType*, Type*> instMap;
            // originCustomDef may bot be the direct parent, try get all inst map.
            GetInstMapFromCustomDefAndParent(*parentFuncCustomDef, instMap, builder);
            for (size_t i = 0; i < originCustomDef->GetGenericTypeParams().size(); i++) {
                if (!outerDeclaredTypes[i]->IsGeneric()) {
                    continue;
                }
                auto found = instMap.find(StaticCast<GenericType*>(outerDeclaredTypes[i]));
                if (found != instMap.end()) {
                    outerDeclaredTypes[i] = found->second;
                }
            }
        }
    }
    // 2. get func inst types from AST
    std::vector<Type*> instArgs;
    /** cj code like:
     * class A<T1> {
     *     func foo<T2>() {
     *         func goo<T3>() {}
     *         var x = goo<Bool>  ==> create `GetInstantiateValue(goo, T1, T2, Bool)`
     *     }
     * }
    */
    // emplace back `T1` and `T2`
    CJC_ASSERT(outerDeclaredTypes.size() >= refExpr.instTys.size());
    for (size_t i = 0; i < outerDeclaredTypes.size() - refExpr.instTys.size(); ++i) {
        instArgs.emplace_back(outerDeclaredTypes[i]);
    }
    // emplace back `Bool`
    for (auto ty : refExpr.instTys) {
        instArgs.emplace_back(TranslateType(*ty));
    }

    if (instArgs.empty()) {
        return &originalFunc;
    }

    // 3. create GetInstantiateValue
    auto resTy = TranslateType(*refExpr.ty);
    auto loc = TranslateLocation(refExpr);
    return CreateAndAppendExpression<GetInstantiateValue>(loc, resTy, &originalFunc, instArgs, currentBlock)
        ->GetResult();
}

Value* Translator::TranslateFuncRef(const AST::RefExpr& refExpr)
{
    auto target = refExpr.ref.target;
    CJC_ASSERT(target);
    CJC_ASSERT(target->astKind == AST::ASTKind::FUNC_DECL);
    auto loc = TranslateLocation(refExpr);

    if (target->outerDecl != nullptr && target->outerDecl->IsNominalDecl()) {
        // Case 1: member method
        return TranslateMemberFuncRef(refExpr);
    } else {
        // Case 2: global or local function
        auto targetFunc = GetSymbolTable(*target);
        return TranslateGlobalOrLocalFuncRef(refExpr, *targetFunc);
    }
}

Ptr<Value> Translator::Visit(const AST::RefExpr& refExpr)
{
    // Case 1: `this` or `super`
    if (refExpr.isThis || refExpr.isSuper) {
        return TranslateThisOrSuperRef(refExpr);
    }

    // Case 2: variable
    auto target = refExpr.ref.target;
    CJC_ASSERT(target);
    if (target->astKind == AST::ASTKind::VAR_DECL || target->astKind == AST::ASTKind::FUNC_PARAM) {
        return TranslateVarRef(refExpr);
    }

    // Case 3: func
    if (target->astKind == AST::ASTKind::FUNC_DECL) {
        return TranslateFuncRef(refExpr);
    }

    CJC_ABORT();
    return nullptr;
}
