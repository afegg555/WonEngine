#ifndef SHADING_COMMON
#define SHADING_COMMON

#include "Common.hlsli"
#include "BRDFCommon.hlsli"
#include "SkyCommon.hlsli"
#include "ShaderInterop_LightCull.h"

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

inline float SampleDirectionalShadowCascade(in ShaderShadowCascade cascade, in float3 world_position, in half3 N, in half NoL)
{
    world_position += (float3) N * cascade.texel_world_size * 2.0f;
    float4 shadow_pos = mul(cascade.shadow_view_projection, float4(world_position, 1.0f));
    float3 shadow_ndc = shadow_pos.xyz / shadow_pos.w;
    float2 shadow_uv = shadow_ndc.xy * float2(0.5f, -0.5f) + 0.5f;

    if (shadow_uv.x < 0.0f || shadow_uv.x > 1.0f ||
        shadow_uv.y < 0.0f || shadow_uv.y > 1.0f ||
        shadow_ndc.z < 0.0f || shadow_ndc.z > 1.0f)
    {
        return 1.0f;
    }

    float2 atlas_uv = shadow_uv * cascade.shadow_atlas_scale_bias.xy + cascade.shadow_atlas_scale_bias.zw;
    uint atlas_width = 0;
    uint atlas_height = 0;
    bindless_textures[DescriptorIndex(GetScene().shadow_atlas)].GetDimensions(atlas_width, atlas_height);
    float2 atlas_texel = 1.0f / float2(atlas_width, atlas_height);
    float2 atlas_uv_min = cascade.shadow_atlas_scale_bias.zw;
    float2 atlas_uv_max = cascade.shadow_atlas_scale_bias.xy + cascade.shadow_atlas_scale_bias.zw;
    float shadow_bias = max(0.0005f * (1.0f - NoL), 0.00005f);
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

    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            float2 sample_uv = atlas_uv + float2(x, y) * filter_step;
            sample_uv = clamp(sample_uv, atlas_uv_min + atlas_texel * 0.5f, atlas_uv_max - atlas_texel * 0.5f);
            float shadow_depth = shadow_map.SampleLevel(sampler_point_clamp, sample_uv, 0).r;
            visibility += shadow_ndc.z + shadow_bias > shadow_depth ? 1.0f : 0.0f;
        }
    }

    return visibility / 25.0f;
}

