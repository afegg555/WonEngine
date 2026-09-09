#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_TEMPORAL_RESOLVE_CONSTANTBUFFER
#include "ShaderInterop_PostProcess.h"

static const float temporal_history_blend = 0.9f;

[numthreads(DISPATCH_THREAD_GROUP_2D, DISPATCH_THREAD_GROUP_2D, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (dispatch_thread_id.x >= temporalresolvecb.resolution.x || dispatch_thread_id.y >= temporalresolvecb.resolution.y)
    {
        return;
    }

    Texture2D current_texture = bindless_textures[DescriptorIndex((int)temporalresolvecb.current_descriptor)];
    RWTexture2D<float> output = bindless_rwtextures_float[DescriptorIndex((int)temporalresolvecb.output_descriptor)];
    RWTexture2D<float> history_output = bindless_rwtextures_float[DescriptorIndex((int)temporalresolvecb.history_output_descriptor)];

    const int2 pixel = int2(dispatch_thread_id.xy);
    const int2 last_pixel = int2(temporalresolvecb.resolution) - 1;

    const float current = current_texture.Load(int3(pixel, 0)).r;

    float neighbor_min = FLT_MAX;
    float neighbor_max = -FLT_MAX;
    float neighbor_sum = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float tap = current_texture.Load(int3(clamp(pixel + int2(x, y), int2(0, 0), last_pixel), 0)).r;
            neighbor_min = min(neighbor_min, tap);
            neighbor_max = max(neighbor_max, tap);
            neighbor_sum += tap;
        }
    }
    const float spatial = neighbor_sum / 9.0f;

    float result = spatial;
    if (temporalresolvecb.motion_descriptor >= 0)
    {
        Texture2D motion_texture = bindless_textures[DescriptorIndex(temporalresolvecb.motion_descriptor)];
        Texture2D history_texture = bindless_textures[DescriptorIndex((int)temporalresolvecb.history_descriptor)];

        const float2 resolution = float2(temporalresolvecb.resolution);
        const float2 uv = (float2(pixel) + 0.5f) / resolution;
        const float2 motion = motion_texture.Load(int3(pixel, 0)).rg;

        const bool invalid = all(motion == motion_vector_previous_transform_invalid_marker);
        const bool unwritten = all(motion == motion_vector_unwritten_marker);
        if (!invalid && !unwritten)
        {
            const float2 previous_uv = uv + motion;
            if (all(previous_uv > 0.0f) && all(previous_uv < 1.0f))
            {
                const float history = history_texture.SampleLevel(sampler_linear_clamp, previous_uv, 0).r;
                const float clamped_history = clamp(history, neighbor_min, neighbor_max);
                result = lerp(current, clamped_history, temporal_history_blend);
            }
        }
    }

    output[pixel] = result;
    history_output[pixel] = result;
}
