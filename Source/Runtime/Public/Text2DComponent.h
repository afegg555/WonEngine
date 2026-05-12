#pragma once
#include "Font.h"
#include "MathTypes.h"
#include "Types.h"

namespace won::ecs
{
    struct Text2DComponent
    {
        enum Flags : uint32
        {
            Empty = 0,
            Dirty = 1 << 0,
        };

        uint32 flags = Dirty;
        std::shared_ptr<resource::Font> font;
        String text;
        float2 anchor = { 0.0f, 0.0f }; // [0..1]
        float2 position = { 0.0f, 0.0f };  // pixel unit
        uint32 pixel_height = 32; // pixel unit
        float2 pivot = { 0.5f, 0.5f };
        int32 layer = 0;

        void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        bool IsDirty() const { return (flags & Dirty) != 0; }
    };
}
