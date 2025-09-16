// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

#include "cangjie/CHIR/ConstantUtils.h"
#include "cangjie/CHIR/Package.h"
#include "cangjie/Mangle/CHIRManglingUtils.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;
using namespace AST;

namespace {
/*
    Use UpperBound to replace Generic type,e.g. if T <: C1
    1、if baseType is T then return C1&
    2、if baseType is T& then return C1&
    3、if baseType is C2<T> then return C2<C1&>
 */
Ptr<CHIR::Type> CreateTypeWithUpperBounds(CHIR::Type& baseType, CHIR::CHIRBuilder& builder)
{
    std::vector<CHIR::Type*> newArgs;
    auto srcType = &baseType;
    if (srcType->IsRef() && StaticCast<CHIR::RefType*>(srcType)->GetBaseType()->IsGeneric()) {
        srcType = StaticCast<CHIR::RefType*>(srcType)->GetBaseType();
    }
    if (srcType->IsGeneric()) {
        for (auto type : StaticCast<GenericType*>(srcType)->GetUpperBounds()) {
            if (type->IsRef() && StaticCast<CHIR::RefType*>(type)->GetBaseType()->IsClass()) {
                return type;
            }
        }
    } else {
        for (auto type : srcType->GetTypeArgs()) {
            newArgs.emplace_back(CreateTypeWithUpperBounds(*type, builder));
        }
        return CreateNewTypeWithArgs(*srcType, newArgs, builder);
    }
    return &baseType;
}
} // namespace

uint64_t Translator::GetFieldOffset(const AST::Decl& target) const
{
    auto parentDecl = target.outerDecl;
    CJC_ASSERT(parentDecl->astKind == ASTKind::CLASS_DECL || parentDecl->astKind == AST::ASTKind::STRUCT_DECL);

    uint64_t superFieldOffset = 0;
    if (parentDecl->astKind == ASTKind::CLASS_DECL) {
        auto classDecl = StaticCast<AST::ClassDecl*>(parentDecl);
        auto nonStaticSuperMemberVars = GetNonStaticSuperMemberVars(*classDecl);
        auto fieldIt = std::find_if(nonStaticSuperMemberVars.begin(), nonStaticSuperMemberVars.end(),
            [&target](auto decl) -> bool { return &target == decl; });
        if (fieldIt != nonStaticSuperMemberVars.end()) {
            return static_cast<uint64_t>(std::distance(nonStaticSuperMemberVars.begin(), fieldIt));
        }
        superFieldOffset = nonStaticSuperMemberVars.size();
    }
    auto nonStaticMemberVars = GetNonStaticMemberVars(*StaticCast<AST::InheritableDecl*>(parentDecl));
    auto fieldIt = std::find_if(nonStaticMemberVars.begin(), nonStaticMemberVars.end(),
        [&target](auto decl) -> bool { return &target == decl; });
    CJC_ASSERT(fieldIt != nonStaticMemberVars.end());
    auto fieldOffset = static_cast<uint64_t>(std::distance(nonStaticMemberVars.begin(), fieldIt));
    return superFieldOffset + fieldOffset;
}

Ptr<Value> Translator::GetBaseFromMemberAccess(const AST::Expr& base)
{
    if (AST::IsThisOrSuper(base)) {
        // This call or super call don't need add `Load`;
        // struct A {
        //   let x:Int64
        //     func foo() {this.x}
        // }
        auto thisVar = GetImplicitThisParam();
        return thisVar;
    }
    auto curObj = TranslateExprArg(base);
    auto loc = TranslateLocation(base);
    if (base.ty->IsClassLike() || curObj->GetType()->IsRawArray()) {
        // class A {func foo(){return 0}
        // var a = A()
        // a.b.c.d           // a is A&& need add `Load`
        // let b = A()
        // b.c.d            // b is A& don't need add `Load`
        // note: generic type will cast to class upper bound, do load if need
        auto objType = curObj->GetType();
        CJC_ASSERT(objType->IsRef());
        if (objType->IsRef()) {
            // objType is A&& or A&
            auto objBaseType = StaticCast<RefType*>(objType)->GetBaseType();
            if (objBaseType->IsRef()) {
                // for example: objBaseType is A&
                curObj = CreateAndAppendExpression<Load>(loc, objBaseType, curObj, currentBlock)->GetResult();
            }
        }
    } else if (curObj->GetType()->IsRef() && StaticCast<RefType*>(curObj->GetType())->GetBaseType()->IsGeneric()) {
        // a generic type variable must be non reference as a parameter in GetElementRef/StoreElementRef
        // Example:
        // Var a: T = xxx     // T is a generic type
        // a.b                // a is T&， need add Load expression,
        auto objBaseType = StaticCast<RefType*>(curObj->GetType())->GetBaseType();
        curObj = CreateAndAppendExpression<Load>(loc, objBaseType, curObj, currentBlock)->GetResult();
    }
    if (curObj->GetType()->IsGeneric()) {
        // type cast to upperbounds, if base is a generic
        auto newType = CreateTypeWithUpperBounds(*curObj->GetType(), builder);
        curObj = TypeCastOrBoxIfNeeded(*curObj, *newType, loc);
    }
    return curObj;
}

