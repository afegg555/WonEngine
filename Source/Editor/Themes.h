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

		void SetupVisualStudioStyle();
	}
}