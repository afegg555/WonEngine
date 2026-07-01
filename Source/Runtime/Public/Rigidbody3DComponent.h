#pragma once
#include "Types.h"

namespace won::ecs
{
    struct Rigidbody3DComponent
    {
        enum class MotionType
        {
            Static = 0,
            Kinematic,
            Dynamic
        };

        enum Flags
        {
            Empty = 0,
            Dirty = 1 << 0,
            Enabled = 1 << 1,
            LockRotation = 1 << 2, // dynamic body may only rotate around world Y (upright character)
        };

        uint32 flags = Dirty | Enabled;
        MotionType motion_type = MotionType::Dynamic;
        float mass = 1.0f;
        float friction = 0.2f;
        float restitution = 0.0f;
        float gravity_factor = 1.0f;

        float3 linear_velocity = {};
        float3 angular_velocity = {};

        constexpr void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        constexpr bool IsDirty() const { return (flags & Dirty) != 0; }
        constexpr void SetEnabled(bool value = true) { if (IsEnabled() == value) { return; } if (value) { flags |= Enabled; } else { flags &= ~Enabled; } SetDirty(); }
        constexpr bool IsEnabled() const { return (flags & Enabled) != 0; }
    };
}
