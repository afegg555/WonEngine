#include "SpriteCommon.hlsli"

float4 main(PixelInput input, in bool is_frontface : SV_IsFrontFace) : SV_Target
{
    ShaderMaterial material = GetMaterial(push.material_index);
    half4 final_color = material.GetBaseColor();
    if (material.textures[BASECOLORMAP].IsValid())
    {
        final_color *= material.textures[BASECOLORMAP].Sample(sampler_sprite, input.uvsets);
    }
    final_color *= input.color;

#ifdef ALPHATEST
    clip(final_color.a - material.GetAlphaCutoff());
#endif // ALPHATEST

    return final_color;
}
