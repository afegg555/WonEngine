#include "WaterCommon.hlsli"

PUSHCONSTANT(waterinfopush, WaterInfoPushConstants);

WaterInfoOutput main(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    ShaderWaterZone zone = bindless_structured_water_zone[DescriptorIndex(waterinfopush.zone_buffer_descriptor)][waterinfopush.zone_index];
    const uint body_index = zone.first_body + instance_id;
    ShaderWaterBody body = bindless_structured_water_body[DescriptorIndex(waterinfopush.body_buffer_descriptor)][body_index];

    const bool writes_body_index = waterinfopush.writes_body_index != 0;
    const float max_vertex_spacing = writes_body_index ? 0.0f
        : max(zone.extent.x, zone.extent.y) / float(zone.tile_resolution);

    const float2 unit = GetQuadPosition(vertex_id) * 2.0f - 1.0f; // [-1, 1] range
    const float3 corner_world = WaterPlanePoint(body, unit) // original corner position in world space
        + body.axis_x * (unit.x * max_vertex_spacing) // first pass, offset the corner position
        + body.axis_z * (unit.y * max_vertex_spacing);
    const float2 zone_uv = (corner_world.xz - zone.origin) / max(zone.extent, water_epsilon);

    const float height_depth = saturate(corner_world.y / WATER_INFO_HEIGHT_RANGE + 0.5f) * 0.5f // [-2048, 2048] range to [0, 0.5] range
                             + (writes_body_index ? 0.5f : 0.0f); // second pass, not-expanded fragment always wins

    WaterInfoOutput output;
    output.position = float4(zone_uv.x * 2.0f - 1.0f, 1.0f - zone_uv.y * 2.0f, height_depth, 1.0f); // to ndc space, z is height depth
    output.height = corner_world.y; // !! actual height
    output.body_index = writes_body_index ? (int) body_index : -1; // second pass, write body index
    return output;
}
