// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"

#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/CHIR/Utils/ConstantUtils.h"
#include "cangjie/CHIR/IR/Package.h"
#include "cangjie/Mangle/CHIRManglingUtils.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;
using namespace AST;

namespace {
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
} // namespace

CHIR::Type* Translator::GetThisTypeWithModal(CHIR::Type& thisType, const AST::FuncDecl& funcDecl)
{
    auto result = AddRefIfFuncIsMutOrClass(thisType, funcDecl, builder);
    ModalInfo chirModal{};
    if (funcDecl.funcBody->paramLists[0]->thisParam) {
        chirModal = ASTModal2CHIRModal(funcDecl.funcBody->paramLists[0]->thisParam->modal);
    } else if (funcDecl.propDecl != nullptr) {
        // Accessor (getter/setter) no longer carries an explicit thisParam; its this-modal
        // is the modal of the owning property's type.
        auto propTy = funcDecl.propDecl->type ? funcDecl.propDecl->type->GetTy() : funcDecl.propDecl->GetTy();
        chirModal = ASTModal2CHIRModal(propTy.Mode());
    }
    // Always normalize this-modal (including NONE) so call-site FuncType matches method
    // FuncType and GetExpectedFunc can distinguish modal overloads on `this`.
    return builder.WithModal(result, chirModal);
}

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
    if (baseExprIsSuper) {
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

        specific package cmp
        public interface I { func foo() {} }
        specific public interface I2 <: I { func foo() {} }
        public class C <: I2 {}

        default package
        import cmp.*
        main() { goo(C()) } // we need to call I2::foo, so we need to use `Invoke`, not `Apply` in goo's body

        2. the func call is in another package
        common package cmp
        public interface I { func foo() {} }
        common public interface I2 <: I {}
        public class C <: I2 {}

        specific package cmp
        public interface I { func foo() {} }
        specific public interface I2 <: I { func foo() {} }
        public class C <: I2 {}

        default package
        import cmp.*
        main() { C().foo() } // we need to call I2::foo, so we need to use `Invoke`, not `Apply` here
                             // this package imports cjo from common package, not specific package, so we can't
                             // decide which function is expected according to the information in current package
    */
    return FuncDeclIsOpen(funcDecl) && funcDecl.outerDecl->IsOpen() && InheritedChainHasCommon(obj);
}

Ptr<CHIR::Type> Translator::GetTypeOfInvokeStatic(const AST::Decl& funcDecl)
{
    CJC_NULLPTR_CHECK(funcDecl.outerDecl);
    auto calledClassType = TranslateType(funcDecl.outerDecl->GetTy());
    if (calledClassType->IsRef()) {
        calledClassType = StaticCast<CHIR::RefType*>(calledClassType)->GetBaseType();
        return calledClassType;
    }
    return calledClassType;
}

