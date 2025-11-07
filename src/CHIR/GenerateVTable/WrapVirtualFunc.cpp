// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/GenerateVTable/WrapVirtualFunc.h"

#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/CHIR/AST2CHIR/Utils.h"
#include "cangjie/Mangle/CHIRManglingUtils.h"

using namespace Cangjie;
using namespace Cangjie::CHIR;

namespace {
bool FuncTypeHasOuterGenericType(const FuncType& funcType, const std::vector<GenericType*>& funcGenericTypeParams)
{
    auto unorderedGenericTypeParams =
        std::unordered_set<const Type*>(funcGenericTypeParams.begin(), funcGenericTypeParams.end());
    bool hasOuterGenericType = false;
    auto visitor = [&unorderedGenericTypeParams, &hasOuterGenericType](const Type& type) {
        if (hasOuterGenericType) {
            return true;
        }
        if (type.IsGeneric() && unorderedGenericTypeParams.find(&type) == unorderedGenericTypeParams.end()) {
            hasOuterGenericType = true;
        }
        return true;
    };
    funcType.VisitTypeRecursively(visitor);
    return hasOuterGenericType;
}

bool FuncTypeMatch(const FuncType& parentFuncType, const FuncType& curFuncType)
{
    auto parentFuncParamTypes = parentFuncType.GetParamTypes();
    auto curFuncParamTypes = curFuncType.GetParamTypes();
    CJC_ASSERT(parentFuncParamTypes.size() == curFuncParamTypes.size());
    for (size_t i = 0; i < parentFuncParamTypes.size(); ++i) {
        if (!VirMethodParamTypeIsMatched(*parentFuncParamTypes[i], *curFuncParamTypes[i])) {
            return false;
        }
    }
    return VirMethodReturnTypeIsMatched(*parentFuncType.GetReturnType(), *curFuncType.GetReturnType());
}

bool FuncIsOverride(const FuncBase& curFunc, const Type& selfTy)
{
    return curFunc.GetParentCustomTypeDef()->GetType() == &selfTy;
}

bool JudgeIfNeedVirtualWrapper(
    const VirtualMethodInfo& parentFuncInfo, const FuncBase& virtualFunc, const Type& selfTy)
{
    /** if struct and enum inherit interface, for non-static function, wrapper func is needed
     *  because `this` in struct is value type, and in interface is ref type
     *  but for static function, we need to follow rules below
     */
    if (!selfTy.IsClassOrArray() && !virtualFunc.TestAttr(Attribute::STATIC)) {
        return true;
    }

    /*  first, we need to clear a defination `generic-related`, if we say a function is generic-related,
        that means there is generic type in function type, and this generic type must be from current function,
        not from its parent class
        e.g.
        class A<T> {
            func foo1(a: T) {}         // foo1 is NOT generic-related, because T is from class A
            func foo2(a: A<T>) {}      // foo2 is NOT generic-related, because T is from class A
            func foo3() {}             // foo3 is NOT generic-related, because there isn't generic type
            func goo1<U>(b: U) {}      // goo1 is generic-related, because U is from func goo1
            func goo2<U>(): A<U> {}    // goo2 is generic-related, because U is from func goo2
        }
        there are some rules to create or NOT create wrapper function:
        1. virtual function is NOT generic-related, and its parent CustomTypeDef doesn't have generic type param
            wrapper function doesn't need to be created
        2. virtual function is NOT generic-related, but its parent CustomTypeDef has generic type param
          2.1 there is NOT override function in sub CustomTypeDef
            wrapper function needs to be created
          2.2 there is override function in sub CustomTypeDef
            wrapper function doesn't need to be created
        3. virtual function is generic-related
          3.1 there is NOT override function in sub CustomTypeDef
            wrapper function needs to be created
          3.2 there is override function in sub CustomTypeDef
            3.2.1 if the function type in sub CustomTypeDef's virtual function is matched
                  the function type in parent CustomTypeDef's virtual function, wrapper function isn't needed
            3.2.2 if the function type in sub CustomTypeDef's virtual function is mismatched
                  the function type in parent CustomTypeDef's virtual function, wrapper function is needed

        there are some rules to explain what is function type MATCHED:
        1. for param type
            generic type and ref type are matched, including generic T and generic U is matched,
            class A and class B is matched, generic T and class A is matched
            for other types, CHIR pointer must be same
        2. for return type
            class A and class B are matched, generic T and generic U are matched, class A and generic T
            are mismatched
            for other types, CHIR pointer must be same
    */
    auto parentDef = virtualFunc.GetParentCustomTypeDef();
    CJC_NULLPTR_CHECK(parentDef);
    bool parentDefHasGenericType = !parentDef->GetGenericTypeParams().empty();
    auto parentFuncType = parentFuncInfo.GetOriginalFuncType();
    auto parentFuncTypeParams = parentFuncInfo.GetGenericTypeParams();
    bool funcHasOuterGenericType = FuncTypeHasOuterGenericType(*parentFuncType, parentFuncTypeParams);
    if (!funcHasOuterGenericType && !parentDefHasGenericType) {
        return false;
    } else if (!funcHasOuterGenericType && parentDefHasGenericType) {
        return !FuncIsOverride(virtualFunc, selfTy);
    } else {
        if (!FuncIsOverride(virtualFunc, selfTy)) {
            return true;
        } else {
            return !FuncTypeMatch(*parentFuncType, *virtualFunc.GetFuncType());
        }
    }
}

// maybe we can not deserialize virutal wrapper function, because it's not in source code
void TryDeleteVirtuallWrapperFunc(CustomTypeDef& customTypeDef, FuncBase& finalFunc, CHIRBuilder& builder)
{
    if (!customTypeDef.TestAttr(Attribute::PLATFORM)) {
        return;
    }
    if (!finalFunc.TestAttr(Attribute::FINAL)) {
        return;
    }
    auto allMethods = customTypeDef.GetMethods();
    for (auto method : allMethods) {
        if (!method->TestAttr(Attribute::DESERIALIZED) || !method->TestAttr(Attribute::VIRTUAL)) {
            continue;
        }
        if (finalFunc.Get<WrappedRawMethod>()->GetSrcCodeIdentifier() !=
            method->Get<WrappedRawMethod>()->GetSrcCodeIdentifier()) {
            continue;
        }
        if (finalFunc.GetFuncType()->IsEqualOrSubTypeOf(*method->GetFuncType(), builder)) {
            method->FuncBase::DestroySelf();
            return;
        }
    }
}
} // namespace

