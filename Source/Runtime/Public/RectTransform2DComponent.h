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
        float2 anchor = { 0.0f, 0.0f };   // relative to the parent rect, range [0..1]
        float2 position = { 0.0f, 0.0f }; // offset from the anchor, in reference pixels
        float2 size = { 100.0f, 100.0f }; // in reference pixels
        float2 pivot = { 0.5f, 0.5f };

		// These values are updated by RectTransformUpdateSystem.
        float2 resolved_position = { 0.0f, 0.0f };
        float2 resolved_size = { 100.0f, 100.0f };
        float2 reference_resolution = { 0.0f, 0.0f };

        void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        bool IsDirty() const { return (flags & Dirty) != 0; }
    };
}
