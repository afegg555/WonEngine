#pragma once
#include "MathTypes.h"
#include "Types.h"

namespace won::ecs
{
    struct RectTransform2DComponent
    {
        enum Flags : uint32
        {
            Empty = 0,
            Dirty = 1 << 0,
        };

        uint32 flags = Dirty;
        float2 anchor_min = { 0.0f, 0.0f };
        float2 anchor_max = { 0.0f, 0.0f };
        float2 anchored_position = { 0.0f, 0.0f };
        float2 size_delta = { 100.0f, 100.0f };
        float2 pivot = { 0.5f, 0.5f };

        float2 resolved_anchor_min = { 0.0f, 0.0f };
        float2 resolved_anchor_max = { 0.0f, 0.0f };
        float2 resolved_offset_min = { 0.0f, 0.0f };
        float2 resolved_offset_max = { 0.0f, 0.0f };
        float2 reference_resolution = { 0.0f, 0.0f };
        uint32 layer_mask = 0xFFFFFFFF;
        float match = 0.5f;

        void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        bool IsDirty() const { return (flags & Dirty) != 0; }
    };
}