Ptr<CHIR::Type> Translator::GetTypeOfInvokeStatic(const AST::Decl& funcDecl)
{
    CJC_NULLPTR_CHECK(funcDecl.outerDecl);
    auto calledClassType = TranslateType(*funcDecl.outerDecl->ty);
    if (calledClassType->IsRef()) {
        calledClassType = StaticCast<CHIR::RefType*>(calledClassType)->GetBaseType();
        return calledClassType;
    }
    return calledClassType;
}

Translator::InstCalleeInfo Translator::GetCustomTypeMemberAccessFuncRef(const AST::MemberAccess& expr)
{
    auto thisInstTy = TranslateType(*expr.baseExpr->ty);
    auto funcType = StaticCast<FuncType*>(TranslateType(*expr.ty));
    auto paramTys = funcType->GetParamTypes();
    if (!expr.target->TestAttr(AST::Attribute::STATIC)) {
        paramTys.insert(paramTys.begin(), thisInstTy);
    }
    return InstCalleeInfo{thisInstTy, thisInstTy, paramTys, funcType->GetReturnType()};
}

Value* Translator::GetWrapperFuncFromMemberAccess(Type& thisType, const std::string funcName,
    FuncType& instFuncType, bool isStatic, std::vector<Type*>& funcInstTypeArgs)
{
    FuncBase* result = nullptr;
    if (auto genericType = DynamicCast<GenericType*>(&thisType)) {
        auto& upperBounds = genericType->GetUpperBounds();
        CJC_ASSERT(!upperBounds.empty());
        for (auto upperBound : upperBounds) {
            ClassType* upperClassType = StaticCast<ClassType*>(StaticCast<RefType*>(upperBound)->GetBaseType());
            return GetWrapperFuncFromMemberAccess(*upperClassType, funcName, instFuncType, isStatic, funcInstTypeArgs);
        }
    } else if (auto customTy = DynamicCast<CustomType*>(&thisType)) {
        result = customTy->GetExpectedFunc(funcName, instFuncType, true, funcInstTypeArgs, builder, false).first;
    } else {
        std::unordered_map<const GenericType*, Type*> replaceTable;
        auto classInstArgs = thisType.GetTypeArgs();
        // extend def
        for (auto ex : thisType.GetExtends(&builder)) {
            auto classGenericArgs = ex->GetExtendedType()->GetTypeArgs();
            CJC_ASSERT(classInstArgs.size() == classGenericArgs.size());
            for (size_t i = 0; i < classInstArgs.size(); ++i) {
                if (auto genericTy = DynamicCast<GenericType*>(classGenericArgs[i])) {
                    replaceTable.emplace(genericTy, classInstArgs[i]);
                }
            }
            auto [func, done] =
                ex->GetExpectedFunc(funcName, instFuncType, true, replaceTable, funcInstTypeArgs, builder, false);
            if (done) {
                result = func;
                break;
            }
        }
    }

    return result;
}

