// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.


#ifdef BUILD_LIB_CANGJIE_DEMANGLE // to reuse the code to compile CangjieDemangle.cpp
#include <cstring>
#include <string>

#include "StdString.h"
#else
#include "Base/CString.h"
#endif
#include "DeCompression.h"
#include "Utils.h"
#include "Demangler.h"

namespace {
constexpr char MANGLE_TUPLE_PREFIX = 'T';
constexpr char MANGLE_ARRAY_PREFIX = 'A';
constexpr char MANGLE_VARRAY_PREFIX = 'V';
constexpr char MANGLE_PTR_PREFIX = 'P';
constexpr char MANGLE_CSTRING_PREFIX = 'k';
constexpr char MANGLE_VOID_PREFIX = 'v';
constexpr char MANGLE_GTY_PREFIX = 'G';
constexpr char MANGLE_DEFAULT_PARAM_FUNCTION_PREFIX = '$';
const char ARGS_DELIMITER[] = ", ";
const char ARGS_DELIMITER_TYPE[] = ",";
const char LEFT_BRACKET[] = "(";
const char RIGHT_BRACKET[] = ")";
const char MODULE_SPLIT[] = "/";
const char MANGLE_MODULE_SPLIT[] = "$";
const char MANGLE_FUNCTY_PREFIX[] = "F0";
const char MANGLE_C_FUNCTY_PREFIX[] = "FC";
const char MANGLE_FLOAT16_PREFIX[] = "Dh";
const char MANGLE_SET_PREFIX[] = "ps";
const char MANGLE_GET_PREFIX[] = "pg";
const char GLOBAL_VARIABLE_INIT_PREFIX[] = "GV";
const char MANGLE_GLOBAL_PACKAGE_INIT_PREFIX[] = "_CGP";
const char MANGLE_GLOBAL_FILE_INIT_PREFIX[] = "_CGF";
const char MANGLE_FUNC_PARA_INIT_PREFIX[] = "_CPI";
const char MANGLE_GLOBAL_INIT_SUFFIX[] = "iiHv";
const char MANGLE_GLOBAL_INIT_RESET_SUFFIX[] = "irHv";
const char MANGLE_GLOBAL_INIT_FLAG_RESET_SUFFIX[] = "ifHv";
const char MANGLE_GLOBAL_INIT_LITERAL_RESET_SUFFIX[] = "ilHv";
const char MANGLE_INNER_FUNCTION_PREFIX[] = "$lambda.";
const char MANGLE_CFUNCTION_WRAPPER[] = "$real";
const char BOX_DECL_PREFIX[] = "$BOX_";
const char CJ_FILE_EXT[] = ".cj";
const char ERROR_EXCEED_THE_INPUT_STRING_LENGTH[] = "exceed the input string length";
const char MANGLE_TUPLE_STR[] = "Tuple";
const char MANGLE_ARRAY_STR[] = "RawArray";
const char MANGLE_VARRAY_STR[] = "VArray";
const char MANGLE_PTR_STR[] = "CPointer";
const char MANGLE_CSTRING_STR[] = "CString";
const char BASE62_CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
const char DECIMAL_CHARS[] = "0123456789";

constexpr size_t BASE62_CHARS_SIZE = 62;
constexpr uint32_t MAX_ARGS_SIZE = 16;
} // namespace

