#ifndef OBJECT_PS_MOTION
#define OBJECT_PS_MOTION

#include "ObjectCommon.hlsli"

float4 main(PixelInput input) : SV_Target0
{
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
#endif // ALPHATEST

    if (input.previous_clip_position.w <= 0.0f) // check only if ahead of camera
    {
        return float4(motion_vector_previous_transform_invalid_marker, motion_vector_previous_transform_invalid_marker, 0.0f, 0.0f);
    }

    float2 current_ndc = input.current_clip_position.xy / input.current_clip_position.w;
    current_ndc -= float2(GetCamera().jitter_x, GetCamera().jitter_y); // to unjittered
    const float2 previous_ndc = input.previous_clip_position.xy / input.previous_clip_position.w; // unjittered, this is ok to be out of screen
    const float2 motion = NDCToScreenUV(previous_ndc) - NDCToScreenUV(current_ndc);
    return float4(motion, min(input.previous_view_depth, MEDIUMP_FLT_MAX), 0.0f); // u_diff, v_diff, view_depth 
}

#endif // OBJECT_PS_MOTION
