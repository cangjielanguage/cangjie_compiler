// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_CHIR_USER_DEFINED_TYPE_H
#define CANGJIE_CHIR_USER_DEFINED_TYPE_H

#include "cangjie/AST/Node.h"
#include "cangjie/CHIR/IR/AttributeInfo.h"
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
    /** @brief Constructor to initialize virtual method information
     *  @param c Function signature information (including function name, function type, and generic type parameters)
     *  @param func Function instance pointer
     *  @param a Attribute information
     *  @param o Original function type (from parent definition)
     *  @param p Instantiated parent type
     *  @param r Instantiated return type
     */
    VirtualMethodInfo(FuncSigInfo&& c, FuncBase* func, const AttributeInfo& a, FuncType& o, Type& p, Type& r);

    // ===--------------------------------------------------------------------===//
    // Get
    // ===--------------------------------------------------------------------===//
    /** @brief Get attribute information */
    AttributeInfo GetAttributeInfo() const;
    /** @brief Get function signature condition information (including function name, function type, and generic type parameters) */
    const FuncSigInfo& GetCondition() const;
    /** @brief Get generic type parameters list */
    std::vector<GenericType*> GetGenericTypeParams() const;
    /** @brief Get instantiated parent type */
    Type* GetInstParentType() const;
    /** @brief Get method instantiated return type */
    Type* GetMethodInstRetType() const;
    /** @brief Get method name */
    std::string GetMethodName() const;
    /** @brief Get method signature type */
    FuncType* GetMethodSigType() const;
    /** @brief Get original function type (from parent definition, including generic parameters) */
    FuncType* GetOriginalFuncType() const;
    /** @brief Get virtual method function instance */
    FuncBase* GetVirtualMethod() const;

    // ===--------------------------------------------------------------------===//
    // Set
    // ===--------------------------------------------------------------------===//
    /** @brief Set function name
     *  @param newName New function name
     */
    void SetFuncName(const std::string& newName);
    /** @brief Set instantiated parent type
     *  @param newParentTy New parent type
     */
    void SetInstParentType(Type& newParentTy);
    /** @brief Set original function type
     *  @param newFuncType New function type
     */
    void SetOriginalFuncType(FuncType& newFuncType);
    /** @brief Set virtual method function instance
     *  @param newFunc New function instance
     */
    void SetVirtualMethod(FuncBase* newFunc);
    /** @brief Update method information (copy information from another VirtualMethodInfo)
     *  @param newInfo New method information to copy from
     */
    void UpdateMethodInfo(const VirtualMethodInfo& newInfo);
    /** @brief Convert private types (used in type conversion scenarios)
     *  @param convertFuncParamsAndRetType Conversion function for function parameters and return type
     *  @param convertType Conversion function for types
     */
    void ConvertPrivateType(
        std::function<FuncType*(FuncType&)>& convertFuncParamsAndRetType, std::function<Type*(Type&)>& convertType);
    
    // ===--------------------------------------------------------------------===//
    // Judgement
    // ===--------------------------------------------------------------------===//
    /** @brief Check if function signature matches (compare with FuncSigInfo)
     *  @param other Function signature information to compare
     *  @param builder CHIR builder
     *  @return true if signatures match, false otherwise
     */
    bool FuncSigIsMatched(const FuncSigInfo& other, CHIRBuilder& builder) const;
    /** @brief Check if function signature matches (compare with FuncCallType, supports generic substitution)
     *  @param other Function call type to compare
     *  @param replaceTable Generic type replacement table (output parameter)
     *  @param builder CHIR builder
     *  @return true if signatures match, false otherwise
     */
    bool FuncSigIsMatched(const FuncCallType& other,
        std::unordered_map<const GenericType*, Type*>& replaceTable, CHIRBuilder& builder) const;
    /** @brief Test if the method has the specified attribute
     *  @param a Attribute to test
     *  @return true if the method has the attribute, false otherwise
     */
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
    /** @brief Default constructor, creates an empty virtual function table */
    VTableInType();
    /** @brief Constructor, creates a virtual function table for the specified parent type
     *  @param p Source parent type (ClassType)
     */
    explicit VTableInType(ClassType& p);
    /** @brief Constructor, creates a virtual function table for the specified parent type and initializes method list
     *  @param p Source parent type (ClassType)
     *  @param methods Virtual method information list
     */
    VTableInType(ClassType& p, std::vector<VirtualMethodInfo>&& methods);

    // ===--------------------------------------------------------------------===//
    // Get
    // ===--------------------------------------------------------------------===//
    /** @brief Get the number of virtual methods */
    size_t GetMethodNum() const;
    /** @brief Get modifiable virtual methods list (non-const reference) */
    std::vector<VirtualMethodInfo>& GetModifiableVirtualMethods();
    /** @brief Get source parent type pointer */
    ClassType* GetSrcParentType() const;
    /** @brief Get virtual methods list (const reference) */
    const std::vector<VirtualMethodInfo>& GetVirtualMethods() const;

    // ===--------------------------------------------------------------------===//
    // Set
    // ===--------------------------------------------------------------------===//
    /** @brief Append a new virtual method to the virtual function table
     *  @param newMethod New virtual method information (rvalue reference)
     */
    void AppendNewMethod(VirtualMethodInfo&& newMethod);
    /** @brief Convert private types (used in type conversion scenarios)
     *  @param convertFuncParamsAndRetType Conversion function for function parameters and return type
     *  @param convertType Conversion function for types
     */
    void ConvertPrivateType(
        std::function<FuncType*(FuncType&)>& convertFuncParamsAndRetType, std::function<Type*(Type&)>& convertType);

    // ===--------------------------------------------------------------------===//
    // Judgement
    // ===--------------------------------------------------------------------===//
    /** @brief Check if the virtual function table is empty (i.e., srcParentType is nullptr) */
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
    /** @brief Get the corresponding virtual function table based on the source parent type
     *  @param srcParentType Source parent type
     *  @return Reference to the virtual function table if found, otherwise returns an empty virtual function table
     */
    const VTableInType& GetExpectedTypeVTable(const ClassType& srcParentType) const;
    /** @brief Get modifiable type virtual function tables list (non-const reference) */
    std::vector<VTableInType>& GetModifiableTypeVTables();
    /** @brief Get type virtual function tables list (const reference) */
    const std::vector<VTableInType>& GetTypeVTables() const;
    
    // ===--------------------------------------------------------------------===//
    // Set
    // ===--------------------------------------------------------------------===//
    /** @brief Add a new virtual method to the virtual function table of the specified type
     *  @param srcParent Source parent type
     *  @param funcInfo New virtual method information (rvalue reference)
     *  @note If the virtual function table for this type does not exist, it will be created automatically
     */
    void AddNewItemToTypeVTable(ClassType& srcParent, VirtualMethodInfo&& funcInfo);
    /** @brief Create a new virtual function table for the specified type and add virtual method list
     *  @param srcParent Source parent type
     *  @param funcInfos Virtual method information list (rvalue reference)
     *  @note Requires that the virtual function table for this type does not exist, otherwise an assertion will be triggered
     */
    void AddNewItemToTypeVTable(ClassType& srcParent, std::vector<VirtualMethodInfo>&& funcInfos);
    /** @brief Update method information at the specified index in the virtual function table of the specified type
     *  @param srcClassTy Source class type
     *  @param index Index of the method in the virtual function table
     *  @param newFunc New function instance (can be nullptr)
     *  @param newParentTy New parent type (can be nullptr)
     *  @param newName New function name (empty string means no update)
     */
    void UpdateItemInTypeVTable(
        ClassType& srcClassTy, size_t index, FuncBase* newFunc, Type* newParentTy, const std::string& newName);
    /** @brief Convert private types (used in type conversion scenarios)
     *  @param convertFuncParamsAndRetType Conversion function for function parameters and return type
     *  @param convertType Conversion function for types
     */
    void ConvertPrivateType(
            std::function<FuncType*(FuncType&)>& convertFuncParamsAndRetType, std::function<Type*(Type&)>& convertType);

private:
    // src parent type -> vtable index, just for fast lookup
    std::unordered_map<ClassType*, size_t> srcParentIndex;
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