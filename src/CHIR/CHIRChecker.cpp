// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/CHIRChecker.h"
#include "cangjie/CHIR/ToStringUtils.h"
#include "cangjie/Mangle/CHIRManglingUtils.h"

using namespace Cangjie::CHIR;

namespace {
std::string IndexToString(const size_t index)
{
    auto unitDigit = index % 10;
    std::string suffix;
    if (index > 10 && index < 20) {
        suffix = "th";
    } else if (unitDigit == 1) {
        suffix = "st";
    } else if (unitDigit == 2) {
        suffix = "nd";
    } else if (unitDigit == 3) {
        suffix = "rd";
    } else {
        suffix = "th";
    }
    return std::to_string(index) + suffix;
}

bool IsStructOrCStruct(const Type& type, bool includeNormalStruct)
{
    if (!type.IsStruct()) {
        return false;
    }
    if (includeNormalStruct) {
        return true;
    }
    return Cangjie::StaticCast<const StructType&>(type).GetStructDef()->IsCStruct();
}

bool IsOptionalCType(const Type& type, bool includeNormalStruct, bool includeCString)
{
    if (auto varray = Cangjie::DynamicCast<const VArrayType*>(&type)) {
        return IsOptionalCType(*varray->GetElementType(), includeNormalStruct, includeCString);
    } else if (auto tuple = Cangjie::DynamicCast<const TupleType*>(&type)) {
        for (auto t : tuple->GetElementTypes()) {
            if (!IsOptionalCType(*t, includeNormalStruct, includeCString)) {
                return false;
            }
        }
        return true;
    } else if (auto ref = Cangjie::DynamicCast<const RefType*>(&type)) {
        return IsOptionalCType(*ref->GetBaseType(), includeNormalStruct, includeCString);
    } else if (auto cp = Cangjie::DynamicCast<const CPointerType*>(&type)) {
        return IsOptionalCType(*cp->GetElementType(), includeNormalStruct, includeCString);
    } else if (auto cf = Cangjie::DynamicCast<const FuncType*>(&type); cf && cf->IsCFunc()) {
        if (!IsOptionalCType(*cf->GetReturnType(), includeNormalStruct, includeCString)) {
            return false;
        }
        for (auto t : cf->GetParamTypes()) {
            if (!IsOptionalCType(*t, includeNormalStruct, includeCString)) {
                return false;
            }
        }
        return true;
    } else if (includeCString && type.IsCString()) {
        return true;
    }
    auto k = type.GetTypeKind();
    return (k >= Type::TypeKind::TYPE_INT8 && k <= Type::TypeKind::TYPE_NOTHING) ||
        type.IsCPointer() || IsStructOrCStruct(type, includeNormalStruct);
}

bool IsCType(const Type& type)
{
    return IsOptionalCType(type, false, true);
}

bool IsCTypeInVArray(const Type& type)
{
    return IsOptionalCType(type, true, true);
}

bool IsCTypeInInout(const Type& type)
{
    return IsOptionalCType(type, true, false);
}

std::string GetExpressionString(const Expression& expr)
{
    if (auto lambda = Cangjie::DynamicCast<const Lambda*>(&expr)) {
        return "lambda " + lambda->GetIdentifier();
    } else if (auto result = expr.GetResult()) {
        return result->ToString();
    } else {
        return expr.ToString();
    }
}

// type must be `ValueType&`, `ReferenceType&&`, or `GenericType&`
bool CheckTypeMustBeRef(const Type& type)
{
    // `ValueType`, `ReferenceType` and `GenericType` are error
    if (!type.IsRef()) {
        return false;
    }

    auto baseType = Cangjie::StaticCast<const RefType&>(type).GetBaseType();
    // `ReferenceType&` is error
    if (baseType->IsReferenceType()) {
        return false;
    }
    // `ValueType&` and `GenericType&` are correct
    if (baseType->IsValueOrGenericType()) {
        return true;
    }

    baseType = Cangjie::StaticCast<RefType*>(baseType)->GetBaseType();
    // `ReferenceType&&` is correct
    if (baseType->IsReferenceType()) {
        return true;
    }
    // `ValueType&&`, `GenericType&&` and `SomeType&&&...` are error
    return false;
}

std::string ValueSymbolToString(const Value& value)
{
    if (auto func = Cangjie::DynamicCast<const Func*>(&value)) {
        return FuncSymbolStr(*func);
    } else {
        return value.ToString();
    }
}

/**
 * @brief generic type includes `T` and `class-A<U>`, we want to know `T` and `U` if is in container
 */
bool GenericTypeIsInContainer(const Type& type, const std::vector<GenericType*>& container)
{
    if (type.IsGeneric()) {
        return std::find(container.begin(), container.end(), &type) != container.end();
    }
    for (auto ty : type.GetTypeArgs()) {
        if (!GenericTypeIsInContainer(*ty, container)) {
            return false;
        }
    }
    return true;
}

bool IsUnreachableApply(const Type* thisType)
{
    if (thisType == nullptr) {
        return false;
    }
    return thisType->StripAllRefs()->IsNothing();
}

bool FuncCanBeDynamicDispatch(const FuncBase& func)
{
    if (func.TestAttr(Attribute::STATIC)) {
        return !func.TestAttr(Attribute::PRIVATE);
    } else {
        return (func.TestAttr(Attribute::PUBLIC) || func.TestAttr(Attribute::PROTECTED)) &&
            func.TestAttr(Attribute::VIRTUAL);
    }
}

std::vector<Type*> CalculateAllUpperBounds(const GenericType& genericType, CHIRBuilder& builder)
{
    /**
     *  class A<T> where T <: B<T> {}
     *  open class B<U> where U <: C {}
     *  open class C {}
     *
     * generic type `T` in class A<T>, its direct upper bound is `B<T>`,
     * but generic type `U` in class B<U> also has constraints `U <: C`,
     * so upper bounds of `T` are `B<T>` and `C`
     */
    auto upperBounds = genericType.GetUpperBounds();
    std::vector<Type*> extraUpperBounds;
    std::unordered_map<const GenericType*, Type*> replaceTable;
    for (auto upperBound : upperBounds) {
        // a hack way, `genericType` may be an orphan generic type
        auto derefUpperBound = Cangjie::DynamicCast<ClassType*>(upperBound->StripAllRefs());
        if (derefUpperBound == nullptr) {
            continue;
        }
        auto originalType = derefUpperBound->GetClassDef()->GetType();
        auto [res, instMap] = originalType->CalculateGenericTyMapping(*derefUpperBound);
        CJC_ASSERT(res);
        for (auto& it : instMap) {
            if (it.second == &genericType) {
                auto temp = it.first->GetUpperBounds();
                extraUpperBounds.insert(extraUpperBounds.begin(), temp.begin(), temp.end());
                replaceTable.emplace(it);
            }
        }
    }
    for (auto type : extraUpperBounds) {
        upperBounds.emplace_back(ReplaceRawGenericArgType(*type, replaceTable, builder));
    }
    return upperBounds;
}

bool Generic1IsEqualOrSubTypeOfGeneric2(GenericType& generic1, const GenericType& generic2, CHIRBuilder& builder)
{
    auto parentUpperBounds = generic2.GetUpperBounds();
    std::unordered_map<const GenericType*, Type*> replaceTable{{&generic2, &generic1}};
    auto subUpperBounds = CalculateAllUpperBounds(generic1, builder);
    while (!parentUpperBounds.empty()) {
        /**
         *  class A<T> where T <: X1 & X2 & X3 {
         *      public func foo(a: T) {}
         *  }
         *  func goo<U>(b: U) where U <: X1 & X2 {
         *      A<M>().foo(b) // not care about type `M`, foo's arg is `b` which is sub type of X1 and X2
         *                    // but foo's param type has more constraints, so we can't send `b` to `foo`
         *  }
         *
         *  class A<T> where T <: X1 & X2 {
         *      public func foo(a: T) {}
         *  }
         *  func goo<U>(b: U) where U <: X1 & X2 & X3 {
         *      A<M>().foo(b) // not care about type `M`, foo's arg is `b` which is sub type of X1, X2 and X3
         *                    // foo's param type has less constraints, so we can send `b` to `foo`
         *  }
         */
        if (subUpperBounds.empty()) {
            return false;
        }
        auto subType = subUpperBounds.back();
        subUpperBounds.pop_back();
        for (auto it = parentUpperBounds.begin(); it != parentUpperBounds.end();) {
            auto parentType = ReplaceRawGenericArgType(**it, replaceTable, builder);
            if (subType->IsEqualOrSubTypeOf(*parentType, builder)) {
                /**
                 * erase but not break, because there may be other parent types which meet the condition
                 * interface I {}
                 * class A <: I {}
                 * if `T1 <: I & A` is sub type and `T2 <: I & A` is parent type, then `subType` is class A and
                 * parent type is interface I in first loop, so `parentUpperBounds` only left class A after erasing,
                 * and if we break this loop, then `subType` is interface I and parent type is class A in second loop,
                 * that won't meet the condition of `IsEqualOrSubTypeOf`
                 */
                it = parentUpperBounds.erase(it);
            } else {
                ++it;
            }
        }
    }
    return true;
}

/**
 * print a vector to string, format: firstChar + ty1, ty2 ... + lastChar
 * for example, if we want to print instantiated type args, then it will be something like `<Int32, Bool, Rune>`,
 * if we want to print parameter types, then it will be something like `(Int32, Bool, Rune)`
 */
template<typename T>
std::string VectorTypesToString(
    const std::string& firstChar, const std::vector<T>& types, const std::string& lastChar)
{
    auto res = firstChar;
    for (auto ty : types) {
        res += ty->ToString() + ", ";
    }
    res.pop_back();
    res.pop_back();
    res += lastChar;
    return res;
}

bool IsBoxRefType(const Type& type)
{
    if (!type.IsRef()) {
        return false;
    }
    auto baseType = Cangjie::StaticCast<const RefType&>(type).GetBaseType();
    return baseType->IsBox();
}

bool IsFuncTypeOrClosureBaseRefType(const Type& type)
{
    if (type.IsFunc()) {
        return true;
    }
    auto refType = Cangjie::DynamicCast<const RefType*>(&type);
    if (refType == nullptr) {
        return false;
    }
    return refType->GetBaseType()->IsAutoEnvBase();
}

/**
 * if value is %0 of `%0 = Constant(null)`, then return `NullLiteral`, otherwise, return `nullptr`
 */
NullLiteral* ConvertToNullLiteral(const Value& value)
{
    // e.g. var x = VArray<Nothing, $1>(repeat: return)
    // %0: Nothing = Constant(null)
    // %1: (Int64) -> Nothing = Constant(null)
    // %2: Int64 = Constant(1i)
    // %3: VArray<Nothing, $1> = VArrayBuilder(%2, %1, %0)
    // there are two `Constant(null)`, %0 and %1, so we need to regard %0 as non-NullLiteral
    if (value.GetType()->IsNothing()) {
        return nullptr;
    }
    auto localVar = Cangjie::DynamicCast<const LocalVar*>(&value);
    if (localVar == nullptr) {
        return nullptr;
    }
    auto constExpr = Cangjie::DynamicCast<Constant*>(localVar->GetExpr());
    if (constExpr == nullptr) {
        return nullptr;
    }
    return Cangjie::DynamicCast<NullLiteral*>(constExpr->GetValue());
}

Value* GetOwnerFuncOrLambda(const BlockGroup& blockGroup)
{
    if (auto ownerFunc = blockGroup.GetOwnerFunc()) {
        return ownerFunc;
    }
    auto ownerExpr = blockGroup.GetOwnerExpression();
    CJC_NULLPTR_CHECK(ownerExpr);
    if (ownerExpr->IsLambda()) {
        return ownerExpr->GetResult();
    }
    auto parentBG = ownerExpr->GetParentBlockGroup();
    CJC_NULLPTR_CHECK(parentBG);
    return GetOwnerFuncOrLambda(*parentBG);
}

void CollectFuncAndGenericTypesRecursively(
    const Value& func, std::vector<std::pair<const Value*, std::vector<GenericType*>>>& result)
{
    if (auto lambdaRes = Cangjie::DynamicCast<const LocalVar*>(&func)) {
        auto lambda = Cangjie::StaticCast<Lambda*>(lambdaRes->GetExpr());
        auto parentBG = lambda->GetParentBlockGroup();
        CJC_NULLPTR_CHECK(parentBG);
        auto parentFuncOrLambda = GetOwnerFuncOrLambda(*parentBG);
        CollectFuncAndGenericTypesRecursively(*parentFuncOrLambda, result);
        result.emplace_back(lambdaRes, lambda->GetGenericTypeParams());
    } else {
        auto funcBase = Cangjie::VirtualCast<const FuncBase*>(&func);
        result.emplace_back(funcBase, funcBase->GetGenericTypeParams());
    }
}

std::string GetFuncIdentifier(const Value& func)
{
    if (auto funcBase = Cangjie::DynamicCast<const FuncBase*>(&func)) {
        return funcBase->GetIdentifier();
    } else {
        auto lambda = Cangjie::StaticCast<const LocalVar&>(func).GetExpr();
        return Cangjie::StaticCast<Lambda*>(lambda)->GetIdentifier();
    }
}
} // namespace

CHIRChecker::CHIRChecker(const Package& package, const Cangjie::GlobalOptions& opts, CHIRBuilder& builder)
    : package(package), opts(opts), builder(builder)
{
}

bool CHIRChecker::CheckPackage(const std::unordered_set<Rule>& r)
{
    optionalRules = r;
    ParallelCheck(&CHIRChecker::CheckFunc, package.GetGlobalFuncs());
    ParallelCheck(&CHIRChecker::CheckGlobalVar, package.GetGlobalVars());
    ParallelCheck(&CHIRChecker::CheckImportedVarAndFuncs, package.GetImportedVarAndFuncs());
    ParallelCheck(&CHIRChecker::CheckStructDef, package.GetAllStructDef());
    ParallelCheck(&CHIRChecker::CheckClassDef, package.GetAllClassDef());
    ParallelCheck(&CHIRChecker::CheckEnumDef, package.GetAllEnumDef());
    ParallelCheck(&CHIRChecker::CheckExtendDef, package.GetAllExtendDef());

    for (auto id : duplicatedGlobalIds) {
        Errorln("duplicated identifier `" + id + "` found.");
    }
    return checkResult;
}

void CHIRChecker::Warningln(const std::string& info)
{
    errorMessage << "chir checker warning:\n    " + info + "\n";
}

void CHIRChecker::Errorln(const std::string& info)
{
    errorMessage << "chir checker error:\n    " + info + "\n";
    checkResult = false;
}

void CHIRChecker::WarningInFunc(const Value& func, const std::string& info)
{
    Warningln("in function " + func.GetIdentifier() + ", " + info);
}

void CHIRChecker::WarningInExpr(const Value& func, const Expression& expr, const std::string& info)
{
    WarningInFunc(func, "in `" + GetExpressionString(expr) + "`, " + info);
}

void CHIRChecker::ErrorInFunc(const Value& func, const std::string& info)
{
    Errorln("in function " + func.GetIdentifier() + ", " + info);
}

void CHIRChecker::ErrorInLambdaOrFunc(const FuncBase& func, const Lambda* lambda, const std::string& info)
{
    if (lambda == nullptr) {
        ErrorInFunc(func, info);
    } else {
        ErrorInExpr(func, *lambda, info);
    }
}

void CHIRChecker::ErrorInExpr(const Value& func, const Expression& expr, const std::string& info)
{
    ErrorInFunc(func, "in `" + GetExpressionString(expr) + "`, " + info);
}

void CHIRChecker::TypeCheckError(
    const Expression& expr, const Value& value, const std::string& expectedType, const Func& topLevelFunc)
{
    auto errMsg = "value " + value.GetIdentifier() + " used in " + GetExpressionString(expr) + " has type " +
        value.GetType()->ToString() + ", but " + expectedType + " type is expected.";
    ErrorInFunc(topLevelFunc, errMsg);
}

bool CHIRChecker::OperandNumIsEqual(size_t expectedNum, const Expression& expr, const Func& topLevelFunc)
{
    auto realNum = expr.GetOperands().size();
    if (expectedNum != realNum) {
        auto errMsg = "expect " + std::to_string(expectedNum) +
            " operand(s), but there are " + std::to_string(realNum) + " in fact.";
        ErrorInExpr(topLevelFunc, expr, errMsg);
        return false;
    }
    return true;
}

bool CHIRChecker::OperandNumIsEqual(
    const std::vector<size_t>& expectedNum, const Expression& expr, const Func& topLevelFunc)
{
    CJC_ASSERT(!expectedNum.empty());
    auto realNum = expr.GetOperands().size();
    for (auto n : expectedNum) {
        if (n == realNum) {
            return true;
        }
    }
    std::string expectedNumStr = "{";
    for (auto n : expectedNum) {
        expectedNumStr += std::to_string(n) + ", ";
    }
    // remove last ", "
    expectedNumStr.pop_back();
    expectedNumStr.pop_back();
    expectedNumStr += "}";
    auto errMsg = "expect " + expectedNumStr +
        " operand(s), but there are " + std::to_string(realNum) + " in fact.";
    ErrorInExpr(topLevelFunc, expr, errMsg);
    return false;
}

bool CHIRChecker::SuccessorNumIsEqual(size_t expectedNum, const Terminator& expr, const Func& topLevelFunc)
{
    auto realNum = expr.GetSuccessors().size();
    if (expectedNum != realNum) {
        auto errMsg = "expect " + std::to_string(expectedNum) +
            " successor(s), but there are " + std::to_string(realNum) + " in fact.";
        ErrorInExpr(topLevelFunc, expr, errMsg);
        return false;
    }
    return true;
}

bool CHIRChecker::OperandNumAtLeast(size_t expectedNum, const Expression& expr, const Func& topLevelFunc)
{
    auto realNum = expr.GetOperands().size();
    if (expectedNum > realNum) {
        auto errMsg = "expect at least " + std::to_string(expectedNum) +
            " operand(s), but there are " + std::to_string(realNum) + " in fact.";
        ErrorInExpr(topLevelFunc, expr, errMsg);
        return false;
    }
    return true;
}

bool CHIRChecker::SuccessorNumAtLeast(size_t expectedNum, const Terminator& expr, const Func& topLevelFunc)
{
    auto realNum = expr.GetSuccessors().size();
    if (expectedNum > realNum) {
        auto errMsg = "expect at least " + std::to_string(expectedNum) +
            " successor(s), but there are " + std::to_string(realNum) + " in fact.";
        ErrorInExpr(topLevelFunc, expr, errMsg);
        return false;
    }
    return true;
}

bool CHIRChecker::CheckHaveResult(const Expression& expr, const Func& topLevelFunc)
{
    auto result = expr.GetResult();
    if (result == nullptr) {
        ErrorInExpr(topLevelFunc, expr, "this expression should have result.");
        return false;
    }
    return true;
}

void CHIRChecker::ShouldNotHaveResult(const Terminator& expr, const Func& topLevelFunc)
{
    if (expr.GetResult()) {
        ErrorInExpr(topLevelFunc, expr, "this terminator shouldn't have result.");
    }
}

bool CHIRChecker::TypeIsExpected(const Type& srcType, const Type& dstType)
{
    if (&srcType == &dstType) {
        return true;
    }
    // `NothingType` can be casted to any type implicitly in CHIR
    if (srcType.IsNothing()) {
        return true;
    }
    // maybe struct S <: I, S is sub type of I, but we can't send S to I directly
    if (srcType.IsStruct() && dstType.IsClass()) {
        return false;
    }
    // we can set a sub type to a parent type, it's safe in llvm ir,
    // but for legal CHIR, we still need to check this case later
    if (srcType.IsEqualOrSubTypeOf(dstType, builder)) {
        return true;
    }
    // it's a hack, in mut wrapper func, CHIR cast param to Any&, this will be corrected later
    if (srcType.StripAllRefs()->IsAny() && dstType.IsClassRef()) {
        return true;
    }
    // a hack way, we will fix later
    if (IsBoxRefType(srcType) || IsBoxRefType(dstType)) {
        return true;
    }
    return false;
}

bool CHIRChecker::CheckCustomTypeDefIdentifier(const CustomTypeDef& def)
{
    auto identifier = def.GetIdentifierWithoutPrefix();
    // 1. identifier can't be empty
    if (identifier.empty()) {
        auto errMsg = def.ToString() + " doesn't have identifier.";
        Errorln(errMsg);
        return false;
    }

    // 2. identifier can't be duplicated
    std::unique_lock<std::mutex> lock(checkIdentMutex);
    if (!identifiers.emplace(identifier).second) {
        duplicatedGlobalIds.emplace(identifier);
        return false;
    }
    return true;
}

bool CHIRChecker::CheckGlobalValueIdentifier(const Value& value)
{
    auto identifier = value.GetIdentifierWithoutPrefix();
    // 1. identifier can't be empty
    if (identifier.empty()) {
        auto errMsg = ValueSymbolToString(value) + " doesn't have identifier.";
        Errorln(errMsg);
        return false;
    }

    // 2. imported C func doesn't need to check duplicated id, there may be same id from different packages
    if (value.TestAttr(Attribute::IMPORTED) && value.GetType()->IsCFunc()) {
        return true;
    }

    // 3. identifier can't be duplicated
    std::unique_lock<std::mutex> lock(checkIdentMutex);
    if (!identifiers.emplace(identifier).second) {
        duplicatedGlobalIds.emplace(identifier);
        return false;
    }
    return true;
}

void CHIRChecker::CheckGlobalVar(const GlobalVarBase& var)
{
    // 1. check identifier
    if (!CheckGlobalValueIdentifier(var)) {
        return;
    }

    // 2. type must be ref
    if (!CheckTypeMustBeRef(*var.GetType())) {
        auto valueType = var.GetType()->StripAllRefs();
        std::string expectedType;
        if (valueType->IsReferenceType()) {
            expectedType = valueType->ToString() + "&&";
        } else {
            expectedType = valueType->ToString() + "&";
        }
        auto errMsg = "the type of global var " + var.GetIdentifier() + " should be " + expectedType +
            ", but now it's " + var.GetType()->ToString();
        Errorln(errMsg);
    }

    // 3. must have initializer or init func
    if (auto globalVar = DynamicCast<const GlobalVar*>(&var)) {
        bool initByFunc = globalVar->GetInitFunc() != nullptr;
        bool initByLiteral = globalVar->GetInitializer() != nullptr;
        auto errMsg = "global var " + globalVar->GetIdentifier() + " should be initialized by literal or function, ";
        std::string hint;
        if (initByFunc && initByLiteral) {
            hint = "can't be both.";
        } else if (!initByFunc && !initByLiteral) {
            // why doesn't it have init func
            if (!globalVar->TestAttr(Attribute::COMMON)) {
                hint = "it must have one of them.";
            }
        }
        if (!hint.empty()) {
            Errorln(errMsg);
        }
    }

    // 4. must have src code identifier
    if (!var.TestAttr(Attribute::COMPILER_ADD) && var.GetSrcCodeIdentifier().empty()) {
        auto errMsg = "global var " + var.GetIdentifier() + " doesn't have src code identifier.";
        Errorln(errMsg);
    }

    // 5. must have package name
    if (var.GetPackageName().empty()) {
        auto errMsg = "global var " + var.GetIdentifier() + " doesn't have package name.";
        Errorln(errMsg);
    }

    // 6. check static member var
    auto def = var.GetParentCustomTypeDef();
    if (def != nullptr && !var.TestAttr(Attribute::STATIC)) {
        auto errMsg = "global var " + var.GetIdentifier() +
            " should be a static member var, but it doesn't have Attribute `STATIC`.";
        Errorln(errMsg);
    } else if (def == nullptr && var.TestAttr(Attribute::STATIC)) {
        auto errMsg = "global var " + var.GetIdentifier() + " has Attribute `STATIC` but it isn't member var.";
        Errorln(errMsg);
    }
}

