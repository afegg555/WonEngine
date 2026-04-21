#ifndef WON_SHADERINTEROP_RENDERER_H
#define WON_SHADERINTEROP_RENDERER_H

#include "ShaderInterop.h"

enum SHADER_OBJECT_FLAGS
{
    SHADER_OBJECT_FLAG_NONE = 0,
    SHADER_OBJECT_FLAG_CAST_SHADOW = 1 << 0,
    SHADER_OBJECT_FLAG_VISIBLE = 1 << 1,
};

enum SHADER_GEOMETRY_FLAGS
{
    SHADER_GEOMETRY_FLAG_NONE = 0,
};

enum SHADER_MATERIAL_FLAGS
{
    SHADER_MATERIAL_FLAG_NONE = 0,
    SHADER_MATERIAL_FLAG_DOUBLE_SIDED = 1 << 0,
    SHADER_MATERIAL_FLAG_TRANSPARENT = 1 << 1,
    SHADER_MATERIAL_FLAG_USE_VERTEX_COLORS = 1 << 2,
    SHADER_MATERIAL_FLAG_RECEIVE_SHADOW = 1 << 3,
};

enum SHADER_CAMERA_FLAGS
{
    SHADER_CAMERA_FLAG_NONE = 0,
    SHADER_CAMERA_FLAG_IS_ORTHOGRAPHIC = 1 << 0,
};

enum SHADER_MATERIAL_TYPE
{
    SHADER_MATERIAL_TYPE_UNLIT,

    SHADER_MATERIAL_TYPE_COUNT
};

enum SHADER_LIGHT_TYPE
{
    SHADER_LIGHT_TYPE_DIRECTIONAL,
    SHADER_LIGHT_TYPE_POINT,
    SHADER_LIGHT_TYPE_SPOT,

    SHADER_LIGHT_TYPE_COUNT
};

enum SHADER_LIGHT_FLAGS
{
    LIGHT_FLAG_LIGHT_STATIC = 1 << 0,
    LIGHT_FLAG_LIGHT_CASTING_SHADOW = 1 << 1,
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

struct alignas(16) ShaderGeometry
{
    // Vertex pulling path: stream descriptors are resolved from bindless SRVs, not IA bindings.
    int position_buffer_descriptor;
    int color_buffer_descriptor;
    int normal_buffer_descriptor;
    int texcoord_buffer_descriptor;

    int tangent_buffer_descriptor;
    int index_buffer_descriptor;
    int2 padding;

    float3 bounds_min;
    uint index_count;
    float3 bounds_max;
    uint flags;

#ifdef __cplusplus
    inline void Init()
    {
        position_buffer_descriptor = -1;
        color_buffer_descriptor = -1;
        normal_buffer_descriptor = -1;
        texcoord_buffer_descriptor = -1;
        tangent_buffer_descriptor = -1;
        index_buffer_descriptor = -1;
    }
#endif
};

struct alignas(16) ShaderMaterial
{
    uint2 base_color;
    uint2 emissive_color_metallic;

    uint2 roughness_reflectance_refraction_padding;
    uint2 anisotropy_sheenroughness_clearcoat_clearcoatroughness;

    uint2 sheencolor_padding;
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
    inline half3 GetSheenColor() { return UnpackHalf4(sheencolor_padding).xyz; }

    inline bool IsDoubleSided() { return flags & SHADER_MATERIAL_FLAG_DOUBLE_SIDED; }
    inline bool IsTransparent() { return flags & SHADER_MATERIAL_FLAG_TRANSPARENT; }
    inline bool IsUsingVertexColors() { return flags & SHADER_MATERIAL_FLAG_USE_VERTEX_COLORS; }
    inline bool IsReceiveShadow() { return flags & SHADER_MATERIAL_FLAG_RECEIVE_SHADOW; }
#endif
};

struct alignas(16) ShaderScene
{
    int instancebuffer;
    int geometrybuffer;
    int materialbuffer;
    int lightbuffer;

    int shadow_atlas;
    int shadow_cascade_buffer;
    int2 padding;

    uint4 lights; // supports indexing 128 lights
#ifdef __cplusplus
    inline void Init()
    {
        instancebuffer = -1;
        geometrybuffer = -1;
        materialbuffer = -1;
        lightbuffer = -1;

        shadow_atlas = -1;
        shadow_cascade_buffer = -1;

        lights = { 0,0,0,0 };
    }
#endif
};

struct alignas(16) ShaderSky
{
    uint2 sun_direction_padding;
    uint flags;
    float padding;

    uint2 sun_color_sun_intensity;
    uint2 sky_horizon_color_sky_intensity;

    uint2 sky_zenith_color_sky_horizon_falloff;
    uint2 ground_horizon_color_ground_intensity;

