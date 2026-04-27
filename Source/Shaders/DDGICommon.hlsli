#ifndef DDGI_COMMON
#define DDGI_COMMON

#include "Common.hlsli"

inline uint3 DDGIProbeAtlasBase(uint3 probe_index, ShaderDDGIVolume ddgi_volume)
{
    return uint3(probe_index.x * (DDGI_IRRADIANCE_RESOLUTION + 2), probe_index.y * (DDGI_IRRADIANCE_RESOLUTION + 2), probe_index.z);
}

inline uint3 DDGIProbeVisibilityAtlasBase(uint3 probe_index, ShaderDDGIVolume ddgi_volume)
{
    return uint3(probe_index.x * (DDGI_VISIBILITY_RESOLUTION + 2), probe_index.y * (DDGI_VISIBILITY_RESOLUTION + 2), probe_index.z);
}

inline uint DDGIProbeDataIndex(uint3 probe_index, ShaderDDGIVolume ddgi_volume)
{
    return probe_index.x + probe_index.y * ddgi_volume.probe_counts.x + probe_index.z * ddgi_volume.probe_counts.x * ddgi_volume.probe_counts.y;
}

inline uint3 DDGIProbeIndexFromLinear(uint probe_linear_index, ShaderDDGIVolume ddgi_volume)
{
    uint probe_xy_count = ddgi_volume.probe_counts.x * ddgi_volume.probe_counts.y;
    uint3 probe_index;
    probe_index.x = probe_linear_index % ddgi_volume.probe_counts.x;
    probe_index.y = (probe_linear_index / ddgi_volume.probe_counts.x) % ddgi_volume.probe_counts.y;
    probe_index.z = probe_linear_index / probe_xy_count;
    return probe_index;
}

inline float4 LoadDDGIProbeData(ShaderDDGIVolume ddgi_volume, uint3 probe_index, int probe_data_buffer)
{
    if (probe_data_buffer < 0)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    return bindless_buffers_float4[DescriptorIndex(probe_data_buffer)][DDGIProbeDataIndex(probe_index, ddgi_volume)];
}

inline float4 LoadDDGIProbeData(ShaderDDGIVolume ddgi_volume, uint3 probe_index)
{
    return LoadDDGIProbeData(ddgi_volume, probe_index, ddgi_volume.probe_data_buffer);
}

inline float3 DDGIProbeGridPosition(ShaderDDGIVolume ddgi_volume, uint3 probe_index)
{
    return ddgi_volume.volume_min + float3(probe_index) * ddgi_volume.probe_spacing;
}

inline float3 DDGIProbePosition(ShaderDDGIVolume ddgi_volume, uint3 probe_index, float4 probe_data)
{
    return DDGIProbeGridPosition(ddgi_volume, probe_index) + probe_data.xyz;
}

inline float2 EncodeOctahedralDirection(float3 direction)
{
    direction /= max(abs(direction.x) + abs(direction.y) + abs(direction.z), 0.0001f);
    if (direction.z < 0.0f)
    {
        float2 direction_sign = float2(direction.x >= 0.0f ? 1.0f : -1.0f, direction.y >= 0.0f ? 1.0f : -1.0f);
        direction.xy = (1.0f - abs(direction.yx)) * direction_sign;
    }
    return direction.xy * 0.5f + 0.5f;
}

inline float3 DecodeOctahedralDirection(float2 encoded)
{
    float3 direction = float3(encoded.x, encoded.y, 1.0f - abs(encoded.x) - abs(encoded.y));
    if (direction.z < 0.0f)
    {
        float2 direction_sign = float2(direction.x >= 0.0f ? 1.0f : -1.0f, direction.y >= 0.0f ? 1.0f : -1.0f);
        direction.xy = (1.0f - abs(direction.yx)) * direction_sign;
    }
    return normalize(direction);
}

inline float3 DecodeOctahedralTexel(uint2 texel, uint resolution)
{
    float2 uv = (float2(texel) + 0.5f) / float(resolution);
    return DecodeOctahedralDirection(uv * 2.0f - 1.0f);
}

inline float3 DecodeOctahedralTexel(uint2 texel)
{
    return DecodeOctahedralTexel(texel, DDGI_IRRADIANCE_RESOLUTION);
}

