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

static const uint luminance_reduce_group_count = 256; // pass 1 group count = partial count = pass 2 thread count
static const uint luminance_reduce_group_size = 256;  // pass 1 threads per group

struct LuminanceReducePushConstants
{
    uint input_descriptor;   // pass 1: SRV of the HDR color buffer, pass 2: SRV of the partials buffer
    uint output_descriptor;  // pass 1: UAV of the partials buffer, pass 2: UAV of the luminance buffer [0]
    uint2 viewport_size;     // pass 1: viewport extent to sample
    uint2 viewport_offset;   // pass 1: viewport top-left inside the color buffer

#ifdef __cplusplus
    inline void Init()
    {
        input_descriptor = 0;
        output_descriptor = 0;
        viewport_size = uint2(0, 0);
        viewport_offset = uint2(0, 0);
    }
#endif
};

#ifdef __cplusplus
static_assert(sizeof(LuminanceReducePushConstants) == 24, "LuminanceReducePushConstants layout mismatch");
#endif

#ifdef WON_LUMINANCE_REDUCE_PUSHCONSTANT
PUSHCONSTANT(luminancereducepush, LuminanceReducePushConstants);
#endif

#endif // WON_SHADERINTEROP_POSTPROCESS_H