void CHIRChecker::CheckImportedVarAndFuncs(const ImportedValue& value)
{
    if (auto importedVar = DynamicCast<const ImportedVar*>(&value)) {
        CheckGlobalVar(*importedVar);
    } else {
        CheckFuncBase(StaticCast<const ImportedFunc&>(value));
    }
}

bool CHIRChecker::CheckFuncBase(const FuncBase& func)
{
    // 1. check identifier
    if (!CheckGlobalValueIdentifier(func)) {
        return false;
    }

    // 2. check func type
    if (!CheckFuncType(func.GetType(), nullptr, func)) {
        return false;
    }

    // 3. check parent CustomTypeDef
    if (auto parentDef = func.GetParentCustomTypeDef()) {
        if (!CheckParentCustomTypeDef(func, *parentDef, false)) {
            return false;
        }
    }

    // 4. must have package name
    if (func.GetPackageName().empty()) {
        auto errMsg = "we need to set package name in func " + func.GetIdentifier() + ".";
        Errorln(errMsg);
        return false;
    }

    // 5. check original lambda info
    if (!CheckOriginalLambdaInfo(func)) {
        return false;
    }
    return true;
}

bool CHIRChecker::CheckFuncType(const Type* type, const Lambda* lambda, const FuncBase& topLevelFunc)
{
    // 1. must have func type
    if (type == nullptr) {
        if (lambda == nullptr) {
            Errorln("func " + topLevelFunc.GetIdentifier() + " doesn't have type.");
        } else {
            ErrorInFunc(topLevelFunc, "lambda " + lambda->GetIdentifier() + " doesn't have type.");
        }
        return false;
    } else if (!type->IsFunc()) {
        if (lambda == nullptr) {
            Errorln("the type of func " + topLevelFunc.GetIdentifier() + " isn't func type.");
        } else {
            ErrorInFunc(topLevelFunc, "the type of lambda " + lambda->GetIdentifier() + " isn't func type.");
        }
        return false;
    }

    // 2. only CFunc can have variable length parameters
    auto funcType = StaticCast<const FuncType*>(type);
    auto isCFunc = funcType->IsCFunc();
    if (!isCFunc && funcType->HasVarArg()) {
        auto errMsg = "this isn't CFunc, but it has variable length parameters.";
        ErrorInLambdaOrFunc(topLevelFunc, lambda, errMsg);
        return false;
    }

    // 3. check CFunc type
    if (isCFunc) {
        return CheckCFuncType(*funcType, lambda, topLevelFunc);
    }

    // 4. check lifted lambda type
    if (lambda == nullptr && !CheckLiftedLambdaType(topLevelFunc)) {
        return false;
    }

    // 5. global var init func's return type must be `Void`
    if (lambda == nullptr && topLevelFunc.IsGVInit() && !funcType->GetReturnType()->IsVoid()) {
        auto errMsg = "function " + topLevelFunc.GetIdentifier() + "is global var init, its return type is " +
            funcType->GetReturnType()->ToString() + ", but `Void` is expected";
        Errorln(errMsg);
        return false;
    }

    // 6. check param type
    return CheckParamTypes(funcType->GetParamTypes(), lambda, topLevelFunc);
}

bool CHIRChecker::CheckParentCustomTypeDef(const FuncBase& func, const CustomTypeDef& def, bool isInDef)
{
    auto parentDef = func.GetParentCustomTypeDef();
    if (parentDef == nullptr) {
        auto errMsg = "func " + func.GetIdentifier() + " is member func of " +
            def.GetIdentifier() +", but it doesn't set parent CustomTypeDef.";
        Errorln(errMsg);
        return false;
    }
    if (parentDef != &def &&
        func.GetIdentifierWithoutPrefix().find(Cangjie::CHIRMangling::MANGLE_OPERATOR_PREFIX) != 0) {
        auto errMsg = "parent CustomTypeDef of func " + func.GetIdentifier() + " is " +
            parentDef->GetIdentifier() +", but this function is also member method of " + def.GetIdentifier() + ".";
        Errorln(errMsg);
        return false;
    }
    if (isInDef) {
        return true;
    }
    for (auto method : def.GetMethods()) {
        if (method == &func) {
            return true;
        }
    }
    auto errMsg = "parent CustomTypeDef of func " + func.GetIdentifier() + " is " +
        parentDef->GetIdentifier() +", but there isn't this method in this CustomTypeDef.";
    Errorln(errMsg);
    return false;
}

bool CHIRChecker::CheckOriginalLambdaInfo(const FuncBase& func)
{
    if (!func.IsLambda()) {
        return true;
    }

    bool res = true;
    // 1. must set original lambda type
    auto originalFuncType = func.GetOriginalLambdaType();
    if (originalFuncType == nullptr) {
        auto errMsg = "func " + func.GetIdentifier() + " is lifted lambda, original lambda type should be set.";
        Errorln(errMsg);
        res = false;
    }

    // 2. check original generic type params
    auto originalTypeParams = func.GetOriginalGenericTypeParams();
    auto curTypeParams = func.GetGenericTypeParams();
    if (originalTypeParams.size() > curTypeParams.size()) {
        auto errMsg = "func " + func.GetIdentifier() +
            " is lifted lambda, original lambda generic type params' size is " +
            std::to_string(originalTypeParams.size()) + ", current func's generic type params' size is " +
            std::to_string(curTypeParams.size()) + ", original size should be equal or less than current size.";
        Errorln(errMsg);
        res = false;
    }
    return res;
}

bool CHIRChecker::CheckCFuncType(const FuncType& funcType, const Lambda* lambda, const FuncBase& topLevelFunc)
{
    // 1. CFunc can't be member method, can only be lambda or global func
    if (lambda == nullptr && topLevelFunc.GetParentCustomTypeDef() != nullptr) {
        auto errMsg = "this is a member method, but @C can only used in global function.";
        ErrorInLambdaOrFunc(topLevelFunc, lambda, errMsg);
        return false;
    }

    // 2. parameter type in CFunc must be CType
    bool typeMatched = true;
    auto paramTypes = funcType.GetParamTypes();
    for (size_t i = 0; i < paramTypes.size(); ++i) {
        auto pType = paramTypes[i];
        if (!IsCType(*pType)) {
            auto errMsg = "this is a CFunc, but the " + IndexToString(i) + " paramter type is " +
                pType->ToString() + ", it's not a CType.";
            ErrorInLambdaOrFunc(topLevelFunc, lambda, errMsg);
            typeMatched = false;
        }
    }

    // 3. check return type
    auto retType = funcType.GetReturnType();
    if (!IsCType(*retType)) {
        // 3.1 return type must be CType
        auto errMsg = "this is a CFunc, but return type is " + retType->ToString() + ", it's not a CType.";
        ErrorInLambdaOrFunc(topLevelFunc, lambda, errMsg);
        typeMatched = false;
    } else if (retType->IsVArray()) {
        // 3.2 VArray can't be CFunc's return type
        ErrorInLambdaOrFunc(topLevelFunc, lambda, "VArray can't be CFunc's return type.");
        typeMatched = false;
    }
    return typeMatched;
}

bool CHIRChecker::CheckLiftedLambdaType(const FuncBase& func)
{
    if (!func.IsLambda()) {
        return true;
    }

    // 1. the first param type must be Class-Env&
    auto errMsg = "func " + func.GetIdentifier() +
        " is lifted lambda, its first parameter type should be Class-Env&, but now func type is " +
        func.GetFuncType()->ToString() + ".";
    auto paramTypes = func.GetFuncType()->GetParamTypes();
    if (paramTypes.empty()) {
        Errorln(errMsg);
        return false;
    }
    auto firstParamType = DynamicCast<RefType*>(paramTypes[0]);
    if (firstParamType == nullptr) {
        Errorln(errMsg);
        return false;
    }
    auto firstParamTypeDeref = firstParamType->GetBaseType();
    if (!firstParamTypeDeref->IsAutoEnv()) {
        Errorln(errMsg);
        return false;
    }
    if (firstParamTypeDeref->IsAutoEnvBase()) {
        errMsg = "func " + func.GetIdentifier() +
            " is lifted lambda, its first parameter type should be Class-Env&, but now it's Class-EnvBase&.";
        Errorln(errMsg);
        return false;
    }
    return true;
}

bool CHIRChecker::CheckParamTypes(
    const std::vector<Type*>& paramTypes, const Lambda* lambda, const FuncBase& topLevelFunc)
{
    auto parentType = topLevelFunc.GetParentCustomTypeOrExtendedType();
    bool firstParamIsThis =
        (lambda == nullptr) && (parentType != nullptr) && !topLevelFunc.TestAttr(Attribute::STATIC);
    auto funcIsInit = [&topLevelFunc]() {
        if (topLevelFunc.IsConstructor()) {
            return true;
        }
        // a hack way, in cjmp, member var init func is like xxx$varInit
        return topLevelFunc.GetSrcCodeIdentifier().find("$varInit") != std::string::npos;
    };
    bool firstParamIsRef = firstParamIsThis &&
        (parentType->IsReferenceType() || topLevelFunc.TestAttr(Attribute::MUT) || funcIsInit());
    bool typeMatched = true;
    for (size_t i = 0; i < paramTypes.size(); ++i) {
        auto pType = paramTypes[i];
        if (i == 0 && firstParamIsThis) {
            // a hack way, we need to replace Any& to correct type
            auto anyTyRef = builder.GetType<RefType>(builder.GetAnyTy());
            Type* expectedType = firstParamIsRef ? builder.GetType<RefType>(parentType) : parentType;
            // 1. check the 1st parameter type
            if (pType != expectedType && pType != anyTyRef) {
                auto errMsg = expectedType->ToString() +
                    " type is expected for the 1st parameter, but now it's " + pType->ToString() + ".";
                ErrorInFunc(topLevelFunc, errMsg);
                typeMatched = false;
            }
            continue;
        }
        if (pType->IsValueType() || pType->IsGeneric()) {
            continue;
        }
        auto errMsg = "the " + IndexToString(i) + " paramter type is " + pType->ToString();
        // 2. class type must add `&`
        if (pType->IsReferenceType()) {
            ErrorInLambdaOrFunc(topLevelFunc, lambda, errMsg + ", but you should add `&` behind it.");
            typeMatched = false;
            continue;
        }
        if (pType->IsRef()) {
            auto baseType = StaticCast<RefType*>(pType)->GetBaseType();
            // 3. can't use This& as parameter type
            if (baseType->IsThis()) {
                ErrorInLambdaOrFunc(topLevelFunc, lambda, errMsg + ", it shouldn't be parameter's type.");
                typeMatched = false;
            } else if (!baseType->IsClassOrArray() && !baseType->IsBox()) {
                // 4. value type and generic type can't use ref type, only class type, Array and box type
                ErrorInLambdaOrFunc(topLevelFunc, lambda,
                    errMsg + ", but only Class type, RawArray type and Box type can use `&`.");
                typeMatched = false;
            }
        }
    }
    return typeMatched;
}

void CHIRChecker::CheckCustomType(const CustomTypeDef& def)
{
    auto type = def.GetType();
    if (type == nullptr) {
        Errorln(def.GetIdentifier() + " should set type.");
        return;
    }
    if (def.GetCustomKind() == CustomDefKind::TYPE_STRUCT && !type->IsStruct()) {
        Errorln(def.GetIdentifier() + " is struct definition, but its type is " + type->ToString() + ".");
    } else if (def.GetCustomKind() == CustomDefKind::TYPE_CLASS && !type->IsClass()) {
        Errorln(def.GetIdentifier() + " is class definition, but its type is " + type->ToString() + ".");
    } else if (def.GetCustomKind() == CustomDefKind::TYPE_ENUM && !type->IsEnum()) {
        Errorln(def.GetIdentifier() + " is enum definition, but its type is " + type->ToString() + ".");
    } else if (def.GetCustomKind() == CustomDefKind::TYPE_EXTEND && !type->IsCustomType() && !type->IsBuiltinType()) {
        Errorln(def.GetIdentifier() + " is extend definition, but its extended type is " + type->ToString() +
            ", it should be custom type or builtin type.");
    }
}

void CHIRChecker::CheckInstanceMemberVar(const CustomTypeDef& def)
{
    std::unordered_set<std::string> memberVarNames;
    auto parentMemberVars = def.GetAllInstanceVars();
    // 1. check current def's member vars
    for (const auto& var : def.GetDirectInstanceVars()) {
        parentMemberVars.pop_back();
        // 1.1 member var should have name
        if (var.name.empty()) {
            Errorln("member var in " + def.GetIdentifier() + " doesn't have name.");
            continue;
        }
        // 1.2 member var name can't be duplicated
        if (memberVarNames.count(var.name) != 0) {
            Errorln("duplicated member var name `" + var.name + "` in " + def.GetIdentifier() + ".");
            continue;
        }
        // 1.3 member var's outer def can't be null
        if (var.outerDef == nullptr) {
            Errorln("member var " + var.name + " in " + def.GetIdentifier() + " doesn't set outer CustomTypeDef.");
            continue;
        }
        // 1.4 member var's outer def must be current def
        if (var.outerDef != &def) {
            Errorln("outer CustomTypeDef of member var " + var.name + " is " +
                var.outerDef->GetIdentifier() + ", but it should be " + def.GetIdentifier() + ".");
            continue;
        }
        // 1.5 member var's type can't be null
        if (var.type == nullptr) {
            Errorln("member var " + var.name + " doesn't set type.");
            continue;
        }
    }

    // 2. check parent def's member vars
    for (const auto& var : parentMemberVars) {
        if (var.TestAttr(Attribute::PUBLIC) || var.TestAttr(Attribute::PROTECTED)) {
            if (memberVarNames.count(var.name) != 0 && var.outerDef != nullptr) {
                Errorln("duplicated member var name `" + var.name + "` in " + def.GetIdentifier() +
                    ", it has same name with parent class " + var.outerDef->GetIdentifier() + " member var.");
            }
        }
    }
}

void CHIRChecker::CheckStaticMemberVar(const CustomTypeDef& def)
{
    for (auto var : def.GetStaticMemberVars()) {
        // 1. must have Attribute::STATIC
        if (!var->TestAttr(Attribute::STATIC)) {
            Errorln("static member var " + var->GetIdentifier() + " of " + def.GetIdentifier() +
                " doesn't have Attribute `STATIC`.");
        }
        // 2. parent custom type def can't be null
        if (var->GetParentCustomTypeDef() == nullptr) {
            Errorln("static member var " + var->GetIdentifier() + " doesn't set parent CustomTypeDef.");
            continue;
        } else if (var->GetParentCustomTypeDef() != &def) {
            // 3. parent custom type def must be current def
            Errorln("parent CustomTypeDef of static member var " + var->GetIdentifier() + " is " +
                var->GetParentCustomTypeDef()->GetIdentifier() + ", but it should be " + def.GetIdentifier() + ".");
            continue;
        }
    }
}

void CHIRChecker::CheckVTable(const CustomTypeDef& def)
{
    for (const auto& it : def.GetDefVTable().GetTypeVTables()) {
        auto parentInstType = it.GetSrcParentType();
        auto parentDef = parentInstType->GetClassDef();
        auto parentOriginalType = parentDef->GetType();
        const auto& parentVTables = parentDef->GetDefVTable();
        const auto& parentVTable = parentVTables.GetExpectedTypeVTable(*parentOriginalType);
        // 1. the same src parent type must be found in parent def's vtable
        if (parentVTable.IsEmpty()) {
            Errorln("in vtable of " + def.GetIdentifier() + ", parent type " +
                parentInstType->ToString() + " can't be found in vtable of parent class " +
                parentDef->GetIdentifier() + ".");
            continue;
        }
        // 2. virtual method num must be equal
        if (it.GetMethodNum() != parentVTable.GetMethodNum()) {
            Errorln("in vtable of " + def.GetIdentifier() + ", parent type " +
                parentInstType->ToString() + " has " + std::to_string(it.GetMethodNum()) +
                " virtual method(s), but in vtable of parent class " + parentDef->GetIdentifier() +
                ", this parent type has " + std::to_string(parentVTable.GetMethodNum()) + " virtual method(s).");
            continue;
        }
        // 3. only interface or abstract class can have unimplemented virtual method
        bool defIsInterfaceOrAbstract = def.IsInterface() || def.TestAttr(Attribute::ABSTRACT);
        for (size_t i = 0; i < it.GetMethodNum(); ++i) {
            if (it.GetVirtualMethods()[i].GetVirtualMethod() == nullptr && !defIsInterfaceOrAbstract) {
                Errorln("in vtable of " + def.GetIdentifier() + ", parent type " + parentInstType->ToString() +
                    ", the " + IndexToString(i) +
                    " virtual method is unimplemented, but only interface or abstract class can have this kind of "
                    "method.");
            }
        }
    }
}

void CHIRChecker::CheckCustomTypeDef(const CustomTypeDef& def)
{
    // 1. check identifier
    if (!CheckCustomTypeDefIdentifier(def)) {
        return;
    }

    // 2. check type
    CheckCustomType(def);

    // 3. check instance member var
    CheckInstanceMemberVar(def);

    // 4. check static member var
    CheckStaticMemberVar(def);

    // 5. check vtable
    CheckVTable(def);

    // 6. check methods
    for (auto method : def.GetMethods()) {
        CheckParentCustomTypeDef(*method, def, true);
    }

    // 7. check parent type
    for (auto parentType : def.GetImplementedInterfaceTys()) {
        if (!parentType->GetClassDef()->IsInterface()) {
            Errorln("there is non-interface type " + parentType->ToString() +
                " in implemented interface list of " + def.GetIdentifier() + ".");
        }
    }
}

void CHIRChecker::CheckCStruct(const StructDef& def)
{
    if (!def.IsCStruct()) {
        return;
    }

    // 1. member var's type must be CType
    for (const auto& var : def.GetDirectInstanceVars()) {
        if (var.type != nullptr && !IsCType(*var.type)) {
            Errorln("in C-struct " + def.GetIdentifier() + ", member var " + var.name +
                " has type " + var.type->ToString() + ", but it should be CType.");
        }
    }
}
void CHIRChecker::CheckStructDef(const StructDef& def)
{
    // 1. check custom type def
    CheckCustomTypeDef(def);

    // 2. check C-struct
    CheckCStruct(def);
}

void CHIRChecker::CheckAbstractMethod(const ClassDef& def)
{
    for (const auto& method : def.GetAbstractMethods()) {
        // 1. must have method name
        if (method.methodName.empty()) {
            Errorln("abstract method in " + def.GetIdentifier() + " doesn't have name.");
            continue;
        }
        // 2. must have Attribute::ABSTRACT
        if (!method.TestAttr(Attribute::ABSTRACT)) {
            Errorln("abstract method " + method.methodName + " of " + def.GetIdentifier() +
                " doesn't have Attribute `ABSTRACT`.");
        }
        // 3. must have method type
        if (method.methodTy == nullptr || !method.methodTy->IsFunc()) {
            Errorln("abstract method " + method.methodName + " of " + def.GetIdentifier() +
                " doesn't have func type.");
        }
        // 4. parent custom type def can't be null
        if (method.parent == nullptr) {
            Errorln("abstract method " + method.methodName + " doesn't set parent CustomTypeDef.");
        } else if (method.parent != &def) {
            // 5. parent custom type def must be current def
            Errorln("parent CustomTypeDef of abstract method " + method.methodName + " is " +
                method.parent->GetIdentifier() + ", but it should be " + def.GetIdentifier() + ".");
        }
        // 6. abstract method must be public or protected
        if (!method.TestAttr(Attribute::PUBLIC) && !method.TestAttr(Attribute::PROTECTED)) {
            Errorln("abstract method " + method.methodName + " of " + def.GetIdentifier() +
                " must be public or protected.");
        }
    }
}

void CHIRChecker::CheckEnumDef(const EnumDef& def)
{
    // 1. check custom type def
    CheckCustomTypeDef(def);
}

void CHIRChecker::CheckExtendDef(const ExtendDef& def)
{
    // 1. check custom type def
    CheckCustomTypeDef(def);

    // 2. extended type can't be null
    auto type = def.GetExtendedType();
    if (type == nullptr) {
        Errorln("extend definition " + def.GetIdentifier() + " doesn't set extended type.");
    } else if (!type->IsCustomType() && !type->IsBuiltinType()) {
        // 3. extended type must be custom type or builtin type
        Errorln("extend definition " + def.GetIdentifier() + " extends " +
            type->ToString() + ", it should be custom type or builtin type.");
    }
}

void CHIRChecker::CheckClassDef(const ClassDef& def)
{
    // 1. check custom type def
    CheckCustomTypeDef(def);

    // 2. check abstract method
    CheckAbstractMethod(def);

    // 3. check super class
    if (auto parent = def.GetSuperClassDef(); parent &&
        !parent->TestAttr(Attribute::VIRTUAL) && !parent->TestAttr(Attribute::ABSTRACT)) {
        Errorln("the super class " + parent->GetIdentifier() + " of class " + def.GetIdentifier() +
            " isn't open class.");
    }
}

void CHIRChecker::CheckFunc(const Func& func)
{
    // 1. check FuncBase
    if (!CheckFuncBase(func)) {
        return;
    }

    // 2. func must have body
    if (func.GetBody() == nullptr) {
        Errorln("func " + func.GetIdentifier() + " doesn't have body.");
        return;
    }

    // 3. check func params
    CheckFuncParams(func.GetParams(), *func.GetFuncType(), func.GetIdentifier());

    // 4. check func return value
    CheckFuncRetValue(func.GetReturnValue(), *func.GetFuncType()->GetReturnType(), nullptr, func);

    // 5. check func body
    // a hack way, there are some wrong AST nodes in this kind of func, we will fix later
    if (auto id = func.GetIdentifier(); id.find("$Mocked") == std::string::npos) {
        CheckBlockGroup(*func.GetBody(), func);
    }

    // 6. check local identifier
    CheckLocalId(*func.GetBody(), func);

    // 7. check unreachable op and unreachable generic type
    CheckUnreachableOpAndGenericTyInFuncBody(*func.GetBody());

    // 8. can't be abstract
    if (func.TestAttr(Attribute::ABSTRACT)) {
        Errorln("func " + func.GetIdentifier() + " shouldn't have attribute: ABSTRACT.");
    }
}

