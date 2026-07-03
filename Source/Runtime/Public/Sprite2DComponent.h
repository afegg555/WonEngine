#pragma once
#include "MathTypes.h"
#include "Types.h"

namespace won::ecs
{
    struct Sprite2DComponent
    {
        enum Flags : uint32
        {
            Empty = 0,
            Dirty = 1 << 0,
        };

        uint32 flags = Dirty;
        float4 uv_rect = { 0.0f, 0.0f, 1.0f, 1.0f };
        int32 layer = 0;

        constexpr void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        constexpr bool IsDirty() const { return (flags & Dirty) != 0; }
    };
}
