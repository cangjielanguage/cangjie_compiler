// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/ClosureConversion.h"

#include "cangjie/CHIR/Annotation.h"
#include "cangjie/CHIR/AST2CHIR/GenerateVTable/VTableGenerator.h"
#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/Transformation/FunctionInline.h"
#include "cangjie/CHIR/Transformation/LambdaInline.h"
#include "cangjie/CHIR/Type/PrivateTypeConverter.h"
#include "cangjie/CHIR/Visitor/Visitor.h"
#include "cangjie/Mangle/CHIRManglingUtils.h"
#include "cangjie/Mangle/CHIRTypeManglingUtils.h"
#include "cangjie/Option/Option.h"

using namespace Cangjie;
using namespace Cangjie::CHIR;

namespace {
const std::string GENERIC_THIS_SRC_NAME = "This";

bool IsCalleeOfApply(const Expression& user, const Value& op)
{
    if (auto apply = DynamicCast<const Apply*>(&user)) {
        return apply->GetCallee() == &op;
    } else if (auto applyWithException = DynamicCast<const ApplyWithException*>(&user)) {
        return applyWithException->GetCallee() == &op;
    } else if (auto invoke = DynamicCast<const Invoke*>(&user)) {
        return invoke->GetObject() == &op;
    } else if (auto invokeWithException = DynamicCast<const InvokeWithException*>(&user)) {
        return invokeWithException->GetObject() == &op;
    }
    return false;
}

bool IsMutableVarType(const Value& value)
{
    if (value.IsParameter()) {
        return false;
    }
    auto ref = DynamicCast<const RefType*>(value.GetType());
    if (ref == nullptr) {
        return false;
    }
    // type is ref, but not ref a class or array, that means this type is a mutable var's type
    return !ref->GetBaseType()->IsClassOrArray();
}

void CollectNestedLambdaExpr(
    const Lambda& root, std::vector<Lambda*>& nestedFuncs, Func* curOutFunc, std::set<std::string>& ccOutFuncs)
{
    CJC_NULLPTR_CHECK(curOutFunc);
    auto preVisit = [&nestedFuncs, &curOutFunc, &ccOutFuncs](Expression& e) {
        if (auto lambdaExpr = DynamicCast<Lambda*>(&e); lambdaExpr) {
            CollectNestedLambdaExpr(*lambdaExpr, nestedFuncs, curOutFunc, ccOutFuncs);
            nestedFuncs.emplace_back(lambdaExpr);
        }
        return VisitResult::CONTINUE;
    };
    VisitFuncBlocksInTopoSort(*root.GetBody(), preVisit);
}

void GetAllGenericType(Type& type, std::vector<Type*>& result)
{
    if (type.IsGeneric() && std::find(result.begin(), result.end(), &type) == result.end()) {
        result.emplace_back(&type);
    } else {
        for (auto argTy : type.GetTypeArgs()) {
            GetAllGenericType(*argTy, result);
        }
    }
}

Ptr<Type> GetInstTypeFromOrphanGenericType(Ptr<Type> type)
{
    if (!type->IsGeneric()) {
        return nullptr;
    }
    auto gTy = StaticCast<GenericType*>(type);
    if (gTy->GetUpperBounds().size() == 1 && gTy->orphanFlag) {
        return gTy->GetUpperBounds()[0];
    }
    return nullptr;
}

Ptr<Type> GetOriFuncTypeFromOrphanType(Ptr<Type> type, CHIRBuilder& builder)
{
    if (!type->IsFunc()) {
        return type;
    }
    auto funcType = StaticCast<FuncType*>(type);
    std::vector<Type*> originalArgs;
    bool isOrphan = false;
    for (auto argType : funcType->GetParamTypes()) {
        if (auto oriType = GetInstTypeFromOrphanGenericType(argType); oriType != nullptr) {
            originalArgs.push_back(oriType);
            isOrphan = true;
            continue;
        } else {
            originalArgs.push_back(argType);
        }
    }
    auto retType = funcType->GetReturnType();
    if (auto oriRetType = GetInstTypeFromOrphanGenericType(funcType->GetReturnType()); oriRetType != nullptr) {
        isOrphan = true;
        retType = oriRetType;
    }
    if (isOrphan) {
        funcType = builder.GetType<FuncType>(originalArgs, retType);
    }
    return funcType;
}

void SetAutoEnvImplDefAttr(ClassDef& def)
{
    def.EnableAttr(Attribute::COMPILER_ADD);
    def.EnableAttr(Attribute::NO_REFLECT_INFO);
    def.Set<IsAutoEnvClass>(true);
    def.Set<LinkTypeInfo>(Linkage::INTERNAL);
}

void SetAutoEnvBaseDefAttr(ClassDef& def)
{
    SetAutoEnvImplDefAttr(def);
    def.EnableAttr(Attribute::ABSTRACT);
}

void SetLiftedLambdaAttr(Func& func, Lambda& lambda)
{
    func.EnableAttr(Attribute::COMPILER_ADD);
    func.EnableAttr(Attribute::NO_REFLECT_INFO);
    func.EnableAttr(Attribute::INTERNAL);
    func.Set<LinkTypeInfo>(Linkage::INTERNAL);
    func.SetFuncKind(FuncKind::LAMBDA);
    if (!lambda.GetGenericTypeParams().empty()) {
        func.EnableAttr(Attribute::GENERIC);
    }
    if (lambda.IsCompileTimeValue()) {
        func.EnableAttr(Attribute::CONST);
    }
    if (auto body = lambda.GetBody(); body && body->TestAttr(Attribute::NO_INLINE)) {
        func.EnableAttr(Attribute::NO_INLINE);
    }
}

void SetMemberMethodAttr(Func& func, bool isConst)
{
    func.EnableAttr(Attribute::COMPILER_ADD);
    func.EnableAttr(Attribute::NO_REFLECT_INFO);
    func.EnableAttr(Attribute::INTERNAL);
    func.EnableAttr(Attribute::PUBLIC);
    func.Set<LinkTypeInfo>(Linkage::INTERNAL);
    if (isConst) {
        func.EnableAttr(Attribute::CONST);
    }
}

bool FuncTypeHasGenericT(const FuncBase& func)
{
    if (func.GetFuncKind() == FuncKind::LAMBDA) {
        auto funcType = func.GetFuncType();
        auto paramTypes = funcType->GetParamTypes();
        paramTypes.erase(paramTypes.begin());
        for (auto ty : paramTypes) {
            if (ty->IsGenericRelated()) {
                return true;
            }
        }
        return funcType->GetReturnType()->IsGenericRelated();
    } else {
        return func.GetFuncType()->IsGenericRelated();
    }
}

bool IsCFuncOrInCFunc(const Lambda& srcFunc)
{
    if (srcFunc.GetFuncType()->IsCFunc()) {
        return true;
    }
    auto blockGroup = srcFunc.GetParentBlockGroup();
    CJC_NULLPTR_CHECK(blockGroup);
    while (blockGroup->GetOwnerFunc() == nullptr) {
        auto ownedExpr = blockGroup->GetOwnerExpression();
        if (auto lambda = DynamicCast<Lambda>(ownedExpr)) {
            if (lambda->GetFuncType()->IsCFunc()) {
                return true;
            }
            blockGroup = lambda->GetParentBlockGroup();
        } else {
            blockGroup = ownedExpr->GetParentBlockGroup();
        }
    }
    return false;
}

bool NeedAddThisType(const Value& srcFunc)
{
    auto localVar = DynamicCast<const LocalVar*>(&srcFunc);
    // only lambda need `This`
    if (localVar == nullptr) {
        return false;
    }
    if (IsCFuncOrInCFunc(*StaticCast<Lambda*>(localVar->GetExpr()))) {
        return false;
    }
    // only lambda in member method need `This`
    auto outerDecl = localVar->GetTopLevelFunc()->GetOuterDeclaredOrExtendedDef();
    if (outerDecl == nullptr) {
        return false;
    }
    // custom type def must be inheritable, if in extend def, we check extended custom type def
    if (outerDecl->IsEnum() || outerDecl->IsStruct()) {
        return false;
    }
    return outerDecl->CanBeInherited();
}

size_t GetFuncGenericTypeParamNum(const Value& srcFunc)
{
    size_t num = 0;
    if (auto funcBase = DynamicCast<const FuncBase*>(&srcFunc)) {
        num = funcBase->GetGenericTypeParams().size();
    } else {
        auto lambda = StaticCast<Lambda*>(StaticCast<const LocalVar&>(srcFunc).GetExpr());
        num = lambda->GetGenericTypeParams().size();
    }
    return num;
}

/** `givInstTypes` store all instantiated info, we need to know what CustomTypeDef's generic params are instantiated.
 *  extend def is different with other CustomTypeDefs, let's view some cases
 *  case 1:
 *  class A<X, Y, Z> {}
 *  extend<T1, T2, T3> A<T2, T1, Option<T3>> {}
 *  if there is a type `A<Bool, Int32, Option<Unit>>`, we need to know T1 is Int32, T2 is Bool, T3 is Unit
 *  so return value is expected to be vector{Int32, Bool, Unit}
 *
 *  case 2:
 *  extend CPointer<Bool> {}
 *  there isn't generic type param in extend def, so if we meet type `CPointer<Bool>`,
 *  so return value is expected to be an empty vector
 *
 *  case 3:
 *  if extended type in extend def is not matched with input type, core dump will come
 *  that means if extend def is `extend CPointer<Bool> {}`, its extended type is `CPointer<Bool>`,
 *  but we set `CPointer<Int32>` as input(`givInstTypes` is {Int32, ...}), ir must be wrong
 */
std::vector<Type*> GetInstTypeParamsForCustomTypeDef(
    const std::vector<Type*>& givInstTypes, const Value& srcFunc)
{
    auto memberFunc = DynamicCast<const FuncBase*>(&srcFunc);
    if (memberFunc == nullptr) {
        memberFunc = GetTopLevelFunc(srcFunc);
    }
    CJC_NULLPTR_CHECK(memberFunc);
    auto parentCustomTypeDef = memberFunc->GetParentCustomTypeDef();
    // only member function need to modify instantiated types
    if (parentCustomTypeDef == nullptr) {
        return std::vector<Type*>{};
    }
    if (auto extendDef = DynamicCast<ExtendDef*>(parentCustomTypeDef)) {
        auto extendedType = extendDef->GetExtendedType();
        auto genericTypeArgs = extendedType->GetTypeArgs();
        auto instTypeArgs =
            std::vector<Type*>(givInstTypes.begin(), givInstTypes.begin() + static_cast<long>(genericTypeArgs.size()));
        std::unordered_map<const GenericType*, Type*> genericMap;
        CJC_ASSERT(genericTypeArgs.size() == instTypeArgs.size());
        for (size_t i = 0; i < genericTypeArgs.size(); ++i) {
            auto newMap = genericTypeArgs[i]->CalculateGenericTyMapping(*instTypeArgs[i]);
            CJC_ASSERT(newMap.first);
            genericMap.merge(newMap.second);
        }
        std::vector<Type*> result;
        for (auto ty : extendDef->GetGenericTypeParams()) {
            result.emplace_back(genericMap.at(ty));
        }
        return result;
    } else {
        size_t offset = parentCustomTypeDef->GetGenericTypeParams().size();
        return std::vector<Type*>(givInstTypes.begin(), givInstTypes.begin() + static_cast<long>(offset));
    }
}

void RemoveGetInstantiateValue(std::vector<Expression*>& maybeGetInsValue)
{
    for (auto expr : maybeGetInsValue) {
        auto getInsValue = DynamicCast<GetInstantiateValue*>(expr);
        if (getInsValue == nullptr) {
            continue;
        }
        auto op = getInsValue->GetGenericResult();
        for (auto user : getInsValue->GetResult()->GetUsers()) {
            user->ReplaceOperand(getInsValue->GetResult(), op);
        }
        expr->RemoveSelfFromBlock();
    }
}

FuncType* GetFuncTypeWithoutThisPtrFromAutoEnvType(ClassType& autoEnvType, CHIRBuilder& builder)
{
    CJC_ASSERT(autoEnvType.IsAutoEnv());
    if (autoEnvType.IsAutoEnvBase()) {
        auto [paramTypes, retType] = GetFuncTypeWithoutThisPtrFromAutoEnvBaseType(autoEnvType);
        return builder.GetType<FuncType>(paramTypes, retType);
    } else {
        return GetFuncTypeWithoutThisPtrFromAutoEnvType(*autoEnvType.GetSuperClassTy(&builder), builder);
    }
}

void PrintMutableVarInfo(const CHIR::Position& pos)
{
    auto posMsg = " in the line:" + std::to_string(pos.line) + " and the column:" + std::to_string(pos.column);
    auto msg = "The mut var" + posMsg + " was boxed";
    std::cout << msg << std::endl;
}

void PrintNestedFuncInfo(const CHIR::Position& pos)
{
    auto posMsg = " in the line:" + std::to_string(pos.line) + " and the column:" + std::to_string(pos.column);
    auto msg = "The nested func" + posMsg + " was closure converted";
    std::cout << msg << std::endl;
}

ClassType* InstantiateAutoEnvBaseType(ClassDef& autoEnvBaseDef, const FuncType& funcType, CHIRBuilder& builder)
{
    std::vector<Type*> typeArgs;
    if (autoEnvBaseDef.TestAttr(Attribute::GENERIC)) {
        typeArgs = funcType.GetTypeArgs();
    }
    return builder.GetType<ClassType>(&autoEnvBaseDef, typeArgs);
}

void LiftCustomDefType(CustomTypeDef& def, TypeConverterForCC& converter)
{
    auto postVisit = [&converter](Expression& e) {
        converter.VisitExpr(e);
        return VisitResult::CONTINUE;
    };
    for (auto method : def.GetMethods()) {
        Visitor::Visit(
            *StaticCast<Func*>(method), [](Expression&) { return VisitResult::CONTINUE; }, postVisit);
        converter.VisitValue(*method);
    }
    converter.VisitDef(def);
}

void CreateVirtualFuncInAutoEnvBaseDef(ClassDef& autoEnvBaseDef,
    const std::string& funcName, const std::vector<Type*>& paramsAndRetType, CHIRBuilder& builder)
{
    std::vector<AbstractMethodParam> params;
    std::vector<Type*> paramTypes{builder.GetType<RefType>(autoEnvBaseDef.GetType())};
    CJC_ASSERT(!paramsAndRetType.empty());
    for (size_t i = 0; i < paramsAndRetType.size() - 1; ++i) {
        params.emplace_back(AbstractMethodParam{"p" + std::to_string(i), paramsAndRetType[i]});
        paramTypes.emplace_back(paramsAndRetType[i]);
    }
    auto methodTy = builder.GetType<FuncType>(paramTypes, paramsAndRetType.back());
    AttributeInfo attr;
    attr.SetAttr(Attribute::ABSTRACT, true);
    attr.SetAttr(Attribute::PUBLIC, true);

    std::string mangleName;
    if (funcName == GENERIC_VIRTUAL_FUNC) {
        mangleName = CHIRMangling::ClosureConversion::GenerateGenericAbstractFuncMangleName(autoEnvBaseDef);
    } else if (funcName == INST_VIRTUAL_FUNC) {
        mangleName = CHIRMangling::ClosureConversion::GenerateInstantiatedAbstractFuncMangleName(autoEnvBaseDef);
    }
    autoEnvBaseDef.AddAbstractMethod(
        AbstractMethodInfo{funcName, autoEnvBaseDef.GetIdentifier() + funcName,
        methodTy, params, attr, AnnoInfo{}, std::vector<GenericType*>{}, false, &autoEnvBaseDef});
}

std::pair<std::vector<Type*>, Type*> CreateFuncTypeWithOrphanT(
    const std::string& typePrefix, const std::vector<Type*>& instFuncTypeArgs,
    const std::unordered_map<const GenericType*, Type*>& replaceTable, CHIRBuilder& builder)
{
    std::vector<Type*> paramTypes;
    CJC_ASSERT(!instFuncTypeArgs.empty());
    for (size_t i = 0; i < instFuncTypeArgs.size() - 1; ++i) {
        auto ty = instFuncTypeArgs[i];
        if (ty->StripAllRefs()->IsGeneric()) {
            paramTypes.emplace_back(ReplaceRawGenericArgType(*ty, replaceTable, builder));
            continue;
        }
        auto tName = "T" + std::to_string(i);
        auto tMangledName = typePrefix + "_cc_" + tName;
        auto genericType = builder.GetType<GenericType>(tMangledName, tName);
        genericType->orphanFlag = true;
        genericType->skipCheck = true;
        genericType->SetUpperBounds(std::vector<Type*>{ReplaceRawGenericArgType(*ty, replaceTable, builder)});
        paramTypes.emplace_back(genericType);
    }
    Type* retType = ReplaceRawGenericArgType(*instFuncTypeArgs.back(), replaceTable, builder);
    if (!retType->StripAllRefs()->IsGeneric()) {
        auto tName = "rT";
        auto tMangledName = typePrefix + "_cc_" + tName;
        auto genericType = builder.GetType<GenericType>(tMangledName, tName);
        genericType->orphanFlag = true;
        genericType->skipCheck = true;
        genericType->SetUpperBounds(std::vector<Type*>{ReplaceRawGenericArgType(*retType, replaceTable, builder)});
        retType = genericType;
    }
    return {paramTypes, retType};
}

std::string GenerateSrcCodeIdentifier(Value& memberVar)
{
    auto memberName = memberVar.GetSrcCodeIdentifier();
    if (memberName.empty()) {
        // let x = 1; let y = { => x }; if compile with `-g`, like `cjc -g xxx.cj`
        // CHIR is:
        // %0: Int64& = Allocate(Int64)
        // %1: Unit = Debug(%0, x)
        // %2: Unit = Store(1, %0)
        // %3: Int64 = Load(%0) --> %3 is captured by lambda expr
        if (auto envLocalVar = DynamicCast<LocalVar*>(&memberVar)) {
            if (auto loadEnvVar = DynamicCast<Load*>(envLocalVar->GetExpr())) {
                memberName = loadEnvVar->GetLocation()->GetSrcCodeIdentifier();
            }
        }
    }
    if (memberName.empty()) {
        memberName = memberVar.GetIdentifier();
    }
    return memberName;
}

void ReplaceEnvVarWithMemberVar(const std::vector<Value*>& capturedValues,
    Expression* firstExpr, Func& globalFunc, Value& curCalss, CHIRBuilder& builder)
{
    uint64_t capturedValueIndex = 0;
    Expression* lastExpr = firstExpr;
    auto parentBlock = firstExpr ? firstExpr->GetParentBlock() : globalFunc.GetEntryBlock();
    for (auto capturedVal : capturedValues) {
        auto envTy = IsMutableVarType(*capturedVal) ? capturedVal->GetType()
                                                    : builder.GetType<RefType>(capturedVal->GetType());
        Expression* field = builder.CreateExpression<GetElementRef>(
            envTy, &curCalss, std::vector<uint64_t>{capturedValueIndex}, parentBlock);
        if (lastExpr == nullptr) {
            globalFunc.GetEntryBlock()->InsertExprIntoHead(*field);
        } else {
            field->MoveAfter(lastExpr);
        }
        lastExpr = field;
        field = builder.CreateExpression<Load>(capturedVal->GetType(), field->GetResult(), parentBlock);
        field->MoveAfter(lastExpr);
        lastExpr = field;
        for (auto user : capturedVal->GetUsers()) {
            if (user->GetTopLevelFunc() != &globalFunc) {
                continue;
            }
            user->ReplaceOperand(capturedVal, field->GetResult());
        }
        CJC_ASSERT(!field->GetResult()->GetUsers().empty());
        ++capturedValueIndex;
    }
}

void ReplaceThisTypeInApplyAndInvoke(
    Lambda& nestedFunc, const std::vector<GenericType*>& genericTypeParams, CHIRBuilder& builder)
{
    if (!NeedAddThisType(*nestedFunc.GetResult())) {
        return;
    }
    CJC_ASSERT(!genericTypeParams.empty());
    auto ThisType = genericTypeParams[0];
    ConvertTypeFunc convertFunc = [&ThisType, &builder](Type& type) {
        // convert `This&` and `This` to generic type which declared in current scope
        return ReplaceThisTypeToConcreteType(type, *ThisType, builder);
    };
    PrivateTypeConverter converter(convertFunc, builder);
    auto postVisit = [&converter](Expression& e) {
        converter.VisitExpr(e);
        return VisitResult::CONTINUE;
    };
    Visitor::Visit(
        *nestedFunc.GetBody(), [](Expression&) { return VisitResult::CONTINUE; }, postVisit);
}

std::vector<GenericType*> CreateGenericTypeParamForAutoEnvImplDef(const Value& srcFunc, CHIRBuilder& builder)
{
    auto visiableGenericTypes = GetVisiableGenericTypes(srcFunc);
    if (NeedAddThisType(srcFunc)) {
        visiableGenericTypes.insert(
            visiableGenericTypes.begin(), builder.GetType<GenericType>("This", GENERIC_THIS_SRC_NAME));
    }
    return visiableGenericTypes;
}

Value* CastTypeFromAutoEnvRefToFuncType(Type& srcFuncType, Value& autoEnvObj,
    Expression& user, const std::vector<Type*>& genericTypeParams, CHIRBuilder& builder)
{
    CJC_ASSERT(srcFuncType.IsFunc());
    auto unboxRetTy = &srcFuncType;
    /**
     * when a class inherit an instantiated class, this child class does not rewrite parent's one function
     * Interface A<T> {
     *     func foo(a: T) {}
     * }
     * class B <: A<Bool> {
     * }
     * In B, we will generate a func B::foo with type T->T to meet consistent function signatures in vtable
     * However when we got a function call B::foo(true), we should get its real function type Bool->Bool,
     *   and type info is stored in orphan flag in generic type.
     */
    unboxRetTy = GetOriFuncTypeFromOrphanType(unboxRetTy, builder);
    if (auto giv = DynamicCast<GetInstantiateValue*>(&user)) {
        auto allInstTypes = giv->GetInstantiateTypes();
        std::unordered_map<const GenericType*, Type*> replaceMap;
        CJC_ASSERT(allInstTypes.size() >= genericTypeParams.size());
        size_t offset = allInstTypes.size() - genericTypeParams.size();
        for (size_t i = offset, j = 0; i < allInstTypes.size(); ++i, ++j) {
            if (auto gTy = DynamicCast<GenericType*>(genericTypeParams[j])) {
                replaceMap.emplace(gTy, allInstTypes[i]);
            }
        }
        /*
            maybe lambda's return type carry with generic type declared in lambda, these generic types must be
            replaced to types declared in current scope
            func foo() {
                func goo<T>(a: T) {a}
                return goo<Int32>
            }
            lambda goo's return type is `T`, we can't generate UnBox like: UnBox(Class-$Auto_Env_xxx&, (T)->T)
            UnBox(Class-$Auto_Env_xxx&, (Int32)->Int32) is correct
        */
        unboxRetTy = ReplaceRawGenericArgType(*unboxRetTy, replaceMap, builder);
    } else {
        /*  we need to get instantiated func type, it must be from $Auto_Env_Base_xxx, but not
            operand of user
            the type of operand may contain generic type which not declared in current scope. e.g.
            func foo<T1>(x: T1) {
                func goo(y: T1): T1 {
                    var a = goo
                    a(y) // there is a `%0 = Load(a)`, and then is `Apply(%0, y)`
                         // type of `a` is `(T1)->T1`, T1 can't be recognized after `func goo` being lifted
                         // to member function, so `T1` must be converted to new generic type which declared in
                         // $Auto_Env_Base_xxx
                }
                goo(x)
            }
        */
        auto autoEnvObjTy = StaticCast<ClassType*>(StaticCast<RefType*>(autoEnvObj.GetType())->GetBaseType());
        unboxRetTy = GetFuncTypeWithoutThisPtrFromAutoEnvType(*autoEnvObjTy, builder);
    }
    // typecast from Class-$Auto_Env_xxx& to func type
    auto castExpr = builder.CreateExpression<UnBox>(unboxRetTy, &autoEnvObj, user.GetParentBlock());
    castExpr->MoveBefore(&user);
    return castExpr->GetResult();
}

void PrintGlobalFuncInfo(const CHIR::Position& pos)
{
    std::string posMsg = " in the line:" + std::to_string(pos.line) + " and the column:" + std::to_string(pos.column);
    auto msg = "The top level func" + posMsg + " was closure converted";
    std::cout << msg << std::endl;
}

void PrintImportedFuncInfo(const ImportedFunc& func)
{
    std::string msg = "The imported func " + func.GetSrcCodeIdentifier() + " from package " +
        func.GetSourcePackageName() + " was closure converted";
    std::cout << msg << std::endl;
}

void ConvertTypeCastToBox(const std::vector<TypeCast*>& typecastExprs, CHIRBuilder& builder)
{
    for (auto e : typecastExprs) {
        auto box = builder.CreateExpression<Box>(e->GetResult()->GetType(), e->GetSourceValue(), e->GetParentBlock());
        e->ReplaceWith(*box);
    }
}

void ConvertTypeCastToUnBox(const std::vector<TypeCast*>& typecastExprs, CHIRBuilder& builder)
{
    for (auto e : typecastExprs) {
        auto unbox =
            builder.CreateExpression<UnBox>(e->GetResult()->GetType(), e->GetSourceValue(), e->GetParentBlock());
        e->ReplaceWith(*unbox);
    }
}

void ConvertBoxToTypeCast(const std::vector<Box*>& boxExprs, CHIRBuilder& builder)
{
    for (auto e : boxExprs) {
        auto typecast =
            builder.CreateExpression<TypeCast>(e->GetResult()->GetType(), e->GetSourceValue(), e->GetParentBlock());
        e->ReplaceWith(*typecast);
    }
}

void ConvertUnBoxToTypeCast(const std::vector<UnBox*>& unboxExprs, CHIRBuilder& builder)
{
    for (auto e : unboxExprs) {
        auto typecast =
            builder.CreateExpression<TypeCast>(e->GetResult()->GetType(), e->GetSourceValue(), e->GetParentBlock());
        e->ReplaceWith(*typecast);
    }
}

bool ApplyNeedConvertToInvoke(Expression& e)
{
    if (e.GetExprKind() != CHIR::ExprKind::APPLY) {
        return false;
    }
    /*
        if callee is local var or parameter, not a function, we need to replace Apply expr with Invoke expr
        func foo(a: ()->Unit) {
            a() // callee `a` is paramter, `Apply(a)` need be converted to `Invoke(a)`
                // note: except for CFunc type
        }
    */
    auto apply = StaticCast<Apply*>(&e);
    return !apply->GetCallee()->IsFunc() && apply->GetCallee()->GetType()->IsCJFunc();
}

bool ApplyWithExceptionNeedConvertToInvokeWithException(Expression& e)
{
    if (e.GetExprKind() != CHIR::ExprKind::APPLY_WITH_EXCEPTION) {
        return false;
    }
    auto apply = StaticCast<ApplyWithException*>(&e);
    return !apply->GetCallee()->IsFunc() && apply->GetCallee()->GetType()->IsCJFunc();
}

bool TypeCastNeedConvertToBox(Expression& e)
{
    if (e.GetExprKind() != CHIR::ExprKind::TYPECAST) {
        return false;
    }
    // TypeCast(value type, func type) need to be converted to Box(value type, Class-$Auto_Env_xxx&)
    // ps: value type is non-func type, func type is not CFunc type
    auto typecast = StaticCast<TypeCast*>(&e);
    return typecast->GetTargetTy()->IsCJFunc() && !typecast->GetSourceTy()->IsCJFunc();
}

bool TypeCastNeedConvertToUnBox(Expression& e)
{
    if (e.GetExprKind() != CHIR::ExprKind::TYPECAST) {
        return false;
    }
    // TypeCast(func type, value type) need to be converted to UnBox(Class-$Auto_Env_xxx&, value type)
    // ps: value type is non-func type, func type is not CFunc type
    auto typecast = StaticCast<TypeCast*>(&e);
    return typecast->GetSourceTy()->IsCJFunc() && !typecast->GetTargetTy()->IsCJFunc();
}

bool BoxNeedConvertToTypeCast(Expression& e)
{
    if (e.GetExprKind() != CHIR::ExprKind::BOX) {
        return false;
    }
    // Box(func type, ref type) need to be converted to TypeCast(Class-$Auto_Env_xxx&, ref type)
    // ps: func type is not CFunc type
    auto box = StaticCast<Box*>(&e);
    return box->GetSourceTy()->IsCJFunc();
}

bool UnBoxNeedConvertToTypeCast(Expression& e)
{
    if (e.GetExprKind() != CHIR::ExprKind::UNBOX) {
        return false;
    }
    // UnBox(ref type, func type) need to be converted to TypeCast(ref type, Class-$Auto_Env_xxx&)
    // ps: func type is not CFunc type
    auto box = StaticCast<UnBox*>(&e);
    return box->GetTargetTy()->IsCJFunc();
}

bool ApplyRetValNeedWrapper(Expression& e)
{
    if (e.GetExprKind() != CHIR::ExprKind::APPLY) {
        return false;
    }
    auto& apply = StaticCast<Apply&>(e);
    auto calleeType = apply.GetCallee()->GetType();
    if (!calleeType->IsCJFunc()) {
        return false;
    }
    /** func foo<T>(a: ()->T): ()->T {
     *      return a
     *  }
     *  func goo(): Bool {
     *      return true
     *  }
     *  var x: ()->Bool = foo<Bool>(goo)
     *  return type of func foo is `()->T`, but x's type is ()->Bool
     *  closure type of `()->T` is Class-$AutoEnvGenericBase, closure type of `()->Bool` is Class-$AutoEnvInstBase
     *  Class-$AutoEnvGenericBase is parent type of Class-$AutoEnvInstBase
     *  we can't cast type from parent type to sub type, a wrapper class is needed
     */
    auto declaredRetType = StaticCast<FuncType*>(calleeType)->GetReturnType()->StripAllRefs();
    auto applyRetType = apply.GetResult()->GetType()->StripAllRefs();
    return !declaredRetType->IsAutoEnvInstBase() && applyRetType->IsAutoEnvInstBase();
}

std::vector<size_t> OperandNeedTypeCast(
    const std::vector<Value*>& args, const std::vector<Type*>& paramTypes, size_t offset)
{
    std::vector<size_t> index;
    CJC_ASSERT(args.size() == paramTypes.size());
    for (size_t i = 0; i < paramTypes.size(); ++i) {
        auto paramTy = paramTypes[i]->StripAllRefs();
        auto argTy = args[i]->GetType()->StripAllRefs();
        if (!paramTy->IsAutoEnvBase() || !argTy->IsAutoEnvBase()) {
            continue;
        }
        auto paramTyDef = StaticCast<ClassType*>(paramTy)->GetClassDef();
        auto argTyDef = StaticCast<ClassType*>(argTy)->GetClassDef();
        // only typecast when argTy is Auto_Env_instBase and paramTy is Auto_Env_genericBase
        if (argTyDef->GetSuperClassDef() == paramTyDef) {
            index.emplace_back(i + offset);
        }
    }

    return index;
}

std::pair<bool, std::vector<size_t>> ApplyArgNeedTypeCast(const Apply& e)
{
    std::vector<size_t> index;
    auto calleeType = e.GetCallee()->GetType();
    if (!calleeType->IsCJFunc()) {
        return {false, index};
    }
    /** func foo<T>(a: ()->T): Int32 {
     *      return true
     *  }
     *  func goo(): Bool {
     *      return true
     *  }
     *  var x: Int32 = foo<Bool>(goo)
     *  closure type of `()->T` is Class-$AutoEnvGenericBase, closure type of `()->Bool` is Class-$AutoEnvInstBase
     *  Class-$AutoEnvGenericBase is parent type of Class-$AutoEnvInstBase
     *  we can cast type from sub type to parent type
     */
    auto args = e.GetArgs();
    auto paramTypes = StaticCast<FuncType*>(calleeType)->GetParamTypes();
    index = OperandNeedTypeCast(args, paramTypes, 1);
    return {!index.empty(), index};
}

bool ApplyWithExceptionRetValNeedWrapper(const ApplyWithException& e)
{
    auto calleeType = e.GetCallee()->GetType();
    if (!calleeType->IsCJFunc()) {
        return false;
    }
    auto declaredRetType = StaticCast<FuncType*>(calleeType)->GetReturnType()->StripAllRefs();
    auto applyRetType = e.GetResult()->GetType()->StripAllRefs();
    return !declaredRetType->IsAutoEnvInstBase() && applyRetType->IsAutoEnvInstBase();
}

bool InvokeRetValNeedWrapper(const Expression& e)
{
    if (e.GetExprKind() != CHIR::ExprKind::INVOKE && e.GetExprKind() != CHIR::ExprKind::INVOKESTATIC) {
        return false;
    }
    // same reason with Apply
    Type* declaredRetType = nullptr;
    if (auto invoke = DynamicCast<const Invoke*>(&e)) {
        declaredRetType = invoke->GetMethodType()->GetReturnType()->StripAllRefs();
    } else {
        declaredRetType = StaticCast<const InvokeStatic&>(e).GetMethodType()->GetReturnType()->StripAllRefs();
    }
    auto invokeRetType = e.GetResult()->GetType()->StripAllRefs();
    return !declaredRetType->IsAutoEnvInstBase() && invokeRetType->IsAutoEnvInstBase();
}

bool InvokeWithExceptionRetValNeedWrapper(const Expression& e)
{
    if (e.GetExprKind() != CHIR::ExprKind::INVOKE_WITH_EXCEPTION &&
        e.GetExprKind() != CHIR::ExprKind::INVOKESTATIC_WITH_EXCEPTION) {
        return false;
    }
    // same reason with Apply
    Type* declaredRetType = nullptr;
    if (auto invoke = DynamicCast<const InvokeWithException*>(&e)) {
        declaredRetType = invoke->GetMethodType()->GetReturnType()->StripAllRefs();
    } else {
        declaredRetType =
            StaticCast<const InvokeStaticWithException>(e).GetMethodType()->GetReturnType()->StripAllRefs();
    }
    auto invokeRetType = e.GetResult()->GetType()->StripAllRefs();
    return !declaredRetType->IsAutoEnvInstBase() && invokeRetType->IsAutoEnvInstBase();
}

bool GetElementRefRetValNeedWrapper(const Expression& e, CHIRBuilder& builder)
{
    if (e.GetExprKind() != CHIR::ExprKind::GET_ELEMENT_REF) {
        return false;
    }
    const auto& getEleRef = StaticCast<const GetElementRef&>(e);
    auto getEleRefRetType = getEleRef.GetResult()->GetType()->StripAllRefs();
    // only Auto_Env_InstBase need wrapper
    if (!getEleRefRetType->IsAutoEnvInstBase()) {
        return false;
    }
    // if it's Tuple<xxx>&, we don't care for now, only care class type, struct type and enum type
    auto locationType = DynamicCast<CustomType*>(getEleRef.GetLocation()->GetType()->StripAllRefs());
    if (locationType == nullptr) {
        return false;
    }
    /** class A<T> {
     *      var a: ()->T
     *  }
     *  var b: ()->Bool = A<Bool>().a
     *
     *  a's type is `()->T`, but b's type is ()->Bool
     *  closure type of `()->T` is Class-$AutoEnvGenericBase, closure type of `()->Bool` is Class-$AutoEnvInstBase
     *  Class-$AutoEnvGenericBase is parent type of Class-$AutoEnvInstBase
     *  we can't cast type from parent type to sub type, a wrapper class is needed
     */
    auto locationDef = locationType->GetCustomTypeDef();
    Type* declaredType = locationDef->GetType();
    for (auto& idx : getEleRef.GetPath()) {
        declaredType = GetFieldOfType(*declaredType, idx, builder);
        CJC_NULLPTR_CHECK(declaredType);
    }
    declaredType = declaredType->StripAllRefs();
    return !declaredType->IsAutoEnvInstBase();
}

Type* GetFieldBaseType(const Value& base)
{
    if (!base.IsLocalVar()) {
        return base.GetType();
    }
    auto expr = StaticCast<const LocalVar&>(base).GetExpr();
    if (expr->GetExprKind() != CHIR::ExprKind::TYPECAST) {
        return base.GetType();
    }
    // there may be a typecast from enum type to tuple type:
    // %1: Enum-Option<xxx> = xxx
    // %2: Tuple<Bool, xxx> = TypeCast(%1, Tuple<Bool, xxx>)
    // this typecast is for codegen, expect to be removed later
    auto srcValType = StaticCast<TypeCast*>(expr)->GetSourceValue()->GetType();
    if (srcValType->IsEnum()) {
        return nullptr;
    } else {
        return base.GetType();
    }
}

bool FieldRetValNeedWrapper(const Expression& e, CHIRBuilder& builder)
{
    if (e.GetExprKind() != CHIR::ExprKind::FIELD) {
        return false;
    }
    const auto& field = StaticCast<const Field&>(e);
    auto fieldRetType = field.GetResult()->GetType()->StripAllRefs();
    // only Auto_Env_InstBase need wrapper
    if (!fieldRetType->IsAutoEnvInstBase()) {
        return false;
    }

    Type* declaredType = GetFieldBaseType(*field.GetBase());
    if (declaredType == nullptr) {
        return true;
    }
    // same reason with `GetElementRef`
    for (auto& idx : field.GetPath()) {
        declaredType = GetFieldOfType(*declaredType, idx, builder);
        CJC_NULLPTR_CHECK(declaredType);
    }
    declaredType = declaredType->StripAllRefs();
    return !declaredType->IsAutoEnvInstBase();
}

bool IsAutoEnvGenericType(const Type& type)
{
    if (!type.IsAutoEnv()) {
        return false;
    }
    if (type.IsAutoEnvGenericBase()) {
        return true;
    }
    return StaticCast<const ClassType&>(type).GetClassDef()->GetMethods().size() == 1;
}

bool TypeCastSrcValNeedWrapper(const Expression& e)
{
    if (e.GetExprKind() != CHIR::ExprKind::TYPECAST) {
        return false;
    }
    /** Class-$AutoEnvGenericBase is parent type of Class-$AutoEnvInstBase
     *  we can't cast type from parent type to sub type, a wrapper class is needed
     */
    const auto& typecast = StaticCast<const TypeCast&>(e);
    auto srcType = typecast.GetSourceTy()->StripAllRefs();
    auto targetType = typecast.GetTargetTy()->StripAllRefs();
    return IsAutoEnvGenericType(*srcType) && targetType->IsAutoEnvInstBase();
}

std::pair<bool, std::vector<size_t>> ApplyWithExceptionArgNeedTypeCast(const ApplyWithException& e)
{
    std::vector<size_t> index;
    const auto& apply = StaticCast<const ApplyWithException&>(e);
    auto calleeType = apply.GetCallee()->GetType();
    if (!calleeType->IsCJFunc()) {
        return {false, index};
    }
    auto args = apply.GetArgs();
    auto paramTypes = StaticCast<FuncType*>(calleeType)->GetParamTypes();
    index = OperandNeedTypeCast(args, paramTypes, 1);
    return {!index.empty(), index};
}

std::pair<bool, std::vector<size_t>> EnumConstructorNeedTypeCast(const Expression& e)
{
    std::vector<size_t> index;
    if (e.GetExprKind() != CHIR::ExprKind::TUPLE) {
        return {false, index};
    }
    const auto& tuple = StaticCast<const Tuple&>(e);
    auto resultType = tuple.GetResult()->GetType();
    if (!resultType->IsEnum()) {
        return {false, index};
    }
    auto enumDef = StaticCast<EnumType*>(resultType)->GetEnumDef();
    auto idxExpr = StaticCast<Constant*>(StaticCast<LocalVar*>(tuple.GetOperand(0))->GetExpr());
    size_t idx = 0;
    if (idxExpr->IsIntLit()) {
        idx = idxExpr->GetUnsignedIntLitVal();
    } else if (idxExpr->IsBoolLit()) {
        idx = idxExpr->GetBoolLitVal() ? 1 : 0;
    } else {
        CJC_ABORT();
    }
    /** enum E<T> {
     *      A(()->T)
     *  }
     *  %0: E<Bool> = Tuple(0, ()->Bool)
     *  param type of E.A is `()->T`, but Tuple arg's type is `()->Bool`, a TypeCast is needed
     *  closure type of `()->T` is Class-$AutoEnvGenericBase, closure type of `()->Bool` is Class-$AutoEnvInstBase
     *  Class-$AutoEnvGenericBase is parent type of Class-$AutoEnvInstBase
     *  we can cast type from sub type to parent type
     */
    auto constructor = enumDef->GetCtor(idx);
    auto paramTypes = constructor.funcType->GetParamTypes();
    auto args = tuple.GetOperands();
    args.erase(args.begin());
    index = OperandNeedTypeCast(args, paramTypes, 1);
    return {!index.empty(), index};
}

std::pair<bool, std::vector<size_t>> TupleNeedTypeCast(const Expression& e)
{
    std::vector<size_t> index;
    if (e.GetExprKind() != CHIR::ExprKind::TUPLE) {
        return {false, index};
    }
    const auto& tuple = StaticCast<const Tuple&>(e);
    auto resultType = tuple.GetResult()->GetType();
    if (!resultType->IsTuple()) {
        return {false, index};
    }
    /** func foo<T>(a:(Bool, ()->T)) {}
     *  func goo(): Bool { true }
     *  foo<Bool>((true, goo))
     *
     *  closure type of `()->T` is Class-$AutoEnvGenericBase, closure type of `()->Bool` is Class-$AutoEnvInstBase
     *  Class-$AutoEnvGenericBase is parent type of Class-$AutoEnvInstBase
     *  we can cast type from sub type to parent type
     */
    auto expectedTypes = StaticCast<TupleType*>(resultType)->GetElementTypes();
    auto args = tuple.GetOperands();
    index = OperandNeedTypeCast(args, expectedTypes, 0);
    return {!index.empty(), index};
}

std::pair<bool, std::vector<size_t>> RawArrayInitByValueNeedTypeCast(const Expression& e)
{
    std::vector<size_t> index;
    if (e.GetExprKind() != CHIR::ExprKind::RAW_ARRAY_INIT_BY_VALUE) {
        return {false, index};
    }
    /** %0: Int64 = Constant(1)
     *  %1: RawArray<$AutoEnvGenericBase<Bool>>& = RawArrayAllocate(RawArray<$AutoEnvGenericBase<Unit>>, %0)
     *  %2: $AutoEnvInstBase_Bool = xxx
     *  %3: Unit = RawArrayInitByValue(%1, %0, %2)
     *
     *  in `RawArrayInitByValue`, type between %1 and %2 is mismatched,
     *  $AutoEnvInstBase_Bool is sub type of $AutoEnvGenericBase<Bool>, we can cast type from sub type to parent type
     */
    const auto& init = StaticCast<const RawArrayInitByValue&>(e);
    auto rawArrayType = StaticCast<RawArrayType*>(init.GetRawArray()->GetType()->StripAllRefs());
    auto expectedType = std::vector<Type*>{rawArrayType->GetElementType()};
    auto initValue = std::vector<Value*>{init.GetInitValue()};
    const size_t opOffset = 2;
    index = OperandNeedTypeCast(initValue, expectedType, opOffset);
    return {!index.empty(), index};
}

void AddTypeCastForOperand(const std::pair<Expression*, std::vector<size_t>>& e, CHIRBuilder& builder)
{
    auto expr = e.first;
    auto parentBlock = expr->GetParentBlock();
    for (auto idx : e.second) {
        auto op = expr->GetOperand(idx);
        auto expectedType = builder.GetType<RefType>(
            StaticCast<ClassType*>(op->GetType()->StripAllRefs())->GetClassDef()->GetSuperClassTy());
        auto typecast = builder.CreateExpression<TypeCast>(expectedType, op, parentBlock);
        typecast->MoveBefore(expr);
        expr->ReplaceOperand(idx, typecast->GetResult());
    }
}

std::pair<bool, std::vector<size_t>> SpawnNeedTypeCast(const Expression& e)
{
    std::vector<size_t> index;
    if (e.GetExprKind() != CHIR::ExprKind::SPAWN) {
        return {false, index};
    }
    const auto& spawn = StaticCast<const Spawn&>(e);
    if (!spawn.IsExecuteClosure()) {
        // only check executeClosure, skip future Execute
        return {false, index};
    }
    /** cj code `spawn { 0 }` is translated to
     *  %0: ()->Int64 = lambda { reteurn 0 }
     *  %1: Future<Int64> = Spawn(%0)
     *
     *  after redundant future removing, codegen is expected to use Future.executeClosure which declared in std.core
     *  however, param type of `executeClosure` is `()->T`, it has gap to %0 which type is `()->Int64`
     *  closure type of `()->T` is Class-$AutoEnvGenericBase, closure type of `()->Int64` is Class-$AutoEnvInstBase
     *  Class-$AutoEnvGenericBase is parent type of Class-$AutoEnvInstBase
     *  we can cast type from sub type to parent type
     */
    auto funcType = spawn.GetExecuteClosure()->GetFuncType();
    index = OperandNeedTypeCast(spawn.GetOperands(), funcType->GetParamTypes(), 0);
    return {!index.empty(), index};
}

void ReplaceOperandWithAutoEnvWrapperClass(
    Value& op, Value& autoEnvWrapperClass, const std::unordered_set<Expression*>& skip)
{
    for (auto user : op.GetUsers()) {
        if (skip.find(user) != skip.end() || user->GetExprKind() == CHIR::ExprKind::INSTANCEOF) {
            continue;
        }
        user->ReplaceOperand(&op, &autoEnvWrapperClass);
    }
}
} // namespace

