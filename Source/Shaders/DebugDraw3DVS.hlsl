#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_DEBUGDRAW_3D_PUSHCONSTANT
#include "ShaderInterop_DebugDraw.h"

struct PixelInput
{
    float4 pos : SV_Position;
    float4 color : COLOR0;
};

PixelInput main(uint vertex_id : SV_VertexID)
{
    ShaderCamera camera = GetCamera();
    StructuredBuffer<float4> vertices = bindless_buffers_float4[DescriptorIndex(debugdraw3dpush.vertex_buffer)];
    const float4 packed = vertices[vertex_id];
    const uint color = asuint(packed.w);

    PixelInput output;
    output.pos = mul(camera.view_projection, float4(packed.xyz, 1.0f));
    output.color = float4(
        float((color >> 24) & 0xff) / 255.0f,
        float((color >> 16) & 0xff) / 255.0f,
        float((color >> 8) & 0xff) / 255.0f,
        float(color & 0xff) / 255.0f);
    return output;
}
