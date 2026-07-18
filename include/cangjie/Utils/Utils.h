// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares some utility functions.
 */

#ifndef CANGJIE_UTILS_UTILS_H
#define CANGJIE_UTILS_UTILS_H

#include <algorithm>
#include <climits>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cangjie/Utils/SafePointer.h"
#include "cangjie/Utils/CheckUtils.h"
#include "cangjie/Utils/ConstantsUtils.h"
#include "cangjie/Utils/Fnv1a.h"
#include "cangjie/Utils/SipHash.h"
#include "cangjie/Utils/PartiallyPersistent.h"

namespace Cangjie {
namespace AST {
// Forward types to avoid include recursively.
struct Decl;
struct ClassLikeDecl;
struct InheritableDecl;
struct FuncDecl;
struct Node;
struct File;
struct Ty;
} // namespace AST

/**
 * Create an unique_ptr and check if succeed.
 */
template <typename T, typename... Args> std::unique_ptr<T> CreateUniquePtr(Args&&... args)
{
    auto ptr = std::make_unique<T>(std::forward<Args>(args)...);
    CJC_NULLPTR_CHECK(ptr);
    return ptr;
}

struct HashPair {
    template <typename T1, typename T2> std::size_t operator()(const std::pair<T1, T2>& pair) const
    {
        auto h1 = std::hash<T1>{}(pair.first);
        auto h2 = std::hash<T2>{}(pair.second);
        return h1 ^ h2;
    }
};

/** hash_combine is a function used to create hash with fewer collisions. */
template <typename T> inline size_t hash_combine(size_t& seed, const T& v)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2); // 6,2 are specific constant in the hash algorithm.
    return seed;
}

namespace Utils {
inline std::string FillZero(int num, int len)
{
    std::ostringstream out;
    out << std::setfill('0') << std::setw(len) << num;
    return out.str();
}
inline std::string FillZero(unsigned int num, int len)
{
    std::ostringstream out;
    out << std::setfill('0') << std::setw(len) << num;
    return out.str();
}

/* Get Mangled name for wrapper function of macro. */
std::string GetMacroFuncName(const std::string& fullPackageName, bool isAttr, const std::string& ident);

/**
 * Trim from left.
 */
static inline void ltrim(std::string& str)
{
    str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](const auto& it) { return !std::isspace(it); }));
}
/**
 * Trim from right.
 */
static inline void rtrim(std::string& str)
{
    str.erase(
        std::find_if(str.rbegin(), str.rend(), [](const auto& it) { return !std::isspace(it); }).base(), str.end());
}
/**
 * Trim from both sides.
 */
inline void TrimString(std::string& str)
{
    ltrim(str);
    rtrim(str);
}

inline auto TrimStringVector(std::vector<std::string>& container)
{
    std::for_each(std::begin(container), std::end(container), [](auto& it) { TrimString(it); });
}

template <typename T1, typename T2> std::vector<T1> SetToVec(const T2& s)
{
    std::vector<T1> res(s.cbegin(), s.cend());
    return res;
}

template <typename T> std::set<T> VecToSet(const std::vector<T>& v)
{
    std::set<T> res;
    for (auto& e : v) {
        res.insert(e);
    }
    return res;
}

template <typename C, typename T> std::set<T, C> VecToSortedSet(const std::vector<T>& v, const C& cmp)
{
    return std::set<T, C>(v.begin(), v.end(), cmp);
}

template <typename T, typename Pred> static inline void EraseIf(T& container, Pred pred)
{
    auto iter = container.begin();
    while (iter != container.end()) {
        if (pred(*iter)) {
            iter = container.erase(iter);
        } else {
            iter++;
        }
    }
}

template <typename K, typename V> static inline std::unordered_set<K> GetKeys(const std::unordered_map<K, V>& container)
{
    std::unordered_set<K> keys;
    std::for_each(container.cbegin(), container.cend(), [&keys](auto& it) { keys.insert(it.first); });
    return keys;
}

template <typename K, typename V> std::set<K> GetKeys(const std::map<K, V>& container)
{
    std::set<K> keys;
    std::for_each(container.cbegin(), container.cend(), [&keys](auto& it) { keys.insert(it.first); });
    return keys;
}

template <typename M, typename K> bool InKeys(const K& key, const M& collection)
{
    static_assert(std::is_same<typename M::key_type, K>(), "");
    return collection.find(key) != collection.cend();
}

/**
 * Element is in container or not.
 */
template <typename T> static inline bool In(const T element, const std::initializer_list<T>& elements)
{
    std::vector<T> vec(elements);
    auto it = std::find(vec.cbegin(), vec.cend(), element);
    return it != vec.cend();
}

template <typename T> static inline bool In(const T element, const std::vector<T>& container)
{
    auto it = std::find(container.cbegin(), container.cend(), element);
    return it != container.cend();
}

template <typename T> static inline bool In(const T element, const std::set<T>& set)
{
    return set.find(element) != set.cend();
}

