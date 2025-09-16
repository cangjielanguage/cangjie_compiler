// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/AST/Utils.h"
#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"
#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/CHIR/ConstantUtils.h"
#include "cangjie/CHIR/Type/Type.h"

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

uint64_t Translator::GetVarMemberIndex(const AST::VarDecl& varDecl)
{
    uint64_t index = 0;
    auto outerDecl = varDecl.outerDecl;
    CJC_ASSERT(outerDecl);

    if (outerDecl->astKind == ASTKind::STRUCT_DECL) {
        auto structDecl = StaticCast<StructDecl*>(outerDecl);
        for (auto& field : structDecl->body->decls) {
            if (field->astKind == AST::ASTKind::VAR_DECL && !field->TestAttr(AST::Attribute::STATIC)) {
                if (field.get() == &varDecl) {
                    return index;
                }
                index++;
            }
        }
    } else if (outerDecl->astKind == ASTKind::CLASS_DECL) {
        auto classDecl = StaticCast<ClassDecl*>(outerDecl);
        auto classTy = StaticCast<ClassType*>(GetNominalSymbolTable(*classDecl)->GetType());
        for (auto& field : classDecl->body->decls) {
            if (field->astKind == AST::ASTKind::VAR_DECL && !field->TestAttr(AST::Attribute::STATIC)) {
                if (field.get() == &varDecl) {
                    return index +
                        (classTy->GetSuperClassTy(&builder)
                                ? classTy->GetSuperClassTy(&builder)->GetClassDef()->GetAllInstanceVarNum()
                                : 0);
                }
                index++;
            }
        }
    }
    CJC_ABORT();
    return 0;
}

Translator::LeftValueInfo Translator::TranslateStructMemberVarRefAsLeftValue(const AST::RefExpr& refExpr)
{
    auto target = refExpr.ref.target;
    CJC_ASSERT(target->astKind == ASTKind::VAR_DECL);
    CJC_ASSERT(target->outerDecl->astKind == ASTKind::STRUCT_DECL);

    auto implicitThis = GetImplicitThisParam();
    auto index = GetVarMemberIndex(*StaticCast<VarDecl*>(target));
    return LeftValueInfo(implicitThis, {index});
}

Translator::LeftValueInfo Translator::TranslateClassMemberVarRefAsLeftValue(const AST::RefExpr& refExpr)
{
    auto target = refExpr.ref.target;
    CJC_ASSERT(target->astKind == ASTKind::VAR_DECL);
    CJC_ASSERT(target->outerDecl->astKind == ASTKind::CLASS_DECL);

    auto implicitThis = GetImplicitThisParam();
    CJC_ASSERT(implicitThis->GetType()->IsReferenceTypeWithRefDims(1));
    auto index = GetVarMemberIndex(*StaticCast<VarDecl*>(target));
    return LeftValueInfo(implicitThis, {index});
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
                auto memberType = thisCustomType->GetInstMemberTypeByPath(path, builder);
                auto getField = CreateAndAppendExpression<Field>(loc, memberType, varLeftValueBase, path, currentBlock);
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

Translator::InstCalleeInfo Translator::GetCustomTypeFuncRef(const AST::RefExpr& expr, const AST::FuncDecl& funcDecl)
{
    auto funcType = StaticCast<FuncType*>(TranslateType(*expr.ty));
    auto paramTys = funcType->GetParamTypes();
    CJC_ASSERT(funcDecl.outerDecl != nullptr);
    auto currentFunc = GetCurrentFunc();
    CJC_NULLPTR_CHECK(currentFunc);
    auto outerDef = currentFunc->GetParentCustomTypeDef();
    CJC_NULLPTR_CHECK(outerDef);
    Type* thisTy = outerDef->GetType();
    if (thisTy->IsClass() || IsStructMutFunction(funcDecl)) {
        thisTy = builder.GetType<RefType>(thisTy);
    }
    if (!funcDecl.TestAttr(AST::Attribute::STATIC)) {
        paramTys.insert(paramTys.begin(), thisTy);
    }
    return InstCalleeInfo{thisTy, thisTy, paramTys, funcType->GetReturnType()};
}

Value* Translator::TranslateMemberFuncRef(const AST::RefExpr& refExpr)
{
    auto target = refExpr.ref.target;
    CJC_ASSERT(target->astKind == ASTKind::FUNC_DECL);
    // should use loc instead of pos
    Position pos = {unsigned(refExpr.begin.line), unsigned(refExpr.begin.column)};

    auto thisObj = GetImplicitThisParam();
    auto instFuncType = GetCustomTypeFuncRef(refExpr, *StaticCast<FuncDecl*>(target));
    std::vector<Ptr<Type>> funcInstArgs;
    for (auto ty : refExpr.instTys) {
        funcInstArgs.emplace_back(TranslateType(*ty));
    }
    return WrapFuncMemberByLambda(
        *StaticCast<FuncDecl*>(target), pos, thisObj, nullptr, instFuncType, funcInstArgs, false);
}

/// Returns true if \ref expr is to be translated to a InvokeStatic call after wrapping function into lambda.
static bool IsInvokeStaticAccess(const AST::RefExpr& ref)
{
    auto fun = DynamicCast<AST::FuncDecl>(ref.ref.target);
    if (!fun || !fun->TestAttr(AST::Attribute::STATIC) ||
        fun->TestAnyAttr(AST::Attribute::PRIVATE, AST::Attribute::GENERIC_INSTANTIATED)) {
        return false;
    }
    // exclude partial instantiation
    if (fun->outerDecl->TestAttr(AST::Attribute::GENERIC_INSTANTIATED)) {
        return false;
    }
    if (fun->ty->HasGeneric() || fun->TestAttr(AST::Attribute::ABSTRACT)) {
        return true;
    }
    auto parent = fun->outerDecl;
    if (parent->ty->HasGeneric()) {
        return true;
    }
    return parent->IsOpen();
}

Value* Translator::TranslateFuncRefInstArgs(const AST::RefExpr& refExpr, Value& originalFunc)
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
        if (IsInvokeStaticAccess(refExpr)) {
            return TranslateStaticAccessAsLambda(refExpr);
        }
        return &originalFunc;
    }

    // 3. create GetInstantiateValue
    auto resTy = TranslateType(*refExpr.ty);
    auto loc = TranslateLocation(refExpr);
    return CreateAndAppendExpression<GetInstantiateValue>(loc, resTy, &originalFunc, instArgs, currentBlock)
        ->GetResult();
}