void CHIRChecker::CheckFuncParams(
    const std::vector<Parameter*>& params, const FuncType& funcType, const std::string& funcIdentifier)
{
    auto paramTypesToString = [](const std::vector<Type*>& paramTypes) {
        std::string paramsStr = "(";
        for (size_t i = 0; i < paramTypes.size(); ++i) {
            if (i > 0) {
                paramsStr += ", ";
            }
            paramsStr += paramTypes[i]->ToString();
        }
        paramsStr += ")";
        return paramsStr;
    };
    std::vector<Type*> paramTypesInBody;
    for (auto p : params) {
        paramTypesInBody.emplace_back(p->GetType());
    }
    auto paramsStrInBody = paramTypesToString(paramTypesInBody);
    auto paramTypesInFuncType = funcType.GetParamTypes();
    auto paramsStrInFuncType = paramTypesToString(paramTypesInFuncType);

    // 1. size must be equal
    if (paramTypesInFuncType.size() != params.size()) {
        auto errMsg = "inconsistent number of parameters in func " + funcIdentifier + ", there are " +
            std::to_string(params.size()) + " parameter(s) in func body, but there are " +
            std::to_string(paramTypesInFuncType.size()) + " parameter(s) in func type.\n";
        auto hint1 = "        parameter type in func body is " + paramsStrInBody + "\n";
        auto hint2 = "        parameter type in func type is " + paramsStrInFuncType + "\n";
        Errorln(errMsg + hint1 + hint2);
        return;
    }

    // 2. type must be equal
    std::vector<size_t> errIdx;
    for (size_t i = 0; i < paramTypesInFuncType.size(); i++) {
        if (paramTypesInFuncType[i] != params[i]->GetType()) {
            errIdx.emplace_back(i);
        }
    }
    std::string errMsg;
    if (!errIdx.empty()) {
        errMsg = "inconsistent parameter type between func body and func type";
    }
    for (auto i : errIdx) {
        errMsg += ", " + std::to_string(i) + "-th";
    }
    if (!errIdx.empty()) {
        errMsg += " parameter type(s) are mismatched.\n";
        auto hint1 = "        parameter type in func body is " + paramsStrInBody + "\n";
        auto hint2 = "        parameter type in func type is " + paramsStrInFuncType + "\n";
        Errorln(errMsg + hint1 + hint2);
    }
}

void CHIRChecker::CheckFuncRetValue(
    const LocalVar* retVal, const Type& retType, const Lambda* lambda, const Func& topLevelFunc)
{
    if (retVal == nullptr) {
        // 1. return value can be null only when return type is Unit, Nothing or Void
        if (!retType.IsUnit() && !retType.IsNothing() && !retType.IsVoid()) {
            ErrorInLambdaOrFunc(topLevelFunc, lambda, "you should set a return value.");
        }
    } else if (!retVal->GetType()->IsRef()) {
        // 2. return value's type must be ref type, it's a memory address
        ErrorInLambdaOrFunc(topLevelFunc, lambda,
            "its return value is " + retVal->GetIdentifier() + ", this value's type should be reference type.");
    } else if (StaticCast<RefType*>(retVal->GetType())->GetBaseType() != &retType) {
        // 3. return value's type must be func's return type
        auto errMsg = "its return value is " + retVal->GetIdentifier() + ", this value's type should be " +
            retType.ToString() + "&, but not " + retVal->GetType()->ToString() + ".";
        ErrorInLambdaOrFunc(topLevelFunc, lambda, errMsg);
    }
}

void CHIRChecker::CheckLocalId(BlockGroup& blockGroup, const Func& topLevelFunc)
{
    std::unordered_set<std::string> allIds;
    std::set<std::string> duplicatedLocalIds;
    std::vector<Expression*> exprResWithoutId;

    for (auto p : topLevelFunc.GetParams()) {
        if (!allIds.emplace(p->GetIdentifier()).second) {
            duplicatedLocalIds.emplace(p->GetIdentifier());
        }
    }
    // 1. local id can't be duplicated in one block group
    // 2. local id can't be empty in one expression
    std::function<VisitResult(Expression&)> preVisit =
        [&allIds, &duplicatedLocalIds, &exprResWithoutId, &preVisit](Expression& expr) {
        auto result = expr.GetResult();
        if (result == nullptr) {
            return VisitResult::CONTINUE;
        }
        if (result->GetIdentifier().empty()) {
            exprResWithoutId.emplace_back(&expr);
        } else if (!allIds.emplace(result->GetIdentifier()).second) {
            duplicatedLocalIds.emplace(result->GetIdentifier());
        }
        if (expr.IsLambda()) {
            for (auto p : StaticCast<Lambda&>(expr).GetParams()) {
                if (!allIds.emplace(p->GetIdentifier()).second) {
                    duplicatedLocalIds.emplace(p->GetIdentifier());
                }
            }
            Visitor::Visit(*StaticCast<Lambda&>(expr).GetBody(), preVisit);
        }
        return VisitResult::CONTINUE;
    };
    Visitor::Visit(blockGroup, preVisit);
    if (!duplicatedLocalIds.empty()) {
        std::string errMsg = "there are duplicated local id: ";
        for (auto id : duplicatedLocalIds) {
            errMsg += id + ", ";
        }
        errMsg.pop_back();
        errMsg.pop_back();
        ErrorInFunc(topLevelFunc, errMsg);
    }
    for (auto expr : exprResWithoutId) {
        auto errMsg = "the result of expression " + expr->ToString() + " doesn't have identifier.";
        ErrorInFunc(topLevelFunc, errMsg);
    }
}

void CHIRChecker::CheckUnreachableOpAndGenericTyInFuncBody(const BlockGroup& blockGroup)
{
    std::vector<Value*> reachableValues;
    std::vector<GenericType*> reachableGenericTypes;
    // 1. check in func body
    CheckUnreachableOpAndGenericTyInBG(blockGroup, reachableValues, reachableGenericTypes);
}

void CHIRChecker::CheckUnreachableOpAndGenericTyInBG(const BlockGroup& blockGroup,
    std::vector<Value*>& reachableValues, std::vector<GenericType*>& reachableGenericTypes)
{
    auto valSize = reachableValues.size();
    auto tySize = reachableGenericTypes.size();
    if (auto e = blockGroup.GetOwnerExpression(); e && e->IsLambda()) {
        auto lambda = StaticCast<Lambda*>(e);
        for (auto genericTy : lambda->GetGenericTypeParams()) {
            reachableGenericTypes.emplace_back(genericTy);
        }
        const auto& params = lambda->GetParams();
        for (size_t i = 0; i < params.size(); ++i) {
            reachableValues.emplace_back(params[i]);
            // 1. generic type in lambda parameter must be reachable
            if (!GenericTypeIsInContainer(*params[i]->GetType(), reachableGenericTypes)) {
                auto errMsg = "generic type " + params[i]->ToString() + "is unreachable, the type is " +
                    std::to_string(i) + "-th parameter in lambda " + lambda->GetIdentifier() + ".";
                ErrorInFunc(*blockGroup.GetTopLevelFunc(), errMsg);
            }
        }
    } else if (auto func = blockGroup.GetOwnerFunc()) {
        if (auto parentDef = func->GetParentCustomTypeDef()) {
            for (auto genericTy : parentDef->GetGenericTypeParams()) {
                reachableGenericTypes.emplace_back(genericTy);
            }
        }
        for (auto genericTy : func->GetGenericTypeParams()) {
            reachableGenericTypes.emplace_back(genericTy);
        }
        const auto& params = func->GetParams();
        for (size_t i = 0; i < params.size(); ++i) {
            reachableValues.emplace_back(params[i]);
            // 2. generic type in global func parameter must be reachable
            if (!GenericTypeIsInContainer(*params[i]->GetType(), reachableGenericTypes)) {
                auto errMsg = "generic type " + params[i]->ToString() + "is unreachable, the type is " +
                    std::to_string(i) + "-th parameter in function " + func->GetIdentifier() + ".";
                ErrorInFunc(*func, errMsg);
            }
        }
    }
    // 3. check in entry block
    if (auto entryBlock = blockGroup.GetEntryBlock()) {
        std::unordered_set<const Block*> visitedBlocks;
        CheckUnreachableOpAndGenericTyInBlock(*entryBlock, reachableValues, reachableGenericTypes, visitedBlocks);
    }
    if (reachableValues.size() > valSize) {
        reachableValues.erase(reachableValues.begin() + static_cast<long>(valSize), reachableValues.end());
    }
    if (reachableGenericTypes.size() > tySize) {
        reachableGenericTypes.erase(
            reachableGenericTypes.begin() + static_cast<long>(tySize), reachableGenericTypes.end());
    }
}

void CHIRChecker::CheckUnreachableOpAndGenericTyInBlock(const Block& block, std::vector<Value*>& reachableValues,
    std::vector<GenericType*>& reachableGenericTypes, std::unordered_set<const Block*>& visitedBlocks)
{
    if (!visitedBlocks.emplace(&block).second) {
        return;
    }
    auto valSize = reachableValues.size();
    for (auto expr : block.GetExpressions()) {
        auto typeSize = reachableGenericTypes.size();
        if (auto lambda = DynamicCast<Lambda*>(expr)) {
            auto tempTypes = lambda->GetGenericTypeParams();
            reachableGenericTypes.insert(reachableGenericTypes.end(), tempTypes.begin(), tempTypes.end());
        }
        // 1. check in expressions
        CheckUnreachableOpAndGenericTyInExpr(*expr, reachableValues, reachableGenericTypes);
        if (auto result = expr->GetResult()) {
            reachableValues.emplace_back(result);
        }
        // 2. check in sub block groups
        for (auto bg : expr->GetBlockGroups()) {
            CheckUnreachableOpAndGenericTyInBG(*bg, reachableValues, reachableGenericTypes);
        }
        if (reachableGenericTypes.size() > typeSize) {
            reachableGenericTypes.erase(
                reachableGenericTypes.begin() + static_cast<long>(typeSize), reachableGenericTypes.end());
        }
        // 3. check successor blocks
        if (expr->IsTerminator()) {
            auto terminator = Cangjie::StaticCast<Terminator*>(expr);
            for (auto suc : terminator->GetSuccessors()) {
                CheckUnreachableOpAndGenericTyInBlock(*suc, reachableValues, reachableGenericTypes, visitedBlocks);
            }
        }
    }
    if (reachableValues.size() > valSize) {
        reachableValues.erase(reachableValues.begin() + static_cast<long>(valSize), reachableValues.end());
    }
}

void CHIRChecker::CheckUnreachableOpAndGenericTyInExpr(
    const Expression& expr, std::vector<Value*>& reachableValues, std::vector<GenericType*>& reachableGenericTypes)
{
    // 1. operand in expression must be reachable
    CheckUnreachableOperandInExpr(expr, reachableValues);

    // 2. generic type in expression must be reachable
    CheckUnreachableGenericTypeInExpr(expr, reachableGenericTypes);
}

void CHIRChecker::CheckUnreachableOperandInExpr(const Expression& expr, std::vector<Value*>& reachableValues)
{
    for (auto op : expr.GetOperands()) {
        if (op->IsLiteral() || op->IsGlobal()) {
            continue;
        }
        if (std::find(reachableValues.begin(), reachableValues.end(), op) == reachableValues.end()) {
            ErrorInFunc(*expr.GetTopLevelFunc(), op->GetIdentifier() + " in " + expr.ToString() + " is unreachable.");
        }
    }
}

void CHIRChecker::CheckUnreachableGenericTypeInExpr(
    const Expression& expr, std::vector<GenericType*>& reachableGenericTypes)
{
    // 1. generic type in result must be reachable
    if (auto result = expr.GetResult()) {
        if (!GenericTypeIsInContainer(*result->GetType(), reachableGenericTypes)) {
            auto errMsg = "generic type " + result->GetType()->ToString() +
                " is unreachable, the type is from result in expression " + expr.ToString() + ".";
            ErrorInFunc(*expr.GetTopLevelFunc(), errMsg);
        }
    }
    auto eKind = expr.GetExprKind();
    if (eKind == ExprKind::ALLOCATE || eKind == ExprKind::ALLOCATE_WITH_EXCEPTION) {
        auto base = AllocateBase(&expr);
        // 2. generic type in Allocate must be reachable
        if (!GenericTypeIsInContainer(*base.GetType(), reachableGenericTypes)) {
            auto errMsg = "generic type " + base.GetType()->ToString() +
                " is unreachable, the type is allocated type in expression " + expr.ToString() + ".";
            ErrorInFunc(*expr.GetTopLevelFunc(), errMsg);
        }
    } else if (Is<FuncCall>(expr) || Is<FuncCallWithException>(expr)) {
        auto base = FuncCallBase(&expr);
        // 3. generic type in instantiated type args must be reachable
        const auto& instantiatedTypeArgs = base.GetInstantiatedTypeArgs();
        for (size_t i = 0; i < instantiatedTypeArgs.size(); ++i) {
            if (!GenericTypeIsInContainer(*instantiatedTypeArgs[i], reachableGenericTypes)) {
                auto errMsg = "generic type " + instantiatedTypeArgs[i]->ToString() + "is unreachable, the type is " +
                    std::to_string(i) + "-th instantiated type args in expression " + expr.ToString() + ".";
                ErrorInFunc(*expr.GetTopLevelFunc(), errMsg);
            }
        }
        // 4. generic type in ThisType must be reachable
        if (auto thisType = base.GetThisType()) {
            if (!GenericTypeIsInContainer(*thisType, reachableGenericTypes)) {
                auto errMsg = "generic type " + thisType->ToString() +
                    " is unreachable, the type is ThisType in expression " + expr.ToString() + ".";
                ErrorInFunc(*expr.GetTopLevelFunc(), errMsg);
            }
        }
    } else if (auto rtti = DynamicCast<const GetRTTIStatic*>(&expr)) {
        // 5. generic type in GetRTTIStatic must be reachable
        if (!GenericTypeIsInContainer(*rtti->GetRTTIType(), reachableGenericTypes)) {
            auto errMsg = "generic type " + rtti->GetRTTIType()->ToString() +
                " is unreachable, the type is rtti type in expression " + expr.ToString() + ".";
            ErrorInFunc(*expr.GetTopLevelFunc(), errMsg);
        }
    } else if (auto instanceOf = DynamicCast<const InstanceOf*>(&expr)) {
        // 6. generic type in InstanceOf must be reachable
        if (!GenericTypeIsInContainer(*instanceOf->GetType(), reachableGenericTypes)) {
            auto errMsg = "generic type " + instanceOf->GetType()->ToString() +
                " is unreachable, the type is instanceOf type in expression " + expr.ToString() + ".";
            ErrorInFunc(*expr.GetTopLevelFunc(), errMsg);
        }
    } else if (eKind == ExprKind::RAW_ARRAY_ALLOCATE || eKind == ExprKind::RAW_ARRAY_ALLOCATE_WITH_EXCEPTION) {
        auto base = RawArrayAllocateBase(&expr);
        // 7. generic type in RawArrayAllocate must be reachable
        if (!GenericTypeIsInContainer(*base.GetElementType(), reachableGenericTypes)) {
            auto errMsg = "generic type " + base.GetElementType()->ToString() +
                " is unreachable, the type is element type in expression " + expr.ToString() + ".";
            ErrorInFunc(*expr.GetTopLevelFunc(), errMsg);
        }
    } else if (eKind == ExprKind::INTRINSIC || eKind == ExprKind::INTRINSIC_WITH_EXCEPTION) {
        auto base = IntrinsicBase(&expr);
        // 8. generic type in Intrinsic must be reachable
        auto instantiatedTypeArgs = base.GetInstantiatedTypeArgs();
        for (size_t i = 0; i < instantiatedTypeArgs.size(); ++i) {
            if (!GenericTypeIsInContainer(*instantiatedTypeArgs[i], reachableGenericTypes)) {
                auto errMsg = "generic type " + instantiatedTypeArgs[i]->ToString() + "is unreachable, the type is " +
                    std::to_string(i) + "-th instantiated type args in expression " + expr.ToString() + ".";
                ErrorInFunc(*expr.GetTopLevelFunc(), errMsg);
            }
        }
    }
}

void CHIRChecker::CheckBlockGroup(const BlockGroup& blockGroup, const Func& topLevelFunc)
{
    if (optionalRules.find(Rule::CHECK_FUNC_BODY) == optionalRules.end()) {
        return;
    }

    // 1. block group's identifier can't be empty
    if (blockGroup.GetIdentifier().empty()) {
        ErrorInFunc(topLevelFunc, "there is block group without identifier.");
        return;
    }

    // 2. block group's top-level function must be correct
    CheckTopLevelFunc(blockGroup.GetTopLevelFunc(), topLevelFunc, "block group", blockGroup.GetIdentifier());

    // 3. there must be entry block in block group
    if (blockGroup.GetEntryBlock() == nullptr) {
        ErrorInFunc(topLevelFunc, "there is no entry block in block group " + blockGroup.GetIdentifier() + ".");
    }

    // 4. owner func and owner expression, there can only be one
    auto ownerFunc = blockGroup.GetOwnerFunc();
    auto ownerExpr = blockGroup.GetOwnerExpression();
    if (ownerFunc == nullptr && ownerExpr == nullptr) {
        ErrorInFunc(topLevelFunc,
            "we need to set owner func or owner expression for block group " + blockGroup.GetIdentifier() + ".");
    } else if (ownerFunc != nullptr && ownerExpr != nullptr) {
        ErrorInFunc(topLevelFunc, "we can only set owner func or owner expression, not both for block group " +
            blockGroup.GetIdentifier() + ".");
    }

    // 5. there must be blocks in block group
    auto blocks = blockGroup.GetBlocks();
    if (blocks.empty()) {
        ErrorInFunc(topLevelFunc, "there is no block in block group " + blockGroup.GetIdentifier() + ".");
    }

    // 6. block's id can not be empty and not be same
    std::unordered_set<std::string> ids;
    for (size_t i = 0; i < blocks.size(); ++i) {
        auto id = blocks[i]->GetIdentifier();
        if (id.empty()) {
            ErrorInFunc(topLevelFunc, "the " + IndexToString(i) + " block's id is empty.");
            continue;
        }
        auto res = ids.emplace(id).second;
        if (!res) {
            ErrorInFunc(topLevelFunc, "block's id " + id + " is duplicated.");
        }
    }

    // 7. check every block
    for (auto block : blocks) {
        CheckBlock(*block, topLevelFunc);
    }
}

void CHIRChecker::CheckTopLevelFunc(
    const Func* calculatedFunc, const Func& realFunc, const std::string& valueName, const std::string& valueId)
{
    if (calculatedFunc == nullptr) {
        ErrorInFunc(realFunc, "can't get top-level function from " + valueName + " " + valueId + ".");
    } else if (calculatedFunc != &realFunc) {
        auto errMsg = "get a wrong top-level function from " + valueName + " " + valueId + ".\n";
        auto hint = "        wrong func name is " + calculatedFunc->GetIdentifier() + ".";
        ErrorInFunc(realFunc, errMsg + hint);
    }
}

void CHIRChecker::CheckBlock(const Block& block, const Func& topLevelFunc)
{
    // 1. block's identifier can't be empty
    if (block.GetIdentifier().empty()) {
        ErrorInFunc(topLevelFunc, "there is block without identifier.");
        return;
    }

    // 2. a block must have parent block group
    if (block.GetParentBlockGroup() == nullptr) {
        ErrorInFunc(topLevelFunc, "block " + block.GetIdentifier() + "'s parent block group is null.");
    }

    // 3. block's top-level func must be correct
    CheckTopLevelFunc(block.GetTopLevelFunc(), topLevelFunc, "block", block.GetIdentifier());

    // 4. expressions in block can't be empty
    auto exprs = block.GetExpressions();
    if (optionalRules.find(Rule::EMPTY_BLOCK) != optionalRules.end() && exprs.size() == 0) {
        ErrorInFunc(topLevelFunc, "there is no expression in block " + block.GetIdentifier() + ".");
    }

    if (!exprs.empty()) {
        // 5. terminator can't appear in the middle of expressions
        for (size_t i = 0; i + 1 < exprs.size(); i++) {
            auto expr = exprs[i];
            if (expr->IsTerminator()) {
                ErrorInFunc(topLevelFunc, "terminator found in the middle of block " + block.GetIdentifier() + ".");
                break;
            }
        }
        // 6. the last expression must be terminator
        if (!exprs.back()->IsTerminator()) {
            ErrorInFunc(topLevelFunc,
                "the last expression in block " + block.GetIdentifier() + " is not terminator.");
        } else {
            // 7. check terminator's jump
            CheckTerminatorJump(*StaticCast<Terminator*>(exprs.back()), topLevelFunc);
        }
    }

    // 8. check predecessors
    CheckPredecessors(block, topLevelFunc);

    // 9. check every expression
    for (auto e : exprs) {
        CheckExpression(*e, topLevelFunc);
    }
}

void CHIRChecker::CheckTerminatorJump(const Terminator& terminator, const Func& topLevelFunc)
{
    // 1. terminator can't jump to another block group
    auto curBlockGroup = terminator.GetParentBlock()->GetParentBlockGroup();
    for (auto suc : terminator.GetSuccessors()) {
        if (suc->GetParentBlockGroup() != curBlockGroup) {
            ErrorInFunc(topLevelFunc, "terminator " + terminator.ToString() + " in block group " +
                curBlockGroup->GetIdentifier() + " jumps to an unreachable block " + suc->GetIdentifier() +
                " in block group " + suc->GetParentBlockGroup()->GetIdentifier());
        }
    }
}

void CHIRChecker::CheckPredecessors(const Block& block, const Func& topLevelFunc)
{
    // 1. the successor of current block's predecessor must be current block
    for (auto b : block.GetPredecessors()) {
        auto successors = b->GetTerminator()->GetSuccessors();
        if (std::find(successors.begin(), successors.end(), &block) == successors.end()) {
            ErrorInFunc(topLevelFunc, "block " + block.GetIdentifier() + "'s predecessor is " + b->GetIdentifier() +
                ", but block " + b->GetIdentifier() + "'s successor is not "  + block.GetIdentifier() + ".");
        }
    }
}

void CHIRChecker::CheckExpression(const Expression& expr, const Func& topLevelFunc)
{
    const std::unordered_map<ExprMajorKind, std::function<void()>> actionMap = {
        {ExprMajorKind::TERMINATOR, [this, &expr, &topLevelFunc]() { CheckTerminator(expr, topLevelFunc); }},
        {ExprMajorKind::UNARY_EXPR, [this, &expr, &topLevelFunc]()
            { CheckUnaryExpression(StaticCast<const UnaryExpression&>(expr), topLevelFunc); }},
        {ExprMajorKind::BINARY_EXPR, [this, &expr, &topLevelFunc]()
            { CheckBinaryExpression(StaticCast<const BinaryExpression&>(expr), topLevelFunc); }},
        {ExprMajorKind::MEMORY_EXPR, [this, &expr, &topLevelFunc]() { CheckMemoryExpression(expr, topLevelFunc); }},
        {ExprMajorKind::STRUCTURED_CTRL_FLOW_EXPR,
            [this, &expr, &topLevelFunc]() { CheckControlFlowExpression(expr, topLevelFunc); }},
        {ExprMajorKind::OTHERS, [this, &expr, &topLevelFunc]() { CheckOtherExpression(expr, topLevelFunc); }},
    };
    // 1. expression must have parent block
    if (expr.GetParentBlock() == nullptr) {
        ErrorInFunc(topLevelFunc, "expression " + expr.ToString() + " doesn't have parent block.");
        return;
    }
    // 2. non-terminator expression must have result
    if (!expr.IsTerminator() && !CheckHaveResult(expr, topLevelFunc)) {
        return;
    }
    if (auto it = actionMap.find(expr.GetExprMajorKind()); it != actionMap.end()) {
        it->second();
    }
}

