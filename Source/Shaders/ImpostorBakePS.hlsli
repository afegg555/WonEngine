#ifndef IMPOSTOR_BAKE_PS
#define IMPOSTOR_BAKE_PS
#include "ObjectCommon.hlsli"
#include "ShadingCommon.hlsli"

struct CaptureOutput
{
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
    float depth : SV_Target2;
};

CaptureOutput main(PixelInput input, in bool is_frontface : SV_IsFrontFace)
{
    ShaderMaterial material = GetMaterial();
    half4 base_color = material.GetBaseColor();

    float2 uvsets = input.uvsets;
    [branch]
    if (material.textures[BASECOLORMAP].IsValid())
    {
        base_color *= material.textures[BASECOLORMAP].Sample(sampler_objectshader, uvsets);
    }
    base_color *= input.color;

#ifdef ALPHATEST
    clip(base_color.a - material.GetAlphaCutoff());
#endif // ALPHATEST

    if (is_frontface == false)
    {
        input.nor = -input.nor;
    }
    half3 world_normal = normalize(input.nor);

    half4 tangent = input.tan;
    tangent.xyz = normalize(tangent.xyz);
    tangent.w = tangent.w < 0 ? -1 : 1;
    half3 bitangent = cross(tangent.xyz, world_normal) * tangent.w;
    float3x3 TBN = float3x3(tangent.xyz, bitangent, world_normal);
    [branch]
    if (material.textures[NORMALMAP].IsValid())
    {
        half3 tangent_normal = DecodeTangentNormal(material.textures[NORMALMAP].Sample(sampler_objectshader, uvsets).xy);
        world_normal = normalize(mul(tangent_normal, TBN));
    }

    ShaderCamera camera = GetCamera();
    float view_depth = dot(input.worldpos - camera.position, camera.forward);
    float linear_depth = saturate((view_depth - camera.z_near) / max(camera.z_far - camera.z_near, 1e-4f));

    CaptureOutput output;
    output.albedo = float4(base_color.rgb, base_color.a);
    output.normal = float4(world_normal * 0.5f + 0.5f, 1.0f);
    output.depth = linear_depth;
    return output;
}

#endif // IMPOSTOR_BAKE_PS