namespace Cangjie {
template<typename T>
T GetPrimitiveDemangleMap(char t)
{
    switch (t) {
        case 'u':
            return "Unit";
        case 'n':
            return "Nothing";
        case 'b':
            return "Bool";
        case 'D':
            return "Float16";
        case 'f':
            return "Float32";
        case 'd':
            return "Float64";
        case 'c':
            return "Rune";
        case 'a':
            return "Int8";
        case 's':
            return "Int16";
        case 'i':
            return "Int32";
        case 'l':
            return "Int64";
        case 'h':
            return "UInt8";
        case 't':
            return "UInt16";
        case 'j':
            return "UInt32";
        case 'm':
            return "UInt64";
        case 'q':
            return "IntNative";
        case 'r':
            return "UIntNative";
        default: {
            return "";
        }
    }
}

/**
 * @brief Get operator name map.
 *
 * @return T return the <name> if the pattern is operator name.
 */
template<typename T>
T GetOperatorNameMap(T replaced)
{
    const size_t ch = 97;
    // The demangled operator name rule is [a-z][a-z]
    if (!((replaced[0] >= 'a' && replaced[0] <= 'z') && (replaced[1] >= 'a' && replaced[1] <= 'z'))) {
        return "";
    }
    // Move left by 5 digits is the way of calculating OperatorKind
    size_t t = ((static_cast<size_t>(replaced[0]) - ch) << 5) + static_cast<size_t>(replaced[1]) - ch;
    OperatorKind result = static_cast<OperatorKind>(t);
    switch (result) {
        case OperatorKind::IX:
            return "[]";
        case OperatorKind::NT:
            return "!";
        case OperatorKind::NG:
            return "-";
        case OperatorKind::PW:
            return "**";
        case OperatorKind::ML:
            return "*";
        case OperatorKind::DV:
            return "/";
        case OperatorKind::RM:
            return "%";
        case OperatorKind::PL:
            return "+";
        case OperatorKind::LS:
            return "<<";
        case OperatorKind::RS:
            return ">>";
        case OperatorKind::LT:
            return "<";
        case OperatorKind::GT:
            return ">";
        case OperatorKind::LE:
            return "<=";
        case OperatorKind::GE:
            return ">=";
        case OperatorKind::EQ:
            return "==";
        case OperatorKind::NE:
            return "!=";
        case OperatorKind::AN:
            return "&";
        case OperatorKind::EO:
            return "^";
        case OperatorKind::OR:
            return "|";
        case OperatorKind::CL:
            return "()";
        default: {
            return "";
        }
    }
}

template<typename T>
T ReplaceString(T str, const char* pattern, const char* replacement)
{
    if (str.IsEmpty()) {
        return str;
    }
    auto pos = str.Find(pattern);
    auto n = str.Length();
    auto pLen = strlen(pattern);
    while (pos > -1 && n - pos - pLen > 0) {
        n = str.Length();
        str = str.SubStr(0, pos) + replacement + str.SubStr(pos + pLen, n - pos - pLen);
        pos = str.Find(pattern);
    }
    return str;
}

template<typename T>
bool IsPrimitive(char ch)
{
    return GetPrimitiveDemangleMap<T>(ch) != T{ "" };
}

inline bool IsCurrentCharDigit(char ch)
{
    return ch >= '0' && ch <= '9';
}

inline bool IsGeneric(char ch)
{
    return ch == MANGLE_GENERIC_PREFIX;
}

inline bool IsFunction(char ch)
{
    return ch == MANGLE_FUNCTION_PREFIX;
}

template<typename T>
bool Demangler<T>::IsProp() const
{
    if (currentIndex + MANGLE_CHAR_LEN < mangledName.Length()) {
        T str = mangledName.SubStr(currentIndex, SPECIAL_NAME_LEN);
        return str == T{ MANGLE_SET_PREFIX } || str == T{ MANGLE_GET_PREFIX };
    }
    return false;
}

template<typename T>
bool Demangler<T>::IsInnerFunction() const
{
    if (mangledName.Length() - MANGLE_CHAR_LEN > strlen(MANGLE_INNER_FUNCTION_PREFIX)) {
        return mangledName.Find(MANGLE_INNER_FUNCTION_PREFIX) > -1;
    }
    return false;
}

template<typename T>
bool Demangler<T>::IsOperatorName() const
{
    const size_t id = currentIndex + SPECIAL_NAME_LEN;
    if (id < mangledName.Length() &&
        GetOperatorNameMap<T>(mangledName.SubStr(currentIndex, SPECIAL_NAME_LEN)) != T{ "" } &&
        (mangledName[id] == MANGLE_GENERIC_PREFIX || mangledName[id] ==
        MANGLE_FUNCTION_PREFIX || mangledName[id] == MANGLE_COUNT_PREFIX)) {
        return true;
    }
    return false;
}

template<typename T>
bool Demangler<T>::IsMaybeDefaultFuncParamFunction() const
{
    return currentIndex < mangledName.Length() && mangledName[currentIndex] == MANGLE_DEFAULT_PARAM_FUNCTION_PREFIX;
}

template<typename T>
bool Demangler<T>::IsNotEndOfMangledName() const
{
    return currentIndex < mangledName.Length() && mangledName[currentIndex] != END;
}

template<typename T>
uint32_t Demangler<T>::DemangleLength()
{
    if (currentIndex >= mangledName.Length()) {
        (void)Reject(ERROR_EXCEED_THE_INPUT_STRING_LENGTH);
        return 0;
    }
    // The decl added by compiler has no package.
    if (mangledName[currentIndex] == '0') {
        ++currentIndex;
        return 0;
    }

    T numStr;
    isValid = false;
    while (currentIndex < mangledName.Length() && IsCurrentCharDigit(mangledName[currentIndex])) {
        numStr += T{ mangledName[currentIndex] };
        ++currentIndex;
        isValid = true;
    }
    return atoi(numStr.Str());
}

template<typename T>
T Demangler<T>::UIntToString(size_t value) const
{
    T result = T{""};
    if (value == 0) {
        return T{"0"};
    }
    while (value != 0) {
        char current = DECIMAL_CHARS[value % 10]; // 10 based calculation.
        value /= 10; // 10 based calculation.
        result = T{current} + result;
    }
    return result;
}

template<typename T>
bool Demangler<T>::IsGlobalInit() const
{
    auto isPkgInit = MatchForward(MANGLE_GLOBAL_PACKAGE_INIT_PREFIX, strlen(MANGLE_GLOBAL_PACKAGE_INIT_PREFIX));
    auto isFileInit = MatchForward(MANGLE_GLOBAL_FILE_INIT_PREFIX, strlen(MANGLE_GLOBAL_FILE_INIT_PREFIX));
    return isPkgInit || isFileInit;
}

template<typename T>
bool Demangler<T>::IsParamInit() const
{
    return MatchForward(MANGLE_FUNC_PARA_INIT_PREFIX, strlen(MANGLE_FUNC_PARA_INIT_PREFIX));
}

template<typename T>
bool Demangler<T>::IsCFunctionWrapper() const
{
    if (mangledName.Length() > strlen(MANGLE_CFUNCTION_WRAPPER)) {
        return mangledName.Find(MANGLE_CFUNCTION_WRAPPER, 0) > -1;
    }
    return false;
}

template<typename T>
bool Demangler<T>::IsQualifiedType() const
{
    if (currentIndex >= mangledName.Length()) {
        return false;
    }
    if (currentIndex + MANGLE_CHAR_LEN < mangledName.Length()) {
        // Dh, F0 and FC belong to the legal qualified type prefix.
        if ((mangledName[currentIndex] == 'D' && mangledName[currentIndex + MANGLE_CHAR_LEN] == 'h') ||
            (mangledName[currentIndex] == 'F' && mangledName[currentIndex + MANGLE_CHAR_LEN] == '0') ||
            (mangledName[currentIndex] == 'F' && mangledName[currentIndex + MANGLE_CHAR_LEN] == 'C')) {
            return true;
        }
    }
    if (IsPrimitive<T>(mangledName[currentIndex])) {
        return true;
    }
    // The {'C', 'R', 'N', 'T', 'P', 'A', 'k', 'V'} belong to the legal qualified type prefix.
    for (auto prefix : { 'C', 'R', 'N', 'T', 'P', 'A', 'k', 'V' }) {
        if (mangledName[currentIndex] == prefix) {
            return true;
        }
    }
    return false;
}

template<typename T>
bool Demangler<T>::IsDecl() const
{
    return MatchForward(MANGLE_NESTED_PREFIX, strlen(MANGLE_NESTED_PREFIX));
}

template<typename T>
void Demangler<T>::ErrorLog(const char* msg) const
{
#if defined(MRT_DEBUG) && (MRT_DEBUG == 1) && !defined(BUILD_LIB_CANGJIE_DEMANGLE)
    PRINT_ERROR("'%s' is not a valid mangling name.\n ", mangledName.Str());
    // Include the first single quote
    for (auto i = 0u; i < currentIndex; ++i) {
        PRINT_ERROR(" ");
    }
    PRINT_ERROR("^ %s\n", msg);
#endif
}

template<typename T>
bool Demangler<T>::MatchForward(const char pattern[], uint32_t len) const
{
    auto maxLen = mangledName.Length() - currentIndex;
    len = len > maxLen ? maxLen : len;
    auto prefix = mangledName.SubStr(currentIndex, len);
    return prefix == T{ pattern };
}

template<typename T>
bool Demangler<T>::PeekChar(char& ch) const
{
    if (currentIndex >= mangledName.Length()) {
        return false;
    }
    ch = mangledName[currentIndex];
    return true;
}

template<typename T>
bool Demangler<T>::GetChar(char& ch)
{
    bool success = PeekChar(ch);
    if (success) {
        ++currentIndex;
    }
    return success;
}

template<typename T>
void Demangler<T>::SkipChar(char ch)
{
    if (currentIndex >= mangledName.Length()) {
        (void)Reject(ERROR_EXCEED_THE_INPUT_STRING_LENGTH);
        return;
    }

    if (mangledName[currentIndex] != ch) {
        (void)Reject(T{ "should be the character: " } + T{ ch });
        return;
    }
    ++currentIndex;
    return;
}

template<typename T>
void Demangler<T>::SkipOptionalChar(char ch)
{
    if (currentIndex >= mangledName.Length()) {
        return;
    }

    if (mangledName[currentIndex] == ch) {
        ++currentIndex;
    }
    return;
}

template<typename T>
void Demangler<T>::SkipString(const char pattern[])
{
    auto len = strlen(pattern);
    for (size_t i = 0; i < len; ++i) {
        SkipChar(pattern[i]);
        if (!isValid) {
            return;
        }
    }
    return;
}

template<typename T>
bool DemangleInfo<T>::MatchSuffix(const char pattern[], uint32_t len) const
{
    auto index = demangled.Length() > len ? demangled.Length() - len : 0;
    auto suffix = demangled.SubStr(index, demangled.Length() - index);
    return suffix == T{ pattern };
}

template<typename T>
T DemangleInfo<T>::GetFullName(const T& scopeRes, const uint32_t argsNum) const
{
    T fullDemangledName;
    switch (this->type) {
        case TypeKind::FUNCTION:
            return GetFunctionName();
        case TypeKind::GENERIC_CONSTRAINTS:
            return GetGenericConstraintsName();
        case TypeKind::ENUM:
        case TypeKind::CLASS:
        case TypeKind::STRUCT:
        case TypeKind::INTERFACE:
        case TypeKind::FUNCTION_DECL:
        default:
            auto pkg = GetPkgName();
            auto identifier = GetIdentifier();
            if (!IsFunctionLike() && type != TypeKind::COMMON_DECL) {
                if (!pkg.IsEmpty() && !identifier.IsEmpty()) {
                    fullDemangledName += pkg;
                    fullDemangledName += T{ scopeRes };
                    fullDemangledName += identifier;
                } else {
                    fullDemangledName += pkg;
                    fullDemangledName += identifier;
                }
            } else {
                fullDemangledName += identifier;
            }
            fullDemangledName += GetGenericTypes();
            fullDemangledName += GetArgTypesName(argsNum);
            fullDemangledName += GetGenericConstraints();
    }
    return fullDemangledName;
}

template<typename T>
T DemangleInfo<T>::GetPkgName() const
{
    return pkgName;
}

template<typename T>
T DemangleInfo<T>::GetReturnType() const
{
    return demangled;
}

template<typename T>
T DemangleInfo<T>::GetGenericConstraints() const
{
    return genericConstraints;
}

template<typename T>
T DemangleInfo<T>::GetGenericTypes() const
{
    return genericTypes;
}

template<typename T>
T DemangleInfo<T>::GetGenericConstraintsName() const
{
    auto separatorFromDecl = T{ ", " };
    auto gcName = separatorFromDecl + demangled;
    gcName += " <: ";
    gcName += ReplaceString(args, ",", " &");
    return gcName;
}

template<typename T>
T GetArgTypesFullName(const std::array<T, MAX_ARGS_SIZE>& args, uint32_t argSize, const T& delimiter)
{
    if (argSize == 0) {
        return T{};
    }
    auto argTypesFullName = args[0];
    for (size_t i = 1; i < argSize; ++i) {
        argTypesFullName += delimiter + args[i];
    }
    return argTypesFullName;
}

template<typename T>
T DemangleInfo<T>::GetArgTypesName(const uint32_t argsNum) const
{
    if (args.IsEmpty()) {
        return IsFunctionLike() ? "()" : T{};
    }
    if (IsFunctionLike()) {
        return T{ "(" } + args + ")"; // Like (arg-type1, arg-type2, ...)
    }
    if (argsNum != 0) {
        T numStr;
#ifdef BUILD_LIB_CANGJIE_DEMANGLE
        numStr = std::to_string(argsNum);
#else
        numStr = MapleRuntime::CString(static_cast<uint64_t>(argsNum));
#endif
        return T{ "<" } + args + ",$" + numStr + ">";
    }
    // The generic-types like <arg-type1, arg-type2, ...>
    return T{ "<" } + args + ">";
}

template<typename T>
bool DemangleInfo<T>::IsFunctionLike() const
{
    return type == TypeKind::FUNCTION_DECL || type == TypeKind::LAMBDA_FUNCTION || type == TypeKind::FUNCTION;
}

template<typename T>
bool DemangleInfo<T>::IsValid() const
{
    return isValid;
}

template<typename T>
T DemangleInfo<T>::GetFunctionName() const
{
    return GetArgTypesName() + " -> " + GetReturnType();
}

template<typename T>
T DemangleInfo<T>::GetIdentifier() const
{
    if (!demangled.IsEmpty() && demangled[0] == '<' && demangled.EndsWith(">")) {
        // When identifier is bracketed by '<>', its original identifier is string inside.
        return demangled.SubStr(1, demangled.Length() - strlen("<>"));
    }
    return demangled;
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleGlobalInit()
{
    T prefix;
    if (MatchForward(MANGLE_GLOBAL_PACKAGE_INIT_PREFIX, strlen(MANGLE_GLOBAL_PACKAGE_INIT_PREFIX))) {
        SkipString(MANGLE_GLOBAL_PACKAGE_INIT_PREFIX);
        auto pkgDi = DemanglePackageName();
        prefix = pkgDi.pkgName + T{ "::package" };
    } else if (MatchForward(MANGLE_GLOBAL_FILE_INIT_PREFIX, strlen(MANGLE_GLOBAL_FILE_INIT_PREFIX))) {
        SkipString(MANGLE_GLOBAL_FILE_INIT_PREFIX);
        auto pkgDi = DemanglePackageName();
        prefix = pkgDi.pkgName + T{ "::file" };
    } else {
        return Reject("invalid global init function");
    }
    if (mangledName.EndsWith(MANGLE_GLOBAL_INIT_RESET_SUFFIX)) {
        return DemangleInfo<T>{ prefix + T{ "_global_reset" }, TypeKind::NAME, isValid };
    } else if (mangledName.EndsWith(MANGLE_GLOBAL_INIT_FLAG_RESET_SUFFIX)) {
        return DemangleInfo<T>{ prefix + T{ "_global_flag_reset" }, TypeKind::NAME, isValid };
    } else if (mangledName.EndsWith(MANGLE_GLOBAL_INIT_LITERAL_RESET_SUFFIX)) {
        return DemangleInfo<T>{ prefix + T{ "_global_init_literal" }, TypeKind::NAME, isValid };
    } else if (!mangledName.EndsWith(MANGLE_GLOBAL_INIT_SUFFIX)) {
        return Reject("global init function should end with iiHv");
    }
    return DemangleInfo<T>{ prefix + T{ "_global_init" }, TypeKind::NAME, isValid };
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleParamInit()
{
    SkipString(MANGLE_FUNC_PARA_INIT_PREFIX);
    DemangleInfo<T> di{ "", TypeKind::COMMON_DECL };
    auto pkgDi = DemanglePackageName();
    di.pkgName = pkgDi.pkgName;
    di.isPrivateDeclaration = pkgDi.isPrivateDeclaration;

    // Take the part before E as identifier and concatenate with scope resolution.
    DemangleInfo<T> nestedDi = DemangleNestedDecls(false, true);
    di.demangled = nestedDi.demangled;
    di.type = nestedDi.type;
    di.args = nestedDi.args;
    if (!isValid || di.demangled.IsEmpty()) {
        return Reject();
    }
    if (di.type == TypeKind::FUNCTION_DECL) {
        di.args = nestedDi.functionParameterTypes;
    }
    return di;
}

template<typename T>
T Demangler<T>::DemangleClassType()
{
    return DemangleClass(TypeKind::CLASS).GetIdentifier();
}

// return likes "pkg.className"
template<typename T>
T Demangler<T>::DemangleQualifiedName()
{
    auto info = Demangle(true);
    auto pkgName = info.GetPkgName();
    auto clsName = info.GetIdentifier();
    if (pkgName.IsEmpty()) {
        return clsName;
    }
    return pkgName + "." + clsName;
}

template<typename T>
DemangleInfo<T> Demangler<T>::Demangle(bool isType)
{
    if (isType) {
        if (!IsQualifiedType()) {
            return Reject("invalid mangling name");
        }
        return DemangleNextUnit("invalid mangling name");
    }

    if (mangledName.IsEmpty()) {
        return Reject("invalid mangling name");
    }
    mangledName = DeCompression<T>(mangledName.SubStr(0)).CJMangledDeCompression();
    if (IsGlobalInit()) {
        return DemangleGlobalInit();
    }
    if (IsParamInit()) {
        return DemangleParamInit();
    }
    // Lambda expressions like "$pkglambda.0"
    if (IsInnerFunction()) {
        return DemangleInnerFunction();
    }
    // C func wrapper like "cfuncInCJ$real"
    if (IsCFunctionWrapper()) {
        return DemangleInfo<T>{ mangledName.SubStr(0, mangledName.Length() - strlen(MANGLE_CFUNCTION_WRAPPER)),
                                TypeKind::NAME, isValid };
    }
    return DemangleDecl();
}

/**
 * @brief Parse a string with a pattern of "<length><name>" or "S<length><name>"
 *        and <length> could be zero.
 */
template<typename T>
DemangleInfo<T> Demangler<T>::DemanglePackageName()
{
    if (currentIndex >= mangledName.Length()) {
        (void)Reject(ERROR_EXCEED_THE_INPUT_STRING_LENGTH);
        return DemangleInfo<T>{ "", TypeKind::COMMON_DECL };
    }
    T pkg{};
    if (MANGLE_STDPKG_MAP.find(mangledName.SubStr(currentIndex, SPECIAL_NAME_LEN).Str()) != MANGLE_STDPKG_MAP.end()) {
        pkg = MANGLE_STDPKG_MAP.at(mangledName.SubStr(currentIndex, SPECIAL_NAME_LEN).Str());
        currentIndex += SPECIAL_NAME_LEN;
    } else if (!IsCurrentCharDigit(mangledName[currentIndex])) {
        (void)Reject("should be a number");
        return DemangleInfo<T>{ "", TypeKind::COMMON_DECL };
    }
    // Take the first string as package name, including '0'.
    if (mangledName[currentIndex] == '0') {
        ++currentIndex;
        return DemangleInfo<T>{ "", TypeKind::COMMON_DECL };
    }
    DemangleInfo<T> di{ "", TypeKind::COMMON_DECL };
    if (pkg.IsEmpty()) {
        pkg = DemangleStringName();
        auto pos = pkg.Find(':');
        if (pos > -1 && pkg.Length() - pos > 0) {
            pkg = pkg.SubStr(0, pos) + T{':'} + pkg.SubStr(pos, pkg.Length() - pos);
        }
    }
    if (IsFileName()) {
        di.isPrivateDeclaration = true;
        DemangleFileName();
    }
    di.pkgName = ReplaceString(pkg, MANGLE_MODULE_SPLIT, MODULE_SPLIT);
    return di;
}

/**
 * @brief Parse a string with a pattern of "<length><name>", and <length> must not be zero.
 *
 * @return T return the <name> if the pattern is "<length><name>"
 */
template<typename T>
T Demangler<T>::DemangleStringName()
{
    auto identifyLength = DemangleLength();
    if (identifyLength == 0) {
        (void)Reject("previous number should be non-zero");
        return T{};
    }
    if (identifyLength > mangledName.Length() - currentIndex) {
        (void)Reject("previous number is greater than the string size");
        return T{};
    }
    // length + string, eg. "3foo"
    T name = mangledName.SubStr(currentIndex, identifyLength);
    currentIndex += identifyLength;
    return name;
}

/**
 * @brief Parse a string "ps/pg".
 *
 * @return T return the "set/get" if it is "ps/pg"
 */
template<typename T>
T Demangler<T>::DemangleProp()
{
    T prop;
    if (mangledName.SubStr(currentIndex, SPECIAL_NAME_LEN) == T{ MANGLE_SET_PREFIX }) {
        prop = T{ "::set" };
    } else {
        prop = T{ "::get" };
    }
    currentIndex += SPECIAL_NAME_LEN;
    return prop;
}

template<typename T>
void Demangler<T>::SkipPrivateTopLevelDeclHash()
{
    if (currentIndex >= mangledName.Length() || !IsCurrentCharDigit(mangledName[currentIndex]) ||
        mangledName[currentIndex] == '0') {
        return;
    }
    size_t oldIdx = currentIndex;
    uint32_t hashLen = DemangleLength();
    if (currentIndex + hashLen >= mangledName.Length() || mangledName[currentIndex] != MANGLE_FILE_NUMBER_END) {
        currentIndex = oldIdx;
        return;
    }
    const bool isBoxedDecl = static_cast<size_t>(mangledName.Find(BOX_DECL_PREFIX, currentIndex)) == currentIndex &&
        !mangledName.SubStr(currentIndex, hashLen).EndsWith(CJ_FILE_EXT);
    if (isBoxedDecl) {
        currentIndex = oldIdx;
        return;
    }
    currentIndex += hashLen;
}

template<typename T>
size_t Demangler<T>::DemangleManglingNumber()
{
    // Get the entire mangling number string. We must have the length of the number (i.e. how many digits it has) to
    // compute its value. Mangling number must end with an underscore.
    T content = "";
    while (true) {
        char c;
        if (!GetChar(c)) {
            (void)Reject("mangling number has no termination symbol");
            return 0;
        }
        // Base 62 number rules: {<0-9A-Za-z>}* _, and _ is the end of the rule.
        if (c == MANGLE_UNDERSCORE_PREFIX) {
            break;
        } else {
            content += T{c};
        }
    }

    // _ represents 0. Otherwise, mangling number represents Hex + 1. For example, 0_ represents 1, A_ represents 11.
    if (content == "") {
        return 0;
    }

    // Iterate from the least significant digit to the most significant digit.
    size_t value = 0;
    size_t power = 1;
    for (size_t index = content.Length() - 1;; index--) {
        bool matched = false;
        for (size_t i = 0; i <= BASE62_CHARS_SIZE; i++) {
            if (content[index] == BASE62_CHARS[i]) {
                value += i * power;
                power *= BASE62_CHARS_SIZE;
                matched = true;
                break;
            }
        }
        if (!matched) {
            (void)Reject("mangling number has invalid character");
            return 0;
        }
        if (index == 0) {
            break;
        }
    }
    return value + 1;
}

template<typename T>
void Demangler<T>::DemangleFileNameNumber()
{
    T content = "";
    while (true) {
        char c;
        if (content.Length() == FILE_HASH_LEN) {
            break;
        } else if (!GetChar(c)) {
            (void)Reject("mangling number has no termination symbol");
            return;
        }
        if (c == MANGLE_FILE_NUMBER_END) {
            return;
        } else {
            content += T{ c };
        }
    }

    for (size_t index = content.Length() - 1;; index--) {
        bool matched = false;
        for (size_t i = 0; i <= BASE62_CHARS_SIZE; i++) {
            if (content[index] == BASE62_CHARS[i]) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            (void)Reject("mangling number has invalid character");
            return;
        }
        if (index == 0) {
            break;
        }
    }
}

template<typename T>
bool Demangler<T>::IsFileName() const
{
    char c;
    if (!PeekChar(c)) {
        return false;
    }
    if (c != MANGLE_FILE_ID_PREFIX) {
        return false;
    }
    return true;
}

template<typename T>
void Demangler<T>::DemangleFileName()
{
    SkipChar(MANGLE_FILE_ID_PREFIX);
    DemangleFileNameNumber();
}

template<typename T>
void Demangler<T>::SkipLocalCounter()
{
    char c;
    if (!PeekChar(c)) {
        return;
    }
    if (c != MANGLE_COUNT_PREFIX) {
        return;
    }
    SkipChar(MANGLE_COUNT_PREFIX);
    (void)DemangleManglingNumber();
}

/**
 * @brief Parse the following arguments.
 *
 * @tparam T
 * @param delimiter to concatenate all args
 * @param size the number of args to be parsed, default -1(all)
 * @return return empty string if no args get parsed or input is invalid
 */
template<typename T>
T Demangler<T>::DemangleArgTypes(const T& delimiter, uint32_t size)
{
    if (currentIndex == mangledName.Length() - 1 && mangledName[currentIndex] == MANGLE_VOID_PREFIX) {
        SkipChar(MANGLE_VOID_PREFIX);
        return T{};
    }
    auto i = 0u;
    T result;
    bool hasTrailingDelimiter = false;
    // In order to not use dynamic memory, the array is used in this function.
    // It is supposed that result is long enough.
    std::array<T, MAX_ARGS_SIZE> argsArr;
    while (IsNotEndOfMangledName() && i < size) {
        // Generic constraints are separated with '_'
        if (mangledName[currentIndex] == MANGLE_UNDERSCORE_PREFIX) {
            SkipChar(MANGLE_UNDERSCORE_PREFIX);
        }
        if (IsMaybeDefaultFuncParamFunction()) {
            break;
        }
        auto curDi = DemangleNextUnit("invalid argument type");
        if (!isValid) {
            return T{};
        }
        auto j = i % MAX_ARGS_SIZE;
        if (j == 0 && i != 0) {
            for (auto& arg : argsArr) {
                result += arg + delimiter;
                arg.Truncate(0);
            }
            hasTrailingDelimiter = true;
        }
        ++i;
        argsArr[j] = curDi.GetFullName(scopeResolution);
    }
    if (i == 0) {
        return result;
    }
    uint32_t remainingLength = ((i - 1) % MAX_ARGS_SIZE) + 1;
    if (remainingLength == 0 && hasTrailingDelimiter) {
        result.Truncate(result.Length() - delimiter.Length());
    }
    result += GetArgTypesFullName(argsArr, remainingLength, delimiter);
    return result;
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleNestedDecls(bool isClass, bool isParamInit)
{
    auto delimiter = T{ scopeResolution };
    auto i = 0u;
    T result;
    // In order to not use dynamic memory, the array is used in this function.
    std::array<T, MAX_ARGS_SIZE> argsArr;
    auto typeKind = TypeKind::COMMON_DECL;
    DemangleInfo<T> lastElement;
    while (IsNotEndOfMangledName()) {
        typeKind = TypeKind::COMMON_DECL;
        if (mangledName[currentIndex] == MANGLE_LAMBDA_PREFIX) {
            ++currentIndex;
            T lambdaIDString = UIntToString(DemangleManglingNumber());
            auto j = i % MAX_ARGS_SIZE;
            ++i;
            // Lambda demangled name prefix "lambda.".
            argsArr[j] = T{"lambda."} + lambdaIDString;
            typeKind = TypeKind::LAMBDA_FUNCTION;
            continue;
        }
        if (mangledName[currentIndex] == MANGLE_EXTEND_PREFIX) {
            // Something I don't really understand. Copied from the current implementation anyway.
            ++currentIndex;
            if (!IsQualifiedType()) {
                return Reject("invalid mangling name");
            }
            auto curDi = DemangleNextUnit("invalid mangling name");
            auto j = i % MAX_ARGS_SIZE;
            ++i;
            argsArr[j] = curDi.GetIdentifier();
            if (IsFileName()) {
                (void)DemangleFileName();
            }
            SkipLocalCounter();
            continue;
        }
        DemangleInfo<T> curDi;
        if (IsOperatorName()) {
            curDi = DemangleInfo<T>{ GetOperatorNameMap<T>(mangledName.SubStr(currentIndex, SPECIAL_NAME_LEN)),
                TypeKind::NAME, isValid };
            currentIndex += SPECIAL_NAME_LEN;
        } else {
            curDi = DemangleInfo<T>{ DemangleStringName(), TypeKind::NAME, isValid };
            if (IsProp()) {
                curDi.demangled += DemangleProp();
            }
        }
        if (currentIndex >= mangledName.Length()) {
            auto j = i % MAX_ARGS_SIZE;
            ++i;
            argsArr[j] = curDi.GetFullName(scopeResolution);
            break;
        }
        SkipLocalCounter();
        if (!isValid) {
            return Reject("mangle name is invalid");
        }
        // Some nested decl has generic param like "_CN1A3fooIlE3barHl"
        //                                         "_CN1A3fooIlEixHl"
        if (IsGeneric(mangledName[currentIndex])) {
            SkipChar(MANGLE_GENERIC_PREFIX);
            auto generic = DemangleGenericTypes().GetFullName(scopeResolution);
            curDi.genericTypes = genericTypefilter(generic);
            if (!isClass) {
                SkipOptionalChar(END);
            }
        }
        char ch;
        if (PeekChar(ch) && IsFunction(ch)) {
            SkipChar(MANGLE_FUNCTION_PREFIX);
            typeKind = TypeKind::FUNCTION_DECL;
            auto funcTys = DemangleFunctionParameterTypes().args;
            curDi.functionParameterTypes = funcTys;
            SkipOptionalChar(END);
        }
        auto j = i % MAX_ARGS_SIZE;
        if (j == 0 && i != 0) {
            for (auto& arg : argsArr) {
                result += arg + delimiter;
                arg.Truncate(0);
            }
            result.Truncate(result.Length() - delimiter.Length());
        }
        ++i;
        if (isParamInit && IsNotEndOfMangledName() && curDi.functionParameterTypes.Length() > 0) {
            argsArr[j] = curDi.GetFullName(scopeResolution) + LEFT_BRACKET + curDi.functionParameterTypes +
                    RIGHT_BRACKET;
        } else {
            argsArr[j] = curDi.GetFullName(scopeResolution);
        }
        lastElement = curDi;
    }
    result += GetArgTypesFullName(argsArr, i % (MAX_ARGS_SIZE + 1), delimiter);
    DemangleInfo<T> resDi = { result, typeKind, isValid };
    resDi.functionParameterTypes = lastElement.functionParameterTypes;
    return resDi;
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleClass(TypeKind typeKind)
{
    ++currentIndex;         // Skip one character representing the typeKind
    auto di = DemangleCommonDecl(true);
    di.type = typeKind;
    char ch;
    if (PeekChar(ch) && ch == END) {
        SkipChar(END);
    }
    return di;
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleCFuncType()
{
    SkipString(MANGLE_C_FUNCTY_PREFIX);
    DemangleInfo<T> di{ DemangleNextUnit().GetFullName(scopeResolution), TypeKind::FUNCTION };
    di.args = DemangleArgTypes(ARGS_DELIMITER);
    SkipChar(END);
    return di;
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleFunction()
{
    SkipString(MANGLE_FUNCTY_PREFIX);
    DemangleInfo<T> di{ DemangleNextUnit().GetFullName(scopeResolution), TypeKind::FUNCTION };
    di.args = DemangleArgTypes(ARGS_DELIMITER);
    SkipChar(END);
    return di;
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleTuple()
{
    DemangleInfo<T> di{ MANGLE_TUPLE_STR, TypeKind::TUPLE };
    SkipChar(MANGLE_TUPLE_PREFIX);
    (void)DemangleManglingNumber();
    // Demangle to get an arr, then call this func to extract
    std::array<T, MAX_ARGS_SIZE> argsArr;
    auto generic = DemangleGenericTypes().GetFullName(scopeResolution);
    di.genericTypes = genericTypefilter(generic);
    SkipChar(END);
    return di;
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleRawArray()
{
    DemangleInfo<T> di{ MANGLE_ARRAY_STR, TypeKind::RAW_ARRAY };
    SkipChar(MANGLE_ARRAY_PREFIX);
    auto dim = DemangleManglingNumber();
    while (dim--) {
        di.genericConstraints += "[]";
    }
    di.args = DemangleNextUnit().GetFullName(scopeResolution);
    return di;
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleVArray()
{
    DemangleInfo<T> di{ MANGLE_VARRAY_STR, TypeKind::TUPLE };
    SkipChar(MANGLE_VARRAY_PREFIX);
    uint32_t num = DemangleManglingNumber();
    di.genericTypes = DemangleGenericTypes().GetFullName(scopeResolution, num);
    return di;
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleCPointer()
{
    DemangleInfo<T> di{ MANGLE_PTR_STR, TypeKind::CPOINTER };
    SkipChar(MANGLE_PTR_PREFIX);
    auto generic = DemangleType().GetFullName(scopeResolution);
    di.genericTypes = genericTypefilter(generic);
    return di;
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleCommonDecl(bool isClass)
{
    bool isGlobalVariableInitPrefix = false;
    if (mangledName[currentIndex] == MANGLE_NESTED_PREFIX[0]) {
        SkipString(MANGLE_NESTED_PREFIX);
    } else {
        SkipString(GLOBAL_VARIABLE_INIT_PREFIX);
        isGlobalVariableInitPrefix = true;
    }
    if (!isValid) {
        return Reject();
    }
    DemangleInfo<T> di{ "", TypeKind::COMMON_DECL };
    auto pkgDi = DemanglePackageName();
    di.pkgName = pkgDi.pkgName;
    di.isPrivateDeclaration = pkgDi.isPrivateDeclaration;
    
    // Take the part before E as identifier and concatenate with scope resolution.
    DemangleInfo<T> nestedDi = DemangleNestedDecls(isClass);
    di.demangled = nestedDi.demangled;
    di.type = nestedDi.type;
    di.args = nestedDi.args;
    if (!isValid || di.demangled.IsEmpty()) {
        return Reject();
    }
    if (di.type == TypeKind::FUNCTION_DECL) {
        di.args = nestedDi.functionParameterTypes;
    }
    // The cjdb currently expects global variable initialization symbol is demangled as a
    // variable (without parentheses). Here is a special demangle handling for keeping that behaviour.
    if (isGlobalVariableInitPrefix) {
        di.type = TypeKind::COMMON_DECL;
    }
    return di;
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleDecl()
{
    SkipString(MANGLE_CANGJIE_PREFIX);
    if (!isValid) {
        return Reject();
    }
    auto di = DemangleCommonDecl();
    if (di.type == TypeKind::LAMBDA_FUNCTION) {
        return di;
    }
    // Invalid input and non-function decl ends here
    if (!isValid || currentIndex >= mangledName.Length()) {
        return di;
    }

    if (mangledName[currentIndex] == END) {
        return di;
    }

    // Collect function param-types
    di.type = TypeKind::FUNCTION_DECL;
    di.args = DemangleArgTypes(ARGS_DELIMITER);
    if (!isValid) {
        return Reject();
    }
    if (IsMaybeDefaultFuncParamFunction()) {
        auto dpf = DemangleDefaultParamFunction();
        if (!isValid) {
            return Reject();
        }
        di.demangled = di.GetFullName(scopeResolution) + scopeResolution + dpf.demangled;
        di.args = dpf.args;
    }
    return di;
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemanglePrimitive()
{
    if (currentIndex >= mangledName.Length()) {
        return Reject(ERROR_EXCEED_THE_INPUT_STRING_LENGTH);
    }
    char ch = mangledName[currentIndex];
    if (MatchForward(MANGLE_FLOAT16_PREFIX, strlen(MANGLE_FLOAT16_PREFIX))) {
        SkipString(MANGLE_FLOAT16_PREFIX); // skip "Dh"
    } else {
        SkipChar(ch);
    }
    DemangleInfo<T> di{ GetPrimitiveDemangleMap<T>(ch), TypeKind::PRIMITIVE };
    return di;
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleCStringType()
{
    SkipChar(MANGLE_CSTRING_PREFIX);
    return DemangleInfo<T>{ MANGLE_CSTRING_STR, TypeKind::CSTRING, isValid };
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleGenericType()
{
    SkipChar(MANGLE_GTY_PREFIX);
    size_t gNumber = DemangleManglingNumber();
    T name;
#ifdef BUILD_LIB_CANGJIE_DEMANGLE
    if (genericVec.empty()) {
        name = T{ MANGLE_TUPLE_PREFIX } + UIntToString(gNumber);
    } else {
        if (gNumber < genericVec.size()) {
            name = T{ genericVec[gNumber].c_str() };
        } else {
            isValid = false;
        }
    }
#else
    name = T{ MANGLE_TUPLE_PREFIX } + UIntToString(gNumber);
#endif
    return DemangleInfo<T>{ name, TypeKind::GENERIC_TYPES, isValid };
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleType()
{
    DemangleInfo<T> di{ "", TypeKind::GENERIC_TYPES };
    if (IsQualifiedType()) {
        auto curDi = DemangleNextUnit();
        if (isValid) {
            di.args = curDi.GetFullName(scopeResolution);
            return di;
        }
    }
    isValid = false;
    return di;
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleGenericTypes()
{
    DemangleInfo<T> di{ "", TypeKind::GENERIC_TYPES };
    bool firstIteration = true;
    T argTypesFullName{};
    // Generic type may be next to a string, like "6<init>"
    while (IsNotEndOfMangledName() && !IsCurrentCharDigit(mangledName[currentIndex]) &&
           !IsFunction(mangledName[currentIndex]) && !IsOperatorName()) {
        auto curDi = DemangleNextUnit();
        if (!isValid) {
            break;
        }
        if (!firstIteration) {
            argTypesFullName += T{ ARGS_DELIMITER_TYPE };
        }
        argTypesFullName += curDi.GetFullName(scopeResolution);
        firstIteration = false;
    }
    di.args = argTypesFullName;
    return di;
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleFunctionParameterTypes()
{
    DemangleInfo<T> di{ "", TypeKind::FUNCTION_PARAMETER_TYPES };
    bool firstIteration = true;
    T argTypesFullName{};
    if (IsNotEndOfMangledName() && mangledName[currentIndex] == MANGLE_VOID_PREFIX) {
        currentIndex += MANGLE_CHAR_LEN;
    } else {
        // Generic type may be next to a string, like "6<init>"
        while (IsNotEndOfMangledName() && !IsCurrentCharDigit(mangledName[currentIndex])) {
            auto curDi = DemangleNextUnit();
            if (!isValid) {
                break;
            }
            if (!firstIteration) {
                argTypesFullName += T{ ARGS_DELIMITER };
            }
            argTypesFullName += curDi.GetFullName(scopeResolution);
            firstIteration = false;
        }
    }
    di.args = argTypesFullName;
    return di;
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleDefaultParamFunction()
{
    auto pos = mangledName.Find(MANGLE_UNDERSCORE_PREFIX, currentIndex);
    if (pos == -1 || pos == static_cast<int>(mangledName.Length()) - 1 ||
        !IsCurrentCharDigit(mangledName[pos + MANGLE_CHAR_LEN])) {
        return Reject("invalid decl");
    }

    SkipChar(MANGLE_DEFAULT_PARAM_FUNCTION_PREFIX);
    auto identifier = mangledName.SubStr(currentIndex, pos - currentIndex);
    DemangleInfo<T> di{ identifier, TypeKind::FUNCTION_DECL };
    SkipString(identifier.Str());
    SkipChar(MANGLE_UNDERSCORE_PREFIX);
    // Skip the number of this param.
    (void)DemangleLength();
    if (!IsNotEndOfMangledName()) {
        return Reject("invalid decl");
    }
    di.args = DemangleArgTypes(ARGS_DELIMITER);
    return di;
}

/**
 * @brief Parse pattern like "$pkglambda.0"
 */
template<typename T>
DemangleInfo<T> Demangler<T>::DemangleInnerFunction()
{
    DemangleInfo<T> di{ "", TypeKind::LAMBDA_FUNCTION };
    // `IsInnerFunction()` has made true the substring exist.
    auto prefixIndex = mangledName.Find(MANGLE_INNER_FUNCTION_PREFIX);
    if (!isValid) {
        return Reject();
    }
    auto pkgName = mangledName.SubStr(currentIndex, prefixIndex - currentIndex);
    di.pkgName = pkgName;
    auto funcName = mangledName.SubStr(prefixIndex + MANGLE_CHAR_LEN); // Skip "$"
    di.demangled = funcName;
    currentIndex = mangledName.Length();
    return di;
}

template<typename T>
DemangleInfo<T> Demangler<T>::Reject(const T& reason)
{
    // Quick return from a failed state
    if (currentIndex == mangledName.Length() && !isValid) {
        return dumpDi;
    }
    ErrorLog(reason.Str());
    isValid = false;
    currentIndex = mangledName.Length();
    return dumpDi;
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleNextUnit(const T& message)
{
    auto di = DemangleByPrefix();
    if (!isValid) {
        return Reject(T{ "Demangle next token error: " } + message);
    }
    return di;
}

template<typename T>
DemangleInfo<T> Demangler<T>::DemangleByPrefix()
{
    if (currentIndex >= mangledName.Length()) {
        return Reject(ERROR_EXCEED_THE_INPUT_STRING_LENGTH);
    }
    char ch = mangledName[currentIndex];
    switch (ch) {
        case 'C':
            return DemangleClass(TypeKind::CLASS);
        case 'R':
            return DemangleClass(TypeKind::STRUCT);
        case 'F':
            if (currentIndex + MANGLE_CHAR_LEN < mangledName.Length()) {
                // The F0 and FC is the function type prefix.
                if (mangledName[currentIndex + MANGLE_CHAR_LEN] == '0') {
                    return DemangleFunction();
                } else if (mangledName[currentIndex + MANGLE_CHAR_LEN] == 'C') {
                    return DemangleCFuncType();
                } else {
                    break;
                }
            } else {
                break;
            }
        case 'N':
            return DemangleClass(TypeKind::ENUM);
        case 'T':
            return DemangleTuple();
        case 'P':
            return DemangleCPointer();
        case '_':
            break;
        case 'A':
            return DemangleRawArray();
        case 'V':
            return DemangleVArray();
        case 'k':
            return DemangleCStringType();
        case 'G':
            return DemangleGenericType();
        default:
            if (IsCurrentCharDigit(ch)) {
                return DemangleInfo<T>{ DemangleStringName(), TypeKind::NAME, isValid };
            } else if (IsPrimitive<T>(ch)) {
                return DemanglePrimitive();
            } else if (IsOperatorName()) {
                auto di = DemangleInfo<T>{ GetOperatorNameMap<T>(mangledName.SubStr(currentIndex, SPECIAL_NAME_LEN)),
                                           TypeKind::NAME, isValid };
                currentIndex += SPECIAL_NAME_LEN;
                return di;
            }
    }
    return Reject("unexpected character");
}

#ifdef BUILD_LIB_CANGJIE_DEMANGLE // to reuse the code to compile CangjieDemangle.cpp
template struct DemangleInfo<StdString>;
template class Demangler<StdString>;
template class DeCompression<StdString>;
#else
template struct DemangleInfo<MapleRuntime::CString>;
template class Demangler<MapleRuntime::CString>;
template class DeCompression<MapleRuntime::CString>;
#endif
} // namespace Cangjie
