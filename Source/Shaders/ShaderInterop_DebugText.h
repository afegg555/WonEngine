#ifndef WON_SHADERINTEROP_DEBUGTEXT_H
#define WON_SHADERINTEROP_DEBUGTEXT_H

#include "ShaderInterop.h"

struct DebugTextPushConstants
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

#ifdef __cplusplus
static_assert(sizeof(DebugTextPushConstants) == 40, "DebugTextPushConstants layout mismatch");
#endif

#ifdef WON_DEBUGTEXT_PUSHCONSTANT
PUSHCONSTANT(debugtextpush, DebugTextPushConstants);
#endif

#endif // WON_SHADERINTEROP_DEBUGTEXT_H
