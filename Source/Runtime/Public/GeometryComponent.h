#pragma once

#include "Mesh.h"
#include "Primitives.h"
#include "Types.h"

#include <memory>

namespace won::ecs
{
    struct GeometryComponent
    {
        // keep component lightweight: reference a shared mesh, which is owned by the resource layer
        std::shared_ptr<resource::Mesh> mesh;

        math::Aabb local_bounds = {};
        bool cast_shadow = true;
    };
}
