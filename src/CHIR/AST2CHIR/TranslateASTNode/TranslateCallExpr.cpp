// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/TranslateASTNode/Translator.h"
#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/AST/Walker.h"
#include "cangjie/CHIR/ConstantUtils.h"
#include "cangjie/CHIR/IntrinsicKind.h"

using namespace Cangjie::CHIR;
using namespace Cangjie;

namespace {
/// Check if a call to \ref func on type \ref baseTy should emit InvokeStatic. That is, the function is static and
/// the outer class of that function is open, and there is generic type in either the type that defines the function,
/// or in the baseExpr of the member access of the call of the function.
bool ShouldInvokeStatic(const AST::FuncDecl& func, const AST::Ty& baseTy)
{
    // private static function is never open, exclude it
    if (!(func.TestAttr(AST::Attribute::STATIC) && !func.TestAttr(AST::Attribute::PRIVATE))) {
        return false;
    }
    // exclude partial instantiation
    CJC_ASSERT(!func.outerDecl->TestAttr(AST::Attribute::GENERIC_INSTANTIATED));
    if (func.outerDecl->IsOpen() &&
        // either function generic, or the type that calls is generic
        (baseTy.HasGeneric() || func.outerDecl->ty->HasGeneric())) {
        return true;
    }
    return Is<AST::InterfaceDecl>(func.outerDecl);
}

bool IsInvokeStatic(const AST::CallExpr& callExpr)
{
    Ptr<AST::FuncDecl> resolvedFunction = callExpr.resolvedFunction;
    CJC_NULLPTR_CHECK(resolvedFunction);

    // sema guarantees that instantiated func/class never use InvokeStatic
    if (resolvedFunction->outerDecl->TestAttr(AST::Attribute::GENERIC_INSTANTIATED) ||
        resolvedFunction->TestAnyAttr(AST::Attribute::PRIVATE, AST::Attribute::GENERIC_INSTANTIATED)) {
        return false;
    }

    if (resolvedFunction->TestAttr(AST::Attribute::ABSTRACT)) {
        return true;
    }

    if (auto memAccess = DynamicCast<AST::MemberAccess*>(callExpr.baseFunc.get())) {
        if (ShouldInvokeStatic(*resolvedFunction, *memAccess->baseExpr->ty)) {
            return true;
        }
    }

    if (auto refExpr = DynamicCast<AST::RefExpr*>(callExpr.baseFunc.get()); refExpr) {
        auto parent = resolvedFunction->outerDecl;
        // static function introduced in extend cannot be redef'ed in subclass
        if (auto extend = DynamicCast<AST::ExtendDecl>(parent)) {
            return false;
        }
        if (parent->IsOpen()) {
            // If the `This` is implicit and callee is defined in interface, then we are definitly
            // in the scope of the interface. In this case, we should do dynamic dispatch
            return true;
        }
    }

    return false;
}
} // namespace

std::vector<Type*> Translator::TranslateASTTypes(const std::vector<Ptr<AST::Ty>>& genericInfos)
{
    std::vector<Type*> ts;
    for (auto& genericInfo : genericInfos) {
        ts.emplace_back(TranslateType(*genericInfo));
    }
    return ts;
}

Ptr<AST::Expr> Translator::GetMapExpr(AST::Node& node) const
{
    if (auto expr = DynamicCast<AST::Expr*>(&node); expr) {
        while (expr != nullptr && expr->desugarExpr != nullptr) {
            expr = expr->desugarExpr.get();
        }
        if (expr != nullptr && expr->mapExpr != nullptr) {
            auto base = expr->mapExpr;
            return base;
        }
    }
    return nullptr;
}

// Init not called by 'this' or 'super'
static bool IsCallRegularInit(const AST::CallExpr& expr)
{
    if (expr.resolvedFunction && IsInstanceConstructor(*expr.resolvedFunction)) {
        bool callOtherInit = expr.baseFunc->astKind == AST::ASTKind::REF_EXPR &&
            (StaticCast<AST::RefExpr*>(expr.baseFunc.get())->isThis ||
                StaticCast<AST::RefExpr*>(expr.baseFunc.get())->isSuper);
        return !callOtherInit;
    }

    return false;
}

std::vector<Type*> Translator::GetFuncInstArgs(const AST::CallExpr& expr)
{
    CJC_ASSERT(expr.resolvedFunction != nullptr);
    std::vector<Type*> funcInstTypeArgs;
    if (auto nre = DynamicCast<AST::NameReferenceExpr*>(expr.baseFunc.get())) {
        // Skip the constructor since the instantiation type args there is for the parent custom type not for the
        // function call，e.g. let x = CA<Int64>()
        if (!expr.resolvedFunction->TestAttr(AST::Attribute::CONSTRUCTOR)) {
            for (auto& instTy : nre->instTys) {
                funcInstTypeArgs.emplace_back(TranslateType(*instTy));
            }
        }
    }
    return funcInstTypeArgs;
}

static bool IsPropertySetterCall(const AST::CallExpr& expr)
{
    auto func = expr.resolvedFunction;
    bool res = func->isSetter;
    return res;
}

Expression* Translator::GenerateDynmaicDispatchFuncCall(const InstInvokeCalleeInfo& funcInfo,
    const std::vector<Value*>& args, Value* thisObj, Value* thisRTTI, DebugLocation loc)
{
    auto instantiatedParamTys = funcInfo.instFuncType->GetParamTypes();

    // Step 1: for the func args, cast it to the corresponding func param type if necessary
    std::vector<Value*> castedArgs;
    Value* castedThisObj = thisObj;
    if (thisObj != nullptr) {
        CJC_ASSERT(args.size() == instantiatedParamTys.size() - 1);
        // do we really need this cast
        castedThisObj = TypeCastOrBoxIfNeeded(*thisObj, *instantiatedParamTys[0], loc);
        for (size_t i = 0; i < args.size(); ++i) {
            auto castedArg = TypeCastOrBoxIfNeeded(*args[i], *instantiatedParamTys[i + 1], loc);
            castedArgs.emplace_back(castedArg);
        }
    } else {
        CJC_ASSERT(args.size() == instantiatedParamTys.size());
        for (size_t i = 0; i < args.size(); ++i) {
            auto castedArg = TypeCastOrBoxIfNeeded(*args[i], *instantiatedParamTys[i], loc);
            castedArgs.emplace_back(castedArg);
        }
    }

    // Step 2: create the func call (might be a `Invoke` or `InvokeWithException`) and set
    // its instantiated type info
    auto invokeInfo = InvokeCallContext {
        .caller = (thisObj == nullptr) ? thisRTTI : castedThisObj,
        .funcCallCtx = FuncCallContext {
            .args = castedArgs,
            .instTypeArgs = funcInfo.instantiatedTypeArgs,
            .thisType = funcInfo.thisType
        },
        .virMethodCtx = VirMethodContext {
            .srcCodeIdentifier = funcInfo.srcCodeIdentifier,
            .originalFuncType = funcInfo.originalFuncType,
            .offset = funcInfo.offset
        }
    };
    if (thisObj != nullptr) {
        return TryCreate<Invoke>(currentBlock, loc, funcInfo.instFuncType->GetReturnType(), invokeInfo);
    } else {
        return TryCreate<InvokeStatic>(currentBlock, loc, funcInfo.instFuncType->GetReturnType(), invokeInfo);
    }
}

Ptr<Value> Translator::GetCurrentThisObject(const AST::FuncDecl& resolved)
{
    auto curFunc = GetCurrentFunc();
    CJC_NULLPTR_CHECK(curFunc);
    auto thisVar = curFunc->GetParam(0);
    bool isThisRef =
        curFunc->IsConstructor() || curFunc->TestAttr(Attribute::MUT) || curFunc->GetFuncKind() == FuncKind::SETTER;
    bool needThisRef =
        resolved.TestAttr(AST::Attribute::MUT) || resolved.TestAttr(AST::Attribute::CONSTRUCTOR) || resolved.isSetter;
    if (IsStructOrExtendMethod(*curFunc) && isThisRef && !needThisRef) {
        auto objType = thisVar->GetType();
        CJC_ASSERT(objType->IsRef() && !StaticCast<RefType*>(objType)->GetBaseType()->IsRef());
        auto objBaseType = StaticCast<RefType*>(objType)->GetBaseType();
        CJC_ASSERT(objBaseType->IsStruct());
        return CreateAndAppendExpression<Load>(objBaseType, thisVar, currentBlock)->GetResult();
    } else {
        return thisVar;
    }
}

