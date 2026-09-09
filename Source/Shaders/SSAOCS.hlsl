#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_AO_PUSHCONSTANT
#include "ShaderInterop_PostProcess.h"
#include "AOCommon.hlsli"
#include "NoiseCommon.hlsli"

static const uint ssao_sample_count = 16;

[numthreads(DISPATCH_THREAD_GROUP_2D, DISPATCH_THREAD_GROUP_2D, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    RWTexture2D<float> output = bindless_rwtextures_float[DescriptorIndex((int)aopush.output_descriptor)];
    uint2 resolution;
    output.GetDimensions(resolution.x, resolution.y);
    if (dispatch_thread_id.x >= resolution.x || dispatch_thread_id.y >= resolution.y)
    {
        return;
    }

    Texture2D linear_depth = bindless_textures[DescriptorIndex(GetView().linear_depth)];
    Texture2D normal_texture = bindless_textures[DescriptorIndex((int)aopush.normal_descriptor)];

    const int2 pixel = int2(dispatch_thread_id.xy);
    const float2 resolution_rcp = 1.0f / float2(resolution);
    const float2 uv = (float2(pixel) + 0.5f) * resolution_rcp;

    const float center_view_z = SampleLinearDepth(linear_depth, uv, 0);
    if (center_view_z >= GetCamera().z_far)
    {
        output[pixel] = 1.0f;
        return;
    }

    const float3 P = ScreenUVToView(uv, center_view_z);
    const float3 view_vector = normalize(-P);
    float3 normal;
    if (!SampleViewNormal(normal_texture, uv, normal))
    {
        output[pixel] = 1.0f;
        return;
    }
    if (dot(normal, view_vector) < 0.0f)
    {
        normal = -normal;
    }

    const float random_angle = InterleavedGradientNoise(float2(pixel), GetFrame().frame_count) * TWO_PI;
    const float3 random_vec = float3(cos(random_angle), sin(random_angle), 0.0f);
    const float3 tangent = normalize(random_vec - normal * dot(random_vec, normal));
    const float3 bitangent = cross(normal, tangent);

    float occlusion = 0.0f;
    [loop]
    for (uint i = 0; i < ssao_sample_count; ++i)
    {
        const float2 hammersley = Hammersley(i, ssao_sample_count);
        const float radius_fraction = sqrt(hammersley.x); // sqrt for more samples to bigger radius(because x is smaller than 1.0)
        const float phi = TWO_PI * hammersley.y;
        float3 hemisphere;
        hemisphere.x = radius_fraction * cos(phi); // rcos
        hemisphere.y = radius_fraction * sin(phi); // rsin
        hemisphere.z = sqrt(1.0f - hammersley.x); // 1 - r^2
        const float distance_scale = lerp(0.1f, 1.0f, hammersley.x);

        const float3 sample_dir = tangent * hemisphere.x + bitangent * hemisphere.y + normal * hemisphere.z;
        const float3 sample_position = P + sample_dir * (ao_radius * distance_scale);
        if (sample_position.z <= FLT_EPSILON)
        {
            continue;
        }

        const float2 sample_uv = ViewToScreenUV(sample_position);
        if (any(sample_uv < 0.0f) || any(sample_uv > 1.0f))
        {
            continue;
        }

        const float scene_view_z = SampleLinearDepth(linear_depth, sample_uv, 0);
        if (scene_view_z + ao_bias < sample_position.z)
        {
            const float range_check = smoothstep(0.0f, 1.0f, ao_radius / max(abs(center_view_z - scene_view_z), FLT_EPSILON)); // prevent dark halo on far away objects' surfaces
            occlusion += range_check;
        }
    }

    const float visibility = saturate(1.0f - occlusion / float(ssao_sample_count));
    output[pixel] = saturate(pow(visibility, ao_intensity));
}
