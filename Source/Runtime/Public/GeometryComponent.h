#pragma once
#include "Mesh.h"
#include "Primitives.h"
#include "Types.h"

namespace won::ecs
{
    struct GeometryComponent
    {
        enum Flags
        {
            Empty = 0,
            CastShadow = 1 << 0,
        };

        uint32 flags = Empty;

        // keep component lightweight: reference a shared mesh, which is owned by the resource layer
        std::shared_ptr<resource::Mesh> mesh;

        math::AABB local_bounds = {};

        uint32 geometry_offset = 0; // internal usage

        void UpdateLocalBounds()
        {
            local_bounds.Invalidate();
            if (!mesh)
            {
                return;
            }

            for (const resource::Submesh& submesh : mesh->submeshes)
            {
                local_bounds.Merge(submesh.local_bounds);
            }
        }

        constexpr void SetCastShadow(bool value = true) { if (value) { flags |= CastShadow; } else { flags &= ~CastShadow; } }
        constexpr bool IsCastShadow() const { return flags & CastShadow; }

    };
}
