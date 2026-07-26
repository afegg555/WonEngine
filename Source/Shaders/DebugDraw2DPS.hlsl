#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_DEBUGDRAW_2D_PUSHCONSTANT
#include "ShaderInterop_DebugDraw.h"

struct PixelInput
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(PixelInput input) : SV_Target0
{
    float4 color = UnpackRGBA8(debugdraw2dpush.color);
    if (debugdraw2dpush.atlas_index != 0xffffffffu)
    {
        const float coverage = bindless_textures[DescriptorIndex((int)debugdraw2dpush.atlas_index)].Sample(sampler_point_clamp, input.uv).r;
        color.a *= coverage;
    }
    return color;
}
