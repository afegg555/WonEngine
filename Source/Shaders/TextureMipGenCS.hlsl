#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_TEXTURE_MIPGEN_PUSHCONSTANT
#include "ShaderInterop_Utility.h"

[numthreads(DISPATCH_THREAD_GROUP_2D, DISPATCH_THREAD_GROUP_2D, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    const bool is_srgb = (mipgenpush.flags & MIPGEN_FLAGS_IS_SRGB) != 0;
    const uint destination_uav = mipgenpush.destination_mip_uav;
    if (dispatch_thread_id.x >= mipgenpush.destination_width || dispatch_thread_id.y >= mipgenpush.destination_height)
    {
        return;
    }

    Texture2D input_texture = bindless_textures[DescriptorIndex((int)mipgenpush.source_mip_srv)];
    RWTexture2D<float4> output_texture = bindless_rwtextures[DescriptorIndex((int)destination_uav)];

    float2 output_resolution = float2(mipgenpush.destination_width, mipgenpush.destination_height);
    float2 uv = (float2(dispatch_thread_id.xy) + 0.5f) / output_resolution;
    float4 color = input_texture.SampleLevel(sampler_linear_clamp, uv, 0.0f);

    if (is_srgb)
    {
        // https://registry.color.org/rgb-registry/srgb
        // linear rgb to srgb
        
        float3 cutoff = step(color.rgb, float3(0.0031308f, 0.0031308f, 0.0031308f)); // <=
        float3 lower = color.rgb * 12.92f;
        float3 upper = 1.055f * pow(abs(color.rgb), 1.0f / 2.4f) - 0.055f;
        color.rgb = lerp(upper, lower, cutoff);
    }

    output_texture[dispatch_thread_id.xy] = color;
}
