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
    const ShaderDebugDraw2DItem item = bindless_structured_debugdraw_2d_item[DescriptorIndex(debugdraw2dpush.item_buffer_descriptor)][debugdraw2dpush.item_index];

    float4 color = UnpackRGBA8(item.color);
    if (item.atlas_index != 0xffffffffu)
    {
        const float coverage = bindless_textures[DescriptorIndex((int)item.atlas_index)].Sample(sampler_point_clamp, input.uv).r;
        color.a *= coverage;
    }
    return color;
}
