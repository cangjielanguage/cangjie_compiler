// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#include "cangjie/CHIR/Transformation/GenerateVTable/VTableGenerator.h"

#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/IR/Type/ClassDef.h"
#include "cangjie/CHIR/IR/Type/ExtendDef.h"
#include "cangjie/CHIR/IR/Type/Type.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include <utility>

using namespace Cangjie;
using namespace Cangjie::CHIR;

namespace {
// `key` is generic type param in def and def's extends
// `value` is instantiated type args in current CustomType
std::unordered_map<const GenericType*, Type*> GetInstMapFromDefAndExtends(const CustomType& type)
{
    std::unordered_map<const GenericType*, Type*> replaceTable;
    auto def = type.GetCustomTypeDef();
    // `def` may be generic decl, generic instantiated decl or just normal decl
    // only generic decl can generate replaceTable
    if (!def->TestAttr(Attribute::GENERIC)) {
        return replaceTable;
    }
    auto genericParams = def->GetGenericTypeParams();
    auto instantiatedArgs = type.GetTypeArgs();
    if (!genericParams.empty()) {
        CJC_ASSERT(genericParams.size() == instantiatedArgs.size());
    }
    for (size_t i = 0; i < genericParams.size(); ++i) {
        // e.g. interface I<T> {}; open class A<T> <: I<(T, T)> {}; class B <: A<Int32> {}
        // when `classType` is `A<Int32>`, item in `replaceTable` is {A::T, Int32}
        replaceTable.emplace(genericParams[i], instantiatedArgs[i]);
    }

    for (auto exDef : def->GetExtends()) {
        auto genericParamsInExtendTy = exDef->GetExtendedType()->GetTypeArgs();
        CJC_ASSERT(genericParamsInExtendTy.size() == instantiatedArgs.size());
        for (size_t i = 0; i < genericParamsInExtendTy.size(); ++i) {
            if (auto genericTy = DynamicCast<GenericType*>(genericParamsInExtendTy[i])) {
                replaceTable.emplace(genericTy, instantiatedArgs[i]);
            }
        }
    }

    return replaceTable;
}

bool FuncMayBeInVtable(const FuncBase& func)
{
    if (func.IsConstructor() || func.IsFinalizer()) {
        return false;
    }
    if (func.GetFuncKind() == FuncKind::ANNOFACTORY_FUNC) {
        return false;
    }
    if (func.Get<WrappedRawMethod>() != nullptr) {
        return false;
    }
    if (func.TestAttr(Attribute::GENERIC_INSTANTIATED)) {
        return false;
    }
    return func.TestAttr(Attribute::PUBLIC) || func.TestAttr(Attribute::PROTECTED) ||
        (func.TestAttr(Attribute::STATIC) && !func.TestAttr(Attribute::PRIVATE));
}

void UpdateInstanceAttr(VTableInDef& vtable)
{
    // copy attr to instance
    for (auto& vtableIt : vtable.GetModifiableTypeVTables()) {
        for (auto& infoIt : vtableIt.GetModifiableVirtualMethods()) {
            if (infoIt.GetVirtualMethod() == nullptr) {
                continue;
            }
            if (!infoIt.GetVirtualMethod()->TestAttr(Attribute::VIRTUAL)) {
                infoIt.GetVirtualMethod()->EnableAttr(Attribute::FINAL);
            }
        }
    }
}

bool TypeIsMatched(const Type& type1, const Type& type2)
{
    if (&type1 == &type2) {
        return true;
    }
    if (type1.IsGeneric() || type2.IsGeneric()) {
        return true;
    }
    if (!type1.IsSameTypeKind(type2)) {
        return false;
    }
    if (auto customType1 = DynamicCast<const CustomType*>(&type1)) {
        if (customType1->GetCustomTypeDef() != StaticCast<const CustomType&>(type2).GetCustomTypeDef()) {
            return false;
        }
    }
    auto type1Args = type1.GetTypeArgs();
    auto type2Args = type2.GetTypeArgs();
    if (type1Args.size() != type2Args.size()) {
        return false;
    }
    for (size_t i = 0; i < type1Args.size(); ++i) {
        if (!TypeIsMatched(*type1Args[i], *type2Args[i])) {
            return false;
        }
    }
    return true;
}

std::vector<CustomTypeDef*> CollectBrotherDefs(const ExtendDef& curExtend, CHIRBuilder& builder)
{
    auto extendDefs = curExtend.GetExtendedType()->GetExtends(&builder);
    CJC_ASSERT(!extendDefs.empty());

    std::vector<CustomTypeDef*> brotherDefs;
    for (auto def : extendDefs) {
        if (def != &curExtend && TypeIsMatched(*def->GetExtendedType(), *curExtend.GetExtendedType())) {
            brotherDefs.emplace_back(def);
        }
    }

    auto extendedTy = DynamicCast<CustomType*>(curExtend.GetExtendedType());
    if (extendedTy != nullptr) {
        brotherDefs.emplace_back(extendedTy->GetCustomTypeDef());
    }
    return brotherDefs;
}
}

