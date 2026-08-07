#ifndef NOISE_COMMON
#define NOISE_COMMON

inline float Hash(float2 position)
{
    // !! Note: sin based hash might vary across driver implementations
    // Standard boilerplate snippet widely used for pseudo-random noise generation
    return frac(sin(dot(position, float2(127.1f, 311.7f))) * 43758.5453f); // [0, 1)
}

inline float2 Hash2(float2 position)
{
    // !! Note: sin based hash might vary across driver implementations
    // Standard boilerplate snippet widely used for pseudo-random noise generation
    float2 hashed = float2(dot(position, float2(127.1f, 311.7f)), dot(position, float2(269.5f, 183.3f)));
    return frac(sin(hashed) * 43758.5453f); // [0, 1)
}

inline float ValueNoise(float2 position)
{
    float2 cell = floor(position);
    float2 cell_uv = frac(position);
    cell_uv = cell_uv * cell_uv * (3.0f - 2.0f * cell_uv); // cubic smoothstep interpolation

    float corner00 = Hash(cell);
    float corner10 = Hash(cell + float2(1.0f, 0.0f));
    float corner01 = Hash(cell + float2(0.0f, 1.0f));
    float corner11 = Hash(cell + float2(1.0f, 1.0f));

    return lerp(lerp(corner00, corner10, cell_uv.x), lerp(corner01, corner11, cell_uv.x), cell_uv.y);
}

inline float PerlinNoise(float2 position)
{
    // gradient noise: each lattice point carries a direction, the value is the interpolated dot with the offset to it
    float2 cell = floor(position);
    float2 cell_uv = frac(position);
    float2 blend = cell_uv * cell_uv * cell_uv * (cell_uv * (cell_uv * 6.0f - 15.0f) + 10.0f); // quintic smoothstep interpolation

    float corner00 = dot(Hash2(cell) * 2.0f - 1.0f, cell_uv);
    float corner10 = dot(Hash2(cell + float2(1.0f, 0.0f)) * 2.0f - 1.0f, cell_uv - float2(1.0f, 0.0f));
    float corner01 = dot(Hash2(cell + float2(0.0f, 1.0f)) * 2.0f - 1.0f, cell_uv - float2(0.0f, 1.0f));
    float corner11 = dot(Hash2(cell + float2(1.0f, 1.0f)) * 2.0f - 1.0f, cell_uv - float2(1.0f, 1.0f));

    float value = lerp(lerp(corner00, corner10, blend.x), lerp(corner01, corner11, blend.x), blend.y);
    return value * 0.5f + 0.5f; // [0, 1)
}

static const float simplex_skew = 0.3660254037844386f; // (sqrt(3) - 1) / 2
static const float simplex_unskew = 0.2113248654051871f; // (3 - sqrt(3)) / 6
static const float simplex_scale = 70.0f; // brings the summed corner contributions into [-1, 1]

inline float SimplexNoise(float2 position)
{
    // gradient noise on a triangular lattice: 3 corners per cell instead of 4, with fewer axis aligned artifacts than perlin
    float skew = (position.x + position.y) * simplex_skew;
    float2 cell = floor(position + skew);
    float unskew = (cell.x + cell.y) * simplex_unskew;
    float2 offset0 = position - cell + unskew;

    float2 corner_step = offset0.x > offset0.y ? float2(1.0f, 0.0f) : float2(0.0f, 1.0f); // which half of the cell the sample falls in
    float2 offset1 = offset0 - corner_step + simplex_unskew;
    float2 offset2 = offset0 - 1.0f + 2.0f * simplex_unskew;

    float3 falloff = max(0.5f - float3(dot(offset0, offset0), dot(offset1, offset1), dot(offset2, offset2)), 0.0f);
    falloff = falloff * falloff * falloff * falloff;

    float3 gradient_dot = float3(
        dot(Hash2(cell) * 2.0f - 1.0f, offset0),
        dot(Hash2(cell + corner_step) * 2.0f - 1.0f, offset1),
        dot(Hash2(cell + float2(1.0f, 1.0f)) * 2.0f - 1.0f, offset2));

    return dot(falloff, gradient_dot) * simplex_scale * 0.5f + 0.5f; // [0, 1)
}

inline float FBMValueNoise(float2 position, uint octaves, float lacunarity, float persistence)
{
    // fractal brownian motion
    float value = 0.0f;
    float weight_sum = 0.0f;
    float amplitude = 1.0f;

    for (uint octave = 0; octave < octaves; ++octave)
    {
        value += amplitude * ValueNoise(position);
        weight_sum += amplitude;

        position *= lacunarity;
        amplitude *= persistence;
    }

    return value / max(weight_sum, 1e-5f); // [0, 1)
}

inline float FBMPerlinNoise(float2 position, uint octaves, float lacunarity, float persistence)
{
    // fractal brownian motion
    float value = 0.0f;
    float weight_sum = 0.0f;
    float amplitude = 1.0f;

    for (uint octave = 0; octave < octaves; ++octave)
    {
        value += amplitude * PerlinNoise(position);
        weight_sum += amplitude;

        position *= lacunarity;
        amplitude *= persistence;
    }

    return value / max(weight_sum, 1e-5f); // [0, 1)
}

inline float FBMSimplexNoise(float2 position, uint octaves, float lacunarity, float persistence)
{
    // fractal brownian motion
    float value = 0.0f;
    float weight_sum = 0.0f;
    float amplitude = 1.0f;

    for (uint octave = 0; octave < octaves; ++octave)
    {
        value += amplitude * SimplexNoise(position);
        weight_sum += amplitude;

        position *= lacunarity;
        amplitude *= persistence;
    }

    return value / max(weight_sum, 1e-5f); // [0, 1)
}

#endif
