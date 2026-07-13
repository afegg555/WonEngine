#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_DEBUGTEXT_PUSHCONSTANT
#include "ShaderInterop_DebugText.h"

struct PixelInput
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(PixelInput input) : SV_Target0
{
    float4 color = UnpackRGBA8(debugtextpush.color);
    const float coverage = bindless_textures[DescriptorIndex((int)debugtextpush.atlas_index)].Sample(sampler_point_clamp, input.uv).r;
    color.a *= coverage;
    return color;
}
