#ifndef WON_SHADERINTEROP_UTILITY_H
#define WON_SHADERINTEROP_UTILITY_H

#include "ShaderInterop.h"

static const uint DISPATCHBLOCKSIZE2D = 8;
static const uint DISPATCHBLOCKSIZE3D = 4;

static const uint MIPGEN_FLAGS_IS_SRGB = 1 << 0;

struct TextureMipGenPushConstants
{
    uint source_mip_srv;
    uint destination_mip_uav;
    uint destination_width;
    uint destination_height;
    uint flags;

#ifdef __cplusplus
    inline void Init()
    {
        source_mip_srv = 0;
        destination_mip_uav = 0;
        destination_width = 0;
        destination_height = 0;

        flags = 0;
    }
#endif
};

#ifdef __cplusplus
static_assert(sizeof(TextureMipGenPushConstants) == 20, "TextureMipGenPushConstants layout mismatch");
#endif

PUSHCONSTANT(mipgenpush, TextureMipGenPushConstants);

#endif // WON_SHADERINTEROP_UTILITY_H
