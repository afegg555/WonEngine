#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "MathTypes.h"

namespace won::ecs
{
    class Scene;
}

namespace won::water
{
    struct SurfaceSample
    {
        float height = 0.0f;
        float3 normal = { 0.0f, 1.0f, 0.0f };
        float3 velocity = { 0.0f, 0.0f, 0.0f };
        bool valid = false;
    };

    SurfaceSample SampleSurface(ecs::Scene& scene, float2 world_xz);
}
