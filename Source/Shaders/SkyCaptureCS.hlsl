#include "Common.hlsli"
#include "SkyCommon.hlsli"
#define WON_SKY_CAPTURE_PUSHCONSTANT
#include "ShaderInterop_PostProcess.h"

[numthreads(DISPATCH_THREAD_GROUP_2D, DISPATCH_THREAD_GROUP_2D, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    const uint resolution = skycapturepush.face_resolution;
    if (dispatch_thread_id.x >= resolution || dispatch_thread_id.y >= resolution)
    {
        return;
    }

    ShaderEnvironment sky = GetEnvironment();
    const float2 uv = (float2(dispatch_thread_id.xy) + 0.5) / float(resolution);
    const uint face = skycapturepush.face_offset + dispatch_thread_id.z;
    const float3 direction = CubeFaceDirection(face, uv);

    float3 color;
    if (sky.GetSkyType() == SHADER_SKY_TYPE_CUBEMAP && sky.HasSkyCubemap())
    {
        color = bindless_cubemaps[DescriptorIndex(sky.sky_cubemap)].SampleLevel(sampler_linear_clamp, direction, 0).rgb;
    }
    else if (sky.GetSkyType() == SHADER_SKY_TYPE_PHYSICALLY_BASED)
    {
        color = EvaluatePhysicalSkyAtmosphere(sky, direction); // without sun disk
    }
    else
    {
        color = EvaluateProceduralSky(sky, direction);
    }

    bindless_rwtextures2DArray[DescriptorIndex((int)skycapturepush.output_descriptor)][uint3(dispatch_thread_id.xy, face)] = float4(min(color, MEDIUMP_FLT_MAX), 1.0);
}
