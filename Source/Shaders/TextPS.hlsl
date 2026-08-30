#include "SpriteCommon.hlsli"

float4 main(PixelInput input, in bool is_frontface : SV_IsFrontFace) : SV_Target
{
    const ShaderSprite sprite = bindless_structured_sprite[DescriptorIndex(push.sprite_buffer_descriptor)][push.sprite_index];

    ShaderMaterial material = GetMaterial(sprite.material_index);
    half4 final_color = material.GetBaseColor();
    final_color *= input.color;
    final_color.a *= bindless_textures[DescriptorIndex(sprite.GetResourceIndex())].Sample(sampler_sprite_clamp, input.uvsets).r;
    return final_color;
}
