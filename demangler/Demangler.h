// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.


#ifndef CANGJIE_DEMANGLER_H
#define CANGJIE_DEMANGLER_H

#include <array>
#ifndef BUILD_LIB_CANGJIE_DEMANGLE
#include <vector>
#include "Base/CString.h"
#endif
#include <functional>
#include <cassert>
#include <cstdint>

namespace Cangjie {

enum class TypeKind {
    NAME,
    PRIMITIVE,
    CSTRING,
    CLASS,
    STRUCT,
    INTERFACE,
    ENUM,
    RAW_ARRAY,
    VARRAY,
    TUPLE,
    CPOINTER,
    FUNCTION,
    COMMON_DECL,
    FUNCTION_DECL,
    FUNCTION_PARAMETER_TYPES,
    LAMBDA_FUNCTION,
    GENERIC_TYPES,
    GENERIC_CONSTRAINTS,
    WRAPPED_FUNCTION
};

template<typename T>
struct DemangleInfo {
    /**
     * @brief The constructor of struct DemangleInfo.
     *
     * @param name The name to be demangled.
     * @param kind The demangled kind.
     * @param pIsValid Whether The demangled name is valid.
     * @return DemangleInfo The instance of DemangleInfo.
     */
    DemangleInfo(const T& name, TypeKind kind, bool pIsValid) : type(kind), demangled(name), isValid(pIsValid) {}

    /**
     * @brief The constructor of struct DemangleInfo.
     *
     * @param name The name to be demangled.
     * @param kind The demangled kind.
     * @return DemangleInfo the instance of DemangleInfo.
     */
    DemangleInfo(const T& name, TypeKind kind) : type(kind), demangled(name) {}

    /**
     * @brief Use the default copy constructor to initialize a new DemangleInfo object.
     *
     * @param copying The DemangleInfo object to be copied.
     * @return DemangleInfo The instance of DemangleInfo.
     */
    DemangleInfo(const DemangleInfo& copying) = default;

    /**
     * @brief The default constructor of struct DemangleInfo.
     *
     * @return DemangleInfo The instance of DemangleInfo.
     */
    DemangleInfo() = default;
    DemangleInfo& operator=(const DemangleInfo& copying) = default;

    /**
     * @brief The destructor of struct DemangleInfo.
     */
    virtual ~DemangleInfo() = default;

    TypeKind type;
    T pkgName;
    T args;
    T genericConstraints;
    T genericTypes;
    T functionParameterTypes;
    T demangled; // As identifier or function return type
    bool isPrivateDeclaration { false };

    /**
     * @brief Get full name.
     *
     * @param scopeRes The scope resolution.
     * @param argsNum The args number.
     * @return T The full name.
     */
    virtual T GetFullName(const T& scopeRes, const uint32_t argsNum = 0) const;

    /**
     * @brief Get package name.
     *
     * @return T The package name.
     */
    virtual T GetPkgName() const;

    /**
     * @brief Get arg types name.
     *
     * @param argsNum The args number.
     * @return T The arg types name.
     */
    virtual T GetArgTypesName(const uint32_t argsNum = 0) const;

    /**
     * @brief Get demangled identifier.
     *
     * @return T The identifier.
     */
    T GetIdentifier() const;

    /**
     * @brief Get return type.
     *
     * @return T The return type name.
     */
    T GetReturnType() const;

    /**
     * @brief Check if it functions like a function.
     *
     * @return bool If yes, true is returned. Otherwise, false is returned.
     */
    bool IsFunctionLike() const;

    /**
     * @brief Check if the demangled name is valid.
     *
     * @return bool If yes, true is returned. Otherwise, false is returned.
     */
    bool IsValid() const;

private:
    bool MatchSuffix(const char pattern[], uint32_t len) const;
    T GetGenericConstraints() const;
    T GetGenericTypes() const;
    T GetFunctionName() const;
    T GetGenericConstraintsName() const;
    bool isValid{ true };
};

template<typename T>
class Demangler {
public:
    /**
     * @brief The constructor of class Demangler.
     *
     * @param mangled The name to be demangled.
     * @param stripUnderscore Whether the mangled name needs to strip underscore.
     * @param scopeRes The scope resolution.
     * @param genericParamFilter The function to filter generic param.
     * @return Demangler The instance of Demangler.
     */
    explicit Demangler(
        const T& mangled,
        const bool stripUnderscore,
        const T& scopeRes, std::function<T(const T&)> genericParamFilter)
        : currentIndex(0), genericTypefilter(genericParamFilter), scopeResolution(scopeRes)
    {
        mangledName = mangled;
        // Skip '_'
        if (stripUnderscore && !mangled.IsEmpty() && mangled[0] == '_') {
            mangledName = mangledName.SubStr(1);
        }
        dumpDi = DemangleInfo<T>{ mangledName, TypeKind::NAME, false };
    }

