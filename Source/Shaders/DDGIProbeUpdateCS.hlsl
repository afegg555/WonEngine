#include "Common.hlsli"
#include "DDGICommon.hlsli"
#include "RayTraceCommon.hlsli"

static const bool ddgi_debug_probe_index = true;

groupshared float shared_distance[DDGI_VISIBILITY_RESOLUTION * DDGI_VISIBILITY_RESOLUTION]; // 1KB
groupshared float shared_distance_sq[DDGI_VISIBILITY_RESOLUTION * DDGI_VISIBILITY_RESOLUTION]; // 1KB
groupshared float3 shared_irradiance[DDGI_VISIBILITY_RESOLUTION * DDGI_VISIBILITY_RESOLUTION]; // 3KB
groupshared float shared_hit[DDGI_VISIBILITY_RESOLUTION * DDGI_VISIBILITY_RESOLUTION]; // 1KB

static float3 EvaluateDirectDiffuse(float3 position, float3 normal)
{
    float3 radiance = float3(0.0f, 0.0f, 0.0f);

    [unroll]
    for (uint bucket = 0; bucket < 4; ++bucket)
    {
        uint bucket_bits = GetScene().lights[bucket];
        [loop]
        while (bucket_bits != 0)
        {
            uint bit_index = firstbitlow(bucket_bits);
            uint light_index = bucket * 32u + bit_index;
            bucket_bits ^= 1u << bit_index;

            ShaderLight light = GetLight(light_index);
            float3 light_radiance = float3(0.0f, 0.0f, 0.0f);

            if (light.GetType() == SHADER_LIGHT_TYPE_DIRECTIONAL)
            {
                float3 L = normalize(-light.GetDirection());
                light_radiance = light.GetColor().xyz * saturate(dot(normal, L));
            }
            else if (light.GetType() == SHADER_LIGHT_TYPE_POINT)
            {
                float3 to_light = light.position - position;
                float distance_sq = max(dot(to_light, to_light), 0.0001f);
                float range = light.GetRange();
                float range_sq = range * range;
                if (distance_sq < range_sq)
                {
                    float3 L = to_light * rsqrt(distance_sq);
                    float factor = distance_sq / max(range_sq, 0.0001f);
                    float smooth_factor = max(1.0f - factor * factor, 0.0f);
                    float attenuation = (smooth_factor * smooth_factor) / distance_sq;
                    light_radiance = light.GetColor().xyz * saturate(dot(normal, L)) * attenuation;
                }
            }
            else if (light.GetType() == SHADER_LIGHT_TYPE_SPOT)
            {
                float3 to_light = light.position - position;
                float distance_sq = max(dot(to_light, to_light), 0.0001f);
                float range = light.GetRange();
                float range_sq = range * range;
                if (distance_sq < range_sq)
                {
                    float3 L = to_light * rsqrt(distance_sq);
                    float factor = distance_sq / max(range_sq, 0.0001f);
                    float smooth_factor = max(1.0f - factor * factor, 0.0f);
                    float attenuation = (smooth_factor * smooth_factor) / distance_sq;
                    float spot_factor = dot(-L, light.GetDirection());
                    float outer_cone = light.GetOuterConeAngleCos();
                    float inner_cone = light.GetInnerConeAngleCos();
                    float spot_scale = rcp(max(inner_cone - outer_cone, 0.0001f));
                    float spot_offset = -outer_cone * spot_scale;
                    float spot_attenuation = saturate(spot_factor * spot_scale + spot_offset);
                    spot_attenuation *= spot_attenuation;
                    light_radiance = light.GetColor().xyz * saturate(dot(normal, L)) * attenuation * spot_attenuation;
                }
            }

            radiance += light_radiance;
        }
    }

    return radiance;
}

