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

		inline const float4 viewport_text_block_background_color = { 0.07058824f, 0.07058824f, 0.07058824f, 0.70588237f };
		inline const float4 viewport_text_block_border_color = { 0.35294119f, 0.35294119f, 0.35294119f, 0.78431374f };
		inline const float4 viewport_text_block_text_color = { 0.90196079f, 0.90196079f, 0.90196079f, 1.0f };
		inline const float4 viewport_title_color = { 0.52f, 0.52f, 0.49f, 1.0f };
		inline const float4 transparent_color = { 0.0f, 0.0f, 0.0f, 0.0f };

		inline const float4 status_success_color = { 0.40f, 0.85f, 0.40f, 1.0f };
		inline const float4 status_error_color = { 0.90f, 0.35f, 0.35f, 1.0f };
		inline const float4 status_warning_color = { 1.0f, 0.0f, 0.0f, 1.0f };

		inline const float4 play_button_color = { 0.13f, 0.24f, 0.13f, 1.0f };
		inline const float4 play_button_hovered_color = { 0.18f, 0.32f, 0.18f, 1.0f };
		inline const float4 play_button_active_color = { 0.18f, 0.32f, 0.18f, 1.0f };
		inline const float4 play_button_text_color = { 0.53f, 0.80f, 0.47f, 1.0f };
		inline const float4 stop_button_color = { 0.36f, 0.16f, 0.16f, 1.0f };
		inline const float4 stop_button_hovered_color = { 0.46f, 0.20f, 0.20f, 1.0f };
		inline const float4 stop_button_active_color = { 0.46f, 0.20f, 0.20f, 1.0f };
		inline const float4 stop_button_text_color = { 0.90f, 0.58f, 0.58f, 1.0f };

		inline const float4 terrain_brush_outline_color = { 0.92156863f, 0.74509805f, 0.27450981f, 1.0f };
		inline const float4 terrain_brush_falloff_color = { 0.92156863f, 0.74509805f, 0.27450981f, 0.50196081f };

		inline const float4 animation_timeline_background_color = { 0.15686275f, 0.15686275f, 0.15686275f, 1.0f };
		inline const float4 animation_timeline_border_color = { 0.35294119f, 0.35294119f, 0.35294119f, 1.0f };
		inline const float4 animation_timeline_playhead_color = { 1.0f, 0.86274511f, 0.23529412f, 1.0f };
		inline const float4 animation_timeline_event_color = { 0.31372550f, 0.70588237f, 1.0f, 1.0f };

		inline const float4 localization_missing_color = { 0.45f, 0.12f, 0.12f, 0.55f };
		inline const float4 localization_stale_color = { 0.45f, 0.36f, 0.10f, 0.55f };

		void SetupVisualStudioStyle();
	}
}
