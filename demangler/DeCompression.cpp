// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.


#include "DeCompression.h"
#ifdef BUILD_LIB_CANGJIE_DEMANGLE // To reuse the code to compile CangjieDemangle.cpp
#include <cstring>
#include <string>

#include "StdString.h"
#else
#include "Base/CString.h"
#endif
#include "Utils.h"

namespace {
constexpr char MANGLE_COMPRESS_PREFIX = 'Y';
constexpr char MANGLE_ANONYMOUS_PREFIX = '0';
constexpr char MANGLE_AT_PREFIX = '@';
constexpr size_t PREFIX_LEN = 2;

const std::unordered_map<char, size_t> base62Chars = {{'0', 0}, {'1', 1}, {'2', 2}, {'3', 3}, {'4', 4}, {'5', 5},
    {'6', 6}, {'7', 7}, {'8', 8}, {'9', 9}, {'A', 10}, {'B', 11}, {'C', 12}, {'D', 13}, {'E', 14}, {'F', 15},
    {'G', 16}, {'H', 17}, {'I', 18}, {'J', 19}, {'K', 20}, {'L', 21}, {'M', 22}, {'N', 23}, {'O', 24}, {'P', 25},
    {'Q', 26}, {'R', 27}, {'S', 28}, {'T', 29}, {'U', 30}, {'V', 31}, {'W', 32}, {'X', 33}, {'Y', 34}, {'Z', 35},
    {'a', 36}, {'b', 37}, {'c', 38}, {'d', 39}, {'e', 40}, {'f', 41}, {'g', 42}, {'h', 43}, {'i', 44}, {'j', 45},
    {'k', 46}, {'l', 47}, {'m', 48}, {'n', 49}, {'o', 50}, {'p', 51}, {'q', 52}, {'r', 53}, {'s', 54}, {'t', 55},
    {'u', 56}, {'v', 57}, {'w', 58}, {'x', 59}, {'y', 60}, {'z', 61}};

const char PRIMITIVE_PREFIX_SET[] = "nucbfdasilqhtjmrDv";
constexpr size_t PRIMITIVE_LEN = 18;
} // namespace

