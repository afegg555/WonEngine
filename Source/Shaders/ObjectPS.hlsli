#ifndef OBJECT_PS
#define OBJECT_PS
#include "ObjectCommon.hlsli"
#ifndef UNLIT
#include "ShadingCommon.hlsli"
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

#ifdef ALPHATEST
    clip(final_color.a - material.GetAlphaCutoff());
#endif // ALPHATEST


#else
    Surface surface;
    surface.Init();
    
    surface.P = input.worldpos;
    surface.V = input.GetViewVector();
    
    ShaderMaterial material = GetMaterial();
    surface.receive_shadow = material.IsReceiveShadow();
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
        // z is derived instead of read: two channel formats such as BC5 do not store it at all,
        // and for three channel maps the stored value is redundant with xy
        half3 normal;
        normal.xy = material.textures[NORMALMAP].Sample(sampler_objectshader, uvsets).xy * 2 - 1;
        normal.z = sqrt(saturate(1 - dot(normal.xy, normal.xy)));

        surface.N = normalize(mul(normal, TBN));
    }
#endif // OBJECTSHADER_USE_UVSETS
#endif // OBJECTSHADER_USE_TANGENT
    
    surface.NoV = saturate(abs(dot(surface.N, surface.V)) + FLT_EPSILON);
    
#ifdef OBJECTSHADER_USE_COLOR
    base_color *= input.color;
#endif // OBJECTSHADER_USE_COLOR

#ifdef ALPHATEST
    clip(base_color.a - material.GetAlphaCutoff());
#endif // ALPHATEST

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
    
    Lighting lighting;
    lighting.Create(0, 0, 0, 0);
    
    EvaluateIndirectLighting(surface, lighting);
    EvaluateDirectLighting(surface, lighting, input.pos.xy); // note: overflow can results in INF, but we will clamp
    
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
