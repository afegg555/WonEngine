#pragma once
#include "Types.h"

namespace won::ecs
{
    struct SoftBodyComponent
    {
        enum class AttachmentMode
        {
            None = 0,
            TopRow,
            TopCorners,
        };

        enum class BendType
        {
            None = 0, // no resistance to folding, correct for cloth
            Distance, // resists folding by the distance between the two vertices across a shared edge, cheap approximation
            Dihedral, // resists folding by the actual angle between the two faces, accurate but more expensive
        };

        // long range attachment constraint : distance constraint of (free vertices - nearest attached vertex)
        enum class LRAType
        {
            None = 0, // free vertices may stretch as far as the edge constraints allow
            EuclideanDistance, // caps the straight line distance to the nearest "attached" vertex
            GeodesicDistance, // caps the distance measured along the edges, accurate on shapes that wrap around obstacles but slower to build
        };

        enum Flags
        {
            Empty = 0,
            Dirty = 1 << 0,
            Enabled = 1 << 1,
            TopologyDirty = 1 << 2,
        };

        uint32 flags = Dirty | Enabled | TopologyDirty;
        AttachmentMode attachment_mode = AttachmentMode::TopRow;

        uint32 divisions_x = 16;
        uint32 divisions_y = 16;
        float size_x = 2.0f; // in world unit
        float size_y = 2.0f; // in world unit
        float mass = 1.0f; // relative heaviness, not a mass in kg: inverse mass is normalized to (0, 1]
        float edge_compliance = 1.0e-5f; // 0 : rigid
        float shear_compliance = 1.0e-5f; // 0 : rigid
        float bend_compliance = 1.0e-2f; // 0 : rigid
        BendType bend_type = BendType::None;
        LRAType lra_type = LRAType::None;
        float lra_max_distance_multiplier = 1.0f;
        float linear_damping = 0.1f; // multiplied to linear velocity
        float friction = 0.5f;
        float restitution = 0.0f;

        // we compute substep using delta_time / solver_iterations
        // more iteration = smaller substep = more stable
        uint32 solver_iterations = 6;

        constexpr void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        constexpr bool IsDirty() const { return (flags & Dirty) != 0; }
        constexpr void SetTopologyDirty(bool value = true) { if (value) { flags |= TopologyDirty; } else { flags &= ~TopologyDirty; } }
        constexpr bool IsTopologyDirty() const { return (flags & TopologyDirty) != 0; }
        constexpr void SetEnabled(bool value = true) { if (IsEnabled() == value) { return; } if (value) { flags |= Enabled; } else { flags &= ~Enabled; } SetDirty(); }
        constexpr bool IsEnabled() const { return (flags & Enabled) != 0; }
    };
}
