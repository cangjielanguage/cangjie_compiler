// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

#ifndef CANGJIE_UTILS_SIPHASH_H
#define CANGJIE_UTILS_SIPHASH_H

#include "cangjie/Utils/CheckUtils.h"
#include <bitset>
#include <climits>
#include <cstddef>
#include <cstdint>

namespace Cangjie::Utils {

// NOTE: This SipHash-2-4 variant deviates from the standard: the message
// length is never mixed into the final block, and inputs of up to 8 bytes are
// zero-padded into a single block. Inputs whose zero-padded byte sequences are
// identical therefore collide across types and widths, not only by appending
// null bytes (e.g. "Chunked" vs "Chunked\0", or bool(true) vs uint32_t(1)).
//
// Deliberately left unfixed: this hash feeds incremental-compilation cache
// keys (ASTHasher, ASTCacheCalculator, GetHashedObjFileName); changing it
// would invalidate all existing incremental-compilation caches.
class SipHash {
public:
    template <std::size_t N> static uint64_t GetHashValue(const std::bitset<N>& rawData)
    {
        const uint64_t data = rawData.to_ullong();
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&data);
        const uint64_t size = sizeof(uint64_t);
        return SipHash_2_4(bytes, size);
    }
    template <typename T> static uint64_t GetHashValue(T data)
    {
        static_assert(std::is_arithmetic_v<T>);
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&data);
        const uint64_t size = sizeof(T);
        return SipHash_2_4(bytes, size);
    }
    static uint64_t GetHashValue(std::string data)
    {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
        const uint64_t size = data.size();
        return SipHash_2_4(bytes, size);
    }
    static uint64_t GetHashValue(const char* data)
    {
        return GetHashValue(std::string{data});
    }

private:
    static const uint64_t k0_ = 0xdeadbeef;
    static const uint64_t k1_ = 0x12345678;

    static uint64_t GetValueLeft(const uint8_t* dataPointer, uint64_t sizeLeft)
    {
        uint64_t valueLeft{0};
        static_assert(CHAR_BIT <= 8);
        switch (sizeLeft) {
            case 7:
                valueLeft |= static_cast<uint64_t>(dataPointer[6]) << 48;
                [[fallthrough]];
            case 6:
                valueLeft |= static_cast<uint64_t>(dataPointer[5]) << 40;
                [[fallthrough]];
            case 5:
                valueLeft |= static_cast<uint64_t>(dataPointer[4]) << 32;
                [[fallthrough]];
            case 4:
                valueLeft |= static_cast<uint64_t>(dataPointer[3]) << 24;
                [[fallthrough]];
            case 3:
                valueLeft |= static_cast<uint64_t>(dataPointer[2]) << 16;
                [[fallthrough]];
            case 2:
                valueLeft |= static_cast<uint64_t>(dataPointer[1]) << 8;
                [[fallthrough]];
            case 1:
                valueLeft |= static_cast<uint64_t>(dataPointer[0]);
                [[fallthrough]];
            default:
                break;
        }
        return valueLeft;
    }

    static uint64_t SipHash_2_4(const uint8_t* data, uint64_t size)
    {
        uint64_t v0 = k0_ ^ 0x736f6d6570736575ull;
        uint64_t v1 = k1_ ^ 0x646f72616e646f6dull;
        uint64_t v2 = k0_ ^ 0x6c7967656e657261ull;
        uint64_t v3 = k1_ ^ 0x7465646279746573ull;

        uint64_t sizep{0};
        const uint64_t* dataPointer{reinterpret_cast<const uint64_t*>(data)};
        for (; sizep + CHAR_BIT <= size; sizep += CHAR_BIT) {
            uint64_t m{*dataPointer++};
            v3 ^= m;
            SipRounds(v0, v1, v2, v3);
            SipRounds(v0, v1, v2, v3);
            v0 ^= m;
        }

        if (size - sizep != 0) {
            auto valueLeft = GetValueLeft(reinterpret_cast<const uint8_t*>(dataPointer), size - sizep);
            v3 ^= valueLeft;
            SipRounds(v0, v1, v2, v3);
            SipRounds(v0, v1, v2, v3);
            v0 ^= valueLeft;
        }

        v2 ^= 0xff;
        SipRounds(v0, v1, v2, v3);
        SipRounds(v0, v1, v2, v3);
        SipRounds(v0, v1, v2, v3);
        SipRounds(v0, v1, v2, v3);

        return v0 ^ v1 ^ v2 ^ v3;
    }

    static void SipRounds(uint64_t& v0, uint64_t& v1, uint64_t& v2, uint64_t& v3)
    {
        v0 += v1;
        v1 = (v1 << 13) | (v1 >> (64 - 13)); // `64` means v1 is 64 bits, `13` comes from siphash algorithm
        v1 ^= v0;
        v0 = (v0 << 32) | (v0 >> (64 - 32)); // `64` means v0 is 64 bits, `32` comes from siphash algorithm
        v2 += v3;
        v3 = (v3 << 16) | (v3 >> (64 - 16)); // `64` means v3 is 64 bits, `16` comes from siphash algorithm
        v3 ^= v2;
        v0 += v3;
        v3 = (v3 << 21) | (v3 >> (64 - 21)); // `64` means v3 is 64 bits, `21` comes from siphash algorithm
        v3 ^= v0;
        v2 += v1;
        v1 = (v1 << 17) | (v1 >> (64 - 17)); // `64` means v1 is 64 bits, `17` comes from siphash algorithm
        v1 ^= v2;
        v2 = (v2 << 32) | (v2 >> (64 - 32)); // `64` means v2 is 64 bits, `32` comes from siphash algorithm
    }
};
}

#endif
