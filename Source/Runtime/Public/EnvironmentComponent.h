#pragma once
#include "Image.h"
#include "Types.h"

namespace won::ecs
{
    struct EnvironmentComponent
    {
        enum Flags
        {
            Empty = 0,
            Active = 1 << 0,
        };

        enum class SkyType : uint32
        {
            None,
            Procedural,
            Cubemap,
            PhysicallyBased,
        };

        enum class DiffuseGIMode : uint32
        {
            None,
            Ambient,
            DDGI,
			Cubemap, // from irradiance cubemap, use irradiance_cubemap_asset_path
            Sky, // captured from the sky at runtime
        };

        enum class ReflectionMode : uint32
        {
            None,
			Cubemap, // from prefiltered cubemap, use specular_cubemap_asset_path(global) or ReflectionProbeComponent(local)
            Sky, // captured from the sky at runtime
        };

        uint32 flags = Active;
        SkyType sky_type = SkyType::Procedural;
        DiffuseGIMode diffuse_gi_mode = DiffuseGIMode::Ambient;
        ReflectionMode reflection_mode = ReflectionMode::Cubemap;

        // Procedural and physically based sky
        float3 sun_direction = { 0.58f, 0.38f, 0.71f };
        float sun_intensity = 100000.0f;
        float3 sun_color = { 1.0f, 0.95f, 0.85f };
        float sun_angular_radius = 0.001f;

        float sky_intensity = 10000.0f;
        float3 ground_color = { 0.20f, 0.18f, 0.15f };
        float ground_intensity = 2000.0f;

        // Procedural sky
        float sun_glow_intensity = 50000.0f;
        float sun_glow_falloff = 1000.0f;
        float3 sky_horizon_color = { 0.75f, 0.85f, 1.0f };
        float3 sky_zenith_color = { 0.10f, 0.35f, 0.85f };
        float sky_horizon_falloff = 1.5f;
        float3 ground_horizon_color = { 0.45f, 0.42f, 0.38f };
        float ground_falloff = 1.0f;

        // Physically based sky
        float turbidity = 3.0f;
        float mie_eccentricity = 0.8f;
        float rayleigh_coefficient = 2.0f;
        float mie_coefficient = 1.0f;
        bool direct_sun_active = true;
        bool direct_sun_cast_shadow = true;
        uint32 direct_sun_shadow_resolution = 2048;
        uint32 direct_sun_cascade_count = 4;
        float direct_sun_cascade_lambda = 0.95f;
        float direct_sun_cascade_blend = 0.1f;
        float direct_sun_shadow_distance = 0.0f; // 0: use the camera far plane

        // Cloud layer
		float cloud_coverage = 0.55f; // 0.0 = clear sky, 1.0 = almost fully covered
		float cloud_density = 0.1f; // how opaque the clouds are
		float cloud_frequency = 2.0f; // noise tiling; higher value makes smaller clouds
		float cloud_speed = 0.01f; // in uv units per second
        float3 cloud_color = { 1.0f, 1.0f, 1.0f };
		float2 cloud_direction = { 1.0f, 0.3f }; // only direction matters, will be normalized internally

        // Cubemap sky
        String sky_cubemap_asset_path;

        // Ambient / GI
        float3 ambient_color = { 0.03f, 0.03f, 0.03f };
        float ambient_intensity = 10000.0f;

        float indirect_diffuse_scale = 1.0f;
        float indirect_specular_scale = 1.0f;

        String irradiance_cubemap_asset_path;
        String specular_cubemap_asset_path;

        

        std::shared_ptr<resource::Image> sky_cubemap;
        std::shared_ptr<resource::Image> irradiance_cubemap;
        std::shared_ptr<resource::Image> specular_cubemap;

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

        constexpr bool HasSky() const
        {
            return sky_type != SkyType::None;
        }
    };
}