VTableGenerator::VTableGenerator(CHIRBuilder& builder)
    : builder(builder)
{
}

std::vector<FuncBase*> VTableGenerator::GetAllMethods(const Type& ty)
{
    std::vector<FuncBase*> methods;
    // all methods belonging to some type, include methods in class/sturct/enum/interface/extend
    if (auto customTy = DynamicCast<const CustomType*>(&ty); customTy) {
        auto other = customTy->GetCustomTypeDef()->GetMethods();
        methods.insert(methods.end(), other.begin(), other.end());
    }
    for (auto extendDef : ty.GetExtends(&builder)) {
        auto m = extendDef->GetMethods();
        methods.insert(methods.end(), m.begin(), m.end());
    }

    return methods;
}

std::vector<FuncBase*> VTableGenerator::GetAllMethods(const CustomTypeDef& def)
{
    std::vector<FuncBase*> allMethods;
    if (auto extendDef = DynamicCast<const ExtendDef*>(&def); extendDef) {
        if (const auto ty = extendDef->GetExtendedType(); ty) {
            allMethods = GetAllMethods(*ty);
        }
    } else {
        allMethods = GetAllMethods(*def.GetType());
    }
    return allMethods;
}

void VTableGenerator::CollectCurDefMethodsMayBeInVtable(const CustomTypeDef& def, std::vector<FuncBase*>& publicFuncs)
{
    for (auto func : GetAllMethods(def)) {
        if (FuncMayBeInVtable(*func)) {
            publicFuncs.emplace_back(func);
        }
    }
}

VirtualMethodInfo VTableGenerator::CreateVirtualFuncInfo(const AbstractMethodInfo& method,
    Type& originalParentType, const std::unordered_map<const GenericType*, Type*>& replaceTable)
{
    auto originalFuncType = StaticCast<FuncType*>(method.methodTy);
    auto originalParamTypes = originalFuncType->GetParamTypes();
    if (!method.TestAttr(Attribute::STATIC)) {
        originalParamTypes.erase(originalParamTypes.begin());
    }
    std::vector<Type*> instParamTypes;
    for (auto type : originalParamTypes) {
        instParamTypes.emplace_back(ReplaceRawGenericArgType(*type, replaceTable, builder));
    }
    auto sigType = builder.GetType<FuncType>(instParamTypes, builder.GetUnitTy());
    auto instParentType = ReplaceRawGenericArgType(originalParentType, replaceTable, builder);
    auto instRetType = ReplaceRawGenericArgType(*originalFuncType->GetReturnType(), replaceTable, builder);
    auto condition = FuncSigInfo{method.methodName, sigType, method.methodGenericTypeParams};
    return VirtualMethodInfo(
        std::move(condition), nullptr, method.attributeInfo, *originalFuncType, *instParentType, *instRetType);
}

