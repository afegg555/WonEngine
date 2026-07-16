#ifndef SKY_COMMON
#define SKY_COMMON

#include "Common.hlsli"

inline float3 EvaluateProceduralSky(ShaderEnvironment sky, float3 direction)
{
    float vertical_factor = direction.y;
    float sky_t = pow(saturate(max(vertical_factor, 0.0f)), max(sky.GetSkyHorizonFalloff(), 0.0001f));
    float ground_t = pow(saturate(max(-vertical_factor, 0.0f)), max(sky.GetGroundFalloff(), 0.0001f));
    float3 sky_color = lerp(sky.GetSkyHorizonColor(), sky.GetSkyZenithColor(), sky_t) * sky.GetSkyIntensity();
    float3 ground_color = lerp(sky.GetGroundHorizonColor(), sky.GetGroundColor(), ground_t) * sky.GetGroundIntensity();
    float horizon_blend = smoothstep(-0.02f, 0.02f, vertical_factor);
    float3 color = lerp(ground_color, sky_color, horizon_blend);

    float3 sun_direction = normalize(sky.GetSunDirection());
    float sun_dot = saturate(dot(direction, sun_direction));
    float sun_disk = smoothstep(cos(sky.GetSunAngularRadius()), cos(sky.GetSunAngularRadius() * 0.5f), sun_dot);
    float sun_glow = pow(sun_dot, max(sky.GetSunGlowFalloff(), 1.0f)) * sky.GetSunGlowIntensity();
    color += sky.GetSunColor() * (sun_disk * sky.GetSunIntensity() + sun_glow * sky.GetSunIntensity());

    return color;
}

#endif
