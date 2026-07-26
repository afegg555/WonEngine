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
    const float2 q = GetQuadUV(vertex_id);
    const float2 screen = debugdraw2dpush.rect.xy + q * debugdraw2dpush.rect.zw;

    PixelInput output;
    output.pos = float4(screen.x * 2.0f - 1.0f, 1.0f - screen.y * 2.0f, 0.0f, 1.0f);
    output.uv = lerp(debugdraw2dpush.uv_rect.xy, debugdraw2dpush.uv_rect.zw, q);
    return output;
}
