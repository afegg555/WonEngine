#include "ImpostorCommon.hlsli"

float4 main(ImpostorPixel input) : SV_Target
{
    const ShaderFoliageImpostor impostor = GetFoliageImpostor(impostorpush.impostor_index);
    const float grid = (float)impostor.grid_size;
    const float2 uv = input.cell_base + input.quad_uv / grid;

    const float4 albedo = bindless_textures[DescriptorIndex(impostor.albedo_texture)].Sample(sampler_linear_clamp, uv);
    clip(albedo.a - 0.5f);

    float3 world_normal = bindless_textures[DescriptorIndex(impostor.normal_texture)].Sample(sampler_linear_clamp, uv).xyz * 2.0f - 1.0f;
    world_normal = normalize(world_normal);

    ShaderEnvironment environment = GetEnvironment();
    const float3 sun_direction = environment.GetSunDirection();
    const float3 sun_color = environment.sun_color_sun_intensity.rgb * environment.sun_color_sun_intensity.a;
    const float3 ambient_color = environment.ambient_color_ambient_intensity.rgb * environment.ambient_color_ambient_intensity.a;

    const float n_dot_l = saturate(dot(world_normal, -sun_direction));
    const float3 lit = albedo.rgb * (ambient_color + sun_color * n_dot_l);

    return float4(lit * GetCamera().exposure, 1.0f);
}
