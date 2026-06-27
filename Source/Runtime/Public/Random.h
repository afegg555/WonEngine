#pragma once
#include "RuntimeExport.h"
#include "Types.h"

namespace won::random
{
    WONENGINE_API uint32 GetRandomSeed();

    inline uint32 NextUint(uint32& seed)
    {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return seed;
    }

    inline float NextFloat(uint32& seed)
    {
        return static_cast<float>(NextUint(seed) & 0xFFFFFFu) / static_cast<float>(0x1000000u);
    }

    inline float NextSignedFloat(uint32& seed)
    {
        return NextFloat(seed) * 2.0f - 1.0f;
    }
}
