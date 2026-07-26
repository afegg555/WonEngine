#include "DebugDraw.h"

#include <utility>

#ifndef WON_SHIPPING
namespace won::debugdraw
{
    namespace
    {
        Vector<Item3D> items_3d;
        Vector<Item2D> items_2d;
    }

    void Line3D(const float3& from, const float3& to, uint32 color)
    {
        items_3d.push_back({ from, color });
        items_3d.push_back({ to, color });
    }

    void Box3D(const float3& bounds_min, const float3& bounds_max, uint32 color)
    {
        const float3 corners[8] = {
            { bounds_min.x, bounds_min.y, bounds_min.z },
            { bounds_max.x, bounds_min.y, bounds_min.z },
            { bounds_min.x, bounds_max.y, bounds_min.z },
            { bounds_max.x, bounds_max.y, bounds_min.z },
            { bounds_min.x, bounds_min.y, bounds_max.z },
            { bounds_max.x, bounds_min.y, bounds_max.z },
            { bounds_min.x, bounds_max.y, bounds_max.z },
            { bounds_max.x, bounds_max.y, bounds_max.z },
        };
        constexpr uint32 edges[12][2] = {
            { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },
            { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
            { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
        };
        for (const auto& edge : edges)
        {
            Line3D(corners[edge[0]], corners[edge[1]], color);
        }
    }

    void Cross3D(const float3& center, float size, uint32 color)
    {
        const float half_size = size * 0.5f;
        Line3D({ center.x - half_size, center.y, center.z }, { center.x + half_size, center.y, center.z }, color);
        Line3D({ center.x, center.y - half_size, center.z }, { center.x, center.y + half_size, center.z }, color);
        Line3D({ center.x, center.y, center.z - half_size }, { center.x, center.y, center.z + half_size }, color);
    }

    const Vector<Item3D>& GetItems3D()
    {
        return items_3d;
    }

    void Clear3D()
    {
        items_3d.clear();
    }

    void Text2D(float2 position, const char* text, uint32 color, float scale)
    {
        if (!text)
        {
            return;
        }
        Item2D item;
        item.position = position;
        item.scale = scale;
        item.color = color;
        item.text = text;
        items_2d.push_back(std::move(item));
    }

    void Rect2D(float2 position, float2 size, uint32 color)
    {
        Item2D item;
        item.position = position;
        item.size = size;
        item.color = color;
        item.is_rect = true;
        items_2d.push_back(std::move(item));
    }

    const Vector<Item2D>& GetItems2D()
    {
        return items_2d;
    }

    void Clear2D()
    {
        items_2d.clear();
    }
}
#endif
