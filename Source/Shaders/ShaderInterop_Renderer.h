#ifndef WON_SHADERINTEROP_RENDERER_H
#define WON_SHADERINTEROP_RENDERER_H

#include "ShaderInterop.h"
#include "ShaderInterop_BVH.h"

#define DDGI_VISIBILITY_RESOLUTION 16
#define DDGI_IRRADIANCE_RESOLUTION DDGI_VISIBILITY_RESOLUTION // Works even with a lower resolution than visibility, but kept the same for calculation convenience

enum SHADER_OBJECT_FLAGS
{
    SHADER_OBJECT_FLAG_NONE = 0,
    SHADER_OBJECT_FLAG_CAST_SHADOW = 1 << 0,
    SHADER_OBJECT_FLAG_VISIBLE = 1 << 1,
};

enum SHADER_GEOMETRY_FLAGS
{
    SHADER_GEOMETRY_FLAG_NONE = 0,
    SHADER_GEOMETRY_FLAG_SKINNED = 1 << 0,
};

enum SHADER_MATERIAL_FLAGS
{
    SHADER_MATERIAL_FLAG_NONE = 0,
    SHADER_MATERIAL_FLAG_DOUBLE_SIDED = 1 << 0,
    SHADER_MATERIAL_FLAG_USE_VERTEX_COLORS = 1 << 2,
    SHADER_MATERIAL_FLAG_RECEIVE_SHADOW = 1 << 3,
};

enum SHADER_CAMERA_FLAGS
{
    SHADER_CAMERA_FLAG_NONE = 0,
    SHADER_CAMERA_FLAG_IS_ORTHOGRAPHIC = 1 << 0,
};

enum SHADER_SKY_TYPE
{
    SHADER_SKY_TYPE_NONE = 0,        // no sky dome (e.g. indoor); the sky pass is skipped
    SHADER_SKY_TYPE_PROCEDURAL = 1,
    SHADER_SKY_TYPE_CUBEMAP = 2,
    SHADER_SKY_TYPE_PHYSICALLY_BASED = 3,
};

enum SHADER_MATERIAL_TYPE
{
    SHADER_MATERIAL_TYPE_UNLIT,
    SHADER_MATERIAL_TYPE_PBR,

    SHADER_MATERIAL_TYPE_COUNT
};

enum SHADER_LIGHT_TYPE
{
    SHADER_LIGHT_TYPE_DIRECTIONAL,
    SHADER_LIGHT_TYPE_POINT,
    SHADER_LIGHT_TYPE_SPOT,
    SHADER_LIGHT_TYPE_RECT,

    SHADER_LIGHT_TYPE_COUNT
};

enum SHADER_LIGHT_FLAGS
{
    LIGHT_FLAG_LIGHT_STATIC = 1 << 0,
    LIGHT_FLAG_LIGHT_CASTING_SHADOW = 1 << 1,
    LIGHT_FLAG_TWO_SIDED = 1 << 2,
};

enum SHADER_DDGI_FLAGS
{
    SHADER_DDGI_FLAG_NONE = 0,
    SHADER_DDGI_FLAG_ACTIVE = 1 << 0,
};

enum SHADER_DIFFUSE_GI_MODE
{
    SHADER_DIFFUSE_GI_MODE_NONE,
    SHADER_DIFFUSE_GI_MODE_AMBIENT,
    SHADER_DIFFUSE_GI_MODE_DDGI,
    SHADER_DIFFUSE_GI_MODE_CUBEMAP,
    SHADER_DIFFUSE_GI_MODE_SKY,
};

enum SHADER_REFLECTION_MODE
{
    SHADER_REFLECTION_MODE_NONE,
    SHADER_REFLECTION_MODE_CUBEMAP,
    SHADER_REFLECTION_MODE_SKY,
};

enum TEXTURESLOT
{
    BASECOLORMAP,
    NORMALMAP,
    EMISSIVEMAP,
    OPACITYMAP,
    DISPLACEMENTMAP,
    OCCLUSIONMAP,
    SHEENCOLORMAP,
    SHEENROUGHNESSMAP,
    CLEARCOATMAP,
    CLEARCOATROUGHNESSMAP,
    CLEARCOATNORMALMAP,
    ANISOTROPYMAP,
    ROUGHNESSMAP,
    METALLICMAP,

    TEXTURESLOT_COUNT
};

struct alignas(16) ShaderTextureSlot
{
    int texture_descriptor;
    int sampler_descriptor;
    uint uvset;
    uint padding0;

#ifdef __cplusplus
    inline void Init()
    {
        texture_descriptor = -1;
        sampler_descriptor = -1;
    }
#else
    inline bool IsValid()
    {
        return texture_descriptor >= 0;
    }
    Texture2D<half4> GetTexture()
    {
        return bindless_textures_half4[texture_descriptor];
    }
    half4 Sample(in SamplerState sam, in float2 uv)
    {
        Texture2D<half4> tex = GetTexture();
        return tex.Sample(sam, uv);
    }

