#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_LUMINANCE_REDUCE_CONSTANTBUFFER
#include "ShaderInterop_PostProcess.h"

// Pass 2: reduce the luminance_reduce_group_count partials from pass 1 to a single geometric-mean luminance.
groupshared float g_partial[luminance_reduce_group_count];

[numthreads(luminance_reduce_group_count, 1, 1)]
void main(uint group_thread_index : SV_GroupIndex)
{
    StructuredBuffer<float> partials = bindless_buffers_float[DescriptorIndex((int)luminancereducecb.input_descriptor)];
    g_partial[group_thread_index] = partials[group_thread_index];
    GroupMemoryBarrierWithGroupSync();

    for (uint s = luminance_reduce_group_count / 2; s > 0; s >>= 1)
    {
        if (group_thread_index < s)
        {
            g_partial[group_thread_index] += g_partial[group_thread_index + s];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (group_thread_index == 0)
    {
        RWStructuredBuffer<float> output = bindless_rwbuffers_float[DescriptorIndex((int)luminancereducecb.output_descriptor)];
        output[0] = exp(g_partial[0] / float(luminance_reduce_group_count));
    }
}