WrapVirtualFunc::WrapVirtualFunc(CHIRBuilder& builder,
    const CompilationCache& increCachedInfo, const IncreKind incrementalKind, const bool targetIsWin)
    : builder(builder), increCachedInfo(increCachedInfo), incrementalKind(incrementalKind), targetIsWin(targetIsWin)
{
}

void WrapVirtualFunc::CheckAndWrap(CustomTypeDef& customTypeDef)
{
    auto selfTy = customTypeDef.GetType();
    for (auto& vtableIt : customTypeDef.GetDefVTable().GetTypeVTables()) {
        auto parentTy = vtableIt.GetSrcParentType();
        if (parentTy == selfTy) {
            continue;
        }
        auto parentDef = parentTy->GetClassDef();
        auto& parentVTable = parentDef->GetDefVTable().GetExpectedTypeVTable(*parentDef->GetType());
        CJC_ASSERT(!parentVTable.IsEmpty());
        CJC_ASSERT(parentVTable.GetMethodNum() == vtableIt.GetMethodNum());
        for (size_t i = 0; i < vtableIt.GetMethodNum(); ++i) {
            auto& curVirMethodInfo = vtableIt.GetVirtualMethods()[i];
            if (curVirMethodInfo.GetVirtualMethod() == nullptr) {
                continue;
            }
            auto& parentVirMethodInfo = parentVTable.GetVirtualMethods()[i];
            auto wrapper = CreateVirtualWrapperIfNeeded(curVirMethodInfo, parentVirMethodInfo, *selfTy, customTypeDef, *parentTy);
            if (incrementalKind != IncreKind::INVALID) {
                HandleVirtualFuncWrapperForIncrCompilation(wrapper, *curVirMethodInfo.GetVirtualMethod());
            }
            if (!wrapper) {
                continue;
            }
            customTypeDef.UpdateVtableItem(*parentTy, i, wrapper, selfTy);
        }
    }
}