void CHIRChecker::CheckTerminator(const Expression& expr, const Func& topLevelFunc)
{
    const std::unordered_map<ExprKind, std::function<void()>> actionMap = {
        {ExprKind::GOTO, [this, &expr, &topLevelFunc]() {
            CheckGoTo(StaticCast<const GoTo&>(expr), topLevelFunc); }},
        {ExprKind::EXIT, [this, &expr, &topLevelFunc]() {
            CheckExit(StaticCast<const Exit&>(expr), topLevelFunc); }},
        {ExprKind::RAISE_EXCEPTION, [this, &expr, &topLevelFunc]() {
            CheckRaiseException(StaticCast<const RaiseException&>(expr), topLevelFunc); }},
        {ExprKind::BRANCH, [this, &expr, &topLevelFunc]() {
            CheckBranch(StaticCast<const Branch&>(expr), topLevelFunc); }},
        {ExprKind::MULTIBRANCH, [this, &expr, &topLevelFunc]() {
            CheckMultiBranch(StaticCast<const MultiBranch&>(expr), topLevelFunc); }},
        {ExprKind::APPLY_WITH_EXCEPTION, [this, &expr, &topLevelFunc]() {
            CheckApplyWithException(StaticCast<const ApplyWithException&>(expr), topLevelFunc); }},
        {ExprKind::INVOKE_WITH_EXCEPTION, [this, &expr, &topLevelFunc]() {
            CheckInvokeWithException(StaticCast<const InvokeWithException&>(expr), topLevelFunc); }},
        {ExprKind::INVOKESTATIC_WITH_EXCEPTION, [this, &expr, &topLevelFunc]() {
            CheckInvokeStaticWithException(StaticCast<const InvokeStaticWithException&>(expr), topLevelFunc); }},
        {ExprKind::INT_OP_WITH_EXCEPTION, [this, &expr, &topLevelFunc]() {
            CheckIntOpWithException(StaticCast<const IntOpWithException&>(expr), topLevelFunc); }},
        {ExprKind::SPAWN_WITH_EXCEPTION, [this, &expr, &topLevelFunc]() {
            CheckSpawnWithException(StaticCast<const SpawnWithException&>(expr), topLevelFunc); }},
        {ExprKind::TYPECAST_WITH_EXCEPTION, [this, &expr, &topLevelFunc]() {
            CheckTypeCastWithException(StaticCast<const TypeCastWithException&>(expr), topLevelFunc); }},
        {ExprKind::INTRINSIC_WITH_EXCEPTION, [this, &expr, &topLevelFunc]() {
            CheckIntrinsicWithException(StaticCast<const IntrinsicWithException&>(expr), topLevelFunc); }},
        {ExprKind::ALLOCATE_WITH_EXCEPTION, [this, &expr, &topLevelFunc]() {
            CheckAllocateWithException(StaticCast<const AllocateWithException&>(expr), topLevelFunc); }},
        {ExprKind::RAW_ARRAY_ALLOCATE_WITH_EXCEPTION, [this, &expr, &topLevelFunc]() {
            CheckRawArrayAllocateWithException(
                StaticCast<const RawArrayAllocateWithException&>(expr), topLevelFunc); }},
    };
    if (auto it = actionMap.find(expr.GetExprKind()); it != actionMap.end()) {
        it->second();
    } else {
        WarningInExpr(topLevelFunc, expr, "find unrecongnized ExprKind `" + expr.GetExprKindName() + ".");
    }
}

void CHIRChecker::CheckGoTo(const GoTo& expr, const Func& topLevelFunc)
{
    // 1. don't have operand
    OperandNumIsEqual(0, expr, topLevelFunc);

    // 2. only have 1 successor
    SuccessorNumIsEqual(1, expr, topLevelFunc);

    // 3. don't have result
    ShouldNotHaveResult(expr, topLevelFunc);
}

void CHIRChecker::CheckExit(const Exit& expr, const Func& topLevelFunc)
{
    // 1. don't have operand
    OperandNumIsEqual(0, expr, topLevelFunc);

    // 2. don't have successor
    SuccessorNumIsEqual(0, expr, topLevelFunc);

    // 3. don't have result
    ShouldNotHaveResult(expr, topLevelFunc);
}

void CHIRChecker::CheckRaiseException(const RaiseException& expr, const Func& topLevelFunc)
{
    // 1. don't have result
    ShouldNotHaveResult(expr, topLevelFunc);

    // 2. there is 1 operand at least
    if (!OperandNumAtLeast(1, expr, topLevelFunc)) {
        return;
    }
    auto exceptionValue = expr.GetExceptionValue();
    auto exceptionType = exceptionValue->GetType();
    // 3. exception value's type must be class ref or generic type
    if (!exceptionType->IsClassRef() && !exceptionType->IsGeneric()) {
        TypeCheckError(expr, *exceptionValue, "Class& or Generic", topLevelFunc);
        return;
    }
    // 4. exception value's type must be equal or sub type of class Exception or class Error
    auto isEqualOrSubTypeOfClassInCore = [this](ClassType& type, const std::string& className) {
        if (CheckCustomTypeDefIsExpected(*type.GetClassDef(), CORE_PACKAGE_NAME, className)) {
            return true;
        }
        for (auto superType : type.GetSuperTypesRecusively(builder)) {
            if (CheckCustomTypeDefIsExpected(*superType->GetClassDef(), CORE_PACKAGE_NAME, className)) {
                return true;
            }
        }
        return false;
    };
    auto isLegalExceptionValueType = [&isEqualOrSubTypeOfClassInCore](ClassType& type) {
        return isEqualOrSubTypeOfClassInCore(type, CLASS_EXCEPTION) ||
            isEqualOrSubTypeOfClassInCore(type, CLASS_ERROR) ||
            CheckCustomTypeDefIsExpected(*type.GetClassDef(), CORE_PACKAGE_NAME, OBJECT_NAME) ||
            // these three classes are from effect handler plan, we set white list temprorarily
            CheckCustomTypeDefIsExpected(*type.GetClassDef(), EFFECT_PACKAGE_NAME, "ImmediateEarlyReturn") ||
            CheckCustomTypeDefIsExpected(*type.GetClassDef(), EFFECT_PACKAGE_NAME, "ImmediateFrameErrorWrapper") ||
            CheckCustomTypeDefIsExpected(*type.GetClassDef(), EFFECT_PACKAGE_NAME, "ImmediateFrameExceptionWrapper");
    };
    auto derefType = exceptionType->StripAllRefs();
    if (auto classType = DynamicCast<ClassType*>(derefType)) {
        if (!isLegalExceptionValueType(*classType)) {
            auto errMsg = "value " + exceptionValue->GetIdentifier() + " used in " + GetExpressionString(expr) +
                " has type " + exceptionType->ToString() +
                ", but this type isn't class Exception, class Error or their sub type.";
            ErrorInFunc(topLevelFunc, errMsg);
        }
    } else if (auto genericType = DynamicCast<GenericType*>(derefType)) {
        bool ok = false;
        for (auto upperBound : genericType->GetUpperBounds()) {
            if (isLegalExceptionValueType(*StaticCast<ClassType*>(upperBound->StripAllRefs()))) {
                ok = true;
                break;
            }
        }
        if (!ok) {
            auto errMsg = "value " + exceptionValue->GetIdentifier() + " used in " + GetExpressionString(expr) +
                " has type " + exceptionType->ToString() +
                ", but none of its upper bounds is class Exception, class Error or their sub type.";
            ErrorInFunc(topLevelFunc, errMsg);
        }
    }
}

void CHIRChecker::CheckBranch(const Branch& expr, const Func& topLevelFunc)
{
    // 1. don't have result
    ShouldNotHaveResult(expr, topLevelFunc);

    // 2. only have 1 operand
    if (!OperandNumIsEqual(1, expr, topLevelFunc)) {
        return;
    }

    // 3. have 2 successors
    if (!SuccessorNumIsEqual(2, expr, topLevelFunc)) {
        return;
    }

    // 4. condition type must be Bool or Nothing
    auto condType = expr.GetCondition()->GetType();
    if (!condType->IsBoolean() && !condType->IsNothing()) {
        TypeCheckError(expr, *expr.GetCondition(), "Bool", topLevelFunc);
    }
}

void CHIRChecker::CheckMultiBranch(const MultiBranch& expr, const Func& topLevelFunc)
{
    // 1. don't have result
    ShouldNotHaveResult(expr, topLevelFunc);

    // 2. only have 1 operand
    if (!OperandNumIsEqual(1, expr, topLevelFunc)) {
        return;
    }

    // 3. have 2 successors at least, one is default block, others are normal blocks
    if (!SuccessorNumAtLeast(2, expr, topLevelFunc)) {
        return;
    }

    // 4. condition type must be Int
    auto condType = expr.GetCondition()->GetType();
    if (!condType->IsInteger()) {
        TypeCheckError(expr, *expr.GetCondition(), "Int", topLevelFunc);
    }

    // 5. normal blocks' size must equal to cases' size
    if (expr.GetNormalBlocks().size() != expr.GetCaseVals().size()) {
        auto errMsg = "value's size and normal block's size must be equal, but now there are " +
            std::to_string(expr.GetCaseVals().size()) + " value(s) and " +
            std::to_string(expr.GetNormalBlocks().size()) + "normal block(s).";
        ErrorInExpr(topLevelFunc, expr, errMsg);
    }

    // 6. must have default block
    if (expr.GetDefaultBlock() == nullptr) {
        ErrorInExpr(topLevelFunc, expr, "default block shouldn't be nullptr.");
    }
}

void CHIRChecker::CheckApplyWithException(const ApplyWithException& expr, const Func& topLevelFunc)
{
    // 1. have result
    if (!CheckHaveResult(expr, topLevelFunc)) {
        return;
    }

    // 2. there is 1 operand at least, must have callee
    if (!OperandNumAtLeast(1, expr, topLevelFunc)) {
        return;
    }

    // 3. must have 2 successors, normal block and exception block
    if (!SuccessorNumIsEqual(2, expr, topLevelFunc)) {
        return;
    }

    CheckApplyBase(ApplyBase(&expr), topLevelFunc);
}

void CHIRChecker::CheckApplyBase(const ApplyBase& expr, const Func& topLevelFunc)
{
    // 1. if this Apply can't be executed in runtime, we don't need to check
    if (IsUnreachableApply(expr.GetThisType())) {
        return;
    }

    // 2. check callee
    if (!CheckCallee(*expr.GetCallee(), *expr.GetRawExpr(), topLevelFunc)) {
        return;
    }

    // 3. check instantiated type args
    std::vector<GenericType*> genericTypeParams;
    if (auto func = DynamicCast<FuncBase*>(expr.GetCallee())) {
        genericTypeParams = func->GetGenericTypeParams();
    } else if (auto localVar = DynamicCast<LocalVar*>(expr.GetCallee())) {
        if (auto lambda = DynamicCast<Lambda*>(localVar->GetExpr())) {
            genericTypeParams = lambda->GetGenericTypeParams();
        }
    }
    auto instTypeArgs = expr.GetInstantiatedTypeArgs();
    if (!CheckInstantiatedTypeArgs(instTypeArgs, genericTypeParams, *expr.GetRawExpr(), topLevelFunc)) {
        return;
    }

    // 4. check thisType
    if (!CheckApplyThisType(*expr.GetCallee(), expr.GetThisType(), *expr.GetRawExpr(), topLevelFunc)) {
        return;
    }

    // 5. check func args
    auto calleeType = StaticCast<FuncType*>(expr.GetCallee()->GetType());
    auto instFuncType = CalculateInstFuncType(
        *calleeType, instTypeArgs, genericTypeParams, expr.GetInstParentCustomTyOfCallee(builder));
    CheckApplyFuncArgs(
        expr.GetArgs(), instFuncType->GetParamTypes(), calleeType->HasVarArg(), *expr.GetRawExpr(), topLevelFunc);

    // 6. check return value
    CheckApplyFuncRetValue(*instFuncType->GetReturnType(), *expr.GetRawExpr(), topLevelFunc);
}

bool CHIRChecker::CheckCallee(const Value& callee, const Expression& expr, const Func& topLevelFunc)
{
    // 1. callee's type must be func type
    if (!callee.GetType()->IsFunc()) {
        ErrorInFunc(topLevelFunc, "callee of `" + GetExpressionString(expr) +
            "` should have func type, not " + callee.GetType()->ToString() + ".");
        return false;
    }
    return true;
}

bool CHIRChecker::CheckInstantiatedTypeArgs(const std::vector<Type*>& instantiatedTypeArgs,
    const std::vector<GenericType*>& genericTypeParams, const Expression& expr, const Func& topLevelFunc)
{
    // 1. instantiated type args' size must equal to generic type params' size
    if (instantiatedTypeArgs.size() != genericTypeParams.size()) {
        auto errMsg = "size mismatched, there are " + std::to_string(instantiatedTypeArgs.size()) +
            " instantiated type args in function call `" + GetExpressionString(expr) + "`, but there are " +
            std::to_string(genericTypeParams.size()) + " generic type args in function declare.";
        ErrorInFunc(topLevelFunc, errMsg);
        return false;
    }
    return true;
}

bool CHIRChecker::CheckThisTypeIsEqualOrSubTypeOfFuncParentType(
    Type& thisType, const FuncBase& func, const Expression& expr, const Func& topLevelFunc)
{
    CJC_ASSERT(thisType.IsBuiltinType() || thisType.IsCustomType());
    auto funcParentType = func.GetParentCustomTypeOrExtendedType();
    // builtin type can compare type directly
    if (thisType.IsBuiltinType() && funcParentType->IsBuiltinType()) {
        return &thisType == funcParentType;
    }
    auto funcParentCustomType = DynamicCast<CustomType*>(funcParentType);
    if (auto thisCustomType = DynamicCast<const CustomType*>(&thisType);
        thisCustomType && funcParentCustomType &&
        thisCustomType->GetCustomTypeDef() == funcParentCustomType->GetCustomTypeDef()) {
        return true;
    }
    for (auto parentTy : thisType.GetSuperTypesRecusively(builder)) {
        // change to generic def to compare
        if (parentTy->GetClassDef() == funcParentCustomType->GetCustomTypeDef()) {
            return true;
        }
    }
    auto parentDef = func.GetParentCustomTypeDef();
    CJC_NULLPTR_CHECK(parentDef);
    auto errMsg = "callee is " + func.GetIdentifier() + ", its parent custom type def is " +
        parentDef->GetIdentifier() + ", but ThisType is " + thisType.ToString() +
        ", not this parent custom type or its sub type.";
    ErrorInExpr(topLevelFunc, expr, errMsg);
    return false;
}

bool CHIRChecker::CheckApplyThisType(
    const Value& callee, const Type* thisType, const Expression& expr, const Func& topLevelFunc)
{
    Type* thisDerefTy = thisType == nullptr ? nullptr : thisType->StripAllRefs();
    // 1. thisType must be builtin type, custom type, generic type or nullptr
    /**
     *  generic type is a little weird, but the following code is allowed in Cangjie:
     *  class A {
     *      static public func foo() {}
     *  }
     *  func goo<T>() where T <: A {
     *      T.foo()  // will be translated to `Apply`
     *  }
     *  generic type T's constraint is a non-open class `A`, that means `T` can only be `A`
     *  so we can translate `T.foo` to `Apply`, not `Invoke`, for better runtime performance
     */
    if (thisDerefTy != nullptr &&
        !thisDerefTy->IsBuiltinType() && !thisDerefTy->IsCustomType() && !thisDerefTy->IsGeneric()) {
        auto errMsg =
            "ThisType must be builtin type, custom type or generic type can't be " + thisType->ToString() + ".";
        ErrorInExpr(topLevelFunc, expr, errMsg);
        return false;
    }
    auto func = DynamicCast<const FuncBase*>(&callee);
    // 2. thisType must be nullptr if callee Parameter or LocalVar
    if (func == nullptr && thisDerefTy != nullptr) {
        auto errMsg = "callee isn't member method, but there is ThisType: " + thisType->ToString() + ".";
        ErrorInExpr(topLevelFunc, expr, errMsg);
        return false;
    }
    // 3. if callee is Paramter or LocalVar, just return
    if (func == nullptr) {
        return true;
    }
    // 4. thisType can't be nullptr if callee is member method
    if (func->IsMemberFunc() && thisDerefTy == nullptr) {
        auto errMsg = "callee is member method, but there is no ThisType.";
        ErrorInExpr(topLevelFunc, expr, errMsg);
        return false;
    }
    // 5. thisType must be nullptr if callee isn't member method
    if (!func->IsMemberFunc() && thisDerefTy != nullptr) {
        auto errMsg = "callee isn't member method, but there is ThisType: " + thisType->ToString() + ".";
        ErrorInExpr(topLevelFunc, expr, errMsg);
        return false;
    }
    if (!func->IsMemberFunc() && thisDerefTy == nullptr) {
        return true;
    }
    
    // 6. thisType must be equal or sub type of callee's parent type
    if (thisDerefTy->IsBuiltinType() || thisDerefTy->IsCustomType()) {
        return CheckThisTypeIsEqualOrSubTypeOfFuncParentType(*thisDerefTy, *func, expr, topLevelFunc);
    } else if (auto gType = DynamicCast<GenericType*>(thisDerefTy)) {
        // 7. we will know if this func can be dynamic dispatched by judging func and upper bound
        if (!FuncCanBeDynamicDispatch(*func)) {
            return true;
        }
        for (auto upperBound : gType->GetUpperBounds()) {
            if (!StaticCast<CustomType*>(upperBound->StripAllRefs())->GetCustomTypeDef()->CanBeInherited()) {
                return true;
            }
        }
        auto errMsg = "ThisType is " + thisType->ToString() +
            ", but its all upper bounds can be inherited and callee can be dynamic dispatched, " +
            "so you should use `Invoke`, not `Apply`.";
        ErrorInExpr(topLevelFunc, expr, errMsg);
        return false;
    }
    auto errMsg = "ThisType is " + thisType->ToString() + ", an unknown type.";
    ErrorInExpr(topLevelFunc, expr, errMsg);
    return false;
}

FuncType* CHIRChecker::CalculateInstFuncType(
    FuncType& originalFuncType, const std::vector<Type*>& instantiatedTypeArgs,
    const std::vector<GenericType*>& genericTypeParams, Type* instOuterType)
{
    std::unordered_map<const GenericType*, Type*> replaceTable;
    if (auto customType = DynamicCast<CustomType*>(instOuterType)) {
        replaceTable = GetInstMapFromCurDefAndExDefToCurType(*customType);
    }
    CJC_ASSERT(genericTypeParams.size() == instantiatedTypeArgs.size());
    for (size_t i = 0; i < genericTypeParams.size(); ++i) {
        replaceTable.emplace(genericTypeParams[i], instantiatedTypeArgs[i]);
    }

    return StaticCast<FuncType*>(ReplaceRawGenericArgType(originalFuncType, replaceTable, builder));
}

void CHIRChecker::CheckApplyFuncArgs(const std::vector<Value*>& args,
    const std::vector<Type*>& instParamTypes, bool varArgs, const Expression& expr, const Func& topLevelFunc)
{
    // 1. don't check variable args's size
    // 2. func args' size must be func params' size
    if (!varArgs && args.size() != instParamTypes.size()) {
        auto errMsg = "size mismatched, there are " + std::to_string(args.size()) +
            " arg(s) in function call `" + GetExpressionString(expr) + "`, but there are " +
            std::to_string(instParamTypes.size()) + " parameter(s) in function declare.";
        ErrorInFunc(topLevelFunc, errMsg);
        return;
    }
    // 3. func arg can set to func param
    for (size_t i = 0; i < instParamTypes.size(); ++i) {
        if (!TypeIsExpected(*args[i]->GetType(), *instParamTypes[i])) {
            TypeCheckError(expr, *args[i], instParamTypes[i]->ToString(), topLevelFunc);
        }
    }
}

void CHIRChecker::CheckApplyFuncRetValue(const Type& instRetType, const Expression& expr, const Func& topLevelFunc)
{
    // 1. check func return value's type, `instRetType` should be src, the second condition is a hack,
    // because some functions' return type is `This`, need to fix
    auto retValue = expr.GetResult();
    CJC_NULLPTR_CHECK(retValue);
    if (!TypeIsExpected(instRetType, *retValue->GetType()) && !TypeIsExpected(*retValue->GetType(), instRetType)) {
        TypeCheckError(expr, *retValue, instRetType.ToString(), topLevelFunc);
    }
}

void CHIRChecker::CheckInvokeWithException(const InvokeWithException& expr, const Func& topLevelFunc)
{
    // 1. have result
    if (!CheckHaveResult(expr, topLevelFunc)) {
        return;
    }

    // 2. there is 1 operand at least, must have object
    if (!OperandNumAtLeast(1, expr, topLevelFunc)) {
        return;
    }

    // 3. must have 2 successors, normal block and exception block
    if (!SuccessorNumIsEqual(2, expr, topLevelFunc)) {
        return;
    }

    CheckInvokeBase(InvokeBase(&expr), topLevelFunc);
}

void CHIRChecker::CheckInvokeBase(const InvokeBase& expr, const Func& topLevelFunc)
{
    // 1. check instantiated type args
    const auto& genericTypeParams = expr.GetGenericTypeParams();
    auto instTypeArgs = expr.GetInstantiatedTypeArgs();
    if (!CheckInstantiatedTypeArgs(instTypeArgs, genericTypeParams, *expr.GetRawExpr(), topLevelFunc)) {
        return;
    }

    // 2. check thisType
    if (!CheckInvokeThisType(*expr.GetObject()->GetType(), expr.GetThisType(), *expr.GetRawExpr(), topLevelFunc)) {
        return;
    }

    // a hack way, skip AutoEnvBase for now, we will fix later
    auto thisType = expr.GetThisType()->StripAllRefs();
    if (!thisType->IsAutoEnvBase()) {
        // 3. check vritual method
        auto virMethodCtx = VirMethodFullContext {
            .srcCodeIdentifier = expr.GetMethodName(),
            .originalFuncType = expr.GetMethodType(),
            .genericTypeParams = expr.GetGenericTypeParams(),
            .offset = expr.GetVirtualMethodOffset(),
            .thisType = thisType,
            .srcParentType = expr.GetInstSrcParentCustomTypeOfMethod(builder)
        };
        if (!CheckVirtualMethod(virMethodCtx, *expr.GetRawExpr(), topLevelFunc)) {
            return;
        }
    }

    // 4. check func args
    auto paramTypes = expr.GetMethodType()->GetParamTypes();
    CheckInvokeFuncArgs(expr.GetArgs(), paramTypes, *expr.GetRawExpr(), topLevelFunc);
}

