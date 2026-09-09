#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_AO_DENOISE_PUSHCONSTANT
#include "ShaderInterop_PostProcess.h"

static const int ao_denoise_radius = 2;
static const float ao_denoise_depth_sigma = 0.05f;
static const float ao_denoise_normal_power = 8.0f;

[numthreads(DISPATCH_THREAD_GROUP_2D, DISPATCH_THREAD_GROUP_2D, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    RWTexture2D<float> output = bindless_rwtextures_float[DescriptorIndex((int)aodenoisepush.output_descriptor)];
    uint2 resolution;
    output.GetDimensions(resolution.x, resolution.y);
    if (dispatch_thread_id.x >= resolution.x || dispatch_thread_id.y >= resolution.y)
    {
        return;
    }

    Texture2D ao_texture = bindless_textures[DescriptorIndex((int)aodenoisepush.ao_descriptor)];
    Texture2D linear_depth = bindless_textures[DescriptorIndex(GetView().linear_depth)];
    Texture2D normal_texture = bindless_textures[DescriptorIndex((int)aodenoisepush.normal_descriptor)];

    const int2 pixel = int2(dispatch_thread_id.xy);
    const float center_ao = ao_texture.Load(int3(pixel, 0)).r;
    const float center_depth = linear_depth.Load(int3(pixel, 0)).r;
    const float3 center_normal_raw = normal_texture.Load(int3(pixel, 0)).xyz;
    if (center_depth >= GetCamera().z_far || dot(center_normal_raw, center_normal_raw) <= FLT_EPSILON)
    {
        output[pixel] = center_ao;
        return;
    }
    const float3 center_normal = normalize(center_normal_raw);

    const int2 last_pixel = int2(resolution) - 1;
    float sum = 0.0f;
    float weight_sum = 0.0f;
    for (int y = -ao_denoise_radius; y <= ao_denoise_radius; ++y) // spatial denoise before temporal denoise
    {
        for (int x = -ao_denoise_radius; x <= ao_denoise_radius; ++x)
        {
            const int2 tap = clamp(pixel + int2(x, y), int2(0, 0), last_pixel);
            const float3 tap_normal_raw = normal_texture.Load(int3(tap, 0)).xyz;
            if (dot(tap_normal_raw, tap_normal_raw) <= FLT_EPSILON)
            {
                continue;
            }

            const float tap_depth = linear_depth.Load(int3(tap, 0)).r;
            const float depth_weight = exp(-abs(center_depth - tap_depth) / (center_depth * ao_denoise_depth_sigma));
            const float normal_weight = pow(saturate(dot(center_normal, normalize(tap_normal_raw))), ao_denoise_normal_power);
            const float weight = depth_weight * normal_weight;

            sum += weight * ao_texture.Load(int3(tap, 0)).r;
            weight_sum += weight;
        }
    }

    output[pixel] = weight_sum > 0.0f ? sum / weight_sum : center_ao;
}