Value* Translator::GetCurrentThisObjectByMemberAccess(const AST::MemberAccess& memAccess, const AST::FuncDecl& resolved,
    const DebugLocation& loc)
{
    // this or super call:this.f(), super.f()
    if (AST::IsThisOrSuper(*memAccess.baseExpr)) {
        // MemberAccess must not be constructor call.
        return GetCurrentThisObject(resolved);
    }
    // member access except this or super call
    auto curObj = TranslateExprArg(*memAccess.baseExpr);
    CJC_NULLPTR_CHECK(curObj);
    if (memAccess.baseExpr->ty->IsClassLike()) {
        // class A {func foo(){return 0}
        // var a = A()
        // a.f()           // a is A&& need add `Load`
        // let b = A()
        // b.f()           // b is A& don't need add `Load`
        auto objType = curObj->GetType();
        if (objType->IsRef()) {
            // objType is A&& or A&
            auto objBaseType = StaticCast<RefType*>(objType)->GetBaseType();
            if (objBaseType->IsRef()) {
                // for example: objBaseType is A&
                auto derefedObj = CreateAndAppendExpression<Load>(
                    TranslateLocation(*memAccess.baseExpr), objBaseType, curObj, currentBlock)
                                      ->GetResult();
                derefedObj->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
                return derefedObj;
            }
        }
        return curObj;
    } else if (!memAccess.baseExpr->ty->IsStruct() || !resolved.TestAttr(AST::Attribute::MUT)) {
        // Non-struct and non-classlike type must perform deref.
        // If `this` obj's type is struct, and resolved function is not `mut`, need add `Load`.
        // struct A {func foo(){return 0}
        // var a = A()
        // a.foo()           // a is A& need add `Load`
        // let b = A()
        // b.foo()           // b is A don't need add `Load`
        auto objType = curObj->GetType();
        if (objType->IsRef()) {
            // objType is A&
            auto objBaseType = StaticCast<RefType*>(objType)->GetBaseType();
            CJC_ASSERT(!objBaseType->IsRef());
            // for example: objBaseType is A
            curObj = CreateAndAppendExpression<Load>(loc, objBaseType, curObj, currentBlock)->GetResult();
        }
    }
    return curObj;
}

// this API should be moved to some other place
Translator::LeftValueInfo Translator::TranslateExprAsLeftValue(const AST::Expr& expr)
{
    auto base = &expr;
    auto backBlock = currentBlock;
    auto desugared = GetDesugaredExpr(expr);
    if (auto dexpr = DynamicCast<AST::Expr*>(desugared); dexpr && dexpr->mapExpr != nullptr) {
        base = dexpr->mapExpr;
    }
    if (auto res = exprValueTable.TryGet(*base)) {
        return LeftValueInfo(res, {});
    }

    if (desugared != &expr) {
        LeftValueInfo res = LeftValueInfo(nullptr, {});
        if (auto dexpr = DynamicCast<AST::Expr*>(desugared)) {
            res = TranslateExprAsLeftValue(*StaticCast<AST::Expr*>(dexpr));
        } else {
            res = LeftValueInfo(TranslateASTNode(*desugared, *this), {});
        }

        /* There are two cases need add `Goto` when translate AST::Block
            case1: if the Block node is a desugar node, then add `Goto`.
            example code：
                                                                                                 |     |   desugar block
            `print("${a.a.b}\n")` => desugar to  `print({var tmp1 = Stringbuilder(); tmp1.append({a.a.b})})`
                                                        |                                                | desugar block
            case2: unsafe block.
            example code:
            var a = unsafe{}
        */
        if (auto subBlock = DynamicCast<AST::Block*>(desugared)) {
            if (desugared != &expr || desugared->TestAttr(AST::Attribute::UNSAFE)) {
                CreateAndAppendTerminator<GoTo>(GetBlockByAST(*subBlock), backBlock);
            }
        }

        return res;
    }

    if (expr.astKind == AST::ASTKind::REF_EXPR) {
        return TranslateRefExprAsLeftValue(*StaticCast<AST::RefExpr*>(&expr));
    } else if (expr.astKind == AST::ASTKind::MEMBER_ACCESS) {
        return TranslateMemberAccessAsLeftValue(*StaticCast<AST::MemberAccess*>(&expr));
    } else if (expr.astKind == AST::ASTKind::PAREN_EXPR) {
        auto parenExpr = StaticCast<AST::ParenExpr*>(&expr);
        return TranslateExprAsLeftValue(*parenExpr->expr);
    } else if (expr.astKind == AST::ASTKind::CALL_EXPR) {
        return TranslateCallExprAsLeftValue(*StaticCast<AST::CallExpr*>(&expr));
    } else {
        return LeftValueInfo(TranslateASTNode(expr, *this), {});
    }
}

Value* Translator::GenerateLeftValue(const Translator::LeftValueInfo& leftValInfo, const DebugLocation& loc)
{
    Value* result = leftValInfo.base;
    if (!leftValInfo.path.empty()) {
        auto baseCustomType = StaticCast<CustomType*>(result->GetType()->StripAllRefs());
        auto memberType = baseCustomType->GetInstMemberTypeByPath(leftValInfo.path, builder);
        if (result->GetType()->IsRef()) {
            auto memberRefType = builder.GetType<RefType>(memberType);
            auto getMemberRef =
                CreateAndAppendExpression<GetElementRef>(loc, memberRefType, result, leftValInfo.path, currentBlock);
            result = getMemberRef->GetResult();
        } else {
            auto getMember = CreateAndAppendExpression<Field>(loc, memberType, result, leftValInfo.path, currentBlock);
            result = getMember->GetResult();
        }
    }
    return result;
}

void Translator::TranslateThisObjectForNonStaticMemberFuncCall(
    const AST::CallExpr& expr, std::vector<Value*>& args, bool needsMutableThis)
{
    Ptr<AST::FuncDecl> resolved = expr.resolvedFunction;
    CJC_ASSERT(resolved && IsInstanceMember(*resolved));
    CJC_NULLPTR_CHECK(resolved->outerDecl);
    // polish here
    // When current is calling a constructor and is not called with 'this' or 'super',
    // it should not using 'this' existed in context.
    CJC_ASSERT(!IsCallRegularInit(expr));

    Value* thisObj = nullptr;
    auto loc = TranslateLocation(expr);
    if (auto memAccess = DynamicCast<AST::MemberAccess*>(expr.baseFunc.get())) {
        if (AST::IsThisOrSuper(*memAccess->baseExpr)) {
            // Case A: the member access is in form like "this.f()" or "super.f()", then we just get the "this" param
            // from current func
            thisObj = GetCurrentThisObject(*resolved);
        } else {
            // Case B: otherwise, we will generate the base part of the member access and get the "this"
            auto thisObjValueInfo = TranslateExprAsLeftValue(*memAccess->baseExpr);
            thisObj = thisObjValueInfo.base;
            // polish this
            if (!thisObjValueInfo.path.empty()) {
                auto lhsCustomType = StaticCast<CustomType*>(thisObj->GetType()->StripAllRefs());
                if (thisObj->GetType()->IsRef()) {
                    thisObj =
                        CreateGetElementRefWithPath(loc, thisObj, thisObjValueInfo.path, currentBlock, *lhsCustomType);
                } else {
                    auto memberType = lhsCustomType->GetInstMemberTypeByPath(thisObjValueInfo.path, builder);
                    auto getMember =
                        CreateAndAppendExpression<Field>(loc, memberType, thisObj, thisObjValueInfo.path, currentBlock);
                    thisObj = getMember->GetResult();
                }
            }
        }
        // this case only happends when extending Unit or Nothing type
        if (thisObj == nullptr) {
            if (memAccess->baseExpr->ty->IsUnit()) {
                thisObj = CreateAndAppendConstantExpression<UnitLiteral>(builder.GetUnitTy(), *currentBlock)
                    ->GetResult();
            } else if (memAccess->baseExpr->ty->IsNothing()) {
                thisObj = CreateAndAppendConstantExpression<NullLiteral>(builder.GetNothingType(), *currentBlock)
                    ->GetResult();
            } else {
                CJC_ABORT();
            }
        }
    } else {
        thisObj = GetCurrentThisObject(*resolved);
    }
    auto thisObjTy = thisObj->GetType();
    CJC_ASSERT(thisObjTy->IsReferenceTypeWithRefDims(CLASS_REF_DIM) || thisObjTy->IsReferenceTypeWithRefDims(1) ||
        thisObjTy->IsValueOrGenericTypeWithRefDims(1) || thisObjTy->IsValueOrGenericTypeWithRefDims(0));
    if (!needsMutableThis) {
        if (thisObjTy->IsReferenceTypeWithRefDims(CLASS_REF_DIM) || thisObjTy->IsValueOrGenericTypeWithRefDims(1)) {
            auto pureThisObjTy = thisObjTy->StripAllRefs();
            auto targetTy = pureThisObjTy;
            if (pureThisObjTy->IsReferenceType()) {
                targetTy = builder.GetType<RefType>(pureThisObjTy);
            }
            thisObj = CreateAndAppendExpression<Load>(loc, targetTy, thisObj, currentBlock)->GetResult();
        }
    } else {
        CJC_ASSERT(!thisObjTy->IsValueOrGenericTypeWithRefDims(0));
        if (thisObjTy->IsReferenceTypeWithRefDims(CLASS_REF_DIM)) {
            auto pureThisObjTy = thisObjTy->StripAllRefs();
            auto targetTy = builder.GetType<RefType>(pureThisObjTy);
            thisObj = CreateAndAppendExpression<Load>(loc, targetTy, thisObj, currentBlock)->GetResult();
        }
    }

    args.insert(args.begin(), thisObj);
}

