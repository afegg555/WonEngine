#pragma once
#include "Types.h"

namespace won
{
	namespace editor::theme
	{
		inline const float4 ddgi_volume_color = { 0.25f, 0.95f, 0.45f, 1.0f };
		inline const float4 ddgi_probe_color = { 1.0f, 0.70f, 0.20f, 1.0f };
		inline const float4 ddgi_probe_relocated_color = { 1.0f, 0.95f, 0.0f, 1.0f };
		inline const float4 ddgi_probe_invalid_color = { 1.0f, 0.1f, 0.1f, 1.0f };
		inline const float4 cpu_bvh_internal_color = { 0.15f, 0.35f, 1.0f, 1.0f };
		inline const float4 cpu_bvh_leaf_color = { 0.15f, 0.85f, 1.0f, 1.0f };
		inline const float4 gpu_bvh_internal_color = { 1.0f, 0.45f, 0.15f, 1.0f };
		inline const float4 gpu_bvh_leaf_color = { 1.0f, 0.85f, 0.15f, 1.0f };
		inline const float4 collider_3d_color = { 0.2f, 0.85f, 1.0f, 1.0f };
		inline const float4 collider_3d_trigger_color = { 1.0f, 0.65f, 0.2f, 1.0f };
		inline const float4 editor_grid_color = { 0.32f, 0.32f, 0.34f, 0.55f };
		inline const float4 editor_grid_axis_x_color = { 0.82f, 0.24f, 0.24f, 0.85f };
		inline const float4 editor_grid_axis_z_color = { 0.24f, 0.42f, 0.88f, 0.85f };

		inline const float4 asset_source_color = { 0.95f, 0.65f, 0.25f, 1.0f };
		inline const float4 asset_imported_color = { 0.45f, 0.80f, 0.55f, 1.0f };
		inline const float4 asset_broken_color = { 0.90f, 0.30f, 0.30f, 1.0f };
		inline const float4 asset_needs_reimport_color = { 0.95f, 0.80f, 0.25f, 1.0f };

		inline const float4 component_header_color = { 0.2f, 0.2f, 0.21568628f, 1.0f };
		inline const float4 component_header_hovered_color = { 0.25490198f, 0.25490198f, 0.27450982f, 1.0f };
		inline const float4 component_header_active_color = { 0.29803923f, 0.29803923f, 0.32156864f, 1.0f };

		void SetupVisualStudioStyle();
	}
}
