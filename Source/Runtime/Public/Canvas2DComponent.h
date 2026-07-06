#pragma once
#include "MathTypes.h"
#include "Types.h"

namespace won::ecs
{
    enum class UIScaleMode
    {
        ConstantPixelSize,
        ScaleWithScreenSize,
    };

    struct Canvas2DComponent
    {
        enum class RenderMode
        {
            ScreenOverlay,
            // WorldSpace,   // not implemented yet
            // ScreenCamera, // not implemented yet
        };

        RenderMode render_mode = RenderMode::ScreenOverlay;
        UIScaleMode scale_mode = UIScaleMode::ScaleWithScreenSize;
        float2 reference_resolution = { 1920.0f, 1080.0f };
        int32 sort_order = 0;
        uint32 layer_mask = 0xFFFFFFFF;
    };
}
