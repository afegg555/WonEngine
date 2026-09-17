#include "TerrainCommon.hlsli"

TerrainPixelInput main(TerrainVertexInput input)
{
    TerrainPixelInput output;
    ShaderGeometry geometry = GetGeometry();
    ShaderTransform transform = GetTransform(GetTerrainTransformIndex(input.instance_id));
    float4 world_position = mul(transform.world_transform, float4(GetTerrainPosition(input.vertex_id), 1.0f));
    output.worldpos = world_position.xyz;
    output.pos = mul(GetCamera().view_projection, world_position);

    output.uvsets = geometry.texcoord_buffer_descriptor >= 0
        ? bindless_buffers_float2[DescriptorIndex(geometry.texcoord_buffer_descriptor)][input.vertex_id]
        : 1.0f;

    float3 local_normal = geometry.normal_buffer_descriptor >= 0
        ? bindless_buffers_float3[DescriptorIndex(geometry.normal_buffer_descriptor)][GetTerrainStreamVertexID(input.vertex_id)]
        : 1.0f;
    float3x3 normal_transform = float3x3(transform.normal_transform_row0, transform.normal_transform_row1, transform.normal_transform_row2);
    output.nor = normalize(mul(local_normal, normal_transform));

    output.color = 1.0h;
    if (GetTerrain(terrainpush.terrain_index).IsUsingVertexColors() && geometry.color_buffer_descriptor >= 0)
    {
        output.color = bindless_buffers_float4[DescriptorIndex(geometry.color_buffer_descriptor)][input.vertex_id];
    }

    return output;
}