void Translator::TranslateTrivialArgsWithSugar(
    const AST::CallExpr& expr, std::vector<Value*>& args, const std::vector<Type*>& expectedArgTys)
{
    Ptr<AST::FuncDecl> resolved = expr.resolvedFunction;
    CJC_ASSERT(resolved->funcBody && !resolved->funcBody->paramLists.empty());
    CJC_ASSERT(resolved->funcBody->paramLists[0]->params.size() == expr.desugarArgs.value().size() ||
        resolved->hasVariableLenArg);

    auto& argExprs = expr.desugarArgs.value();
    auto& params = resolved->funcBody->paramLists[0]->params;
    const auto& loc = TranslateLocation(expr);

    for (size_t i = 0; i < argExprs.size(); i++) {
        if (argExprs[i]->TestAttr(AST::Attribute::HAS_INITIAL)) {
            // In this case, the corresponding func param has default value which has been desugared into
            // a default-value-func, thus the func arg expr here will becomes a call to the default-value-func
            // which use all the previous args as input. For example:
            //      Original Code:
            //          func foo(x: Int64, y!: Int64 = x + 1) {...}
            //          let res = foo(1)
            //
            //      Desugared Result:
            //          func foo(x: Int64, y: Int64) {...}
            //          func foo_y_defalut_value(x: Int64) { x + 1 }
            //          let res = foo(1, foo_y_defalut_value(1))

            // 1) get the default-value-func
            CJC_NULLPTR_CHECK(params[i]->desugarDecl.get());
            auto defaultValueFunc = GetSymbolTable(*params[i]->desugarDecl);

            // 2) collect the previous args as the input of the call to default-value-func
            std::vector<Value*> defaultValueFuncArgs;
            for (size_t j = 0; j < args.size(); ++j) {
                defaultValueFuncArgs.emplace_back(args[j]);
            }

            // 3) calculte the instantiated type of the default-value-func
            auto instDefaultValueFuncRetTy = TranslateType(*argExprs[i]->ty);
            std::vector<Type*> instDefaultValueFuncParamInstTys;
            for (size_t k = 0; k < defaultValueFuncArgs.size(); ++k) {
                instDefaultValueFuncParamInstTys.emplace_back(defaultValueFuncArgs[k]->GetType());
            }
            auto instDefaultValueFuncTy =
                builder.GetType<FuncType>(instDefaultValueFuncParamInstTys, instDefaultValueFuncRetTy);
            auto thisInstType = GetMemberFuncCallerInstType(expr);

            std::vector<Type*> instArgs;
            // e.g. class A<T> { init(a!: Int64 = 1) }; var x = A<Int32>()
            // `A<Int32>()` is callExpr for class constructor, it use `instTys` to store Int32, but desugar func is
            // `a.0(): Int64 {...}` without generic param. So this apply should be `A<Int32>(a.0())`.
            if (expr.resolvedFunction == nullptr || !IsClassOrEnumConstructor(*expr.resolvedFunction)) {
                auto instParamsInOwnerFunc = StaticCast<AST::NameReferenceExpr*>(expr.baseFunc.get())->instTys;
                for (auto ty : instParamsInOwnerFunc) {
                    instArgs.emplace_back(TranslateType(*ty));
                }
            }

            // check the this type and instParentCustomType value here
            auto defaultValueCall = GenerateFuncCall(*defaultValueFunc, instDefaultValueFuncTy, std::move(instArgs),
                thisInstType, defaultValueFuncArgs, loc);
            auto ret = defaultValueCall->GetResult();

            Value* castedRet = ret;
            // remove this condition later
            if (!expectedArgTys.empty()) {
                castedRet = GenerateLoadIfNeccessary(*ret, false, false, false, loc);
                CJC_ASSERT(expectedArgTys.size() > i);
                castedRet = TypeCastOrBoxIfNeeded(*castedRet, *expectedArgTys[i], loc);
            }
            args.emplace_back(castedRet);
        } else {
            Type* expectedArgTy = nullptr;
            if (!expectedArgTys.empty()) {
                if (i < expectedArgTys.size()) {
                    expectedArgTy = expectedArgTys[i];
                } else {
                    CJC_ASSERT(resolved->ty->IsCFunc());
                }
            }
            Value* argVal = TranslateTrivialArgWithNoSugar(*argExprs[i], expectedArgTy, loc);
            args.emplace_back(argVal);
        }
    }
}

Value* Translator::TranslateTrivialArgWithNoSugar(
    const AST::FuncArg& arg, Type* expectedArgTy, const DebugLocation& loc)
{
    Value* argVal = nullptr;
    if (arg.withInout) {
        auto argLeftValInfo = TranslateExprAsLeftValue(*arg.expr);
        argVal = GenerateLeftValue(argLeftValInfo, loc);
        auto ty = TranslateType(*arg.ty);
        auto callContext = IntrisicCallContext {
            .kind = IntrinsicKind::INOUT_PARAM,
            .args = std::vector<Value*>{argVal}
        };
        argVal = CreateAndAppendExpression<Intrinsic>(loc, ty, callContext, currentBlock)->GetResult();
    } else {
        argVal = TranslateExprArg(*arg.expr);
        // This load should be able to remove since we are always generate right value from
        // `TranslateASTNode` API
        argVal = GenerateLoadIfNeccessary(*argVal, false, false, false, loc);
        if (expectedArgTy) {
            argVal = TypeCastOrBoxIfNeeded(*argVal, *expectedArgTy, loc);
        }
    }
    CJC_NULLPTR_CHECK(argVal);
    return argVal;
}

void Translator::TranslateTrivialArgsWithNoSugar(
    const AST::CallExpr& expr, std::vector<Value*>& args, const std::vector<Type*>& expectedArgTys)
{
    auto loc = TranslateLocation(expr);
    bool needCastToExpectedTy = !expectedArgTys.empty();
    size_t i = 0;
    for (auto& arg : expr.args) {
        Type* expectedArgTy = nullptr;
        if (needCastToExpectedTy) {
            if (i < expectedArgTys.size()) {
                expectedArgTy = expectedArgTys[i];
            } else {
                CJC_ASSERT(expr.resolvedFunction && expr.resolvedFunction->ty->IsCFunc());
            }
        }
        Value* argVal = TranslateTrivialArgWithNoSugar(*arg, expectedArgTy, loc);
        args.emplace_back(argVal);
        ++i;
    }
}

void Translator::TranslateTrivialArgs(
    const AST::CallExpr& expr, std::vector<Value*>& args, const std::vector<Type*>& expectedArgTys)
{
    if (expr.desugarArgs.has_value()) {
        TranslateTrivialArgsWithSugar(expr, args, expectedArgTys);
    } else {
        TranslateTrivialArgsWithNoSugar(expr, args, expectedArgTys);
    }
}

Ptr<Value> Translator::TranslateIntrinsicCall(const AST::CallExpr& expr)
{
    // Conditions to check if this is a call to intrinsic
    if (expr.callKind != AST::CallKind::CALL_INTRINSIC_FUNCTION) {
        return nullptr;
    }

    auto target = expr.baseFunc->GetTarget();
    CJC_NULLPTR_CHECK(target);
    std::string identfifier = target->identifier;

    // Translate code position info
    const auto& loc = TranslateLocation(expr);

    auto ty = chirTy.TranslateType(*expr.ty);

    // Get the intrinsic kind
    std::string packageName{};
    if (target->genericDecl) {
        packageName = target->genericDecl->fullPackageName;
    } else if (target->outerDecl && target->outerDecl->genericDecl) {
        packageName = target->outerDecl->genericDecl->fullPackageName;
    } else {
        packageName = target->fullPackageName;
    }
    CHIR::IntrinsicKind intrinsicKind{NOT_INTRINSIC};
    if (auto it = packageMap.find(packageName); it != packageMap.end()) {
        CJC_ASSERT(it->second.find(identfifier) != it->second.end());
        intrinsicKind = it->second.at(identfifier);
    } else if (auto it1 = headlessIntrinsics.find(identfifier); it1 != headlessIntrinsics.end()) {
        intrinsicKind = it1->second;
    }

    // Translate arguments
    std::vector<Value*> args;
    TranslateTrivialArgs(expr, args, std::vector<Type*>{});
    auto ne = StaticCast<AST::NameReferenceExpr*>(expr.baseFunc.get());
    // wrap this into the `GenerateFuncCall` API
    auto callContext = IntrisicCallContext {
        .kind = intrinsicKind,
        .args = args,
        .instTypeArgs = TranslateASTTypes(ne->instTys)
    };
    auto intriVar = TryCreate<Intrinsic>(currentBlock, loc, ty, callContext)->GetResult();

    // what is this for
    if (expr.ty->IsUnit()) {
        // Codegen will not generate valid 'unit' value for intrinsic call.
        return CreateAndAppendConstantExpression<UnitLiteral>(builder.GetUnitTy(), *currentBlock)->GetResult();
    }

    return intriVar;
}

