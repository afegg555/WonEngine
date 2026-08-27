#define WON_MESH_NORMAL_PUSHCONSTANT
#include "Common.hlsli"

[numthreads(64, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    const uint vertex = dispatch_thread_id.x;
    if (vertex >= meshnormalpush.vertex_count)
    {
        return;
    }

    const uint2 range = bindless_buffers_uint2[DescriptorIndex(meshnormalpush.adjacency_range_descriptor)][vertex]; // (start_index, count) of triangles

    float3 accumulated = 0;
    for (uint i = 0; i < range.y; ++i)
    {
        const uint triangle_index = bindless_buffers_uint[DescriptorIndex(meshnormalpush.adjacency_triangle_descriptor)][range.x + i];
        const uint i0 = bindless_buffers_uint[DescriptorIndex(meshnormalpush.index_descriptor)][triangle_index * 3 + 0];
        const uint i1 = bindless_buffers_uint[DescriptorIndex(meshnormalpush.index_descriptor)][triangle_index * 3 + 1];
        const uint i2 = bindless_buffers_uint[DescriptorIndex(meshnormalpush.index_descriptor)][triangle_index * 3 + 2];

        const float3 p0 = bindless_buffers_float3[DescriptorIndex(meshnormalpush.position_descriptor)][meshnormalpush.stream_offset + i0];
        const float3 p1 = bindless_buffers_float3[DescriptorIndex(meshnormalpush.position_descriptor)][meshnormalpush.stream_offset + i1];
        const float3 p2 = bindless_buffers_float3[DescriptorIndex(meshnormalpush.position_descriptor)][meshnormalpush.stream_offset + i2];

        accumulated += cross(p1 - p0, p2 - p0);
    }

    const float length_squared = dot(accumulated, accumulated);
    const float3 normal = length_squared > 0.0f ? accumulated * rsqrt(length_squared) : float3(0.0f, 0.0f, 1.0f);
    bindless_rwbuffers_float3[DescriptorIndex(meshnormalpush.normal_uav_descriptor)][meshnormalpush.stream_offset + vertex] = normal;
}
