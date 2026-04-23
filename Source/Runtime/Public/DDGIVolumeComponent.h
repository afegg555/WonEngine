#pragma once
#include "Types.h"

namespace won::ecs
{
    struct DDGIVolumeComponent
    {
        enum Flags
        {
            Empty = 0,
            Active = 1 << 0,
            Dynamic = 1 << 1,
        };

        uint32 flags = Active | Dynamic;

        constexpr void SetActive(bool value = true) { if (value) { flags |= Active; } else { flags &= ~Active; } }
        constexpr bool IsActive() const { return flags & Active; }

        constexpr void SetDynamic(bool value = true) { if (value) { flags |= Dynamic; } else { flags &= ~Dynamic; } }
        constexpr bool IsDynamic() const { return flags & Dynamic; }
    };
}