    uint2 ground_color_ground_falloff;
    uint2 sun_params;

#ifdef __cplusplus
    inline void Init()
    {
        sun_direction_padding = { 0,0 };
        flags = 0;

        sun_color_sun_intensity = { 0,0 };
        sky_horizon_color_sky_intensity = { 0,0 };

        sky_zenith_color_sky_horizon_falloff = { 0,0 };
        ground_horizon_color_ground_intensity = { 0,0 };

        ground_color_ground_falloff = { 0,0 };
        sun_params = { 0,0 };
    }

    inline void SetSunDirection(const float3& value)
    {
        sun_direction_padding.x = XMConvertFloatToHalf(value.x) | (XMConvertFloatToHalf(value.y) << 16u);
        sun_direction_padding.y = XMConvertFloatToHalf(value.z);
    }

    inline void SetSunColorIntensity(const float3& color, float intensity)
    {
        sun_color_sun_intensity.x = XMConvertFloatToHalf(color.x) | (XMConvertFloatToHalf(color.y) << 16u);
        sun_color_sun_intensity.y = XMConvertFloatToHalf(color.z) | (XMConvertFloatToHalf(intensity) << 16u);
    }

    inline void SetSkyHorizonColorIntensity(const float3& color, float intensity)
    {
        sky_horizon_color_sky_intensity.x = XMConvertFloatToHalf(color.x) | (XMConvertFloatToHalf(color.y) << 16u);
        sky_horizon_color_sky_intensity.y = XMConvertFloatToHalf(color.z) | (XMConvertFloatToHalf(intensity) << 16u);
    }

    inline void SetSkyZenithColorFalloff(const float3& color, float falloff)
    {
        sky_zenith_color_sky_horizon_falloff.x = XMConvertFloatToHalf(color.x) | (XMConvertFloatToHalf(color.y) << 16u);
        sky_zenith_color_sky_horizon_falloff.y = XMConvertFloatToHalf(color.z) | (XMConvertFloatToHalf(falloff) << 16u);
    }

    inline void SetGroundHorizonColorIntensity(const float3& color, float intensity)
    {
        ground_horizon_color_ground_intensity.x = XMConvertFloatToHalf(color.x) | (XMConvertFloatToHalf(color.y) << 16u);
        ground_horizon_color_ground_intensity.y = XMConvertFloatToHalf(color.z) | (XMConvertFloatToHalf(intensity) << 16u);
    }

    inline void SetGroundColorFalloff(const float3& color, float falloff)
    {
        ground_color_ground_falloff.x = XMConvertFloatToHalf(color.x) | (XMConvertFloatToHalf(color.y) << 16u);
        ground_color_ground_falloff.y = XMConvertFloatToHalf(color.z) | (XMConvertFloatToHalf(falloff) << 16u);
    }

