#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_LUMINANCE_REDUCE_CONSTANTBUFFER
#include "ShaderInterop_PostProcess.h"

// Pass 1: each of luminance_reduce_group_count groups grid-strides across the viewport
// region, reducing its samples to a single partial average log-luminance in the partials buffer.
groupshared float g_partial[luminance_reduce_group_size];

[numthreads(luminance_reduce_group_size, 1, 1)]
void main(uint3 group_id : SV_GroupID, uint group_thread_index : SV_GroupIndex, uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    Texture2D source = bindless_textures[DescriptorIndex((int)luminancereducecb.input_descriptor)];
    const uint vp_width = luminancereducecb.viewport_size.x;
    const uint vp_height = luminancereducecb.viewport_size.y;
    const uint pixel_count = vp_width * vp_height;
    const uint total_threads = luminance_reduce_group_count * luminance_reduce_group_size;
    const uint global_id = dispatch_thread_id.x;

    float sum_log = 0.0;
    uint count = 0;
    for (uint idx = global_id; idx < pixel_count; idx += total_threads)
    {
        const uint x = idx % vp_width;
        const uint y = idx / vp_width;
        const float3 color = source.Load(int3(x, y, 0)).rgb;
        const float luminance = Luminance(color);
        sum_log += log(max(luminance, 1e-5));
        count++;
    }
    g_partial[group_thread_index] = (count > 0) ? (sum_log / float(count)) : 0.0;
    GroupMemoryBarrierWithGroupSync();

    for (uint s = luminance_reduce_group_size / 2; s > 0; s >>= 1)
    {
        if (group_thread_index < s)
        {
            g_partial[group_thread_index] += g_partial[group_thread_index + s];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (group_thread_index == 0)
    {
        RWStructuredBuffer<float> partials = bindless_rwbuffers_float[DescriptorIndex((int)luminancereducecb.output_descriptor)];
        partials[group_id.x] = g_partial[0] / float(luminance_reduce_group_size);
    }
}