Ptr<Value> Translator::TranslateStaticTargetOrPackageMemberAccess(const AST::MemberAccess& member)
{
    // only classA.foo need a wrapper, pkgA.foo doesn't
    if (member.target->astKind == AST::ASTKind::FUNC_DECL) {
        auto funcDecl = member.target;
        if (funcDecl->outerDecl != nullptr) {
            Position pos = {unsigned(funcDecl->begin.line), unsigned(funcDecl->begin.column)};
            auto instFuncType = GetCustomTypeMemberAccessFuncRef(member);
            std::vector<Ptr<Type>> funcInstArgs;
            for (auto ty : member.instTys) {
                funcInstArgs.emplace_back(TranslateType(*ty));
            }
            return WrapFuncMemberByLambda(*StaticCast<FuncDecl*>(funcDecl),
                pos, nullptr, instFuncType.thisType, instFuncType, funcInstArgs, false);
        }
    }
    auto targetNode = GetSymbolTable(*member.target);
    auto targetTy = TranslateType(*member.target->ty);
    auto resTy = TranslateType(*member.ty);
    auto loc = TranslateLocation(member);
    if (auto refExpr = DynamicCast<AST::RefExpr*>(&*member.baseExpr)) {
        // this is a package member access, return the target directly
        if (refExpr->ref.target->ty->IsInvalid()) {
            // global var, load and typecast if needed
            if (Is<AST::VarDecl>(member.target)) {
                auto targetVal = CreateAndAppendExpression<Load>(loc, targetTy, targetNode, currentBlock)->GetResult();
                auto castedTargetVal = TypeCastOrBoxIfNeeded(*targetVal, *resTy, loc);
                return castedTargetVal;
            }
            return targetNode;
        }
    }

    auto targetVal = CreateAndAppendExpression<Load>(loc, targetTy, targetNode, currentBlock)->GetResult();
    auto castedTargetVal = TypeCastOrBoxIfNeeded(*targetVal, *resTy, loc);
    return castedTargetVal;
}

Ptr<Value> Translator::TranslateFuncMemberAccess(const AST::MemberAccess& member)
{
    auto funcDecl = RawStaticCast<AST::FuncDecl*>(member.target);
    bool isSuper = false;
    if (auto base = DynamicCast<RefExpr*>(member.baseExpr.get()); (base && base->isSuper)) {
        isSuper = true;
    }
    auto instFuncType = GetCustomTypeMemberAccessFuncRef(member);

    CJC_NULLPTR_CHECK(funcDecl->outerDecl);
    auto calledThisType = TranslateType(*funcDecl->outerDecl->ty);

    // polish the code here
    if (funcDecl->funcBody->parentClassLike && !isSuper && IsVirtualMember(*funcDecl)) {
        // Never consider try-catch context for wrapped lambda of member function.
        std::vector<Ptr<Type>> funcInstArgs;
        for (auto ty : member.instTys) {
            funcInstArgs.emplace_back(TranslateType(*ty));
        }
        auto funcInfo = CreateVirFuncInvokeInfo(instFuncType, funcInstArgs, *funcDecl);
        calledThisType = funcInfo.instParentCustomTy;
        if (calledThisType->IsClass()) {
            calledThisType = builder.GetType<RefType>(calledThisType);
        }
    } else {
        std::vector<Type*> funcInstArgs;
        for (auto ty : member.instTys) {
            funcInstArgs.emplace_back(TranslateType(*ty));
        }
        auto thisTy = instFuncType.thisType->StripAllRefs();
        // for non-virtual or virtual static function we should also find and calculate instantited this Type
        instFuncType.instParentCustomTy = GetExactParentType(*thisTy, *funcDecl,
            *builder.GetType<FuncType>(instFuncType.instParamTys, instFuncType.instRetTy), funcInstArgs, false, true);
    }

    auto thisVal = GetCurrentThisObjectByMemberAccess(member, *funcDecl, TranslateLocation(*member.baseExpr));
    auto loc = TranslateLocation(member);
    Position pos = {unsigned(member.begin.line), unsigned(member.begin.column)};
    std::vector<Ptr<Type>> funcInstArgs;
    for (auto ty : member.instTys) {
        funcInstArgs.emplace_back(TranslateType(*ty));
    }
    return WrapFuncMemberByLambda(
        *funcDecl, pos, thisVal, instFuncType.thisType, instFuncType, funcInstArgs, isSuper);
}

Ptr<Value> Translator::TransformThisType(Value& rawThis, Type& expectedTy, Lambda& curLambda)
{
    /** this function is used in lambda which generated by member access, such as:
        struct A {
            func a(): Int64 { return 1 }
            mut func b(): Int64 {
                let c = a // `a` will be translated to lambda, in lambda, there is `Apply(this.a)`
                return c() // Apply(lambda(a))
            }
        }
        so, param `this` in function b may not be passed to `Apply(this.a)` directly
        we need to add load or typecast for `this`, `this` has three cases:
        1. in class's member function, `this` is ref type
        2. in struct's mut member function, `this` is ref type
        3. in struct's immut member function, `this` is struct type
        considered cangjie rules, there are five cases for transform:
        a. struct& -> struct
        b. struct& -> struct&
        c. struct -> struct
        d. class& -> class&
        e. class& -> sub class& or super class&
        cangjie rules:
        1. struct can't inheritance struct, only interface
            so struct type doesn't have sub struct or super struct
        2. a mut function can't be called in an immut function
            so we can't transform struct type to struct ref type
    */
    // case b, c, d
    if (rawThis.GetType() == &expectedTy) {
        return &rawThis;
    }
    // case a
    Expression* expr = nullptr;
    if (rawThis.GetType()->IsRef() && StaticCast<RefType*>(rawThis.GetType())->GetBaseType()->IsStruct()) {
        CJC_ASSERT(StaticCast<RefType*>(rawThis.GetType())->GetBaseType() == &expectedTy);
        expr = builder.CreateExpression<Load>(&expectedTy, &rawThis, curLambda.GetParentBlock());
    } else {
        // case e
        expr = StaticCast<LocalVar*>(TypeCastOrBoxIfNeeded(rawThis, expectedTy, INVALID_LOCATION))->GetExpr();
    }
    // this is really hack, should change this
    if (expr->GetResult() != &rawThis) {
        // `load` or `typecast` must be created before lambda, or we will get wrong llvm ir, and core dump in llvm-opt
        expr->MoveBefore(&curLambda);
    }
    return expr->GetResult();
}

