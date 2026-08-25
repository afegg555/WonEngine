#include "WaterCommon.hlsli"

PUSHCONSTANT(waterripplesteppush, WaterRippleStepPushConstants);

static const float water_ripple_damping = 0.985f;
static const float water_ripple_max_wave_factor = 0.5f;
static const float water_wetness_damping = 0.995f;
static const float water_wetness_gain = 4.0f;

[numthreads(WATER_RIPPLE_GROUP_SIZE, WATER_RIPPLE_GROUP_SIZE, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
    ShaderWaterZone zone = bindless_structured_water_zone[DescriptorIndex(waterripplesteppush.zone_buffer_descriptor)][waterripplesteppush.zone_index];

    const int resolution = (int)zone.ripple_resolution;
    const int2 texel = (int2)thread_id.xy;
    if (any(texel >= resolution))
    {
        return;
    }

    RWTexture2D<float> height_previous = bindless_rwtextures_float[DescriptorIndex(waterripplesteppush.height_previous_descriptor)];
    RWTexture2D<float> height_current = bindless_rwtextures_float[DescriptorIndex(waterripplesteppush.height_current_descriptor)];
    RWTexture2D<float> wetness = bindless_rwtextures_float[DescriptorIndex(waterripplesteppush.wetness_descriptor)];

    const int last = resolution - 1;
    const float center = height_previous[texel];
    const float left = height_previous[int2(max(texel.x - 1, 0), texel.y)];
    const float right = height_previous[int2(min(texel.x + 1, last), texel.y)];
    const float down = height_previous[int2(texel.x, max(texel.y - 1, 0))];
    const float up = height_previous[int2(texel.x, min(texel.y + 1, last))];

    const float2 texel_size = WaterRippleTexelSize(zone.extent, zone.ripple_resolution);
    const float grid_spacing = min(texel_size.x, texel_size.y);
    const float courant = zone.ripple_speed * waterripplesteppush.step_seconds / grid_spacing;
    const float wave_factor = min(courant * courant, water_ripple_max_wave_factor);

    // Wave simulation update equation : h[t+1] = 2 * h[t] - h[t-1] + c^2 * (L + R + U + D - 4 * h[t])
    const float current_height = (2.0f * center - height_current[texel]
        + wave_factor * (left + right + down + up - 4.0f * center)) * water_ripple_damping; // damping factor to reduce wave amplitude over time
    const float current_wetness = wetness[texel] * water_wetness_damping; // same

    height_current[texel] = current_height;
    wetness[texel] = max(current_wetness, saturate(abs(current_height) * water_wetness_gain));
}
