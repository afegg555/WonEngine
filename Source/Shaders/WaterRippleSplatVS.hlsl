#include "WaterCommon.hlsli"

PUSHCONSTANT(waterripplesplatpush, WaterRippleSplatPushConstants);

WaterRippleSplatOutput main(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    ShaderWaterZone zone = bindless_structured_water_zone[DescriptorIndex(waterripplesplatpush.zone_buffer_descriptor)][waterripplesplatpush.zone_index];
    ShaderWaterRipple injection = bindless_structured_water_ripple[DescriptorIndex(waterripplesplatpush.injection_descriptor)][zone.first_injection + instance_id];

    const float2 texel_size = WaterRippleTexelSize(zone.extent, zone.ripple_resolution);
    const float grid_spacing = min(texel_size.x, texel_size.y);
    const float radius_texels = max(zone.ripple_radius / grid_spacing, 1.0f);

    const float2 unit = GetQuadPosition(vertex_id) * 2.0f - 1.0f; // [-1, 1] range
    const float2 injection_texel = (injection.position.xz - zone.origin) / texel_size;
    const float2 corner_texel = injection_texel + unit * radius_texels;
    const float2 zone_uv = corner_texel / (float) zone.ripple_resolution;

    WaterRippleSplatOutput output;
    output.position = float4(zone_uv.x * 2.0f - 1.0f, 1.0f - zone_uv.y * 2.0f, 0.0f, 1.0f);
    output.unit = unit;
    output.strength = injection.strength;
    return output;
}
