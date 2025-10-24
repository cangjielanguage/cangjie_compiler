// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

#include "cangjie/CHIR/AST2CHIR/Utils.h"
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

bool InheritedChainHasCommon(const CustomTypeDef& def)
{
    if (def.TestAttr(CHIR::Attribute::COMMON)) {
        return true;
    }
    for (auto super : def.GetSuperTypesInCurDef()) {
        auto superDef = super->GetCustomTypeDef();
        if (InheritedChainHasCommon(*superDef)) {
            return true;
        }
    }
    return false;
}

bool FuncDeclIsOpen(const AST::FuncDecl& funcDecl)
{
    if (funcDecl.TestAnyAttr(AST::Attribute::GENERIC_INSTANTIATED,
        AST::Attribute::PRIMARY_CONSTRUCTOR, AST::Attribute::CONSTRUCTOR, AST::Attribute::FINALIZER)) {
        return false;
    }
    auto funcHasOpenFlag = funcDecl.TestAnyAttr(AST::Attribute::OPEN, AST::Attribute::ABSTRACT) ||
        (funcDecl.outerDecl && funcDecl.outerDecl->astKind == AST::ASTKind::INTERFACE_DECL);
    auto instanceMethodIsOpen = funcHasOpenFlag &&
        (funcDecl.TestAttr(AST::Attribute::PUBLIC) || funcDecl.TestAttr(AST::Attribute::PROTECTED));
    auto staticMethodIsOpen = funcDecl.outerDecl->IsClassLikeDecl() &&
        funcDecl.TestAttr(AST::Attribute::STATIC) && !funcDecl.TestAttr(AST::Attribute::PRIVATE);
    return instanceMethodIsOpen || staticMethodIsOpen;
}

bool IsInsideCFuncLambda(const CHIR::Block& bl)
{
    auto expr = bl.GetParentBlockGroup()->GetOwnerExpression();
    while (expr) {
        if (auto lambda = Cangjie::DynamicCast<Lambda>(expr)) {
            if (lambda->GetFuncType()->IsCFunc()) {
                return true;
            }
        }
        expr = expr->GetParentBlockGroup()->GetOwnerExpression();
    }
    return false;
}
} // namespace

