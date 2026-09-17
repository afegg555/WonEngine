#pragma once
#include "Primitives.h"
#include "Types.h"

namespace won::ecs
{
    enum class FoliageCollisionMode
    {
        None = 0,
        Trunk,
    };

    struct FoliageInstance
    {
        float3 position = {};
        float scale = 1.0f;
        float4 rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    };

    struct FoliageType
    {
        String mesh_asset_path;
        String material_asset_path;

        float density = 1.0f;
        float2 scale_range = { 0.8f, 1.2f };
        float random_yaw = 1.0f;
        float align_to_normal = 0.0f;
        float cull_distance = 80.0f;
        float2 lod_distances = { 30.0f, 60.0f };
        bool cast_shadow = false;
        float wind_influence = 0.0f;

        float slope_min_degrees = 0.0f;
        float slope_max_degrees = 40.0f;
        float height_min = -100000.0f;
        float height_max = 100000.0f;
        int32 terrain_layer = -1;

        FoliageCollisionMode collision = FoliageCollisionMode::None;
        float trunk_radius = 0.2f;
        float trunk_height = 2.0f;

        Vector<FoliageInstance> instances;
    };

    struct FoliageComponent
    {
        enum Flags
        {
            Empty = 0,
            Dirty = 1 << 0,
            Active = 1 << 1,
        };

        uint32 flags = Dirty | Active;

        Vector<FoliageType> types;
        float3 region_center = {};
        float3 region_extent = { 50.0f, 50.0f, 50.0f };
        uint32 seed = 1337;

        constexpr void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        constexpr bool IsDirty() const { return (flags & Dirty) != 0; }
        constexpr void SetActive(bool value = true) { if (IsActive() == value) { return; } if (value) { flags |= Active; } else { flags &= ~Active; } SetDirty(); }
        constexpr bool IsActive() const { return (flags & Active) != 0; }
    };
}