void WrapVirtualFunc::HandleVirtualFuncWrapperForIncrCompilation(const FuncBase* wrapper, const FuncBase& curFunc)
{
    CJC_ASSERT(!curFunc.GetSrcCodeIdentifier().empty());
    const auto& cachedVirDep = increCachedInfo.virtualFuncDep;
    const std::string& rawMangleName = curFunc.GetRawMangledName();
    // compiler add func doesn't have rawMangle
    if (curFunc.TestAttr(Attribute::COMPILER_ADD) && rawMangleName.empty()) {
        return;
    }
    if (rawMangleName.empty()) {
        CJC_ABORT();
        return;
    }
    if (wrapper) {
        curVirtFuncWrapDep.emplace(rawMangleName, wrapper->GetIdentifierWithoutPrefix());
    }
    if (incrementalKind != IncreKind::INCR || cachedVirDep.empty()) {
        return;
    }
    if (auto it = cachedVirDep.find(rawMangleName); it != cachedVirDep.end()) {
        if (wrapper) {
            if (it->second != wrapper->GetIdentifierWithoutPrefix()) {
                delVirtFuncWrapForIncr[rawMangleName] = it->second;
            }
        } else {
            delVirtFuncWrapForIncr[rawMangleName] = it->second;
        }
    }
}

WrapVirtualFunc::WrapperFuncGenericTable WrapVirtualFunc::GetReplaceTableForVirtualFunc(
    const ClassType& parentTy, const std::string& funcIdentifier, const VirtualMethodInfo& parentFuncInfo)
{
    auto genericParentTypeArgs = parentTy.GetCustomTypeDef()->GetGenericTypeParams();
    auto instParentTypeArgs = parentTy.GetTypeArgs();
    if (!genericParentTypeArgs.empty()) {
        CJC_ASSERT(genericParentTypeArgs.size() == instParentTypeArgs.size());
    }
    WrapperFuncGenericTable resultTable;
    size_t newGenericIdx = 0;
    for (size_t i = 0; i < genericParentTypeArgs.size(); ++i) {
        resultTable.replaceTable.emplace(genericParentTypeArgs[i], instParentTypeArgs[i]);
    }
    auto parentMethodGenericTys = parentFuncInfo.GetGenericTypeParams();
    for (auto& parentMethodGenericTy : parentMethodGenericTys) {
        auto srcIdentifier = "fT" + std::to_string(newGenericIdx++);
        auto tyIdentifier = funcIdentifier + '_' + srcIdentifier;
        auto funcGenericParam = builder.GetType<GenericType>(tyIdentifier, srcIdentifier);
        resultTable.funcGenericTypeParams.emplace_back(funcGenericParam);
        resultTable.replaceTable.emplace(parentMethodGenericTy, funcGenericParam);
    }
    CJC_ASSERT(resultTable.funcGenericTypeParams.size() == parentMethodGenericTys.size());
    for (size_t i = 0; i < resultTable.funcGenericTypeParams.size(); ++i) {
        std::vector<Type*> newUpperBounds;
        for (auto ty : parentMethodGenericTys[i]->GetUpperBounds()) {
            newUpperBounds.emplace_back(ReplaceRawGenericArgType(*ty, resultTable.replaceTable, builder));
        }
        resultTable.funcGenericTypeParams[i]->SetUpperBounds(newUpperBounds);
    }
    return resultTable;
}

