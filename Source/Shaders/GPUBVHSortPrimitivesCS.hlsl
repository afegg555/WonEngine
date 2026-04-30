#include "GPUBVHBuildCommon.hlsli"

PUSHCONSTANT(bvh_sort_push, ShaderBVHSortPrimitivesPushConstants);

// bitonic sort
[numthreads(BVH_BUILDER_GROUPSIZE, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint sort_count = 0;
    uint sort_stride = 0;
    bvh_build_sort_keys.GetDimensions(sort_count, sort_stride);

    uint index = dispatch_thread_id.x; // primitive index
    uint compare_index = index ^ bvh_sort_push.sort_compare_stride;
    if (index >= sort_count || compare_index >= sort_count || compare_index <= index)
    {
        return;
    }

    const bool ascending = (index & bvh_sort_push.sort_merge_size) == 0;
    uint2 key = bvh_build_sort_keys[index];
    uint2 compare_key = bvh_build_sort_keys[compare_index];
    if (SortKeyGreater(key, compare_key) == ascending)
    {
        ShaderBVHPrimitive primitive = bvh_build_primitives[index];
        bvh_build_sort_keys[index] = compare_key;
        bvh_build_primitives[index] = bvh_build_primitives[compare_index];
        bvh_build_sort_keys[compare_index] = key;
        bvh_build_primitives[compare_index] = primitive;
    }
}
