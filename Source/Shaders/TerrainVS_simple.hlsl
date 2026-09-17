#include "TerrainCommon.hlsli"

float4 main(TerrainVertexInput input) : SV_Position
{
    ShaderTransform transform = GetTransform(GetTerrainTransformIndex(input.instance_id));
    float4 world_position = mul(transform.world_transform, float4(GetTerrainPosition(input.vertex_id), 1.0f));
    return mul(GetCamera().view_projection, world_position);
}