VirtualMethodInfo VTableGenerator::CreateVirtualFuncInfo(
    FuncBase& method, Type& originalParentType, const std::unordered_map<const GenericType*, Type*>& replaceTable)
{
    auto originalFuncType = StaticCast<FuncType*>(method.GetType());
    auto originalParamTypes = originalFuncType->GetParamTypes();
    if (!method.TestAttr(Attribute::STATIC)) {
        originalParamTypes.erase(originalParamTypes.begin());
    }
    std::vector<Type*> instParamTypes;
    for (auto type : originalParamTypes) {
        instParamTypes.emplace_back(ReplaceRawGenericArgType(*type, replaceTable, builder));
    }
    auto sigType = builder.GetType<FuncType>(instParamTypes, builder.GetUnitTy());
    auto instParentType = ReplaceRawGenericArgType(originalParentType, replaceTable, builder);
    auto instRetType = ReplaceRawGenericArgType(*originalFuncType->GetReturnType(), replaceTable, builder);
    auto condition = FuncSigInfo{method.GetSrcCodeIdentifier(), sigType, method.GetGenericTypeParams()};
    return VirtualMethodInfo(
        std::move(condition), &method, method.GetAttributeInfo(), *originalFuncType, *instParentType, *instRetType);
}

bool VTableGenerator::IsSigTypeMatched(const VirtualMethodInfo& curFuncInfo, const VirtualMethodInfo& funcInfoInVtable)
{
    // static method can not override non-static method
    if (curFuncInfo.GetAttributeInfo().TestAttr(Attribute::STATIC) !=
        funcInfoInVtable.GetAttributeInfo().TestAttr(Attribute::STATIC)) {
        return false;
    }

    // abstract method can not override implemented method
    if (curFuncInfo.GetVirtualMethod() == nullptr && funcInfoInVtable.GetVirtualMethod() != nullptr) {
        return false;
    }

    return funcInfoInVtable.FuncSigIsMatched(curFuncInfo.GetCondition(), builder);
}

bool VTableGenerator::VirtualFuncShouldAddToVTableInItsOwnParent(ClassType& ownParent, ClassType& alreadyIn)
{
    std::vector<ClassType*> inheritanceList;
    auto res = ownParent.GetCustomTypeDef()->GetType()->CalculateGenericTyMapping(ownParent);
    CJC_ASSERT(res.first);
    for (auto ty : ownParent.GetCustomTypeDef()->GetImplementedInterfaceTys()) {
        auto instTy = StaticCast<ClassType*>(ReplaceRawGenericArgType(*ty, res.second, builder));
        GetAllInstantiatedParentType(*instTy, builder, inheritanceList);
    }
    if (auto superClass = ownParent.GetSuperClassTy(&builder)) {
        GetAllInstantiatedParentType(*superClass, builder, inheritanceList);
    }
    return std::find(inheritanceList.begin(), inheritanceList.end(), &alreadyIn) == inheritanceList.end();
}

bool VTableGenerator::UpdateVtable(VirtualMethodInfo& curFuncInfo, VTableInDef& vtable)
{
    // update rules:
    // 1. method in sub type udpate method in parent type
    // 2. method in interface update method in brother interface
    // 3. method in class update method in brother interface
    // 4. method in interface can NOT update method in brother class
    bool maybeAddNewItemToVtable = true;
    for (auto& vtableIt : vtable.GetModifiableTypeVTables()) {
        for (auto& funcInfo : vtableIt.GetModifiableVirtualMethods()) {
            if (IsSigTypeMatched(curFuncInfo, funcInfo)) {
                funcInfo.UpdateMethodInfo(curFuncInfo);
                // if a function declared in sub type updates its parent type's vtable, then don't need to
                // add new item to vtable
                // but if a function updates its brother type's vtable(rules 2 and 3), then maybe this function
                // will be added in vtable under its parent type
                /* e.g.
                    interface I1 { func foo() {} }
                    open class C1 { open public func foo() {} }
                    class C2 <: C1 & I1 {}

                    in class C2's vtable, interface I1 is brother type of class C1, method `C1::foo` overrides
                    method `I::foo`, but `C1::foo` still need to be added under class C1
                    C2's vtable {
                        I1: { foo: ()->Unit, C1::foo } --> `C1::foo` updated I1's vtable
                        C1: { foo: ()->Unit, C1::foo } --> but still need to add it in C1's vtable
                    }
                */
                auto parentTy = DynamicCast<ClassType*>(curFuncInfo.GetInstParentType());
                if (parentTy == nullptr) {
                    // if current function is declared in struct or enum, it must be sub type
                    maybeAddNewItemToVtable = false;
                } else {
                    // maybe update many times
                    // update one function from class's parent interface or class's brother interface,
                    // but it can't be guaranteed which function is visited first, so `&=` is needed
                    maybeAddNewItemToVtable &=
                        VirtualFuncShouldAddToVTableInItsOwnParent(*parentTy, *vtableIt.GetSrcParentType());
                }
                break;
            }
        }
        // must visit all src parent types, maybe one function update many virtual function in different src parent type
    }
    return maybeAddNewItemToVtable;
}

