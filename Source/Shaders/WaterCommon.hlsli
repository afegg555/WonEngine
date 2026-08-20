#ifndef WATER_COMMON
#define WATER_COMMON

#include "ShaderInterop_Water.h"
#include "Common.hlsli"

PUSHCONSTANT(waterpush, WaterPushConstants);

static const float water_epsilon = 0.000001f;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 world_position : WORLDPOSITION;
};

struct WaterInfoOutput
{
    float4 position : SV_Position;
    float height : WATERHEIGHT;
    nointerpolation int body_index : WATERBODYINDEX; // can't be uniform because we use SV_InstanceID to index into the body buffer
};

inline ShaderWaterBody GetWaterBody(uint body_index)
{
    return bindless_structured_water_body[DescriptorIndex(waterpush.body_buffer_descriptor)][body_index];
}

inline ShaderWaterZone GetWaterZone()
{
    return bindless_structured_water_zone[DescriptorIndex(waterpush.zone_buffer_descriptor)][waterpush.zone_index];
}

inline ShaderWaterTile GetWaterTile(uint instance_id)
{
    return bindless_structured_water_tile[DescriptorIndex(waterpush.tile_buffer_descriptor)][waterpush.first_tile + instance_id];
}

inline float2 WaterZoneUV(in ShaderWaterZone zone, float3 world_position)
{
    return (world_position.xz - zone.origin) / max(zone.extent, water_epsilon);
}

inline float3 WaterPlanePoint(in ShaderWaterBody water, float2 unit)
{
    return water.plane_origin + water.axis_x * (unit.x * water.half_extent_x) + water.axis_z * (unit.y * water.half_extent_z);
}

inline float3 WaterPlaneNormal(in ShaderWaterBody water)
{
    return normalize(cross(water.axis_z, water.axis_x));
}

inline float2 WaterPlaneUnitCoord(in ShaderWaterBody water, float3 world_position)
{
    const float3 offset = world_position - water.plane_origin;
    return float2(dot(offset, water.axis_x) / max(water.half_extent_x, water_epsilon),
                  dot(offset, water.axis_z) / max(water.half_extent_z, water_epsilon));
}

inline float2 WaterSimulationGrid(float3 world_position)
{
    return world_position.xz / WATER_SIMULATION_TEXEL_SIZE;
}

inline int2 WaterWrapTexel(int2 grid)
{
    return ((grid % WATER_RIPPLE_RESOLUTION) + WATER_RIPPLE_RESOLUTION) % WATER_RIPPLE_RESOLUTION;
}

inline float WaterSampleRippleGrid(Texture2D ripple_texture, float2 grid)
{
    const float2 base = floor(grid - 0.5f);
    const float2 weight = grid - 0.5f - base;
    const int2 corner = (int2)base;
    const float h00 = ripple_texture.Load(int3(WaterWrapTexel(corner), 0)).r;
    const float h10 = ripple_texture.Load(int3(WaterWrapTexel(corner + int2(1, 0)), 0)).r;
    const float h01 = ripple_texture.Load(int3(WaterWrapTexel(corner + int2(0, 1)), 0)).r;
    const float h11 = ripple_texture.Load(int3(WaterWrapTexel(corner + int2(1, 1)), 0)).r;
    return lerp(lerp(h00, h10, weight.x), lerp(h01, h11, weight.x), weight.y);
}

inline bool WaterGridInWindow(in ShaderWaterSimulation simulation, int2 grid)
{
    const int2 local = grid - simulation.window_grid_min;
    return all(local >= 0) && all(local < WATER_RIPPLE_RESOLUTION);
}

#endif