void WrapVirtualFunc::CreateWrapperFuncBody(Func& wrapperFunc,
    const VirtualMethodInfo& childFuncInfo, Type& selfTy, WrapVirtualFunc::WrapperFuncGenericTable& genericTable)
{
    /*
        interface I<T> {
            func foo(a: T) {}
        }
            class <: I<Bool> {
                func foo(a: Bool) {}
                public func foo_wrapper(a: Box<Bool>&) {
                    foo(UnBox(a))
                }
            }
    */
    // if there is override method in child class, `rawFunc` is this method, otherwise is parent class method
    auto rawFunc = childFuncInfo.GetVirtualMethod();
    auto wrapperFuncType = wrapperFunc.GetFuncType();
    auto wrapperRetTy = wrapperFuncType->GetReturnType();
    auto body = builder.CreateBlockGroup(wrapperFunc);
    wrapperFunc.InitBody(*body);

    std::vector<Value*> args;
    for (auto ty : wrapperFuncType->GetParamTypes()) {
        args.emplace_back(builder.CreateParameter(ty, INVALID_LOCATION, wrapperFunc));
    }

    auto entry = builder.CreateBlock(body);
    body->SetEntryBlock(entry);
    auto ret =
        CreateAndAppendExpression<Allocate>(builder, builder.GetType<RefType>(wrapperRetTy), wrapperRetTy, entry);
    wrapperFunc.SetReturnValue(*ret->GetResult());

    Type* instParentType = childFuncInfo.GetInstParentType();
    if (instParentType->IsClassOrArray() || (instParentType->IsStruct() && rawFunc->TestAttr(Attribute::MUT))) {
        instParentType = builder.GetType<RefType>(instParentType);
    }
    auto expectedParamTypes = wrapperFuncType->GetParamTypes();
    for (auto& ty : expectedParamTypes) {
        ty = RemoveBoxTypeShellIfNeed(*ty);
    }
    if (!rawFunc->TestAttr(Attribute::STATIC)) {
        expectedParamTypes[0] = instParentType;
    }

    Type* applyRetTy = rawFunc->GetFuncType()->GetReturnType();
    if (applyRetTy->IsGenericRelated()) {
        applyRetTy = RemoveBoxTypeShellIfNeed(*wrapperRetTy);
    }

    CJC_ASSERT(args.size() == expectedParamTypes.size());
    for (size_t i = 0; i < expectedParamTypes.size(); ++i) {
        args[i] = TypeCastOrBoxIfNeeded(*args[i], *expectedParamTypes[i], builder, *entry, INVALID_LOCATION);
    }
    std::vector<Type*> instArgTypes;
    instArgTypes.reserve(genericTable.funcGenericTypeParams.size());
    for (auto& funcGenericParam : genericTable.funcGenericTypeParams) {
        instArgTypes.emplace_back(funcGenericParam);
    }
    auto thisInstTy = &selfTy;
    if (thisInstTy->IsClassOrArray()) {
        thisInstTy = builder.GetType<RefType>(thisInstTy);
    }
    auto apply = CreateAndAppendExpression<Apply>(builder, applyRetTy, rawFunc, FuncCallContext{
        .args = args,
        .instTypeArgs = instArgTypes,
        .thisType = thisInstTy}, entry);

    auto res = TypeCastOrBoxIfNeeded(*apply->GetResult(), *wrapperRetTy, builder, *entry, INVALID_LOCATION);
    CreateAndAppendExpression<Store>(builder, builder.GetUnitTy(), res, wrapperFunc.GetReturnValue(), entry);
    entry->AppendExpression(builder.CreateTerminator<Exit>(entry));
}

FuncType* WrapVirtualFunc::GetWrapperFuncType(FuncType& parentFuncTyWithoutThisArg,
    Type& selfTy, const std::unordered_map<const GenericType*, Type*>& replaceTable, bool isStatic)
{
    auto erasedFuncTy =
        StaticCast<FuncType*>(ReplaceRawGenericArgType(parentFuncTyWithoutThisArg, replaceTable, builder));
    auto wrapperParamTypes = erasedFuncTy->GetParamTypes();
    auto wrapperRetTy = erasedFuncTy->GetReturnType();
    auto parentParamTypes = parentFuncTyWithoutThisArg.GetParamTypes();
    auto parentRetType = parentFuncTyWithoutThisArg.GetReturnType();
    CJC_ASSERT(wrapperParamTypes.size() == parentParamTypes.size());
    for (size_t i = 0; i < wrapperParamTypes.size(); ++i) {
        if (!VirMethodParamTypeIsMatched(*wrapperParamTypes[i], *parentParamTypes[i])) {
            wrapperParamTypes[i] = CreateBoxRefTypeIfNeed(*wrapperParamTypes[i], builder);
        }
    }
    if (!VirMethodReturnTypeIsMatched(*wrapperRetTy, *parentRetType)) {
        wrapperRetTy = CreateBoxRefTypeIfNeed(*wrapperRetTy, builder);
    }

    if (!isStatic) {
        Type* thisArgTy = builder.GetType<RefType>(&selfTy); // builder.GetType<RefType>(funcInfo.typeInfo.parentType);
        if (!selfTy.IsClassOrArray()) {
            thisArgTy = builder.GetType<RefType>(builder.GetAnyTy());
        }
        wrapperParamTypes.insert(wrapperParamTypes.begin(), thisArgTy);
    }
    return builder.GetType<FuncType>(wrapperParamTypes, wrapperRetTy);
}

FuncType* WrapVirtualFunc::RemoveThisArg(FuncType* funcTy)
{
    auto paramTys = funcTy->GetParamTypes();
    auto retTy = funcTy->GetReturnType();
    return builder.GetType<FuncType>(std::vector<Type*>(paramTys.begin() + 1, paramTys.end()), retTy);
}