GenericType* Translator::TranslateCompleteGenericType(AST::GenericsTy& ty)
{
    auto gType = StaticCast<GenericType*>(TranslateType(ty));
    chirTy.FillGenericArgType(ty);
    return gType;
}

Translator::InstInvokeCalleeInfo Translator::CreateVirFuncInvokeInfo(
    InstCalleeInfo& strInstFuncType, const std::vector<Ptr<Type>>& funcInstArgs, const AST::FuncDecl& resolvedFunction)
{
    std::string funcName = resolvedFunction.identifier;

    auto instFuncType = builder.GetType<FuncType>(strInstFuncType.instParamTys, strInstFuncType.instRetTy);
    auto rootTy = strInstFuncType.instParentCustomTy->StripAllRefs();
    std::vector<Type*> funcInstTypeArgs{funcInstArgs.begin(), funcInstArgs.end()};
    FuncCallType funcCallType{funcName, instFuncType, funcInstTypeArgs};
    auto vtableRes = GetFuncIndexInVTable(
        *rootTy, funcCallType, resolvedFunction.TestAttr(AST::Attribute::STATIC))[0];
    auto thisType = strInstFuncType.thisType;
    if (thisType == nullptr) {
        thisType = builder.GetType<RefType>(builder.GetType<ThisType>());
    }
    return InstInvokeCalleeInfo{
        resolvedFunction.identifier, instFuncType, vtableRes.originalFuncType, vtableRes.instSrcParentType,
        StaticCast<ClassType*>(vtableRes.instSrcParentType->GetCustomTypeDef()->GetType()), funcInstTypeArgs,
        thisType, vtableRes.offset};
}

