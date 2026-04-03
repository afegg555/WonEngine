#ifndef SHADING_COMMON
#define SHADING_COMMON

#include "Common.hlsli"
#include "BRDFCommon.hlsli"

#define min_roughness 0.045

//#define PCSS_SHADOW

struct Surface
{
    float3 P; // world space position
    float3 N; // world space normal
    float3 V; // world space view vector

    half4 T; // tangent
    half3 B; // bitangent
    
    half NoV;
    
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
        
        NoV = 0.0h;
        
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
#ifdef PHONG
    // Blinn-Phong (almost 4 ~ 16384)
    half r = saturate(surface.roughness);
    half e = exp2((1.0h - r) * 13.0h + 2.0h);
    half phongExponent = max((half) 2.0h, e);

    half NoH = saturate(lighting_context.NoH);
    half specLobe = pow(NoH, phongExponent);
    
    half3 specular = lighting_context.F * specLobe;

    return specular * lighting_context.NoL;
#else // PHONG
    
#ifdef ANISOTROPIC
    half D = D_GGX_Anisotropic(lighting_context.NoH, lighting_context.H, surface.T.xyz, surface.B, surface.roughness, surface.roughnessBitangent);
    half Vis = V_SmithGGXCorrelated_Anisotropic(surface.roughness, surface.roughnessBitangent, 
        lighting_context.ToV, lighting_context.BoV, lighting_context.ToL, lighting_context.BoL, surface.NoV, lighting_context.NoL);
#else
    half D = D_GGX(surface.roughness, lighting_context.NoH, surface.N, lighting_context.H);
    half Vis = V_SmithGGXCorrelated(surface.roughness, surface.NoV, lighting_context.NoL);
#endif
    half3 specular = D * Vis * lighting_context.F;
    
#ifdef SHEEN
    D = D_Charlie(surface.sheenRoughness, lighting_context.NoH);
    Vis = V_Neubelt(surface.NoV, lighting_context.NoL);
	specular += D * Vis * surface.sheenColor;
#endif // SHEEN

#ifdef CLEARCOAT
    specular *= 1 - lighting_context.FClearCoat;
    D = D_GGX(surface.clearCoatRoughness, lighting_context.NoH, surface.N, lighting_context.H);
    Vis = V_Kelemen(lighting_context.LoH);
    specular += D * Vis * lighting_context.FClearCoat;
#endif // CLEARCOAT
    
    return specular * lighting_context.NoL;
#endif //PHONG
}
half3 GetDiffuseBRDF(in Surface surface, in LightingContext lighting_context)
{
#ifdef PHONG
    half ndotl = saturate(lighting_context.NoL);

    // Diffuse reduced by Fresnel (common in hybrid models):
    //half3 kd = (1.0h - surface_to_light.F);
    half3 kd = half3(1.h, 1.h, 1.h);
    half3 diffuse = surface.albedo * kd;// * (1.0h / 3.14159265h);

    return diffuse * ndotl;
#else
	// Note: subsurface scattering will remove Fresnel (F), because otherwise
	//	there would be artifact on backside where diffuse wraps
    half3 diffuse = 1 - lighting_context.F;

#ifdef CLEARCOAT
    diffuse *= 1 - lighting_context.FClearCoat;
#endif // CLEARCOAT
    
    return diffuse * lighting_context.NoL;
#endif //PHONG
}

