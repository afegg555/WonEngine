#ifndef WATER_PS
#define WATER_PS
#include "WaterCommon.hlsli"
#include "ShadingCommon.hlsli"
#include "NoiseCommon.hlsli"

static const float water_wave_min_gradient_step = 0.35f; // lower bound for the wave normal finite difference, in world meters
static const uint water_wave_octaves = 3;
static const float water_wave_lacunarity = 2.0f;
static const float water_wave_persistence = 0.5f;
static const float water_wave_secondary_frequency = 2.13f; // off a whole multiple so the two layers never line up
static const float water_wave_primary_weight = 0.6f;

static const float water_min_perceptual_roughness = 0.045f;
static const float water_reflectance_to_f0 = 0.16f;
static const float water_fresnel_exponent = 5.0f;

inline float WaterHeightField(float2 plane_position, in ShaderWaterBody water, float time)
{
    const float2 primary = plane_position * water.wave_frequency + water.wave_velocity_primary * time;
    const float2 secondary = plane_position * water.wave_frequency * water_wave_secondary_frequency + water.wave_velocity_secondary * time;
    return FBMValueNoise(primary, water_wave_octaves, water_wave_lacunarity, water_wave_persistence) * water_wave_primary_weight
         + FBMValueNoise(secondary, water_wave_octaves, water_wave_lacunarity, water_wave_persistence) * (1.0f - water_wave_primary_weight);
}