Ptr<Value> Translator::TranslateForeignFuncCall(const AST::CallExpr& expr)
{
    // Conditions to check if this is a call to foreign func
    if (expr.resolvedFunction == nullptr) {
        return nullptr;
    }
    if (!expr.resolvedFunction->TestAttr(AST::Attribute::FOREIGN)) {
        return nullptr;
    }

    auto resolvedFunction = expr.resolvedFunction;

    // Translate code position info
    const auto& loc = TranslateLocation(expr);
    const auto& warningLoc = TranslateLocation(*expr.baseFunc);

    // polish this API
    auto [paramInstTys, retInstTy] = GetMemberFuncParamAndRetInstTypes(expr);
    bool hasVarArg = StaticCast<AST::FuncTy*>(expr.resolvedFunction->ty)->hasVariableLenArg;
    bool isCFunc = StaticCast<AST::FuncTy*>(expr.resolvedFunction->ty)->isC;
    auto instTargetFuncTy = builder.GetType<FuncType>(paramInstTys, retInstTy, hasVarArg, isCFunc);

    // Translate arguments
    std::vector<Value*> args;
    TranslateTrivialArgs(expr, args, paramInstTys);

    auto callee = GetSymbolTable(*resolvedFunction);
    CJC_ASSERT(callee != nullptr && "TranslateApply: not supported callee now!");
    auto funcCall = GenerateFuncCall(*callee, instTargetFuncTy, {}, nullptr, args, loc);
    if (HasNothingTypeArg(args)) {
        funcCall->Set<DebugLocationInfoForWarning>(warningLoc);
    }

    auto targetCallResTy = TranslateType(*expr.ty);
    auto castedCallRes = TypeCastOrBoxIfNeeded(*funcCall->GetResult(), *targetCallResTy, loc);
    return castedCallRes;
}

Ptr<Value> Translator::TranslateCStringCtorCall(const AST::CallExpr& expr)
{
    if (auto target = DynamicCast<AST::BuiltInDecl*>(expr.baseFunc->GetTarget());
        target && target->type == AST::BuiltInType::CSTRING) {
        auto ty = TranslateType(*expr.ty);
        const auto& loc = TranslateLocation(expr);
        CJC_ASSERT(expr.args.size() == 1);
        auto argVal = TranslateExprArg(*expr.args[0]);
        auto callContext = IntrisicCallContext {
            .kind = IntrinsicKind::CSTRING_INIT,
            .args = std::vector<Value*>{argVal}
        };
        return CreateAndAppendExpression<Intrinsic>(loc, ty, callContext, currentBlock)->GetResult();
    }
    return nullptr;
}

Ptr<Value> Translator::TranslateEnumCtorCall(const AST::CallExpr& expr)
{
    // Conditions to check if this is a call to construct an enum value
    if (expr.resolvedFunction == nullptr) {
        return nullptr;
    }
    if (!expr.resolvedFunction->TestAttr(AST::Attribute::ENUM_CONSTRUCTOR)) {
        return nullptr;
    }

    auto resolvedFunction = expr.resolvedFunction;

    // Translate code position info
    const auto& loc = TranslateLocation(expr);

    // Get the enum case ID
    auto enumDecl = resolvedFunction->funcBody->parentEnum;
    CJC_NULLPTR_CHECK(enumDecl);
    auto& constrs = enumDecl->constructors;
    auto fieldIt = std::find_if(constrs.begin(), constrs.end(),
        [&resolvedFunction](auto const& decl) -> bool { return resolvedFunction == decl.get(); });
    CJC_ASSERT(fieldIt != constrs.end());
    auto enumId = static_cast<uint64_t>(std::distance(constrs.begin(), fieldIt));

    // Calculate instantiated callee func type
    // polish this API
    auto [paramInstTys, retInstTy] = GetMemberFuncParamAndRetInstTypes(expr);

    // Translate arguments
    std::vector<Value*> args;
    TranslateTrivialArgs(expr, args, paramInstTys);
    auto ty = chirTy.TranslateType(*expr.ty);
    auto selectorTy = GetSelectorType(*StaticCast<AST::EnumTy>(expr.ty));
    CJC_ASSERT(ty->IsEnum());
    auto constExpr = (selectorTy->IsBoolean()
            ? CreateAndAppendConstantExpression<BoolLiteral>(
                  loc, selectorTy, *currentBlock, static_cast<bool>(enumId))
            : CreateAndAppendConstantExpression<IntLiteral>(loc, selectorTy, *currentBlock, enumId));
    args.insert(args.begin(), constExpr->GetResult());

    return CreateAndAppendExpression<Tuple>(TranslateLocation(expr), ty, args, currentBlock)->GetResult();
}

// merge this function with the right value version
Translator::LeftValueInfo Translator::TranslateStructOrClassCtorCallAsLeftValue(const AST::CallExpr& expr)
{
    // Conditions to check if this is a call to member func (constructor is not counted here)
    if (expr.resolvedFunction == nullptr) {
        return LeftValueInfo(nullptr, {});
    }
    if (!expr.resolvedFunction->TestAttr(AST::Attribute::CONSTRUCTOR)) {
        return LeftValueInfo(nullptr, {});
    }
    if (expr.resolvedFunction->outerDecl == nullptr) {
        return LeftValueInfo(nullptr, {});
    }
    if (!(expr.resolvedFunction->outerDecl->astKind == AST::ASTKind::CLASS_DECL ||
            expr.resolvedFunction->outerDecl->astKind == AST::ASTKind::STRUCT_DECL)) {
        return LeftValueInfo(nullptr, {});
    }
    // Specially, static init is not handled here
    if (expr.resolvedFunction->TestAttr(AST::Attribute::STATIC)) {
        return LeftValueInfo(nullptr, {});
    }

    // Translate code position info
    const auto& loc = TranslateLocation(expr);
    const auto& warningLoc = TranslateLocation(*expr.baseFunc);

    // Calculate instantiated callee func type
    auto thisTy = chirTy.TranslateType(*expr.ty);
    if (expr.ty->IsClass() || expr.ty->IsArray()) {
        thisTy = StaticCast<RefType*>(thisTy)->GetBaseType();
    }
    auto [paramInstTys, retInstTy] = GetMemberFuncParamAndRetInstTypes(expr);
    auto paramInstTysWithoutThis = paramInstTys;
    paramInstTys.insert(paramInstTys.begin(), builder.GetType<RefType>(thisTy));
    auto instTargetFuncTy = builder.GetType<FuncType>(paramInstTys, builder.GetVoidTy());

    // Translate arguments
    std::vector<Value*> args;
    TranslateTrivialArgs(expr, args, paramInstTysWithoutThis);
    Value* thisArg = nullptr;
    if (IsSuperOrThisCall(expr)) {
        // For super constructor call site, the `this` arg of current constructor should be passed into super
        // constructor
        auto curFunc = GetCurrentFunc();
        CJC_NULLPTR_CHECK(curFunc);
        thisArg = curFunc->GetParam(0);
    } else {
        // For trivial constructor call site, the object allocation is lifted out and then pass into constructor as
        // `this` arg
        auto allocateThis = TryCreate<Allocate>(currentBlock, loc, builder.GetType<RefType>(thisTy), thisTy);
        allocateThis->Set<DebugLocationInfoForWarning>(loc);
        thisArg = allocateThis->GetResult();
    }
    args.insert(args.begin(), thisArg);

    auto callee = GetSymbolTable(*expr.resolvedFunction);
    CJC_ASSERT(callee != nullptr && "TranslateApply: not supported callee now!");
    auto funcCall = GenerateFuncCall(
        *callee, instTargetFuncTy, {}, builder.GetType<RefType>(thisTy), args, loc);
    if (expr.callKind == AST::CallKind::CALL_SUPER_FUNCTION) {
        StaticCast<Apply*>(funcCall)->SetSuperCall();
    }
    if (HasNothingTypeArg(args)) {
        funcCall->Set<DebugLocationInfoForWarning>(warningLoc);
    }

    return LeftValueInfo(thisArg, {});
}

// Conditions to check if this is a call to member func (constructor is not counted here)
static bool IsCtorCall(const AST::CallExpr& expr)
{
    return expr.resolvedFunction && expr.resolvedFunction->TestAttr(AST::Attribute::CONSTRUCTOR) &&
        expr.resolvedFunction->outerDecl && (expr.resolvedFunction->outerDecl->astKind == AST::ASTKind::CLASS_DECL ||
        expr.resolvedFunction->outerDecl->astKind == AST::ASTKind::STRUCT_DECL) &&
        // Specially, static init is not handled here
        !expr.resolvedFunction->TestAttr(AST::Attribute::STATIC);
}

