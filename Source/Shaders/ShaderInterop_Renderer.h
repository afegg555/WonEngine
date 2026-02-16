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
};

struct alignas(16) ShaderMaterial
{
    float4 base_color;
    float4 emissive_color_metallic;

    float4 roughness_reflectance_metalness_refraction;
    float4 anisotropy_sheenroughness_clearcoat_clearcoatroughness;

    float4 sheencolor_padding;

    uint flags;
    uint3 padding;
    
    ShaderTextureSlot textures[TEXTURESLOT_COUNT]; // basecolormap... normalmap... etc..
};

struct ObjectPushConstants
{
    uint geometry_index;
    uint material_index;
    int mesh_descriptor;
    uint padding0;
};

struct alignas(16) ShaderObject
{
    float4x4 local_to_world;
};

#ifdef __cplusplus
static_assert(sizeof(ShaderTextureSlot) == 16, "ShaderTextureSlot layout mismatch");
static_assert(sizeof(ShaderGeometry) == 48, "ShaderGeometry layout mismatch");
static_assert(sizeof(ShaderMaterial) == 336, "ShaderMaterial layout mismatch");
static_assert(sizeof(ObjectPushConstants) == 16, "ObjectPushConstants layout mismatch");
static_assert(sizeof(ShaderObject) == 64, "ShaderObject layout mismatch");
#else
StructuredBuffer<ShaderObject> bindless_structured_object[] : register(t0, space200);
StructuredBuffer<ShaderGeometry> bindless_structured_geometry[] : register(t0, space201);
StructuredBuffer<ShaderMaterial> bindless_structured_material[] : register(t0, space202);
StructuredBuffer<float3> bindless_structured_position[] : register(t0, space203);
StructuredBuffer<float3> bindless_structured_normal[] : register(t0, space204);
StructuredBuffer<float2> bindless_structured_texcoord[] : register(t0, space205);
StructuredBuffer<uint> bindless_structured_index[] : register(t0, space206);

PUSHCONSTANT(object_push_constants, ObjectPushConstants);

inline ShaderObject LoadObject(uint object_buffer_index, uint object_index)
{
    return bindless_structured_object[object_buffer_index][object_index];
}

inline ShaderGeometry LoadGeometry(uint geometry_buffer_index, uint geometry_index)
{
    return bindless_structured_geometry[geometry_buffer_index][geometry_index];
}

inline ShaderMaterial LoadMaterial(uint material_buffer_index, uint material_index)
{
    return bindless_structured_material[material_buffer_index][material_index];
}
#endif // __cplusplus

#endif // WON_SHADERINTEROP_RENDERER_H
