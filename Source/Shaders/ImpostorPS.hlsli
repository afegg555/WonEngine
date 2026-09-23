#ifndef IMPOSTOR_PS
#define IMPOSTOR_PS
#include "ImpostorCommon.hlsli"
#include "ShadingCommon.hlsli"

float4 main(ImpostorPixel input) : SV_Target
{
    const ShaderFoliageImpostor impostor = GetFoliageImpostor(impostorpush.impostor_index);
    const float grid = (float)impostor.grid_size;

    const float2 uv = input.cell_base + input.quad_uv / grid;

    const half4 base_color = (half4)bindless_textures[DescriptorIndex(impostor.albedo_texture)].Sample(sampler_linear_clamp, uv);
    clip(base_color.a - 0.5f);

    const float3 local_normal = bindless_textures[DescriptorIndex(impostor.normal_texture)].Sample(sampler_linear_clamp, uv).xyz * 2.0f - 1.0f;

    Surface surface;
    surface.Init();
    surface.P = input.worldpos;
    surface.V = normalize(GetCamera().position - input.worldpos);
    surface.N = normalize(mul(ImpostorRotationMatrix(input.rotation), local_normal));
    surface.NoV = saturate(abs(dot(surface.N, surface.V)) + FLT_EPSILON);
    surface.albedo = base_color.rgb;
    surface.f0 = 0.04h;

    Lighting lighting;
    lighting.Create(0, 0, 0, 0);

    EvaluateIndirectLighting(surface, lighting, input.pos.xy);
    EvaluateDirectLighting(surface, lighting, input.pos.xy);

    half3 diffuse = (lighting.direct.diffuse + lighting.indirect.diffuse) * Fd_Lambert();
    half3 specular = lighting.direct.specular + lighting.indirect.specular;
    half4 final_color = base_color;
    final_color.rgb = surface.albedo * diffuse;
    final_color.rgb += specular;

#ifndef WON_SHIPPING
    final_color = ApplyDebugViewMode(final_color, surface, base_color, 0.0h, input.pos.xy, impostorpush.lod_index);
#endif

    final_color.rgb = saturateMediump(final_color.rgb);
    return final_color;
}

#endif // IMPOSTOR_PS