// `obj.foo()` is a virtual func call ?
bool Translator::IsVirtualFuncCall(
    const CustomTypeDef& obj, const AST::FuncDecl& funcDecl, bool baseExprIsSuper)
{
    /**
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
    if (baseExprIsSuper || IsInsideCFuncLambda(*GetCurrentBlock())) {
        return false;
    }
    if (obj.CanBeInherited() && FuncDeclIsOpen(funcDecl)) {
        return true;
    }
    /** we need to think about two cases:
        1. the func call is in common package
        common package cmp
        public interface I { func foo() {} }
        common public interface I2 <: I {}
        public class C <: I2 {}
        public func goo(x: C) { x.foo() } // for now, maybe I::foo is expected

        platform package cmp
        public interface I { func foo() {} }
        platform public interface I2 <: I { func foo() {} }
        public class C <: I2 {}

        default package
        import cmp.*
        main() { goo(C()) } // we need to call I2::foo, so we need to use `Invoke`, not `Apply` in goo's body

        2. the func call is in another package
        common package cmp
        public interface I { func foo() {} }
        common public interface I2 <: I {}
        public class C <: I2 {}

        platform package cmp
        public interface I { func foo() {} }
        platform public interface I2 <: I { func foo() {} }
        public class C <: I2 {}

        default package
        import cmp.*
        main() { C().foo() } // we need to call I2::foo, so we need to use `Invoke`, not `Apply` here
                             // this package imports cjo from common package, not platform package, so we can't
                             // decide which function is expected according to the information in current package
    */
    return FuncDeclIsOpen(funcDecl) && funcDecl.outerDecl->IsOpen() && InheritedChainHasCommon(obj);
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

std::pair<CHIR::Type*, FuncCallType> Translator::GetExactParentTypeAndFuncType(
    const AST::NameReferenceExpr& expr, Type& thisType, const AST::FuncDecl& funcDecl, bool isVirtualFuncCall)
{
    auto funcType = StaticCast<FuncType*>(TranslateType(*expr.ty));
    auto paramTys = funcType->GetParamTypes();
    if (!funcDecl.TestAttr(AST::Attribute::STATIC)) {
        paramTys.insert(paramTys.begin(), &thisType);
        funcType = builder.GetType<FuncType>(paramTys, funcType->GetReturnType());
    }
    std::vector<Type*> funcInstArgs;
    for (auto ty : expr.instTys) {
        funcInstArgs.emplace_back(TranslateType(*ty));
    }
    auto thisDerefTy = thisType.StripAllRefs();
    auto parentType = GetExactParentType(*thisDerefTy, funcDecl, *funcType, funcInstArgs, isVirtualFuncCall);
    if (!funcDecl.TestAttr(AST::Attribute::STATIC)) {
        CJC_ASSERT(!paramTys.empty());
        /**
            interface I {
                mut func foo() { goo() }
                mut func goo() {}
            }
            struct S <: I {
                mut func goo() {}
            }
            S().foo() // even if foo's parent type is `I`, we shouldn't typecast `S()` from `S&` to `I&`,
                      // because `S&` is a memory in stack, but `I&` is a memory in heap, so if typecast happened,
                      // `S()` will be copied to heap, that will not implement `mut` semantic correctly.
        */
        if (thisDerefTy->IsStruct() && parentType != thisDerefTy && funcDecl.TestAttr(AST::Attribute::MUT)) {
            paramTys[0] = &thisType;
        } else if (parentType->IsClass()) {
            paramTys[0] = AddRefIfFuncIsMutOrClass(*parentType, funcDecl, builder);
            funcType = builder.GetType<FuncType>(paramTys, funcType->GetReturnType());
        }
    }
    return {parentType, FuncCallType{funcDecl.identifier.Val(), funcType, funcInstArgs}};
}

Translator::InstCalleeInfo Translator::GetInstCalleeInfoFromVarInit(const AST::RefExpr& expr)
{
    /**
     *  class A {
     *      static func foo() {}
     *      static let x = if (...) { foo }
     *  }
     *  maybe we are translating `foo` in static member var `x`'s initializer, its initializer is a global func
     *  which added by CHIR, and this `foo` can't be from class A's sub type, must be from class A or its parent type
     */
    auto funcType = StaticCast<FuncType*>(TranslateType(*expr.ty));
    auto paramTys = funcType->GetParamTypes();
    auto funcDecl = StaticCast<AST::FuncDecl*>(expr.ref.target);
    CJC_NULLPTR_CHECK(funcDecl->outerDecl);
    CJC_ASSERT(funcDecl->TestAttr(AST::Attribute::STATIC));
    auto parentType = GetNominalSymbolTable(*funcDecl->outerDecl)->GetType();
    return InstCalleeInfo {
        .instParentCustomTy = parentType,
        .thisType = parentType,
        .instParamTys = paramTys,
        .instRetTy = funcType->GetReturnType(),
        .isVirtualFuncCall = false
    };
}

Translator::InstCalleeInfo Translator::GetInstCalleeInfoFromRefExpr(const AST::RefExpr& expr)
{
    auto currentFunc = GetCurrentFunc();
    CJC_NULLPTR_CHECK(currentFunc);
    if (currentFunc->IsGVInit() || currentFunc->IsStaticInit()) {
        return GetInstCalleeInfoFromVarInit(expr);
    }
    
    // 1. calculate `thisType`
    auto thisType = currentFunc->GetParentCustomTypeOrExtendedType();
    CJC_NULLPTR_CHECK(thisType);
    auto thisDerefTy = thisType;
    auto funcDecl = StaticCast<AST::FuncDecl*>(expr.ref.target);
    thisType = AddRefIfFuncIsMutOrClass(*thisType, *funcDecl, builder);

    // 2. calculate if is virtual func call
    auto caller = currentFunc->GetParentCustomTypeDef();
    CJC_NULLPTR_CHECK(caller);
    /*  a. for builtin type, the `caller` should be extend def, and this function call may be a dynamic dispatch
          because of CJMP feature, you can read the comment in `IsVirtualFuncCall` for more details.
        b. for custom type, the `caller` should be extended def, `currentFunc` may be called by sub type, for example:
        open class A {
            static public func goo() { println("1") }
        }
        extend A {
            static func foo() { goo() }
        }
        class B <: A {
            static public func goo() {  println("2") }
        }
        B.foo()  // should print "2"
    */
    if (auto customType = DynamicCast<CustomType*>(thisDerefTy)) {
        caller = customType->GetCustomTypeDef();
    }
    auto isVirtualFuncCall = IsVirtualFuncCall(*caller, *funcDecl, false);

    // 3. calculate parent type and func type
    auto [parentType, funcCallType] = GetExactParentTypeAndFuncType(expr, *thisType, *funcDecl, isVirtualFuncCall);
    if (isVirtualFuncCall) {
        thisType = builder.GetType<RefType>(builder.GetType<ThisType>());
    }
    return InstCalleeInfo {
        .instParentCustomTy = parentType,
        .thisType = thisType,
        .instParamTys = funcCallType.funcType->GetParamTypes(),
        .instRetTy = funcCallType.funcType->GetReturnType(),
        .instantiatedTypeArgs = std::move(funcCallType.genericTypeArgs),
        .isVirtualFuncCall = isVirtualFuncCall
    };
}

Translator::InstCalleeInfo Translator::GetInstCalleeInfoFromMemberAccess(const AST::MemberAccess& expr)
{
    // 1. calculate `thisType`
    auto thisType = TranslateType(*expr.baseExpr->ty);
    auto funcDecl = StaticCast<AST::FuncDecl*>(expr.target);
    thisType = AddRefIfFuncIsMutOrClass(*thisType, *funcDecl, builder);

    // 2. calculate if is virtual func call

    auto thisDerefTy = thisType->StripAllRefs();
    bool isVirtualFuncCall = false;
    bool isSuper = false;
    if (auto base = DynamicCast<RefExpr*>(expr.baseExpr.get()); (base && base->isSuper)) {
        isSuper = true;
    }
    // only `obj.foo()` and `T.foo()` need to calculate if is virtual func call
    // `classA.foo()` is Apply call
    if (auto customType = DynamicCast<CustomType*>(thisDerefTy); customType &&
        !funcDecl->TestAttr(AST::Attribute::STATIC)) {
        isVirtualFuncCall = IsVirtualFuncCall(*customType->GetCustomTypeDef(), *funcDecl, isSuper);
    } else if (auto genericType = DynamicCast<GenericType*>(thisDerefTy)) {
    isVirtualFuncCall = true;
        // maybe T <: open class A & non-open class B, then it still not be virtual func call
        for (auto ty : genericType->GetUpperBounds()) {
            auto customDef = StaticCast<ClassType*>(ty->StripAllRefs())->GetClassDef();
            isVirtualFuncCall &= IsVirtualFuncCall(*customDef, *funcDecl, isSuper);
        }
    }

    // 3. calculate parent type and func type
    auto [parentType, funcCallType] = GetExactParentTypeAndFuncType(expr, *thisType, *funcDecl, isVirtualFuncCall);
    return InstCalleeInfo {
        .instParentCustomTy = parentType,
        .thisType = thisType,
        .instParamTys = funcCallType.funcType->GetParamTypes(),
        .instRetTy = funcCallType.funcType->GetReturnType(),
        .instantiatedTypeArgs = std::move(funcCallType.genericTypeArgs),
        .isVirtualFuncCall = isVirtualFuncCall
    };
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
    if (member.target->astKind == AST::ASTKind::FUNC_DECL && member.target->outerDecl != nullptr) {
        auto instFuncType = GetInstCalleeInfoFromMemberAccess(member);
        return WrapMemberMethodByLambda(*StaticCast<FuncDecl*>(member.target), instFuncType, nullptr);
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
    auto instFuncType = GetInstCalleeInfoFromMemberAccess(member);
    auto funcDecl = StaticCast<AST::FuncDecl*>(member.target);
    CJC_NULLPTR_CHECK(funcDecl->outerDecl);
    auto thisVal = GetCurrentThisObjectByMemberAccess(member, *funcDecl, TranslateLocation(*member.baseExpr));
    return WrapMemberMethodByLambda(*funcDecl, instFuncType, thisVal);
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
        auto memberType = GetInstMemberTypeByName(*customType, leftValueInfo.path, builder);
        auto getMember =
            CreateAndAppendExpression<FieldByName>(loc, memberType, base, leftValueInfo.path, currentBlock);
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
        std::vector<std::string> path;
        bool readOnly = false;
        AST::Ty* targetBaseASTTy = nullptr;
        for (;;) {
            base = base->desugarExpr ? base->desugarExpr.get().get() : base;
            if (auto ma = DynamicCast<AST::MemberAccess*>(base)) {
                bool isTargetClassOrClassUpper = ma->ty->IsClassLike() || ma->ty->IsGeneric();
                if ((!isTargetClassOrClassUpper || path.empty()) && !ma->target->TestAttr(AST::Attribute::STATIC) &&
                    ma->target->astKind != ASTKind::PROP_DECL && !IsPackageMemberAccess(*ma)) {
                    auto name = ma->target->identifier.Val();
                    CJC_ASSERT(!name.empty());
                    path.insert(path.begin(), name);
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
                        auto name = refTarget->identifier.Val();
                        CJC_ASSERT(!name.empty());
                        path.insert(path.begin(), name);
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
                    auto memberType = GetInstMemberTypeByName(*baseCustomType, baseLeftValuePath, builder);
                    CJC_ASSERT(memberType->IsReferenceTypeWithRefDims(1) ||
                        memberType->IsValueOrGenericTypeWithRefDims(0));
                    auto getField = CreateAndAppendExpression<FieldByName>(
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