ClosureConversion::ClosureConversion(Package& package, CHIRBuilder& builder, const GlobalOptions& opts,
    const std::unordered_set<Func*>& srcCodeImportedFuncs)
    : package(package),
      builder(builder),
      objClass(*builder.GetObjectTy()->GetClassDef()),
      opts(opts),
      srcCodeImportedFuncs(srcCodeImportedFuncs)
{
}

std::set<std::string> ClosureConversion::GetCCOutFuncsRawMangle() const
{
    return ccOutFuncsRawMangle;
}

std::unordered_set<ClassDef*> ClosureConversion::GetUselessClassDef() const
{
    return uselessClasses;
}

std::unordered_set<Func*> ClosureConversion::GetUselessLambda() const
{
    return uselessLambda;
}

std::vector<Lambda*> ClosureConversion::CollectNestedFunctions()
{
    std::vector<Lambda*> nestedFuncs;
    Func* curOutFunc = nullptr;
    auto preVisit = [&nestedFuncs, &curOutFunc, this](Expression& e) {
        if (auto lambdaExpr = DynamicCast<Lambda*>(&e); lambdaExpr) {
            // maybe there is another lambda in current lambda's body, but we can't visit another lambda
            // by walking Func node, so we need to walk current lambda body manually
            CollectNestedLambdaExpr(*lambdaExpr, nestedFuncs, curOutFunc, ccOutFuncsRawMangle);
            // we must collect inner lambda first, and then outer lambda,
            // because we need to lift lambda from inner to outer
            nestedFuncs.emplace_back(lambdaExpr);
            if (opts.enIncrementalCompilation) {
                if (!curOutFunc->GetRawMangledName().empty()) {
                    ccOutFuncsRawMangle.emplace(curOutFunc->GetRawMangledName());
                }
            }
        }
        return VisitResult::CONTINUE;
    };
    for (auto func : package.GetGlobalFuncs()) {
        if (func->TestAttr(Attribute::SKIP_ANALYSIS)) {
            continue;
        }
        curOutFunc = func;
        VisitFuncBlocksInTopoSort(*func->GetBody(), preVisit);
    }
    return nestedFuncs;
}

