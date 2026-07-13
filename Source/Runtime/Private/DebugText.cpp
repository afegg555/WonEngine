#include "DebugText.h"

#include <utility>

namespace won::debugtext
{
    namespace
    {
        Vector<Item> items;
    }

    void DrawScreenText(float x, float y, const char* text, uint32 color, float scale)
    {
        if (!text)
        {
            return;
        }
        Item item;
        item.x = x;
        item.y = y;
        item.scale = scale;
        item.color = color;
        item.text = text;
        items.push_back(std::move(item));
    }

    const Vector<Item>& GetItems()
    {
        return items;
    }

    void Clear()
    {
        items.clear();
    }
}
