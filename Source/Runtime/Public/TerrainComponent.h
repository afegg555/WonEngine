#pragma once
#include "Types.h"

namespace won::ecs
{
    // Authoring recipe for a generated terrain mesh. This component holds only the
    // parameters; the generated mesh is baked into the entity's GeometryComponent at
    // authoring time (see GenerateTerrainMesh)
    struct TerrainComponent
    {
        enum Flags
        {
            Empty = 0,
        };

        uint32 flags = Empty;

        uint32 resolution_x = 64; // grid cell count along X
        uint32 resolution_z = 64; // grid cell count along Z
        float world_size_x = 100.0f;
        float world_size_z = 100.0f;
        float height_scale = 10.0f;
        uint32 seed = 0;
    };
}
