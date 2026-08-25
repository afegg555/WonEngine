#ifndef WON_SHADERINTEROP_WATER_H
#define WON_SHADERINTEROP_WATER_H

#ifndef WON_DISABLE_RENDERER_PUSHCONSTANT
#define WON_DISABLE_RENDERER_PUSHCONSTANT
#endif

#include "ShaderInterop.h"

#define WATER_RIPPLE_GROUP_SIZE 8
#define WATER_RIPPLE_MIN_RESOLUTION 64u
#define WATER_RIPPLE_MAX_RESOLUTION 1024u
#define WATER_WAVE_GRAVITY 9.81f
#define WATER_WAVE_LENGTH_FALLOFF 0.7f
#define WATER_WAVE_AMPLITUDE_FALLOFF 0.6f
#define WATER_WAVE_MIN_COUNT 1u
#define WATER_WAVE_MAX_COUNT 8u
#define WATER_INFO_NO_BODY -1.0f
#define WATER_INFO_HEIGHT_RANGE 4096.0f
#define WATER_TILE_NEIGHBOR_NEG_X 1
#define WATER_TILE_NEIGHBOR_POS_X 2
#define WATER_TILE_NEIGHBOR_NEG_Z 4
#define WATER_TILE_NEIGHBOR_POS_Z 8

struct WaterInfoPushConstants
{
    uint zone_buffer_descriptor;
    uint body_buffer_descriptor;
    uint zone_index;
    uint first_body;
    float max_vertex_spacing;
    uint writes_body_index;

#ifdef __cplusplus
    inline void Init()
    {
        zone_buffer_descriptor = 0;
        body_buffer_descriptor = 0;
        zone_index = 0;
        first_body = 0;
        max_vertex_spacing = 0.0f;
        writes_body_index = 0;
    }
#endif
};

struct WaterPushConstants
{
    uint depth_descriptor;
    uint scene_color_descriptor;
    uint body_buffer_descriptor;
    uint zone_buffer_descriptor;
    uint zone_index;
    uint info_texture_descriptor;
    uint tile_buffer_descriptor;
    uint first_tile;
    uint tile_resolution;

#ifdef __cplusplus
    inline void Init()
    {
        depth_descriptor = 0;
        scene_color_descriptor = 0;
        body_buffer_descriptor = 0;
        zone_buffer_descriptor = 0;
        zone_index = 0;
        info_texture_descriptor = 0;
        tile_buffer_descriptor = 0;
        first_tile = 0;
        tile_resolution = 0;
    }
#endif
};

struct WaterRippleStepPushConstants
{
    uint zone_buffer_descriptor;
    uint zone_index;

    uint height_current_descriptor;
    uint height_previous_descriptor;
    uint wetness_descriptor;
    float step_seconds;

#ifdef __cplusplus
    inline void Init()
    {
        zone_buffer_descriptor = 0;
        zone_index = 0;
        height_current_descriptor = 0;
        height_previous_descriptor = 0;
        wetness_descriptor = 0;
        step_seconds = 0.0f;
    }
#endif
};

struct WaterRippleSplatPushConstants
{
    uint zone_buffer_descriptor;
    uint zone_index;
    uint injection_descriptor;

#ifdef __cplusplus
    inline void Init()
    {
        zone_buffer_descriptor = 0;
        zone_index = 0;
        injection_descriptor = 0;
    }
#endif
};

#ifdef __cplusplus
inline float2 operator+(const float2& a, const float2& b) { return float2(a.x + b.x, a.y + b.y); }
inline float2 operator-(const float2& a, const float2& b) { return float2(a.x - b.x, a.y - b.y); }
inline float2 operator*(const float2& a, float s) { return float2(a.x * s, a.y * s); }
inline float2 operator*(float s, const float2& a) { return float2(a.x * s, a.y * s); }
inline float2& operator+=(float2& a, const float2& b) { a.x += b.x; a.y += b.y; return a; }
inline float dot(const float2& a, const float2& b) { return a.x * b.x + a.y * b.y; }
#endif

struct WaterSurfaceSample
{
    float height;
    float2 horizontal;
    float2 gradient;
    float2 velocity_horizontal;
    float velocity_vertical;
};

// Gerstner wave: 
// phase = k * dot(D, P) - omega * t
// k = 2 * PI / L
// omega = sqrt(g * k)
// height = A * sin(phase)
// horizontal = D * q * cos(phase)
// q = steepness / (k * wave_count)
// P is the undisplaced world xz position, D is travel direction, L is wavelength, A is amplitude, t is time, and g is gravity
// !! Compiled by both HLSL and C++. amplitude_scale is a render only distance fade, so the cpu query passes 1.
inline WaterSurfaceSample EvaluateWaterSurface(float2 world_xz, float2 base_direction, float direction_spread,
    uint wave_count, float base_length, float base_amplitude, float steepness, float time, float amplitude_scale)
{
    WaterSurfaceSample result;
    result.height = 0.0f;
    result.horizontal = float2(0.0f, 0.0f);
    result.gradient = float2(0.0f, 0.0f);
    result.velocity_horizontal = float2(0.0f, 0.0f);
    result.velocity_vertical = 0.0f;

    float wavelength = base_length;
    float amplitude = base_amplitude * amplitude_scale;
    
    const float center = (float(wave_count) - 1.0f) * 0.5f;

    for (uint i = 0u; i < wave_count; ++i)
    {
        const float angle = direction_spread * (float(i) - center);
        const float angle_sin = sin(angle);
        const float angle_cos = cos(angle);
        const float2 direction = float2(base_direction.x * angle_cos - base_direction.y * angle_sin,
                                        base_direction.x * angle_sin + base_direction.y * angle_cos);

        const float k = PI * 2.0f / wavelength;
        const float omega = sqrt(WATER_WAVE_GRAVITY * k);
        const float phase = k * dot(direction, world_xz) - omega * time;
        const float phase_sin = sin(phase);
        const float phase_cos = cos(phase);
        const float q = steepness / (k * float(wave_count));

        result.height += amplitude * phase_sin;
        result.gradient += direction * (amplitude * k * phase_cos);
        result.velocity_vertical += amplitude * omega * -phase_cos;
        result.horizontal += direction * (q * phase_cos);
        result.velocity_horizontal += direction * (q * omega * phase_sin);

        wavelength *= WATER_WAVE_LENGTH_FALLOFF;
        amplitude *= WATER_WAVE_AMPLITUDE_FALLOFF;
    }
    return result;
}

#ifdef __cplusplus
static_assert(sizeof(WaterPushConstants) == 36, "WaterPushConstants layout mismatch");
static_assert(sizeof(WaterRippleStepPushConstants) == 24, "WaterRippleStepPushConstants layout mismatch");
static_assert(sizeof(WaterRippleSplatPushConstants) == 12, "WaterRippleSplatPushConstants layout mismatch");
#endif

#endif
