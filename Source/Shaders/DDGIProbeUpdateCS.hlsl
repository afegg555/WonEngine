#include "Common.hlsli"

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

[numthreads(DISPATCH_THREAD_GROUP_3D, DISPATCH_THREAD_GROUP_3D, DISPATCH_THREAD_GROUP_3D)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    ShaderDDGIVolume ddgi_volume = GetDDGIVolume();
    if (!ddgi_volume.IsActive() || ddgi_volume.irradiance_texture_uav < 0)
    {
        return;
    }

    if (dispatch_thread_id.x >= ddgi_volume.probe_counts.x ||
        dispatch_thread_id.y >= ddgi_volume.probe_counts.y ||
        dispatch_thread_id.z >= ddgi_volume.probe_counts.z)
    {
        return;
    }

    float3 probe_position = ddgi_volume.volume_min + float3(dispatch_thread_id) * ddgi_volume.probe_spacing;
    uint ray_count = max(ddgi_volume.rays_per_probe, 1u);
    float3 irradiance = float3(0.0f, 0.0f, 0.0f);

    [loop]
    for (uint ray_index = 0; ray_index < ray_count; ++ray_index)
    {
        float2 xi = Hammersley(ray_index, ray_count);
        float3 direction = SampleSphere(xi);
        irradiance += EvaluateDirectDiffuse(probe_position, direction);
    }

    irradiance *= (4.0f * PI) / float(ray_count);
    bindless_rwtextures3D[DescriptorIndex(ddgi_volume.irradiance_texture_uav)][dispatch_thread_id] = float4(irradiance, 1.0f);
}
