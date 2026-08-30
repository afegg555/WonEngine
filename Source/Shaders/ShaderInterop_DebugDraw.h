#ifndef WON_SHADERINTEROP_DEBUGDRAW_H
#define WON_SHADERINTEROP_DEBUGDRAW_H

#include "ShaderInterop.h"

struct DebugDraw2DPushConstants
{
    uint item_buffer_descriptor;
    uint item_index;

#ifdef __cplusplus
    inline void Init()
    {
        item_buffer_descriptor = 0;
        item_index = 0;
    }
#endif
};

struct DebugDraw3DPushConstants
{
    uint vertex_buffer;

#ifdef __cplusplus
    inline void Init()
    {
        vertex_buffer = 0;
    }
#endif
};

#ifdef __cplusplus
static_assert(sizeof(DebugDraw2DPushConstants) == 8, "DebugDraw2DPushConstants layout mismatch");
static_assert(sizeof(DebugDraw3DPushConstants) == 4, "DebugDraw3DPushConstants layout mismatch");
#endif

#ifdef WON_DEBUGDRAW_2D_PUSHCONSTANT
PUSHCONSTANT(debugdraw2dpush, DebugDraw2DPushConstants);
#endif

#ifdef WON_DEBUGDRAW_3D_PUSHCONSTANT
PUSHCONSTANT(debugdraw3dpush, DebugDraw3DPushConstants);
#endif

#endif // WON_SHADERINTEROP_DEBUGDRAW_H