    half4 SampleLevel(in SamplerState sam, in float2 uv, in float lod)
    {
        Texture2D<half4> tex = GetTexture();
        return tex.SampleLevel(sam, uv, lod);
    }

#endif

};

struct alignas(16) ShaderGeometry // per submesh
{
    // Vertex pulling path: stream descriptors are resolved from bindless SRVs, not IA bindings.
    int position_buffer_descriptor;
    int color_buffer_descriptor;
    int normal_buffer_descriptor;
    int texcoord_buffer_descriptor;

    int tangent_buffer_descriptor;
    int index_buffer_descriptor;
    int bone_indices_buffer_descriptor;
    int bone_weights_buffer_descriptor;

    uint first_index;
    uint index_count;
    uint flags;
    uint padding;

    float3 bounds_min;
    uint padding0;
    float3 bounds_max;
    uint padding1;

#ifdef __cplusplus
    inline void Init()
    {
        position_buffer_descriptor = -1;
        color_buffer_descriptor = -1;
        normal_buffer_descriptor = -1;
        texcoord_buffer_descriptor = -1;
        tangent_buffer_descriptor = -1;
        index_buffer_descriptor = -1;
        bone_indices_buffer_descriptor = -1;
        bone_weights_buffer_descriptor = -1;
        first_index = 0;
        index_count = 0;
        flags = SHADER_GEOMETRY_FLAG_NONE;
        padding = 0;
        padding0 = 0;
        padding1 = 0;
    }
#endif
};

struct alignas(16) ShaderMaterial
{
    uint2 base_color;
    uint2 emissive_color_metallic;

    uint2 roughness_reflectance_refraction_padding;
    uint2 anisotropy_sheenroughness_clearcoat_clearcoatroughness;

    uint2 sheencolor_alphacutoff;
    uint flags; // see SHADER_MATERIAL_FLAGS
    uint padding;
    
    ShaderTextureSlot textures[TEXTURESLOT_COUNT];

#ifdef __cplusplus
    inline void Init()
    {
        for (size_t i = 0; i < TEXTURESLOT_COUNT; i++)
        {
            textures[i].Init();
        }
    }
#else
    inline half4 GetBaseColor() { return UnpackHalf4(base_color); }
    inline half3 GetEmissiveColor() { return UnpackHalf4(emissive_color_metallic).xyz; }
    inline half GetMetallic() { return UnpackHalf4(emissive_color_metallic).w; }
    inline half GetRoughness() { return UnpackHalf4(roughness_reflectance_refraction_padding).x; }
    inline half GetReflectance() { return UnpackHalf4(roughness_reflectance_refraction_padding).y; }
    inline half GetRefraction() { return UnpackHalf4(roughness_reflectance_refraction_padding).z; }
    inline half GetAnisotropy() { return UnpackHalf4(anisotropy_sheenroughness_clearcoat_clearcoatroughness).x; }
    inline half GetSheenRoughness() { return UnpackHalf4(anisotropy_sheenroughness_clearcoat_clearcoatroughness).y; }
    inline half GetClearCoat() { return UnpackHalf4(anisotropy_sheenroughness_clearcoat_clearcoatroughness).z; }
    inline half GetClearCoatRoughness() { return UnpackHalf4(anisotropy_sheenroughness_clearcoat_clearcoatroughness).w; }
    inline half3 GetSheenColor() { return UnpackHalf4(sheencolor_alphacutoff).xyz; }
    inline half GetAlphaCutoff() { return UnpackHalf4(sheencolor_alphacutoff).w; }

    inline bool IsDoubleSided() { return flags & SHADER_MATERIAL_FLAG_DOUBLE_SIDED; }
    inline bool IsUsingVertexColors() { return flags & SHADER_MATERIAL_FLAG_USE_VERTEX_COLORS; }
    inline bool IsReceiveShadow() { return flags & SHADER_MATERIAL_FLAG_RECEIVE_SHADOW; }
#endif
};

struct alignas(16) ShaderDecal
{
    float4x4 inv_world;

    uint instance_index;
    uint material_index;
    uint2 decal_padding;

#ifdef __cplusplus
    inline void Init()
    {
        inv_world = won::math::IDENTITY_MATRIX;
        instance_index = 0;
        material_index = 0;
        decal_padding = { 0, 0 };
    }
#endif
};

static const uint DEBUG_VIEW_MODE_NONE = 0;
static const uint DEBUG_VIEW_MODE_UNLIT = 1;
static const uint DEBUG_VIEW_MODE_BASE_COLOR = 2;
static const uint DEBUG_VIEW_MODE_WORLD_NORMAL = 3;
static const uint DEBUG_VIEW_MODE_ROUGHNESS = 4;
static const uint DEBUG_VIEW_MODE_METALLIC = 5;
static const uint DEBUG_VIEW_MODE_LIGHT_COMPLEXITY = 6;
static const uint DEBUG_VIEW_MODE_SHADOW_CASCADES = 7;
static const uint DEBUG_VIEW_MODE_WIREFRAME = 8;
static const uint DEBUG_VIEW_MODE_OVERDRAW = 9;