inline void LightDirectional(in ShaderLight light, in Surface surface, inout Lighting lighting)
{
    half3 L = normalize(-light.GetDirection());
    LightingContext lighting_context;
    lighting_context.Create(surface, L);

    if (lighting_context.NoL <= FLT_EPSILON)
        return; // facing away from light
    
    half3 light_color = light.GetColor().xyz;
    
	[branch]
    if (light.IsCastingShadow() && GetMaterial().IsReceiveShadow())
    {
        if (GetScene().shadow_atlas >= 0 && light.shadow_atlas_scale_bias.x > 0.0f && light.shadow_atlas_scale_bias.y > 0.0f)
        {
            float4 shadow_pos = mul(light.shadow_view_projection, float4(surface.P, 1.0f));
            float3 shadow_ndc = shadow_pos.xyz / shadow_pos.w;
            float2 shadow_uv = shadow_ndc.xy * float2(0.5f, -0.5f) + 0.5f;

            if (shadow_uv.x >= 0.0f && shadow_uv.x <= 1.0f &&
                shadow_uv.y >= 0.0f && shadow_uv.y <= 1.0f &&
                shadow_ndc.z >= 0.0f && shadow_ndc.z <= 1.0f)
            {
                float2 atlas_uv = shadow_uv * light.shadow_atlas_scale_bias.xy + light.shadow_atlas_scale_bias.zw;
                uint atlas_width = 0;
                uint atlas_height = 0;
                bindless_textures[DescriptorIndex(GetScene().shadow_atlas)].GetDimensions(atlas_width, atlas_height);
                float2 atlas_texel = 1.0f / float2(atlas_width, atlas_height);
                float2 atlas_uv_min = light.shadow_atlas_scale_bias.zw;
                float2 atlas_uv_max = light.shadow_atlas_scale_bias.xy + light.shadow_atlas_scale_bias.zw;
                float shadow_bias = max(0.0005f * (1.0f - lighting_context.NoL), 0.00005f);
                float visibility = 0.0f;
                Texture2D shadow_map = bindless_textures[DescriptorIndex(GetScene().shadow_atlas)];
                float2 filter_step = atlas_texel;
                
#ifdef PCSS_SHADOW
                float avg_blocker_depth = 0.0f;
                
                [unroll]
                for (int py = -2; py <= 2; ++py)
                {
                    [unroll]
                    for (int px = -2; px <= 2; ++px)
                    {
                        float2 sample_uv = atlas_uv + float2(px, py) * atlas_texel * 2.f;
                        sample_uv = clamp(sample_uv, atlas_uv_min + atlas_texel * 0.5f, atlas_uv_max - atlas_texel * 0.5f);
                        float shadow_depth = shadow_map.SampleLevel(sampler_point_clamp, sample_uv, 0).r;
                        [flatten]
                        if (shadow_ndc.z - shadow_bias < shadow_depth)
                        {
                            visibility += 1.0f;
                            avg_blocker_depth += shadow_depth;
                        }
                    }
                }
                if (visibility > 0.0f)
                {
                    avg_blocker_depth /= visibility;
                    float penumbra = saturate((shadow_ndc.z - avg_blocker_depth) / max(avg_blocker_depth, 0.0001f));
                    float filter_radius = lerp(1.0f, 6.0f, penumbra);
                    filter_step *= filter_radius;
                    visibility = 0.f;
                }
#endif
                // PCF
                [unroll]
                for (int y = -2; y <= 2; ++y)
                {
                    [unroll]
                    for (int x = -2; x <= 2; ++x)
                    {
                        float2 sample_uv = atlas_uv + float2(x, y) * filter_step;
                        sample_uv = clamp(sample_uv, atlas_uv_min + atlas_texel * 0.5f, atlas_uv_max - atlas_texel * 0.5f);
                        float shadow_depth = shadow_map.SampleLevel(sampler_point_clamp, sample_uv, 0).r;
                        visibility += shadow_ndc.z - shadow_bias > shadow_depth ? 1.0f : 0.0f;
                    }
                }

                visibility /= 25.0f;
                light_color *= visibility;
            }
        }
    }
    
    lighting.direct.diffuse = mad(light_color, GetDiffuseBRDF(surface, lighting_context), lighting.direct.diffuse);
    lighting.direct.specular = mad(light_color, GetSpecularBRDF(surface, lighting_context), lighting.direct.specular);
}

inline half GetSquareFalloffAttenuation(in half dist2, in half range2)
{
    float factor = dist2 / range2;
    float smooth_factor = max(1.0 - factor * factor, 0.0);
    return (smooth_factor * smooth_factor) / max(dist2, 1e-4);
}