std::string ClosureConversion::GenerateGlobalFuncIdentifier(const Lambda& lambda)
{
    auto it = duplicateLambdaName.find(lambda.GetIdentifier());
    CJC_ASSERT(it != duplicateLambdaName.end());
    if (it->second == 0) {
        return lambda.GetIdentifier();
    } else {
        return lambda.GetIdentifier() + "$" + std::to_string(it->second);
    }
}

Ptr<LocalVar> ClosureConversion::CreateAutoEnvImplObject(
    Block& parent, ClassType& autoEnvImplType, const std::vector<Value*>& envs, Expression& user, Value& srcFunc)
{
    auto instantiateClassTy = GenerateInstantiatedClassType(autoEnvImplType, user, srcFunc);
    auto refType = builder.GetType<RefType>(instantiateClassTy);
    Expression* expr = builder.CreateExpression<Allocate>(refType, instantiateClassTy, &parent);
    auto obj = expr->GetResult();
    uint64_t index = 0;
    for (auto env : envs) {
        auto store = builder.CreateExpression<StoreElementRef>(
            builder.GetUnitTy(), env, obj, std::vector<uint64_t>{index}, &parent);
        store->MoveBefore(&user);
        index++;
    }
    // if not move to head, in some cases which call lambda recursively, we will get unreachable expression
    // func foo1() {
    //     func foo2(a!: Int32 = 1) { foo2(); return 1}
    //     foo2()
    // }
    parent.InsertExprIntoHead(*expr);

    return expr->GetResult();
}

ClassType* ClosureConversion::GenerateInstantiatedClassType(
    ClassType& autoEnvImplType, const Expression& user, Value& srcFunc)
{
    // The instantiated type args include two parts:
    // 1) args for the generic param of the outer scope decls
    // 2) args for the generic param of func itself
    auto visibleGenericTypes = GetVisiableGenericTypes(srcFunc);
    auto autoEnvInstTypeArgs = std::vector<Type*>(visibleGenericTypes.begin(), visibleGenericTypes.end());
    if (auto giv = DynamicCast<const GetInstantiateValue*>(&user); giv) {
        auto givInstTypes = giv->GetInstantiateTypes();
        auto funcGenericParamNum = GetFuncGenericTypeParamNum(srcFunc);
        CJC_ASSERT(funcGenericParamNum <= autoEnvInstTypeArgs.size());
        CJC_ASSERT(funcGenericParamNum <= givInstTypes.size());
        size_t instArgsOffset = autoEnvInstTypeArgs.size() - funcGenericParamNum;
        size_t givInstTysOffset = givInstTypes.size() - funcGenericParamNum;
        for (size_t i = 0; i < funcGenericParamNum; ++i) {
            autoEnvInstTypeArgs[instArgsOffset + i] = givInstTypes[givInstTysOffset + i];
        }
        auto instTypeParams = GetInstTypeParamsForCustomTypeDef(givInstTypes, srcFunc);
        for (size_t i = 0; i < instTypeParams.size(); ++i) {
            autoEnvInstTypeArgs[i] = instTypeParams[i];
        }
    } else if (auto apply = DynamicCast<const Apply*>(&user); apply && apply->GetCallee() == &srcFunc) {
        auto& partInstTys = apply->GetInstantiatedTypeArgs();
        CJC_ASSERT(autoEnvInstTypeArgs.size() >= partInstTys.size());
        size_t offset = autoEnvInstTypeArgs.size() - partInstTys.size();
        for (size_t i = offset, j = 0; i < autoEnvInstTypeArgs.size(); ++i, ++j) {
            autoEnvInstTypeArgs[i] = partInstTys[j];
        }
    } else if (auto awe = DynamicCast<const ApplyWithException*>(&user); awe && awe->GetCallee() == &srcFunc) {
        auto& partInstTys = awe->GetInstantiatedTypeArgs();
        CJC_ASSERT(autoEnvInstTypeArgs.size() >= partInstTys.size());
        size_t offset = autoEnvInstTypeArgs.size() - partInstTys.size();
        for (size_t i = offset, j = 0; i < autoEnvInstTypeArgs.size(); ++i, ++j) {
            autoEnvInstTypeArgs[i] = partInstTys[j];
        }
    }
    if (NeedAddThisType(srcFunc)) {
        autoEnvInstTypeArgs.insert(autoEnvInstTypeArgs.begin(), builder.GetType<ThisType>());
    }

    CJC_ASSERT(autoEnvImplType.GetGenericArgs().size() == autoEnvInstTypeArgs.size());
    return builder.GetType<ClassType>(autoEnvImplType.GetClassDef(), autoEnvInstTypeArgs);
}

ClassDef* ClosureConversion::CreateBoxClassDef(Type& type)
{
    auto it = boxClassMap.find(&type);
    if (it != boxClassMap.end()) {
        return it->second;
    }

    auto className = "$Captured_" + type.ToString();
    std::vector<Type*> genericArgTypes;
    GetAllGenericType(type, genericArgTypes);
    auto mangledName = MangleUtils::GetMangledNameOfCompilerAddedClass(className);
    auto classDef =
        builder.CreateClass(INVALID_LOCATION, className, mangledName, package.GetName(), true, false);
    auto classTy = builder.GetType<ClassType>(classDef, genericArgTypes);
    classDef->SetType(*classTy);
    classDef->SetSuperClassTy(*StaticCast<ClassType*>(objClass.GetType()));
    classDef->Set<IsCapturedClassInCC>(true);
    classDef->EnableAttr(Attribute::COMPILER_ADD);
    classDef->EnableAttr(Attribute::NO_REFLECT_INFO);
    if (!genericArgTypes.empty()) {
        classDef->EnableAttr(Attribute::GENERIC);
    }

    AttributeInfo attributeInfo;
    attributeInfo.SetAttr(Attribute::PUBLIC, true);
    classDef->AddInstanceVar(MemberVarInfo{"value", "", &type, attributeInfo, INVALID_LOCATION});

    boxClassMap.emplace(&type, classDef);
    return classDef;
}

LocalVar* ClosureConversion::CreateBoxClassObj(const LocalVar& env, const ClassDef& classDef)
{
    auto& loc = env.GetExpr()->GetDebugLocation();
    auto classTy = classDef.GetType();
    auto retTy = builder.GetType<RefType>(classTy);
    auto obj = builder.CreateExpression<Allocate>(loc, retTy, classTy, env.GetExpr()->GetParentBlock());
    obj->MoveBefore(env.GetExpr());

    return obj->GetResult();
}

