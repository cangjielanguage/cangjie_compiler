// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares the Type class in CHIR.
 */

#ifndef CANGJIE_CHIR_TYPE_H
#define CANGJIE_CHIR_TYPE_H

#include "cangjie/CHIR/AttributeInfo.h"
#include "cangjie/CHIR/UserDefinedType.h"
#include "cangjie/Utils/CheckUtils.h"

#include <algorithm>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Cangjie::CHIR {
class CHIRContext;
class CustomTypeDef;
class ClassDef;
class EnumDef;
class ExtendDef;
struct EnumCtorInfo;
class StructDef;
class FuncBase;
class ClassType;
class CHIRBuilder;
class GenericType;
struct AbstractMethodInfo;
struct AbstractMethodParam;

/** HashCombine is a function used to create hash with fewer collisions. */
template <typename T> inline void HashCombine(size_t& hashVal, const T& val)
{
    hashVal ^= std::hash<T>()(val) + 0x9e3779b9 + (hashVal << 6) +
        (hashVal >> 2); // 6, 2 are specific constant in the hash algorithm.
}

// The base case.
template <typename T> inline void HashValue(std::size_t& hashVal, const T& val)
{
    HashCombine(hashVal, val);
}

// Iterate for any args.
template <typename T, typename... Types>
inline void HashValue(std::size_t& hashVal, const T& val, const Types&... others)
{
    HashCombine(hashVal, val);
    HashValue(hashVal, others...);
}

template <typename... Types> inline size_t HashValue(const Types&... values)
{
    std::size_t hashVal = 0;
    HashValue(hashVal, values...);
    return hashVal;
}

/*
 * @brief Definitions of all of the base types for the Type system.
 *
 * The instances of the Type class are immutable: once they are created,they are never changed.
 *
 */
class Type {
public:
    enum TypeKind : uint8_t {
        TYPE_INVALID = 0,
        // integer
        TYPE_INT8,
        TYPE_INT16,
        TYPE_INT32,
        TYPE_INT64,
        TYPE_INT_NATIVE,
        // unsigned integer
        TYPE_UINT8,
        TYPE_UINT16,
        TYPE_UINT32,
        TYPE_UINT64,
        TYPE_UINT_NATIVE,
        // float
        TYPE_FLOAT16,
        TYPE_FLOAT32,
        TYPE_FLOAT64,
        // other primitive type
        TYPE_RUNE,
        TYPE_BOOLEAN,
        TYPE_UNIT,
        TYPE_NOTHING,
        // Void type
        TYPE_VOID,
        // composite type
        TYPE_TUPLE,
        TYPE_STRUCT,
        TYPE_ENUM,
        TYPE_FUNC,
        TYPE_CLASS,
        // Built-in array related type
        TYPE_RAWARRAY,
        TYPE_VARRAY,
        // Built-in CFFI related type
        TYPE_CPOINTER,
        TYPE_CSTRING,
        // Generic type
        TYPE_GENERIC,
        // Referece to an value with abritray type
        TYPE_REFTYPE,
        // Built-in box type
        TYPE_BOXTYPE,
        TYPE_THIS,
        MAX_TYPE_KIND,
    };

protected:
    explicit Type(TypeKind kind);

public:
    virtual ~Type() = default;

    virtual std::string ToString() const;

    void Dump() const;

    /** @brief Return the type kind. */
    inline TypeKind GetTypeKind() const
    {
        return kind;
    }

    bool SatisfyCType() const;

    bool IsSameTypeKind(const Type& type) const
    {
        return this->kind == type.kind;
    }

    /** @brief Check whether the type is Interger. */
    bool IsInteger() const
    {
        return kind >= TYPE_INT8 && kind <= TYPE_UINT_NATIVE;
    }

    bool IsUnsignedInteger() const
    {
        return kind >= TYPE_UINT8 && kind <= TYPE_UINT_NATIVE;
    }

    bool IsInvalid() const
    {
        return kind == TYPE_INVALID;
    }

    /** @brief Check whether the type is Float. */
    bool IsFloat() const
    {
        return kind >= TYPE_FLOAT16 && kind <= TYPE_FLOAT64;
    }

    bool IsRune() const
    {
        return kind == TYPE_RUNE;
    }

    bool IsBoolean() const
    {
        return kind == TYPE_BOOLEAN;
    }

