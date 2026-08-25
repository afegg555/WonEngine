#include "DecalCommon.hlsli"

float4 main(VertexOutput input) : SV_Target0
{
    ShaderCamera camera = GetCamera();

    float2 screen_uv = input.position.xy * camera.internal_resolution_rcp;
    float scene_depth = bindless_textures[DescriptorIndex(decalpush.depth_descriptor)].Load(int3((int2)input.position.xy, 0)).r;

    float3 world = ScreenUVToWorld(screen_uv, scene_depth);

    ShaderDecal decal = bindless_structured_decal[DescriptorIndex(decalpush.decal_buffer)][decalpush.decal_index];

    float3 local = mul(decal.inv_world, float4(world, 1.0f)).xyz;
    if (any(abs(local) > 0.5f)) // is in unit cube
    {
        discard;
    }

    float2 uv = local.xy + 0.5f; // xy projection
    ShaderMaterial material = GetMaterial(decal.material_index);
    half4 base_color = material.GetBaseColor();
    half4 tex = half4(1.0h, 1.0h, 1.0h, 1.0h);
    if (material.textures[BASECOLORMAP].IsValid())
    {
        tex = material.textures[BASECOLORMAP].Sample(sampler_linear_clamp, uv);
    }

    float4 final_color = (float4)(base_color * tex);
    return final_color;
}