    /**
     * @brief The constructor of class Demangler.
     *
     * @param mangled The name to be demangled.
     * @param scopeRes The scope resolution.
     * @param genericParamFilter The function to filter generic param.
     * @return Demangler The instance of Demangler.
     */
    explicit Demangler(
        const T& mangled,
        const T& scopeRes = "::", std::function<T(const T&)> genericParamFilter = [](const T& str) { return str; })
        : Demangler(mangled, false, scopeRes, genericParamFilter)
    {
    }

    /**
     * @brief Demangle the string.
     *
     * @param isType Whether the demangled name is type.
     * @return DemangleInfo<T> The demangled information.
     */
    DemangleInfo<T> Demangle(bool isType = false);

    /**
     * @brief Get demangled class type name.
     *
     * @return T The class type name.
     */
    T DemangleClassType();

    /**
     * @brief Get demangled qualified name.
     *
     * @return T The qualified name.
     */
    T DemangleQualifiedName();

    /**
     * @brief Get scope resolution.
     *
     * @return T The scope resolution.
     */
    T ScopeResolution() const { return scopeResolution; };
#ifdef BUILD_LIB_CANGJIE_DEMANGLE
    /**
     * @brief Set generic vector.
     *
     * @param vec The generic vector.
     */
    void setGenericVec(const std::vector<std::string>& vec) { genericVec = vec; }
#endif

private:
    DemangleInfo<T> DemanglePackageName();
    T DemangleStringName();
    T DemangleProp();
    size_t DemangleManglingNumber();
    void DemangleFileNameNumber();
    T UIntToString(size_t value) const;
    void SkipPrivateTopLevelDeclHash();
    void DemangleFileName();
    void SkipLocalCounter();
    T DemangleArgTypes(const T& delimiter, uint32_t size = -1);
    DemangleInfo<T> DemangleNestedDecls(bool isClass = false, bool isParamInit = false);
    uint32_t DemangleLength();
    DemangleInfo<T> Reject(const T& reason = T{});
    DemangleInfo<T> DemangleNextUnit(const T& message = T{});
    DemangleInfo<T> DemangleByPrefix();
    DemangleInfo<T> DemangleClass(TypeKind typeKind);
    DemangleInfo<T> DemangleCFuncType();
    DemangleInfo<T> DemangleTuple();
    DemangleInfo<T> DemangleCommonDecl(bool isClass = false);
    DemangleInfo<T> DemangleDecl();
    DemangleInfo<T> DemangleRawArray();
    DemangleInfo<T> DemangleVArray();
    DemangleInfo<T> DemangleCPointer();
    DemangleInfo<T> DemangleFunction();
    DemangleInfo<T> DemanglePrimitive();
    DemangleInfo<T> DemangleCStringType();
    DemangleInfo<T> DemangleGenericType();
    DemangleInfo<T> DemangleDefaultParamFunction();
    DemangleInfo<T> DemangleInnerFunction();
    DemangleInfo<T> DemangleType();
    DemangleInfo<T> DemangleGenericTypes();
    DemangleInfo<T> DemangleFunctionParameterTypes();
    DemangleInfo<T> DemangleGlobalInit();
    DemangleInfo<T> DemangleParamInit();
    DemangleInfo<T> DemangleWrappedFunction();

    bool IsFileName() const;
    bool IsProp() const;
    bool IsInnerFunction() const;
    bool IsMaybeDefaultFuncParamFunction() const;
    bool IsOperatorName() const;
    bool IsGlobalInit() const;
    bool IsParamInit() const;
    bool IsCFunctionWrapper() const;
    bool IsWrappedFunction() const;
    bool IsQualifiedType() const;
    bool IsDecl() const;
    bool IsNotEndOfMangledName() const;
    bool MatchForward(const char pattern[], uint32_t len) const;

    /**
     * @brief Get the current character without incrementing `currentIndex`.
     *
     * @param ch The current character, only set when the function returns true.
     * @return false if `currentIndex` is out of bound, otherwise true.
    */
    bool PeekChar(char& ch) const;

    /**
     * @brief Get the current character and increment `currentIndex` by one.
     *
     * @param ch The current character, only set when the function returns true.
     * @return false if `currentIndex` is out of bound, otherwise true.
    */
    bool GetChar(char& ch);
    void SkipChar(char ch);

    void SkipOptionalChar(char ch);

    void SkipString(const char pattern[]);
    void ErrorLog(const char* msg) const;

    // Record false and reject parsing if the input mangled name is invalid
    bool isValid{ true };
    // Record which kind is being parsed
    TypeKind curType{ TypeKind::NAME };
    size_t currentIndex;
    // Filter specified content in generic parameters
    std::function<T(const T&)> genericTypefilter;
    T mangledName;
    T scopeResolution;
    DemangleInfo<T> dumpDi;
#ifdef BUILD_LIB_CANGJIE_DEMANGLE
    // Generic name vector. e.g. {"T", "K", "U"}
    std::vector<std::string> genericVec;
#endif
};
} // namespace Cangjie
#endif // CANGJIE_DEMANGLER_H
