#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "MathTypes.h"

namespace won::ecs
{
    class Scene;
}

namespace won::wind
{
    struct WindField
    {
        float3 global_velocity = { 0.0f, 0.0f, 0.0f };

        float3 Sample(const float3& position) const;
    };

    WONENGINE_API WindField BuildWindField(const ecs::Scene& scene);
}
