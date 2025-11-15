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
#include "cangjie/CHIR/Type/CustomTypeDef.h"

#include <iostream>
#include <optional>

#include "cangjie/CHIR/CHIRCasting.h"
#include "cangjie/CHIR/ToStringUtils.h"
#include "cangjie/CHIR/Utils.h"
#include "cangjie/CHIR/Value.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/ConstantsUtils.h"
#include "cangjie/Utils/ICEUtil.h"
#include "cangjie/Utils/Utils.h"

using namespace Cangjie::CHIR;

void CustomTypeDef::AddMethod(FuncBase* method)
{
    CJC_NULLPTR_CHECK(method);
    method->declaredParent = this;
    methods.emplace_back(method);
}

void CustomTypeDef::AddStaticMemberVar(GlobalVarBase* variable)
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

std::string CustomTypeDef::GenericDefArgsToString() const
{
    auto genericTypeParams = GetGenericTypeParams();
    if (genericTypeParams.empty()) {
        return "";
    }
    std::string res;
    res += "<";
    for (size_t i = 0; i < genericTypeParams.size(); ++i) {
        res += genericTypeParams[i]->ToString();
        // not the last one
        if (i != genericTypeParams.size() - 1) {
            res += ", ";
        }
    }
    res += ">";
    return res;
}

