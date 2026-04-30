#ifndef WON_SHADERINTEROP_BVH_H
#define WON_SHADERINTEROP_BVH_H

#include "ShaderInterop.h"

static const uint BVH_BUILDER_GROUPSIZE = 64;

struct alignas(16) ShaderBVHNode
{
    float3 bounds_min;
    int left_index;

    float3 bounds_max;
    int right_index;

    uint first_primitive;
    uint primitive_count;
    uint2 padding;
};

struct alignas(16) ShaderBVHPrimitive
{
    float3 v0;
    uint submesh_index;

    float3 v1;
    uint triangle_index;

    float3 v2;
    uint material_slot;
};

struct BVHGeneratePrimitivesPushConstants
{
    uint first_index;
    uint primitive_offset;
    uint triangle_count;
    uint submesh_index;
    uint material_slot;
};

struct BVHSortPrimitivesPushConstants
{
    uint sort_merge_size;
    uint sort_compare_stride;
};

struct BVHPrimitiveCountPushConstants
{
    uint primitive_count;
};

struct alignas(16) ShaderBVHInstance
{
    float4x4 world_to_local;
    float4x4 local_to_world;

    float3 bounds_min;
    int blas_node_buffer;

    float3 bounds_max;
    int blas_primitive_buffer;

    uint blas_node_count;
    uint blas_primitive_count;
    uint geometry_offset;
    uint material_offset;

    uint material_count;
    uint3 padding;
};

#ifdef __cplusplus
static_assert(sizeof(ShaderBVHNode) == 48, "ShaderBVHNode layout mismatch");
static_assert(sizeof(ShaderBVHPrimitive) == 48, "ShaderBVHPrimitive layout mismatch");
static_assert(sizeof(BVHGeneratePrimitivesPushConstants) == 20, "BVHGeneratePrimitivesPushConstants layout mismatch");
static_assert(sizeof(BVHSortPrimitivesPushConstants) == 8, "BVHSortPrimitivesPushConstants layout mismatch");
static_assert(sizeof(BVHPrimitiveCountPushConstants) == 4, "BVHPrimitiveCountPushConstants layout mismatch");
static_assert(sizeof(ShaderBVHInstance) == 192, "ShaderBVHInstance layout mismatch");
#endif

#endif
