#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_DEBUGDRAW_2D_PUSHCONSTANT
#include "ShaderInterop_DebugDraw.h"

struct PixelInput
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

PixelInput main(uint vertex_id : SV_VertexID)
{
    const ShaderDebugDraw2DItem item = bindless_structured_debugdraw_2d_item[DescriptorIndex(debugdraw2dpush.item_buffer_descriptor)][debugdraw2dpush.item_index];

    const float2 q = GetQuadUV(vertex_id);
    const float2 screen = item.rect.xy + q * item.rect.zw;

    PixelInput output;
    output.pos = float4(screen.x * 2.0f - 1.0f, 1.0f - screen.y * 2.0f, 0.0f, 1.0f);
    output.uv = lerp(item.uv_rect.xy, item.uv_rect.zw, q);
    return output;
}
