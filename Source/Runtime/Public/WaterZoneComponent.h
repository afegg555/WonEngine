#pragma once
#include "Types.h"
#include "MathTypes.h"

namespace won::ecs
{
    struct WaterZoneComponent
    {
        enum Flags
        {
            Empty = 0,
            Active = 1 << 0,
        };

        uint32 flags = Active;

        float2 half_extent = { 64.0f, 64.0f }; // world unit
        uint32 info_resolution = 512;

		uint32 tile_resolution = 16; // each tile is tessellated into (tile_resolution x tile_resolution) quads
        uint32 lod_levels = 6;
        float lod_distance_scale = 2.0f;

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
