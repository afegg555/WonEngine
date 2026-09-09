#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_LINEARIZE_DEPTH_PUSHCONSTANT
#include "ShaderInterop_PostProcess.h"

[numthreads(DISPATCH_THREAD_GROUP_2D, DISPATCH_THREAD_GROUP_2D, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    Texture2D depth_texture = bindless_textures[DescriptorIndex((int)linearizedepthpush.depth_descriptor)];
    RWTexture2D<float> output = bindless_rwtextures_float[DescriptorIndex((int)linearizedepthpush.output_descriptor)];

    uint2 resolution;
    output.GetDimensions(resolution.x, resolution.y);
    if (dispatch_thread_id.x >= resolution.x || dispatch_thread_id.y >= resolution.y)
    {
        return;
    }

    const int2 pixel = int2(dispatch_thread_id.xy);
    const float device_depth = depth_texture.Load(int3(pixel, 0)).r;
    if (device_depth <= 0.0f) // reversed, 0 means far plane
    {
        output[pixel] = GetCamera().z_far;
        return;
    }

    const float2 uv = (float2(pixel) + 0.5f) / float2(resolution);
    const float3 world = ScreenUVToWorld(uv, device_depth);
    output[pixel] = dot(world - GetCamera().position, GetCamera().forward);
}
