#ifndef WON_SHADERINTEROP_DECAL_H
#define WON_SHADERINTEROP_DECAL_H

#ifndef WON_DISABLE_RENDERER_PUSHCONSTANT
#define WON_DISABLE_RENDERER_PUSHCONSTANT
#endif

#include "ShaderInterop.h"

struct DecalPushConstants
{
    uint decal_buffer;
    uint decal_index;
    uint depth_descriptor;

#ifdef __cplusplus
    inline void Init()
    {
        decal_buffer = 0;
        decal_index = 0;
        depth_descriptor = 0;
    }
#endif
};

PUSHCONSTANT(decalpush, DecalPushConstants);

#ifdef __cplusplus
static_assert(sizeof(DecalPushConstants) == 12, "DecalPushConstants layout mismatch");
#endif

#endif