inline void LightDirectional(in ShaderLight light, uint light_index, float3 radiance_scale, in Surface surface, inout Lighting lighting)
{
    half3 L = normalize(-light.GetDirection());
    LightingContext lighting_context;
    lighting_context.Create(surface, L);

    if (lighting_context.NoL <= FLT_EPSILON)
        return; // facing away from light
    
    half3 light_color = (half3) (light.GetColor().xyz * radiance_scale * GetCamera().exposure); // pre exposure to avoid precision issues with small values
    
	[branch]
    if (light.IsCastingShadow() && GetMaterial().IsReceiveShadow())
    {
        uint shadow_slice = (GetScene().light_shadow_slice_buffer >= 0)
            ? bindless_buffers_uint[DescriptorIndex(GetScene().light_shadow_slice_buffer)][light_index] : 0u;
        uint shadow_slice_offset = shadow_slice & 0xFFFFu;
        uint shadow_slice_count = (shadow_slice >> 16u) & 0xFFFFu;

        if (GetScene().shadow_atlas >= 0 && GetScene().shadow_cascade_buffer >= 0 && shadow_slice_count > 0)
        {
            ShaderCamera camera = GetCamera();
            float linear_depth = max(0.0f, dot(surface.P - camera.position, camera.forward));
            uint cascade_local_index = 0;

            [unroll]
            for (uint i = 0; i < 4; ++i)
            {
                if (i >= shadow_slice_count)
                {
                    break;
                }
                ShaderShadowCascade cascade_candidate = GetShadowCascade(shadow_slice_offset + i);
                if (linear_depth <= cascade_candidate.split_far)
                {
                    cascade_local_index = i;
                    break;
                }
                cascade_local_index = i;
            }

            ShaderShadowCascade cascade = GetShadowCascade(shadow_slice_offset + cascade_local_index);
            float visibility = SampleDirectionalShadowCascade(cascade, surface.P, surface.N, lighting_context.NoL);

            if (cascade_local_index + 1 < shadow_slice_count)
            {
                float split_near = camera.z_near;
                if (cascade_local_index > 0)
                {
                    split_near = GetShadowCascade(shadow_slice_offset + cascade_local_index - 1).split_far;
                }

                float cascade_range = max(cascade.split_far - split_near, 0.0001f);
                float blend_distance = cascade_range * cascade.blend_band;
                if (blend_distance > 0.0f)
                {
                    float blend_start = cascade.split_far - blend_distance;
                    if (linear_depth > blend_start)
                    {
                        ShaderShadowCascade next_cascade = GetShadowCascade(shadow_slice_offset + cascade_local_index + 1);
                        float next_visibility = SampleDirectionalShadowCascade(next_cascade, surface.P, surface.N, lighting_context.NoL);
                        float blend_weight = saturate((linear_depth - blend_start) / blend_distance);
                        visibility = lerp(visibility, next_visibility, blend_weight);
                    }
                }
            }

            light_color *= visibility;
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
	
    half3 light_color = (half3) (light.GetColor().rgb * GetCamera().exposure); // pre exposure to avoid precision issues with small values
	
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

    half3 light_color = (half3) (light.GetColor().rgb * GetCamera().exposure); // pre exposure to avoid precision issues with small values
    light_color *= GetSquareFalloffAttenuation(dist2, range2);
    light_color *= GetSpotAngleAttenuation(spot_factor, inner_cone_angle_cos, outer_cone_angle_cos);
    
    lighting.direct.diffuse = mad(light_color, GetDiffuseBRDF(surface, context), lighting.direct.diffuse);
    lighting.direct.specular = mad(light_color, GetSpecularBRDF(surface, context), lighting.direct.specular);
}

inline void ForwardLighting(inout Surface surface, inout Lighting lighting, float2 pixel_position)
{
    for (uint d = 0; d < GetScene().directional_count; ++d)
    {
        LightDirectional(GetLight(d), d, float3(1.0f, 1.0f, 1.0f), surface, lighting);
    }

    if (GetEnvironment().GetDerivedSunIndex() != ~0u)
    {
        const uint derived_sun_index = GetEnvironment().GetDerivedSunIndex();
        LightDirectional(GetLight(derived_sun_index), derived_sun_index, EvaluatePhysicalSkyExtinction(GetEnvironment(), GetEnvironment().GetSunDirection()), surface, lighting);
    }

#ifdef CLUSTERED
    const uint2 tile = (uint2)(pixel_position / LIGHTCULL_TILE_SIZE);
    const float cluster_view_z = max(0.0f, dot(surface.P - GetCamera().position, GetCamera().forward));
    const uint cluster_slice = ClusterSliceFromViewZ(cluster_view_z, GetCamera().z_near, GetCamera().z_far, GetScene().cluster_depth_slices);
    const uint cluster_tiles = GetScene().cluster_count.x * GetScene().cluster_count.y;
    const uint cluster_index = cluster_slice * cluster_tiles + tile.y * GetScene().cluster_count.x + tile.x;
    const uint cluster_light_count = bindless_buffers_uint[DescriptorIndex(GetScene().cluster_light_count_buffer)][cluster_index];
    const uint cluster_light_base = bindless_buffers_uint[DescriptorIndex(GetScene().cluster_light_offset_buffer)][cluster_index];
    [loop]
    for (uint t = 0; t < cluster_light_count; ++t)
    {
        const uint light_index = bindless_buffers_uint[DescriptorIndex(GetScene().cluster_light_index_buffer)][cluster_light_base + t];
        ShaderLight light = GetLight(light_index);
        switch (light.GetType())
        {
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
#else
    if (GetScene().forward_light_index_buffer >= 0)
    {
        const uint forward_light_count = GetScene().forward_light_count;
        [loop]
        for (uint t = 0; t < forward_light_count; ++t)
        {
            const uint light_index = bindless_buffers_uint[DescriptorIndex(GetScene().forward_light_index_buffer)][t];
            ShaderLight light = GetLight(light_index);
            switch (light.GetType())
            {
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
#endif
}

inline void ApplyLighting(inout Surface surface, inout Lighting lighting, float2 pixel_position)
{
#ifdef FORWARD
    ForwardLighting(surface, lighting, pixel_position);
#endif
}

#ifndef WON_SHIPPING
static const uint debug_complexity_max_lights = 32u;
static const float3 debug_complexity_low = float3(0.0f, 0.2f, 0.0f); // green
static const float3 debug_complexity_mid = float3(1.0f, 1.0f, 0.0f); // yellow
static const float3 debug_complexity_high = float3(1.0f, 0.0f, 0.0f); // red
static const half3 debug_cascade_tint[4] = {
    half3(1.0h, 0.3h, 0.3h), // red
    half3(0.3h, 1.0h, 0.3h), // green
    half3(0.3h, 0.3h, 1.0h), // blue
    half3(1.0h, 1.0h, 0.3h), // yellow
};
static const half3 debug_overdraw_color = half3(0.06h, 0.06h, 0.06h); // dark gray

inline uint GetDebugLightCount(in float3 world_position, in float2 pixel_position)
{
    uint light_count = GetScene().directional_count;
#ifdef CLUSTERED
    const uint2 tile = (uint2)(pixel_position / LIGHTCULL_TILE_SIZE);
    const float cluster_view_z = max(0.0f, dot(world_position - GetCamera().position, GetCamera().forward));
    const uint cluster_slice = ClusterSliceFromViewZ(cluster_view_z, GetCamera().z_near, GetCamera().z_far, GetScene().cluster_depth_slices);
    const uint cluster_tiles = GetScene().cluster_count.x * GetScene().cluster_count.y;
    const uint cluster_index = cluster_slice * cluster_tiles + tile.y * GetScene().cluster_count.x + tile.x;
    if (GetScene().cluster_light_count_buffer >= 0)
    {
        light_count += bindless_buffers_uint[DescriptorIndex(GetScene().cluster_light_count_buffer)][cluster_index];
    }
#else
    if (GetScene().forward_light_index_buffer >= 0)
    {
        light_count += GetScene().forward_light_count;
    }
#endif
    return light_count;
}

inline float3 GetDebugLightComplexityColor(in uint light_count)
{
    const float t = saturate(float(light_count) / float(debug_complexity_max_lights));
    if (t < 0.5f)
    {
        return lerp(debug_complexity_low, debug_complexity_mid, t * 2.0f);
    }
    return lerp(debug_complexity_mid, debug_complexity_high, t * 2.0f - 1.0f);
}

inline int GetDebugShadowCascadeIndex(in float3 world_position)
{
    if (GetScene().shadow_cascade_buffer < 0 || GetScene().light_shadow_slice_buffer < 0)
    {
        return -1;
    }

    for (uint d = 0; d < GetScene().directional_count; ++d)
    {
        ShaderLight light = GetLight(d);
        if (!light.IsCastingShadow())
        {
            continue;
        }

        const uint shadow_slice = bindless_buffers_uint[DescriptorIndex(GetScene().light_shadow_slice_buffer)][d];
        const uint shadow_slice_offset = shadow_slice & 0xFFFFu;
        const uint shadow_slice_count = (shadow_slice >> 16u) & 0xFFFFu;
        if (shadow_slice_count == 0)
        {
            continue;
        }

        ShaderCamera camera = GetCamera();
        const float linear_depth = max(0.0f, dot(world_position - camera.position, camera.forward));
        uint cascade_local_index = 0;

        [unroll]
        for (uint i = 0; i < 4; ++i)
        {
            if (i >= shadow_slice_count)
            {
                break;
            }
            ShaderShadowCascade cascade_candidate = GetShadowCascade(shadow_slice_offset + i);
            if (linear_depth <= cascade_candidate.split_far)
            {
                cascade_local_index = i;
                break;
            }
            cascade_local_index = i;
        }
        return (int)cascade_local_index;
    }
    return -1;
}

inline half4 ApplyDebugViewMode(in half4 lit_color, in Surface surface, in half4 base_color, in half metallic, in float2 pixel_position)
{
    const uint debug_view_mode = GetScene().debug_view_mode;
    if (debug_view_mode == DEBUG_VIEW_MODE_NONE || debug_view_mode == DEBUG_VIEW_MODE_WIREFRAME)
    {
        return lit_color;
    }

    half4 debug_color = lit_color;
    switch (debug_view_mode)
    {
    case DEBUG_VIEW_MODE_UNLIT:
        debug_color = half4(base_color.rgb + (half3)surface.emissive_color, lit_color.a);
        break;
    case DEBUG_VIEW_MODE_BASE_COLOR:
        debug_color = half4(base_color.rgb, lit_color.a);
        break;
    case DEBUG_VIEW_MODE_WORLD_NORMAL:
        debug_color = half4((half3)(surface.N * 0.5f + 0.5f), lit_color.a);
        break;
    case DEBUG_VIEW_MODE_ROUGHNESS:
        debug_color = half4((half3)sqrt(surface.roughness).xxx, lit_color.a);
        break;
    case DEBUG_VIEW_MODE_METALLIC:
        debug_color = half4(metallic.xxx, lit_color.a);
        break;
    case DEBUG_VIEW_MODE_LIGHT_COMPLEXITY:
        debug_color = half4((half3)GetDebugLightComplexityColor(GetDebugLightCount(surface.P, pixel_position)), lit_color.a);
        break;
    case DEBUG_VIEW_MODE_SHADOW_CASCADES:
    {
        const int cascade_index = GetDebugShadowCascadeIndex(surface.P);
        const half3 cascade_tint = (cascade_index >= 0 && cascade_index < 4) ? debug_cascade_tint[cascade_index] : half3(1.0h, 1.0h, 1.0h);
        debug_color = half4((half3)(base_color.rgb * cascade_tint), lit_color.a);
        break;
    }
    case DEBUG_VIEW_MODE_OVERDRAW:
        debug_color = half4(debug_overdraw_color, 1.0h);
        break;
    }
    return debug_color;
}
#endif // WON_SHIPPING

#endif // SHADING_COMMON
