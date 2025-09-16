// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file implements name mangling compression.
 */

#include "Compression.h"

#include "cangjie/Mangle/BaseMangler.h"

using namespace Cangjie::MangleUtils;

namespace Cangjie::Compression {
std::string DecimalToManglingNumber(const std::string& decimal)
{
    CJC_ASSERT(!decimal.empty() && "the string of decimal is empty.");
    const size_t n = 62;
    const std::string base62Chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    for (size_t i = 0; i < decimal.length(); i++) {
        if (!std::isdigit(decimal[i])) {
            return "";
        }
    }
    // Because _ represents 1, decimal to mangling number requires subtracting 1.
    int num = std::stoi(decimal) - 1;
    if (num < 0) {
        return MANGLE_WILDCARD_PREFIX;
    }
    std::string base62 = num > 0 ? "" : "0";
    while (num > 0) {
        base62 = base62Chars[num % n] + base62;
        num /= n;
    }
    return base62 + MANGLE_WILDCARD_PREFIX;
}

inline void RecursionTypes(size_t& idx, char pre, std::string& compressed,
    std::tuple<size_t, std::vector<std::unique_ptr<CJType>>&, std::unordered_map<std::string, size_t>&, size_t&> tuple)
{
    // The subscript indexes of the tuple are as follows:
    // { 0: nextIdx, 1: tys, 2: treeIdMap, 3: mid }
    size_t nextIdx = std::get<0>(tuple);
    if (nextIdx != idx + MANGLE_CHAR_LEN && !std::get<1>(tuple).empty()) {
        compressed += pre;
        for (const auto& ty : std::get<1>(tuple)) {
            // The index 2 of the tuple is treeIdMap and 3 is mid.
            RecursionType(ty, std::get<2>(tuple), std::get<3>(tuple), compressed, false);
        }
        idx = nextIdx;
    }
}

// Get identifier without prefix "@"
inline std::string StripCangjieAt(std::string& identifier, size_t &pid)
{
    if (identifier != "" && identifier[0] == MANGLE_AT_PREFIX[0]) {
        pid += MANGLE_CHAR_LEN;
        return identifier.substr(MANGLE_CHAR_LEN);
    }
    return identifier;
}

// Get identifier without prefix "_C"
inline std::string StripCangjiePrefix(std::string& identifier, size_t &pid)
{
    if (identifier.size() > MANGLE_PREFIX_LEN) {
        pid += MANGLE_PREFIX_LEN;
        return identifier.substr(MANGLE_PREFIX_LEN);
    }
    return identifier;
}

inline bool IsGeneralEntity(EntityType entityTy)
{
    if (entityTy == EntityType::GENERAL_FUNCTION || entityTy == EntityType::GENERIC_FUNCTION ||
        entityTy == EntityType::GENERIC_DATA || entityTy == EntityType::EXTEND) {
        return false;
    }
    return true;
}

inline bool IsGeneralType(BaseType bt)
{
    if (bt == BaseType::TUPLE_TYPE || bt == BaseType::FUNCTION_TYPE || bt == BaseType::ENUM_TYPE ||
        bt == BaseType::STRUCT_TYPE || bt == BaseType::CLASS_TYPE) {
        return false;
    }
    return true;
}

inline bool IsPrimitiveType(char ch)
{
    if (PRIMITIVE_PREFIX_SET.find(ch) != PRIMITIVE_PREFIX_SET.end()) {
        return true;
    }
    return false;
}

inline bool IsPropName(std::string& mangled, size_t idx)
{
    if (idx + MANGLE_CHAR_LEN >= mangled.size()) {
        return false;
    }
    // Judge current mangled name is prop name "ps/pg".
    if (mangled[idx] == 'p' && (mangled[idx + MANGLE_CHAR_LEN] == 's' || mangled[idx + MANGLE_CHAR_LEN] == 'g')) {
        return true;
    }
    return false;
}

inline bool NeedCompressed(std::string& identifier)
{
    size_t pos = 0;
    while ((pos = identifier.find(MANGLE_COMPRESSED_PREFIX, pos)) != static_cast<size_t>(-1)) {
        size_t nextIdx = ForwardNumber(identifier, pos + MANGLE_CHAR_LEN);
        if (nextIdx != pos + MANGLE_CHAR_LEN) {
            return true;
        } else {
            pos += MANGLE_CHAR_LEN;
        }
    }
    return false;
}

inline bool IsGlobalEncode(std::string& mangled)
{
    if (mangled.find(MANGLE_GLOBAL_VARIABLE_INIT_PREFIX, 0) == 0 ||
        mangled.find(MANGLE_GLOBAL_PACKAGE_INIT_PREFIX, 0) == 0 ||
        mangled.find(MANGLE_GLOBAL_FILE_INIT_PREFIX, 0) == 0) {
        return true;
    }
    return false;
}

/**
 * Determine whether the code is a var decl code.
 * _CN<path><name>E
 * _CN<path><name>K<number>E
 *
 */
bool IsVarDeclEncode(std::string& mangled)
{
    bool isCompressed = false;
    const size_t n = mangled.size();
    size_t idx = 0;
    if (mangled[0] == MANGLE_SUFFIX[0]) {
        return true;
    } else if (isdigit(mangled[0])) {
        idx = ForwardName(mangled, isCompressed);
        if (idx < n && mangled[idx] == MANGLE_LOCAL_VAR_PREFIX[0]) {
            idx = ForwardNumber(mangled, idx + MANGLE_CHAR_LEN);
            if (idx < n && mangled[idx] == MANGLE_SUFFIX[0]) {
                return true;
            } else {
                return false;
            }
        } else if (idx < n && mangled[idx] == MANGLE_SUFFIX[0]) {
            return true;
        }
    } else if (mangled[0] == MANGLE_LOCAL_VAR_PREFIX[0]) {
        idx = ForwardNumber(mangled, idx + MANGLE_CHAR_LEN);
        if (idx < n && mangled[idx] == MANGLE_SUFFIX[0]) {
            return true;
        }
    }
    return false;
}

/**
 * Determine whether the code is a default param function code.
 * _CPI<path><function-name>H<type>+E<name>H<type>+
 */
bool IsDefaultParamFuncEncode(std::string& mangled)
{
    if (mangled.find(MANGLE_FUNC_PARA_INIT_PREFIX, 0) == 0) {
        return true;
    }
    return false;
}

// Determine whether the code is operator name or std package name.
inline bool IsSpecialName(std::string& mangled, size_t idx, const std::unordered_map<std::string, std::string>& map)
{
    // Special name range is [a-y][a-y], z is for extra scene.
    if (idx + MANGLE_CHAR_LEN < mangled.size() && mangled[idx] >= 'a' && mangled[idx] <= 'y' &&
        mangled[idx + MANGLE_CHAR_LEN] >= 'a' && mangled[idx + MANGLE_CHAR_LEN] <= 'y') {
        std::string curName = mangled.substr(idx, MANGLE_SPECIAL_NAME_LEN);
        for (auto it = map.begin(); it != map.end(); ++it) {
            if (it->second == curName) {
                return true;
            }
        }
    }
    return false;
}

inline size_t ForwardCompressed(std::string& mangled, size_t idx, bool& isCompressed)
{
    if (idx >= mangled.size()) {
        return idx;
    }
    if (mangled[idx] == MANGLE_COMPRESSED_PREFIX[0]) {
        isCompressed = true;
        return ForwardNumber(mangled, idx + MANGLE_CHAR_LEN);
    }
    return idx;
}

// Forward name, <identifier-length><identifier>.
size_t ForwardName(std::string& mangled, bool& isCompressed, size_t idx)
{
    size_t cIdx = ForwardCompressed(mangled, idx, isCompressed);
    if (isCompressed) {
        return cIdx;
    }
    if (cIdx >= mangled.size()) {
        return cIdx;
    }
    if (IsSpecialName(mangled, cIdx, MangleUtils::OPERATOR_TYPE_MANGLE) || IsPropName(mangled, cIdx)) {
        return cIdx + MANGLE_SPECIAL_NAME_LEN;
    }
    if (!isdigit(mangled[cIdx])) {
        return cIdx;
    }
    if (mangled[cIdx] == MANGLE_ANONYMOUS_VARIABLE_PREFIX[0]) { return idx + MANGLE_CHAR_LEN; }
    size_t numberLen = 0;
    while (idx < mangled.size() && isdigit(mangled[idx + numberLen])) {
        numberLen++;
    }
    size_t number = static_cast<size_t>(std::stoi(mangled.substr(idx, numberLen).c_str()));
    return idx + numberLen + number;
}

// Forward base62 number.
size_t ForwardNumber(std::string& mangled, size_t idx)
{
    size_t curIdx = idx;
    while (curIdx < mangled.size() && isalnum(mangled[curIdx])) {
        curIdx++;
    }
    if (curIdx < mangled.size() && mangled[curIdx] == MANGLE_WILDCARD_PREFIX[0]) {
        return curIdx + MANGLE_CHAR_LEN;
    }
    return idx;
}

/**
 * Forward filename number.
 *        if filename length < 12, then <filename>$
 *        else then <hashcode>, the length of <hashcode> is 13
 */
size_t ForwardFileNameNumber(std::string& mangled, size_t idx)
{
    size_t curIdx = idx;
    bool isValid = false;
    while (true) {
        if (curIdx >= mangled.size()) {
            return idx;
        }
        if (mangled[curIdx] == MANGLE_DOLLAR_PREFIX[0]) {
            return ++curIdx;
        }
        if (curIdx + 1 - idx == FILE_HASH_LEN) {
            isValid = true;
            break;
        }
        curIdx++;
    }
    if (isValid) {
        for (size_t i = idx; i <= curIdx; i++) {
            if (!isalnum(mangled[i])) {
                return idx;
            }
        }
        return ++curIdx;
    }
    return idx;
}

// Forward package name, <identifier-length><identifier> or std pkg.
inline size_t ForwardUnitPackageName(std::string& mangled, size_t idx, bool& isCompressed)
{
    size_t curIdx = idx;
    if (IsSpecialName(mangled, curIdx, MangleUtils::STD_PKG_MANGLE)) {
        curIdx += MANGLE_SPECIAL_NAME_LEN;
    }
    if (curIdx != idx + MANGLE_SPECIAL_NAME_LEN) {
        curIdx = ForwardName(mangled, isCompressed, idx);
        size_t cIdx = ForwardCompressed(mangled, curIdx, isCompressed);
        if (isCompressed) {
            return cIdx;
        }
    }
    return curIdx;
}

// Forward package name, <identifier-length><identifier>[U<number>].
inline size_t ForwardPackageName(std::string& mangled, size_t idx, bool& isCompressed)
{
    size_t curIdx = ForwardUnitPackageName(mangled, idx, isCompressed);
    if (curIdx < mangled.size() && mangled[curIdx] == MANGLE_FILE_ID_PREFIX[0]) {
        return ForwardFileNameNumber(mangled, curIdx + MANGLE_CHAR_LEN);
    }
    return curIdx;
}

inline size_t ForwardPrimitiveType(std::string& mangled, size_t idx)
{
    const size_t n = mangled.size();
    if (mangled[idx] != 'D' && IsPrimitiveType(mangled[idx])) {
        return idx + MANGLE_CHAR_LEN;
    }
    if (mangled[idx] == 'D' && idx + MANGLE_CHAR_LEN < n && mangled[idx + MANGLE_CHAR_LEN] == 'h') {
        return idx + MANGLE_SPECIAL_NAME_LEN;
    }
    return idx;
}

// Forward generic types, I <type>+ E.
inline size_t ForwardGenericTypes(std::string& mangled, std::vector<std::unique_ptr<CJType>>& tys, bool& isCompressed,
    size_t idx)
{
    size_t curIdx = ForwardTypes(mangled, tys, isCompressed, idx + MANGLE_CHAR_LEN);
    if (isCompressed) {
        return idx;
    }
    if (curIdx != idx && mangled[curIdx] == MANGLE_SUFFIX[0]) {
        return curIdx + MANGLE_CHAR_LEN;
    }
    return idx;
}

// Forward generic type, G<number>.
inline size_t ForwardGenericType(std::string& mangled, size_t idx)
{
    return ForwardNumber(mangled, idx + MANGLE_CHAR_LEN);
}

/**
 * Forward class type, CN<package><name>E.
 *                     CN<package><name>I<type>+E
 */
inline size_t ForwardClassType(std::string& mangled, BaseType bt, std::vector<std::unique_ptr<CJType>>& tys,
    size_t idx, bool& isCompressed)
{
    ForwardCompressed(mangled, idx, isCompressed);
    if (isCompressed) {
        return idx;
    }
    size_t curIdx = idx;
    const size_t n = mangled.size();
    std::string mangledName;
    std::vector<std::unique_ptr<CJType>> genericTys;
    std::string pkg;
    std::string name;
    // Skip 'CN' 'RN' 'NN'
    if (idx + MANGLE_PREFIX_LEN < n && (mangled[idx] == 'C' || mangled[idx] == 'R' || mangled[idx] == 'N')
        && mangled[idx + MANGLE_CHAR_LEN] == MANGLE_NESTED_PREFIX[0]) {
        curIdx += MANGLE_PREFIX_LEN;
    } else {
        tys.push_back(std::make_unique<CompositeType>(CompositeType(mangledName, BaseType::ERROR_BASE_TYPE,
            genericTys, pkg, name)));
        return idx;
    }
    size_t pIdx = curIdx;
    curIdx = ForwardPackageName(mangled, curIdx, isCompressed);
    if (isCompressed) {
        return idx;
    }
    if (pIdx < n) {
        pkg = mangled.substr(pIdx, curIdx - pIdx);
    }
    size_t nIdx = curIdx;
    curIdx = ForwardName(mangled, isCompressed, curIdx);
    if (isCompressed) {
        return idx;
    }
    if (nIdx < n) {
        name = mangled.substr(nIdx, curIdx - nIdx);
    }
    if (mangled[curIdx] == MANGLE_GENERIC_PREFIX[0]) {
        size_t genericIdx = ForwardGenericTypes(mangled, genericTys, isCompressed, curIdx);
        if (isCompressed) {
            return idx;
        }
        if (curIdx != genericIdx) {
            mangledName = mangled.substr(idx, genericIdx - idx);
            tys.push_back(std::make_unique<CompositeType>(CompositeType(mangledName, bt, genericTys, pkg, name)));
            return genericIdx;
        }
    } else if (mangled[curIdx] == MANGLE_SUFFIX[0]) {
        mangledName = mangled.substr(idx, curIdx + MANGLE_CHAR_LEN - idx);
        tys.push_back(std::make_unique<CompositeType>(CompositeType(mangledName, bt, genericTys, pkg, name)));
        return curIdx + MANGLE_CHAR_LEN;
    }
    tys.push_back(std::make_unique<CompositeType>(CompositeType(mangledName, BaseType::ERROR_BASE_TYPE, genericTys,
        pkg, name)));
    return idx;
}

/**
 * Forward function type, include general function and C function.
 *         F0<return-type><param-types>E
 *         FC<return-type><param-types>E
 */
inline size_t ForwardFunctionType(std::string& mangled, std::vector<std::unique_ptr<CJType>>& tys, size_t idx,
    bool& isCompressed)
{
    ForwardCompressed(mangled, idx, isCompressed);
    if (isCompressed) {
        return idx;
    }
    std::vector<std::unique_ptr<CJType>> retTy;
    std::vector<std::unique_ptr<CJType>> paramTys;
    std::string mangledName;
    size_t curIdx = ForwardType(mangled, retTy, isCompressed, idx + MANGLE_PREFIX_LEN);
    if (isCompressed) {
        return idx;
    }
    curIdx = ForwardTypes(mangled, paramTys, isCompressed, curIdx);
    if (isCompressed) {
        return idx;
    }
    if (curIdx != idx && mangled[curIdx] == MANGLE_SUFFIX[0]) {
        mangledName = mangled.substr(idx, curIdx + MANGLE_CHAR_LEN - idx);
        tys.push_back(std::make_unique<FunctionType>(FunctionType(mangledName, BaseType::FUNCTION_TYPE,
            std::move(retTy[0]), paramTys)));
        return curIdx + MANGLE_CHAR_LEN;
    }
    tys.push_back(std::make_unique<FunctionType>(FunctionType(mangledName, BaseType::ERROR_BASE_TYPE,
        std::move(retTy[0]), paramTys)));
    return idx;
}

// Forward tuple type, T<number><type>+E.
inline size_t ForwardTupleType(std::string& mangled, std::vector<std::unique_ptr<CJType>>& tys, size_t idx,
    bool& isCompressed)
{
    ForwardCompressed(mangled, idx, isCompressed);
    if (isCompressed) {
        return idx;
    }
    std::vector<std::unique_ptr<CJType>> elementTys;
    std::string mangledName;
    size_t curIdx = idx + MANGLE_CHAR_LEN;
    if (curIdx < mangled.size()) {
        curIdx = ForwardNumber(mangled, curIdx);
    }
    if (curIdx < mangled.size()) {
        size_t endIdx = ForwardTypes(mangled, elementTys, isCompressed, curIdx);
        if (isCompressed) {
            return idx;
        }
        if (curIdx != endIdx && mangled[endIdx] == MANGLE_SUFFIX[0]) {
            mangledName = mangled.substr(idx, endIdx + MANGLE_CHAR_LEN - idx);
            tys.push_back(std::make_unique<TupleType>(TupleType(mangledName, BaseType::TUPLE_TYPE, elementTys)));
            return endIdx + MANGLE_CHAR_LEN;
        }
    }
    tys.push_back(std::make_unique<TupleType>(TupleType(mangledName, BaseType::ERROR_BASE_TYPE, elementTys)));
    return idx;
}

// Forward CPointer type, P<type>.
inline size_t ForwardCPointer(std::string& mangled, size_t idx, bool& isCompressed)
{
    ForwardCompressed(mangled, idx, isCompressed);
    if (isCompressed) {
        return idx;
    }
    std::vector<std::unique_ptr<CJType>> ty;
    size_t curIdx = ForwardType(mangled, ty, isCompressed, idx + MANGLE_CHAR_LEN);
    if (isCompressed) {
        return idx;
    }
    if (curIdx != idx) {
        return curIdx;
    }
    return idx;
}

/**
 * Forward RawArray type or VArray type.
 *                  A<number><type>
 *                  V<number><type>
 */
inline size_t ForwardArrayType(std::string& mangled, size_t idx, bool& isCompressed)
{
    ForwardCompressed(mangled, idx, isCompressed);
    if (isCompressed) {
        return idx;
    }
    size_t curIdx = idx + MANGLE_CHAR_LEN;
    if (curIdx < mangled.size()) {
        curIdx = ForwardNumber(mangled, curIdx);
    }
    if (curIdx < mangled.size()) {
        std::vector<std::unique_ptr<CJType>> ty;
        size_t endIdx = ForwardType(mangled, ty, isCompressed, curIdx);
        if (isCompressed) {
            return idx;
        }
        if (endIdx != curIdx) {
            return endIdx;
        }
    }
    return idx;
}

// Forward type, if type is invalid, return idx is initial value.
size_t ForwardType(std::string& mangled, std::vector<std::unique_ptr<CJType>>& tys, bool& isCompressed, size_t idx)
{
    char ch = mangled[idx];
    BaseType bt;
    size_t nextIdx = idx;
    switch (ch) {
        case 'C':
            bt = BaseType::CLASS_TYPE;
            nextIdx = ForwardClassType(mangled, bt, tys, idx, isCompressed);
            return nextIdx;
        case 'R':
            bt = BaseType::STRUCT_TYPE;
            nextIdx = ForwardClassType(mangled, bt, tys, idx, isCompressed);
            return nextIdx;
        case 'N':
            bt = BaseType::ENUM_TYPE;
            nextIdx = ForwardClassType(mangled, bt, tys, idx, isCompressed);
            return nextIdx;
        case 'F':
            if (idx + MANGLE_CHAR_LEN < mangled.size() && (mangled[idx + MANGLE_CHAR_LEN] == '0' ||
                mangled[idx + MANGLE_CHAR_LEN] == 'C')) {
                nextIdx = ForwardFunctionType(mangled, tys, idx, isCompressed);
            } else {
                nextIdx = idx;
            }
            return nextIdx;
        case 'T':
            nextIdx = ForwardTupleType(mangled, tys, idx, isCompressed);
            return nextIdx;
        case 'P':
            bt = BaseType::CPOINTER_TYPE;
            nextIdx = ForwardCPointer(mangled, idx, isCompressed);
            break;
        case 'A':
            bt = BaseType::RAWARRAY_TYPE;
            nextIdx = ForwardArrayType(mangled, idx, isCompressed);
            break;
        case 'V':
            bt = BaseType::VARRAY_TYPE;
            nextIdx = ForwardArrayType(mangled, idx, isCompressed);
            break;
        case 'k':
            bt = BaseType::CSTRING_TYPE;
            nextIdx = idx + MANGLE_CHAR_LEN;
            break;
        case 'G':
            bt = BaseType::GENERIC_TYPE;
            nextIdx = ForwardGenericType(mangled, idx);
            break;
        default:
            if (IsPrimitiveType(ch)) {
                bt = BaseType::PRIMITIVE_TYPE;
                nextIdx = ForwardPrimitiveType(mangled, idx);
            } else {
                bt = BaseType::ERROR_BASE_TYPE;
                nextIdx = idx;
            }
            break;
    }
    if (isCompressed) {
        return idx;
    }
    std::string mangledTy;
    if (nextIdx != idx) {
        mangledTy = mangled.substr(idx, nextIdx - idx);
        if (IsGeneralType(bt)) {
            tys.push_back(std::make_unique<CJType>(CJType(mangledTy, bt)));
        }
        return nextIdx;
    } else {
        tys.push_back(std::make_unique<CJType>(CJType(mangledTy, bt)));
        return idx;
    }
}

size_t ForwardTypes(std::string& mangled, std::vector<std::unique_ptr<CJType>>& tys, bool& isCompressed,
    size_t startId)
{
    const size_t n = mangled.size();
    if (n - startId <= 0) {
        return startId;
    }
    size_t idx = startId;
    while (idx < n && mangled[idx] != MANGLE_SUFFIX[0]) {
        size_t curIdx = ForwardType(mangled, tys, isCompressed, idx);
        if (curIdx == idx || isCompressed) {
            return curIdx;
        }
        idx = curIdx;
    }
    return idx;
}

std::string CJMangledCompression(const std::string mangled, bool isType)
{
    bool isCompressed = false;
    size_t preIdx = 0;
    std::string cMangled = mangled;
    cMangled = StripCangjieAt(cMangled, preIdx);
    if (cMangled.empty() || isType || cMangled.find(MANGLE_CANGJIE_PREFIX, 0) != 0 || NeedCompressed(cMangled)) {
        return mangled;
    }
    // Skip "_C"
    cMangled = StripCangjiePrefix(cMangled, preIdx);
    if (IsGlobalEncode(cMangled)) {
        return mangled;
    }

    std::string rest;
    if (IsDefaultParamFuncEncode(cMangled)) {
        std::string compressed = "";
        std::string nestedName = cMangled.substr(MANGLE_PREFIX_LEN);
        preIdx += MANGLE_PREFIX_LEN;
        std::vector<std::unique_ptr<Entity>> entities = TryParsePath(nestedName, rest, isCompressed);
        if (isCompressed) {
            return mangled;
        }
        isCompressed = SpanningDefaultParamFuncDeclTree(entities, rest, compressed);
        if (isCompressed) {
            return mangled;
        }
        return preIdx == 0 ? compressed : mangled.substr(0, preIdx) + compressed;
    }

    if (!cMangled.empty() && cMangled[0] == MANGLE_NESTED_PREFIX[0]) {
        std::string compressed = "";
        std::string nestedName = cMangled.substr(MANGLE_CHAR_LEN);
        preIdx += MANGLE_CHAR_LEN;
        std::vector<std::unique_ptr<Entity>> entities = TryParsePath(nestedName, rest, isCompressed);
        if (isCompressed) {
            return mangled;
        }
        if (IsVarDeclEncode(rest)) {
            isCompressed = SpanningVarDeclTree(entities, rest, compressed);
        } else {
            // Determine whether the code is a function code.
            // _CN<path><function-name>[I<type>+]H<type>+
            // _CN<path><function-name>K<number>[I<type>+]H<type>+
            // _CN<path>L<number>E
            isCompressed = SpanningFuncDeclTree(entities, rest, compressed);
        }
        if (isCompressed) {
            return mangled;
        }
        return preIdx == 0 ? compressed : mangled.substr(0, preIdx) + compressed;
    }
    return mangled;
}

/**
 * Get Entity of path.
 * Complying with the mangle rule the name prefix entity which contains I<type>+.
 */
std::tuple<size_t, EntityType, std::string, bool> TryGenericPrefixPath(std::string& mangled, bool& isCompressed,
    std::vector<std::unique_ptr<Entity>>& entities, size_t idx, size_t nextIdx, EntityType entityTy)
{
    const size_t n = mangled.size();
    std::string entityName = "";
    std::vector<std::unique_ptr<CJType>> genericTys;
    nextIdx = ForwardTypes(mangled, genericTys, isCompressed, nextIdx + MANGLE_CHAR_LEN);
    if (isCompressed) {
        return std::tuple{nextIdx, entityTy, entityName, true};
    }
    if (nextIdx < n && mangled[nextIdx] == MANGLE_SUFFIX[0]) {
        entityTy = EntityType::GENERIC_DATA;
        entityName = mangled.substr(idx, nextIdx - idx);
        entities.push_back(std::make_unique<DataEntity>(DataEntity(entityName, entityTy, genericTys)));
        nextIdx += MANGLE_CHAR_LEN;
    } else if (nextIdx < n && mangled[nextIdx] == MANGLE_FUNC_PARAM_TYPE_PREFIX[0]) {
        std::vector<std::unique_ptr<CJType>> paramTys;
        nextIdx = ForwardTypes(mangled, paramTys, isCompressed, nextIdx + MANGLE_CHAR_LEN);
        if (isCompressed) {
            return std::tuple{nextIdx, entityTy, entityName, true};
        }
        if (nextIdx < n && mangled[nextIdx] == MANGLE_SUFFIX[0]) {
            nextIdx++;
            entityTy = EntityType::GENERIC_FUNCTION;
            entityName = mangled.substr(idx, nextIdx - idx);
            entities.push_back(std::make_unique<FunctionEntity>(FunctionEntity(entityName, entityTy,
                paramTys, genericTys)));
        } else {
            nextIdx = idx;
        }
    } else {
        nextIdx = idx;
    }
    return std::tuple{nextIdx, entityTy, entityName, false};
}

/**
 * Get Entity of path.
 * Complying with the mangle rule of the name prefix entity.
 */
std::tuple<size_t, EntityType, std::string, bool> TryNamePrefixPath(std::string& mangled, bool& isCompressed,
    std::vector<std::unique_ptr<Entity>>& entities, size_t idx)
{
    const size_t n = mangled.size();
    std::string entityName = "";
    EntityType entityTy = EntityType::NAME;
    size_t nextIdx = ForwardName(mangled, isCompressed, idx);
    if (isCompressed) {
        return std::tuple{nextIdx, entityTy, entityName, true};
    }
    size_t nameIdx = nextIdx;
    if (nextIdx < n && mangled[nextIdx] == MANGLE_LOCAL_VAR_PREFIX[0]) {
        nextIdx = ForwardNumber(mangled, nextIdx + MANGLE_CHAR_LEN);
    }
    if (nextIdx < n) {
        if (mangled[nextIdx] == MANGLE_FILE_ID_PREFIX[0]) {
            nextIdx = ForwardFileNameNumber(mangled, nameIdx + MANGLE_CHAR_LEN);
            entityTy = nextIdx == nameIdx + MANGLE_CHAR_LEN ? EntityType::ERROR_ENTITY_TYPE : EntityType::PACKAGE;
            nextIdx = nextIdx == nameIdx + MANGLE_CHAR_LEN ? nameIdx : nextIdx;
        } else if (mangled[nextIdx] == MANGLE_GENERIC_PREFIX[0]) {
            return TryGenericPrefixPath(mangled, isCompressed, entities, idx, nextIdx, entityTy);
        } else if (mangled[nextIdx] == MANGLE_FUNC_PARAM_TYPE_PREFIX[0]) {
            std::vector<std::unique_ptr<CJType>> paramTys;
            std::vector<std::unique_ptr<CJType>> genericTys;
            nextIdx = ForwardTypes(mangled, paramTys, isCompressed, nameIdx + MANGLE_CHAR_LEN);
            if (isCompressed) {
                return std::tuple{nextIdx, entityTy, entityName, true};
            }
            if (nextIdx < n && mangled[nextIdx] == MANGLE_SUFFIX[0]) {
                nextIdx++;
                entityTy = EntityType::GENERAL_FUNCTION;
                entityName = mangled.substr(idx, nextIdx - idx);
                entities.push_back(std::make_unique<FunctionEntity>(FunctionEntity(entityName, entityTy,
                    paramTys, genericTys)));
            } else {
                nextIdx = idx;
            }
        } else {
            nextIdx = nameIdx;
        }
    }
    return std::tuple{nextIdx, entityTy, entityName, false};
}

/**
 * Get Entity of path.
 * Complying with the mangle rule of lambda entity.
 */
std::tuple<size_t, EntityType> TryLambdaPath(std::string& mangled, size_t idx)
{
    EntityType entityTy = EntityType::ERROR_ENTITY_TYPE;
    size_t nextIdx = ForwardNumber(mangled, idx + MANGLE_CHAR_LEN);
    if (nextIdx == mangled.size() - MANGLE_CHAR_LEN && mangled[nextIdx] == MANGLE_SUFFIX[0]) {
        nextIdx = idx;
    } else {
        entityTy = nextIdx == idx + MANGLE_CHAR_LEN ? EntityType::ERROR_ENTITY_TYPE : EntityType::LAMBDA;
        nextIdx = nextIdx == idx + MANGLE_CHAR_LEN ? idx : nextIdx;
    }
    return std::tuple{nextIdx, entityTy};
}

/**
 * Get Entity of path.
 * Complying with the mangle rule of extend entity.
 */
std::tuple<size_t, EntityType, std::string, bool> TryExtendPath(std::string& mangled, bool& isCompressed,
    std::vector<std::unique_ptr<Entity>>& entities, size_t idx)
{
    const size_t n = mangled.size();
    EntityType entityTy = EntityType::ERROR_ENTITY_TYPE;
    std::string entityName = "";
    std::vector<std::unique_ptr<CJType>> baseTy;
    size_t nextIdx = ForwardType(mangled, baseTy, isCompressed, idx + MANGLE_CHAR_LEN);
    if (isCompressed) {
        return std::tuple{nextIdx, entityTy, entityName, true};
    }
    size_t tyId = nextIdx;
    std::string extendTy = mangled.substr(idx + MANGLE_CHAR_LEN, nextIdx - idx - MANGLE_CHAR_LEN);
    std::string fileId;
    std::string localId;
    if (nextIdx < n && mangled[nextIdx] == MANGLE_FILE_ID_PREFIX[0]) {
        size_t fileIdx = nextIdx;
        nextIdx = ForwardFileNameNumber(mangled, nextIdx + MANGLE_CHAR_LEN);
        fileId = mangled.substr(fileIdx, nextIdx - fileIdx);
        if (nextIdx < n && mangled[nextIdx] == MANGLE_LOCAL_VAR_PREFIX[0]) {
            size_t localIdx = nextIdx;
            nextIdx = ForwardNumber(mangled, nextIdx);
            localId = mangled.substr(localIdx, nextIdx - localIdx);
            entityName = mangled.substr(idx, nextIdx - idx);
            entityTy = EntityType::EXTEND;
            entities.push_back(std::make_unique<ExtendEntity>(ExtendEntity(entityName, entityTy,
                std::move(baseTy[0]), fileId, localId)));
        } else {
            nextIdx = idx;
        }
    } else {
        nextIdx = tyId;
        entityTy = EntityType::EXTEND;
        entityName = mangled.substr(idx, nextIdx - idx);
        entities.push_back(std::make_unique<ExtendEntity>(ExtendEntity(entityName, entityTy,
            std::move(baseTy[0]), fileId, localId)));
    }
    return std::tuple{nextIdx, entityTy, entityName, false};
}

/**
 * Get Entities of path.
 * Complying with the mangle rule of the entity.
 */
std::vector<std::unique_ptr<Entity>> TryParsePath(std::string& mangled, std::string& rest, bool& isCompressed)
{
    const size_t n = mangled.size();
    std::vector<std::unique_ptr<Entity>> entities;
    size_t idx = 0;
    while (idx < n) {
        char ch = mangled[idx];
        size_t nextIdx = idx;
        EntityType entityTy = EntityType::ERROR_ENTITY_TYPE;
        std::string entityName = "";
        if (ch == MANGLE_ANONYMOUS_VARIABLE_PREFIX[0]) {
            nextIdx = ForwardNumber(mangled, nextIdx + MANGLE_CHAR_LEN);
            entityTy = nextIdx == idx + MANGLE_CHAR_LEN ? EntityType::ERROR_ENTITY_TYPE : EntityType::ANONYMOUS;
            nextIdx = nextIdx == idx + MANGLE_CHAR_LEN ? idx : nextIdx;
        } else if (isdigit(ch) || IsSpecialName(mangled, idx, MangleUtils::OPERATOR_TYPE_MANGLE) ||
            IsPropName(mangled, idx)) {
            bool exit = false;
            std::tie(nextIdx, entityTy, entityName, exit) = TryNamePrefixPath(mangled, isCompressed, entities, idx);
            if (exit) {
                return entities;
            }
        } else if (ch == MANGLE_EXTEND_PREFIX[0]) {
            bool exit = false;
            std::tie(nextIdx, entityTy, entityName, exit) = TryExtendPath(mangled, isCompressed, entities, idx);
            if (exit) {
                return entities;
            }
        } else if (ch == MANGLE_LAMBDA_PREFIX[0]) {
            std::tie(nextIdx, entityTy) = TryLambdaPath(mangled, idx);
        } else if (IsSpecialName(mangled, idx, MangleUtils::STD_PKG_MANGLE)) {
            nextIdx = ForwardPackageName(mangled, idx, isCompressed);
            entityTy = EntityType::PACKAGE;
            entityName = mangled.substr(idx, MANGLE_PREFIX_LEN);
        }

        if (idx != nextIdx && entityTy != EntityType::ERROR_ENTITY_TYPE) {
            if (IsGeneralEntity(entityTy)) {
                std::string mangledName = mangled.substr(idx, nextIdx - idx);
                entities.push_back(std::make_unique<Entity>(Entity(mangledName, entityTy)));
            }
            idx = nextIdx;
        } else {
            break;
        }
    }
    if (idx < n) {
        rest = mangled.substr(idx);
    }
    return entities;
}

/**
 * Generate a tree for var decl.
 * Iterates through the generated index mapping, and compresses mangled name.
 */
bool SpanningVarDeclTree(std::vector<std::unique_ptr<Entity>>& entities, std::string& mangled,
    std::string& compressed)
{
    bool isCompressed = false;
    std::unordered_map<std::string, size_t> treeIdMap;
    size_t mid = 0;
    for (const auto& entity : entities) {
        RecursionEntity(entity, treeIdMap, mid, compressed);
    }
    if (mangled.size() > 0 && isdigit(mangled[0])) {
        size_t idx = ForwardName(mangled, isCompressed);
        if (isCompressed) {
            return true;
        }
        std::string name = mangled.substr(0, idx);
        RecursionHelper(name, treeIdMap, mid, compressed, false);
        if (idx < mangled.size()) {
            compressed += mangled.substr(idx);
        }
    } else if (mangled.size() > 0 && !isdigit(mangled[0])) {
        compressed += mangled;
    }
    return false;
}

/**
 * Generate a tree for sub func decl.
 */
std::tuple<bool, size_t, std::string> SpanningFuncSubTree(std::unordered_map<std::string, size_t>& treeIdMap,
    std::string& mangled, std::string compressed, size_t idx, size_t& mid, bool& isCompressed)
{
    const size_t n = mangled.size();
    if (idx < n && mangled[idx] == MANGLE_GENERIC_PREFIX[0]) {
        compressed += MANGLE_GENERIC_PREFIX;
        std::vector<std::unique_ptr<CJType>> genericsTys;
        size_t nextIdx = ForwardTypes(mangled, genericsTys, isCompressed, idx + MANGLE_CHAR_LEN);
        if (isCompressed) {
            return std::tuple{true, idx, compressed};
        }
        if (nextIdx != idx + MANGLE_CHAR_LEN) { idx = nextIdx; }
        for (const auto& ty : genericsTys) {
            RecursionType(ty, treeIdMap, mid, compressed, false);
        }
    }
    if (idx < n && mangled[idx] == MANGLE_FUNC_PARAM_TYPE_PREFIX[0]) {
        std::vector<std::unique_ptr<CJType>> paramTys;
        size_t nextIdx = ForwardTypes(mangled, paramTys, isCompressed, idx + MANGLE_CHAR_LEN);
        if (isCompressed) {
            return std::tuple{true, idx, compressed};
        }
        auto tu = std::tie(nextIdx, paramTys, treeIdMap, mid);
        RecursionTypes(idx, MANGLE_FUNC_PARAM_TYPE_PREFIX[0], compressed, tu);
    }
    return std::tuple{false, idx, compressed};
}

/**
 * Generate a tree for func decl.
 * Iterates through the generated index mapping, and compresses mangled name.
 */
bool SpanningFuncDeclTree(std::vector<std::unique_ptr<Entity>>& entities, std::string& mangled,
    std::string& compressed)
{
    bool isCompressed = false;
    size_t mid = 0;
    const size_t n = mangled.size();
    std::unordered_map<std::string, size_t> treeIdMap;
    for (const auto& entity : entities) {
        RecursionEntity(entity, treeIdMap, mid, compressed);
    }
    if (n == 0 || (n > 0 && mangled[0] == MANGLE_LAMBDA_PREFIX[0])) {
        compressed += mangled;
        return false;
    }
    size_t idx = 0;
    if (isdigit(mangled[idx]) || IsSpecialName(mangled, idx, MangleUtils::OPERATOR_TYPE_MANGLE) ||
        IsPropName(mangled, idx)) {
        idx = ForwardName(mangled, isCompressed);
        if (isCompressed) {
            return true;
        }
        std::string funcName = mangled.substr(0, idx);
        if (idx != 0) {
            RecursionHelper(mangled, treeIdMap, mid, compressed, true);
            RecursionHelper(funcName, treeIdMap, mid, compressed, false);
        }
    }
    bool isFunc = false;
    std::tie(isFunc, idx, compressed) = SpanningFuncSubTree(treeIdMap, mangled, compressed, idx, mid, isCompressed);
    if (idx < n) {
        compressed += mangled.substr(idx);
    }
    return false;
}

/**
 * Generate a tree for default param func decl.
 * Iterates through the generated index mapping, and compresses mangled name.
 */
bool SpanningDefaultParamFuncDeclTree(std::vector<std::unique_ptr<Entity>>& entities, std::string& mangled,
    std::string& compressed)
{
    bool isCompressed = false;
    const size_t n = mangled.size();
    size_t mid = 0;
    std::unordered_map<std::string, size_t> treeIdMap;
    for (const auto& entity : entities) {
        RecursionEntity(entity, treeIdMap, mid, compressed);
    }
    if (n == 0) {
        return false;
    }
    size_t idx = 0;
    if (isdigit(mangled[idx]) || IsSpecialName(mangled, idx, MangleUtils::OPERATOR_TYPE_MANGLE) ||
        IsPropName(mangled, idx)) {
        idx = ForwardName(mangled, isCompressed);
        if (isCompressed) {
            return true;
        }
        std::string funcName = mangled.substr(0, idx);
        if (idx != 0) {
            RecursionHelper(mangled, treeIdMap, mid, compressed, true);
            RecursionHelper(funcName, treeIdMap, mid, compressed, false);
        }
        bool isFunc = false;
        std::tie(isFunc, idx, compressed) = SpanningFuncSubTree(treeIdMap, mangled, compressed, idx, mid, isCompressed);
    }
    if (idx < n) {
        compressed += mangled.substr(idx);
    }
    return false;
}

/**
 * Help RecursionEntity achieve compressed generation.
 * The meaning of the isReplaced is whether it is added to the suffix of the compressed.
 */
bool RecursionHelper(std::string& name, std::unordered_map<std::string, size_t>& treeIdMap, size_t& mid,
    std::string& compressed, bool isReplaced, bool isLeaf)
{
    if (name.empty()) {
        return false;
    }
    if (treeIdMap.find(name) == treeIdMap.end()) {
        treeIdMap.emplace(name, mid++);
        if (!isReplaced && isLeaf) {
            compressed += name;
        }
    } else {
        if (!isReplaced) {
            std::string cid = MANGLE_COMPRESSED_PREFIX + DecimalToManglingNumber(std::to_string(treeIdMap.at(name)));
            if (name.size() > cid.size()) {
                compressed += cid;
            } else {
                compressed += name;
            }
            return true;
        }
    }
    return false;
}

/**
 * Generate the treeIdMap and obtain the compressed name.
 * Compression of a generic func entity.
 */
std::string RecursionFuncEntity(const std::unique_ptr<Entity>& entity,
    std::unordered_map<std::string, size_t>& treeIdMap, size_t& mid, std::string compressed,
    std::tuple<bool, bool>& exInfo)
{
    // The subscript indexes of the exInfo are as follows:
    // { 0: isGeneric, 1: isReplaced }
    bool isCompressed = false;
    bool isGeneric = std::get<0>(exInfo);
    bool isReplaced = std::get<1>(exInfo);
    size_t idx = ForwardName(entity->mangledName, isCompressed);
    std::string funcName = entity->mangledName.substr(0, idx);
    RecursionHelper(funcName, treeIdMap, mid, compressed, isReplaced);
    FunctionEntity* fe = static_cast<FunctionEntity*>(entity.get());
    if (isGeneric) {
        if (!fe->genericTys.empty()) {
            compressed += MANGLE_GENERIC_PREFIX;
        }
        for (const auto& gty : fe->genericTys) {
            RecursionType(gty, treeIdMap, mid, compressed, isReplaced);
        }
    }
    if (!fe->paramTys.empty()) {
        compressed += MANGLE_FUNC_PARAM_TYPE_PREFIX;
    }
    for (const auto& pty : fe->paramTys) {
        RecursionType(pty, treeIdMap, mid, compressed, isReplaced);
    }
    if (!fe->paramTys.empty()) {
        compressed += MANGLE_SUFFIX;
    }
    return compressed;
}

/**
 * Generate the treeIdMap and obtain the compressed name.
 * Compression of an entity.
 */
void RecursionEntity(const std::unique_ptr<Entity>& entity, std::unordered_map<std::string, size_t>& treeIdMap,
    size_t& mid, std::string& compressed)
{
    bool isCompressed = false;
    bool isReplaced = false;
    if (entity->mangledName.empty()) { return; }
    if (entity->entityTy == EntityType::PACKAGE || (entity->entityTy >= EntityType::GENERAL_FUNCTION &&
        entity->entityTy <= EntityType::EXTEND)) {
        isReplaced = RecursionHelper(entity->mangledName, treeIdMap, mid, compressed, false, false);
        if (isReplaced) {
            return;
        }
    }
    switch (entity->entityTy) {
        case EntityType::NAME:
        case EntityType::LAMBDA:
        case EntityType::ANONYMOUS:
            RecursionHelper(entity->mangledName, treeIdMap, mid, compressed, false);
            break;
        case EntityType::PACKAGE: {
            size_t idx = ForwardUnitPackageName(entity->mangledName, 0, isCompressed);
            std::string pkg = entity->mangledName.substr(0, idx);
            RecursionHelper(pkg, treeIdMap, mid, compressed, isReplaced);
            std::string fid = entity->mangledName.substr(idx);
            RecursionHelper(fid, treeIdMap, mid, compressed, isReplaced);
            break;
        }
        case EntityType::GENERAL_FUNCTION: {
            std::tuple<bool, bool> exInfo(false, isReplaced);
            compressed = RecursionFuncEntity(entity, treeIdMap, mid, compressed, exInfo);
            break;
        }
        case EntityType::GENERIC_FUNCTION: {
            std::tuple<bool, bool> exInfo(true, isReplaced);
            compressed = RecursionFuncEntity(entity, treeIdMap, mid, compressed, exInfo);
            break;
        }
        case EntityType::GENERIC_DATA: {
            size_t idx = ForwardName(entity->mangledName, isCompressed);
            std::string name = entity->mangledName.substr(0, idx);
            RecursionHelper(name, treeIdMap, mid, compressed, isReplaced);
            DataEntity* de = static_cast<DataEntity*>(entity.get());
            if (!de->genericTys.empty()) {
                compressed += MANGLE_GENERIC_PREFIX;
            }
            for (const auto& gty : de->genericTys) {
                RecursionType(gty, treeIdMap, mid, compressed, isReplaced);
            }
            if (!de->genericTys.empty()) {
                compressed += MANGLE_SUFFIX;
            }
            break;
        }
        case EntityType::EXTEND: {
            ExtendEntity* ee = static_cast<ExtendEntity*>(entity.get());
            compressed += MANGLE_EXTEND_PREFIX;
            RecursionType(ee->extendTy, treeIdMap, mid, compressed, isReplaced);
            RecursionHelper(ee->fileId, treeIdMap, mid, compressed, isReplaced);
            RecursionHelper(ee->localId, treeIdMap, mid, compressed, isReplaced);
            break;
        }
        default:
            RecursionHelper(entity->mangledName, treeIdMap, mid, compressed, false);
            break;
    }
}

/**
 * Generate the treeIdMap and obtain the compressed name.
 * Compression of type.
 */
void RecursionType(const std::unique_ptr<CJType>& ty, std::unordered_map<std::string, size_t>& treeIdMap, size_t& mid,
    std::string& compressed, bool isReplaced)
{
    if (isReplaced || ty->mangledName.empty()) {
        return;
    }
    if ((ty->baseTy >= BaseType::RAWARRAY_TYPE && ty->baseTy <= BaseType::CPOINTER_TYPE) ||
        (ty->baseTy >= BaseType::ENUM_TYPE && ty->baseTy <= BaseType::FUNCTION_TYPE)) {
        isReplaced = RecursionHelper(ty->mangledName, treeIdMap, mid, compressed, isReplaced, false);
        if (isReplaced) {
            return;
        }
    }
    bool isCompressed = false;
    switch (ty->baseTy) {
        case BaseType::PRIMITIVE_TYPE:
            compressed += ty->mangledName;
            break;
        case BaseType::CSTRING_TYPE:
        case BaseType::GENERIC_TYPE:
            RecursionHelper(ty->mangledName, treeIdMap, mid, compressed, isReplaced);
            break;
        case BaseType::RAWARRAY_TYPE:
        case BaseType::VARRAY_TYPE: {
            size_t idx = ForwardNumber(ty->mangledName, MANGLE_CHAR_LEN);
            compressed += ty->mangledName.substr(0, idx);
            std::vector<std::unique_ptr<CJType>> vty;
            ForwardType(ty->mangledName, vty, isCompressed, idx);
            RecursionType(vty[0], treeIdMap, mid, compressed, isReplaced);
            break;
        }
        case BaseType::TUPLE_TYPE: {
            size_t idx = ForwardNumber(ty->mangledName, MANGLE_CHAR_LEN);
            compressed += ty->mangledName.substr(0, idx);
            TupleType* tty = static_cast<TupleType*>(ty.get());
            for (const auto& param : tty->elementTys) {
                RecursionType(param, treeIdMap, mid, compressed, isReplaced);
            }
            compressed += MANGLE_SUFFIX;
            break;
        }
        case BaseType::CPOINTER_TYPE: {
            compressed += MANGLE_POINTER_PREFIX;
            std::vector<std::unique_ptr<CJType>> pty;
            ForwardType(ty->mangledName, pty, isCompressed, MANGLE_CHAR_LEN);
            RecursionType(pty[0], treeIdMap, mid, compressed, isReplaced);
            break;
        }
        case BaseType::ENUM_TYPE:
        case BaseType::STRUCT_TYPE:
        case BaseType::CLASS_TYPE: {
            compressed += ty->mangledName.substr(0, MANGLE_PREFIX_LEN);
            CompositeType* ct = static_cast<CompositeType*>(ty.get());
            size_t idx = ForwardPackageName(ct->pkg, 0, isCompressed);
            std::string pkg = ct->pkg.substr(0, idx);
            RecursionHelper(pkg, treeIdMap, mid, compressed, isReplaced);
            if (idx < ct->pkg.size()) {
                std::string fid = ct->pkg.substr(idx);
                RecursionHelper(fid, treeIdMap, mid, compressed, isReplaced);
            }
            RecursionHelper(ct->name, treeIdMap, mid, compressed, isReplaced);
            if (!ct->genericTys.empty()) {
                compressed += MANGLE_GENERIC_PREFIX;
            }
            for (const auto& gty : ct->genericTys) {
                RecursionType(gty, treeIdMap, mid, compressed, isReplaced);
            }
            if (!isReplaced) {
                compressed += MANGLE_SUFFIX;
            }
            break;
        }
        case BaseType::FUNCTION_TYPE: {
            compressed += ty->mangledName.substr(0, MANGLE_PREFIX_LEN);
            auto* ft = static_cast<FunctionType*>(ty.get());
            RecursionType(ft->retTy, treeIdMap, mid, compressed, isReplaced);
            for (const auto& param : ft->paramTys) {
                RecursionType(param, treeIdMap, mid, compressed, isReplaced);
            }
            compressed += MANGLE_SUFFIX;
            break;
        }
        default:
            RecursionHelper(ty->mangledName, treeIdMap, mid, compressed, false);
            break;
    }
}
}  // namespace Cangjie::Compression