inline bool IsInsideDDGIVolume(ShaderDDGIVolume ddgi_volume, float3 sample_position)
{
    float3 safe_probe_spacing = max(ddgi_volume.probe_spacing, float3(0.001f, 0.001f, 0.001f));
    float3 probe_coord = (sample_position - ddgi_volume.volume_min) / safe_probe_spacing;
    float3 max_probe_coord = float3(
        (float)(ddgi_volume.probe_counts.x > 0 ? ddgi_volume.probe_counts.x - 1 : 0),
        (float)(ddgi_volume.probe_counts.y > 0 ? ddgi_volume.probe_counts.y - 1 : 0),
        (float)(ddgi_volume.probe_counts.z > 0 ? ddgi_volume.probe_counts.z - 1 : 0)
    );
    return all(probe_coord >= float3(0.0f, 0.0f, 0.0f)) && all(probe_coord <= max_probe_coord);
}

inline float3 SampleDDGIIrradiance(ShaderDDGIVolume ddgi_volume, uint3 probe_index, float3 normal, int irradiance_texture)
{
    if (irradiance_texture < 0)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    uint2 atlas_size = uint2(max(ddgi_volume.probe_counts.x, 1u) * (DDGI_IRRADIANCE_RESOLUTION + 2), max(ddgi_volume.probe_counts.y, 1u) * (DDGI_IRRADIANCE_RESOLUTION + 2));
    uint3 atlas_base = DDGIProbeAtlasBase(probe_index, ddgi_volume);
    float2 oct_uv = EncodeOctahedralDirection(normal);
    float2 tile_texel = clamp(oct_uv * float(DDGI_IRRADIANCE_RESOLUTION), 0.5f, float(DDGI_IRRADIANCE_RESOLUTION) - 0.5f) + 1.0f;
    float3 atlas_uv = float3((float2(atlas_base.xy) + tile_texel) / float2(atlas_size), float(atlas_base.z));
    return bindless_textures2DArray[DescriptorIndex(irradiance_texture)].SampleLevel(sampler_linear_clamp, atlas_uv, 0).rgb;
}

inline float3 SampleDDGIIrradiance(ShaderDDGIVolume ddgi_volume, uint3 probe_index, float3 normal)
{
    return SampleDDGIIrradiance(ddgi_volume, probe_index, normal, ddgi_volume.irradiance_texture);
}

inline float SampleDDGIVisibility(ShaderDDGIVolume ddgi_volume, uint3 probe_index, float3 probe_position, float3 sample_position, int visibility_texture)
{
    if (visibility_texture < 0)
    {
        return 1.0f;
    }

    float3 probe_to_sample = sample_position - probe_position;
    float distance = length(probe_to_sample);
    if (distance <= 0.0001f)
    {
        return 1.0f;
    }

    uint2 atlas_size = uint2(max(ddgi_volume.probe_counts.x, 1u) * (DDGI_VISIBILITY_RESOLUTION + 2), max(ddgi_volume.probe_counts.y, 1u) * (DDGI_VISIBILITY_RESOLUTION + 2));
    uint3 atlas_base = DDGIProbeVisibilityAtlasBase(probe_index, ddgi_volume);
    float2 oct_uv = EncodeOctahedralDirection(probe_to_sample / distance);
    float2 tile_texel = clamp(oct_uv * float(DDGI_VISIBILITY_RESOLUTION), 0.5f, float(DDGI_VISIBILITY_RESOLUTION) - 0.5f) + 1.0f;
    float3 atlas_uv = float3((float2(atlas_base.xy) + tile_texel) / float2(atlas_size), float(atlas_base.z));
    float3 moments = bindless_textures2DArray[DescriptorIndex(visibility_texture)].SampleLevel(sampler_linear_clamp, atlas_uv, 0).rgb;

    float mean_distance = moments.x;
    float mean_distance_sq = moments.y;
    float hit_ratio = saturate(moments.z);
    float min_probe_spacing = min(ddgi_volume.probe_spacing.x, min(ddgi_volume.probe_spacing.y, ddgi_volume.probe_spacing.z));
    float min_variance = min_probe_spacing * min_probe_spacing * 0.01f;
    float variance = max(mean_distance_sq - mean_distance * mean_distance, min_variance);
    float difference = distance - mean_distance;
    if (difference <= ddgi_volume.normal_bias)
    {
        return 1.0f;
    }

    float chebyshev = saturate(variance / (variance + difference * difference));
    return lerp(1.0f, chebyshev, hit_ratio);
}

