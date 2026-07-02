#pragma once
#include "Types.h"

namespace won::ecs
{
    struct VisibilityLayerComponent
    {
        uint32 layer_mask = 0xFFFFFFFF;
    };
}
