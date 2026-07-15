#pragma once
#include "Image.h"
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
		float influence_radius = 20.0f; // in world units, the radius of influence for this reflection probe
        float intensity_multiplier = 1.0f;

        // Runtime-only: the loaded prefiltered cubemap (not serialized).
        std::shared_ptr<resource::Image> cubemap;

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
