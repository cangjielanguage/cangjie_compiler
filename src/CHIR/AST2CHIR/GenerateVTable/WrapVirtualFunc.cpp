// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/AST2CHIR/GenerateVTable/WrapVirtualFunc.h"

#include "cangjie/CHIR/CHIRBuilder.h"
#include "cangjie/CHIR/Utils.h"
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
    std::function<bool(const Type&, const Type&)> typeIsMatched =
        [&typeIsMatched](const Type& type1, const Type& type2) {
        if (type1.GetTypeKind() != type2.GetTypeKind()) {
            return false;
        }
        if (type1.IsGeneric() && type2.IsGeneric()) {
            return true;
        }
        auto typeArgs1 = type1.GetTypeArgs();
        auto typeArgs2 = type2.GetTypeArgs();
        if (typeArgs1.size() != typeArgs2.size()) {
            return false;
        }
        for (size_t i = 0; i < typeArgs1.size(); ++i) {
            if (!typeIsMatched(*typeArgs1[i], *typeArgs2[i])) {
                return false;
            }
        }
        return true;
    };
    auto paramTypeIsMatched = [&typeIsMatched](const Type& type1, const Type& type2) {
        if ((type1.IsGeneric() || type1.IsRef()) && (type2.IsGeneric() || type2.IsRef())) {
            return true;
        } else {
            return typeIsMatched(type1, type2);
        }
    };
    auto parentFuncParamTypes = parentFuncType.GetParamTypes();
    auto curFuncParamTypes = curFuncType.GetParamTypes();
    CJC_ASSERT(parentFuncParamTypes.size() == curFuncParamTypes.size());
    for (size_t i = 0; i < parentFuncParamTypes.size(); ++i) {
        if (!paramTypeIsMatched(*parentFuncParamTypes[i], *curFuncParamTypes[i])) {
            return false;
        }
    }
    auto returnTypeIsMatched = [&typeIsMatched](const Type& type1, const Type& type2) {
        if (type1.IsGeneric() && type2.IsGeneric()) {
            return true;
        }
        if (type1.IsRef() && type2.IsRef()) {
            return true;
        }
        return typeIsMatched(type1, type2);
    };
    return returnTypeIsMatched(*parentFuncType.GetReturnType(), *curFuncType.GetReturnType());
}

bool FuncIsOverride(const FuncBase& curFunc, const Type& selfTy)
{
    return curFunc.GetParentCustomTypeDef()->GetType() == &selfTy;
}

bool JudgeIfNeedVirtualWrapper(const VirtualFuncInfo& parentFuncInfo, const FuncBase& virtualFunc, const Type& selfTy)
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
    auto parentFuncType = parentFuncInfo.typeInfo.originalType;
    auto parentFuncTypeParams = parentFuncInfo.typeInfo.methodGenericTypeParams;
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
}

WrapVirtualFunc::WrapVirtualFunc(CHIRBuilder& builder,
    const CompilationCache& increCachedInfo, const IncreKind incrementalKind, const bool targetIsWin)
    : builder(builder), increCachedInfo(increCachedInfo), incrementalKind(incrementalKind), targetIsWin(targetIsWin)
{
}