void VTableGenerator::MergeVtable(ClassType& instParentTy, VTableInDef& vtable)
{
    auto replaceTable = GetInstMapFromDefAndExtends(instParentTy);
    auto parentDef = instParentTy.GetClassDef();
    // not include abstract methods
    std::vector<FuncBase*> publicAndProtectedFuncs;
    CollectCurDefMethodsMayBeInVtable(*parentDef, publicAndProtectedFuncs);

    std::unordered_map<std::string, VirtualMethodInfo> newMethodsInVTable;
    // update vtable
    // 1. visit all abstract methods in parent def
    for (auto abstractMethod : parentDef->GetAbstractMethods()) {
        if (abstractMethod.hasBody) {
            continue;
        }
        auto funcInfo = CreateVirtualFuncInfo(abstractMethod, *parentDef->GetType(), replaceTable);
        auto maybeAddNewItemToVtable = UpdateVtable(funcInfo, vtable);
        if (maybeAddNewItemToVtable) {
            newMethodsInVTable.emplace(abstractMethod.GetASTMangledName(), std::move(funcInfo));
        }
    }

    // 2. visit all public and protected non-abstract methods, including
    //   a. static and non-static methods
    //   b. methods in parent def and parent's extend def
    for (auto func : publicAndProtectedFuncs) {
        auto parentType = func->GetParentCustomTypeOrExtendedType();
        CJC_NULLPTR_CHECK(parentType);
        auto funcInfo = CreateVirtualFuncInfo(*func, *parentType, replaceTable);
        auto maybeAddNewItemToVtable = UpdateVtable(funcInfo, vtable);
        if (maybeAddNewItemToVtable && IsVirtualFunction(*funcInfo.GetVirtualMethod())) {
            newMethodsInVTable.emplace(func->GetIdentifierWithoutPrefix(), std::move(funcInfo));
        }
    }
    // 3. only for correct virtual method order
    for (auto& name : parentDef->GetAllMethodMangledNames()) {
        auto it = newMethodsInVTable.find(name);
        if (it != newMethodsInVTable.end()) {
            vtable.AddNewItemToTypeVTable(instParentTy, std::move(it->second));
        }
    }
}

void VTableGenerator::UpdateAbstractMethodWithImplementedMethod(
    VTableInDef& vtable, const ClassType& curParentTy, VirtualMethodInfo& abstractFuncInfo)
{
    bool done = false;
    for (auto& vtableIt : vtable.GetModifiableTypeVTables()) {
        // must be from brother interface
        // Note: method declared in class also can override method in interface,
        // but this case has been handled in front step
        if (vtableIt.GetSrcParentType()->GetClassDef()->IsClass() || vtableIt.GetSrcParentType() == &curParentTy) {
            continue;
        }
        for (auto& funcInfo : vtableIt.GetModifiableVirtualMethods()) {
            // skip abstract method, need use implemented method to override abstract method
            if (funcInfo.GetVirtualMethod() == nullptr) {
                continue;
            }
            if (IsSigTypeMatched(funcInfo, abstractFuncInfo)) {
                abstractFuncInfo = funcInfo;
                done = true;
                break;
            }
        }
        if (done) {
            break;
        }
    }
}

void VTableGenerator::UpdateAbstractMethodInVtable(VTableInDef& vtable)
{
    for (auto& vtableIt : vtable.GetModifiableTypeVTables()) {
        for (auto& funcInfo : vtableIt.GetModifiableVirtualMethods()) {
            // skip non-abstract method, only update abstract method
            if (funcInfo.GetVirtualMethod() != nullptr) {
                continue;
            }
            UpdateAbstractMethodWithImplementedMethod(vtable, *vtableIt.GetSrcParentType(), funcInfo);
        }
    }
}

