#ifndef TERRAIN_PS
#define TERRAIN_PS

#include "TerrainCommon.hlsli"
#include "ShadingCommon.hlsli"

half3x3 GetTerrainTangentBasis(float3 world_position, half3 world_normal)
{
    float3 position_dx = ddx(world_position);
    float3 position_dy = ddy(world_position);
    float2 uv_dx = ddx(world_position.xz);
    float2 uv_dy = ddy(world_position.xz);
    float3 position_dy_perpendicular = cross(position_dy, world_normal);
    float3 position_dx_perpendicular = cross(world_normal, position_dx);
    float3 tangent = position_dy_perpendicular * uv_dx.x + position_dx_perpendicular * uv_dy.x;
    float3 bitangent = position_dy_perpendicular * uv_dx.y + position_dx_perpendicular * uv_dy.y;
    float inverse_length = rsqrt(max(max(dot(tangent, tangent), dot(bitangent, bitangent)), 1e-8f));
    return half3x3(tangent * inverse_length, bitangent * inverse_length, world_normal);
}

void SampleTerrainLayer(
    ShaderTerrainLayer terrain_layer,
    float3 world_position,
    out half4 base_color,
    out half3 tangent_normal,
    out half3 emissive,
    out half roughness,
    out half metallic,
    out half reflectance)
{
    ShaderMaterial material = GetMaterial(terrain_layer.material_index);
    float2 uv = world_position.xz * terrain_layer.uv_scale;
    base_color = material.GetBaseColor();
    if (material.textures[BASECOLORMAP].IsValid())
    {
        base_color *= material.textures[BASECOLORMAP].Sample(sampler_linear_wrap, uv);
    }

    tangent_normal = half3(0.0h, 0.0h, 1.0h);
    if (material.textures[NORMALMAP].IsValid())
    {
        tangent_normal = DecodeTangentNormal(material.textures[NORMALMAP].Sample(sampler_linear_wrap, uv).xy);
    }

    emissive = material.GetEmissiveColor();
    if (material.textures[EMISSIVEMAP].IsValid())
    {
        half4 emissive_map = material.textures[EMISSIVEMAP].Sample(sampler_linear_wrap, uv);
        emissive *= emissive_map.rgb * emissive_map.a;
    }

    roughness = material.GetRoughness();
    if (material.textures[ROUGHNESSMAP].IsValid())
    {
        roughness *= material.textures[ROUGHNESSMAP].Sample(sampler_linear_wrap, uv).g;
    }

    metallic = material.GetMetallic();
    if (material.textures[METALLICMAP].IsValid())
    {
        metallic *= material.textures[METALLICMAP].Sample(sampler_linear_wrap, uv).b;
    }
    reflectance = material.GetReflectance();
}

void SampleTerrain(
    TerrainPixelInput input,
    out ShaderTerrain terrain,
    out half4 base_color,
    out half3 tangent_normal,
    out half3 emissive,
    out half roughness,
    out half metallic,
    out half reflectance)
{
    terrain = GetTerrain(terrainpush.terrain_index);
    base_color = 0.0h;
    tangent_normal = 0.0h;
    emissive = 0.0h;
    roughness = 0.0h;
    metallic = 0.0h;
    reflectance = 0.0h;
    half weight_sum = 0.0h;

    for (uint layer = 0; layer < terrain.layer_count; ++layer)
    {
        ShaderTerrainLayer terrain_layer = GetTerrainLayer(terrain.layer_offset + layer);
        if (terrain_layer.weight_map_descriptor < 0)
        {
            continue;
        }

        half4 packed_weights = bindless_textures[DescriptorIndex(terrain_layer.weight_map_descriptor)].Sample(sampler_linear_clamp, input.uvsets);
        half weight = packed_weights[terrain_layer.weight_map_channel];
        if (weight <= 0.0h)
        {
            continue;
        }

        half4 layer_base_color;
        half3 layer_tangent_normal;
        half3 layer_emissive;
        half layer_roughness;
        half layer_metallic;
        half layer_reflectance;
        SampleTerrainLayer(terrain_layer, input.worldpos, layer_base_color, layer_tangent_normal, layer_emissive, layer_roughness, layer_metallic, layer_reflectance);
        base_color += layer_base_color * weight;
        tangent_normal += layer_tangent_normal * weight;
        emissive += layer_emissive * weight;
        roughness += layer_roughness * weight;
        metallic += layer_metallic * weight;
        reflectance += layer_reflectance * weight;
        weight_sum += weight;
    }

    if (weight_sum > 0.0h)
    {
        half inverse_weight = rcp(weight_sum);
        base_color *= inverse_weight;
        tangent_normal *= inverse_weight;
        emissive *= inverse_weight;
        roughness *= inverse_weight;
        metallic *= inverse_weight;
        reflectance *= inverse_weight;
    }
    else
    {
        SampleTerrainLayer(GetTerrainLayer(terrain.layer_offset), input.worldpos, base_color, tangent_normal, emissive, roughness, metallic, reflectance);
    }
}

float4 main(TerrainPixelInput input, in bool is_frontface : SV_IsFrontFace) : SV_Target
{
    ShaderTerrain terrain;
    half4 base_color;
    half3 tangent_normal;
    half3 emissive;
    half perceptual_roughness;
    half metallic;
    half reflectance;
    SampleTerrain(input, terrain, base_color, tangent_normal, emissive, perceptual_roughness, metallic, reflectance);
    if (terrain.IsUsingVertexColors())
    {
        base_color *= input.color;
    }

#ifdef ALPHATEST
    clip(base_color.a - terrain.alpha_cutoff);
#endif

#ifdef UNLIT
    return half4(saturateMediump(base_color.rgb + emissive * GetCamera().exposure), base_color.a);
#else
    half3 world_normal = normalize(input.nor);
    if (!is_frontface)
    {
        world_normal = -world_normal;
    }

    Surface surface;
    surface.Init();
    surface.P = input.worldpos;
    surface.V = input.GetViewVector();
    surface.receive_shadow = terrain.IsReceiveShadow();
    surface.N = normalize(mul(normalize(tangent_normal), GetTerrainTangentBasis(input.worldpos, world_normal)));
    surface.NoV = saturate(abs(dot(surface.N, surface.V)) + FLT_EPSILON);
    perceptual_roughness = clamp(perceptual_roughness, 0.045h, 1.0h);
    surface.roughness = perceptual_roughness * perceptual_roughness;
    metallic = saturate(metallic);
    surface.albedo = base_color.rgb * (1.0h - metallic);
    half3 dielectric_f0 = 0.16h * reflectance * reflectance;
    surface.f0 = lerp(dielectric_f0, base_color.rgb, metallic);
    surface.emissive_color = emissive * GetCamera().exposure;

    Lighting lighting;
    lighting.Create(0, 0, 0, 0);
    EvaluateIndirectLighting(surface, lighting, input.pos.xy);
    EvaluateDirectLighting(surface, lighting, input.pos.xy);
    half4 final_color = half4(surface.albedo * (lighting.direct.diffuse + lighting.indirect.diffuse) * Fd_Lambert(), base_color.a);
    final_color.rgb += lighting.direct.specular + lighting.indirect.specular + surface.emissive_color;

#ifndef WON_SHIPPING
    final_color = ApplyDebugViewMode(final_color, surface, base_color, metallic, input.pos.xy, 0);
#endif

    final_color.rgb = saturateMediump(final_color.rgb);
    return final_color;
#endif
}

#endif
