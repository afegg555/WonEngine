#include "GPUBVHBuildCommon.hlsli"

PUSHCONSTANT(bvh_generate_push, BVHGeneratePrimitivesPushConstants);

[numthreads(BVH_BUILDER_GROUPSIZE, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID, uint3 group_id : SV_GroupID)
{
    uint local_triangle_index = dispatch_thread_id.x;
    uint triangle_count = bvh_generate_push.triangle_count;
    if (local_triangle_index >= triangle_count)
    {
        return;
    }

    uint index_offset = bvh_generate_push.first_index + local_triangle_index * 3u;
    uint primitive_index = bvh_generate_push.primitive_offset + local_triangle_index;
    uint i0 = bvh_build_indices[index_offset];
    uint i1 = bvh_build_indices[index_offset + 1u];
    uint i2 = bvh_build_indices[index_offset + 2u];

    ShaderBVHPrimitive primitive;
    primitive.v0 = bvh_build_positions[i0];
    primitive.v1 = bvh_build_positions[i1];
    primitive.v2 = bvh_build_positions[i2];
    primitive.submesh_index = bvh_generate_push.submesh_index;
    primitive.triangle_index = local_triangle_index;
    primitive.material_slot = bvh_generate_push.material_slot;
    bvh_build_primitives[primitive_index] = primitive;

    float3 triangle_centroid = (primitive.v0 + primitive.v1 + primitive.v2) * (1.0f / 3.0f);
    float3 normalized_centroid = (triangle_centroid - bvh_build_bounds_min) * bvh_build_bounds_rcp_extent;
    bvh_build_sort_keys[primitive_index] = uint2(EncodeMorton3D(normalized_centroid), primitive_index);
}