Ptr<Value> Translator::WrapFuncMemberByLambda(
    const AST::FuncDecl& funcDecl, const Position& pos, Ptr<Value> thisVal, Type* thisType,
    InstCalleeInfo& strInstFuncType, std::vector<Ptr<Type>>& funcInstArgs, bool isSuper)
{
    // 1. Create lambda node.
    CJC_NULLPTR_CHECK(currentBlock->GetTopLevelFunc());
    auto lambdaBlockGroup = builder.CreateBlockGroup(*currentBlock->GetTopLevelFunc());
    std::string lambdaName = funcDecl.identifier;
    auto parentFunc = currentBlock->GetParentBlockGroup()->GetTopLevelFunc();
    CJC_NULLPTR_CHECK(parentFunc);
    std::string parentFuncMangledName = parentFunc->GetIdentifierWithoutPrefix();
    CJC_ASSERT(!parentFuncMangledName.empty());
    CJC_ASSERT(pos.IsLegal());
    std::string lambdaMangledName =
        CHIRMangling::GenerateLambdaFuncMangleName(*parentFunc, lambdaWrapperIndex++);
    auto lambdaParamTys = strInstFuncType.instParamTys;
    if (!funcDecl.TestAttr(AST::Attribute::STATIC)) {
        lambdaParamTys.erase(lambdaParamTys.begin());
    }
    auto lambdaTy = builder.GetType<FuncType>(lambdaParamTys, strInstFuncType.instRetTy);
    CJC_ASSERT(funcDecl.outerDecl != nullptr);
    Lambda* lambda =
        CreateAndAppendExpression<Lambda>(lambdaTy, lambdaTy, currentBlock, false, lambdaMangledName, lambdaName);
    lambda->InitBody(*lambdaBlockGroup);
    for (auto paramTy : lambdaParamTys) {
        builder.CreateParameter(paramTy, INVALID_LOCATION, *lambda);
    }

    auto entry = builder.CreateBlock(lambdaBlockGroup);
    lambdaBlockGroup->SetEntryBlock(entry);

    auto instRetType = strInstFuncType.instRetTy;
    auto retVal =
        CreateAndAppendExpression<Allocate>(INVALID_LOCATION, builder.GetType<RefType>(instRetType), instRetType, entry)
            ->GetResult();
    lambda->SetReturnValue(*retVal);

    // 2.translate lambda body.
    auto bodyBlock = builder.CreateBlock(lambdaBlockGroup);
    auto currentBlockBackup = currentBlock;
    currentBlock = bodyBlock;
    std::stack<Ptr<Block>> tryCatchContextBackUp = {};
    std::swap(tryCatchContext, tryCatchContextBackUp);
    auto lambdaArgs = lambda->GetParams();
    std::vector<Value*> args{lambdaArgs.begin(), lambdaArgs.end()};

    Ptr<Value> ret = nullptr;
    /** super function call must be `Apply`
     *  open class A {
     *      public open func foo() {}
     *  }
     *  class B <: A {
     *      public func foo() {}
     *      func goo() {
     *          var a = super.foo // we must call A.foo, not B.foo by vtable
     *          a()
     *      }
     *  }
     */
    if (!isSuper && IsVirtualMember(funcDecl) && !funcDecl.TestAttr(AST::Attribute::STATIC)) {
        auto funcInfo = CreateVirFuncInvokeInfo(strInstFuncType, funcInstArgs, funcDecl);
        auto originalParamTys = funcInfo.originalFuncType->GetParamTypes();
        originalParamTys.erase(originalParamTys.begin());
        Type* invokeObjTargetTy = funcInfo.instParentCustomTy;
        if (invokeObjTargetTy->IsClass()) {
            invokeObjTargetTy = builder.GetType<RefType>(invokeObjTargetTy);
        }
        auto thisValTy = thisVal->GetType();
        if (thisValTy != invokeObjTargetTy) {
            thisVal = TypeCastOrBoxIfNeeded(*thisVal, *invokeObjTargetTy, INVALID_LOCATION);
        }
        auto invokeInfo = InvokeCallContext {
            .caller = thisVal,
            .funcCallCtx = FuncCallContext {
                .args = args,
                .instTypeArgs = funcInfo.instantiatedTypeArgs,
                .thisType = funcInfo.thisType
            },
            .virMethodCtx = VirMethodContext {
                .srcCodeIdentifier = funcInfo.srcCodeIdentifier,
                .originalFuncType = funcInfo.originalFuncType,
                .offset = funcInfo.offset
            }
        };
        ret = CreateAndAppendExpression<Invoke>(
            funcInfo.instFuncType->GetReturnType(), invokeInfo, currentBlock)->GetResult();
    } else if (funcDecl.TestAttr(AST::Attribute::STATIC) && !IsInsideCFunc(*currentBlock) &&
        (thisType == nullptr || thisType->IsGeneric())) {
        auto funcInfo = CreateVirFuncInvokeInfo(strInstFuncType, funcInstArgs, funcDecl);
        Value* rtti;
        if (thisVal) {
            rtti = CreateGetRTTIWrapper(thisVal, currentBlock, INVALID_LOCATION);
        } else if (thisType == nullptr) {
            rtti = CreateAndAppendExpression<GetRTTIStatic>(
                builder.GetUnitTy(), builder.GetType<ThisType>(), currentBlock)->GetResult();
        } else {
            rtti = CreateAndAppendExpression<GetRTTIStatic>(
                builder.GetUnitTy(), thisType, currentBlock)->GetResult();
        }
        auto invokeInfo = InvokeCallContext {
            .caller = rtti,
            .funcCallCtx = FuncCallContext {
                .args = args,
                .instTypeArgs = funcInfo.instantiatedTypeArgs,
                .thisType = funcInfo.thisType
            },
            .virMethodCtx = VirMethodContext {
                .srcCodeIdentifier = funcInfo.srcCodeIdentifier,
                .originalFuncType = funcInfo.originalFuncType,
                .offset = funcInfo.offset
            }
        };
        ret = CreateAndAppendExpression<InvokeStatic>(
            funcInfo.instFuncType->GetReturnType(), invokeInfo, currentBlock)->GetResult();
    } else {
        auto callee = GetSymbolTable(funcDecl);
        if (thisVal != nullptr) {
            args.insert(args.begin(), TransformThisType(*thisVal, *strInstFuncType.instParamTys[0], *lambda));
        }
        CJC_ASSERT(args.size() == strInstFuncType.instParamTys.size());
        for (size_t i = 0; i < args.size(); ++i) {
            args[i] = TypeCastOrBoxIfNeeded(*args[i], *strInstFuncType.instParamTys[i], INVALID_LOCATION);
        }
        // check the thisType and instParentCustomDefTy
        CJC_NULLPTR_CHECK(strInstFuncType.instParentCustomTy);
        auto instFuncType = builder.GetType<FuncType>(strInstFuncType.instParamTys, strInstFuncType.instRetTy);
        std::vector<Type*> instArgs(funcInstArgs.begin(), funcInstArgs.end());
        auto wrapperFunc = GetWrapperFuncFromMemberAccess(*strInstFuncType.instParentCustomTy->StripAllRefs(),
            callee->GetSrcCodeIdentifier(), *instFuncType, callee->TestAttr(Attribute::STATIC), instArgs);
        if (wrapperFunc != nullptr) {
            callee = wrapperFunc;
        }
        auto expr = CreateAndAppendExpression<Apply>(strInstFuncType.instRetTy, callee, FuncCallContext{
            .args = args,
            .instTypeArgs = instArgs,
            .thisType = strInstFuncType.thisType}, currentBlock);
        ret = expr->GetResult();
    }
    ret = TypeCastOrBoxIfNeeded(*ret, *instRetType, INVALID_LOCATION);
    CreateWrappedStore(ret, retVal, currentBlock);
    CreateAndAppendTerminator<Exit>(currentBlock);

    CreateAndAppendTerminator<GoTo>(bodyBlock, entry);
    currentBlock = currentBlockBackup;
    std::swap(tryCatchContext, tryCatchContextBackUp);
    auto lambdaRes = lambda->GetResult();

    return lambdaRes;
}

