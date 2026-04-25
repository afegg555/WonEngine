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

        uint3 probe_counts = { 8, 4, 8 };

        float3 probe_spacing = { 2.0f, 2.0f, 2.0f };

        float3 volume_offset = { 0.0f, 0.0f, 0.0f };

        uint32 probes_per_frame = 32;
        uint32 priority = 0;
        float hysteresis = 0.97f; // hysteresis * prev_frame + (1 - hysteresis) * curr_frame
        float normal_bias = 0.3f; // for preventing self-shadow
        float view_bias = 0.1f; // for preventing self-shadow
        float max_distance = 20.0f;

        constexpr void SetActive(bool value = true) { if (value) { flags |= Active; } else { flags &= ~Active; } }
        constexpr bool IsActive() const { return flags & Active; }

        constexpr void SetDynamic(bool value = true) { if (value) { flags |= Dynamic; } else { flags &= ~Dynamic; } }
        constexpr bool IsDynamic() const { return flags & Dynamic; }
    };
}
