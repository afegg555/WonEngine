#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_OCCLUSION_PUSHCONSTANT
#include "ShaderInterop_Occlusion.h"

static const uint occlusion_box_corners[36] = {
    0, 2, 1, 1, 2, 3,
    4, 5, 6, 5, 7, 6,
    0, 1, 4, 1, 5, 4,
    2, 6, 3, 3, 6, 7,
    0, 4, 2, 2, 4, 6,
    1, 3, 5, 3, 7, 5
};

float4 main(uint vertex_id : SV_VertexID) : SV_Position
{
    const ShaderOcclusionBox box = bindless_structured_occlusion_box[DescriptorIndex(occlusionpush.box_buffer_descriptor)][occlusionpush.box_index];

    const uint corner = occlusion_box_corners[vertex_id];
    const float3 position = float3(
        (corner & 1u) != 0u ? box.aabb_max.x : box.aabb_min.x,
        (corner & 2u) != 0u ? box.aabb_max.y : box.aabb_min.y,
        (corner & 4u) != 0u ? box.aabb_max.z : box.aabb_min.z);

    return mul(GetCamera().view_projection, float4(position, 1.0f));
}