bool CHIRChecker::CheckInvokeThisType(
    Type& objType, const Type* thisType, const Expression& expr, const Func& topLevelFunc)
{
    // 1. there must be thisType in `Invoke` and `InvokeStatic`
    if (thisType == nullptr) {
        auto errMsg = "we must have `ThisType`, can't be nullptr.";
        ErrorInExpr(topLevelFunc, expr, errMsg);
        return false;
    }
    auto thisDerefTy = thisType->StripAllRefs();
    // 2. thisType must be builtin type, custom type, `This` or generic type
    if (!thisDerefTy->IsBuiltinType() && !thisDerefTy->IsCustomType() && !thisDerefTy->IsThis() &&
        !thisDerefTy->IsGeneric()) {
        auto errMsg =
            "ThisType must be builtin type, custom type, `This` type or generic type, can't be " +
            thisType->ToString() + ".";
        ErrorInExpr(topLevelFunc, expr, errMsg);
        return false;
    }
    if (auto genericType = DynamicCast<GenericType*>(thisDerefTy)) {
        const auto& upperBounds = genericType->GetUpperBounds();
        // 3. thisType must have upper bounds if it's generic type
        if (upperBounds.empty()) {
            auto errMsg =
                "ThisType is " + thisType->ToString() + ", a generic type, but it doesn't have upper bounds.";
            ErrorInExpr(topLevelFunc, expr, errMsg);
            return false;
        }
        auto objDerefType = objType.StripAllRefs();
        bool ok = false;
        if (auto gType = DynamicCast<GenericType*>(objDerefType)) {
            ok = Generic1IsEqualOrSubTypeOfGeneric2(*genericType, *gType, builder);
        } else {
            // 4. object's type must be parent type of one of thisType's upper bounds
            /**
             *  interface I1 {
             *      func foo() {}
             *  }
             *  interface I2 <: I1 {}
             *  interface I3 {}
             *  func goo<T>(a: T) where T <: I2 & I3 {
             *      a.foo() // a's type is `I1`, thisType's upper bounds are {I2, I3}
             *              // a's type only need to be I2's parent type, not be all upper bounds' parent type
             *  }
             */
            for (auto upperBound : upperBounds) {
                if (upperBound->StripAllRefs()->IsEqualOrSubTypeOf(*objDerefType, builder)) {
                    ok = true;
                    break;
                }
            }
        }
        
        if (!ok) {
            auto errMsg = "type mismatched, in `" + GetExpressionString(expr) + "`, ThisType is " +
                thisType->ToString() + ", its upper bounds are " +
                VectorTypesToString("(", genericType->GetUpperBounds(), ")") + ", but object's type is " +
                objType.ToString() + ", any one of ThisType's upper bounds is not equal or sub type of object's type.";
            ErrorInFunc(topLevelFunc, errMsg);
            return false;
        }
    } else if (thisDerefTy->IsThis()) {
        // 5. `This` must be used in expression which is created by member method
        if (!topLevelFunc.IsMemberFunc()) {
            auto errMsg = "use a `This` type in `" + GetExpressionString(expr) +
                "`, this expression is in function `" + topLevelFunc.GetIdentifier() + "` which isn't member method.";
            ErrorInFunc(topLevelFunc, errMsg);
            return false;
        }
        /**
         *  open class A {
         *      open public func foo() {}
         *  }
         *  extend A {
         *      public func goo() {
         *          foo() // this `foo` should be invoked by `This&`
         *      }
         *  }
         * so we need to check `class A` whether can be inherited, not `extend A`,
         * so we should use `GetOuterDeclaredOrExtendedDef`, not `GetParentCustomTypeDef`
         */
        auto parentDef = topLevelFunc.GetOuterDeclaredOrExtendedDef();
        // 6. custom type def must be inheritable
        if (parentDef == nullptr || !parentDef->CanBeInherited()) {
            auto errMsg = "use a `This` type in `" + GetExpressionString(expr) +
                "`, this expression is in function `" + topLevelFunc.GetIdentifier() +
                "` which is member method of `" + topLevelFunc.GetParentCustomTypeDef()->GetIdentifier() +
                "`, but this type can't be inherited.";
            ErrorInFunc(topLevelFunc, errMsg);
            return false;
        }
    } else if (!thisDerefTy->IsEqualOrSubTypeOf(*objType.StripAllRefs(), builder)) {
        // 7. thisType is custom type or builtin type now, so object's type must be thisType's parent type
        auto errMsg = "type mismatched, in `" + GetExpressionString(expr) +
            "`, ThisType is " + thisType->ToString() + ", but object's type is " + objType.ToString() +
            ", ThisType is not equal or sub type of object's type.";
        ErrorInFunc(topLevelFunc, errMsg);
        return false;
    }
    
    return true;
}

bool CHIRChecker::CheckVirtualMethod(
    const VirMethodFullContext& methodCtx, const Expression& expr, const Func& topLevelFunc)
{
    // 1. check vtable must exist
    auto vtablePtr = CheckVTableExist(*methodCtx.thisType, *methodCtx.srcParentType, expr, topLevelFunc);
    if (vtablePtr == nullptr) {
        return false;
    }
    // 2. offset can't be out of bounds
    if (methodCtx.offset >= (*vtablePtr).size()) {
        auto errMsg = "invoke a wrong virtual method in " + GetExpressionString(expr) +
            ", parent type " + methodCtx.srcParentType->ToString() + " in vtable of " +
            methodCtx.thisType->ToString() + ", only has " + std::to_string((*vtablePtr).size()) +
            " virtual method(s), but the offset is " + std::to_string(methodCtx.offset) + ".";
        ErrorInFunc(topLevelFunc, errMsg);
        return false;
    }
    const auto& funcInfo = (*vtablePtr)[methodCtx.offset];
    auto errMsgBase = "invoke a wrong virtual method in " + GetExpressionString(expr) +
        ", in vtable of [" + methodCtx.thisType->ToString() + "][" + methodCtx.srcParentType->ToString() + "], the " +
        std::to_string(methodCtx.offset) + "-th ";
    // 3. `InvokeStatic` must call a static member method
    // 4. `Invoke` must call a non-static member method
    if (expr.IsInvokeStaticBase() && !funcInfo.TestAttr(Attribute::STATIC)) {
        auto errMsg = errMsgBase + "virtual method is not static, `InvokeStatic` should call a static method.";
        ErrorInFunc(topLevelFunc, errMsg);
        return false;
    } else if (!expr.IsInvokeStaticBase() && funcInfo.TestAttr(Attribute::STATIC)) {
        auto errMsg = errMsgBase + "virtual method is static, `Invoke` shouldn't call a static method.";
        ErrorInFunc(topLevelFunc, errMsg);
        return false;
    }
    bool result = true;
    // 5. src code identifer must be same
    if (funcInfo.GetMethodName() != methodCtx.srcCodeIdentifier) {
        auto errMsg = errMsgBase + "virtual method's name is " + funcInfo.GetMethodName() +
            ", but the method name in expression is " + methodCtx.srcCodeIdentifier + ".";
        ErrorInFunc(topLevelFunc, errMsg);
        result = false;
    }
    return result;
}

void CHIRChecker::CheckInvokeFuncArgs(const std::vector<Value*>& args,
    const std::vector<Type*>& originalParamTypes, const Expression& expr, const Func& topLevelFunc)
{
    if (args.size() != originalParamTypes.size()) {
        auto errMsg = "size mismatched, there are " + std::to_string(args.size()) +
            " arg(s) in function call `" + GetExpressionString(expr) + "`, but there are " +
            std::to_string(originalParamTypes.size()) + " parameter(s) in function declare.";
        ErrorInFunc(topLevelFunc, errMsg);
        return;
    }
    for (size_t i = 0; i < originalParamTypes.size(); ++i) {
        // a hack way, skip to check 1st param type, will check it later
        if (i == 0) {
            continue;
        }
        if (!InstTypeCanSetToGenericRelatedType(*args[i]->GetType(), *originalParamTypes[i])) {
            TypeCheckError(expr, *args[i], originalParamTypes[i]->ToString(), topLevelFunc);
        }
    }
}

bool CHIRChecker::InstTypeCanSetToGenericRelatedType(Type& instType, const Type& genericRelatedType)
{
    if (instType.IsNothing() || genericRelatedType.IsAny() || &instType == &genericRelatedType) {
        return true;
    }
    if (instType.IsRef() && genericRelatedType.IsRef()) {
        auto instBaseType = Cangjie::StaticCast<const RefType&>(instType).GetBaseType();
        auto genericBaseType = Cangjie::StaticCast<const RefType&>(genericRelatedType).GetBaseType();
        return InstTypeCanSetToGenericRelatedType(*instBaseType, *genericBaseType);
    }
    auto instCustomType = Cangjie::DynamicCast<const CustomType*>(&instType);
    auto genericCustomType = Cangjie::DynamicCast<const CustomType*>(&genericRelatedType);
    if (instCustomType != nullptr && genericCustomType != nullptr) {
        // 1. `Class-A<Int64>` can be set to `Class-A<T>`, only if `Int64` satisfy Generic-T's constraints
        if (instCustomType->GetCustomTypeDef() == genericCustomType->GetCustomTypeDef()) {
            return InstTypeArgsSatisfyGenericConstraints(
                instCustomType->GetGenericArgs(), genericCustomType->GetGenericArgs());
        } else {
            // 2. `Class-A` can be set to `Class-I<T>`, only if Class-A's parent type satisfy Class-I<T>'s constraints
            /**
             *  interface I1 {}
             *  interface I2 {}
             *  interface I3<T> where T <: I1 {
             *      func foo(this: I3<T>) {}
             *  }
             *  open class A <: I3<I1> {}
             *  open class B <: I3<I2> {}
             *  var x: A = xxx
             *  x.foo() // Class-A's parent type is `Class-I3<I1>`, satisfy Class-I3<T>'s constraints
             *  var y: B = xxx
             *  y.foo() // Class-B's parent type is `Class-I3<I2>`, doesn't satisfy Class-I3<T>'s constraints
             *          // this case failed to pass Sema check, but you can write ir by CHIR directly
             */
            auto superTypes =
                Cangjie::StaticCast<CustomType*>(instCustomType->StripAllRefs())->GetSuperTypesRecusively(builder);
            for (auto superType : superTypes) {
                if (superType->GetCustomTypeDef() == genericCustomType->GetCustomTypeDef() &&
                    InstTypeArgsSatisfyGenericConstraints(
                        superType->GetGenericArgs(), genericCustomType->GetGenericArgs())) {
                    return true;
                }
            }
            return false;
        }
    }
    if (auto genericType = Cangjie::DynamicCast<const GenericType*>(&genericRelatedType)) {
        if (auto gType = Cangjie::DynamicCast<GenericType*>(&instType)) {
            // 3. `Generic-U` can set to `Generic-T`, only if Generic-U's upper bounds satisfy Generic-T's constraints
            return Generic1IsEqualOrSubTypeOfGeneric2(*gType, *genericType, builder);
        } else {
            // 4. `Class-A` can set to `Generic-T`, only if `Class-A` can satisfy Generic-T's constraints
            std::unordered_map<const GenericType*, Type*> emptyMap;
            return instType.SatisfyGenericConstraints(*genericType, builder, emptyMap);
        }
    }
    if (instType.GetTypeKind() != genericRelatedType.GetTypeKind()) {
        return false;
    }
    auto instTypeArgs = instType.GetTypeArgs();
    auto genericTypeArgs = genericRelatedType.GetTypeArgs();
    if (instTypeArgs.size() != genericTypeArgs.size()) {
        return false;
    }
    for (size_t i = 0; i < instTypeArgs.size(); ++i) {
        if (!InstTypeCanSetToGenericRelatedType(*instTypeArgs[i], *genericTypeArgs[i])) {
            return false;
        }
    }
    return true;
}

bool CHIRChecker::InstTypeArgsSatisfyGenericConstraints(
    const std::vector<Type*>& instTypeArgs, const std::vector<Type*>& genericTypeArgs)
{
    if (instTypeArgs.size() != genericTypeArgs.size()) {
        return false;
    }
    for (size_t i = 0; i < instTypeArgs.size(); ++i) {
        if (!InstTypeCanSetToGenericRelatedType(*instTypeArgs[i], *genericTypeArgs[i])) {
            return false;
        }
    }
    return true;
}

const std::vector<VirtualMethodInfo>* CHIRChecker::CheckVTableExist(
    const BuiltinType& thisType, const ClassType& srcParentType)
{
    for (auto extendDef : thisType.GetExtends(&builder)) {
        auto [res, replaceTable] = extendDef->GetExtendedType()->CalculateGenericTyMapping(thisType);
        CJC_ASSERT(res);
        for (const auto& vtableIt : extendDef->GetDefVTable().GetTypeVTables()) {
            auto instType = ReplaceRawGenericArgType(*vtableIt.GetSrcParentType(), replaceTable, builder);
            if (instType == &srcParentType) {
                return &vtableIt.GetVirtualMethods();
            }
        }
    }
    return nullptr;
}

const std::vector<VirtualMethodInfo>* CHIRChecker::CheckVTableExist(
    const CustomType& thisType, const ClassType& srcParentType)
{
    auto replaceTable = GetInstMapFromCurDefToCurType(thisType);
    for (const auto& vtableIt : thisType.GetCustomTypeDef()->GetDefVTable().GetTypeVTables()) {
        auto instType = ReplaceRawGenericArgType(*vtableIt.GetSrcParentType(), replaceTable, builder);
        if (instType == &srcParentType) {
            return &vtableIt.GetVirtualMethods();
        }
    }
    for (auto extendDef : thisType.GetCustomTypeDef()->GetExtends()) {
        // maybe we can meet `extend<T> A<B<T>> {}`, and `curType` is A<Int32>, then ignore this def,
        // so not need to check `res`
        auto [res, replaceTable2] = extendDef->GetExtendedType()->CalculateGenericTyMapping(thisType);
        for (const auto& vtableIt : extendDef->GetDefVTable().GetTypeVTables()) {
            auto instType = ReplaceRawGenericArgType(*vtableIt.GetSrcParentType(), replaceTable2, builder);
            if (instType == &srcParentType) {
                return &vtableIt.GetVirtualMethods();
            }
        }
    }
    return nullptr;
}

const std::vector<VirtualMethodInfo>* CHIRChecker::CheckVTableExist(const ClassType& srcParentType, const Func& topLevelFunc)
{
    auto parentType = topLevelFunc.GetParentCustomTypeOrExtendedType();
    CJC_NULLPTR_CHECK(parentType);
    if (auto builtinType = DynamicCast<BuiltinType*>(parentType)) {
        for (auto def : GetBuiltinTypeWithVTable(*builtinType, builder)->GetExtends()) {
            for (const auto& vtableIt : def->GetDefVTable().GetTypeVTables()) {
                if (vtableIt.GetSrcParentType() == &srcParentType) {
                    return &vtableIt.GetVirtualMethods();
                }
            }
        }
    } else {
        auto customType = StaticCast<CustomType*>(parentType);
        auto customDef = customType->GetCustomTypeDef();
        auto [res, replaceTable] = customDef->GetType()->CalculateGenericTyMapping(*customType);
        CJC_ASSERT(res);
        for (const auto& vtableIt : customDef->GetDefVTable().GetTypeVTables()) {
            if (ReplaceRawGenericArgType(*vtableIt.GetSrcParentType(), replaceTable, builder) == &srcParentType) {
                return &vtableIt.GetVirtualMethods();
            }
        }
        for (auto extendDef : customDef->GetExtends()) {
            auto [res2, replaceTable2] = extendDef->GetType()->CalculateGenericTyMapping(*customType);
            if (!res2) {
                continue;
            }
            for (const auto& vtableIt : extendDef->GetDefVTable().GetTypeVTables()) {
                if (ReplaceRawGenericArgType(*vtableIt.GetSrcParentType(), replaceTable2, builder) == &srcParentType) {
                    return &vtableIt.GetVirtualMethods();
                }
            }
        }
    }
    return nullptr;
}

const std::vector<VirtualMethodInfo>* CHIRChecker::CheckVTableExist(
    const GenericType& thisType, const ClassType& srcParentType)
{
    for (auto upperBound : thisType.GetUpperBounds()) {
        auto res = CheckVTableExist(*StaticCast<ClassType*>(upperBound->StripAllRefs()), srcParentType);
        if (res != nullptr) {
            return res;
        }
    }
    return nullptr;
}

const std::vector<VirtualMethodInfo>* CHIRChecker::CheckVTableExist(
    const Type& thisType, const ClassType& srcParentType, const Expression& expr, const Func& topLevelFunc)
{
    // 1. in thisType's vtable, we must find `srcParentType`
    const std::vector<VirtualMethodInfo>* res = nullptr;
    if (auto bType = DynamicCast<const BuiltinType*>(&thisType)) {
        res = CheckVTableExist(*bType, srcParentType);
    } else if (auto cType = DynamicCast<const CustomType*>(&thisType)) {
        res = CheckVTableExist(*cType, srcParentType);
    } else if (Is<ThisType>(&thisType)) {
        res = CheckVTableExist(srcParentType, topLevelFunc);
    } else if (auto gType = DynamicCast<const GenericType*>(&thisType)) {
        res = CheckVTableExist(*gType, srcParentType);
    } else {
        CJC_ABORT();
    }
    if (res == nullptr) {
        std::string errMsg;
        if (thisType.IsBuiltinType() || thisType.IsCustomType()) {
            errMsg = "invoke a wrong virtual method in `" + GetExpressionString(expr) +
                "`, we can't find super type " + srcParentType.ToString() + " in type " + thisType.ToString() + ".";
        } else if (thisType.IsThis()) {
            auto parentDef = topLevelFunc.GetParentCustomTypeDef();
            CJC_NULLPTR_CHECK(parentDef);
            errMsg = "invoke a wrong virtual method in " + GetExpressionString(expr) +
                ", we can't find super type " + srcParentType.ToString() + " in custom type def " +
                parentDef->GetIdentifier() + ".";
        } else if (thisType.IsGeneric()) {
            errMsg = "invoke a wrong virtual method in " + GetExpressionString(expr) +
                ", we can't find super type " + srcParentType.ToString() + " in all upper bounds of " +
                thisType.ToString() + ".\n";
            auto hint = "        upper bounds are " +
                VectorTypesToString("(", StaticCast<GenericType&>(thisType).GetUpperBounds(), ")") + ".";
            errMsg += hint;
        }
        ErrorInFunc(topLevelFunc, errMsg);
    }
    return res;
}

void CHIRChecker::CheckInvokeStaticWithException(const InvokeStaticWithException& expr, const Func& topLevelFunc)
{
    // 1. have result
    if (!CheckHaveResult(expr, topLevelFunc)) {
        return;
    }

    // 2. there is 1 operand at least, must have rtti
    if (!OperandNumAtLeast(1, expr, topLevelFunc)) {
        return;
    }

    // 3. must have 2 successors, normal block and exception block
    if (!SuccessorNumIsEqual(2, expr, topLevelFunc)) {
        return;
    }

    CheckInvokeStaticBase(InvokeStaticBase(&expr), topLevelFunc);
}

void CHIRChecker::CheckInvokeStaticBase(const InvokeStaticBase& expr, const Func& topLevelFunc)
{
    // 1. check instantiated type args
    const auto& genericTypeParams = expr.GetGenericTypeParams();
    auto instTypeArgs = expr.GetInstantiatedTypeArgs();
    if (!CheckInstantiatedTypeArgs(instTypeArgs, genericTypeParams, *expr.GetRawExpr(), topLevelFunc)) {
        return;
    }

    // 2. check rtti
    auto rtti = DynamicCast<LocalVar*>(expr.GetRTTIValue());
    if (rtti == nullptr) {
        auto errMsg = "its first operand must be from `GetRTTI` or `GetRTTIStatic`.";
        ErrorInExpr(topLevelFunc, *expr.GetRawExpr(), errMsg);
        return;
    }
    Type* objectType = nullptr;
    if (auto getRtti = DynamicCast<GetRTTI*>(rtti->GetExpr())) {
        objectType = getRtti->GetOperand()->GetType();
    } else if (auto getRttiStatic = DynamicCast<GetRTTIStatic*>(rtti->GetExpr())) {
        objectType = getRttiStatic->GetRTTIType();
    } else {
        auto errMsg = "its first operand must be from `GetRTTI` or `GetRTTIStatic`.";
        ErrorInExpr(topLevelFunc, *expr.GetRawExpr(), errMsg);
        return;
    }

    // 3. check thisType
    if (!CheckInvokeThisType(*objectType, expr.GetThisType(), *expr.GetRawExpr(), topLevelFunc)) {
        return;
    }

    // 4. check vritual method
    auto virMethodCtx = VirMethodFullContext {
        .srcCodeIdentifier = expr.GetMethodName(),
        .originalFuncType = expr.GetMethodType(),
        .genericTypeParams = expr.GetGenericTypeParams(),
        .offset = expr.GetVirtualMethodOffset(),
        .thisType = expr.GetThisType()->StripAllRefs(),
        .srcParentType = expr.GetInstSrcParentCustomTypeOfMethod(builder)
    };
    if (!CheckVirtualMethod(virMethodCtx, *expr.GetRawExpr(), topLevelFunc)) {
        return;
    }

    // 5. check func args
    auto paramTypes = expr.GetMethodType()->GetParamTypes();
    CheckInvokeFuncArgs(expr.GetArgs(), paramTypes, *expr.GetRawExpr(), topLevelFunc);
}

void CHIRChecker::CheckIntOpWithException(const IntOpWithException& expr, const Func& topLevelFunc)
{
    // 1. must have result
    if (!CheckHaveResult(expr, topLevelFunc)) {
        return;
    }

    // 2. only have 1 or 2 operands
    if (!OperandNumIsEqual({1, 2}, expr, topLevelFunc)) {
        return;
    }

    // 3. have 2 successors
    if (!SuccessorNumIsEqual(2, expr, topLevelFunc)) {
        return;
    }

    // 4. check all kinds of expressions
    auto exprKind = expr.GetOpKind();
    if (exprKind >= ExprKind::NEG && exprKind <= ExprKind::BITNOT) {
        CheckUnaryExprBase(UnaryExprBase(&expr), topLevelFunc);
    } else if (exprKind >= ExprKind::ADD && exprKind <= ExprKind::OR) {
        CheckBinaryExprBase(BinaryExprBase(&expr), topLevelFunc);
    } else {
        WarningInExpr(topLevelFunc, expr, "find unrecongnized ExprKind " + expr.GetOpKindName() + ".");
    }
}

