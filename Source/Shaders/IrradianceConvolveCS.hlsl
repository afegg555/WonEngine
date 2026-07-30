#include "Common.hlsli"
#include "BRDFCommon.hlsli"
#define WON_SKY_CAPTURE_PUSHCONSTANT
#include "ShaderInterop_PostProcess.h"

static const uint irradiance_sample_count = 256u;

[numthreads(DISPATCH_THREAD_GROUP_2D, DISPATCH_THREAD_GROUP_2D, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    const uint resolution = skycapturepush.face_resolution;
    if (dispatch_thread_id.x >= resolution || dispatch_thread_id.y >= resolution)
    {
        return;
    }

    const float2 uv = (float2(dispatch_thread_id.xy) + 0.5) / float(resolution);
    const uint face = skycapturepush.face_offset + dispatch_thread_id.z;
    const float3 normal = CubeFaceDirection(face, uv);
    TextureCube source = bindless_cubemaps[DescriptorIndex((int)skycapturepush.source_cubemap)];

    float3 irradiance = float3(0.0, 0.0, 0.0);
    for (uint i = 0u; i < irradiance_sample_count; ++i)
    {
        float2 xi = Hammersley(i, irradiance_sample_count);
        float3 direction = ImportanceSampleCosine(xi, normal);
        irradiance += source.SampleLevel(sampler_linear_clamp, direction, 0).rgb;
    }
    irradiance /= float(irradiance_sample_count);

    bindless_rwtextures2DArray[DescriptorIndex((int)skycapturepush.output_descriptor)][uint3(dispatch_thread_id.xy, face)] = float4(min(irradiance, MEDIUMP_FLT_MAX), 1.0);
}
