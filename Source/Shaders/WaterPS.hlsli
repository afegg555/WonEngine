#ifndef WATER_PS
#define WATER_PS
#include "WaterCommon.hlsli"
#include "ShadingCommon.hlsli"
#include "NoiseCommon.hlsli"

static const float water_detail_min_gradient_step = 0.35f;
static const uint water_detail_octaves = 3;
static const float water_detail_lacunarity = 2.0f;
static const float water_detail_persistence = 0.5f;
static const float water_detail_secondary_frequency = 2.13f; // off a whole multiple so the two layers never line up
static const float water_detail_primary_weight = 0.6f;

static const float water_refraction_fade_distance = 0.5f;
static const float water_normal_flatten_scale = 200.0f;
static const float water_distance_roughness_gain = 0.05f;

static const float water_min_perceptual_roughness = 0.045f;
static const float water_reflectance_to_f0 = 0.16f;
static const float water_fresnel_exponent = 5.0f;

inline float WaterHeightField(float2 plane_position, in ShaderWaterBody water, float time)
{
    const float2 primary = plane_position * water.detail_frequency + water.detail_velocity_primary * time;
    const float2 secondary = plane_position * water.detail_frequency * water_detail_secondary_frequency + water.detail_velocity_secondary * time;
    return FBMValueNoise(primary, water_detail_octaves, water_detail_lacunarity, water_detail_persistence) * water_detail_primary_weight
         + FBMValueNoise(secondary, water_detail_octaves, water_detail_lacunarity, water_detail_persistence) * (1.0f - water_detail_primary_weight);
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

    const float scene_depth = bindless_textures[DescriptorIndex(watercb.depth_descriptor)].Load(int3((int2)input.position.xy, 0)).r;
    const float2 screen_uv = input.position.xy * camera.internal_resolution_rcp;
    const float3 scene_world = ScreenUVToWorld(screen_uv, scene_depth);

    const float3 extinction_coefficient = water.absorption_coefficient + water.scattering_coefficient;
    const float3 single_scattering_albedo = water.scattering_coefficient / max(extinction_coefficient, water_epsilon);

    const float3 plane_normal = WaterPlaneNormal(water);
    const float3 tangent_x = water.axis_x;
    const float3 tangent_z = water.axis_z;

    const float2 unit_coord = WaterPlaneUnitCoord(water, input.world_position);
    const float2 plane_position = float2(unit_coord.x * water.half_extent_x, unit_coord.y * water.half_extent_z);

    const float time = GetTime();
    const float2 plane_footprint = max(abs(ddx(plane_position)), abs(ddy(plane_position)));
    const float gradient_step = max(length(plane_footprint), water_detail_min_gradient_step);
    const float height_center = WaterHeightField(plane_position, water, time);
    const float height_x = WaterHeightField(plane_position + float2(gradient_step, 0.0f), water, time);
    const float height_z = WaterHeightField(plane_position + float2(0.0f, gradient_step), water, time);
    float2 gradient = float2(height_x - height_center, height_z - height_center) / gradient_step;

    if (zone.HasRipple())
    {
        Texture2D ripple_texture = bindless_textures[DescriptorIndex(zone.ripple_texture)];
        const float texel_uv = 1.0f / (float)zone.ripple_resolution;
        const float ripple_left = ripple_texture.SampleLevel(sampler_linear_clamp, zone_uv - float2(texel_uv, 0.0f), 0).r;
        const float ripple_right = ripple_texture.SampleLevel(sampler_linear_clamp, zone_uv + float2(texel_uv, 0.0f), 0).r;
        const float ripple_down = ripple_texture.SampleLevel(sampler_linear_clamp, zone_uv - float2(0.0f, texel_uv), 0).r;
        const float ripple_up = ripple_texture.SampleLevel(sampler_linear_clamp, zone_uv + float2(0.0f, texel_uv), 0).r;
        const float2 ripple_gradient = float2(ripple_right - ripple_left, ripple_up - ripple_down) / (2.0f * WaterRippleTexelSize(zone.extent, zone.ripple_resolution));

        gradient += water.ripple_strength * float2(
            dot(float3(ripple_gradient.x, 0.0f, ripple_gradient.y), tangent_x),
            dot(float3(ripple_gradient.x, 0.0f, ripple_gradient.y), tangent_z));
    }

    const float camera_distance = distance(camera.position, input.world_position);
    const WaterSurfaceSample wave_surface = EvaluateWaterBodySurface(water, zone.wave_time, input.wave_coord, camera_distance);
    const float2 wave_gradient = float2(
        dot(float3(wave_surface.gradient.x, 0.0f, wave_surface.gradient.y), tangent_x),
        dot(float3(wave_surface.gradient.x, 0.0f, wave_surface.gradient.y), tangent_z));

    Surface surface;
    surface.Init();
    surface.P = input.world_position;
    surface.V = normalize(camera.position - input.world_position);
    const float footprint = length(plane_footprint);
    const float normal_sharpness = 1.0f / (1.0f + footprint * footprint * water_normal_flatten_scale);
    const float2 total_gradient = gradient * water.detail_strength + wave_gradient;
    const float3 wave_normal = normalize(plane_normal - tangent_x * total_gradient.x - tangent_z * total_gradient.y);
    surface.N = normalize(lerp(plane_normal, wave_normal, normal_sharpness));
    surface.NoV = saturate(abs(dot(surface.N, surface.V)) + FLT_EPSILON);
    surface.albedo = (half3)single_scattering_albedo;
    surface.receive_shadow = water.IsReceiveShadow();

    const float distance_roughness = (1.0f - normal_sharpness) * water_distance_roughness_gain;
    const half perceptual_roughness = (half)clamp(water.roughness + distance_roughness, water_min_perceptual_roughness, 1.0f);
    surface.roughness = perceptual_roughness * perceptual_roughness;
    const float f0 = water_reflectance_to_f0 * water.reflectance * water.reflectance;
    surface.f0 = (half3)f0;

    Lighting lighting;
    lighting.Create(0, 0, 0, 0);

    EvaluateIndirectLighting(surface, lighting);
    EvaluateDirectLighting(surface, lighting, input.position.xy);

    const half3 diffuse = (lighting.direct.diffuse + lighting.indirect.diffuse) * Fd_Lambert();
    const half3 specular = lighting.direct.specular + lighting.indirect.specular;

    const float f0_sqrt = sqrt(f0);
    const float water_ior = (1.0f + f0_sqrt) / max(1.0f - f0_sqrt, water_epsilon);
    const float scene_depth_below_plane = dot(water.plane_origin - scene_world, plane_normal);

    const float3 plane_normal_view = mul((float3x3)camera.view, plane_normal);
    const float3 normal_view = mul((float3x3)camera.view, surface.N);
    const float2 distortion = (plane_normal_view.xy - normal_view.xy) * float2(1.0f, -1.0f)
        * ((water_ior - 1.0f) * water.refraction_strength
           * saturate(scene_depth_below_plane / water_refraction_fade_distance));
    const float2 distorted_uv = clamp(screen_uv + distortion, 0.0f, 1.0f);

    const float sample_depth = bindless_textures[DescriptorIndex(watercb.depth_descriptor)].SampleLevel(sampler_point_clamp, distorted_uv, 0).r;
    const float3 sample_world = ScreenUVToWorld(distorted_uv, sample_depth);
    const float2 sample_info = bindless_textures[DescriptorIndex(waterpush.info_texture_descriptor)].SampleLevel(sampler_point_clamp, WaterZoneUV(zone, sample_world), 0).rg;

    const float is_water = sample_info.g >= 0.0f ? 1.0f : 0.0f;
    const float under_surface = saturate((sample_info.r - sample_world.y) / water_refraction_fade_distance);
    const float2 background_uv = lerp(screen_uv, distorted_uv, is_water * under_surface);

    const float3 background = bindless_textures[DescriptorIndex(watercb.scene_color_descriptor)].SampleLevel(sampler_linear_clamp, background_uv, 0).rgb;

    const float background_depth = bindless_textures[DescriptorIndex(watercb.depth_descriptor)].SampleLevel(sampler_point_clamp, background_uv, 0).r;
    const float3 background_world = ScreenUVToWorld(background_uv, background_depth);
    const float2 background_info = bindless_textures[DescriptorIndex(waterpush.info_texture_descriptor)].SampleLevel(sampler_point_clamp, WaterZoneUV(zone, background_world), 0).rg;

    const float out_path = distance(background_world, input.world_position);
    const float in_path = background_info.g >= 0.0f ? max(0.0f, background_info.r - background_world.y) : 0.0f;
    const float3 transmittance = EvaluateVolumeTransmittance(extinction_coefficient, out_path + in_path);

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
