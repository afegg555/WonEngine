#ifndef WON_SHADERINTEROP_SPRITE_H
#define WON_SHADERINTEROP_SPRITE_H

#ifndef WON_DISABLE_RENDERER_PUSHCONSTANT
#define WON_DISABLE_RENDERER_PUSHCONSTANT
#endif

#include "ShaderInterop.h"

struct SpritePushConstants
{
    uint sprite_buffer_descriptor;
    uint sprite_index;

#ifdef __cplusplus
    inline void Init()
    {
        sprite_buffer_descriptor = 0;
        sprite_index = 0;
    }
#endif
};

PUSHCONSTANT(push, SpritePushConstants);

#ifdef __cplusplus
static_assert(sizeof(SpritePushConstants) == 8, "SpritePushConstants layout mismatch");
#endif

#endif