void CHIRChecker::CheckUnaryExprBase(const UnaryExprBase& expr, const Func& topLevelFunc)
{
    // 1. skip checking if operand type is Nothing
    auto operand = expr.GetOperand();
    auto operandType = operand->GetType();
    if (operandType->IsNothing()) {
        return;
    }

    // 2. check operand type
    if (expr.GetOpKind() == ExprKind::NOT && !operandType->IsBoolean()) {
        // 2.1 must be Bool when it's NOT (!a)
        TypeCheckError(*expr.GetRawExpr(), *operand, "Bool", topLevelFunc);
    } else if (expr.GetOpKind() == ExprKind::BITNOT && !operandType->IsInteger()) {
        // 2.2 must be Int when it's BITNOT (!a)
        TypeCheckError(*expr.GetRawExpr(), *operand, "Int", topLevelFunc);
    } else if (expr.GetOpKind() == ExprKind::NEG && !operandType->IsInteger() && !operandType->IsFloat()) {
        // 2.3 must be Int or Float when it's NEG (-a)
        TypeCheckError(*expr.GetRawExpr(), *operand, "Int or Float", topLevelFunc);
    }

    // 3. result type must equal to operand type
    auto result = expr.GetResult();
    auto resultType = result->GetType();
    if (operandType != resultType) {
        TypeCheckError(*expr.GetRawExpr(), *result, operandType->ToString(), topLevelFunc);
    }

    // 4. check overflow strategy
    if (expr.GetOpKind() == ExprKind::NEG) {
        OverflowStrategyMustBeValid(expr.GetOverflowStrategy(), *expr.GetRawExpr(), topLevelFunc);
    }
}

void CHIRChecker::CheckBinaryExprBase(const BinaryExprBase& expr, const Func& topLevelFunc)
{
    // 1. skip checking if operand type is Nothing
    if (expr.GetLHSOperand()->GetType()->IsNothing() || expr.GetRHSOperand()->GetType()->IsNothing()) {
        return;
    }

    // 2. check all kinds of binary expressions
    auto exprKind = expr.GetOpKind();
    if (exprKind >= ExprKind::ADD && exprKind <= ExprKind::MOD) {
        CheckCalculExpression(expr, topLevelFunc);
    } else if (exprKind == ExprKind::EXP) {
        CheckExponentiationExpression(expr, topLevelFunc);
    } else if (exprKind >= ExprKind::LSHIFT && exprKind <= ExprKind::BITXOR) {
        CheckBitExpression(expr, topLevelFunc);
    } else if (exprKind >= ExprKind::LT && exprKind <= ExprKind::NOTEQUAL) {
        CheckCompareExpression(expr, topLevelFunc);
    } else if (exprKind >= ExprKind::AND && exprKind <= ExprKind::OR) {
        CheckLogicExpression(expr, topLevelFunc);
    } else {
        WarningInExpr(topLevelFunc, *expr.GetRawExpr(), "find unrecongnized ExprKind " + expr.GetExprKindName() + ".");
    }
}

void CHIRChecker::OverflowStrategyMustBeValid(
    const OverflowStrategy& ofs, const Expression& expr, const Func& topLevelFunc)
{
    // we will add `NA` later
    if (ofs == OverflowStrategy::OVERFLOW_STRATEGY_END) {
        ErrorInExpr(topLevelFunc, expr, "overflow strategy is invalid.");
    }
}

void CHIRChecker::CheckCalculExpression(const BinaryExprBase& expr, const Func& topLevelFunc)
{
    auto leftOperand = expr.GetLHSOperand();
    auto rightOperand = expr.GetRHSOperand();
    auto leftOpType = leftOperand->GetType();
    auto rightOpType = rightOperand->GetType();
    auto result = expr.GetResult();
    // 1. check operand type
    if (expr.GetOpKind() == ExprKind::MOD) {
        // 1.1 mod's operands' type must be Int or UInt, can't be Bool or Float
        if (!leftOpType->IsInteger()) {
            TypeCheckError(*expr.GetRawExpr(), *leftOperand, "Int or UInt", topLevelFunc);
        }
        if (!rightOpType->IsInteger()) {
            TypeCheckError(*expr.GetRawExpr(), *rightOperand, "Int or UInt", topLevelFunc);
        }
    } else {
        // 1.2 other operands' type must be numeric type
        if (!leftOpType->IsNumeric()) {
            TypeCheckError(*expr.GetRawExpr(), *leftOperand, "numeric", topLevelFunc);
        }
        if (!rightOpType->IsNumeric()) {
            TypeCheckError(*expr.GetRawExpr(), *rightOperand, "numeric", topLevelFunc);
        }
    }

    // 2. left operand's type, right operand's type and result type must be same
    if (leftOpType != rightOpType) {
        auto errMsg = "left operand and right operand don't have same type in " + result->ToString() + ".";
        ErrorInFunc(topLevelFunc, errMsg);
    } else if (result->GetType() != leftOpType) {
        auto errMsg = "the result value and operand don't have same type in " + result->ToString() + ".";
        ErrorInFunc(topLevelFunc, errMsg);
    }

    // 3. check overflow strategy
    OverflowStrategyMustBeValid(expr.GetOverflowStrategy(), *expr.GetRawExpr(), topLevelFunc);
}

void CHIRChecker::CheckExponentiationExpression(const BinaryExprBase& expr, const Func& topLevelFunc)
{
    auto leftOperand = expr.GetLHSOperand();
    auto rightOperand = expr.GetRHSOperand();
    auto leftOpType = leftOperand->GetType();
    auto rightOpType = rightOperand->GetType();
    // 1. left operand's type must be Int64 or Float64
    if (leftOpType->GetTypeKind() != Type::TypeKind::TYPE_INT64 &&
        leftOpType->GetTypeKind() != Type::TypeKind::TYPE_FLOAT64) {
        TypeCheckError(*expr.GetRawExpr(), *leftOperand, "Int64 or Float64", topLevelFunc);
    }
    auto result = expr.GetResult();
    auto resultType = result->GetType();
    if (leftOpType->GetTypeKind() == Type::TypeKind::TYPE_INT64) {
        // 2. if left operand's type is Int64, right operand's type must be UInt64, and result type must be Int64
        if (rightOpType->GetTypeKind() != Type::TypeKind::TYPE_UINT64) {
            TypeCheckError(*expr.GetRawExpr(), *rightOperand, "UInt64", topLevelFunc);
        }
        if (resultType->GetTypeKind() != Type::TypeKind::TYPE_INT64) {
            TypeCheckError(*expr.GetRawExpr(), *result, "Int64", topLevelFunc);
        }
    } else if (leftOpType->GetTypeKind() == Type::TypeKind::TYPE_FLOAT64) {
        // 3. if left operand's type is Float64, right operand's type must be Int64 or Float64,
        // and result type must be Float64
        if (rightOpType->GetTypeKind() != Type::TypeKind::TYPE_INT64 &&
            rightOpType->GetTypeKind() != Type::TypeKind::TYPE_FLOAT64) {
            TypeCheckError(*expr.GetRawExpr(), *rightOperand, "Int64 or Float64", topLevelFunc);
        }
        if (resultType->GetTypeKind() != Type::TypeKind::TYPE_FLOAT64) {
            TypeCheckError(*expr.GetRawExpr(), *result, "Float64", topLevelFunc);
        }
    }

    // 4. check overflow strategy
    OverflowStrategyMustBeValid(expr.GetOverflowStrategy(), *expr.GetRawExpr(), topLevelFunc);
}

void CHIRChecker::CheckBitExpression(const BinaryExprBase& expr, const Func& topLevelFunc)
{
    auto leftOperand = expr.GetLHSOperand();
    auto rightOperand = expr.GetRHSOperand();
    auto leftOpType = leftOperand->GetType();
    auto rightOpType = rightOperand->GetType();
    // 1. operands' type must be Int
    if (!leftOpType->IsInteger()) {
        TypeCheckError(*expr.GetRawExpr(), *leftOperand, "Int", topLevelFunc);
    }
    if (!rightOpType->IsInteger()) {
        TypeCheckError(*expr.GetRawExpr(), *rightOperand, "Int", topLevelFunc);
    }

    // 2. check overflow strategy
    if (expr.GetOpKind() == ExprKind::LSHIFT || expr.GetOpKind() == ExprKind::RSHIFT) {
        OverflowStrategyMustBeValid(expr.GetOverflowStrategy(), *expr.GetRawExpr(), topLevelFunc);
    }
}

void CHIRChecker::CheckCompareExpression(const BinaryExprBase& expr, const Func& topLevelFunc)
{
    auto leftOperand = expr.GetLHSOperand();
    auto rightOperand = expr.GetRHSOperand();
    auto leftOpType = leftOperand->GetType();
    auto rightOpType = rightOperand->GetType();
    auto result = expr.GetResult();
    // 1. operands' type must be same
    if (leftOpType != rightOpType) {
        auto errMsg = "left operand and right operand don't have same type in " + result->ToString() + ".";
        ErrorInFunc(topLevelFunc, errMsg);
    }

    // 2. result type must be Bool
    if (!result->GetType()->IsBoolean()) {
        TypeCheckError(*expr.GetRawExpr(), *result, "Bool", topLevelFunc);
    }
}

void CHIRChecker::CheckLogicExpression(const BinaryExprBase& expr, const Func& topLevelFunc)
{
    auto leftOperand = expr.GetLHSOperand();
    auto rightOperand = expr.GetRHSOperand();
    auto leftOpType = leftOperand->GetType();
    auto rightOpType = rightOperand->GetType();
    // 1. operands' type must be Bool
    if (!leftOpType->IsBoolean()) {
        TypeCheckError(*expr.GetRawExpr(), *leftOperand, "Bool", topLevelFunc);
    }
    if (!rightOpType->IsBoolean()) {
        TypeCheckError(*expr.GetRawExpr(), *rightOperand, "Bool", topLevelFunc);
    }
    // 2. result type must be Bool
    auto result = expr.GetResult();
    if (!result->GetType()->IsBoolean()) {
        TypeCheckError(*expr.GetRawExpr(), *result, "Bool", topLevelFunc);
    }
}

void CHIRChecker::CheckSpawnWithException(const SpawnWithException& expr, const Func& topLevelFunc)
{
    // 1. must have result
    if (!CheckHaveResult(expr, topLevelFunc)) {
        return;
    }

    // 2. have 1 or 2 operands
    if (!OperandNumIsEqual({1, 2}, expr, topLevelFunc)) {
        return;
    }

    // 3. have 2 successors
    if (!SuccessorNumIsEqual(2, expr, topLevelFunc)) {
        return;
    }

    CheckSpawnBase(SpawnBase(&expr), topLevelFunc);
}

void CHIRChecker::CheckSpawnBase(const SpawnBase& expr, const Func& topLevelFunc)
{
    // 1. the first operand's type must be class&
    auto obj = expr.GetObject();
    auto objType = obj->GetType();
    if (!objType->IsRef() || !StaticCast<RefType>(objType)->GetBaseType()->IsClass()) {
        TypeCheckError(*expr.GetRawExpr(), *obj, "Class&", topLevelFunc);
        return;
    }

    // 2. the first operand's type must be Class-Future& or closure type
    auto def = StaticCast<ClassType*>(objType->StripAllRefs())->GetClassDef();
    if (!IsCoreFuture(*def) && !IsFuncTypeOrClosureBaseRefType(*objType)) {
        auto errMsg = "the first operand's type is `" + objType->ToString() +
            "`, but Class-Future& type or closure type are expected.";
        ErrorInExpr(topLevelFunc, *expr.GetRawExpr(), errMsg);
    }

    // 3. if the first operand's type is closure type, `spawn` must set function `executeClosure`
    if (IsFuncTypeOrClosureBaseRefType(*objType) && !expr.IsExecuteClosure()) {
        auto errMsg = "the first operand's type is closure type but you don't set `executeClosure` function";
        ErrorInExpr(topLevelFunc, *expr.GetRawExpr(), errMsg);
    }
}

void CHIRChecker::CheckTypeCastWithException(
    [[maybe_unused]] const TypeCastWithException& expr, [[maybe_unused]] const Func& topLevelFunc)
{
}

void CHIRChecker::CheckIntrinsicWithException(const IntrinsicWithException& expr, const Func& topLevelFunc)
{
    // 1. must have result
    if (!CheckHaveResult(expr, topLevelFunc)) {
        return;
    }

    // 2. have 2 successors
    if (!SuccessorNumIsEqual(2, expr, topLevelFunc)) {
        return;
    }

    CheckIntrinsicBase(IntrinsicBase(&expr), topLevelFunc);
}

void CHIRChecker::CheckInoutOpSrc(const Value& op, const IntrinsicBase& expr, const Func& topLevelFunc)
{
    auto type = op.GetType()->StripAllRefs();
    if (!IsCTypeInInout(*type)) {
        auto errMsg = "`inout` operand's type is `" + type->ToString() + "`, but C-type (exclude CString) is expected.";
        ErrorInExpr(topLevelFunc, *expr.GetRawExpr(), errMsg);
        return;
    }
    if (op.IsBlock() || op.IsBlockGroup() || op.IsFunc() || op.IsLiteral()) {
        ErrorInExpr(topLevelFunc, *expr.GetRawExpr(), "`inout` operand can't be Block, BlockGroup, Literal or Func.");
        return;
    }
    if (auto localVar = DynamicCast<const LocalVar*>(&op)) {
        auto localExpr = localVar->GetExpr();
        if (auto load = DynamicCast<const Load*>(localExpr)) {
            CheckInoutOpSrc(*load->GetLocation(), expr, topLevelFunc);
        } else if (auto ger = DynamicCast<const GetElementRef*>(localExpr)) {
            auto locationType = ger->GetLocation()->GetType()->StripAllRefs();
            for (auto p : ger->GetPath()) {
                locationType = GetFieldOfType(*locationType, p, builder);
                if (locationType == nullptr) {
                    return;
                }
                if (!IsCTypeInInout(*locationType)) {
                    auto errMsg = "there is " + locationType->ToString() + " type that is calculated by path in " +
                        op.ToString() + ", but C-type (exclude CString) is expected in `inout` operand chain.";
                    ErrorInExpr(topLevelFunc, *expr.GetRawExpr(), errMsg);
                    return;
                }
            }
            CheckInoutOpSrc(*ger->GetLocation(), expr, topLevelFunc);
        } else if (auto field = DynamicCast<const Field*>(localExpr)) {
            auto locationType = field->GetBase()->GetType()->StripAllRefs();
            for (auto p : field->GetPath()) {
                locationType = GetFieldOfType(*locationType, p, builder);
                if (locationType == nullptr) {
                    return;
                }
                if (!IsCTypeInInout(*locationType)) {
                    auto errMsg = "there is " + locationType->ToString() + " type that is calculated by path in " +
                        op.ToString() + ", but C-type (exclude CString) is expected in `inout` operand chain.";
                    ErrorInExpr(topLevelFunc, *expr.GetRawExpr(), errMsg);
                    return;
                }
            }
            CheckInoutOpSrc(*field->GetBase(), expr, topLevelFunc);
        } else if (Is<TypeCastWithException>(localExpr) || Is<TypeCast>(localExpr)) {
            CheckInoutOpSrc(*localExpr->GetOperand(0), expr, topLevelFunc);
        } else if (!Is<FuncCallWithException>(localExpr) && !Is<FuncCall>(localExpr) &&
                   !Is<AllocateWithException>(localExpr) && !Is<Allocate>(localExpr)) {
            ErrorInExpr(topLevelFunc, *expr.GetRawExpr(),
                "a wrong expression `" + op.ToString() + "` in `inout` operand chain.");
            return;
        }
    }
    // if operand is global var or parameter, do nothing
}

void CHIRChecker::CheckInout(const IntrinsicBase& expr, const Func& topLevelFunc)
{
    if (expr.GetIntrinsicKind() != IntrinsicKind::INOUT_PARAM) {
        return;
    }

    // 1. can't have type args
    if (!expr.GetInstantiatedTypeArgs().empty()) {
        ErrorInExpr(topLevelFunc, *expr.GetRawExpr(), "`inout` intrinsic can't have type args.");
    }

    // 2. only have 1 operand
    if (!OperandNumIsEqual(1, *expr.GetRawExpr(), topLevelFunc)) {
        return;
    }

    // 3. operand type must be ref, but can't be type&&
    auto operands = expr.GetOperands();
    auto opType = operands[0]->GetType();
    if (!opType->IsRef()) {
        ErrorInExpr(topLevelFunc, *expr.GetRawExpr(),
            "`inout` operand's type is `" + opType->ToString() + "`, but ref type is expected.");
        return;
    }
    auto baseType = StaticCast<RefType*>(opType)->GetBaseType();
    if (baseType->IsRef()) {
        ErrorInExpr(topLevelFunc, *expr.GetRawExpr(),
            "`inout` operand's type is `" + opType->ToString() + "`, but type&& is NOT allowed.");
        return;
    }

    // 4. operand type must be C-type (exclude CString)
    CheckInoutOpSrc(*operands[0], expr, topLevelFunc);

    // 5. result type must be CPointer
    auto resultType = expr.GetResult()->GetType();
    if (!resultType->IsCPointer()) {
        TypeCheckError(*expr.GetRawExpr(), *expr.GetResult(), "CPointer", topLevelFunc);
        return;
    }

    // 6. CPointer<xxx>, xxx must be CType, but can't be CString
    if (!IsCTypeInInout(*resultType)) {
        auto errMsg = "the pointed type of result CPointer is `" +
            StaticCast<CPointerType*>(resultType)->GetElementType()->ToString() +
            "`, but CType (exclude CString) is expected.";
        ErrorInExpr(topLevelFunc, *expr.GetRawExpr(), errMsg);
        return;
    }

    // 7. result must be Func's or Intrinsic/pointerInit1's arg
    for (auto user : expr.GetResult()->GetUsers()) {
        auto errMsgBase = "the result is used in a wrong expression `" + user->ToString() + "`, ";
        if (Is<ApplyWithException>(user) || Is<Apply>(user)) {
            continue;
        } else if (Is<InvokeWithException>(user) || Is<Invoke>(user)) {
            auto funcName = InvokeBase(user).GetMethodName();
            if (funcName != INST_VIRTUAL_FUNC && funcName != GENERIC_VIRTUAL_FUNC) {
                ErrorInExpr(topLevelFunc, *expr.GetRawExpr(),
                    errMsgBase + "the result can't be used as virtual method's argument.");
            }
            continue;
        } else if (Is<IntrinsicWithException>(user) || Is<Intrinsic>(user)) {
            auto userBase = IntrinsicBase(user);
            if (userBase.GetIntrinsicKind() != IntrinsicKind::CPOINTER_INIT1) {
                ErrorInExpr(topLevelFunc, *expr.GetRawExpr(),
                    errMsgBase + "the result must be used as pointerInit1's argument.");
            }
            continue;
        }
        ErrorInExpr(topLevelFunc, *expr.GetRawExpr(),
            errMsgBase + "the result must be used as Func's or Intrinsic/pointerInit1's argument.");
    }
}
void CHIRChecker::CheckIntrinsicBase(const IntrinsicBase& expr, const Func& topLevelFunc)
{
    if (topLevelFunc.GetIdentifier() == "@_CNbv4mockIG_HRNat5ArrayINNbv8StubModeEE" ||
        topLevelFunc.GetIdentifier() == "@_CNbv3spyIG_HG_" ||
        topLevelFunc.GetIdentifier() == "@_CNbv4mockIG_Hv") {
        return;
    }
    // 1. must have valid intrinsic kind
    if (expr.GetIntrinsicKind() == IntrinsicKind::NOT_INTRINSIC ||
        expr.GetIntrinsicKind() == IntrinsicKind::NOT_IMPLEMENTED) {
        ErrorInExpr(topLevelFunc, *expr.GetRawExpr(), "intrinsic kind must be valid.");
    }

    // 2. check inout
    CheckInout(expr, topLevelFunc);
}

void CHIRChecker::CheckAllocateWithException(const AllocateWithException& expr, const Func& topLevelFunc)
{
    // 1. must have result
    if (!CheckHaveResult(expr, topLevelFunc)) {
        return;
    }

    // 2. don't have operands
    if (!OperandNumIsEqual(0, expr, topLevelFunc)) {
        return;
    }

    // 3. have 2 successors
    if (!SuccessorNumIsEqual(2, expr, topLevelFunc)) {
        return;
    }

    CheckAllocateBase(AllocateBase(&expr), topLevelFunc);
}

void CHIRChecker::CheckAllocateBase(const AllocateBase& expr, const Func& topLevelFunc)
{
    auto result = expr.GetResult();
    auto resultTy = result->GetType();
    auto allocatedType = expr.GetType();
    // 1. result type must be ref of allocated type
    if (!resultTy->IsRef() || StaticCast<RefType*>(resultTy)->GetBaseType() != allocatedType) {
        TypeCheckError(*expr.GetRawExpr(), *result, allocatedType->ToString() + "&", topLevelFunc);
    }

    // 2. can't allocate `Void` type
    if (allocatedType->IsVoid()) {
        ErrorInFunc(topLevelFunc, "can't allocate `Void` type in expression " + result->ToString());
    }

    // 3. if allocated type is CustomType, then must be a valid type
    CheckTypeIsValid(*allocatedType, "allocated", *expr.GetRawExpr(), topLevelFunc);
}

bool CHIRChecker::CheckTypeIsValid(
    const Type& type, const std::string& typeName, const Expression& expr, const Func& topLevelFunc)
{
    if (auto customType = DynamicCast<const CustomType*>(&type)) {
        auto genericType = customType->GetCustomTypeDef()->GetType();
        auto declaredSize = genericType->GetTypeArgs().size();
        auto callSiteSize = customType->GetTypeArgs().size();
        if (declaredSize != callSiteSize) {
            auto errMsg = "type args' size NOT matched, the " + typeName + " type is `" + customType->ToString() +
                "` which has " + std::to_string(callSiteSize) + " type arg(s), but its original type is `" +
                genericType->ToString() + "` which has " + std::to_string(declaredSize) + " type arg(s).";
            ErrorInExpr(topLevelFunc, expr, errMsg);
            return false;
        }
    }
    return true;
}

void CHIRChecker::CheckRawArrayAllocateWithException(
    const RawArrayAllocateWithException& expr, const Func& topLevelFunc)
{
    // 1. must have result
    if (!CheckHaveResult(expr, topLevelFunc)) {
        return;
    }

    // 2. only have 1 operand
    if (!OperandNumIsEqual(1, expr, topLevelFunc)) {
        return;
    }

    // 3. have 2 successors
    if (!SuccessorNumIsEqual(2, expr, topLevelFunc)) {
        return;
    }

    CheckRawArrayAllocateBase(RawArrayAllocateBase(&expr), topLevelFunc);
}

void CHIRChecker::CheckRawArrayAllocateBase(const RawArrayAllocateBase& expr, const Func& topLevelFunc)
{
    // 1. result type must be RawArray&
    auto result = expr.GetResult();
    if (!result->GetType()->IsRef() || !result->GetType()->StripAllRefs()->IsRawArray()) {
        TypeCheckError(*expr.GetRawExpr(), *result, "RawArray&", topLevelFunc);
        return;
    }

    // 2. if element type is CustomType, then must be a valid type
    if (!CheckTypeIsValid(*expr.GetElementType(), "element", *expr.GetRawExpr(), topLevelFunc)) {
        return;
    }

    // 3. result type and element type must be matched
    auto resultTypeArg = StaticCast<RawArrayType*>(result->GetType()->StripAllRefs())->GetElementType();
    if (resultTypeArg != expr.GetElementType()) {
        TypeCheckError(
            *expr.GetRawExpr(), *result, "RawArray<" + expr.GetElementType()->ToString() + ">&", topLevelFunc);
    }

    // 4. size type must be Int64
    auto size = expr.GetSize();
    if (size->GetType()->GetTypeKind() != Type::TypeKind::TYPE_INT64) {
        TypeCheckError(*expr.GetRawExpr(), *size, "Int64", topLevelFunc);
    }
}