void ClosureConversion::ReplaceEnvWithBoxObjMemberVar(LocalVar& env, LocalVar& boxObj, LocalVar& lValue)
{
    auto debugExpr = env.GetDebugExpr();
    for (auto user : env.GetUsers()) {
        if (user->GetExprKind() == CHIR::ExprKind::STORE) {
            auto storeNode = StaticCast<Store*>(user);
            auto base = storeNode->GetValue();
            auto value = builder.CreateExpression<StoreElementRef>(
                builder.GetUnitTy(), base, &boxObj, std::vector<uint64_t>{0}, user->GetParentBlock());
            value->MoveBefore(user);
            user->RemoveSelfFromBlock();
            continue;
        }
        auto value = builder.CreateExpression<GetElementRef>(
            env.GetType(), &boxObj, std::vector<uint64_t>{0}, user->GetParentBlock());
        value->MoveBefore(user);
        user->ReplaceOperand(&env, value->GetResult());
    }
    if (debugExpr != nullptr) {
        auto loc = debugExpr->GetDebugLocation();
        auto newDebug = builder.CreateExpression<Debug>(
            loc, builder.GetUnitTy(), &lValue, debugExpr->GetSrcCodeIdentifier(), debugExpr->GetParentBlock());
        newDebug->MoveBefore(debugExpr);
        debugExpr->RemoveSelfFromBlock();
    }
    if (opts.chirDebugOptimizer) {
        PrintMutableVarInfo(env.GetExpr()->GetDebugLocation().GetBeginPos());
    }
    env.GetExpr()->RemoveSelfFromBlock();
}

std::pair<LocalVar*, LocalVar*> ClosureConversion::SetBoxClassAsMutableVar(LocalVar& rValue)
{
    auto retTy = builder.GetType<RefType>(rValue.GetType());
    auto lValue = builder.CreateExpression<Allocate>(retTy, rValue.GetType(), rValue.GetExpr()->GetParentBlock());
    lValue->MoveAfter(rValue.GetExpr());
    auto asg =
        builder.CreateExpression<Store>(builder.GetUnitTy(), &rValue, lValue->GetResult(), lValue->GetParentBlock());
    asg->MoveAfter(lValue);
    auto load = builder.CreateExpression<Load>(rValue.GetType(), lValue->GetResult(), asg->GetParentBlock());
    load->MoveAfter(asg);
    return {lValue->GetResult(), load->GetResult()};
}

Value* ClosureConversion::BoxMutableVar(LocalVar& env)
{
    // let a: Class-$BOX_xxx = $BOX_xxx()
    auto classDef = CreateBoxClassDef(*StaticCast<RefType*>(env.GetType())->GetBaseType());
    auto boxObj = CreateBoxClassObj(env, *classDef);
    LocalVar* allocRes = boxObj;
    // if not use `-g`, we generate less expressions to improve runtime performance
    if (opts.enableCompileDebug) {
        // var a: Class-$BOX_xxx = $BOX_xxx()
        auto [lValue, rValue] = SetBoxClassAsMutableVar(*boxObj);
        allocRes = lValue;
        boxObj = rValue;
    }
    ReplaceEnvWithBoxObjMemberVar(env, *boxObj, *allocRes);

    return boxObj;
}

std::vector<Value*> ClosureConversion::BoxAllMutableVars(const std::vector<Value*>& rawEnvs)
{
    std::vector<Value*> boxedEnvs;
    for (auto env : rawEnvs) {
        if (IsMutableVarType(*env)) {
            // only LocalVar and Parameter can be environment, but Parameter is read only, can't be mutable var
            CJC_ASSERT(env->IsLocalVar());
            boxedEnvs.emplace_back(BoxMutableVar(*StaticCast<LocalVar*>(env)));
        } else {
            boxedEnvs.emplace_back(env);
        }
    }

    return boxedEnvs;
}

void ClosureConversion::LiftNestedFunctionWithCFuncType(Lambda& nestedFunc)
{
    const auto& loc = nestedFunc.GetResult()->GetDebugLocation();
    // move lambda to global function
    auto globalFuncId = GenerateGlobalFuncIdentifier(nestedFunc);
    auto genericParamTypes = nestedFunc.GetGenericTypeParams();
    auto globalFunc = builder.CreateFunc(loc, StaticCast<FuncType*>(nestedFunc.GetResult()->GetType()), globalFuncId,
        nestedFunc.GetSrcCodeIdentifier(), "", package.GetName(), genericParamTypes);
    // After globalFunc is used, new localId needs to be generated based on the ID in oldFunc during subsequent
    // optimization. Otherwise, duplicate IDs may exist. Therefore, the ID in oldFunc is transferred to globalFunc.
    CJC_NULLPTR_CHECK(nestedFunc.GetBody()->GetTopLevelFunc());
    globalFunc->InheritIDFromFunc(*nestedFunc.GetBody()->GetTopLevelFunc());
    globalFunc->InitBody(*nestedFunc.GetBody());
    SetLiftedLambdaAttr(*globalFunc, nestedFunc);
    auto sigInfo = FuncSigInfo{
        .funcName = nestedFunc.GetSrcCodeIdentifier(),
        .funcType = nestedFunc.GetFuncType(),
        .genericTypeParams = nestedFunc.GetGenericTypeParams()
    };
    globalFunc->SetOriginalLambdaInfo(sigInfo);
    globalFunc->SetReturnValue(*nestedFunc.GetReturnValue());
    convertedCache.emplace(&nestedFunc, globalFunc);

    // set args of global function
    for (auto arg : nestedFunc.GetParams()) {
        globalFunc->AddParam(*arg);
    }

    for (auto user : nestedFunc.GetResult()->GetUsers()) {
        user->ReplaceOperand(nestedFunc.GetResult(), globalFunc);
    }
    nestedFunc.RemoveSelfFromBlock();
}

void ClosureConversion::RecordDuplicateLambdaName(const Lambda& func)
{
    auto it = duplicateLambdaName.find(func.GetIdentifier());
    if (it == duplicateLambdaName.end()) {
        duplicateLambdaName.emplace(func.GetIdentifier(), 0);
    } else {
        it->second++;
    }
}

bool ClosureConversion::LambdaCanBeInlined(const Expression& user, const FuncBase& lambda)
{
    if (user.GetExprKind() != CHIR::ExprKind::APPLY) {
        return false;
    }
    auto callee = StaticCast<const Apply&>(user).GetCallee();
    // must be callee of apply
    if (&lambda != callee) {
        return false;
    }

    bool isRecursive = false;
    auto preVisit = [&callee, &isRecursive](Expression& e) {
        if (e.GetExprKind() == CHIR::ExprKind::APPLY && StaticCast<Apply&>(e).GetCallee() == callee) {
            isRecursive = true;
            return VisitResult::STOP;
        } else if (e.GetExprKind() == CHIR::ExprKind::APPLY_WITH_EXCEPTION &&
            StaticCast<ApplyWithException&>(e).GetCallee() == callee) {
            isRecursive = true;
            return VisitResult::STOP;
        }
        return VisitResult::CONTINUE;
    };
    Visitor::Visit(*VirtualCast<Func*>(callee), preVisit);

    // recursive function doesn't need to be inlined
    return !isRecursive;
}

void ClosureConversion::DoFunctionInlineForLambda()
{
    if (!opts.IsOptimizationExisted(GlobalOptions::OptimizationFlag::FUNC_INLINING)) {
        return;
    }
    auto funcInline = FunctionInline(builder, GlobalOptions::OptimizationLevel::O2, opts.chirDebugOptimizer);
    for (auto& it : autoEnvImplDefs) {
        auto methods = it.second->GetMethods();
        for (auto lambda : it.second->GetMethods()) {
            // after inlining generic virtual func, we should remvoe Box and Unbox expr, we will do it later
            if (lambda->GetSrcCodeIdentifier() == GENERIC_VIRTUAL_FUNC) {
                continue;
            }
            for (auto user : lambda->GetUsers()) {
                if (LambdaCanBeInlined(*user, *lambda)) {
                    funcInline.DoFunctionInline(*StaticCast<Apply*>(user), "functionInlineForLambda");
                }
            }
        }
    }
}

ClassDef* ClosureConversion::GetOrCreateAutoEnvBaseDef(const FuncType& funcType)
{
    auto autoEnvBaseDef = GetOrCreateGenericAutoEnvBaseDef(funcType.GetParamTypes().size());
    if (!funcType.IsGenericRelated()) {
        autoEnvBaseDef = GetOrCreateInstAutoEnvBaseDef(funcType, *autoEnvBaseDef);
    }
    return autoEnvBaseDef;
}

void ClosureConversion::InlineLambda(const std::vector<Lambda*>& funcs)
{
    LambdaInline(builder, opts).InlineLambda(funcs);
}

void ClosureConversion::ConvertNestedFunctions(const std::vector<Lambda*>& funcs)
{
    for (auto func : funcs) {
        RecordDuplicateLambdaName(*func);
        if (func->GetFuncType()->IsCFunc()) {
            LiftNestedFunctionWithCFuncType(*func);
            continue;
        }
        auto rawEnvs = func->GetCapturedVariables();
        auto boxedEnvs = BoxAllMutableVars(rawEnvs);
        auto autoEnvBaseDef = GetOrCreateAutoEnvBaseDef(*func->GetFuncType());
        auto autoEnvImplDef = GetOrCreateAutoEnvImplDef(*func, *autoEnvBaseDef, boxedEnvs);
        // `users` may be refreshed after the above processes.
        auto users = func->GetResult()->GetUsers();
        for (auto user : users) {
            ReplaceUserPoint(*func, *user, boxedEnvs, *autoEnvImplDef);
        }
        if (opts.chirDebugOptimizer) {
            PrintNestedFuncInfo(func->GetDebugLocation().GetBeginPos());
        }
        RemoveGetInstantiateValue(users);
        func->RemoveSelfFromBlock();
    }

    DoFunctionInlineForLambda();
}

std::vector<Type*> ClosureConversion::ConvertArgsType(const std::vector<Type*>& types)
{
    std::vector<Type*> argsType;
    argsType.reserve(types.size());
    for (const auto ty : types) {
        argsType.emplace_back(ConvertTypeToClosureType(*ty));
    }
    return argsType;
}

Type* ClosureConversion::ConvertTupleType(const TupleType& type)
{
    return builder.GetType<TupleType>(ConvertArgsType(type.GetElementTypes()));
}

Type* ClosureConversion::ConvertFuncType(const FuncType& type)
{
    auto autoEnvBaseDef = GetOrCreateGenericAutoEnvBaseDef(type.GetParamTypes().size());
    auto convertedFuncType = ConvertFuncArgsAndRetType(type);
    return builder.GetType<RefType>(InstantiateAutoEnvBaseType(*autoEnvBaseDef, *convertedFuncType, builder));
}

Type* ClosureConversion::ConvertEnumType(const EnumType& type)
{
    return builder.GetType<EnumType>(type.GetEnumDef(), ConvertArgsType(type.GetGenericArgs()));
}

Type* ClosureConversion::ConvertStructType(const StructType& type)
{
    return builder.GetType<StructType>(type.GetStructDef(), ConvertArgsType(type.GetGenericArgs()));
}

Type* ClosureConversion::ConvertClassType(const ClassType& type)
{
    return builder.GetType<ClassType>(type.GetClassDef(), ConvertArgsType(type.GetGenericArgs()));
}

Type* ClosureConversion::ConvertRawArrayType(const RawArrayType& type)
{
    return builder.GetType<RawArrayType>(ConvertTypeToClosureType(*type.GetElementType()), type.GetDims());
}

Type* ClosureConversion::ConvertVArrayType(const VArrayType& type)
{
    return builder.GetType<VArrayType>(ConvertTypeToClosureType(*type.GetElementType()), type.GetSize());
}

Type* ClosureConversion::ConvertCPointerType(const CPointerType& type)
{
    return builder.GetType<CPointerType>(ConvertTypeToClosureType(*type.GetElementType()));
}

Type* ClosureConversion::ConvertRefType(const RefType& type)
{
    return builder.GetType<RefType>(ConvertTypeToClosureType(*type.GetBaseType()));
}

Type* ClosureConversion::ConvertBoxType(const BoxType& type)
{
    return builder.GetType<BoxType>(ConvertTypeToClosureType(*type.GetBaseType()));
}

Type* ClosureConversion::ConvertCompositionalType(const Type& type)
{
    Type* curConvertedTy = nullptr;
    switch (type.GetTypeKind()) {
        case Type::TypeKind::TYPE_TUPLE:
            curConvertedTy = ConvertTupleType(StaticCast<const TupleType&>(type));
            break;
        case Type::TypeKind::TYPE_FUNC:
            curConvertedTy = ConvertFuncType(StaticCast<const FuncType&>(type));
            break;
        case Type::TypeKind::TYPE_ENUM:
            curConvertedTy = ConvertEnumType(StaticCast<const EnumType&>(type));
            break;
        case Type::TypeKind::TYPE_STRUCT:
            curConvertedTy = ConvertStructType(StaticCast<const StructType&>(type));
            break;
        case Type::TypeKind::TYPE_CLASS:
            curConvertedTy = ConvertClassType(StaticCast<const ClassType&>(type));
            break;
        case Type::TypeKind::TYPE_RAWARRAY:
            curConvertedTy = ConvertRawArrayType(StaticCast<const RawArrayType&>(type));
            break;
        case Type::TypeKind::TYPE_VARRAY:
            curConvertedTy = ConvertVArrayType(StaticCast<const VArrayType&>(type));
            break;
        case Type::TypeKind::TYPE_CPOINTER:
            curConvertedTy = ConvertCPointerType(StaticCast<const CPointerType&>(type));
            break;
        case Type::TypeKind::TYPE_REFTYPE:
            curConvertedTy = ConvertRefType(StaticCast<const RefType&>(type));
            break;
        case Type::TypeKind::TYPE_BOXTYPE:
            curConvertedTy = ConvertBoxType(StaticCast<const BoxType&>(type));
            break;
        default:
            CJC_ABORT();
    }
    CJC_NULLPTR_CHECK(curConvertedTy);
    return curConvertedTy;
}

Type* ClosureConversion::ConvertTypeToClosureType(Type& type)
{
    // case 1: just return original type
    if (type.IsPrimitive() || type.IsInvalid() || type.IsAny() || type.IsVoid() ||
        type.IsCFunc() || type.IsCString() || type.IsThis()) {
        return &type;
    }

    // case 2: store generic type and convert later
    if (type.IsGeneric()) {
        needConvertedGenericTys.emplace(StaticCast<GenericType*>(&type));
        return &type;
    }

    const auto tyCollected = typeConvertMap.find(&type);
    if (tyCollected != typeConvertMap.end()) {
        return tyCollected->second;
    }

    // case 3: compositional type
    auto curConvertedTy = ConvertCompositionalType(type);
    typeConvertMap[&type] = curConvertedTy;

    return curConvertedTy;
}

FuncType* ClosureConversion::ConvertFuncArgsAndRetType(const FuncType& oldFuncTy)
{
    std::vector<Type*> newArgsTy;
    for (auto ty : oldFuncTy.GetParamTypes()) {
        newArgsTy.emplace_back(ConvertTypeToClosureType(*ty));
    }
    auto newRetTy = ConvertTypeToClosureType(*oldFuncTy.GetReturnType());
    return builder.GetType<FuncType>(newArgsTy, newRetTy, oldFuncTy.HasVarArg(), oldFuncTy.IsCFunc());
}

void ClosureConversion::LiftGenericTypes()
{
    // we can't set upper bounds in `ConvertCompositionalType`, it will cause unlimited recursion
    // e.g. interface A<T> where T <: A<T>
    auto temp = needConvertedGenericTys;
    for (auto ty : temp) {
        std::vector<Type*> newUpperBounds;
        for (auto upperBound : ty->GetUpperBounds()) {
            newUpperBounds.emplace_back(ConvertTypeToClosureType(*upperBound));
        }
        ty->SetUpperBounds(newUpperBounds);
    }
}

void ClosureConversion::LiftType()
{
    ConvertTypeFunc convertTypeToClosure = [this](Type& type) {
        return ConvertTypeToClosureType(type);
    };
    ConvertTypeFunc convertTypeToInstBase = [this, &convertTypeToClosure, &convertTypeToInstBase](Type& type) {
        auto& funcType = StaticCast<FuncType&>(type);
        auto autoEnvBaseDef = GetOrCreateAutoEnvBaseDef(funcType);
        TypeConverterForCC converter(convertTypeToClosure, convertTypeToInstBase, builder);
        LiftCustomDefType(*autoEnvBaseDef, converter);
        auto convertedFuncType = ConvertFuncArgsAndRetType(funcType);
        return builder.GetType<RefType>(InstantiateAutoEnvBaseType(*autoEnvBaseDef, *convertedFuncType, builder));
    };
    TypeConverterForCC converter(convertTypeToClosure, convertTypeToInstBase, builder);
    auto postVisit = [&converter](Expression& e) {
        converter.VisitExpr(e);
        return VisitResult::CONTINUE;
    };
    for (auto func : package.GetGlobalFuncs()) {
        if (func->TestAttr(Attribute::SKIP_ANALYSIS)) {
            continue;
        }
        Visitor::Visit(
            *func, [](Expression&) { return VisitResult::CONTINUE; }, postVisit);
        converter.VisitValue(*func);
    }

    for (auto& importedValue : package.GetImportedVarAndFuncs()) {
        converter.VisitValue(*importedValue);
    }
    for (auto& globalVar : package.GetGlobalVars()) {
        converter.VisitValue(*globalVar);
    }

    for (auto def : package.GetAllCustomTypeDef()) {
        if (def->TestAttr(Attribute::SKIP_ANALYSIS)) {
            continue;
        }
        converter.VisitDef(*def);
    }

    LiftGenericTypes();
    for (auto type : builder.GetAllCustomTypes()) {
        type->ResetAllInstantiatedType();
    }
}

