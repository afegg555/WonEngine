#ifndef IMPOSTOR_PS_PREPASS
#define IMPOSTOR_PS_PREPASS
#include "ImpostorCommon.hlsli"

struct PrepassOutput
{
#ifdef OBJECTSHADER_OUTPUT_MOTION
    float4 motion : SV_Target0;
#ifdef OBJECTSHADER_USE_NORMAL
    float4 normal : SV_Target1;
#endif
#elif defined(OBJECTSHADER_USE_NORMAL)
    float4 normal : SV_Target0;
#endif
};

PrepassOutput main(ImpostorPixel input)
{
    PrepassOutput output;

    const ShaderFoliageImpostor impostor = GetFoliageImpostor(impostorpush.impostor_index);
    const float grid = (float)impostor.grid_size;
    const float2 uv = input.cell_base + input.quad_uv / grid;
    clip(bindless_textures[DescriptorIndex(impostor.albedo_texture)].Sample(sampler_linear_clamp, uv).a - 0.5f);

#ifdef OBJECTSHADER_OUTPUT_MOTION
    float2 current_ndc = input.current_clip_position.xy / input.current_clip_position.w;
    current_ndc -= float2(GetCamera().jitter_x, GetCamera().jitter_y);
    const float2 previous_ndc = input.previous_clip_position.xy / input.previous_clip_position.w;
    const float2 motion = NDCToScreenUV(previous_ndc) - NDCToScreenUV(current_ndc);
    output.motion = float4(motion, min(input.previous_view_depth, MEDIUMP_FLT_MAX), 0.0f);
#endif

#ifdef OBJECTSHADER_USE_NORMAL
    const float3 local_normal = bindless_textures[DescriptorIndex(impostor.normal_texture)].Sample(sampler_linear_clamp, uv).xyz * 2.0f - 1.0f;
    const float3 world_normal = normalize(mul(ImpostorRotationMatrix(input.rotation), local_normal));
    output.normal = float4(normalize(mul((float3x3)GetCamera().view, world_normal)), 0.0f);
#endif

    return output;
}

#endif // IMPOSTOR_PS_PREPASS