inline half GetSpotAngleAttenuation(half spot_factor,
        half inner_angle_cos, half outer_angle_cos)
{
    // https://google.github.io/filament/Filament.md.html#lighting/directlighting/punctuallights/attenuationfunction
    const half spot_scale = rcp(max(inner_angle_cos - outer_angle_cos, 0.0001h));
    const half spot_offset = -outer_angle_cos * spot_scale;
    half angular_attenuation = saturate(mad(spot_factor, spot_scale, spot_offset));
    return angular_attenuation * angular_attenuation;
}


inline void LightPoint(in ShaderLight light, in Surface surface, inout Lighting lighting)
{
    half3 L = light.position - surface.P;

    const half dist2 = max(dot(L, L), 0.01);
    const half range = light.GetRange();
    const half range2 = range * range;
    
    if (dist2 > range2)
        return; // outside range
		
    const half dist_rcp = rsqrt(dist2);
    L = L * dist_rcp;

    LightingContext context;
    context.Create(surface, L);
		
    if (context.NoL <= FLT_EPSILON)
        return; // facing away from light
		
    half3 light_color = light.GetColor().rgb;
	
    light_color *= GetSquareFalloffAttenuation(dist2, range2);
    
    lighting.direct.diffuse = mad(light_color, GetDiffuseBRDF(surface, context), lighting.direct.diffuse);
    lighting.direct.specular = mad(light_color, GetSpecularBRDF(surface, context), lighting.direct.specular);
}

inline void LightSpotlight(in ShaderLight light, in Surface surface, inout Lighting lighting)
{
    half3 L = light.position - surface.P;

    const half dist2 = max(dot(L, L), 0.01);
    const half range = light.GetRange();
    const half range2 = range * range;
    
    if (dist2 > range2)
        return; // outside range
		
    const half dist_rcp = rsqrt(dist2);
    L = L * dist_rcp;

    LightingContext context;
    context.Create(surface, L);
		
    if (context.NoL <= FLT_EPSILON)
        return; // facing away from light

    const half spot_factor = dot(-L, light.GetDirection());
    const half outer_cone_angle_cos = light.GetOuterConeAngleCos();
    const half inner_cone_angle_cos = light.GetInnerConeAngleCos();
    
    if (spot_factor <= outer_cone_angle_cos)
        return; // outside spotlight cone

    half3 light_color = light.GetColor().rgb;
    light_color *= GetSquareFalloffAttenuation(dist2, range2);
    light_color *= GetSpotAngleAttenuation(spot_factor, inner_cone_angle_cos, outer_cone_angle_cos);
    
    lighting.direct.diffuse = mad(light_color, GetDiffuseBRDF(surface, context), lighting.direct.diffuse);
    lighting.direct.specular = mad(light_color, GetSpecularBRDF(surface, context), lighting.direct.specular);
}

inline void ForwardLighting(inout Surface surface, inout Lighting lighting)
{
    if (any(GetScene().lights))
    {
        uint max_bucket_count = 4;
        [unroll]
        for (uint bucket = 0; bucket < max_bucket_count; ++bucket)
        {
            uint bucket_bits = GetScene().lights[bucket];
            [loop]
            while (bucket_bits != 0)
            {
                // Retrieve global light index from local bucket, then remove bit from local bucket:
                const uint bucket_bit_index = firstbitlow(bucket_bits);
                const uint light_index = bucket * 32 + bucket_bit_index;
                bucket_bits ^= 1u << bucket_bit_index;
                
                ShaderLight light = GetLight(light_index);
                switch (light.GetType())
                {
                    case SHADER_LIGHT_TYPE_DIRECTIONAL:
				    {
                        LightDirectional(light, surface, lighting);
                    }
                    break;
                    case SHADER_LIGHT_TYPE_POINT:
				    {
                        LightPoint(light, surface, lighting);
                    }
                    break;
                    case SHADER_LIGHT_TYPE_SPOT:
				    {
                        LightSpotlight(light, surface, lighting);
                    }
                    break;
                }

            }
        }
    }
}

#endif // SHADING_COMMON
