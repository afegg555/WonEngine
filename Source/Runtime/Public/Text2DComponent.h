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
        String font_asset_path;
        String text;
        uint32 pixel_height = 32; // pixel unit
        int32 layer = 0;

        void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        bool IsDirty() const { return (flags & Dirty) != 0; }
    };
}
