#ifndef WON_SHADERINTEROP_DEBUGDRAW_H
#define WON_SHADERINTEROP_DEBUGDRAW_H

#include "ShaderInterop.h"

struct DebugDraw2DPushConstants
{
    float4 rect;    // normalized screen-space quad: x, y (top-left), w, h in [0,1]
    float4 uv_rect; // atlas uv: min.xy, max.zw
    uint color;     // 0xRRGGBBAA
    uint atlas_index;

#ifdef __cplusplus
    inline void Init()
    {
        rect = { 0.0f, 0.0f, 0.0f, 0.0f };
        uv_rect = { 0.0f, 0.0f, 1.0f, 1.0f };
        color = 0xffffffffu;
        atlas_index = 0;
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
static_assert(sizeof(DebugDraw2DPushConstants) == 40, "DebugDraw2DPushConstants layout mismatch");
static_assert(sizeof(DebugDraw3DPushConstants) == 4, "DebugDraw3DPushConstants layout mismatch");
#endif

#ifdef WON_DEBUGDRAW_2D_PUSHCONSTANT
PUSHCONSTANT(debugdraw2dpush, DebugDraw2DPushConstants);
#endif

#ifdef WON_DEBUGDRAW_3D_PUSHCONSTANT
PUSHCONSTANT(debugdraw3dpush, DebugDraw3DPushConstants);
#endif

#endif // WON_SHADERINTEROP_DEBUGDRAW_H
