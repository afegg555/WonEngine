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

enum TEXTURESLOT
{
    BASECOLORMAP,
    NORMALMAP,
    SURFACEMAP,
    EMISSIVEMAP,
    DISPLACEMENTMAP,
    OCCLUSIONMAP,
    TRANSMISSIONMAP,
    SHEENCOLORMAP,
    SHEENROUGHNESSMAP,
    CLEARCOATMAP,
    CLEARCOATROUGHNESSMAP,
    CLEARCOATNORMALMAP,
    SPECULARMAP,
    ANISOTROPYMAP,
    TRANSPARENCYMAP,

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
#endif
};

struct alignas(16) ShaderScene
{
    int instancebuffer;
    int geometrybuffer;
    int materialbuffer;
    int padding0;

#ifdef __cplusplus
    inline void Init()
    {
        instancebuffer = -1;
        geometrybuffer = -1;
        materialbuffer = -1;
    }
#endif
};

struct alignas(16) ShaderFrame
{
    ShaderScene scene;

#ifdef __cplusplus
    inline void Init()
    {
        scene.Init();
    }
#endif
};

struct alignas(16) ShaderCamera
{
    float4x4 view;
    float4x4 projection;
    float4x4 view_projection;

#ifdef __cplusplus
    inline void Init()
    {
    }
#endif
};

struct alignas(16) ShaderLight
{
	float3 position;
    float padding;

    float3 color;
    float padding2;

    float3 direction;
    float padding3;
#ifndef __cplusplus
#endif // __cplusplus
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
        instance_index = -1;
        geometry_index = -1;
        material_index = -1;
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

#ifdef __cplusplus
static_assert(sizeof(ShaderTextureSlot) == 16, "ShaderTextureSlot layout mismatch");
static_assert(sizeof(ShaderGeometry) == 64, "ShaderGeometry layout mismatch");
static_assert(sizeof(ShaderMaterial) == 288, "ShaderMaterial layout mismatch");
static_assert(sizeof(ShaderScene) == 16, "ShaderScene layout mismatch");
static_assert(sizeof(ShaderFrame) == 16, "ShaderFrame layout mismatch");
static_assert(sizeof(ShaderCamera) == 192, "ShaderCamera layout mismatch");
static_assert(sizeof(ObjectPushConstants) == 16, "ObjectPushConstants layout mismatch");
static_assert(sizeof(ShaderInstance) == 112, "ShaderInstance layout mismatch");
#endif // __cplusplus

#endif // WON_SHADERINTEROP_RENDERER_H
