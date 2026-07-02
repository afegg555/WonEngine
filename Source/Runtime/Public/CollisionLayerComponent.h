#pragma once
#include "Types.h"

namespace won::ecs
{
    struct CollisionLayerComponent
    {
        // Single collision layer index for this body: it belongs to exactly ONE layer.
        // This is NOT a bitmask - assign one value in [0, 31], do not OR bits together.
        // Whether two layers actually collide is decided by the global 32x32 collision
        // matrix (rows are uint32 masks), not by this field.
        uint32 layer = 0;
    };
}