    bool IsUnit() const
    {
        return kind == TYPE_UNIT;
    }

    bool IsNothing() const
    {
        return kind == TYPE_NOTHING;
    }

    bool IsPrimitive() const
    {
        return kind >= TYPE_INT8 && kind <= TYPE_NOTHING;
    }

    bool IsTuple() const
    {
        return kind == TYPE_TUPLE;
    }

    /**
     * @brief if this is a class type for representing the context of a closure, return true.
     */
    bool IsAutoEnv() const;

    /**
     * @brief if this is a abstract class type for representing the context of a closure, return true.
     */
    bool IsAutoEnvBase() const;

    bool IsAutoEnvInstBase() const;

    bool IsAutoEnvGenericBase() const;

    bool IsFunc() const
    {
        return kind == TYPE_FUNC;
    }

    bool IsStruct() const
    {
        return kind == TYPE_STRUCT;
    }

    bool IsClass() const
    {
        return kind == TYPE_CLASS;
    }

    bool IsRawArray() const
    {
        return kind == TYPE_RAWARRAY;
    }

    bool IsVArray() const
    {
        return kind == TYPE_VARRAY;
    }

    bool IsEnum() const
    {
        return kind == TYPE_ENUM;
    }

    bool IsCPointer() const
    {
        return kind == TYPE_CPOINTER;
    }

    bool IsCString() const
    {
        return kind == TYPE_CSTRING;
    }

    bool IsString() const;

    bool IsClassRef(bool nullable = false) const;

    bool IsGeneric() const
    {
        return kind == TYPE_GENERIC;
    }

    bool IsVoid() const
    {
        return kind == TYPE_VOID;
    }

    bool IsRef() const
    {
        return kind == TYPE_REFTYPE;
    }

    bool IsNominal() const
    {
        return kind == TYPE_CLASS || kind == TYPE_STRUCT || kind == TYPE_ENUM;
    }

    bool IsClassOrArray() const
    {
        return kind == TYPE_CLASS || kind == TYPE_RAWARRAY;
    }

    bool IsBox() const
    {
        return kind == TYPE_BOXTYPE;
    }

    bool IsThis() const
    {
        return kind == TYPE_THIS;
    }

    bool IsConstant() const;

    bool IsAny() const;

    bool IsCJFunc() const;
    bool IsCFunc() const;
    bool IsCType() const;

    bool IsStructArray() const;
    bool IsCustomType() const
    {
        return kind == TYPE_CLASS || kind == TYPE_STRUCT || kind == TYPE_ENUM;
    }
    bool IsBuiltinType() const;
    bool IsValueType() const
    {
        return IsPrimitive() || IsEnum() || IsTuple() || IsStruct() || IsVArray() || IsCPointer() || IsCString() ||
            IsFunc();
    }

    bool IsValueOrGenericType() const
    {
        return IsValueType() || IsGeneric();
    }

    bool IsReferenceType() const
    {
        return IsClassOrArray() || IsBox() || IsThis();
    }

    Type* StripAllRefs() const;

    bool IsReferenceTypeWithRefDims(size_t dims) const;

    bool IsValueOrGenericTypeWithRefDims(size_t dims) const;

    bool IsGenericRelated() const
    {
        if (IsGeneric()) {
            return true;
        }
        for (auto arg : GetTypeArgs()) {
            if (arg->IsGenericRelated()) {
                return true;
            }
        }
        return false;
    }

    /** @brief Get hash value. */
    virtual size_t Hash() const;

    /** @brief Check whether two Type instances are equal. */
    virtual bool operator==(const Type& other) const;

    /** @brief Get the nested levels of nested reference type. */
    uint8_t GetRefDims() const
    {
        return refDims;
    }

    std::vector<Type*> GetTypeArgs() const
    {
        return argTys;
    }

    /**
     * @brief if `this` is generic related, and we need to know which type every generic type is instantiated to
     * @param targetTy: an instantiated type whose type structure is same with `this`
     * @return if `this` is not generic related, an empty map will return
     * if type structure between `this` and `targetTy` is different, `false` will return
     */
    std::pair<bool, std::unordered_map<const GenericType*, Type*>> CalculateGenericTyMapping(
        const Type& targetTy) const;

