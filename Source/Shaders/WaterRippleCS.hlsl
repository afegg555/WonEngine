#include "WaterCommon.hlsli"

PUSHCONSTANT(waterripplepush, WaterRipplePushConstants);

static const float water_ripple_damping = 0.985f;
static const float water_ripple_injection_radius_texels = 3.0f;
static const float water_wetness_damping = 0.995f;
static const float water_wetness_gain = 4.0f;

[numthreads(WATER_RIPPLE_GROUP_SIZE, WATER_RIPPLE_GROUP_SIZE, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
    const int2 local = (int2)thread_id.xy;
    if (local.x >= WATER_RIPPLE_RESOLUTION || local.y >= WATER_RIPPLE_RESOLUTION)
    {
        return;
    }

    RWTexture2D<float> height_previous = bindless_rwtextures_float[DescriptorIndex(waterripplepush.height_previous_descriptor)];
    RWTexture2D<float> height_current = bindless_rwtextures_float[DescriptorIndex(waterripplepush.height_current_descriptor)];
    RWTexture2D<float> wetness = bindless_rwtextures_float[DescriptorIndex(waterripplepush.wetness_descriptor)];

    const int2 grid = waterripplepush.window_grid_min + local;
    const int2 texel = WaterWrapTexel(grid);

    const int2 previous_grid_min = waterripplepush.window_grid_min - waterripplepush.window_grid_shift;
    const int2 previous_local = grid - previous_grid_min;
    const bool entered_window = any(previous_local < 0) || any(previous_local >= WATER_RIPPLE_RESOLUTION); // true if this texel was not in the previous window

    float current_height = 0.0f;
    float current_wetness = 0.0f;
    if (!entered_window)
    {
        const float left = height_previous[WaterWrapTexel(grid + int2(-1, 0))];
        const float right = height_previous[WaterWrapTexel(grid + int2(1, 0))];
        const float down = height_previous[WaterWrapTexel(grid + int2(0, -1))];
        const float up = height_previous[WaterWrapTexel(grid + int2(0, 1))];

        // Wave simulation update equation : h[t+1] = 2 * h[t] - h[t-1] + c^2 * (L + R + U + D - 4 * h[t])
        // if we use c^2 = 0.5 simplification        
        // h[t+1] = 0.5 * (L + R + U + D) - h[t-1]
        current_height = ((left + right + down + up) * 0.5f - height_current[texel]) * water_ripple_damping; // damping factor to reduce wave amplitude over time
        current_wetness = wetness[texel] * water_wetness_damping; // same
    }

    for (uint i = 0; i < waterripplepush.injection_count; ++i)
    {
        ShaderWaterRipple injection = bindless_structured_water_ripple[DescriptorIndex(waterripplepush.injection_descriptor)][i];
        const float2 injection_grid = WaterSimulationGrid(injection.position);
        const float distance_texels = length(((float2) grid + 0.5f) - injection_grid); // distance from the center of the texel to the injection point in texels
        const float falloff = saturate(1.0f - distance_texels / water_ripple_injection_radius_texels);
        current_height += injection.strength * falloff * falloff;
    }

    height_current[texel] = current_height;
    wetness[texel] = max(current_wetness, saturate(abs(current_height) * water_wetness_gain));
}