Value* Translator::TranslateStructOrClassCtorCall(const AST::CallExpr& expr)
{
    // Translate code position info
    const auto& loc = TranslateLocation(expr);
    const auto& warningLoc = TranslateLocation(*expr.baseFunc);

    // Calculate instantiated callee func type
    auto thisTy = chirTy.TranslateType(*expr.ty);
    if (expr.ty->IsClass() || expr.ty->IsArray()) {
        thisTy = StaticCast<RefType*>(thisTy)->GetBaseType();
    }
    auto [paramInstTys, retInstTy] = GetMemberFuncParamAndRetInstTypes(expr);
    auto paramInstTysWithoutThis = paramInstTys;
    paramInstTys.insert(paramInstTys.begin(), builder.GetType<RefType>(thisTy));
    auto instTargetFuncTy = builder.GetType<FuncType>(paramInstTys, builder.GetVoidTy());

    // Translate arguments
    std::vector<Value*> args;
    TranslateTrivialArgs(expr, args, paramInstTysWithoutThis);
    Value* thisArg = nullptr;
    if (IsSuperOrThisCall(expr)) {
        // For super constructor call site, the `this` arg of current constructor should be passed into super
        // constructor
        auto curFunc = GetCurrentFunc();
        CJC_NULLPTR_CHECK(curFunc);
        thisArg = curFunc->GetParam(0);
    } else {
        // For trivial constructor call site, the object allocation is lifted out and then pass into constructor as
        // `this` arg
        auto allocateThis = TryCreate<Allocate>(currentBlock, loc, builder.GetType<RefType>(thisTy), thisTy);
        allocateThis->Set<DebugLocationInfoForWarning>(loc);
        thisArg = allocateThis->GetResult();
    }
    args.insert(args.begin(), thisArg);

    auto callee = GetSymbolTable(*expr.resolvedFunction);
    CJC_ASSERT(callee != nullptr && "TranslateApply: not supported callee now!");
    auto funcCall = GenerateFuncCall(
        *callee, instTargetFuncTy, {}, builder.GetType<RefType>(thisTy), args, loc);
    if (expr.callKind == AST::CallKind::CALL_SUPER_FUNCTION) {
        StaticCast<Apply*>(funcCall)->SetSuperCall();
    }
    if (HasNothingTypeArg(args)) {
        funcCall->Set<DebugLocationInfoForWarning>(warningLoc);
    }

    if (expr.resolvedFunction->outerDecl->astKind == AST::ASTKind::STRUCT_DECL) {
        if (IsSuperOrThisCall(expr)) {
            return nullptr;
            // should be: return nullptr;
        }
        auto load = CreateAndAppendExpression<Load>(loc, thisTy, thisArg, currentBlock);
        // this load should be removed if it is a super/this call, but it will trigger IRChecker error in:
        if (IsSuperOrThisCall(expr)) {
            load->Set<SkipCheck>(SkipKind::SKIP_DCE_WARNING);
            // should be: return nullptr;
        }
        return load->GetResult();
    }
    return thisArg;
}

Ptr<Value> Translator::TranslateCFuncConstructorCall(const AST::CallExpr& expr)
{
    if (!IsValidCFuncConstructorCall(expr)) {
        return nullptr;
    }
    auto& arg = expr.args[0]->expr;
    auto argValue = TranslateExprArg(*arg, *TranslateType(*expr.ty), true);
    return argValue;
}

Ptr<Value> Translator::TranslateFuncTypeValueCall(const AST::CallExpr& expr)
{
    // Conditions to check if this is a call to func type value
    if (expr.resolvedFunction != nullptr) {
        return nullptr;
    }

    // Translate code position info
    const auto& loc = TranslateLocation(expr);

    // translate callee before args translate
    //   eg: foo()(a), translate foo() first, then translate args a
    auto callee = TranslateExprArg(*expr.baseFunc);
    // Translate arguments
    std::vector<Value*> args;
    // we should calcuate the expected args type here
    TranslateTrivialArgsWithNoSugar(expr, args, std::vector<Type*>{});

    CJC_ASSERT(callee != nullptr && "TranslateApply: not supported callee now!");
    callee = GenerateLoadIfNeccessary(*callee, false, false, false, loc);
    auto funcCall =
        GenerateFuncCall(*callee, StaticCast<FuncType*>(callee->GetType()), {}, nullptr, args, loc);
    if (HasNothingTypeArg(args)) {
        funcCall->Set<DebugLocationInfoForWarning>(loc);
    }

    auto targetCallResTy = TranslateType(*expr.ty);
    auto castedCallRes = TypeCastOrBoxIfNeeded(*funcCall->GetResult(), *targetCallResTy, loc);
    return castedCallRes;
}

namespace Cangjie::CHIR {
std::string GetSplitOperatorName(const std::string& name, OverflowStrategy st)
{
    return OverflowStrategyPrefix(st) + name;
}
}

bool Translator::IsOverflowOpCall(const AST::FuncDecl& func)
{
    if (!Is<AST::InterfaceDecl>(func.outerDecl)) {
        return false;
    }
    return IsOverflowOperator(func.identifier, *StaticCast<FuncType>(TranslateType(*func.ty)));
}

Value* Translator::TranslateNonStaticMemberFuncCall(const AST::CallExpr& expr)
{
    auto resolvedFunction = expr.resolvedFunction;
    CJC_NULLPTR_CHECK(resolvedFunction);
    CJC_ASSERT(!resolvedFunction->TestAttr(AST::Attribute::STATIC));
    bool isMutFunc =
        (resolvedFunction->TestAttr(AST::Attribute::MUT) || resolvedFunction->isSetter) && !resolvedFunction->isGetter;
    // Translate code position info
    const auto& loc = TranslateLocation(expr);
    const auto& warningLoc = TranslateLocation(*expr.baseFunc);

    // Note that, `thisType` might be different from the parent custom type of the callee func,
    // since we can call a func inherited from upstream type
    Type* thisType;
    if (auto ma = DynamicCast<AST::MemberAccess*>(expr.baseFunc.get())) {
        // polish here
        auto basePartTy = TranslateType(*ma->baseExpr->ty);
        while (basePartTy->IsRef()) {
            basePartTy = StaticCast<RefType*>(basePartTy)->GetBaseType();
        }
        thisType = basePartTy;
    } else {
        // If there is no `this` object, then we must be inside of another non-static memeber func
        auto currentFunc = currentBlock->GetTopLevelFunc();
        CJC_NULLPTR_CHECK(currentFunc);
        auto outerDef = currentFunc->GetParentCustomTypeDef();
        CJC_NULLPTR_CHECK(outerDef);
        Type* outerType = nullptr;
        if (outerDef->IsExtend()) {
            auto outerExtend = StaticCast<ExtendDef*>(outerDef);
            outerType = outerExtend->GetExtendedType();
        } else {
            outerType = StaticCast<CustomType*>(outerDef->GetType());
        }
        thisType = outerType;
    }
    CJC_ASSERT(!thisType->IsRef());
    auto thisRefType = builder.GetType<RefType>(thisType);

    auto funcInstTypeArgs = GetFuncInstArgs(expr);
    // polish this API
    auto [paramInstTys, retInstTy] = GetMemberFuncParamAndRetInstTypes(expr);
    auto paramInstTysWithoutThis = paramInstTys;
    auto tempParamInstTys = paramInstTys;
    tempParamInstTys.insert(tempParamInstTys.begin(), thisRefType);
    auto tempInstTargetFuncTy = builder.GetType<FuncType>(tempParamInstTys, retInstTy);

    // Translate arguments
    std::vector<Value*> args;
    TranslateThisObjectForNonStaticMemberFuncCall(expr, args, isMutFunc || IsPropertySetterCall(expr));
    TranslateTrivialArgs(expr, args, paramInstTysWithoutThis);

    bool calledByInheritableTy = thisType->IsClass() || thisType->IsGeneric();
    bool isSuperCall = expr.callKind == AST::CallKind::CALL_SUPER_FUNCTION;
    Expression* funcCall = nullptr;
    if (calledByInheritableTy && !isSuperCall && IsVirtualMember(*resolvedFunction)) {
        CJC_ASSERT(args.size() >= 1);
        auto obj = args[0];
        args.erase(args.begin());
        auto funcName = expr.resolvedFunction->identifier.Val();
        if (IsOverflowOpCall(*resolvedFunction)) {
            funcName = GetSplitOperatorName(funcName, expr.overflowStrategy);
        }
        auto outerDecl = expr.resolvedFunction->outerDecl;
        auto genericThisTy = TranslateType(*outerDecl->ty);
        if (IsStructMutFunction(*expr.resolvedFunction)) {
            genericThisTy = builder.GetType<RefType>(genericThisTy);
        }
        auto instParentCustomTy =
            GetExactParentType(*thisType, *resolvedFunction, *tempInstTargetFuncTy, funcInstTypeArgs, true);
        Ptr<Type> instParentTy = instParentCustomTy;
        if (instParentCustomTy->IsReferenceType()) {
            instParentTy = builder.GetType<RefType>(instParentCustomTy);
        } else if (instParentCustomTy->IsStruct() && isMutFunc) {
            instParentTy = builder.GetType<RefType>(instParentCustomTy);
        }
        paramInstTys.insert(paramInstTys.begin(), instParentTy);
        auto instTargetFuncTy = builder.GetType<FuncType>(paramInstTys, retInstTy);
        FuncCallType funcCallType{funcName, instTargetFuncTy, funcInstTypeArgs};
        auto vtableRes = GetFuncIndexInVTable(*thisType, funcCallType, false)[0];
        InstInvokeCalleeInfo dynamicDispatchFuncInfo{funcName, instTargetFuncTy, vtableRes.originalFuncType,
            vtableRes.instSrcParentType,
            StaticCast<ClassType*>(vtableRes.instSrcParentType->GetCustomTypeDef()->GetType()), funcInstTypeArgs,
            thisRefType, vtableRes.offset};

        funcCall = GenerateDynmaicDispatchFuncCall(dynamicDispatchFuncInfo, args, obj, nullptr, loc);
        PrintDevirtualizationMessage(expr, "invoke");
    } else {
        auto instParentCustomTy =
            GetExactParentType(*thisType, *resolvedFunction, *tempInstTargetFuncTy, funcInstTypeArgs, false);
        Ptr<Type> instParentTy = instParentCustomTy;
        if (instParentCustomTy->IsReferenceType()) {
            instParentTy = builder.GetType<RefType>(instParentCustomTy);
        } else if (instParentCustomTy->IsStruct() && isMutFunc) {
            instParentTy = builder.GetType<RefType>(instParentCustomTy);
        }
        auto callee = GetSymbolTable(*resolvedFunction);
        CJC_ASSERT(callee != nullptr && "TranslateApply: not supported callee now!");
        // get mut wrapped function if needed
        auto wrapperFuncMaybe = callee;
        if (auto customTy = DynamicCast<CustomType*>(thisType)) {
            auto func = mutWrapperMap.GetWrapperFunc(callee, customTy->GetCustomTypeDef(), instParentCustomTy);
            if (func != nullptr) {
                wrapperFuncMaybe = func;
            }
        }
        if (wrapperFuncMaybe != callee) {
            // function is in this type if wrapped function is set
            instParentTy = thisType;
            paramInstTys.insert(paramInstTys.begin(), thisRefType);
        } else {
            paramInstTys.insert(paramInstTys.begin(), instParentTy);
        }
        auto instTargetFuncTy = builder.GetType<FuncType>(paramInstTys, retInstTy);
        funcCall = GenerateFuncCall(
            *wrapperFuncMaybe, instTargetFuncTy, funcInstTypeArgs, thisRefType, args, loc);
        PrintDevirtualizationMessage(expr, "apply");
    }
    // polish this
    if (HasNothingTypeArg(args)) {
        funcCall->Set<DebugLocationInfoForWarning>(warningLoc);
    }

    auto targetCallResTy = TranslateType(*expr.ty);
    auto castedCallRes = TypeCastOrBoxIfNeeded(*funcCall->GetResult(), *targetCallResTy, loc);
    return castedCallRes;
}