    /** @brief visit type, its type args, its type args' type args recursively
     *  @param visitor: a lambda, use current type or type args as param
     *  if lambda returns false, then type args won't be visited
     *  if lambda returns true, then visit all type args
     */
    void VisitTypeRecursively(const std::function<bool(const Type&)>& visitor) const;

    bool IsEqualOrSubTypeOf(const Type& parentType, CHIRBuilder& builder,
        std::set<std::pair<const Type*, const Type*>>* visited = nullptr) const;

    bool IsEqualOrInstantiatedTypeOf(const Type& genericRelatedType, CHIRBuilder& builder,
        std::set<std::pair<const Type*, const Type*>>* visited = nullptr) const;

    virtual std::vector<FuncBase*> GetDeclareAndExtendMethods(CHIRBuilder& builder) const;

    virtual const std::vector<ExtendDef*>& GetExtends(CHIRBuilder* builder = nullptr) const;

protected:
    /**
     * funcType: argTys = { paramTypes, returnType }
     * refType: argTys = { baseType }
     * tupleType: argTys = { fieldTypes }
     * rawArray: argTys = { elementType }
     * struct/class/enum: argTys = { generic instantiated arg types}
     * closureType: argTys = {funcType, envType }
     * others argTys is empty
     */
    std::vector<Type*> argTys;
    TypeKind kind : 8;  // The current base type of this type.
    uint8_t refDims{0}; // The nested reference levels, default 0 indicate non-reference type.
};

class BuiltinType : public Type {
    friend class CHIRSerializer;
public:
    explicit BuiltinType(TypeKind kind) : Type{kind} {}
    ~BuiltinType() override = default;

    /** @brief visit all extend defs which meet the condition, and get their super interfaces recursively
     *
     *  interface I1 {}
     *  interface I2 <: I1 {}
     *  extend CPointer<Bool> <: I2 {} // meet the condition
     *
     *  interface I3 {}
     *  interface I4 <: I3 {}
     *  extend CPointer<Int64> <: I4 {} // NOT meet the condition
     *
     *  interface I5 {}
     *  extend Bool <: I5 {}
     *  interface I6 {}
     *  extend<T> CPointer<T> <: I6 where T <: I5 {} // meet the condition
     *
     *  interface I7 {}
     *  extend<T> CPointer<T> <: I7 where T <: I1 {} // NOT meet the condition
     *
     *  so the result of `CPointer<Bool>.GetSuperTypesRecusively()` is {I1, I2, I6}
     */
    std::vector<ClassType*> GetSuperTypesRecusively(CHIRBuilder& builder,
        std::set<std::pair<const Type*, const Type*>>* visited = nullptr) const;

    const std::vector<ExtendDef*>& GetExtends(CHIRBuilder* builder = nullptr) const override;
    void AddExtend(ExtendDef& extend);

    std::vector<FuncBase*> GetExtendMethods() const;
    std::vector<FuncBase*> GetDeclareAndExtendMethods(CHIRBuilder& builder) const override;

protected:
    std::vector<ExtendDef*> extends;
};

class NumericType : public BuiltinType {
    const std::map<TypeKind, uint64_t> INTEGER_TO_BIT_LEN{
        {TypeKind::TYPE_INT8, 8},
        {TypeKind::TYPE_INT16, 16},
        {TypeKind::TYPE_INT32, 32},
        {TypeKind::TYPE_INT64, 64},
        {TypeKind::TYPE_UINT8, 8},
        {TypeKind::TYPE_UINT16, 16},
        {TypeKind::TYPE_UINT32, 32},
        {TypeKind::TYPE_UINT64, 64},
#if (defined(__x86_64__) || defined(__aarch64__))
        {TypeKind::TYPE_UINT_NATIVE, 64},
        {TypeKind::TYPE_INT_NATIVE, 64},
#else
        {TypeKind::TYPE_UINT_NATIVE, 32},
        {TypeKind::TYPE_INT_NATIVE, 32},
#endif
        {TypeKind::TYPE_FLOAT16, 16},
        {TypeKind::TYPE_FLOAT32, 32},
        {TypeKind::TYPE_FLOAT64, 64},
    };

protected:
    explicit NumericType(TypeKind kind) : BuiltinType(kind)
    {
    }
    ~NumericType() override = default;

public:
    /**
     * @brief Get bit length of current numeric type.
     */
    uint64_t GetBitness() const
    {
        return INTEGER_TO_BIT_LEN.at(kind);
    }
};

