#ifndef SHADING_COMMON
#define SHADING_COMMON

#include "Common.hlsli"
#include "BRDFCommon.hlsli"

#define min_roughness 0.045

struct Surface
{
    float3 P; // world space position
    float3 N; // world space normal
    float3 V; // world space view vector

    half4 T; // tangent
    half3 B; // bitangent
    
    half3 albedo; // diffuse light absorbtion value (rgb)
    half3 f0; // fresnel value (rgb) (reflectance at incidence angle, also known as specular color)
    half roughness;
    half occlusion;
    half3 emissive_color;

    inline void Init()
    {
        P = float3(0.0f, 0.0f, 0.0f);
        V = float3(0.0f, 0.0f, 0.0f);
        N = float3(0.0f, 0.0f, 0.0f);
        
        T = half4(0.0h, 0.0h, 0.0h, 0.0h);
        B = half3(0.0h, 0.0h, 0.0h);
        
        albedo = half3(0.0h, 0.0h, 0.0h);
        f0 = half3(0.0h, 0.0h, 0.0h);
        roughness = (half) 1.0h;
        occlusion = (half) 1.0h;
        emissive_color = half3(0.0h, 0.0h, 0.0h);
    }
};

struct LightingContext
{
    half3 L; // surface to light vector (normalized)

    float3 H; // half-vector between view vector and light vector
    half NoL; // cos angle between normal and light direction
    
    float NoH; // cos angle between normal and half vector
    half LoH; // cos angle between light direction and half vector
    half VoH; // cos angle between view direction and half vector
    half3 F; // fresnel term computed from VdotH

    inline void Create(in Surface surface, in half3 Lnormalized)
    {
        L = Lnormalized;
        H = normalize(L + surface.V);

        NoL = saturate(dot(L, surface.N));
        
        NoH = saturate(dot(surface.N, H));
        LoH = saturate(dot(L, H));
        VoH = saturate(dot(surface.V, H));

        F = F_Schlick(surface.f0, VoH);
    }
};

struct LightingPart
{
    half3 diffuse;
    half3 specular;
};

struct Lighting
{
    LightingPart direct;
    LightingPart indirect;

    inline void Create(
		in half3 diffuse_direct,
		in half3 specular_direct,
		in half3 diffuse_indirect,
		in half3 specular_indirect
	)
    {
        direct.diffuse = diffuse_direct;
        direct.specular = specular_direct;
        indirect.diffuse = diffuse_indirect;
        indirect.specular = specular_indirect;
    }
};

half3 GetSpecularBRDF(in Surface surface, in LightingContext lighting_context)
{
    half r = saturate(surface.roughness);
    half e = exp2((1.0h - r) * 13.0h + 2.0h);
    half phongExponent = max((half) 2.0h, e);

    half NdotH = saturate(lighting_context.NoH);
    half specLobe = pow(NdotH, phongExponent);
    
    half3 specular = lighting_context.F * specLobe;

    return specular * lighting_context.NoL;
}
half3 GetDiffuseBRDF(in Surface surface, in LightingContext lighting_context)
{
    half ndotl = saturate(lighting_context.NoL);

    half3 kd = half3(1.h, 1.h, 1.h);
    half3 diffuse = surface.albedo * kd; // * (1.0h / 3.14159265h);

    return diffuse * ndotl;
}

inline void LightDirectional(in ShaderLight light, in Surface surface, inout Lighting lighting)
{
    half3 L = normalize(-light.direction);
    LightingContext lighting_context;
    lighting_context.Create(surface, L);

    if (lighting_context.NoL <= FLT_EPSILON)
        return; // facing away from light
    
    half3 light_color = light.color;
    
    lighting.direct.diffuse = mad(light_color, GetDiffuseBRDF(surface, lighting_context), lighting.direct.diffuse);
    lighting.direct.specular = mad(light_color, GetSpecularBRDF(surface, lighting_context), lighting.direct.specular);
}

inline void ForwardLighting(inout Surface surface, inout Lighting lighting)
{
    ShaderLight light;
    light.direction = float3(0, -0.f, 1.f);
    light.color = float3(10, 10, 10);
    LightDirectional(light, surface, lighting);
}

#endif // SHADING_COMMON