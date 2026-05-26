#pragma once

#include <stddef.h>
#include <stdint.h>

namespace won
{
    inline constexpr uint64_t stable_hash_offset_basis = 14695981039346656037ull; // FNV offset basis, 64-bit
    inline constexpr uint64_t stable_hash_prime = 1099511628211ull; // FNV prime, 64-bit
    // we use FNV-1a hash algorithm

    constexpr uint64_t StableHash(const char* text)
    {
        uint64_t hash = stable_hash_offset_basis;
        while (text && *text)
        {
            hash ^= static_cast<uint8_t>(*text++);
            hash *= stable_hash_prime;
        }
        return hash;
    }

    constexpr uint64_t StableHash(const char* data, size_t size)
    {
        uint64_t hash = stable_hash_offset_basis;
        for (size_t i = 0; data && i < size; ++i)
        {
            hash ^= static_cast<uint8_t>(data[i]);
            hash *= stable_hash_prime;
        }
        return hash;
    }
}