class FloatType : public NumericType {
private:
    explicit FloatType(TypeKind kind);
    ~FloatType() override = default;
    friend class CHIRContext;
};

class IntType : public NumericType {
public:
    bool IsSigned() const
    {
        return kind >= TYPE_INT8 && kind <= TYPE_INT_NATIVE;
    }

    bool IsIntNative() const
    {
        return kind == TYPE_INT_NATIVE;
    }

    bool IsUIntNative() const
    {
        return kind == TYPE_UINT_NATIVE;
    }

private:
    explicit IntType(TypeKind kind);
    ~IntType() override = default;
    friend class CHIRContext;
};

class FuncType : public Type {
public:
    Type* GetReturnType() const
    {
        return argTys.back();
    }

    std::vector<Type*> GetParamTypes() const
    {
        return std::vector<Type*>(argTys.begin(), argTys.end() - 1);
    }

    size_t GetNumOfParams() const
    {
        CJC_ASSERT(!argTys.empty());
        return argTys.size() - 1;
    }

    Type* GetParamType(size_t index) const
    {
        CJC_ASSERT(index + 1 < argTys.size());
        return argTys[index];
    }

    /**
     * @brief Mark whether this FuncType has `VarLenParam`
     *
     * Note: Only foreign functions can have variable-length arguments,
     * which means the func must be a CFunc.
     *
     * the Cangjie self's (without foreign) `VarLenParam` is just a desugar, which is desugared to
     * Struct-Array and not reflected on CHIR.
     *
     */
    inline bool HasVarArg() const
    {
        return hasVarArg;
    }

    std::string ToString() const override;

    size_t Hash() const override;

    bool operator==(const Type& other) const override;

private:
    explicit FuncType(
        const std::vector<Type*>& paramTys, Type* retTy, bool hasVarLenParam = false, bool isCFunc = false)
        : Type(TypeKind::TYPE_FUNC), hasVarArg(hasVarLenParam), isCFunc(isCFunc)
    {
        this->argTys = paramTys;
        this->argTys.emplace_back(retTy);
    }
    ~FuncType() override = default;
    friend class CHIRContext;
    friend class Type;

    /**
     * Whether params in this FuncType has `VarLenParam`, the func must be CFunc.
     */
    bool hasVarArg{false};
    bool isCFunc{false};
};

class CustomType : public Type {
    friend class CHIRSerializer;
public:
    CustomTypeDef* GetCustomTypeDef() const;

    std::vector<Type*> GetGenericArgs() const;

    /** @brief visit CustomTypeDef and all extend defs which meet the condition,
     *  and get their super classes or interfaces recursively
     *
     *  interface I1 {}
     *  interface I2 <: I1 {}
     *  class C1 <: I2
     *
     *  interface I3 {}
     *  interface I4 <: I3 {}
     *  extend C1 <: I4
     *
     *  interface I5 {}
     *  class C2 <: C1 & I5
     *
     *  interface I6 {}
     *  extend C2 <: I6 {}
     *
     *  so the result of `C2.GetSuperTypesRecusively()` is {I1 ~ I6, C1}
     */
    std::vector<ClassType*> GetSuperTypesRecusively(CHIRBuilder& builder,
        std::set<std::pair<const Type*, const Type*>>* visited = nullptr);

    /**
     * @brief Retrieves the instance map for generic types.
     *
     * @param instMap The map to store the instance types.
     * @param builder The CHIR builder used for building the types.
     */
    void GetInstMap(std::unordered_map<const GenericType*, Type*>& instMap, CHIRBuilder& builder) const;
    
    /**
     * @brief Retrieves the instantiated member type by a given path.
     *
     * @param path The path to the member type.
     * @param builder The CHIR builder used for building the type.
     * @return The instantiated member type.
     */
    Type* GetInstMemberTypeByPath(const std::vector<uint64_t>& path, CHIRBuilder& builder) const
    {
        return GetInstMemberTypeByPathCheckingReadOnly(path, builder).first;
    }
    
    /**
     * @brief Retrieves the instantiated method types.
     *
     * @param builder The CHIR builder used for building the types.
     * @return A vector of instantiated method types.
     */
    std::vector<FuncType*> GetInstMethodTypes(CHIRBuilder& builder) const;
    
