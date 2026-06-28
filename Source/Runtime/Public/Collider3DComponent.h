#pragma once
#include "Primitives.h"
#include "Types.h"

#include <cfloat>

namespace won::ecs
{
    struct Collider3DComponent
    {
        enum Flags
        {
            Empty = 0,
            Dirty = 1 << 0,
            Enabled = 1 << 1,
			Trigger = 1 << 2, // if true, this collider will only generate trigger events and won't cause physical collision response
        };

        enum class ShapeType
        {
            Box = 0,
            Sphere,
        };

        uint32 flags = Dirty | Enabled;
        ShapeType shape_type = ShapeType::Box;
        float3 offset = {};
        float3 half_extent = { 0.5f, 0.5f, 0.5f };
        float radius = 0.5f;
        math::AABB world_bounds = { XMFLOAT3(FLT_MAX, FLT_MAX, FLT_MAX), XMFLOAT3(-FLT_MAX, -FLT_MAX, -FLT_MAX) };
        math::Sphere world_sphere = {};

        constexpr void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        constexpr bool IsDirty() const { return (flags & Dirty) != 0; }
        constexpr void SetEnabled(bool value = true) { if (IsEnabled() == value) { return; } if (value) { flags |= Enabled; } else { flags &= ~Enabled; } SetDirty(); }
        constexpr bool IsEnabled() const { return (flags & Enabled) != 0; }
        constexpr void SetTrigger(bool value = true) { if (IsTrigger() == value) { return; } if (value) { flags |= Trigger; } else { flags &= ~Trigger; } SetDirty(); }
        constexpr bool IsTrigger() const { return (flags & Trigger) != 0; }
    };
}
