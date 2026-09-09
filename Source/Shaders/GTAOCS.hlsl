#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_AO_PUSHCONSTANT
#include "ShaderInterop_PostProcess.h"
#include "AOCommon.hlsli"
#include "NoiseCommon.hlsli"

static const uint ao_direction_count = 3;
static const uint ao_step_count = 3;
static const float ao_max_radius_uv = 0.01f;
static const float ao_falloff_start = ao_radius * 0.6f;
static const float ao_mip_bias = 3.0f;

float AOIntegrateArc(float horizon, float normal_angle, float cos_normal, float sin_normal)
{
    return 0.25f * (cos_normal + 2.0f * horizon * sin_normal - cos(2.0f * horizon - normal_angle));
}

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

    const float view_depth = max(center_view_z, FLT_EPSILON);
    float2 radius_uv; // get the radius in uv space
    radius_uv.x = 0.5f * GetCamera().projection._11 * ao_radius / view_depth; // ao_radius(view space) to clip space(* projection / z) to uv space(* 0.5 for half size)
    radius_uv.y = 0.5f * GetCamera().projection._22 * ao_radius / view_depth;
    radius_uv = min(radius_uv, ao_max_radius_uv.xx);

    const float radius_pixels = max(length(radius_uv * float2(resolution)), 1.0f); // for mip selection
    const float noise = InterleavedGradientNoise(float2(pixel), GetFrame().frame_count);
    float visibility = 0.0f;

    [unroll]
    for (uint direction_index = 0; direction_index < ao_direction_count; ++direction_index)
    {
        const float phi = (float(direction_index) + noise) * (PI / float(ao_direction_count));
        const float cos_phi = cos(phi);
        const float sin_phi = sin(phi);
        const float2 offset_direction = float2(cos_phi, -sin_phi); // screen uv offset (view y is up, uv y is down)
        const float3 direction_vec = float3(cos_phi, sin_phi, 0.0f); // screen aligned direction in view space

        const float3 ortho_direction = direction_vec - dot(direction_vec, view_vector) * view_vector; // slice direction orthogonal to view
        const float3 slice_axis = normalize(cross(ortho_direction, view_vector)); // normal of the slice plane
        const float3 projected_normal = normal - slice_axis * dot(normal, slice_axis); // project to the slice plane
        const float projected_normal_length = length(projected_normal);
        if (projected_normal_length <= FLT_EPSILON)
        {
            continue; // if normal is perpendicular to the slice plane
        }

        const float cos_normal = saturate(dot(projected_normal, view_vector) / projected_normal_length);
        const float normal_angle = sign(dot(ortho_direction, projected_normal)) * acos(cos_normal);
        const float sin_normal = sin(normal_angle);

        const float low_horizon_cos_plus = cos(normal_angle + HALF_PI); // hemisphere limit for the +offset side
        const float low_horizon_cos_minus = cos(normal_angle - HALF_PI);
        float cos_horizon_plus = low_horizon_cos_plus;
        float cos_horizon_minus = low_horizon_cos_minus;

        [unroll]
        for (uint step_index = 0; step_index < ao_step_count; ++step_index)
        {
            const float march = (float(step_index) + noise) / float(ao_step_count); // [0, 1)
            const float2 offset = offset_direction * radius_uv * march;
            const float sample_pixels = march * radius_pixels;
            const uint mip = min((uint)max(log2(max(sample_pixels, 1.0f)) - ao_mip_bias, 0.0f), GetView().linear_depth_mip_count - 1u);

            const float2 uv_plus = uv + offset;
            const float2 uv_minus = uv - offset;
            const float3 sample_plus = ScreenUVToView(uv_plus, SampleLinearDepth(linear_depth, uv_plus, mip));
            const float3 sample_minus = ScreenUVToView(uv_minus, SampleLinearDepth(linear_depth, uv_minus, mip));

            const float3 diff_plus = sample_plus - P; // view space diff
            const float3 diff_minus = sample_minus - P;
            const float len_plus = length(diff_plus);
            const float len_minus = length(diff_minus);

            if (len_plus > ao_bias && len_plus < ao_radius)
            {
                const float falloff = saturate((ao_radius - len_plus) / (ao_radius - ao_falloff_start));
                const float sample_cos = lerp(low_horizon_cos_plus, dot(diff_plus / len_plus, view_vector), falloff);
                cos_horizon_plus = max(cos_horizon_plus, sample_cos);
            }
            if (len_minus > ao_bias && len_minus < ao_radius)
            {
                const float falloff = saturate((ao_radius - len_minus) / (ao_radius - ao_falloff_start));
                const float sample_cos = lerp(low_horizon_cos_minus, dot(diff_minus / len_minus, view_vector), falloff);
                cos_horizon_minus = max(cos_horizon_minus, sample_cos);
            }
        }

        const float horizon_plus = acos(clamp(cos_horizon_plus, -1.0f, 1.0f));
        const float horizon_minus = -acos(clamp(cos_horizon_minus, -1.0f, 1.0f));

        visibility += projected_normal_length *
            (AOIntegrateArc(horizon_plus, normal_angle, cos_normal, sin_normal)
             + AOIntegrateArc(horizon_minus, normal_angle, cos_normal, sin_normal));
    }

    visibility = saturate(visibility / float(ao_direction_count));
    output[pixel] = saturate(pow(visibility, ao_intensity));
}
