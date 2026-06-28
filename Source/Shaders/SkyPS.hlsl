#include "Common.hlsli"

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
    float2 ndc = float2(screen_uv.x * 2.0f - 1.0f, 1.0f - screen_uv.y * 2.0f);
    float4 far_clip = float4(ndc, 0.0f, 1.0f);
    float4 far_world = mul(camera.inv_view_projection, far_clip);
    far_world.xyz /= max(far_world.w, 0.000001f);

    float3 ray_direction = normalize(far_world.xyz - camera.position);
    float vertical_factor = ray_direction.y;

    float sky_t = pow(saturate(max(vertical_factor, 0.0f)), max(sky.GetSkyHorizonFalloff(), 0.0001f));
    float ground_t = pow(saturate(max(-vertical_factor, 0.0f)), max(sky.GetGroundFalloff(), 0.0001f));
    float3 sky_color = lerp(sky.GetSkyHorizonColor(), sky.GetSkyZenithColor(), sky_t) * sky.GetSkyIntensity();
    float3 ground_color = lerp(sky.GetGroundHorizonColor(), sky.GetGroundColor(), ground_t) * sky.GetGroundIntensity();
    float horizon_blend = smoothstep(-0.02f, 0.02f, vertical_factor);
    float3 color = lerp(ground_color, sky_color, horizon_blend);

    float3 sun_direction = normalize(sky.GetSunDirection());
    float sun_dot = saturate(dot(ray_direction, sun_direction));
    float sun_disk = smoothstep(cos(sky.GetSunAngularRadius()), cos(sky.GetSunAngularRadius() * 0.5f), sun_dot);
    float sun_glow = pow(sun_dot, max(sky.GetSunGlowFalloff(), 1.0f)) * sky.GetSunGlowIntensity();
    color += sky.GetSunColor() * (sun_disk * sky.GetSunIntensity() + sun_glow * sky.GetSunIntensity());

    color *= camera.exposure;
    
    return float4(color, 1.0f);
}
