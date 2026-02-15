#pragma once
#include "MathTypes.h"
#include <array>

namespace won::math
{
    struct Aabb
    {
        float3 min = {};
        float3 max = {};
    };

    struct Ray
    {
        float3 origin = {};
        float3 direction = { 0.0f, 0.0f, 1.0f };
    };

    struct Plane
    {
        float3 normal = { 0.0f, 1.0f, 0.0f };
        float distance = 0.0f;
    };

    struct Frustum
    {
        std::array<Plane, 6> planes = {};
    };

    struct Sphere
    {
        float3 center = {};
        float radius = 0.0f;
    };
}
