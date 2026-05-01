#include "GPUBVHBuildCommon.hlsli"

PUSHCONSTANT(bvh_primitive_count_push, BVHPrimitiveCountPushConstants);

[numthreads(BVH_BUILDER_GROUPSIZE, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint primitive_count = bvh_primitive_count_push.primitive_count;

    uint index = dispatch_thread_id.x;
    if (index >= primitive_count || primitive_count <= 1)
    {
        return;
    }

    uint node_index = primitive_count - 1 + index;
    uint parent_index = bvh_build_parent_indices[node_index];

    [loop]
    while (parent_index != BVH_INVALID_PARENT)
    {
        uint original_count = 0;
        InterlockedAdd(bvh_build_node_counters[parent_index], 1u, original_count);
        if (original_count == 0)
        {
            return;
        }

        ShaderBVHNode parent = bvh_build_nodes[parent_index];
        ShaderBVHNode left = bvh_build_nodes[parent.left_index];
        ShaderBVHNode right = bvh_build_nodes[parent.right_index];

        ShaderBVHNode merged = parent;
        if (BoundsValid(left) && BoundsValid(right))
        {
            merged.bounds_min = min(left.bounds_min, right.bounds_min);
            merged.bounds_max = max(left.bounds_max, right.bounds_max);
        }
        else if (BoundsValid(left))
        {
            merged.bounds_min = left.bounds_min;
            merged.bounds_max = left.bounds_max;
        }
        else if (BoundsValid(right))
        {
            merged.bounds_min = right.bounds_min;
            merged.bounds_max = right.bounds_max;
        }
        bvh_build_nodes[parent_index] = merged;

        DeviceMemoryBarrier();
        parent_index = bvh_build_parent_indices[parent_index];
    }
}
