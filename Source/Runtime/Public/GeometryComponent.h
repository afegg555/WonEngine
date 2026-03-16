#pragma once
#include "Mesh.h"
#include "Primitives.h"
#include "Types.h"

namespace won::ecs
{
    struct GeometryComponent
    {
        // keep component lightweight: reference a shared mesh, which is owned by the resource layer
        std::shared_ptr<resource::Mesh> mesh;

        math::AABB local_bounds = {};
        bool cast_shadow = true;

        uint32 geometry_offset = 0; // internal usage
    };
}
