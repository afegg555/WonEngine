#pragma once
#include "Types.h"

namespace won::ecs
{
    struct ReflectionProbeComponent
    {
        enum Flags
        {
            Empty = 0,
            Active = 1 << 0,
        };

        uint32 flags = Active;

        String cubemap_asset_path;
        float influence_radius = 20.0f;
        float intensity = 1.0f;

        constexpr void SetActive(bool value = true)
        {
            if (value)
            {
                flags |= Active;
            }
            else
            {
                flags &= ~Active;
            }
        }

        constexpr bool IsActive() const
        {
            return (flags & Active) != 0;
        }
    };
}
