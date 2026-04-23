#pragma once
#include "Types.h"

namespace won::ecs
{
    struct SkyComponent
    {
        enum Flags
        {
            Empty = 0,
            Active = 1 << 0,
        };

        uint32 flags = Active;

        float3 sun_direction = { 0.58f, 0.38f, 0.71f };
        float sun_intensity = 2.0f;

        float3 sun_color = { 1.0f, 0.95f, 0.85f };
        float sun_angular_radius = 0.001f;

        float sun_glow_intensity = 2.91f;
        float sun_glow_falloff = 1000.0f;

        float3 sky_horizon_color = { 0.75f, 0.85f, 1.0f };
        float sky_intensity = 3.0f;

        float3 sky_zenith_color = { 0.10f, 0.35f, 0.85f };
        float sky_horizon_falloff = 1.5f;

        float3 ground_horizon_color = { 0.45f, 0.42f, 0.38f };
        float ground_intensity = 1.5f;

        float3 ground_color = { 0.20f, 0.18f, 0.15f };
        float ground_falloff = 1.0f;

        constexpr void SetActive(bool value = true) { if (value) { flags |= Active; } else { flags &= ~Active; } }
        constexpr bool IsActive() const { return flags & Active; }
    };
}
