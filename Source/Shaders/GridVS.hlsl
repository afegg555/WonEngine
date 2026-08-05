#include "Common.hlsli"

struct VertexOutput
{
    float4 position : SV_Position;
    float3 near_point : NEARPOINT;
    float3 far_point : FARPOINT;
};

float3 UnprojectPoint(float2 ndc, float depth)
{
    float4 world_position = mul(GetCamera().inv_view_projection, float4(ndc, depth, 1.0f));
    return world_position.xyz / max(abs(world_position.w), 0.000001f);
}

VertexOutput main(uint vertex_id : SV_VertexID)
{
    float2 vertices[3] = {
        float2(-1.0f, 1.0f),
        float2(3.0f, 1.0f),
        float2(-1.0f, -3.0f)
    };

    float2 ndc = vertices[vertex_id];

    VertexOutput output;
    output.position = float4(ndc, 0.0f, 1.0f);
    output.near_point = UnprojectPoint(ndc, 1.0f);
    output.far_point = UnprojectPoint(ndc, 0.0f);
    return output;
}