ClassDef* ClosureConversion::GetOrCreateGenericAutoEnvBaseDef(size_t paramNum)
{
    auto className = CHIRMangling::ClosureConversion::GenerateGenericBaseClassMangleName(paramNum);
    auto it = genericAutoEnvBaseDefs.find(className);
    if (it != genericAutoEnvBaseDefs.end()) {
        return it->second;
    }

    /*
        abstract class $Auto_Env_Base_param_n<T0, T1, ... Tn> <: core.Object {
            public func $VirtualFuncInCC(p0: T0, p1: T1, ...): Tn
        }
    */
    // create generic type params
    std::vector<Type*> genericTypeParams;
    auto paramTypePrefix = "ccbase_p" + std::to_string(paramNum) + "_";
    for (size_t i = 0; i < paramNum; ++i) {
        auto paramTypeName = paramTypePrefix + std::to_string(i);
        genericTypeParams.emplace_back(builder.GetType<GenericType>(paramTypeName, paramTypeName));
    }
    auto returnTypeName = "ccbase_r" + std::to_string(paramNum);
    genericTypeParams.emplace_back(builder.GetType<GenericType>(returnTypeName, returnTypeName));

    // create class def
    auto classDef = builder.CreateClass(INVALID_LOCATION, "", className, package.GetName(), true, false);
    auto classTy = builder.GetType<ClassType>(classDef, genericTypeParams);
    classDef->SetType(*classTy);

    // set attribute
    SetAutoEnvBaseDefAttr(*classDef);
    classDef->EnableAttr(Attribute::GENERIC);

    // add abstract method
    CreateVirtualFuncInAutoEnvBaseDef(*classDef, GENERIC_VIRTUAL_FUNC, genericTypeParams, builder);

    // cache class def
    genericAutoEnvBaseDefs.emplace(className, classDef);

    return classDef;
}

void ClosureConversion::CreateGenericOverrideMethodInAutoEnvImplDef(ClassDef& autoEnvImplDef, FuncBase& srcFunc,
    const std::unordered_map<const GenericType*, Type*>& originalTypeToNewType)
{
    // create new func type
    auto srcFuncParamTypesAndRetType = srcFunc.GetType()->GetTypeArgs();
    if (srcFunc.GetFuncKind() == FuncKind::LAMBDA) {
        // first param in lambda is `env`
        srcFuncParamTypesAndRetType.erase(srcFuncParamTypesAndRetType.begin());
    }
    auto [newFuncParamTypes, newFuncRetType] = CreateFuncTypeWithOrphanT(
        srcFunc.GetIdentifierWithoutPrefix(), srcFuncParamTypesAndRetType, originalTypeToNewType, builder);
    auto classRefTy = builder.GetType<RefType>(autoEnvImplDef.GetType());
    newFuncParamTypes.insert(newFuncParamTypes.begin(), classRefTy);
    auto newFuncTy = builder.GetType<FuncType>(newFuncParamTypes, newFuncRetType);

    // create override func
    auto mangledName = CHIRMangling::ClosureConversion::GenerateGenericOverrideFuncMangleName(srcFunc);
    auto newFunc = builder.CreateFunc(
        INVALID_LOCATION, newFuncTy, mangledName, GENERIC_VIRTUAL_FUNC, "", package.GetName());
    autoEnvImplDef.AddMethod(newFunc);

    // set attribute
    SetMemberMethodAttr(*newFunc, srcFunc.TestAttr(Attribute::CONST));

    // create func body
    auto blockGroup = builder.CreateBlockGroup(*newFunc);
    newFunc->InitBody(*blockGroup);
    auto entry = builder.CreateBlock(blockGroup);
    blockGroup->SetEntryBlock(entry);

    // create parameters
    for (auto paramTy : newFuncParamTypes) {
        builder.CreateParameter(paramTy, INVALID_LOCATION, *newFunc);
    }

    // create return value
    auto retRefTy = builder.GetType<RefType>(newFuncRetType);
    auto retVal = CreateAndAppendExpression<Allocate>(builder, retRefTy, newFuncRetType, entry);
    newFunc->SetReturnValue(*retVal->GetResult());

    auto expectedParamTypes = srcFunc.GetFuncType()->GetParamTypes();
    for (size_t i = 0; i < expectedParamTypes.size(); ++i) {
        expectedParamTypes[i] = ReplaceRawGenericArgType(*expectedParamTypes[i], originalTypeToNewType, builder);
    }
    auto& params = newFunc->GetParams();
    std::vector<Value*> applyArgs;
    size_t offset = srcFunc.GetFuncKind() == FuncKind::LAMBDA ? 0 : 1;
    CJC_ASSERT(expectedParamTypes.size() == params.size() - offset);
    for (size_t i = offset; i < params.size(); ++i) {
        applyArgs.emplace_back(
            TypeCastOrBoxIfNeeded(*params[i], *expectedParamTypes[i - offset], builder, *entry, INVALID_LOCATION));
    }
    auto applyRetType =
        ReplaceRawGenericArgType(*srcFunc.GetFuncType()->GetReturnType(), originalTypeToNewType, builder);
    std::vector<Type*> instTyArgs;
    for (auto ty : srcFunc.GetGenericTypeParams()) {
        instTyArgs.emplace_back(ReplaceRawGenericArgType(*ty, originalTypeToNewType, builder));
    }
    Type* thisTy = srcFunc.GetParentCustomTypeOrExtendedType();
    if (thisTy != nullptr) {
        thisTy = ReplaceRawGenericArgType(*thisTy, originalTypeToNewType, builder);
    }
    // this type equal to parent type
    auto callSrcFunc = CreateAndAppendExpression<Apply>(builder, applyRetType, &srcFunc, FuncCallContext{
        .args = applyArgs,
        .instTypeArgs = instTyArgs,
        .thisType = thisTy}, entry);

    auto applyRes = TypeCastOrBoxIfNeeded(
        *callSrcFunc->GetResult(), *newFuncRetType, builder, *entry, INVALID_LOCATION);

    // store return value and exit
    CreateAndAppendExpression<Store>(
        builder, builder.GetType<UnitType>(), applyRes, retVal->GetResult(), entry);
    CreateAndAppendTerminator<Exit>(builder, entry);
}

void ClosureConversion::CreateInstOverrideMethodInAutoEnvImplDef(ClassDef& autoEnvImplDef,
    FuncBase& srcFunc, const std::unordered_map<const GenericType*, Type*>& originalTypeToNewType)
{
    if (FuncTypeHasGenericT(srcFunc)) {
        return;
    }
    // create new func type
    auto newFuncParamTypes = srcFunc.GetFuncType()->GetParamTypes();
    auto classRefTy = builder.GetType<RefType>(autoEnvImplDef.GetType());
    if (srcFunc.GetFuncKind() != FuncKind::LAMBDA) {
        newFuncParamTypes.insert(newFuncParamTypes.begin(), classRefTy);
    }
    auto newFuncRetType = srcFunc.GetFuncType()->GetReturnType();
    auto newFuncTy = builder.GetType<FuncType>(newFuncParamTypes, newFuncRetType);

    // create override func
    auto mangledName = CHIRMangling::ClosureConversion::GenerateInstOverrideFuncMangleName(srcFunc);
    auto newFunc = builder.CreateFunc(
        INVALID_LOCATION, newFuncTy, mangledName, INST_VIRTUAL_FUNC, "", package.GetName());
    autoEnvImplDef.AddMethod(newFunc);

    // set attribute
    SetMemberMethodAttr(*newFunc, srcFunc.TestAttr(Attribute::CONST));

    // create func body
    auto blockGroup = builder.CreateBlockGroup(*newFunc);
    newFunc->InitBody(*blockGroup);
    auto entry = builder.CreateBlock(blockGroup);
    blockGroup->SetEntryBlock(entry);

    // create parameters
    for (auto paramTy : newFuncParamTypes) {
        builder.CreateParameter(paramTy, INVALID_LOCATION, *newFunc);
    }

    // create return value
    auto retRefTy = builder.GetType<RefType>(newFuncRetType);
    auto retVal = CreateAndAppendExpression<Allocate>(builder, retRefTy, newFuncRetType, entry);
    newFunc->SetReturnValue(*retVal->GetResult());

    // call src function
    auto& params = newFunc->GetParams();
    long offset = srcFunc.GetFuncKind() == FuncKind::LAMBDA ? 0 : 1;
    std::vector<Value*> applyArgs(params.begin() + offset, params.end());
    std::vector<Type*> instTyArgs;
    for (auto ty : srcFunc.GetGenericTypeParams()) {
        instTyArgs.emplace_back(ReplaceRawGenericArgType(*ty, originalTypeToNewType, builder));
    }
    Type* thisTy = srcFunc.GetParentCustomTypeOrExtendedType();
    if (thisTy != nullptr) {
        thisTy = ReplaceRawGenericArgType(*thisTy, originalTypeToNewType, builder);
    }
    auto callSrcFunc = CreateAndAppendExpression<Apply>(builder, newFuncRetType, &srcFunc, FuncCallContext{
        .args = applyArgs,
        .instTypeArgs = instTyArgs,
        .thisType = thisTy}, entry);

    // store return value and exit
    CreateAndAppendExpression<Store>(
        builder, builder.GetType<UnitType>(), callSrcFunc->GetResult(), retVal->GetResult(), entry);
    CreateAndAppendTerminator<Exit>(builder, entry);
}

void ClosureConversion::CreateMemberVarInAutoEnvImplDef(ClassDef& parentClass,
    const std::vector<Value*>& boxedEnvs, const std::unordered_map<const GenericType*, Type*>& originalTypeToNewType)
{
    AttributeInfo attributeInfo;
    attributeInfo.SetAttr(Attribute::PUBLIC, true);
    for (size_t i = 0; i < boxedEnvs.size(); ++i) {
        // 1. get member var's location
        DebugLocation loc;
        if (auto localVar = DynamicCast<LocalVar*>(boxedEnvs[i])) {
            loc = localVar->GetExpr()->GetDebugLocation();
        } else if (auto param = DynamicCast<Parameter*>(boxedEnvs[i]); param && param->GetDebugExpr()) {
            // param may be copied in function inline, then it doesn't have Debug
            loc = param->GetDebugExpr()->GetDebugLocation();
        }

        // 2. get member var's type
        auto memberTy = boxedEnvs[i]->GetType();
        if (IsMutableVarType(*boxedEnvs[i])) {
            memberTy = StaticCast<RefType*>(boxedEnvs[i]->GetType())->GetBaseType();
        }
        memberTy = ReplaceRawGenericArgType(*memberTy, originalTypeToNewType, builder);

        // 3. get member var's name
        auto memberName = GenerateSrcCodeIdentifier(*boxedEnvs[i]);

        // 4. add member var
        parentClass.AddInstanceVar(MemberVarInfo{std::move(memberName), "", memberTy, attributeInfo, loc});
    }
}

// only create shell, member func and member var will be added in next step
ClassDef* ClosureConversion::CreateAutoEnvImplDef(const std::string& className,
    const std::vector<GenericType*>& genericTypes, const Value& srcFunc, ClassDef& superClassDef,
    std::unordered_map<const GenericType*, Type*>& originalTypeToNewType)
{
    // 1. create generic type params
    std::vector<Type*> classGenericTypeParams;
    classGenericTypeParams.reserve(genericTypes.size());
    auto genericTypePrefix = className + "_";
    for (size_t i = 0; i < genericTypes.size(); ++i) {
        auto typeName = genericTypePrefix + std::to_string(i);
        auto newGenericType = builder.GetType<GenericType>(typeName, genericTypes[i]->GetSrcCodeIdentifier());
        classGenericTypeParams.emplace_back(newGenericType);
        originalTypeToNewType.emplace(genericTypes[i], newGenericType);
    }
    /**
     * maybe upper bounds have `T`, we need to replace the old `T` with the new `T`, e.g.
     *  func foo<T>() where T <: Option<T> {
     *      func goo() {} // create AutoEnvImpl def
     *  }
     *
     *  class AutoEnvImpl<U> <: AutoEnvBase where U <: Option<U> {}
     *                                            ^^^^^^^^^^^^^^ it shouldn't be `U <: Option<T>`
     */
    for (size_t i = 0; i < classGenericTypeParams.size(); ++i) {
        std::vector<Type*> newUpperBounds;
        auto genericType = StaticCast<GenericType*>(classGenericTypeParams[i]);
        if (genericType->GetSrcCodeIdentifier() == GENERIC_THIS_SRC_NAME) {
            auto parentCustomType = GetTopLevelFunc(srcFunc)->GetParentCustomTypeDef()->GetType();
            auto parentRef = builder.GetType<RefType>(parentCustomType);
            newUpperBounds.emplace_back(ReplaceRawGenericArgType(*parentRef, originalTypeToNewType, builder));
        } else {
            for (auto type : genericTypes[i]->GetUpperBounds()) {
                newUpperBounds.emplace_back(ReplaceRawGenericArgType(*type, originalTypeToNewType, builder));
            }
        }
        genericType->SetUpperBounds(newUpperBounds);
    }

    // 2. create class def
    auto classDef = builder.CreateClass(INVALID_LOCATION, "", className, package.GetName(), true, false);
    auto classTy = builder.GetType<ClassType>(classDef, classGenericTypeParams);
    classDef->SetType(*classTy);

    // 3. set super type
    /**
     * when a class inherit an instantiated class, this child class does not override parent's virtual function
     * Interface A<T> {
     *     static func foo(a: T) {}
     * }
     * class B <: A<Bool> {
     * }
     * In class `B`, we will generate a func B::foo with type (T)->T to meet consistent function signatures in vtable
     * However when we got a function call B::foo(true), we should get its real function type (Bool)->Bool.
     * So the class `AutoEnvImpl`'s super type should be `AutoEnvBase<Bool, Bool>` instead of `AutoEnvBase<T, T>`
     */
    std::vector<Type*> superClassGenericArgs;
    auto memberFuncType = StaticCast<FuncType*>(srcFunc.GetType());
    if (memberFuncType->IsGenericRelated()) {
        for (auto paramTy : memberFuncType->GetParamTypes()) {
            if (auto orphanType = GetInstTypeFromOrphanGenericType(paramTy)) {
                superClassGenericArgs.push_back(orphanType);
            } else {
                superClassGenericArgs.emplace_back(ReplaceRawGenericArgType(*paramTy, originalTypeToNewType, builder));
            }
        }
        if (auto retOrphanType = GetInstTypeFromOrphanGenericType(memberFuncType->GetReturnType())) {
            superClassGenericArgs.push_back(retOrphanType);
        } else {
            superClassGenericArgs.emplace_back(
                ReplaceRawGenericArgType(*memberFuncType->GetReturnType(), originalTypeToNewType, builder));
        }
    }
    /**
     * if func type is generic related, the class `AutoEnvImpl` inherits `AutoEnvGenericBase<...>`,
     * otherwise, class `AutoEnvImpl` inherits `AutoEnvInstBase` which don't have generic args
     */
    auto superClassTy = builder.GetType<ClassType>(&superClassDef, superClassGenericArgs);
    classDef->SetSuperClassTy(*superClassTy);

    // 4. set attribute
    SetAutoEnvImplDefAttr(*classDef);
    if (!classGenericTypeParams.empty()) {
        classDef->EnableAttr(Attribute::GENERIC);
    }

    return classDef;
}

ClassDef* ClosureConversion::GetOrCreateAutoEnvImplDef(FuncBase& func, ClassDef& superClassDef)
{
    auto className = CHIRMangling::ClosureConversion::GenerateGlobalImplClassMangleName(func);
    auto it = autoEnvImplDefs.find(className);
    if (it != autoEnvImplDefs.end()) {
        return it->second;
    }

    /*
    1. if func is nothing to do with generic type, like: func foo(a: Int32): Bool, then class def is like:
        class $Auto_Env_fooMangleName <: $Auto_Env_Base_param_1<Int32, Bool> {
            public override func fooMangleName_suffix(p0: Int32): Bool { // srcCodeIdentifier: $VirtualFuncInCC
                return foo(p0)
            }
        }

    2. if func is related with generic type, then collect all visiable generic types as class def's generic type params
        2.1 if func's params type and return type are nothing to do with generic type, like: foo<T>(a: Int32): Bool
            then class def is like:
            class $Auto_Env_fooMangleName<U> <: $Auto_Env_Base_param_1<Int32, Bool> {
                public override func fooMangleName_suffix(p0: Int32): Bool { // srcCodeIdentifier: $VirtualFuncInCC
                    return foo<U>(p0)
                }
            }
            note: declared new generic type in class def, use `U` as instantiated type args to call func foo
        2.2 if func's params type or return type is related with generic type, like: foo<T1>(a: T0): T1, and T0
            is declared in foo's outer decl, assume it's a struct decl named `S`, then class def is like:
            class $Auto_Env_fooMangleName<U0, U1> <: $Auto_Env_Base_param_1<U0, U1> {
                public override func fooMangleName_suffix(p0: U0): U1 { // srcCodeIdentifier: $VirtualFuncInCC
                    return S<U0>.foo<U1>(p0)
                }
            }
            note: in this case, func foo must be static, if func foo is non-static member method, it will be
                    wrappered by lambda
    */
    std::unordered_map<const GenericType*, Type*> originalTypeToNewType;
    auto genericTypes = CreateGenericTypeParamForAutoEnvImplDef(func, builder);
    auto classDef = CreateAutoEnvImplDef(
        className, genericTypes, func, superClassDef, originalTypeToNewType);
    CreateGenericOverrideMethodInAutoEnvImplDef(*classDef, func, originalTypeToNewType);
    CreateInstOverrideMethodInAutoEnvImplDef(*classDef, func, originalTypeToNewType);

    // cache class def
    autoEnvImplDefs.emplace(className, classDef);

    return classDef;
}