struct alignas(16) ShaderScene
{
    int instancebuffer;
    int geometrybuffer;
    int materialbuffer;
    int lightbuffer;

    int bvh_node_buffer;
    int bvh_instance_buffer;
    uint bvh_node_count;
    uint bvh_instance_count;

    int bone_matrix_buffer;
    uint directional_count;
    uint light_count;
    int ltc_matrix_lut;

    int ltc_fresnel_lut;
    int _scene_padding0;
    int _scene_padding1;
    int _scene_padding2;
#ifdef __cplusplus
    inline void Init()
    {
        instancebuffer = -1;
        geometrybuffer = -1;
        materialbuffer = -1;
        lightbuffer = -1;

        bvh_node_buffer = -1;
        bvh_instance_buffer = -1;
        bvh_node_count = 0;
        bvh_instance_count = 0;

        bone_matrix_buffer = -1;
        directional_count = 0;
        light_count = 0;
        ltc_matrix_lut = -1;

        ltc_fresnel_lut = -1;
        _scene_padding0 = 0;
        _scene_padding1 = 0;
        _scene_padding2 = 0;
    }
#endif
};

// Unified global environment (procedural sky + sun + ambient/GI)
struct alignas(16) ShaderEnvironment
{
    float3 sun_direction;
    uint sky_type;                  // SHADER_SKY_TYPE_*; NONE means the sky pass is skipped

    float4 sun_color_sun_intensity;
    float4 sun_params;              // x angular_radius, y glow_intensity, z glow_falloff, w unused

    float4 sky_horizon_color_sky_intensity;
    float4 sky_zenith_color_sky_horizon_falloff;
    float4 ground_horizon_color_ground_intensity;
    float4 ground_color_ground_falloff;

    float4 ambient_color_ambient_intensity;

    float2 indirect_diffuse_specular_scale;
    uint diffuse_gi_mode;
    uint reflection_mode;

    int sky_cubemap;
    int irradiance_cubemap;
    int specular_cubemap;
    float specular_mip_count;

    int brdf_lut;
    float turbidity;
    float mie_eccentricity;
    float rayleigh_coefficient;

    float mie_coefficient;
    uint derived_sun_index;
    float cloud_coverage;
    float cloud_density;

    float4 cloud_color_cloud_frequency;     // xyz color, w frequency
    float4 cloud_direction_cloud_speed;     // xy scroll direction, z speed, w unused

#ifdef __cplusplus
    inline void Init()
    {
        sun_direction = { 0,0,0 };
        sky_type = SHADER_SKY_TYPE_NONE;

        sun_color_sun_intensity = { 0,0,0,0 };
        sun_params = { 0,0,0,0 };

        sky_horizon_color_sky_intensity = { 0,0,0,0 };
        sky_zenith_color_sky_horizon_falloff = { 0,0,0,0 };
        ground_horizon_color_ground_intensity = { 0,0,0,0 };
        ground_color_ground_falloff = { 0,0,0,0 };

        ambient_color_ambient_intensity = { 0,0,0,0 };
        indirect_diffuse_specular_scale = { 0,0 };
        diffuse_gi_mode = SHADER_DIFFUSE_GI_MODE_NONE;
        reflection_mode = SHADER_REFLECTION_MODE_NONE;
        sky_cubemap = -1;
        irradiance_cubemap = -1;
        specular_cubemap = -1;
        specular_mip_count = 0.0f;
        brdf_lut = -1;
        turbidity = 0.0f;
        mie_eccentricity = 0.0f;
        rayleigh_coefficient = 0.0f;
        mie_coefficient = 0.0f;
        derived_sun_index = ~0u;
        cloud_coverage = 0.0f;
        cloud_density = 0.0f;
        cloud_color_cloud_frequency = { 0,0,0,0 };
        cloud_direction_cloud_speed = { 0,0,0,0 };
    }

    inline void SetSunDirection(const float3& value)
    {
        sun_direction = value;
    }

    inline void SetSunColorIntensity(const float3& color, float intensity)
    {
        sun_color_sun_intensity = float4(color.x, color.y, color.z, intensity);
    }

    inline void SetSkyHorizonColorIntensity(const float3& color, float intensity)
    {
        sky_horizon_color_sky_intensity = float4(color.x, color.y, color.z, intensity);
    }

    inline void SetSkyZenithColorFalloff(const float3& color, float falloff)
    {
        sky_zenith_color_sky_horizon_falloff = float4(color.x, color.y, color.z, falloff);
    }

