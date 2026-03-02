#define OBJECTSHADER_LAYOUT_COMMON
#include "ObjectCommon.hlsli"

float4 main(PixelInput input) : SV_Target
{
    ShaderMaterial material = GetMaterial();
    half4 base_color = material.GetBaseColor();
    const int base_color_texture_descriptor = material.textures[BASECOLORMAP].texture_descriptor;
    if (base_color_texture_descriptor >= 0)
    {
        base_color *= bindless_textures[DescriptorIndex(base_color_texture_descriptor)].Sample(sampler_linear_wrap, input.uvsets);
    }
    return base_color;
}
