#pragma once
#include "Types.h"
#include "Entity.h"

namespace won::ecs
{
    struct JointComponent
    {
        enum class JointType
        {
            Fixed = 0,
            Hinge,
        };

        enum Flags
        {
            Empty = 0,
            Dirty = 1 << 0,
            Enabled = 1 << 1,
        };

        uint32 flags = Dirty | Enabled;
        JointType type = JointType::Fixed;
		Entity connected_entity = INVALID_ENTITY; // entity to which this joint is connected; if invalid, joint is connected to world

		// hinge joint parameters
        float3 anchor = { 0.0f, 0.0f, 0.0f }; // world-space hinge pivot; at creation time resolved into connected/owner local frames
		float3 axis = { 0.0f, 1.0f, 0.0f }; // world-space hinge axis; at creation time resolved into connected/owner local frames
        bool use_limit = false;
		float limit_min = 0.0f; // in radians
        float limit_max = 0.0f; // in radians

        constexpr void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        constexpr bool IsDirty() const { return (flags & Dirty) != 0; }
        constexpr void SetEnabled(bool value = true) { if (IsEnabled() == value) { return; } if (value) { flags |= Enabled; } else { flags &= ~Enabled; } SetDirty(); }
        constexpr bool IsEnabled() const { return (flags & Enabled) != 0; }
    };
}
