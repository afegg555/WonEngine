#include "Common.hlsli"

struct PrimitiveVertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

[RootSignature(DEFAULT_ROOTSIGNATURE)]
PrimitiveVertexOutput main(uint vertex_id : SV_VertexID)
{
    PrimitiveVertexOutput output;
    ShaderGeometry geometry = GetGeometry();
    float3 position = bindless_buffers_float3[DescriptorIndex(geometry.position_buffer_descriptor)][vertex_id];
    float4 color = geometry.color_buffer_descriptor >= 0 ? bindless_buffers_float4[DescriptorIndex(geometry.color_buffer_descriptor)][vertex_id] : float4(1.0f, 1.0f, 1.0f, 1.0f);
    float4 world_position = mul(GetInstance().world_transform, float4(position, 1.0f));
    output.position = mul(GetCamera().view_projection, world_position);
    output.color = color;
    return output;
}