Func* ClosureConversion::LiftLambdaToGlobalFunc(
    ClassDef& autoEnvImplDef, Lambda& nestedFunc, const std::vector<GenericType*>& genericTypeParams,
    const std::unordered_map<const GenericType*, Type*>& instMap, const std::vector<Value*>& capturedValues)
{
    // ======================= Generate the global func declaration ======================= //
    // 1. create the global func identifier
    auto globalFuncIdentifier = GenerateGlobalFuncIdentifier(nestedFunc);
    auto srcCodeIdentifier = nestedFunc.GetSrcCodeIdentifier();

    // 2. create new func type
    auto newFuncParamTypes = nestedFunc.GetFuncType()->GetParamTypes();
    auto classRefTy = builder.GetType<RefType>(autoEnvImplDef.GetType());
    newFuncParamTypes.insert(newFuncParamTypes.begin(), classRefTy);
    auto newFuncRetType = nestedFunc.GetFuncType()->GetReturnType();
    auto newFuncTy = builder.GetType<FuncType>(newFuncParamTypes, newFuncRetType);
    newFuncTy = StaticCast<FuncType*>(ReplaceRawGenericArgType(*newFuncTy, instMap, builder));

    // 3. other info
    auto loc = nestedFunc.GetDebugLocation();
    std::vector<GenericType*> convertedGenericTypeParams;
    for (auto ty : genericTypeParams) {
        convertedGenericTypeParams.emplace_back(
            StaticCast<GenericType*>(ReplaceRawGenericArgType(*ty, instMap, builder)));
    }

    // 4. create global function declare
    auto globalFunc = builder.CreateFunc(loc, newFuncTy, globalFuncIdentifier, srcCodeIdentifier, "",
        nestedFunc.GetTopLevelFunc()->GetPackageName(), convertedGenericTypeParams);
    SetLiftedLambdaAttr(*globalFunc, nestedFunc);
    auto sigInfo = FuncSigInfo{
        .funcName = nestedFunc.GetSrcCodeIdentifier(),
        .funcType = nestedFunc.GetFuncType(),
        .genericTypeParams = nestedFunc.GetGenericTypeParams()
    };
    globalFunc->SetOriginalLambdaInfo(sigInfo);

    // Specially, the lifted lambda should inherit local var ID
    globalFunc->InheritIDFromFunc(*nestedFunc.GetBody()->GetTopLevelFunc());
    // Specially, record the `env` value
    Value* thisPtr = builder.CreateParameter(classRefTy, INVALID_LOCATION, *globalFunc);
    for (auto param : nestedFunc.GetParams()) {
        globalFunc->AddParam(*param);
    }

    // ======================= Generate the global func body ======================= //
    ReplaceThisTypeInApplyAndInvoke(nestedFunc, convertedGenericTypeParams, builder);
    globalFunc->InitBody(*nestedFunc.GetBody());
    globalFunc->SetReturnValue(*nestedFunc.GetReturnValue());

    // Then we need to insert:
    // 1) expressions to help with the debug
    // if there is no captured vars in lambda, we don't generate Debug for env, then in cjdb
    // it will look like this: `func lambda(param1, param2, ...)`
    // not like this: `func lambda($CapturedVars, param1, param2, ...)`
    // it just looks like fine, there won't be bug if generate Debug
    Expression* envParamDebug = nullptr;
    if (autoEnvImplDef.GetAllInstanceVarNum() != 0) {
        envParamDebug =
            builder.CreateExpression<Debug>(builder.GetUnitTy(), thisPtr, "$env", globalFunc->GetEntryBlock());
        globalFunc->GetEntryBlock()->InsertExprIntoHead(*envParamDebug);
    }

    // Finally we should：
    // 1) convert all the usage of captured variable to fields in env
    ReplaceEnvVarWithMemberVar(capturedValues, envParamDebug, *globalFunc, *thisPtr, builder);

    // 2) keep relations between default's param's desugar func and the func where the param is belonged.
    // logic below depends on the desugar func is CCed after the func where the param is belonged.
    if (nestedFunc.GetParamDftValHostFunc()) {
        if (auto it = convertedCache.find(nestedFunc.GetParamDftValHostFunc()); it != convertedCache.cend()) {
            globalFunc->SetParamDftValHostFunc(*it->second);
        } else {
            InternalError("never come here in ConvertNestedFunctions");
        }
    }

    // 3) Traverse and instantiate the original generics with the new generics of lifted func
    auto thisTyReplacement = nestedFunc.GetTopLevelFunc()->GetParentCustomTypeOrExtendedType();
    GenericTypeConvertor gConvertor(instMap, builder);
    ConvertTypeFunc convertFunc = [&gConvertor, &thisTyReplacement, this](Type& type) {
        if (thisTyReplacement != nullptr) {
            auto res = ReplaceThisTypeToConcreteType(type, *thisTyReplacement, builder);
            if (res != &type) {
                return res;
            }
        }
        return gConvertor.ConvertToInstantiatedType(type);
    };
    PrivateTypeConverter converter(convertFunc, builder);
    auto postVisit = [&converter](Expression& e) {
        converter.VisitExpr(e);
        return VisitResult::CONTINUE;
    };
    Visitor::Visit(
        *globalFunc, [](Expression&) { return VisitResult::CONTINUE; }, postVisit);
    for (auto& param : globalFunc->GetParams()) {
        converter.VisitValue(*param);
    }

    convertedCache.emplace(&nestedFunc, globalFunc);
    return globalFunc;
}

ClassDef* ClosureConversion::GetOrCreateAutoEnvImplDef(
    Lambda& func, ClassDef& superClassDef, const std::vector<Value*>& boxedEnvs)
{
    auto it = duplicateLambdaName.find(func.GetIdentifier());
    CJC_ASSERT(it != duplicateLambdaName.end());
    auto className = CHIRMangling::ClosureConversion::GenerateLambdaImplClassMangleName(func, it->second);

    CJC_ASSERT(autoEnvImplDefs.count(className) == 0);

    /*
    1. if lambda is nothing to do with generic type, like: func foo(a: Int32): Bool, and there are captured
        variable in lambda body, assume they are `let a: Int32` and `var b: Int64`, then class def is like:
        class $Capture_Int64 {
            var value: Int64
        }
        class $Auto_Env_fooMangleName <: $Auto_Env_Base_param_1<Int32, Bool> {
            var a: Int32
            var b: Class-$Capture_Int64
            public override func fooMangleName(p0: Int32): Bool { // srcCodeIdentifier: $VirtualFuncInCC
                lambda's body
            }
        }
        note: member var `a` in $Auto_Env will be set value in member func `$VirtualFuncInCC`, so it's
                a mutable var not a readonly var

    2. if lambda is related with generic type, then collect all visiable generic types as class def's
    generic type params. specially, if lambda is like: foo<T>(a: Int32): Bool, there is generic type
    declared in lambda, class def is like:
        class $Auto_Env_fooMangleName<U> <: $Auto_Env_Base_param_1<Int32, Bool> {
            public override func fooMangleName(p0: Int32): Bool { // srcCodeIdentifier: $VirtualFuncInCC
                lambda's body
            }
        }
        note: replace generic type `T` in lambda body with `U`
    */
    std::unordered_map<const GenericType*, Type*> originalTypeToNewType;
    auto genericTypes = CreateGenericTypeParamForAutoEnvImplDef(*func.GetResult(), builder);
    auto classDef = CreateAutoEnvImplDef(
        className, genericTypes, *func.GetResult(), superClassDef, originalTypeToNewType);
    CreateMemberVarInAutoEnvImplDef(*classDef, boxedEnvs, originalTypeToNewType);
    
    auto parentFunc = func.GetTopLevelFunc();
    CJC_NULLPTR_CHECK(parentFunc);
    auto globalFunc = LiftLambdaToGlobalFunc(*classDef, func, genericTypes, originalTypeToNewType, boxedEnvs);
    if (srcCodeImportedFuncs.find(parentFunc) != srcCodeImportedFuncs.end()) {
        uselessClasses.emplace(classDef);
        uselessLambda.emplace(globalFunc);
    }
    CreateGenericOverrideMethodInAutoEnvImplDef(*classDef, *globalFunc, originalTypeToNewType);
    CreateInstOverrideMethodInAutoEnvImplDef(*classDef, *globalFunc, originalTypeToNewType);

    autoEnvImplDefs.emplace(className, classDef);
    return classDef;
}

void ClosureConversion::ReplaceUserPoint(FuncBase& srcFunc, Expression& user, ClassDef& autoEnvImplDef)
{
    auto curBlock = user.GetParentBlock();
    auto autoEnvImplType = StaticCast<ClassType*>(autoEnvImplDef.GetType());
    std::vector<Value*> emptyEnvs;
    auto autoEnvObj = CreateAutoEnvImplObject(*curBlock, *autoEnvImplType, emptyEnvs, user, srcFunc);

    // typecast to base type
    auto curClassTy = StaticCast<ClassType*>(StaticCast<RefType*>(autoEnvObj->GetType())->GetBaseType());
    auto superClassTy = curClassTy->GetSuperClassTy(&builder);
    auto superClassRefTy = builder.GetType<RefType>(superClassTy);
    auto castToBaseType = builder.CreateExpression<TypeCast>(superClassRefTy, autoEnvObj, user.GetParentBlock());
    castToBaseType->MoveBefore(&user);

    auto res = CastTypeFromAutoEnvRefToFuncType(
        *srcFunc.GetType(), *castToBaseType->GetResult(), user, GetOutDefDeclaredTypes(srcFunc), builder);
    user.ReplaceOperand(&srcFunc, res);
}

void ClosureConversion::ReplaceUserPoint(
    Lambda& srcFunc, Expression& user, const std::vector<Value*>& envs, ClassDef& autoEnvImplDef)
{
    auto curBlock = user.GetParentBlock();
    auto autoEnvImplType = StaticCast<ClassType*>(autoEnvImplDef.GetType());
    Value* autoEnvObj = nullptr;
    // if user is in lambda func body, then `autoEnvObj` is `this`
    auto it = convertedCache.find(&srcFunc);
    CJC_ASSERT(it != convertedCache.end());
    auto globalFunc = it->second;
    if (user.GetTopLevelFunc() == globalFunc) {
        autoEnvObj = user.GetTopLevelFunc()->GetParam(0);
    } else {
        autoEnvObj = CreateAutoEnvImplObject(*curBlock, *autoEnvImplType, envs, user, *srcFunc.GetResult());
    }

    if (auto apply = DynamicCast<Apply*>(&user); apply && apply->GetCallee() == srcFunc.GetResult()) {
        auto methods = autoEnvImplDef.GetMethods();
        auto newCallee = methods.back();
        auto newArgs = apply->GetArgs();
        newArgs.insert(newArgs.begin(), autoEnvObj);
        auto retType = apply->GetResult()->GetType();
        auto loc = apply->GetDebugLocation();
        auto thisTy = autoEnvObj->GetType();

        auto newApply = builder.CreateExpression<Apply>(loc, retType, newCallee, FuncCallContext{
            .args = newArgs,
            .thisType = thisTy}, user.GetParentBlock());
        apply->ReplaceWith(*newApply);
    } else if (auto awe = DynamicCast<ApplyWithException*>(&user); awe && awe->GetCallee() == srcFunc.GetResult()) {
        auto methods = autoEnvImplDef.GetMethods();
        auto newCallee = methods.back();
        auto newArgs = awe->GetArgs();
        newArgs.insert(newArgs.begin(), autoEnvObj);
        auto retType = awe->GetResult()->GetType();
        auto loc = awe->GetDebugLocation();
        auto thisTy = autoEnvObj->GetType();

        auto newApply = builder.CreateExpression<ApplyWithException>(loc, retType, newCallee, FuncCallContext{
            .args = newArgs,
            .thisType = thisTy}, awe->GetSuccessBlock(), awe->GetErrorBlock(), user.GetParentBlock());
        awe->ReplaceWith(*newApply);
    } else {
        auto res = CastTypeFromAutoEnvRefToFuncType(*srcFunc.GetResult()->GetType(),
            *autoEnvObj, user, GetOutDefDeclaredTypes(*srcFunc.GetResult()), builder);
        user.ReplaceOperand(srcFunc.GetResult(), res);
    }
}

ClassDef* ClosureConversion::GetOrCreateInstAutoEnvBaseDef(const FuncType& funcType, ClassDef& superClass)
{
    auto className = CHIRMangling::ClosureConversion::GenerateInstantiatedBaseClassMangleName(funcType);
    auto it = instAutoEnvBaseDefs.find(className);
    if (it != instAutoEnvBaseDefs.end()) {
        return it->second;
    }

    // create class def
    auto classDef = builder.CreateClass(INVALID_LOCATION, "", className, package.GetName(), true, false);
    std::vector<Type*> emptyTypeArgs;
    auto classTy = builder.GetType<ClassType>(classDef, emptyTypeArgs);
    classDef->SetType(*classTy);

    // set super class type
    auto superClassTy = builder.GetType<ClassType>(&superClass, funcType.GetTypeArgs());
    classDef->SetSuperClassTy(*superClassTy);

    // set attribute
    SetAutoEnvBaseDefAttr(*classDef);

    // add abstract method
    CreateVirtualFuncInAutoEnvBaseDef(*classDef, INST_VIRTUAL_FUNC, funcType.GetTypeArgs(), builder);

    // cache class def
    instAutoEnvBaseDefs.emplace(className, classDef);

    return classDef;
}

void ClosureConversion::ConvertGlobalFunctions()
{
    for (auto func : package.GetGlobalFuncs()) {
        if (func->IsCFunc()) {
            continue; // never lift CFunc
        }
        auto users = func->GetUsers();
        bool convertFlag{false};
        for (auto user : users) {
            if (IsCalleeOfApply(*user, *func)) {
                continue;
            }
            auto autoEnvBaseDef = GetOrCreateAutoEnvBaseDef(*func->GetFuncType());
            auto autoEnvImplDef = GetOrCreateAutoEnvImplDef(*func, *autoEnvBaseDef);
            ReplaceUserPoint(*func, *user, *autoEnvImplDef);
            convertFlag = true;
            if (opts.chirDebugOptimizer) {
                PrintGlobalFuncInfo(func->GetDebugLocation().GetBeginPos());
            }
        }
        if (opts.enIncrementalCompilation && convertFlag) {
            if (!func->GetRawMangledName().empty()) {
                ccOutFuncsRawMangle.emplace(func->GetRawMangledName());
            }
        }
        RemoveGetInstantiateValue(users);
    }
}

void ClosureConversion::ConvertImportedFunctions()
{
    for (auto ele : package.GetImportedVarAndFuncs()) {
        if (ele->IsImportedVar() || ele->GetType()->IsCFunc()) {
            continue;
        }
        auto users = ele->GetUsers();
        auto func = StaticCast<ImportedFunc*>(ele);
        bool convertFlag{false};
        for (auto user : users) {
            if (IsCalleeOfApply(*user, *func)) {
                continue;
            }
            auto autoEnvBaseDef = GetOrCreateAutoEnvBaseDef(*func->GetFuncType());
            auto autoEnvImplDef = GetOrCreateAutoEnvImplDef(*func, *autoEnvBaseDef);
            ReplaceUserPoint(*func, *user, *autoEnvImplDef);
            convertFlag = true;
            if (opts.chirDebugOptimizer) {
                PrintImportedFuncInfo(*func);
            }
        }
        if (opts.enIncrementalCompilation && convertFlag) {
            if (!func->GetRawMangledName().empty()) {
                ccOutFuncsRawMangle.emplace(func->GetRawMangledName());
            }
        }
        RemoveGetInstantiateValue(users);
    }
}

void ClosureConversion::ConvertApplyToInvoke(const std::vector<Apply*>& applyExprs)
{
    for (auto e : applyExprs) {
        auto callee = e->GetCallee();
        ClassDef* autoEnvBaseDef = nullptr;
        ClassType* instParentType = nullptr;
        if (auto funcType = DynamicCast<FuncType*>(callee->GetType())) {
            // callee is still func type
            autoEnvBaseDef = GetOrCreateAutoEnvBaseDef(*funcType);
            instParentType = InstantiateAutoEnvBaseType(*autoEnvBaseDef, *funcType, builder);
        } else {
            // callee has been replaced in closure conversion, then apply must be converted to invoke
            instParentType = StaticCast<ClassType*>(StaticCast<RefType*>(callee->GetType())->GetBaseType());
            autoEnvBaseDef = instParentType->GetClassDef();
        }
        auto [methodName, originalFuncType] = GetFuncTypeFromAutoEnvBaseDef(*autoEnvBaseDef);

        auto invokeInfo = InvokeCallContext {
            .caller = callee,
            .funcCallCtx = FuncCallContext {
                .args = e->GetArgs(),
                .thisType = instParentType
            },
            .virMethodCtx = VirMethodContext {
                .srcCodeIdentifier = methodName,
                .originalFuncType = originalFuncType,
                .offset = 0
            }
        };
        auto invoke = builder.CreateExpression<Invoke>(
            e->GetDebugLocation(), e->GetResult()->GetType(), invokeInfo, e->GetParentBlock());
        e->ReplaceWith(*invoke);
    }
}

void ClosureConversion::ConvertApplyWithExceptionToInvokeWithException(
    const std::vector<ApplyWithException*>& applyExprs)
{
    for (auto e : applyExprs) {
        auto callee = e->GetCallee();
        ClassDef* autoEnvBaseDef = nullptr;
        ClassType* instParentType = nullptr;
        if (auto funcType = DynamicCast<FuncType*>(callee->GetType())) {
            // callee is still func type
            autoEnvBaseDef = GetOrCreateAutoEnvBaseDef(*funcType);
            instParentType = InstantiateAutoEnvBaseType(*autoEnvBaseDef, *funcType, builder);
        } else {
            // callee has been replaced in closure conversion, then apply must be converted to invoke
            instParentType = StaticCast<ClassType*>(StaticCast<RefType*>(callee->GetType())->GetBaseType());
            autoEnvBaseDef = instParentType->GetClassDef();
        }
        auto [methodName, originalFuncType] = GetFuncTypeFromAutoEnvBaseDef(*autoEnvBaseDef);

        auto invokeInfo = InvokeCallContext {
            .caller = callee,
            .funcCallCtx = FuncCallContext {
                .args = e->GetArgs(),
                .thisType = instParentType
            },
            .virMethodCtx = VirMethodContext {
                .srcCodeIdentifier = methodName,
                .originalFuncType = originalFuncType,
                .offset = 0
            }
        };
        auto invoke = builder.CreateExpression<InvokeWithException>(e->GetDebugLocation(),
            e->GetResult()->GetType(), invokeInfo, e->GetSuccessBlock(), e->GetErrorBlock(), e->GetParentBlock());
        e->ReplaceWith(*invoke);
    }
}

