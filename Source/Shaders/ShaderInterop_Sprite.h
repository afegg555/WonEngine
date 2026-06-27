#ifndef WON_SHADERINTEROP_SPRITE_H
#define WON_SHADERINTEROP_SPRITE_H

#ifndef WON_DISABLE_RENDERER_PUSHCONSTANT
#define WON_DISABLE_RENDERER_PUSHCONSTANT
#endif

#include "ShaderInterop.h"

enum SHADER_SPRITE_FLAGS
{
    SHADER_SPRITE_FLAG_NONE = 0,
    SHADER_SPRITE_FLAG_BILLBOARD = 1 << 0,
    SHADER_SPRITE_FLAG_PARTICLE = 1 << 1, // billboard center + color come from the bindless particle buffer (GetResourceIndex), indexed by instance_index
};

struct SpritePushConstants
{
    float4 size_pivot;
    float4 uv_rect;
    uint instance_index;
    uint flags;
    uint material_index;

    uint GetFlags()
    {
        return flags & 0xFFu;
    }

    uint GetResourceIndex()
    {
        return flags >> 8u;
    }

    void SetResourceIndex(uint resource_index)
    {
        flags = (flags & 0xFFu) | (resource_index << 8u);
    }

#ifdef __cplusplus
    inline void Init()
    {
        size_pivot = { 1.0f, 1.0f, 0.5f, 0.5f };
        uv_rect = { 0.0f, 0.0f, 1.0f, 1.0f };
        instance_index = 0;
        flags = SHADER_SPRITE_FLAG_NONE;
        material_index = 0;
    }
#endif
};

PUSHCONSTANT(push, SpritePushConstants);

#ifdef __cplusplus
static_assert(sizeof(SpritePushConstants) == 44, "SpritePushConstants layout mismatch");
#endif

#endif
