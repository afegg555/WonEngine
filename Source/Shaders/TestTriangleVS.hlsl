#include "Common.hlsli"

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VertexOutput main(uint vertex_id : SV_VertexID)
{
    VertexOutput output = (VertexOutput)0;
    // Vertex Pulling path: read index/vertex streams from bindless SRVs without IA vertex buffers.
    ShaderGeometry geometry = GetGeometry(object_push_constants.geometry_index);
    //uint pulled_index = bindless_structured_index[DescriptorIndex(geometry.index_buffer_descriptor)][vertex_id];
    float3 position = bindless_structured_position[DescriptorIndex(geometry.position_buffer_descriptor)][vertex_id];
    if (geometry.texcoord_buffer_descriptor >= 0)
    {
        output.uv = bindless_structured_texcoord[DescriptorIndex(geometry.texcoord_buffer_descriptor)][vertex_id];
    }
    ShaderInstance shader_object = GetInstance(object_push_constants.instance_index);
    ShaderCamera camera = GetCamera();
    output.position = mul(shader_object.local_to_world, float4(position, 1.0f));
    output.position = mul(camera.view_projection, output.position);
    return output;
}
