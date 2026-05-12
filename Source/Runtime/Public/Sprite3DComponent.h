#pragma once
#include "MathTypes.h"
#include "Types.h"

namespace won::ecs
{
    struct Sprite3DComponent
    {
        enum Flags : uint32
        {
            Empty = 0,
            Dirty = 1 << 0,
            Billboard = 1 << 1,
        };

        uint32 flags = Dirty;

        float2 size = { 1.0f, 1.0f };
        float2 pivot = { 0.5f, 0.5f };
        float4 uv_rect = { 0.0f, 0.0f, 1.0f, 1.0f };

        constexpr void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        constexpr bool IsDirty() const { return (flags & Dirty) != 0; }
        constexpr void SetBillboard(bool value = true) { if (value) { flags |= Billboard; } else { flags &= ~Billboard; } SetDirty(); }
        constexpr bool IsBillboard() const { return (flags & Billboard) != 0; }
    };
}