Value* Translator::CreateGetRTTIWrapper(Value* value, Block* bl, const DebugLocation& loc)
{
    auto type = value->GetType();
    Expression* expr;
    // GetRTTI can only be used on Class& or This&. use GetRTTIStatic otherwise
    if (Is<RefType>(type) && (type->StripAllRefs()->IsClass() || type->StripAllRefs()->IsThis())) {
        expr = builder.CreateExpression<GetRTTI>(loc, builder.GetUnitTy(), value, bl);
    } else {
        expr = builder.CreateExpression<GetRTTIStatic>(loc, builder.GetUnitTy(), type, bl);
    }
    bl->AppendExpression(expr);
    return expr->GetResult();
}

Value* Translator::TranslateStaticMemberFuncCall(const AST::CallExpr& expr)
{
    auto resolvedFunction = expr.resolvedFunction;
    CJC_NULLPTR_CHECK(resolvedFunction);
    CJC_ASSERT(resolvedFunction->TestAttr(AST::Attribute::STATIC));

    // Translate code position info
    const auto& loc = TranslateLocation(expr);
    const auto& warningLoc = TranslateLocation(*expr.baseFunc);

    // Calculate instantiated callee func type
    std::vector<Type*> thisTypeInstArgs;
    // Note that, `thisType` might be different from the parent custom type of the callee func,
    // since we can call a func inherited from upstream type
    Type* thisType;
    // a static call without callee type translates to apply
    bool isInGVInit{false};
    if (auto ma = DynamicCast<AST::MemberAccess*>(expr.baseFunc.get())) {
        // polish here
        auto basePartTy = TranslateType(*ma->baseExpr->ty);
        while (basePartTy->IsRef()) {
            basePartTy = StaticCast<RefType*>(basePartTy)->GetBaseType();
        }
        thisType = basePartTy;
        if (auto thisCustomType = DynamicCast<CustomType*>(thisType)) {
            for (auto parentCustomDefTyInstArg : thisCustomType->GetGenericArgs()) {
                thisTypeInstArgs.emplace_back(parentCustomDefTyInstArg);
            }
        }
    } else {
        // If there is no `This` type, then we must be inside of another static memeber func
        auto currentFunc = currentBlock->GetTopLevelFunc();
        auto outerDef = currentFunc->GetParentCustomTypeDef();
        if (outerDef == nullptr) {
            // a hack solution for the GVInit func used for the member var default init value
            // where the `This` is also missing. But this is not accurate if we are calling a func
            // inherited from super type
            auto outerType = TranslateType(*resolvedFunction->outerDecl->ty);
            while (outerType->IsRef()) {
                outerType = StaticCast<RefType*>(outerType)->GetBaseType();
            }
            auto outerCustomType = StaticCast<CustomType*>(outerType);
            for (auto parentCustomDefTyInstArg : outerCustomType->GetGenericArgs()) {
                thisTypeInstArgs.emplace_back(parentCustomDefTyInstArg);
            }
            thisType = outerCustomType;
            isInGVInit = true;
        } else {
            CJC_NULLPTR_CHECK(outerDef);
            Type* outerType = nullptr;
            if (outerDef->IsExtend()) {
                auto outerExtend = StaticCast<ExtendDef*>(outerDef);
                outerType = outerExtend->GetExtendedType();
            } else {
                auto outerDefType = StaticCast<CustomType*>(outerDef->GetType());
                outerType = outerDefType;
            }
            if (auto outerCustomType = DynamicCast<CustomType*>(outerType)) {
                for (auto parentCustomDefTyInstArg : outerCustomType->GetGenericArgs()) {
                    thisTypeInstArgs.emplace_back(parentCustomDefTyInstArg);
                }
            }
            thisType = outerType;
        }
    }
    CJC_ASSERT(!thisType->IsRef());
    auto thisRefType = builder.GetType<RefType>(thisType);

    auto funcInstTypeArgs = GetFuncInstArgs(expr);
    // polish this API
    auto [paramInstTys, retInstTy] = GetMemberFuncParamAndRetInstTypes(expr);
    auto instTargetFuncTy = builder.GetType<FuncType>(paramInstTys, retInstTy);

    // Translate arguments
    std::vector<Value*> args;
    TranslateTrivialArgs(expr, args, paramInstTys);

    Expression* funcCall = nullptr;
    if (IsInvokeStatic(expr) && !IsInsideCFunc(*currentBlock) && !isInGVInit) {
        auto funcName = expr.resolvedFunction->identifier;
        FuncCallType funcCallType{funcName, instTargetFuncTy, funcInstTypeArgs};
        auto vtableRes = GetFuncIndexInVTable(*thisType, funcCallType, true)[0];
        auto useThisType = [thisType, &expr]() {
            if (auto thisCustomType = DynamicCast<CustomType*>(thisType)) {
                if (auto outerInterface = DynamicCast<ClassDef*>(thisCustomType->GetCustomTypeDef())) {
                    // invoke static function without prefix equals using This. prefix
                    // (to be done after 'This' proposals)
                    if (expr.baseFunc->astKind == AST::ASTKind::REF_EXPR) {
                        return true;
                    }
                }
            }
            return false;
        };
        if (useThisType()) {
            thisRefType = builder.GetType<RefType>(builder.GetType<ThisType>());
        }
        InstInvokeCalleeInfo dynamicDispatchFuncInfo{funcName, instTargetFuncTy, vtableRes.originalFuncType,
            vtableRes.instSrcParentType,
            StaticCast<ClassType*>(vtableRes.instSrcParentType->GetCustomTypeDef()->GetType()), funcInstTypeArgs,
            thisRefType, vtableRes.offset};

        Value* rtti{};
        if (!Is<AST::MemberAccess>(expr.baseFunc)) {
            if (currentBlock->GetTopLevelFunc()->TestAttr(Attribute::STATIC)) {
                rtti = CreateAndAppendExpression<GetRTTIStatic>(TranslateLocation(expr),
                    builder.GetUnitTy(), thisRefType, currentBlock)->GetResult();
            } else {
                // calling InvokeStatic in instance member function, use GetRTTI(%this)
                rtti = CreateGetRTTIWrapper(
                    currentBlock->GetTopLevelFunc()->GetParam(0), currentBlock, TranslateLocation(expr));
            }
        } else {
            // otherwise use rtti of thisRefType
            rtti = CreateAndAppendExpression<GetRTTIStatic>(TranslateLocation(expr),
                builder.GetUnitTy(), thisRefType, currentBlock)->GetResult();
        }
        funcCall = GenerateDynmaicDispatchFuncCall(dynamicDispatchFuncInfo, args, nullptr, rtti, loc);
        PrintDevirtualizationMessage(expr, "invoke");
    } else {
        auto callee = GetSymbolTable(*resolvedFunction);
        CJC_ASSERT(callee != nullptr && "TranslateApply: not supported callee now!");
        funcCall = GenerateFuncCall(
            *callee, instTargetFuncTy, funcInstTypeArgs, thisRefType, args, loc);
        PrintDevirtualizationMessage(expr, "apply");
    }
    // polish this
    if (HasNothingTypeArg(args)) {
        funcCall->Set<DebugLocationInfoForWarning>(warningLoc);
    }

    auto targetCallResTy = TranslateType(*expr.ty);
    auto castedCallRes = TypeCastOrBoxIfNeeded(*funcCall->GetResult(), *targetCallResTy, loc);
    return castedCallRes;
}

