#pragma once
#include "Entity.h"
#include "MathTypes.h"
#include "Types.h"

namespace won::ecs
{
    struct PrefabSpawnRequest
    {
        String path;
        float3 position = { 0.0f, 0.0f, 0.0f };
        float yaw = 0.0f;
        Entity parent = INVALID_ENTITY;
        Entity reserved_root = INVALID_ENTITY;
    };

    struct WaterRippleRequest
    {
        float3 position = { 0.0f, 0.0f, 0.0f };
        float strength = 0.0f;
    };
}
