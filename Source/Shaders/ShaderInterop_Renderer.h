#ifndef WON_SHADERINTEROP_RENDERER_H
#define WON_SHADERINTEROP_RENDERER_H

#include "ShaderInterop.h"

#define BINDLESS_SPACE_RENDERER_OBJECT 200
#define BINDLESS_SPACE_RENDERER_GEOMETRY 201
#define BINDLESS_SPACE_RENDERER_MATERIAL 202
#define BINDLESS_SPACE_RENDERER_POSITION 203
#define BINDLESS_SPACE_RENDERER_NORMAL 204
#define BINDLESS_SPACE_RENDERER_TEXCOORD 205
#define BINDLESS_SPACE_RENDERER_INDEX 206
#define BINDLESS_SPACE_RENDERER_TEXTURE 207

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
    int normal_buffer_descriptor;
    int texcoord_buffer_descriptor;
    int index_buffer_descriptor;

    float3 bounds_min;
    uint index_count;
    float3 bounds_max;
    uint flags;

#ifdef __cplusplus
    inline void Init()
    {
        position_buffer_descriptor = -1;
        normal_buffer_descriptor = -1;
        texcoord_buffer_descriptor = -1;
        index_buffer_descriptor = -1;
    }
#endif
};

struct alignas(16) ShaderMaterial
{
    float4 base_color;
    float4 emissive_color_metallic;

    float4 roughness_reflectance_metalness_refraction;
    float4 anisotropy_sheenroughness_clearcoat_clearcoatroughness;

    float4 sheencolor_padding;

    uint flags; // see SHADER_MATERIAL_FLAGS
    uint3 padding;
    
    ShaderTextureSlot textures[TEXTURESLOT_COUNT];

#ifdef __cplusplus
    inline void Init()
    {
        for (size_t i = 0; i < TEXTURESLOT_COUNT; i++)
        {
            textures[i].Init();
        }
    }
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
    float4x4 local_to_world;

#ifdef __cplusplus
    inline void Init()
    {
    }
#endif
};

CONSTANTBUFFER(g_frame, ShaderFrame, CBSLOT_RENDERER_FRAME);
CONSTANTBUFFER(g_camera, ShaderCamera, CBSLOT_RENDERER_CAMERA);

PUSHCONSTANT(object_push_constants, ObjectPushConstants);

#ifdef __cplusplus
static_assert(sizeof(ShaderTextureSlot) == 16, "ShaderTextureSlot layout mismatch");
static_assert(sizeof(ShaderGeometry) == 48, "ShaderGeometry layout mismatch");
static_assert(sizeof(ShaderMaterial) == 336, "ShaderMaterial layout mismatch");
static_assert(sizeof(ShaderScene) == 16, "ShaderScene layout mismatch");
static_assert(sizeof(ShaderFrame) == 16, "ShaderFrame layout mismatch");
static_assert(sizeof(ShaderCamera) == 192, "ShaderCamera layout mismatch");
static_assert(sizeof(ObjectPushConstants) == 16, "ObjectPushConstants layout mismatch");
static_assert(sizeof(ShaderInstance) == 64, "ShaderInstance layout mismatch");
#endif // __cplusplus

#endif // WON_SHADERINTEROP_RENDERER_H
