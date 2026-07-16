#ifndef BRDF_COMMON
#define BRDF_COMMON
#include "Common.hlsli"

// BRDF functions source: https://github.com/google/filament/blob/main/shaders/src/brdf.fs
// https://google.github.io/filament/Filament.md.html

float D_GGX(float roughness, float NoH, float3 n, float3 h) {
    float3 NxH = cross(n, h);
    float a = NoH * roughness;
    float k = roughness / (dot(NxH, NxH) + a * a);
    float d = k * k * (1.0 / PI);
    return saturateMediump(d);
}

half D_GGX_Anisotropic(half NoH, half3 h,
        half3 t, half3 b, half at, half ab)
{
    half ToH = dot(t, h);
    half BoH = dot(b, h);
    half a2 = at * ab;
    float3 v = float3(ab * ToH, at * BoH, a2 * NoH);
    float v2 = dot(v, v);
    float w2 = a2 / v2;
    return a2 * w2 * w2 * (1.0 / PI);
}

half D_Charlie(half roughness, half NoH)
{
	// Estevez and Kulla 2017, "Production Friendly Microfacet Sheen BRDF"
    half invAlpha = 1.0 / roughness;
    half cos2h = NoH * NoH;
    half sin2h = max(1.0 - cos2h, 0.0078125); // 2^(-14/2), so sin2h^2 > 0 in fp16
    return saturateMediump((2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * PI));
}

half V_SmithGGXCorrelated(half roughness, half NoV, half NoL)
{
    half a2 = roughness * roughness;
    half GGXV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    half GGXL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / (GGXV + GGXL);
}

float V_SmithGGXCorrelatedFast(half roughness, half NoV, half NoL)
{
    // This approximation is mathematically wrong but saves two square root operations and is good enough for real-time mobile applications
    float a = roughness;
    float GGXV = NoL * (NoV * (1.0 - a) + a);
    float GGXL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / (GGXV + GGXL);
}

half V_SmithGGXCorrelated_Anisotropic(half at, half ab, half ToV, half BoV,
	half ToL, half BoL, half NoV, half NoL)
{
	// Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
	// TODO: lambdaV can be pre-computed for all the lights, it should be moved out of this function
    half lambdaV = NoL * length(half3(at * ToV, ab * BoV, NoV));
    half lambdaL = NoV * length(half3(at * ToL, ab * BoL, NoL));
    half v = 0.5 / (lambdaV + lambdaL);
    return saturateMediump(v);
}

half V_Kelemen(half LoH)
{
	// Kelemen 2001, "A Microfacet Based Coupled Specular-Matte BRDF Model with Importance Sampling"
    return saturateMediump(0.25 / (LoH * LoH));
}

half V_Neubelt(half NoV, half NoL)
{
	// Neubelt and Pettineo 2013, "Crafting a Next-gen Material Pipeline for The Order: 1886"
    return saturateMediump(1.0 / (4.0 * (NoL + NoV - NoL * NoV)));
}

half3 F_Schlick(const half3 f0, half VoH)
{
	// Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"
    //half f90 = saturate(50.0 * dot(f0, 0.33)); // reflectance at grazing angle
    //return f0 + (f90 - f0) * pow(1.0 - VoH, 5);
    
    float f = pow(1.0 - VoH, 5.0); // simplified version using f90 = 1
    return f + f0 * (1.0 - f);
}

float F_Schlick(float u, float f0, float f90) // originally f0 has each color channels..
{
    return f0 + (f90 - f0) * pow(1.0 - u, 5.0);
}

float Fd_Burley(float NoV, float NoL, float LoH, float roughness)
{
    // Disney Diffuse BRDF
    float f90 = 0.5 + 2.0 * roughness * LoH * LoH;
    float lightScatter = F_Schlick(NoL, 1.0, f90);
    float viewScatter = F_Schlick(NoV, 1.0, f90);
    return lightScatter * viewScatter * (1.0 / PI);
}

float Fd_Lambert()
{
    return 1.0 / PI;
}

half iorToF0(half transmittedIor, half incidentIor)
{
    half val = (transmittedIor - incidentIor) / (transmittedIor + incidentIor);
    return val * val;
}

half f0ToIor(half f0)
{
    half r = sqrt(f0);
    return (1.0 + r) / (1.0 - r);
}

float3 ImportanceSampleGGX(float2 xi, float perceptual_roughness, float3 n)
{
    // Importance sample the GGX normal distribution function (NDF) to generate a half vector h
    float a = perceptual_roughness * perceptual_roughness;
    float phi = 2.0 * PI * xi.x;
    float cos_theta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sin_theta = sqrt(1.0 - cos_theta * cos_theta);

    float3 h = float3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);
    float3 up = abs(n.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, n));
    float3 bitangent = cross(n, tangent);
    return tangent * h.x + bitangent * h.y + n * h.z;
}

float3 EnvBRDF(int brdf_lut_descriptor, float3 f0, float perceptual_roughness, float nov)
{
    if (brdf_lut_descriptor < 0)
    {
        return f0;
    }
    float2 brdf = bindless_textures[DescriptorIndex(brdf_lut_descriptor)].SampleLevel(sampler_linear_clamp, float2(nov, perceptual_roughness), 0).rg;
    return f0 * brdf.x + brdf.y;
}

#endif