std::pair<CHIR::Type*, FuncCallType> Translator::GetExactParentTypeAndFuncType(
    const AST::NameReferenceExpr& expr, Type& thisType, const AST::FuncDecl& funcDecl, bool& isVirtualFuncCall)
{
    auto funcType = StaticCast<FuncType*>(TranslateType(expr.GetTy()));
    auto paramTys = funcType->GetParamTypes();
    if (!funcDecl.TestAttr(AST::Attribute::STATIC)) {
        paramTys.insert(paramTys.begin(), GetThisTypeWithModal(thisType, funcDecl));
        funcType = builder.GetType<FuncType>(paramTys, funcType->GetReturnType());
    }
    std::vector<Type*> funcInstArgs;
    for (auto ty : expr.instTys) {
        funcInstArgs.emplace_back(TranslateType(ty));
    }
    auto thisDerefTy = thisType.StripAllRefs();
    // Result parentType is the instantiated parent custom type used as InstCalleeInfo::instParentCustomTy.
    auto parentType = GetExactParentType(*thisDerefTy, funcDecl, *funcType, funcInstArgs, isVirtualFuncCall);
    /** we can't find a parent type in following case:
        interface I { func foo(): Unit }
        class A<T> where T <: I {}
        func goo<X>(a : X) where X <: A<X> {
            a.foo()  // which `foo` we should pick ?
        }

        generic type X's upper bound is A<X> and I(because of T <: I in A<T>)
        because A<X> is not `open`, so there is no sub type of A<X>, so `a.foo()` is not a virtual func call,
        then we should find a function with body in class A<T> and interface I, unfortunately, we can't find one,
        so we have to pick a virtual method, the `foo` in interface I
        Sema doesn't care about virtual func call or not, but CHIR does, otherwise, we will generate many
        virtual func call in O0, that will cause runtime problems
    */
    if (parentType == nullptr) {
        CJC_ASSERT(!isVirtualFuncCall);
        parentType = GetExactParentType(*thisDerefTy, funcDecl, *funcType, funcInstArgs, true);
        CJC_NULLPTR_CHECK(parentType);
        isVirtualFuncCall = true;
    }
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
    if (!funcDecl.TestAttr(AST::Attribute::STATIC) &&
        !(thisDerefTy->IsStruct() && funcDecl.TestAttr(AST::Attribute::MUT))) {
        CJC_ASSERT(!paramTys.empty());
        paramTys[0] = GetThisTypeWithModal(*parentType, funcDecl);
        funcType = builder.GetType<FuncType>(paramTys, funcType->GetReturnType());
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
    auto funcType = StaticCast<FuncType*>(TranslateType(expr.GetTy()));
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
        .isVirtualFuncCall = false,
        .originalFuncDecl = funcDecl
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
    auto funcDecl = StaticCast<AST::FuncDecl*>(expr.ref.target);
    Type* thisType = currentFunc->GetParentCustomTypeOrExtendedType();
    CJC_NULLPTR_CHECK(thisType);
    thisType = GetThisTypeWithModal(*thisType, *funcDecl);

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
    if (auto customType = DynamicCast<CustomType*>(thisType->StripAllRefs())) {
        caller = customType->GetCustomTypeDef();
    }
    auto isVirtualFuncCall = IsVirtualFuncCall(*caller, *funcDecl, false);

    // 3. real Invoke callee is the top-overridden virtual method
    auto originalFuncDecl = isVirtualFuncCall ? typeManager.GetTopOverriddenFuncDecl(funcDecl) : funcDecl;
    CJC_NULLPTR_CHECK(originalFuncDecl);

    // 4. calculate instantiated parent type from originalFuncDecl (vtable src parent of the real callee)
    auto [parentType, funcCallType] =
        GetExactParentTypeAndFuncType(expr, *thisType, *originalFuncDecl, isVirtualFuncCall);
    if (isVirtualFuncCall) {
        thisType = GetThisTypeWithModal(*builder.GetType<ThisType>(), *funcDecl);
    }
    return InstCalleeInfo {
        .instParentCustomTy = parentType, // instantiated parent type for vtable / casting `this`
        .thisType = thisType,
        .instParamTys = funcCallType.funcType->GetParamTypes(),
        .instRetTy = funcCallType.funcType->GetReturnType(),
        .instantiatedTypeArgs = std::move(funcCallType.genericTypeArgs),
        .isVirtualFuncCall = isVirtualFuncCall,
        .originalFuncDecl = originalFuncDecl.get()
    };
}

Translator::InstCalleeInfo Translator::GetInstCalleeInfoFromMemberAccess(const AST::MemberAccess& expr)
{
    // 1. calculate `thisType`
    auto thisType = TranslateType(expr.baseExpr->GetTy());
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

    // 3. real Invoke callee is the top-overridden virtual method
    auto originalFuncDecl = isVirtualFuncCall ? typeManager.GetTopOverriddenFuncDecl(funcDecl) : funcDecl;
    CJC_NULLPTR_CHECK(originalFuncDecl);

    // 4. calculate instantiated parent type from originalFuncDecl (vtable src parent of the real callee)
    auto [parentType, funcCallType] =
        GetExactParentTypeAndFuncType(expr, *thisType, *originalFuncDecl, isVirtualFuncCall);
    return InstCalleeInfo {
        .instParentCustomTy = parentType, // instantiated parent type for vtable / casting `this`
        .thisType = thisType,
        .instParamTys = funcCallType.funcType->GetParamTypes(),
        .instRetTy = funcCallType.funcType->GetReturnType(),
        .instantiatedTypeArgs = std::move(funcCallType.genericTypeArgs),
        .isVirtualFuncCall = isVirtualFuncCall,
        .originalFuncDecl = originalFuncDecl.get()
    };
}

Ptr<Value> Translator::TranslateStaticTargetOrPackageMemberAccess(const AST::MemberAccess& member)
{
    // 1. classA.foo, pkgA.classB.foo
    if (member.target->astKind == AST::ASTKind::FUNC_DECL && member.target->outerDecl != nullptr) {
        auto instFuncType = GetInstCalleeInfoFromMemberAccess(member);
        return WrapMemberMethodByLambda(*StaticCast<FuncDecl*>(member.target), instFuncType, nullptr);
    }
    auto targetNode = GetSymbolTable(*member.target);
    if (member.target->astKind == AST::ASTKind::VAR_DECL) {
        // 2. classA.x, pkgA.x, pkgA.classB.x
        auto targetTy = TranslateType(member.target->GetTy());
        auto resTy = TranslateType(member.GetTy());
        auto loc = TranslateLocation(member);
        auto targetVal = CreateAndAppendExpression<Load>(loc, targetTy, targetNode, currentBlock)->GetResult();
        return TypeCastOrBoxIfNeeded(*targetVal, *resTy, loc);
    } else if (member.target->astKind == AST::ASTKind::FUNC_DECL) {
        // 3. pkgA.foo
        return targetNode;
    } else {
        CJC_ABORT();
    }
    return nullptr;
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
        expr = StaticCast<LocalVar*>(
            CHIR::TypeCastOrBoxIfNeeded(rawThis, expectedTy, builder, *curLambda.GetParentBlock()))->GetExpr();
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
    auto gType = StaticCast<GenericType*>(TranslateType(&ty));
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
    auto enumTy = StaticCast<AST::EnumTy*>(member.baseExpr->DataTy());
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
        AST::ModalTy targetBaseASTTy = {};
        for (;;) {
            base = base->desugarExpr ? base->desugarExpr.get().get() : base;
            if (auto ma = DynamicCast<AST::MemberAccess*>(base)) {
                bool isTargetClassOrClassUpper = ma->GetTy()->IsClassLike() || ma->GetTy()->IsGeneric();
                if ((!isTargetClassOrClassUpper || path.empty()) && !ma->target->TestAttr(AST::Attribute::STATIC) &&
                    ma->target->astKind != ASTKind::PROP_DECL && !IsPackageMemberAccess(*ma)) {
                    auto name = ma->target->identifier.Val();
                    CJC_ASSERT(!name.empty());
                    path.insert(path.begin(), name);
                    readOnly = readOnly || !StaticCast<AST::VarDecl*>(ma->target)->isVar;

                    targetBaseASTTy = ma->GetTarget()->outerDecl->GetTy().With(ma->baseExpr->TyMode());
                    CJC_ASSERT(targetBaseASTTy->IsStruct() || targetBaseASTTy->IsClass());

                    base = ma->baseExpr.get();
                    continue;
                }
                break;
            } else if (auto ref = DynamicCast<AST::RefExpr*>(base)) {
                if (!ref->isThis && !ref->isSuper && !ref->GetTy()->IsClassLike() && !ref->GetTy()->IsGeneric()) {
                    auto refTarget = ref->ref.target;
                    if (refTarget->outerDecl &&
                        (refTarget->outerDecl->astKind == AST::ASTKind::STRUCT_DECL ||
                            refTarget->outerDecl->astKind == AST::ASTKind::CLASS_DECL) &&
                        !refTarget->TestAttr(AST::Attribute::STATIC)) {
                        auto name = refTarget->identifier.Val();
                        CJC_ASSERT(!name.empty());
                        path.insert(path.begin(), name);
                        readOnly = readOnly || !StaticCast<AST::VarDecl*>(refTarget)->isVar;

                        const AST::FuncDecl* curMemberFunc = StaticCast<const AST::FuncDecl*>(topLevelDecl);
                        CJC_ASSERT(curMemberFunc && TypeManager::HasThisParam(*curMemberFunc));
                        auto& tm = const_cast<TypeManager&>(typeManager);
                        targetBaseASTTy = refTarget->outerDecl->GetTy().With(tm.GetThisParamTy(*curMemberFunc).Mode());
                        CJC_ASSERT(targetBaseASTTy.Ty()->IsStruct() || targetBaseASTTy.Ty()->IsClass());

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

        auto baseValRefDims = GetRefDims(*baseVal->GetType());
        auto baseValTy = baseVal->GetType()->StripAllRefs();
        std::unordered_map<const GenericType*, Type*> instMap;
        if (auto baseValCustomTy = DynamicCast<CustomType*>(baseValTy)) {
            baseValCustomTy->GetInstMap(instMap, builder);
        } else if (auto baseValGenericTy = DynamicCast<GenericType*>(baseValTy)) {
            baseValGenericTy->GetInstMap(instMap, builder);
        }
        CJC_NULLPTR_CHECK(targetBaseASTTy);
        Type* targetBaseTy = TranslateType(targetBaseASTTy);
        // Handle the case where the baseValTy is a generic which ref dims is zero
        baseValRefDims = std::max(GetRefDims(*targetBaseTy), baseValRefDims);
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
