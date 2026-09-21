#ifndef IMPOSTOR_COMMON
#define IMPOSTOR_COMMON
#include "Common.hlsli"
#include "OctahedralMapping.hlsli"
#include "ShaderInterop_Impostor.h"

struct ImpostorPixel
{
    float4 pos : SV_Position;
    float2 quad_uv : TEXCOORD0;
    float2 cell_base : TEXCOORD1;
    float3 worldpos : TEXCOORD2;
};

static const float2 impostor_quad_corners[6] =
{
    float2(0.0f, 0.0f), float2(1.0f, 0.0f), float2(1.0f, 1.0f),
    float2(0.0f, 0.0f), float2(1.0f, 1.0f), float2(0.0f, 1.0f)
};

inline ShaderFoliageImpostor GetFoliageImpostor(uint impostor_index)
{
    const uint impostor_buffer = DescriptorIndex(GetScene().foliage_impostor_buffer);
    const uint4 packed0 = bindless_buffers_uint4[impostor_buffer][impostor_index * 2 + 0];
    const float4 packed1 = bindless_buffers_float4[impostor_buffer][impostor_index * 2 + 1];
    ShaderFoliageImpostor result;
    result.albedo_texture = (int)packed0.x;
    result.normal_texture = (int)packed0.y;
    result.depth_texture = (int)packed0.z;
    result.grid_size = packed0.w;
    result.radius = packed1.x;
    result.center = packed1.yzw;
    return result;
}

inline float3x3 ImpostorRotationMatrix(float4 q)
{
    const float qx = q.x, qy = q.y, qz = q.z, qw = q.w;
    return float3x3(
        1.0f - 2.0f * (qy * qy + qz * qz), 2.0f * (qx * qy - qw * qz),        2.0f * (qx * qz + qw * qy),
        2.0f * (qx * qy + qw * qz),        1.0f - 2.0f * (qx * qx + qz * qz), 2.0f * (qy * qz - qw * qx),
        2.0f * (qx * qz - qw * qy),        2.0f * (qy * qz + qw * qx),        1.0f - 2.0f * (qx * qx + qy * qy));
}

#endif // IMPOSTOR_COMMON