    /**
     * @brief Retrieves the instantiated member type by a given path, checking for read-only.
     *
     * @param path The path to the member type.
     * @param builder The CHIR builder used for building the type.
     * @return A pair containing the type and a boolean flag indicating read-only status.
     */
    std::pair<Type*, bool> GetInstMemberTypeByPathCheckingReadOnly(
        const std::vector<uint64_t>& path, CHIRBuilder& builder) const;
    
    /**
     * @brief Retrieves all instance member types.
     *
     * @param builder The CHIR builder used for building the types.
     * @return A vector of instance member types.
     */
    std::vector<Type*> GetInstantiatedMemberTys(CHIRBuilder& builder);
    
    /**
     * @brief Retrieves the implemented interface types.
     *
     * @param builder The CHIR builder used for building the types.
     * @return A vector of implemented interface types.
     */
    std::vector<ClassType*> GetImplementedInterfaceTys(CHIRBuilder* builder,
        std::set<std::pair<const Type*, const Type*>>* visited = nullptr);
    
    /**
     * @brief Retrieves the implemented interface types without extension.
     *
     * @param builder The CHIR builder used for building the types.
     * @return A vector of implemented interface types without extension.
     */
    std::vector<ClassType*> GetImplementedInterfaceTysWithoutExtend(CHIRBuilder& builder);
    
    /**
     * @brief Retrieves the exact parent type for a given function.
     *
     * @param funcName The name of the function.
     * @param funcType The type of the function.
     * @param isStatic Indicates whether the function is static.
     * @param funcInstTypeArgs A vector to store the function instance type arguments.
     * @param builder The CHIR builder used for building the type.
     * @param checkAbstractMethod Indicates whether to check for an abstract method.
     * @return The exact parent type.
     */
    Type* GetExactParentType(const std::string& funcName, FuncType& funcType, bool isStatic,
        std::vector<Type*>& funcInstTypeArgs, CHIR::CHIRBuilder& builder, bool checkAbstractMethod);
    
    /**
     * @brief Retrieves the expected function for a given name and type.
     *
     * @param funcName The name of the function.
     * @param funcType The type of the function.
     * @param isStatic Indicates whether the function is static.
     * @param funcInstTypeArgs A vector to store the function instance type arguments.
     * @param builder The CHIR builder used for building the function.
     * @param checkAbstractMethod Indicates whether to check for an abstract method.
     * @return A pair containing the expected function and a boolean flag.
     */
    std::pair<FuncBase*, bool> GetExpectedFunc(const std::string& funcName, FuncType& funcType, bool isStatic,
        std::vector<Type*>& funcInstTypeArgs, CHIR::CHIRBuilder& builder, bool checkAbstractMethod);
    
    /**
     * @brief Retrieves the index of a function in the virtual table.
     *
     * @param funcCallType Function name and type.
     * @param isStatic Indicates whether the function is static.
     * @param builder The CHIR builder used for building the function.
     * @return The virtual table search result.
     */
    std::vector<VTableSearchRes> GetFuncIndexInVTable(
        const FuncCallType& funcCallType, bool isStatic, CHIR::CHIRBuilder& builder) const;
    
    /**
     * @brief Retrieves the declared and extended methods.
     *
     * @param builder The CHIR builder used for building the methods.
     * @return A vector of declared and extended methods.
     */
    std::vector<FuncBase*> GetDeclareAndExtendMethods(CHIRBuilder& builder) const override;
    
    virtual void ResetAllInstantiatedType()
    {
        implementedInterfaceTys.clear();
        instantiatedMemberTys.clear();
        hasSetSuperInterface = false;
        hasSetInstMemberTy = false;
    }

    const std::vector<ExtendDef*>& GetExtends(CHIRBuilder* builder = nullptr) const override;

    size_t Hash() const override;

