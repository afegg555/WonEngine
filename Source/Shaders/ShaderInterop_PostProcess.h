#ifndef WON_SHADERINTEROP_POSTPROCESS_H
#define WON_SHADERINTEROP_POSTPROCESS_H

#include "ShaderInterop.h"

struct FXAAPushConstants
{
    uint input_descriptor;  // SRV of the source color (current ping-pong buffer)
    uint output_descriptor; // UAV of the destination color (opposite ping-pong buffer)
    float2 rcp_resolution;  // 1/width, 1/height
    uint2 resolution;       // width, height (dispatch bound)

#ifdef __cplusplus
    inline void Init()
    {
        input_descriptor = 0;
        output_descriptor = 0;
        rcp_resolution = float2(0.0f, 0.0f);
        resolution = uint2(0, 0);
    }
#endif
};

#ifdef __cplusplus
static_assert(sizeof(FXAAPushConstants) == 24, "FXAAPushConstants layout mismatch");
#endif

#ifdef WON_FXAA_PUSHCONSTANT
PUSHCONSTANT(fxaapush, FXAAPushConstants);
#endif

struct CompositePushConstants
{
    uint input_descriptor;

#ifdef __cplusplus
    inline void Init()
    {
        input_descriptor = 0;
    }
#endif
};

#ifdef __cplusplus
static_assert(sizeof(CompositePushConstants) == 4, "CompositePushConstants layout mismatch");
#endif

#ifdef WON_COMPOSITE_PUSHCONSTANT
PUSHCONSTANT(compositepush, CompositePushConstants);
#endif

static const uint TONEMAP_TYPE_REINHARD = 0;
static const uint TONEMAP_TYPE_ACES = 1;

struct TonemapPushConstants
{
    uint input_descriptor;
    uint output_descriptor;
    uint2 resolution;
    uint tonemap_type;

#ifdef __cplusplus
    inline void Init()
    {
        input_descriptor = 0;
        output_descriptor = 0;
        resolution = uint2(0, 0);
        tonemap_type = TONEMAP_TYPE_REINHARD;
    }
#endif
};

#ifdef __cplusplus
static_assert(sizeof(TonemapPushConstants) == 20, "TonemapPushConstants layout mismatch");
#endif

#ifdef WON_TONEMAP_PUSHCONSTANT
PUSHCONSTANT(tonemappush, TonemapPushConstants);
#endif

#endif // WON_SHADERINTEROP_POSTPROCESS_H