Ptr<Value> Translator::TranslateVarMemberAccess(const AST::MemberAccess& member)
{
    const auto& loc = TranslateLocation(member);
    auto leftValueInfo = TranslateMemberAccessAsLeftValue(member);
    auto base = leftValueInfo.base;
    CJC_ASSERT(!leftValueInfo.path.empty());
    auto customType = StaticCast<CustomType*>(base->GetType()->StripAllRefs());
    if (base->GetType()->IsReferenceTypeWithRefDims(1) || base->GetType()->IsValueOrGenericTypeWithRefDims(1)) {
        base = CreateGetElementRefWithPath(loc, base, leftValueInfo.path, currentBlock, *customType);
        CJC_ASSERT(base && base->GetType()->IsRef());
        auto loadMemberVal = CreateAndAppendExpression<Load>(
            loc, StaticCast<RefType*>(base->GetType())->GetBaseType(), base, currentBlock);
        return loadMemberVal->GetResult();
    } else if (base->GetType()->IsValueOrGenericTypeWithRefDims(0)) {
        auto memberType = customType->GetInstMemberTypeByPath(leftValueInfo.path, builder);
        auto getMember = CreateAndAppendExpression<Field>(loc, memberType, base, leftValueInfo.path, currentBlock);
        return getMember->GetResult();
    }

    CJC_ABORT();
    return nullptr;
}

Ptr<Value> Translator::TranslateEnumMemberAccess(const AST::MemberAccess& member)
{
    // The target is varDecl.
    // example cangjie code:
    // enum A {
    // C|D(Int64)
    // }
    // var a = A.c // varDecl
    auto enumTy = StaticCast<AST::EnumTy*>(member.baseExpr->ty);
    auto enumDecl = enumTy->decl;
    auto& constructors = enumDecl->constructors;
    auto fieldIt = std::find_if(constructors.begin(), constructors.end(), [&member](auto const& decl) -> bool {
        return decl.get() && decl->astKind == AST::ASTKind::VAR_DECL && decl->identifier == member.field;
    });
    CJC_ASSERT(fieldIt != constructors.end());
    auto enumId = static_cast<uint64_t>(std::distance(constructors.begin(), fieldIt));

    auto ty = chirTy.TranslateType(*enumTy);
    const auto& loc = TranslateLocation(**fieldIt);
    auto selectorTy = GetSelectorType(*enumTy);
    if (!enumTy->decl->hasArguments) {
        auto intExpr = CreateAndAppendConstantExpression<IntLiteral>(loc, selectorTy, *currentBlock, enumId);
        return TypeCastOrBoxIfNeeded(*intExpr->GetResult(), *ty, loc);
    }
    std::vector<Value*> args;
    if (selectorTy->IsBoolean()) {
        auto boolExpr = CreateAndAppendConstantExpression<BoolLiteral>(loc, selectorTy, *currentBlock, enumId);
        args.emplace_back(boolExpr->GetResult());
    } else {
        auto intExpr = CreateAndAppendConstantExpression<IntLiteral>(loc, selectorTy, *currentBlock, enumId);
        args.emplace_back(intExpr->GetResult());
    }

    return CreateAndAppendExpression<Tuple>(TranslateLocation(member), ty, args, currentBlock)
        ->GetResult();
}

