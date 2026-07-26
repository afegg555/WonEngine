#pragma once
#include "Types.h"

namespace won
{
	namespace editor::theme
	{
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
