#ifndef OBJECT_PS
#define OBJECT_PS
#include "ObjectCommon.hlsli"
#ifndef UNLIT
#include "ShadingCommon.hlsli"
#include "DDGICommon.hlsli"
#endif

float4 main(PixelInput input, in bool is_frontface : SV_IsFrontFace) : SV_Target
{
    half4 final_color;
#ifdef UNLIT
    ShaderMaterial material = GetMaterial(push.material_index);
    final_color = material.GetBaseColor();

#ifdef OBJECTSHADER_USE_UVSETS
    float2 uvsets = input.uvsets;
    [branch]
    if (material.textures[BASECOLORMAP].IsValid())
    {
        final_color *= material.textures[BASECOLORMAP].Sample(sampler_objectshader, uvsets);
    }
#endif // OBJECTSHADER_USE_UVSETS

#ifdef OBJECTSHADER_USE_COLOR
    final_color *= input.color;
#endif // OBJECTSHADER_USE_COLOR
    
    
#else
    Surface surface;
    surface.Init();
    
    surface.P = input.worldpos;
    surface.V = input.GetViewVector();
    
    ShaderMaterial material = GetMaterial();
    half4 base_color = material.GetBaseColor();
    
#ifdef OBJECTSHADER_USE_NORMAL
    if (is_frontface == false)
    {
        input.nor = -input.nor;
    }
    surface.N = normalize(input.nor);
#endif // OBJECTSHADER_USE_NORMAL
    
#ifdef OBJECTSHADER_USE_UVSETS
    float2 uvsets = input.uvsets;
    [branch]
    if (material.textures[BASECOLORMAP].IsValid())
    {
        base_color *= material.textures[BASECOLORMAP].Sample(sampler_objectshader, uvsets);
    }
#endif // OBJECTSHADER_USE_UVSETS
    
#ifdef OBJECTSHADER_USE_TANGENT
    surface.T = input.tan;
    surface.T.xyz = normalize(surface.T.xyz);
    
    surface.T.w = surface.T.w < 0 ? -1 : 1;
    half3 bitangent = cross(surface.T.xyz, surface.N) * surface.T.w;
    surface.B = bitangent;
    float3x3 TBN = float3x3(surface.T.xyz, bitangent, surface.N);
    
#ifdef OBJECTSHADER_USE_UVSETS
    [branch]
    if (material.textures[NORMALMAP].IsValid())
    {
        half3 normal = material.textures[NORMALMAP].Sample(sampler_objectshader, uvsets);
        normal = normal * 2 - 1;
	    
        surface.N = normalize(mul(normal, TBN));
    }
#endif // OBJECTSHADER_USE_UVSETS
#endif // OBJECTSHADER_USE_TANGENT
    
    surface.NoV = saturate(abs(dot(surface.N, surface.V)) + FLT_EPSILON);
    
#ifdef OBJECTSHADER_USE_COLOR
    base_color *= input.color;
#endif // OBJECTSHADER_USE_COLOR
    
#ifdef OBJECTSHADER_USE_EMISSIVE
    surface.emissive_color = material.GetEmissiveColor();
#ifdef OBJECTSHADER_USE_UVSETS
	[branch]
    if (any(surface.emissive_color) && material.textures[EMISSIVEMAP].IsValid())
    {
        half4 emissiveMap = material.textures[EMISSIVEMAP].Sample(sampler_objectshader, uvsets);
        surface.emissive_color *= emissiveMap.rgb * emissiveMap.a;
    }
#endif // OBJECTSHADER_USE_UVSETS
    surface.emissive_color *= GetCamera().exposure;
#endif // OBJECTSHADER_USE_EMISSIVE
    
    half metallic = material.GetMetallic();
    {
		// Metallic-roughness workflow:
        half perceptual_roughness = material.GetRoughness();

#ifdef OBJECTSHADER_USE_UVSETS
        [branch]
        if (material.textures[ROUGHNESSMAP].IsValid())
        {
            perceptual_roughness *= material.textures[ROUGHNESSMAP].Sample(sampler_objectshader, uvsets).g;
        }
        [branch]
        if (material.textures[METALLICMAP].IsValid())
        {
            metallic *= material.textures[METALLICMAP].Sample(sampler_objectshader, uvsets).b;
        }
#endif // OBJECTSHADER_USE_UVSETS

        perceptual_roughness = clamp(perceptual_roughness, 0.045, 1.0); // fp32
        //perceptual_roughness = clamp(perceptual_roughness, 0.089, 1.0); // fp16
        surface.roughness = perceptual_roughness * perceptual_roughness; // perceptually linear roughness to roughness
        metallic = saturate(metallic);
        half reflectance = material.GetReflectance();

        surface.albedo = base_color.rgb * (1 - metallic);
        half3 dielectricF0 = 0.16 * reflectance * reflectance;
        surface.f0 = lerp(dielectricF0, base_color.xyz, metallic);
    }
    
    final_color = base_color;
    
    float3 ambient = float3(0.0, 0.0, 0.0);
    ShaderEnvironment environment_lighting = GetEnvironment();
    if (environment_lighting.GetDiffuseGIMode() == SHADER_DIFFUSE_GI_MODE_AMBIENT)
    {
        ambient = environment_lighting.GetAmbientColor() * environment_lighting.GetAmbientIntensity();
    }
    else if (environment_lighting.GetDiffuseGIMode() == SHADER_DIFFUSE_GI_MODE_DDGI)
    {
        ShaderDDGIVolume ddgi_volume = GetDDGIVolume();
        if (ddgi_volume.IsActive() && ddgi_volume.HasIrradianceTexture())
        {
            float3 sample_position = surface.P + surface.N * ddgi_volume.normal_bias + surface.V * ddgi_volume.view_bias;
            if (IsInsideDDGIVolume(ddgi_volume, sample_position))
            {
                ambient = SampleDDGI(ddgi_volume, sample_position, surface.N);
                ambient *= environment_lighting.GetIndirectDiffuseScale();
            }

        }
    }
    else if (environment_lighting.GetDiffuseGIMode() == SHADER_DIFFUSE_GI_MODE_CUBEMAP
        || environment_lighting.GetDiffuseGIMode() == SHADER_DIFFUSE_GI_MODE_SKY)
    {
        if (environment_lighting.HasIrradianceCubemap())
        {
            ambient = bindless_cubemaps[DescriptorIndex(environment_lighting.irradiance_cubemap)].SampleLevel(sampler_linear_clamp, surface.N, 0).rgb;
            ambient *= environment_lighting.GetIndirectDiffuseScale();
        }
    }
    float3 indirect_specular = float3(0.0, 0.0, 0.0);
    if (environment_lighting.GetReflectionMode() == SHADER_REFLECTION_MODE_CUBEMAP
        || environment_lighting.GetReflectionMode() == SHADER_REFLECTION_MODE_SKY)
    {
        float3 reflection_direction = reflect(-surface.V, surface.N);
        float perceptual_roughness = sqrt(surface.roughness);

        float3 reflection_radiance = float3(0.0, 0.0, 0.0);
        bool has_reflection = false;
        if (environment_lighting.HasSpecularCubemap())
        {
            float lod = perceptual_roughness * max(environment_lighting.specular_mip_count - 1.0, 0.0);
            reflection_radiance = bindless_cubemaps[DescriptorIndex(environment_lighting.specular_cubemap)].SampleLevel(sampler_linear_clamp, reflection_direction, lod).rgb;
            has_reflection = true;
        }

        ShaderReflectionProbe reflection_probe = GetReflectionProbe();
        if (reflection_probe.IsActive() && reflection_probe.HasCubemap())
        {
            float distance_to_probe = distance(surface.P, reflection_probe.position);
            float probe_attenuation = reflection_probe.influence_radius > 0.0
                ? saturate(1.0 - distance_to_probe / reflection_probe.influence_radius)
                : 1.0;
            if (probe_attenuation > 0.0)
            {
                float lod = perceptual_roughness * max(reflection_probe.cubemap_mip_count - 1.0, 0.0);
                float3 probe_radiance = bindless_cubemaps[DescriptorIndex(reflection_probe.cubemap_texture)].SampleLevel(sampler_linear_clamp, reflection_direction, lod).rgb * reflection_probe.intensity;
                reflection_radiance = lerp(reflection_radiance, probe_radiance, probe_attenuation);
                has_reflection = true;
            }
        }

        if (has_reflection)
        {
            indirect_specular = reflection_radiance * EnvBRDF(environment_lighting.brdf_lut, surface.f0, perceptual_roughness, surface.NoV);
            indirect_specular *= environment_lighting.GetIndirectSpecularScale();
        }
    }

    Lighting lighting;
    ambient *= GetCamera().exposure;
    indirect_specular *= GetCamera().exposure;
    lighting.Create(0, 0, ambient, indirect_specular);
    
    ApplyLighting(surface, lighting, input.pos.xy); // note: overflow can results in INF, but we will clamp
    
    half3 diffuse = (lighting.direct.diffuse + lighting.indirect.diffuse) * Fd_Lambert(); // apply fd here for efficiency
    half3 specular = lighting.direct.specular + lighting.indirect.specular;
    final_color.rgb = surface.albedo * diffuse;
    final_color.rgb += specular;
    final_color.rgb += surface.emissive_color;

#ifndef WON_SHIPPING
    final_color = ApplyDebugViewMode(final_color, surface, base_color, metallic, input.pos.xy);
#endif

#endif // UNLIT
 
    final_color.rgb = saturateMediump(final_color.rgb);
    return final_color;
}
#endif // OBJECT_PS
