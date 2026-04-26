#ifndef DDGI_COMMON
#define DDGI_COMMON

#include "Common.hlsli"

inline uint2 DDGIProbeAtlasBase(uint3 probe_index, ShaderDDGIVolume ddgi_volume)
{
    return uint2(probe_index.x * DDGI_IRRADIANCE_RESOLUTION, (probe_index.y + probe_index.z * ddgi_volume.probe_counts.y) * DDGI_IRRADIANCE_RESOLUTION);
}

inline uint2 DDGIProbeVisibilityAtlasBase(uint3 probe_index, ShaderDDGIVolume ddgi_volume)
{
    return uint2(probe_index.x * DDGI_VISIBILITY_RESOLUTION, (probe_index.y + probe_index.z * ddgi_volume.probe_counts.y) * DDGI_VISIBILITY_RESOLUTION);
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

inline float3 SampleDDGIIrradianceProbe(ShaderDDGIVolume ddgi_volume, uint3 probe_index, float3 normal)
{
    uint2 atlas_size = uint2(max(ddgi_volume.probe_counts.x, 1u) * DDGI_IRRADIANCE_RESOLUTION, max(ddgi_volume.probe_counts.y, 1u) * max(ddgi_volume.probe_counts.z, 1u) * DDGI_IRRADIANCE_RESOLUTION);
    uint2 atlas_base = DDGIProbeAtlasBase(probe_index, ddgi_volume);
    float2 oct_uv = EncodeOctahedralDirection(normal);
    float2 tile_texel = clamp(oct_uv * float(DDGI_IRRADIANCE_RESOLUTION), 0.5f, float(DDGI_IRRADIANCE_RESOLUTION) - 0.5f);
    float2 atlas_uv = (float2(atlas_base) + tile_texel) / float2(atlas_size);
    return bindless_textures[DescriptorIndex(ddgi_volume.irradiance_texture)].SampleLevel(sampler_linear_clamp, atlas_uv, 0).rgb;
}

inline float SampleDDGIVisibility(ShaderDDGIVolume ddgi_volume, uint3 probe_index, float3 sample_position)
{
    if (!ddgi_volume.HasVisibilityTexture())
    {
        return 1.0f;
    }

    float3 probe_position = ddgi_volume.volume_min + float3(probe_index) * ddgi_volume.probe_spacing;
    float3 probe_to_sample = sample_position - probe_position;
    float distance = length(probe_to_sample);
    if (distance <= 0.0001f)
    {
        return 1.0f;
    }

    uint2 atlas_size = uint2(max(ddgi_volume.probe_counts.x, 1u) * DDGI_VISIBILITY_RESOLUTION, max(ddgi_volume.probe_counts.y, 1u) * max(ddgi_volume.probe_counts.z, 1u) * DDGI_VISIBILITY_RESOLUTION);
    uint2 atlas_base = DDGIProbeVisibilityAtlasBase(probe_index, ddgi_volume);
    float2 oct_uv = EncodeOctahedralDirection(probe_to_sample / distance);
    float2 tile_texel = clamp(oct_uv * float(DDGI_VISIBILITY_RESOLUTION), 0.5f, float(DDGI_VISIBILITY_RESOLUTION) - 0.5f);
    float2 atlas_uv = (float2(atlas_base) + tile_texel) / float2(atlas_size);
    float2 moments = bindless_textures[DescriptorIndex(ddgi_volume.visibility_texture)].SampleLevel(sampler_linear_clamp, atlas_uv, 0).rg;

    float mean_distance = moments.x;
    float mean_distance_sq = moments.y;
    float variance = max(mean_distance_sq - mean_distance * mean_distance, 0.01f);
    float difference = distance - mean_distance;
    if (difference <= ddgi_volume.normal_bias)
    {
        return 1.0f;
    }

    return saturate(variance / (variance + difference * difference));
}

float3 SampleDDGIIrradiance(ShaderDDGIVolume ddgi_volume, float3 sample_position, float3 normal)
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
                float3 weight3 = lerp(1.0f - blend, blend, float3(offset));
                float weight = weight3.x * weight3.y * weight3.z * SampleDDGIVisibility(ddgi_volume, probe_index, sample_position);
                irradiance += SampleDDGIIrradianceProbe(ddgi_volume, probe_index, normal) * weight;
            }
        }
    }

    return irradiance;
}

#endif