// remove this after 'This' proposals
bool Translator::IsInsideCFunc(const Block& bl)
{
    if (bl.GetTopLevelFunc()->IsCFunc()) {
        return true;
    }
    auto expr = bl.GetParentBlockGroup()->GetOwnerExpression();
    while (expr) {
        if (auto lambda = DynamicCast<Lambda>(expr)) {
            if (lambda->GetFuncType()->IsCFunc()) {
                return true;
            }
        }
        expr = expr->GetParentBlockGroup()->GetOwnerExpression();
    }
    return false;
};

Value* Translator::TranslateMemberFuncCall(const AST::CallExpr& expr)
{
    // Instance member function、static member function、global func.
    auto resolvedFunction = expr.resolvedFunction;
    CJC_NULLPTR_CHECK(resolvedFunction);
    CJC_ASSERT(!resolvedFunction->TestAttr(AST::Attribute::CONSTRUCTOR));

    if (resolvedFunction->TestAttr(AST::Attribute::STATIC)) {
        return TranslateStaticMemberFuncCall(expr);
    }
    return TranslateNonStaticMemberFuncCall(expr);
}

Value* Translator::TranslateTrivialFuncCall(const AST::CallExpr& expr)
{
    // Conditions to check if this is a call to declared function which
    // can be a global func or local func
    if (expr.resolvedFunction == nullptr) {
        return nullptr;
    }

    auto resolvedFunction = expr.resolvedFunction;

    // Translate code position info
    const auto& loc = TranslateLocation(expr);
    const auto& warningLoc = TranslateLocation(*expr.baseFunc);

    auto funcInstTypeArgs = GetFuncInstArgs(expr);
    // polish this API
    auto [paramInstTys, retInstTy] = GetMemberFuncParamAndRetInstTypes(expr);
    auto instTargetFuncTy = builder.GetType<FuncType>(paramInstTys, retInstTy);

    // Translate arguments
    std::vector<Value*> args;
    TranslateTrivialArgs(expr, args, paramInstTys);

    auto callee = GetSymbolTable(*resolvedFunction);
    CJC_ASSERT(callee != nullptr && "TranslateApply: not supported callee now!");
    auto funcCall = GenerateFuncCall(*callee, instTargetFuncTy, funcInstTypeArgs, nullptr, args, loc);
    // polish this
    if (HasNothingTypeArg(args)) {
        funcCall->Set<DebugLocationInfoForWarning>(warningLoc);
    }

    auto targetCallResTy = TranslateType(*expr.ty);
    auto castedCallRes = TypeCastOrBoxIfNeeded(*funcCall->GetResult(), *targetCallResTy, loc);
    return castedCallRes;
}

static bool IsCallingConstructor(const AST::CallExpr& expr)
{
    if (expr.resolvedFunction == nullptr) {
        return false;
    }
    if (expr.callKind == AST::CallKind::CALL_SUPER_FUNCTION) {
        return true;
    }
    // non-static init func, because expr.ty is Unit in static init
    return expr.resolvedFunction->TestAttr(AST::Attribute::CONSTRUCTOR) &&
        !expr.resolvedFunction->TestAttr(AST::Attribute::STATIC);
}

Ptr<Type> Translator::GetMemberFuncCallerInstType(const AST::CallExpr& expr, bool needExactTy)
{
    Type* callerType = nullptr;
    if (auto memAccess = DynamicCast<AST::MemberAccess*>(expr.baseFunc.get()); memAccess) {
        // xxx.memberFunc()
        if (!IsPackageMemberAccess(*memAccess)) {
            callerType = TranslateType(*memAccess->baseExpr->ty);
        } else if (IsCallingConstructor(expr)) {
            callerType = TranslateType(*expr.ty);
        }
    } else if (IsCallingConstructor(expr)) {
        callerType = TranslateType(*expr.ty);
    } else if (expr.resolvedFunction != nullptr && expr.resolvedFunction->outerDecl != nullptr &&
        expr.resolvedFunction->outerDecl->IsNominalDecl()) {
        // call own member function in nominal decl, there are 3 cases:
        auto outerDef = currentBlock->GetTopLevelFunc()->GetParentCustomTypeDef();
        if (outerDef != nullptr) {
            if (auto exDef = DynamicCast<ExtendDef*>(outerDef)) {
                // 1. struct A { func foo() {} }; extend A { func goo() { foo() } }
                //                                                        ^^^  call `foo` in extend A, then return `A`
                callerType = exDef->GetExtendedType();
            } else {
                // 2. struct A { func foo() {}; func goo() { foo() } }
                //                                           ^^^  call `foo` in struct A, then return `A`
                callerType = outerDef->GetType();
            }
            if (callerType->IsClassOrArray()) {
                callerType = builder.GetType<RefType>(callerType);
            }
        } else if (IsStaticInit(*expr.resolvedFunction)) {
            // 3. in CHIR, we treat `static.init()` as global function, not member function,
            // because its outerDecl is something like `class A<T>`, if it's member function, ir is as follows:
            // Func gv$_init() {
            //     Apply(static.init)(A<T>, [], Unit) // `T` is not declared in this scope
            // }
            callerType = nullptr;
        } else {
            // 4. struct A { static let a = foo(); func foo() {} }
            //                              ^^^ call `foo` while initializing static member var, then return `A`
            callerType = TranslateType(*expr.resolvedFunction->outerDecl->ty);
        }
    }

    if (needExactTy && callerType != nullptr && expr.resolvedFunction != nullptr) {
        auto [paramInstTys, retInstTy] = GetMemberFuncParamAndRetInstTypes(expr);
        if (expr.resolvedFunction != nullptr && !expr.resolvedFunction->TestAttr(AST::Attribute::STATIC)) {
            paramInstTys.insert(paramInstTys.begin(), callerType);
        }
        auto instFuncType = builder.GetType<FuncType>(paramInstTys, retInstTy);
        std::vector<Type*> funcInstTypeArgs;
        if (auto nre = DynamicCast<AST::NameReferenceExpr*>(expr.baseFunc.get()); nre) {
            // a constructor mustn't have generic param, `init<T>()` is error, but `baseFunc` may have `instTys`
            // e.g. let x = CA<Int64>()
            // `CA<Int64>()` is CallExpr,
            if (expr.resolvedFunction == nullptr || !expr.resolvedFunction->TestAttr(AST::Attribute::CONSTRUCTOR)) {
                auto tmp = TranslateASTTypes(nre->instTys);
                funcInstTypeArgs.insert(funcInstTypeArgs.end(), tmp.begin(), tmp.end());
            }
        }
        Type* root = callerType->IsRef() ? StaticCast<RefType*>(callerType)->GetBaseType() : callerType;
        callerType = GetExactParentType(*root, *expr.resolvedFunction, *instFuncType, funcInstTypeArgs, false);
        if (callerType != nullptr && callerType->IsClass()) {
            callerType = builder.GetType<RefType>(callerType);
        }
    }

    // Note that, the constructor doesn't have caller type
    if (expr.resolvedFunction != nullptr && IsStructMutFunction(*expr.resolvedFunction) && callerType != nullptr) {
        callerType = builder.GetType<RefType>(callerType);
    }

    return callerType;
}

std::pair<std::vector<Type*>, Type*> Translator::GetMemberFuncParamAndRetInstTypes(const AST::CallExpr& expr)
{
    FuncType* funcType = nullptr;
    if (auto genericTy = DynamicCast<AST::GenericsTy*>(expr.baseFunc->ty); genericTy) {
        CJC_ASSERT(genericTy->upperBounds.size() == 1 && "not support multi-upperBounds for funcType in CHIR");
        funcType = StaticCast<FuncType*>(TranslateType(**genericTy->upperBounds.begin()));
    } else {
        funcType = StaticCast<FuncType*>(TranslateType(*expr.baseFunc->ty));
    }
    return std::pair<std::vector<Type*>, Type*>{funcType->GetParamTypes(), funcType->GetReturnType()};
}

