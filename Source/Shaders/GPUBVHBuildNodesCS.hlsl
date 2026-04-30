#include "GPUBVHBuildCommon.hlsli"

PUSHCONSTANT(bvh_primitive_count_push, ShaderBVHPrimitiveCountPushConstants);

[numthreads(BVH_BUILDER_GROUPSIZE, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint primitive_count = bvh_primitive_count_push.primitive_count;

    uint index = dispatch_thread_id.x;
    if (index >= primitive_count)
    {
        return;
    }

    // internal node: N - 1
    // leaf node: N
    // total node: 2N - 1
    uint leaf_offset = primitive_count - 1;
    uint leaf_node_index = leaf_offset + index;

    ShaderBVHPrimitive primitive = bvh_build_primitives[index];
    ShaderBVHNode leaf = MakeInvalidNode();
    float3 bmin = min(primitive.v0, min(primitive.v1, primitive.v2));
    float3 bmax = max(primitive.v0, max(primitive.v1, primitive.v2));
    float3 eps = float3(0.001f, 0.001f, 0.001f);
    leaf.bounds_min = bmin - eps;
    leaf.bounds_max = bmax + eps;
    leaf.first_primitive = index;
    leaf.primitive_count = 1;
    bvh_build_nodes[leaf_node_index] = leaf;

    if (primitive_count == 1)
    {
        bvh_build_parent_indices[0] = BVH_INVALID_PARENT;
        return;
    }

    if (index == 0)
    {
        bvh_build_parent_indices[0] = BVH_INVALID_PARENT;
    }

    if (index >= primitive_count - 1)
    {
        // only 0 ~ N-2 thread creates internal node
        // last thread create only leaf node
        return;
    }

    int delta_next = CommonPrefix((int)index, (int)index + 1, primitive_count);
    int delta_prev = CommonPrefix((int)index, (int)index - 1, primitive_count);
    int direction = delta_next >= delta_prev ? 1 : -1;
    int delta_min = direction == 1 ? delta_prev : delta_next;

    // exponential search first
    int max_length = 2;
    [loop]
    while (CommonPrefix((int)index, (int)index + max_length * direction, primitive_count) > delta_min)
    {
        max_length <<= 1;
    }

    // binary search
    int length = 0;
    [loop]
    for (int step = max_length >> 1; step >= 1; step >>= 1)
    {
        if (CommonPrefix((int)index, (int)index + (length + step) * direction, primitive_count) > delta_min)
        {
            length += step;
        }
    }

    int range_end = (int)index + length * direction;
    int first = min((int)index, range_end);
    int last = max((int)index, range_end);
    int split = FindSplit(first, last, primitive_count);

    uint left_index = split == first ? leaf_offset + (uint)split : (uint)split;
    uint right_index = split + 1 == last ? leaf_offset + (uint)(split + 1) : (uint)(split + 1);

    ShaderBVHNode node = MakeInvalidNode();
    node.left_index = (int)left_index;
    node.right_index = (int)right_index;
    bvh_build_nodes[index] = node;
    bvh_build_parent_indices[left_index] = index;
    bvh_build_parent_indices[right_index] = index;
}
