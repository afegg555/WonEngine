#include "Common.hlsli"
#include "DDGICommon.hlsli"
#include "RayTraceCommon.hlsli"

static const bool ddgi_debug_probe_index = true;

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

[numthreads(DISPATCH_THREAD_GROUP_2D, DISPATCH_THREAD_GROUP_2D, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    ShaderDDGIVolume ddgi_volume = GetDDGIVolume();
    if (!ddgi_volume.IsActive() || ddgi_volume.irradiance_texture_uav < 0 || ddgi_volume.visibility_texture_uav < 0)
    {
        return;
    }

    uint atlas_width = ddgi_volume.probe_counts.x * DDGI_VISIBILITY_RESOLUTION;
    uint atlas_height = ddgi_volume.probe_counts.y * ddgi_volume.probe_counts.z * DDGI_VISIBILITY_RESOLUTION;
    if (dispatch_thread_id.x >= atlas_width || dispatch_thread_id.y >= atlas_height)
    {
        return;
    }

    uint3 probe_index;
    uint2 probe_texel;
    probe_index.x = dispatch_thread_id.x / DDGI_VISIBILITY_RESOLUTION;
    probe_texel.x = dispatch_thread_id.x - probe_index.x * DDGI_VISIBILITY_RESOLUTION;
    uint probe_yz = dispatch_thread_id.y / DDGI_VISIBILITY_RESOLUTION;
    probe_texel.y = dispatch_thread_id.y - probe_yz * DDGI_VISIBILITY_RESOLUTION;
    probe_index.y = probe_yz % ddgi_volume.probe_counts.y;
    probe_index.z = probe_yz / ddgi_volume.probe_counts.y;
    if (probe_index.x >= ddgi_volume.probe_counts.x || probe_index.z >= ddgi_volume.probe_counts.z)
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

    bindless_rwtextures[DescriptorIndex(ddgi_volume.irradiance_texture_uav)][dispatch_thread_id.xy] = float4(irradiance, 1.0f);
    bindless_rwtextures[DescriptorIndex(ddgi_volume.visibility_texture_uav)][dispatch_thread_id.xy] = float4(distance, distance * distance, hit.hit ? 1.0f : 0.0f, 1.0f);
}
