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

    float2 screen_uv = (input.position.xy - float2(camera.viewport_offset)) * camera.internal_resolution_rcp;
    float2 ndc = float2(screen_uv.x * 2.0f - 1.0f, 1.0f - screen_uv.y * 2.0f);
    float4 far_clip = float4(ndc, 0.0f, 1.0f);
    float4 far_world = mul(camera.inv_view_projection, far_clip);
    far_world.xyz /= max(far_world.w, 0.000001f);

    float3 ray_direction = normalize(far_world.xyz - camera.position);
    float3 color;
    if (sky.GetSkyType() == SHADER_SKY_TYPE_CUBEMAP && sky.HasSkyCubemap())
    {
        color = bindless_cubemaps[DescriptorIndex(sky.sky_cubemap)].SampleLevel(sampler_linear_clamp, ray_direction, 0).rgb;
    }
    else
    {
        color = EvaluateProceduralSky(sky, ray_direction);
    }

    color *= camera.exposure;
    color = saturateMediump(color);

    return float4(color, 1.0f);
}
