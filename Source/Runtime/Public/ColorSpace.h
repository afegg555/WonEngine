#pragma once
#include "MathTypes.h"

#include <cmath>

namespace won::color
{
    inline float SrgbToLinear(float value)
    {
        const float color = value > 0.0f ? value : 0.0f;
        return color <= 0.04045f
            ? color / 12.92f
            : std::pow((color + 0.055f) / 1.055f, 2.4f);
    }

    inline float LinearToSrgb(float value)
    {
        const float color = value > 0.0f ? value : 0.0f;
        return color <= 0.0031308f
            ? color * 12.92f
            : 1.055f * std::pow(color, 1.0f / 2.4f) - 0.055f;
    }

    inline float3 SrgbToLinear(const float3& value)
    {
        return {
            SrgbToLinear(value.x),
            SrgbToLinear(value.y),
            SrgbToLinear(value.z)
        };
    }

    inline float3 LinearToSrgb(const float3& value)
    {
        return {
            LinearToSrgb(value.x),
            LinearToSrgb(value.y),
            LinearToSrgb(value.z)
        };
    }
}
