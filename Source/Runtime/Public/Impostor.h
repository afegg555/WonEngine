#pragma once
#include "Primitives.h"
#include "Types.h"
#include "RuntimeExport.h"

namespace won::impostor
{
    enum class ImpostorLayout : uint8
    {
		Hemi, // only for z > 0 directions(only top down view), more precision for top down view
        Full
    };

    // get capture/view direction for atlas cell (x, y): decodes the cell-center to the direction that tile was rendered from.
    WONENGINE_API float3 CellCaptureDirection(uint32 x, uint32 y, uint32 grid_size, ImpostorLayout layout = ImpostorLayout::Hemi);
}
