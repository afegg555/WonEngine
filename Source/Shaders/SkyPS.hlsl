#include "Common.hlsli"
#include "SkyCommon.hlsli"

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float4 main(VertexOutput input) : SV_Target
{
    ShaderEnvironment sky = GetEnvironment();
    ShaderCamera camera = GetCamera();

    float2 screen_uv = input.position.xy * camera.internal_resolution_rcp;
    float3 ray_direction = normalize(UnprojectRay(ScreenUVToNDC(screen_uv)) - camera.position);
    float3 color;
    if (sky.GetSkyType() == SHADER_SKY_TYPE_CUBEMAP && sky.HasSkyCubemap())
    {
        color = bindless_cubemaps[DescriptorIndex(sky.sky_cubemap)].SampleLevel(sampler_linear_clamp, ray_direction, 0).rgb;
    }
    else if (sky.GetSkyType() == SHADER_SKY_TYPE_PHYSICALLY_BASED)
    {
        color = EvaluatePhysicallyBasedSky(sky, ray_direction);
    }
    else
    {
        color = EvaluateProceduralSky(sky, ray_direction);
    }

    color = CompositeCloudLayer(sky, ray_direction, color, GetTime());

    color *= camera.exposure;
    color = saturateMediump(color);

    return float4(color, 1.0f);
}
