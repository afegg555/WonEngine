#ifndef WATER_COMMON
#define WATER_COMMON

#include "ShaderInterop_Water.h"
#include "Common.hlsli"

PUSHCONSTANT(waterpush, WaterPushConstants);

static const float water_epsilon = 0.000001f;
static const float water_wave_fade_begin = 60.0f;
static const float water_wave_fade_end = 300.0f;

inline float WaterWaveAmplitudeScale(float camera_distance)
{
    return 1.0f - smoothstep(water_wave_fade_begin, water_wave_fade_end, camera_distance);
}

inline WaterSurfaceSample EvaluateWaterBodySurface(in ShaderWaterBody body, float wave_time, float2 world_xz, float camera_distance)
{
    return EvaluateWaterSurface(world_xz, body.wave_direction, body.wave_direction_spread,
        body.wave_count, body.wave_length, body.wave_amplitude, body.wave_steepness,
        wave_time, WaterWaveAmplitudeScale(camera_distance));
}

struct VertexOutput
{
    float4 position : SV_Position;
    float3 world_position : WORLDPOSITION;
    float2 wave_coord : WAVECOORD;
};

struct WaterRippleSplatOutput
{
    float4 position : SV_Position;
    float2 unit : RIPPLEUNIT; // [-1, 1] range
    nointerpolation float strength : RIPPLESTRENGTH;
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

inline float2 WaterRippleTexelSize(float2 zone_extent, uint resolution)
{
    return max(zone_extent, water_epsilon) / (float)resolution;
}

#endif