    inline void SetGroundHorizonColorIntensity(const float3& color, float intensity)
    {
        ground_horizon_color_ground_intensity = float4(color.x, color.y, color.z, intensity);
    }

    inline void SetGroundColorFalloff(const float3& color, float falloff)
    {
        ground_color_ground_falloff = float4(color.x, color.y, color.z, falloff);
    }

    inline void SetSunParams(float angular_radius, float glow_intensity, float glow_falloff)
    {
        sun_params = float4(angular_radius, glow_intensity, glow_falloff, 0);
    }

    inline void SetAmbientColorIntensity(const float3& color, float intensity)
    {
        ambient_color_ambient_intensity = float4(color.x, color.y, color.z, intensity);
    }

    inline void SetIndirectScale(float diffuse_scale, float specular_scale)
    {
        indirect_diffuse_specular_scale = float2(diffuse_scale, specular_scale);
    }

    inline void SetAtmosphere(float turbidity_in, float mie_eccentricity_in, float rayleigh_coefficient_in, float mie_coefficient_in)
    {
        turbidity = turbidity_in;
        mie_eccentricity = mie_eccentricity_in;
        rayleigh_coefficient = rayleigh_coefficient_in;
        mie_coefficient = mie_coefficient_in;
    }

    inline void SetCloud(float coverage, float density, const float3& color, float frequency, const float2& direction, float speed)
    {
        cloud_coverage = coverage;
        cloud_density = density;
        cloud_color_cloud_frequency = float4(color.x, color.y, color.z, frequency);
        cloud_direction_cloud_speed = float4(direction.x, direction.y, speed, 0);
    }

#else
    inline uint GetSkyType() { return sky_type; }
    inline uint GetDiffuseGIMode() { return diffuse_gi_mode; }
    inline uint GetReflectionMode() { return reflection_mode; }
    inline bool HasSkyCubemap() { return sky_cubemap >= 0; }
    inline bool HasIrradianceCubemap() { return irradiance_cubemap >= 0; }
    inline bool HasSpecularCubemap() { return specular_cubemap >= 0; }
    inline bool HasBRDFLUT() { return brdf_lut >= 0; }
    inline float3 GetSunDirection() { return normalize(sun_direction); }
    inline float3 GetSunColor() { return sun_color_sun_intensity.xyz; }
    inline float GetSunIntensity() { return sun_color_sun_intensity.w; }
    inline float3 GetSkyHorizonColor() { return sky_horizon_color_sky_intensity.xyz; }
    inline float GetSkyIntensity() { return sky_horizon_color_sky_intensity.w; }
    inline float3 GetSkyZenithColor() { return sky_zenith_color_sky_horizon_falloff.xyz; }
    inline float GetSkyHorizonFalloff() { return sky_zenith_color_sky_horizon_falloff.w; }
    inline float3 GetGroundHorizonColor() { return ground_horizon_color_ground_intensity.xyz; }
    inline float GetGroundIntensity() { return ground_horizon_color_ground_intensity.w; }
    inline float3 GetGroundColor() { return ground_color_ground_falloff.xyz; }
    inline float GetGroundFalloff() { return ground_color_ground_falloff.w; }
    inline float GetSunAngularRadius() { return sun_params.x; }
    inline float GetTurbidity() { return turbidity; }
    inline float GetMieEccentricity() { return mie_eccentricity; }
    inline float GetRayleighCoefficient() { return rayleigh_coefficient; }
    inline float GetMieCoefficient() { return mie_coefficient; }
    inline uint GetDerivedSunIndex() { return derived_sun_index; }
    inline float GetSunGlowIntensity() { return sun_params.y; }
    inline float GetSunGlowFalloff() { return sun_params.z; }
    inline float3 GetAmbientColor() { return ambient_color_ambient_intensity.xyz; }
    inline float GetAmbientIntensity() { return ambient_color_ambient_intensity.w; }
    inline float GetIndirectDiffuseScale() { return indirect_diffuse_specular_scale.x; }
    inline float GetIndirectSpecularScale() { return indirect_diffuse_specular_scale.y; }
    inline float GetCloudCoverage() { return cloud_coverage; }
    inline float GetCloudDensity() { return cloud_density; }
    inline float3 GetCloudColor() { return cloud_color_cloud_frequency.xyz; }
    inline float GetCloudFrequency() { return cloud_color_cloud_frequency.w; }
    inline float2 GetCloudDirection() { return cloud_direction_cloud_speed.xy; }
    inline float GetCloudSpeed() { return cloud_direction_cloud_speed.z; }
    inline bool HasCloud() { return cloud_coverage > 0.0f; }
#endif
};

struct alignas(16) ShaderDDGIVolume
{
    uint flags;
    int irradiance_texture;
    int irradiance_texture_uav;
    int visibility_texture;

