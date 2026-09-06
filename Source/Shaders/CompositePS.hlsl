#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#include "ColorSpace.hlsli"
#define WON_COMPOSITE_PUSHCONSTANT
#include "ShaderInterop_PostProcess.h"

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(VertexOutput input) : SV_Target0
{
    Texture2D source = bindless_textures[DescriptorIndex((int)compositepush.input_descriptor)];
    float4 color = source.SampleLevel(sampler_point_clamp, input.uv, 0);
    if (compositepush.output_encoding == output_encoding_srgb)
    {
        color.rgb = LinearToSrgb(color.rgb);
    }
    color.a = 1.0f;
    return color;
}
