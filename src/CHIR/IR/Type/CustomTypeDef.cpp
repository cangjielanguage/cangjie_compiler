// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements the Type class in CHIR.
 */
#include "cangjie/CHIR/IR/Type/CustomTypeDef.h"

#include <cstddef>
#include <iostream>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <utility>

#include "cangjie/CHIR/Utils/CHIRCasting.h"
#include "cangjie/CHIR/Utils/ToStringUtils.h"
#include "cangjie/CHIR/Utils/Utils.h"
#include "cangjie/CHIR/IR/Value/Value.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/ConstantsUtils.h"
#include "cangjie/Utils/ICEUtil.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie::CHIR;

namespace {
/**
 *  we choose the well matching but there may be many results, if there are many results, their `instance` must be same
 *  so we need to define what function is NOT well matching, of course, the rule is from spec:
 *  1. you can review how the `candidateTypes` are calculated
 *  2. we assume that i and j are from `candidateTypes`, we define function `i` is NOT well matching if all parameters
 *     in function `j` can be forwarded to function `i`, but not all parameters in function `i` can be forwarded to
 *     function `j`
 */
std::vector<VTableSearchRes> GetWellMatchingResults(const std::vector<VTableSearchRes>& candidateRes,
    const std::vector<FuncType*>& candidateTypes, CHIRBuilder& builder)
{
    if (candidateRes.size() <= 1) {
        return candidateRes;
    }
    CJC_ASSERT(candidateRes.size() == candidateTypes.size());
    auto candidateNum = candidateTypes.size();
    std::vector<bool> targetMark(candidateNum, true);
    for (size_t i = 0; i < candidateNum; ++i) {
        if (!targetMark[i]) {
            continue;
        }
        for (size_t j = i + 1; j < candidateNum; ++j) {
            if (!targetMark[j]) {
                continue;
            }
            auto iToJ = candidateTypes[i]->IsEqualOrInstantiatedTypeOf(*candidateTypes[j], builder);
            auto jToI = candidateTypes[j]->IsEqualOrInstantiatedTypeOf(*candidateTypes[i], builder);
            iToJ &= candidateTypes[i]->IsEqualOrSubTypeOf(*candidateTypes[j], builder);
            jToI &= candidateTypes[j]->IsEqualOrSubTypeOf(*candidateTypes[i], builder);
            // according to spec, the more specific type is expected
            // but if iToJ and jToI are both true, that means i and j have same func signature, we need to store both,
            // if iToJ and jToI are both false, that means there is genric param in i and j, generic params can't
            // be forwarded to each other
            if (iToJ && !jToI) {
                targetMark[j] = false;
            } else if (!iToJ && jToI) {
                targetMark[i] = false;
                break;
            }
        }
    }
    std::vector<VTableSearchRes> result;
    std::unordered_set<Function*> instances;
    for (size_t i = 0; i < candidateNum; ++i) {
        if (targetMark[i]) {
            result.emplace_back(candidateRes[i]);
            if (auto rawFunc = candidateRes[i].instance->Get<WrappedRawMethod>()) {
                instances.emplace(rawFunc);
            } else {
                instances.emplace(candidateRes[i].instance);
            }
        }
    }
    CJC_ASSERT(!result.empty());
    CJC_ASSERT(instances.size() == 1);
    return result;
}
}

void CustomTypeDef::AddMethod(Function* method)
{
    CJC_NULLPTR_CHECK(method);
    method->declaredParent = this;
    methods.emplace_back(method);
}

void CustomTypeDef::AddStaticMemberVar(GlobalVar* variable)
{
    CJC_NULLPTR_CHECK(variable);
    variable->declaredParent = this;
    staticVars.emplace_back(variable);
}

void CustomTypeDef::Dump() const
{
    std::cout << ToString() << std::endl;
}