void ClosureConversion::ConvertExpressions()
{
    std::vector<Apply*> applyExprs;
    std::vector<ApplyWithException*> applyWithExceptionExprs;
    std::vector<TypeCast*> typecastToBoxExprs;
    std::vector<TypeCast*> typecastToUnBoxExprs;
    std::vector<Box*> boxToTypecastExprs;
    std::vector<UnBox*> unboxToTypecastExprs;
    auto postVisit = [&applyExprs, &applyWithExceptionExprs, &typecastToBoxExprs, &typecastToUnBoxExprs,
        &boxToTypecastExprs, &unboxToTypecastExprs](Expression& e) {
        if (ApplyNeedConvertToInvoke(e)) {
            applyExprs.emplace_back(StaticCast<Apply*>(&e));
        } else if (ApplyWithExceptionNeedConvertToInvokeWithException(e)) {
            applyWithExceptionExprs.emplace_back(StaticCast<ApplyWithException*>(&e));
        } else if (TypeCastNeedConvertToBox(e)) {
            typecastToBoxExprs.emplace_back(StaticCast<TypeCast*>(&e));
        } else if (TypeCastNeedConvertToUnBox(e)) {
            typecastToUnBoxExprs.emplace_back(StaticCast<TypeCast*>(&e));
        } else if (BoxNeedConvertToTypeCast(e)) {
            boxToTypecastExprs.emplace_back(StaticCast<Box*>(&e));
        } else if (UnBoxNeedConvertToTypeCast(e)) {
            unboxToTypecastExprs.emplace_back(StaticCast<UnBox*>(&e));
        }
        return VisitResult::CONTINUE;
    };
    for (auto func : package.GetGlobalFuncs()) {
        if (func->TestAttr(Attribute::SKIP_ANALYSIS)) {
            continue;
        }
        Visitor::Visit(
            *func, [](Expression&) { return VisitResult::CONTINUE; }, postVisit);
    }
    ConvertApplyToInvoke(applyExprs);
    ConvertApplyWithExceptionToInvokeWithException(applyWithExceptionExprs);
    ConvertTypeCastToBox(typecastToBoxExprs, builder);
    ConvertTypeCastToUnBox(typecastToUnBoxExprs, builder);
    ConvertBoxToTypeCast(boxToTypecastExprs, builder);
    ConvertUnBoxToTypeCast(unboxToTypecastExprs, builder);
}

void ClosureConversion::CreateVTableForAutoEnvDef()
{
    auto vtableGenerator = VTableGenerator(builder);
    for (auto& it : genericAutoEnvBaseDefs) {
        vtableGenerator.GenerateVTable(*it.second);
    }
    for (auto& it : instAutoEnvBaseDefs) {
        vtableGenerator.GenerateVTable(*it.second);
    }
    for (auto& it : instAutoEnvWrapperDefs) {
        vtableGenerator.GenerateVTable(*it.second);
    }
    for (auto& it : autoEnvImplDefs) {
        vtableGenerator.GenerateVTable(*it.second);
    }
}

ClassDef* ClosureConversion::CreateAutoEnvWrapper(const std::string& className, ClassType& superClassType)
{
    auto wrapperClassDef = builder.CreateClass(INVALID_LOCATION, "", className, package.GetName(), true, false);
    std::vector<Type*> emptyTypeArgs;
    auto classType = builder.GetType<ClassType>(wrapperClassDef, emptyTypeArgs);
    wrapperClassDef->SetType(*classType);

    wrapperClassDef->SetSuperClassTy(superClassType);

    SetAutoEnvImplDefAttr(*wrapperClassDef);

    return wrapperClassDef;
}

void ClosureConversion::CreateMemberVarInAutoEnvWrapper(ClassDef& autoEnvWrapperDef)
{
    Type* memberVarType = autoEnvWrapperDef.GetSuperClassTy()->GetSuperClassTy(&builder);
    memberVarType = builder.GetType<RefType>(memberVarType);

    AttributeInfo attributeInfo;
    attributeInfo.SetAttr(Attribute::PUBLIC, true);

    autoEnvWrapperDef.AddInstanceVar(MemberVarInfo{"v", "", memberVarType, attributeInfo, INVALID_LOCATION});
}

Func* ClosureConversion::CreateGenericMethodInAutoEnvWrapper(ClassDef& autoEnvWrapperDef)
{
    // 1. create function type
    auto memberVars = autoEnvWrapperDef.GetDirectInstanceVars();
    CJC_ASSERT(memberVars.size() == 1);
    auto memberVarType = StaticCast<ClassType*>(memberVars[0].type->StripAllRefs());
    auto funcNamePrefix = autoEnvWrapperDef.GetIdentifierWithoutPrefix();
    std::unordered_map<const GenericType*, Type*> emptyTable;
    auto [paramTypes, retType] = CreateFuncTypeWithOrphanT(
        funcNamePrefix, memberVarType->GetGenericArgs(), emptyTable, builder);
    auto autoEnvWrapperRefType = builder.GetType<RefType>(autoEnvWrapperDef.GetType());
    paramTypes.insert(paramTypes.begin(), autoEnvWrapperRefType);
    auto funcType = builder.GetType<FuncType>(paramTypes, retType);

    // 2. create function
    auto funcMangledName =
        CHIRMangling::ClosureConversion::GenerateWrapperClassGenericOverrideFuncMangleName(autoEnvWrapperDef);
    auto func = builder.CreateFunc(
        INVALID_LOCATION, funcType, funcMangledName, GENERIC_VIRTUAL_FUNC, "", package.GetName());
    autoEnvWrapperDef.AddMethod(func);

    // 3. set attribute
    SetMemberMethodAttr(*func, false);

    // 4. create function body
    auto blockGroup = builder.CreateBlockGroup(*func);
    func->InitBody(*blockGroup);
    auto entry = builder.CreateBlock(blockGroup);
    blockGroup->SetEntryBlock(entry);

    // 5. create parameters
    for (auto paramTy : paramTypes) {
        builder.CreateParameter(paramTy, INVALID_LOCATION, *func);
    }

    // 6. create return value
    auto retRefTy = builder.GetType<RefType>(retType);
    auto retVal = CreateAndAppendExpression<Allocate>(builder, retRefTy, retType, entry);
    func->SetReturnValue(*retVal->GetResult());

    // 7. create invoke
    auto instCustomType = memberVarType;
    auto instParamTypesAndRetType = instCustomType->GetGenericArgs();
    auto instParamTypes = std::vector<Type*>(instParamTypesAndRetType.begin(), instParamTypesAndRetType.end() - 1);
    auto [methodName, originalFuncType] = GetFuncTypeFromAutoEnvBaseDef(*instCustomType->GetClassDef());

    auto& funcParams = func->GetParams();
    std::vector<Value*> invokeArgs(funcParams.begin() + 1, funcParams.end());

    auto memberVarRefType = builder.GetType<RefType>(memberVarType);
    auto memberVarRefRefType = builder.GetType<RefType>(memberVarRefType);
    auto memberVarRefRef = CreateAndAppendExpression<GetElementRef>(
        builder, memberVarRefRefType, funcParams[0], std::vector<uint64_t>{0}, entry)->GetResult();
    auto memberVarRef = CreateAndAppendExpression<Load>(builder, memberVarRefType, memberVarRefRef, entry)->GetResult();
    
    auto invokeInfo = InvokeCallContext {
        .caller = memberVarRef,
        .funcCallCtx = FuncCallContext {
            .args = invokeArgs,
            .thisType = instCustomType
        },
        .virMethodCtx = VirMethodContext {
            .srcCodeIdentifier = methodName,
            .originalFuncType = originalFuncType,
            .offset = 0
        }
    };
    auto invokeRes = CreateAndAppendExpression<Invoke>(builder, retType, invokeInfo, entry)->GetResult();

    // 8. store return value and exit
    CreateAndAppendExpression<Store>(builder, builder.GetType<UnitType>(), invokeRes, retVal->GetResult(), entry);
    CreateAndAppendTerminator<Exit>(builder, entry);

    return func;
}

void ClosureConversion::CreateInstMethodInAutoEnvWrapper(ClassDef& autoEnvWrapperDef, Func& genericFunc)
{
    // 1. create function type
    auto superDef = autoEnvWrapperDef.GetSuperClassDef();
    CJC_ASSERT(superDef->GetType()->IsAutoEnvInstBase());
    auto abstractMethods = superDef->GetAbstractMethods();
    CJC_ASSERT(abstractMethods.size() == 1);
    auto parentFuncType = StaticCast<FuncType*>(abstractMethods[0].methodTy);
    auto paramTypes = parentFuncType->GetParamTypes();
    CJC_ASSERT(!paramTypes.empty());
    auto autoEnvWrapperRefType = builder.GetType<RefType>(autoEnvWrapperDef.GetType());
    paramTypes[0] = autoEnvWrapperRefType;
    auto retType = parentFuncType->GetReturnType();
    auto funcType = builder.GetType<FuncType>(paramTypes, retType);

    // 2. create function
    auto funcNamePrefix = autoEnvWrapperDef.GetIdentifierWithoutPrefix();
    auto funcMangledName =
        CHIRMangling::ClosureConversion::GenerateWrapperClassInstOverrideFuncMangleName(autoEnvWrapperDef);
    auto func = builder.CreateFunc(
        INVALID_LOCATION, funcType, funcMangledName, INST_VIRTUAL_FUNC, "", package.GetName());
    autoEnvWrapperDef.AddMethod(func);

    // 3. set attribute
    SetMemberMethodAttr(*func, false);

    // 4. create function body
    auto blockGroup = builder.CreateBlockGroup(*func);
    func->InitBody(*blockGroup);
    auto entry = builder.CreateBlock(blockGroup);
    blockGroup->SetEntryBlock(entry);

    // 5. create parameters
    for (auto paramTy : paramTypes) {
        builder.CreateParameter(paramTy, INVALID_LOCATION, *func);
    }

    // 6. create return value
    auto retRefTy = builder.GetType<RefType>(retType);
    auto retVal = CreateAndAppendExpression<Allocate>(builder, retRefTy, retType, entry);
    func->SetReturnValue(*retVal->GetResult());

    auto& params = func->GetParams();
    auto applyArgs = std::vector<Value*>(params.begin(), params.end());
    auto customType = autoEnvWrapperDef.GetType();
    auto apply = CreateAndAppendExpression<Apply>(builder, retType, &genericFunc, FuncCallContext{
        .args = applyArgs,
        .thisType = customType}, entry);

    CreateAndAppendExpression<Store>(
        builder, builder.GetType<UnitType>(), apply->GetResult(), retVal->GetResult(), entry);
    CreateAndAppendTerminator<Exit>(builder, entry);

    // 7. create wrapper class if type is mismatched
    if (auto res = ApplyArgNeedTypeCast(*apply); res.first) {
        AddTypeCastForOperand({apply, res.second}, builder);
    }
    if (ApplyRetValNeedWrapper(*apply)) {
        WrapApplyRetVal(*apply);
    }
}

ClassDef* ClosureConversion::GetOrCreateAutoEnvWrapper(ClassType& instAutoEnvBaseType)
{
    auto instAutoEnvBaseDef = instAutoEnvBaseType.GetClassDef();
    auto wrapperName = CHIRMangling::ClosureConversion::GenerateWrapperClassMangleName(*instAutoEnvBaseDef);
    auto it = instAutoEnvWrapperDefs.find(wrapperName);
    if (it != instAutoEnvWrapperDefs.end()) {
        return it->second;
    }

    auto wrapperClassDef = CreateAutoEnvWrapper(wrapperName, instAutoEnvBaseType);
    CreateMemberVarInAutoEnvWrapper(*wrapperClassDef);
    auto genericFunc = CreateGenericMethodInAutoEnvWrapper(*wrapperClassDef);
    CreateInstMethodInAutoEnvWrapper(*wrapperClassDef, *genericFunc);

    instAutoEnvWrapperDefs.emplace(wrapperName, wrapperClassDef);
    return wrapperClassDef;
}

void ClosureConversion::WrapApplyRetVal(Apply& apply)
{
    /** convert from:
     *  %0: $AutoEnvInstBase& = Apply(xxx)
     *  %1: xxx = Expression(%0)
     *
     *  to:
     *  %0: $AutoEnvGenericBase& = Apply(xxx)
     *  %2: $AutoEnvWrapperClass& = Allocate($AutoEnvWrapperClass)
     *  %3: Unit = StoreElementRef(%0, %2, 0)
     *  %4: $AutoEnvInstBase& = TypeCast(%2)
     *  %1: xxx = Expression(%4)
     */
    // 1. create $Auto_Env_wrapper
    auto applyRetVal = apply.GetResult();
    auto applyRetType = applyRetVal->GetType();
    auto applyRetTypeNoRef = StaticCast<ClassType*>(applyRetType->StripAllRefs());
    auto autoEnvWrapperDef = GetOrCreateAutoEnvWrapper(*applyRetTypeNoRef);

    // 2. convert return type
    auto newApplyRetType = builder.GetType<RefType>(autoEnvWrapperDef->GetSuperClassDef()->GetSuperClassTy());
    ConvertTypeFunc convertRetType = [&applyRetType, &newApplyRetType](const Type& type) {
        CJC_ASSERT(&type == applyRetType);
        return newApplyRetType;
    };
    PrivateTypeConverter converter(convertRetType, builder);
    converter.VisitValue(*applyRetVal);

    // 3. create $Auto_Env_wrapper object
    auto parentBlock = apply.GetParentBlock();
    auto autoEnvWrapperType = autoEnvWrapperDef->GetType();
    auto autoEnvWrapperRefType = builder.GetType<RefType>(autoEnvWrapperType);
    auto allocate = builder.CreateExpression<Allocate>(autoEnvWrapperRefType, autoEnvWrapperType, parentBlock);
    allocate->MoveAfter(&apply);

    auto memberVar = applyRetVal;
    auto storeMemberVar = builder.CreateExpression<StoreElementRef>(
        builder.GetUnitTy(), memberVar, allocate->GetResult(), std::vector<uint64_t>{0}, parentBlock);
    storeMemberVar->MoveAfter(allocate);

    // typecast from $Auto_Env_xxx_wrapper to $Auto_Env_InstBase
    auto typecast = builder.CreateExpression<TypeCast>(applyRetType, allocate->GetResult(), parentBlock);
    typecast->MoveAfter(storeMemberVar);

    // 4. replace user
    ReplaceOperandWithAutoEnvWrapperClass(*apply.GetResult(), *typecast->GetResult(), {storeMemberVar});
}

void ClosureConversion::WrapApplyWithExceptionRetVal(ApplyWithException& apply)
{
    /** convert from:
     *  Block #0:
     *  %0: $AutoEnvInstBase& = ApplyWithException(xxx, #1, #2)
     *  Block #1:
     *  %1: xxx = Expression(%0)
     *  Block #2:
     *  %2: xxx = Expression(%0)
     *
     *  to:
     *  Block #0:
     *  %0: $AutoEnvGenericBase& = ApplyWithException(xxx, #1, #2)
     *  Block #1:
     *  %3: $AutoEnvWrapperClass& = Allocate($AutoEnvWrapperClass)
     *  %4: Unit = StoreElementRef(%0, %3, 0)
     *  %5: $AutoEnvInstBase& = TypeCast(%3)
     *  %1: xxx = Expression(%5)
     *  Block #2:
     *  %6: $AutoEnvWrapperClass& = Allocate($AutoEnvWrapperClass)
     *  %7: Unit = StoreElementRef(%0, %6, 0)
     *  %8: $AutoEnvInstBase& = TypeCast(%6)
     *  %2: xxx = Expression(%8)
     */
    // 1. create $Auto_Env_wrapper
    auto applyRetVal = apply.GetResult();
    auto applyRetType = applyRetVal->GetType();
    auto applyRetTypeNoRef = StaticCast<ClassType*>(applyRetType->StripAllRefs());
    auto autoEnvWrapperDef = GetOrCreateAutoEnvWrapper(*applyRetTypeNoRef);

    // 2. convert return type
    auto newApplyRetType = builder.GetType<RefType>(autoEnvWrapperDef->GetSuperClassDef()->GetSuperClassTy());
    ConvertTypeFunc convertRetType = [&applyRetType, &newApplyRetType](const Type& type) {
        CJC_ASSERT(&type == applyRetType);
        return newApplyRetType;
    };
    PrivateTypeConverter converter(convertRetType, builder);
    converter.VisitValue(*applyRetVal);

    auto autoEnvWrapperType = autoEnvWrapperDef->GetType();
    auto autoEnvWrapperRefType = builder.GetType<RefType>(autoEnvWrapperType);
    for (auto user : applyRetVal->GetUsers()) {
        if (user->GetExprKind() == ExprKind::INSTANCEOF) {
            continue;
        }
        // 3. create $Auto_Env_wrapper object
        auto userParentBlock = user->GetParentBlock();
        auto allocate = builder.CreateExpression<Allocate>(autoEnvWrapperRefType, autoEnvWrapperType, userParentBlock);
        allocate->MoveBefore(user);

        auto memberVar = applyRetVal;
        auto storeMemberVar = builder.CreateExpression<StoreElementRef>(
            builder.GetUnitTy(), memberVar, allocate->GetResult(), std::vector<uint64_t>{0}, userParentBlock);
        storeMemberVar->MoveAfter(allocate);

        // typecast from $Auto_Env_xxx_wrapper to $Auto_Env_InstBase
        auto typecast = builder.CreateExpression<TypeCast>(applyRetType, allocate->GetResult(), userParentBlock);
        typecast->MoveAfter(storeMemberVar);

        // 4. replace user
        user->ReplaceOperand(applyRetVal, typecast->GetResult());
    }
}