void CHIRChecker::CheckUnaryExpression(const UnaryExpression& expr, const Func& topLevelFunc)
{
    if (!OperandNumIsEqual(1, expr, topLevelFunc)) {
        return;
    }
    CheckUnaryExprBase(UnaryExprBase(&expr), topLevelFunc);
}

void CHIRChecker::CheckBinaryExpression(const BinaryExpression& expr, const Func& topLevelFunc)
{
    // 1. must have 2 operands
    if (!OperandNumIsEqual(2, expr, topLevelFunc)) {
        return;
    }
    CheckBinaryExprBase(BinaryExprBase(&expr), topLevelFunc);
}

void CHIRChecker::CheckControlFlowExpression(const Expression& expr, const Func& topLevelFunc)
{
    const std::unordered_map<ExprKind, std::function<void()>> actionMap = {
        {ExprKind::LAMBDA, [this, &expr, &topLevelFunc]() {
            CheckLambda(StaticCast<const Lambda&>(expr), topLevelFunc); }},
    };
    if (auto it = actionMap.find(expr.GetExprKind()); it != actionMap.end()) {
        it->second();
    } else {
        WarningInExpr(topLevelFunc, expr, "find unrecongnized ExprKind `" + expr.GetExprKindName() + ".");
    }
}

void CHIRChecker::CheckLambda(const Lambda& expr, const Func& topLevelFunc)
{
    // 1. identifier can't be empty
    auto result = expr.GetResult();
    if (expr.GetIdentifier().empty()) {
        ErrorInFunc(topLevelFunc, "lambda " + result->GetIdentifier()+ " doesn't have identifier.");
        return;
    }

    // 2. result type must be func type
    if (!result->GetType()->IsFunc()) {
        TypeCheckError(expr, *result, "Func", topLevelFunc);
    }

    // 3. top-level func must be correct
    if (expr.GetTopLevelFunc() != &topLevelFunc) {
        ErrorInExpr(topLevelFunc, expr, "its top-level func is " + expr.GetTopLevelFunc()->GetIdentifier() +
            " by calculated, but it should be " + topLevelFunc.GetIdentifier() + " in fact.");
    }

    // 4. check func type
    auto funcTy = expr.GetFuncType();
    if (!CheckFuncType(funcTy, &expr, topLevelFunc)) {
        return;
    }

    // 5. check func parameters
    CheckFuncParams(expr.GetParams(), *funcTy, expr.GetIdentifier());

    // 6. check func body
    CheckBlockGroup(*expr.GetBody(), topLevelFunc);

    // 7. check return value
    CheckFuncRetValue(expr.GetReturnValue(), *funcTy->GetReturnType(), &expr, topLevelFunc);
}

void CHIRChecker::CheckOtherExpression(const Expression& expr, const Func& topLevelFunc)
{
    const std::unordered_map<ExprKind, std::function<void()>> actionMap = {
        {ExprKind::CONSTANT, [this, &expr, &topLevelFunc]() {
            CheckConstant(StaticCast<const Constant&>(expr), topLevelFunc); }},
        {ExprKind::DEBUGEXPR, [this, &expr, &topLevelFunc]() {
            CheckDebug(StaticCast<const Debug&>(expr), topLevelFunc); }},
        {ExprKind::TUPLE, [this, &expr, &topLevelFunc]() {
            CheckTuple(StaticCast<const Tuple&>(expr), topLevelFunc); }},
        {ExprKind::FIELD, [this, &expr, &topLevelFunc]() {
            CheckField(StaticCast<const Field&>(expr), topLevelFunc); }},
        {ExprKind::FIELD_BY_NAME, [this, &expr, &topLevelFunc]() {
            CheckFieldByName(StaticCast<const FieldByName&>(expr), topLevelFunc); }},
        {ExprKind::APPLY, [this, &expr, &topLevelFunc]() {
            CheckApply(StaticCast<const Apply&>(expr), topLevelFunc); }},
        {ExprKind::INVOKE, [this, &expr, &topLevelFunc]() {
            CheckInvoke(StaticCast<const Invoke&>(expr), topLevelFunc); }},
        {ExprKind::INVOKESTATIC, [this, &expr, &topLevelFunc]() {
            CheckInvokeStatic(StaticCast<const InvokeStatic&>(expr), topLevelFunc); }},
        {ExprKind::INSTANCEOF, [this, &expr, &topLevelFunc]() {
            CheckInstanceOf(StaticCast<const InstanceOf&>(expr), topLevelFunc); }},
        {ExprKind::TYPECAST, [this, &expr, &topLevelFunc]() {
            CheckTypeCast(StaticCast<const TypeCast&>(expr), topLevelFunc); }},
        {ExprKind::GET_EXCEPTION, [this, &expr, &topLevelFunc]() {
            CheckGetException(StaticCast<const GetException&>(expr), topLevelFunc); }},
        {ExprKind::SPAWN, [this, &expr, &topLevelFunc]() {
            CheckSpawn(StaticCast<const Spawn&>(expr), topLevelFunc); }},
        {ExprKind::RAW_ARRAY_ALLOCATE, [this, &expr, &topLevelFunc]() {
            CheckRawArrayAllocate(StaticCast<const RawArrayAllocate&>(expr), topLevelFunc); }},
        {ExprKind::RAW_ARRAY_LITERAL_INIT, [this, &expr, &topLevelFunc]() {
            CheckRawArrayLiteralInit(StaticCast<const RawArrayLiteralInit&>(expr), topLevelFunc); }},
        {ExprKind::RAW_ARRAY_INIT_BY_VALUE, [this, &expr, &topLevelFunc]() {
            CheckRawArrayInitByValue(StaticCast<const RawArrayInitByValue&>(expr), topLevelFunc); }},
        {ExprKind::VARRAY, [this, &expr, &topLevelFunc]() {
            CheckVArray(StaticCast<const VArray&>(expr), topLevelFunc); }},
        {ExprKind::VARRAY_BUILDER, [this, &expr, &topLevelFunc]() {
            CheckVArrayBuilder(StaticCast<const VArrayBuilder&>(expr), topLevelFunc); }},
        {ExprKind::INTRINSIC, [this, &expr, &topLevelFunc]() {
            CheckIntrinsic(StaticCast<const Intrinsic&>(expr), topLevelFunc); }},
        {ExprKind::BOX, [this, &expr, &topLevelFunc]() {
            CheckBox(StaticCast<const Box&>(expr), topLevelFunc); }},
        {ExprKind::UNBOX, [this, &expr, &topLevelFunc]() {
            CheckUnBox(StaticCast<const UnBox&>(expr), topLevelFunc); }},
        {ExprKind::TRANSFORM_TO_GENERIC, [this, &expr, &topLevelFunc]() {
            CheckTransformToGeneric(StaticCast<const TransformToGeneric&>(expr), topLevelFunc); }},
        {ExprKind::TRANSFORM_TO_CONCRETE, [this, &expr, &topLevelFunc]() {
            CheckTransformToConcrete(StaticCast<const TransformToConcrete&>(expr), topLevelFunc); }},
        {ExprKind::GET_INSTANTIATE_VALUE, [this, &expr, &topLevelFunc]() {
            CheckGetInstantiateValue(StaticCast<const GetInstantiateValue&>(expr), topLevelFunc); }},
        {ExprKind::UNBOX_TO_REF, [this, &expr, &topLevelFunc]() {
            CheckUnBoxToRef(StaticCast<const UnBoxToRef&>(expr), topLevelFunc); }},
        {ExprKind::GET_RTTI, [this, &expr, &topLevelFunc]() {
            CheckGetRTTI(StaticCast<const GetRTTI&>(expr), topLevelFunc); }},
        {ExprKind::GET_RTTI_STATIC, [this, &expr, &topLevelFunc]() {
            CheckGetRTTIStatic(StaticCast<const GetRTTIStatic&>(expr), topLevelFunc); }},
    };
    if (auto it = actionMap.find(expr.GetExprKind()); it != actionMap.end()) {
        it->second();
    } else {
        WarningInExpr(topLevelFunc, expr, "find unrecongnized ExprKind `" + expr.GetExprKindName() + ".");
    }
}

void CHIRChecker::CheckConstant(const Constant& expr, const Func& topLevelFunc)
{
    // 1. only have 1 operand
    if (!OperandNumIsEqual(1, expr, topLevelFunc)) {
        return;
    }

    // 2. operand must be literal value
    auto value = expr.GetValue();
    auto litVal = DynamicCast<LiteralValue*>(value);
    if (litVal == nullptr) {
        ErrorInExpr(topLevelFunc, expr, "value must be `LiteralValue`");
        return;
    }

    // we will check it later
    if (litVal->IsNullLiteral()) {
        return;
    }

    // 3. operand type must be Bool, Rune, String, Int, Float or Unit
    auto valueType = value->GetType();
    if (!valueType->IsBoolean() && !valueType->IsRune() && !valueType->IsString() &&
        !valueType->IsInteger() && !valueType->IsFloat() && !valueType->IsUnit()) {
        TypeCheckError(expr, *value, "Bool, Rune, String, Int, Float, Unit", topLevelFunc);
    }

    // 4. result type must equal to operand type
    auto result = expr.GetResult();
    if (result->GetType() != valueType) {
        TypeCheckError(expr, *result, valueType->ToString(), topLevelFunc);
    }
}

void CHIRChecker::CheckDebug(const Debug& expr, const Func& topLevelFunc)
{
    // 1. only have 1 operand
    if (!OperandNumIsEqual(1, expr, topLevelFunc)) {
        return;
    }

    // 2. result type must be `Unit`
    auto result = expr.GetResult();
    if (!result->GetType()->IsUnit()) {
        TypeCheckError(expr, *result, "Unit", topLevelFunc);
    }

    auto value = expr.GetValue();
    if (!value->IsParameter() && opts.enableCompileDebug) {
        // 3. source value's type must be `ReferenceType&&` or `ValueType&`
        if (!CheckTypeMustBeRef(*value->GetType())) {
            auto valueType = value->GetType()->StripAllRefs();
            if (valueType->IsReferenceType()) {
                TypeCheckError(expr, *value, valueType->ToString() + "&&", topLevelFunc);
            } else {
                TypeCheckError(expr, *value, valueType->ToString() + "&", topLevelFunc);
            }
        }
    }
}

void CHIRChecker::CheckTuple(const Tuple& expr, const Func& topLevelFunc)
{
    // 1. there is 1 operand at least
    if (!OperandNumAtLeast(1, expr, topLevelFunc)) {
        return;
    }

    auto result = expr.GetResult();
    auto resultTy = result->GetType();
    if (resultTy->IsEnum()) {
        // 2. check tuple if result type is Enum
        CheckEnumTuple(expr, topLevelFunc);
    } else if (resultTy->IsTuple()) {
        // 3. check tuple if result type is Tuple
        CheckNormalTuple(expr, topLevelFunc);
    } else if (resultTy->IsStruct()) {
        // 4. check tuple if result type is Struct
        CheckStructTuple(expr, topLevelFunc);
    } else {
        // 5. result type must be Enum, Tuple or Struct
        TypeCheckError(expr, *result, "Enum, Tuple or Struct", topLevelFunc);
    }
}

void CHIRChecker::CheckEnumTuple(const Tuple& expr, const Func& topLevelFunc)
{
    // case: Enum-xxx = Tuple(a, b, c...)

    // 1. operands can't be empty
    auto result = expr.GetResult();
    const auto& operands = expr.GetOperands();
    if (operands.empty()) {
        auto errMsg = "must have one operand in `" + result->ToString() +
            "` at least, and the 1st operand's type is UInt32 or Bool.";
        ErrorInFunc(topLevelFunc, errMsg);
        return;
    }

    // 2. the 1st operand is enum constructor's index, so its type must be Bool or UInt32
    auto index = operands[0];
    if (!IsEnumSelectorType(*index->GetType())) {
        TypeCheckError(expr, *index, "UInt32 or Bool", topLevelFunc);
        return;
    }

    // 3. the 1st operand must be a local var
    auto localVar = DynamicCast<LocalVar*>(index);
    if (localVar == nullptr) {
        auto errMsg = "the 1st operand in `" + result->ToString() +
            "` must be from Constant UInt32 or Constant Bool.";
        ErrorInFunc(topLevelFunc, errMsg);
        return;
    }

    // 4. the 1st operand must be from `Constant`
    auto indexExpr = localVar->GetExpr();
    if (!indexExpr->IsConstantInt() && !indexExpr->IsConstantBool()) {
        auto errMsg = "the 1st operand in `" + result->ToString() +
            "` must be from Constant UInt32 or Constant Bool.";
        ErrorInFunc(topLevelFunc, errMsg);
        return;
    }

    if (operands.size() == 1) {
        return;
    }
    // 5. check the other operands, they are enum constructor's parameter types
    auto resultTy = StaticCast<EnumType*>(result->GetType());
    auto ctors = resultTy->GetConstructorInfos(builder);
    size_t idx = 0;
    auto constantExpr = StaticCast<Constant*>(indexExpr);
    if (constantExpr->IsBoolLit()) {
        idx = constantExpr->GetBoolLitVal() ? 1 : 0;
    } else {
        idx = constantExpr->GetUnsignedIntLitVal();
    }
    if (idx >= ctors.size()) {
        auto errMsg = "index out of range, in `" + result->ToString() +
            "` its enum constructor's number is " + std::to_string(ctors.size()) +
            " but the index is " + std::to_string(idx) + ".";
        ErrorInFunc(topLevelFunc, errMsg);
        return;
    }
    auto paramTypes = ctors[idx].funcType->GetParamTypes();
    if (operands.size() - 1 != paramTypes.size()) {
        auto errMsg = "size mismatched, there are " + std::to_string(paramTypes.size()) +
            " parameter(s) in the " + std::to_string(idx) + "-th constructor, but " +
            std::to_string(operands.size() - 1) + " arguments are provided in `" + result->ToString() + "`.";
        ErrorInFunc(topLevelFunc, errMsg);
        return;
    }
    for (size_t i = 1; i < operands.size(); ++i) {
        if (!TypeIsExpected(*operands[i]->GetType(), *paramTypes[i - 1])) {
            auto errMsg = "type mismatched, the " + std::to_string(i - 1) + "-th parameter type is " +
                paramTypes[i - 1]->ToString() + ", but " + operands[i]->GetIdentifier() + "'s type is " +
                operands[i]->GetType()->ToString() + " in `" + result->ToString() + "`.";
            ErrorInFunc(topLevelFunc, errMsg);
        }
    }
}

void CHIRChecker::CheckStructTuple(const Tuple& expr, const Func& topLevelFunc)
{
    // case: Struct-xxx = Tuple(a, b, c...)

    // 1. tuple's operands must be struct's instance member vars, so their sizes and types must be equal
    auto result = expr.GetResult();
    auto resultTy = StaticCast<StructType*>(result->GetType());
    const auto& operands = expr.GetOperands();
    auto structDef = resultTy->GetStructDef();
    auto memberVarTypes = resultTy->GetInstantiatedMemberTys(builder);
    if (operands.size() != memberVarTypes.size()) {
        auto errMsg = "size mismatched, there are " + std::to_string(memberVarTypes.size()) +
            " instance member var(s) in the struct " + structDef->GetIdentifier() + ", but " +
            std::to_string(operands.size()) + " operand(s) are provided in `" + result->ToString() + "`.";
        ErrorInFunc(topLevelFunc, errMsg);
        return;
    }
    for (size_t i = 0; i < memberVarTypes.size(); ++i) {
        if (!TypeIsExpected(*operands[i]->GetType(), *memberVarTypes[i])) {
            auto errMsg = "type mismatched, the " + std::to_string(i) + "-th member var type in struct " +
                structDef->GetIdentifier() + " is " + memberVarTypes[i]->ToString() + ", but " +
                operands[i]->GetIdentifier() + "'s type is " + operands[i]->GetType()->ToString() +
                " in `" + result->ToString() + "`.";
            ErrorInFunc(topLevelFunc, errMsg);
        }
    }
}

void CHIRChecker::CheckNormalTuple(const Tuple& expr, const Func& topLevelFunc)
{
    // case: Tuple(type1, type2, type3...) = Tuple(a, b, c...)

    // 1. operands's size must equal to size of TupleType's element type
    auto result = expr.GetResult();
    auto resultTy = StaticCast<TupleType*>(result->GetType());
    const auto& operands = expr.GetOperands();
    auto elementTypes = StaticCast<TupleType*>(resultTy)->GetElementTypes();
    if (operands.size() != elementTypes.size()) {
        auto errMsg = "size mismatched, there are " + std::to_string(elementTypes.size()) +
            " element(s) in the tuple type `" + resultTy->ToString() + "`, but " +
            std::to_string(operands.size()) + " operand(s) are provided in `" + result->ToString() + "`.";
        ErrorInFunc(topLevelFunc, errMsg);
        return;
    }

    // 2. operand's type must equal to element type
    for (size_t i = 0; i < elementTypes.size(); ++i) {
        if (!TypeIsExpected(*operands[i]->GetType(), *elementTypes[i])) {
            auto errMsg = "type mismatched, the " + std::to_string(i) + "-th element type in tuple type `" +
                resultTy->ToString() + "` is " + elementTypes[i]->ToString() + ", but " +
                operands[i]->GetIdentifier() + "'s type is " + operands[i]->GetType()->ToString() +
                " in `" + result->ToString() + "`.";
            ErrorInFunc(topLevelFunc, errMsg);
        }
    }
}

void CHIRChecker::CheckField(const Field& expr, const Func& topLevelFunc)
{
    // 1. only have 1 operand
    if (!OperandNumIsEqual(1, expr, topLevelFunc)) {
        return;
    }

    // 2. location type can't be ref, must be Struct, Enum or Tuple
    auto location = expr.GetBase();
    auto locationType = location->GetType();
    if (locationType->IsRef()) {
        TypeCheckError(expr, *location, locationType->StripAllRefs()->ToString(), topLevelFunc);
    } else if (!locationType->IsStruct() && !locationType->IsEnum() && !locationType->IsTuple()) {
        TypeCheckError(expr, *location, "Struct, Enum or Tuple", topLevelFunc);
    }

    // 3. path can't be out of bounds
    const auto& path = expr.GetPath();
    auto result = expr.GetResult();
    if (path.empty()) {
        auto errMsg = "path is empty in Field `" + result->ToString() + "`.";
        ErrorInFunc(topLevelFunc, errMsg);
    }
    std::string errMsg = "wrong path: ";
    for (uint64_t i = 0; i < path.size(); ++i) {
        errMsg += locationType->ToString() + "[index: " + std::to_string(path[i]) + "] --> ";
        locationType = GetFieldOfType(*locationType, path[i], builder);
        if (locationType == nullptr) {
            errMsg += "unknown type, in Field `" + result->ToString() + "`.";
            ErrorInFunc(topLevelFunc, errMsg);
            return;
        }
    }

    // 4. calculated type by path can set to result type
    if (!TypeIsExpected(*locationType, *result->GetType())) {
        TypeCheckError(expr, *result, locationType->ToString(), topLevelFunc);
    }
}

void CHIRChecker::CheckFieldByName(const FieldByName& expr, const Func& topLevelFunc)
{
    ErrorInExpr(topLevelFunc, expr, "you should convert this expression to `Field`.");
}

void CHIRChecker::CheckApply(const Apply& expr, const Func& topLevelFunc)
{
    // 1. there is 1 operand at least
    if (!OperandNumAtLeast(1, expr, topLevelFunc)) {
        return;
    }
    
    CheckApplyBase(ApplyBase(&expr), topLevelFunc);
}

void CHIRChecker::CheckInvoke(const Invoke& expr, const Func& topLevelFunc)
{
    if (!OperandNumAtLeast(1, expr, topLevelFunc)) {
        return;
    }
    CheckInvokeBase(InvokeBase(&expr), topLevelFunc);
}

void CHIRChecker::CheckInvokeStatic(const InvokeStatic& expr, const Func& topLevelFunc)
{
    if (!OperandNumAtLeast(1, expr, topLevelFunc)) {
        return;
    }
    CheckInvokeStaticBase(InvokeStaticBase(&expr), topLevelFunc);
}

void CHIRChecker::CheckInstanceOf(const InstanceOf& expr, const Func& topLevelFunc)
{
    if (!OperandNumIsEqual(1, expr, topLevelFunc)) {
        return;
    }
    auto result = expr.GetResult();
    // 1. return type must be bool
    if (!result->GetType()->IsBoolean()) {
        TypeCheckError(expr, *result, "Bool", topLevelFunc);
    }
    // 2. target type must be valid
    CheckTypeIsValid(*expr.GetType(), "target", expr, topLevelFunc);
}

void CHIRChecker::CheckTypeCast([[maybe_unused]] const TypeCast& expr, [[maybe_unused]] const Func& topLevelFunc)
{
}

void CHIRChecker::CheckGetException(const GetException& expr, const Func& topLevelFunc)
{
    if (!OperandNumIsEqual(0, expr, topLevelFunc)) {
        return;
    }
    auto result = expr.GetResult();
    // 1. return type must be class&
    if (!result->GetType()->IsRef()) {
        TypeCheckError(expr, *result, "Object&", topLevelFunc);
         return;
    }

    auto baseType = StaticCast<RefType*>(result->GetType())->GetBaseType();
    if (!baseType->IsClass()) {
        TypeCheckError(expr, *result, "Object&", topLevelFunc);
        return;
    }

    if (!CheckCustomTypeDefIsExpected(*StaticCast<ClassType*>(baseType)->GetClassDef(), CORE_PACKAGE_NAME, "Object")) {
        TypeCheckError(expr, *result, "Object&", topLevelFunc);
    }
}

void CHIRChecker::CheckSpawn(const Spawn& expr, const Func& topLevelFunc)
{
    if (!OperandNumAtLeast(1, expr, topLevelFunc)) {
        return;
    }
    CheckSpawnBase(SpawnBase(&expr), topLevelFunc);
}

void CHIRChecker::CheckRawArrayAllocate(const RawArrayAllocate& expr, const Func& topLevelFunc)
{
    if (!OperandNumIsEqual(1, expr, topLevelFunc)) {
        return;
    }
    CheckRawArrayAllocateBase(RawArrayAllocateBase(&expr), topLevelFunc);
}

void CHIRChecker::CheckRawArrayLiteralInit(const RawArrayLiteralInit& expr, const Func& topLevelFunc)
{
    if (!OperandNumAtLeast(1, expr, topLevelFunc)) {
        return;
    }

    // 1. result type must be Unit
    auto result = expr.GetResult();
    if (!result->GetType()->IsUnit()) {
        TypeCheckError(expr, *result, "Unit", topLevelFunc);
    }

    // 2. the 1st operand must be RawArray&
    auto rawArray = expr.GetRawArray();
    if (!rawArray->GetType()->IsRef() || !rawArray->GetType()->StripAllRefs()->IsRawArray()) {
        TypeCheckError(expr, *rawArray, "RawArray&", topLevelFunc);
    }

    // 3. all elements' type must be equal to RawArray's element type
    auto rawArrayEleTy = StaticCast<RawArrayType*>(rawArray->GetType()->StripAllRefs())->GetElementType();
    for (auto e : expr.GetElements()) {
        if (!e->GetType()->IsEqualOrSubTypeOf(*rawArrayEleTy, builder)) {
            TypeCheckError(expr, *e, rawArrayEleTy->ToString(), topLevelFunc);
        }
    }
}