void CustomTypeDef::AddImplementedInterfaceTy(ClassType& interfaceTy)
{
    CJC_NULLPTR_CHECK(interfaceTy.GetCustomTypeDef());
    CJC_ASSERT(StaticCast<ClassDef*>(interfaceTy.GetCustomTypeDef())->IsInterface());
    implementedInterfaceTys.push_back(&interfaceTy);
}

std::vector<ClassDef*> CustomTypeDef::GetImplementedInterfaceDefs() const
{
    std::vector<ClassDef*> defs;
    for (auto ty : implementedInterfaceTys) {
        CJC_NULLPTR_CHECK(ty->GetCustomTypeDef());
        defs.emplace_back(StaticCast<ClassDef*>(ty->GetCustomTypeDef()));
    }
    return defs;
}

std::vector<ClassType*> CustomTypeDef::GetSuperTypesInCurDef() const
{
    auto res = implementedInterfaceTys;
    if (auto classDef = DynamicCast<const ClassDef*>(this)) {
        auto superClassType = classDef->GetSuperClassTy();
        if (superClassType != nullptr) {
            res.insert(res.begin(), superClassType);
        }
    }
    return res;
}

std::string CustomTypeDef::CustomTypeDefTitleToString() const
{
    std::string kindStr;
    if (IsInterface()) {
        kindStr = "interface";
    } else if (IsClass()) {
        kindStr = "class";
    } else if (IsStruct()) {
        kindStr = "struct";
    } else if (IsEnum()) {
        kindStr = "enum";
    } else if (IsExtend()) {
        kindStr = "extend";
    }
    return kindStr + " " + identifier + TypeVecToString("<", GetGenericTypeParams(), ">");
}

std::string CustomTypeDef::GenericInsArgsToString(const CustomType& ty) const
{
    std::string res;
    auto args = ty.GetGenericArgs();
    if (args.empty()) {
        return res;
    }
    res += "<";
    for (size_t i = 0; i < args.size(); ++i) {
        res += args[i]->ToString();
        // not the last one
        if (i != args.size() - 1) {
            res += ", ";
        }
    }
    res += ">";
    return res;
}

std::string CustomTypeDef::ParentToString() const
{
    auto parentTys = GetSuperTypesInCurDef();
    if (parentTys.empty()) {
        return "";
    }
    return " <: " + TypeVecToString("", parentTys, "",  " & ");
}

std::string CustomTypeDef::AddExtraComment() const
{
    return "";
}

std::string CustomTypeDef::CommentToString() const
{
    std::vector<std::string> result;
    if (auto baseComment = BaseCommentToString(); !baseComment.empty()) {
        result.emplace_back(baseComment);
    }
    if (!srcCodeIdentifier.empty()) {
        result.emplace_back("srcCodeIdentifier: " + srcCodeIdentifier);
    }
    auto genericTypeParams = GetGenericTypeParams();
    if (!genericTypeParams.empty()) {
        auto constraintsStr = GetGenericTypeConstaintsStr(genericTypeParams);
        if (!constraintsStr.empty()) {
            result.emplace_back("genericConstrains: " + constraintsStr);
        }
    }
    if (genericDecl != nullptr) {
        result.emplace_back("genericDecl: " + genericDecl->GetIdentifier());
    }
    if (auto extra = AddExtraComment(); !extra.empty()) {
        result.emplace_back(extra);
    }
    return ::CommentToString(result);
}

std::string CustomTypeDef::LocalVarToString(size_t indent) const
{
    std::stringstream ss;
    for (auto& localVar : instanceVars) {
        ss << AddNewLineOrNot(localVar.annoInfo.ToString(indent));
        ss << IndentToString(indent) << localVar.attributeInfo.ToString();
        localVar.TestAttr(Attribute::READONLY) ? ss << "let " : ss << "var ";
        ss << localVar.name << ": " << localVar.type->ToString();
        std::vector<std::string> comments;
        if (!localVar.loc.IsInvalidPos()) {
            comments.emplace_back("loc: " + localVar.loc.ToString());
        }
        if (localVar.initializerFunc != nullptr) {
            comments.emplace_back("initFunc: " + localVar.initializerFunc->GetIdentifier());
        }
        if (!localVar.rawMangledName.empty()) {
            comments.emplace_back("rawMangledName: " + localVar.rawMangledName);
        }
        ss << ::CommentToString(comments);
        ss << std::endl;
    }
    return ss.str();
}