void WrapVirtualFunc::CheckAndWrap(CustomTypeDef& customTypeDef)
{
    auto selfTy = customTypeDef.GetType();
    for (auto& [parentTy, infos] : customTypeDef.GetVTable()) {
        if (parentTy == selfTy) {
            continue;
        }
        auto parentDef = parentTy->GetCustomTypeDef();
        auto parentVTableIt = parentDef->GetVTable().find(StaticCast<ClassType*>(parentDef->GetType()));
        CJC_ASSERT(parentVTableIt != parentDef->GetVTable().end());
        CJC_ASSERT(parentVTableIt->second.size() == infos.size());
        for (size_t i = 0; i < infos.size(); ++i) {
            auto& info = infos[i];
            if (!info.instance) {
                continue;
            }
            auto& parentInfo = parentVTableIt->second[i];
            auto wrapper = CreateVirtualWrapperIfNeeded(info, parentInfo, *selfTy, customTypeDef, *parentTy);
            if (incrementalKind != IncreKind::INVALID) {
                HandleVirtualFuncWrapperForIncrCompilation(wrapper, *info.instance);
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
    const ClassType& parentTy, const std::string& funcIdentifier, const VirtualFuncInfo& parentFuncInfo)
{
    auto genericParentTypeArgs = parentTy.GetCustomTypeDef()->GetGenericTypeParams();
    auto instParentTypeArgs = parentTy.GetTypeArgs();
    if (!genericParentTypeArgs.empty()) {
        CJC_ASSERT(genericParentTypeArgs.size() == instParentTypeArgs.size());
    }
    WrapperFuncGenericTable resultTable;
    size_t newGenericIdx = 0;
    for (size_t i = 0; i < genericParentTypeArgs.size(); ++i) {
        auto instArgTy = instParentTypeArgs[i];
        if (instArgTy->IsGeneric() || instArgTy->IsClassOrArray()) {
            resultTable.replaceTable.emplace(genericParentTypeArgs[i], instArgTy);
        } else {
            auto srcIdentifier = 'T' + std::to_string(newGenericIdx++);
            auto tyIdentifier = funcIdentifier + '_' + srcIdentifier;
            auto funcGenericParam = builder.GetType<GenericType>(tyIdentifier, srcIdentifier);
            funcGenericParam->orphanFlag = true;
            funcGenericParam->SetUpperBounds(std::vector<Type*>{instArgTy});
            funcGenericParam->skipCheck = true;
            resultTable.replaceTable.emplace(genericParentTypeArgs[i], funcGenericParam);
            resultTable.inverseReplaceTable.emplace(funcGenericParam, instArgTy);
        }
    }
    auto& parentMethodGenericTys = parentFuncInfo.typeInfo.methodGenericTypeParams;
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

void WrapVirtualFunc::CreateVirtualWrapperFunc(Func& func, FuncType& wrapperTy,
    const VirtualFuncInfo& funcInfo, Type& selfTy, WrapVirtualFunc::WrapperFuncGenericTable& genericTable)
{
    auto rawFunc = funcInfo.instance;
    auto wrapperRetTy = wrapperTy.GetReturnType();
    BlockGroup* body = builder.CreateBlockGroup(func);
    func.InitBody(*body);

    std::vector<Value*> args;
    for (auto ty : wrapperTy.GetParamTypes()) {
        args.emplace_back(builder.CreateParameter(ty, INVALID_LOCATION, func));
    }

    auto entry = builder.CreateBlock(body);
    body->SetEntryBlock(entry);
    auto ret =
        CreateAndAppendExpression<Allocate>(builder, builder.GetType<RefType>(wrapperRetTy), wrapperRetTy, entry);
    func.SetReturnValue(*ret->GetResult());

    Type* instParentType = funcInfo.typeInfo.parentType;
    if (instParentType->IsClassOrArray() || (instParentType->IsStruct() && rawFunc->TestAttr(Attribute::MUT))) {
        instParentType = builder.GetType<RefType>(instParentType);
    }
    auto instTy =
        StaticCast<FuncType*>(ReplaceRawGenericArgType(wrapperTy, genericTable.inverseReplaceTable, builder));
    auto paramInstTy = instTy->GetParamTypes();
    if (!rawFunc->TestAttr(Attribute::STATIC)) {
        paramInstTy[0] = instParentType;
    }

    Type* applyRetTy = rawFunc->GetFuncType()->GetReturnType();
    if (applyRetTy->IsGenericRelated()) {
        applyRetTy = instTy->GetReturnType();
    }

    CJC_ASSERT(args.size() == paramInstTy.size());
    for (size_t i = 0; i < paramInstTy.size(); ++i) {
        args[i] = TypeCastOrBoxIfNeeded(*args[i], *paramInstTy[i], builder, *entry, INVALID_LOCATION);
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
    CreateAndAppendExpression<Store>(builder, builder.GetUnitTy(), res, func.GetReturnValue(), entry);
    entry->AppendExpression(builder.CreateTerminator<Exit>(entry));
}

FuncType* WrapVirtualFunc::GetWrapperFuncType(FuncType& parentFuncTyWithoutThisArg,
    Type& selfTy, const std::unordered_map<const GenericType*, Type*>& replaceTable, bool isStatic)
{
    auto erasedFuncTy =
        StaticCast<FuncType*>(ReplaceRawGenericArgType(parentFuncTyWithoutThisArg, replaceTable, builder));
    auto wrapperParamTy = erasedFuncTy->GetParamTypes();
    auto wrapperRetTy = erasedFuncTy->GetReturnType();

    if (!isStatic) {
        Type* thisArgTy = builder.GetType<RefType>(&selfTy); // builder.GetType<RefType>(funcInfo.typeInfo.parentType);
        if (!selfTy.IsClassOrArray()) {
            thisArgTy = builder.GetType<RefType>(builder.GetAnyTy());
        }
        wrapperParamTy.insert(wrapperParamTy.begin(), thisArgTy);
    }
    return builder.GetType<FuncType>(wrapperParamTy, wrapperRetTy);
}

FuncType* WrapVirtualFunc::RemoveThisArg(FuncType* funcTy)
{
    auto paramTys = funcTy->GetParamTypes();
    auto retTy = funcTy->GetReturnType();
    return builder.GetType<FuncType>(std::vector<Type*>(paramTys.begin() + 1, paramTys.end()), retTy);
}

// change this function to class method, to much args in functions called by this function
FuncBase* WrapVirtualFunc::CreateVirtualWrapperIfNeeded(const VirtualFuncInfo& funcInfo,
    const VirtualFuncInfo& parentFuncInfo, Type& selfTy, CustomTypeDef& customTypeDef, const ClassType& parentTy)
{
    auto curFunc = funcInfo.instance;
    // 1. Judge if need virtual wrapper
    if (!JudgeIfNeedVirtualWrapper(parentFuncInfo, *curFunc, selfTy)) {
        return nullptr;
    }
    auto isStatic = curFunc->TestAttr(Attribute::STATIC);
    auto parentFuncTy = parentFuncInfo.typeInfo.originalType;
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
    customTypeDef.AddMethod(funcBase);
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
    CreateVirtualWrapperFunc(*func, *wrapperTy, funcInfo, selfTy, genericTable);
    return func;
}

VirtualWrapperDepMap WrapVirtualFunc::GetCurVirtFuncWrapDep() const
{
    return curVirtFuncWrapDep;
}

VirtualWrapperDepMap WrapVirtualFunc::GetDelVirtFuncWrapForIncr() const
{
    return delVirtFuncWrapForIncr;
}