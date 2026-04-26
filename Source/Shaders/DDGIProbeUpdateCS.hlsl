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
    if (!ddgi_volume.IsActive() || ddgi_volume.irradiance_texture_uav < 0 || ddgi_volume.visibility_texture_uav < 0 || ddgi_volume.probe_data_buffer_uav < 0)
    {
        return;
    }

    uint update_index = group_id.x + group_id.y * ddgi_volume.probe_update_dispatch_width;
    if (update_index >= ddgi_volume.probes_per_frame || ddgi_volume.total_probe_count == 0)
    {
        return;
    }

    uint probe_linear_index = (ddgi_volume.probe_update_start + update_index) % ddgi_volume.total_probe_count;
    uint3 probe_index = DDGIProbeIndexFromLinear(probe_linear_index, ddgi_volume);
    uint2 probe_texel = group_thread_id.xy;

    float4 previous_probe_data = float4(0.0f, 0.0f, 0.0f, 1.0f);
    if (ddgi_volume.HasHistory())
    {
        previous_probe_data = bindless_buffers_float4[DescriptorIndex(ddgi_volume.previous_probe_data_buffer)][DDGIProbeDataIndex(probe_index, ddgi_volume)];
    }
    float3 probe_position = DDGIProbePosition(ddgi_volume, probe_index, previous_probe_data);
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

    if (all(group_thread_id.xy == uint2(0, 0)))
    {
        float near_hit_count = 0.0f;
        float3 relocation_delta = float3(0.0f, 0.0f, 0.0f);
        [loop]
        for (uint sample_index = 0; sample_index < DDGI_VISIBILITY_RESOLUTION * DDGI_VISIBILITY_RESOLUTION; ++sample_index)
        {
            uint2 sample_texel = uint2(sample_index % DDGI_VISIBILITY_RESOLUTION, sample_index / DDGI_VISIBILITY_RESOLUTION);
            float3 sample_direction = DecodeOctahedralTexel(sample_texel, DDGI_VISIBILITY_RESOLUTION);
            float near_hit = shared_hit[sample_index] * (shared_distance[sample_index] <= ddgi_volume.normal_bias ? 1.0f : 0.0f);
            relocation_delta += -sample_direction * max(ddgi_volume.normal_bias - shared_distance[sample_index], 0.0f) * near_hit;
            near_hit_count += near_hit;
        }

        float near_hit_ratio = near_hit_count / float(DDGI_VISIBILITY_RESOLUTION * DDGI_VISIBILITY_RESOLUTION);
        if (near_hit_count > 0.0f)
        {
            relocation_delta /= near_hit_count;
        }

        float max_relocation = min(ddgi_volume.probe_spacing.x, min(ddgi_volume.probe_spacing.y, ddgi_volume.probe_spacing.z)) * 0.45f;
        float3 target_offset = previous_probe_data.xyz + relocation_delta;
        float relocation_length = length(target_offset);
        if (relocation_length > max_relocation)
        {
            target_offset *= max_relocation / relocation_length;
        }

        float validity = near_hit_ratio < 0.75f ? 1.0f : 0.0f;
        float4 probe_data = float4(target_offset, validity);
        if (ddgi_volume.HasHistory())
        {
            probe_data.xyz = lerp(probe_data.xyz, previous_probe_data.xyz, ddgi_volume.hysteresis);
        }

        bindless_rwbuffers_float4[DescriptorIndex(ddgi_volume.probe_data_buffer_uav)][DDGIProbeDataIndex(probe_index, ddgi_volume)] = probe_data;
    }

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
    uint3 irradiance_atlas_texel = DDGIProbeAtlasBase(probe_index, ddgi_volume) + uint3(probe_texel, 0);
    uint3 visibility_atlas_texel = DDGIProbeVisibilityAtlasBase(probe_index, ddgi_volume) + uint3(probe_texel, 0);
    float4 irradiance_output = float4(sum_irradiance * inv_weight, 1.0f);
    float4 visibility_output = float4(sum_distance * inv_weight, sum_distance_sq * inv_weight, sum_hit * inv_weight, 1.0f);
    if (ddgi_volume.HasHistory())
    {
        float4 previous_irradiance = bindless_textures2DArray[DescriptorIndex(ddgi_volume.previous_irradiance_texture)].Load(int4(irradiance_atlas_texel, 0));
        float4 previous_visibility = bindless_textures2DArray[DescriptorIndex(ddgi_volume.previous_visibility_texture)].Load(int4(visibility_atlas_texel, 0));
        irradiance_output.rgb = lerp(irradiance_output.rgb, previous_irradiance.rgb, ddgi_volume.hysteresis);
        visibility_output.rgb = lerp(visibility_output.rgb, previous_visibility.rgb, ddgi_volume.hysteresis);
    }

    bindless_rwtextures2DArray[DescriptorIndex(ddgi_volume.irradiance_texture_uav)][irradiance_atlas_texel] = irradiance_output;
    bindless_rwtextures2DArray[DescriptorIndex(ddgi_volume.visibility_texture_uav)][visibility_atlas_texel] = visibility_output;
}
