#include "Impostor.h"

#include "MathUtils.h"

namespace won::impostor
{
    float3 CellCaptureDirection(uint32 x, uint32 y, uint32 grid_size, ImpostorLayout layout)
    {
        if (grid_size == 0)
        {
            return float3(0.0f, 0.0f, 1.0f);
        }
        const float2 uv = float2((static_cast<float>(x) + 0.5f) / static_cast<float>(grid_size),
            (static_cast<float>(y) + 0.5f) / static_cast<float>(grid_size));
        const float2 encoded = float2(uv.x * 2.0f - 1.0f, uv.y * 2.0f - 1.0f);
        return layout == ImpostorLayout::Full
            ? math::DecodeOctahedralDirection(encoded)
            : math::DecodeHemiOctahedralDirection(encoded);
    }
}