inline float SampleDDGIVisibility(ShaderDDGIVolume ddgi_volume, uint3 probe_index, float3 probe_position, float3 sample_position)
{
    return SampleDDGIVisibility(ddgi_volume, probe_index, probe_position, sample_position, ddgi_volume.visibility_texture);
}

float3 SampleDDGI(ShaderDDGIVolume ddgi_volume, float3 sample_position, float3 normal, int irradiance_texture, int visibility_texture, int probe_data_buffer)
{
    float3 safe_probe_spacing = max(ddgi_volume.probe_spacing, float3(0.001f, 0.001f, 0.001f));
    float3 probe_coord = (sample_position - ddgi_volume.volume_min) / safe_probe_spacing;
    float3 max_probe_coord = float3(
        (float)(ddgi_volume.probe_counts.x > 0 ? ddgi_volume.probe_counts.x - 1 : 0),
        (float)(ddgi_volume.probe_counts.y > 0 ? ddgi_volume.probe_counts.y - 1 : 0),
        (float)(ddgi_volume.probe_counts.z > 0 ? ddgi_volume.probe_counts.z - 1 : 0)
    );

    if (!all(probe_coord >= float3(0.0f, 0.0f, 0.0f)) || !all(probe_coord <= max_probe_coord))
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 base_coord = floor(probe_coord);
    float3 blend = saturate(probe_coord - base_coord);
    uint3 base_probe = uint3(base_coord);
    uint3 max_probe = uint3(max_probe_coord);
    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    float weight_sum = 0.0f;

    [unroll]
    for (uint z = 0; z < 2; ++z)
    {
        [unroll]
        for (uint y = 0; y < 2; ++y)
        {
            [unroll]
            for (uint x = 0; x < 2; ++x)
            {
                uint3 offset = uint3(x, y, z);
                uint3 probe_index = min(base_probe + offset, max_probe);
                float4 probe_data = LoadDDGIProbeData(ddgi_volume, probe_index, probe_data_buffer);
                if (probe_data.w <= 0.0f)
                {
                    continue;
                }

                float3 weight3 = lerp(1.0f - blend, blend, float3(offset));
                float3 probe_position = DDGIProbePosition(ddgi_volume, probe_index, probe_data);
                float3 probe_to_sample = sample_position - probe_position;
                float distance_sq = dot(probe_to_sample, probe_to_sample);
                float normal_weight = distance_sq > 0.0001f ? saturate(dot(normal, -probe_to_sample * rsqrt(distance_sq))) : 1.0f;
                normal_weight = max(normal_weight * normal_weight, 0.05f);
                float weight = weight3.x * weight3.y * weight3.z * normal_weight * probe_data.w * SampleDDGIVisibility(ddgi_volume, probe_index, probe_position, sample_position, visibility_texture);
                irradiance += SampleDDGIIrradiance(ddgi_volume, probe_index, normal, irradiance_texture) * weight;
                weight_sum += weight;
            }
        }
    }

    return weight_sum > 0.0001f ? irradiance / weight_sum : float3(0.0f, 0.0f, 0.0f);
}

float3 SampleDDGI(ShaderDDGIVolume ddgi_volume, float3 sample_position, float3 normal)
{
    return SampleDDGI(ddgi_volume, sample_position, normal, ddgi_volume.irradiance_texture, ddgi_volume.visibility_texture, ddgi_volume.probe_data_buffer);
}

float3 SamplePreviousDDGI(ShaderDDGIVolume ddgi_volume, float3 sample_position, float3 normal)
{
    if (!ddgi_volume.HasHistory())
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    return SampleDDGI(ddgi_volume, sample_position, normal, ddgi_volume.previous_irradiance_texture, ddgi_volume.previous_visibility_texture, ddgi_volume.previous_probe_data_buffer);
}

#endif
