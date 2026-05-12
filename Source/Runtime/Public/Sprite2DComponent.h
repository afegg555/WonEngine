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

        float2 anchor = { 0.0f, 0.0f }; // [0..1]
        float2 position = { 0.0f, 0.0f }; // pixel unit
        float2 size = { 100.0f, 100.0f }; // pixel unit
        float2 pivot = { 0.5f, 0.5f };
        float4 uv_rect = { 0.0f, 0.0f, 1.0f, 1.0f };
        int32 layer = 0;

        constexpr void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        constexpr bool IsDirty() const { return (flags & Dirty) != 0; }
    };
}