void CustomTypeDef::PrintAttrAndTitle(std::stringstream& ss) const
{
    ss << attributeInfo.ToString();
    ss << CustomTypeKindToString(*this) << " " << GetIdentifier() << GenericDefArgsToString();
    PrintParent(ss);
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

void CustomTypeDef::PrintParent(std::stringstream& ss) const
{
    auto parentTys = GetSuperTypesInCurDef();
    if (parentTys.empty()) {
        return;
    }
    ss << " <: ";
    for (size_t i = 0; i < parentTys.size(); ++i) {
        ss << parentTys[i]->GetCustomTypeDef()->GetIdentifier() << GenericInsArgsToString(*parentTys[i]);
        // not the last one
        if (i != parentTys.size() - 1) {
            ss << " & ";
        }
    }
}

void CustomTypeDef::PrintComment(std::stringstream& ss) const
{
    std::stringstream comment;
    if (!srcCodeIdentifier.empty()) {
        comment << "srcCodeIdentifier: " << srcCodeIdentifier;
    }
    AddCommaOrNot(comment);
    auto genericTypeParams = GetGenericTypeParams();
    if (!genericTypeParams.empty()) {
        auto constraintsStr = GetGenericConstaintsStr(genericTypeParams);
        if (!constraintsStr.empty()) {
            comment << "genericConstrains: " << constraintsStr;
        }
    }
    AddCommaOrNot(comment);
    comment << ToStringAnnotationMap();
    AddCommaOrNot(comment);
    if (genericDecl != nullptr) {
        comment << "genericDecl: " << genericDecl->GetIdentifierWithoutPrefix();
        AddCommaOrNot(comment);
    }
    comment << "packageName: " << packageName;
    if (comment.str() != "") {
        ss << " // " << comment.str();
    }
}

void CustomTypeDef::PrintLocalVar(std::stringstream& ss) const
{
    for (auto& localVar : instanceVars) {
        PrintIndent(ss);
        localVar.TestAttr(Attribute::READONLY) ? ss << "let " : ss << "var ";
        ss << localVar.name << ": " << localVar.type->ToString() << " // " << localVar.loc.ToString() << "\n";
    }
}

void CustomTypeDef::PrintStaticVar(std::stringstream& ss) const
{
    for (auto& staticVar : staticVars) {
        PrintIndent(ss);
        ss << "[static] ";
        staticVar->TestAttr(Attribute::READONLY) ? ss << "let " : ss << "var ";
        ss << staticVar->GetIdentifier() << ": " << staticVar->GetType()->ToString() << "\n";
    }
}

void CustomTypeDef::PrintMethod(std::stringstream& ss) const
{
    for (auto& method : methods) {
        PrintIndent(ss);
        ss << method->GetAttributeInfo().ToString();
        ss << "func " << method->GetIdentifier() << ": " << method->GetType()->ToString() << "\n";
    }
}

void CustomTypeDef::PrintVTable(std::stringstream& ss) const
{
    unsigned indent = 1;
    if (vtable.size() > 0) {
        PrintIndent(ss, indent);
        ss << "vtable {\n";
        ++indent;
        for (auto& vtableIt : vtable) {
            PrintIndent(ss, indent);
            ss << vtableIt.first->ToString() << " {\n";
            ++indent;
            for (auto& funcInfo : vtableIt.second) {
                PrintIndent(ss, indent);
                ss << "@" << funcInfo.srcCodeIdentifier;
                if (funcInfo.srcCodeIdentifier != "$Placeholder") {
                    ss << ": " << funcInfo.typeInfo.originalType->ToString();
                    ss << " => " <<
                        (funcInfo.instance ? funcInfo.instance->GetIdentifier() : "[abstract]");
                }
                ss << "\n";
            }
            --indent;
            PrintIndent(ss, indent);
            ss << "}\n";
        }
        --indent;
        PrintIndent(ss, indent);
        ss << "}\n";
    }
}

std::pair<FuncBase*, bool> CustomTypeDef::GetExpectedFunc(
    const std::string& funcName, FuncType& funcType, bool isStatic,
    std::unordered_map<const GenericType*, Type*> replaceTable,
    std::vector<Type*>& funcInstTypeArgs, CHIRBuilder& builder, bool checkAbstractMethod) const
{
    // you shouldn't search a function without name
    CJC_ASSERT(!funcName.empty());
    auto instParamTys = funcType.GetParamTypes();
    if (!isStatic) {
        CJC_ASSERT(!instParamTys.empty());
        instParamTys.erase(instParamTys.begin());
    }
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
        if (!method->TestAttr(Attribute::STATIC)) {
            CJC_ASSERT(!originalFuncParamTys.empty());
            originalFuncParamTys.erase(originalFuncParamTys.begin());
        }
        if (originalFuncParamTys.size() != instParamTys.size()) {
            continue;
        }
        std::vector<GenericType*> genericTypeParams;
        if (auto func = DynamicCast<Func*>(method)) {
            genericTypeParams = func->GetGenericTypeParams();
        } else {
            genericTypeParams = StaticCast<ImportedFunc*>(method)->GetGenericTypeParams();
        }
        if (genericTypeParams.size() != funcInstTypeArgs.size()) {
            continue;
        }
        for (size_t i = 0; i < genericTypeParams.size(); ++i) {
            replaceTable.emplace(genericTypeParams[i], funcInstTypeArgs[i]);
        }
        bool matched = true;
        for (size_t i = 0; i < originalFuncParamTys.size(); ++i) {
            auto instType = ReplaceRawGenericArgType(*originalFuncParamTys[i], replaceTable, builder);
            if (auto genericTy = DynamicCast<GenericType*>(instType); genericTy && genericTy->orphanFlag) {
                auto upperBounds = genericTy->GetUpperBounds();
                CJC_ASSERT(upperBounds.size() == 1);
                instType = upperBounds[0];
            }
            if (!instParamTys[i]->IsGeneric() && instType != instParamTys[i]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            if (auto rawFunc = method->Get<WrappedRawMethod>(); rawFunc && rawFunc->GetParentCustomTypeDef() == this) {
                return {rawFunc, true};
            }
            return {method, true};
        }
    }
    auto failed = std::pair<FuncBase*, bool>{nullptr, false};
    if (!checkAbstractMethod) {
        return failed;
    }
    auto classDef = DynamicCast<ClassDef*>(this);
    if (classDef == nullptr) {
        return failed;
    }
    for (auto method : classDef->GetAbstractMethods()) {
        if (isStatic != method.attributeInfo.TestAttr(Attribute::STATIC)) {
            continue;
        }
        if (method.methodName != funcName) {
            continue;
        }
        auto originalFuncParamTys = StaticCast<FuncType*>(method.methodTy)->GetParamTypes();
        if (!method.attributeInfo.TestAttr(Attribute::STATIC)) {
            originalFuncParamTys.erase(originalFuncParamTys.begin());
        }
        if (originalFuncParamTys.size() != instParamTys.size()) {
            continue;
        }
        auto& genericTypeParams = method.methodGenericTypeParams;
        if (genericTypeParams.size() != funcInstTypeArgs.size()) {
            continue;
        }
        for (size_t i = 0; i < genericTypeParams.size(); ++i) {
            replaceTable.emplace(genericTypeParams[i], funcInstTypeArgs[i]);
        }
        bool matched = true;
        for (size_t i = 0; i < originalFuncParamTys.size(); ++i) {
            auto instType = ReplaceRawGenericArgType(*originalFuncParamTys[i], replaceTable, builder);
            if (!instParamTys[i]->IsGeneric() && instType != instParamTys[i]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return {nullptr, true};
        }
    }
    
    return failed;
}

std::vector<VTableSearchRes> CustomTypeDef::GetFuncIndexInVTable(const FuncCallType& funcCallType,
    bool isStatic, std::unordered_map<const GenericType*, Type*>& replaceTable, CHIRBuilder& builder) const
{
    auto& funcName = funcCallType.funcName;
    auto& funcInstTypeArgs = funcCallType.genericTypeArgs;
    auto instArgTys = funcCallType.funcType->GetParamTypes();
    if (!isStatic) {
        CJC_ASSERT(!instArgTys.empty());
        instArgTys.erase(instArgTys.begin());
    }
    std::vector<VTableSearchRes> res;
    for (auto& mapIt : vtable) {
        for (size_t i = 0; i < mapIt.second.size(); ++i) {
            if (mapIt.second[i].srcCodeIdentifier != funcName) {
                continue;
            }
            auto genericParamTys = mapIt.second[i].typeInfo.sigType->GetParamTypes();
            if (genericParamTys.size() != instArgTys.size()) {
                continue;
            }
            auto genericTypeParams = mapIt.second[i].typeInfo.methodGenericTypeParams;
            if (genericTypeParams.size() != funcInstTypeArgs.size()) {
                continue;
            }
            for (size_t j = 0; j < genericTypeParams.size(); ++j) {
                replaceTable.emplace(genericTypeParams[j], funcInstTypeArgs[j]);
            }
            bool matched = true;
            for (size_t j = 0; j < genericParamTys.size(); ++j) {
                auto declaredInstType = ReplaceRawGenericArgType(*genericParamTys[j], replaceTable, builder);
                if (!ParamTypeIsEquivalent(*declaredInstType, *instArgTys[j])) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                auto instSrcParentTy =
                    ReplaceRawGenericArgType(*const_cast<ClassType*>(mapIt.first), replaceTable, builder);
                auto& funcInfo = mapIt.second[i];
                res.emplace_back(VTableSearchRes{
                    .instSrcParentType = StaticCast<ClassType*>(instSrcParentTy),
                    .originalFuncType = funcInfo.typeInfo.originalType,
                    .instance = funcInfo.instance,
                    .attr = funcInfo.attr,
                    .offset = i
                });
                break;
            }
        }
    }
    return res;
}

std::string CustomTypeDef::ToString() const
{
    /* [public][generic][...] class XXX {      // loc: xxx, genericDecl: xxx
       ^^^^^^^^^^^^^^ attr    ^^^^^^^^^ title  ^^^^^^^^^^^^^^^^^^ comment
           local var
           static var
           method
           vtable
       }
    */
    std::stringstream ss;
    PrintAttrAndTitle(ss);
    ss << " {";
    PrintComment(ss);
    ss << "\n";
    PrintLocalVar(ss);   // has a \n in the end
    PrintStaticVar(ss);  // has a \n in the end
    PrintMethod(ss);     // has a \n in the end
    PrintVTable(ss);     // has a \n in the end
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

void CustomTypeDef::SetMethods(const std::vector<FuncBase*>& items)
{
    for (auto m : methods) {
        m->declaredParent = nullptr;
    }
    for (auto m : items) {
        m->declaredParent = this;
    }
    methods = items;
}

void CustomTypeDef::SetStaticMemberVars(const std::vector<GlobalVarBase*>& vars)
{
    for (auto v : staticVars) {
        v->declaredParent = nullptr;
    }
    for (auto v : vars) {
        v->declaredParent = this;
    }
    staticVars = vars;
}

std::vector<FuncBase*> CustomTypeDef::GetMethods() const
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

const VTableType& CustomTypeDef::GetVTable() const
{
    return vtable;
}

void CustomTypeDef::SetVTable(const VTableType& table)
{
    vtable = table;
}

void CustomTypeDef::UpdateVtableItem(ClassType& srcClassTy,
    size_t index, FuncBase* newFunc, Type* newParentTy, const std::string newName)
{
    auto& funcInfo = vtable[&srcClassTy][index];
    funcInfo.instance = newFunc;
    if (newFunc != nullptr) {
        funcInfo.typeInfo.originalType = newFunc->GetFuncType();
    }
    if (newParentTy != nullptr) {
        funcInfo.typeInfo.parentType = newParentTy;
    }
    if (!newName.empty()) {
        funcInfo.srcCodeIdentifier = newName;
    }
}

void CustomTypeDef::AddVtableItem(ClassType& srcClassTy, VirtualFuncInfo&& info)
{
    vtable[&srcClassTy].push_back(std::move(info));
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

void CustomTypeDef::AppendAttributeInfo(const AttributeInfo& info)
{
    attributeInfo.AppendAttrs(info);
}

/**
 * Get identifier without prefix '@'
 */
std::string CustomTypeDef::GetIdentifierWithoutPrefix() const
{
    CJC_ASSERT(!identifier.empty());
    return identifier.substr(1);
}

void CustomTypeDef::EnableAttr(Attribute attr)
{
    attributeInfo.SetAttr(attr, true);
}

bool CustomTypeDef::TestAttr(Attribute attr) const
{
    return attributeInfo.TestAttr(attr);
}

AttributeInfo CustomTypeDef::GetAttributeInfo() const
{
    return attributeInfo;
}

std::vector<GlobalVarBase*> CustomTypeDef::GetStaticMemberVars() const
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
    return IsInterface() || TestAttr(Attribute::VIRTUAL) || TestAttr(Attribute::ABSTRACT);
}
