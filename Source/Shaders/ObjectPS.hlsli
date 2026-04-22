#ifndef OBJECT_PS
#define OBJECT_PS
#include "ObjectCommon.hlsli"
#include "ShadingCommon.hlsli"

float4 main(PixelInput input, in bool is_frontface : SV_IsFrontFace) : SV_Target
{
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
#endif // OBJECTSHADER_USE_EMISSIVE
    
    {
		// Metallic-roughness workflow:
        half perceptual_roughness = material.GetRoughness();
        perceptual_roughness = clamp(perceptual_roughness, 0.045, 1.0); // fp32
        //perceptual_roughness = clamp(perceptual_roughness, 0.089, 1.0); // fp16
        surface.roughness = perceptual_roughness * perceptual_roughness; // perceptually linear roughness to roughness
        
        half metallic = material.GetMetallic();
        half reflectance = material.GetReflectance();

        surface.albedo = base_color.rgb * (1 - metallic);
        half3 dielectricF0 = 0.16 * reflectance * reflectance;
        surface.f0 = lerp(dielectricF0, base_color.xyz, metallic);
    }
    
    half4 final_color = base_color;
    
#ifndef UNLIT
    half3 ambient = half3(0.0, 0.0, 0.0);
    ShaderEnvironmentLighting environment_lighting = GetEnvironmentLighting();
    if (environment_lighting.IsActive() && environment_lighting.gi_mode == SHADER_ENVIRONMENT_GI_MODE_AMBIENT)
    {
        ambient = environment_lighting.GetAmbientColor() * environment_lighting.GetAmbientIntensity();
    }

    Lighting lighting;
    lighting.Create(0, 0, ambient, 0);
    
    ForwardLighting(surface, lighting);
    
    half3 diffuse = (lighting.direct.diffuse + lighting.indirect.diffuse) * Fd_Lambert(); // apply fd here for efficiency
    half3 specular = lighting.direct.specular + lighting.indirect.specular;
    final_color.rgb = surface.albedo * diffuse;
    final_color.rgb += specular;
    final_color.rgb += surface.emissive_color;
#else
    
#endif // UNLIT
    
    final_color = saturateMediump(final_color);
    
    // !!! temp Reinhard tone mapping
    final_color.xyz = final_color.xyz / (1.0 + final_color.xyz);
    
    return final_color;
}
#endif // OBJECT_PS
