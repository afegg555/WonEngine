#include "DecalCommon.hlsli"

VertexOutput main(uint vertex_id : SV_VertexID)
{
    static const float3 cube_corners[8] =
    {
        float3(-0.5f, -0.5f, -0.5f),
        float3( 0.5f, -0.5f, -0.5f),
        float3( 0.5f,  0.5f, -0.5f),
        float3(-0.5f,  0.5f, -0.5f),
        float3(-0.5f, -0.5f,  0.5f),
        float3( 0.5f, -0.5f,  0.5f),
        float3( 0.5f,  0.5f,  0.5f),
        float3(-0.5f,  0.5f,  0.5f),
    };
    static const uint cube_indices[36] =
    {
        0, 1, 2, 0, 2, 3,
        5, 4, 7, 5, 7, 6,
        4, 5, 1, 4, 1, 0,
        3, 2, 6, 3, 6, 7,
        4, 0, 3, 4, 3, 7,
        1, 5, 6, 1, 6, 2,
    };

    ShaderDecal decal = bindless_structured_decal[DescriptorIndex(decalpush.decal_buffer)][decalpush.decal_index];
    ShaderInstance instance = GetInstance(decal.instance_index);

    float3 local = cube_corners[cube_indices[vertex_id]];
    float4 world_pos = mul(instance.world_transform, float4(local, 1.0f));

    ShaderCamera camera = GetCamera();
    VertexOutput output;
    output.position = mul(camera.view_projection, world_pos);
    return output;
}