    int visibility_texture_uav;
    int probe_data_buffer;
    int probe_data_buffer_uav;
    int previous_irradiance_texture;

    int previous_visibility_texture;
    int previous_probe_data_buffer;
    uint history_valid;
    float hysteresis;

    uint probe_update_start;
    uint total_probe_count;
    uint probes_per_frame;
    uint probe_update_dispatch_width;

    uint3 probe_counts;
    float normal_bias;

    float3 volume_min;
    float view_bias;

    float3 probe_spacing;
    float max_distance;

#ifdef __cplusplus
    inline void Init()
    {
        flags = SHADER_DDGI_FLAG_NONE;
        irradiance_texture = -1;
        irradiance_texture_uav = -1;
        visibility_texture = -1;
        visibility_texture_uav = -1;
        probe_data_buffer = -1;
        probe_data_buffer_uav = -1;
        previous_irradiance_texture = -1;
        previous_visibility_texture = -1;
        previous_probe_data_buffer = -1;
        history_valid = 0;
        hysteresis = 0.0f;
        probe_update_start = 0;
        total_probe_count = 1;
        probes_per_frame = 1;
        probe_update_dispatch_width = 1;
        probe_counts = { 1, 1, 1 };
        normal_bias = 0.0f;
        volume_min = { 0.0f, 0.0f, 0.0f };
        view_bias = 0.0f;
        probe_spacing = { 1.0f, 1.0f, 1.0f };
        max_distance = 0.0f;
    }
#else
    inline bool IsActive() { return (flags & SHADER_DDGI_FLAG_ACTIVE) != 0; }
    inline bool HasIrradianceTexture() { return irradiance_texture >= 0; }
    inline bool HasVisibilityTexture() { return visibility_texture >= 0; }
    inline bool HasProbeDataBuffer() { return probe_data_buffer >= 0; }
    inline bool HasHistory() { return history_valid != 0 && previous_irradiance_texture >= 0 && previous_visibility_texture >= 0 && previous_probe_data_buffer >= 0; }
#endif
};

enum SHADER_REFLECTION_PROBE_FLAGS
{
    SHADER_REFLECTION_PROBE_FLAG_NONE = 0,
    SHADER_REFLECTION_PROBE_FLAG_ACTIVE = 1 << 0,
};

struct alignas(16) ShaderReflectionProbe
{
    uint flags;
    float intensity;
    float influence_radius;
    int cubemap_texture;

    float3 position;
    float cubemap_mip_count;

#ifdef __cplusplus
    inline void Init()
    {
        flags = SHADER_REFLECTION_PROBE_FLAG_NONE;
        intensity = 1.0f;
        influence_radius = 0.0f;
        cubemap_texture = -1;
        position = { 0.0f, 0.0f, 0.0f };
        cubemap_mip_count = 0.0f;
    }
#else
    inline bool IsActive() { return (flags & SHADER_REFLECTION_PROBE_FLAG_ACTIVE) != 0; }
    inline bool HasCubemap() { return cubemap_texture >= 0; }
#endif
};

static const uint water_ripple_max_injections = 64;

struct ShaderWaterRipple
{
    float3 position;
    float strength;

#ifdef __cplusplus
    inline void Init()
    {
        position = { 0.0f, 0.0f, 0.0f };
        strength = 0.0f;
    }
#endif
};

enum SHADER_WATER_FLAGS
{
    SHADER_WATER_FLAG_NONE = 0,
    SHADER_WATER_FLAG_ACTIVE = 1 << 0,
    SHADER_WATER_FLAG_RECEIVE_SHADOW = 1 << 1,
};

struct alignas(16) ShaderWaterBody
{
    uint flags;
    float roughness;
    float reflectance;
    float refraction_strength;

    float3 absorption_coefficient;
    float wave_frequency;

    float3 scattering_coefficient;
    float normal_strength;

	float3 plane_origin; // center of the water body in world space
    float ripple_strength;

    float3 axis_x;
    float half_extent_x;

    float3 axis_z;
    float half_extent_z;

    float2 wave_velocity_primary;
    float2 wave_velocity_secondary;

#ifdef __cplusplus
    inline void Init()
    {
        flags = SHADER_WATER_FLAG_NONE;
        roughness = 1.0f;
        reflectance = 0.354f;
        refraction_strength = 0.0f;
        absorption_coefficient = { 0.0f, 0.0f, 0.0f };
        wave_frequency = 0.0f;
        scattering_coefficient = { 0.0f, 0.0f, 0.0f };
        normal_strength = 0.0f;
        plane_origin = { 0.0f, 0.0f, 0.0f };
        ripple_strength = 0.0f;
        axis_x = { 0.0f, 0.0f, 0.0f };
        half_extent_x = 0.0f;
        axis_z = { 0.0f, 0.0f, 0.0f };
        half_extent_z = 0.0f;
        wave_velocity_primary = { 0.0f, 0.0f };
        wave_velocity_secondary = { 0.0f, 0.0f };
    }
#else
    inline bool IsActive() { return (flags & SHADER_WATER_FLAG_ACTIVE) != 0; }
    inline bool IsReceiveShadow() { return (flags & SHADER_WATER_FLAG_RECEIVE_SHADOW) != 0; }
#endif
};

