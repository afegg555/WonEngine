#ifndef WON_SHADERINTEROP_UTILITY_H
#define WON_SHADERINTEROP_UTILITY_H

#include "ShaderInterop.h"

static const uint MIPGEN_FLAGS_IS_SRGB = 1 << 0;
static const uint TEXTURE_BC_COMPRESS_FLAGS_IS_SRGB = 1 << 0;

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

struct TextureBCCompressPushConstants
{
    uint source_srv;
    uint output_uav;
    uint width;
    uint height;
    uint flags;
    float quality;
    uint output_offset;
    uint padding0;

#ifdef __cplusplus
    inline void Init()
    {
        source_srv = 0;
        output_uav = 0;
        width = 0;
        height = 0;
        flags = 0;
        quality = 0.0f;
        output_offset = 0;
        padding0 = 0;
    }
#endif
};

#ifdef __cplusplus
static_assert(sizeof(TextureMipGenPushConstants) == 20, "TextureMipGenPushConstants layout mismatch");
static_assert(sizeof(TextureBCCompressPushConstants) == 32, "TextureBCCompressPushConstants layout mismatch");
#endif

#ifdef WON_TEXTURE_MIPGEN_PUSHCONSTANT
PUSHCONSTANT(mipgenpush, TextureMipGenPushConstants);
#endif

#ifdef WON_TEXTURE_BC_COMPRESS_PUSHCONSTANT
PUSHCONSTANT(bccompresspush, TextureBCCompressPushConstants);
#endif

#endif // WON_SHADERINTEROP_UTILITY_H
