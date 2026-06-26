#pragma once
#include "RuntimeExport.h"
#include "Types.h"

namespace won::noise
{
    struct FractalNoiseDesc // fractal noise means layering multiple octaves of noise together to create more complex patterns
    {
        float frequency = 1.0f;
        uint32 octaves = 4;
        float lacunarity = 2.0f;
        float persistence = 0.5f;
        uint32 seed = 0;
    };

    WONENGINE_API float SineWave2D(float x, float y, uint32 seed);
    WONENGINE_API float Perlin2D(float x, float y, uint32 seed); // psuedo-random, coherent
    WONENGINE_API float FractalBrownianMotion2D(float x, float y, const FractalNoiseDesc& desc); // shortened to fBm, perlin noise with multiple octaves
    WONENGINE_API float Ridged2D(float x, float y, const FractalNoiseDesc& desc); // similar to fBm but with ridged noise, which produces sharper features
}
