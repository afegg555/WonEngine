#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_FXAA_PUSHCONSTANT
#include "ShaderInterop_PostProcess.h"

// FXAA (Timothy Lottes, simplified luma-based edge AA). Operates on LDR color.
#define FXAA_SPAN_MAX   8.0
#define FXAA_REDUCE_MUL (1.0 / 8.0)
#define FXAA_REDUCE_MIN (1.0 / 128.0)

float FXAALuma(float3 rgb)
{
    return dot(rgb, float3(0.299, 0.587, 0.114));
}

[numthreads(DISPATCH_THREAD_GROUP_2D, DISPATCH_THREAD_GROUP_2D, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (dispatch_thread_id.x >= fxaapush.resolution.x || dispatch_thread_id.y >= fxaapush.resolution.y)
    {
        return;
    }

    Texture2D source = bindless_textures[DescriptorIndex((int)fxaapush.input_descriptor)];
    RWTexture2D<float4> destination = bindless_rwtextures[DescriptorIndex((int)fxaapush.output_descriptor)];

    const float2 rcp_resolution = fxaapush.rcp_resolution;
    const float2 uv = (float2(dispatch_thread_id.xy) + 0.5) * rcp_resolution;

    float4 center = source.SampleLevel(sampler_linear_clamp, uv, 0);
    float3 rgb_m  = center.rgb;
    float3 rgb_nw = source.SampleLevel(sampler_linear_clamp, uv + float2(-1.0, -1.0) * rcp_resolution, 0).rgb;
    float3 rgb_ne = source.SampleLevel(sampler_linear_clamp, uv + float2( 1.0, -1.0) * rcp_resolution, 0).rgb;
    float3 rgb_sw = source.SampleLevel(sampler_linear_clamp, uv + float2(-1.0,  1.0) * rcp_resolution, 0).rgb;
    float3 rgb_se = source.SampleLevel(sampler_linear_clamp, uv + float2( 1.0,  1.0) * rcp_resolution, 0).rgb;

    float luma_m  = FXAALuma(rgb_m);
    float luma_nw = FXAALuma(rgb_nw);
    float luma_ne = FXAALuma(rgb_ne);
    float luma_sw = FXAALuma(rgb_sw);
    float luma_se = FXAALuma(rgb_se);

    float luma_min = min(luma_m, min(min(luma_nw, luma_ne), min(luma_sw, luma_se)));
    float luma_max = max(luma_m, max(max(luma_nw, luma_ne), max(luma_sw, luma_se)));

    // Blend direction perpendicular to the local luma gradient.
    float2 dir;
    dir.x = -((luma_nw + luma_ne) - (luma_sw + luma_se));
    dir.y =  ((luma_nw + luma_sw) - (luma_ne + luma_se));

    float dir_reduce = max((luma_nw + luma_ne + luma_sw + luma_se) * 0.25 * FXAA_REDUCE_MUL, FXAA_REDUCE_MIN);
    float rcp_dir_min = 1.0 / (min(abs(dir.x), abs(dir.y)) + dir_reduce);
    dir = clamp(dir * rcp_dir_min, -FXAA_SPAN_MAX, FXAA_SPAN_MAX) * rcp_resolution;

    float3 rgb_a = 0.5 * (
        source.SampleLevel(sampler_linear_clamp, uv + dir * (1.0 / 3.0 - 0.5), 0).rgb +
        source.SampleLevel(sampler_linear_clamp, uv + dir * (2.0 / 3.0 - 0.5), 0).rgb);
    float3 rgb_b = rgb_a * 0.5 + 0.25 * (
        source.SampleLevel(sampler_linear_clamp, uv + dir * -0.5, 0).rgb +
        source.SampleLevel(sampler_linear_clamp, uv + dir *  0.5, 0).rgb);

    float luma_b = FXAALuma(rgb_b);
    float3 result = (luma_b < luma_min || luma_b > luma_max) ? rgb_a : rgb_b;

    // Preserve the source alpha so the composite blend (alpha 0 = undrawn) still shows the clear color.
    destination[dispatch_thread_id.xy] = float4(result, center.a);
}
