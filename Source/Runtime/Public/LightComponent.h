#pragma once
#include "Types.h"
#include "Primitives.h"

static constexpr uint NUM_MAX_LIGHTS_FORWARD_RENDERING = 128;

namespace won::ecs
{
    struct LightComponent
    {
		enum FLAGS
		{
			EMPTY = 0,
			ACTIVE = 1 << 0,
		};
		uint32_t flags = ACTIVE;

		enum LightType : uint32_t
		{
			DIRECTIONAL,
			POINT,
			SPOT,

			LIGHTTYPE_COUNT,
		};
		LightType type = DIRECTIONAL;

		XMFLOAT3 color = XMFLOAT3(1, 1, 1);
		// Brightness of light in. The units that this is defined in depend on the type of light.
		// Point and spot lights : luminous power lumen (800 lumen for a household light bulb)
		// Directional lights : Illuminance lux (lm/m2). (120,000 lux for daylight sky and sun) https://google.github.io/filament/Filament.md.html#table_sunskyilluminance
		float intensity = 800.0f;
		float range = 10.0f;
		float outer_cone_angle = XM_PIDIV4;
		float inner_cone_angle = 0; // default value is 0, means only outer cone angle is used

		// these value will be updated on LightUpdateSystem
		// you can use TransformComponent for manipulation !!
		float3 position = { 0, 0, 0 }; // vec(0, 0, 0) * transform_matrix
		float3 direction = { 0, 0, 1 }; // vec(0, 0, 1) * transform_matrix
		math::AABB aabb = {};

		constexpr void SetActive(bool value = true) { if (value) { flags |= ACTIVE; } else { flags &= ~ACTIVE; } }
		constexpr bool IsActive() const { return flags & ACTIVE; }
    };
}
