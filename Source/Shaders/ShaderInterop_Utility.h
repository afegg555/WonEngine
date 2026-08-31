#ifndef WON_SHADERINTEROP_UTILITY_H
#define WON_SHADERINTEROP_UTILITY_H

#include "ShaderInterop.h"

static const uint MIPGEN_FLAGS_IS_SRGB = 1 << 0;
static const uint TEXTURE_BC_COMPRESS_FLAGS_IS_SRGB = 1 << 0;

struct TextureMipGenPushConstants
{
    uint source_mip_srv;
    uint destination_mip_uav;
    uint flags;

#ifdef __cplusplus
    inline void Init()
    {
        source_mip_srv = 0;
        destination_mip_uav = 0;
        flags = 0;
    }
#endif
};

struct TextureBCCompressPushConstants
{
    uint source_srv;
    uint output_uav;
    uint output_offset;
    uint flags;

#ifdef __cplusplus
    inline void Init()
    {
        source_srv = 0;
        output_uav = 0;
        output_offset = 0;
        flags = 0;
    }
#endif
};

#ifdef __cplusplus
static_assert(sizeof(TextureMipGenPushConstants) == 12, "TextureMipGenPushConstants layout mismatch");
static_assert(sizeof(TextureBCCompressPushConstants) == 16, "TextureBCCompressPushConstants layout mismatch");
#endif

#ifdef WON_TEXTURE_MIPGEN_PUSHCONSTANT
PUSHCONSTANT(mipgenpush, TextureMipGenPushConstants);
#endif

#ifdef WON_TEXTURE_BC_COMPRESS_PUSHCONSTANT
PUSHCONSTANT(bccompresspush, TextureBCCompressPushConstants);
#endif

#endif // WON_SHADERINTEROP_UTILITY_H