Ptr<Value> Translator::TranslateInstanceMemberMemberAccess(const AST::MemberAccess& member)
{
    Ptr<Value> res = nullptr;
    switch (member.target->astKind) {
        case ASTKind::VAR_DECL: {
            res = TranslateVarMemberAccess(member);
            break;
        }
        case ASTKind::FUNC_DECL: {
            res = TranslateFuncMemberAccess(member);
            break;
        }
        default:
            CJC_ABORT();
    }
    return res;
}

Translator::LeftValueInfo Translator::TranslateMemberAccessAsLeftValue(const AST::MemberAccess& member)
{
    auto target = member.target;
    CJC_ASSERT(target->astKind == AST::ASTKind::VAR_DECL);
    const auto& loc = TranslateLocation(member);

    // Case 1: target is case variable in enum
    if (target->TestAttr(AST::Attribute::ENUM_CONSTRUCTOR)) {
        return LeftValueInfo(TranslateASTNode(member, *this), {});
    }

    // Case 2.2: target is global variable or static variable
    if (target->TestAttr(AST::Attribute::STATIC) || IsPackageMemberAccess(member)) {
        auto targetVal = GetSymbolTable(*target);
        CJC_NULLPTR_CHECK(targetVal);
        return LeftValueInfo(targetVal, {});
    }

    // Case 2.4: target is non-static member variable
    if (target->outerDecl && !target->TestAttr(AST::Attribute::STATIC)) {
        // following code is used in serveral places, should wrap into an API
        const AST::Expr* base = &member;
        std::vector<uint64_t> path;
        bool readOnly = false;
        AST::Ty* targetBaseASTTy = nullptr;
        for (;;) {
            base = base->desugarExpr ? base->desugarExpr.get().get() : base;
            if (auto ma = DynamicCast<AST::MemberAccess*>(base)) {
                bool isTargetClassOrClassUpper = ma->ty->IsClassLike() || ma->ty->IsGeneric();
                if ((!isTargetClassOrClassUpper || path.empty()) && !ma->target->TestAttr(AST::Attribute::STATIC) &&
                    ma->target->astKind != ASTKind::PROP_DECL && !IsPackageMemberAccess(*ma)) {
                    path.insert(path.begin(), GetFieldOffset(*ma->target));
                    readOnly = readOnly || !StaticCast<AST::VarDecl*>(ma->target)->isVar;

                    targetBaseASTTy = ma->target->outerDecl->ty;
                    CJC_ASSERT(targetBaseASTTy->IsStruct() || targetBaseASTTy->IsClass());

                    base = ma->baseExpr.get();
                    continue;
                }
                break;
            } else if (auto ref = DynamicCast<AST::RefExpr*>(base)) {
                if (!ref->isThis && !ref->isSuper && !ref->ty->IsClassLike() && !ref->ty->IsGeneric()) {
                    auto refTarget = ref->ref.target;
                    if (refTarget->outerDecl &&
                        (refTarget->outerDecl->astKind == AST::ASTKind::STRUCT_DECL ||
                            refTarget->outerDecl->astKind == AST::ASTKind::CLASS_DECL) &&
                        !refTarget->TestAttr(AST::Attribute::STATIC)) {
                        path.insert(path.begin(), GetFieldOffset(*refTarget));
                        readOnly = readOnly || !StaticCast<AST::VarDecl*>(refTarget)->isVar;

                        targetBaseASTTy = refTarget->outerDecl->ty;
                        CJC_ASSERT(targetBaseASTTy->IsStruct() || targetBaseASTTy->IsClass());

                        // this is a hack
                        base = nullptr;
                    }
                }
                break;
            } else {
                break;
            }
        }

        Value* baseVal = nullptr;
        if (base == nullptr) {
            baseVal = GetImplicitThisParam();
        } else {
            auto baseLeftValueInfo = TranslateExprAsLeftValue(*base);
            auto baseLeftValue = baseLeftValueInfo.base;
            auto baseLeftValueTy = baseLeftValue->GetType();
            if (baseLeftValueTy->IsReferenceTypeWithRefDims(CLASS_REF_DIM)) {
                baseLeftValueTy = StaticCast<RefType*>(baseLeftValueTy)->GetBaseType();
                auto loadBaseValue = CreateAndAppendExpression<Load>(loc, baseLeftValueTy, baseLeftValue, currentBlock);
                baseLeftValue = loadBaseValue->GetResult();
            }
            auto baseLeftValuePath = baseLeftValueInfo.path;
            if (!baseLeftValuePath.empty()) {
                auto baseCustomType = StaticCast<CustomType*>(baseLeftValueTy->StripAllRefs());
                if (baseLeftValueTy->IsReferenceTypeWithRefDims(1) ||
                    baseLeftValueTy->IsValueOrGenericTypeWithRefDims(1)) {
                    auto getMemberRef = CreateGetElementRefWithPath(
                        loc, baseLeftValue, baseLeftValuePath, currentBlock, *baseCustomType);
                    auto memberType = StaticCast<RefType*>(getMemberRef->GetType())->GetBaseType();
                    CJC_ASSERT(memberType->IsReferenceTypeWithRefDims(1) ||
                        memberType->IsValueOrGenericTypeWithRefDims(0));
                    auto loadMemberValue =
                        CreateAndAppendExpression<Load>(loc, memberType, getMemberRef, currentBlock);
                    baseVal = loadMemberValue->GetResult();
                } else if (baseLeftValueTy->IsValueOrGenericTypeWithRefDims(0)) {
                    auto memberType = baseCustomType->GetInstMemberTypeByPath(baseLeftValuePath, builder);
                    CJC_ASSERT(memberType->IsReferenceTypeWithRefDims(1) ||
                        memberType->IsValueOrGenericTypeWithRefDims(0));
                    auto getField = CreateAndAppendExpression<Field>(
                        loc, memberType, baseLeftValue, baseLeftValuePath, currentBlock);
                    baseVal = getField->GetResult();
                }
            } else {
                CJC_ASSERT(baseLeftValueTy->IsReferenceTypeWithRefDims(1) ||
                    baseLeftValueTy->IsValueOrGenericTypeWithRefDims(1) ||
                    baseLeftValueTy->IsValueOrGenericTypeWithRefDims(0));
                baseVal = baseLeftValue;
            }
        }

        auto baseValRefDims = baseVal->GetType()->GetRefDims();
        auto baseValTy = baseVal->GetType()->StripAllRefs();
        std::unordered_map<const GenericType*, Type*> instMap;
        if (auto baseValCustomTy = DynamicCast<CustomType*>(baseValTy)) {
            baseValCustomTy->GetInstMap(instMap, builder);
        } else if (auto baseValGenericTy = DynamicCast<GenericType*>(baseValTy)) {
            baseValGenericTy->GetInstMap(instMap, builder);
        }
        CJC_NULLPTR_CHECK(targetBaseASTTy);
        Type* targetBaseTy = TranslateType(*targetBaseASTTy);
        // Handle the case where the baseValTy is a generic which ref dims is zero
        baseValRefDims = std::max(targetBaseTy->GetRefDims(), baseValRefDims);
        targetBaseTy = targetBaseTy->StripAllRefs();
        targetBaseTy = ReplaceRawGenericArgType(*targetBaseTy, instMap, builder);
        for (size_t i = 0; i < baseValRefDims; ++i) {
            targetBaseTy = builder.GetType<RefType>(targetBaseTy);
        }
        auto castedBaseVal = TypeCastOrBoxIfNeeded(*baseVal, *targetBaseTy, INVALID_LOCATION);

        return LeftValueInfo(castedBaseVal, path);
    }

    CJC_ABORT();
    return LeftValueInfo(nullptr, {});
}

Ptr<Value> Translator::Visit(const AST::MemberAccess& member)
{
    CJC_NULLPTR_CHECK(member.baseExpr);
    CJC_NULLPTR_CHECK(member.target);
    if (member.target && (member.target->TestAttr(AST::Attribute::STATIC) || IsPackageMemberAccess(member))) {
        return TranslateStaticTargetOrPackageMemberAccess(member);
    } else if (member.target->TestAttr(AST::Attribute::ENUM_CONSTRUCTOR)) {
        return TranslateEnumMemberAccess(member);
    } else if (IsInstanceMember(*member.target)) {
        return TranslateInstanceMemberMemberAccess(member);
    }
    InternalError("translating unsupported MemberAccess");
    return nullptr;
}