struct alignas(16) ShaderWaterTile
{
	float2 center; // center of the tile in world space (x,z)
	float half_size; // half size of the tile in world space (x,z)
    uint coarser_neighbor_mask;

#ifdef __cplusplus
    inline void Init()
    {
        center = { 0.0f, 0.0f };
        half_size = 0.0f;
        coarser_neighbor_mask = 0;
    }
#endif
};

struct alignas(16) ShaderWaterZone
{
	float2 origin; // left-bottom corner of the zone in world space (x,z)
	float2 extent; // full extent of the zone in world space (x,z)

    uint first_body;
    uint body_count;
    uint info_resolution;
	uint tile_resolution; // each tile is tessellated into (tile_resolution x tile_resolution) quads

    uint lod_levels;
    float lod_distance_scale;
    int ripple_texture;
    int wetness_texture;

    uint ripple_resolution;
    float ripple_speed;
    float ripple_radius;
    uint _zone_padding0;

#ifdef __cplusplus
    inline void Init()
    {
        origin = { 0.0f, 0.0f };
        extent = { 0.0f, 0.0f };
        first_body = 0;
        body_count = 0;
        info_resolution = 0;
        tile_resolution = 0;
        lod_levels = 0;
        lod_distance_scale = 0.0f;
        ripple_texture = -1;
        wetness_texture = -1;
        ripple_resolution = 0;
        ripple_speed = 0.0f;
        ripple_radius = 0.0f;
        _zone_padding0 = 0;
    }
#else
    inline bool HasRipple() { return ripple_texture >= 0; }
    inline bool HasWetness() { return wetness_texture >= 0; }
#endif
};

struct alignas(16) ShaderFrame
{
    ShaderScene scene;
    ShaderEnvironment environment;
    ShaderDDGIVolume ddgi_volume;
    ShaderReflectionProbe reflection_probe;

	float time; // accumulated time in seconds
    float _frame_padding0;
    float _frame_padding1;
    float _frame_padding2;

#ifdef __cplusplus
    inline void Init()
    {
        scene.Init();
        environment.Init();
        ddgi_volume.Init();
        reflection_probe.Init();
        time = 0.0f;
    }
#endif
};

struct alignas(16) ShaderCamera
{
    float3		position;
    uint		output_index; // viewport or rendertarget array index

    float3		forward;
    float		z_near;

    float3		up;
    float		z_far;

    uint2 internal_resolution;
    float2 internal_resolution_rcp;

    float4x4 view;
    float4x4 projection;
    float4x4 view_projection;
    float4x4 inv_view_projection;

    float exposure;
    float _camera_padding0;
    uint2 viewport_offset;

#ifdef __cplusplus
    inline void Init()
    {
        view = won::math::IDENTITY_MATRIX;
        projection = won::math::IDENTITY_MATRIX;
        view_projection = won::math::IDENTITY_MATRIX;
        inv_view_projection = won::math::IDENTITY_MATRIX;
        exposure = 1.0f;
        _camera_padding0 = 0.0f;
        viewport_offset = { 0, 0 };

    }
#endif
};

struct alignas(16) ShaderView
{
    ShaderCamera camera;

    int instance_sort_buffer;
    int forward_light_index_buffer;
    uint forward_light_count;
    uint debug_view_mode;

    int shadow_atlas;
    int shadow_cascade_buffer;
    int light_shadow_slice_buffer;
    int cluster_light_count_buffer;

    int cluster_light_index_buffer;
    int cluster_light_offset_buffer;
    uint2 cluster_count;

    uint cluster_depth_slices;
    uint _view_padding0;
    uint _view_padding1;
    uint _view_padding2;
#ifdef __cplusplus
    inline void Init()
    {
        camera.Init();

        instance_sort_buffer = -1;
        forward_light_index_buffer = -1;
        forward_light_count = 0;
        debug_view_mode = DEBUG_VIEW_MODE_NONE;

        shadow_atlas = -1;
        shadow_cascade_buffer = -1;
        light_shadow_slice_buffer = -1;
        cluster_light_count_buffer = -1;

        cluster_light_index_buffer = -1;
        cluster_light_offset_buffer = -1;
        cluster_count = { 0, 0 };

        cluster_depth_slices = 1;
        _view_padding0 = 0;
        _view_padding1 = 0;
        _view_padding2 = 0;
    }
#endif
};

struct ShaderLightIterator
{
    uint value;

#ifdef __cplusplus
    ShaderLightIterator(uint offset, uint count)
    {
        value = offset | (count << 16u);
    }
    constexpr operator uint() const { return value; }
#endif // __cplusplus

