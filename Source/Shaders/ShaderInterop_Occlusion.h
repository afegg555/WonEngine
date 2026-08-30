#ifndef WON_SHADERINTEROP_OCCLUSION_H
#define WON_SHADERINTEROP_OCCLUSION_H

#include "ShaderInterop.h"

struct OcclusionPushConstants
{
    uint box_buffer_descriptor;
    uint box_index;

#ifdef __cplusplus
    inline void Init()
    {
        box_buffer_descriptor = 0;
        box_index = 0;
    }
#endif
};

#ifdef __cplusplus
static_assert(sizeof(OcclusionPushConstants) == 8, "OcclusionPushConstants layout mismatch");
#endif

#ifdef WON_OCCLUSION_PUSHCONSTANT
PUSHCONSTANT(occlusionpush, OcclusionPushConstants);
#endif

#endif // WON_SHADERINTEROP_OCCLUSION_H