std::string CustomTypeDef::StaticVarToString(size_t indent) const
{
    std::stringstream ss;
    for (auto staticVar : staticVars) {
        ss << IndentToString(indent) << staticVar->GetAttributeInfo().ToString();
        staticVar->TestAttr(Attribute::READONLY) ? ss << " let " : ss << " var ";
        ss << staticVar->GetIdentifier() << ": " << staticVar->GetType()->ToString() << std::endl;
    }
    return ss.str();
}

std::string CustomTypeDef::MethodToString(size_t indent) const
{
    std::stringstream ss;
    for (auto method : methods) {
        ss << IndentToString(indent) << method->GetAttributeInfo().ToString();
        ss << " func " << method->GetIdentifier() << ": " << method->GetType()->ToString() << std::endl;
    }
    return ss.str();
}

Function* CustomTypeDef::GetExpectedFunc(
    const std::string& funcName, FuncType& funcType, bool isStatic,
    std::unordered_map<const GenericType*, Type*> replaceTable,
    std::vector<Type*>& funcInstTypeArgs, CHIRBuilder& builder, bool checkAbstractMethod) const
{
    // you shouldn't search a function without name
    CJC_ASSERT(!funcName.empty());
    auto instParamTys = funcType.GetParamTypes();
    Function* foundFunc = nullptr;
    for (auto method : methods) {
        if (isStatic != method->TestAttr(Attribute::STATIC)) {
            continue;
        }
        auto methodName = method->GetSrcCodeIdentifier();
        if (auto rawMethod = method->Get<WrappedRawMethod>()) {
            methodName = rawMethod->GetSrcCodeIdentifier();
        }
        if (methodName != funcName) {
            continue;
        }
        auto originalFuncParamTys = method->GetFuncType()->GetParamTypes();
        if (originalFuncParamTys.size() != instParamTys.size()) {
            continue;
        }
        auto genericTypeParams = method->GetGenericTypeParams();
        if (genericTypeParams.size() != funcInstTypeArgs.size()) {
            continue;
        }
        for (size_t i = 0; i < genericTypeParams.size(); ++i) {
            replaceTable.emplace(genericTypeParams[i], funcInstTypeArgs[i]);
        }
        bool matched = true;
        for (size_t i = 0; i < originalFuncParamTys.size(); ++i) {
            auto instType = ReplaceRawGenericArgType(*originalFuncParamTys[i], replaceTable, builder);
            if (!isStatic && i == 0) {
                // `this` data type may be a subtype of the declared parent; only require modal equality
                // so modal overloads (e.g. hashCode vs hashCode(this @local?)) are distinguished.
                auto modalInCall = instParamTys[i]->StripAllRefs()->GetModalInfo();
                auto modalInMethod = instType->StripAllRefs()->GetModalInfo();
                if (modalInCall != modalInMethod) {
                    matched = false;
                    break;
                }
            } else if (!instParamTys[i]->IsGeneric() && instType != instParamTys[i]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            if (auto rawFunc = method->Get<WrappedRawMethod>(); rawFunc && rawFunc->GetParentCustomTypeDef() == this) {
                foundFunc = rawFunc;
            } else {
                foundFunc = method;
            }
            break;
        }
    }
    if (foundFunc == nullptr) {
        return nullptr;
    }
    if (!checkAbstractMethod && foundFunc->TestAttr(Attribute::ABSTRACT)) {
        return nullptr;
    }
    return foundFunc;
}

std::vector<VTableSearchRes> CustomTypeDef::GetFuncIndexInVTable(const FuncCallType& funcCallType,
    std::unordered_map<const GenericType*, Type*>& replaceTable, CHIRBuilder& builder) const
{
    std::vector<VTableSearchRes> res;
    std::vector<FuncType*> candidateTypes;
    for (const auto& vtableIt : vtable.GetTypeVTables()) {
        for (size_t i = 0; i < vtableIt.GetMethodNum(); ++i) {
            const auto& funcInfo = vtableIt.GetVirtualMethods()[i];
            if (funcInfo.FuncSigIsMatched(
                funcCallType, funcInfo.GetAttributeInfo().TestAttr(Attribute::STATIC), replaceTable, builder)) {
                auto originalParentType = vtableIt.GetSrcParentType();
                auto instSrcParentTy = ReplaceRawGenericArgType(*originalParentType, replaceTable, builder);
                res.emplace_back(VTableSearchRes {
                    .instSrcParentType = StaticCast<ClassType*>(instSrcParentTy),
                    .halfInstSrcParentType = originalParentType,
                    .originalFuncType = funcInfo.GetOriginalFuncType(),
                    .instance = funcInfo.GetVirtualMethod(),
                    .originalDef = const_cast<CustomTypeDef*>(this),
                    .genericTypeParams = funcInfo.GetGenericTypeParams(),
                    .attr = funcInfo.GetAttributeInfo(),
                    .offset = i
                });
                /** open class A<X> {
                 *      open public func test<T>(x: X, y: X): Unit {
                 *          println("a");
                 *      }
                 *  }
                 *
                 *  open class B<X> <: A<X> {
                 *      open public func test<Y>(x: C<Y>, y: C<Y>): Unit {
                 *          println("b");
                 *      }
                 *  }
                 *
                 *  class C<T> {}
                 *
                 *  main() {
                 *      let x: C<Int64> = C<Int64>()
                 *      B<C<Int64>>().test<Int64>(x, x)
                 *  }
                 *  face to this example, we can get two candidates for func call `B<C<Int64>>().test<Int64>(x, x)`
                 *  one is A<X>.test<T>(x: X, y: X), the other is B<X>.test<Y>(x: C<Y>, y: C<Y>)
                 *  we need to choose which one is better, according to spec, we need to instantiate func type,
                 *  but only instantiate generic type which defined in class decl, not in func decl,
                 *  so our candidate types are:
                 *  1. (C<Int64>, C<Int64>), instantiated result of A<X>.test<T>(x: X, y: X)
                 *      because generic type `X` is instantiated by `C<Int64>`
                 *  2. (C<Y>, C<Y>), instantiated result of B<X>.test<Y>(x: C<Y>, y: C<Y>)
                 *      because generic type `Y` is defined in func decl, not in class decl
                 *
                 */
                auto instTy = ReplaceRawGenericArgType(*funcInfo.GetCondition().funcType, replaceTable, builder);
                candidateTypes.emplace_back(StaticCast<FuncType*>(instTy));
            }
        }
    }
    return GetWellMatchingResults(res, candidateTypes, builder);
}

std::string CustomTypeDef::ToString() const
{
    /* @Anno1[arg1, arg2, ...]
       [public][generic][...] class XXX<T1, T2> <: parent1 & parent2 { // loc: xxx, genericDecl: xxx
         ^^^^^^^^^^^^^^ attr    ^^^^^^^^^^^^^^ title   ^^^^^^^^^^^^ parent   ^^^^^^^^^^^^^^^^^^ comment
           local var (or enum constructor)
           static var
           method
           vtable
       }
    */
    size_t indent = 0;
    std::stringstream ss;
    ss << AddNewLineOrNot(annoInfo.ToString(indent++));
    ss << attributes.ToString();
    ss << CustomTypeDefTitleToString();
    ss << ParentToString();
    ss << " {";
    ss << AddNewLineOrNot(CommentToString());
    ss << LocalVarToString(indent);   // has a \n in the end
    ss << StaticVarToString(indent);  // has a \n in the end
    ss << MethodToString(indent);     // has a \n in the end
    ss << vtable.ToString(indent) << std::endl;
    ss << "}";
    return ss.str();
}

std::vector<GenericType*> CustomTypeDef::GetGenericTypeParams() const
{
    std::vector<GenericType*> genericTypes;
    if (this->TestAttr(Attribute::GENERIC)) {
        for (auto ty : type->GetGenericArgs()) {
            genericTypes.emplace_back(StaticCast<GenericType*>(ty));
        }
    }
    return genericTypes;
}

std::vector<ClassType*> CustomTypeDef::GetSuperTypesRecusively(CHIRBuilder& builder) const
{
    std::vector<ClassType*> inheritanceList;
    for (auto interface : this->GetImplementedInterfaceTys()) {
        GetAllInstantiatedParentType(*interface, builder, inheritanceList);
    }
    if (this->IsClassLike()) {
        auto superClass = StaticCast<const ClassDef*>(this)->GetSuperClassTy();
        if (superClass != nullptr) {
            GetAllInstantiatedParentType(*superClass, builder, inheritanceList);
        }
    }
    return inheritanceList;
}

void CustomTypeDef::SetMethods(const std::vector<Function*>& items)
{
    for (auto m : methods) {
        m->declaredParent = nullptr;
    }
    for (auto m : items) {
        m->declaredParent = this;
    }
    methods = items;
}

void CustomTypeDef::SetStaticMemberVars(const std::vector<GlobalVar*>& vars)
{
    for (auto v : staticVars) {
        v->declaredParent = nullptr;
    }
    for (auto v : vars) {
        v->declaredParent = this;
    }
    staticVars = vars;
}

std::vector<Function*> CustomTypeDef::GetMethods() const
{
    return methods;
}

bool CustomTypeDef::IsInterface() const
{
    if (!IsClassLike()) {
        return false;
    }
    return StaticCast<const ClassDef*>(this)->IsInterface();
}

bool CustomTypeDef::IsClass() const
{
    if (!IsClassLike()) {
        return false;
    }
    return StaticCast<const ClassDef*>(this)->IsClass();
}

const std::vector<ExtendDef*>& CustomTypeDef::GetExtends() const
{
    return extends;
}

void CustomTypeDef::AddExtend(ExtendDef& extend)
{
    extends.emplace_back(&extend);
}

const VTableInDef& CustomTypeDef::GetDefVTable() const
{
    return vtable;
}

VTableInDef& CustomTypeDef::GetModifiableDefVTable()
{
    return vtable;
}

void CustomTypeDef::SetVTable(VTableInDef&& table)
{
    vtable = std::move(table);
}

void CustomTypeDef::UpdateVtableItem(ClassType& srcClassTy,
    size_t index, Function* newFunc, Type* newParentTy, const std::string newName)
{
    vtable.UpdateItemInTypeVTable(srcClassTy, index, newFunc, newParentTy, newName);
}

void CustomTypeDef::AddVtableItem(ClassType& srcClassTy, VirtualMethodInfo&& info)
{
    for (auto& vtableIt : vtable.GetModifiableTypeVTables()) {
        if (vtableIt.GetSrcParentType() == &srcClassTy) {
            vtableIt.AppendNewMethod(std::forward<VirtualMethodInfo>(info));
        }
    }
}

CustomDefKind CustomTypeDef::GetCustomKind() const
{
    return kind;
}

bool CustomTypeDef::IsStruct() const
{
    return kind == TYPE_STRUCT;
}

bool CustomTypeDef::IsEnum() const
{
    return kind == TYPE_ENUM;
}

bool CustomTypeDef::IsClassLike() const
{
    return kind == TYPE_CLASS;
}

bool CustomTypeDef::IsExtend() const
{
    return kind == TYPE_EXTEND;
}

std::string CustomTypeDef::GetIdentifier() const
{
    return identifier;
}

/**
 * Get identifier without prefix '@'
 */
std::string CustomTypeDef::GetIdentifierWithoutPrefix() const
{
    CJC_ASSERT(!identifier.empty());
    return identifier.substr(1);
}

Function* CustomTypeDef::GetVarInitializationFunc() const
{
    return varInitializationFunc;
}

void CustomTypeDef::SetVarInitializationFunc(Function* func)
{
    varInitializationFunc = func;
}

std::vector<GlobalVar*> CustomTypeDef::GetStaticMemberVars() const
{
    return staticVars;
}

size_t CustomTypeDef::GetAllInstanceVarNum() const
{
    size_t res = instanceVars.size();
    if (auto classDef = DynamicCast<const ClassDef*>(this)) {
        auto parent = classDef->GetSuperClassDef();
        while (parent != nullptr) {
            res += parent->instanceVars.size();
            parent = parent->GetSuperClassDef();
        }
    }
    return res;
}

std::vector<MemberVarInfo> CustomTypeDef::GetAllInstanceVars() const
{
    std::vector<MemberVarInfo> res;
    if (auto classDef = DynamicCast<const ClassDef*>(this)) {
        auto parent = classDef->GetSuperClassDef();
        while (parent != nullptr) {
            res.insert(res.begin(), parent->instanceVars.begin(), parent->instanceVars.end());
            parent = parent->GetSuperClassDef();
        }
    }
    res.insert(res.end(), instanceVars.begin(), instanceVars.end());
    return res;
}

size_t CustomTypeDef::GetDirectInstanceVarNum() const
{
    return instanceVars.size();
}

MemberVarInfo CustomTypeDef::GetDirectInstanceVar(size_t index) const
{
    CJC_ASSERT(index < instanceVars.size());
    return instanceVars[index];
}

std::vector<MemberVarInfo> CustomTypeDef::GetDirectInstanceVars() const
{
    return instanceVars;
}

MemberVarInfo CustomTypeDef::GetInstanceVar(size_t index) const
{
    auto allVars = GetAllInstanceVars();
    CJC_ASSERT(allVars.size() > index);
    return allVars[index];
}

void CustomTypeDef::AddInstanceVar(MemberVarInfo variable)
{
    instanceVars.emplace_back(std::move(variable));
}

void CustomTypeDef::SetDirectInstanceVars(const std::vector<MemberVarInfo>& vars)
{
    instanceVars = vars;
}

std::string CustomTypeDef::GetPackageName() const
{
    return packageName;
}

std::string CustomTypeDef::GetSrcCodeIdentifier() const
{
    return srcCodeIdentifier;
}

Type* CustomTypeDef::GetType() const
{
    return type;
}

void CustomTypeDef::SetAnnoInfo(const AnnoInfo& info)
{
    annoInfo = info;
}

AnnoInfo CustomTypeDef::GetAnnoInfo() const
{
    return annoInfo;
}

std::vector<ClassType*> CustomTypeDef::GetImplementedInterfaceTys() const
{
    return implementedInterfaceTys;
}

size_t CustomTypeDef::GetImplementedInterfacesNum() const
{
    return implementedInterfaceTys.size();
}

bool CustomTypeDef::IsGenericDef() const
{
    return !GetGenericTypeParams().empty();
}

void CustomTypeDef::SetGenericDecl(CustomTypeDef& decl)
{
    genericDecl = &decl;
}

CustomTypeDef* CustomTypeDef::GetGenericDecl() const
{
    return genericDecl;
}

bool CustomTypeDef::CanBeInherited() const
{
    // we shouldn't care about if current def is GENEIC_INSTANTIATED
    return IsInterface() || TestAttr(Attribute::VIRTUAL) || TestAttr(Attribute::ABSTRACT);
}
