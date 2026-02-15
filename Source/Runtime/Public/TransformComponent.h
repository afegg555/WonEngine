#pragma once
#include "Types.h"

namespace won::ecs
{
    struct TransformComponent
    {
        float3 position = {};
        float4 rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
        float3 scale = { 1.0f, 1.0f, 1.0f };
    };
}
