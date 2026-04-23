#pragma once
#include "Types.h"

namespace won::ecs
{
    struct FogVolumeComponent
    {
        enum Flags
        {
            Empty = 0,
            Active = 1 << 0,
        };

        uint32 flags = Active;

        constexpr void SetActive(bool value = true) { if (value) { flags |= Active; } else { flags &= ~Active; } }
        constexpr bool IsActive() const { return flags & Active; }
    };
}
