#pragma once
#include "Types.h"
#include "Primitives.h"

static constexpr uint NUM_MAX_LIGHTS_FORWARD_RENDERING = 128;
static constexpr uint SHADOW_CASCADE_COUNT_MAX = 4;

namespace won::ecs
{
    struct LightComponent
    {
		enum Flags
		{
			Empty = 0,
			Active = 1 << 0,
			Dynamic = 1 << 1,
			CastShadow = 1 << 2,
		};
		uint32_t flags = Active | Dynamic | CastShadow;

		enum class LightType : uint32_t
		{
			Directional,
			Point,
			Spot,

			LIGHTTYPE_COUNT,
		};
		LightType type = LightType::Directional;

		XMFLOAT3 color = XMFLOAT3(1, 1, 1);
		// Brightness of light in. The units that this is defined in depend on the type of light.
		// Point and spot lights : luminous power lumen (800 lumen for a household light bulb)
		// Directional lights : Illuminance lux (lm/m2). (120,000 lux for daylight sky and sun) https://google.github.io/filament/Filament.md.html#table_sunskyilluminance
		float intensity = 100000.0f;
		float range = 10.0f;
		float outer_cone_angle = XM_PIDIV4;
		float inner_cone_angle = 0; // default value is 0, means only outer cone angle is used

        uint32 shadow_map_resolution = 1024;
		uint32 shadow_cascade_count = 4;
		float shadow_cascade_lambda = 0.95f; // 0: uniform split, 1: logarithmic split(more precision for near area)
		float shadow_cascade_blend = 0.1f;

		// these value will be updated on LightUpdateSystem
		// you can use TransformComponent for manipulation !!
		float3 position = { 0, 0, 0 }; // vec(0, 0, 0) * transform_matrix
		float3 direction = { 0, 0, 1 }; // vec(0, 0, 1) * transform_matrix
		math::AABB aabb = {};

		constexpr void SetActive(bool value = true) { if (value) { flags |= Active; } else { flags &= ~Active; } }
		constexpr bool IsActive() const { return flags & Active; }

		constexpr void SetCastShadow(bool value = true) { if (value) { flags |= CastShadow; } else { flags &= ~CastShadow; } }
		constexpr bool IsCastShadow() const { return flags & CastShadow; }
		constexpr void SetDynamic(bool value = true) { if (value) { flags |= Dynamic; } else { flags &= ~Dynamic; } }
		constexpr bool IsDynamic() const { return flags & Dynamic; }
    };
}
