#pragma once
#include "Types.h"

namespace won::ecs
{
	// (optional) helper component for moving entities along a path
	// you can also control it directly through behavior trees and scripts.
    struct NavAgentComponent
    {
        enum class MoveState : uint8
        {
            Idle,
            Moving,
            Arrived,
            Failed,
        };

        enum Flags
        {
            Empty = 0,
            Enabled = 1 << 0,
            RotateToMovement = 1 << 1,
        };

        uint32 flags = Enabled | RotateToMovement;

		float move_speed = 3.5f; // Units per second
		float arrival_threshold = 0.35f; // distance that is considered to have arrived
		float turn_rate = 6.0f; // radians per second

        MoveState state = MoveState::Idle;
        float3 move_target = {};
        Vector<float3> path;
		int32 path_index = 0; // index of the next point in the path to move towards
        float yaw = 0.0f;

        constexpr void SetEnabled(bool value = true) { if (value) { flags |= Enabled; } else { flags &= ~Enabled; } }
        constexpr bool IsEnabled() const { return (flags & Enabled) != 0; }
        constexpr void SetRotateToMovement(bool value = true) { if (value) { flags |= RotateToMovement; } else { flags &= ~RotateToMovement; } }
        constexpr bool RotatesToMovement() const { return (flags & RotateToMovement) != 0; }
    };
}
