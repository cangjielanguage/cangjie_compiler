// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_USER_DEFINED_TYPE_H
#define CANGJIE_CHIR_USER_DEFINED_TYPE_H

#include "cangjie/AST/Node.h"
#include "cangjie/CHIR/AttributeInfo.h"
#include <functional>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Cangjie::CHIR {

class FuncType;
class Type;
class GenericType;
class FuncBase;
class AttributeInfo;
class ClassType;
class Translator;
class Value;
class CustomTypeDef;
class CHIRBuilder;

using TranslateASTNodeFunc = std::function<Value*(const Cangjie::AST::Decl&, Translator&)>;

struct FuncSigInfo {
    std::string funcName;         // src code name
    FuncType* funcType{nullptr};  // declared type, including `this` type and return type
                                  // there may be generic type in it
    std::vector<GenericType*> genericTypeParams;
};

struct FuncCallType {
    std::string funcName;         // src code name
    FuncType* funcType{nullptr};  // inst type, including `this` type and return type
    std::vector<Type*> genericTypeArgs;
};

class VirtualMethodInfo {
public:
    VirtualMethodInfo(FuncSigInfo&& c, FuncBase* func, const AttributeInfo& a, FuncType& o, Type& p, Type& r);

    // ===--------------------------------------------------------------------===//
    // Get
    // ===--------------------------------------------------------------------===//
    AttributeInfo GetAttributeInfo() const;
    const FuncSigInfo& GetCondition() const;
    std::vector<GenericType*> GetGenericTypeParams() const;
    Type* GetInstParentType() const;
    Type* GetMethodInstRetType() const;
    std::string GetMethodName() const;
    FuncType* GetMethodSigType() const;
    FuncType* GetOriginalFuncType() const;
    FuncBase* GetVirtualMethod() const;

    // ===--------------------------------------------------------------------===//
    // Set
    // ===--------------------------------------------------------------------===//
    void SetFuncName(const std::string& newName);
    void SetInstParentType(Type& newParentTy);
    void SetOriginalFuncType(FuncType& newFuncType);
    void SetVirtualMethod(FuncBase* newFunc);
    void UpdateMethodInfo(const VirtualMethodInfo& newInfo);
    void ConvertPrivateType(
        std::function<FuncType*(FuncType&)>& convertFuncParamsAndRetType, std::function<Type*(Type&)>& convertType);
    
    // ===--------------------------------------------------------------------===//
    // Judgement
    // ===--------------------------------------------------------------------===//
    bool FuncSigIsMatched(const FuncSigInfo& other, CHIRBuilder& builder) const;
    bool FuncSigIsMatched(const FuncCallType& other,
        std::unordered_map<const GenericType*, Type*>& replaceTable, CHIRBuilder& builder) const;
    bool TestAttr(Attribute a) const;

private:
    // condition
    FuncSigInfo condition;
    // result
    FuncBase* instance{nullptr};
    AttributeInfo attr;
    FuncType* originalType{nullptr}; // virtual func's original func type from parent def, (param types)->retType,
                                     // param types include `this` type
    Type* parentType{nullptr}; // CustomType or extended type(may be primitive type)
    Type* returnType{nullptr}; // instantiated type
};

class VTableInType {
public:
    VTableInType();
    explicit VTableInType(ClassType& p);
    VTableInType(ClassType& p, std::vector<VirtualMethodInfo>&& methods);

    // ===--------------------------------------------------------------------===//
    // Get
    // ===--------------------------------------------------------------------===//
    size_t GetMethodNum() const;
    std::vector<VirtualMethodInfo>& GetModifiableVirtualMethods();
    ClassType* GetSrcParentType() const;
    const std::vector<VirtualMethodInfo>& GetVirtualMethods() const;

    // ===--------------------------------------------------------------------===//
    // Set
    // ===--------------------------------------------------------------------===//
    void AppendNewMethod(VirtualMethodInfo&& newMethod);
    void ConvertPrivateType(
        std::function<FuncType*(FuncType&)>& convertFuncParamsAndRetType, std::function<Type*(Type&)>& convertType);

    // ===--------------------------------------------------------------------===//
    // Judgement
    // ===--------------------------------------------------------------------===//
    bool TheFirstLevelIsMatched(const ClassType& parentType) const;
    bool IsEmpty() const;
    
private:
    // the 1st level
    ClassType* srcParentType{nullptr};
    std::unordered_map<Type*, std::unordered_set<ClassType*>> genericConstraints;
    // the 2nd level
    std::vector<VirtualMethodInfo> virtualMethods;
};

class VTableInDef {
public:
    // ===--------------------------------------------------------------------===//
    // Get
    // ===--------------------------------------------------------------------===//
    const VTableInType& GetExpectedTypeVTable(const ClassType& srcParentType) const;
    std::vector<VTableInType>& GetModifiableTypeVTables();
    const std::vector<VTableInType>& GetTypeVTables() const;
    
    // ===--------------------------------------------------------------------===//
    // Set
    // ===--------------------------------------------------------------------===//
    void AddNewItemToTypeVTable(ClassType& srcParent, VirtualMethodInfo&& funcInfo);
    void AddNewItemToTypeVTable(ClassType& srcParent, std::vector<VirtualMethodInfo>&& funcInfos);
    void UpdateItemInTypeVTable(
        ClassType& srcClassTy, size_t index, FuncBase* newFunc, Type* newParentTy, const std::string& newName);

private:
    // src parent type -> vtable index, just for fast lookup
    std::unordered_map<const ClassType*, size_t> srcParentIndex;
    std::vector<VTableInType> vtables;
    VTableInType empty;
};

struct VTableSearchRes {
    ClassType* instSrcParentType{nullptr};     // instantiated by instantiate func type
    ClassType* halfInstSrcParentType{nullptr}; // instantiated by current def
    FuncType* originalFuncType{nullptr};       // a generic func type, from current def, not parent def
    FuncBase* instance{nullptr};
    CustomTypeDef* originalDef{nullptr};       // this virtual func belongs to a vtable,
                                               // and this vtable belongs to a CustomTypeDef
    std::vector<GenericType*> genericTypeParams;
    AttributeInfo attr;
    size_t offset{0};
};

using ConvertTypeFunc = std::function<Type*(Type&)>;
}
#endif