#include "WaterCommon.hlsli"

VertexOutput main(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    const uint resolution = GetWaterZone().tile_resolution;
    const uint quad_index = vertex_id / 6u; // 0 ~ (resolution * resolution - 1)
    const uint2 quad_coord = uint2(quad_index % resolution, quad_index / resolution);
    uint2 vertex_coord = quad_coord + (uint2) GetQuadPosition(vertex_id); // 0 ~ resolution

    ShaderWaterTile tile = GetWaterTile(instance_id); // 1 instance per tile
    
    // neighboring tiles may have different resolutions
    if (vertex_coord.x == 0u && (tile.coarser_neighbor_mask & WATER_TILE_NEIGHBOR_NEG_X))
    {
        vertex_coord.y &= ~1u; // 0b111111..0
    }
    if (vertex_coord.x == resolution && (tile.coarser_neighbor_mask & WATER_TILE_NEIGHBOR_POS_X))
    {
        vertex_coord.y &= ~1u;
    }
    if (vertex_coord.y == 0u && (tile.coarser_neighbor_mask & WATER_TILE_NEIGHBOR_NEG_Z))
    {
        vertex_coord.x &= ~1u;
    }
    if (vertex_coord.y == resolution && (tile.coarser_neighbor_mask & WATER_TILE_NEIGHBOR_POS_Z))
    {
        vertex_coord.x &= ~1u;
    }

    const float2 unit = (float2) vertex_coord / (float) resolution * 2.0f - 1.0f; // [-1, 1] range
    const float2 world_xz = tile.center + unit * tile.half_size;

    const ShaderWaterZone zone = GetWaterZone();
    const float2 zone_uv = (world_xz - zone.origin) / max(zone.extent, water_epsilon);
    const float2 water_info = bindless_textures[DescriptorIndex(waterpush.info_texture_descriptor)].SampleLevel(sampler_point_clamp, zone_uv, 0).rg;

    float3 world_position = float3(world_xz.x, water_info.r, world_xz.y);
    if (water_info.g >= 0.0f)
    {
        const ShaderWaterBody body = GetWaterBody((uint) water_info.g);
        const float camera_distance = distance(GetCamera().position, world_position);
        const WaterSurfaceSample surface = EvaluateWaterBodySurface(body, zone.wave_time, world_xz, camera_distance);
        world_position.x += surface.horizontal.x;
        world_position.y += surface.height;
        world_position.z += surface.horizontal.y;
    }

    VertexOutput output;
    output.position = mul(GetCamera().view_projection, float4(world_position, 1.0f));
    output.world_position = world_position;
    output.wave_coord = world_xz;
    return output;
}
