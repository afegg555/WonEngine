#ifndef OBJECT_PS_PREPASS
#define OBJECT_PS_PREPASS
#include "ObjectCommon.hlsli"

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

PrepassOutput main(PixelInput input, in bool is_frontface : SV_IsFrontFace)
{
    PrepassOutput output;

#ifdef ALPHATEST
    const ShaderMaterial material = GetMaterial();
    half4 base_color = material.GetBaseColor();
    [branch]
    if (material.textures[BASECOLORMAP].IsValid())
    {
        base_color *= material.textures[BASECOLORMAP].Sample(sampler_objectshader, input.uvsets);
    }
    base_color *= input.color;
    clip(base_color.a - material.GetAlphaCutoff());
#endif

#ifdef OBJECTSHADER_OUTPUT_MOTION
    if (input.previous_clip_position.w <= 0.0f)
    {
        output.motion = float4(motion_vector_previous_transform_invalid_marker, motion_vector_previous_transform_invalid_marker, 0.0f, 0.0f);
    }
    else
    {
        float2 current_ndc = input.current_clip_position.xy / input.current_clip_position.w;
        current_ndc -= float2(GetCamera().jitter_x, GetCamera().jitter_y);
        const float2 previous_ndc = input.previous_clip_position.xy / input.previous_clip_position.w;
        const float2 motion = NDCToScreenUV(previous_ndc) - NDCToScreenUV(current_ndc);
        output.motion = float4(motion, min(input.previous_view_depth, MEDIUMP_FLT_MAX), 0.0f);
    }
#endif

#ifdef OBJECTSHADER_USE_NORMAL
    float3 world_normal = normalize(input.nor);
    if (is_frontface == false)
    {
        world_normal = -world_normal;
    }
    output.normal = float4(normalize(mul((float3x3)GetCamera().view, world_normal)), 0.0f);
#endif

    return output;
}

#endif // OBJECT_PS_PREPASS