Value* Translator::GenerateLoadIfNeccessary(Value& arg, bool isThis, bool isMut, bool isInOut, const DebugLocation& loc)
{
    auto argTy = arg.GetType();
    auto pureArgTy = argTy;
    while (pureArgTy->IsRef()) {
        pureArgTy = StaticCast<RefType*>(pureArgTy)->GetBaseType();
    }

    if (((isMut && isThis) || isInOut) && (pureArgTy->IsValueType() || pureArgTy->IsGeneric())) {
        // We are handling the `this` param for a mut function, thus we need a single-ref type even
        // it is value type
        if (argTy->IsCPointer()) {
            // CPointer is a special since it is value type but represent a pointer thus no need
            // to load
        } else {
            CJC_ASSERT(argTy->IsRef());
            CJC_ASSERT(!StaticCast<RefType*>(argTy)->GetBaseType()->IsRef());
            if (pureArgTy->IsGeneric()) {
                // But if this is a generic type, we still need to generate load cause generic itself can handle
                // mut semantics
                auto baseTy = StaticCast<RefType*>(argTy)->GetBaseType();
                CJC_ASSERT(!baseTy->IsRef());
                return CreateAndAppendExpression<Load>(loc, baseTy, &arg, currentBlock)->GetResult();
            }
        }
    } else {
        // Otherwise, value type will always pass by copy (i.e. with no ref in type) and reference type
        // will always pass by reference (i.e. with single-ref in type). Specially, the generic type and
        // func type are treated like value type
        if (pureArgTy->IsValueType() || pureArgTy->IsGeneric() || pureArgTy->IsFunc()) {
            if (argTy->IsRef()) {
                // Generate load if it is a single-ref value type (due to `var`)
                auto baseTy = StaticCast<RefType*>(argTy)->GetBaseType();
                CJC_ASSERT(!baseTy->IsRef());
                return CreateAndAppendExpression<Load>(loc, baseTy, &arg, currentBlock)->GetResult();
            }
        } else if (pureArgTy->IsReferenceType()) {
            CJC_ASSERT(argTy->IsRef());
            auto baseTy = StaticCast<RefType*>(argTy)->GetBaseType();
            if (baseTy->IsRef()) {
                // Generate load if it is a double-ref reference type (due to `var`)
                return CreateAndAppendExpression<Load>(loc, baseTy, &arg, currentBlock)->GetResult();
            }
        }
    }
    return &arg;
}

bool Translator::HasNothingTypeArg(std::vector<Value*>& args) const
{
    auto it = std::find_if(args.begin(), args.end(), [](auto arg) { return arg->GetType()->IsNothing(); });
    if (it != args.end()) {
        return true;
    }
    return false;
}

// Conditions to check if this is a call to member func (constructor is not counted here)
static bool IsMemberFuncCall(const AST::CallExpr& expr)
{
    return expr.resolvedFunction && !expr.resolvedFunction->TestAttr(AST::Attribute::CONSTRUCTOR) &&
        expr.resolvedFunction->outerDecl && expr.resolvedFunction->outerDecl->IsNominalDecl();
}

Ptr<Value> Translator::ProcessCallExpr(const AST::CallExpr& expr)
{
    if (auto res = TranslateIntrinsicCall(expr); res) {
        return res;
    }
    if (auto res = TranslateForeignFuncCall(expr); res) {
        return res;
    }
    if (auto res = TranslateCFuncConstructorCall(expr)) {
        return res;
    }
    if (auto res = TranslateCStringCtorCall(expr); res) {
        return res;
    }
    if (auto res = TranslateEnumCtorCall(expr); res) {
        return res;
    }
    if (IsCtorCall(expr)) {
        return TranslateStructOrClassCtorCall(expr);
    }
    if (bool isNothingCall = !expr.resolvedFunction && expr.baseFunc->ty->kind == AST::TypeKind::TYPE_NOTHING;
        isNothingCall) {
        return TranslateExprArg(*expr.baseFunc);
    }
    if (auto res = TranslateFuncTypeValueCall(expr); res) {
        return res;
    }
    if (IsMemberFuncCall(expr)) {
        return TranslateMemberFuncCall(expr);
    }
    if (auto res = TranslateTrivialFuncCall(expr); res) {
        return res;
    }
    InternalError("translating unsupported CallExpr");
    return nullptr;
}

void Translator::ProcessMapExpr(AST::Node& originExpr, bool isSubScript)
{
    if (auto base = GetMapExpr(originExpr); base) {
        if (exprValueTable.Has(originExpr)) {
            return;
        }
        /* In the following cangjie code, we do not need to translate `base`.
        open class A {
            operator func [](y : Int64) { x }
            operator func [](y : Int64, value! : Int64) { x = value }}
        class B <: A {
            func f() {
                super[3] *= 4
            }
        }
         */
        if (!(isSubScript && originExpr.astKind == AST::ASTKind::REF_EXPR &&
                StaticCast<AST::RefExpr*>(&originExpr)->isSuper)) {
            auto chirNode = TranslateExprArg(originExpr);
            exprValueTable.Set(originExpr, *chirNode);
        }
    }
}

Translator::LeftValueInfo Translator::TranslateCallExprAsLeftValue(const AST::CallExpr& expr)
{
    if (auto res = TranslateStructOrClassCtorCallAsLeftValue(expr); res.base) {
        return res;
    }

    auto val = TranslateASTNode(expr, *this);
    return LeftValueInfo(val, {});
}

void Translator::TranslateCompoundAssignmentElementRef(const AST::MemberAccess& ma)
{
    auto loc = TranslateLocation(ma);
    auto baseResLeftValueInfo = TranslateExprAsLeftValue(*ma.baseExpr);
    auto baseResLeftValuePath = baseResLeftValueInfo.path;
    auto baseResLeftValue = baseResLeftValueInfo.base;
    if (baseResLeftValuePath.empty()) {
        exprValueTable.Set(*ma.baseExpr, *baseResLeftValue);
    } else {
        auto baseResLeftValueCustomType = StaticCast<CustomType*>(baseResLeftValue->GetType()->StripAllRefs());
        if (baseResLeftValue->GetType()->IsReferenceTypeWithRefDims(1) ||
            baseResLeftValue->GetType()->IsValueOrGenericTypeWithRefDims(1)) {
            auto getMemberRef = CreateGetElementRefWithPath(
                loc, baseResLeftValue, baseResLeftValuePath, currentBlock, *baseResLeftValueCustomType);
            exprValueTable.Set(*ma.baseExpr, *getMemberRef);
        } else {
            auto memberType = baseResLeftValueCustomType->GetInstMemberTypeByPath(baseResLeftValuePath, builder);
            CJC_ASSERT(baseResLeftValue->GetType()->IsValueOrGenericTypeWithRefDims(0));
            auto getMember =
                CreateAndAppendExpression<Field>(loc, memberType, baseResLeftValue, baseResLeftValuePath, currentBlock);
            exprValueTable.Set(*ma.baseExpr, *getMember->GetResult());
        }
    }
}

Ptr<Value> Translator::Visit(const AST::CallExpr& callExpr)
{
    /****** Handle side-effect `mapExpr` here ******/
    if (!callExpr.TestAttr(AST::Attribute::SIDE_EFFECT)) {
        return ProcessCallExpr(callExpr);
    }
    CJC_ASSERT(callExpr.resolvedFunction);

    // Case 1: a call expr like:
    //      S.xxxSet(S.xxxGet() + k)
    if (callExpr.resolvedFunction->isSetter) {
        if (callExpr.baseFunc->astKind == AST::ASTKind::MEMBER_ACCESS) {
            auto ma = StaticCast<AST::MemberAccess*>(callExpr.baseFunc.get());
            if (ma->baseExpr->mapExpr != nullptr) {
                TranslateCompoundAssignmentElementRef(*ma);
            }
        } else if (callExpr.baseFunc->astKind == AST::ASTKind::REF_EXPR) {
            // Nothing need to do here
        }
    } else if (callExpr.resolvedFunction->TestAttr(AST::Attribute::OPERATOR)) {
        CJC_ASSERT(callExpr.baseFunc->astKind == AST::ASTKind::MEMBER_ACCESS);
        auto ma = StaticCast<AST::MemberAccess*>(callExpr.baseFunc.get());
        CJC_NULLPTR_CHECK(ma->baseExpr->mapExpr);
        TranslateCompoundAssignmentElementRef(*ma);
    } else {
        CJC_ABORT();
    }
    bool isSubScript = callExpr.resolvedFunction && callExpr.resolvedFunction->identifier == "[]";
    if (isSubScript) {
        // If the case is array access(`[]`), then callexpr's baseFunc and args may have side effect.
        std::vector<Ptr<AST::FuncArg>> args = callExpr.desugarArgs.value();
        CJC_ASSERT(!args.empty());
        for (size_t i = 0; i < args.size() - 1; i++) {
            ProcessMapExpr(*args[i]->expr, isSubScript);
        }
    }
    return ProcessCallExpr(callExpr);
}

void Translator::PrintDevirtualizationMessage(const AST::CallExpr& expr, const std::string& nodeType)
{
    if (!opts.chirDebugOptimizer) {
        return;
    }

    Ptr<AST::FuncDecl> resolvedFunction = expr.resolvedFunction;

    std::string message = "The function call to " + resolvedFunction->identifier + " in the line " +
        std::to_string(expr.begin.line) + " and the column " + std::to_string(expr.begin.column) + " was an " +
        nodeType + " call.\n";
    std::cout << message;
}