    inline uint item_offset()
    {
        return value & 0xFFFF;
    }
    inline uint item_count()
    {
        return value >> 16u;
    }
    inline bool empty()
    {
        return item_count() == 0;
    }
    inline uint first_item()
    {
        return item_offset();
    }
    inline uint last_item() // includes last valid item
    {
        return empty() ? 0 : (item_offset() + item_count() - 1);
    }
    inline uint end_item() // excludes last valid item
    {
        return empty() ? 0 : (item_offset() + item_count());
    }
    inline uint first_bucket()
    {
        return first_item() / 32u;
    }
    inline uint last_bucket()
    {
        return last_item() / 32u;
    }
};

struct alignas(16) ShaderLight
{
	float3 position;
    uint type8_flags8_range16;

    float4 color; // can't use half4 because light intensity can be greater than 65504.h

    uint2 direction_outer_cone_angle_cos; // direction 3 cos_outer_cone 1
    uint inner_cone_angle_cos_padding; // cos_inner_cone 1 padding 1
    uint shadow_slice16_count16; // offset [0:15] count [16:31]

    uint2 right_half_extent_x; // right 3 half + half_extent.x 1 half (Rect light only)
    uint half_extent_y_padding16; // half_extent.y 1 half + padding 1 half (Rect light only)
    uint padding0;
#ifdef __cplusplus
    inline void Init()
    {
        position = { 0,0,0 };
        type8_flags8_range16 = 0;

        color = { 0,0,0,0 };
        direction_outer_cone_angle_cos = { 0,0 };
        inner_cone_angle_cos_padding = 0;
        shadow_slice16_count16 = 0;

        right_half_extent_x = { 0,0 };
        half_extent_y_padding16 = 0;
        padding0 = 0;
    }
    inline void SetType(uint type)
    {
        type8_flags8_range16 |= type & 0xFF;
    }
    inline void SetFlags(uint flags)
    {
        type8_flags8_range16 |= (flags & 0xFF) << 8u;
    }
    inline void SetRange(float value)
    {
        type8_flags8_range16 |= XMConvertFloatToHalf(value) << 16u;
    }
    inline void SetDirection(float3 value)
    {
        direction_outer_cone_angle_cos.x |= XMConvertFloatToHalf(value.x);
        direction_outer_cone_angle_cos.x |= XMConvertFloatToHalf(value.y) << 16u;
        direction_outer_cone_angle_cos.y |= XMConvertFloatToHalf(value.z);
    }
    inline void SetOuterConeAngleCos(float value)
    {
        direction_outer_cone_angle_cos.y |= XMConvertFloatToHalf(value) << 16u;
    }
    inline void SetColor(float4 value)
    {
        color = value;
    }
    inline void SetInnerConeAngleCos(float value)
    {
        inner_cone_angle_cos_padding |= XMConvertFloatToHalf(value);
    }
    inline void SetShadowSliceOffset(uint value)
    {
        shadow_slice16_count16 |= value & 0xFFFFu;
    }
    inline void SetShadowSliceCount(uint value)
    {
        shadow_slice16_count16 |= (value & 0xFFFFu) << 16u;
    }
    inline void SetRight(float3 value)
    {
        right_half_extent_x.x |= XMConvertFloatToHalf(value.x);
        right_half_extent_x.x |= XMConvertFloatToHalf(value.y) << 16u;
        right_half_extent_x.y |= XMConvertFloatToHalf(value.z);
    }
    inline void SetHalfExtents(float2 value)
    {
        right_half_extent_x.y |= XMConvertFloatToHalf(value.x) << 16u;
        half_extent_y_padding16 |= XMConvertFloatToHalf(value.y);
    }
#else
    inline min16uint GetType()
    {
        return type8_flags8_range16 & 0xFF;
    }
    inline min16uint GetFlags()
    {
        return (type8_flags8_range16 >> 8u) & 0xFF;
    }
    inline half GetRange()
    {
        return (half)f16tof32(type8_flags8_range16 >> 16u);
    }
    inline half3 GetDirection()
    {
        return normalize(half3(
            (half)f16tof32(direction_outer_cone_angle_cos.x),
            (half)f16tof32(direction_outer_cone_angle_cos.x >> 16u),
            (half)f16tof32(direction_outer_cone_angle_cos.y)
        ));
    }
    inline half GetOuterConeAngleCos()
    {
        return (half)f16tof32(direction_outer_cone_angle_cos.y >> 16u);
    }
    inline float4 GetColor()
    {
        return color;
    }
    inline half GetInnerConeAngleCos()
    {
        return (half)f16tof32(inner_cone_angle_cos_padding);
    }
    inline bool IsCastingShadow()
    {
        return GetFlags() & LIGHT_FLAG_LIGHT_CASTING_SHADOW;
    }
    inline bool IsStaticLight()
    {
        return GetFlags() & LIGHT_FLAG_LIGHT_STATIC;
    }
    inline uint GetShadowSliceOffset()
    {
        return shadow_slice16_count16 & 0xFFFFu;
    }
    inline uint GetShadowSliceCount()
    {
        return (shadow_slice16_count16 >> 16u) & 0xFFFFu;
    }
    inline bool HasShadowSlices()
    {
        return GetShadowSliceCount() > 0;
    }
    inline bool IsTwoSided()
    {
        return GetFlags() & LIGHT_FLAG_TWO_SIDED;
    }
    inline half3 GetRight()
    {
        return normalize(half3(
            (half)f16tof32(right_half_extent_x.x),
            (half)f16tof32(right_half_extent_x.x >> 16u),
            (half)f16tof32(right_half_extent_x.y)
        ));
    }
    inline half2 GetHalfExtents()
    {
        return half2(
            (half)f16tof32(right_half_extent_x.y >> 16u),
            (half)f16tof32(half_extent_y_padding16)
        );
    }
#endif // __cplusplus
};

