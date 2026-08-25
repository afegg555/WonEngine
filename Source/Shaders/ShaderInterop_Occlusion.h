#ifndef WON_SHADERINTEROP_OCCLUSION_H
#define WON_SHADERINTEROP_OCCLUSION_H

#include "ShaderInterop.h"

struct OcclusionPushConstants
{
    float3 aabb_min;
    float padding0;
    float3 aabb_max;

#ifdef __cplusplus
    inline void Init()
    {
        aabb_min = float3(0.0f, 0.0f, 0.0f);
        padding0 = 0.0f;
        aabb_max = float3(0.0f, 0.0f, 0.0f);
    }
#endif
};

#ifdef __cplusplus
static_assert(sizeof(OcclusionPushConstants) == 28, "OcclusionPushConstants layout mismatch");
#endif

#ifdef WON_OCCLUSION_PUSHCONSTANT
PUSHCONSTANT(occlusionpush, OcclusionPushConstants);
#endif

#endif // WON_SHADERINTEROP_OCCLUSION_H