// change this function to class method, to much args in functions called by this function
FuncBase* WrapVirtualFunc::CreateVirtualWrapperIfNeeded(const VirtualMethodInfo& funcInfo,
    const VirtualMethodInfo& parentFuncInfo, Type& selfTy, CustomTypeDef& customTypeDef, const ClassType& parentTy)
{
    auto curFunc = funcInfo.GetVirtualMethod();
    // 1. Judge if need virtual wrapper
    if (!JudgeIfNeedVirtualWrapper(parentFuncInfo, *curFunc, selfTy)) {
        return nullptr;
    }
    auto isStatic = curFunc->TestAttr(Attribute::STATIC);
    auto parentFuncTy = parentFuncInfo.GetOriginalFuncType();
    auto parentFuncTyWithoutThisArg = !isStatic ? RemoveThisArg(parentFuncTy) : parentFuncTy;
    auto funcIdentifier = CHIRMangling::GenerateVirtualFuncMangleName(curFunc, customTypeDef, &parentTy, true);
    // ensure every method is wrapped exactly once. This occurs in split operator, where three vtable entries use
    // the same function
    if (auto ca = wrapperCache.find(funcIdentifier); ca != wrapperCache.cend()) {
        return ca->second;
    }
    // 2. Collect replace map for generic type params
    auto genericTable = GetReplaceTableForVirtualFunc(parentTy, funcIdentifier, parentFuncInfo);
    // 3. Get func type
    auto wrapperTy =
        GetWrapperFuncType(*parentFuncTyWithoutThisArg, selfTy, genericTable.replaceTable, isStatic);
    // 4. Create the function
    auto funcSrcIdentifier = "";
    auto rawMangledName = "";
    auto packageName = customTypeDef.GetPackageName();
    bool isImported =
        customTypeDef.TestAttr(Attribute::IMPORTED) && parentTy.GetCustomTypeDef()->TestAttr(Attribute::IMPORTED);
    bool parentIsFromExtend = ParentDefIsFromExtend(customTypeDef, *parentTy.GetClassDef());
    FuncBase* funcBase = nullptr;
    if (isImported && !parentIsFromExtend) {
        funcBase = builder.CreateImportedVarOrFunc<ImportedFunc>(wrapperTy,
            funcIdentifier, funcSrcIdentifier, rawMangledName, packageName, genericTable.funcGenericTypeParams);
    } else {
        funcBase = builder.CreateFunc(INVALID_LOCATION, wrapperTy, funcIdentifier, funcSrcIdentifier, "", packageName,
            genericTable.funcGenericTypeParams);
    }
    CJC_NULLPTR_CHECK(funcBase);
    funcBase->Set<WrappedRawMethod>(curFunc);
    funcBase->AppendAttributeInfo(curFunc->GetAttributeInfo());
    funcBase->EnableAttr(Attribute::NO_REFLECT_INFO);
    funcBase->EnableAttr(Attribute::NO_DEBUG_INFO);
    customTypeDef.AddMethod(funcBase);
    // For cjmp, try delete virutal wrapper function when final func gererated.
    TryDeleteVirtuallWrapperFunc(customTypeDef, *funcBase, builder);
    funcBase->Set<LinkTypeInfo>(Linkage::EXTERNAL);
    if (parentIsFromExtend) {
        // weak_odr is invalid in windows, we have to set internal
        if (targetIsWin) {
            funcBase->Set<LinkTypeInfo>(Linkage::INTERNAL);
        } else {
            funcBase->Set<LinkTypeInfo>(Linkage::WEAK_ODR);
        }
    }

    wrapperCache[std::move(funcIdentifier)] = funcBase;
    if (isImported && !parentIsFromExtend) {
        return funcBase;
    }
    // 5. Create the function body if the raw function is not imported
    auto func = StaticCast<Func*>(funcBase);
    CreateWrapperFuncBody(*func, funcInfo, selfTy, genericTable);
    return func;
}

VirtualWrapperDepMap&& WrapVirtualFunc::GetCurVirtFuncWrapDep()
{
    return std::move(curVirtFuncWrapDep);
}

VirtualWrapperDepMap&& WrapVirtualFunc::GetDelVirtFuncWrapForIncr()
{
    return std::move(delVirtFuncWrapForIncr);
}
