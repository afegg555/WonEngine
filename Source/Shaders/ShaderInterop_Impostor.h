#ifndef WON_SHADERINTEROP_IMPOSTOR_H
#define WON_SHADERINTEROP_IMPOSTOR_H

#ifndef WON_DISABLE_RENDERER_PUSHCONSTANT
#define WON_DISABLE_RENDERER_PUSHCONSTANT
#endif

#include "ShaderInterop.h"

struct ImpostorPushConstants
{
    uint instance_offset;
    uint impostor_index;

#ifdef __cplusplus
    inline void Init()
    {
        instance_offset = 0;
        impostor_index = 0;
    }
#endif
};

PUSHCONSTANT(impostorpush, ImpostorPushConstants);

#ifdef __cplusplus
static_assert(sizeof(ImpostorPushConstants) == 8, "ImpostorPushConstants layout mismatch");
#endif

#endif // WON_SHADERINTEROP_IMPOSTOR_H
