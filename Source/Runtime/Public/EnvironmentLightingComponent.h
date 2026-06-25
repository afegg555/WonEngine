#pragma once
#include "Types.h"

namespace won::ecs
{
    struct EnvironmentLightingComponent
    {
        enum Flags
        {
            Empty = 0,
            Active = 1 << 0,
        };

        enum GIMode : uint32
        {
            None,
            Ambient,
            DDGI,
        };

        uint32 flags = Active;
        GIMode gi_mode = Ambient;

        float3 ambient_color = { 0.03f, 0.03f, 0.03f };
        float ambient_intensity = 10000.0f;

        float indirect_diffuse_scale = 1.0f;
        float indirect_specular_scale = 1.0f;

        constexpr void SetActive(bool value = true) { if (value) { flags |= Active; } else { flags &= ~Active; } }
        constexpr bool IsActive() const { return flags & Active; }
    };
}
