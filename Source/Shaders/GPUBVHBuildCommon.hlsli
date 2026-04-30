#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"

Buffer<float3> bvh_build_positions : register(t0);
Buffer<uint> bvh_build_indices : register(t1);
RWStructuredBuffer<ShaderBVHPrimitive> bvh_build_primitives : register(u0);
RWStructuredBuffer<ShaderBVHNode> bvh_build_nodes : register(u1);
RWStructuredBuffer<uint2> bvh_build_sort_keys : register(u2);
RWStructuredBuffer<uint> bvh_build_parent_indices : register(u3);
RWStructuredBuffer<uint> bvh_build_node_counters : register(u4);

cbuffer BVHBuildConstants : register(b2)
{
    float3 bvh_build_bounds_min;
    float bvh_build_padding0;
    float3 bvh_build_bounds_rcp_extent;
    float bvh_build_padding1;
};

static const uint BVH_INVALID_PARENT = 0xFFFFFFFFu;

uint CountLeadingZeros32(uint value)
{
    return value == 0 ? 32u : 31u - firstbithigh(value);
}

uint ExpandMortonBits(uint value)
{
    value = (value * 0x00010001u) & 0xFF0000FFu;
    value = (value * 0x00000101u) & 0x0F00F00Fu;
    value = (value * 0x00000011u) & 0xC30C30C3u;
    value = (value * 0x00000005u) & 0x49249249u;
    return value;
}

uint EncodeMorton3D(float3 value)
{
    value = saturate(value);
    uint3 quantized = (uint3)min(value * 1024.0f, 1023.0f); // 10 bits per axis
    return (ExpandMortonBits(quantized.x) << 2u) | (ExpandMortonBits(quantized.y) << 1u) | ExpandMortonBits(quantized.z); // 00xyzxyz...
}

bool SortKeyGreater(uint2 lhs, uint2 rhs)
{
    return lhs.x > rhs.x || (lhs.x == rhs.x && lhs.y > rhs.y);
}

bool BoundsValid(ShaderBVHNode node)
{
    return node.bounds_min.x <= node.bounds_max.x && node.bounds_min.y <= node.bounds_max.y && node.bounds_min.z <= node.bounds_max.z;
}

ShaderBVHNode MakeInvalidNode()
{
    ShaderBVHNode node;
    node.bounds_min = float3(FLT_MAX, FLT_MAX, FLT_MAX);
    node.bounds_max = float3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    node.left_index = -1;
    node.right_index = -1;
    node.first_primitive = 0;
    node.primitive_count = 0;
    node.padding = uint2(0, 0);
    return node;
}

int CommonPrefix(int lhs, int rhs, uint primitive_count)
{
    // return the number of matching upper bits
    if (lhs < 0 || rhs < 0 || lhs >= (int)primitive_count || rhs >= (int)primitive_count)
    {
        return -1;
    }

    uint2 lhs_key = bvh_build_sort_keys[lhs];
    uint2 rhs_key = bvh_build_sort_keys[rhs];
    if (lhs_key.x == rhs_key.x)
    {
        return 32 + (int)CountLeadingZeros32(lhs_key.y ^ rhs_key.y);
    }
    return (int)CountLeadingZeros32(lhs_key.x ^ rhs_key.x);
}

int FindSplit(int first, int last, uint primitive_count)
{
    int common_prefix = CommonPrefix(first, last, primitive_count);
    int split = first;
    int step = last - first;

    [loop]
    while (step > 1)
    {
        step = (step + 1) >> 1;
        int new_split = split + step;
        if (new_split < last)
        {
            int split_prefix = CommonPrefix(first, new_split, primitive_count);
            if (split_prefix > common_prefix)
            {
                split = new_split;
            }
        }
    }

    return split;
}
