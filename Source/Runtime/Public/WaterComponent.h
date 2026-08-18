#pragma once
#include "Types.h"
#include "MathTypes.h"

namespace won::ecs
{
    struct WaterComponent
    {
        enum Flags
        {
            Empty = 0,
            Active = 1 << 0,
            ReceiveShadow = 1 << 1,
        };

        uint32 flags = Active | ReceiveShadow;

        float2 half_extent = { 10.0f, 10.0f };

        float3 absorption_coefficient = { 0.465f, 0.0565f, 0.0092f };
        float3 scattering_coefficient = { 0.06f, 0.06f, 0.06f };

        float wave_frequency = 0.08f;
        float normal_strength = 0.6f;
		float2 wave_velocity_primary = { 0.020f, 0.013f }; // only for normal, not for geometry displacement
        float2 wave_velocity_secondary = { -0.011f, 0.017f }; // only for normal, not for geometry displacement

        float refraction_strength = 0.3f;
        float roughness = 0.06f;
        float reflectance = 0.354f; // this means 0.02 f0
        float ripple_strength = 1.0f;

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

        constexpr void SetReceiveShadow(bool value = true)
        {
            if (value)
            {
                flags |= ReceiveShadow;
            }
            else
            {
                flags &= ~ReceiveShadow;
            }
        }

        constexpr bool IsReceiveShadow() const
        {
            return (flags & ReceiveShadow) != 0;
        }
    };
}