    bool operator==(const Type& other) const override;

protected:
    explicit CustomType(TypeKind kind, CustomTypeDef* def, const std::vector<Type*>& typeArgs);
    ~CustomType() override = default;
    friend class CHIRContext;

protected:
    CustomTypeDef* def{nullptr}; // Custom Type Define.
    std::vector<ClassType*> implementedInterfaceTys;
    std::vector<Type*> instantiatedMemberTys;

private:
    std::vector<ClassType*> CalculateImplementedInterfaceTys(CHIRBuilder& builder,
        std::set<std::pair<const Type*, const Type*>>* visited = nullptr);
    std::vector<Type*> CalculateCurDefInstantiatedMemberTys(CHIRBuilder& builder);
    std::vector<ClassType*> CalculateExtendImplementedInterfaceTys(CHIRBuilder& builder,
        std::set<std::pair<const Type*, const Type*>>* visited = nullptr) const;

private:
    bool hasSetSuperInterface{false};
    bool hasSetInstMemberTy{false};
    std::recursive_mutex setSuperInterfaceMtx;
    std::mutex setInstMemberTyMtx;
};

class ClassType : public CustomType {
public:
    ClassDef* GetClassDef() const;

    ClassType* GetSuperClassTy(CHIRBuilder* builder);

    std::string ToString() const override;

    void ResetAllInstantiatedType() override
    {
        CustomType::ResetAllInstantiatedType();
        superClassTy = nullptr;
        hasSetSuperClass = false;
    }

    std::vector<AbstractMethodInfo> GetInstAbstractMethodTypes(CHIRBuilder& builder) const;
private:
    explicit ClassType(ClassDef* classDef, const std::vector<Type*>& genericArgs = {});
    ~ClassType() override = default;
    friend class CHIRContext;

    ClassType* CalculateSuperClassTy(CHIRBuilder& builder);

private:
    ClassType* superClassTy{nullptr};
    bool hasSetSuperClass{false};
    std::mutex setSuperClassMtx;
};

class StructType : public CustomType {
public:
    StructDef* GetStructDef() const;

    std::string ToString() const override;

private:
    explicit StructType(StructDef* structDef, const std::vector<Type*>& genericArgs = {});
    ~StructType() override = default;
    friend class CHIRContext;
};

class EnumType : public CustomType {
public:
    EnumDef* GetEnumDef() const;

    /**
     * @brief Is Option enum from std/core
     */
    bool IsOption() const;

    std::vector<EnumCtorInfo> GetConstructorInfos(CHIRBuilder& builder) const;

    std::string ToString() const override;

    /**
     * @brief whether this EnumType is boxed, boxed Enum Type is used to break the ring of types.
     *
     * e.g.      *
     * struct S {
     *     let x:Option<S> = None
     * }
     *
     * the type of S's member x above is boxed EnumType, otherwise as struct and enum are both value type,
     * S will contain Option<S>, Option<S> contains S, S contains Option<S> again,
     * and infinite recursion of inclusion continues.
     */
    bool IsBoxed(CHIRBuilder& builder);

private:
    explicit EnumType(EnumDef* enumDef, const std::vector<Type*>& genericArgs = {});
    ~EnumType() override = default;
    friend class CHIRContext;

    bool isBoxed = false; // Annotation: indicate EnumType is boxed.

    bool CheckIsBoxed(const EnumType& original,
        Type& curType, CHIRBuilder& builder, bool doCheck, std::unordered_set<Type*>& visited);
};

class TupleType : public Type {
public:
    std::vector<Type*> GetElementTypes() const
    {
        return argTys;
    }

    Type* GetElementType(size_t index) const
    {
        return argTys[index];
    }

    std::string ToString() const override;

private:
    explicit TupleType(const std::vector<Type*>& argTys) : Type(TypeKind::TYPE_TUPLE)
    {
        this->argTys = argTys;
    }
    ~TupleType() override = default;
    friend class CHIRContext;
};

class RefType : public Type {
public:
    std::string ToString() const override;

    Type* GetBaseType() const
    {
        return argTys[0];
    }

    /**
     * @brief recursively get base type
     */
    Type* GetRootBaseType() const
    {
        Type* root = argTys[0];
        if (argTys[0]->GetTypeKind() == Type::TypeKind::TYPE_REFTYPE) {
            root = static_cast<RefType*>(argTys[0])->GetRootBaseType();
        }
        return root;
    }

private:
    explicit RefType(Type* baseType) : Type(TypeKind::TYPE_REFTYPE)
    {
        this->argTys.emplace_back(baseType);
        this->refDims = static_cast<uint8_t>(baseType->GetRefDims() + static_cast<uint8_t>(1));
    }
    ~RefType() override = default;
    friend class CHIRContext;
};

class BoxType : public Type {
public:
    std::string ToString() const override;