    inline void SetSunParams(float angular_radius, float glow_intensity, float glow_falloff)
    {
        sun_params.x = XMConvertFloatToHalf(angular_radius) | (XMConvertFloatToHalf(glow_intensity) << 16u);
        sun_params.y = XMConvertFloatToHalf(glow_falloff);
    }

#else
    inline half3 GetSunDirection() { return normalize(half3((half)f16tof32(sun_direction_padding.x), (half)f16tof32(sun_direction_padding.x >> 16u), (half)f16tof32(sun_direction_padding.y))); }
    inline half3 GetSunColor() { return UnpackHalf4(sun_color_sun_intensity).xyz; }
    inline half GetSunIntensity() { return UnpackHalf4(sun_color_sun_intensity).w; }
    inline half3 GetSkyHorizonColor() { return UnpackHalf4(sky_horizon_color_sky_intensity).xyz; }
    inline half GetSkyIntensity() { return UnpackHalf4(sky_horizon_color_sky_intensity).w; }
    inline half3 GetSkyZenithColor() { return UnpackHalf4(sky_zenith_color_sky_horizon_falloff).xyz; }
    inline half GetSkyHorizonFalloff() { return UnpackHalf4(sky_zenith_color_sky_horizon_falloff).w; }
    inline half3 GetGroundHorizonColor() { return UnpackHalf4(ground_horizon_color_ground_intensity).xyz; }
    inline half GetGroundIntensity() { return UnpackHalf4(ground_horizon_color_ground_intensity).w; }
    inline half3 GetGroundColor() { return UnpackHalf4(ground_color_ground_falloff).xyz; }
    inline half GetGroundFalloff() { return UnpackHalf4(ground_color_ground_falloff).w; }
    inline half GetSunAngularRadius() { return (half)f16tof32(sun_params.x); }
    inline half GetSunGlowIntensity() { return (half)f16tof32(sun_params.x >> 16u); }
    inline half GetSunGlowFalloff() { return (half)f16tof32(sun_params.y); }
#endif
};

struct alignas(16) ShaderFrame
{
    ShaderScene scene;
    ShaderSky sky;

#ifdef __cplusplus
    inline void Init()
    {
        scene.Init();
        sky.Init();
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

#ifdef __cplusplus
    inline void Init()
    {
        view = won::math::IDENTITY_MATRIX;
        projection = won::math::IDENTITY_MATRIX;
        view_projection = won::math::IDENTITY_MATRIX;
        inv_view_projection = won::math::IDENTITY_MATRIX;
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

    uint2 direction_outer_cone_angle_cos; // direction 3 cos_outer_cone 1
    uint2 color; // half4 packed

    uint inner_cone_angle_cos_padding; // cos_inner_cone 1 padding 1
    uint shadow_slice_offset;
    uint shadow_slice_count;
    uint padding;
#ifdef __cplusplus
    inline void Init()
    {
        position = { 0,0,0 };
        type8_flags8_range16 = 0;

        direction_outer_cone_angle_cos = { 0,0 };
        color = { 0,0 };
        inner_cone_angle_cos_padding = 0;
        shadow_slice_offset = 0;
        shadow_slice_count = 0;
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
        color.x |= XMConvertFloatToHalf(value.x);
        color.x |= XMConvertFloatToHalf(value.y) << 16u;
        color.y |= XMConvertFloatToHalf(value.z);
        color.y |= XMConvertFloatToHalf(value.w) << 16u;
    }
    inline void SetInnerConeAngleCos(float value)
    {
        inner_cone_angle_cos_padding |= XMConvertFloatToHalf(value);
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
    inline half4 GetColor()
    {
        half4 retVal;
        retVal.x = (half)f16tof32(color.x);
        retVal.y = (half)f16tof32(color.x >> 16u);
        retVal.z = (half)f16tof32(color.y);
        retVal.w = (half)f16tof32(color.y >> 16u);
        return retVal;
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
    inline bool HasShadowSlices()
    {
        return shadow_slice_count > 0;
    }
#endif // __cplusplus
};

struct alignas(16) ShaderShadowCascade
{
    float4x4 shadow_view_projection;
    float4 shadow_atlas_scale_bias;

    float split_far;
    float blend_band;
    uint2 padding;

#ifdef __cplusplus
    inline void Init()
    {
        shadow_view_projection = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
        shadow_atlas_scale_bias = { 0,0,0,0 };

        split_far = 0.0f;
        blend_band = 0.0f;
    }
#endif
};

struct ObjectPushConstants
{
    uint instance_index;
    uint geometry_index;
    uint material_index;
    uint padding0;

#ifdef __cplusplus
    inline void Init()
    {
        instance_index = 0;
        geometry_index = 0;
        material_index = 0;
    }
#endif
};

struct alignas(16) ShaderInstance
{
    float4x4 world_transform;
    float3x3 normal_transform; // will be removed
    float3 padding;

#ifdef __cplusplus
    inline void Init()
    {
    }
#endif
};

CONSTANTBUFFER(g_frame, ShaderFrame, CBSLOT_RENDERER_FRAME);
CONSTANTBUFFER(g_camera, ShaderCamera, CBSLOT_RENDERER_CAMERA);

PUSHCONSTANT(push, ObjectPushConstants);

//CBUFFER(ForwardLightMaskCB, CBSLOT_RENDERER_FORWARD_LIGHTMASK)
//{
//    uint4 forward_light_mask;	// supports indexing 128 lights
//};

#ifdef __cplusplus
static_assert(sizeof(ShaderTextureSlot) == 16, "ShaderTextureSlot layout mismatch");
static_assert(sizeof(ShaderGeometry) == 64, "ShaderGeometry layout mismatch");
static_assert(sizeof(ShaderMaterial) == 272, "ShaderMaterial layout mismatch");
static_assert(sizeof(ShaderScene) == 48, "ShaderScene layout mismatch");
static_assert(sizeof(ShaderSky) == 64, "ShaderSky layout mismatch");
static_assert(sizeof(ShaderFrame) == 112, "ShaderFrame layout mismatch");
static_assert(sizeof(ShaderCamera) == 320, "ShaderCamera layout mismatch");
static_assert(sizeof(ShaderLight) == 48, "ShaderLight layout mismatch");
static_assert(sizeof(ShaderShadowCascade) == 96, "ShaderShadowCascade layout mismatch");
static_assert(sizeof(ObjectPushConstants) == 16, "ObjectPushConstants layout mismatch");
static_assert(sizeof(ShaderInstance) == 112, "ShaderInstance layout mismatch");
#endif // __cplusplus

#endif // WON_SHADERINTEROP_RENDERER_H
