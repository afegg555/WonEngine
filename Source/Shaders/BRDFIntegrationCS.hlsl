#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#include "BRDFCommon.hlsli"
#define WON_BRDF_INTEGRATION_PUSHCONSTANT
#include "ShaderInterop_PostProcess.h"

// Bakes the second term of the split-sum IBL approximation (Karis, UE4) into a 2D LUT:
//   integral(env * BRDF) ~= prefiltered_env(roughness) * EnvBRDF(f0, roughness, NoV)

static const uint brdf_sample_count = 1024u;

float2 IntegrateBRDF(float nov, float perceptual_roughness)
{
    // we assume BRDF is isotropic, so only the angle between n and v matters: pin n to +z and place
    // v in the xz plane with v.z == NoV
    float3 v = float3(sqrt(1.0 - nov * nov), 0.0, nov);
    float3 n = float3(0.0, 0.0, 1.0);
    float alpha = perceptual_roughness * perceptual_roughness;

    // Why the result fits in two channels: with the Schlick Fresnel approximation
    //   F = f0 + (1 - f0) * (1 - VoH)^5
    // f0 factors out of the integral:
    //   F = f0 * (1 - (1 - VoH)^5) + 1 * (1 - VoH)^5
    //       \--- scales with f0 ---/   \-- independent of f0 --/
    // so the integral becomes f0 * A + B. A (scale) and B (bias) depend only on (NoV, roughness)
    float a = 0.0; // A part
    float b = 0.0; // B part
    for (uint i = 0u; i < brdf_sample_count; ++i)
    {
        // we use importance sampling because the integrand is very spiky for low roughness values, and uniform sampling would
        // require a lot more samples to converge to a good result.
        float2 xi = Hammersley(i, brdf_sample_count);
        float3 h = ImportanceSampleGGX(xi, perceptual_roughness, n); // importance sample the half vector h
        float3 l = 2.0 * dot(v, h) * h - v; // reflect v about h to get the light vector l

        float nol = saturate(l.z);
        float noh = saturate(h.z);
        float voh = saturate(dot(v, h));
        // l below the surface contributes nothing; noh is a denominator below.
        if (nol > 0.0 && noh > 0.0)
        {
            float vis = V_SmithGGXCorrelatedPrecise(alpha, nov, nol); // V_SmithGGXCorrelated causes error
            float g_vis = 4.0 * vis * nol * voh / noh;

            // Split Fresnel into its two Schlick parts and accumulate them separately.
            float fc = pow(1.0 - voh, 5.0);
            a += (1.0 - fc) * g_vis; // scale: multiplied by f0 at runtime
            b += fc * g_vis;         // bias: added at runtime, independent of f0
        }
    }
    return float2(a, b) / float(brdf_sample_count);
}

[numthreads(DISPATCH_THREAD_GROUP_2D, DISPATCH_THREAD_GROUP_2D, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (dispatch_thread_id.x >= brdf_lut_resolution || dispatch_thread_id.y >= brdf_lut_resolution)
    {
        return;
    }
    // x axis is NoV, y axis is perceptual roughness; sample at texel centers so the runtime
    // bilinear lookup lands on the values that were integrated here.
    float nov = (float(dispatch_thread_id.x) + 0.5) / float(brdf_lut_resolution);
    float perceptual_roughness = (float(dispatch_thread_id.y) + 0.5) / float(brdf_lut_resolution);

    float2 brdf = IntegrateBRDF(nov, perceptual_roughness);
    bindless_rwtextures[DescriptorIndex((int)brdfintegrationpush.output_descriptor)][dispatch_thread_id.xy] = float4(brdf, 0.0, 0.0);
}