    Type* GetBaseType() const
    {
        return argTys[0];
    }

private:
    explicit BoxType(Type* baseType) : Type(TypeKind::TYPE_BOXTYPE)
    {
        this->argTys.emplace_back(baseType);
    }
    ~BoxType() override = default;
    friend class CHIRContext;
};

class ThisType : public Type {
public:
    std::string ToString() const override;

private:
    explicit ThisType() : Type(TypeKind::TYPE_THIS)
    {
    }
    ~ThisType() override = default;
    friend class CHIRContext;
};

class RawArrayType : public BuiltinType {
public:
    std::string ToString() const override;

    size_t Hash() const override;

    bool operator==(const Type& other) const override;

    unsigned int GetDims() const
    {
        return dims;
    }

    Type* GetElementType() const
    {
        return argTys[0];
    }

private:
    explicit RawArrayType(Type* elemTy, unsigned int dims) : BuiltinType(TypeKind::TYPE_RAWARRAY), dims(dims)
    {
        argTys.emplace_back(elemTy);
    }
    ~RawArrayType() override = default;
    friend class CHIRContext;

    unsigned int dims; // array dimensions.
};

class VArrayType : public BuiltinType {
public:
    std::string ToString() const override;

    size_t Hash() const override;

    bool operator==(const Type& other) const override;

    unsigned int GetSize() const
    {
        return static_cast<unsigned>(size);
    }

    Type* GetElementType() const
    {
        return argTys[0];
    }

private:
    explicit VArrayType(Type* elemTy, int64_t size) : BuiltinType(TypeKind::TYPE_VARRAY), size(size)
    {
        argTys.emplace_back(elemTy);
    }
    ~VArrayType() override = default;
    friend class CHIRContext;

    int64_t size; // varray size.
};

class RuneType : public BuiltinType {
private:
    explicit RuneType() : BuiltinType(TypeKind::TYPE_RUNE)
    {
    }
    ~RuneType() override = default;
    friend class CHIRContext;
};

class BooleanType : public BuiltinType {
private:
    explicit BooleanType() : BuiltinType(TypeKind::TYPE_BOOLEAN)
    {
    }
    ~BooleanType() override = default;
    friend class CHIRContext;
};

class UnitType : public BuiltinType {
private:
    explicit UnitType() : BuiltinType(TypeKind::TYPE_UNIT)
    {
    }
    ~UnitType() override = default;
    friend class CHIRContext;
};

class NothingType : public BuiltinType {
private:
    explicit NothingType() : BuiltinType(TypeKind::TYPE_NOTHING)
    {
    }
    ~NothingType() override = default;
    friend class CHIRContext;
};

class CStringType : public BuiltinType {
private:
    explicit CStringType() : BuiltinType(TypeKind::TYPE_CSTRING)
    {
    }
    ~CStringType() override = default;
    friend class CHIRContext;
};

struct CPointerType : BuiltinType {
public:
    std::string ToString() const override;

    Type* GetElementType() const
    {
        return argTys[0];
    }

    const std::vector<ExtendDef*>& GetExtends(CHIRBuilder* builder = nullptr) const override;

private:
    explicit CPointerType(Type* elemTy) : BuiltinType(TypeKind::TYPE_CPOINTER)
    {
        this->argTys.emplace_back(elemTy);
    }
    friend class CHIRContext;
};

class GenericType : public Type {
public:
    const std::vector<Type*>& GetUpperBounds() const
    {
        return upperBounds;
    }

    void SetUpperBounds(const std::vector<Type*>& args);

    std::string GetSrcCodeIdentifier() const
    {
        return srcCodeIdentifier;
    }

    /**
     * @brief Retrieves the instantiated member type by a given path.
     *
     * @param path The path to the member type.
     * @param builder The CHIR builder used for building the type.
     * @return The instantiated member type.
     */
    Type* GetInstMemberTypeByPath(const std::vector<uint64_t>& path, CHIRBuilder& builder) const
    {
        return GetInstMemberTypeByPathCheckingReadOnly(path, builder).first;
    }
    
    /**
     * @brief Retrieves the instantiated member type by a given path, checking for read-only.
     *
     * @param path The path to the member type.
     * @param builder The CHIR builder used for building the type.
     * @return A pair containing the type and a boolean flag indicating read-only status.
     */
    std::pair<Type*, bool> GetInstMemberTypeByPathCheckingReadOnly(
        const std::vector<uint64_t>& path, CHIRBuilder& builder) const;
    