/// open class I {
///     static func f() {println("I")}
///     static func f2() { f }
/// }
/// In this case, the refExpr 'f' in func f2 should be wrapped by lambda, and the result of \ref ref is the lambda
Value* Translator::TranslateStaticAccessAsLambda(const AST::RefExpr& ref)
{
    auto target = StaticCast<AST::FuncDecl>(ref.ref.target);
    Position pos = {unsigned(target->begin.line), unsigned(target->begin.column)};
    auto instFuncType = GetCustomTypeFuncRef(ref, *target);
    std::vector<Ptr<Type>> funcInstArgs;
    for (auto ty : ref.instTys) {
        funcInstArgs.emplace_back(TranslateType(*ty));
    }
    return WrapFuncMemberByLambda(*target, pos, nullptr, nullptr, instFuncType, funcInstArgs, false);
}

Value* Translator::TranslateFuncRef(const AST::RefExpr& refExpr)
{
    auto target = refExpr.ref.target;
    CJC_ASSERT(target);
    CJC_ASSERT(target->astKind == AST::ASTKind::FUNC_DECL);
    auto loc = TranslateLocation(refExpr);

    // Case 1: non static member func
    if (!target->TestAttr(AST::Attribute::STATIC) &&
        target->outerDecl != nullptr && target->outerDecl->IsNominalDecl()) {
        return TranslateMemberFuncRef(refExpr);
    }
    // Case 2: static abstract function
    if (target->TestAttr(AST::Attribute::STATIC, AST::Attribute::ABSTRACT)) {
        auto curFunc = GetCurrentFunc();
        CJC_NULLPTR_CHECK(curFunc);
        CJC_ASSERT(target->astKind == ASTKind::FUNC_DECL);
        // should use loc instead of pos
        Position pos = {unsigned(refExpr.begin.line), unsigned(refExpr.begin.column)};

        auto instFuncType = GetCustomTypeFuncRef(refExpr, *StaticCast<FuncDecl*>(target));
        std::vector<Ptr<Type>> funcInstArgs;
        for (auto ty : refExpr.instTys) {
            funcInstArgs.emplace_back(TranslateType(*ty));
        }
        return WrapFuncMemberByLambda(
            *StaticCast<FuncDecl*>(target), pos, nullptr, nullptr, instFuncType, funcInstArgs, false);
    }
    // Case 3: global func or local func
    auto targetFunc = GetSymbolTable(*target);
    return TranslateFuncRefInstArgs(refExpr, *targetFunc);
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