float4 main(VertexOutput input) : SV_Target0
{
    const ShaderWaterZone zone = GetWaterZone();
    const float2 zone_uv = WaterZoneUV(zone, input.world_position);
    const float body_index = bindless_textures[DescriptorIndex(waterpush.info_texture_descriptor)].SampleLevel(sampler_point_clamp, zone_uv, 0).g;
    if (body_index < 0.0f)
    {
        discard;
    }

    ShaderWaterBody water = GetWaterBody((uint)body_index);
    ShaderCamera camera = GetCamera();

    const float scene_depth = bindless_textures[DescriptorIndex(waterpush.depth_descriptor)].Load(int3((int2)input.position.xy, 0)).r;
    const float2 screen_uv = input.position.xy * camera.internal_resolution_rcp;
    const float2 ndc = float2(screen_uv.x * 2.0f - 1.0f, 1.0f - screen_uv.y * 2.0f);
    float4 scene_world = mul(camera.inv_view_projection, float4(ndc, scene_depth, 1.0f));
    scene_world.xyz /= scene_world.w;

    const float water_thickness = distance(scene_world.xyz, input.world_position);
    const float3 extinction_coefficient = water.absorption_coefficient + water.scattering_coefficient;
    const float3 transmittance = EvaluateVolumeTransmittance(extinction_coefficient, water_thickness); // higher extinction : lower transmittance
    const float3 single_scattering_albedo = water.scattering_coefficient / max(extinction_coefficient, water_epsilon);

    const float3 plane_normal = WaterPlaneNormal(water);
    const float3 tangent_x = water.axis_x;
    const float3 tangent_z = water.axis_z;

    const float2 unit_coord = WaterPlaneUnitCoord(water, input.world_position);
    const float2 plane_position = float2(unit_coord.x * water.half_extent_x, unit_coord.y * water.half_extent_z);

    const float time = GetTime();
    const float2 plane_footprint = max(abs(ddx(plane_position)), abs(ddy(plane_position)));
    const float gradient_step = max(length(plane_footprint), water_wave_min_gradient_step);
    const float height_center = WaterHeightField(plane_position, water, time);
    const float height_x = WaterHeightField(plane_position + float2(gradient_step, 0.0f), water, time);
    const float height_z = WaterHeightField(plane_position + float2(0.0f, gradient_step), water, time);
    float2 gradient = float2(height_x - height_center, height_z - height_center) / gradient_step;

    ShaderWaterSimulation simulation = GetWaterSimulation();
    const float2 ripple_grid = WaterSimulationGrid(input.world_position);
    if (simulation.HasRipple() && WaterGridInWindow(simulation, (int2)floor(ripple_grid)))
    {
        Texture2D ripple_texture = bindless_textures[DescriptorIndex(simulation.ripple_texture)];
        const float ripple_left = WaterSampleRippleGrid(ripple_texture, ripple_grid - float2(1.0f, 0.0f));
        const float ripple_right = WaterSampleRippleGrid(ripple_texture, ripple_grid + float2(1.0f, 0.0f));
        const float ripple_down = WaterSampleRippleGrid(ripple_texture, ripple_grid - float2(0.0f, 1.0f));
        const float ripple_up = WaterSampleRippleGrid(ripple_texture, ripple_grid + float2(0.0f, 1.0f));
        const float2 ripple_gradient = float2(ripple_right - ripple_left, ripple_up - ripple_down) / (2.0f * WATER_SIMULATION_TEXEL_SIZE);

        gradient += water.ripple_strength * float2(
            dot(float3(ripple_gradient.x, 0.0f, ripple_gradient.y), tangent_x),
            dot(float3(ripple_gradient.x, 0.0f, ripple_gradient.y), tangent_z));
    }

    Surface surface;
    surface.Init();
    surface.P = input.world_position;
    surface.V = normalize(camera.position - input.world_position);
    surface.N = normalize(plane_normal - tangent_x * (gradient.x * water.normal_strength) - tangent_z * (gradient.y * water.normal_strength));
    surface.NoV = saturate(abs(dot(surface.N, surface.V)) + FLT_EPSILON);
    surface.albedo = (half3)single_scattering_albedo;
    surface.receive_shadow = water.IsReceiveShadow();

    const half perceptual_roughness = (half)clamp(water.roughness, water_min_perceptual_roughness, 1.0f);
    surface.roughness = perceptual_roughness * perceptual_roughness;
    const float f0 = water_reflectance_to_f0 * water.reflectance * water.reflectance;
    surface.f0 = (half3)f0;

    Lighting lighting;
    lighting.Create(0, 0, 0, 0);

    EvaluateIndirectLighting(surface, lighting);
    EvaluateDirectLighting(surface, lighting, input.position.xy);

    const half3 diffuse = (lighting.direct.diffuse + lighting.indirect.diffuse) * Fd_Lambert();
    const half3 specular = lighting.direct.specular + lighting.indirect.specular;

    // refraction disabled
    //const float3 surface_tangential = surface.N - plane_normal * dot(surface.N, plane_normal);
    //const float3 surface_tangential_view = mul(camera.view, float4(surface_tangential, 0.0f)).xyz;
    //const float2 refraction_driver = float2(surface_tangential_view.x, -surface_tangential_view.y);
    //
    //const float2 refraction_offset = refraction_driver * water.refraction_strength;
    //const float2 refracted_uv = saturate(screen_uv + refraction_offset);
    //const int2 refracted_texel = (int2)(refracted_uv * (float2)camera.internal_resolution);
    //const float refracted_scene_depth = bindless_textures[DescriptorIndex(waterpush.depth_descriptor)].Load(int3(refracted_texel, 0)).r;
    //const float2 refracted_ndc = float2(refracted_uv.x * 2.0f - 1.0f, 1.0f - refracted_uv.y * 2.0f);
    //float4 refracted_world = mul(camera.inv_view_projection, float4(refracted_ndc, refracted_scene_depth, 1.0f));
    //refracted_world.xyz /= refracted_world.w;
    //const float refracted_sample_height = dot(refracted_world.xyz - water.plane_origin, plane_normal);
    //const float2 background_uv = refracted_sample_height > 0.0f ? screen_uv : refracted_uv;
    const float2 background_uv = screen_uv;
    const float3 background = bindless_textures[DescriptorIndex(waterpush.scene_color_descriptor)].SampleLevel(sampler_linear_clamp, background_uv, 0).rgb;


    const float fresnel = f0 + (1.0f - f0) * pow(1.0f - surface.NoV, water_fresnel_exponent);
    const float3 in_scattered = (float3)(surface.albedo * diffuse) * (1.0f - transmittance);
    const float3 transmitted = background * transmittance;

    half4 final_color;
    final_color.rgb = (half3)((transmitted + in_scattered) * (1.0f - fresnel)) + specular;
    final_color.a = 1.0h;

    final_color.rgb = saturateMediump(final_color.rgb);
    return final_color;
}
#endif // WATER_PS
