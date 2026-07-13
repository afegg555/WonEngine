#pragma once
#include "Types.h"

namespace won::debugtext
{
    // Color is packed as 0xRRGGBBAA (R in the most significant byte).
    inline constexpr uint32 PackRGBA8(uint8 r, uint8 g, uint8 b, uint8 a = 0xff)
    {
        return (static_cast<uint32>(r) << 24) | (static_cast<uint32>(g) << 16) | (static_cast<uint32>(b) << 8) | static_cast<uint32>(a);
    }

    struct Item
    {
        float x = 0.0f;
        float y = 0.0f;
        float scale = 1.0f;
        uint32 color = 0xffffffffu;
        String text;
    };

    void DrawScreenText(float x, float y, const char* text, uint32 color = 0xffffffffu, float scale = 1.0f);
    const Vector<Item>& GetItems();
    void Clear();
}