// `key` is generic type param in def, def's parents and def's parents' extend
// `value` is instantiated type args in current Type
std::unordered_map<const GenericType*, Type*> VTableGenerator::GetInstMapFromDefIncludeParents(
    const CustomTypeDef& def, const Type& curType)
{
    auto inheritanceList = def.GetSuperTypesRecusively(builder);

    /* e.g.
        interface I<T> { func foo(a: T) {} }
        class A<T> {}
        extend<U1> A<U1> <: I<U1> { func goo(a: U1) {} }

        if input `def` is `extend<U1> A<U1>` and `curType` is A<Int32>, then
        we need to get correct `replaceTable`
    */
    std::unordered_map<const GenericType*, Type*> replaceTable;
    // 1. collect table from def's parent type, in this case, def is `extend<U1> A<U1>`
    // its parent type is `I<U1>`, then replaceTable is {I::T -> U1}
    for (auto parent : inheritanceList) {
        replaceTable.merge(GetInstMapFromDefAndExtends(*parent));
    }
    // 2. collect table from input def, then temp table is {U1 -> Int32}
    // maybe we can meet `extend<T> A<B<T>> {}`, and `curType` is A<Int32>, then ignore this def,
    // so not need to check `res`
    auto [res, tmpTable] = def.GetType()->CalculateGenericTyMapping(curType);
    // 3. update replaceTable with temp table, from {I::T -> U1} to {I::T -> Int32}
    for (auto& it : replaceTable) {
        if (auto genericTy = DynamicCast<GenericType*>(it.second)) {
            auto tmp = tmpTable.find(genericTy);
            if (tmp != tmpTable.end()) {
                it.second = tmp->second;
            }
        }
    }
    // 4. merge temp table to replaceTable, then result is {U1 -> Int32}, {I::T -> Int32}
    replaceTable.merge(tmpTable);

    return replaceTable;
}

void VTableGenerator::CollectMethodsFromAncestorInterfaceMayBeInVTable(
    const CustomTypeDef& curDef, std::vector<FuncBase*>& methods)
{
    for (auto parent : curDef.GetImplementedInterfaceDefs()) {
        CollectCurDefMethodsMayBeInVtable(*parent, methods);
        CollectMethodsFromAncestorInterfaceMayBeInVTable(*parent, methods);
    }
}

std::vector<FuncBase*> VTableGenerator::CollectMethodsIncludeParentsMayBeInVtable(const CustomTypeDef& curDef)
{
    std::vector<FuncBase*> methods;
    // scan methods from all ancestors
    CollectMethodsFromAncestorInterfaceMayBeInVTable(curDef, methods);
    if (auto clsDef = DynamicCast<const ClassDef*>(&curDef)) {
        auto super = clsDef->GetSuperClassDef();
        while (super != nullptr) {
            CollectCurDefMethodsMayBeInVtable(*super, methods);
            CollectMethodsFromAncestorInterfaceMayBeInVTable(*super, methods);
            super = super->GetSuperClassDef();
        }
    }
    for (auto func : curDef.GetMethods()) {
        if (FuncMayBeInVtable(*func)) {
            methods.emplace_back(func);
        }
    }

    return methods;
}

std::unordered_map<std::string, VirtualMethodInfo> VTableGenerator::CollectAllPublicAndProtectedMethods(
    const CustomTypeDef& curDef)
{
    std::unordered_map<std::string, VirtualMethodInfo> allMethods;
    std::unordered_map<const GenericType*, Type*> emptyTable;
    if (auto extendDef = DynamicCast<const ExtendDef*>(&curDef)) {
        auto brotherDefs = CollectBrotherDefs(*extendDef, builder);
        // get methods from brother extend's parent def and brother extend's def
        for (auto brotherDef : brotherDefs) {
            auto replaceTable =
                GetInstMapFromDefIncludeParents(*brotherDef, *extendDef->GetExtendedType());
            auto publicFuncs = CollectMethodsIncludeParentsMayBeInVtable(*brotherDef);
            for (auto func : publicFuncs) {
                auto parentType = func->GetParentCustomTypeOrExtendedType();
                CJC_NULLPTR_CHECK(parentType);
                auto funcInfo = CreateVirtualFuncInfo(*func, *parentType, replaceTable);
                allMethods.emplace(func->GetIdentifierWithoutPrefix(), std::move(funcInfo));
            }
        }
    } else if (auto classDef = DynamicCast<ClassDef*>(&curDef)) {
        // get methods from current def
        for (auto aMethod : classDef->GetAbstractMethods()) {
            if (aMethod.hasBody) {
                continue;
            }
            if (aMethod.attributeInfo.TestAttr(Attribute::PUBLIC) ||
                aMethod.attributeInfo.TestAttr(Attribute::PROTECTED)) {
                auto funcInfo = CreateVirtualFuncInfo(aMethod, *classDef->GetType(), emptyTable);
                allMethods.emplace(aMethod.GetASTMangledName(), std::move(funcInfo));
            }
        }
    }

    // get methods from current def
    for (auto func : curDef.GetMethods()) {
        if (FuncMayBeInVtable(*func)) {
            auto parentType = func->GetParentCustomTypeOrExtendedType();
            CJC_NULLPTR_CHECK(parentType);
            auto funcInfo = CreateVirtualFuncInfo(*func, *parentType, emptyTable);
            allMethods.emplace(func->GetIdentifierWithoutPrefix(), std::move(funcInfo));
        }
    }

    return allMethods;
}

