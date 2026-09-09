#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_LINEAR_DEPTH_MIP_PUSHCONSTANT
#include "ShaderInterop_PostProcess.h"

[numthreads(DISPATCH_THREAD_GROUP_2D, DISPATCH_THREAD_GROUP_2D, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    Texture2D source = bindless_textures[DescriptorIndex((int)lineardepthmippush.input_descriptor)];
    RWTexture2D<float> output = bindless_rwtextures_float[DescriptorIndex((int)lineardepthmippush.output_descriptor)];

    uint2 dst_resolution;
    output.GetDimensions(dst_resolution.x, dst_resolution.y);
    if (dispatch_thread_id.x >= dst_resolution.x || dispatch_thread_id.y >= dst_resolution.y)
    {
        return;
    }

    const int2 base = int2(dispatch_thread_id.xy) * 2;

    const float d0 = source.Load(int3(base + int2(0, 0), 0)).r;
    const float d1 = source.Load(int3(base + int2(1, 0), 0)).r;
    const float d2 = source.Load(int3(base + int2(0, 1), 0)).r;
    const float d3 = source.Load(int3(base + int2(1, 1), 0)).r;

    output[int2(dispatch_thread_id.xy)] = min(min(d0, d1), min(d2, d3));
}