template <typename T> static inline bool In(const T element, const PSet<T>& set)
{
    return set.find(element) != set.cend();
}

template <typename T> static inline bool In(const T element, const std::unordered_set<T>& set)
{
    return set.find(element) != set.cend();
}

template <typename T, typename U>
static inline std::enable_if_t<std::is_same_v<std::remove_pointer_t<T>, U> && std::is_pointer_v<T>, bool> In(
    const T element, const std::set<Ptr<U>>& set)
{
    return set.find(Ptr(element)) != set.cend();
}

template <typename T, typename U>
static inline std::enable_if_t<std::is_same_v<std::remove_pointer_t<T>, U> && std::is_pointer_v<T>, bool> In(
    const T element, const std::unordered_set<Ptr<U>>& set)
{
    return set.find(Ptr(element)) != set.cend();
}

template <typename T, typename Pred> static inline bool In(const T& container, Pred pred)
{
    auto it = std::find_if(container.begin(), container.end(), pred);
    return it != container.end();
}

template <typename T> static inline bool NotIn(T element, const std::vector<T>& container)
{
    auto it = std::find(container.begin(), container.end(), element);
    return it == container.end();
}

template <typename T, typename Pred> static inline bool NotIn(const T& container, Pred pred)
{
    auto it = std::find_if(container.begin(), container.end(), pred);
    return it == container.end();
}

template <typename T, typename Pred> inline bool All(const T& container, Pred pred)
{
    return std::all_of(container.cbegin(), container.cend(), pred);
}

template <typename... Args> static inline bool AnyOf(Args... args)
{
    return (... || args);
}

template <typename... Args> static inline bool AllOf(Args... args)
{
    return (... && args);
}

template <typename T> inline void RemoveFromVec(std::vector<T>& vec, const T& item)
{
    vec.erase(std::remove(vec.begin(), vec.end(), item), vec.end());
}

inline std::string StrToLower(std::string str)
{
    (void)transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}

OverflowStrategy StringToOverflowStrategy(const std::string& name);
bool ValidOverflowStrategy(const std::string& name);
std::string OverflowStrategyName(const OverflowStrategy& overflowStrategy);
std::string GenerateRandomHexString();
/**
 * Try to parse a string into an integer.
 * Return std::nullopt if input string is not a valid integer or it exceeds the range integer can represent.
 */
std::optional<int> TryParseInt(const std::string& str);

std::vector<std::string> StringifyArgumentVector(int argc, const char** argv);
std::unordered_map<std::string, std::string> StringifyEnvironmentPointer(const char** envp);

std::string GetRootPackageName(const std::string& fullPackageName);

#ifdef _WIN32
std::optional<std::string> GetApplicationPath();
#else
std::optional<std::string> GetApplicationPath(
    const std::string& argv0, const std::unordered_map<std::string, std::string>& environmentVars);
#endif
// integer to char sequence compresser
struct Out64 {
    explicit Out64(uint_fast64_t v1 = 0, bool start = true, bool finish = true)
        : v{v1}, delimAtStart{start}, delimAtFinish{finish}
    {}

    Out64& operator<<(uint_fast64_t v1)
    {
        v |= v1;
        return *this;
    }

    friend std::ostream& operator<<(std::ostream& out, const Out64& w)
    {
        constexpr uint_fast64_t byteLen{6};
        static constexpr unsigned char chars[1 << byteLen] = {
            '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
            'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
            'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
            'u', 'v', 'w', 'x', 'y', 'z',
            'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
            'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
            'U', 'V', 'W', 'X', 'Y', 'Z',
            '+', '-'
        };
        // putchar internally cast the int argument to unsigned char right before printing, let's do the same here
        constexpr unsigned char delim = '&';
        if (w.v) {
            if (w.delimAtStart) {
                out << delim;
            }
            constexpr int cacheSize = static_cast<int>(sizeof(v) * CHAR_BIT / byteLen + 1);
            unsigned char cache[cacheSize];
            auto v1{w.v};
            int bit{0};
            // compress a 2^6 value into one printable character
            while (v1) {
                cache[bit++] = chars[v1 % (1 << byteLen)];
                v1 >>= byteLen;
            }
            while (--bit >= 0) {
                CJC_ASSERT(bit < cacheSize);
                out << cache[bit];
            }
            if (w.delimAtFinish) {
                out << delim;
            }
        }
        return out;
    }

private:
    uint_fast64_t v;
    bool delimAtStart, delimAtFinish;
};

inline std::string HashString64(const std::string& str)
{
    uint64_t hash = Fnv1a64(str);
    if (hash == 0) {
        // Out64 prints nothing for a zero value; return a fixed digit so the
        // generated name never degenerates to the bare prefix.
        return "0";
    }
    Out64 printer{0, false, false};
    printer << hash;
    std::stringstream ss;
    ss << printer;
    return std::move(*ss.rdbuf()).str();
}

void FreeIdleMemoryToOS();

void ThrowNullPointerException();
} // namespace Utils
} // namespace Cangjie
#endif // CANGJIE_UTILS_UTILS_H