struct alignas(16) ShaderShadowCascade
{
    float4x4 shadow_view_projection;
    float4 shadow_atlas_scale_bias;

    float split_far;
    float blend_band;
    float texel_world_size;
    float padding;

#ifdef __cplusplus
    inline void Init()
    {
        shadow_view_projection = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
        shadow_atlas_scale_bias = { 0,0,0,0 };

        split_far = 0.0f;
        blend_band = 0.0f;
        texel_world_size = 0.0f;
    }
#endif
};

struct ObjectPushConstants
{
    uint draw_offset;
    uint geometry_index;
    uint material_index;
    uint padding0;

#ifdef __cplusplus
    inline void Init()
    {
        draw_offset = 0;
        geometry_index = 0;
        material_index = 0;
    }
#endif
};

struct alignas(16) ShaderInstance // per transform
{
    float4x4 world_transform;

    float3 normal_transform_row0;
    uint bone_count;
    float3 normal_transform_row1;
    uint bone_matrix_offset;
    float3 normal_transform_row2;
    uint instance_padding;

#ifdef __cplusplus
    inline void Init()
    {
        bone_count = 0;
        bone_matrix_offset = 0;
        instance_padding = 0;
    }
#endif
};

CONSTANTBUFFER(g_frame, ShaderFrame, CBSLOT_RENDERER_FRAME);
CONSTANTBUFFER(g_view, ShaderView, CBSLOT_RENDERER_CAMERA);

#ifndef WON_DISABLE_RENDERER_PUSHCONSTANT
PUSHCONSTANT(push, ObjectPushConstants);
#endif

//CBUFFER(ForwardLightMaskCB, CBSLOT_RENDERER_FORWARD_LIGHTMASK)
//{
//    uint4 forward_light_mask;	// supports indexing 128 lights
//};

#ifdef __cplusplus
static_assert(sizeof(ShaderTextureSlot) == 16, "ShaderTextureSlot layout mismatch");
static_assert(sizeof(ShaderGeometry) == 80, "ShaderGeometry layout mismatch");
static_assert(sizeof(ShaderMaterial) == 272, "ShaderMaterial layout mismatch");
static_assert(sizeof(ShaderScene) == 64, "ShaderScene layout mismatch");
static_assert(sizeof(ShaderEnvironment) == 224, "ShaderEnvironment layout mismatch");
static_assert(sizeof(ShaderDDGIVolume) == 112, "ShaderDDGIVolume layout mismatch");
static_assert(sizeof(ShaderReflectionProbe) == 32, "ShaderReflectionProbe layout mismatch");
static_assert(sizeof(ShaderWaterRipple) == 16, "ShaderWaterRipple layout mismatch");
static_assert(sizeof(ShaderWaterBody) == 112, "ShaderWaterBody layout mismatch");
static_assert(sizeof(ShaderWaterZone) == 64, "ShaderWaterZone layout mismatch");
static_assert(sizeof(ShaderWaterTile) == 16, "ShaderWaterTile layout mismatch");
static_assert(sizeof(ShaderFrame) == 448, "ShaderFrame layout mismatch");
static_assert(sizeof(ShaderCamera) == 336, "ShaderCamera layout mismatch");
static_assert(sizeof(ShaderView) == 400, "ShaderView layout mismatch");
static_assert(sizeof(ShaderLight) == 64, "ShaderLight layout mismatch");
static_assert(sizeof(ShaderShadowCascade) == 96, "ShaderShadowCascade layout mismatch");
static_assert(sizeof(ObjectPushConstants) == 16, "ObjectPushConstants layout mismatch");
static_assert(sizeof(ShaderInstance) == 112, "ShaderInstance layout mismatch");
#endif // __cplusplus

#endif // WON_SHADERINTEROP_RENDERER_H
