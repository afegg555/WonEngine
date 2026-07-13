#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_DEBUGTEXT_PUSHCONSTANT
#include "ShaderInterop_DebugText.h"

struct PixelInput
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

PixelInput main(uint vertex_id : SV_VertexID)
{
    const float2 q = GetQuadUV(vertex_id);
    const float2 screen = debugtextpush.rect.xy + q * debugtextpush.rect.zw;

    PixelInput output;
    output.pos = float4(screen.x * 2.0f - 1.0f, 1.0f - screen.y * 2.0f, 0.0f, 1.0f);
    output.uv = lerp(debugtextpush.uv_rect.xy, debugtextpush.uv_rect.zw, q);
    return output;
}
