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
        };

        enum class DiffuseGIMode : uint32
        {
            None,
            Ambient,
            DDGI,
			Cubemap, // from irradiance cubemap, use irradiance_cubemap_asset_path
        };

        enum class ReflectionMode : uint32
        {
            None,
			Cubemap, // from prefiltered cubemap, use specular_cubemap_asset_path(global) or ReflectionProbeComponent(local)
        };

        uint32 flags = Active;
        SkyType sky_type = SkyType::Procedural;
        DiffuseGIMode diffuse_gi_mode = DiffuseGIMode::Ambient;
        ReflectionMode reflection_mode = ReflectionMode::Cubemap;

        // Sky / atmosphere
        float3 sun_direction = { 0.58f, 0.38f, 0.71f };
        float sun_intensity = 100000.0f;

        float3 sun_color = { 1.0f, 0.95f, 0.85f };
        float sun_angular_radius = 0.001f;

        float sun_glow_intensity = 50000.0f;
        float sun_glow_falloff = 1000.0f;

        float3 sky_horizon_color = { 0.75f, 0.85f, 1.0f };
        float sky_intensity = 10000.0f;

        float3 sky_zenith_color = { 0.10f, 0.35f, 0.85f };
        float sky_horizon_falloff = 1.5f;

        float3 ground_horizon_color = { 0.45f, 0.42f, 0.38f };
        float ground_intensity = 2000.0f;

        float3 ground_color = { 0.20f, 0.18f, 0.15f };
        float ground_falloff = 1.0f;

        // Ambient / GI
        float3 ambient_color = { 0.03f, 0.03f, 0.03f };
        float ambient_intensity = 10000.0f;

        float indirect_diffuse_scale = 1.0f;
        float indirect_specular_scale = 1.0f;

        String sky_cubemap_asset_path;
        String irradiance_cubemap_asset_path;
        String specular_cubemap_asset_path;

		// These values are consumed by GPUScene environment extraction.
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
