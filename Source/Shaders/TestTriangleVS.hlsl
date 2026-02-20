#include "Common.hlsli"

float4 main(uint vertex_id : SV_VertexID) : SV_Position
{
    // Vertex Pulling path: read index/vertex streams from bindless SRVs without IA vertex buffers.
    ShaderGeometry geometry = GetGeometry(object_push_constants.geometry_index);
    uint pulled_index = bindless_structured_index[DescriptorIndex(geometry.index_buffer_descriptor)][vertex_id];
    float3 position = bindless_structured_position[DescriptorIndex(geometry.position_buffer_descriptor)][pulled_index];
    ShaderObject shader_object = GetInstance(object_push_constants.instance_index);
    return mul(float4(position, 1.0f), shader_object.local_to_world);
}
