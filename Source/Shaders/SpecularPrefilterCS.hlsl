#include "Common.hlsli"
#include "BRDFCommon.hlsli"
#define WON_SKY_PREFILTER_PUSHCONSTANT
#include "ShaderInterop_PostProcess.h"

static const uint specular_prefilter_sample_count = 64u;

[numthreads(DISPATCH_THREAD_GROUP_2D, DISPATCH_THREAD_GROUP_2D, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    const uint mip = skyprefilterpush.mip;
    const uint resolution = sky_specular_resolution >> mip;
    if (dispatch_thread_id.x >= resolution || dispatch_thread_id.y >= resolution)
    {
        return;
    }

    const float2 uv = (float2(dispatch_thread_id.xy) + 0.5) / float(resolution);
    const uint face = dispatch_thread_id.z;
    const float3 normal = CubeFaceDirection(face, uv);
    TextureCube source = bindless_cubemaps[DescriptorIndex((int)skyprefilterpush.source_cubemap)];

    const float perceptual_roughness = float(mip) / float(sky_specular_mip_count - 1u);
    float3 prefiltered;
    if (perceptual_roughness <= 0.0)
    {
        prefiltered = source.SampleLevel(sampler_linear_clamp, normal, 0).rgb;
    }
    else
    {
        float3 sum = float3(0.0, 0.0, 0.0);
        float weight = 0.0;
        for (uint i = 0u; i < specular_prefilter_sample_count; ++i)
        {
            float2 xi = Hammersley(i, specular_prefilter_sample_count);
            float3 h = ImportanceSampleGGX(xi, perceptual_roughness, normal);
            float3 l = 2.0 * dot(normal, h) * h - normal;

            float nol = dot(normal, l);
            if (nol > 0.0)
            {
                sum += source.SampleLevel(sampler_linear_clamp, l, float(mip) * 0.5).rgb * nol;
                weight += nol;
            }
        }
        prefiltered = weight > 0.0 ? sum / weight : source.SampleLevel(sampler_linear_clamp, normal, 0).rgb;
    }

    bindless_rwtextures2DArray[DescriptorIndex((int)skyprefilterpush.output_descriptor)][uint3(dispatch_thread_id.xy, face)] = float4(min(prefiltered, MEDIUMP_FLT_MAX), 1.0);
}