namespace Cangjie {
// Get identifier without prefix "@"
template<typename T>
inline size_t StripCangjieAt(T& identifier, size_t idx)
{
    if (idx < identifier.Length() && identifier[idx] == MANGLE_AT_PREFIX) {
        return idx + MANGLE_CHAR_LEN;
    }
    return idx;
}

// Get identifier without prefix "_C" or "_CV"
template<typename T>
inline size_t StripCangjiePrefix(T& identifier, size_t idx)
{
    if (idx + MANGLE_CHAR_LEN < identifier.Length() && identifier[idx] == MANGLE_UNDERSCORE_PREFIX &&
        identifier[idx + MANGLE_CHAR_LEN] == 'C') {
        if(idx + MANGLE_CHAR_LEN  * 2 < identifier.Length() && identifier[idx + MANGLE_CHAR_LEN  * 2] == 'V') {
            return idx + PREFIX_LEN + MANGLE_CHAR_LEN;
        } else {
            return idx + PREFIX_LEN;
        }
    }
    return idx;
}

// Get decimal number from base 62 number.
template<typename T>
inline size_t GetNumber(T base62)
{
    const size_t n = 62;
    // _ is the end of base 62 number.
    if (base62 == "_") {
        return 0;
    }
    T newBase62 = base62.SubStr(0, base62.Length() - 1);
    long long decimal = 0;
    long long power = 1;
    for (size_t i = newBase62.Length() - 1; i != NPOS; i--) {
        decimal += base62Chars.at(newBase62[i]) * power;
        power *= n;
    }
    return decimal + 1;
}

template<typename T>
bool DeCompression<T>::IsGlobalEncode(T& mangled)
{
    // "GV"/"GP"/"GF" is the global-mangled prefix.
    if (IsSamePrefix(mangled, "GV", 0) ||
        IsSamePrefix(mangled, "GP", 0) ||
        IsSamePrefix(mangled, "GF", 0)) {
        return true;
    }
    return false;
}

template<typename T>
bool DeCompression<T>::IsSamePrefix(T& first, T second, size_t idx)
{
    for (size_t i = idx; i < idx + PREFIX_LEN; i++) {
        if (first[i] != second[i]) {
            return false;
        }
    }
    return true;
}

template<typename T>
inline bool IsStdPkgName(T& mangled, size_t idx)
{
    if (idx + MANGLE_CHAR_LEN >= mangled.Length()) { return false; }
    if (MANGLE_STDPKG_MAP.find(mangled.SubStr(idx, SPECIAL_NAME_LEN).Str()) != MANGLE_STDPKG_MAP.end()) {
        return true;
    }
    return false;
}

template<typename T>
inline bool IsOperatorName(T& mangled, size_t idx)
{
    if (idx + MANGLE_CHAR_LEN >= mangled.Length()) { return false; }
    if (!((mangled[idx] >= 'a' && mangled[idx] <= 'z') &&
        (mangled[idx + MANGLE_CHAR_LEN] >= 'a' && mangled[idx + MANGLE_CHAR_LEN] <= 'z'))) {
        return false;
    }
    // Move left by 5 digits is the way of calculating OperatorKind
    size_t t = ((static_cast<size_t>(mangled[idx]) - static_cast<size_t>('a')) << 5) +
                 static_cast<size_t>(mangled[idx + MANGLE_CHAR_LEN]) - static_cast<size_t>('a');
    OperatorKind result = static_cast<OperatorKind>(t);
    switch (result) {
        case OperatorKind::IX:
        case OperatorKind::NT:
        case OperatorKind::NG:
        case OperatorKind::PW:
        case OperatorKind::ML:
        case OperatorKind::DV:
        case OperatorKind::RM:
        case OperatorKind::PL:
        case OperatorKind::LS:
        case OperatorKind::RS:
        case OperatorKind::LT:
        case OperatorKind::GT:
        case OperatorKind::LE:
        case OperatorKind::GE:
        case OperatorKind::EQ:
        case OperatorKind::NE:
        case OperatorKind::AN:
        case OperatorKind::EO:
        case OperatorKind::OR:
        case OperatorKind::CL:
            return true;
        default: {
            return false;
        }
    }
}

/**
 * Determine whether the code is a var decl code.
 * _CN<path><name>E
 * _CN<path><name>K<number>E
 *
 */
template<typename T>
bool DeCompression<T>::IsVarDeclEncode(T& mangled)
{
    this->isRecord = false;
    const size_t n = mangled.Length();
    size_t idx = 0;
    if (mangled[0] == END) {
        return true;
    } else if (mangled[0] == MANGLE_COMPRESS_PREFIX) {
        idx = ForwardNumber(mangled, idx + MANGLE_CHAR_LEN);
        if (idx < n && mangled[idx] == END) {
            return true;
        }
    } else if (isdigit(mangled[0])) {
        idx = ForwardName(mangled);
        if (idx < n && mangled[idx] == MANGLE_COUNT_PREFIX) {
            idx = ForwardNumber(mangled, idx + MANGLE_CHAR_LEN);
            if (idx < n && mangled[idx] == END) {
                return true;
            } else {
                return false;
            }
        } else if (idx < n && mangled[idx] == END) {
            return true;
        }
    } else if (mangled[0] == MANGLE_COUNT_PREFIX) {
        idx = ForwardNumber(mangled, idx + MANGLE_CHAR_LEN);
        if (idx < n && mangled[idx] == END) {
            return true;
        }
    }
    return false;
}

template<typename T>
bool DeCompression<T>::IsDefaultParamFuncEncode(T& mangled)
{
    if (IsSamePrefix(mangled, "PI", 0)) {
        return true;
    }
    return false;
}

inline bool IsPrimitiveType(char ch)
{
    for (size_t i = 0; i < PRIMITIVE_LEN; i++) {
        if (ch == PRIMITIVE_PREFIX_SET[i]) {
            return true;
        }
    }
    return false;
}

template<typename T>
inline bool IsPropName(T& mangled, size_t idx)
{
    if (idx + MANGLE_CHAR_LEN >= mangled.Length()) {
        return false;
    }
    // The ps/pg is prop mangled name.
    if (mangled[idx] == 'p' && (mangled[idx + MANGLE_CHAR_LEN] == 's' || mangled[idx + MANGLE_CHAR_LEN] == 'g')) {
        return true;
    }
    return false;
}

template<typename T>
inline size_t ForwardPrimitiveType(T& mangled, size_t idx)
{
    // The Dh is primitive type.
    if (mangled[idx] != 'D' && IsPrimitiveType(mangled[idx])) {
        return idx + MANGLE_CHAR_LEN;
    }
    if (idx + MANGLE_CHAR_LEN < mangled.Length() && mangled[idx] == 'D' && mangled[idx + MANGLE_CHAR_LEN] == 'h') {
        return idx + SPECIAL_NAME_LEN;
    }
    return idx;
}

template<typename T>
bool DeCompression<T>::HasDuplicates(T& mangled, std::tuple<size_t, size_t>& pos)
{
    // The subscript indexes of the pos are as follows:
    // { 0: start, 1: length }
    T posStr = mangled.SubStr(std::get<0>(pos), std::get<1>(pos));
    for (auto it = this->treeIdMap.begin(); it != this->treeIdMap.end(); ++it) {
        T itStr = mangled.SubStr(std::get<0>(*it), std::get<1>(*it));
        if (itStr.IsEmpty()) { continue; }
        if (itStr == posStr) {
            return true;
        }
    }
    return false;
}

template<typename T>
bool DeCompression<T>::HasDuplicates(T& mangled, size_t mid)
{
    // The subscript indexes of the treeIdMap are as follows:
    // { 0: index, 1: pos }
    T curStr = mangled.SubStr(std::get<0>(treeIdMap[mid]), std::get<1>(treeIdMap[mid]));
    size_t tid = 0;
    for (auto it = treeIdMap.begin(); it != treeIdMap.end(); ++it, ++tid) {
        if (tid == mid) { continue; }
        T itStr = mangled.SubStr(std::get<0>(*it), std::get<1>(*it));
        if (itStr.IsEmpty()) { continue; }
        if (itStr == curStr) {
            return true;
        }
    }
    return false;
}

/**
 * Forward filename number.
 *        if filename length < 12, then <filename>$
 *        else then <hashcode>, the length of <hashcode> is 13
 */
template<typename T>
inline size_t ForwardFileNameNumber(T& mangled, size_t idx)
{
    size_t curIdx = idx;
    bool isValid = false;
    while (true) {
        if (curIdx >= mangled.Length()) {
            return idx;
        }
        if (mangled[curIdx] == MANGLE_END) {
            return ++curIdx;
        }
        if (curIdx + 1 - idx == FILE_HASH_LEN) {
            isValid = true;
            break;
        }
        curIdx++;
    }
    if (isValid) {
        for (int i = idx; i <= static_cast<int>(curIdx); i++) {
            if (!isalnum(mangled[i])) {
                return idx;
            }
        }
        return ++curIdx;
    }
    return idx;
}

template<typename T>
size_t DeCompression<T>::ForwardPackageName(T& mangled, size_t& cnt, size_t idx)
{
    size_t curIdx = idx;
    if (IsStdPkgName(mangled, curIdx)) {
        curIdx += SPECIAL_NAME_LEN;
    } else {
        curIdx = ForwardName(mangled, idx);
    }
    if (this->isRecord && curIdx != idx) {
        std::tuple<size_t, size_t> pos(idx, curIdx - idx);
        bool isPush = TreeIdMapPushBack(mangled, pos);
        cnt = isPush ? cnt + 1 : cnt;
    }
    if (curIdx < mangled.Length() && mangled[curIdx] == MANGLE_FILE_ID_PREFIX) {
        size_t fid = ForwardFileNameNumber(mangled, curIdx + MANGLE_CHAR_LEN);
        if (this->isRecord && fid != curIdx + MANGLE_CHAR_LEN) {
            std::tuple<size_t, size_t> pos(curIdx, fid - curIdx);
            bool isPush = TreeIdMapPushBack(mangled, pos);
            cnt = isPush ? cnt + 1 : cnt;
        }
        curIdx = fid;
    }

    return curIdx;
}

template<typename T>
size_t DeCompression<T>::ForwardGenericTypes(T& mangled, size_t& cnt, size_t idx)
{
    size_t curCnt = cnt;
    size_t curIdx = ForwardTypes(mangled, cnt, idx + MANGLE_CHAR_LEN);
    if (curIdx != idx && mangled[curIdx] == END) {
        return curIdx + MANGLE_CHAR_LEN;
    }
    if (this->isRecord) {
        TreeIdMapPop(cnt, curCnt);
    }
    return idx;
}

/**
 * Forward class type, CN<package><name>E.
 *                     CN<package><name>I<type>+E
 */
template<typename T>
size_t DeCompression<T>::ForwardClassType(T& mangled, size_t& cnt, size_t idx)
{
    size_t curIdx = idx;
    size_t curCnt = cnt;
    // Skip 'CN' 'RN' 'NN'
    if (idx + PREFIX_LEN < mangled.Length() && (mangled[idx] == 'C' || mangled[idx] == 'R' || mangled[idx] == 'N')
        && mangled[idx + MANGLE_CHAR_LEN] == MANGLE_NESTED_PREFIX[0]) {
        curIdx += PREFIX_LEN;
    } else {
        return idx;
    }
    UpdateCompressedName(mangled, curIdx);
    curIdx = ForwardPackageName(mangled, cnt, curIdx);
    size_t fid = curIdx;
    UpdateCompressedName(mangled, fid);
    curIdx = ForwardName(mangled, fid);
    if (this->isRecord && curIdx != fid) {
        std::tuple<size_t, size_t> pos(fid, curIdx - fid);
        bool isPush = TreeIdMapPushBack(mangled, pos);
        cnt = isPush ? cnt + 1 : cnt;
    }
    if (mangled[curIdx] == MANGLE_GENERIC_PREFIX) {
        size_t genericIdx = ForwardGenericTypes(mangled, cnt, curIdx);
        if (curIdx != genericIdx) {
            return genericIdx;
        }
    } else if (mangled[curIdx] == END) {
        return curIdx + MANGLE_CHAR_LEN;
    }
    if (this->isRecord) {
        TreeIdMapPop(cnt, curCnt);
    }
    return idx;
}

/**
 * Forward function type, include general function and C function.
 *         F0<return-type><param-types>E
 *         FC<return-type><param-types>E
 */
template<typename T>
size_t DeCompression<T>::ForwardFunctionType(T& mangled, size_t& cnt, size_t idx)
{
    size_t curCnt = cnt;
    size_t curIdx = ForwardType(mangled, cnt, idx + PREFIX_LEN);
    curIdx = ForwardTypes(mangled, cnt, curIdx);
    if (curIdx != idx && mangled[curIdx] == END) {
        return curIdx + MANGLE_CHAR_LEN;
    }
    if (this->isRecord) {
        TreeIdMapPop(cnt, curCnt);
    }
    return idx;
}

// Forward tuple type, T<number><type>+E.
template<typename T>
size_t DeCompression<T>::ForwardTupleType(T& mangled, size_t& cnt, size_t idx)
{
    size_t curIdx = ForwardNumber(mangled, idx + MANGLE_CHAR_LEN);
    if (curIdx == idx + MANGLE_CHAR_LEN || curIdx >= mangled.Length() || idx + MANGLE_CHAR_LEN >= mangled.Length()) {
        return idx;
    }
    size_t curCnt = cnt;
    size_t tyCnt = GetNumber(mangled.SubStr(idx + MANGLE_CHAR_LEN, curIdx - idx - MANGLE_CHAR_LEN));
    size_t endIdx = curIdx;
    while (endIdx < mangled.Length() && mangled[endIdx] != END && tyCnt) {
        size_t tyIdx = ForwardType(mangled, cnt, endIdx);
        if (tyIdx == endIdx) {
            break;
        }
        endIdx = tyIdx;
        --tyCnt;
    }
    if (curIdx != endIdx && mangled[endIdx] == END) {
        return endIdx + MANGLE_CHAR_LEN;
    }
    if (this->isRecord) {
        TreeIdMapPop(cnt, curCnt);
    }
    return idx;
}

// Forward CPointer type, P<type>.
template<typename T>
size_t DeCompression<T>::ForwardCPointer(T& mangled, size_t& cnt, size_t idx)
{
    size_t curCnt = cnt;
    size_t curIdx = ForwardType(mangled, cnt, idx + MANGLE_CHAR_LEN);
    if (curIdx != idx) {
        return curIdx;
    }
    if (this->isRecord) {
        TreeIdMapPop(cnt, curCnt);
    }
    return idx;
}

/**
 * Forward RawArray type or VArray type.
 *                  A<number><type>
 *                  V<number><type>
 */
template<typename T>
size_t DeCompression<T>::ForwardArrayType(T& mangled, size_t& cnt, size_t idx)
{
    size_t curCnt = cnt;
    size_t curIdx = idx + MANGLE_CHAR_LEN;
    if (curIdx < mangled.Length()) {
        curIdx = ForwardNumber(mangled, curIdx);
        if (curIdx < mangled.Length()) {
            size_t endIdx = ForwardType(mangled, cnt, curIdx);
            if (endIdx != curIdx) {
                return endIdx;
            }
        }
    }
    if (this->isRecord) {
        TreeIdMapPop(cnt, curCnt);
    }
    return idx;
}

// Forward generic type, G<identifier-length><identifier>.
template<typename T>
size_t DeCompression<T>::ForwardGenericType(T& mangled, size_t& cnt, size_t idx)
{
    size_t nextIdx = ForwardNumber(mangled, idx + MANGLE_CHAR_LEN);
    if (nextIdx != idx + MANGLE_CHAR_LEN) {
        return nextIdx;
    }
    return idx;
}

template<typename T>
size_t DeCompression<T>::ForwardNumber(T& mangled, size_t idx)
{
    size_t curIdx = idx;
    while (curIdx < mangled.Length() && isalnum(mangled[curIdx])) {
        curIdx++;
    }
    if (curIdx < mangled.Length() && mangled[curIdx] == MANGLE_UNDERSCORE_PREFIX) {
        return curIdx + MANGLE_CHAR_LEN;
    }
    return idx;
}

/**
 * Forward name:
 * 1. operator name
 * 2. prop name
 * 3. general name -- <identifier-length><identifier>
 */
template<typename T>
size_t DeCompression<T>::ForwardName(T& mangled, size_t idx)
{
    if (idx >= mangled.Length()) {
        return idx;
    }
    if (IsOperatorName(mangled, idx) || IsPropName(mangled, idx)) {
        return idx + SPECIAL_NAME_LEN;
    }
    if (!isdigit(mangled[idx])) {
        return idx;
    }
    if (mangled[idx] == MANGLE_ANONYMOUS_PREFIX) { return idx + MANGLE_CHAR_LEN; }
    size_t numberLen = 0;
    while (idx < mangled.Length() && isdigit(mangled[idx + numberLen])) {
        numberLen++;
    }
    size_t number = atoi(mangled.SubStr(idx, numberLen).Str());
    return idx + numberLen + number;
}

template<typename T>
void DeCompression<T>::TreeIdMapPop(size_t& from, size_t to)
{
    while (from > to) {
        this->treeIdMap.pop_back();
        from--;
    }
}

template<typename T>
T DeCompression<T>::TreeIdMapErase(T& mangled, size_t& cnt, size_t entityId, size_t sid)
{
    treeIdMap.erase(treeIdMap.begin() + entityId);
    UpdateCompressedName(mangled, sid, mangled.Length());
    cnt--;
    return mangled;
}

template<typename T>
bool DeCompression<T>::TreeIdMapPushBack(T& mangled, std::tuple<size_t, size_t>& pos)
{
    if (std::get<1>(pos) == 0) {
        this->treeIdMap.push_back(pos);
        return true;
    } else if (std::get<1>(pos) != 0 && !HasDuplicates(mangled, pos)) {
        this->treeIdMap.push_back(pos);
        return true;
    }
    return false;
}

template<typename T>
void DeCompression<T>::TreeIdMapAssign(T& mangled, T& mangledCopy, size_t mapId, size_t& cnt,
    std::tuple<size_t, size_t>& eleInfo)
{
    // The subscript indexes of the eleInfo are as follows:
    // { 0: idx, 1: nextIdx }
    size_t idx = std::get<0>(eleInfo);
    size_t nextIdx = std::get<1>(eleInfo);
    std::get<1>(treeIdMap[mapId]) = nextIdx - idx;
    if (HasDuplicates(mangled, mapId)) {
        TreeIdMapErase(mangledCopy, cnt, mapId, idx);
        mangled = mangledCopy;
    }
}

template<typename T>
size_t DeCompression<T>::ForwardType(T& mangled, size_t& cnt, size_t idx)
{
    char ch = mangled[idx];
    size_t nextIdx = idx;
    size_t curCnt = cnt;
    T mangledCopy = mangled;
    // Record current treeIdMap's index after treeIdMap pushed type
    size_t mapId = this->treeIdMap.size();
    if (this->isRecord) {
        std::tuple<size_t, size_t> pos (idx, 0);
        bool isPush = TreeIdMapPushBack(mangled, pos);
        cnt = isPush ? cnt + 1 : cnt;
    }
    switch (ch) {
        case 'C':
        case 'R':
        case 'N':
            nextIdx = ForwardClassType(mangled, cnt, idx);
            break;
        case 'F': {
            // The function type prefix is F0/FC.
            if (idx + MANGLE_CHAR_LEN < mangled.Length() &&
                (mangled[idx + MANGLE_CHAR_LEN] == '0' || mangled[idx + MANGLE_CHAR_LEN] == 'C')) {
                nextIdx = ForwardFunctionType(mangled, cnt, idx);
            } else {
                nextIdx = idx;
            }
            break;
        }
        case 'T':
            nextIdx = ForwardTupleType(mangled, cnt, idx);
            break;
        case 'P':
            nextIdx = ForwardCPointer(mangled, cnt, idx);
            break;
        case 'A':
        case 'V':
            nextIdx = ForwardArrayType(mangled, cnt, idx);
            break;
        case 'k': {
            nextIdx = idx + MANGLE_CHAR_LEN;
            if (this->isRecord) {
                TreeIdMapPop(cnt, curCnt);
            }
            return nextIdx;
        }
        case 'G':
            nextIdx = ForwardGenericType(mangled, cnt, idx);
            break;
        case 'Y': {
            nextIdx = ForwardNumber(mangled, idx + MANGLE_CHAR_LEN);
            std::tuple<size_t, size_t> updateRes{0, nextIdx};
            if (nextIdx != idx) {
                updateRes = UpdateCompressedName(mangled, idx);
            }
            if (this->isRecord) {
                TreeIdMapPop(cnt, curCnt);
            }
            return std::get<0>(updateRes) == 0 ? idx : std::get<1>(updateRes);
        }
        default: {
            if (IsPrimitiveType(ch)) {
                nextIdx = ForwardPrimitiveType(mangled, idx);
                if (this->isRecord) {
                    TreeIdMapPop(cnt, curCnt);
                }
                return nextIdx;
            } else {
                nextIdx = idx;
            }
            break;
        }
    }
    if (this->isRecord && nextIdx != idx) {
        std::tuple<size_t, size_t> eleInfo(idx, nextIdx);
        TreeIdMapAssign(mangled, mangledCopy, mapId, cnt, eleInfo);
    } else if (this->isRecord && nextIdx == idx) {
        TreeIdMapPop(cnt, curCnt);
    }
    return nextIdx;
}

template<typename T>
size_t DeCompression<T>::ForwardTypes(T& mangled, size_t& cnt, size_t startId)
{
    if (mangled.Length() - startId <= 0) {
        return startId;
    }
    size_t idx = startId;
    while (idx < mangled.Length() && mangled[idx] != END) {
        size_t curIdx = ForwardType(mangled, cnt, idx);
        if (curIdx == idx) {
            return curIdx;
        }
        idx = curIdx;
    }
    return idx;
}

template<typename T>
bool DeCompression<T>::IsCompressed(T& mangled)
{
    if (mangled.IsEmpty()) {
        return false;
    }
    size_t pos = 0;
    while ((pos = mangled.Find(MANGLE_COMPRESS_PREFIX, pos)) != NPOS) {
        size_t nextIdx = ForwardNumber(mangled, pos + MANGLE_CHAR_LEN);
        if (nextIdx != pos + MANGLE_CHAR_LEN) {
            return true;
        } else {
            pos += 1;
        }
    }
    return false;
}

/**
 * Generate a tree for var decl.
 * Iterates through the generated index mapping, and decompresses mangled name.
 */
template<typename T>
void DeCompression<T>::SpanningVarDeclTree()
{
    this->isRecord = true;
    size_t idx = this->currentIndex;
    size_t change = std::get<0>(UpdateCompressedName(this->decompressed, idx));
    if (this->decompressed.Length() > 0 && isdigit(this->decompressed[idx])) {
        idx = ForwardName(this->decompressed, idx);
        if (idx != this->currentIndex && change == 0) {
            std::tuple<size_t, size_t> pos(this->currentIndex, idx - this->currentIndex);
            TreeIdMapPushBack(this->decompressed, pos);
        }
    }
    this->isRecord = false;
}

/**
 * Generate a tree for func decl.
 * Iterates through the generated index mapping, and decompresses mangled name.
 */
template<typename T>
void DeCompression<T>::SpanningFuncDeclTree()
{
    size_t idx = this->currentIndex;
    if (this->decompressed.Length() == 0 || (this->decompressed.Length() > 0 &&
        this->decompressed[idx] == MANGLE_LAMBDA_PREFIX)) {
        return;
    }
    this->isRecord = true;
    size_t change = std::get<0>(UpdateCompressedName(this->decompressed, idx));
    if (isdigit(this->decompressed[idx]) || IsOperatorName(this->decompressed, idx) ||
        IsPropName(this->decompressed, idx)) {
        idx = ForwardName(this->decompressed, idx);
        if (idx != this->currentIndex && change == 0) {
            std::tuple<size_t, size_t> funcPos(this->currentIndex, this->decompressed.Length() - this->currentIndex);
            TreeIdMapPushBack(this->decompressed, funcPos);
            std::tuple<size_t, size_t> pos(this->currentIndex, idx - this->currentIndex);
            TreeIdMapPushBack(this->decompressed, pos);
        }
    }
    change = std::get<0>(UpdateCompressedName(this->decompressed, idx));
    if (idx < this->decompressed.Length() && this->decompressed[idx] == MANGLE_COUNT_PREFIX) {
        size_t nextIdx = ForwardNumber(this->decompressed, idx + MANGLE_CHAR_LEN);
        if (nextIdx != idx + MANGLE_CHAR_LEN && change == 0) {
            std::tuple<size_t, size_t> pos(idx, nextIdx - idx);
            TreeIdMapPushBack(this->decompressed, pos);
            idx = nextIdx;
        }
    }
    if (idx < this->decompressed.Length() && this->decompressed[idx] == MANGLE_GENERIC_PREFIX) {
        size_t cnt = 0;
        size_t nextIdx = ForwardTypes(this->decompressed, cnt, idx + MANGLE_CHAR_LEN);
        if (nextIdx != idx + MANGLE_CHAR_LEN) { idx = nextIdx; }
    }
    if (idx < this->decompressed.Length() && this->decompressed[idx] == MANGLE_FUNCTION_PREFIX) {
        size_t cnt = 0;
        size_t nextIdx = ForwardTypes(this->decompressed, cnt, idx + MANGLE_CHAR_LEN);
        if (nextIdx != idx + MANGLE_CHAR_LEN) {
            idx = nextIdx;
        }
    }
    this->isRecord = false;
}


template<typename T>
T DeCompression<T>::CJMangledDeCompression(bool isType)
{
    if (!IsCompressed(this->mangledName) || this->mangledName.IsEmpty()) {
        return this->mangledName;
    }
    this->pid = 0;
    T demangled = this->mangledName.SubStr(0);
    this->pid = StripCangjieAt(demangled, this->pid);
    if (demangled.IsEmpty() || isType || !IsSamePrefix(demangled, MANGLE_CANGJIE_PREFIX, this->pid)) {
        return this->mangledName;
    }
    // Skip "_C" or "_CV"
    this->pid = StripCangjiePrefix(demangled, this->pid);
    demangled = this->mangledName.SubStr(this->pid);
    if (IsGlobalEncode(demangled)) {
        return this->mangledName;
    }

    if (IsDefaultParamFuncEncode(demangled)) {
        this->pid += PREFIX_LEN;
        demangled = demangled.SubStr(PREFIX_LEN);
        this->currentIndex = TryParsePath(demangled);
        if (IsCompressed(this->decompressed)) {
            SpanningFuncDeclTree();
        }
        return this->pid == 0 ? this->decompressed : this->mangledName.SubStr(0, this->pid) + this->decompressed;
    }
    if (!demangled.IsEmpty() && demangled[0] == MANGLE_NESTED_PREFIX[0]) {
        this->pid += MANGLE_CHAR_LEN;
        demangled = demangled.SubStr(MANGLE_CHAR_LEN);
        this->currentIndex = TryParsePath(demangled);
        T rest = this->currentIndex >= decompressed.Length() ? "" : this->decompressed.SubStr(this->currentIndex);
        if (!IsCompressed(rest)) {
            return this->pid == 0 ? this->decompressed : this->mangledName.SubStr(0, this->pid) + this->decompressed;
        }
        if (IsVarDeclEncode(rest)) {
            SpanningVarDeclTree();
        } else {
            // Determine whether the code is a function code.
            // _CN<path><function-name>[I<type>+]H<type>+
            // _CN<path><function-name>K<number>[I<type>+]H<type>+
            // _CN<path>L<number>E
            SpanningFuncDeclTree();
        }
        return this->pid == 0 ? this->decompressed : this->mangledName.SubStr(0, this->pid) + this->decompressed;
    }
    return demangled;
}

template<typename T>
std::tuple<size_t, size_t> DeCompression<T>::UpdateCompressedName(T& compressed, size_t idx)
{
    size_t change = 0;
    char ch = compressed[idx];
    while (ch == MANGLE_COMPRESS_PREFIX && !this->treeIdMap.empty()) {
        size_t curIdx = ForwardNumber(compressed, idx + MANGLE_CHAR_LEN);
        if (curIdx != idx + MANGLE_CHAR_LEN) {
            T numberStr = compressed.SubStr(idx + MANGLE_CHAR_LEN, curIdx - idx - MANGLE_CHAR_LEN);
            size_t mapIdx = GetNumber(numberStr);
            if (mapIdx >= this->treeIdMap.size() || std::get<1>(this->treeIdMap[mapIdx]) == 0) {
                break;
            }
            T curRest = curIdx < compressed.Length() ? compressed.SubStr(curIdx) : "";
            compressed = compressed.SubStr(0, idx) + compressed.SubStr(std::get<0>(this->treeIdMap[mapIdx]),
                std::get<1>(this->treeIdMap[mapIdx])) + curRest;
            change++;
            idx += std::get<1>(this->treeIdMap[mapIdx]);
            if (idx >= compressed.Length()) {
                break;
            }
            ch = compressed[idx];
        } else {
            break;
        }
    }
    return std::tuple<size_t, size_t>(change, idx);
}

template<typename T>
size_t DeCompression<T>::UpdateCompressedName(T& compressed, size_t sid, size_t eid)
{
    size_t idx = sid;
    while (idx < eid) {
        if (compressed[idx] == MANGLE_COMPRESS_PREFIX && !this->treeIdMap.empty() &&
            idx + MANGLE_CHAR_LEN < compressed.Length()) {
            size_t curIdx = ForwardNumber(compressed, idx + MANGLE_CHAR_LEN);
            if (curIdx == idx + MANGLE_CHAR_LEN) {
                idx++;
                continue;
            }
            T numberStr = compressed.SubStr(idx + MANGLE_CHAR_LEN, curIdx - idx - MANGLE_CHAR_LEN);
            size_t mapIdx = GetNumber(numberStr);
            if (mapIdx >= this->treeIdMap.size()) {
                return 0;
            }
            if (std::get<1>(this->treeIdMap[mapIdx]) == 0) {
                idx++;
                continue;
            }
            T curRest = curIdx < compressed.Length() ? compressed.SubStr(curIdx) : "";
            compressed = compressed.SubStr(0, idx) + compressed.SubStr(std::get<0>(this->treeIdMap[mapIdx]),
                std::get<1>(this->treeIdMap[mapIdx])) + curRest;
            eid = compressed.Length();
            idx += std::get<1>(this->treeIdMap[mapIdx]);
        } else {
            idx++;
        }
    }
    return 0;
}

template<typename T>
size_t DeCompression<T>::TryExtendPath(T& mangled, size_t& count, size_t idx, size_t entityId, T& curMangled)
{
    bool isPush = false;
    T mangledCopy = mangled;
    size_t nextIdx = ForwardType(mangled, count, idx + MANGLE_CHAR_LEN);
    size_t change = std::get<0>(UpdateCompressedName(mangled, nextIdx));
    size_t tyId = nextIdx;
    if (nextIdx < mangled.Length() && mangled[nextIdx] == MANGLE_FILE_ID_PREFIX) {
        size_t fid = nextIdx;
        nextIdx = ForwardFileNameNumber(mangled, fid + MANGLE_CHAR_LEN);
        if (nextIdx != fid + MANGLE_CHAR_LEN && change == 0) {
            std::tuple<size_t, size_t> pos(fid, nextIdx - fid);
            isPush = TreeIdMapPushBack(mangled, pos);
            count = isPush ? count + 1 : count;
        }
        change = std::get<0>(UpdateCompressedName(mangled, nextIdx));
        if (nextIdx < mangled.Length() && mangled[nextIdx] == MANGLE_COUNT_PREFIX) {
            size_t lid = nextIdx;
            nextIdx = ForwardNumber(mangled, nextIdx + MANGLE_CHAR_LEN);
            std::tuple<size_t, size_t> eleInfo(idx, nextIdx);
            TreeIdMapAssign(mangled, mangledCopy, entityId, count, eleInfo);
            if (nextIdx != lid + MANGLE_CHAR_LEN && change == 0) {
                std::tuple<size_t, size_t> pos(lid, nextIdx - lid);
                isPush = TreeIdMapPushBack(mangled, pos);
                count = isPush ? count + 1 : count;
            }
        } else {
            nextIdx = idx;
        }
    } else {
        nextIdx = tyId;
    }
    return nextIdx;
}

template<typename T>
size_t DeCompression<T>::TryLambdaPath(T& mangled, size_t& count, size_t idx, size_t entityId, size_t change)
{
    T mangledCopy = mangled;
    size_t nextIdx = ForwardNumber(mangled, idx + MANGLE_CHAR_LEN);
    if (nextIdx == mangled.Length() - 1 && mangled[nextIdx] == END) {
        nextIdx = idx;
    } else {
        nextIdx = nextIdx == idx + MANGLE_CHAR_LEN ? idx : nextIdx;
        if (nextIdx != idx || change > 0) {
            std::tuple<size_t, size_t> eleInfo(idx, nextIdx);
            TreeIdMapAssign(mangled, mangledCopy, entityId, count, eleInfo);
        }
    }
    return nextIdx;
}

template<typename T>
size_t DeCompression<T>::TryGenericPrefixPath(T& mangled, size_t& count, T& curMangled,
    std::tuple<size_t, size_t, size_t> rParams)
{
    T mangledCopy = mangled;
    size_t idx = std::get<0>(rParams);
    size_t entityId = std::get<1>(rParams);
    size_t nextIdx = std::get<2>(rParams);
    nextIdx = ForwardTypes(mangled, count, nextIdx + MANGLE_CHAR_LEN);
    if (nextIdx < mangled.Length() && mangled[nextIdx] == END) {
        nextIdx++;
        std::tuple<size_t, size_t> eleInfo(idx, nextIdx);
        TreeIdMapAssign(mangled, mangledCopy, entityId, count, eleInfo);
    } else if (nextIdx < mangled.Length() && mangled[nextIdx] == MANGLE_FUNCTION_PREFIX) {
        nextIdx = ForwardTypes(mangled, count, nextIdx + MANGLE_CHAR_LEN);
        if (nextIdx < mangled.Length() && mangled[nextIdx] == END) {
            nextIdx++;
            std::tuple<size_t, size_t> eleInfo(idx, nextIdx);
            TreeIdMapAssign(mangled, mangledCopy, entityId, count, eleInfo);
        } else if (nextIdx == mangled.Length()) {
            std::tuple<size_t, size_t> eleInfo(idx, nextIdx);
            TreeIdMapAssign(mangled, mangledCopy, entityId, count, eleInfo);
        } else {
            nextIdx = idx;
        }
    } else {
        nextIdx = idx;
    }
    return nextIdx;
}

template<typename T>
size_t DeCompression<T>::TryNamePrefixPath(T& mangled, size_t& count, T& curMangled,
    std::tuple<size_t, size_t, size_t> rParams)
{
    T mangledCopy = mangled;
    size_t idx = std::get<0>(rParams);
    size_t entityId = std::get<1>(rParams);
    size_t change = std::get<2>(rParams);
    size_t idFlag = 0;
    size_t nextIdx = ForwardName(mangled, idx);
    size_t nameIdx = nextIdx;
    if (nextIdx < mangled.Length() && mangled[nextIdx] == MANGLE_COUNT_PREFIX) {
        idFlag = nextIdx;
        nextIdx = ForwardNumber(mangled, nextIdx + MANGLE_CHAR_LEN);
    }
    if (nextIdx != idx && change == 0) {
        std::tuple<size_t, size_t> pos(idx, nextIdx - idx);
        bool isPush = TreeIdMapPushBack(mangled, pos);
        count = isPush ? count + 1 : count;
    }
    if (nextIdx < mangled.Length()) {
        idFlag = nextIdx;
        change = std::get<0>(UpdateCompressedName(mangled, idFlag));
        nextIdx = idFlag;
        if (mangled[nextIdx] == MANGLE_FILE_ID_PREFIX) {
            nextIdx = ForwardFileNameNumber(mangled, nameIdx + MANGLE_CHAR_LEN);
            std::tuple<size_t, size_t> eleInfo(idx, nextIdx);
            TreeIdMapAssign(mangled, mangledCopy, entityId, count, eleInfo);
            if (nextIdx != idFlag + MANGLE_CHAR_LEN && change == 0) {
                std::tuple<size_t, size_t> pos(idFlag, nextIdx - idFlag);
                bool isPush = TreeIdMapPushBack(mangled, pos);
                count = isPush ? count + 1 : count;
            }
            nextIdx = nextIdx == nameIdx + 1 ? nameIdx : nextIdx;
        } else if (mangled[nextIdx] == MANGLE_GENERIC_PREFIX) {
            std::tuple<size_t, size_t, size_t> rParams{idx, entityId, nextIdx};
            nextIdx = TryGenericPrefixPath(mangled, count, curMangled, rParams);
        } else if (mangled[nextIdx] == MANGLE_FUNCTION_PREFIX) {
            nextIdx = ForwardTypes(mangled, count, nameIdx + MANGLE_CHAR_LEN);
            if (nextIdx < mangled.Length() && mangled[nextIdx] == END) {
                nextIdx++;
                std::tuple<size_t, size_t> eleInfo(idx, nextIdx);
                TreeIdMapAssign(mangled, mangledCopy, entityId, count, eleInfo);
            } else if (nextIdx == mangled.Length()) {
                std::tuple<size_t, size_t> eleInfo(idx, nextIdx);
                TreeIdMapAssign(mangled, mangledCopy, entityId, count, eleInfo);
            } else {
                nextIdx = idx;
            }
        } else {
            nextIdx = nameIdx;
        }
    }
    if (nextIdx == nameIdx) {
        std::get<1>(this->treeIdMap[entityId]) = nameIdx - idx;
        TreeIdMapPop(count, 1);
    }
    return nextIdx;
}

template<typename T>
size_t DeCompression<T>::TryParsePath(T mangled)
{
    this->isRecord = true;
    size_t idx = 0;
    while (idx < mangled.Length()) {
        char ch = mangled[idx];
        size_t nextIdx = idx;
        size_t change = std::get<0>(UpdateCompressedName(mangled, nextIdx));
        T curMangled = mangled;
        std::tuple<size_t, size_t> pos(idx, 0);
        bool isPush = TreeIdMapPushBack(mangled, pos);
        size_t count = isPush ? 1 : 0;
        size_t entityId = this->treeIdMap.size() - 1;
        if (ch == MANGLE_ANONYMOUS_PREFIX) {
            nextIdx = ForwardNumber(mangled, idx + MANGLE_CHAR_LEN);
            if (nextIdx != idx + MANGLE_CHAR_LEN) {
                std::tuple<size_t, size_t> eleInfo(idx, nextIdx);
                TreeIdMapAssign(mangled, curMangled, entityId, count, eleInfo);
            }
            if (nextIdx == idx + MANGLE_CHAR_LEN || change > 0) {
                nextIdx = idx;
            }
        } else if (isdigit(ch) || IsOperatorName(mangled, idx) || IsPropName(mangled, idx)) {
            std::tuple<size_t, size_t, size_t> rParams{idx, entityId, change};
            nextIdx = TryNamePrefixPath(mangled, count, curMangled, rParams);
        } else if (ch == MANGLE_EXTEND_PREFIX) {
            nextIdx = TryExtendPath(mangled, count, idx, entityId, curMangled);
        } else if (ch == MANGLE_LAMBDA_PREFIX) {
            nextIdx = TryLambdaPath(mangled, count, idx, entityId, change);
        } else if (IsStdPkgName(mangled, idx)) {
            nextIdx = ForwardPackageName(mangled, count, idx);
            if (nextIdx != idx || !change) {
                std::tuple<size_t, size_t> eleInfo(idx, nextIdx);
                TreeIdMapAssign(mangled, curMangled, entityId, count, eleInfo);
            }
        }

        if (nextIdx != idx) {
            idx = nextIdx;
        } else {
            TreeIdMapPop(count, 0);
            break;
        }
    }

    this->decompressed = mangled;
    this->isRecord = false;
    return idx;
}
#ifdef BUILD_LIB_CANGJIE_DEMANGLE // to reuse the code to compile CangjieDemangle.cpp
template class DeCompression<StdString>;
#else
template class DeCompression<MapleRuntime::CString>;
#endif
} // namespace Cangjie