void VTableGenerator::GenerateVTable(CustomTypeDef& customTypeDef)
{
    if (customTypeDef.TestAttr(Attribute::GENERIC_INSTANTIATED)) {
        return;
    }
    auto inheritanceList = customTypeDef.GetSuperTypesRecusively(builder);

    VTableInDef vtable;
    // 1. merge all parent vtable, from grand-parent to parent
    for (auto parent : inheritanceList) {
        MergeVtable(*parent, vtable);
    }

    // 2. use implemented method which declared in parent to override abstract method which declared in another parent
    /* e.g.
        interface A { func foo() {} }
        interface B { func foo(): Unit }
        class C <: A & B {}

        after step 1, class C's vtable is as follows:
        vtable {
            interface-A: [ foo: ()->Unit, A::foo ]
            interface-B: [ foo: ()->Unit, nullptr ]
        }
        we need to override abstract `foo` with `A::foo`, then it will be as follows:
        vtable {
            interface-A: [ foo: ()->Unit, A::foo ]
            interface-B: [ foo: ()->Unit, A::foo ]
        }
    */
    UpdateAbstractMethodInVtable(vtable);

    // 3. visit all public and protected methods related with current def, including
    //   a. static and non-static methods
    //   b. abstract and non-abstract methods
    //   c. methods in current def, current extended def and current extended parent def
    // not include methods declared in current parent def, they have been handled in step 1
    auto publicAndProtectedMethods = CollectAllPublicAndProtectedMethods(customTypeDef);
    std::unordered_map<std::string, VirtualMethodInfo> newMethodsInVTable;
    for (auto& [name, funcInfo] : publicAndProtectedMethods) {
        auto maybeAddNewItemToVtable = UpdateVtable(funcInfo, vtable);
        if (!maybeAddNewItemToVtable) {
            continue;
        }
        if (funcInfo.GetVirtualMethod() == nullptr) {
            newMethodsInVTable.emplace(name, std::move(funcInfo));
        } else if (customTypeDef.IsClassLike() && IsVirtualFunction(*funcInfo.GetVirtualMethod())) {
            newMethodsInVTable.emplace(name, std::move(funcInfo));
        }
    }
    if (auto classDef = DynamicCast<ClassDef*>(&customTypeDef)) {
        for (const auto& name : classDef->GetAllMethodMangledNames()) {
            auto it = newMethodsInVTable.find(name);
            if (it == newMethodsInVTable.end()) {
                continue;
            }
            auto& funcInfo = it->second;
            if (funcInfo.GetVirtualMethod() == nullptr) {
                vtable.AddNewItemToTypeVTable(*StaticCast<ClassType*>(customTypeDef.GetType()), std::move(funcInfo));
            } else {
                auto srcParentType =
                    StaticCast<ClassType*>(funcInfo.GetVirtualMethod()->GetParentCustomTypeOrExtendedType());
                vtable.AddNewItemToTypeVTable(*srcParentType, std::move(funcInfo));
            }
        }
    }
    UpdateInstanceAttr(vtable);
    customTypeDef.SetVTable(std::move(vtable));
}
