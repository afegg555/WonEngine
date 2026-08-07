#pragma once
#include "Types.h"

namespace won::rendering
{
    enum class ViewResizePolicy
    {
        Manual,
        MatchWindow
    };

    enum class AntiAliasingMode : uint8
    {
        None,
        FXAA,
        // TAA, MSAA: future
    };

    enum class TonemapMode : uint8
    {
        Reinhard,
        ACES
    };

    enum class ViewMode : uint8
    {
        Lit,
        Unlit,
        BaseColor,
        WorldNormal,
        Roughness,
        Metallic,
        LightComplexity,
        ShadowCascades,
        Wireframe,
        Overdraw,

        VIEWMODE_COUNT,
    };

    enum ShowFlags : uint32
    {
        Show_Opaque = 1 << 0,
        Show_Transparent = 1 << 1,
        Show_Decals = 1 << 2,
        Show_Particles = 1 << 3,
        Show_Sprites3D = 1 << 4,
        Show_Sprites2D = 1 << 5,
        Show_Shadows = 1 << 6,

        Show_Grid = 1 << 16,
        Show_Colliders = 1 << 17,
        Show_BVH = 1 << 18,
        Show_DDGI = 1 << 19,

        Show_Default = Show_Opaque | Show_Transparent | Show_Decals | Show_Particles | Show_Sprites3D | Show_Sprites2D | Show_Shadows,
    };
}