[numthreads(DDGI_VISIBILITY_RESOLUTION, DDGI_VISIBILITY_RESOLUTION, 1)]
void main(uint3 group_id : SV_GroupID, uint3 group_thread_id : SV_GroupThreadID)
{
    ShaderDDGIVolume ddgi_volume = GetDDGIVolume();
    if (!ddgi_volume.IsActive() || ddgi_volume.irradiance_texture_uav < 0 || ddgi_volume.visibility_texture_uav < 0)
    {
        return;
    }

    if (group_id.x >= ddgi_volume.probe_counts.x || group_id.y >= ddgi_volume.probe_counts.y * ddgi_volume.probe_counts.z)
    {
        return;
    }

    uint3 probe_index;
    uint2 probe_texel = group_thread_id.xy;
    uint probe_yz = group_id.y;
    probe_index.x = group_id.x;
    probe_index.y = probe_yz % ddgi_volume.probe_counts.y;
    probe_index.z = probe_yz / ddgi_volume.probe_counts.y;
    if (probe_index.z >= ddgi_volume.probe_counts.z)
    {
        return;
    }

    float3 probe_position = ddgi_volume.volume_min + float3(probe_index) * ddgi_volume.probe_spacing;
    float3 direction = DecodeOctahedralTexel(probe_texel, DDGI_VISIBILITY_RESOLUTION);
    float3 irradiance = float3(0.0f, 0.0f, 0.0f);
    float distance = ddgi_volume.max_distance;
    SceneRayHit hit = TraceSceneRay(probe_position, direction, 0.02f, ddgi_volume.max_distance);
    if (hit.hit)
    {
        distance = hit.distance;
        float hit_weight = saturate(dot(hit.normal, -direction));
        irradiance = EvaluateDirectDiffuse(hit.position + hit.normal * ddgi_volume.normal_bias, hit.normal) * hit_weight;
        //irradiance += float3(1, 0, 0) * hit_weight;
    }

    uint thread_index = group_thread_id.y * DDGI_VISIBILITY_RESOLUTION + group_thread_id.x;
    shared_distance[thread_index] = distance;
    shared_distance_sq[thread_index] = distance * distance;
    shared_irradiance[thread_index] = irradiance;
    shared_hit[thread_index] = hit.hit ? 1.0f : 0.0f;
    GroupMemoryBarrierWithGroupSync();

    float sum_weight = 0.0f;
    float sum_distance = 0.0f;
    float sum_distance_sq = 0.0f;
    float3 sum_irradiance = float3(0.0f, 0.0f, 0.0f);
    float sum_hit = 0.0f;

    [unroll]
    for (int sample_y = -1; sample_y <= 1; ++sample_y)
    {
        [unroll]
        for (int sample_x = -1; sample_x <= 1; ++sample_x)
        {
            int2 sample_texel = clamp(int2(probe_texel) + int2(sample_x, sample_y), int2(0, 0), int2(DDGI_VISIBILITY_RESOLUTION - 1, DDGI_VISIBILITY_RESOLUTION - 1));
            uint sample_index = sample_texel.y * DDGI_VISIBILITY_RESOLUTION + sample_texel.x;
            float sample_weight = sample_x == 0 && sample_y == 0 ? 4.0f : (sample_x == 0 || sample_y == 0 ? 2.0f : 1.0f);
            sum_weight += sample_weight;
            sum_distance += shared_distance[sample_index] * sample_weight;
            sum_distance_sq += shared_distance_sq[sample_index] * sample_weight;
            sum_irradiance += shared_irradiance[sample_index] * sample_weight;
            sum_hit += shared_hit[sample_index] * sample_weight;
        }
    }

    float inv_weight = rcp(max(sum_weight, 0.0001f));
    uint2 irradiance_atlas_texel = DDGIProbeAtlasBase(probe_index, ddgi_volume) + probe_texel;
    uint2 visibility_atlas_texel = DDGIProbeVisibilityAtlasBase(probe_index, ddgi_volume) + probe_texel;
    bindless_rwtextures[DescriptorIndex(ddgi_volume.irradiance_texture_uav)][irradiance_atlas_texel] = float4(sum_irradiance * inv_weight, 1.0f);
    bindless_rwtextures[DescriptorIndex(ddgi_volume.visibility_texture_uav)][visibility_atlas_texel] = float4(sum_distance * inv_weight, sum_distance_sq * inv_weight, sum_hit * inv_weight, 1.0f);
}