void CHIRChecker::CheckRawArrayInitByValue(const RawArrayInitByValue& expr, const Func& topLevelFunc)
{
    // 1. have 3 operands
    if (!OperandNumIsEqual(3, expr, topLevelFunc)) {
        return;
    }
    auto result = expr.GetResult();
    if (!result->GetType()->IsUnit()) {
        TypeCheckError(expr, *result, "Unit", topLevelFunc);
    }
    auto size = expr.GetSize();
    if (size->GetType()->GetTypeKind() != Type::TypeKind::TYPE_INT64) {
        TypeCheckError(expr, *size, "Int64", topLevelFunc);
    }
    auto rawArray = expr.GetRawArray();
    if (!rawArray->GetType()->IsRef() || !rawArray->GetType()->StripAllRefs()->IsRawArray()) {
        TypeCheckError(expr, *rawArray, "RawArray&", topLevelFunc);
    } else {
        auto rawArrayEleTy = StaticCast<RawArrayType*>(rawArray->GetType()->StripAllRefs())->GetElementType();
        if (rawArrayEleTy != expr.GetInitValue()->GetType()) {
            TypeCheckError(expr, *expr.GetInitValue(), rawArrayEleTy->ToString(), topLevelFunc);
        }
    }
}

void CHIRChecker::CheckVArray(const VArray& expr, const Func& topLevelFunc)
{
    auto result = expr.GetResult();
    if (!result->GetType()->IsVArray()) {
        TypeCheckError(expr, *result, "VArray", topLevelFunc);
        return;
    }
    // elements type check
    auto elemType = StaticCast<VArrayType*>(result->GetType())->GetElementType();
    // only use VArray to be parameter type in CFunc, its element type shouldn't be normal struct,
    // must be marked with @C
    if (!IsCTypeInVArray(*elemType)) {
        auto errMsg = "its element type is " + elemType->ToString() + ", but a C type is expected.\n";
        auto hint =
            "    C type includes: numeric data types, Rune, Bool, Unit, Nothing, CPointer, CString, CFunc and Struct.";
        ErrorInExpr(topLevelFunc, expr, errMsg + hint);
        return;
    }
    for (auto arg : expr.GetOperands()) {
        if (!arg->GetType()->IsEqualOrSubTypeOf(*elemType, builder)) {
            TypeCheckError(expr, *arg, elemType->ToString(), topLevelFunc);
        }
    }
}

void CHIRChecker::CheckVArrayBuilder(const VArrayBuilder& expr, const Func& topLevelFunc)
{
    // 1. have 3 operands
    if (!OperandNumIsEqual(3, expr, topLevelFunc)) {
        return;
    }
    auto result = expr.GetResult();
    if (!result->GetType()->IsVArray()) {
        TypeCheckError(expr, *result, "VArray", topLevelFunc);
    }
    auto size = expr.GetSize();
    if (size->GetType()->GetTypeKind() != Type::TypeKind::TYPE_INT64) {
        TypeCheckError(expr, *size, "Int64", topLevelFunc);
    }
    auto item = ConvertToNullLiteral(*expr.GetItem());
    auto initFunc = ConvertToNullLiteral(*expr.GetInitFunc());
    if (item == nullptr && initFunc == nullptr) {
        ErrorInExpr(topLevelFunc, expr, "one of item and init func should be null.");
    } else if (item != nullptr && initFunc != nullptr) {
        ErrorInExpr(topLevelFunc, expr, "item and init func can't be both null.");
    }
    auto funcType = expr.GetInitFunc()->GetType();
    if (!IsFuncTypeOrClosureBaseRefType(*funcType)) {
        TypeCheckError(expr, *expr.GetInitFunc(), "Func type or closure ref", topLevelFunc);
    }
}

void CHIRChecker::CheckIntrinsic(const Intrinsic& expr, const Func& topLevelFunc)
{
    CheckIntrinsicBase(IntrinsicBase(&expr), topLevelFunc);
}

void CHIRChecker::CheckBox(const Box& expr, const Func& topLevelFunc)
{
    if (!OperandNumIsEqual(1, expr, topLevelFunc)) {
        return;
    }
    auto targetType = expr.GetTargetTy();
    auto sourceType = expr.GetSourceTy();
    if (!targetType->IsRef()) {
        TypeCheckError(expr, *expr.GetResult(), "reference", topLevelFunc);
    } else if (targetType->StripAllRefs()->IsBox()) {
        if (StaticCast<BoxType*>(targetType->StripAllRefs())->GetBaseType() != sourceType) {
            auto errMsg = "the target type is " + targetType->ToString() +
                ", then source type must be its base type, can't be " + sourceType->ToString() + ".";
            ErrorInExpr(topLevelFunc, expr, errMsg);
        }
    } else if (!targetType->StripAllRefs()->IsClass()) {
        auto errMsg = "the target type must be Class& or Box&, can't be " + targetType->ToString() + ".";
        ErrorInExpr(topLevelFunc, expr, errMsg);
    }
    if (!sourceType->IsValueType()) {
        TypeCheckError(expr, *expr.GetSourceValue(), "value", topLevelFunc);
    }
}

void CHIRChecker::CheckUnBox(const UnBox& expr, const Func& topLevelFunc)
{
    if (!OperandNumIsEqual(1, expr, topLevelFunc)) {
        return;
    }
    auto targetType = expr.GetTargetTy();
    auto sourceType = expr.GetSourceTy();
    if (!sourceType->IsRef()) {
        TypeCheckError(expr, *expr.GetSourceValue(), "reference", topLevelFunc);
    } else if (sourceType->StripAllRefs()->IsBox()) {
        if (StaticCast<BoxType*>(sourceType->StripAllRefs())->GetBaseType() != targetType) {
            auto errMsg = "the source type is " + sourceType->ToString() +
                ", then target type must be its base type, can't be " + targetType->ToString() + ".";
            ErrorInExpr(topLevelFunc, expr, errMsg);
        }
    } else if (!sourceType->StripAllRefs()->IsClass()) {
        auto errMsg = "the source type must be Class& or Box&, can't be " + sourceType->ToString() + ".";
        ErrorInExpr(topLevelFunc, expr, errMsg);
    }
    if (!targetType->IsValueType()) {
        TypeCheckError(expr, *expr.GetResult(), "value", topLevelFunc);
    }
}

void CHIRChecker::CheckTransformToGeneric(const TransformToGeneric& expr, const Func& topLevelFunc)
{
    if (!OperandNumIsEqual(1, expr, topLevelFunc)) {
        return;
    }
    if (!expr.GetTargetTy()->IsGenericRelated()) {
        TypeCheckError(expr, *expr.GetResult(), "generic related", topLevelFunc);
    }
    if (expr.GetSourceTy()->IsGeneric()) {
        TypeCheckError(expr, *expr.GetSourceValue(), "concrete", topLevelFunc);
    }
}

void CHIRChecker::CheckTransformToConcrete(const TransformToConcrete& expr, const Func& topLevelFunc)
{
    if (!OperandNumIsEqual(1, expr, topLevelFunc)) {
        return;
    }
    if (expr.GetTargetTy()->IsGeneric()) {
        TypeCheckError(expr, *expr.GetResult(), "concrete", topLevelFunc);
    }
    if (!expr.GetSourceTy()->IsGenericRelated()) {
        TypeCheckError(expr, *expr.GetSourceValue(), "generic related", topLevelFunc);
    }
}

void CHIRChecker::CheckGetInstantiateValue(const GetInstantiateValue& expr, const Func& topLevelFunc)
{
    if (optionalRules.find(Rule::GET_INSTANTIATE_VALUE_SHOULD_GONE) != optionalRules.end()) {
        ErrorInExpr(topLevelFunc, expr,
            "`GetInstantiateValue` should be removed now, it can't be seen in CodeGen Stage.");
        return;
    }
    if (!OperandNumIsEqual(1, expr, topLevelFunc)) {
        return;
    }

    // 1. operand must be FuncType
    auto func = expr.GetGenericResult();
    if (!func->GetType()->IsFunc()) {
        TypeCheckError(expr, *func, "Func", topLevelFunc);
        return;
    }

    // 2. must have instantiated type args, or you will use FuncBase* directly
    auto instTypeArgs = expr.GetInstantiateTypes();
    if (instTypeArgs.empty()) {
        auto errMsg = "there must be instantiated type in `GetInstantiateValue`";
        ErrorInExpr(topLevelFunc, expr, errMsg);
        return;
    }

    // 3. instantiated type args must satisfy all generic constraints
    std::vector<std::pair<const Value*, std::vector<GenericType*>>> funcAndGenericTypes;
    CollectFuncAndGenericTypesRecursively(*func, funcAndGenericTypes);
    CJC_ASSERT(!funcAndGenericTypes.empty());
    size_t allGenericTypes = 0;
    for (auto& it : funcAndGenericTypes) {
        allGenericTypes += it.second.size();
    }
    auto funcBase = VirtualCast<const FuncBase*>(funcAndGenericTypes.front().first);
    if (auto parentType = funcBase->GetParentCustomTypeOrExtendedType()) {
        allGenericTypes += parentType->GetTypeArgs().size();
    }
    if (allGenericTypes != instTypeArgs.size()) {
        auto genericTypeSize = funcAndGenericTypes.back().second.size();
        auto errMsg = "current func is " + GetFuncIdentifier(*func) + " with " +
            std::to_string(genericTypeSize) + " generic param type(s), and its outer func is ";
        funcAndGenericTypes.pop_back();
        for (auto it = funcAndGenericTypes.crbegin(); it != funcAndGenericTypes.crend(); ++it) {
            errMsg += "[" + GetFuncIdentifier(*it->first) + " with " +
                std::to_string(it->second.size()) + " generic param type(s) ], ";
        }
        if (auto parentType = funcBase->GetParentCustomTypeOrExtendedType()) {
            errMsg += "and the parent Custom type is " + parentType->ToString() + " with " +
                std::to_string(parentType->GetTypeArgs().size()) + " generic param type(s), ";
        }
        errMsg += "they have " + std::to_string(allGenericTypes) +
            " generic param type(s) in total, but there are " + std::to_string(instTypeArgs.size()) +
            " instantiated type arg(s) in current `GetInstantiateValue`.";
        ErrorInExpr(topLevelFunc, expr, errMsg);
        return;
    }
    size_t typeIdx = 0;
    std::string errMsg = "the ";
    bool hasError = false;
    if (auto parentType = funcBase->GetParentCustomTypeOrExtendedType()) {
        if (auto customType = DynamicCast<CustomType*>(parentType)) {
            for (auto arg : customType->GetGenericArgs()) {
                if (!instTypeArgs[typeIdx]->IsEqualOrInstantiatedTypeOf(*arg, builder)) {
                    errMsg += IndexToString(typeIdx + 1) + ", ";
                    hasError = true;
                }
                ++typeIdx;
            }
        }
    }
    for (auto& it : funcAndGenericTypes) {
        for (auto genericType : it.second) {
            if (!instTypeArgs[typeIdx]->IsEqualOrInstantiatedTypeOf(*genericType, builder)) {
                errMsg += IndexToString(typeIdx + 1) + ", ";
                hasError = true;
            }
            ++typeIdx;
        }
    }
    if (hasError) {
        // remove ", "
        errMsg.pop_back();
        errMsg.pop_back();
        errMsg += " instantiated type(s) don't satisfy the generic constraints.";
        ErrorInExpr(topLevelFunc, expr, errMsg);
    }
}

void CHIRChecker::CheckUnBoxToRef(const UnBoxToRef& expr, const Func& topLevelFunc)
{
    if (!OperandNumIsEqual(1, expr, topLevelFunc)) {
        return;
    }

    auto targetType = expr.GetTargetTy();
    auto isValueRefType = targetType->IsRef() &&
        StaticCast<RefType*>(targetType)->GetBaseType()->IsValueType();
    if (!isValueRefType) {
        TypeCheckError(expr, *expr.GetResult(), "Value&", topLevelFunc);
    }
    
    auto sourceType = expr.GetSourceTy();
    auto isReferenceRefType = sourceType->IsRef() &&
        StaticCast<RefType*>(sourceType)->GetBaseType()->IsReferenceType();
    if (!isReferenceRefType) {
        TypeCheckError(expr, *expr.GetSourceValue(), "Reference&", topLevelFunc);
    }
}

void CHIRChecker::CheckGetRTTI(const GetRTTI& expr, const Func& topLevelFunc)
{
    if (!OperandNumIsEqual(1, expr, topLevelFunc)) {
        return;
    }
    auto result = expr.GetResult();
    if (!result->GetType()->IsUnit()) {
        TypeCheckError(expr, *result, "Unit", topLevelFunc);
    }
    auto operand = expr.GetOperand();
    auto operandType = operand->GetType();
    if (!operandType->IsRef()) {
        TypeCheckError(expr, *operand, "Class& or This&", topLevelFunc);
    } else {
        auto baseType = StaticCast<RefType*>(operandType)->GetBaseType();
        if (!baseType->IsClass() && !baseType->IsThis()) {
            TypeCheckError(expr, *operand, "Class& or This&", topLevelFunc);
        }
    }
}

void CHIRChecker::CheckGetRTTIStatic(const GetRTTIStatic& expr, const Func& topLevelFunc)
{
    if (!OperandNumIsEqual(0, expr, topLevelFunc)) {
        return;
    }
    auto result = expr.GetResult();
    if (!result->GetType()->IsUnit()) {
        TypeCheckError(expr, *result, "Unit", topLevelFunc);
    }
    if (optionalRules.find(Rule::CHIR_GET_RTTI_STATIC_TYPE) == optionalRules.end()) {
        return;
    }
    auto type = expr.GetRTTIType()->StripAllRefs();
    if (!type->IsGeneric() && !type->IsThis()) {
        ErrorInExpr(topLevelFunc, expr,
            "RTTI type should be `This` or Generic type, but now it's " + expr.GetRTTIType()->ToString() + ".");
    }
}

void CHIRChecker::CheckMemoryExpression(const Expression& expr, const Func& topLevelFunc)
{
    const std::unordered_map<ExprKind, std::function<void()>> actionMap = {
        {ExprKind::ALLOCATE, [this, &expr, &topLevelFunc]() {
            CheckAllocate(StaticCast<const Allocate&>(expr), topLevelFunc); }},
        {ExprKind::LOAD, [this, &expr, &topLevelFunc]() {
            CheckLoad(StaticCast<const Load&>(expr), topLevelFunc); }},
        {ExprKind::STORE, [this, &expr, &topLevelFunc]() {
            CheckStore(StaticCast<const Store&>(expr), topLevelFunc); }},
        {ExprKind::GET_ELEMENT_REF, [this, &expr, &topLevelFunc]() {
            CheckGetElementRef(StaticCast<const GetElementRef&>(expr), topLevelFunc); }},
        {ExprKind::GET_ELEMENT_BY_NAME, [this, &expr, &topLevelFunc]() {
            CheckGetElementByName(StaticCast<const GetElementByName&>(expr), topLevelFunc); }},
        {ExprKind::STORE_ELEMENT_REF, [this, &expr, &topLevelFunc]() {
            CheckStoreElementRef(StaticCast<const StoreElementRef&>(expr), topLevelFunc); }},
        {ExprKind::STORE_ELEMENT_BY_NAME, [this, &expr, &topLevelFunc]() {
            CheckStoreElementByName(StaticCast<const StoreElementByName&>(expr), topLevelFunc); }},
    };
    if (auto it = actionMap.find(expr.GetExprKind()); it != actionMap.end()) {
        it->second();
    } else {
        WarningInExpr(topLevelFunc, expr, "find unrecongnized ExprKind `" + expr.GetExprKindName() + ".");
    }
}

void CHIRChecker::CheckAllocate(const Allocate& expr, const Func& topLevelFunc)
{
    // 1. don't have operands
    OperandNumIsEqual(0, expr, topLevelFunc);

    CheckAllocateBase(AllocateBase(&expr), topLevelFunc);
}

void CHIRChecker::CheckLoad(const Load& expr, const Func& topLevelFunc)
{
    // 1. only have 1 operand
    if (!OperandNumIsEqual(1, expr, topLevelFunc)) {
        return;
    }

    // 2. location type must be ref
    auto location = expr.GetLocation();
    auto result = expr.GetResult();
    if (!location->GetType()->IsRef()) {
        TypeCheckError(expr, *location, result->GetType()->ToString() + "&", topLevelFunc);
        return;
    }

    // 3. location type must be ref of result type
    if (auto expectedTy = StaticCast<RefType*>(location->GetType())->GetBaseType();
        expectedTy != result->GetType()) {
        TypeCheckError(expr, *result, expectedTy->ToString(), topLevelFunc);
    }
}

void CHIRChecker::CheckStore(const Store& expr, const Func& topLevelFunc)
{
    // 1. have 2 operands
    if (!OperandNumIsEqual(2, expr, topLevelFunc)) {
        return;
    }

    // 2. location type must be ref
    auto location = expr.GetLocation();
    if (!location->GetType()->IsRef()) {
        TypeCheckError(expr, *location, location->GetType()->ToString() + "&", topLevelFunc);
        return;
    }

    // 3. location type must be ref of value type
    auto value = expr.GetValue();
    if (auto expectedTy = StaticCast<RefType*>(location->GetType())->GetBaseType();
        !TypeIsExpected(*value->GetType(), *expectedTy)) {
        TypeCheckError(expr, *value, expectedTy->ToString(), topLevelFunc);
    }

    // 4. result type must be `Unit`
    auto result = expr.GetResult();
    if (!result->GetType()->IsUnit()) {
        TypeCheckError(expr, *result, "Unit", topLevelFunc);
    }

    // 5. can't store a value to GetElementRef's result
    if (auto res = DynamicCast<LocalVar*>(location)) {
        if (res->GetExpr()->GetExprKind() == ExprKind::GET_ELEMENT_REF) {
            auto errMsg = "destination of Store `" + result->GetIdentifier() + "` should not be GetElementRef.";
            ErrorInFunc(topLevelFunc, errMsg);
        }
    }
}

void CHIRChecker::CheckGetElementRef(const GetElementRef& expr, const Func& topLevelFunc)
{
    // 1. only have 1 operand
    if (!OperandNumIsEqual(1, expr, topLevelFunc)) {
        return;
    }

    // 2. location type must be ref type
    auto location = expr.GetLocation();
    auto locationType = location->GetType();
    if (!locationType->IsRef()) {
        TypeCheckError(expr, *location, locationType->ToString() + "&", topLevelFunc);
        return;
    }

    // 3. location type must be Class&, Struct&, Enum&, RawArray& or Tuple&
    if (auto baseType = StaticCast<RefType*>(locationType)->GetBaseType();
        !baseType->IsClass() && !baseType->IsStruct() && !baseType->IsEnum() && !baseType->IsTuple() &&
        !baseType->IsRawArray()) {
        TypeCheckError(expr, *location, "Class&, Struct&, Enum&, RawArray& or Tuple&", topLevelFunc);
        return;
    }

    // 4. path can't be out of bounds
    const auto& path = expr.GetPath();
    auto result = expr.GetResult();
    if (path.empty()) {
        auto errMsg = "path is empty in GetElementRef `" + result->ToString() + "`.";
        ErrorInFunc(topLevelFunc, errMsg);
    }
    std::string errMsg = "wrong path: ";
    for (uint64_t i = 0; i < path.size(); ++i) {
        errMsg += locationType->ToString() + "[index: " + std::to_string(path[i]) + "] --> ";
        locationType = GetFieldOfType(*locationType, path[i], builder);
        if (locationType == nullptr) {
            errMsg += "unknown type, in GetElementRef `" + result->ToString() + "`.";
            ErrorInFunc(topLevelFunc, errMsg);
        }
    }

    // 5. result type must be ref, `&` or `&&`
    if (!result->GetType()->IsRef()) {
        TypeCheckError(expr, *result, result->GetType()->ToString() + "&", topLevelFunc);
        return;
    }

    // 6. calculated type by path must be matched with result type
    if (auto baseType = StaticCast<RefType*>(result->GetType())->GetBaseType();
        !TypeIsExpected(*locationType, *baseType)) {
        TypeCheckError(expr, *result, locationType->ToString() + "&", topLevelFunc);
    }
}

void CHIRChecker::CheckGetElementByName(const GetElementByName& expr, const Func& topLevelFunc)
{
    ErrorInExpr(topLevelFunc, expr, "you should convert this expression to `GetElementRef`.");
}

void CHIRChecker::CheckStoreElementRef(const StoreElementRef& expr, const Func& topLevelFunc)
{
    // 1. have 2 operands
    if (!OperandNumIsEqual(2, expr, topLevelFunc)) {
        return;
    }

    // 2. location type must be ref
    auto location = expr.GetLocation();
    auto locationType = location->GetType();
    if (!locationType->IsRef()) {
        TypeCheckError(expr, *location, locationType->ToString() + "&", topLevelFunc);
        return;
    }

    // 3. location type must be Class&, Struct&, RawArray& or Tuple&
    auto baseType = StaticCast<RefType*>(locationType)->GetBaseType();
    if (!baseType->IsClass() && !baseType->IsStruct() && !baseType->IsTuple() && !baseType->IsRawArray()) {
        TypeCheckError(expr, *location, "Class&, Struct&, RawArray& or Tuple&", topLevelFunc);
    }

    // 4. path can't be out of bounds
    const auto& path = expr.GetPath();
    auto result = expr.GetResult();
    if (path.empty()) {
        auto errMsg = "path is empty in StoreElementRef `" + result->ToString() + "`.";
        ErrorInFunc(topLevelFunc, errMsg);
    }
    std::string errMsg = "wrong path: ";
    for (uint64_t i = 0; i < path.size(); ++i) {
        errMsg += locationType->ToString() + "[index: " + std::to_string(path[i]) + "] --> ";
        locationType = GetFieldOfType(*locationType, path[i], builder);
        if (locationType == nullptr) {
            errMsg += "unknown type, in StoreElementRef `" + result->ToString() + "`.";
            ErrorInFunc(topLevelFunc, errMsg);
        }
    }

    // 5. calculated type by path must be matched with value type
    if (locationType != nullptr) {
        auto valueType = expr.GetValue()->GetType();
        if (!TypeIsExpected(*valueType, *locationType)) {
            TypeCheckError(expr, *expr.GetValue(), locationType->ToString(), topLevelFunc);
        }
    }
    
    // 6. result type must be `Unit`
    if (!result->GetType()->IsUnit()) {
        TypeCheckError(expr, *result, "Unit", topLevelFunc);
    }
}

void CHIRChecker::CheckStoreElementByName(const StoreElementByName& expr, const Func& topLevelFunc)
{
    ErrorInExpr(topLevelFunc, expr, "you should convert this expression to `StoreElementRef`.");
}