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

static const float3 physical_sky_rayleigh_beta = float3(5.5e-6f, 13.0e-6f, 22.4e-6f);
static const float physical_sky_mie_beta = 0.8e-6f;
static const float physical_sky_rayleigh_zenith_length = 8.4e3f; // 8.4 km, Three.js Preetham-derived SkyShader.
static const float physical_sky_mie_zenith_length = 1.25e3f; // 1.25 km, Three.js Preetham-derived SkyShader.
static const float physical_sky_radiance_scale = 0.0005f;
static const float physical_sky_sun_energy_scale = 1000.0f; // 1000.0f is an empirical value to scale the sun energy to a reasonable range for rendering
static const float physical_sky_reference_sun_illuminance = 100000.0f; // 100000 lux for upper energy scale 1000
static const float physical_sky_cutoff_angle = PI / 1.95f;
static const float physical_sky_sun_steepness = 1.5f;

inline float HenyeyGreensteinPhase(float cos_theta, float g)
{
    float g2 = g * g;
    float denom = max(1.0f + g2 - 2.0f * g * cos_theta, 0.0001f);
    return (1.0f - g2) / (4.0f * PI * pow(denom, 1.5f));
}

inline float3 EvaluatePhysicallyBasedSky(ShaderEnvironment sky, float3 direction)
{
    const float3 up = float3(0.0f, 1.0f, 0.0f);
    float3 sun_direction = normalize(sky.GetSunDirection());
    float turbidity = max(sky.GetTurbidity(), 1.0f);

    float sun_zenith_cos = clamp(dot(up, sun_direction), -1.0f, 1.0f); // cos of the sun zenith angle
    float sun_illuminance_scale = max(sky.GetSunIntensity(), 0.0f) / physical_sky_reference_sun_illuminance;
    float sun_energy = physical_sky_sun_energy_scale * sun_illuminance_scale * max(0.0f, 1.0f - exp(-((physical_sky_cutoff_angle - acos(sun_zenith_cos)) / physical_sky_sun_steepness)));
    float sun_fade = 1.0f - saturate(1.0f - exp(sun_direction.y)); // fade out the sun when it is below the horizon, upper horizon : 1.0

    float rayleigh_scale = max(sky.GetRayleighCoefficient() - (1.0f - sun_fade), 0.0f);
    float3 beta_rayleigh = physical_sky_rayleigh_beta * rayleigh_scale; // proportional to sun fade
    float3 beta_mie = physical_sky_mie_beta * turbidity * max(sky.GetMieCoefficient(), 0.0f); // proportional to turbidity

    float zenith_cos = max(dot(up, direction), 0.0f); // cos of the zenith angle of the view direction
    float zenith_degrees = degrees(acos(zenith_cos));
    float optical_mass = 1.0f / (zenith_cos + 0.15f * pow(max(93.885f - zenith_degrees, 0.0001f), -1.253f)); // relative air mass(Kasten and Young 1989) vertical : 1.0, horizontal: 38.0
    float3 extinction = exp(-(beta_rayleigh * physical_sky_rayleigh_zenith_length + beta_mie * physical_sky_mie_zenith_length) * optical_mass); // Beer-Lambert law, proportion of remaining light after scattering and absorption

    float cos_theta = dot(direction, sun_direction);
    float rayleigh_phase = (3.0f / (16.0f * PI)) * (1.0f + cos_theta * cos_theta); // Rayleigh phase function
    float mie_phase = HenyeyGreensteinPhase(cos_theta, clamp(sky.GetMieEccentricity(), -0.99f, 0.99f)); // Mie phase function
    float3 scatter_ratio = (beta_rayleigh * rayleigh_phase + beta_mie * mie_phase) / max(beta_rayleigh + beta_mie, 1e-9f); // proportion of light scattered into the view direction

    float3 in_scatter = pow(max(sun_energy * scatter_ratio * (1.0f - extinction), 0.0f), 1.5f); // pow 1.5 is empirical correction
    float horizon_weight = saturate(pow(max(1.0f - dot(up, sun_direction), 0.0f), 5.0f)); // 1.0 at horizon, 0.0 at zenith
    in_scatter *= lerp(float3(1.0f, 1.0f, 1.0f), pow(max(sun_energy * scatter_ratio * extinction, 0.0f), 0.5f), horizon_weight); // empirical correction to reduce the brightness of the sun at the horizon

    float sun_cos = cos(max(sky.GetSunAngularRadius(), 0.0001f));
    float sun_disk = smoothstep(sun_cos, lerp(sun_cos, 1.0f, 0.5f), cos_theta);

    float3 color = in_scatter * physical_sky_radiance_scale * sky.GetSkyIntensity();
    color += sky.GetSunColor() * sun_disk * sky.GetSunIntensity() * extinction;

    float3 ground_color = sky.GetGroundColor() * sky.GetGroundIntensity();
    float horizon_blend = smoothstep(-0.02f, 0.02f, direction.y);
    color = lerp(ground_color, color, horizon_blend);

    return min(color, MEDIUMP_FLT_MAX);
}

#endif
