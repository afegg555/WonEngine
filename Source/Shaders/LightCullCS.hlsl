#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_LIGHTCULL_PUSHCONSTANT
#include "ShaderInterop_LightCull.h"

groupshared uint gs_slice_count[MAX_DEPTH_SLICES];
groupshared uint gs_slice_offset[MAX_DEPTH_SLICES];
groupshared uint gs_slice_cursor[MAX_DEPTH_SLICES];

bool CullLightToSlices(uint li, float3 cam, float3 fwd, float3 planes[4], float cam_near, float cam_far, uint slice_count, out uint slice_min, out uint slice_max)
{
    slice_min = 0;
    slice_max = 0;

    ShaderLight light = GetLight(li);
    if (light.GetType() == SHADER_LIGHT_TYPE_DIRECTIONAL)
    {
        return false;
    }

    const float3 to_center = light.position - cam;
    const float r = (float)light.GetRange();

    [unroll]
    for (uint pi = 0; pi < 4; ++pi)
    {
        if (dot(planes[pi], to_center) < -r)
        {
            return false;
        }
    }

    const float t = dot(to_center, fwd); // view space light_pos.z
    if (t + r < cam_near)
    {
        return false;
    }
    // if depth is divided uniformly, the near froxel becomes needle-like
    // so we use a logarithmic depth division to avoid that(more froxels near the camera)
    slice_min = ClusterSliceFromViewZ(max(t - r, cam_near), cam_near, cam_far, slice_count);
    slice_max = ClusterSliceFromViewZ(max(t + r, cam_near), cam_near, cam_far, slice_count);
    return true;
}

[numthreads(LIGHTCULL_TILE_SIZE, LIGHTCULL_TILE_SIZE, 1)]
void main(uint3 group_id : SV_GroupID, uint group_index : SV_GroupIndex)
{
    if (group_id.x >= lightcullpush.cluster_count.x || group_id.y >= lightcullpush.cluster_count.y)
    {
        return;
    }

    const uint slice_count = min(lightcullpush.depth_slice_count, MAX_DEPTH_SLICES);
    const uint total_threads = LIGHTCULL_TILE_SIZE * LIGHTCULL_TILE_SIZE;
    const uint cluster_tiles = lightcullpush.cluster_count.x * lightcullpush.cluster_count.y;
    const uint tile_index = group_id.y * lightcullpush.cluster_count.x + group_id.x;
    const uint tile_region = slice_count * MAX_LIGHTS_PER_CLUSTER; // froxels in one tile share this budget
    const uint tile_base = tile_index * tile_region;

    for (uint s = group_index; s < slice_count; s += total_threads)
    {
        gs_slice_count[s] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    const float2 res = (float2)GetCamera().internal_resolution;
    const float2 px_min = float2(group_id.xy) * LIGHTCULL_TILE_SIZE;
    const float2 px_max = min(px_min + LIGHTCULL_TILE_SIZE, res);

    const float2 ndc_tl = float2(px_min.x / res.x * 2.0 - 1.0, 1.0 - px_min.y / res.y * 2.0);
    const float2 ndc_tr = float2(px_max.x / res.x * 2.0 - 1.0, 1.0 - px_min.y / res.y * 2.0);
    const float2 ndc_br = float2(px_max.x / res.x * 2.0 - 1.0, 1.0 - px_max.y / res.y * 2.0);
    const float2 ndc_bl = float2(px_min.x / res.x * 2.0 - 1.0, 1.0 - px_max.y / res.y * 2.0);

    const float3 cam = GetCamera().position;
    const float3 fwd = GetCamera().forward;

    const float3 d_tl = normalize(UnprojectRay(ndc_tl) - cam);
    const float3 d_tr = normalize(UnprojectRay(ndc_tr) - cam);
    const float3 d_br = normalize(UnprojectRay(ndc_br) - cam);
    const float3 d_bl = normalize(UnprojectRay(ndc_bl) - cam);
    const float3 d_center = normalize(d_tl + d_tr + d_br + d_bl);

    float3 planes[4];
    planes[0] = cross(d_tl, d_tr);
    planes[1] = cross(d_tr, d_br);
    planes[2] = cross(d_br, d_bl);
    planes[3] = cross(d_bl, d_tl);
    [unroll]
    for (uint p = 0; p < 4; ++p)
    {
        planes[p] = normalize(planes[p]);
        if (dot(planes[p], d_center) < 0.0)
        {
            planes[p] = -planes[p];
        }
    }

    const float cam_near = GetCamera().z_near;
    const float cam_far = GetCamera().z_far;

    // 1. count lights per slice pass
    for (uint li = group_index; li < lightcullpush.light_count; li += total_threads)
    {
        uint slice_min;
        uint slice_max;
        if (!CullLightToSlices(li, cam, fwd, planes, cam_near, cam_far, slice_count, slice_min, slice_max))
        {
            continue;
        }
        for (uint s = slice_min; s <= slice_max; ++s)
        {
            InterlockedAdd(gs_slice_count[s], 1);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (group_index == 0)
    {
        uint running = 0;
        for (uint s = 0; s < slice_count; ++s)
        {
            const uint avail = (running < tile_region) ? (tile_region - running) : 0u; // froxels in one tile share this budget
            const uint cnt = min(gs_slice_count[s], avail);
            const uint froxel_index = s * cluster_tiles + tile_index;
            
            bindless_rwbuffers_uint[DescriptorIndex((int) lightcullpush.cluster_light_count_uav)][froxel_index] = cnt;
            bindless_rwbuffers_uint[DescriptorIndex((int)lightcullpush.cluster_light_offset_uav)][froxel_index] = tile_base + running; // prefix sum offset
            gs_slice_offset[s] = running;
            gs_slice_cursor[s] = running;
            gs_slice_count[s] = cnt;
            running += cnt;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // 2. write light indices to slices pass
    for (uint lj = group_index; lj < lightcullpush.light_count; lj += total_threads)
    {
        uint slice_min;
        uint slice_max;
        if (!CullLightToSlices(lj, cam, fwd, planes, cam_near, cam_far, slice_count, slice_min, slice_max))
        {
            continue;
        }
        for (uint s = slice_min; s <= slice_max; ++s)
        {
            uint slot;
            InterlockedAdd(gs_slice_cursor[s], 1, slot);
            if (slot < gs_slice_offset[s] + gs_slice_count[s])
            {
                bindless_rwbuffers_uint[DescriptorIndex((int)lightcullpush.cluster_light_index_uav)][tile_base + slot] = lj;
            }
        }
    }
}
