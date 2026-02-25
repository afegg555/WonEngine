#include "Common.hlsli"

struct PixelInput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(PixelInput input) : SV_Target
{
    ShaderMaterial material = GetMaterial(object_push_constants.material_index);
    float4 base_color = material.base_color;
    const int base_color_texture_descriptor = material.textures[BASECOLORMAP].texture_descriptor;
    if (base_color_texture_descriptor >= 0)
    {
        base_color *= bindless_textures[DescriptorIndex(base_color_texture_descriptor)].Sample(sampler_linear_wrap, input.uv);
    }
    return base_color;
}