void ClosureConversion::WrapInvokeRetVal(Expression& e)
{
    /** convert from:
     *  %0: $AutoEnvInstBase& = Invoke(xxx)
     *  %1: xxx = Expression(%0)
     *
     *  to:
     *  %0: $AutoEnvGenericBase& = Invoke(xxx)
     *  %2: $AutoEnvWrapperClass& = Allocate($AutoEnvWrapperClass)
     *  %3: Unit = StoreElementRef(%0, %2, 0)
     *  %4: $AutoEnvInstBase& = TypeCast(%2)
     *  %1: xxx = Expression(%4)
     */
    // 1. create $Auto_Env_wrapper
    auto invokeRetVal = e.GetResult();
    auto invokeRetType = invokeRetVal->GetType();
    auto invokeRetTypeNoRef = StaticCast<ClassType*>(invokeRetType->StripAllRefs());
    auto autoEnvWrapperDef = GetOrCreateAutoEnvWrapper(*invokeRetTypeNoRef);

    // 2. convert return type
    CJC_NULLPTR_CHECK(autoEnvWrapperDef->GetSuperClassDef());
    auto newInvokeRetType = builder.GetType<RefType>(autoEnvWrapperDef->GetSuperClassDef()->GetSuperClassTy());
    ConvertTypeFunc convertRetType = [&invokeRetType, &newInvokeRetType](const Type& type) {
        CJC_ASSERT(&type == invokeRetType);
        return newInvokeRetType;
    };
    PrivateTypeConverter converter(convertRetType, builder);
    converter.VisitValue(*invokeRetVal);

    // 3. create $Auto_Env_wrapper object
    auto parentBlock = e.GetParentBlock();
    auto autoEnvWrapperType = autoEnvWrapperDef->GetType();
    auto autoEnvWrapperRefType = builder.GetType<RefType>(autoEnvWrapperType);
    auto allocate = builder.CreateExpression<Allocate>(autoEnvWrapperRefType, autoEnvWrapperType, parentBlock);
    allocate->MoveAfter(&e);

    auto memberVar = invokeRetVal;
    auto storeMemberVar = builder.CreateExpression<StoreElementRef>(
        builder.GetUnitTy(), memberVar, allocate->GetResult(), std::vector<uint64_t>{0}, parentBlock);
    storeMemberVar->MoveAfter(allocate);

    // typecast from $Auto_Env_xxx_wrapper to $Auto_Env_InstBase
    auto typecast = builder.CreateExpression<TypeCast>(invokeRetType, allocate->GetResult(), parentBlock);
    typecast->MoveAfter(storeMemberVar);

    // 4. replace user
    ReplaceOperandWithAutoEnvWrapperClass(*e.GetResult(), *typecast->GetResult(), {storeMemberVar});
}

void ClosureConversion::WrapInvokeWithExceptionRetVal(Expression& e)
{
    /** convert from:
     *  Block #0:
     *  %0: $AutoEnvInstBase& = InvokeWithException(xxx, #1, #2)
     *  Block #1:
     *  %1: xxx = Expression(%0)
     *  Block #2:
     *  %2: xxx = Expression(%0)
     *
     *  to:
     *  Block #0:
     *  %0: $AutoEnvGenericBase& = InvokeWithException(xxx, #1, #2)
     *  Block #1:
     *  %3: $AutoEnvWrapperClass& = Allocate($AutoEnvWrapperClass)
     *  %4: Unit = StoreElementRef(%0, %3, 0)
     *  %5: $AutoEnvInstBase& = TypeCast(%3)
     *  %1: xxx = Expression(%5)
     *  Block #2:
     *  %6: $AutoEnvWrapperClass& = Allocate($AutoEnvWrapperClass)
     *  %7: Unit = StoreElementRef(%0, %6, 0)
     *  %8: $AutoEnvInstBase& = TypeCast(%6)
     *  %2: xxx = Expression(%8)
     */
    // 1. create $Auto_Env_wrapper
    auto invokeRetVal = e.GetResult();
    auto invokeRetType = invokeRetVal->GetType();
    auto invokeRetTypeNoRef = StaticCast<ClassType*>(invokeRetType->StripAllRefs());
    auto autoEnvWrapperDef = GetOrCreateAutoEnvWrapper(*invokeRetTypeNoRef);

    // 2. convert return type
    auto newInvokeRetType = builder.GetType<RefType>(autoEnvWrapperDef->GetSuperClassDef()->GetSuperClassTy());
    ConvertTypeFunc convertRetType = [&invokeRetType, &newInvokeRetType](const Type& type) {
        CJC_ASSERT(&type == invokeRetType);
        return newInvokeRetType;
    };
    PrivateTypeConverter converter(convertRetType, builder);
    converter.VisitValue(*invokeRetVal);

    auto autoEnvWrapperType = autoEnvWrapperDef->GetType();
    auto autoEnvWrapperRefType = builder.GetType<RefType>(autoEnvWrapperType);
    for (auto user : invokeRetVal->GetUsers()) {
        if (user->GetExprKind() == ExprKind::INSTANCEOF) {
            continue;
        }
        // 3. create $Auto_Env_wrapper object
        auto userParentBlock = user->GetParentBlock();
        auto allocate = builder.CreateExpression<Allocate>(autoEnvWrapperRefType, autoEnvWrapperType, userParentBlock);
        allocate->MoveBefore(user);

        auto memberVar = invokeRetVal;
        auto storeMemberVar = builder.CreateExpression<StoreElementRef>(
            builder.GetUnitTy(), memberVar, allocate->GetResult(), std::vector<uint64_t>{0}, userParentBlock);
        storeMemberVar->MoveAfter(allocate);

        // typecast from $Auto_Env_xxx_wrapper to $Auto_Env_InstBase
        auto typecast = builder.CreateExpression<TypeCast>(invokeRetType, allocate->GetResult(), userParentBlock);
        typecast->MoveAfter(storeMemberVar);

        // 4. replace user
        user->ReplaceOperand(invokeRetVal, typecast->GetResult());
    }
}

void ClosureConversion::WrapGetElementRefRetVal(GetElementRef& getEleRef)
{
    /** convert from:
     *  %0: $AutoEnvInstBase&& = GetElementRef(xxx)
     *  %1: $AutoEnvInstBase& = Load(%0)
     *  %2: xxx = Expression(%1)
     *
     *  to:
     *  %0: $AutoEnvGenericBase&& = GetElementRef(xxx)
     *  %3: $AutoEnvWrapperClass& = Allocate($AutoEnvWrapperClass)
     *  %4: $AutoEnvGenericBase& = Load(%0)
     *  %5: Unit = StoreElementRef(%4, %3, 0)
     *  %6: $AutoEnvInstBase& = TypeCast(%3)
     *  %7: $AutoEnvInstBase&& = Allocate($AutoEnvInstBase&)
     *  %8: Unit = Store(%6, %7)
     *  %1: $AutoEnvInstBase& = Load(%7)
     *  %2: xxx = Expression(%1)
     */
    // 1. create $Auto_Env_wrapper
    auto getEleRefRetVal = getEleRef.GetResult();
    auto getEleRefRetType = getEleRefRetVal->GetType();
    const size_t refDim = 2;
    CJC_ASSERT(getEleRefRetType->IsReferenceTypeWithRefDims(refDim));
    auto getEleRefRetTypeNoRef = StaticCast<ClassType*>(getEleRefRetType->StripAllRefs());
    auto autoEnvWrapperDef = GetOrCreateAutoEnvWrapper(*getEleRefRetTypeNoRef);

    // 2. convert return type
    auto newGetEleRefRetType =
        builder.GetType<RefType>(builder.GetType<RefType>(autoEnvWrapperDef->GetSuperClassDef()->GetSuperClassTy()));
    ConvertTypeFunc convertRetType = [&getEleRefRetType, &newGetEleRefRetType](const Type& type) {
        CJC_ASSERT(&type == getEleRefRetType);
        return newGetEleRefRetType;
    };
    PrivateTypeConverter converter(convertRetType, builder);
    converter.VisitValue(*getEleRefRetVal);

    // 3. create $Auto_Env_wrapper object
    auto parentBlock = getEleRef.GetParentBlock();
    auto autoEnvWrapperType = autoEnvWrapperDef->GetType();
    auto autoEnvWrapperRefType = builder.GetType<RefType>(autoEnvWrapperType);
    auto allocate1 = builder.CreateExpression<Allocate>(autoEnvWrapperRefType, autoEnvWrapperType, parentBlock);
    allocate1->MoveAfter(&getEleRef);

    auto loadRetType = StaticCast<RefType*>(newGetEleRefRetType)->GetBaseType();
    auto load = builder.CreateExpression<Load>(loadRetType, getEleRefRetVal, parentBlock);
    load->MoveAfter(allocate1);
    
    auto memberVar = load->GetResult();
    auto storeMemberVar = builder.CreateExpression<StoreElementRef>(
        builder.GetUnitTy(), memberVar, allocate1->GetResult(), std::vector<uint64_t>{0}, parentBlock);
    storeMemberVar->MoveAfter(load);

    // typecast from $Auto_Env_xxx_wrapper to $Auto_Env_InstBase
    auto expectedType = StaticCast<RefType*>(getEleRefRetType)->GetBaseType();
    auto typecast = builder.CreateExpression<TypeCast>(expectedType, allocate1->GetResult(), parentBlock);
    typecast->MoveAfter(storeMemberVar);

    auto typecastRetType = typecast->GetResult()->GetType();
    auto allocate2RetType = builder.GetType<RefType>(typecastRetType);
    auto allocate2 = builder.CreateExpression<Allocate>(allocate2RetType, typecastRetType, parentBlock);
    allocate2->MoveAfter(typecast);

    auto store = builder.CreateExpression<Store>(
        builder.GetUnitTy(), typecast->GetResult(), allocate2->GetResult(), parentBlock);
    store->MoveAfter(allocate2);

    // 4. replace user
    ReplaceOperandWithAutoEnvWrapperClass(*getEleRefRetVal, *allocate2->GetResult(), {load});
}

void ClosureConversion::WrapFieldRetVal(Field& field)
{
    /** convert from:
     *  %0: $AutoEnvInstBase& = Field(xxx)
     *  %1: xxx = Expression(%0)
     *
     *  to:
     *  %0: $AutoEnvGenericBase& = Field(xxx)
     *  %2: $AutoEnvWrapperClass& = Allocate($AutoEnvWrapperClass)
     *  %3: Unit = StoreElementRef(%0, %2, 0)
     *  %4: $AutoEnvInstBase& = TypeCast(%2)
     *  %1: xxx = Expression(%4)
     */
    // 1. create $Auto_Env_wrapper
    auto fieldRetVal = field.GetResult();
    auto fieldRetType = fieldRetVal->GetType();
    CJC_ASSERT(fieldRetType->IsReferenceTypeWithRefDims(1));
    auto fieldRetTypeNoRef = StaticCast<ClassType*>(fieldRetType->StripAllRefs());
    auto autoEnvWrapperDef = GetOrCreateAutoEnvWrapper(*fieldRetTypeNoRef);

    // 2. convert return type
    auto newfieldRetType = builder.GetType<RefType>(autoEnvWrapperDef->GetSuperClassDef()->GetSuperClassTy());
    ConvertTypeFunc convertRetType = [&fieldRetType, &newfieldRetType](const Type& type) {
        CJC_ASSERT(&type == fieldRetType);
        return newfieldRetType;
    };
    PrivateTypeConverter converter(convertRetType, builder);
    converter.VisitValue(*fieldRetVal);

    // 3. create $Auto_Env_wrapper object
    auto parentBlock = field.GetParentBlock();
    auto autoEnvWrapperType = autoEnvWrapperDef->GetType();
    auto autoEnvWrapperRefType = builder.GetType<RefType>(autoEnvWrapperType);
    auto allocate = builder.CreateExpression<Allocate>(autoEnvWrapperRefType, autoEnvWrapperType, parentBlock);
    allocate->MoveAfter(&field);
    
    auto memberVar = fieldRetVal;
    auto storeMemberVar = builder.CreateExpression<StoreElementRef>(
        builder.GetUnitTy(), memberVar, allocate->GetResult(), std::vector<uint64_t>{0}, parentBlock);
    storeMemberVar->MoveAfter(allocate);

    // typecast from $Auto_Env_xxx_wrapper to $Auto_Env_InstBase
    auto typecast = builder.CreateExpression<TypeCast>(fieldRetType, allocate->GetResult(), parentBlock);
    typecast->MoveAfter(storeMemberVar);

    // 4. replace user
    ReplaceOperandWithAutoEnvWrapperClass(*fieldRetVal, *typecast->GetResult(), {storeMemberVar});
}

void ClosureConversion::WrapTypeCastSrcVal(TypeCast& typecast)
{
    /** convert from:
     *  %0: $AutoEnvGenericBase& = xxx
     *  %1: $AutoEnvInstBase& = TypeCast(%0)
     *
     *  to:
     *  %0: $AutoEnvGenericBase& = xxx
     *  %2: $AutoEnvWrapperClass& = Allocate($AutoEnvWrapperClass)
     *  %3: Unit = StoreElementRef(%0, %2, 0)
     *  %1: $AutoEnvInstBase& = TypeCast(%3)
     */
    // 1. create $Auto_Env_wrapper
    auto targetTypeNoRef = StaticCast<ClassType*>(typecast.GetTargetTy()->StripAllRefs());
    auto autoEnvWrapperDef = GetOrCreateAutoEnvWrapper(*targetTypeNoRef);

    // 2. create $Auto_Env_wrapper object
    auto parentBlock = typecast.GetParentBlock();
    auto autoEnvWrapperType = autoEnvWrapperDef->GetType();
    auto autoEnvWrapperRefType = builder.GetType<RefType>(autoEnvWrapperType);
    auto allocate = builder.CreateExpression<Allocate>(autoEnvWrapperRefType, autoEnvWrapperType, parentBlock);
    allocate->MoveBefore(&typecast);
    
    auto srcVal = typecast.GetSourceValue();
    auto memberVar = srcVal;
    auto storeMemberVar = builder.CreateExpression<StoreElementRef>(
        builder.GetUnitTy(), memberVar, allocate->GetResult(), std::vector<uint64_t>{0}, parentBlock);
    storeMemberVar->MoveAfter(allocate);

    // 3. replace user
    typecast.ReplaceOperand(srcVal, allocate->GetResult());
}

void ClosureConversion::ModifyTypeMismatchInExpr()
{
    std::vector<Apply*> applyWrapRetVal;
    std::vector<ApplyWithException*> applyWithExceptionWrapRetVal;
    std::vector<Expression*> invokeWrapRetVal;
    std::vector<Expression*> invokeWithExceptionWrapRetVal;
    std::vector<GetElementRef*> getElementRefWrapRetVal;
    std::vector<Field*> fieldWrapRetVal;
    std::vector<TypeCast*> typecastWrapSrcVal;
    std::vector<std::pair<Expression*, std::vector<size_t>>> needTypeCastExprs;
    auto postVisit = [this, &applyWrapRetVal, &applyWithExceptionWrapRetVal, &invokeWrapRetVal,
        &invokeWithExceptionWrapRetVal, &getElementRefWrapRetVal, &fieldWrapRetVal, &typecastWrapSrcVal,
        &needTypeCastExprs](Expression& e) {
        if (e.GetExprKind() == ExprKind::APPLY) {
            if (ApplyRetValNeedWrapper(e)) {
                applyWrapRetVal.emplace_back(StaticCast<Apply*>(&e));
            }
            if (auto res = ApplyArgNeedTypeCast(StaticCast<Apply>(e)); res.first) {
                needTypeCastExprs.emplace_back(&e, res.second);
            }
        } else if (e.GetExprKind() == ExprKind::APPLY_WITH_EXCEPTION) {
            if (ApplyWithExceptionRetValNeedWrapper(StaticCast<ApplyWithException>(e))) {
                applyWithExceptionWrapRetVal.emplace_back(StaticCast<ApplyWithException>(&e));
            }
            if (auto res = ApplyWithExceptionArgNeedTypeCast(StaticCast<ApplyWithException>(e)); res.first) {
                needTypeCastExprs.emplace_back(&e, res.second);
            }
        } else if (InvokeRetValNeedWrapper(e)) {
            invokeWrapRetVal.emplace_back(&e);
        } else if (InvokeWithExceptionRetValNeedWrapper(e)) {
            invokeWithExceptionWrapRetVal.emplace_back(&e);
        } else if (auto enumRes = EnumConstructorNeedTypeCast(e); enumRes.first) {
            needTypeCastExprs.emplace_back(&e, enumRes.second);
        } else if (auto tupleRes = TupleNeedTypeCast(e); tupleRes.first) {
            needTypeCastExprs.emplace_back(&e, tupleRes.second);
        } else if (GetElementRefRetValNeedWrapper(e, builder)) {
            getElementRefWrapRetVal.emplace_back(StaticCast<GetElementRef*>(&e));
        } else if (FieldRetValNeedWrapper(e, builder)) {
            fieldWrapRetVal.emplace_back(StaticCast<Field*>(&e));
        } else if (auto arrRes = RawArrayInitByValueNeedTypeCast(e); arrRes.first) {
            needTypeCastExprs.emplace_back(&e, arrRes.second);
        } else if (TypeCastSrcValNeedWrapper(e)) {
            typecastWrapSrcVal.emplace_back(StaticCast<TypeCast*>(&e));
        } else if (auto spawnRes = SpawnNeedTypeCast(e); spawnRes.first) {
            needTypeCastExprs.emplace_back(&e, spawnRes.second);
        }
        return VisitResult::CONTINUE;
    };
    for (auto func : package.GetGlobalFuncs()) {
        if (func->TestAttr(Attribute::SKIP_ANALYSIS)) {
            continue;
        }
        Visitor::Visit(
            *func, [](Expression&) { return VisitResult::CONTINUE; }, postVisit);
    }
    for (auto e : applyWrapRetVal) {
        WrapApplyRetVal(*e);
    }
    for (auto e : applyWithExceptionWrapRetVal) {
        WrapApplyWithExceptionRetVal(*e);
    }
    for (auto e : invokeWrapRetVal) {
        WrapInvokeRetVal(*e);
    }
    for (auto e : invokeWithExceptionWrapRetVal) {
        WrapInvokeWithExceptionRetVal(*e);
    }
    for (auto e : getElementRefWrapRetVal) {
        WrapGetElementRefRetVal(*e);
    }
    for (auto e : fieldWrapRetVal) {
        WrapFieldRetVal(*e);
    }
    for (auto e : typecastWrapSrcVal) {
        WrapTypeCastSrcVal(*e);
    }
    for (auto& e : needTypeCastExprs) {
        AddTypeCastForOperand(e, builder);
    }
}

void ClosureConversion::Convert()
{
    ConvertGlobalFunctions();
    auto nestedFuncs = CollectNestedFunctions();
    InlineLambda(nestedFuncs);
    nestedFuncs = CollectNestedFunctions();
    ConvertNestedFunctions(nestedFuncs);
    ConvertImportedFunctions();
    ConvertExpressions();
    LiftType();
    ModifyTypeMismatchInExpr();
    CreateVTableForAutoEnvDef();
}
