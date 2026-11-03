// Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_UTILS_FNV1A_H
#define CANGJIE_UTILS_FNV1A_H

#include <cstdint>
#include <string>

namespace Cangjie::Utils {

// Standard parameters of the 64-bit FNV-1a hash algorithm.
constexpr uint64_t FNV_64_OFFSET_BASIS = 0xcbf29ce484222325ULL;
constexpr uint64_t FNV_64_PRIME = 0x100000001b3ULL;

// Deterministic 64-bit FNV-1a hash of a string. Unlike std::hash, the result
// is independent of the standard library implementation and stable across
// platforms and compilers.
inline uint64_t Fnv1a64(const std::string& data)
{
    uint64_t hash = FNV_64_OFFSET_BASIS;
    for (auto c : data) {
        hash ^= static_cast<unsigned char>(c);
        hash *= FNV_64_PRIME;
    }
    return hash;
}
}

#endif
