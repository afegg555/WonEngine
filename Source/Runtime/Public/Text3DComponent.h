#pragma once
#include "Font.h"
#include "MathTypes.h"
#include "Types.h"

namespace won::ecs
{
    struct Text3DComponent
    {
        enum Flags : uint32
        {
            Empty = 0,
            Dirty = 1 << 0,
            Billboard = 1 << 1,
        };

        uint32 flags = Dirty;
        std::shared_ptr<resource::Font> font;
        String font_asset_path;
        String text;
        uint32 pixel_height = 32;
        float height = 1.0f;
        float2 pivot = { 0.5f, 0.5f };

        void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        bool IsDirty() const { return (flags & Dirty) != 0; }
        void SetBillboard(bool value = true) { if (value) { flags |= Billboard; } else { flags &= ~Billboard; } SetDirty(); }
        bool IsBillboard() const { return (flags & Billboard) != 0; }
    };
}
