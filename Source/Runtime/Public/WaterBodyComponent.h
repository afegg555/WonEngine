#pragma once
#include "Types.h"
#include "MathTypes.h"

namespace won::ecs
{
    struct WaterBodyComponent
    {
        enum class Type : uint32
        {
            Lake,
        };

        enum Flags
        {
            Empty = 0,
            Active = 1 << 0,
            ReceiveShadow = 1 << 1,
        };

        uint32 flags = Active | ReceiveShadow;

        Type type = Type::Lake;
		float2 half_extent = { 10.0f, 10.0f }; // world unit

        float3 absorption_coefficient = { 0.465f, 0.0565f, 0.0092f };
        float3 scattering_coefficient = { 0.06f, 0.06f, 0.06f };

		// only applied to surface normal
        float detail_frequency = 0.08f;
        float detail_strength = 0.6f;
		float2 detail_velocity_primary = { 0.1f, 0.1f };
        float2 detail_velocity_secondary = { -0.011f, 0.017f };

		// vertex displacement waves
		uint32 wave_count = 4; // like octaves in fbm noise
        float wave_length = 12.0f; // world unit, longest wave
		float wave_amplitude = 0.25f; // world unit, largest wave   !! Amplitude = (Crest - Trough) / 2
        float wave_steepness = 0.5f; // 0 is a sine wave, 1 is a cusp(sharp crest)
        float2 wave_direction = { 1.0f, 0.0f };
        float wave_direction_spread = 0.6f; // radian between waves, 0 is parallel, PI/2 is omnidirectional

        float refraction_strength = 0.1f;
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
