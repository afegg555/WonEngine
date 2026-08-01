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
		String text; // text to display, if text_key is empty, this will be used as the text to display
		String text_key; // for text localization, if not empty, this will be used to look up the localized text
        String resolved_text;
        uint32 locale_revision = 0;
        uint32 pixel_height = 32; // pixel unit
        int32 layer = 0;

        void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        bool IsDirty() const { return (flags & Dirty) != 0; }
    };
}
