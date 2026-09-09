#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_AO_PUSHCONSTANT
#include "ShaderInterop_PostProcess.h"
#include "AOCommon.hlsli"
#include "NoiseCommon.hlsli"

static const uint ao_direction_count = 4;
static const uint ao_step_count = 4;
static const float ao_max_radius_uv = 0.1f;
static const float ao_thickness = 0.05f;
static const float ao_falloff_start = ao_radius * 0.9f; // falloff starts with 60%

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
    const float2 texel = resolution_rcp;
    const float3 pos_right = ScreenUVToView(uv + float2(texel.x, 0.0f), SampleLinearDepth(linear_depth, uv + float2(texel.x, 0.0f), 0));
    const float3 pos_up = ScreenUVToView(uv + float2(0.0f, texel.y), SampleLinearDepth(linear_depth, uv + float2(0.0f, texel.y), 0));
    const float3 delta_x = pos_right - P; // view space delta for one pixel in screen space
    const float3 delta_y = pos_up - P;

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
        const float2 direction = float2(cos(phi), sin(phi)); // screen space direction

        const float3 slice_direction = normalize(delta_x * direction.x + delta_y * direction.y); // multiply screen space direction by view space delta
        const float3 slice_axis = normalize(cross(slice_direction, view_vector)); // normal of the slice plane
        const float3 projected_normal = normal - slice_axis * dot(normal, slice_axis); // project to the slice plane
        const float projected_normal_length = length(projected_normal);
        if (projected_normal_length <= FLT_EPSILON)
        {
            continue; // if normal is perpendicular to the slice plane
        }

        const float3 in_plane_tangent = cross(view_vector, slice_axis); // slice_direction and view_vector are not always perpendicular
        const float cos_normal = clamp(dot(projected_normal, view_vector) / projected_normal_length, -1.0f, 1.0f);
        const float normal_angle = sign(dot(projected_normal, in_plane_tangent)) * acos(cos_normal);
        const float sin_normal = sin(normal_angle);

        float cos_horizon_plus = -1.0f;
        float cos_horizon_minus = -1.0f;

        [unroll]
        for (uint step_index = 0; step_index < ao_step_count; ++step_index)
        {
            const float march = (float(step_index) + noise) / float(ao_step_count); // [0, 1)
            const float2 offset = direction * radius_uv * march;
            const float sample_pixels = march * radius_pixels;
            const uint mip = min((uint)max(log2(max(sample_pixels, 1.0f)) - 2.0f, 0.0f), GetView().linear_depth_mip_count - 1u);

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
                const float sample_cos = lerp(-1.0f, dot(diff_plus / len_plus, view_vector), falloff); // higher cos = smaller angle with view vector = closer depth
                cos_horizon_plus = sample_cos > cos_horizon_plus
                    ? sample_cos
                    : lerp(cos_horizon_plus, sample_cos, ao_thickness);
            }
            if (len_minus > ao_bias && len_minus < ao_radius)
            {
                const float falloff = saturate((ao_radius - len_minus) / (ao_radius - ao_falloff_start));
                const float sample_cos = lerp(-1.0f, dot(diff_minus / len_minus, view_vector), falloff);
                cos_horizon_minus = sample_cos > cos_horizon_minus
                    ? sample_cos
                    : lerp(cos_horizon_minus, sample_cos, ao_thickness);
            }
        }

        float horizon_plus = acos(clamp(cos_horizon_plus, -1.0f, 1.0f));
        float horizon_minus = -acos(clamp(cos_horizon_minus, -1.0f, 1.0f));
        horizon_plus = normal_angle + min(horizon_plus - normal_angle, HALF_PI);
        horizon_minus = normal_angle + max(horizon_minus - normal_angle, -HALF_PI);

        visibility += projected_normal_length *
            (AOIntegrateArc(horizon_plus, normal_angle, cos_normal, sin_normal)
             + AOIntegrateArc(horizon_minus, normal_angle, cos_normal, sin_normal));
    }

    visibility = saturate(visibility / float(ao_direction_count));
    output[pixel] = saturate(pow(visibility, ao_intensity));
}
