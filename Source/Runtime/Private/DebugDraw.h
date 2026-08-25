#pragma once
#include "Types.h"
#include "MathTypes.h"

namespace won::debugdraw
{
    // Color is packed as 0xRRGGBBAA (R in the most significant byte).
    inline constexpr uint32 PackRGBA8(uint8 r, uint8 g, uint8 b, uint8 a = 0xff)
    {
        return (static_cast<uint32>(r) << 24) | (static_cast<uint32>(g) << 16) | (static_cast<uint32>(b) << 8) | static_cast<uint32>(a);
    }

#ifndef WON_SHIPPING
    namespace color
    {
        inline constexpr uint32 bvh_cpu_internal = PackRGBA8(0x30, 0xa0, 0x30);
        inline constexpr uint32 bvh_cpu_leaf = PackRGBA8(0x60, 0xff, 0x60);
        inline constexpr uint32 bvh_gpu_internal = PackRGBA8(0x30, 0x60, 0xc0);
        inline constexpr uint32 bvh_gpu_leaf = PackRGBA8(0x60, 0xc0, 0xff);
        inline constexpr uint32 ddgi_volume = PackRGBA8(0xff, 0xa0, 0x30);
        inline constexpr uint32 ddgi_probe = PackRGBA8(0x40, 0xff, 0x80);
        inline constexpr uint32 ddgi_probe_relocated = PackRGBA8(0xff, 0xe0, 0x40);
        inline constexpr uint32 ddgi_probe_invalid = PackRGBA8(0xff, 0x40, 0x40);
        inline constexpr uint32 collider = PackRGBA8(0x40, 0xd0, 0xff);
        inline constexpr uint32 collider_trigger = PackRGBA8(0xff, 0xd0, 0x40);
        inline constexpr uint32 occluded = PackRGBA8(0xff, 0x40, 0xa0);
    }

    struct Item3D
    {
        float3 position = { 0.0f, 0.0f, 0.0f };
        uint32 color = 0xffffffffu;
    };

    struct Item2D
    {
        float2 position = { 0.0f, 0.0f };
        float2 size = { 0.0f, 0.0f };
        float scale = 1.0f;
        uint32 color = 0xffffffffu;
        String text;
        bool is_rect = false;
    };

    void Line3D(const float3& from, const float3& to, uint32 color);
    void Box3D(const float3& bounds_min, const float3& bounds_max, uint32 color);
    void Cross3D(const float3& center, float size, uint32 color);
    void Sphere3D(const float3& center, float radius, uint32 color);
    const Vector<Item3D>& GetItems3D();
    void Clear3D();

    void Text2D(float2 position, const char* text, uint32 color = 0xffffffffu, float scale = 1.0f);
    void Rect2D(float2 position, float2 size, uint32 color);
    const Vector<Item2D>& GetItems2D();
    void Clear2D();
#else
    inline void Line3D(const float3&, const float3&, uint32) {}
    inline void Box3D(const float3&, const float3&, uint32) {}
    inline void Cross3D(const float3&, float, uint32) {}
    inline void Sphere3D(const float3&, float, uint32) {}
    inline void Text2D(float2, const char*, uint32 = 0xffffffffu, float = 1.0f) {}
    inline void Rect2D(float2, float2, uint32) {}
#endif
}
