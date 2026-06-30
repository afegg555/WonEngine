#include "Noise.h"
#include "MathUtils.h"

#include <cmath>

namespace won::noise
{
    namespace
    {
        constexpr uint8 permutation_table[256] =
        {
            151, 160, 137, 91, 90, 15, 131, 13, 201, 95, 96, 53, 194, 233, 7, 225,
            140, 36, 103, 30, 69, 142, 8, 99, 37, 240, 21, 10, 23, 190, 6, 148,
            247, 120, 234, 75, 0, 26, 197, 62, 94, 252, 219, 203, 117, 35, 11, 32,
            57, 177, 33, 88, 237, 149, 56, 87, 174, 20, 125, 136, 171, 168, 68, 175,
            74, 165, 71, 134, 139, 48, 27, 166, 77, 146, 158, 231, 83, 111, 229, 122,
            60, 211, 133, 230, 220, 105, 92, 41, 55, 46, 245, 40, 244, 102, 143, 54,
            65, 25, 63, 161, 1, 216, 80, 73, 209, 76, 132, 187, 208, 89, 18, 169,
            200, 196, 135, 130, 116, 188, 159, 86, 164, 100, 109, 198, 173, 186, 3, 64,
            52, 217, 226, 250, 124, 123, 5, 202, 38, 147, 118, 126, 255, 82, 85, 212,
            207, 206, 59, 227, 47, 16, 58, 17, 182, 189, 28, 42, 223, 183, 170, 213,
            119, 248, 152, 2, 44, 154, 163, 70, 221, 153, 101, 155, 167, 43, 172, 9,
            129, 22, 39, 253, 19, 98, 108, 110, 79, 113, 224, 232, 178, 185, 112, 104,
            218, 246, 97, 228, 251, 34, 242, 193, 238, 210, 144, 12, 191, 179, 162, 241,
            81, 51, 145, 235, 249, 14, 239, 107, 49, 192, 214, 31, 181, 199, 106, 157,
            184, 84, 204, 176, 115, 121, 50, 45, 127, 4, 150, 254, 138, 236, 205, 93,
            222, 114, 67, 29, 24, 72, 243, 141, 128, 195, 78, 66, 215, 61, 156, 180,
        };

        constexpr float gradient_table[8][2] =
        {
            { 1.0f, 0.0f },
            { -1.0f, 0.0f },
            { 0.0f, 1.0f },
            { 0.0f, -1.0f },
            { 0.70710678f, 0.70710678f },
            { -0.70710678f, 0.70710678f },
            { 0.70710678f, -0.70710678f },
            { -0.70710678f, -0.70710678f },
        };
        constexpr float perlin_2d_gradient_scale = math::sqrt_2;
        constexpr uint32 octave_seed_stride = 1013u;

        float Fade(float t)
        {
			// 6t^5 - 15t^4 + 10t^3
            return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
        }

        uint8 Permute(int32 value, uint32 seed)
        {
            return permutation_table[(static_cast<uint32>(value) + seed) & 255u];
        }

        uint8 Permute2D(int32 x, int32 y, uint32 seed)
        {
            return Permute(x + Permute(y, seed), seed);
        }

        float DotGradient(uint8 gradient_index, float offset_x, float offset_y)
        {
            const float* gradient = gradient_table[gradient_index & 7u];
            return gradient[0] * offset_x + gradient[1] * offset_y;
        }
    }

    float SineWave2D(float x, float y, uint32 seed)
    {
        const float seed_offset = static_cast<float>(seed) * 0.123f;
        float value = 0.0f;
        value += 0.6f * std::sin((x * 3.0f) + seed_offset) * std::cos((y * 3.0f) + seed_offset);
        value += 0.3f * std::sin((x * 7.0f) + seed_offset * 2.0f) * std::cos((y * 5.0f) + seed_offset);
        value += 0.1f * std::sin((x * 13.0f) + seed_offset) * std::cos((y * 11.0f) + seed_offset * 0.5f);
        return value;
    }

    float Perlin2D(float x, float y, uint32 seed)
    {
		// psuedo-random: same output for same seed, different output for different seed
		// coherent: no discontinuities, smooth transitions between values
        const int32 x0 = static_cast<int32>(std::floor(x));
        const int32 y0 = static_cast<int32>(std::floor(y));
        const int32 x1 = x0 + 1;
        const int32 y1 = y0 + 1;

        const float fx = x - static_cast<float>(x0);
        const float fy = y - static_cast<float>(y0);
		const float u = Fade(fx); // smooth interpolation factor
        const float v = Fade(fy);

        const uint32 seed_offset = seed & 255u;
		const float n00 = DotGradient(Permute2D(x0, y0, seed_offset), fx, fy); // dot product of gradient and offset
        const float n10 = DotGradient(Permute2D(x1, y0, seed_offset), fx - 1.0f, fy);
        const float n01 = DotGradient(Permute2D(x0, y1, seed_offset), fx, fy - 1.0f);
        const float n11 = DotGradient(Permute2D(x1, y1, seed_offset), fx - 1.0f, fy - 1.0f);

        const float nx0 = math::Lerp(n00, n10, u);
        const float nx1 = math::Lerp(n01, n11, u);
        // Scale 2D unit-gradient dot values toward the conventional [-1, 1] noise range.
        return math::Clamp(math::Lerp(nx0, nx1, v) * perlin_2d_gradient_scale, -1.0f, 1.0f);
    }

    float FractalBrownianMotion2D(float x, float y, const FractalNoiseDesc& desc)
    {
        float value = 0.0f;
        float amplitude = 1.0f;
        float amplitude_sum = 0.0f;
        float frequency = desc.frequency;
        const uint32 octave_count = desc.octaves > 0 ? desc.octaves : 1;

        for (uint32 octave = 0; octave < octave_count; ++octave)
        {
            value += Perlin2D(x * frequency, y * frequency, desc.seed + octave * octave_seed_stride) * amplitude;
            amplitude_sum += amplitude;
            amplitude *= desc.persistence;
            frequency *= desc.lacunarity;
        }

        return amplitude_sum > 0.0f ? math::Clamp(value / amplitude_sum, -1.0f, 1.0f) : 0.0f;
    }

    float Ridged2D(float x, float y, const FractalNoiseDesc& desc)
    {
        float value = 0.0f;
        float amplitude = 1.0f;
        float amplitude_sum = 0.0f;
        float frequency = desc.frequency;
        const uint32 octave_count = desc.octaves > 0 ? desc.octaves : 1;

        for (uint32 octave = 0; octave < octave_count; ++octave)
        {
            const float ridge = 1.0f - std::abs(Perlin2D(x * frequency, y * frequency, desc.seed + octave * octave_seed_stride));
            value += ridge * ridge * amplitude;
            amplitude_sum += amplitude;
            amplitude *= desc.persistence;
            frequency *= desc.lacunarity;
        }

        return amplitude_sum > 0.0f ? math::Saturate(value / amplitude_sum) : 0.0f;
    }
}
