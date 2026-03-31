#pragma once
#include "Types.h"
#define STB_RECT_PACK_IMPLEMENTATION
#define STBRP_STATIC
#include "stb/stb_rect_pack.h"

namespace won::rectpacker
{
    using Rect = stbrp_rect;

    struct State
    {
        stbrp_context context = {};
        Vector<stbrp_node> nodes;
        Vector<Rect> rects;
        int width = 0;
        int height = 0;

        void Clear()
        {
            rects.clear();
        }

        void AddRect(const Rect& rect)
        {
            rects.push_back(rect);
            width = (std::max)(width, rect.w); // starting min size
            height = (std::max)(height, rect.h); // starting min size
        }

        bool Pack(int max_width)
        {
            while (width <= max_width && height <= max_width)
            {
                if (nodes.size() < static_cast<Size>(width))
                {
                    nodes.resize(width);
                }

                stbrp_init_target(&context, width, height, nodes.data(), static_cast<int>(nodes.size()));
                if (stbrp_pack_rects(&context, rects.data(), static_cast<int>(rects.size())) != 0)
                {
                    return true;
                }

                if (height < width)
                {
                    height *= 2;
                }
                else
                {
                    width *= 2;
                }
            }

            width = 0;
            height = 0;
            return false;
        }
    };
}
