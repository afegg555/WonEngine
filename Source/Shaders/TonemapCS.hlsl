#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_TONEMAP_PUSHCONSTANT
#include "ShaderInterop_PostProcess.h"

// ACES fitted curve from Krzysztof Narkowicz
float3 ACESFilm(float3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}

[numthreads(DISPATCH_THREAD_GROUP_2D, DISPATCH_THREAD_GROUP_2D, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (dispatch_thread_id.x >= tonemappush.resolution.x || dispatch_thread_id.y >= tonemappush.resolution.y)
    {
        return;
    }

    Texture2D source = bindless_textures[DescriptorIndex((int)tonemappush.input_descriptor)];
    RWTexture2D<float4> destination = bindless_rwtextures[DescriptorIndex((int)tonemappush.output_descriptor)];

    uint2 pixel_coord = dispatch_thread_id.xy;
    float4 hdr_color = source.Load(int3(pixel_coord, 0));

    float3 ldr_color = float3(0.f, 0.f, 0.f);

    if (tonemappush.tonemap_type == TONEMAP_TYPE_REINHARD) // this is ok, no branch divergence happens
    {
        // Reinhard
        ldr_color = hdr_color.rgb / (1.0f + hdr_color.rgb);
    }
    else if (tonemappush.tonemap_type == TONEMAP_TYPE_ACES)
    {
        // ACES
        ldr_color = ACESFilm(hdr_color.rgb);
    }
    else if (tonemappush.tonemap_type == TONEMAP_TYPE_NONE)
    {
        ldr_color = saturate(hdr_color.rgb);
    }

    destination[pixel_coord] = float4(ldr_color, hdr_color.a);
}
