#ifndef WON_SHADERINTEROP_LIGHTCULL_H
#define WON_SHADERINTEROP_LIGHTCULL_H

#include "ShaderInterop.h"

static const uint LIGHTCULL_TILE_SIZE = 16;
static const uint MAX_LIGHTS_PER_CLUSTER = 32;
static const uint MAX_DEPTH_SLICES = 32;

struct LightCullPushConstants
{
    uint2 cluster_count;
    uint cluster_light_count_uav;
    uint cluster_light_offset_uav;
    uint cluster_light_index_uav;
    uint light_count;
    uint depth_slice_count;

#ifdef __cplusplus
    inline void Init()
    {
        cluster_count = uint2(0, 0);
        cluster_light_count_uav = 0;
        cluster_light_offset_uav = 0;
        cluster_light_index_uav = 0;
        light_count = 0;
        depth_slice_count = 1;
    }
#endif
};

#ifdef __cplusplus
static_assert(sizeof(LightCullPushConstants) == 28, "LightCullPushConstants layout mismatch");
#endif

#ifndef __cplusplus
uint ClusterSliceFromViewZ(float view_z, float z_near, float z_far, uint slice_count)
{
    const float z = max(view_z, z_near);
	const float t = log(z / z_near) / log(z_far / z_near); // (log(z) - log(z_near)) / (log(z_far) - log(z_near))   -->   (val - min) / (max - min)
    return min((uint)(t * (float)slice_count), slice_count - 1u);
}

float ClusterSliceViewZ(uint slice, float z_near, float z_far, uint slice_count)
{
    return z_near * pow(z_far / z_near, (float)slice / (float)slice_count);
}
#endif

#ifdef WON_LIGHTCULL_PUSHCONSTANT
PUSHCONSTANT(lightcullpush, LightCullPushConstants);
#endif

#endif // WON_SHADERINTEROP_LIGHTCULL_H
