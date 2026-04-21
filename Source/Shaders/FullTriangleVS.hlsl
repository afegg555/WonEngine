#include "Common.hlsli"

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOutput main(uint vertex_id : SV_VertexID)
{
    VertexOutput output = (VertexOutput)0;
    float2 pos = float2((vertex_id << 1) & 2, vertex_id & 2);
    output.uv = pos * 0.5f;
    output.position = float4(pos * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}