    /**
     * @brief Retrieves the instance map for generic types.
     *
     * @param instMap The map to store the instance types.
     * @param builder The CHIR builder used for building the types.
     */
    void GetInstMap(std::unordered_map<const GenericType*, Type*>& instMap, CHIRBuilder& builder) const;
    
    /**
     * @brief Checks if the generic constraints are satisfied for a given type.
     *
     * @param type The type to check.
     * @param builder The CHIR builder used for building the type.
     * @return True if the generic constraints are satisfied, false otherwise.
     */
    bool SatisfyGenericConstraints(Type& type, CHIRBuilder& builder,
        std::set<std::pair<const Type*, const Type*>>* visited = nullptr) const;

    size_t Hash() const override;
    bool operator==(const Type& other) const override;
    std::string ToString() const override;
    std::string GetIdentifier() const
    {
        return identifier;
    }

    bool orphanFlag = false;
    // mark unimplemented free T
    bool skipCheck = false;

private:
    explicit GenericType(const std::string& identifier, const std::string& srcName)
        : Type(TypeKind::TYPE_GENERIC), identifier(identifier), srcCodeIdentifier(srcName)
    {
    }
    ~GenericType() override = default;
    friend class CHIRContext;

    // Note, this identifier is different from the identifier in CJ source code,
    // cause it should be unique to help distinguish different generics in CHIR
    std::string identifier;
    std::string srcCodeIdentifier;
    std::vector<Type*> upperBounds;
};

class VoidType : public BuiltinType {
private:
    explicit VoidType() : BuiltinType(TypeKind::TYPE_VOID)
    {
    }
    ~VoidType() override = default;
    friend class CHIRContext;
};

const static std::unordered_map<Type::TypeKind, std::string> TYPEKIND_TO_STRING{
    {Type::TypeKind::TYPE_INVALID, "Invalid"}, {Type::TypeKind::TYPE_INT8, "Int8"},
    {Type::TypeKind::TYPE_INT16, "Int16"}, {Type::TypeKind::TYPE_INT32, "Int32"}, {Type::TypeKind::TYPE_INT64, "Int64"},
    {Type::TypeKind::TYPE_INT_NATIVE, "IntNative"}, {Type::TypeKind::TYPE_UINT8, "UInt8"},
    {Type::TypeKind::TYPE_UINT16, "UInt16"}, {Type::TypeKind::TYPE_UINT32, "UInt32"},
    {Type::TypeKind::TYPE_UINT64, "UInt64"}, {Type::TypeKind::TYPE_UINT_NATIVE, "UIntNative"},
    {Type::TypeKind::TYPE_FLOAT16, "Float16"}, {Type::TypeKind::TYPE_FLOAT32, "Float32"},
    {Type::TypeKind::TYPE_FLOAT64, "Float64"}, {Type::TypeKind::TYPE_RUNE, "Rune"},
    {Type::TypeKind::TYPE_BOOLEAN, "Bool"}, {Type::TypeKind::TYPE_UNIT, "Unit"},
    {Type::TypeKind::TYPE_NOTHING, "Nothing"}, {Type::TypeKind::TYPE_TUPLE, "Tuple"},
    {Type::TypeKind::TYPE_BOXTYPE, "BoxType"}, {Type::TypeKind::TYPE_STRUCT, "Struct"},
    {Type::TypeKind::TYPE_ENUM, "Enum"}, {Type::TypeKind::TYPE_CLASS, "Class"}, {Type::TypeKind::TYPE_FUNC, "Func"},
    {Type::TypeKind::TYPE_RAWARRAY, "RawArray"}, {Type::TypeKind::TYPE_VARRAY, "VArray"},
    {Type::TypeKind::TYPE_CPOINTER, "CPointer"}, {Type::TypeKind::TYPE_CSTRING, "CString"},
    {Type::TypeKind::TYPE_GENERIC, "GenericType"}, {Type::TypeKind::TYPE_VOID, "Void"},
    {Type::TypeKind::TYPE_REFTYPE, "RefType"}};

Type* GetFieldOfType(Type& baseTy, uint64_t index, CHIRBuilder& builder);
} // namespace Cangjie::CHIR
#endif // CANGJIE_CHIR_TYPE_H
